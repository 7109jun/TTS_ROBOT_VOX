/* Text parser: UTF-8 decoding, normalization, tokenization, number reading,
 * then dispatch to the English / Korean rule-based G2P. */
#include "g2p.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LANG_AUTO = 0, LANG_EN = 1, LANG_KO = 2 };
enum { PU_NONE = 0, PU_COMMA, PU_PERIOD, PU_QUESTION, PU_EXCL, PU_COLON, PU_DASH };

typedef struct {
    uint32_t *cp;      /* Hangul code points or lowercase ASCII */
    int n;
    int lang;
    int punct;         /* punctuation that follows this word */
} Token;

typedef struct { Token *v; size_t n, cap; } TokVec;

static int utf8_next(const char **ps, uint32_t *out) {
    const unsigned char *s = (const unsigned char *)*ps;
    if (!*s) return 0;
    uint32_t c = s[0];
    int len = 1;
    if (c >= 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        c = ((c & 7) << 18) | ((s[1] & 63) << 12) | ((s[2] & 63) << 6) | (s[3] & 63); len = 4;
    } else if (c >= 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        c = ((c & 15) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); len = 3;
    } else if (c >= 0xC0 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 31) << 6) | (s[1] & 63); len = 2;
    }
    *out = c;
    *ps += len;
    return 1;
}

