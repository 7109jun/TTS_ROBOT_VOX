/* English rule-based letter-to-sound conversion.
 * No pronunciation dictionary: context rules over letters, plus a short table
 * of irregular function words (the, of, you ...) that no rule can derive. */
#include "g2p.h"
#include <stdlib.h>
#include <string.h>

#define RED  0x4000u /* vowel may reduce to schwa when unstressed (internal) */
#define WEAK 0x8000u /* weak prefix vowel: stress moves to next syllable (internal) */

typedef struct { PhList *l; size_t start; unsigned word_flags; } Out;

static void emit(Out *o, PhId id, unsigned fl) { phl_push(o->l, id, fl); }
static void diph(Out *o, PhId a, PhId b) { emit(o, a, 0); emit(o, b, PF_DIPH2); }
#define EM(id)  emit(o, (id), 0)
#define EMR(id) emit(o, (id), RED)

/* ---------- irregular words (own phoneme notation, '~' = unstressed) ---------- */
static const struct { const char *w, *p; } IRR[] = {
    {"the","~dh ah"},{"a","~ah"},{"an","~ah n"},{"and","~ae n d"},{"of","~ah v"},{"to","~t uw"},
    {"too","t uw"},{"two","t uw"},{"do","d uw"},{"does","d ah z"},{"is","~ih z"},{"was","~w ah z"},
    {"are","~aa r"},{"were","~w er"},{"be","~b iy"},{"been","~b ih n"},{"you","y uw"},{"your","y ao r"},
    {"i","~ay"},{"my","~m ay"},{"by","~b ay"},{"he","~h iy"},{"she","~sh iy"},{"we","~w iy"},{"me","~m iy"},
    {"they","dh ey"},{"their","dh eh r"},{"there","dh eh r"},{"these","dh iy z"},{"those","dh ow z"},
    {"this","dh ih s"},{"that","dh ae t"},{"than","dh ae n"},{"then","dh eh n"},{"them","dh eh m"},
    {"with","w ih dh"},{"thus","dh ah s"},{"though","dh ow"},{"through","th r uw"},{"thought","th ao t"},
    {"where","w eh r"},{"what","w ah t"},{"who","h uw"},{"whom","h uw m"},{"why","w ay"},{"one","w ah n"},
    {"once","w ah n s"},{"some","s ah m"},{"come","k ah m"},{"done","d ah n"},{"love","l ah v"},
    {"above","ah b ah v"},{"have","h ae v"},{"give","g ih v"},{"live","l ih v"},{"gone","g ao n"},
    {"move","m uw v"},{"lose","l uw z"},{"prove","p r uw v"},{"said","s eh d"},{"says","s eh z"},
    {"people","p iy p ah l"},{"enough","ih n ah f"},{"tough","t ah f"},{"rough","r ah f"},{"laugh","l ae f"},
    {"friend","f r eh n d"},{"great","g r ey t"},{"break","b r ey k"},{"very","v eh r iy"},
    {"every","eh v r iy"},{"again","ah g eh n"},{"get","g eh t"},{"girl","g er l"},{"gift","g ih f t"},
    {"good","g uh d"},{"wood","w uh d"},{"stood","s t uh d"},{"foot","f uh t"},{"would","w uh d"},
    {"could","k uh d"},{"should","sh uh d"},{"put","p uh t"},{"push","p uh sh"},{"pull","p uh l"},
    {"full","f uh l"},{"bull","b uh l"},{"bush","b uh sh"},{"sugar","sh uh g er"},{"sure","sh uh r"},
    {"over","ow v er"},{"hour","aw er"},{"our","aw er"},{"us","ah s"},{"four","f ao r"},{"pour","p ao r"},
    {"young","y ah ng"},{"country","k ah n t r iy"},{"other","ah dh er"},{"mother","m ah dh er"},
    {"brother","b r ah dh er"},{"another","ah n ah dh er"},{"father","f aa dh er"},{"money","m ah n iy"},
    {"honey","h ah n iy"},{"month","m ah n th"},{"none","n ah n"},{"son","s ah n"},{"won","w ah n"},
    {"nothing","n ah th ih ng"},{"wonder","w ah n d er"},{"front","f r ah n t"},{"heart","h aa r t"},
    {"heard","h er d"},{"earth","er th"},{"early","er l iy"},{"learn","l er n"},{"world","w er l d"},
    {"want","w aa n t"},{"water","w ao t er"},{"watch","w aa ch"},{"was","w ah z"},{"its","ih t s"},
    {"it","ih t"},{"dont","d ow n t"},{"cant","k ae n t"},{"wont","w ow n t"},{"im","ay m"},
    {"youre","y ao r"},{"theyre","dh eh r"},{"thats","dh ae t s"},{"lets","l eh t s"},{"ive","ay v"},
    {"ill","ay l"},{"singer","s ih ng er"},{"ginger","jh ih n jh er"},{"robot","r ow b aa t"},
    {"vox","v aa k s"},{"daft","d ae f t"},{"punk","p ah ng k"},{"music","m y uw z ih k"},
    {"voice","v oy s"},{"human","h y uw m ah n"},{"after","ae f t er"},{"ever","eh v er"},{"never","n eh v er"},
    {"harder","h aa r d er"},{"better","b eh t er"},{"faster","f ae s t er"},{"stronger","s t r ao ng g er"},
    {"work","w er k"},{"makes","m ey k s"},{"more","m ao r"},{"hello","h eh l ow"},{"world","w er l d"},
    {"hundred","h ah n d r ah d"},{"thousand","th aw z ah n d"},{"million","m ih l y ah n"},{"yes","y eh s"},{"no","n ow"},{"so","s ow"},{"go","g ow"},{"oh","ow"},{"ok","ow k ey"},{"okay","ow k ey"},
};

