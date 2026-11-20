/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Audio Manager Information (AUDIO_TS.IFO): the disc's title list.
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define AMGI_MAT_MIN_SIZE 0xd0
#define SRPT_HEADER_SIZE  4
#define SRPT_ENTRY_SIZE   14
#define SRPT_MAX_TITLES   99

int audvd_parse_amg(audvd_disc *d, const audvd_buf *b)
{
    uint32_t att_sector;
    size_t srpt;
    int nb;

    if (!buf_has(b, 0, AMGI_MAT_MIN_SIZE) || memcmp(b->data, "DVDAUDIO-AMG", 12)) {
        audvd_log(d, AUDVD_LOG_ERROR, "AMG: missing DVDAUDIO-AMG identifier");
        return AUDVD_ERR_INVALID;
    }

    /* 0xc4: start sector of the ATT_SRPT (all titles, audio and video) */
    att_sector = rb32(b, 0xc4);
    srpt = (size_t)att_sector * AUDVD_SECTOR_SIZE;
    if (!att_sector || !buf_has(b, srpt, SRPT_HEADER_SIZE)) {
        audvd_log(d, AUDVD_LOG_ERROR, "AMG: bad ATT_SRPT sector %u", att_sector);
        return AUDVD_ERR_INVALID;
    }
    nb = rb16(b, srpt);
    if (!nb || nb > SRPT_MAX_TITLES ||
        !buf_has(b, srpt + SRPT_HEADER_SIZE, (size_t)nb * SRPT_ENTRY_SIZE)) {
        audvd_log(d, AUDVD_LOG_ERROR, "AMG: bad title count %d", nb);
        return AUDVD_ERR_INVALID;
    }

    d->titles = calloc((size_t)nb, sizeof(*d->titles));
    if (!d->titles)
        return AUDVD_ERR_NOMEM;

    for (int i = 0; i < nb; i++) {
        size_t o = srpt + SRPT_HEADER_SIZE + (size_t)i * SRPT_ENTRY_SIZE;
        audvd_title *t = &d->titles[d->nb_titles].pub;
        uint8_t type = rb8(b, o);

        if (!type)
            continue;
        t->index      = d->nb_titles;
        t->number     = i + 1;
        t->type_raw   = type;
        t->audio_only = !!(type & 0x80);
        t->nb_tracks  = rb8(b, o + 1);
        t->length_pts = rb32(b, o + 4);
        t->ats        = rb8(b, o + 8);
        t->ats_title  = rb8(b, o + 9);
        /* provisional, refined from the SAMG by the caller */
        t->group      = (type & 0x0f) >= 1 && (type & 0x0f) <= 9 ? type & 0x0f : 1;
        d->nb_titles++;
    }
    return d->nb_titles ? AUDVD_OK : AUDVD_ERR_INVALID;
}
