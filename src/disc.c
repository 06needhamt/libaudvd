/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Opening a disc: loading the IFOs, linking titles to their ATS PGCs and
 * cross-checking against the SAMG; the public accessors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

static int load_ats(audvd_disc *d, int n)
{
    audvd_ats *ats = &d->ats[n - 1];
    char base[16];
    int ret = AUDVD_ERR_NOTFOUND;

    snprintf(base, sizeof(base), "ATS_%02d_0", n);
    for (int backup = 0; backup < 2; backup++) {
        uint8_t *data = NULL;
        audvd_buf b;

        if (backup && (d->flags & AUDVD_OPEN_NO_BUP))
            break;
        ret = audvd_load_ifo(d, base, backup, &b, &data);
        if (ret < 0)
            continue;
        audvd_free_ats(ats);
        ats->number = n;
        ret = audvd_parse_ats(d, ats, &b);
        free(data);
        if (ret == AUDVD_OK) {
            if (backup)
                audvd_log(d, AUDVD_LOG_WARNING, "ATS %d: using %s.BUP", n, base);
            break;
        }
    }
    if (ret < 0) {
        audvd_free_ats(ats);
        return ret;
    }

    for (int i = 0; i < AUDVD_MAX_AOB_FILES; i++) {
        char name[16];
        char *p;
        uint32_t sectors;

        snprintf(name, sizeof(name), "ATS_%02d_%d.AOB", n, i + 1);
        p = audvd_find_file(d->dir, name);
        if (!p)
            break;
        if (audvd_file_sectors(p, &sectors) < 0) {
            free(p);
            break;
        }
        ats->aob_path[i]    = p;
        ats->aob_sectors[i] = sectors;
        ats->nb_aob_files   = i + 1;
    }
    ats->present = 1;
    ats->aob_lba = -1;
    return AUDVD_OK;
}

static int load_samg(audvd_disc *d)
{
    uint8_t *data = NULL;
    audvd_buf b;
    int ret = audvd_load_ifo(d, "AUDIO_PP", 0, &b, &data);

    if (ret < 0)
        return ret;
    ret = audvd_parse_samg(d, &b);
    free(data);
    return ret;
}

static int load_amg(audvd_disc *d)
{
    int ret = AUDVD_ERR_NOTFOUND;

    for (int backup = 0; backup < 2; backup++) {
        uint8_t *data = NULL;
        audvd_buf b;

        if (backup && (d->flags & AUDVD_OPEN_NO_BUP))
            break;
        ret = audvd_load_ifo(d, "AUDIO_TS", backup, &b, &data);
        if (ret < 0)
            continue;
        ret = audvd_parse_amg(d, &b);
        free(data);
        if (ret == AUDVD_OK)
            break;
        free(d->titles);
        d->titles    = NULL;
        d->nb_titles = 0;
    }
    return ret;
}

/* No usable AMG: one title per ATS PGC. */
static int titles_from_ats(audvd_disc *d)
{
    int nb = 0;

    for (int i = 0; i < d->nb_ats; i++)
        nb += d->ats[i].nb_pgcs;
    if (!nb)
        return AUDVD_ERR_NOTFOUND;
    d->titles = calloc((size_t)nb, sizeof(*d->titles));
    if (!d->titles)
        return AUDVD_ERR_NOMEM;
    for (int i = 0; i < d->nb_ats; i++) {
        for (int j = 0; j < d->ats[i].nb_pgcs; j++) {
            audvd_title *t = &d->titles[d->nb_titles].pub;
            t->index      = d->nb_titles;
            t->number     = d->nb_titles + 1;
            t->group      = 1;
            t->audio_only = 1;
            t->ats        = i + 1;
            t->ats_title  = j + 1;
            t->length_pts = d->ats[i].pgcs[j].length_pts;
            d->nb_titles++;
        }
    }
    return AUDVD_OK;
}

static const audvd_samg_track *samg_find(const audvd_disc *d, uint32_t sector,
                                         uint32_t length_pts)
{
    for (int i = 0; i < d->nb_samg_tracks; i++) {
        const audvd_samg_track *s = &d->samg_tracks[i];
        if (s->first_sector == sector && s->length_pts == length_pts)
            return s;
    }
    return NULL;
}

/*
 * The SAMG lists tracks with absolute sectors while the ATS uses sectors
 * relative to its AOBs: find the offset that makes the ATS tracks agree
 * with SAMG entries of the same length and first PTS.
 */
