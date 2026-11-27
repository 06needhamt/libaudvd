/*
 * libaudvd title reader tests on synthetic zones.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audvd/audvd.h"
#include "synth.h"

static int failures;

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

#define F_TITLE AUDVD_POS_TITLE_START
#define F_TRACK AUDVD_POS_TRACK_START
#define F_CELL  AUDVD_POS_CELL_START
#define F_DISC  AUDVD_POS_DISCONTINUITY
#define F_ENC   AUDVD_POS_ENCRYPTED

static uint8_t buf[64 * AUDVD_SECTOR_SIZE];

static void log_cb(void *opaque, audvd_log_level level, const char *msg)
{
    (void)opaque;
    if (getenv("AUDVD_TEST_VERBOSE"))
        fprintf(stderr, "  [log %d] %s\n", level, msg);
}

typedef struct fixture {
    synth_disc    s;
    char         *root;
    audvd_disc   *disc;
    audvd_reader *r;
} fixture;

static int setup(fixture *f, void (*tweak)(synth_disc *))
{
    audvd_open_opts o = { log_cb, NULL, 0 };

    memset(f, 0, sizeof(*f));
    synth_default(&f->s);
    if (tweak)
        tweak(&f->s);
    f->root = synth_tmpdir();
    if (!f->root || synth_write(&f->s, f->root)) {
        fprintf(stderr, "cannot write synthetic disc\n");
        exit(1);
    }
    CHECK_EQ(audvd_open(f->root, &o, &f->disc), AUDVD_OK);
    if (!f->disc)
        return -1;
    CHECK_EQ(audvd_reader_open(f->disc, 0, &f->r), AUDVD_OK);
    return f->r ? 0 : -1;
}

static void teardown(fixture *f)
{
    audvd_reader_close(f->r);
    audvd_close(f->disc);
    synth_rmtree(f->root);
    free(f->root);
}

/* Expect one run; check its first sector, length and flags. */
static void expect_run(audvd_reader *r, int max, uint32_t sector, int n,
                       int track, int cell, int flags)
{
    audvd_pos pos;
    int got = audvd_reader_read(r, buf, max, &pos);

    CHECK_EQ(got, n);
    if (got <= 0)
        return;
    CHECK_EQ(pos.sector, sector);
    CHECK_EQ(pos.track, track);
    CHECK_EQ(pos.cell, cell);
    CHECK_EQ(pos.flags, flags);
    for (int k = 0; k < got; k++)
        CHECK_EQ(synth_sector_number(buf + (size_t)k * AUDVD_SECTOR_SIZE),
                 sector + k);
}

static void test_play_order(void)
{
    fixture f;
    audvd_pos pos;

    if (setup(&f, NULL) < 0)
        return;
    CHECK_EQ(audvd_reader_tell(f.r, &pos), AUDVD_OK);
    CHECK_EQ(pos.sector, 0);
    CHECK_EQ(pos.flags, F_TITLE | F_CELL | F_TRACK | F_DISC);

    expect_run(f.r, 64, 0, 10, 0, 0, F_TITLE | F_CELL | F_TRACK | F_DISC);
    expect_run(f.r, 64, 10, 5, 1, 1, F_CELL | F_TRACK | F_DISC);
    expect_run(f.r, 64, 15, 5, 1, 2, F_CELL);
    expect_run(f.r, 64, 20, 4, 2, 3, F_CELL | F_TRACK | F_DISC);
    CHECK_EQ(audvd_reader_read(f.r, buf, 64, &pos), AUDVD_ERR_EOF);
    CHECK_EQ(audvd_reader_tell(f.r, &pos), AUDVD_ERR_EOF);

    /* small reads split runs but keep the flags on the first only */
    CHECK_EQ(audvd_reader_set_tracks(f.r, 0, 2), AUDVD_OK);
    expect_run(f.r, 3, 0, 3, 0, 0, F_TITLE | F_CELL | F_TRACK | F_DISC);
    expect_run(f.r, 3, 3, 3, 0, 0, 0);
    expect_run(f.r, 3, 6, 3, 0, 0, 0);
    expect_run(f.r, 3, 9, 1, 0, 0, 0);
    expect_run(f.r, 3, 10, 3, 1, 1, F_CELL | F_TRACK | F_DISC);
    CHECK_EQ(audvd_reader_read(f.r, buf, 0, &pos), AUDVD_ERR_RANGE);

    /* the track position data for rebasing */
    CHECK_EQ(audvd_reader_tell(f.r, &pos), AUDVD_OK);
    CHECK_EQ(pos.track_start_pts, 900000);
    CHECK_EQ(pos.track_first_pts, 100);
    teardown(&f);
}

