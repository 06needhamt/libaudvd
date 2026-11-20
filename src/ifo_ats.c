/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Audio Title Set Information (ATS_xx_0.IFO). See doc/FORMAT.md for the
 * layout and how each field was established.
 */

#include <stdlib.h>
#include <string.h>

#include "internal.h"

#define ATSI_MAT_MIN_SIZE   672
#define ATSI_ATTR_OFFSET    0x100
#define ATSI_ATTR_SIZE      16
#define ATSI_DM_OFFSET      0x180
#define ATSI_DM_SIZE        18
#define PGCIT_HEADER_SIZE   8
#define PGCIT_SRP_SIZE      8
#define PGC_HEADER_SIZE     16
#define PGI_SIZE            20
#define CELL_SIZE           12

static audvd_codec attr_codec(uint8_t b)
{
    switch (b) {
    case 0x00: return AUDVD_CODEC_LPCM;
    case 0x01: return AUDVD_CODEC_MLP;
    default:   return AUDVD_CODEC_UNKNOWN;
    }
}

static int parse_pgc(audvd_disc *d, audvd_ats *ats, const audvd_buf *b,
                     size_t pgc_off, size_t table_end, audvd_pgc *pgc)
{
    size_t track_off, cell_off;
    uint64_t start = 0;

    if (!buf_has(b, pgc_off, PGC_HEADER_SIZE) || pgc_off + PGC_HEADER_SIZE > table_end) {
        audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: PGC header out of bounds",
                  ats->number);
        return AUDVD_ERR_INVALID;
    }

    pgc->nb_tracks  = rb8(b, pgc_off + 2);
    pgc->nb_cells   = rb8(b, pgc_off + 3);
    pgc->length_pts = rb32(b, pgc_off + 4);
    track_off       = pgc_off + rb16(b, pgc_off + 10);
    cell_off        = pgc_off + rb16(b, pgc_off + 12);

    if (!pgc->nb_tracks || !pgc->nb_cells) {
        audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: PGC without tracks or cells",
                  ats->number);
        return AUDVD_ERR_INVALID;
    }
    if (!buf_has(b, track_off, (size_t)pgc->nb_tracks * PGI_SIZE) ||
        track_off + (size_t)pgc->nb_tracks * PGI_SIZE > table_end ||
        !buf_has(b, cell_off, (size_t)pgc->nb_cells * CELL_SIZE) ||
        cell_off + (size_t)pgc->nb_cells * CELL_SIZE > table_end) {
        audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: PGC tables out of bounds",
                  ats->number);
        return AUDVD_ERR_INVALID;
    }

    pgc->cells  = calloc((size_t)pgc->nb_cells, sizeof(*pgc->cells));
    pgc->tracks = calloc((size_t)pgc->nb_tracks, sizeof(*pgc->tracks));
    if (!pgc->cells || !pgc->tracks)
        return AUDVD_ERR_NOMEM;

    for (int i = 0; i < pgc->nb_cells; i++) {
        size_t o = cell_off + (size_t)i * CELL_SIZE;
        audvd_cell *c = &pgc->cells[i];
        c->id           = rb8(b, o);
        c->first_sector = rb32(b, o + 4);
        c->last_sector  = rb32(b, o + 8);
        if (c->last_sector < c->first_sector) {
            audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: cell %d ends before it starts",
                      ats->number, i + 1);
            return AUDVD_ERR_INVALID;
        }
    }

    for (int i = 0; i < pgc->nb_tracks; i++) {
        size_t o = track_off + (size_t)i * PGI_SIZE;
        audvd_track *t = &pgc->tracks[i];
        uint8_t f0 = rb8(b, o), f1 = rb8(b, o + 1);

        t->number             = i + 1;
        t->raw_flags[0]       = f0;
        t->raw_flags[1]       = f1;
        t->stc_discontinuity  = !!(f0 & 0x40);
        t->attr_index         = (f0 >> 3) & 7;
        t->downmix_prohibited = !!(f1 & 0x20);
        t->downmix_valid      = !(f1 & 0x10);
        t->downmix_table      = f1 & 0xf;
        t->first_cell         = rb8(b, o + 4) - 1;
        t->first_pts          = rb32(b, o + 6);
        t->length_pts         = rb32(b, o + 10);
        t->pause_pts          = rb32(b, o + 14);
        t->start_pts          = start;
        start += t->length_pts;

        if (t->first_cell < 0 || t->first_cell >= pgc->nb_cells ||
            (i && t->first_cell <= pgc->tracks[i - 1].first_cell)) {
            audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: track %d has bad first cell %d",
                      ats->number, i + 1, t->first_cell + 1);
            return AUDVD_ERR_INVALID;
        }
    }

    for (int i = 0; i < pgc->nb_tracks; i++) {
        audvd_track *t = &pgc->tracks[i];
        int end = i + 1 < pgc->nb_tracks ? pgc->tracks[i + 1].first_cell
                                         : pgc->nb_cells;
        t->nb_cells     = end - t->first_cell;
        t->first_sector = pgc->cells[t->first_cell].first_sector;
        t->last_sector  = pgc->cells[end - 1].last_sector;
        t->attr         = ats->attrs[t->attr_index];
        if (t->attr_index + 1 > ats->nb_attrs)
            ats->nb_attrs = t->attr_index + 1;
    }

    if (start != pgc->length_pts)
        audvd_log(d, AUDVD_LOG_WARNING,
                  "ATS %d: track lengths sum to %llu, PGC length is %u",
                  ats->number, (unsigned long long)start, pgc->length_pts);
    return AUDVD_OK;
}

