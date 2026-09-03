/*
 * Emulatorn — Office Buddy i ett fönster på MacBooken.
 *
 * Samma ritkod och samma bildpunkter som kortet. Skillnaden är bara var
 * pixlarna hamnar: här går de till ett SDL-fönster i stället för till
 * AMOLED-panelen.
 *
 * Tangenter:
 *   vänster / höger   bläddra mellan uttrycken (stänger av humöret)
 *   M                 slå på humöret igen, så att klockan styr
 *   mellanslag        tillbaka till neutral
 *   B                 blinka nu
 *   G                 gäspa
 *   P                 peta (samma som ett tryck på glaset)
 *   K                 knacka, L lyfta, A bocka av något (händelser till humöret)
 *   + / -             vrid klockan en timme fram eller tillbaka
 *   piltangenter med skift   titta åt det hållet
 *   Esc               avsluta
 *
 * Emulatorn läser också samma rader som kortet på stdin (se protokoll.h),
 * så ett "uttryck arg" eller "vantande 16 3" går att skriva direkt i
 * terminalen, eller skickas från länkprogrammet med --port sim.
 *
 * Klockan: --klocka HH:MM sätter starttiden, --fart N låter buddyns tid gå
 * N gånger fortare, --vantande N --roda R säger vad som ligger och väntar.
 *
 * Bildläge, för att granska utan fönster eller spara ett facit:
 *   --bild fil.bmp [--uttryck namn] [--tid ms] [--blink]
 * Bildserie, för att se en rörelse bild för bild:
 *   --serie katalog/prefix --antal N --steg ms [--uttryck namn] [--blink]
 * Dygnet, en bild per timme med humöret påslaget:
 *   --dag katalog/prefix
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <SDL2/SDL.h>

#include "lvgl.h"
#include "ansikte.h"
#include "humor.h"
#include "protokoll.h"
#include "vyer.h"
#include <sys/select.h>
#include <unistd.h>

/*
 * LVGL behöver veta hur tiden går. I fönstret tas den från systemet, i
 * bildläget räknas den upp för hand så att varje bild hamnar på exakt den
 * millisekund som beställts.
 */
static bool     lasad_tid;
static uint32_t lasad_ms;

static uint32_t tick_nu(void)
{
    if (lasad_tid) return lasad_ms;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/*
 * Buddyns egen klocka. Går som datorns, eller fortare med --fart, eller från
 * ett valt klockslag med --klocka. Humöret får den som timme 0..24.
 */
static float    klocka_start_timme = -1;   /* -1 = datorns tid */
static uint32_t klocka_start_ms;
static float    klocka_fart = 1;
static bool     humor_pa = true;
static int      vantande = 0, roda = 0;

static float timme_nu(void)
{
    if (klocka_start_timme < 0) {
        time_t t = time(NULL);
        struct tm *lt = localtime(&t);
        klocka_start_timme = lt->tm_hour + lt->tm_min / 60.0f + lt->tm_sec / 3600.0f;
        klocka_start_ms = tick_nu();
    }
    float gatt = (float)(tick_nu() - klocka_start_ms) * klocka_fart / 3600000.0f;
    return fmodf(klocka_start_timme + gatt, 24.0f);
}

static void klocka_satt(float timme)
{
    klocka_start_timme = fmodf(timme + 24, 24);
    klocka_start_ms = tick_nu();
}

static bool tolka_klockslag(const char *text, float *ut)
{
    int hh, mm = 0;
    if (sscanf(text, "%d:%d", &hh, &mm) < 1) return false;
    *ut = (float)hh + (float)mm / 60.0f;
    return true;
}

/* Protokollets krokar: tid sätter emulatorns klocka, ljus skrivs bara ut. */
static void ny_tid(long epok, long forskjutning_s)
{
    (void)forskjutning_s;   /* emulatorn använder datorns egen tidszon */
    time_t t = (time_t)epok;
    struct tm *lt = localtime(&t);
    klocka_satt(lt->tm_hour + lt->tm_min / 60.0f + lt->tm_sec / 3600.0f);
}

static void nytt_ljus(int procent)
{
    printf("ljus %d %%\n", procent);
}

static void ljud(humor_ljud_t l)
{
    static const char *NAMN[] = { "blipp", "lyft-ton", "glad-ton", "trudelutt" };
    printf("♪ %s\n", NAMN[l]);
}

/* En rad från stdin, om det finns någon, utan att blockera. */
static bool las_stdin_rad(char *rad, size_t storlek)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval noll = { 0, 0 };
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &noll) <= 0) return false;
    return fgets(rad, (int)storlek, stdin) != NULL;
}

