#include "z_display.h"
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>

LOG_MODULE_REGISTER(display_module, LOG_LEVEL_INF);

static const struct device *display_dev = DEVICE_DT_GET(DT_NODELABEL(ssd1306));  

int app_display_setup(void) {
    if (display_dev == NULL || !device_is_ready(display_dev)) {
        LOG_ERR("Failed to initialize display");
        return -ENODEV;
    }

    if (cfb_framebuffer_init(display_dev) != 0) {
        LOG_ERR("Failed to initialize framebuffer");
        return -EIO;
    }

    /* Encender y limpiar pantalla */
    display_blanking_off(display_dev);
    display_clear(display_dev);

    /* Limpiar RAM y elegir fuente */
    cfb_framebuffer_clear(display_dev, true);
    if (cfb_framebuffer_set_font(display_dev, 0) != 0) {
        LOG_ERR("Font not found. Check prj.conf");
        return -EINVAL;
    }

    LOG_INF("Display initialized successfully");

    return 0;
}

int app_display_clear(void) {
    cfb_framebuffer_clear(display_dev, false);

    return 0;
}

int app_display_flush(void) {
    return cfb_framebuffer_finalize(display_dev);
}

int app_display_line(const char* text, int x, int y) {
    return cfb_print(display_dev, text, x, y);
}