/* ROBOT VOX - command line front end.
 *   robotvox speak   "text"  -o out.wav [options]
 *   robotvox sing    "lyrics" --melody "C4:1 E4:1 G4:2" --bpm 120 -o out.wav
 *   robotvox convert in.wav  -o out.wav [options]
 *   robotvox phonemes "text"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "synth.h"
#include "dsp.h"
#include "audio.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

typedef struct {
    const char *out, *text, *in, *melody, *preset;
    int lang, sr;
    float f0, speed, range, robot, stress, bpm, vibrato, pshift, formant, breath, jitter;
    int vocoder, follow, voices, bands;
    float carrier_hz, snap, detune, sub, noise, dry;
    float harm[4]; int nharm; float harm_gain;
    float ring_hz, ring_mix;
    int crush_bits, crush_ds; float crush_mix;
    float chorus_mix, delay_ms, delay_fb, delay_mix, drive;
    unsigned seed;
    int block; const char *test_in;
    int key; unsigned scale_mask; float grid; float chord[4]; int nchord; float chord_level, preemph, comp, voc_q, consonant, power;
} Opts;

static void defaults(Opts *o) {
    memset(o, 0, sizeof(*o));
    o->sr = RV_SR_DEFAULT; o->f0 = 110.f; o->speed = 1.f; o->range = 0.5f; o->robot = 0.6f; o->stress = 1.f;
    o->bpm = 120.f; o->formant = 1.f; o->breath = 0.05f; o->seed = 12345u;
    o->bands = 24; o->voices = 3; o->detune = 9.f; o->sub = 0.5f; o->noise = 0.6f; o->dry = 0.1f; o->snap = 1.f;
    o->block = 512; o->key = 9; o->chord_level = 0.5f; o->voc_q = 7.f; o->consonant = 1.f; o->power = 1.f; o->carrier_hz = 110.f; o->harm_gain = 0.45f; o->crush_bits = 16; o->crush_ds = 1; o->drive = 1.f;
}

static int apply_preset(Opts *o, const char *name) {
    if (!strcmp(name, "clean")) { return 0; }
    if (!strcmp(name, "harder") || !strcmp(name, "daft")) { /* French-house vocoder vocal: A minor pentatonic, 123 BPM grid */
        o->robot = 1.f; o->range = 0.7f; o->stress = 1.6f; o->f0 = 110.f; o->speed = 1.f;
        o->key = 9; o->scale_mask = 0x4A9u; o->grid = 123.f; /* pcs 0,3,5,7,10 */
        o->vocoder = 1; o->follow = 1; o->snap = 1.f; o->bands = 28; o->voc_q = 8.f; o->voices = 4; o->detune = 14.f; o->sub = 0.5f;
        o->nchord = 2; o->chord[0] = 7.f; o->chord[1] = 12.f; o->chord_level = 0.45f;
        o->noise = 0.75f; o->preemph = 0.3f; o->consonant = 3.f; o->power = 1.6f; o->dry = 0.05f; o->chorus_mix = 0.35f; o->comp = 1.f; o->drive = 1.4f;
        return 0;
    }
    if (!strcmp(name, "classic")) { /* ring-modulated metal robot */
        o->robot = 0.8f; o->range = 0.15f; o->ring_hz = 55.f; o->ring_mix = 0.55f;
        o->crush_bits = 10; o->crush_ds = 2; o->crush_mix = 0.6f; o->formant = 0.92f;
        return 0;
    }
    if (!strcmp(name, "choir")) { /* vocoder choir with harmonies */
        o->robot = 0.9f; o->vocoder = 1; o->follow = 1; o->voices = 5; o->detune = 16.f; o->sub = 0.3f;
        o->nharm = 2; o->harm[0] = 7.f; o->harm[1] = 12.f; o->chorus_mix = 0.5f; o->delay_ms = 180.f; o->delay_fb = 0.3f; o->delay_mix = 0.25f;
        return 0;
    }
    fprintf(stderr, "unknown preset '%s' (clean, harder, classic, choir)\n", name);
    return -1;
}