int audvd_parse_ats(audvd_disc *d, audvd_ats *ats, const audvd_buf *b)
{
    size_t pgcit, table_end;
    uint32_t pgcit_sector, last_byte;
    int ret;

    if (!buf_has(b, 0, ATSI_MAT_MIN_SIZE) || memcmp(b->data, "DVDAUDIO-ATS", 12)) {
        audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: missing DVDAUDIO-ATS identifier",
                  ats->number);
        return AUDVD_ERR_INVALID;
    }

    ats->ats_last_sector  = rb32(b, 0x0c);
    ats->version          = rb16(b, 0x20);
    ats->aob_start_sector = rb32(b, 0xc4);
    pgcit_sector          = rb32(b, 0xcc);

    for (int i = 0; i < AUDVD_MAX_ATTRS; i++) {
        size_t o = ATSI_ATTR_OFFSET + (size_t)i * ATSI_ATTR_SIZE;
        audvd_audio_attr *a = &ats->attrs[i];
        memcpy(a->raw, b->data + o, ATSI_ATTR_SIZE);
        a->codec = attr_codec(a->raw[0]);
        audvd_decode_attr_bytes(a->raw[2], a->raw[3], a->raw[4], a);
    }
    for (int i = 0; i < AUDVD_MAX_DOWNMIX_TABLES; i++)
        audvd_decode_downmix(b->data + ATSI_DM_OFFSET + (size_t)i * ATSI_DM_SIZE,
                             &ats->downmix[i]);

    if (!pgcit_sector || pgcit_sector > (b->size / AUDVD_SECTOR_SIZE)) {
        audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: bad ATS_PGCIT sector %u",
                  ats->number, pgcit_sector);
        return AUDVD_ERR_INVALID;
    }
    pgcit = (size_t)pgcit_sector * AUDVD_SECTOR_SIZE;
    if (!buf_has(b, pgcit, PGCIT_HEADER_SIZE)) {
        audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: ATS_PGCIT out of bounds",
                  ats->number);
        return AUDVD_ERR_INVALID;
    }
    ats->nb_pgcs = rb16(b, pgcit);
    last_byte    = rb32(b, pgcit + 4);
    table_end    = pgcit + (size_t)last_byte + 1;
    if (table_end > b->size) {
        audvd_log(d, AUDVD_LOG_WARNING,
                  "ATS %d: ATS_PGCIT end byte %u beyond the file, clamping",
                  ats->number, last_byte);
        table_end = b->size;
    }
    if (!ats->nb_pgcs ||
        pgcit + PGCIT_HEADER_SIZE + (size_t)ats->nb_pgcs * PGCIT_SRP_SIZE > table_end) {
        audvd_log(d, AUDVD_LOG_ERROR, "ATS %d: bad PGC count %d", ats->number,
                  ats->nb_pgcs);
        ats->nb_pgcs = 0;
        return AUDVD_ERR_INVALID;
    }

    ats->pgcs = calloc((size_t)ats->nb_pgcs, sizeof(*ats->pgcs));
    if (!ats->pgcs) {
        ats->nb_pgcs = 0;
        return AUDVD_ERR_NOMEM;
    }
    for (int i = 0; i < ats->nb_pgcs; i++) {
        size_t srp = pgcit + PGCIT_HEADER_SIZE + (size_t)i * PGCIT_SRP_SIZE;
        audvd_pgc *pgc = &ats->pgcs[i];
        memcpy(pgc->srp_raw, b->data + srp, 4);
        ret = parse_pgc(d, ats, b, pgcit + rb32(b, srp + 4), table_end, pgc);
        if (ret < 0)
            return ret;
    }
    if (!ats->nb_attrs)
        ats->nb_attrs = 1;
    return AUDVD_OK;
}

void audvd_free_ats(audvd_ats *ats)
{
    for (int i = 0; i < ats->nb_pgcs; i++) {
        free(ats->pgcs[i].tracks);
        free(ats->pgcs[i].cells);
    }
    free(ats->pgcs);
    for (int i = 0; i < ats->nb_aob_files; i++)
        free(ats->aob_path[i]);
    memset(ats, 0, sizeof(*ats));
}