static void test_ranges(void)
{
    fixture f;
    audvd_pos pos;
    audvd_reader *r2;

    if (setup(&f, NULL) < 0)
        return;
    CHECK_EQ(audvd_reader_set_tracks(f.r, 1, 1), AUDVD_OK);
    expect_run(f.r, 64, 10, 5, 1, 1, F_TITLE | F_CELL | F_TRACK | F_DISC);
    expect_run(f.r, 64, 15, 5, 1, 2, F_CELL);
    CHECK_EQ(audvd_reader_read(f.r, buf, 64, &pos), AUDVD_ERR_EOF);

    CHECK_EQ(audvd_reader_set_tracks(f.r, 2, 1), AUDVD_ERR_RANGE);
    CHECK_EQ(audvd_reader_set_tracks(f.r, -1, 1), AUDVD_ERR_RANGE);
    CHECK_EQ(audvd_reader_set_tracks(f.r, 0, 3), AUDVD_ERR_RANGE);

    CHECK_EQ(audvd_reader_set_tracks(f.r, 0, 1), AUDVD_OK);
    CHECK_EQ(audvd_reader_seek_track(f.r, 2), AUDVD_ERR_RANGE);
    CHECK_EQ(audvd_reader_seek_track(f.r, 1), AUDVD_OK);
    expect_run(f.r, 64, 10, 5, 1, 1, F_CELL | F_TRACK | F_DISC);
    CHECK_EQ(audvd_reader_seek_track(f.r, 0), AUDVD_OK);
    expect_run(f.r, 64, 0, 10, 0, 0, F_TITLE | F_CELL | F_TRACK | F_DISC);

    /* seeking by time stays within the range too */
    CHECK_EQ(audvd_reader_seek_pts(f.r, 1350000, NULL), AUDVD_ERR_RANGE);

    /* audio+video and missing titles have no reader */
    CHECK_EQ(audvd_reader_open(f.disc, 1, &r2), AUDVD_ERR_RANGE);
    CHECK(r2 == NULL);
    CHECK_EQ(audvd_reader_open(f.disc, 7, &r2), AUDVD_ERR_RANGE);
    teardown(&f);
}

static void test_seek_pts(void)
{
    fixture f;
    audvd_pos pos;
    uint64_t landed;

    if (setup(&f, NULL) < 0)
        return;

    /* mid track 1, odd pack (no PTS): lands on pack 6 = sector 16 */
    CHECK_EQ(audvd_reader_seek_pts(f.r, 900000 + 7 * 45000 + 100, &landed), AUDVD_OK);
    CHECK_EQ(landed, 900000 + 6 * 45000);
    expect_run(f.r, 64, 16, 4, 1, 2, F_DISC);

    CHECK_EQ(audvd_reader_seek_pts(f.r, 0, &landed), AUDVD_OK);
    CHECK_EQ(landed, 0);
    expect_run(f.r, 1, 0, 1, 0, 0, F_TITLE | F_CELL | F_TRACK | F_DISC);

    CHECK_EQ(audvd_reader_seek_pts(f.r, 900000, &landed), AUDVD_OK);
    CHECK_EQ(landed, 900000);
    expect_run(f.r, 64, 10, 5, 1, 1, F_CELL | F_TRACK | F_DISC);

    CHECK_EQ(audvd_reader_seek_pts(f.r, 1350000 + 3 * 22500 + 5, &landed), AUDVD_OK);
    CHECK_EQ(landed, 1350000 + 2 * 22500);
    expect_run(f.r, 64, 22, 2, 2, 3, F_DISC);

    CHECK_EQ(audvd_reader_seek_pts(f.r, 1440000, &landed), AUDVD_ERR_RANGE);

    /* sweep: land at or before the target, within two packs, and the
     * pack read next carries the landed time */
    for (uint64_t t = 0; t < 1440000; t += 7919) {
        uint32_t dur = t < 900000 ? 90000 : t < 1350000 ? 45000 : 22500;
        int got;

        CHECK_EQ(audvd_reader_seek_pts(f.r, t, &landed), AUDVD_OK);
        CHECK(landed <= t);
        CHECK(t - landed < 2 * dur);
        got = audvd_reader_read(f.r, buf, 1, &pos);
        CHECK_EQ(got, 1);
        if (got == 1)
            CHECK_EQ(audvd_pos_title_pts(&pos, audvd_pack_pts(buf)), (int64_t)landed);
    }
    teardown(&f);
}

static void tweak_scrambled(synth_disc *s)
{
    s->scrambled_first = 3;
    s->scrambled_count = 4;
}

static void tweak_scrambled_track(synth_disc *s)
{
    s->scrambled_first = 20;
    s->scrambled_count = 4;
}

