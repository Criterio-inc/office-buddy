/*
 * Ansiktet — allt som gör buddyn levande, och ingenting annat.
 *
 * Den här modulen känner bara till LVGL. Den vet inget om ESP32, USB eller
 * SDL, och det är hela poängen: kortet och emulatorn på MacBooken kör exakt
 * samma kod, så det du ser i fönstret är det som kommer stå på glaset.
 *
 * Ansiktet ritas som former, inte bilder. Ögonen är rundade rektanglar som
 * täcks av svarta lock och bågar, munnen är en båge, ett streck eller en
 * öppen form. Varje uttryck är en uppsättning tal, och rörelsen är att talen
 * glider mot sina mål med olika hastighet. Det är därför ett byte av uttryck
 * aldrig hoppar, och därför en blinkning kan lägga sig ovanpå vad som helst.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/*
 * Grundfärgen: allt som lyser i vila. En varm salviagrön; byt gärna till
 * din egen. Orange är reserverad för "något behöver dig".
 */
#define ANSIKTE_FARG_GRUND 0x7C9A78

/* Skärmens mått. Kortet är 368x448 stående, och emulatorn öppnar lika stort. */
#define ANSIKTE_BREDD 368
#define ANSIKTE_HOJD  448

typedef enum {
    UTTRYCK_NEUTRAL = 0,
    UTTRYCK_GLAD,
    UTTRYCK_VALDIGT_GLAD,
    UTTRYCK_FORVANAD,
    UTTRYCK_ENTUSIASTISK,
    UTTRYCK_NOJD,
    UTTRYCK_BLINKNING,      /* blinkar med ena ögat */
    UTTRYCK_LEDSEN,
    UTTRYCK_BESVIKEN,
    UTTRYCK_OROLIG,
    UTTRYCK_ARG,
    UTTRYCK_FUNDERSAM,
    UTTRYCK_TROTT,
    UTTRYCK_SOMNIG,
    UTTRYCK_GASPAR,
    UTTRYCK_STRESSAD,
    UTTRYCK_NYFIKEN,
    UTTRYCK_KAR,
    UTTRYCK_OVERVALDIGAD,
    UTTRYCK_SOVER,          /* slutna ögon, används av humöret nattetid */
    UTTRYCK_ANTAL
} uttryck_t;

/* Bygger ansiktet på den aktiva skärmen och startar dess egen klocka.
 * Anropas en gång, under LVGL-låset på kortet. */
void ansikte_bygg(void);

/* Byter grunduttryck. Övergången sker mjukt under ungefär en halv sekund. */
void ansikte_satt_uttryck(uttryck_t u);
uttryck_t ansikte_uttryck(void);
const char *uttryck_namn(uttryck_t u);

/* Ett uttryck en stund, sedan tillbaka till grunduttrycket. */
void ansikte_tillfalligt(uttryck_t u, int32_t ms);

/*
 * Sover: ögonen hålls slutna, blinkningen och den vandrande blicken vilar,
 * och andningen blir djupare och långsammare. Falskt väcker.
 */
void ansikte_sover(bool sover);

/*
 * Säger något: en kort rad under ansiktet som tonar in, står kvar i ms och
 * tonar ut. En ny rad ersätter den gamla. Tomt text tystar direkt.
 */
void ansikte_sag(const char *text, int32_t ms);

/*
 * Varm: hela ansiktet glider från cyan till en varm orange ton, som när
 * något på datorn behöver dig. Falskt glider tillbaka.
 */
void ansikte_varm(bool varm);
bool ansikte_ar_varm(void);

/*
 * En tillfällig färgton, t.ex. ett mejlkontos färg, i ms millisekunder.
 * Därefter glider ansiktet tillbaka till cyan, eller orange om något
 * fortfarande behöver dig. 0 tar bort tonen direkt.
 */
void ansikte_ton(uint32_t hex, int32_t ms);

/* Gömmer eller visar ansiktet, när en annan vy tar glaset. Livet går på ändå. */
void ansikte_synlig(bool synlig);

/* Blinkar nu, oavsett var den slumpade blinkningen låg. */
void ansikte_blinka(void);

/* Gäspar en gång och återgår sedan till det uttryck som gällde. */
void ansikte_gaspa(void);

/* Reagerar på en petning: en kort förvåning, sedan tillbaka. */
void ansikte_petad(void);

/*
 * Vem som ska få veta när glaset trycks. Utan krok reagerar ansiktet självt
 * med ansikte_petad(); med humöret påslaget ska humöret få händelsen i
 * stället, så att en petning också gör buddyn gladare.
 */
void ansikte_vid_petning(void (*krok)(void));

/*
 * Tittar åt ett håll, -1..1 i båda leder där (0,0) är rakt fram, och håller
 * blicken där ett par sekunder innan det egna vandrandet tar över igen.
 */
void ansikte_titta(float x, float y);
