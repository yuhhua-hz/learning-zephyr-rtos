#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_ip.h>

#include <errno.h>
#include <string.h>
#include <stdint.h>

LOG_MODULE_REGISTER(modbus_client, LOG_LEVEL_INF);

#define MODBUS_SERVER_IP	"192.168.2.230"
#define MODBUS_PORT			502

#define MBAP_HEADER_LEN		7
#define MODBUS_MAX_ADU		260
#define MODBUS_MAX_REGS		124

#define FLOAT_REGION_START	0x0006
#define FLOAT_REGION_END	0x01D9

#define FUNCTION_CODE		0x03
#define SLAVE_ID			1
#define START_ADDR			0x0006
#define NUM_REGS			18

static struct net_mgmt_event_callback mgmt_cb;
K_SEM_DEFINE(ip_acquired_sem, 0 ,1);

static void dhcp_event_handler(struct net_mgmt_event_callback *cb,
                   uint64_t mgmt_event,
                   struct net_if *iface)
{
    ARG_UNUSED(cb);

    if (mgmt_event != NET_EVENT_IPV4_DHCP_BOUND || iface == NULL) {
        return;
    }

    char buf[NET_IPV4_ADDR_LEN];
    const struct net_if_dhcpv4 *dhcpv4 = &iface->config.dhcpv4;
	/* Convertir la IP en formato texto */
    if (net_addr_ntop(AF_INET, &dhcpv4->requested_ip, buf, sizeof(buf)) != NULL) {
        LOG_INF("IPv4 address obtained via DHCP: %s", buf);
    } else {
        LOG_WRN("DHCP bound, but failed to format IP string");
    }

    k_sem_give(&ip_acquired_sem);
}

static int wait_for_dhcp(void) {
	/* Obtener la interfaz de red */
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		LOG_ERR("No default network interface found");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&mgmt_cb, dhcp_event_handler, NET_EVENT_IPV4_DHCP_BOUND);
	net_mgmt_add_event_callback(&mgmt_cb);

	LOG_INF("Waiting for IPv4 address via DHCP...");
	net_dhcpv4_start(iface);

	if (k_sem_take(&ip_acquired_sem, K_SECONDS(30)) != 0) {
		LOG_ERR("DHCP lease timeout");
		return -ETIMEDOUT;
	}

	return 0;
}

static int sendall(int sock, const uint8_t *buf, size_t len) {
	size_t total_sent = 0;
	int bytes_sent;

	/*  Seguir enviando hasta vaciar el buffer */
	while (total_sent < len) {
		bytes_sent = zsock_send(sock, buf + total_sent, len - total_sent, 0);
		
		if (bytes_sent < 0) {
			return -errno;
		}

		if (bytes_sent == 0) {
			return -ENOTCONN;
		}

		total_sent = total_sent + bytes_sent;

	}

	return 0;
}

static int recvall(int sock, uint8_t *buf, size_t len) {
	size_t total_received = 0;
	int bytes_received;

	/* Seguir leyendo hasta llenar el buffer */
	while (total_received < len) {
		bytes_received = zsock_recv(sock, buf + total_received, len - total_received, 0);

		if (bytes_received < 0) {
			return -errno;
		}

		if (bytes_received == 0) {
			return -ENOTCONN;
		}

		total_received = total_received + bytes_received;
	}
	
	return 0;
}

static int connect_to_server(void) {
	int sock;
	struct sockaddr_in dst;

	sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		return -errno;
	}

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_port = htons(MODBUS_PORT);

	if (zsock_inet_pton(AF_INET, MODBUS_SERVER_IP, &dst.sin_addr) <= 0) {
		zsock_close(sock);
		return -EINVAL;
	}

	if (zsock_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
		LOG_ERR("Failed to connect %s:%d (errno: %d)", MODBUS_SERVER_IP, MODBUS_PORT, errno);
		zsock_close(sock);
		return -errno;
	}

	LOG_INF("Connected to %s:%d", MODBUS_SERVER_IP, MODBUS_PORT);
	return sock;
}

/** @brief Request Modbus TCP (12 bytes):
 * [0-1] Transaction ID
 * [2-3] Protocol ID (0 = Modbus)
 * [4-5] Length
 * [6]   Slave ID
 * [7]   Function Code
 * [8-9] Address
 * [10-11] Quantity
 */
static int modbus_build_request(uint8_t *buf, uint8_t fc, uint16_t addr, uint16_t quantity) {
	/* Slave ID(1) + FC(1) + Addr(2) + Quantity(2) */
	uint16_t len = 6; 
	static uint16_t transaction_id;

	if (quantity == 0 || quantity > MODBUS_MAX_REGS) {
		return -EINVAL;
	}

	transaction_id++;

	buf[0] = (transaction_id >> 8) & 0xFF;
	buf[1] = transaction_id & 0xFF;
	buf[2] = 0x00;	/* El protocolo es 00 (msw-lsw) */
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = len;
	buf[6] = SLAVE_ID;
	buf[7] = fc;
	buf[8] = (addr >> 8) & 0xFF;
	buf[9] = addr & 0xFF;
	buf[10] = (quantity >> 8) & 0xFF;
	buf[11] = quantity & 0xFF;

	return 12;
}

/** @brief Response Modbus TCP:
 * [0-1] Transaction ID
 * [2-3] Protocol ID (0 = Modbus)
 * [4-5] Length
 * [6]   Slave ID
 * [7]   Function Code
 * [8]   Byte Count (n = num_regs * 2)
 * [9-n] Register Data
 */
