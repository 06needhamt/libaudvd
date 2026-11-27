/*
 * libaudvd pack inspection tests on handmade packs.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <string.h>

#include "audvd/audvd.h"
#include "synth.h"

static int failures;

#define CHECK_EQ(a, b) do {                                               \
    long long va_ = (long long)(a), vb_ = (long long)(b);                 \
    if (va_ != vb_) {                                                     \
        fprintf(stderr, "%s:%d: %s == %lld, expected %lld\n", __FILE__,   \
                __LINE__, #a, va_, vb_);                                  \
        failures++;                                                       \
    }                                                                     \
} while (0)

static uint8_t pack[AUDVD_SECTOR_SIZE];

static const uint8_t mpeg2_pack_header[14] = {
    0x00, 0x00, 0x01, 0xba, 0x44, 0x00, 0x04, 0x00, 0x04, 0x01,
    0x01, 0x89, 0xc3, 0xf8,
};

static void put_pts(uint8_t *q, int64_t pts)
{
    q[0] = 0x21 | ((pts >> 29) & 0x0e);
    q[1] = (uint8_t)(pts >> 22);
    q[2] = ((pts >> 14) & 0xfe) | 1;
    q[3] = (uint8_t)(pts >> 7);
    q[4] = ((pts << 1) & 0xfe) | 1;
}

/* Write a PES at off: id, total length after the length field, MPEG-2
 * header with an optional PTS. Returns the offset after it. */
static int put_pes(int off, uint8_t id, int len, uint8_t flags1, int64_t pts)
{
    uint8_t *q = pack + off;
    q[0] = 0; q[1] = 0; q[2] = 1; q[3] = id;
    q[4] = (uint8_t)(len >> 8);
    q[5] = (uint8_t)len;
    q[6] = flags1;
    q[7] = pts >= 0 ? 0x80 : 0;
    q[8] = pts >= 0 ? 5 : 0;
    if (pts >= 0)
        put_pts(q + 9, pts);
    return off + 6 + len;
}

static void fill_padding(int off)
{
    if (off + 6 <= AUDVD_SECTOR_SIZE) {
        int len = AUDVD_SECTOR_SIZE - off - 6;
        pack[off + 2] = 1;
        pack[off + 3] = 0xbe;
        pack[off + 4] = (uint8_t)(len >> 8);
        pack[off + 5] = (uint8_t)len;
        memset(pack + off + 6, 0xff, (size_t)len);
    }
}

static void test_synth_packs(void)
{
    synth_disc s;

    synth_default(&s);
    s.scrambled_first = 5;
    s.scrambled_count = 1;
    for (uint32_t sec = 0; sec < 24; sec++) {
        synth_pack(&s, sec, pack);
        CHECK_EQ(audvd_pack_is_scrambled(pack), sec == 5);
    }
    synth_pack(&s, 2, pack);
    CHECK_EQ(audvd_pack_pts(pack), 94 + 2 * 90000);
    synth_pack(&s, 3, pack);
    CHECK_EQ(audvd_pack_pts(pack), -1);
}

static void test_layouts(void)
{
    int off;

    /* system header and stuffing before the audio PES */
    memset(pack, 0, sizeof(pack));
    memcpy(pack, mpeg2_pack_header, 14);
    pack[13] = 0xf8 | 3;
    off = 17;
    pack[off + 2] = 1;
    pack[off + 3] = 0xbb;
    pack[off + 5] = 6;
    off += 12;
    off = put_pes(off, 0xbd, 100, 0x81, 0x1ffffffffLL);
    fill_padding(off);
    CHECK_EQ(audvd_pack_pts(pack), 0x1ffffffffLL);
    CHECK_EQ(audvd_pack_is_scrambled(pack), 0);

    /* scrambling: both '01' and '11' patterns, PTS still readable */
    pack[off - 100 + 0] = 0x81 | 0x30; /* flags1 of that PES */
    CHECK_EQ(audvd_pack_is_scrambled(pack), 1);
    CHECK_EQ(audvd_pack_pts(pack), 0x1ffffffffLL);

    /* padding-only pack: no PTS, not scrambled */
    memset(pack, 0, sizeof(pack));
    memcpy(pack, mpeg2_pack_header, 14);
    fill_padding(14);
    CHECK_EQ(audvd_pack_pts(pack), -1);
    CHECK_EQ(audvd_pack_is_scrambled(pack), 0);

    /* PES with a PTS flag but a header too short to hold it */
    memset(pack, 0, sizeof(pack));
    memcpy(pack, mpeg2_pack_header, 14);
    off = put_pes(14, 0xbd, 2028, 0x81, 1234);
    pack[14 + 8] = 2;
    CHECK_EQ(audvd_pack_pts(pack), -1);
    (void)off;

    /* MPEG-1 pack header with an MPEG-2 PES */
    memset(pack, 0, sizeof(pack));
    pack[2] = 1; pack[3] = 0xba; pack[4] = 0x21;
    put_pes(12, 0xbd, 2030, 0x81, 777);
    CHECK_EQ(audvd_pack_pts(pack), 777);
}

static void test_malformed(void)
{
    /* zeros, noise, missing start codes */
    memset(pack, 0, sizeof(pack));
    CHECK_EQ(audvd_pack_pts(pack), -1);
    CHECK_EQ(audvd_pack_is_scrambled(pack), 0);
    memset(pack, 0xff, sizeof(pack));
    CHECK_EQ(audvd_pack_pts(pack), -1);
    CHECK_EQ(audvd_pack_is_scrambled(pack), 0);

    memset(pack, 0, sizeof(pack));
    memcpy(pack, mpeg2_pack_header, 14);
    pack[16] = 0x02; /* not a start code */
    CHECK_EQ(audvd_pack_pts(pack), -1);

    /* a length that runs off the end of the pack */
    memset(pack, 0, sizeof(pack));
    memcpy(pack, mpeg2_pack_header, 14);
    pack[16] = 1; pack[17] = 0xbe; pack[18] = 0xff; pack[19] = 0xff;
    CHECK_EQ(audvd_pack_pts(pack), -1);

    /* a PES header ending exactly at the end of the pack */
    memset(pack, 0, sizeof(pack));
    memcpy(pack, mpeg2_pack_header, 14);
    fill_padding(14);
    pack[19] = (uint8_t)(AUDVD_SECTOR_SIZE - 14 - 6 - 8);
    memcpy(pack + AUDVD_SECTOR_SIZE - 8, (uint8_t[8]){ 0, 0, 1, 0xbd, 0, 2, 0x81, 0 }, 8);
    CHECK_EQ(audvd_pack_pts(pack), -1);
    CHECK_EQ(audvd_pack_is_scrambled(pack), 0);

    /* program end code stops the walk */
    memset(pack, 0, sizeof(pack));
    memcpy(pack, mpeg2_pack_header, 14);
    pack[16] = 1; pack[17] = 0xb9;
    put_pes(18, 0xbd, 100, 0x81, 5);
    CHECK_EQ(audvd_pack_pts(pack), -1);
}

int main(void)
{
    test_synth_packs();
    test_layouts();
    test_malformed();

    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
