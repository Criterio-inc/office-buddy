#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Händelser har egen livslängd; färg och beröring kvitterar aldrig en agent. */
typedef enum { SIGNAL_MEJL = 1, SIGNAL_CLAUDE, SIGNAL_CODEX, SIGNAL_MOTE, SIGNAL_KLAR, SIGNAL_SMS = 7, SIGNAL_TEAMS, SIGNAL_PAMINNELSE, SIGNAL_BACKUP, SIGNAL_CLAUDE_KLAR, SIGNAL_CODEX_KLAR } signal_typ_t;
void signaler_agent(const char *aktor, const char *lage, const char *id, const char *projekt);
void signaler_mejl(uint32_t farg, const char *text);
void signaler_mote(int minuter, const char *text);
void signaler_timer(void);
void signaler_notis(signal_typ_t typ, uint32_t farg, const char *text);
void signaler_kvittera(void);  /* datorns explicita tysta-kommando, inget agentsvar */
void signaler_vila(bool vila);
bool signaler_upptagen(void);
int signaler_vantande(void);
