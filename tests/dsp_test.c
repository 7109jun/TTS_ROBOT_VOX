#include <stdio.h>
#include <stdlib.h>
#include "../src/dsp.h"
#include "../src/synth.h"
int main(void) {
    int sr = 44100;
    /* 1) pitch shift of a 220 Hz tone by +7 semitones -> expect ~329.6 Hz */
    RvBuf a; rv_buf_alloc(&a, sr * 2, sr);
    for (size_t i = 0; i < a.len; i++) a.data[i] = 0.5f * sinf(2 * M_PI * 220.0 * i / sr) + 0.2f * sinf(2 * M_PI * 440.0 * i / sr);
    RvBuf b; rv_pitch_shift(&a, 7.f, &b);
    float f = rv_yin(b.data + sr / 2, 3000, sr, 70, 800);
    printf("shift +7: expected 329.6 got %.1f (len %zu/%zu)\n", f, b.len, a.len);
    RvBuf c; rv_pitch_shift(&a, -12.f, &c);
    printf("shift -12: expected 110.0 got %.1f\n", rv_yin(c.data + sr / 2, 3000, sr, 70, 800));
    /* 2) yin on tone */
    printf("yin 220: %.2f\n", rv_yin(a.data + 5000, 3000, sr, 70, 800));
    /* 3) vocoder with speech modulator: carrier at 130.8 Hz fixed; check pitch of output voiced part */
    PhList pl; phl_init(&pl); text_to_phonemes("more than ever", 0, &pl);
    ProsodyParams pp; prosody_defaults(&pp); SegList sl; seg_init(&sl); prosody_speech(&pl, &pp, &sl);
    SynthParams sp; synth_defaults(&sp, sr); RvBuf sp_b; synth_render(&sl, &sp, &sp_b);
    VocoderParams vp; rv_vocoder_defaults(&vp); vp.carrier_hz = 130.81f; vp.voices = 1; vp.sub = 0;
    RvBuf v; if (rv_vocoder(&sp_b, &vp, &v)) return 1;
    float mx = 0; for (size_t i = 0; i < v.len; i++) if (fabsf(v.data[i]) > mx) mx = fabsf(v.data[i]);
    printf("vocoder peak %.3f len %zu ; yin %.1f (expect 130.8)\n", mx, v.len, rv_yin(v.data + (size_t)(0.12 * sr), 3000, sr, 70, 800));
    rv_buf_normalize(&v, 0.8f); rv_wav_write("/tmp/voc.wav", &v);
    return 0;
}
