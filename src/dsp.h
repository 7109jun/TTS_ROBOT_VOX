#ifndef RV_DSP_H
#define RV_DSP_H

#include "rv_common.h"

/* ---- core (dsp_core.c) ---- */
void  rv_fft(double *re, double *im, int n, int inverse);       /* in-place radix-2, n power of 2 */
float rv_yin(const float *x, int n, int sr, float fmin, float fmax); /* f0 in Hz or 0 if unvoiced */
int   rv_pitch_track(const RvBuf *in, float hop_ms, float **f0, size_t *nframes);
int   rv_pitch_shift(const RvBuf *in, float semitones, RvBuf *out);   /* phase vocoder, duration preserved */

/* ---- effects (dsp_fx.c) ---- */
typedef enum { VOC_CARRIER_FIXED = 0, VOC_CARRIER_FOLLOW = 1 } VocCarrier;

typedef struct {
    int   bands;          /* analysis/synthesis bands (8..48) */
    float f_lo, f_hi;     /* band range in Hz */
    float q;              /* band-pass Q */
    VocCarrier carrier;
    float carrier_hz;     /* fixed pitch (Hz) for VOC_CARRIER_FIXED */
    float snap;           /* FOLLOW: 0..1 semitone quantization of tracked pitch */
    int   voices;         /* detuned sawtooth voices (1..7) */
    float detune_cents;
    float sub;            /* sub-octave level 0..1 */
    float noise_mix;      /* HF noise carrier for consonants 0..1 */
    float attack_ms, release_ms;
    float gain;
    int   nchord; float chord[4]; /* extra carrier notes (semitones above the root), e.g. 7, 12 */
    float chord_level;    /* level of each chord note 0..1 */
    float hf_boost;       /* gain for bands above ~2.5 kHz (consonant presence), 1 = neutral */
    float preemph;        /* modulator high-frequency tilt 0..0.95 (consonant clarity) */
} VocoderParams;

void rv_vocoder_defaults(VocoderParams *p);
int  rv_vocoder(const RvBuf *mod, const VocoderParams *p, RvBuf *out);

/* streaming vocoder (causal, ~40 ms pitch-analysis latency in FOLLOW mode) */
typedef struct RvVoc RvVoc;
RvVoc *rv_voc_create(const VocoderParams *p, int sr);
void   rv_voc_process(RvVoc *v, const float *in, float *out, size_t n);
void   rv_voc_free(RvVoc *v);

void rv_ringmod(RvBuf *b, float freq_hz, float mix);
void rv_bitcrush(RvBuf *b, int bits, int downsample, float mix);
int  rv_chorus(RvBuf *b, float depth_ms, float rate_hz, int voices, float mix);
int  rv_delay(RvBuf *b, float ms, float feedback, float mix);
int  rv_mix_into(RvBuf *dst, const RvBuf *src, float gain);      /* dst += src*gain (grows dst) */
void rv_softclip(RvBuf *b, float drive);
void rv_compress(RvBuf *b, float thresh_db, float ratio, float attack_ms, float release_ms);

#endif
