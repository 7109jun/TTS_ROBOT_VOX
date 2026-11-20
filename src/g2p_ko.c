/* Korean rule-based pronunciation: syllable decomposition, basic phonological
 * rules (liaison, coda neutralization, nasalization, liquidization,
 * tensification, aspiration, palatalization) and phoneme emission. */
#include "g2p.h"
#include <stdlib.h>

/* consonant indices follow the Unicode initial order:
 * 0 g 1 gg 2 n 3 d 4 dd 5 r 6 m 7 b 8 bb 9 s 10 ss 11 (null/ng) 12 j 13 jj 14 ch 15 k 16 t 17 p 18 h */
typedef struct { int on, v, c1, c2; } Syl;

static const signed char FIN1[28] = {-1, 0,1,0,2,2,2,3,5,5,5,5,5,5,5,5,6,7,7,9,10,11,12,14,15,16,17,18};
static const signed char FIN2[28] = {-1,-1,-1,9,-1,12,18,-1,-1,0,6,7,9,16,17,18,-1,-1,9,-1,-1,-1,-1,-1,-1,-1,-1,-1};

static int rep_class(int c) {
    switch (c) {
        case 0: case 1: case 15: return 0;
        case 3: case 9: case 10: case 12: case 14: case 16: case 18: return 3;
        case 7: case 17: return 7;
        default: return c;
    }
}

static void reduce_cluster(Syl *a, int next_on) {
    if (a->c2 < 0) return;
    int c1 = a->c1, c2 = a->c2, keep = c1;
    if (c1 == 5) {
        if (c2 == 0) keep = (next_on == 0) ? 5 : 0;
        else if (c2 == 6) keep = 6;
        else if (c2 == 17) keep = 17;
        else keep = 5;
    }
    a->c1 = keep;
    a->c2 = -1;
}

static void apply_rules(Syl *s, int n) {
    for (int i = 0; i + 1 < n; i++) {
        Syl *a = &s[i], *b = &s[i + 1];
        if (a->c1 < 0) continue;
        if (b->on == 11) { /* liaison */
            int moved = -1;
            if (a->c2 >= 0) {
                if (a->c2 == 18) { moved = a->c1; a->c1 = -1; a->c2 = -1; }
                else { moved = a->c2; a->c2 = -1; }
            } else if (a->c1 == 18) {
                a->c1 = -1;
            } else if (a->c1 != 11) {
                moved = a->c1; a->c1 = -1;
            }
            if (moved >= 0) {
                if (b->v == 20) { if (moved == 3) moved = 12; else if (moved == 16) moved = 14; }
                b->on = moved;
            }
            continue;
        }
        int on = b->on;
        int has_h = (a->c2 == 18) || (a->c1 == 18 && a->c2 < 0);
        if (has_h) {
            int handled = 1;
            if (on == 0) b->on = 15; else if (on == 3) b->on = 16; else if (on == 12) b->on = 14;
            else if (on == 9) b->on = 10; else handled = 0;
            if (a->c2 == 18) a->c2 = -1;
            else if (handled) { a->c1 = -1; a->c2 = -1; }
            else a->c1 = 3; /* ㅎ before sonorants: unreleased ㄷ-like closure */
            if (a->c1 < 0) continue;
            on = b->on;
        }
        int tense_after = (a->c1 == 5 && a->c2 == 0 && on == 0); /* ㄺ + ㄱ -> ㄹ + ㄲ */
        reduce_cluster(a, on);
        int rep = rep_class(a->c1);
        if (on == 18 && (rep == 0 || rep == 3 || rep == 7)) {
            b->on = rep == 0 ? 15 : rep == 3 ? 16 : 17;
            a->c1 = -1;
            continue;
        }
        a->c1 = rep;
        if (tense_after) { b->on = 1; continue; }
        if ((rep == 0 || rep == 3 || rep == 7) && (on == 2 || on == 6)) {
            a->c1 = rep == 0 ? 11 : rep == 3 ? 2 : 6;
        } else if ((rep == 6 || rep == 11) && on == 5) {
            b->on = 2;
        } else if ((rep == 0 || rep == 7) && on == 5) {
            a->c1 = rep == 0 ? 11 : 6; b->on = 2;
        } else if (rep == 3 && on == 5) {
            a->c1 = 2; b->on = 2;
        } else if (rep == 2 && on == 5) {
            a->c1 = 5;
        } else if (rep == 5 && on == 2) {
            b->on = 5;
        } else if (rep == 0 || rep == 3 || rep == 7) {
            if (on == 0) b->on = 1; else if (on == 3) b->on = 4; else if (on == 7) b->on = 8;
            else if (on == 9) b->on = 10; else if (on == 12) b->on = 13;
        }
    }
    /* word-final syllable */
    Syl *l = &s[n - 1];
    if (l->c2 >= 0) { if (l->c2 == 18) l->c2 = -1; else reduce_cluster(l, -1); }
    if (l->c1 == 18) l->c1 = -1;
    if (l->c1 >= 0) l->c1 = rep_class(l->c1);
}

typedef struct { PhList *o; unsigned pend; int prev_voiced; } Ctx;