/* Vyerna vill veta vad klockan är: emulatorns egen klocka, som kan vridas. */
static bool tid_till_vyer(struct tm *ut)
{
    time_t nu = time(NULL);
    struct tm *lt = localtime(&nu);
    *ut = *lt;
    float t = timme_nu();
    ut->tm_hour = (int)t;
    ut->tm_min  = (int)((t - (int)t) * 60);
    return true;
}

/* Ett tryck på glaset går via humöret när det är påslaget. */
static void petning(void)
{
    if (humor_pa) humor_handelse(HANDELSE_PETAD);
    else ansikte_petad();
}

static uttryck_t uttryck_fran_namn(const char *namn)
{
    for (int u = 0; u < UTTRYCK_ANTAL; u++) {
        if (strcmp(uttryck_namn((uttryck_t)u), namn) == 0) return (uttryck_t)u;
    }
    fprintf(stderr, "Okänt uttryck: %s. Kända:", namn);
    for (int u = 0; u < UTTRYCK_ANTAL; u++) fprintf(stderr, " '%s'", uttryck_namn((uttryck_t)u));
    fprintf(stderr, "\n");
    exit(1);
}

/* ---- Bildläget --------------------------------------------------------- */

static uint8_t *bild_pixlar;

static void bild_flush(lv_display_t *skarm, const lv_area_t *yta, uint8_t *px)
{
    int32_t bredd = lv_area_get_width(yta);
    for (int32_t y = yta->y1; y <= yta->y2; y++) {
        memcpy(bild_pixlar + ((size_t)y * ANSIKTE_BREDD + yta->x1) * 4,
               px + (size_t)(y - yta->y1) * bredd * 4,
               (size_t)bredd * 4);
    }
    lv_display_flush_ready(skarm);
}

/* Okomprimerad BMP, 32 bitar. Konverteras till PNG med verktyg/bmp2png.py. */
static bool skriv_bmp(const char *sokvag, const uint8_t *bgra, int bredd, int hojd)
{
    FILE *f = fopen(sokvag, "wb");
    if (f == NULL) return false;

    const uint32_t pixelbytes = (uint32_t)bredd * (uint32_t)hojd * 4;
    const uint32_t filstorlek = 14 + 108 + pixelbytes;
    uint8_t huvud[14 + 108];
    memset(huvud, 0, sizeof(huvud));

    huvud[0] = 'B'; huvud[1] = 'M';
    memcpy(huvud + 2, &filstorlek, 4);
    uint32_t offset = 14 + 108;
    memcpy(huvud + 10, &offset, 4);
    uint32_t huvudstorlek = 108;
    memcpy(huvud + 14, &huvudstorlek, 4);
    int32_t b = bredd, h = -hojd;
    memcpy(huvud + 18, &b, 4);
    memcpy(huvud + 22, &h, 4);
    uint16_t plan = 1, bitar = 32;
    memcpy(huvud + 26, &plan, 2);
    memcpy(huvud + 28, &bitar, 2);
    uint32_t komprimering = 3;
    memcpy(huvud + 30, &komprimering, 4);
    memcpy(huvud + 34, &pixelbytes, 4);
    uint32_t rod = 0x00FF0000, gron = 0x0000FF00, bla = 0x000000FF, alfa = 0xFF000000;
    memcpy(huvud + 54, &rod, 4);
    memcpy(huvud + 58, &gron, 4);
    memcpy(huvud + 62, &bla, 4);
    memcpy(huvud + 66, &alfa, 4);
    uint32_t fargrymd = 0x73524742;
    memcpy(huvud + 70, &fargrymd, 4);

    fwrite(huvud, 1, sizeof(huvud), f);
    fwrite(bgra, 1, pixelbytes, f);
    fclose(f);
    return true;
}

