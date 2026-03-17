#ifndef Z_DS1302_H
#define Z_DS1302_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <stdint.h>

// --- Registros de escritura registros pares ---
#define DS1302_REG_SEC      0x80
#define DS1302_REG_MIN      0x82
#define DS1302_REG_HOUR     0x84
#define DS1302_REG_DATE     0x86
#define DS1302_REG_MONTH    0x88
#define DS1302_REG_DAY      0x8A
#define DS1302_REG_YEAR     0x8C
#define DS1302_REG_WP       0x8E ///< Write Protect Register
#define DS1302_REG_TCR      0x90 ///< Trickle Charger Register

// --- Registros de lectura registros impares ---
#define DS1302_REG_SEC_RD   0x81
#define DS1302_REG_MIN_RD   0x83
#define DS1302_REG_HOUR_RD  0x85
#define DS1302_REG_DATE_RD  0x87
#define DS1302_REG_MONTH_RD 0x89
#define DS1302_REG_DAY_RD   0x8B
#define DS1302_REG_YEAR_RD  0x8D

typedef struct {
    uint8_t seconds; ///< 0-59
    uint8_t minutes; ///< 0-59
    uint8_t hours;   ///< 0-23
    uint8_t day;     ///< 1-7
    uint8_t date;    ///< 1-31
    uint8_t month;   ///< 1-12
    uint8_t year;    ///< 00-99
} ds1302_time_t;

/** @brief Inicializa GPIOs y quita la protección de escritura.
 *  @return 0 en éxito, código de error negativo si un GPIO no está listo. */
int ds1302_init(void);

/** @brief Establece la hora y arranca el reloj. */
void ds1302_setTime(ds1302_time_t *time);

/** @brief Lee los registros y obtiene la hora. */
void ds1302_getTime(ds1302_time_t *time);

/** @brief Devuelve en el buffer actual la hora formateada en HH::MM::SS */
/** @return Longitud de cadena escrita en el buffer */
int ds1302_getTime_str(char *buf, size_t size);

/** @brief Devuelve la fecha en formato WEKDAY DD/MM/YY */
/** @return Longitud de cadena escrita en el buffer */
int ds1302_getDate_str(char *buf, size_t size);

#endif /* Z_DS1302_H */