static int voiced_type(PhId id) {
    PhType t = ph_info(id)->type;
    return t == PT_VOWEL || t == PT_GLIDE || t == PT_NASAL || t == PT_VFRIC || t == PT_VSTOP;
}

static int put(Ctx *c, PhId id, unsigned flags) {
    flags |= c->pend;
    c->pend = 0;
    c->prev_voiced = voiced_type(id);
    return phl_push(c->o, id, flags);
}

static int is_y_vowel(int v) { return v == 2 || v == 3 || v == 6 || v == 7 || v == 12 || v == 17 || v == 16; }

static int emit_onset(Ctx *c, int on, int v) {
    int voiced = c->prev_voiced;
    int pal = is_y_vowel(v) || v == 20;
    switch (on) {
        case 0:  return put(c, voiced ? PH_G : PH_K, 0);
        case 1:  return put(c, PH_K, PF_TENSE);
        case 2:  return put(c, PH_N, 0);
        case 3:  return put(c, voiced ? PH_D : PH_T, 0);
        case 4:  return put(c, PH_T, PF_TENSE);
        case 5:  return put(c, PH_L, voiced ? PF_TENSE : 0); /* intervocalic: short flap-like */
        case 6:  return put(c, PH_M, 0);
        case 7:  return put(c, voiced ? PH_B : PH_P, 0);
        case 8:  return put(c, PH_P, PF_TENSE);
        case 9:  return put(c, pal ? PH_SH : PH_S, 0);
        case 10: return put(c, pal ? PH_SH : PH_S, PF_TENSE);
        case 11: return 0;
        case 12: return voiced ? (put(c, PH_D, 0) || put(c, PH_ZH, 0)) : (put(c, PH_T, 0) || put(c, PH_SH, 0));
        case 13: return put(c, PH_T, PF_TENSE) || put(c, PH_SH, PF_TENSE);
        case 14: return put(c, PH_T, PF_ASPIRATED) || put(c, PH_SH, PF_ASPIRATED);
        case 15: return put(c, PH_K, PF_ASPIRATED);
        case 16: return put(c, PH_T, PF_ASPIRATED);
        case 17: return put(c, PH_P, PF_ASPIRATED);
        case 18: return put(c, PH_HH, 0);
        default: return 0;
    }
}

static int emit_vowel(Ctx *c, int v, unsigned vf) {
    switch (v) {
        case 0:  return put(c, PH_AA, vf);
        case 1: case 5: return put(c, PH_EH, vf);
        case 2:  return put(c, PH_Y, 0) || put(c, PH_AA, vf);
        case 3: case 7: return put(c, PH_Y, 0) || put(c, PH_EH, vf);
        case 4:  return put(c, PH_AO, vf);
        case 6:  return put(c, PH_Y, 0) || put(c, PH_AO, vf);
        case 8:  return put(c, PH_OH, vf);
        case 9:  return put(c, PH_W, 0) || put(c, PH_AA, vf);
        case 10: case 11: case 15: return put(c, PH_W, 0) || put(c, PH_EH, vf);
        case 12: return put(c, PH_Y, 0) || put(c, PH_OH, vf);
        case 13: return put(c, PH_UW, vf);
        case 14: return put(c, PH_W, 0) || put(c, PH_AO, vf);
        case 16: return put(c, PH_W, 0) || put(c, PH_IY, vf);
        case 17: return put(c, PH_Y, 0) || put(c, PH_UW, vf);
        case 18: return put(c, PH_EU, vf);
        case 19: return put(c, PH_EU, vf) || put(c, PH_IY, PF_DIPH2);
        default: return put(c, PH_IY, vf);
    }
}

static int emit_coda(Ctx *c, int k) {
    switch (k) {
        case 0: return put(c, PH_K, PF_UNRELEASED);
        case 3: return put(c, PH_T, PF_UNRELEASED);
        case 7: return put(c, PH_P, PF_UNRELEASED);
        case 2: return put(c, PH_N, 0);
        case 5: return put(c, PH_L, 0);
        case 6: return put(c, PH_M, 0);
        case 11: return put(c, PH_NG, 0);
        default: return 0;
    }
}

int g2p_ko_word(const uint32_t *cps, int n, PhList *out) {
    if (n <= 0) return 0;
    Syl *s = (Syl *)malloc((size_t)n * sizeof(Syl));
    if (!s) return -1;
    for (int i = 0; i < n; i++) {
        uint32_t code = cps[i] - 0xAC00;
        int t = (int)(code % 28);
        s[i].on = (int)(code / 588);
        s[i].v = (int)((code % 588) / 28);
        s[i].c1 = FIN1[t];
        s[i].c2 = FIN2[t];
    }
    apply_rules(s, n);
    Ctx c = { out, 0, 0 };
    int rc = 0;
    for (int i = 0; i < n && !rc; i++) {
        c.pend = PF_SYL_START;
        rc |= emit_onset(&c, s[i].on, s[i].v);
        rc |= emit_vowel(&c, s[i].v, i == 0 ? PF_STRESS2 : 0);
        if (s[i].c1 >= 0) rc |= emit_coda(&c, s[i].c1);
    }
    free(s);
    return rc ? -1 : 0;
}
