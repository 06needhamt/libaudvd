/*
 * Fuzzing of the IFO parsers. Built as a libFuzzer target with
 * -Dfuzzer=true, otherwise as a deterministic mutation smoke test.
 *
 * The first input byte picks the file that is replaced (AMG, SAMG or
 * ATS IFO) in an otherwise valid synthetic zone; the rest is its content.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audvd/audvd.h"
#include "synth.h"

static char *root;
static const char *const names[3] = { "AUDIO_TS.IFO", "AUDIO_PP.IFO", "ATS_01_0.IFO" };

static void setup(void)
{
    synth_disc s;

    if (root)
        return;
    synth_default(&s);
    root = synth_tmpdir();
    if (!root || synth_write(&s, root))
        abort();
}

static void walk(audvd_disc *d)
{
    uint8_t buf[2 * AUDVD_SECTOR_SIZE];

    for (int n = 1; n <= audvd_num_ats(d); n++) {
        audvd_ats_info ai;
        audvd_downmix dm;
        audvd_get_ats(d, n, &ai);
        for (int t = 0; t < AUDVD_MAX_DOWNMIX_TABLES; t++)
            audvd_get_downmix(d, n, t, &dm);
        audvd_read_aob_sectors(d, n, 9, 2, buf);
    }
    for (int i = 0; i < audvd_num_titles(d); i++) {
        audvd_title t;
        audvd_get_title(d, i, &t);
        for (int k = 0; k < t.nb_tracks + 1; k++) {
            audvd_track tr;
            audvd_cell c;
            if (audvd_get_track(d, i, k, &tr) == AUDVD_OK)
                for (int j = tr.first_cell; j < tr.first_cell + tr.nb_cells; j++)
                    if (audvd_get_cell(d, i, j, &c) != AUDVD_OK)
                        abort(); /* tracks must only reference real cells */
        }
    }
    for (int i = 0; i < audvd_samg_num_tracks(d); i++) {
        audvd_samg_track st;
        audvd_samg_get_track(d, i, &st);
    }
}

static void fuzz_one(const uint8_t *data, size_t size)
{
    char path[1100];
    audvd_open_opts o = { NULL, NULL, AUDVD_OPEN_NO_BUP };
    audvd_disc *d;

    if (size < 1)
        return;
    setup();
    snprintf(path, sizeof(path), "%s/AUDIO_TS/%s", root, names[data[0] % 3]);
    if (synth_write_file(path, data + 1, size - 1))
        abort();
    if (audvd_open(root, &o, &d) == AUDVD_OK) {
        walk(d);
        audvd_close(d);
    }
}

#ifdef AUDVD_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_one(data, size);
    return 0;
}
#else
static uint32_t rng = 0x12345678;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

int main(int argc, char **argv)
{
    int iterations = argc > 1 ? atoi(argv[1]) : 3000;
    synth_disc s;
    uint8_t *img[3];
    size_t size[3];

    synth_default(&s);
    img[0] = synth_amg(&s, &size[0]);
    img[1] = synth_samg(&s, &size[1]);
    img[2] = synth_ats(&s, &size[2]);

    for (int it = 0; it < iterations; it++) {
        int f = it % 3;
        size_t len = size[f];
        uint8_t *m = malloc(len + 1);
        int nb = 1 + rnd() % 8;

        m[0] = (uint8_t)f;
        memcpy(m + 1, img[f], len);
        for (int k = 0; k < nb; k++) {
            /* bias mutations towards the populated table areas */
            size_t off = rnd() % 4 ? rnd() % 0x200 + (rnd() % 2) * 2048 : rnd() % len;
            if (off < len)
                m[1 + off] = (uint8_t)(rnd() % 3 ? rnd() : (rnd() % 2 ? 0 : 0xff));
        }
        if (rnd() % 16 == 0)
            len = rnd() % len; /* truncation */
        fuzz_one(m, len + 1);
        free(m);
        /* restore the file for the next round */
        {
            char path[1100];
            snprintf(path, sizeof(path), "%s/AUDIO_TS/%s", root, names[f]);
            synth_write_file(path, img[f], size[f]);
        }
    }
    for (int f = 0; f < 3; f++)
        free(img[f]);
    synth_rmtree(root);
    free(root);
    printf("%d iterations ok\n", iterations);
    return 0;
}
#endif
