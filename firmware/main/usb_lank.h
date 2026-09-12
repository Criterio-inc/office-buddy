#pragma once
#include <stdint.h>

/* Startar läsningen av rader från datorn över USB. Anropas en gång. */
void usb_lank_starta(void);

/* Millisekunder sedan datorn senast skrev en rad, över USB eller wifi. */
int32_t usb_lank_tyst_ms(void);

/* Nätverkslänken säger till här när en rad kommit den vägen. */
void usb_lank_markera_rad(void);