static int is_hangul(uint32_t c) { return c >= 0xAC00 && c <= 0xD7A3; }
static int is_alpha(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_digit(uint32_t c) { return c >= '0' && c <= '9'; }

static int tv_push(TokVec *tv, const uint32_t *cp, int n, int lang) {
    if (n <= 0) return 0;
    if (tv->n == tv->cap) {
        size_t nc = tv->cap ? tv->cap * 2 : 64;
        Token *p = (Token *)realloc(tv->v, nc * sizeof(Token));
        if (!p) return -1;
        tv->v = p; tv->cap = nc;
    }
    Token *t = &tv->v[tv->n++];
    t->cp = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
    if (!t->cp) return -1;
    memcpy(t->cp, cp, (size_t)n * sizeof(uint32_t));
    t->n = n; t->lang = lang; t->punct = PU_NONE;
    return 0;
}

static int tv_push_ascii(TokVec *tv, const char *s, int lang) {
    uint32_t tmp[64];
    int n = 0;
    while (s[n] && n < 63) { tmp[n] = (unsigned char)s[n]; n++; }
    return tv_push(tv, tmp, n, lang);
}

static void tv_free(TokVec *tv) {
    for (size_t i = 0; i < tv->n; i++) free(tv->v[i].cp);
    free(tv->v);
}

/* ---------- number reading ---------- */
static const char *EN_ONES[] = {"zero","one","two","three","four","five","six","seven","eight","nine",
    "ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen"};
static const char *EN_TENS[] = {"","","twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety"};

static int en_below_1000(TokVec *tv, unsigned v) {
    if (v >= 100) {
        if (tv_push_ascii(tv, EN_ONES[v / 100], LANG_EN)) return -1;
        if (tv_push_ascii(tv, "hundred", LANG_EN)) return -1;
        v %= 100;
        if (!v) return 0;
    }
    if (v >= 20) {
        if (tv_push_ascii(tv, EN_TENS[v / 10], LANG_EN)) return -1;
        v %= 10;
        if (!v) return 0;
    }
    return tv_push_ascii(tv, EN_ONES[v], LANG_EN);
}

static int en_number(TokVec *tv, const char *digits, int nd) {
    while (nd > 1 && digits[0] == '0') { digits++; nd--; }
    if (nd > 9) { /* too long: digit by digit */
        for (int i = 0; i < nd; i++) { char d[2] = { digits[i], 0 }; if (tv_push_ascii(tv, EN_ONES[d[0] - '0'], LANG_EN)) return -1; }
        return 0;
    }
    unsigned long v = strtoul(digits, NULL, 10);
    if (v == 0) return tv_push_ascii(tv, "zero", LANG_EN);
    static const struct { unsigned long div; const char *name; } sc[] = { {1000000, "million"}, {1000, "thousand"} };
    for (int i = 0; i < 2; i++) {
        if (v >= sc[i].div) {
            if (en_below_1000(tv, (unsigned)(v / sc[i].div))) return -1;
            if (tv_push_ascii(tv, sc[i].name, LANG_EN)) return -1;
            v %= sc[i].div;
        }
    }
    if (v) return en_below_1000(tv, (unsigned)v);
    return 0;
}

static int ko_digit_char(uint32_t *buf, int *n, int d) {
    static const uint32_t D[] = { 0xACF5, 0xC77C, 0xC774, 0xC0BC, 0xC0AC, 0xC624, 0xC721, 0xCE60, 0xD314, 0xAD6C };
    buf[(*n)++] = D[d];
    return 0;
}

/* Sino-Korean reading (일 이 삼 ... 십 백 천 만 억) */
static void ko_number_chars(uint32_t *buf, int *n, const char *digits, int nd) {
    while (nd > 1 && digits[0] == '0') { digits++; nd--; }
    if (nd > 12) { for (int i = 0; i < nd; i++) ko_digit_char(buf, n, digits[i] - '0'); return; }
    if (nd == 1 && digits[0] == '0') { buf[(*n)++] = 0xC601; return; } /* 영 */
    unsigned long long v = strtoull(digits, NULL, 10);
    static const struct { unsigned long long div; uint32_t ch; } big[] = { {100000000ULL, 0xC5B5}, {10000ULL, 0xB9CC} };
    for (int b = 0; b < 3; b++) {
        unsigned long long chunk;
        uint32_t unit = 0;
        if (b < 2) { chunk = v / big[b].div; v %= big[b].div; unit = big[b].ch; }
        else { chunk = v; v = 0; }
        if (!chunk) continue;
        static const uint32_t U[] = { 0, 0xC2ED, 0xBC31, 0xCC9C }; /* 십 백 천 */
        for (int p = 3; p >= 0; p--) {
            unsigned long long pw = p == 3 ? 1000 : p == 2 ? 100 : p == 1 ? 10 : 1;
            int d = (int)((chunk / pw) % 10);
            if (!d) continue;
            if (!(d == 1 && p > 0)) ko_digit_char(buf, n, d); /* 십, not 일십 */
            if (p > 0) buf[(*n)++] = U[p == 1 ? 1 : p == 2 ? 2 : 3];
        }
        if (unit && !(b < 2 && chunk == 1 && unit == 0xB9CC && 0)) buf[(*n)++] = unit;
    }
}

/* ---------- symbol words ---------- */
static int symbol_word(TokVec *tv, uint32_t c, int lang) {
    const char *en = NULL;
    const uint32_t *ko = NULL;
    static const uint32_t K_PCT[] = { 0xD37C, 0xC13C, 0xD2B8 };         /* 퍼센트 */
    static const uint32_t K_AND[] = { 0xC564, 0xB4DC };                 /* 앤드 */
    static const uint32_t K_PLUS[] = { 0xD50C, 0xB7EC, 0xC2A4 };        /* 플러스 */
    static const uint32_t K_EQ[] = { 0xC774, 0xCF5C };                  /* 이콜 */
    static const uint32_t K_AT[] = { 0xC5E3 };                          /* 엣 */
    int kn = 0;
    switch (c) {
        case '%': en = "percent"; ko = K_PCT; kn = 3; break;
        case '&': en = "and"; ko = K_AND; kn = 2; break;
        case '+': en = "plus"; ko = K_PLUS; kn = 3; break;
        case '=': en = "equals"; ko = K_EQ; kn = 2; break;
        case '@': en = "at"; ko = K_AT; kn = 1; break;
        case '$': en = "dollars"; break;
        case '#': en = "number"; break;
        default: return 0;
    }
    if (lang == LANG_KO && ko) tv_push(tv, ko, kn, LANG_KO);
    else if (en) tv_push_ascii(tv, en, LANG_EN);
    return 1;
}

/* ---------- tokenizer ---------- */
static int tokenize(const char *s, int forced, TokVec *tv) {
    /* decode everything first */
    size_t cap = strlen(s) + 1, n = 0;
    uint32_t *cps = (uint32_t *)malloc(cap * sizeof(uint32_t));
    if (!cps) return -1;
    uint32_t c;
    const char *p = s;
    while (utf8_next(&p, &c)) cps[n++] = c;

    size_t i = 0;
    while (i < n) {
        c = cps[i];
        if (is_hangul(c)) {
            size_t j = i;
            while (j < n && is_hangul(cps[j])) j++;
            tv_push(tv, cps + i, (int)(j - i), LANG_KO);
            i = j;
        } else if (is_alpha(c)) {
            uint32_t w[128];
            int wn = 0;
            size_t j = i;
            while (j < n && (is_alpha(cps[j]) || (cps[j] == '\'' && j + 1 < n && is_alpha(cps[j + 1]) && wn > 0))) {
                if (cps[j] != '\'' && wn < 127) w[wn++] = (cps[j] | 0x20);
                j++;
            }
            tv_push(tv, w, wn, LANG_EN);
            i = j;
        } else if (is_digit(c)) {
            char dg[64];
            int nd = 0;
            size_t j = i;
            while (j < n && (is_digit(cps[j]) || (cps[j] == ',' && j + 1 < n && is_digit(cps[j + 1]) && nd > 0))) {
                if (cps[j] != ',' && nd < 63) dg[nd++] = (char)cps[j];
                j++;
            }
            dg[nd] = 0;
            int frac = 0;
            char fd[64];
            if (j + 1 < n && cps[j] == '.' && is_digit(cps[j + 1])) {
                j++;
                while (j < n && is_digit(cps[j]) && frac < 63) fd[frac++] = (char)cps[j++];
                fd[frac] = 0;
            }
            int lang = forced;
            if (lang == LANG_AUTO) {
                int adj = (j < n && is_hangul(cps[j])) || (i > 0 && is_hangul(cps[i - 1])) ||
                          (j + 1 < n && cps[j] == ' ' && is_hangul(cps[j + 1]));
                lang = adj ? LANG_KO : LANG_EN;
            }
            if (lang == LANG_KO) {
                uint32_t buf[160];
                int bn = 0;
                ko_number_chars(buf, &bn, dg, nd);
                if (frac) { buf[bn++] = 0xC810; for (int k = 0; k < frac; k++) ko_digit_char(buf, &bn, fd[k] - '0'); } /* 점 */
                tv_push(tv, buf, bn, LANG_KO);
            } else {
                en_number(tv, dg, nd);
                if (frac) {
                    tv_push_ascii(tv, "point", LANG_EN);
                    for (int k = 0; k < frac; k++) tv_push_ascii(tv, EN_ONES[fd[k] - '0'], LANG_EN);
                }
            }
            i = j;
        } else if (c == '.' || c == '!' || c == '?' || c == ',' || c == ';' || c == ':' || c == 0x3002 || c == 0xFF01 || c == 0xFF1F ||
                   c == '-' || c == 0x2014 || c == 0x2026) {
            int pu = PU_COMMA;
            if (c == '.' || c == 0x3002 || c == 0x2026) pu = PU_PERIOD;
            else if (c == '?' || c == 0xFF1F) pu = PU_QUESTION;
            else if (c == '!' || c == 0xFF01) pu = PU_EXCL;
            else if (c == ':' || c == ';') pu = PU_COLON;
            else if (c == '-' || c == 0x2014) pu = PU_DASH;
            /* a lone '-' between letters is just a word joiner */
            int joiner = (c == '-' && i > 0 && i + 1 < n && is_alpha(cps[i - 1]) && is_alpha(cps[i + 1]));
            if (!joiner && tv->n > 0 && tv->v[tv->n - 1].punct < pu) tv->v[tv->n - 1].punct = pu;
            i++;
        } else {
            int lang = forced ? forced : (i + 1 < n && is_hangul(cps[i + 1]) ? LANG_KO : LANG_EN);
            if (c == '-' ) { i++; continue; }
            symbol_word(tv, c, lang);
            i++;
        }
    }
    free(cps);
    return 0;
}

int text_to_phonemes(const char *utf8, int lang, PhList *out) {
    TokVec tv = {0};
    if (tokenize(utf8, lang, &tv)) { tv_free(&tv); return -1; }
    for (size_t i = 0; i < tv.n; i++) {
        Token *t = &tv.v[i];
        size_t first = out->n;
        if (t->lang == LANG_KO) {
            if (g2p_ko_word(t->cp, t->n, out)) { tv_free(&tv); return -1; }
        } else {
            char w[160];
            int k = 0;
            for (int j = 0; j < t->n && k < 159; j++) w[k++] = (char)t->cp[j];
            w[k] = 0;
            if (g2p_en_word(w, out)) { tv_free(&tv); return -1; }
        }
        if (out->n > first) out->v[out->n - 1].flags |= PF_WORD_END;
        float pause = 0;
        unsigned fl = 0;
        switch (t->punct) {
            case PU_COMMA: pause = 220; break;
            case PU_COLON: pause = 300; break;
            case PU_DASH: pause = 160; break;
            case PU_PERIOD: pause = 420; fl = PF_PHRASE_END; break;
            case PU_EXCL: pause = 420; fl = PF_PHRASE_END; break;
            case PU_QUESTION: pause = 420; fl = PF_PHRASE_END | PF_QUESTION; break;
            default: break;
        }
        if (fl && out->n > first) out->v[out->n - 1].flags |= fl;
        if (t->punct == PU_COMMA && out->n > first) out->v[out->n - 1].flags |= PF_WORD_END; /* continuation rise handled in prosody */
        if (pause > 0) phl_pause(out, pause);
    }
    /* end of text: mark the final phoneme as phrase end if no punctuation did */
    if (out->n > 0) {
        size_t k = out->n;
        while (k > 0 && out->v[k - 1].id == PH_PAUSE) k--;
        if (k > 0) {
            int has = 0;
            for (size_t m = k; m > 0 && m + 0 > 0; m--) { if (out->v[m - 1].flags & PF_PHRASE_END) { has = 1; break; } if (out->v[m - 1].id == PH_PAUSE) break; }
            if (!has && k == out->n) out->v[k - 1].flags |= PF_PHRASE_END;
        }
    }
    tv_free(&tv);
    return 0;
}
