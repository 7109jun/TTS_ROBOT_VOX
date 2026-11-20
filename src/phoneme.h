#ifndef RV_PHONEME_H
#define RV_PHONEME_H

#include <stddef.h>

/* Own phoneme inventory (shared by English and Korean rules). */
typedef enum {
    PH_SIL = 0, PH_PAUSE,
    /* vowels */
    PH_IY, PH_IH, PH_EH, PH_AE, PH_AA, PH_AO, PH_UH, PH_UW, PH_AH, PH_ER, PH_EU, PH_OH,
    /* glides / liquids */
    PH_W, PH_Y, PH_R, PH_L,
    /* nasals */
    PH_M, PH_N, PH_NG,
    /* voiced fricatives */
    PH_V, PH_DH, PH_Z, PH_ZH,
    /* unvoiced fricatives */
    PH_F, PH_TH, PH_S, PH_SH, PH_HH,
    /* stops */
    PH_B, PH_D, PH_G, PH_P, PH_T, PH_K,
    PH_COUNT
} PhId;

typedef enum {
    PT_SILENCE, PT_VOWEL, PT_GLIDE, PT_NASAL, PT_VFRIC, PT_UFRIC, PT_VSTOP, PT_USTOP
} PhType;

typedef struct {
    const char *name;
    PhType type;
    float f1, f2, f3;       /* formant targets (Hz) */
    float b1, b2, b3;       /* bandwidths (Hz) */
    float noise_fc, noise_bw; /* frication / burst noise filter */
    float amp;              /* relative amplitude 0..1 */
    float dur_ms;           /* default duration */
    float nasal_zero;       /* antiresonance Hz, 0 = none */
} PhInfo;

const PhInfo *ph_info(PhId id);

/* Flags on a phoneme instance */
#define PF_STRESS1   0x01  /* primary stress */
#define PF_STRESS2   0x02  /* secondary stress */
#define PF_ASPIRATED 0x04  /* Korean aspirated stop: long burst + aspiration */
#define PF_TENSE     0x08  /* Korean tense stop: short, high pitch onset */
#define PF_WORD_END  0x10  /* last phoneme of a word */
#define PF_SYL_START 0x20  /* first phoneme of a syllable (used by sing mode) */
#define PF_PHRASE_END 0x40 /* last phoneme before sentence-final punctuation */
#define PF_QUESTION  0x80  /* phrase ends with '?' */
#define PF_UNRELEASED 0x100 /* stop without release burst (Korean coda) */
#define PF_DIPH2     0x200 /* second element of a diphthong (same syllable nucleus) */

typedef struct {
    PhId id;
    unsigned flags;
    float pause_ms;         /* for PH_PAUSE */
} PhItem;

typedef struct {
    PhItem *v;
    size_t n, cap;
} PhList;

void phl_init(PhList *l);
void phl_free(PhList *l);
int  phl_push(PhList *l, PhId id, unsigned flags);
int  phl_pause(PhList *l, float ms);

/* Text -> phonemes. lang: 0 = auto per word, 1 = English, 2 = Korean */
int text_to_phonemes(const char *utf8, int lang, PhList *out);

#endif
