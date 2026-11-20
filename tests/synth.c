/*
 * Synthetic DVD-Audio zone generator for the tests. The layout mirrors
 * the one documented in doc/FORMAT.md and observed on real discs.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synth.h"

#define SECTOR 2048

static void w16(uint8_t *p, uint32_t v) { p[0] = v >> 8; p[1] = v; }
static void w32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

void synth_default(synth_disc *s)
{
    static const uint8_t dm2[16] = {
        0x00, 0xff,  /* L   -> L only        */
        0xff, 0x00,  /* R   -> R only        */
        0x32, 0xff,  /* Ls  -> L at -10 dB   */
        0xff, 0x32,  /* Rs  -> R at -10 dB   */
        0x1e, 0x1e,  /* C   -> both at -6 dB */
        0xff, 0xff,  /* LFE muted            */
    };

    memset(s, 0, sizeof(*s));
    s->nb_tracks = 3;
    s->tracks[0] = (synth_track){ 900000, 94, 1, 10, 0, 2, 1 };
    s->tracks[1] = (synth_track){ 450000, 100, 2, 5, 0, 0, 0 };
    s->tracks[2] = (synth_track){ 90000, 85, 1, 4, 1, 0, 0 };
    memcpy(s->attr[0], (uint8_t[5]){ 0x00, 0x00, 0x22, 0x00, 0x14 }, 5);
    memcpy(s->attr[1], (uint8_t[5]){ 0x01, 0x00, 0x2f, 0x1f, 0x01 }, 5);
    memcpy(s->dm_codes[2], dm2, sizeof(dm2));
    s->aob_lba          = 1707;
    s->samg_group       = 3;
    s->att_type         = 0xc1;
    s->with_video_title = 1;
    s->aob_file_sectors[0] = 10;
    s->aob_file_sectors[1] = 14;
}

static uint32_t total_len(const synth_disc *s)
{
    uint32_t t = 0;
    for (int i = 0; i < s->nb_tracks; i++)
        t += s->tracks[i].length_pts;
    return t;
}

static void track_sectors(const synth_disc *s, int i, uint32_t *first,
                          uint32_t *last)
{
    uint32_t sec = 0;
    for (int k = 0; k < i; k++)
        sec += s->tracks[k].nb_cells * s->tracks[k].cell_sectors;
    *first = sec;
    *last  = sec + s->tracks[i].nb_cells * s->tracks[i].cell_sectors - 1;
}

static void srpt(uint8_t *p, const synth_disc *s, uint8_t type)
{
    int nb = 1 + !!s->with_video_title;
    w16(p, nb);
    w16(p + 2, 4 + 14 * nb - 1);
    p += 4;
    p[0] = type;
    p[1] = s->nb_tracks;
    w32(p + 4, total_len(s));
    p[8] = 1;
    p[9] = 1;
    w32(p + 10, 1375);
    if (s->with_video_title) {
        p += 14;
        p[0] = 0x42;
        p[1] = 1;
        p[2] = 1;
        w32(p + 4, 180180);
        p[8] = 1;
        p[9] = 1;
        w32(p + 10, 1845179);
    }
}

uint8_t *synth_amg(const synth_disc *s, size_t *size)
{
    uint8_t *b = calloc(5, SECTOR);
    if (!b)
        return NULL;
    memcpy(b, "DVDAUDIO-AMG", 12);
    w32(b + 0x0c, 0x265);
    w32(b + 0x1c, 4);
    w16(b + 0x20, 0x0011);
    w32(b + 0x80, 0x7ff);
    w32(b + 0xc4, 1);
    w32(b + 0xc8, 2);
    w32(b + 0xcc, 3);
    srpt(b + 1 * SECTOR, s, s->att_type);
    srpt(b + 2 * SECTOR, s, (uint8_t)((s->att_type & ~0x40) | 0x80));
    *size = 5 * SECTOR;
    return b;
}

