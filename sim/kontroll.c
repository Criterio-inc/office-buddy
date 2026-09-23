/* Kör de riktiga modulerna med LVGL:s simulerade klocka och ritning. */
#include <stdio.h>
#include <string.h>
#include "kontroll.h"
#include "ansikte.h"
#include "humor.h"
#include "protokoll.h"
#include "signaler.h"
static int ljud[LJUD_ANTAL];
static void hors(humor_ljud_t l) { ljud[l]++; }
static int rad(const char *s) { char svar[256]; return protokoll_rad(s,svar,sizeof(svar)); }
#define KRAV(x) do { if (!(x)) { fprintf(stderr,"FEL rad %d: %s\n",__LINE__,#x); return 1; } } while(0)
int kontroll_signaler(lv_display_t *skarm, void (*steg)(lv_display_t *, uint32_t))
{
    humor_vid_ljud(hors);
    KRAV(rad("agent codex vantar vp-1 Projekt")); steg(skarm,5000);
    KRAV(ansikte_signal_typ()==SIGNAL_CODEX && ansikte_ar_varm());
    KRAV(signaler_vantande()==1 && ljud[LJUD_CODEX]==1);
    KRAV(rad("peta")); KRAV(rad("knack")); KRAV(signaler_vantande()==1);
    KRAV(rad("agent codex vantar vp-1 Projekt")); KRAV(ljud[LJUD_CODEX]==1);
    KRAV(rad("mejl Avsändare")); KRAV(ansikte_signal_typ()==SIGNAL_CODEX);
    KRAV(rad("mote 0 Testmöte")); KRAV(ansikte_signal_typ()==SIGNAL_MOTE);
    steg(skarm,12500); KRAV(ansikte_signal_typ()==SIGNAL_CODEX);
    KRAV(ljud[LJUD_CODEX]==1); /* återgången är tyst */
    KRAV(rad("agent codex borta vp-1")); steg(skarm,300);
    KRAV(ansikte_signal_typ()==SIGNAL_MEJL && !ansikte_ar_varm());
    KRAV(ljud[LJUD_MEJL]==1);
    steg(skarm,5500); KRAV(!signaler_upptagen() && ansikte_signal_typ()==0);
    KRAV(rad("mejl Ny avsändare")); KRAV(ljud[LJUD_MEJL]==1);
    steg(skarm,5500);
    KRAV(rad("agent claude vantar vp-2 Två"));
    KRAV(rad("agent codex vantar vp-3 Tre")); steg(skarm,5500);
    KRAV(ansikte_signal_typ()==SIGNAL_CODEX);
    KRAV(rad("agent codex jobbar vp-3")); KRAV(ansikte_signal_typ()==SIGNAL_CLAUDE);
    KRAV(rad("tysta")); KRAV(!signaler_upptagen() && signaler_vantande()==1);
    KRAV(rad("agent claude vantar vp-2 Två")); KRAV(!signaler_upptagen());
    KRAV(rad("agent claude borta vp-2"));
    KRAV(rad("agent codex klar vp-4 Bygget")); KRAV(ansikte_signal_typ()==SIGNAL_KLAR);
    steg(skarm,6500); KRAV(signaler_upptagen() && ansikte_agentlogga_typ()==3);
    KRAV(rad("agent codex jobbar vp-4")); KRAV(!signaler_upptagen());
    KRAV(rad("agent codex vantar vp-5 Gammal")); steg(skarm,91000);
    KRAV(signaler_vantande()==0 && !signaler_upptagen() && !ansikte_ar_varm());
    KRAV(rad("mote 10 Test")); steg(skarm,8500);
    KRAV(!ansikte_ar_varm() && !signaler_upptagen());
    humor_starta(10);
    KRAV(rad("agent codex vantar vp-6 Vila"));
    humor_tick(10,1000); KRAV(ansikte_signal_typ()==SIGNAL_CODEX);
    humor_satt_hemma(false); steg(skarm,1000);
    KRAV(!signaler_upptagen() && ansikte_uttryck()==UTTRYCK_SOVER);
    humor_satt_hemma(true); steg(skarm,1000);
    KRAV(signaler_upptagen() && ansikte_signal_typ()==SIGNAL_CODEX);
    KRAV(rad("agent codex borta vp-6"));
    KRAV(!rad("agent okand vantar a App"));
    KRAV(rad("agent codex vantar vp-notis Fråga"));
    KRAV(rad("teams Nytt meddelande"));
    KRAV(ansikte_signal_typ()==SIGNAL_CODEX);
    KRAV(rad("agent codex borta vp-notis"));
    KRAV(ansikte_signal_typ()==SIGNAL_TEAMS);
    steg(skarm,8500); KRAV(!signaler_upptagen());
    KRAV(rad("mote 0 Möte"));
    KRAV(rad("sms Nytt sms"));
    KRAV(ansikte_signal_typ()==SIGNAL_MOTE);
    steg(skarm,12500); KRAV(ansikte_signal_typ()==SIGNAL_SMS);
    steg(skarm,8500); KRAV(!signaler_upptagen());
    KRAV(rad("paminnelse Test")); KRAV(ansikte_signal_typ()==SIGNAL_PAMINNELSE);
    steg(skarm,12500); KRAV(!signaler_upptagen());
    KRAV(rad("agent claude klar logga Claude"));
    KRAV(ansikte_agentlogga_typ()==2);
    KRAV(!ansikte_ar_varm() && signaler_vantande()==0);
    steg(skarm,19500); KRAV(ansikte_agentlogga_typ()==2);
    steg(skarm,1000); KRAV(!signaler_upptagen() && ansikte_agentlogga_typ()==0);
    KRAV(rad("agent claude jobbar logga")); KRAV(ansikte_agentlogga_typ()==0);
    KRAV(rad("agent codex klar logga Codex"));
    KRAV(ansikte_agentlogga_typ()==3);
    KRAV(!ansikte_ar_varm() && signaler_vantande()==0);
    steg(skarm,19500); KRAV(ansikte_agentlogga_typ()==3);
    steg(skarm,1000); KRAV(!signaler_upptagen() && ansikte_agentlogga_typ()==0);
    KRAV(rad("agent codex jobbar logga")); KRAV(ansikte_agentlogga_typ()==0);
    /* Alla tidsbegränsade typer måste lämna både scen och logga. */
    static const struct { const char *rad; int ms; } korta[] = {
        {"mejl Kontroll", 5500}, {"sms Kontroll", 8500},
        {"teams Kontroll", 8500}, {"mote 10 Kontroll", 8500},
        {"mote 0 Kontroll", 12500}, {"paminnelse Kontroll", 12500},
        {"backup saknas Kontroll", 8500}
    };
    for (unsigned i=0; i<sizeof(korta)/sizeof(korta[0]); i++) {
        KRAV(rad(korta[i].rad)); KRAV(signaler_upptagen());
        steg(skarm,korta[i].ms);
        KRAV(!signaler_upptagen() && ansikte_signal_typ()==0);
        KRAV(ansikte_agentlogga_typ()==0 && !ansikte_ar_varm());
    }
    signaler_timer(); steg(skarm,12500); KRAV(!signaler_upptagen());
    /* Både Claude och Codex: nya ID:n stänger gamla klara, inte frågor. */
    KRAV(rad("agent claude klar gammal A"));
    KRAV(rad("agent codex klar gammal B"));
    KRAV(rad("agent claude vantar obesvarad C"));
    KRAV(rad("agent claude jobbar ny A")); KRAV(signaler_vantande()==1);
    KRAV(rad("agent codex jobbar ny B")); KRAV(signaler_vantande()==1);
    KRAV(rad("agent claude borta obesvarad")); KRAV(!signaler_upptagen());
    /* Gamla köade vardagsnotiser återupplivas inte efter en lång fråga. */
    KRAV(rad("agent claude vantar lang Fråga"));
    KRAV(rad("teams Gammal")); steg(skarm,61000);
    KRAV(rad("agent claude jobbar lang")); KRAV(!signaler_upptagen());
    KRAV(rad("sms Vila")); signaler_vila(true); signaler_vila(false);
    KRAV(!signaler_upptagen() && ansikte_signal_typ()==0);
    puts("PASS: frågor, kvittering, prioritet, ljud, avbrott, tidsgränser och automatisk återgång");
    return 0;
}
