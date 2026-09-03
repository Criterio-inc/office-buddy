/*
 * Vyerna — det som visas när man sveper på glaset.
 *
 * Ansiktet är hemma. Ett svep åt sidan ger klockan, ett till ger timern,
 * ett till är hemma igen. Vyerna delar skärm med ansiktet och gömmer det
 * medan de syns; ansiktets liv går på under tiden så att det inte hoppar
 * när man kommer tillbaka.
 *
 * Tiden kommer utifrån: kortet räknar lokal tid ur sin klockkrets och
 * datorns tidszon, emulatorn tar datorns.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum { VY_ANSIKTE = 0, VY_KLOCKA, VY_TIMER, VY_ANTAL } vy_t;

/* Bygger vyerna och kopplar svep på skärmen. Anropas efter ansikte_bygg(). */
void vyer_bygg(void);

/* Vem som vet vad klockan är. Fyller i en struct tm i lokal tid, falskt om okänt. */
void vyer_satt_tid_krok(bool (*krok)(struct tm *ut));

void vyer_visa(vy_t vy);
vy_t vyer_aktiv(void);
void vyer_nasta(void);
void vyer_forra(void);

/* Timern kan styras utifrån också, t.ex. över USB. Minuter, 0 stoppar. */
void vyer_timer_satt(int minuter);
void vyer_timer_starta(bool kor);
int  vyer_timer_kvar_s(void);   /* -1 om ingen timer går */
