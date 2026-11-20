#include "rv_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rv_buf_alloc(RvBuf *b, size_t len, int sr) {
    b->data = (float *)calloc(len ? len : 1, sizeof(float));
    b->len = len;
    b->sample_rate = sr;
    return b->data ? 0 : -1;
}

void rv_buf_free(RvBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
}

int rv_buf_grow(RvBuf *b, size_t newlen) {
    if (newlen <= b->len) return 0;
    float *p = (float *)realloc(b->data, newlen * sizeof(float));
    if (!p) return -1;
    memset(p + b->len, 0, (newlen - b->len) * sizeof(float));
    b->data = p;
    b->len = newlen;
    return 0;
}

void rv_buf_normalize(RvBuf *b, float peak) {
    float m = 0.f;
    for (size_t i = 0; i < b->len; i++) {
        float a = fabsf(b->data[i]);
        if (a > m) m = a;
    }
    if (m < 1e-9f) return;
    float g = peak / m;
    for (size_t i = 0; i < b->len; i++) b->data[i] *= g;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int rv_wav_read(const char *path, RvBuf *out) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "WAV open failed: %s\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 44) { fclose(f); fprintf(stderr, "WAV too small\n"); return -1; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    fclose(f);

    if (memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) { free(buf); fprintf(stderr, "not a RIFF/WAVE file\n"); return -1; }

    int fmt_tag = 0, ch = 0, bits = 0, sr = 0;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    size_t pos = 12;
    while (pos + 8 <= (size_t)sz) {
        uint32_t clen = rd32(buf + pos + 4);
        const uint8_t *c = buf + pos + 8;
        if (!memcmp(buf + pos, "fmt ", 4) && pos + 8 + 16 <= (size_t)sz) {
            fmt_tag = rd16(c);
            ch = rd16(c + 2);
            sr = (int)rd32(c + 4);
            bits = rd16(c + 14);
            if (fmt_tag == 0xFFFE && clen >= 26) fmt_tag = rd16(c + 24); /* extensible subformat */
        } else if (!memcmp(buf + pos, "data", 4)) {
            data = c;
            data_len = clen;
            if (pos + 8 + data_len > (size_t)sz) data_len = (size_t)sz - pos - 8;
            break;
        }
        pos += 8 + (size_t)clen + (clen & 1);
    }
    if (!data || ch < 1 || sr <= 0) { free(buf); fprintf(stderr, "invalid WAV structure\n"); return -1; }
    int bytes = bits / 8;
    int is_float = (fmt_tag == 3);
    if (!((fmt_tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (is_float && bits == 32))) {
        free(buf);
        fprintf(stderr, "unsupported WAV format (tag=%d bits=%d)\n", fmt_tag, bits);
        return -1;
    }
    size_t frames = data_len / ((size_t)bytes * (size_t)ch);
    if (rv_buf_alloc(out, frames, sr)) { free(buf); return -1; }
    for (size_t i = 0; i < frames; i++) {
        double acc = 0;
        for (int c2 = 0; c2 < ch; c2++) {
            const uint8_t *p = data + (i * (size_t)ch + (size_t)c2) * (size_t)bytes;
            double v;
            if (is_float) { float fv; memcpy(&fv, p, 4); v = fv; }
            else if (bits == 8) v = ((int)p[0] - 128) / 128.0;
            else if (bits == 16) v = (int16_t)rd16(p) / 32768.0;
            else if (bits == 24) { int32_t s = (int32_t)((p[0] << 8) | (p[1] << 16) | ((uint32_t)p[2] << 24)); v = (s >> 8) / 8388608.0; }
            else v = (int32_t)rd32(p) / 2147483648.0;
            acc += v;
        }
        out->data[i] = (float)(acc / ch);
    }
    free(buf);
    return 0;
}

static void wr32(uint8_t *p, uint32_t v) { p[0] = v & 255; p[1] = (v >> 8) & 255; p[2] = (v >> 16) & 255; p[3] = (v >> 24) & 255; }

int rv_wav_write(const char *path, const RvBuf *in) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return -1; }
    uint32_t dlen = (uint32_t)(in->len * 2);
    uint8_t h[44];
    memcpy(h, "RIFF", 4); wr32(h + 4, 36 + dlen);
    memcpy(h + 8, "WAVEfmt ", 8); wr32(h + 16, 16);
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
    wr32(h + 24, (uint32_t)in->sample_rate);
    wr32(h + 28, (uint32_t)in->sample_rate * 2);
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); wr32(h + 40, dlen);
    fwrite(h, 1, 44, f);
    for (size_t i = 0; i < in->len; i++) {
        float v = rv_clampf(in->data[i], -1.f, 1.f);
        int16_t s = (int16_t)lrintf(v * 32767.f);
        uint8_t b[2] = { (uint8_t)(s & 255), (uint8_t)((s >> 8) & 255) };
        fwrite(b, 1, 2, f);
    }
    fclose(f);
    return 0;
}