static void locate_ats(audvd_disc *d, audvd_ats *ats)
{
    for (int p = 0; p < ats->nb_pgcs; p++) {
        const audvd_pgc *pgc = &ats->pgcs[p];
        const audvd_track *t0 = &pgc->tracks[0];

        for (int i = 0; i < d->nb_samg_tracks; i++) {
            const audvd_samg_track *s = &d->samg_tracks[i];
            int64_t lba = (int64_t)s->first_sector - t0->first_sector;
            int ok = 1;

            if (s->length_pts != t0->length_pts || s->first_pts != t0->first_pts ||
                lba < 0)
                continue;
            for (int k = 1; k < pgc->nb_tracks && ok; k++)
                ok = !!samg_find(d, (uint32_t)(lba + pgc->tracks[k].first_sector),
                                 pgc->tracks[k].length_pts);
            if (ok) {
                ats->aob_lba = lba;
                return;
            }
        }
    }
}

static void link_titles(audvd_disc *d)
{
    for (int i = 0; i < d->nb_titles; i++) {
        audvd_title_int *ti = &d->titles[i];
        audvd_title *t = &ti->pub;
        audvd_ats *ats;
        const audvd_samg_track *s;

        if (!t->audio_only)
            continue;
        if (t->ats < 1 || t->ats > d->nb_ats || !d->ats[t->ats - 1].present ||
            t->ats_title < 1 || t->ats_title > d->ats[t->ats - 1].nb_pgcs) {
            audvd_log(d, AUDVD_LOG_WARNING,
                      "title %d: ATS %d title %d does not exist", t->number,
                      t->ats, t->ats_title);
            t->nb_tracks = 0;
            continue;
        }
        ats = &d->ats[t->ats - 1];
        ti->pgc = &ats->pgcs[t->ats_title - 1];
        if (t->nb_tracks != ti->pgc->nb_tracks || t->length_pts != ti->pgc->length_pts)
            audvd_log(d, AUDVD_LOG_WARNING,
                      "title %d: AMG lists %d tracks / %llu PTS, ATS has %d / %u",
                      t->number, t->nb_tracks, (unsigned long long)t->length_pts,
                      ti->pgc->nb_tracks, ti->pgc->length_pts);
        t->nb_tracks  = ti->pgc->nb_tracks;
        t->length_pts = ti->pgc->length_pts;

        if (ats->aob_lba >= 0 &&
            (s = samg_find(d, (uint32_t)(ats->aob_lba + ti->pgc->tracks[0].first_sector),
                           ti->pgc->tracks[0].length_pts)) &&
            s->group >= 1 && s->group <= 9) {
            if (s->group != t->group)
                audvd_log(d, AUDVD_LOG_DEBUG, "title %d: group %d from SAMG",
                          t->number, s->group);
            t->group = s->group;
        }
    }

    for (int i = 0; i < d->nb_titles; i++)
        if (d->titles[i].pub.group > d->nb_groups)
            d->nb_groups = d->titles[i].pub.group;
}

int audvd_open(const char *path, const audvd_open_opts *opts, audvd_disc **out)
{
    audvd_disc *d;
    int ret;

    *out = NULL;
    d = calloc(1, sizeof(*d));
    if (!d)
        return AUDVD_ERR_NOMEM;
    if (opts) {
        d->log        = opts->log;
        d->log_opaque = opts->log_opaque;
        d->flags      = opts->flags;
    }
    d->aob.ats = -1;

    d->dir = audvd_find_audio_ts(path);
    if (!d->dir) {
        audvd_log(d, AUDVD_LOG_ERROR, "%s: no AUDIO_TS directory", path);
        ret = AUDVD_ERR_NOTFOUND;
        goto fail;
    }

    if (load_samg(d) < 0)
        audvd_log(d, AUDVD_LOG_INFO, "no usable SAMG (AUDIO_PP.IFO)");

    for (int n = 1; n <= AUDVD_MAX_ATS; n++) {
        ret = load_ats(d, n);
        if (ret == AUDVD_ERR_NOMEM)
            goto fail;
        if (ret == AUDVD_OK) {
            d->nb_ats = n;
            locate_ats(d, &d->ats[n - 1]);
        } else if (ret != AUDVD_ERR_NOTFOUND) {
            audvd_log(d, AUDVD_LOG_ERROR, "ATS %d unusable: %s", n,
                      audvd_strerror(ret));
        }
    }

    ret = AUDVD_ERR_NOTFOUND;
    if (!(d->flags & AUDVD_OPEN_IGNORE_AMG))
        ret = load_amg(d);
    if (ret == AUDVD_ERR_NOMEM)
        goto fail;
    if (ret < 0) {
        if (!(d->flags & AUDVD_OPEN_IGNORE_AMG))
            audvd_log(d, AUDVD_LOG_WARNING,
                      "no usable AMG (AUDIO_TS.IFO), using the ATS title lists");
        ret = titles_from_ats(d);
        if (ret == AUDVD_ERR_NOMEM)
            goto fail;
    }
    if (ret < 0 && !d->nb_samg_tracks) {
        audvd_log(d, AUDVD_LOG_ERROR, "no DVD-Audio titles found");
        ret = AUDVD_ERR_NOTFOUND;
        goto fail;
    }
    link_titles(d);

    *out = d;
    return AUDVD_OK;

fail:
    audvd_close(d);
    return ret;
}

