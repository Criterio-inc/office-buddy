#pragma once
#include <stdbool.h>
#include "humor.h"

/* Startar högtalaren. Anropas en gång. */
void ljud_starta(void);

/* Spelar ett av buddyns få ljud. Kan anropas från vilken uppgift som helst. */
void ljud_spela(humor_ljud_t l);

void ljud_satt_pa(bool pa);

/* Volym 0..100. 0 är tyst. */
void ljud_satt_volym(int procent);
