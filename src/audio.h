#ifndef RV_AUDIO_H
#define RV_AUDIO_H
#include <stddef.h>
#include "dsp.h"

/* Mono full-duplex audio device. Linux: ALSA loaded at runtime via dlopen (no link dependency).
 * Windows: winmm waveIn/waveOut. */
typedef struct RvAudio RvAudio;
RvAudio *rv_audio_open(int sr, int block, char *err, size_t errn);
int  rv_audio_read(RvAudio *a, float *buf, int frames);        /* blocking, returns 0 on success */
int  rv_audio_write(RvAudio *a, const float *buf, int frames); /* blocking, returns 0 on success */
void rv_audio_close(RvAudio *a);

/* Real-time effect chain (vocoder, ring mod, bit crush, soft clip). */
typedef struct {
    VocoderParams voc; int use_voc; float dry;
    float ring_hz, ring_mix; int crush_bits, crush_ds; float crush_mix; float drive; float out_gain;
} LiveParams;

typedef struct RvLive RvLive;
RvLive *rv_live_create(const LiveParams *p, int sr);
void rv_live_process(RvLive *l, const float *in, float *out, size_t n);
void rv_live_free(RvLive *l);

/* Run the live pipeline from a microphone until interrupted (Ctrl+C). */
int rv_live_run(const LiveParams *p, int sr, int block);
/* Offline check of the live pipeline: block-wise processing of a WAV, reports real-time factor. */
int rv_live_test(const LiveParams *p, const char *in_wav, const char *out_wav, int block);
#endif