static void usage(void) {
    puts("ROBOT VOX - self-built formant TTS and robot vocoder (no external speech data)\n\n"
         "  robotvox speak   \"text\" -o out.wav [options]\n"
         "  robotvox sing    \"lyrics\" --melody \"C4:1 E4:1 G4:2 R:1\" [--bpm 120] -o out.wav\n"
         "  robotvox convert in.wav -o out.wav [options]\n"
         "  robotvox live    [--block 512] [--test-in file.wav -o out.wav]   (microphone -> robot voice, real time)\n"
         "  robotvox phonemes \"text\"          (print the rule-based pronunciation)\n\n"
         "options:\n"
         "  -o FILE            output WAV (default out.wav)       --text-file FILE  read text from file\n"
         "  --lang auto|en|ko  language of numbers/text          --sr HZ           sample rate (speak/sing)\n"
         "  --preset NAME      clean | harder (=daft) | classic | choir  (default harder for speak/sing/convert)\n"
         "  --f0 HZ  --speed X  --range 0..1  --robot 0..1  --stress X   speech prosody\n"
         "  --formant X        vocal tract scale (<1 darker, >1 brighter)\n"
         "  --melody STR  --bpm N  --vibrato SEMITONES                    sing mode\n"
         "  --pitch-shift ST   shift the dry voice in semitones\n"
         "  --vocoder on|off   --carrier follow|HZ   --snap 0..1   --voices N   --detune CENTS\n"
         "  --bands N  --sub 0..1  --noise 0..1  --dry 0..1\n"
         "  --harmony \"7,12\"   extra pitch-shifted voices (semitones)   --harmony-gain G\n"
         "  --ringmod HZ[:MIX]  --bitcrush BITS[:DOWNSAMPLE[:MIX]]  --chorus MIX\n"
         "  --key A --scale major|minor|pent|majpent|chromatic   snap speech pitch to a musical scale\n"
         "  --grid BPM         snap syllable lengths to a 16th-note grid     --chord \"7,12\" --chord-level X\n"
         "  --consonant X      consonant clarity (1 = neutral, 3 = 3x; harder preset uses 3)\n"
         "  --power X          vocal effort 0..2 (1 = neutral, <1 soft, >1 pushed/tense; harder preset uses 1.6)\n"
         "  --preemph 0..0.95  --q N  --comp 1  (vocoder clarity / dynamics)\n"
         "  --delay MS[:FEEDBACK[:MIX]]  --drive X\n");
}

static int parse_lang(const char *s) { return !strcmp(s, "en") ? 1 : !strcmp(s, "ko") ? 2 : 0; }

static char *read_all(FILE *f) {
    size_t cap = 4096, n = 0;
    char *b = (char *)malloc(cap);
    if (!b) return NULL;
    size_t r;
    while ((r = fread(b + n, 1, cap - n - 1, f)) > 0) {
        n += r;
        if (cap - n < 2) { cap *= 2; char *p = (char *)realloc(b, cap); if (!p) { free(b); return NULL; } b = p; }
    }
    b[n] = 0;
    return b;
}

static int parse_args(Opts *o, int argc, char **argv, int first) {
    for (int i = first; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "missing value for %s\n", a), NULL))
