/* Klatt-style formant synthesizer: glottal pulse + noise sources driving a
 * cascade of formant resonators (vowels, glides, nasals, voiced consonants) and
 * a parallel band-pass noise path (frication, stop bursts). All math, no samples. */
#include "synth.h"
#include <stdlib.h>
#include <string.h>

#define G_AV 1.0
#define G_AH 0.55
#define G_AF 1.6

void synth_defaults(SynthParams *p, int sr) {
    p->sr = sr;
    p->formant_shift = 1.0f;
    p->rise = 0.42f;
    p->fall = 0.13f;
    p->breathiness = 0.05f;
    p->jitter = 0.0f;
    p->tau_ms = 9.0f;
    p->consonant = 1.f;
    p->effort = 0.f;
    p->seed = 12345u;
}

typedef struct { double a, b, c, y1, y2; } Res;
typedef struct { double a, b, c, x1, x2; } Anti;

static void res_set(Res *r, double f, double bw, double sr) {
    if (f < 20) f = 20;
    if (f > 0.45 * sr) f = 0.45 * sr;
    if (bw < 10) bw = 10;
    r->c = -exp(-2.0 * M_PI * bw / sr);
    r->b = 2.0 * exp(-M_PI * bw / sr) * cos(2.0 * M_PI * f / sr);
    r->a = 1.0 - r->b - r->c;
}
static double res_run(Res *r, double x) {
    double y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1; r->y1 = y;
    return y;
}
static void anti_set(Anti *z, double f, double bw, double sr) {
    Res t;
    res_set(&t, f, bw, sr);
    z->a = 1.0 / t.a; z->b = -t.b / t.a; z->c = -t.c / t.a;
}
static double anti_run(Anti *z, double x) {
    double y = z->a * x + z->b * z->x1 + z->c * z->x2;
    z->x2 = z->x1; z->x1 = x;
    return y;
}

typedef struct {
    float f0, av, ah, af;
    float F[3], B[3];
    float fc, bw, nz;
} Frame;

typedef struct { int has; float F[3], B[3]; } Form;

static int voiced_ph(const Seg *s) {
    if (!s) return 0;
    PhType t = ph_info(s->id)->type;
    return t == PT_VOWEL || t == PT_GLIDE || t == PT_NASAL || t == PT_VFRIC || t == PT_VSTOP;
}

static Form form_of(const PhInfo *pi) {
    Form f = {1, {pi->f1, pi->f2, pi->f3}, {pi->b1, pi->b2, pi->b3}};
    return f;
}

static Form locus_of(PhId id) {
    PhId v = id == PH_P ? PH_B : id == PH_T ? PH_D : id == PH_K ? PH_G : id;
    return form_of(ph_info(v));
}

/* formants the stop/aspiration should glide toward: the next voiced segment */
static Form next_form(const SegList *sl, size_t si, PhId fallback) {
    if (si + 1 < sl->n) {
        const PhInfo *ni = ph_info(sl->v[si + 1].id);
        if (ni->type == PT_VOWEL || ni->type == PT_GLIDE || ni->type == PT_NASAL || ni->type == PT_VFRIC)
            return form_of(ni);
    }
    return locus_of(fallback);
}

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

