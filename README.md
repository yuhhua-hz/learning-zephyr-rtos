# learning-zephyr

Colección de proyectos de aprendizaje con **Zephyr RTOS**, desarrollados para la placa **ST Nucleo-H743ZI** (STM32H743).

Cada subdirectorio es una aplicación Zephyr independiente

## Proyectos

| Proyecto | Descripción |
|---|---|
| [`z_clock_display`](z_clock_display/) | **Reloj digital**: driver propio para RTC **DS1302** (bit-bang por GPIO con binding de devicetree incluido), display OLED **SSD1306** por I2C (character framebuffer) y consola serie UART. Muestra fecha y hora en pantalla. |
| [`z_lightmodbus`](z_lightmodbus/) | **Maestro Modbus TCP** basado en la librería open-source [LightModbus](https://github.com/Jacajack/liblightmodbus) (incluida en `lib/`). Obtiene IP por DHCP, se conecta al servidor y lee registros float combinando pares de registros IEEE-754. |
| [`z_tcp_client_modbus`](z_tcp_client_modbus/) | **Cliente Modbus TCP** implementado a mano, sin librerías: construye la trama MBAP byte a byte, espera la IP por DHCP usando eventos `net_mgmt`, lee registros float, valida la respuesta (transaction ID, protocol ID, slave ID, excepciones) y decodifica los flotantes. |

## Requisitos

- Zephyr RTOS (v3.x) con `west` instalado
- Placa: `nucleo_h743zi`

## Compilar

```bash
west build -b nucleo_h743zi z_clock_display
west build -b nucleo_h743zi z_lightmodbus
west build -b nucleo_h743zi z_tcp_client_modbus
```
