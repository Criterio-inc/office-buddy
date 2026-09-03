/*
 * Humöret — buddyns inre tillstånd.
 *
 * Uttrycket ska följa av ett tillstånd, inte kopplas rakt på enskilda
 * händelser. Tillståndet är tre tal: energi (pigg eller seg, styrd av tid på
 * dygnet), glädje (upp av en petning eller en avbockad sak, ned av oro) och
 * oro (av det som ligger och väntar). Ur dem väljs ett grunduttryck, och
 * ovanpå kommer små episoder med slumpade mellanrum: en fundersam blick, en
 * gäspning när energin är låg, en orolig stund när mycket väntar.
 *
 * Modulen känner till ansiktet men inget om klockor eller kort. Den som
 * anropar skickar in tiden, så att emulatorn kan snurra ett dygn på en minut.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float energi;   /* 0..1 */
    float gladje;   /* -1..1 */
    float oro;      /* 0..1 */
    bool  sover;
} humor_t;

typedef enum {
    HANDELSE_PETAD,     /* tryck på glaset */
    HANDELSE_KNACK,     /* knack på bordet, från rörelsesensorn */
    HANDELSE_LYFT,      /* lyft upp, från rörelsesensorn */
    HANDELSE_AVBOCKAT,  /* något som väntade blev gjort */
} humor_handelse_t;

/* Startar humöret vid en viss tid på dygnet, 0..24 som decimaltal. */
void humor_starta(float timme);

/* Låter tiden gå. dt_ms i buddyns egen tid, som kan gå fortare än klockan. */
void humor_tick(float timme, int32_t dt_ms);

void humor_handelse(humor_handelse_t h);

/* Vad som ligger och väntar: antal poster och hur många som är röda. */
void humor_satt_vantande(int antal, int roda);

/* Det som väntat längst: projektets namn och hur många dagar. Ger repliker. */
void humor_satt_aldst(const char *projekt, int dagar);

/*
 * Claude på datorn. "vantar" = Claude behöver dig (tillstånd eller en fråga),
 * "klar" = Claude är färdig och väntar på nästa steg, "jobbar" = du svarade
 * och Claude arbetar. Buddyn tittar upp och säger till, en gång, och håller
 * raden kvar tills du är tillbaka.
 */
typedef enum { CLAUDE_JOBBAR, CLAUDE_KLAR, CLAUDE_VANTAR } claude_lage_t;

/*
 * Länken till datorn. Tystnar den, sover datorn, och då sover buddyn
 * oavsett klockslag. Kommer den tillbaka vaknar buddyn med en trudelutt.
 */
void humor_satt_lank(bool ok);

/* Ett möte: minuter kvar (0 = nu) och rubrik. Datorn skickar vid 10 och vid 0. */
void humor_mote(int minuter, const char *rubrik);

/* En påminnelse som förfaller: orange en halv minut, texten, ett blipp. */
void humor_paminnelse(const char *text);

/* Nytt mejl: en blick, en rad och kontots färg en stund. Inget ljud. hex 0 = ingen färg. */
void humor_mejl(uint32_t hex, const char *text);

/* Nattens backup: ok tystar, saknas ger en bekymrad stund med texten. */
void humor_satt_backup(bool ok, const char *text);
/*
 * Varje Claude-session har ett id och ett projektnamn. En ny session som
 * väntar annonseras alltid, även om en annan redan väntar. "jobbar" tar
 * bort just den sessionen; det som återstår avgör vad glaset visar.
 * id NULL eller "0" betyder "en enda, okänd session".
 */
void humor_satt_claude(claude_lage_t lage, const char *id, const char *namn);

/* Hur många sessioner som väntar just nu, för status. */
int humor_claude_vantande(void);

const humor_t *humor_las(void);

/* Ljud som humöret vill ge ifrån sig. Värden får koppla en krok; utan krok tyst. */
typedef enum { LJUD_BLIPP, LJUD_LYFT, LJUD_GLAD, LJUD_TRUDELUTT, LJUD_ANTAL } humor_ljud_t;
void humor_vid_ljud(void (*krok)(humor_ljud_t));

/* Ber om ett ljud utifrån, t.ex. från protokollet. Tyst utan krok. */
void humor_ljud(humor_ljud_t l);

/* Ett ord om läget, för fönstertiteln och loggen: "pigg", "seg", "sover"… */
const char *humor_ord(void);