int synth_render(const SegList *sl, const SynthParams *sp, RvBuf *out) {
    const double sr = sp->sr;
    const float cons = sp->consonant > 0.f ? sp->consonant : 1.f;
    const double eff = sp->effort < -1.f ? -1.0 : (sp->effort > 1.f ? 1.0 : sp->effort);
    const int FR = (int)(sr / 1000.0 + 0.5); /* samples per 1 ms frame */
    double total = 0;
    double *start = (double *)malloc((sl->n + 1) * sizeof(double));
    if (!start) return -1;
    for (size_t i = 0; i < sl->n; i++) { start[i] = total; total += sl->v[i].dur_ms; }
    start[sl->n] = total;
    size_t nframes = (size_t)total + 60;
    Frame *fr = (Frame *)calloc(nframes + 1, sizeof(Frame));
    if (!fr) { free(start); return -1; }

    /* ---- pass 1: per-millisecond control frames ---- */
    float cur[3] = {500, 1500, 2500}, curB[3] = {80, 100, 150};
    float av_c = 0, ah_c = 0;
    const float a_form = 1.f - expf(-1.f / sp->tau_ms);
    const float a_amp = 1.f - expf(-1.f / 3.f);
    float last_f0 = sl->n ? sl->v[0].f0a : 110.f;
    size_t si = 0;
    for (size_t k = 0; k < nframes; k++) {
        double t = (double)k;
        Frame *f = &fr[k];
        float av = 0, ah = 0, af = 0, fc = 0, bw = 1000, nz = 0;
        Form tf = {0, {0}, {0}};
        float f0 = last_f0;
        if (sl->n && t < total) {
            while (si + 1 < sl->n && t >= start[si + 1]) si++;
            const Seg *s = &sl->v[si];
            const PhInfo *pi = ph_info(s->id);
            float d = s->dur_ms > 1.f ? s->dur_ms : 1.f;
            float lt = (float)(t - start[si]);
            f0 = s->f0a + (s->f0b - s->f0a) * clamp01(lt / d);
            float amp = s->amp * pi->amp;
            const Seg *prev = si > 0 ? &sl->v[si - 1] : NULL;
            const Seg *next = si + 1 < sl->n ? &sl->v[si + 1] : NULL;
            float edge = 1.f;
            if (!voiced_ph(prev)) edge *= clamp01(lt / 8.f);
            if (!voiced_ph(next)) edge *= clamp01((d - lt) / 10.f);
            fc = pi->noise_fc; bw = pi->noise_bw;
            switch (pi->type) {
            case PT_VOWEL: case PT_GLIDE:
                tf = form_of(pi); av = amp * edge; break;
            case PT_NASAL:
                tf = form_of(pi); av = amp * edge; nz = pi->nasal_zero; break;
            case PT_VFRIC:
                tf = form_of(pi); av = amp * 0.5f * edge; af = amp * 0.7f * edge * cons; break;
            case PT_UFRIC:
                if (s->id == PH_HH) { tf = next_form(sl, si, PH_HH); if (!tf.has || ph_info(PH_HH)->f1 == 0) { /* hold */ } ah = amp * 1.4f * edge * cons; }
                else af = amp * edge * cons;
                break;
            case PT_VSTOP: {
                float cl = d * 0.65f, bl = 9.f;
                if (lt < cl) { tf = locus_of(s->id); av = voiced_ph(prev) ? 0.15f * s->amp : 0.f; }
                else if (lt < cl + bl) { tf = next_form(sl, si, s->id); af = amp * cons * (1.f - 0.6f * (lt - cl) / bl); av = 0.3f * s->amp; }
                else { tf = next_form(sl, si, s->id); av = 0.5f * s->amp * clamp01((lt - cl - bl) / 4.f + 0.4f); }
                break;
            }
            case PT_USTOP: {
                unsigned fl = s->flags;
                float asp = (fl & PF_ASPIRATED) ? 55.f : ((fl & PF_TENSE) ? 4.f : 22.f);
                float bl = (fl & PF_UNRELEASED) ? 0.f : ((fl & PF_ASPIRATED) ? 14.f : 9.f);
                float cl = d - bl - asp;
                if (fl & PF_UNRELEASED) { cl = d; asp = 0; }
                if (cl < 12.f) cl = 12.f;
                float gain = (fl & PF_TENSE) ? 1.25f : 1.f;
                if (lt < cl) { tf = locus_of(s->id); }
                else if (lt < cl + bl) { tf = next_form(sl, si, s->id); af = amp * gain * cons * (1.f - 0.5f * (lt - cl) / bl); }
                else { tf = next_form(sl, si, s->id); ah = amp * 1.1f * cons * (1.f - 0.5f * clamp01((lt - cl - bl) / (asp > 1 ? asp : 1))); }
                break;
            }
            default: break;
            }
        }
        if (tf.has) {
            for (int j = 0; j < 3; j++) { cur[j] += (tf.F[j] - cur[j]) * a_form; curB[j] += (tf.B[j] - curB[j]) * a_form; }
        }
        av_c += (av - av_c) * a_amp;
        ah_c += (ah - ah_c) * a_amp;
        f->f0 = f0; last_f0 = f0;
        f->av = av_c; f->ah = ah_c; f->af = af;
        for (int j = 0; j < 3; j++) { f->F[j] = cur[j]; f->B[j] = curB[j]; }
        f->fc = fc; f->bw = bw; f->nz = nz;
    }
    fr[nframes] = fr[nframes - 1];

    /* ---- pass 2: sample-rate synthesis ---- */
    size_t nsamp = nframes * (size_t)FR;
    if (rv_buf_alloc(out, nsamp, sp->sr)) { free(start); free(fr); return -1; }
    RvRng rng; rv_rng_seed(&rng, sp->seed);
    Res r1 = {0}, r2 = {0}, r3 = {0}, r4 = {0}, r5 = {0}, rf = {0};
    Anti az = {0};
    const double sh = sp->formant_shift;
    res_set(&r4, 3300.0 * sh, 250, sr);
    res_set(&r5, 3750.0 * sh, 200, sr);
    double ph = 0, gprev = 0, per_mod = 1.0, nzmix = 0;
    double rise = sp->rise - 0.04 * eff, fall = sp->fall / (1.0 + 0.7 * eff);
    if (fall < 0.05) fall = 0.05;
    if (fall > 0.25) fall = 0.25;
    const double f1_scale = 1.0 + 0.06 * eff, bw_scale = 1.0 - 0.15 * eff;
    const double breath = sp->breathiness * (1.0 - 0.6 * eff > 0 ? 1.0 - 0.6 * eff : 0.0);
    const double tilt = eff > 0 ? 0.45 * eff : 0.0; /* high-frequency emphasis of the voiced output */
    double yprev = 0;
    const double norm = (M_PI / (2.0 * fall)); /* peak slope of the closing phase */
    size_t pos = 0;
    for (size_t k = 0; k < nframes; k++) {
        const Frame *a = &fr[k], *b = &fr[k + 1];
        res_set(&r1, a->F[0] * sh * f1_scale, a->B[0] * bw_scale, sr);
        res_set(&r2, a->F[1] * sh, a->B[1] * bw_scale, sr);
        res_set(&r3, a->F[2] * sh, a->B[2] * bw_scale, sr);
        if (a->fc > 0) res_set(&rf, a->fc, a->bw, sr);
        if (a->nz > 0) anti_set(&az, a->nz, 150, sr);
        for (int j = 0; j < FR; j++, pos++) {
            double u = (double)j / FR;
            double f0 = a->f0 + (b->f0 - a->f0) * u;
            if (f0 < 50) f0 = 50;
            if (f0 > 1000) f0 = 1000;
            double av = a->av + (b->av - a->av) * u;
            double ah = a->ah + (b->ah - a->ah) * u;
            double af = a->af + (b->af - a->af) * u;
            ph += f0 * per_mod / sr;
            if (ph >= 1.0) { ph -= 1.0; per_mod = 1.0 + sp->jitter * rv_rng_white(&rng); }
            double g;
            if (ph < rise) g = 0.5 * (1.0 - cos(M_PI * ph / rise));
            else if (ph < rise + fall) g = cos(M_PI * (ph - rise) / (2.0 * fall));
            else g = 0.0;
            double dg = (g - gprev) * (sr / f0) / norm;
            gprev = g;
            double noise = rv_rng_white(&rng);
            double x = dg * av * G_AV + noise * (ah + breath * av * 0.3) * G_AH;
            double y = res_run(&r1, x);
            y = res_run(&r2, y);
            y = res_run(&r3, y);
            y = res_run(&r4, y);
            y = res_run(&r5, y);
            { double yy = y; y = yy + tilt * (yy - yprev); yprev = yy; }
            double want = a->nz > 0 ? 1.0 : 0.0;
            nzmix += (want - nzmix) * 0.002;
            if (nzmix > 0.001) y = y * (1.0 - nzmix) + anti_run(&az, y) * nzmix * 0.5;
            double frc = (af > 1e-4 && a->fc > 0) ? res_run(&rf, noise * af * G_AF) : (rf.y1 = rf.y2 = 0.0);
            out->data[pos] = (float)(y * 0.12 + frc * 0.10);
        }
    }
    free(start);
    free(fr);
    return 0;
}
