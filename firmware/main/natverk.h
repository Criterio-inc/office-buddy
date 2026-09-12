#pragma once
#include <stdbool.h>

/*
 * Wifi och nätverkslänken. Startar stationsläget, annonserar kortet som
 * office-buddy.local och lyssnar på port 8740 efter samma protokollrader
 * som USB-länken tar emot. Utan secrets.h (wifi-uppgifter) gör funktionen
 * ingenting, och buddyn går enbart på USB.
 */
void natverk_starta(void);
bool natverk_uppkopplat(void);

/* Kortets IP som text, "" om ingen. */
const char *natverk_ip(void);
