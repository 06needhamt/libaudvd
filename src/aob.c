/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Sector reads across the ATS_xx_1..9.AOB files of a title set, which
 * form a single address space.
 */

#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/types.h>

#include "internal.h"

void audvd_aob_cache_close(audvd_aob_cache *c)
{
    if (c->fp)
        fclose(c->fp);
    c->fp  = NULL;
    c->ats = -1;
}

int audvd_aob_read(const audvd_disc *d, audvd_aob_cache *c, int n,
                   uint32_t sector, int nb_sectors, uint8_t *buf)
{
    const audvd_ats *ats;
    int done = 0;

    if (n < 1 || n > d->nb_ats || !d->ats[n - 1].present || nb_sectors < 0)
        return AUDVD_ERR_RANGE;
    ats = &d->ats[n - 1];

    while (done < nb_sectors) {
        uint64_t s = (uint64_t)sector + done;
        int idx = 0, want;
        size_t got;

        while (idx < ats->nb_aob_files && s >= ats->aob_sectors[idx])
            s -= ats->aob_sectors[idx++];
        if (idx >= ats->nb_aob_files)
            break;

        if (!c->fp || c->ats != n || c->index != idx) {
            audvd_aob_cache_close(c);
            c->fp = fopen(ats->aob_path[idx], "rb");
            if (!c->fp)
                return done ? done : AUDVD_ERR_IO;
            c->ats   = n;
            c->index = idx;
        }
        want = nb_sectors - done;
        if ((uint64_t)want > ats->aob_sectors[idx] - s)
            want = (int)(ats->aob_sectors[idx] - s);
        if (fseeko(c->fp, (off_t)(s * AUDVD_SECTOR_SIZE), SEEK_SET))
            return done ? done : AUDVD_ERR_IO;
        got = fread(buf + (size_t)done * AUDVD_SECTOR_SIZE, AUDVD_SECTOR_SIZE,
                    (size_t)want, c->fp);
        done += (int)got;
        if (got != (size_t)want)
            return done ? done : AUDVD_ERR_IO;
    }
    return done;
}
