/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Title reader: a title's AOB sectors in play order, with track/cell
 * positions, CPPM detection and seeking.
 */

#include <stdlib.h>

#include "internal.h"

#define PROBE_SECTORS 16

struct audvd_reader {
    const audvd_disc *d;
    const audvd_pgc  *pgc;
    int               ats;
    int               first_track, last_track; /* inclusive */
    int               first_cell, end_cell;    /* end exclusive */
    int               cell;                    /* current cell */
    uint32_t          offset;                  /* sectors into the cell */
    int               pending;                 /* AUDVD_POS_* for the next run */
    int               warned_cppm;
    audvd_aob_cache   cache;
    uint8_t           probe[AUDVD_SECTOR_SIZE];
};

static uint32_t cell_len(const audvd_cell *c)
{
    return c->last_sector - c->first_sector + 1;
}

static int track_of_cell(const audvd_reader *r, int cell)
{
    for (int t = r->pgc->nb_tracks - 1; t >= 0; t--)
        if (r->pgc->tracks[t].first_cell <= cell)
            return t;
    return 0;
}

/* Move to the start of a cell; flags are added to the derived ones. */
static void enter_cell(audvd_reader *r, int cell, int flags)
{
    r->cell    = cell;
    r->offset  = 0;
    r->pending = flags;
    if (cell >= r->end_cell)
        return;
    r->pending |= AUDVD_POS_CELL_START;
    if (cell == r->first_cell)
        r->pending |= AUDVD_POS_TITLE_START;
    for (int t = r->first_track; t <= r->last_track; t++) {
        const audvd_track *tr = &r->pgc->tracks[t];
        if (tr->first_cell == cell) {
            r->pending |= AUDVD_POS_TRACK_START;
            if (tr->stc_discontinuity)
                r->pending |= AUDVD_POS_DISCONTINUITY;
        }
    }
}

static void fill_pos(const audvd_reader *r, audvd_pos *pos)
{
    const audvd_track *tr = &r->pgc->tracks[track_of_cell(r, r->cell)];

    pos->track           = tr->number - 1;
    pos->cell            = r->cell;
    pos->sector          = r->pgc->cells[r->cell].first_sector + r->offset;
    pos->flags           = r->pending;
    pos->track_start_pts = tr->start_pts;
    pos->track_first_pts = tr->first_pts;
}

int audvd_reader_open(audvd_disc *d, int title, audvd_reader **out)
{
    audvd_reader *r;

    *out = NULL;
    if (title < 0 || title >= d->nb_titles || !d->titles[title].pgc)
        return AUDVD_ERR_RANGE;
    r = calloc(1, sizeof(*r));
    if (!r)
        return AUDVD_ERR_NOMEM;
    r->d         = d;
    r->pgc       = d->titles[title].pgc;
    r->ats       = d->titles[title].pub.ats;
    r->cache.ats = -1;
    audvd_reader_set_tracks(r, 0, r->pgc->nb_tracks - 1);
    *out = r;
    return AUDVD_OK;
}

void audvd_reader_close(audvd_reader *r)
{
    if (!r)
        return;
    audvd_aob_cache_close(&r->cache);
    free(r);
}

int audvd_reader_set_tracks(audvd_reader *r, int first, int last)
{
    const audvd_track *lt;

    if (first < 0 || last < first || last >= r->pgc->nb_tracks)
        return AUDVD_ERR_RANGE;
    lt = &r->pgc->tracks[last];
    r->first_track = first;
    r->last_track  = last;
    r->first_cell  = r->pgc->tracks[first].first_cell;
    r->end_cell    = lt->first_cell + lt->nb_cells;
    enter_cell(r, r->first_cell, 0);
    return AUDVD_OK;
}

int audvd_reader_tell(const audvd_reader *r, audvd_pos *pos)
{
    if (r->cell >= r->end_cell)
        return AUDVD_ERR_EOF;
    fill_pos(r, pos);
    return AUDVD_OK;
}

int audvd_reader_read(audvd_reader *r, uint8_t *buf, int max_sectors,
                      audvd_pos *pos)
{
    const audvd_cell *c;
    uint32_t left;
    int n, got, scrambled;

    if (max_sectors < 1)
        return AUDVD_ERR_RANGE;
    if (r->cell >= r->end_cell)
        return AUDVD_ERR_EOF;

    c    = &r->pgc->cells[r->cell];
    left = cell_len(c) - r->offset;
    n    = (uint32_t)max_sectors < left ? max_sectors : (int)left;

    got = audvd_aob_read(r->d, &r->cache, r->ats, c->first_sector + r->offset,
                         n, buf);
    if (got < 0)
        return got;
    if (got == 0) {
        audvd_log(r->d, AUDVD_LOG_ERROR,
                  "ATS %d: AOB data ends at sector %u, inside cell %d",
                  r->ats, c->first_sector + r->offset, r->cell + 1);
        return AUDVD_ERR_IO;
    }
    n = got;

    /* one run has a single scrambled state */
    scrambled = audvd_pack_is_scrambled(buf);
    for (int k = 1; k < n; k++) {
        if (audvd_pack_is_scrambled(buf + (size_t)k * AUDVD_SECTOR_SIZE) != scrambled) {
            n = k;
            break;
        }
    }
    if (scrambled && !r->warned_cppm) {
        audvd_log(r->d, AUDVD_LOG_WARNING,
                  "ATS %d: CPPM-scrambled sectors from %u; decryption is not supported",
                  r->ats, c->first_sector + r->offset);
        r->warned_cppm = 1;
    }

    fill_pos(r, pos);
    if (scrambled)
        pos->flags |= AUDVD_POS_ENCRYPTED;
    r->pending = 0;
    r->offset += (uint32_t)n;
    if (r->offset >= cell_len(c))
        enter_cell(r, r->cell + 1, 0);
    return n;
}

