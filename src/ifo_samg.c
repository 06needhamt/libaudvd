/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Simple Audio Manager (AUDIO_PP.IFO): the flat track list read by
 * audio-only players. Sector addresses in it are absolute disc sectors.
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define SAMG_HEADER_SIZE 16
#define SAMG_TRACK_SIZE  52
#define SAMG_MAX_TRACKS  314

int audvd_parse_samg(audvd_disc *d, const audvd_buf *b)
{
    int nb;

    if (!buf_has(b, 0, SAMG_HEADER_SIZE) || memcmp(b->data, "DVDAUDIOSAPP", 12)) {
        audvd_log(d, AUDVD_LOG_WARNING, "SAMG: missing DVDAUDIOSAPP identifier");
        return AUDVD_ERR_INVALID;
    }
    nb = rb16(b, 12);
    if (nb > SAMG_MAX_TRACKS || !buf_has(b, SAMG_HEADER_SIZE, (size_t)nb * SAMG_TRACK_SIZE)) {
        audvd_log(d, AUDVD_LOG_WARNING, "SAMG: bad track count %d", nb);
        return AUDVD_ERR_INVALID;
    }
    if (!nb)
        return AUDVD_OK;

    d->samg_tracks = calloc((size_t)nb, sizeof(*d->samg_tracks));
    if (!d->samg_tracks)
        return AUDVD_ERR_NOMEM;
    d->nb_samg_tracks = nb;

    for (int i = 0; i < nb; i++) {
        size_t o = SAMG_HEADER_SIZE + (size_t)i * SAMG_TRACK_SIZE;
        audvd_samg_track *t = &d->samg_tracks[i];

        t->group          = rb8(b, o + 2);
        t->track          = rb8(b, o + 3);
        t->first_pts      = rb32(b, o + 4);
        t->length_pts     = rb32(b, o + 8);
        t->first_in_group = !!(rb8(b, o + 16) & 0x80);
        memcpy(t->attr.raw, b->data + o + 16, 4);
        t->attr.codec     = AUDVD_CODEC_UNKNOWN;
        audvd_decode_attr_bytes(rb8(b, o + 17), rb8(b, o + 18), rb8(b, o + 19),
                                &t->attr);
        t->first_sector   = rb32(b, o + 40);
        t->first_sector_2 = rb32(b, o + 44);
        t->last_sector    = rb32(b, o + 48);
    }
    return AUDVD_OK;
}