#define NUM(dst) do { const char *nv_ = NEXT(); if (!nv_) return -1; (dst) = (float)atof(nv_); } while (0)
        if (!strcmp(a, "--preset")) { if (!NEXT()) return -1; }   /* applied in first pass */
        else if (!strcmp(a, "-o")) { o->out = NEXT(); if (!o->out) return -1; }
        else if (!strcmp(a, "--text-file")) {
            const char *p = NEXT(); if (!p) return -1;
            FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", p); return -1; }
            o->text = read_all(f); fclose(f);
        }
        else if (!strcmp(a, "--lang")) { const char *v = NEXT(); if (!v) return -1; o->lang = parse_lang(v); }
        else if (!strcmp(a, "--sr")) { float v; NUM(v); o->sr = (int)v; }
        else if (!strcmp(a, "--f0")) NUM(o->f0);
        else if (!strcmp(a, "--speed")) NUM(o->speed);
        else if (!strcmp(a, "--range")) NUM(o->range);
        else if (!strcmp(a, "--robot")) NUM(o->robot);
        else if (!strcmp(a, "--stress")) NUM(o->stress);
        else if (!strcmp(a, "--formant")) NUM(o->formant);
        else if (!strcmp(a, "--breath")) NUM(o->breath);
        else if (!strcmp(a, "--jitter")) NUM(o->jitter);
        else if (!strcmp(a, "--bpm")) NUM(o->bpm);
        else if (!strcmp(a, "--vibrato")) NUM(o->vibrato);
        else if (!strcmp(a, "--melody")) { o->melody = NEXT(); if (!o->melody) return -1; }
        else if (!strcmp(a, "--pitch-shift")) NUM(o->pshift);
        else if (!strcmp(a, "--vocoder")) { const char *v = NEXT(); if (!v) return -1; o->vocoder = strcmp(v, "off") != 0 && strcmp(v, "0") != 0; }
        else if (!strcmp(a, "--carrier")) {
            const char *v = NEXT(); if (!v) return -1;
            o->vocoder = 1;
            if (!strcmp(v, "follow")) o->follow = 1; else { o->follow = 0; o->carrier_hz = (float)atof(v); }
        }
        else if (!strcmp(a, "--snap")) NUM(o->snap);
        else if (!strcmp(a, "--voices")) { float v; NUM(v); o->voices = (int)v; }
        else if (!strcmp(a, "--detune")) NUM(o->detune);
        else if (!strcmp(a, "--bands")) { float v; NUM(v); o->bands = (int)v; }
        else if (!strcmp(a, "--sub")) NUM(o->sub);
        else if (!strcmp(a, "--noise")) NUM(o->noise);
        else if (!strcmp(a, "--dry")) NUM(o->dry);
        else if (!strcmp(a, "--harmony")) {
            const char *v = NEXT(); if (!v) return -1;
            o->nharm = 0;
            const char *p = v;
            while (*p && o->nharm < 4) { char *e; o->harm[o->nharm++] = strtof(p, &e); if (e == p) break; p = e; while (*p == ',' || *p == ' ') p++; }
        }
        else if (!strcmp(a, "--harmony-gain")) NUM(o->harm_gain);
        else if (!strcmp(a, "--ringmod")) { const char *v = NEXT(); if (!v) return -1; o->ring_hz = (float)atof(v); o->ring_mix = 0.6f; const char *c = strchr(v, ':'); if (c) o->ring_mix = (float)atof(c + 1); }
        else if (!strcmp(a, "--bitcrush")) {
            const char *v = NEXT(); if (!v) return -1;
            o->crush_bits = atoi(v); o->crush_ds = 1; o->crush_mix = 0.7f;
            const char *c = strchr(v, ':'); if (c) { o->crush_ds = atoi(c + 1); const char *c2 = strchr(c + 1, ':'); if (c2) o->crush_mix = (float)atof(c2 + 1); }
        }
        else if (!strcmp(a, "--chorus")) NUM(o->chorus_mix);
        else if (!strcmp(a, "--delay")) {
            const char *v = NEXT(); if (!v) return -1;
            o->delay_ms = (float)atof(v); o->delay_fb = 0.35f; o->delay_mix = 0.3f;
            const char *c = strchr(v, ':'); if (c) { o->delay_fb = (float)atof(c + 1); const char *c2 = strchr(c + 1, ':'); if (c2) o->delay_mix = (float)atof(c2 + 1); }
        }
        else if (!strcmp(a, "--drive")) NUM(o->drive);
        else if (!strcmp(a, "--key")) { const char *v = NEXT(); if (!v) return -1; static const char *nm[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; for (int k = 0; k < 12; k++) if (!strcmp(v, nm[k])) o->key = k; }
        else if (!strcmp(a, "--scale")) {
            const char *v = NEXT(); if (!v) return -1;
            static const struct { const char *n; int pc[8]; } sc[] = { {"major",{0,2,4,5,7,9,11,-1}}, {"minor",{0,2,3,5,7,8,10,-1}}, {"pent",{0,3,5,7,10,-1}}, {"majpent",{0,2,4,7,9,-1}}, {"chromatic",{-1}} };
            int ok = 0;
            for (size_t k = 0; k < sizeof sc / sizeof sc[0]; k++) if (!strcmp(v, sc[k].n)) { o->scale_mask = 0; for (int j = 0; sc[k].pc[j] >= 0; j++) o->scale_mask |= 1u << sc[k].pc[j]; ok = 1; }
            if (!ok) { fprintf(stderr, "unknown scale %s (major minor pent majpent chromatic)\n", v); return -1; }
        }
        else if (!strcmp(a, "--grid")) NUM(o->grid);
        else if (!strcmp(a, "--chord")) { const char *v = NEXT(); if (!v) return -1; o->nchord = 0; const char *p = v; while (*p && o->nchord < 4) { char *e; o->chord[o->nchord++] = strtof(p, &e); if (e == p) break; p = e; while (*p == ',' || *p == ' ') p++; } }
        else if (!strcmp(a, "--chord-level")) NUM(o->chord_level);
        else if (!strcmp(a, "--preemph")) NUM(o->preemph);
        else if (!strcmp(a, "--comp")) NUM(o->comp);
        else if (!strcmp(a, "--q")) NUM(o->voc_q);
        else if (!strcmp(a, "--consonant")) NUM(o->consonant);
        else if (!strcmp(a, "--power")) NUM(o->power);
        else if (!strcmp(a, "--block")) { float bv; NUM(bv); o->block = (int)bv; }
        else if (!strcmp(a, "--test-in")) { o->test_in = NEXT(); if (!o->test_in) return -1; }
        else if (a[0] == '-' && a[1] == '-') { fprintf(stderr, "unknown option %s\n", a); return -1; }
        else if (!o->text && !o->in) { o->text = a; o->in = a; }
        else { fprintf(stderr, "unexpected argument %s\n", a); return -1; }
    }
    return 0;
}

