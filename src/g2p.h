#ifndef RV_G2P_H
#define RV_G2P_H

#include <stdint.h>
#include "phoneme.h"

/* English rule-based letter-to-sound. w: lowercase ASCII letters only. */
int g2p_en_word(const char *w, PhList *out);

/* Korean rule-based pronunciation. cps: Hangul syllable code points (U+AC00..U+D7A3). */
int g2p_ko_word(const uint32_t *cps, int n, PhList *out);

#endif
