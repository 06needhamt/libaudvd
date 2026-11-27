/*
 * libaudvd parsing tests on synthetic zones.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define _XOPEN_SOURCE 700

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audvd/audvd.h"
#include "synth.h"

static int failures;
static int log_warnings;

#define CHECK(cond) do {                                                  \
    if (!(cond)) {                                                        \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,  \
                #cond);                                                   \
        failures++;                                                       \
    }                                                                     \
} while (0)

#define CHECK_EQ(a, b) do {                                               \
    long long va_ = (long long)(a), vb_ = (long long)(b);                 \
    if (va_ != vb_) {                                                     \
        fprintf(stderr, "%s:%d: %s == %lld, expected %lld\n", __FILE__,   \
                __LINE__, #a, va_, vb_);                                  \
        failures++;                                                       \
    }                                                                     \
} while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) < (eps))

static void log_cb(void *opaque, audvd_log_level level, const char *msg)
{
    (void)opaque;
    if (level <= AUDVD_LOG_WARNING)
        log_warnings++;
    if (getenv("AUDVD_TEST_VERBOSE"))
        fprintf(stderr, "  [log %d] %s\n", level, msg);
}

static audvd_disc *open_synth(const synth_disc *s, char **root, int flags,
                              int *ret)
{
    audvd_open_opts o = { log_cb, NULL, flags };
    audvd_disc *d = NULL;

    *root = synth_tmpdir();
    if (!*root || synth_write(s, *root)) {
        fprintf(stderr, "cannot write synthetic disc\n");
        exit(1);
    }
    log_warnings = 0;
    *ret = audvd_open(*root, &o, &d);
    return d;
}

static void close_synth(audvd_disc *d, char *root)
{
    audvd_close(d);
    synth_rmtree(root);
    free(root);
}

static void test_basic(void)
{
    synth_disc s;
    audvd_disc *d;
    audvd_title t;
    audvd_track tr;
    audvd_ats_info ai;
    audvd_samg_track st;
    char *root;
    int ret;

    synth_default(&s);
    d = open_synth(&s, &root, 0, &ret);
    CHECK_EQ(ret, AUDVD_OK);
    if (!d)
        return;
    CHECK_EQ(log_warnings, 0);

    CHECK_EQ(audvd_num_ats(d), 1);
    CHECK_EQ(audvd_num_titles(d), 2);
    CHECK_EQ(audvd_samg_num_tracks(d), 3);

    CHECK_EQ(audvd_get_ats(d, 1, &ai), AUDVD_OK);
    CHECK_EQ(ai.version, 0x0011);
    CHECK_EQ(ai.nb_titles, 1);
    CHECK_EQ(ai.nb_attrs, 2);
    CHECK_EQ(ai.nb_aob_files, 2);
    CHECK_EQ(ai.aob_sectors, 24);
    CHECK_EQ(ai.aob_lba, 1707);
    CHECK_EQ(ai.attrs[0].codec, AUDVD_CODEC_LPCM);
    CHECK_EQ(ai.attrs[0].group_bits[0], 24);
    CHECK_EQ(ai.attrs[0].group_bits[1], 24);
    CHECK_EQ(ai.attrs[0].group_rate[0], 48000);
    CHECK_EQ(ai.attrs[0].channel_assignment, 20);
    CHECK_EQ(ai.attrs[0].layout.nb_channels, 6);
    CHECK_EQ(ai.attrs[1].codec, AUDVD_CODEC_MLP);
    CHECK_EQ(ai.attrs[1].group_rate[0], 96000);
    CHECK_EQ(ai.attrs[1].group_bits[1], 0);
    CHECK_EQ(ai.attrs[1].layout.nb_channels, 2);
    CHECK_EQ(audvd_get_ats(d, 2, &ai), AUDVD_ERR_RANGE);

    CHECK_EQ(audvd_get_title(d, 0, &t), AUDVD_OK);
    CHECK_EQ(t.number, 1);
    CHECK_EQ(t.audio_only, 1);
    CHECK_EQ(t.group, 3);          /* from the SAMG */
    CHECK_EQ(t.ats, 1);
    CHECK_EQ(t.ats_title, 1);
    CHECK_EQ(t.nb_tracks, 3);
    CHECK_EQ(t.length_pts, 1440000);
    CHECK_EQ(audvd_title_num_cells(d, 0), 4);
    CHECK_EQ(audvd_num_groups(d), 3);

    CHECK_EQ(audvd_get_title(d, 1, &t), AUDVD_OK);
    CHECK_EQ(t.audio_only, 0);
    CHECK_EQ(audvd_get_track(d, 1, 0, &tr), AUDVD_ERR_RANGE);
    CHECK_EQ(audvd_get_title(d, 2, &t), AUDVD_ERR_RANGE);

    CHECK_EQ(audvd_get_track(d, 0, 0, &tr), AUDVD_OK);
    CHECK_EQ(tr.number, 1);
    CHECK_EQ(tr.start_pts, 0);
    CHECK_EQ(tr.length_pts, 900000);
    CHECK_EQ(tr.first_pts, 94);
    CHECK_EQ(tr.first_sector, 0);
    CHECK_EQ(tr.last_sector, 9);
    CHECK_EQ(tr.attr_index, 0);
    CHECK_EQ(tr.attr.codec, AUDVD_CODEC_LPCM);
    CHECK_EQ(tr.downmix_valid, 1);
    CHECK_EQ(tr.downmix_table, 2);
    CHECK_EQ(tr.downmix_prohibited, 0);
    CHECK_EQ(tr.stc_discontinuity, 1);

    CHECK_EQ(audvd_get_track(d, 0, 1, &tr), AUDVD_OK);
    CHECK_EQ(tr.start_pts, 900000);
    CHECK_EQ(tr.first_cell, 1);
    CHECK_EQ(tr.nb_cells, 2);
    CHECK_EQ(tr.first_sector, 10);
    CHECK_EQ(tr.last_sector, 19);
    CHECK_EQ(tr.downmix_valid, 0);

    CHECK_EQ(audvd_get_track(d, 0, 2, &tr), AUDVD_OK);
    CHECK_EQ(tr.start_pts, 1350000);
    CHECK_EQ(tr.attr_index, 1);
    CHECK_EQ(tr.attr.codec, AUDVD_CODEC_MLP);
    CHECK_EQ(tr.first_sector, 20);
    CHECK_EQ(tr.last_sector, 23);
    CHECK_EQ(audvd_get_track(d, 0, 3, &tr), AUDVD_ERR_RANGE);

    CHECK_EQ(audvd_samg_get_track(d, 0, &st), AUDVD_OK);
    CHECK_EQ(st.group, 3);
    CHECK_EQ(st.track, 1);
    CHECK_EQ(st.first_in_group, 1);
    CHECK_EQ(st.first_sector, 1707);
    CHECK_EQ(st.last_sector, 1716);
    CHECK_EQ(st.attr.channel_assignment, 20);
    CHECK_EQ(audvd_samg_get_track(d, 1, &st), AUDVD_OK);
    CHECK_EQ(st.first_in_group, 0);

    close_synth(d, root);
}