uint8_t *synth_samg(const synth_disc *s, size_t *size)
{
    uint8_t *b = calloc(2, SECTOR);
    if (!b)
        return NULL;
    memcpy(b, "DVDAUDIOSAPP", 12);
    w16(b + 12, s->nb_tracks);
    w16(b + 14, 0x0200);
    for (int i = 0; i < s->nb_tracks; i++) {
        uint8_t *e = b + 16 + 52 * i;
        const synth_track *t = &s->tracks[i];
        const uint8_t *a = s->attr[t->attr];
        uint32_t first, last;

        track_sectors(s, i, &first, &last);
        e[2] = s->samg_group;
        e[3] = i + 1;
        w32(e + 4, t->first_pts);
        w32(e + 8, t->length_pts);
        e[16] = 0x58 | (i ? 0 : 0x80);
        e[17] = a[2];
        e[18] = a[3];
        e[19] = a[4];
        w32(e + 40, s->aob_lba + first);
        w32(e + 44, s->aob_lba + first);
        w32(e + 48, s->aob_lba + last);
    }
    *size = 2 * SECTOR;
    return b;
}

uint8_t *synth_ats(const synth_disc *s, size_t *size)
{
    uint8_t *b = calloc(2, SECTOR);
    uint8_t *t, *pgc;
    int nb_cells = 0, cell = 0;
    uint32_t sec = 0;
    size_t track_off = 0x10, cell_off;

    if (!b)
        return NULL;
    for (int i = 0; i < s->nb_tracks; i++)
        nb_cells += s->tracks[i].nb_cells;
    cell_off = track_off + 20 * (size_t)s->nb_tracks;

    memcpy(b, "DVDAUDIO-ATS", 12);
    w16(b + 0x20, 0x0011);
    w32(b + 0x1c, 1);
    w32(b + 0x80, 0x7ff);
    w32(b + 0xc4, 2);
    w32(b + 0xcc, 1);
    for (int a = 0; a < 2; a++)
        memcpy(b + 0x100 + 16 * a, s->attr[a], 5);
    for (int d = 0; d < 16; d++)
        memcpy(b + 0x180 + 18 * d + 2, s->dm_codes[d], 16);

    t = b + SECTOR;
    w16(t, 1);
    w32(t + 4, 0x10 + cell_off + 12 * nb_cells - 1);
    memcpy(t + 8, (uint8_t[4]){ 0x81, 0x01, 0x01, 0x00 }, 4);
    w32(t + 12, 0x10);
    pgc = t + 0x10;
    pgc[2] = s->nb_tracks;
    pgc[3] = nb_cells;
    w32(pgc + 4, total_len(s));
    w16(pgc + 10, track_off);
    w16(pgc + 12, cell_off);
    w16(pgc + 14, cell_off + 12 * nb_cells);

    for (int i = 0; i < s->nb_tracks; i++) {
        const synth_track *tr = &s->tracks[i];
        uint8_t *e = pgc + track_off + 20 * i;

        e[0] = (i ? 0 : 0x80) | 0x40 | (tr->attr << 3);
        e[1] = tr->dm_valid ? tr->dm_table : 0x10;
        e[4] = cell + 1;
        w32(e + 6, tr->first_pts);
        w32(e + 10, tr->length_pts);
        for (int c = 0; c < tr->nb_cells; c++, cell++) {
            uint8_t *ce = pgc + cell_off + 12 * cell;
            ce[0] = c + 1;
            w32(ce + 4, sec);
            w32(ce + 8, sec + tr->cell_sectors - 1);
            sec += tr->cell_sectors;
        }
    }
    w32(b + 0x0c, 2 + sec + 2 - 1);
    *size = 2 * SECTOR;
    return b;
}

uint32_t synth_pack_pts(const synth_disc *s, int track)
{
    const synth_track *t = &s->tracks[track];
    return t->length_pts / (t->nb_cells * t->cell_sectors);
}