/* shared effect chain applied to the dry voice */
static int effect_chain(Opts *o, RvBuf *b) {
    RvBuf tmp = {0};
    if (fabsf(o->pshift) > 1e-3f) {
        if (rv_pitch_shift(b, o->pshift, &tmp)) return -1;
        rv_buf_free(b); *b = tmp;
    }
    if (o->vocoder) {
        VocoderParams vp; rv_vocoder_defaults(&vp);
        vp.bands = o->bands; vp.voices = o->voices; vp.detune_cents = o->detune; vp.sub = o->sub; vp.noise_mix = o->noise;
        vp.snap = o->snap; vp.q = o->voc_q; vp.nchord = o->nchord; for (int k = 0; k < o->nchord; k++) vp.chord[k] = o->chord[k]; vp.chord_level = o->chord_level; vp.preemph = o->preemph; vp.hf_boost = 1.f + 0.8f * (o->consonant - 1.f); vp.carrier_hz = o->carrier_hz; vp.carrier = o->follow ? VOC_CARRIER_FOLLOW : VOC_CARRIER_FIXED;
        RvBuf voc;
        if (rv_vocoder(b, &vp, &voc)) return -1;
        rv_buf_normalize(&voc, 0.8f);
        rv_buf_normalize(b, 0.8f);
        for (size_t i = 0; i < voc.len; i++) voc.data[i] += o->dry * b->data[i];
        rv_buf_free(b); *b = voc;
    }
    for (int h = 0; h < o->nharm; h++) {
        RvBuf hv;
        if (rv_pitch_shift(b, o->harm[h], &hv)) return -1;
        int rc = rv_mix_into(b, &hv, o->harm_gain);
        rv_buf_free(&hv);
        if (rc) return -1;
    }
    if (o->ring_hz > 0.f) rv_ringmod(b, o->ring_hz, o->ring_mix);
    if (o->crush_bits < 16 || o->crush_ds > 1) rv_bitcrush(b, o->crush_bits, o->crush_ds, o->crush_mix > 0 ? o->crush_mix : 0.7f);
    if (o->chorus_mix > 0.f && rv_chorus(b, 3.5f, 0.45f, 3, o->chorus_mix)) return -1;
    if (o->delay_ms > 0.f && rv_delay(b, o->delay_ms, o->delay_fb, o->delay_mix)) return -1;
    if (o->power > 1.f) { o->drive *= 1.f + 0.6f * (o->power - 1.f); }
    if (o->comp > 0.f) { rv_buf_normalize(b, 0.9f); rv_compress(b, -20.f, 4.f, 5.f, 80.f); }
    if (o->drive > 1.01f) rv_softclip(b, o->drive);
    rv_buf_normalize(b, 0.9f);
    return 0;
}