static void test_downmix(void)
{
    synth_disc s;
    audvd_disc *d;
    audvd_downmix dm;
    char *root;
    int ret;

    CHECK_NEAR(audvd_downmix_code_to_db(0x00), 0.0, 1e-9);
    CHECK_NEAR(audvd_downmix_code_to_db(0x32), -10.0, 1e-9);
    CHECK_NEAR(audvd_downmix_code_to_db(0xc8), -40.0, 1e-9);
    CHECK_NEAR(audvd_downmix_code_to_db(0xc9), -40.4, 1e-9);
    CHECK(isinf(audvd_downmix_code_to_db(0xff)));
    CHECK_NEAR(audvd_downmix_code_to_gain(0x00), 1.0, 1e-9);
    CHECK_NEAR(audvd_downmix_code_to_gain(0x1e), 0.501187, 1e-5);
    CHECK_NEAR(audvd_downmix_code_to_gain(0xff), 0.0, 1e-12);

    synth_default(&s);
    d = open_synth(&s, &root, 0, &ret);
    CHECK_EQ(ret, AUDVD_OK);
    if (!d)
        return;
    CHECK_EQ(audvd_get_downmix(d, 1, 2, &dm), AUDVD_OK);
    CHECK_EQ(dm.gain_code[0][0], 0x00);
    CHECK_EQ(dm.gain_code[0][1], 0xff);
    CHECK_NEAR(dm.gain[0][0], 1.0, 1e-6);
    CHECK_NEAR(dm.gain[0][1], 0.0, 1e-6);
    CHECK_NEAR(dm.gain[2][0], 0.316228, 1e-5);
    CHECK_NEAR(dm.gain[4][0], 0.501187, 1e-5);
    CHECK_NEAR(dm.gain[4][1], 0.501187, 1e-5);
    CHECK_NEAR(dm.gain[5][0], 0.0, 1e-6);
    CHECK_EQ(audvd_get_downmix(d, 1, 0, &dm), AUDVD_OK);
    CHECK_NEAR(dm.gain[0][0], 1.0, 1e-6); /* zero table: all 0 dB codes */
    CHECK_EQ(audvd_get_downmix(d, 1, 16, &dm), AUDVD_ERR_RANGE);
    CHECK_EQ(audvd_get_downmix(d, 2, 0, &dm), AUDVD_ERR_RANGE);
    close_synth(d, root);
}

