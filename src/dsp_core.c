/* FFT, YIN pitch tracker, phase-vocoder pitch shifter (own implementations). */
#include "dsp.h"
#include <stdlib.h>
#include <string.h>

void rv_fft(double *re, double *im, int n, int inverse) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * M_PI / len * (inverse ? 1 : -1);
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                double xr = re[b] * cr - im[b] * ci;
                double xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr; im[a] += xi;
                double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
    if (inverse) for (int i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

/* ---------- YIN ---------- */
float rv_yin(const float *x, int n, int sr, float fmin, float fmax) {
    int tmin = (int)(sr / fmax), tmax = (int)(sr / fmin);
    int W = n - tmax;
    if (W < tmax / 2 || tmin < 2) return 0.f;
    double *d = (double *)malloc((size_t)(tmax + 2) * sizeof(double));
    if (!d) return 0.f;
    d[0] = 1.0;
    double run = 0;
    for (int t = 1; t <= tmax; t++) {
        double s = 0;
        for (int j = 0; j < W; j++) { double df = (double)x[j] - x[j + t]; s += df * df; }
        run += s;
        d[t] = run > 0 ? s * t / run : 1.0; /* cumulative mean normalized difference */
    }
    int best = -1;
    for (int t = tmin; t < tmax; t++) {
        if (d[t] < 0.15) {
            while (t + 1 < tmax && d[t + 1] < d[t]) t++;
            best = t; break;
        }
    }
    if (best < 0) {
        double m = 1e9; int mi = -1;
        for (int t = tmin; t < tmax; t++) if (d[t] < m) { m = d[t]; mi = t; }
        if (m < 0.25) best = mi;
    }
    float res = 0.f;
    if (best > 0 && best + 1 <= tmax) {
        double a = d[best - 1], b = d[best], c = d[best + 1];
        double den = a + c - 2 * b;
        double sh = den != 0 ? 0.5 * (a - c) / den : 0;
        res = (float)(sr / (best + sh));
    }
    free(d);
    return res;
}

int rv_pitch_track(const RvBuf *in, float hop_ms, float **f0, size_t *nframes) {
    int sr = in->sample_rate;
    int hop = (int)(sr * hop_ms / 1000.f);
    if (hop < 1) hop = 1;
    int tmax = (int)(sr / 70.f);
    int win = tmax * 2 + 64;
    size_t nf = in->len > (size_t)win ? (in->len - (size_t)win) / (size_t)hop + 1 : 0;
    float *v = (float *)calloc(nf ? nf : 1, sizeof(float));
    if (!v) return -1;
    for (size_t k = 0; k < nf; k++) {
        const float *p = in->data + k * (size_t)hop;
        double e = 0;
        for (int j = 0; j < win; j += 4) e += (double)p[j] * p[j];
        v[k] = (e / (win / 4) > 1e-6) ? rv_yin(p, win, sr, 70.f, 800.f) : 0.f;
    }
    *f0 = v; *nframes = nf;
    return 0;
}

/* ---------- phase vocoder pitch shifter ---------- */
static double wrap_pi(double x) {
    while (x > M_PI) x -= 2 * M_PI;
    while (x < -M_PI) x += 2 * M_PI;
    return x;
}

static float cubic(const float *s, size_t len, double pos) {
    long i = (long)floor(pos);
    double f = pos - i;
    double p0 = (i - 1 >= 0 && (size_t)(i - 1) < len) ? s[i - 1] : 0;
    double p1 = (i >= 0 && (size_t)i < len) ? s[i] : 0;
    double p2 = (i + 1 >= 0 && (size_t)(i + 1) < len) ? s[i + 1] : 0;
    double p3 = (i + 2 >= 0 && (size_t)(i + 2) < len) ? s[i + 2] : 0;
    return (float)(p1 + 0.5 * f * (p2 - p0 + f * (2 * p0 - 5 * p1 + 4 * p2 - p3 + f * (3 * (p1 - p2) + p3 - p0))));
}

int rv_pitch_shift(const RvBuf *in, float semitones, RvBuf *out) {
    if (rv_buf_alloc(out, in->len, in->sample_rate)) return -1;
    if (fabsf(semitones) < 1e-3f || in->len == 0) { memcpy(out->data, in->data, in->len * sizeof(float)); return 0; }
    const int N = 2048, Ha = 512, nb = N / 2 + 1;
    double r = pow(2.0, semitones / 12.0);
    int Hs = (int)floor(Ha * r + 0.5);
    if (Hs < 1) Hs = 1;
    double alpha = (double)Hs / Ha;
    size_t padded = in->len + 2 * (size_t)N;
    size_t nfr = (padded - N) / (size_t)Ha + 1;
    size_t slen = (nfr - 1) * (size_t)Hs + (size_t)N;
    float *st = (float *)calloc(slen, sizeof(float));
    float *ws = (float *)calloc(slen, sizeof(float));
    double *re = (double *)malloc(N * sizeof(double)), *im = (double *)malloc(N * sizeof(double));
    double *win = (double *)malloc(N * sizeof(double));
    double *mag = (double *)malloc(nb * sizeof(double)), *ph = (double *)malloc(nb * sizeof(double));
    double *pph = (double *)calloc(nb, sizeof(double)), *syn = (double *)calloc(nb, sizeof(double));
    int *pk = (int *)malloc(nb * sizeof(int));
    if (!st || !ws || !re || !im || !win || !mag || !ph || !pph || !syn || !pk) {
        free(st); free(ws); free(re); free(im); free(win); free(mag); free(ph); free(pph); free(syn); free(pk);
        rv_buf_free(out); return -1;
    }
    for (int i = 0; i < N; i++) win[i] = 0.5 - 0.5 * cos(2.0 * M_PI * i / N);
    for (size_t m = 0; m < nfr; m++) {
        for (int i = 0; i < N; i++) {
            long q = (long)(m * (size_t)Ha) + i - N; /* padded -> original index */
            double s = (q >= 0 && (size_t)q < in->len) ? in->data[q] : 0.0;
            re[i] = s * win[i]; im[i] = 0;
        }
        rv_fft(re, im, N, 0);
        for (int k = 0; k < nb; k++) { mag[k] = hypot(re[k], im[k]); ph[k] = atan2(im[k], re[k]); }
        if (m == 0) { for (int k = 0; k < nb; k++) syn[k] = ph[k]; }
        else {
            for (int k = 0; k < nb; k++) {
                double om = 2.0 * M_PI * k / N;
                double dphi = wrap_pi(ph[k] - pph[k] - om * Ha);
                syn[k] += Hs * (om + dphi / Ha);
            }
        }
        /* identity phase locking around spectral peaks */
        int np = 0;
        for (int k = 2; k < nb - 2; k++)
            if (mag[k] > mag[k - 1] && mag[k] > mag[k - 2] && mag[k] >= mag[k + 1] && mag[k] >= mag[k + 2]) pk[np++] = k;
        if (np > 0) {
            int pi = 0;
            for (int k = 0; k < nb; k++) {
                while (pi + 1 < np && abs(pk[pi + 1] - k) < abs(pk[pi] - k)) pi++;
                int p = pk[pi];
                if (k != p) syn[k] = syn[p] + (ph[k] - ph[p]);
            }
        }
        memcpy(pph, ph, nb * sizeof(double));
        for (int k = 0; k < nb; k++) { re[k] = mag[k] * cos(syn[k]); im[k] = mag[k] * sin(syn[k]); }
        for (int k = 1; k < N / 2; k++) { re[N - k] = re[k]; im[N - k] = -im[k]; }
        im[0] = 0; im[N / 2] = 0;
        rv_fft(re, im, N, 1);
        size_t o = m * (size_t)Hs;
        for (int i = 0; i < N; i++) { st[o + i] += (float)(re[i] * win[i]); ws[o + i] += (float)(win[i] * win[i]); }
    }
    for (size_t i = 0; i < slen; i++) if (ws[i] > 1e-4f) st[i] /= ws[i];
    for (size_t n = 0; n < in->len; n++) out->data[n] = cubic(st, slen, ((double)n + N / 2.0) * alpha + N / 2.0);
    free(st); free(ws); free(re); free(im); free(win); free(mag); free(ph); free(pph); free(syn); free(pk);
    return 0;
}
