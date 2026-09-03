#pragma once
#include <stdint.h>

/* Startar läsningen av rader från datorn över USB. Anropas en gång. */
void usb_lank_starta(void);

/* Millisekunder sedan datorn senast skrev en rad. */
int32_t usb_lank_tyst_ms(void);