static void test_layouts(void)
{
    audvd_channel_layout l;

    CHECK_EQ(audvd_channel_assignment_layout(0, &l), AUDVD_OK);
    CHECK_EQ(l.nb_channels, 1);
    CHECK_EQ(l.speakers[0], AUDVD_SPK_C);
    CHECK_EQ(audvd_channel_assignment_layout(17, &l), AUDVD_OK);
    CHECK_EQ(l.nb_channels, 6);
    CHECK_EQ(l.group_channels[0], 3);
    CHECK_EQ(l.speakers[3], AUDVD_SPK_LFE);
    CHECK_EQ(audvd_channel_assignment_layout(20, &l), AUDVD_OK);
    CHECK_EQ(l.speakers[2], AUDVD_SPK_LS);
    CHECK_EQ(l.speakers[5], AUDVD_SPK_LFE);
    CHECK_EQ(audvd_channel_assignment_layout(21, &l), AUDVD_ERR_RANGE);
    CHECK_EQ(audvd_channel_assignment_layout(-1, &l), AUDVD_ERR_RANGE);
    for (int ca = 0; ca <= 20; ca++) {
        CHECK_EQ(audvd_channel_assignment_layout(ca, &l), AUDVD_OK);
        CHECK_EQ(l.group_channels[0] + l.group_channels[1], l.nb_channels);
    }
}

static void test_read_aob(void)
{
    synth_disc s;
    audvd_disc *d;
    uint8_t *buf = malloc(30 * AUDVD_SECTOR_SIZE);
    char *root;
    int ret;

    synth_default(&s);
    d = open_synth(&s, &root, 0, &ret);
    CHECK_EQ(ret, AUDVD_OK);
    if (!d || !buf)
        return;

    /* across the ATS_01_1 / ATS_01_2 boundary at sector 10 */
    CHECK_EQ(audvd_read_aob_sectors(d, 1, 8, 4, buf), 4);
    for (int i = 0; i < 4; i++) {
        const uint8_t *p = buf + (size_t)i * AUDVD_SECTOR_SIZE;
        CHECK_EQ(synth_sector_number(p), 8 + i);
    }
    /* short read at the end, then nothing */
    CHECK_EQ(audvd_read_aob_sectors(d, 1, 20, 10, buf), 4);
    CHECK_EQ(synth_sector_number(buf + 3 * AUDVD_SECTOR_SIZE), 23);
    CHECK_EQ(audvd_read_aob_sectors(d, 1, 24, 1, buf), 0);
    CHECK_EQ(audvd_read_aob_sectors(d, 1, 0, 24, buf), 24);
    CHECK_EQ(synth_sector_number(buf + 23 * AUDVD_SECTOR_SIZE), 23);
    CHECK_EQ(audvd_read_aob_sectors(d, 3, 0, 1, buf), AUDVD_ERR_RANGE);
    close_synth(d, root);
    free(buf);
}