static int modbus_recv_response(int sock, uint8_t *buf, size_t buf_size) {

	int ret;
	uint16_t len_field;
	size_t pdu_len;

	/* Buffer minimo para recibir la cabecera MBAP */
	if (buf_size < MBAP_HEADER_LEN) {
		return -EINVAL;
	}

	/* Recibir el header primero */
	ret = recvall(sock, buf, MBAP_HEADER_LEN);
	if (ret < 0) {
		return ret;
	}

	/* Extraer el campo len del mensaje */
	len_field = ((uint16_t)buf[4] << 8) | buf[5];
	/* Comprobar que venga al menos SlaveId + FC */
	if (len_field < 2) {
		return -EIO;
	}

	/* Recibir PDU (FC + Data), se resta 1 porque ya se recibio Slave ID */
	pdu_len = len_field - 1;

	if ((MBAP_HEADER_LEN + pdu_len) > buf_size) {
		return -EMSGSIZE;
	}

	/* Recibir el resto del mensaje */
	ret = recvall(sock, &buf[MBAP_HEADER_LEN], pdu_len);
	if (ret < 0) {
		return ret;
	}

	return MBAP_HEADER_LEN + (int)pdu_len;
}

static float ui16_to_f32(uint16_t reg1, uint16_t reg2) {
	uint32_t ui32 = ((uint32_t)reg1 << 16) | (uint32_t)reg2;
	float value;

	memcpy(&value, &ui32, sizeof(value));
	return value;
}

static int modbus_read_float_registers(int sock, uint8_t fc,
			uint16_t start_addr, uint16_t num_regs) 
{
	uint8_t req[12];
	uint8_t resp[MODBUS_MAX_ADU];
	uint16_t req_tid;
	uint16_t resp_tid;
	uint16_t protocol_id;
	uint16_t len;
	uint8_t slave_id;
	uint8_t resp_fc;
	uint8_t byte_count;
	int ret;

	if (fc != 0x03 && fc != 0x04) {
		LOG_ERR("Unsupported Function Code: 0x%02X. Allowed: 0x03, 0x04", fc);
		return -EINVAL;
	}
	
	if (num_regs == 0 || num_regs > MODBUS_MAX_REGS) {
		return -EINVAL;
	}

	uint16_t last_addr = start_addr + num_regs -1;

	if (start_addr < FLOAT_REGION_START || last_addr > FLOAT_REGION_END) {
		LOG_WRN("Address out of range. Allowed: 0x%04X a 0x%04X", FLOAT_REGION_START, FLOAT_REGION_END);
		return -EINVAL;
	}

	/* Construir la peticion */
	ret = modbus_build_request(req, fc, start_addr, num_regs);
	if (ret < 0) return ret;

	LOG_HEXDUMP_INF(req, (size_t)ret, "Poll =>");

	/* Guardar transaccion para verificacion posterior */
	req_tid = ((uint16_t)req[0] << 8) | req[1];

	/* Enviar petcion */
	ret = sendall(sock, req, sizeof(req));
	if (ret < 0) return ret;

	/* Recibir la respuesta */
	ret = modbus_recv_response(sock, resp, sizeof(resp));
	if (ret < 0) return ret;

	LOG_HEXDUMP_INF(resp, (size_t)ret, "Response <=");

	/* Desempaquetar cabecera */
	resp_tid = ((uint16_t)resp[0] << 8) | resp[1];
	protocol_id = ((uint16_t)resp[2] << 8) | resp[3];
	len = ((uint16_t)resp[4] << 8) | resp[5];
	slave_id = resp[6];

	/* Verificacion de la cabecera */
	if (resp_tid != req_tid) {
		LOG_ERR("Transaction ID mismatch");
		return -EIO;
	}

	if (protocol_id != 0) {
		LOG_ERR("Invalid Protocol ID");
		return -EIO;
	}

	if (len < 3) {
		LOG_ERR("Invalid length");
		return -EIO;
	}

	if (slave_id != SLAVE_ID) {
		LOG_ERR("Invalid Slave ID");
		return -EIO;
	}

	resp_fc = resp[7];
	/* Excepcion = (FC + 0x80) + Exception code */
	if (resp_fc == (fc | 0x80)) {
		LOG_ERR("Exception: FC=0x%02X code=0x%02X", fc, resp[8]);
		return -EIO;
	}

	if (resp_fc != fc) {
		LOG_ERR("Invalid Function Code: 0x%02X", resp_fc);
		return -EIO;
	}

	byte_count = resp[8];
	if (byte_count != (num_regs * 2)) {
		LOG_ERR("Invalid Byte Count: %u Expected: %u", byte_count, num_regs * 2);
		return -EIO;
	}

	int measures;
	measures = byte_count / 4;

	for (int i = 0; i < measures; i++) {
		int base_idx = 9 + (i * 4);
		uint16_t msw_reg = ((uint16_t)resp[base_idx] << 8) | resp[base_idx + 1];
		uint16_t lsw_reg =  ((uint16_t)resp[base_idx + 2] << 8) | resp[base_idx + 3];

		float value = ui16_to_f32(msw_reg, lsw_reg);
		LOG_INF("Measurement %d -> %.2f", i+1, (double)value);
	}

	return 0;
}

int main(void)
{

	int ret;

	LOG_INF("Starting Modbus TCP client...");

	ret = wait_for_dhcp();
	if (ret < 0) {
		LOG_ERR("DHCP failed: %d", ret);
		return 1;
	}

	int sock = connect_to_server();
	if (sock < 0) return 2;

	ret = modbus_read_float_registers(sock, FUNCTION_CODE, START_ADDR, NUM_REGS);
	if (ret < 0) {
		LOG_ERR("FC%02X read error: %d", FUNCTION_CODE, ret);
		zsock_close(sock);
		return 3;
	}

	zsock_close(sock);

	return 0;
}
