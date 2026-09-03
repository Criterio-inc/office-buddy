/*
 * Klockan — PCF85063 realtidsklocka på I2C 0x51.
 *
 * Kortet vet vad klockan är även när nätet är borta, vilket låter skärmen
 * säga hur gammal datan är i stället för att bara konstatera tystnad.
 */

#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

/* Kopplar upp mot kretsen. Anropas en gång före de övriga. */
esp_err_t klocka_starta(void);

/* Läser kretsens tid. Falskt om den inte svarar eller tappat tiden. */
bool klocka_las(struct tm *ut);

/* Ställer kretsen och systemklockan efter en tidsstämpel i UTC. */
bool klocka_stall(time_t epok);

/* Läser kretsen och ställer systemklockan efter den. */
bool klocka_till_systemet(void);