int audvd_reader_seek_track(audvd_reader *r, int track)
{
    if (track < r->first_track || track > r->last_track)
        return AUDVD_ERR_RANGE;
    enter_cell(r, r->pgc->tracks[track].first_cell, AUDVD_POS_DISCONTINUITY);
    return AUDVD_OK;
}

/* Map a sector index within a track to its cell and offset. */
static void track_locate(const audvd_reader *r, const audvd_track *tr,
                         uint64_t idx, int *cell, uint32_t *offset)
{
    for (int c = tr->first_cell; c < tr->first_cell + tr->nb_cells; c++) {
        uint32_t len = cell_len(&r->pgc->cells[c]);
        if (idx < len || c == tr->first_cell + tr->nb_cells - 1) {
            *cell   = c;
            *offset = (uint32_t)(idx < len ? idx : len - 1);
            return;
        }
        idx -= len;
    }
}

/*
 * From sector index idx of the track, find the first pack carrying a PTS
 * within PROBE_SECTORS. Returns 0 and sets *found and *pts, 1 if there is
 * none before the window or the track ends, or -1 if a pack in the way is
 * scrambled or unreadable.
 */
static int probe(audvd_reader *r, const audvd_track *tr, uint64_t total,
                 uint64_t idx, uint64_t *found, int64_t *pts)
{
    for (int k = 0; k < PROBE_SECTORS && idx + k < total; k++) {
        int cell;
        uint32_t off;
        int64_t p;

        track_locate(r, tr, idx + k, &cell, &off);
        if (audvd_aob_read(r->d, &r->cache, r->ats,
                           r->pgc->cells[cell].first_sector + off, 1, r->probe) != 1)
            return -1;
        if (audvd_pack_is_scrambled(r->probe))
            return -1;
        if ((p = audvd_pack_pts(r->probe)) >= 0) {
            *found = idx + k;
            *pts   = p;
            return 0;
        }
    }
    return 1;
}

int audvd_reader_seek_pts(audvd_reader *r, uint64_t title_pts, uint64_t *landed)
{
    const audvd_track *tr = NULL;
    uint64_t total = 0, lo, hi, target, found, land;
    int64_t pts, lo_pts;
    int cell, interp = 0;
    uint32_t off;

    for (int t = r->first_track; t <= r->last_track; t++) {
        const audvd_track *c = &r->pgc->tracks[t];
        if (title_pts >= c->start_pts && title_pts < c->start_pts + c->length_pts) {
            tr = c;
            break;
        }
    }
    if (!tr)
        return AUDVD_ERR_RANGE;
    for (int c = tr->first_cell; c < tr->first_cell + tr->nb_cells; c++)
        total += cell_len(&r->pgc->cells[c]);
    target = tr->first_pts + (title_pts - tr->start_pts);

    /* largest sector index whose pack PTS is <= target */
    lo = 0;
    hi = total;
    /* the start of the track is at its first PTS whatever pack 0 holds;
     * probing it detects scrambled tracks */
    if (probe(r, tr, total, 0, &found, &lo_pts) < 0) {
        interp = 1;
    } else {
        lo_pts = tr->first_pts;
        while (hi - lo > 1) {
            uint64_t mid = lo + (hi - lo) / 2;
            int ret = probe(r, tr, total, mid, &found, &pts);
            if (ret < 0) {
                interp = 1;
                break;
            }
            if (!ret && found < hi && (uint64_t)pts <= target) {
                lo     = found;
                lo_pts = pts;
            } else {
                hi = mid;
            }
        }
    }

    if (interp) {
        lo   = tr->length_pts ? total * (title_pts - tr->start_pts) / tr->length_pts : 0;
        land = title_pts;
    } else {
        land = tr->start_pts + (uint64_t)(lo_pts > (int64_t)tr->first_pts
                                          ? lo_pts - tr->first_pts : 0);
    }

    track_locate(r, tr, lo, &cell, &off);
    enter_cell(r, cell, AUDVD_POS_DISCONTINUITY);
    if (off) {
        r->offset   = off;
        r->pending &= ~(AUDVD_POS_CELL_START | AUDVD_POS_TRACK_START |
                        AUDVD_POS_TITLE_START);
    }
    if (landed)
        *landed = land;
    return AUDVD_OK;
}
