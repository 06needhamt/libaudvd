/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Inspection of the 2048-byte MPEG program stream packs of an AOB.
 */

#include "internal.h"

#define PTS_WRAP (INT64_C(1) << 33)

/*
 * Find the first PES packet of the pack with an MPEG-2 PES header (the
 * '10' marker bits) and return its offset, or -1. System headers, padding
 * and private stream 2 packets have no such header and are skipped.
 */
static int first_mpeg2_pes(const uint8_t *p)
{
    int off;

    if (p[0] || p[1] || p[2] != 1 || p[3] != 0xba)
        return -1;
    if ((p[4] & 0xc0) == 0x40)       /* MPEG-2 pack header */
        off = 14 + (p[13] & 7);
    else if ((p[4] & 0xf0) == 0x20)  /* MPEG-1 pack header */
        off = 12;
    else
        return -1;

    while (off + 6 <= AUDVD_SECTOR_SIZE) {
        int id, len;

        if (p[off] || p[off + 1] || p[off + 2] != 1)
            return -1;
        id  = p[off + 3];
        len = p[off + 4] << 8 | p[off + 5];
        if (id < 0xbb)               /* not a system header or PES (0xb9 end code, ...) */
            return -1;
        if (id != 0xbb && id != 0xbe && id != 0xbf && len >= 3 &&
            off + 9 <= AUDVD_SECTOR_SIZE && (p[off + 6] & 0xc0) == 0x80)
            return off;
        off += 6 + len;
    }
    return -1;
}

int audvd_pack_is_scrambled(const uint8_t *pack)
{
    int off = first_mpeg2_pes(pack);
    return off >= 0 && (pack[off + 6] & 0x30) != 0;
}

int64_t audvd_pack_pts(const uint8_t *pack)
{
    int off = first_mpeg2_pes(pack);
    const uint8_t *q;

    if (off < 0 || !(pack[off + 7] & 0x80) || pack[off + 8] < 5 ||
        off + 14 > AUDVD_SECTOR_SIZE)
        return -1;
    q = pack + off + 9;
    return (int64_t)((q[0] >> 1) & 7) << 30 | (int64_t)q[1] << 22 |
           (int64_t)(q[2] >> 1) << 15 | (int64_t)q[3] << 7 | (q[4] >> 1);
}

int64_t audvd_pos_title_pts(const audvd_pos *pos, int64_t pkt_pts)
{
    int64_t delta;

    if (pkt_pts < 0)
        return -1;
    delta = (pkt_pts - (int64_t)pos->track_first_pts) & (PTS_WRAP - 1);
    if (delta >= PTS_WRAP / 2)       /* slightly before the first PTS */
        delta -= PTS_WRAP;
    return (int64_t)pos->track_start_pts + delta;
}
