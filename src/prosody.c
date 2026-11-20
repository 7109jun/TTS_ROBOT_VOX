/* Prosody engine: duration, pitch contour, stress, pauses (speech mode) and
 * note mapping with vibrato (sing mode). */
#include "prosody.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

void seg_init(SegList *s) { s->v = NULL; s->n = s->cap = 0; }
void seg_free(SegList *s) { free(s->v); s->v = NULL; s->n = s->cap = 0; }
int seg_push(SegList *s, const Seg *seg) {
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 256;
        Seg *p = (Seg *)realloc(s->v, nc * sizeof(Seg));
        if (!p) return -1;
        s->v = p; s->cap = nc;
    }
    s->v[s->n++] = *seg;
    return 0;
}

void prosody_defaults(ProsodyParams *p) {
    p->f0 = 110.f;
    p->speed = 1.f;
    p->pitch_range = 0.5f;
    p->robotness = 0.6f;
    p->stress = 1.f;
    p->pause_scale = 1.f;
    p->vibrato = 0.f;
    p->consonant = 1.f; p->effort = 0.f; p->bpm = 0.f; p->scale_mask = 0; p->key = 9;
}

static float snap_scale(float f0, const ProsodyParams *pp) {
    if (!pp->scale_mask) return f0;
    float m = 69.f + 12.f * log2f(f0 / 440.f); /* midi */
    int base = (int)floorf(m + 0.5f), best = base; float bd = 99.f;
    for (int d = -6; d <= 6; d++) {
        int n = base + d, pc = ((n - pp->key) % 12 + 12) % 12;
        if (!(pp->scale_mask & (1u << pc))) continue;
        float dist = fabsf((float)n - m);
        if (dist < bd) { bd = dist; best = n; }
    }
    return 440.f * powf(2.f, ((float)best - 69.f) / 12.f);
}

static float snap_pitch(float f0, float amount) {
    /* quantize to the nearest semitone, blended by amount (mechanical auto-tune feel) */
    float m = 12.f * log2f(f0 / 110.f);
    float q = floorf(m + 0.5f);
    return 110.f * powf(2.f, (m + (q - m) * amount) / 12.f);
}

static int is_voiced_type(PhType t) {
    return t == PT_VOWEL || t == PT_GLIDE || t == PT_NASAL || t == PT_VFRIC || t == PT_VSTOP;
}

/* base duration for one phoneme instance */
static float base_dur(const PhItem *it) {
    const PhInfo *pi = ph_info(it->id);
    float d = pi->dur_ms;
    if (pi->type == PT_USTOP) {
        if (it->flags & PF_ASPIRATED) d = 115.f;
        else if (it->flags & PF_TENSE) d = 60.f;
        else if (it->flags & PF_UNRELEASED) d = 55.f;
    } else if (pi->type == PT_VSTOP) {
        d = 62.f;
    } else if ((it->flags & PF_TENSE) && (pi->type == PT_UFRIC || pi->type == PT_GLIDE)) {
        d = pi->type == PT_GLIDE ? d * 0.6f : d * 1.25f; /* tense s long, flap l short */
    }
    if (it->flags & PF_DIPH2) d = 95.f;
    return d;
}