void audvd_close(audvd_disc *d)
{
    if (!d)
        return;
    audvd_aob_cache_close(&d->aob);
    for (int i = 0; i < AUDVD_MAX_ATS; i++)
        audvd_free_ats(&d->ats[i]);
    free(d->titles);
    free(d->samg_tracks);
    free(d->dir);
    free(d);
}

/* ---- accessors ---- */

int audvd_num_ats(const audvd_disc *d)
{
    return d->nb_ats;
}

static const audvd_ats *get_ats(const audvd_disc *d, int n)
{
    if (n < 1 || n > d->nb_ats || !d->ats[n - 1].present)
        return NULL;
    return &d->ats[n - 1];
}

int audvd_get_ats(const audvd_disc *d, int n, audvd_ats_info *out)
{
    const audvd_ats *ats = get_ats(d, n);

    if (!ats)
        return AUDVD_ERR_RANGE;
    memset(out, 0, sizeof(*out));
    out->number       = n;
    out->nb_titles    = ats->nb_pgcs;
    out->version      = ats->version;
    out->nb_attrs     = ats->nb_attrs;
    memcpy(out->attrs, ats->attrs, sizeof(out->attrs));
    out->nb_aob_files = ats->nb_aob_files;
    for (int i = 0; i < ats->nb_aob_files; i++)
        out->aob_sectors += ats->aob_sectors[i];
    out->aob_lba      = ats->aob_lba;
    return AUDVD_OK;
}

int audvd_get_downmix(const audvd_disc *d, int n, int table, audvd_downmix *out)
{
    const audvd_ats *ats = get_ats(d, n);

    if (!ats || table < 0 || table >= AUDVD_MAX_DOWNMIX_TABLES)
        return AUDVD_ERR_RANGE;
    *out = ats->downmix[table];
    return AUDVD_OK;
}

int audvd_num_groups(const audvd_disc *d)
{
    return d->nb_groups;
}

int audvd_num_titles(const audvd_disc *d)
{
    return d->nb_titles;
}

int audvd_get_title(const audvd_disc *d, int title, audvd_title *out)
{
    if (title < 0 || title >= d->nb_titles)
        return AUDVD_ERR_RANGE;
    *out = d->titles[title].pub;
    return AUDVD_OK;
}

static const audvd_pgc *title_pgc(const audvd_disc *d, int title)
{
    if (title < 0 || title >= d->nb_titles)
        return NULL;
    return d->titles[title].pgc;
}

int audvd_get_track(const audvd_disc *d, int title, int track, audvd_track *out)
{
    const audvd_pgc *pgc = title_pgc(d, title);

    if (!pgc || track < 0 || track >= pgc->nb_tracks)
        return AUDVD_ERR_RANGE;
    *out = pgc->tracks[track];
    return AUDVD_OK;
}

int audvd_title_num_cells(const audvd_disc *d, int title)
{
    const audvd_pgc *pgc = title_pgc(d, title);
    return pgc ? pgc->nb_cells : 0;
}

int audvd_get_cell(const audvd_disc *d, int title, int cell, audvd_cell *out)
{
    const audvd_pgc *pgc = title_pgc(d, title);

    if (!pgc || cell < 0 || cell >= pgc->nb_cells)
        return AUDVD_ERR_RANGE;
    *out = pgc->cells[cell];
    return AUDVD_OK;
}

int audvd_samg_num_tracks(const audvd_disc *d)
{
    return d->nb_samg_tracks;
}

int audvd_samg_get_track(const audvd_disc *d, int i, audvd_samg_track *out)
{
    if (i < 0 || i >= d->nb_samg_tracks)
        return AUDVD_ERR_RANGE;
    *out = d->samg_tracks[i];
    return AUDVD_OK;
}

/* ---- raw AOB access ---- */

int audvd_read_aob_sectors(audvd_disc *d, int n, uint32_t sector,
                           int nb_sectors, uint8_t *buf)
{
    return audvd_aob_read(d, &d->aob, n, sector, nb_sectors, buf);
}
