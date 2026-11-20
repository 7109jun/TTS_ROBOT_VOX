/* Effects: channel vocoder, ring modulator, bitcrusher, chorus, delay. */
#include "dsp.h"
#include <stdlib.h>
#include <string.h>

typedef struct { double b0, b1, b2, a1, a2, z1, z2; } Bq;

static void bq_bandpass(Bq *q, double f, double Q, double sr) {
    if (f > 0.45 * sr) f = 0.45 * sr;
    double w = 2.0 * M_PI * f / sr, al = sin(w) / (2.0 * Q), a0 = 1.0 + al;
    q->b0 = al / a0; q->b1 = 0; q->b2 = -al / a0;
    q->a1 = -2.0 * cos(w) / a0; q->a2 = (1.0 - al) / a0;
    q->z1 = q->z2 = 0;
}
static double bq_run(Bq *q, double x) { /* transposed direct form II */
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static double blep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

void rv_vocoder_defaults(VocoderParams *p) {
    p->bands = 24;
    p->f_lo = 120.f; p->f_hi = 9000.f;
    p->q = 7.f;
    p->carrier = VOC_CARRIER_FIXED;
    p->carrier_hz = 110.f;
    p->snap = 1.f;
    p->voices = 3;
    p->detune_cents = 9.f;
    p->sub = 0.5f;
    p->noise_mix = 0.6f;
    p->attack_ms = 2.5f; p->release_ms = 14.f;
    p->gain = 1.f;
    p->nchord = 0; p->chord_level = 0.5f; p->preemph = 0.f; p->hf_boost = 1.f;
}

struct RvVoc {
    VocoderParams p; int sr, nb, nv, hop, win;
    Bq *mb, *cb; double *env, *nmix, *bw;
    double att, rel, ph[7], phs, f_cur, f_hold, norm, phc[4], x1;
    float *ring; int rpos, since; /* causal pitch-analysis ring buffer */
    float *tmp; RvRng rng;
};

RvVoc *rv_voc_create(const VocoderParams *pr, int sr) {
    RvVoc *v = (RvVoc *)calloc(1, sizeof(RvVoc));
    if (!v) return NULL;
    v->p = *pr; v->sr = sr;
    v->nb = pr->bands < 8 ? 8 : (pr->bands > 48 ? 48 : pr->bands);
    v->nv = pr->voices < 1 ? 1 : (pr->voices > 7 ? 7 : pr->voices);
    double fhi = pr->f_hi > 0.45 * sr ? 0.45 * sr : pr->f_hi;
    v->mb = (Bq *)calloc((size_t)v->nb * 2, sizeof(Bq));
    v->cb = (Bq *)calloc((size_t)v->nb * 2, sizeof(Bq));
    v->env = (double *)calloc((size_t)v->nb, sizeof(double));
    v->nmix = (double *)calloc((size_t)v->nb, sizeof(double));
    v->bw = (double *)calloc((size_t)v->nb, sizeof(double));
    v->hop = (int)(sr * 0.010);
    v->win = (int)(sr / 70.f) * 2 + 64;
    v->ring = (float *)calloc((size_t)v->win, sizeof(float));
    v->tmp = (float *)calloc((size_t)v->win, sizeof(float));
    if (!v->mb || !v->cb || !v->env || !v->nmix || !v->bw || !v->ring || !v->tmp) { rv_voc_free(v); return NULL; }
    double ratio = pow(fhi / pr->f_lo, 1.0 / (v->nb - 1));
    for (int b = 0; b < v->nb; b++) {
        double fc = pr->f_lo * pow(ratio, b);
        for (int s = 0; s < 2; s++) { bq_bandpass(&v->mb[b * 2 + s], fc, pr->q, sr); bq_bandpass(&v->cb[b * 2 + s], fc, pr->q, sr); }
        double t = (fc - 2000.0) / 3000.0;
        v->nmix[b] = pr->noise_mix * (t < 0 ? 0 : (t > 1 ? 1 : t));
        v->bw[b] = 1.0 + ((pr->hf_boost > 0 ? pr->hf_boost : 1.0) - 1.0) * (t < 0 ? 0 : (t > 1 ? 1 : t));
    }
    v->att = 1.0 - exp(-1.0 / (sr * pr->attack_ms / 1000.0));
    v->rel = 1.0 - exp(-1.0 / (sr * pr->release_ms / 1000.0));
    v->f_cur = v->f_hold = pr->carrier_hz;
    v->norm = 1.0 / (v->nv + pr->sub + pr->nchord * pr->chord_level);
    rv_rng_seed(&v->rng, 777u);
    return v;
}

void rv_voc_free(RvVoc *v) {
    if (!v) return;
    free(v->mb); free(v->cb); free(v->env); free(v->nmix); free(v->bw); free(v->ring); free(v->tmp); free(v);
}

void rv_voc_process(RvVoc *v, const float *in, float *out, size_t n) {
    const VocoderParams *pr = &v->p;
    for (size_t i = 0; i < n; i++) {
        double x = in[i] - pr->preemph * v->x1;
        v->x1 = in[i];
        if (pr->carrier == VOC_CARRIER_FOLLOW) {
            v->ring[v->rpos] = in[i];
            v->rpos = (v->rpos + 1) % v->win;
            if (++v->since >= v->hop) {
                v->since = 0;
                for (int k = 0; k < v->win; k++) v->tmp[k] = v->ring[(v->rpos + k) % v->win];
                double e = 0;
                for (int k = 0; k < v->win; k += 4) e += (double)v->tmp[k] * v->tmp[k];
                float f = (e / (v->win / 4) > 1e-6) ? rv_yin(v->tmp, v->win, v->sr, 70.f, 800.f) : 0.f;
                if (f > 0.f) {
                    double m = 12.0 * log2((double)f / 110.0), q = floor(m + 0.5);
                    v->f_hold = 110.0 * pow(2.0, (m + (q - m) * pr->snap) / 12.0);
                }
            }
        }
        double ft = pr->carrier == VOC_CARRIER_FOLLOW ? v->f_hold : pr->carrier_hz;
        v->f_cur += (ft - v->f_cur) * 0.0015;
        double saw = 0;
        for (int k = 0; k < v->nv; k++) {
            double cents = pr->detune_cents * (k - (v->nv - 1) * 0.5);
            double f = v->f_cur * pow(2.0, cents / 1200.0), dt = f / v->sr;
            v->ph[k] += dt; if (v->ph[k] >= 1.0) v->ph[k] -= 1.0;
            saw += 2.0 * v->ph[k] - 1.0 - blep(v->ph[k], dt);
        }
        double dts = v->f_cur * 0.5 / v->sr;
        v->phs += dts; if (v->phs >= 1.0) v->phs -= 1.0;
        saw += pr->sub * (2.0 * v->phs - 1.0 - blep(v->phs, dts));
        for (int k = 0; k < pr->nchord && k < 4; k++) {
            double f = v->f_cur * pow(2.0, pr->chord[k] / 12.0 + 4.0 * (k + 1) / 1200.0), dt = f / v->sr;
            v->phc[k] += dt; if (v->phc[k] >= 1.0) v->phc[k] -= 1.0;
            saw += pr->chord_level * (2.0 * v->phc[k] - 1.0 - blep(v->phc[k], dt));
        }
        saw *= v->norm;
        double noise = rv_rng_white(&v->rng), acc = 0;
        for (int b = 0; b < v->nb; b++) {
            double m = bq_run(&v->mb[b * 2 + 1], bq_run(&v->mb[b * 2], x));
            double a = fabs(m);
            v->env[b] += (a - v->env[b]) * (a > v->env[b] ? v->att : v->rel);
            double c = (1.0 - v->nmix[b]) * saw + v->nmix[b] * noise * 0.7;
            c = bq_run(&v->cb[b * 2 + 1], bq_run(&v->cb[b * 2], c));
            acc += c * v->env[b] * v->bw[b];
        }
        out[i] = (float)(acc * pr->gain * 6.0);
    }
}

int rv_vocoder(const RvBuf *mod, const VocoderParams *pr, RvBuf *out) {
    RvVoc *v = rv_voc_create(pr, mod->sample_rate);
    if (!v || rv_buf_alloc(out, mod->len, mod->sample_rate)) { rv_voc_free(v); return -1; }
    rv_voc_process(v, mod->data, out->data, mod->len);
    rv_voc_free(v);
    return 0;
}

void rv_ringmod(RvBuf *b, float freq_hz, float mix) {
    double ph = 0, inc = 2.0 * M_PI * freq_hz / b->sample_rate;
    for (size_t i = 0; i < b->len; i++) {
        double m = sin(ph); ph += inc;
        b->data[i] = (float)(b->data[i] * (1.0 - mix) + b->data[i] * m * mix);
    }
}

void rv_bitcrush(RvBuf *b, int bits, int ds, float mix) {
    if (bits < 2) bits = 2;
    if (bits > 16) bits = 16;
    if (ds < 1) ds = 1;
    float levels = (float)(1 << (bits - 1));
    float held = 0;
    for (size_t i = 0; i < b->len; i++) {
        if (i % (size_t)ds == 0) held = floorf(b->data[i] * levels + 0.5f) / levels;
        b->data[i] = b->data[i] * (1.f - mix) + held * mix;
    }
}

int rv_chorus(RvBuf *b, float depth_ms, float rate_hz, int voices, float mix) {
    if (voices < 1) voices = 1;
    int sr = b->sample_rate;
    float *dry = (float *)malloc(b->len * sizeof(float));
    if (!dry) return -1;
    memcpy(dry, b->data, b->len * sizeof(float));
    double base = sr * 0.018, depth = sr * depth_ms / 1000.0;
    for (size_t i = 0; i < b->len; i++) {
        double wet = 0;
        for (int v = 0; v < voices; v++) {
            double ph = 2.0 * M_PI * (rate_hz * (1.0 + 0.13 * v) * (double)i / sr + (double)v / voices);
            double d = base + depth * (0.5 + 0.5 * sin(ph)) + v * sr * 0.003;
            double pos = (double)i - d;
            long k = (long)floor(pos);
            double fr = pos - k;
            double s0 = (k >= 0 && (size_t)k < b->len) ? dry[k] : 0, s1 = (k + 1 >= 0 && (size_t)(k + 1) < b->len) ? dry[k + 1] : 0;
            wet += s0 + (s1 - s0) * fr;
        }
        b->data[i] = (float)(dry[i] * (1.0 - 0.5 * mix) + wet / voices * mix);
    }
    free(dry);
    return 0;
}

int rv_delay(RvBuf *b, float ms, float feedback, float mix) {
    size_t d = (size_t)(b->sample_rate * ms / 1000.f);
    if (d < 1) return 0;
    if (feedback > 0.95f) feedback = 0.95f;
    size_t tail = d * 5;
    size_t olen = b->len;
    if (rv_buf_grow(b, olen + tail)) return -1;
    float *dl = (float *)calloc(d, sizeof(float));
    if (!dl) return -1;
    size_t w = 0;
    for (size_t i = 0; i < b->len; i++) {
        float in = b->data[i], echo = dl[w];
        dl[w] = in + echo * feedback;
        w = (w + 1) % d;
        b->data[i] = in + echo * mix;
    }
    free(dl);
    return 0;
}

int rv_mix_into(RvBuf *dst, const RvBuf *src, float gain) {
    if (rv_buf_grow(dst, src->len)) return -1;
    for (size_t i = 0; i < src->len; i++) dst->data[i] += src->data[i] * gain;
    return 0;
}

void rv_softclip(RvBuf *b, float drive) {
    float t = tanhf(drive);
    for (size_t i = 0; i < b->len; i++) b->data[i] = tanhf(b->data[i] * drive) / t;
}

void rv_compress(RvBuf *b, float thresh_db, float ratio, float attack_ms, float release_ms) {
    double sr = b->sample_rate, att = exp(-1.0 / (sr * attack_ms / 1000.0)), rel = exp(-1.0 / (sr * release_ms / 1000.0));
    double env = 0, thr = pow(10.0, thresh_db / 20.0);
    for (size_t i = 0; i < b->len; i++) {
        double a = fabs(b->data[i]);
        env = a > env ? att * env + (1 - att) * a : rel * env + (1 - rel) * a;
        double g = 1.0;
        if (env > thr) g = pow(env / thr, 1.0 / ratio - 1.0);
        b->data[i] = (float)(b->data[i] * g);
    }
}