static int cmd_text(Opts *o, int sing) {
    if (!o->text) { fprintf(stderr, "no text given\n"); return 1; }
    PhList pl; phl_init(&pl);
    if (text_to_phonemes(o->text, o->lang, &pl) || pl.n == 0) { fprintf(stderr, "nothing to pronounce\n"); phl_free(&pl); return 1; }
    ProsodyParams pp; prosody_defaults(&pp);
    pp.f0 = o->f0; pp.speed = o->speed; pp.pitch_range = o->range; pp.robotness = o->robot; pp.stress = o->stress; pp.vibrato = o->vibrato; pp.consonant = o->consonant; pp.effort = o->power - 1.f; pp.bpm = o->grid; pp.key = o->key; pp.scale_mask = o->scale_mask;
    SegList sl; seg_init(&sl);
    int rc;
    if (sing) {
        Note *notes = NULL;
        int nn = o->melody ? prosody_parse_melody(o->melody, &notes) : -1;
        if (nn <= 0) { fprintf(stderr, "sing mode needs --melody \"C4:1 E4:1 ...\"\n"); phl_free(&pl); return 1; }
        rc = prosody_sing(&pl, notes, (size_t)nn, o->bpm, &pp, &sl);
        free(notes);
    } else rc = prosody_speech(&pl, &pp, &sl);
    if (rc) { fprintf(stderr, "prosody failed\n"); return 1; }
    SynthParams sp; synth_defaults(&sp, o->sr);
    sp.consonant = o->consonant; sp.effort = o->power - 1.f; sp.formant_shift = o->formant; sp.breathiness = o->breath; sp.jitter = o->jitter; sp.seed = o->seed;
    RvBuf b;
    if (synth_render(&sl, &sp, &b)) { fprintf(stderr, "synthesis failed\n"); return 1; }
    seg_free(&sl); phl_free(&pl);
    if (effect_chain(o, &b)) { fprintf(stderr, "effect chain failed\n"); return 1; }
    if (rv_wav_write(o->out ? o->out : "out.wav", &b)) return 1;
    printf("wrote %s (%.2f s, %d Hz)\n", o->out ? o->out : "out.wav", (double)b.len / b.sample_rate, b.sample_rate);
    rv_buf_free(&b);
    return 0;
}

static int cmd_convert(Opts *o) {
    if (!o->in) { fprintf(stderr, "no input WAV given\n"); return 1; }
    RvBuf b;
    if (rv_wav_read(o->in, &b)) return 1;
    rv_buf_normalize(&b, 0.9f);
    if (!o->vocoder && !o->preset) { o->vocoder = 1; o->follow = 1; }
    if (effect_chain(o, &b)) { fprintf(stderr, "effect chain failed\n"); return 1; }
    if (rv_wav_write(o->out ? o->out : "out.wav", &b)) return 1;
    printf("wrote %s (%.2f s, %d Hz)\n", o->out ? o->out : "out.wav", (double)b.len / b.sample_rate, b.sample_rate);
    rv_buf_free(&b);
    return 0;
}

