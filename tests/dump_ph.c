#include <stdio.h>
#include "../src/phoneme.h"
int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        PhList l; phl_init(&l);
        text_to_phonemes(argv[a], 0, &l);
        printf("%-28s:", argv[a]);
        for (size_t i = 0; i < l.n; i++) {
            if (l.v[i].flags & PF_SYL_START) printf(" .");
            printf(" %s%s", ph_info(l.v[i].id)->name, (l.v[i].flags & PF_STRESS1) ? "'" : "");
        }
        printf("\n");
        phl_free(&l);
    }
    return 0;
}
