#ifndef RV_COMMON_H
#define RV_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RV_SR_DEFAULT 44100

typedef struct {
    float *data;      /* mono float samples, -1..1 */
    size_t len;
    int sample_rate;
} RvBuf;

/* xorshift32 noise source */
typedef struct { uint32_t s; } RvRng;
static inline void rv_rng_seed(RvRng *r, uint32_t seed) { r->s = seed ? seed : 0x9E3779B9u; }
static inline float rv_rng_white(RvRng *r) {
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return (float)((double)x / 2147483648.0 - 1.0);
}

static inline float rv_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* buffer helpers (wav.c) */
int  rv_buf_alloc(RvBuf *b, size_t len, int sr);
void rv_buf_free(RvBuf *b);
int  rv_buf_grow(RvBuf *b, size_t newlen);
void rv_buf_normalize(RvBuf *b, float peak);

/* WAV I/O (wav.c) */
int rv_wav_read(const char *path, RvBuf *out);
int rv_wav_write(const char *path, const RvBuf *in);

#endif
