#ifndef RV_SYNTH_H
#define RV_SYNTH_H

#include "rv_common.h"
#include "prosody.h"

typedef struct {
    int   sr;
    float formant_shift;   /* 1.0 = default vocal tract length */
    float rise;            /* glottal pulse opening phase (fraction of period) */
    float fall;            /* glottal pulse closing phase (fraction of period) */
    float breathiness;     /* aspiration noise mixed into voiced sounds 0..1 */
    float jitter;          /* random period variation 0..0.05 */
    float tau_ms;          /* formant transition time constant */
    float consonant;       /* obstruent noise level multiplier (1 = neutral, 3 = 3x clearer) */
    float effort;          /* vocal effort -1..1 (0 = neutral): brighter, tenser glottal source */
    uint32_t seed;
} SynthParams;

void synth_defaults(SynthParams *p, int sr);

/* Render the timed segment list to a mono buffer (formant synthesizer). */
int synth_render(const SegList *segs, const SynthParams *sp, RvBuf *out);

#endif