static int cmd_live(Opts *o) {
    LiveParams lp; memset(&lp, 0, sizeof lp);
    rv_vocoder_defaults(&lp.voc);
    lp.voc.bands = o->bands; lp.voc.voices = o->voices; lp.voc.detune_cents = o->detune; lp.voc.sub = o->sub; lp.voc.noise_mix = o->noise;
    lp.voc.snap = o->snap; lp.voc.q = o->voc_q; lp.voc.nchord = o->nchord; for (int k = 0; k < o->nchord; k++) lp.voc.chord[k] = o->chord[k]; lp.voc.chord_level = o->chord_level; lp.voc.preemph = o->preemph; lp.voc.hf_boost = 1.f + 0.8f * (o->consonant - 1.f); lp.voc.carrier_hz = o->carrier_hz; lp.voc.carrier = o->follow ? VOC_CARRIER_FOLLOW : VOC_CARRIER_FIXED;
    lp.use_voc = o->vocoder; lp.dry = o->dry;
    lp.ring_hz = o->ring_hz; lp.ring_mix = o->ring_mix; lp.crush_bits = o->crush_bits; lp.crush_ds = o->crush_ds;
    lp.crush_mix = o->crush_mix > 0 ? o->crush_mix : 0.7f; lp.drive = o->drive; lp.out_gain = 0.9f;
    if (o->block < 64) o->block = 64;
    if (o->test_in) return rv_live_test(&lp, o->test_in, o->out ? o->out : "live_test.wav", o->block);
    return rv_live_run(&lp, o->sr, o->block);
}

static int cmd_phonemes(Opts *o) {
    if (!o->text) return 1;
    PhList pl; phl_init(&pl);
    if (text_to_phonemes(o->text, o->lang, &pl)) return 1;
    for (size_t i = 0; i < pl.n; i++) {
        if (pl.v[i].flags & PF_SYL_START) printf(" .");
        printf(" %s%s", ph_info(pl.v[i].id)->name, (pl.v[i].flags & PF_STRESS1) ? "'" : "");
    }
    printf("\n");
    phl_free(&pl);
    return 0;
}

static int real_main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) { usage(); return argc < 2 ? 1 : 0; }
    const char *cmd = argv[1];
    Opts o; defaults(&o);
    /* pass 1: preset (default: harder for speak/sing, none unless named for convert) */
    const char *preset = NULL;
    for (int i = 2; i + 1 < argc; i++) if (!strcmp(argv[i], "--preset")) preset = argv[i + 1];
    o.preset = preset;
    if (!preset && strcmp(cmd, "phonemes") && strcmp(cmd, "convert")) preset = "harder";
    if (!preset && !strcmp(cmd, "convert")) preset = "harder";
    if (preset && apply_preset(&o, preset)) return 1;
    if (!strcmp(cmd, "convert")) { o.preset = preset; }
    if (parse_args(&o, argc, argv, 2)) return 1;
    if (!strcmp(cmd, "speak")) return cmd_text(&o, 0);
    if (!strcmp(cmd, "sing")) return cmd_text(&o, 1);
    if (!strcmp(cmd, "convert")) { o.text = NULL; return cmd_convert(&o); }
    if (!strcmp(cmd, "live")) return cmd_live(&o);
    if (!strcmp(cmd, "phonemes")) return cmd_phonemes(&o);
    fprintf(stderr, "unknown command '%s'\n", cmd);
    usage();
    return 1;
}

#ifdef _WIN32
int main(void) {
    int wargc;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) return 1;
    char **argv = (char **)calloc((size_t)wargc + 1, sizeof(char *));
    for (int i = 0; i < wargc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);
        argv[i] = (char *)malloc((size_t)n);
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], n, NULL, NULL);
    }
    SetConsoleOutputCP(CP_UTF8);
    return real_main(wargc, argv);
}
#else
int main(int argc, char **argv) { return real_main(argc, argv); }
#endif
