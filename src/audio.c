#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#define NBUF 4
struct RvAudio {
    HWAVEIN in; HWAVEOUT out; HANDLE ev_in, ev_out; int block;
    WAVEHDR hin[NBUF], hout[NBUF]; short *din[NBUF], *dout[NBUF]; int rin, wout;
};
RvAudio *rv_audio_open(int sr, int block, char *err, size_t errn) {
    RvAudio *a = (RvAudio *)calloc(1, sizeof(*a));
    WAVEFORMATEX f = {WAVE_FORMAT_PCM, 1, (DWORD)sr, (DWORD)sr * 2, 2, 16, 0};
    a->block = block;
    a->ev_in = CreateEvent(NULL, FALSE, FALSE, NULL); a->ev_out = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (waveInOpen(&a->in, WAVE_MAPPER, &f, (DWORD_PTR)a->ev_in, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { snprintf(err, errn, "waveInOpen failed (no microphone?)"); free(a); return NULL; }
    if (waveOutOpen(&a->out, WAVE_MAPPER, &f, (DWORD_PTR)a->ev_out, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { snprintf(err, errn, "waveOutOpen failed"); free(a); return NULL; }
    for (int i = 0; i < NBUF; i++) {
        a->din[i] = (short *)calloc((size_t)block, 2); a->dout[i] = (short *)calloc((size_t)block, 2);
        a->hin[i].lpData = (LPSTR)a->din[i]; a->hin[i].dwBufferLength = (DWORD)block * 2;
        waveInPrepareHeader(a->in, &a->hin[i], sizeof(WAVEHDR)); waveInAddBuffer(a->in, &a->hin[i], sizeof(WAVEHDR));
        a->hout[i].lpData = (LPSTR)a->dout[i]; a->hout[i].dwBufferLength = (DWORD)block * 2;
        waveOutPrepareHeader(a->out, &a->hout[i], sizeof(WAVEHDR)); a->hout[i].dwFlags |= WHDR_DONE;
    }
    waveInStart(a->in);
    return a;
}
int rv_audio_read(RvAudio *a, float *buf, int frames) {
    WAVEHDR *h = &a->hin[a->rin];
    while (!(h->dwFlags & WHDR_DONE)) WaitForSingleObject(a->ev_in, 100);
    for (int i = 0; i < frames && i < a->block; i++) buf[i] = a->din[a->rin][i] / 32768.f;
    h->dwFlags &= ~WHDR_DONE; waveInAddBuffer(a->in, h, sizeof(WAVEHDR));
    a->rin = (a->rin + 1) % NBUF; return 0;
}
int rv_audio_write(RvAudio *a, const float *buf, int frames) {
    WAVEHDR *h = &a->hout[a->wout];
    while (!(h->dwFlags & WHDR_DONE)) WaitForSingleObject(a->ev_out, 100);
    for (int i = 0; i < frames && i < a->block; i++) a->dout[a->wout][i] = (short)(rv_clampf(buf[i], -1.f, 1.f) * 32767.f);
    h->dwFlags &= ~WHDR_DONE; waveOutWrite(a->out, h, sizeof(WAVEHDR));
    a->wout = (a->wout + 1) % NBUF; return 0;
}
void rv_audio_close(RvAudio *a) { if (!a) return; waveInStop(a->in); waveInClose(a->in); waveOutClose(a->out); free(a); }
#else
#include <dlfcn.h>
struct RvAudio {
    void *lib, *cap, *play; int block;
    int (*open)(void **, const char *, int, int);
    int (*set_params)(void *, int, int, unsigned, unsigned, int, unsigned);
    long (*readi)(void *, void *, unsigned long);
    long (*writei)(void *, const void *, unsigned long);
    int (*recover)(void *, int, int);
    int (*close)(void *);
};
#define SND_FLOAT_LE 14
#define SND_RW_INTERLEAVED 3
RvAudio *rv_audio_open(int sr, int block, char *err, size_t errn) {
    RvAudio *a = (RvAudio *)calloc(1, sizeof(*a));
    a->block = block;
    a->lib = dlopen("libasound.so.2", RTLD_NOW);
    if (!a->lib) { snprintf(err, errn, "libasound.so.2 not found (install ALSA runtime)"); free(a); return NULL; }
    *(void **)&a->open = dlsym(a->lib, "snd_pcm_open");
    *(void **)&a->set_params = dlsym(a->lib, "snd_pcm_set_params");
    *(void **)&a->readi = dlsym(a->lib, "snd_pcm_readi");
    *(void **)&a->writei = dlsym(a->lib, "snd_pcm_writei");
    *(void **)&a->recover = dlsym(a->lib, "snd_pcm_recover");
    *(void **)&a->close = dlsym(a->lib, "snd_pcm_close");
    if (!a->open || !a->set_params || !a->readi || !a->writei || !a->recover || !a->close) { snprintf(err, errn, "ALSA symbols missing"); free(a); return NULL; }
    unsigned lat = (unsigned)(block * 1000000LL / sr * 3);
    if (a->open(&a->cap, "default", 1, 0) < 0 || a->set_params(a->cap, SND_FLOAT_LE, SND_RW_INTERLEAVED, 1, (unsigned)sr, 1, lat) < 0) { snprintf(err, errn, "cannot open ALSA capture device \"default\""); free(a); return NULL; }
    if (a->open(&a->play, "default", 0, 0) < 0 || a->set_params(a->play, SND_FLOAT_LE, SND_RW_INTERLEAVED, 1, (unsigned)sr, 1, lat) < 0) { snprintf(err, errn, "cannot open ALSA playback device \"default\""); free(a); return NULL; }
    return a;
}
int rv_audio_read(RvAudio *a, float *buf, int frames) {
    int got = 0;
    while (got < frames) {
        long r = a->readi(a->cap, buf + got, (unsigned long)(frames - got));
        if (r < 0) { if (a->recover(a->cap, (int)r, 1) < 0) return -1; continue; }
        got += (int)r;
    }
    return 0;
}
int rv_audio_write(RvAudio *a, const float *buf, int frames) {
    int put = 0;
    while (put < frames) {
        long r = a->writei(a->play, buf + put, (unsigned long)(frames - put));
        if (r < 0) { if (a->recover(a->play, (int)r, 1) < 0) return -1; continue; }
        put += (int)r;
    }
    return 0;
}
void rv_audio_close(RvAudio *a) { if (!a) return; a->close(a->cap); a->close(a->play); dlclose(a->lib); free(a); }
#endif
