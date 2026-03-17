#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "lightmodbus/lightmodbus.h"

#define MODBUS_SERVER_IP	"192.168.2.230"
#define MODBUS_PORT			502
#define SLAVE_ID			1		

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Buffer for static memory allocation */
typedef struct {
	uint8_t buffer[256];
	uint16_t max_size;
	const char* name;
} modbusMemoryPool;

/* Memory pool for master */
static modbusMemoryPool master_pool = {
	.buffer = {0},
	.max_size = 256,
	.name = "MasterPool"
};

/** @brief Converts two 16-bit Modbus registers into one IEEE-754 float32 value */
static float ui16_to_f32(uint16_t reg1, uint16_t reg2) {
	uint32_t ui32 = ((uint32_t)reg1 << 16) | (uint32_t)reg2;
	float value;

	memcpy(&value, &ui32, sizeof(value));
	return value;
}

/** @brief Allocates request buffer memory from a fixed static pool */
static ModbusError static_allocator(ModbusBuffer *buffer, uint16_t size, void *context) {
	modbusMemoryPool *mem_pool = (modbusMemoryPool*)context;

	if (mem_pool == NULL) return MODBUS_ERROR_ALLOC;

	if (size == 0 ) {

		/* Free request */
		buffer->data = NULL;
		return MODBUS_OK;
	}
	else if (size > mem_pool->max_size) {
		/* More memory than allowed was requested */
		buffer->data = NULL;
		return MODBUS_ERROR_ALLOC;
	}
	else {
		/* Assign memory */
		buffer->data = mem_pool->buffer;
		return MODBUS_OK;
	}
} 

/** @brief Handles Modbus data */
static ModbusError data_cb(const ModbusMaster *master, const ModbusDataCallbackArgs *args) {
	ARG_UNUSED(master);

	if (args->type == MODBUS_HOLDING_REGISTER || args->type == MODBUS_INPUT_REGISTER) {
		
		/* Currently supports only float values */
		if (args->index >= 0x0006 && args->index <= 0x01D9) {
			static uint16_t last_index = 0;
			static uint16_t msw_reg;
			static bool is_first_half = true;
		
			/* Gap detection: reset pairing state when register sequence is broken */
			if (args->index != last_index +1) {
				is_first_half = true;
			}

			if (is_first_half) {
				msw_reg = args->value;
				is_first_half = false;
			} 
			else 
			{
				float f = ui16_to_f32(msw_reg, args->value);
				LOG_INF("[Slave %d] Float [%04X-%04X] = %f", args->address, args->index - 1, args->index, (double)f);
				is_first_half = true;
			}

			/* Update index */
			last_index = args->index;
		} 
		else {
			LOG_WRN("Modbus region not supported yet");
		}
	}
	return MODBUS_OK;
}

/** @brief Handles Modbus exception */
static ModbusError exception_cb(const ModbusMaster *master,
                                       uint8_t address,
                                       uint8_t function,
                                       ModbusExceptionCode code)
{
	LOG_ERR("Slave %d, FC%d -> exception: %d", address, function, code);
	return MODBUS_OK;
}

/** @brief Opens a TCP socket and connects to the target Modbus server */
static int tcp_connect(const char *ip, int port) {
    int sock;
    struct sockaddr_in dst; 

	/* Create an IPv4 TCP socket */
    sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        return -errno;
    }

	/* Configure destination address and port */
	memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    
	/* Convert IP address to binary */
    if (zsock_inet_pton(AF_INET, ip, &dst.sin_addr) <= 0) {
        zsock_close(sock);
        return -EINVAL;
    }

	/* Start TCP connection to destination */
    if (zsock_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
		LOG_ERR("Failed to connect to %s:%d (errno: %d)", ip, port, errno);
		zsock_close(sock);
		return -errno;
	}
    
    return sock;
}

/** @brief Sends a Modbus request and receives the raw response frame */
static int modbus_exchange(int sock,
                           const uint8_t *req, uint16_t req_len,
                           uint8_t *resp_buf, uint16_t resp_size)
{
	/* Send request to remote endpoint */
	if (zsock_send(sock, req, req_len, 0) < 0) return -1;

	/* Configure timeout */
	struct zsock_timeval tv = {.tv_sec = 1, .tv_usec = 0};
	zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	/* Read response payload */
	return zsock_recv(sock, resp_buf, resp_size, 0);

}

/** @brief Builds, sends, and parses one Modbus register read request (FC03/FC04) */
static void read_registers(ModbusMaster *master, int sock, uint8_t function_code,
                            uint8_t slave, uint16_t offset, uint16_t count)
{
	static uint8_t resp_buf[256];
	ModbusErrorInfo err;
	static uint16_t transaction_id = 1;

	switch (function_code) {
		case 3:
		err = modbusBuildRequest03TCP(master, transaction_id++, slave, offset, count);
		break;
		case 4:
		err = modbusBuildRequest04TCP(master, transaction_id++,slave, offset, count);
		break;
		default:
		LOG_WRN("Function Code: %d not supported in this version", function_code);
		return;
	}

    if (!modbusIsOk(err)) {
		LOG_ERR("Failed to build request FC%02d: %d", function_code, modbusGetErrorCode(err));
        return;
    }

	/* Request in hex */
	LOG_HEXDUMP_INF(modbusMasterGetRequest(master), modbusMasterGetRequestLength(master), "=> Poll");

    int resp_len = modbus_exchange(sock,
                                   modbusMasterGetRequest(master),
                                   modbusMasterGetRequestLength(master),
                                   resp_buf, sizeof(resp_buf));

    if (resp_len <= 0) {
		LOG_WRN("No response from slave %d", slave);
        return;
    }

	/* Response in hex */
	LOG_HEXDUMP_INF(resp_buf, resp_len, "<= Response");

    err = modbusParseResponseTCP(master,
                                 modbusMasterGetRequest(master),
                                 modbusMasterGetRequestLength(master),
                                 resp_buf,
                                 (uint16_t)resp_len);
    if (!modbusIsOk(err)) {
		LOG_ERR("Error parsing response: %d", modbusGetErrorCode(err));
    }

}

/** @brief Initializes networking and performs one Modbus polling */
int main(void)
{
	LOG_INF("Waiting for IP assignment...");
	struct net_if *iface = net_if_get_default();
	if (iface == NULL) {
		LOG_ERR("No default network interface found");
		return -1;
	}

	net_dhcpv4_start(iface);

	k_sleep(K_SECONDS(5));

	ModbusMaster master;
	ModbusErrorInfo err = modbusMasterInit(
		&master,
		data_cb,
		exception_cb,
		static_allocator,
		modbusMasterDefaultFunctions,
    	modbusMasterDefaultFunctionCount
	);

	/* Link static memory pool to the master user context */
	modbusMasterSetUserPointer(&master, &master_pool);
	
	if (!modbusIsOk(err)) {
		LOG_ERR("Failed to initialize Modbus master");
		return -1;
	}

	LOG_INF("Connecting to %s:%d...", MODBUS_SERVER_IP, MODBUS_PORT);
	int sock = tcp_connect(MODBUS_SERVER_IP, MODBUS_PORT);
	if (sock < 0) {
		LOG_ERR("TCP connection failed");
		modbusMasterDestroy(&master);
		return -1;
	}

	LOG_INF("Connected");

	read_registers(&master, sock, 4, SLAVE_ID, 0x0006, 18);

	zsock_close(sock);
	modbusMasterDestroy(&master);
	return 0;
}