/* Låter tiden gå i steg om 33 ms och kör ansiktets klocka för varje steg. */
static void simulera(lv_display_t *skarm, uint32_t ms)
{
    uint32_t slut = lasad_ms + ms;
    while (lasad_ms < slut) {
        lasad_ms += 33;
        if (humor_pa) humor_tick(timme_nu(), (int32_t)(33 * klocka_fart));
        lv_timer_handler();
    }
    lv_refr_now(skarm);
}

static int bildlage(const char *bildfil, const char *serie, const char *dag, int antal, int steg,
                    const char *uttryck, int tid, bool blink, const char *sag, bool varm, const char *vy, const char *radarg)
{
    /* LVGL kräver att bufferten är minnesjusterad, annars snurrar en assert
     * tyst på full CPU. */
    static __attribute__((aligned(64))) uint8_t rityta[ANSIKTE_BREDD * 50 * 4];
    bild_pixlar = calloc((size_t)ANSIKTE_BREDD * ANSIKTE_HOJD, 4);
    if (bild_pixlar == NULL) return 1;

    lasad_tid = true;
    lasad_ms  = 1000;
    lv_rand_set_seed(0x0FF1CE);

    lv_display_t *skarm = lv_display_create(ANSIKTE_BREDD, ANSIKTE_HOJD);
    lv_display_set_flush_cb(skarm, bild_flush);
    lv_display_set_buffers(skarm, rityta, NULL, sizeof(rityta), LV_DISPLAY_RENDER_MODE_PARTIAL);

    ansikte_bygg();
    vyer_bygg();
    vyer_satt_tid_krok(tid_till_vyer);
    humor_satt_vantande(vantande, roda);
    char namn[512];

    /* Dygnet: en bild per timme, med humöret startat på nytt vid varje. */
    if (dag != NULL) {
        for (int t = 0; t < 24; t++) {
            klocka_satt((float)t);
            humor_starta((float)t);
            humor_pa = true;
            simulera(skarm, 12000);
            snprintf(namn, sizeof(namn), "%s-%02d.bmp", dag, t);
            if (!skriv_bmp(namn, bild_pixlar, ANSIKTE_BREDD, ANSIKTE_HOJD)) return 1;
            printf("%02d:00  energi %.2f  %-8s %s\n", t, humor_las()->energi, humor_ord(),
                   uttryck_namn(ansikte_uttryck()));
        }
        return 0;
    }

    humor_pa = false;   /* stillbilderna visar det uttryck som beställts */
    if (uttryck != NULL) ansikte_satt_uttryck(uttryck_fran_namn(uttryck));
    if (sag != NULL) ansikte_sag(sag, 20000);
    if (varm) ansikte_varm(true);
    if (vy != NULL) { char r[64], sv[96]; snprintf(r, sizeof(r), "vy %s", vy); protokoll_rad(r, sv, sizeof(sv)); }
    if (radarg != NULL) { char sv[128]; protokoll_rad(radarg, sv, sizeof(sv)); printf("%s\n", sv); }

    simulera(skarm, (uint32_t)tid);
    if (blink) ansikte_blinka();

    if (serie == NULL) {
        if (blink) simulera(skarm, 66);
        bool ok = skriv_bmp(bildfil, bild_pixlar, ANSIKTE_BREDD, ANSIKTE_HOJD);
        printf(ok ? "Skrev %s\n" : "Kunde inte skriva %s\n", bildfil);
        return ok ? 0 : 1;
    }
    for (int i = 0; i < antal; i++) {
        snprintf(namn, sizeof(namn), "%s-%02d.bmp", serie, i);
        if (!skriv_bmp(namn, bild_pixlar, ANSIKTE_BREDD, ANSIKTE_HOJD)) return 1;
        simulera(skarm, (uint32_t)steg);
    }
    printf("Skrev %d bilder: %s-00.bmp ...\n", antal, serie);
    return 0;
}