uint32_t synth_sector_number(const uint8_t *p)
{
    p += SECTOR - 4;
    return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

void synth_pack(const synth_disc *s, uint32_t sector, uint8_t *p)
{
    static const uint8_t pack_header[14] = {
        0x00, 0x00, 0x01, 0xba, 0x44, 0x00, 0x04, 0x00, 0x04, 0x01,
        0x01, 0x89, 0xc3, 0xf8,
    };
    int track = 0, has_pts;
    uint32_t first = 0, last = 0, k, pts;
    const synth_track *t;
    const uint8_t *a;
    uint8_t *q;

    for (track = 0; track < s->nb_tracks; track++) {
        track_sectors(s, track, &first, &last);
        if (sector <= last)
            break;
    }
    if (track == s->nb_tracks)
        track = s->nb_tracks - 1;
    t = &s->tracks[track];
    a = s->attr[t->attr];
    k = sector - first;
    has_pts = !(k & 1);
    pts = t->first_pts + k * synth_pack_pts(s, track);

    memset(p, 0, SECTOR);
    memcpy(p, pack_header, 14);
    q = p + 14;
    q[0] = 0x00; q[1] = 0x00; q[2] = 0x01; q[3] = 0xbd;
    w16(q + 4, SECTOR - 20);
    q[6] = 0x81;
    if (sector >= s->scrambled_first &&
        sector - s->scrambled_first < s->scrambled_count)
        q[6] |= 0x10;
    q[7] = has_pts ? 0x80 : 0x00;
    q[8] = has_pts ? 5 : 0;
    q += 9;
    if (has_pts) {
        q[0] = 0x21 | ((pts >> 29) & 0x0e);
        q[1] = pts >> 22;
        q[2] = ((pts >> 14) & 0xfe) | 1;
        q[3] = pts >> 7;
        q[4] = ((pts << 1) & 0xfe) | 1;
        q += 5;
    }
    if (a[0] == 0x00) {
        /* DVD-Audio LPCM: sub_stream_id and the private header */
        q[0] = 0xa0;
        q[3] = 8;                       /* private header length */
        q[6] = t->dm_valid ? t->dm_table : 0;
        q[7] = a[2];
        q[8] = a[3];
        q[10] = a[4];
        q[11] = 0x80;                   /* unity dynamic range control */
    } else {
        q[0] = 0xa1;
    }
    w32(p + SECTOR - 4, sector);
}

int synth_write_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f)
        return -1;
    ok = fwrite(data, 1, size, f) == size;
    return (fclose(f) || !ok) ? -1 : 0;
}

static int write_named(const synth_disc *s, const char *dir, const char *name,
                       const uint8_t *data, size_t size)
{
    char path[1024], lname[64];
    size_t i;

    for (i = 0; name[i] && i < sizeof(lname) - 1; i++)
        lname[i] = s->lowercase && name[i] >= 'A' && name[i] <= 'Z'
                   ? name[i] - 'A' + 'a' : name[i];
    lname[i] = 0;
    snprintf(path, sizeof(path), "%s/%s", dir, lname);
    return synth_write_file(path, data, size);
}

int synth_write(const synth_disc *s, const char *root)
{
    char dir[1024];
    uint8_t *img;
    size_t size;
    uint32_t sector = 0;
    int ret = 0;

    snprintf(dir, sizeof(dir), "%s/%s", root, s->lowercase ? "audio_ts" : "AUDIO_TS");
    if (mkdir(dir, 0755))
        return -1;

    if ((img = synth_amg(s, &size))) {
        ret |= write_named(s, dir, "AUDIO_TS.IFO", img, size);
        ret |= write_named(s, dir, "AUDIO_TS.BUP", img, size);
        free(img);
    }
    if ((img = synth_samg(s, &size))) {
        ret |= write_named(s, dir, "AUDIO_PP.IFO", img, size);
        free(img);
    }
    if ((img = synth_ats(s, &size))) {
        ret |= write_named(s, dir, "ATS_01_0.IFO", img, size);
        ret |= write_named(s, dir, "ATS_01_0.BUP", img, size);
        free(img);
    }
    for (int f = 0; f < 2 && s->aob_file_sectors[f]; f++) {
        char name[16];
        uint8_t *aob = calloc(s->aob_file_sectors[f], SECTOR);
        if (!aob)
            return -1;
        for (uint32_t k = 0; k < s->aob_file_sectors[f]; k++)
            synth_pack(s, sector++, aob + (size_t)k * SECTOR);
        snprintf(name, sizeof(name), "ATS_01_%d.AOB", f + 1);
        ret |= write_named(s, dir, name, aob, (size_t)s->aob_file_sectors[f] * SECTOR);
        free(aob);
    }
    return ret;
}

char *synth_tmpdir(void)
{
    const char *base = getenv("TMPDIR");
    char *p = malloc(1024);
    if (!p)
        return NULL;
    snprintf(p, 1024, "%s/audvd-test-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(p)) {
        free(p);
        return NULL;
    }
    return p;
}

void synth_rmtree(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *de;

    if (d) {
        while ((de = readdir(d))) {
            char p[1024];
            struct stat st;
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            snprintf(p, sizeof(p), "%s/%s", path, de->d_name);
            if (!lstat(p, &st) && S_ISDIR(st.st_mode))
                synth_rmtree(p);
            else
                unlink(p);
        }
        closedir(d);
    }
    rmdir(path);
}
