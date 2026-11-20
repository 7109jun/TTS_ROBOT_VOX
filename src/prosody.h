#ifndef RV_PROSODY_H
#define RV_PROSODY_H

#include <stddef.h>
#include "phoneme.h"

/* One timed synthesis segment (output of the prosody engine). */
typedef struct {
    PhId id;
    unsigned flags;
    float dur_ms;
    float f0a, f0b;   /* pitch (Hz) at segment start / end */
    float amp;        /* amplitude scale */
} Seg;

typedef struct { Seg *v; size_t n, cap; } SegList;

void seg_init(SegList *s);
void seg_free(SegList *s);
int  seg_push(SegList *s, const Seg *seg);

typedef struct {
    float f0;           /* base pitch in Hz (speech mode) */
    float speed;        /* speaking rate, 1.0 = normal */
    float pitch_range;  /* intonation depth 0..1, 0 = monotone */
    float robotness;    /* 0..1: semitone-snapped pitch, gridded durations */
    float stress;       /* stress emphasis 0..2 */
    float pause_scale;  /* pause length multiplier */
    float vibrato;      /* sing mode vibrato depth in semitones (0 = none) */
    float bpm;          /* >0: snap syllable lengths to a 16th-note grid at this tempo */
    unsigned scale_mask; /* 12-bit allowed pitch classes relative to key (0 = chromatic) */
    float consonant;    /* obstruent lengthening factor source (1 = neutral) */
    float effort;       /* vocal effort -1..1: stronger stress contrast and accent loudness */
    int   key;          /* pitch class of the root, 0 = C ... 11 = B */
} ProsodyParams;

void prosody_defaults(ProsodyParams *p);

/* Speaking mode. */
int prosody_speech(const PhList *ph, const ProsodyParams *pp, SegList *out);

/* Singing mode: syllables are mapped, in order, onto the notes. */
typedef struct { int midi; float beats; } Note;   /* midi < 0 = rest */

/* Parse "C4:1 E4:0.5 G#4:0.5 R:1" (note[:beats]); returns note count or -1. */
int prosody_parse_melody(const char *s, Note **out);

int prosody_sing(const PhList *ph, const Note *notes, size_t nnotes, float bpm,
                 const ProsodyParams *pp, SegList *out);

#endif
