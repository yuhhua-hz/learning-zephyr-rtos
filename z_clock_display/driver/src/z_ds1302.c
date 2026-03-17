#include "z_ds1302.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(rtc_module, LOG_LEVEL_INF);

/* Mutex para acceso al RTC */
K_MUTEX_DEFINE(ds1302_mutex);

static const char* weekday_label[] = {"", "LUN", "MAR", "MIE", "JUE", "VIE", "SAB", "DOM"};

// Sakamoto algorithm
static int weekday(int d, int m, int y) {
    y -= m < 3;
    static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    
    int _d = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;

    /* 0: Domingo 1: Lunes ... 6: Sabado */
    if (_d == 0) return 7;
    return _d;
}

static const struct gpio_dt_spec ce_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(ds1302_0), ce_gpios);
static const struct gpio_dt_spec data_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(ds1302_0), io_gpios);
static const struct gpio_dt_spec sclk_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(ds1302_0), sclk_gpios);

/* ────────────── Funciones privadas ────────────── */

static inline uint8_t bcd2dec(uint8_t bcd) {
    return (10 * ((bcd & 0xF0) >> 4) + (bcd & 0x0F));
}

static inline uint8_t dec2bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

static void ds1302_data_set_output(void) {
    gpio_pin_configure_dt(&data_pin, GPIO_OUTPUT);
}

static void ds1302_data_set_input(void) {
    gpio_pin_configure_dt(&data_pin, GPIO_INPUT);
}

static void ds1302_writeByte(uint8_t data) {
    ds1302_data_set_output();

    /* Procesar bit a bit, activar reloj, estabilizar y procesar el siguiente bit */ 
    for (int i = 0; i < 8; ++i) {
        gpio_pin_set_dt(&data_pin, data & 0x01);

        k_busy_wait(1);
        gpio_pin_set_dt(&sclk_pin, 1);
        k_busy_wait(1);
        gpio_pin_set_dt(&sclk_pin, 0);
        data = data >> 1;
    }

}

static uint8_t ds1302_readByte(void) {
    uint8_t data = 0;
    
    ds1302_data_set_input();

    /* Si el bit leido es 1 colocar en data */
    for (int i = 0; i < 8; ++i) {
        if (gpio_pin_get_dt(&data_pin) > 0)
            data = data | (1 << i);

        gpio_pin_set_dt(&sclk_pin, 1);
        k_busy_wait(1);
        gpio_pin_set_dt(&sclk_pin, 0);
        k_busy_wait(1);
    }

    return data;
}

static void ds1302_writeReg(uint8_t reg, uint8_t data) {
    gpio_pin_set_dt(&ce_pin, 1);
    
    k_busy_wait(4);

    /* Escribir el comando y luego el dato */
    ds1302_writeByte(reg);
    ds1302_writeByte(data);

    gpio_pin_set_dt(&ce_pin, 0);
    k_busy_wait(1);
}

static uint8_t ds1302_readReg(uint8_t reg) {
    uint8_t data = 0;

    gpio_pin_set_dt(&ce_pin, 1);
    k_busy_wait(4);

    /* Escribir el comando y leer el dato */
    ds1302_writeByte(reg);
    data = ds1302_readByte();

    gpio_pin_set_dt(&ce_pin, 0);
    k_busy_wait(1);

    return data;
}

/* ────────────── API publica ────────────── */

int ds1302_init(void) {

    if (!gpio_is_ready_dt(&ce_pin) || 
        !gpio_is_ready_dt(&sclk_pin) || 
        !gpio_is_ready_dt(&data_pin)) {
        LOG_ERR("Failed to initialize DS1302");
        return -ENODEV;
    }

    gpio_pin_configure_dt(&ce_pin, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&sclk_pin, GPIO_OUTPUT_INACTIVE);
    LOG_INF("DS1302 initialized successfully");

    return 0;
}

void ds1302_setTime(ds1302_time_t *time) {
    k_mutex_lock(&ds1302_mutex, K_FOREVER);

    /* Deshabilitar write protection */
    ds1302_writeReg(DS1302_REG_WP, 0x00);

    ds1302_writeReg(DS1302_REG_YEAR,  dec2bcd(time->year));
    ds1302_writeReg(DS1302_REG_DAY,   dec2bcd(time->day));
    ds1302_writeReg(DS1302_REG_MONTH, dec2bcd(time->month));
    ds1302_writeReg(DS1302_REG_DATE,  dec2bcd(time->date));
    ds1302_writeReg(DS1302_REG_HOUR,  dec2bcd(time->hours));
    ds1302_writeReg(DS1302_REG_MIN,   dec2bcd(time->minutes));

    /* Apagar el bit 7 Clock Halt para que empiece a contar*/
    uint8_t sec = dec2bcd(time->seconds) & ~0x80;
    ds1302_writeReg(DS1302_REG_SEC, sec);

    /* Habilitar write protection */
    ds1302_writeReg(DS1302_REG_WP, 0x80);

    k_mutex_unlock(&ds1302_mutex);
}

void ds1302_getTime(ds1302_time_t *time) {
    k_mutex_lock(&ds1302_mutex, K_FOREVER);


    uint8_t sec = ds1302_readReg(DS1302_REG_SEC_RD) & ~0x80;
    uint8_t min = ds1302_readReg(DS1302_REG_MIN_RD);

    /* Modo 24h apagar Bit 7 y 6 a 0*/
    uint8_t hr  = ds1302_readReg(DS1302_REG_HOUR_RD) & ~0xC0;
    uint8_t dt  = ds1302_readReg(DS1302_REG_DATE_RD);
    uint8_t mon = ds1302_readReg(DS1302_REG_MONTH_RD);
    uint8_t day = ds1302_readReg(DS1302_REG_DAY_RD);
    uint8_t yr  = ds1302_readReg(DS1302_REG_YEAR_RD);

    time->seconds = bcd2dec(sec);
    time->minutes = bcd2dec(min);
    time->hours   = bcd2dec(hr);
    time->date    = bcd2dec(dt);
    time->month   = bcd2dec(mon);
    time->day     = bcd2dec(day);
    time->year    = bcd2dec(yr);

    k_mutex_unlock(&ds1302_mutex);

}

int ds1302_getTime_str(char *buf, size_t size) {
    ds1302_time_t t;
    ds1302_getTime(&t);

    /* Efecto parpadeo */
    char sep = (t.seconds % 2 == 0) ? ':' : ' ';

    return snprintf(buf, size, "%02d%c%02d%c%02d",
    t.hours, sep, t.minutes, sep, t.seconds);
}

int ds1302_getDate_str(char *buf, size_t size) {
    ds1302_time_t t;
    ds1302_getTime(&t);

    int _weekday = weekday(t.date, t.month, 2000 + t.year);
    if (_weekday < 1 || _weekday > 7) _weekday = 1;

    return snprintf(buf, size, "%s %02d/%02d/%02d",
    weekday_label[_weekday], t.date, t.month, t.year);
}
