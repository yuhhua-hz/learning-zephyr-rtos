#include "z_serial_console.h"
#include "z_ds1302.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

LOG_MODULE_REGISTER(uart_cmd_module, LOG_LEVEL_INF);

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));

/* Definir cola 4 comandos maximos de 32 longitud */
K_MSGQ_DEFINE(uart_cmd_q, CMD_MAX_LEN, 4, 4);

/** @brief IRQ que construye el comando y lo encola */
static void uart_isr(const struct device *dev, void *user_data) {
    ARG_UNUSED(user_data);

    static char rx_buf[CMD_MAX_LEN];
    static int rx_idx = 0;

    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        uint8_t c;
        if (uart_fifo_read(dev, &c, 1) != 1) {
            break;
        }
        
        if (c == '\n' || c == '\r') {
            if (rx_idx > 0) {
                rx_buf[rx_idx] = '\0';
                
                /* Enviar comando a la cola */
                if (k_msgq_put(&uart_cmd_q, rx_buf, K_NO_WAIT) != 0) {
                    LOG_WRN("Queue full, command dropped: %s", rx_buf);
                }
                rx_idx = 0;

            }
        } else if (rx_idx < CMD_MAX_LEN - 1) {
            /* Construir el comando */
            rx_buf[rx_idx++] = (char)c;
        } else {
            LOG_WRN("Overflow, resetting");
            rx_idx = 0;
        }
    }

}

/** @brief Procesar comandos, si el comando es correcto se hace eco respondiendo */
static void process_cmd(const char *cmd) {

    /* Comparar que el comando sea exacto y 9 letras 
     * en ese caso procesar la hora o fecha y actualizar el RTC
     * El comando status unicamente devuelve la hora y fecha
     */
    if (strncmp(cmd, "SET_TIME ", 9) == 0) {
        int hh, mm, ss;

        if (sscanf(cmd + 9, "%d:%d:%d", &hh, &mm, &ss) == 3) {
            ds1302_time_t t;
            ds1302_getTime(&t);
            t.hours = (uint8_t)hh;
            t.minutes = (uint8_t)mm;
            t.seconds = (uint8_t)ss;
            ds1302_setTime(&t);
            uart_cmd_send("Time updated\r\n");
        } else {
            uart_cmd_send("ERR: SET_TIME HH:MM:SS\r\n");
        }
    }
    else if (strncmp(cmd, "SET_DATE ", 9) == 0) {
        int dd, mm, yy;
        
        if (sscanf(cmd + 9, "%d/%d/%d", &dd, &mm, &yy) == 3) {
            ds1302_time_t t;
            ds1302_getTime(&t);
            t.date = (uint8_t)dd;
            t.month = (uint8_t)mm;
            t.year = (uint8_t)yy;
            ds1302_setTime(&t);
            uart_cmd_send("Date updated\r\n");
        } else {
            uart_cmd_send("ERR: SET_DATE DD/MM/YY\r\n");
        }
    }
    else if (strcmp(cmd, "STATUS") == 0) {
        char date_buf[16], time_buf[9], resp[40];
        ds1302_getDate_str(date_buf, sizeof(date_buf));
        ds1302_getTime_str(time_buf, sizeof(time_buf));
        snprintf(resp, sizeof(resp), "%s %s\r\n", date_buf, time_buf);
        uart_cmd_send(resp);
    }
    else {
        uart_cmd_send("ERR unknown command\r\n");
    }

}

/** @brief Hilo de tarea comandos por uart y procesarlos */
static void uart_cmd_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    char cmd[CMD_MAX_LEN];
    for (;;) {
        k_msgq_get(&uart_cmd_q, cmd, K_FOREVER);
        process_cmd(cmd);
    }
}

/* Definir hilo de tarea */
K_THREAD_DEFINE(uart_cmd_id,
                UART_CMD_STACK_SIZE,
                uart_cmd_thread,
                NULL, NULL, NULL,
                UART_CMD_PRIORITY, 0, 0);


/* ────────────── API publica  ────────────── */

int uart_cmd_init(void) {
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("Failed to initialize UART");
        return -ENODEV;
    }

    uart_irq_callback_set(uart_dev, uart_isr);
    uart_irq_rx_enable(uart_dev);

    LOG_INF("UART initialized successfully");
    return 0;
}

void uart_cmd_send(const char *msg) {
    while (*msg) {
        uart_poll_out(uart_dev, (unsigned char)*msg++);
    }
}