static int ph_by_name(const char *s, size_t len) {
    for (int i = 2; i < PH_COUNT; i++) {
        const char *nm = ph_info((PhId)i)->name;
        if (strlen(nm) == len && !strncmp(nm, s, len)) return i;
    }
    return -1;
}

static int try_irregular(const char *w, Out *o) {
    for (size_t k = 0; k < sizeof(IRR) / sizeof(IRR[0]); k++) {
        if (strcmp(IRR[k].w, w)) continue;
        const char *p = IRR[k].p;
        if (*p == '~') { o->word_flags |= 1; p++; }
        while (*p) {
            while (*p == ' ') p++;
            const char *q = p;
            while (*q && *q != ' ') q++;
            size_t len = (size_t)(q - p);
            if (len == 2 && !strncmp(p, "ay", 2)) diph(o, PH_AA, PH_IY);
            else if (len == 2 && !strncmp(p, "aw", 2)) diph(o, PH_AA, PH_UW);
            else if (len == 2 && !strncmp(p, "ey", 2)) diph(o, PH_EH, PH_IY);
            else if (len == 2 && !strncmp(p, "ow", 2)) diph(o, PH_OH, PH_UW);
            else if (len == 2 && !strncmp(p, "oy", 2)) diph(o, PH_AO, PH_IY);
            else if (len == 2 && !strncmp(p, "ch", 2)) { emit(o, PH_T, 0); emit(o, PH_SH, 0); }
            else if (len == 2 && !strncmp(p, "jh", 2)) { emit(o, PH_D, 0); emit(o, PH_ZH, 0); }
            else { int id = ph_by_name(p, len); if (id > 0) emit(o, (PhId)id, 0); }
            p = q;
        }
        return 1;
    }
    return 0;
}

/* ---------- letter rules ---------- */
static int isv(int c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }
static int isc(int c) { return c >= 'a' && c <= 'z' && !isv(c) && c != 'y'; }

typedef struct { const char *w; int n; } Wd;
static int ch(const Wd *d, int i) { return (i >= 0 && i < d->n) ? d->w[i] : 0; }
static int starts(const Wd *d, int i, const char *s) { size_t L = strlen(s); return i >= 0 && (size_t)i + L <= (size_t)d->n && !strncmp(d->w + i, s, L); }
static int ends_at(const Wd *d, const char *s) { size_t L = strlen(s); return (size_t)d->n >= L && !strncmp(d->w + d->n - L, s, L); }

