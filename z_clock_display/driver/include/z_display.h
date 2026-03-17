#ifndef Z_DISPLAY_H
#define Z_DISPLAY_H

/** @brief Inicializa display y character framebuffer */
int app_display_setup(void);

/** @brief Limpia la RAM cfb */
int app_display_clear(void);

/** @brief Envia los datos de la RAM a la pantalla */
int app_display_flush(void);

/** @brief Escribir texto en las coordenadas dadas */
int app_display_line(const char* text, int x, int y);

#endif /* Z_DISPLAY_H */