/* ---- Fönstret ---------------------------------------------------------- */

static void visa_titel(lv_display_t *skarm)
{
    char titel[128];
    float t = timme_nu();
    snprintf(titel, sizeof(titel), "Office Buddy  %02d:%02d  %s  %s%s",
             (int)t, (int)((t - (int)t) * 60), humor_pa ? humor_ord() : "manuellt",
             uttryck_namn(ansikte_uttryck()), humor_pa ? "" : "  (M slår på humöret)");
    lv_sdl_window_set_title(skarm, titel);
}

int main(int argc, char **argv)
{
    const char *bildfil = NULL, *serie = NULL, *uttryck = NULL, *dag = NULL;
    const char *sag = NULL, *vy = NULL, *radarg = NULL;
    bool varm = false;
    int antal = 10, steg = 33, tid = 600;
    bool blink = false;

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--bild") == 0    && i + 1 < argc) bildfil = argv[++i];
        else if (strcmp(argv[i], "--serie") == 0   && i + 1 < argc) serie   = argv[++i];
        else if (strcmp(argv[i], "--uttryck") == 0 && i + 1 < argc) uttryck = argv[++i];
        else if (strcmp(argv[i], "--antal") == 0   && i + 1 < argc) antal   = atoi(argv[++i]);
        else if (strcmp(argv[i], "--steg") == 0    && i + 1 < argc) steg    = atoi(argv[++i]);
        else if (strcmp(argv[i], "--tid") == 0     && i + 1 < argc) tid     = atoi(argv[++i]);
        else if (strcmp(argv[i], "--blink") == 0) blink = true;
        else if (strcmp(argv[i], "--sag") == 0      && i + 1 < argc) sag = argv[++i];
        else if (strcmp(argv[i], "--varm") == 0) varm = true;
        else if (strcmp(argv[i], "--vy") == 0       && i + 1 < argc) vy = argv[++i];
        else if (strcmp(argv[i], "--rad") == 0      && i + 1 < argc) radarg = argv[++i];
        else if (strcmp(argv[i], "--dag") == 0      && i + 1 < argc) dag = argv[++i];
        else if (strcmp(argv[i], "--fart") == 0     && i + 1 < argc) klocka_fart = (float)atof(argv[++i]);
        else if (strcmp(argv[i], "--vantande") == 0 && i + 1 < argc) vantande = atoi(argv[++i]);
        else if (strcmp(argv[i], "--roda") == 0     && i + 1 < argc) roda = atoi(argv[++i]);
        else if (strcmp(argv[i], "--klocka") == 0   && i + 1 < argc) {
            float t;
            if (!tolka_klockslag(argv[++i], &t)) { fprintf(stderr, "Klockslag som HH:MM\n"); return 1; }
            klocka_start_timme = t;
        }
        else { fprintf(stderr, "Okänt argument: %s\n", argv[i]); return 1; }
    }

    lv_init();
    lv_tick_set_cb(tick_nu);

    if (bildfil != NULL || serie != NULL || dag != NULL) {
        return bildlage(bildfil, serie, dag, antal, steg, uttryck, tid, blink, sag, varm, vy, radarg);
    }

    lv_rand_set_seed((uint32_t)time(NULL));
    lv_display_t *skarm = lv_sdl_window_create(ANSIKTE_BREDD, ANSIKTE_HOJD);
    lv_sdl_mouse_create();

    ansikte_bygg();
    ansikte_vid_petning(petning);
    vyer_bygg();
    vyer_satt_tid_krok(tid_till_vyer);
    protokoll_krokar_t krokar = { .tid = ny_tid, .ljus = nytt_ljus };
    protokoll_satt_krokar(&krokar);
    humor_vid_ljud(ljud);
    klocka_start_ms = tick_nu();
    humor_satt_vantande(vantande, roda);
    humor_starta(timme_nu());
    if (uttryck != NULL) { humor_pa = false; ansikte_satt_uttryck(uttryck_fran_namn(uttryck)); }
    visa_titel(skarm);
    printf("vänster/höger byter uttryck (stänger av humöret), M slår på humöret, mellanslag neutral\n");
    printf("B blink, G gäsp, P peta, K knack, L lyft, A bocka av, +/- vrider klockan, Esc avslutar\n");

    uint32_t forra_ms = tick_nu(), titel_ms = 0;
    bool kor = true;
    while (kor) {
        uint32_t nu_ms = tick_nu();
        int32_t dt = (int32_t)(nu_ms - forra_ms);
        forra_ms = nu_ms;
        if (humor_pa && dt > 0) humor_tick(timme_nu(), (int32_t)(dt * klocka_fart));
        titel_ms += (uint32_t)dt;
        if (titel_ms > 1000) { titel_ms = 0; visa_titel(skarm); }

        char rad[256], svar[256];
        if (las_stdin_rad(rad, sizeof(rad))) {
            protokoll_rad(rad, svar, sizeof(svar));
            printf("%s\n", svar);
            fflush(stdout);
        }

        /*
         * Bara tangenter och stängning plockas härifrån. Musen lämnas kvar i
         * kön åt LVGL:s egen SDL-drivrutin, annars når ett klick aldrig
         * ansiktet och petningen slutar fungera.
         */
        SDL_Event h;
        SDL_PumpEvents();
        if (SDL_PeepEvents(&h, 1, SDL_GETEVENT, SDL_QUIT, SDL_QUIT) > 0) kor = false;
        while (SDL_PeepEvents(&h, 1, SDL_GETEVENT, SDL_KEYDOWN, SDL_KEYDOWN) > 0) {
            bool skift = (h.key.keysym.mod & KMOD_SHIFT) != 0;
            int u = (int)ansikte_uttryck();
            switch (h.key.keysym.sym) {
            case SDLK_ESCAPE: kor = false; break;
            case SDLK_LEFT:
                if (skift) ansikte_titta(-0.9f, 0);
                else { humor_pa = false; ansikte_sover(false); ansikte_satt_uttryck((uttryck_t)((u + UTTRYCK_ANTAL - 1) % UTTRYCK_ANTAL)); visa_titel(skarm); }
                break;
            case SDLK_RIGHT:
                if (skift) ansikte_titta(0.9f, 0);
                else { humor_pa = false; ansikte_sover(false); ansikte_satt_uttryck((uttryck_t)((u + 1) % UTTRYCK_ANTAL)); visa_titel(skarm); }
                break;
            case SDLK_UP:   if (skift) ansikte_titta(0, -0.9f); break;
            case SDLK_DOWN: if (skift) ansikte_titta(0, 0.9f);  break;
            case SDLK_SPACE: humor_pa = false; ansikte_sover(false); ansikte_satt_uttryck(UTTRYCK_NEUTRAL); visa_titel(skarm); break;
            case SDLK_m: humor_pa = true; humor_starta(timme_nu()); visa_titel(skarm); break;
            case SDLK_v: vyer_nasta(); break;
            case SDLK_b: ansikte_blinka(); break;
            case SDLK_g: ansikte_gaspa(); break;
            case SDLK_p: petning(); break;
            case SDLK_k: humor_handelse(HANDELSE_KNACK); break;
            case SDLK_l: humor_handelse(HANDELSE_LYFT); break;
            case SDLK_a: humor_handelse(HANDELSE_AVBOCKAT); break;
            case SDLK_PLUS: case SDLK_KP_PLUS: case SDLK_EQUALS:
                klocka_satt(timme_nu() + 1); visa_titel(skarm); break;
            case SDLK_MINUS: case SDLK_KP_MINUS:
                klocka_satt(timme_nu() - 1); visa_titel(skarm); break;
            default: break;
            }
        }
        lv_timer_handler();
        SDL_Delay(5);
    }
    return 0;
}
