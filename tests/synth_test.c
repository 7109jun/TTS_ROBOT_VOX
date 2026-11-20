#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/synth.h"
#include "../src/phoneme.h"
int main(int argc, char **argv) {
    const char *text = argc > 1 ? argv[1] : "harder better faster stronger";
    const char *outp = argc > 2 ? argv[2] : "/tmp/t.wav";
    PhList pl; phl_init(&pl);
    text_to_phonemes(text, 0, &pl);
    ProsodyParams pp; prosody_defaults(&pp);
    SegList sl; seg_init(&sl);
    if (prosody_speech(&pl, &pp, &sl)) return 1;
    SynthParams sp; synth_defaults(&sp, 44100);
    RvBuf b;
    if (synth_render(&sl, &sp, &b)) return 2;
    rv_buf_normalize(&b, 0.8f);
    rv_wav_write(outp, &b);
    printf("%zu samples, %.2fs\n", b.len, (double)b.len / b.sample_rate);
    return 0;
}