int prosody_speech(const PhList *ph, const ProsodyParams *pp, SegList *out) {
    float speed = pp->speed > 0.1f ? pp->speed : 1.f;
    float prev_f0 = 0;
    /* locate phrase boundaries to apply declination */
    size_t i = 0;
    while (i < ph->n) {
        size_t j = i;
        while (j < ph->n && ph->v[j].id != PH_PAUSE) j++;
        /* phrase = [i, j) ; ends at pause or text end */
        size_t cnt = j - i;
        int question = 0;
        for (size_t k = i; k < j; k++) if (ph->v[k].flags & PF_QUESTION) question = 1;
        float range = pp->pitch_range * (1.f - 0.5f * pp->robotness);
        for (size_t k = i; k < j; k++) {
            const PhItem *it = &ph->v[k];
            const PhInfo *pi = ph_info(it->id);
            float d = base_dur(it);
            float dm = 1.f;
            if (it->flags & (PF_STRESS1)) dm *= 1.0f + (0.25f + 0.1f * pp->effort) * pp->stress;
            if (pp->consonant > 1.f && (pi->type == PT_UFRIC || pi->type == PT_USTOP || pi->type == PT_VFRIC || pi->type == PT_VSTOP)) dm *= 1.f + 0.1f * (pp->consonant - 1.f);
            else if (pi->type == PT_VOWEL && !(it->flags & PF_STRESS2) && !(it->flags & PF_DIPH2)) dm *= 0.85f;
            if (it->flags & PF_WORD_END) dm *= 1.08f;
            if (k + 3 >= j) dm *= 1.25f; /* phrase-final lengthening */
            d *= dm / speed;
            if (pp->robotness > 0.f && d > 30.f) { /* grid durations toward a 20 ms tick */
                float q = floorf(d / 20.f + 0.5f) * 20.f;
                d += (q - d) * pp->robotness;
            }
            float pos = cnt > 1 ? (float)(k - i) / (float)(cnt - 1) : 0.f;
            float f = pp->f0 * (1.f + 0.12f * range - 0.30f * range * pos); /* declination */
            if (it->flags & PF_STRESS1) f *= 1.f + 0.22f * range * pp->stress;
            else if (it->flags & PF_STRESS2) f *= 1.f + 0.07f * range;
            if (question && pos > 0.7f) f *= 1.f + 0.45f * range * (pos - 0.7f) / 0.3f;
            if ((it->flags & PF_TENSE) || (it->flags & PF_ASPIRATED)) f *= 1.05f; /* Korean consonant effect */
            if (!is_voiced_type(pi->type) && pi->type != PT_SILENCE) f = prev_f0 > 0 ? prev_f0 : f;
            if (pp->scale_mask) f = snap_scale(f, pp);
            else if (pp->robotness > 0.f) f = snap_pitch(f, pp->robotness);
            Seg s;
            s.id = it->id; s.flags = it->flags; s.dur_ms = d;
            s.f0a = (prev_f0 > 0 && !pp->scale_mask) ? prev_f0 : f; s.f0b = f; s.amp = (it->flags & PF_STRESS1) ? 1.f + 0.3f * pp->effort : 1.f; /* scale mode: stepped notes, no glides */
            if (seg_push(out, &s)) return -1;
            prev_f0 = f;
        }
        if (j < ph->n) { /* pause item */
            Seg s = { PH_PAUSE, 0, ph->v[j].pause_ms * pp->pause_scale / speed, prev_f0, prev_f0, 0.f };
            if (seg_push(out, &s)) return -1;
            j++;
        }
        i = j;
    }
    if (pp->bpm > 0.f) { /* snap each syllable (SYL_START..next) to the 16th-note grid by stretching its nucleus */
        float grid = 60000.f / pp->bpm / 4.f;
        size_t k = 0;
        while (k < out->n) {
            if (!(out->v[k].flags & PF_SYL_START) || out->v[k].id == PH_PAUSE) { k++; continue; }
            size_t e = k + 1;
            while (e < out->n && !(out->v[e].flags & PF_SYL_START) && out->v[e].id != PH_PAUSE && out->v[e].id != PH_SIL) e++;
            float tot = 0; size_t nuc = k; int found = 0;
            for (size_t q = k; q < e; q++) { tot += out->v[q].dur_ms; if (!found && ph_info(out->v[q].id)->type == PT_VOWEL) { nuc = q; found = 1; } }
            float target = floorf(tot / grid + 0.5f) * grid;
            if (target < grid) target = grid;
            float d = out->v[nuc].dur_ms + (target - tot);
            if (found && d > 40.f) out->v[nuc].dur_ms = d;
            k = e;
        }
    }
    /* short trailing silence */
    Seg tail = { PH_SIL, 0, 120.f, prev_f0, prev_f0, 0.f };
    return seg_push(out, &tail);
}

/* ---------- sing mode ---------- */
static int note_midi(const char *s, const char **end) {
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; /* A B C D E F G */
    if (!isalpha((unsigned char)*s)) return -2;
    int c = toupper((unsigned char)*s);
    if (c < 'A' || c > 'G') return -2;
    int semi = base[c - 'A'];
    s++;
    if (*s == '#') { semi++; s++; } else if (*s == 'b') { semi--; s++; }
    int oct = 4, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    if (isdigit((unsigned char)*s)) { oct = *s - '0'; if (neg) oct = -oct; s++; }
    *end = s;
    return 12 * (oct + 1) + semi;
}