static void test_scrambled(void)
{
    fixture f;
    uint64_t landed;
    audvd_pos pos;

    if (setup(&f, tweak_scrambled) < 0)
        return;
    expect_run(f.r, 64, 0, 3, 0, 0, F_TITLE | F_CELL | F_TRACK | F_DISC);
    expect_run(f.r, 64, 3, 4, 0, 0, F_ENC);
    expect_run(f.r, 64, 7, 3, 0, 0, 0);
    expect_run(f.r, 64, 10, 5, 1, 1, F_CELL | F_TRACK | F_DISC);

    /* the binary search hits scrambled packs: falls back to interpolating */
    CHECK_EQ(audvd_reader_seek_pts(f.r, 500000, &landed), AUDVD_OK);
    CHECK_EQ(landed, 500000);
    CHECK_EQ(audvd_reader_tell(f.r, &pos), AUDVD_OK);
    CHECK_EQ(pos.track, 0);
    CHECK_EQ(pos.sector, 5);
    teardown(&f);

    if (setup(&f, tweak_scrambled_track) < 0)
        return;
    CHECK_EQ(audvd_reader_seek_pts(f.r, 1350000 + 45000, &landed), AUDVD_OK);
    CHECK_EQ(landed, 1350000 + 45000);
    expect_run(f.r, 64, 22, 2, 2, 3, F_DISC | F_ENC);
    teardown(&f);
}

static void tweak_truncated(synth_disc *s)
{
    s->aob_file_sectors[1] = 10; /* sectors 20..23 are missing */
}

static void test_truncated(void)
{
    fixture f;
    audvd_pos pos;

    if (setup(&f, tweak_truncated) < 0)
        return;
    CHECK_EQ(audvd_reader_seek_track(f.r, 2), AUDVD_OK);
    CHECK_EQ(audvd_reader_read(f.r, buf, 64, &pos), AUDVD_ERR_IO);
    CHECK_EQ(audvd_reader_seek_track(f.r, 1), AUDVD_OK);
    expect_run(f.r, 64, 10, 5, 1, 1, F_CELL | F_TRACK | F_DISC);
    expect_run(f.r, 64, 15, 5, 1, 2, F_CELL);
    CHECK_EQ(audvd_reader_read(f.r, buf, 64, &pos), AUDVD_ERR_IO);
    teardown(&f);
}

static void test_two_readers(void)
{
    fixture f;
    audvd_reader *r2;
    uint8_t one[AUDVD_SECTOR_SIZE];

    if (setup(&f, NULL) < 0)
        return;
    CHECK_EQ(audvd_reader_open(f.disc, 0, &r2), AUDVD_OK);
    if (!r2) {
        teardown(&f);
        return;
    }
    CHECK_EQ(audvd_reader_seek_track(r2, 2), AUDVD_OK);
    expect_run(f.r, 2, 0, 2, 0, 0, F_TITLE | F_CELL | F_TRACK | F_DISC);
    expect_run(r2, 2, 20, 2, 2, 3, F_CELL | F_TRACK | F_DISC);
    /* the raw API keeps its own file too */
    CHECK_EQ(audvd_read_aob_sectors(f.disc, 1, 12, 1, one), 1);
    CHECK_EQ(synth_sector_number(one), 12);
    expect_run(f.r, 2, 2, 2, 0, 0, 0);
    expect_run(r2, 2, 22, 2, 2, 3, 0);
    audvd_reader_close(r2);
    teardown(&f);
}

static void test_rebase(void)
{
    audvd_pos pos = { 0 };

    pos.track_start_pts = 1000;
    pos.track_first_pts = 94;
    CHECK_EQ(audvd_pos_title_pts(&pos, 94), 1000);
    CHECK_EQ(audvd_pos_title_pts(&pos, 194), 1100);
    CHECK_EQ(audvd_pos_title_pts(&pos, 50), 956);
    CHECK_EQ(audvd_pos_title_pts(&pos, -1), -1);

    /* PTS beyond 32 bits, and packets just before the first PTS across the
     * 33-bit wrap */
    pos.track_first_pts = 0xffffff00u;
    CHECK_EQ(audvd_pos_title_pts(&pos, 0xffffff00), 1000);
    CHECK_EQ(audvd_pos_title_pts(&pos, INT64_C(0x100000010)), 1000 + 0x110);
    pos.track_start_pts = 5000;
    pos.track_first_pts = 100;
    CHECK_EQ(audvd_pos_title_pts(&pos, (INT64_C(1) << 33) - 100), 5000 - 200);
}

int main(void)
{
    test_play_order();
    test_ranges();
    test_seek_pts();
    test_scrambled();
    test_truncated();
    test_two_readers();
    test_rebase();

    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
