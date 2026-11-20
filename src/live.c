/* Real-time (block based, causal) pipeline + driver loops. */
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

struct RvLive {
    LiveParams p; int sr; RvVoc *voc; double ring_ph; size_t crush_i; float held;
    float *tmp; size_t tmp_n;
};

RvLive *rv_live_create(const LiveParams *p, int sr) {
    RvLive *l = (RvLive *)calloc(1, sizeof(RvLive));
    if (!l) return NULL;
    l->p = *p; l->sr = sr;
    if (p->use_voc) { l->voc = rv_voc_create(&p->voc, sr); if (!l->voc) { free(l); return NULL; } }
    return l;
}

void rv_live_free(RvLive *l) { if (!l) return; rv_voc_free(l->voc); free(l->tmp); free(l); }

void rv_live_process(RvLive *l, const float *in, float *out, size_t n) {
    const LiveParams *p = &l->p;
    if (l->tmp_n < n) { free(l->tmp); l->tmp = (float *)malloc(n * sizeof(float)); l->tmp_n = l->tmp ? n : 0; if (!l->tmp) return; }
    if (l->voc) {
        rv_voc_process(l->voc, in, l->tmp, n);
        for (size_t i = 0; i < n; i++) out[i] = l->tmp[i] * 0.35f + p->dry * in[i];
    } else for (size_t i = 0; i < n; i++) out[i] = in[i];
    double inc = 2.0 * M_PI * p->ring_hz / l->sr;
    float lv = p->crush_bits < 16 ? (float)(1 << (p->crush_bits - 1)) : 0.f;
    float t = p->drive > 1.01f ? tanhf(p->drive) : 1.f;
    for (size_t i = 0; i < n; i++) {
        float y = out[i];
        if (p->ring_hz > 0.f) { y = y * (1.f - p->ring_mix) + y * (float)sin(l->ring_ph) * p->ring_mix; l->ring_ph += inc; if (l->ring_ph > 2 * M_PI) l->ring_ph -= 2 * M_PI; }
        if (lv > 0.f || p->crush_ds > 1) {
            if (l->crush_i++ % (size_t)(p->crush_ds > 1 ? p->crush_ds : 1) == 0) l->held = lv > 0.f ? floorf(y * lv + 0.5f) / lv : y;
            y = y * (1.f - p->crush_mix) + l->held * p->crush_mix;
        }
        if (p->drive > 1.01f) y = tanhf(y * p->drive) / t;
        out[i] = rv_clampf(y * p->out_gain, -1.f, 1.f);
    }
}

static volatile sig_atomic_t g_stop;
static void on_sig(int s) { (void)s; g_stop = 1; }

int rv_live_run(const LiveParams *p, int sr, int block) {
    char err[256] = "";
    RvAudio *a = rv_audio_open(sr, block, err, sizeof err);
    if (!a) { fprintf(stderr, "audio device error: %s\n", err); return 1; }
    RvLive *l = rv_live_create(p, sr);
    float *in = (float *)malloc((size_t)block * sizeof(float)), *out = (float *)malloc((size_t)block * sizeof(float));
    if (!l || !in || !out) { fprintf(stderr, "out of memory\n"); return 1; }
    signal(SIGINT, on_sig);
    fprintf(stderr, "live: %d Hz, block %d (%.1f ms). Use headphones to avoid feedback. Ctrl+C to stop.\n", sr, block, 1000.0 * block / sr);
    int rc = 0;
    while (!g_stop) {
        if (rv_audio_read(a, in, block)) { rc = 1; break; }
        rv_live_process(l, in, out, (size_t)block);
        if (rv_audio_write(a, out, block)) { rc = 1; break; }
    }
    rv_audio_close(a); rv_live_free(l); free(in); free(out);
    return rc;
}

int rv_live_test(const LiveParams *p, const char *in_wav, const char *out_wav, int block) {
    RvBuf b;
    if (rv_wav_read(in_wav, &b)) return 1;
    RvLive *l = rv_live_create(p, b.sample_rate);
    if (!l) return 1;
    RvBuf o; rv_buf_alloc(&o, b.len, b.sample_rate);
    clock_t t0 = clock();
    for (size_t i = 0; i < b.len; i += (size_t)block) {
        size_t n = b.len - i < (size_t)block ? b.len - i : (size_t)block;
        rv_live_process(l, b.data + i, o.data + i, n);
    }
    double cpu = (double)(clock() - t0) / CLOCKS_PER_SEC, dur = (double)b.len / b.sample_rate;
    printf("live pipeline: %.2f s audio in %.3f s CPU -> %.1fx real time (block %d = %.1f ms)\n", dur, cpu, dur / cpu, block, 1000.0 * block / b.sample_rate);
    rv_buf_normalize(&o, 0.9f);
    int rc = rv_wav_write(out_wav, &o);
    rv_live_free(l); rv_buf_free(&b); rv_buf_free(&o);
    return rc;
}