int prosody_parse_melody(const char *s, Note **out) {
    size_t cap = 32, n = 0;
    Note *v = (Note *)malloc(cap * sizeof(Note));
    if (!v) return -1;
    while (*s) {
        while (*s && (isspace((unsigned char)*s) || *s == ',')) s++;
        if (!*s) break;
        Note nt;
        const char *e = s;
        if ((*s == 'R' || *s == 'r' || *s == '-') && !isalnum((unsigned char)s[1])) { nt.midi = -1; e = s + 1; }
        else if (*s == 'R' || *s == 'r') { nt.midi = -1; e = s + 1; }
        else {
            nt.midi = note_midi(s, &e);
            if (nt.midi == -2) { free(v); fprintf(stderr, "bad note near '%.8s'\n", s); return -1; }
        }
        nt.beats = 1.f;
        if (*e == ':') { char *q; nt.beats = strtof(e + 1, &q); e = q; if (nt.beats <= 0) nt.beats = 1.f; }
        if (n == cap) { cap *= 2; Note *p = (Note *)realloc(v, cap * sizeof(Note)); if (!p) { free(v); return -1; } v = p; }
        v[n++] = nt;
        s = e;
    }
    *out = v;
    return (int)n;
}

static float midi_hz(float m) { return 440.f * powf(2.f, (m - 69.f) / 12.f); }

int prosody_sing(const PhList *ph, const Note *notes, size_t nnotes, float bpm, const ProsodyParams *pp, SegList *out) {
    if (!nnotes) return -1;
    float beat_ms = 60000.f / (bpm > 10.f ? bpm : 120.f);
    /* split phonemes into syllables: each starts at a PF_SYL_START flag; pauses separate */
    size_t ni = 0;       /* note index */
    float prev_f0 = 0;
    size_t i = 0;
    while (i < ph->n) {
        if (ph->v[i].id == PH_PAUSE) { i++; continue; }
        size_t j = i + 1;
        while (j < ph->n && !(ph->v[j].flags & PF_SYL_START) && ph->v[j].id != PH_PAUSE) j++;
        /* skip rests */
        while (nnotes && notes[ni % nnotes].midi < 0) {
            Seg r = { PH_SIL, 0, notes[ni % nnotes].beats * beat_ms, prev_f0, prev_f0, 0.f };
            if (seg_push(out, &r)) return -1;
            ni++;
        }
        const Note *nt = &notes[ni % nnotes];
        ni++;
        float total = nt->beats * beat_ms * 0.96f;       /* slight gap between notes */
        float f = midi_hz((float)nt->midi);
        /* find nucleus (first non-DIPH2-continued vowel) */
        size_t nuc_a = j, nuc_b = j;
        for (size_t k = i; k < j; k++) if (ph_info(ph->v[k].id)->type == PT_VOWEL) { nuc_a = k; break; }
        if (nuc_a < j) { nuc_b = nuc_a + 1; while (nuc_b < j && (ph->v[nuc_b].flags & PF_DIPH2)) nuc_b++; }
        float cons_ms = 0;
        for (size_t k = i; k < j; k++) if (k < nuc_a || k >= nuc_b) cons_ms += base_dur(&ph->v[k]) * 0.75f;
        float scale = 1.f;
        if (cons_ms > total * 0.45f) scale = total * 0.45f / cons_ms;
        float vowel_ms = total - cons_ms * scale;
        if (vowel_ms < 30.f) vowel_ms = 30.f;
        size_t nvow = nuc_b > nuc_a ? nuc_b - nuc_a : 1;
        float t = 0;
        for (size_t k = i; k < j; k++) {
            const PhItem *it = &ph->v[k];
            int in_nuc = (k >= nuc_a && k < nuc_b);
            float d = in_nuc ? vowel_ms / (float)nvow : base_dur(it) * 0.75f * scale;
            if (in_nuc && nvow > 1 && (it->flags & PF_DIPH2)) d = vowel_ms * 0.25f, (void)0;
            else if (in_nuc && nvow > 1) d = vowel_ms * 0.75f;
            Seg s;
            s.id = it->id; s.flags = it->flags; s.dur_ms = d; s.amp = 1.f;
            float f_here = f;
            if (pp->vibrato > 0.f) { /* delayed vibrato after 150 ms */
                float onset = t > 150.f ? (t - 150.f) / 200.f : 0.f;
                if (onset > 1.f) onset = 1.f;
                float ph_v = 2.f * 3.14159265f * 5.5f * (t + d * 0.5f) / 1000.f;
                f_here = f * powf(2.f, pp->vibrato * onset * sinf(ph_v) / 12.f);
            }
            s.f0a = (k == i && prev_f0 > 0) ? prev_f0 : f_here;
            s.f0b = f_here;
            if (seg_push(out, &s)) return -1;
            t += d;
        }
        prev_f0 = f;
        i = j;
        if (i < ph->n && ph->v[i].id == PH_PAUSE) i++;
    }
    Seg tail = { PH_SIL, 0, 150.f, prev_f0, prev_f0, 0.f };
    return seg_push(out, &tail);
}