/* vowel at i, one consonant, then final e (or e+s/d) => long vowel */
static int magic(const Wd *d, int i) {
    if (!isc(ch(d, i + 1))) return 0;
    if (ch(d, i + 2) == 'e' && i + 2 == d->n - 1) return 1;
    if (ch(d, i + 2) == 'e' && i + 3 == d->n - 1 && (ch(d, i + 3) == 's' || ch(d, i + 3) == 'd')) return 1;
    return 0;
}

static int vowel_letters(const Wd *d) { int c = 0; for (int i = 0; i < d->n; i++) if (isv(d->w[i]) || d->w[i] == 'y') c++; return c; }

static int weak_prefix(const Wd *d) {
    if (d->n < 5) return 0;
    if (!(d->w[0] == 'b' || d->w[0] == 'd' || d->w[0] == 'r') || d->w[1] != 'e') return 0;
    return isc(ch(d, 2)) && isv(ch(d, 3)) && ch(d, 2) != ch(d, 3);
}

static void rules(const char *word, Out *o) {
    Wd d = { word, (int)strlen(word) };
    int n = d.n, i = 0;
    while (i < n) {
        int c = d.w[i], p = ch(&d, i - 1), x1 = ch(&d, i + 1), x2 = ch(&d, i + 2);
        int dbl = (x1 == c);
        switch (c) {
        case 'a':
            if (x1 == 'l' && (x2 == 'l' || x2 == 'k' || x2 == 't')) { EM(PH_AO); i += 1; }
            else if (x1 == 'u' || x1 == 'w') { EM(PH_AO); i += 2; }
            else if (x1 == 'y' || x1 == 'i') {
                if (x1 == 'i' && x2 == 'r') { EM(PH_EH); EM(PH_R); i += 3; }
                else { diph(o, PH_EH, PH_IY); i += 2; }
            }
            else if (x1 == 'r' && x2 == 'e' && i + 2 == n - 1 && i > 0) { EM(PH_EH); EM(PH_R); i += 3; }
            else if (x1 == 'r') { EM(p == 'w' ? PH_AO : PH_AA); EM(PH_R); i += 2; }
            else if (starts(&d, i + 1, "tion") || starts(&d, i + 1, "nge") || magic(&d, i)) { diph(o, PH_EH, PH_IY); i++; }
            else if (i == n - 1 && n > 1) { emit(o, PH_AH, RED); i++; }
            else if (i == 0 && isc(x1) && isv(x2) && n > 3) { emit(o, PH_AH, RED | WEAK); i++; }
            else { EMR(PH_AE); i++; }
            break;
        case 'e':
            if (i == n - 1 && i > 0 && !isv(p) && vowel_letters(&d) > 1) { i++; break; } /* silent e */
            if (x1 == 'd' && i == n - 2 && i >= 2 && !isv(p)) {
                if (p == 't' || p == 'd') { EM(PH_IH); EM(PH_D); }
                else if (p == 'k' || p == 'p' || p == 's' || p == 'f' || p == 'x' || (p == 'h' && (ch(&d, i - 2) == 'c' || ch(&d, i - 2) == 's'))) EM(PH_T);
                else EM(PH_D);
                i += 2; break;
            }
            if (x1 == 's' && i == n - 2 && i >= 2 && !isv(p)) {
                if (p == 's' || p == 'z' || p == 'x' || p == 'c' || p == 'g' || (p == 'h' && (ch(&d, i - 2) == 'c' || ch(&d, i - 2) == 's'))) { EM(PH_IH); EM(PH_Z); }
                else if (p == 'k' || p == 'p' || p == 't' || p == 'f') EM(PH_S);
                else EM(PH_Z);
                i += 2; break;
            }
            if (x1 == 'e') { EM(PH_IY); i += 2; }
            else if (x1 == 'a') {
                if (x2 == 'r') {
                    int x3 = ch(&d, i + 3);
                    if (x3 == 'n' || x3 == 'l' || x3 == 't' || (x3 == 't' && ch(&d, i + 4) == 'h') || (x3 == 'c' && ch(&d, i + 4) == 'h')) { EM(PH_ER); i += 3; }
                    else { EM(PH_IH); EM(PH_R); i += 3; }
                }
                else if (x2 == 'd' || starts(&d, i + 2, "th") || starts(&d, i + 2, "lth") || starts(&d, i + 2, "dy") || starts(&d, i + 2, "ther") || starts(&d, i + 2, "sure") || (x2 == 'v' && (ch(&d, i + 3) == 'y' || ch(&d, i + 3) == 'e'))) { EM(PH_EH); i += 2; }
                else { EM(PH_IY); i += 2; }
            }
            else if (starts(&d, i, "eigh")) { diph(o, PH_EH, PH_IY); i += 4; }
            else if (x1 == 'i') { EM(PH_IY); i += 2; }
            else if (x1 == 'w') { if (p && strchr("bfhkmpv", p)) EM(PH_Y); EM(PH_UW); i += 2; }
            else if (x1 == 'u') { EM(PH_Y); EM(PH_UW); i += 2; }
            else if (x1 == 'y') { if (i == n - 2) { if (vowel_letters(&d) > 2) EM(PH_IY); else diph(o, PH_EH, PH_IY); } else diph(o, PH_EH, PH_IY); i += 2; }
            else if (x1 == 'r' && x2 == 'e' && i + 2 == n - 1) { EM(PH_IH); EM(PH_R); i += 3; }
            else if (x1 == 'r') { EM(PH_ER); i += 2; }
            else if (magic(&d, i)) { EM(PH_IY); i++; }
            else if (i == n - 1) { EM(PH_IY); i++; }
            else if (i == 0) { emit(o, PH_IH, RED); i++; }
            else if (i == 1 && weak_prefix(&d)) { emit(o, PH_IH, RED | WEAK); i++; }
            else { EMR(PH_EH); i++; }
            break;
        case 'i':
            if (starts(&d, i, "igh")) { diph(o, PH_AA, PH_IY); i += 3; }
            else if (x1 == 'e') {
                if (i + 1 == n - 1) diph(o, PH_AA, PH_IY); else EM(PH_IY);
                i += 2;
            }
            else if (x1 == 'r' && x2 == 'e' && i + 2 == n - 1) { diph(o, PH_AA, PH_IY); EM(PH_ER); i += 3; }
            else if (x1 == 'r') { EM(PH_ER); i += 2; }
            else if ((x1 == 'l' && x2 == 'd') || (x1 == 'n' && x2 == 'd' && i + 3 == n) || starts(&d, i, "ign") || magic(&d, i)) { diph(o, PH_AA, PH_IY); i++; }
            else if (isv(x1) && i > 0) { EM((p == 'l' || p == 'n') ? PH_Y : PH_IY); i++; }
            else if (i == n - 1 && n > 1) { EM(PH_IY); i++; }
            else { EMR(PH_IH); i++; }
            break;
        case 'o':
            if (x1 == 'o') {
                if (x2 == 'k') { EM(PH_UH); i += 2; }
                else if (x2 == 'r') { EM(PH_AO); EM(PH_R); i += 3; }
                else { EM(PH_UW); i += 2; }
            }
            else if (x1 == 'u') {
                if (x2 == 'r') {
                    int x3 = ch(&d, i + 3);
                    if (x3 == 't' || starts(&d, i + 3, "se")) { EM(PH_AO); EM(PH_R); }
                    else { diph(o, PH_AA, PH_UW); EM(PH_ER); }
                    i += 3;
                }
                else if (x2 == 'l' && ch(&d, i + 3) == 'd') { EM(PH_UH); i += 3; }
                else if (starts(&d, i, "ough")) { EM(PH_AO); i += 4; }
                else if (starts(&d, i, "ous") && i + 3 == n) { emit(o, PH_AH, RED); EM(PH_S); i += 3; }
                else { diph(o, PH_AA, PH_UW); i += 2; }
            }
            else if (x1 == 'w') {
                if (i + 2 == n) {
                    if (n <= 3 && p && strchr("nhcwv", p)) diph(o, PH_AA, PH_UW);
                    else diph(o, PH_OH, PH_UW);
                }
                else if (x2 == 'l' || x2 == 'd' || starts(&d, i + 2, "er")) { diph(o, PH_AA, PH_UW); }
                else if (ends_at(&d, "down") || ends_at(&d, "town") || ends_at(&d, "gown") || ends_at(&d, "brown") ||
                         ends_at(&d, "crown") || ends_at(&d, "clown") || ends_at(&d, "drown") || ends_at(&d, "frown")) diph(o, PH_AA, PH_UW);
                else diph(o, PH_OH, PH_UW);
                i += 2;
            }
            else if (x1 == 'y' || x1 == 'i') { diph(o, PH_AO, PH_IY); i += 2; }
            else if (x1 == 'a') { diph(o, PH_OH, PH_UW); i += 2; }
            else if (x1 == 'r') {
                if (p == 'w') { EM(PH_ER); i += 2; }
                else if (x2 == 'e' && i + 2 == n - 1) { EM(PH_AO); EM(PH_R); i += 3; }
                else { EM(PH_AO); EM(PH_R); i += 2; }
            }
            else if (x1 == 'l' && (x2 == 'd' || x2 == 't' || x2 == 'k')) { diph(o, PH_OH, PH_UW); i++; }
            else if (starts(&d, i, "ost") && i + 3 == n) { diph(o, PH_OH, PH_UW); i++; }
            else if (x1 == 'n' && x2 == 'g') { EM(PH_AO); i++; }
            else if (starts(&d, i + 1, "tion") || magic(&d, i)) { diph(o, PH_OH, PH_UW); i++; }
            else if (i == n - 1 && n > 1) { diph(o, PH_OH, PH_UW); i++; }
            else { EMR(PH_AA); i++; }
            break;
        case 'u':
            if (x1 == 'e' && i + 1 == n - 1) { EM(PH_UW); i += 2; }
            else if (x1 == 'i') { EM(PH_UW); i += 2; }
            else if (x1 == 'r' && x2 == 'e' && i + 2 == n - 1) { EM(PH_Y); EM(PH_ER); i += 3; }
            else if (x1 == 'r') { EM(PH_ER); i += 2; }
            else if (x1 == 'y') { diph(o, PH_AA, PH_IY); i += 2; }
            else if (magic(&d, i)) { if (p && strchr("cbhmfpkv", p)) EM(PH_Y); EM(PH_UW); i++; }
            else { EMR(PH_AH); i++; }
            break;
        case 'y':
            if (i == 0) { EM(PH_Y); i++; }
            else if (x1 == 'i' && x2 == 'n') { diph(o, PH_AA, PH_IY); i++; }
            else if (i == n - 1) { if (vowel_letters(&d) == 1) diph(o, PH_AA, PH_IY); else EM(PH_IY); i++; }
            else if (magic(&d, i)) { diph(o, PH_AA, PH_IY); i++; }
            else { EMR(PH_IH); i++; }
            break;

        case 'b': if (p == 'm' && i == n - 1) { i++; break; } EM(PH_B); i += dbl ? 2 : 1; break;
        case 'c':
            if (x1 == 'h') { EM(PH_T); EM(PH_SH); i += 2; }
            else if (x1 == 'k') { EM(PH_K); i += 2; }
            else if (x1 == 'i' && x2 == 'a' && ch(&d, i + 3) == 'l') { EM(PH_SH); emit(o, PH_AH, RED); EM(PH_L); i += 4; }
            else if (x1 == 'e' || x1 == 'i' || x1 == 'y') { EM(PH_S); i++; }
            else { EM(PH_K); i += dbl ? 2 : 1; }
            break;
        case 'd':
            if (x1 == 'g') { EM(PH_D); EM(PH_ZH); i += 2; }
            else { EM(PH_D); i += dbl ? 2 : 1; }
            break;
        case 'f': EM(PH_F); i += dbl ? 2 : 1; break;
        case 'g':
            if (x1 == 'h') {
                if (i == 0) { EM(PH_G); i += 2; } else i += 2; /* silent after vowel: night, though */
            }
            else if (x1 == 'n' && (i == 0 || i + 2 == n)) { i++; } /* gnome, sign */
            else if (x1 == 'u' && isv(x2)) { EM(PH_G); i += 2; }
            else if (x1 == 'e' || x1 == 'i' || x1 == 'y') { EM(PH_D); EM(PH_ZH); i++; }
            else { EM(PH_G); i += dbl ? 2 : 1; }
            break;
        case 'h':
            if (isv(p) && (i == n - 1 || !isv(x1))) { i++; break; } /* ah, oh */
            EM(PH_HH); i++; break;
        case 'j': EM(PH_D); EM(PH_ZH); i++; break;
        case 'k': if (i == 0 && x1 == 'n') { i++; break; } EM(PH_K); i += dbl ? 2 : 1; break;
        case 'l':
            if (i == n - 2 && x1 == 'e' && isc(p) && p != 'l') { emit(o, PH_AH, RED); EM(PH_L); i += 2; break; } /* -ble, -tle */
            if ((p == 'a' || p == 'o') && (x1 == 'k' || x1 == 'm' || (x1 == 'f' && p == 'a'))) { i++; break; } /* walk, calm, half */
            EM(PH_L); i += dbl ? 2 : 1; break;
        case 'm': EM(PH_M); i += dbl ? 2 : 1; break;
        case 'n':
            if (x1 == 'g') {
                if (x2 == 'e' && (ch(&d, i + 3) == 0 || ch(&d, i + 3) == 's' || ch(&d, i + 3) == 'd') && i + 2 >= n - 2) { EM(PH_N); EM(PH_D); EM(PH_ZH); i += 3; }
                else if (x2 == 'e' && ch(&d, i + 3) == 'r') {
                    if (p == 'a') { EM(PH_N); EM(PH_D); EM(PH_ZH); i += 3; } else { EM(PH_NG); EM(PH_G); i += 3; }
                }
                else { EM(PH_NG); i += 2; }
            }
            else if (x1 == 'k') { EM(PH_NG); EM(PH_K); i += 2; }
            else { EM(PH_N); i += dbl ? 2 : 1; }
            break;
        case 'p':
            if (x1 == 'h') { EM(PH_F); i += 2; } else { EM(PH_P); i += dbl ? 2 : 1; }
            break;
        case 'q': EM(PH_K); if (x1 == 'u') { EM(PH_W); i += 2; } else i++; break;
        case 'r': if (i == 0 && x1 == 'h') { i++; break; } EM(PH_R); i += dbl ? 2 : 1; break;
        case 's':
            if (x1 == 'h') { EM(PH_SH); i += 2; }
            else if (starts(&d, i, "sion")) { EM(isv(p) ? PH_ZH : PH_SH); emit(o, PH_AH, RED); EM(PH_N); i += 4; }
            else if (starts(&d, i, "sure") && i + 3 == n - 1) { EM(PH_ZH); EM(PH_ER); i += 4; }
            else if (x1 == 's') { EM(PH_S); i += 2; }
            else if (i == n - 1 && i > 0 && (isv(p) || (p && strchr("bdglmnrvyz", p))) && p != 's') { EM(PH_Z); i++; }
            else if (isv(p) && isv(x1) && i > 0) { EM(PH_Z); i++; }
            else if (x1 == 'e' && i + 1 == n - 1 && isv(p)) { EM(PH_Z); i += 2; }
            else { EM(PH_S); i++; }
            break;
        case 't':
            if (starts(&d, i, "tion")) { if (p == 's') { EM(PH_T); EM(PH_SH); } else EM(PH_SH); emit(o, PH_AH, RED); EM(PH_N); i += 4; }
            else if (starts(&d, i, "tial")) { EM(PH_SH); emit(o, PH_AH, RED); EM(PH_L); i += 4; }
            else if (starts(&d, i, "ture") && i + 3 == n - 1) { EM(PH_T); EM(PH_SH); EM(PH_ER); i += 4; }
            else if (starts(&d, i, "tch")) { EM(PH_T); EM(PH_SH); i += 3; }
            else if (x1 == 'h') {
                if ((x2 == 'e' && (ch(&d, i + 3) == 'r' || i + 2 == n - 1)) && isv(p)) EM(PH_DH); else EM(PH_TH);
                i += 2;
            }
            else { EM(PH_T); i += dbl ? 2 : 1; }
            break;
        case 'v': EM(PH_V); i += dbl ? 2 : 1; break;
        case 'w':
            if (i == 0 && x1 == 'r') { i++; break; }
            if (x1 == 'h') { EM(PH_W); i += 2; break; }
            EM(PH_W); i++; break;
        case 'x':
            if (i == 0) EM(PH_Z);
            else if (starts(&d, i - 1, "ex") && isv(x1)) { EM(PH_G); EM(PH_Z); }
            else { EM(PH_K); EM(PH_S); }
            i++; break;
        case 'z': EM(PH_Z); i += dbl ? 2 : 1; break;
        default: i++; break;
        }
    }
}

