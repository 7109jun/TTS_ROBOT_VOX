#include "phoneme.h"
#include <stdlib.h>
#include <string.h>

/* name, type, F1 F2 F3, B1 B2 B3, noise fc, noise bw, amp, dur, nasal zero */
static const PhInfo TABLE[PH_COUNT] = {
    [PH_SIL]   = {"_",  PT_SILENCE, 0,0,0, 0,0,0, 0,0, 0.0f,  80, 0},
    [PH_PAUSE] = {"|",  PT_SILENCE, 0,0,0, 0,0,0, 0,0, 0.0f, 200, 0},

    [PH_IY] = {"iy", PT_VOWEL,  270,2290,3010,  60, 90, 150, 0,0, 1.00f, 115, 0},
    [PH_IH] = {"ih", PT_VOWEL,  390,1990,2550,  70, 100,150, 0,0, 1.00f,  85, 0},
    [PH_EH] = {"eh", PT_VOWEL,  530,1840,2480,  70, 100,150, 0,0, 1.00f,  95, 0},
    [PH_AE] = {"ae", PT_VOWEL,  660,1720,2410,  80, 100,150, 0,0, 1.00f, 125, 0},
    [PH_AA] = {"aa", PT_VOWEL,  730,1090,2440,  90, 110,150, 0,0, 1.00f, 130, 0},
    [PH_AO] = {"ao", PT_VOWEL,  570, 840,2410,  80, 100,150, 0,0, 1.00f, 125, 0},
    [PH_UH] = {"uh", PT_VOWEL,  440,1020,2240,  70, 100,150, 0,0, 1.00f,  85, 0},
    [PH_UW] = {"uw", PT_VOWEL,  300, 870,2240,  60,  90,150, 0,0, 1.00f, 120, 0},
    [PH_AH] = {"ah", PT_VOWEL,  640,1190,2390,  80, 100,150, 0,0, 0.95f,  75, 0},
    [PH_ER] = {"er", PT_VOWEL,  490,1350,1690,  70, 100,150, 0,0, 0.95f, 110, 0},
    [PH_EU] = {"eu", PT_VOWEL,  330,1300,2300,  70, 100,150, 0,0, 1.00f,  95, 0},

    [PH_OH] = {"oh", PT_VOWEL,  400, 800,2300,  70, 100,150, 0,0, 1.00f, 120, 0},

    [PH_W] = {"w", PT_GLIDE, 290, 610,2150,  70, 100,150, 0,0, 0.85f, 70, 0},
    [PH_Y] = {"y", PT_GLIDE, 260,2070,3020,  60, 100,150, 0,0, 0.85f, 65, 0},
    [PH_R] = {"r", PT_GLIDE, 310,1060,1380,  70, 100,150, 0,0, 0.85f, 75, 0},
    [PH_L] = {"l", PT_GLIDE, 360,1300,2500,  70, 100,150, 0,0, 0.85f, 70, 0},

    [PH_M]  = {"m",  PT_NASAL, 250,1100,2200, 100,150,200, 0,0, 0.55f, 80,  800},
    [PH_N]  = {"n",  PT_NASAL, 250,1500,2500, 100,150,200, 0,0, 0.55f, 75, 1100},
    [PH_NG] = {"ng", PT_NASAL, 250,2000,2700, 100,150,200, 0,0, 0.55f, 85, 1700},

    [PH_V]  = {"v",  PT_VFRIC, 250,1100,2200, 80,120,200, 5000,5000, 0.45f, 70, 0},
    [PH_DH] = {"dh", PT_VFRIC, 270,1300,2500, 80,120,200, 5500,4000, 0.35f, 55, 0},
    [PH_Z]  = {"z",  PT_VFRIC, 250,1700,2600, 80,120,200, 6500,2500, 0.55f, 80, 0},
    [PH_ZH] = {"zh", PT_VFRIC, 250,1500,2300, 80,120,200, 3200,1800, 0.55f, 85, 0},

    [PH_F]  = {"f",  PT_UFRIC, 0,0,0, 0,0,0, 5000,5000, 0.35f, 90, 0},
    [PH_TH] = {"th", PT_UFRIC, 0,0,0, 0,0,0, 5500,4000, 0.30f, 85, 0},
    [PH_S]  = {"s",  PT_UFRIC, 0,0,0, 0,0,0, 6500,2500, 0.60f, 110, 0},
    [PH_SH] = {"sh", PT_UFRIC, 0,0,0, 0,0,0, 3200,1800, 0.65f, 115, 0},
    [PH_HH] = {"h",  PT_UFRIC, 0,0,0, 0,0,0, 1500,2500, 0.30f, 70, 0},

    [PH_B] = {"b", PT_VSTOP, 250,1100,2200, 80,120,200, 800,1500,  0.50f, 60, 0},
    [PH_D] = {"d", PT_VSTOP, 250,1700,2600, 80,120,200, 4000,2500, 0.50f, 55, 0},
    [PH_G] = {"g", PT_VSTOP, 250,1900,2300, 80,120,200, 2200,1500, 0.50f, 60, 0},
    [PH_P] = {"p", PT_USTOP, 0,0,0, 0,0,0, 1000,1500, 0.55f, 80, 0},
    [PH_T] = {"t", PT_USTOP, 0,0,0, 0,0,0, 4500,2500, 0.60f, 75, 0},
    [PH_K] = {"k", PT_USTOP, 0,0,0, 0,0,0, 2200,1500, 0.55f, 80, 0},
};

const PhInfo *ph_info(PhId id) { return &TABLE[(id >= 0 && id < PH_COUNT) ? id : PH_SIL]; }

void phl_init(PhList *l) { l->v = NULL; l->n = l->cap = 0; }
void phl_free(PhList *l) { free(l->v); l->v = NULL; l->n = l->cap = 0; }

static int phl_reserve(PhList *l) {
    if (l->n < l->cap) return 0;
    size_t nc = l->cap ? l->cap * 2 : 256;
    PhItem *p = (PhItem *)realloc(l->v, nc * sizeof(PhItem));
    if (!p) return -1;
    l->v = p; l->cap = nc;
    return 0;
}

int phl_push(PhList *l, PhId id, unsigned flags) {
    if (phl_reserve(l)) return -1;
    l->v[l->n].id = id; l->v[l->n].flags = flags; l->v[l->n].pause_ms = 0;
    l->n++;
    return 0;
}

int phl_pause(PhList *l, float ms) {
    if (phl_reserve(l)) return -1;
    l->v[l->n].id = PH_PAUSE; l->v[l->n].flags = 0; l->v[l->n].pause_ms = ms;
    l->n++;
    return 0;
}