static void test_lowercase(void)
{
    synth_disc s;
    audvd_disc *d;
    audvd_ats_info ai;
    char *root, path[1100];
    int ret;

    synth_default(&s);
    s.lowercase = 1;
    d = open_synth(&s, &root, 0, &ret);
    CHECK_EQ(ret, AUDVD_OK);
    if (!d)
        return;
    CHECK_EQ(audvd_num_titles(d), 2);
    CHECK_EQ(audvd_get_ats(d, 1, &ai), AUDVD_OK);
    CHECK_EQ(ai.nb_aob_files, 2);
    audvd_close(d);

    /* the AUDIO_TS directory itself can be opened too */
    snprintf(path, sizeof(path), "%s/audio_ts", root);
    ret = audvd_open(path, NULL, &d);
    CHECK_EQ(ret, AUDVD_OK);
    close_synth(d, root);
}

static void test_no_amg(void)
{
    synth_disc s;
    audvd_disc *d;
    audvd_title t;
    char *root, path[1100];
    int ret;

    synth_default(&s);
    d = open_synth(&s, &root, AUDVD_OPEN_IGNORE_AMG, &ret);
    CHECK_EQ(ret, AUDVD_OK);
    if (!d)
        return;
    CHECK_EQ(audvd_num_titles(d), 1);
    CHECK_EQ(audvd_get_title(d, 0, &t), AUDVD_OK);
    CHECK_EQ(t.nb_tracks, 3);
    CHECK_EQ(t.group, 3);
    audvd_close(d);

    /* both copies of the AMG missing */
    snprintf(path, sizeof(path), "%s/AUDIO_TS/AUDIO_TS.IFO", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/AUDIO_TS/AUDIO_TS.BUP", root);
    unlink(path);
    ret = audvd_open(root, NULL, &d);
    CHECK_EQ(ret, AUDVD_OK);
    CHECK_EQ(audvd_num_titles(d), 1);
    close_synth(d, root);
}

static void test_bup_fallback(void)
{
    synth_disc s;
    audvd_disc *d;
    audvd_title t;
    char *root, path[1100];
    uint8_t junk[4096] = { 0 };
    audvd_open_opts o = { log_cb, NULL, 0 };
    int ret;

    synth_default(&s);
    d = open_synth(&s, &root, 0, &ret);
    audvd_close(d);

    memcpy(junk, "DVDAUDIO-ATS", 12); /* identifier ok, tables broken */
    snprintf(path, sizeof(path), "%s/AUDIO_TS/ATS_01_0.IFO", root);
    CHECK_EQ(synth_write_file(path, junk, sizeof(junk)), 0);
    snprintf(path, sizeof(path), "%s/AUDIO_TS/AUDIO_TS.IFO", root);
    CHECK_EQ(synth_write_file(path, junk, 100), 0);

    log_warnings = 0;
    ret = audvd_open(root, &o, &d);
    CHECK_EQ(ret, AUDVD_OK);
    CHECK(log_warnings > 0);
    CHECK_EQ(audvd_num_titles(d), 2);
    CHECK_EQ(audvd_get_title(d, 0, &t), AUDVD_OK);
    CHECK_EQ(t.nb_tracks, 3);
    audvd_close(d);

    o.flags = AUDVD_OPEN_NO_BUP;
    ret = audvd_open(root, &o, &d);
    CHECK_EQ(ret, AUDVD_OK);     /* SAMG still lists the tracks */
    CHECK_EQ(audvd_num_ats(d), 0);
    CHECK_EQ(audvd_num_titles(d), 0);
    CHECK_EQ(audvd_samg_num_tracks(d), 3);
    close_synth(d, root);
}

static void test_corrupt_tracks(void)
{
    synth_disc s;
    audvd_disc *d;
    char *root, path[1100];
    uint8_t *img;
    size_t size;
    int ret;

    synth_default(&s);
    d = open_synth(&s, &root, AUDVD_OPEN_NO_BUP, &ret);
    audvd_close(d);

    struct { size_t off; uint8_t val; } cases[] = {
        { 2048 + 0x10 + 0x10 + 20 * 1 + 4, 9 },    /* first cell past the end */
        { 2048 + 0x10 + 0x10 + 20 * 1 + 4, 1 },    /* cells not increasing */
        { 2048 + 0x10 + 2, 200 },                  /* track count overflows */
        { 2048 + 0x10 + 3, 0 },                    /* no cells */
        { 2048 + 0x10 + 0x10 + 60 + 12 + 11, 0 },  /* cell 2 ends before start */
        { 0xcc + 3, 9 },                           /* PGCIT past the file */
        { 2048 + 12, 0xff },                       /* PGC offset past the table */
        { 2048 + 1, 200 },                         /* PGC count overflows */
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        audvd_open_opts o = { log_cb, NULL, AUDVD_OPEN_NO_BUP };
        audvd_title t;
        audvd_track tr;

        img = synth_ats(&s, &size);
        img[cases[c].off] = cases[c].val;
        snprintf(path, sizeof(path), "%s/AUDIO_TS/ATS_01_0.IFO", root);
        CHECK_EQ(synth_write_file(path, img, size), 0);
        free(img);

        ret = audvd_open(root, &o, &d);
        CHECK_EQ(ret, AUDVD_OK);
        if (!d)
            continue;
        CHECK_EQ(audvd_num_ats(d), 0);
        CHECK_EQ(audvd_get_title(d, 0, &t), AUDVD_OK);
        CHECK_EQ(t.nb_tracks, 0);
        CHECK_EQ(audvd_get_track(d, 0, 0, &tr), AUDVD_ERR_RANGE);
        audvd_close(d);
    }
    close_synth(d = NULL, root);
}

static void test_not_a_disc(void)
{
    char *root = synth_tmpdir();
    audvd_disc *d = (audvd_disc *)1;

    CHECK_EQ(audvd_open(root, NULL, &d), AUDVD_ERR_NOTFOUND);
    CHECK(d == NULL);
    CHECK_EQ(audvd_open("/nonexistent/path", NULL, &d), AUDVD_ERR_NOTFOUND);
    synth_rmtree(root);
    free(root);
    CHECK(strcmp(audvd_strerror(AUDVD_ERR_INVALID), "unknown error"));
}

/*
 * Structural checks on a real disc given with AUDVD_TEST_DISC; skipped
 * otherwise. Nothing disc-specific is assumed.
 */
static void test_real_disc(void)
{
    const char *path = getenv("AUDVD_TEST_DISC");
    audvd_open_opts o = { log_cb, NULL, 0 };
    audvd_disc *d;
    int ret;

    if (!path || !*path)
        return;
    log_warnings = 0;
    ret = audvd_open(path, &o, &d);
    CHECK_EQ(ret, AUDVD_OK);
    if (ret < 0)
        return;
    CHECK_EQ(log_warnings, 0);
    CHECK(audvd_num_titles(d) > 0);
    for (int i = 0; i < audvd_num_titles(d); i++) {
        audvd_title t;
        uint64_t sum = 0;
        uint32_t prev_end = 0;

        audvd_get_title(d, i, &t);
        if (!t.audio_only)
            continue;
        CHECK(t.nb_tracks > 0);
        for (int k = 0; k < t.nb_tracks; k++) {
            audvd_track tr;
            CHECK_EQ(audvd_get_track(d, i, k, &tr), AUDVD_OK);
            CHECK_EQ(tr.start_pts, sum);
            CHECK(tr.first_sector <= tr.last_sector);
            CHECK(!k || tr.first_sector > prev_end);
            CHECK(tr.attr.layout.nb_channels > 0);
            sum += tr.length_pts;
            prev_end = tr.last_sector;
        }
        CHECK_EQ(sum, t.length_pts);
    }
    for (int n = 1; n <= audvd_num_ats(d); n++) {
        audvd_ats_info ai;
        if (audvd_get_ats(d, n, &ai) == AUDVD_OK && audvd_samg_num_tracks(d))
            CHECK(ai.aob_lba >= 0);
    }
    audvd_close(d);
    printf("real disc %s: ok\n", path);
}

int main(void)
{
    test_layouts();
    test_downmix();
    test_basic();
    test_read_aob();
    test_lowercase();
    test_no_amg();
    test_bup_fallback();
    test_corrupt_tracks();
    test_not_a_disc();
    test_real_disc();

    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
