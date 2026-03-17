#ifndef Z_SERIAL_CONSOLE_H
#define Z_SERIAL_CONSOLE_H

#include <zephyr/kernel.h>

#define CMD_MAX_LEN 32
#define UART_CMD_STACK_SIZE 1024
#define UART_CMD_PRIORITY   5

extern struct k_msgq uart_cmd_q;

/** @brief Inicializa el envio de comandos por uart */
int uart_cmd_init(void);

/** @brief Echo de los comandos recibidos */
void uart_cmd_send(const char *msg);

#endif /* Z_SERIAL_CONSOLE_H */