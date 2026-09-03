/*
 * Protokollet — raderna som datorn skickar till buddyn.
 *
 * En rad, ett kommando, ett svar. Texten är UTF-8 och avslutas med radslut.
 * Svaren börjar alltid med "ob " så att datorn kan skilja dem från kortets
 * vanliga logg, som går på samma USB-ström.
 *
 *   hej                        ob hej office-buddy <uttryck>
 *   status                     ob status energi … glädje … oro … <ord> <uttryck>
 *   tid <epok> [förskjutning]  ställer klockan: unix-tid i UTC, och datorns
 *                              tidszon som sekunder öster om UTC
 *   vantande <antal> <röda>    vad som ligger och väntar, ur pulsservern
 *   aldst <dagar> <projekt>    det som väntat längst, ger repliker
 *   sag <text>                 säger raden under ansiktet i sju sekunder
 *   avbockat                   något som väntade blev gjort
 *   peta | knack | lyft        händelser till humöret
 *   uttryck <namn> [ms]        ett uttryck en stund (4 s om inget anges)
 *   ljus <procent>             glasets ljusstyrka
 *   ljud <0|1>                 högtalaren av eller på
 *   spela <blipp|lyft|glad|trudelutt>   ett av ljuden
 *   claude <vantar|klar|jobbar> [id] [projekt]   en Claude-session behöver dig, är klar, eller arbetar
 *   backup <ok|saknas [text]>  nattens backup, från datorn en gång per dag
 *   mote <minuter> <rubrik>    ett möte om så många minuter (0 = nu)
 *   mejl [#rrggbb] <text>      nytt mejl, med kontots färg först om man vill
 *   paminnelse <text>          en påminnelse som förfaller
 *   vy <ansikte|klocka|timer>  byter vy, som ett svep
 *   timer <minuter>            ställer och startar timern, 0 stoppar
 *
 * Delad mellan kortet och emulatorn, så att båda svarar likadant.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void (*tid)(long epok, long forskjutning_s);   /* ny tid; förskjutning LONG_MIN = okänd */
    void (*ljus)(int procent);    /* får en ny ljusstyrka, får vara NULL */
    void (*ljud)(bool pa);        /* högtalaren av eller på, får vara NULL */
} protokoll_krokar_t;

void protokoll_satt_krokar(const protokoll_krokar_t *krokar);

/*
 * Tolkar en rad och skriver svaret, utan radslut, i svar. Returnerar false
 * om raden inte förstods; svaret säger då vad som gick fel.
 */
bool protokoll_rad(const char *rad, char *svar, size_t storlek);