/* ---------- stress, reduction, syllable marking ---------- */
static int is_vowel_ph(const PhItem *it) { return ph_info(it->id)->type == PT_VOWEL && !(it->flags & PF_DIPH2); }

static int onset_pair_ok(PhId a, PhId b) {
    PhType ta = ph_info(a)->type;
    int liq = (b == PH_L || b == PH_R || b == PH_W || b == PH_Y);
    if (liq && (ta == PT_VSTOP || ta == PT_USTOP || ta == PT_UFRIC || ta == PT_VFRIC) && a != PH_HH) return 1;
    if (a == PH_S && (b == PH_P || b == PH_T || b == PH_K || b == PH_M || b == PH_N || b == PH_L || b == PH_W)) return 1;
    return 0;
}

static void finish_word(Out *o) {
    PhList *l = o->l;
    size_t s = o->start, e = l->n;
    if (e <= s) return;
    size_t vi[64];
    int nv = 0;
    for (size_t k = s; k < e && nv < 64; k++) if (is_vowel_ph(&l->v[k])) vi[nv++] = k;
    if (nv > 0) {
        int stress = -1;
        if (!(o->word_flags & 1)) {
            stress = 0;
            if (nv >= 2 && (l->v[vi[0]].flags & WEAK)) stress = 1;
            l->v[vi[stress]].flags |= PF_STRESS1;
        }
        for (int j = 0; j < nv; j++) {
            PhItem *it = &l->v[vi[j]];
            if (nv >= 2 && j != stress && (it->flags & RED)) {
                if (it->id == PH_AE || it->id == PH_EH || it->id == PH_AA || it->id == PH_AO || it->id == PH_UH) it->id = PH_AH;
            }
        }
    }
    for (size_t k = s; k < e; k++) l->v[k].flags &= ~(RED | WEAK);
    l->v[s].flags |= PF_SYL_START;
    for (int j = 1; j < nv; j++) {
        size_t nuc = vi[j];
        size_t prev_end = vi[j - 1] + 1;
        while (prev_end < e && (l->v[prev_end].flags & PF_DIPH2)) prev_end++;
        size_t start = nuc;
        if (nuc > prev_end && ph_info(l->v[nuc - 1].id)->type != PT_VOWEL) {
            start = nuc - 1;
            if (start > prev_end + 0 && onset_pair_ok(l->v[start - 1].id, l->v[start].id)) {
                start--;
                if (start > prev_end && l->v[start].id != PH_SIL && l->v[start - 1].id == PH_S && ph_info(l->v[start].id)->type == PT_USTOP) start--;
            }
        }
        l->v[start].flags |= PF_SYL_START;
    }
}

int g2p_en_word(const char *w, PhList *out) {
    Out o = { out, out->n, 0 };
    if (!try_irregular(w, &o)) rules(w, &o);
    finish_word(&o);
    return 0;
}
