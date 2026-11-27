/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef AUDVD_INTERNAL_H
#define AUDVD_INTERNAL_H

#include <stdint.h>
#include <stdio.h>

#include "audvd/audvd.h"

#if defined(__GNUC__)
#  define AUDVD_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#  define AUDVD_PRINTF(a, b)
#endif

#define AUDVD_MAX_ATS        99
#define AUDVD_MAX_AOB_FILES  9
#define AUDVD_MAX_IFO_SIZE   (16 * 1024 * 1024)

/* ---- bounds-checked big-endian access to an in-memory IFO ---- */

typedef struct audvd_buf {
    const uint8_t *data;
    size_t         size;
} audvd_buf;

/** Non-zero if [off, off + len) lies inside the buffer. */
static inline int buf_has(const audvd_buf *b, size_t off, size_t len)
{
    return off <= b->size && len <= b->size - off;
}

/* Callers check the range with buf_has() first. */
static inline uint8_t  rb8 (const audvd_buf *b, size_t o) { return b->data[o]; }
static inline uint16_t rb16(const audvd_buf *b, size_t o)
{
    return (uint16_t)(b->data[o] << 8 | b->data[o + 1]);
}
static inline uint32_t rb32(const audvd_buf *b, size_t o)
{
    return (uint32_t)b->data[o] << 24 | (uint32_t)b->data[o + 1] << 16 |
           (uint32_t)b->data[o + 2] << 8 | b->data[o + 3];
}

/* ---- parsed structures ---- */

typedef struct audvd_pgc {
    int          nb_tracks;
    audvd_track *tracks;
    int          nb_cells;
    audvd_cell  *cells;
    uint32_t     length_pts;
    uint8_t      srp_raw[4];  /* search pointer category bytes */
} audvd_pgc;

typedef struct audvd_ats {
    int            present;
    int            number;
    uint16_t       version;
    uint32_t       ats_last_sector;
    uint32_t       aob_start_sector;
    int            nb_attrs;
    audvd_audio_attr attrs[AUDVD_MAX_ATTRS];
    audvd_downmix  downmix[AUDVD_MAX_DOWNMIX_TABLES];
    int            nb_pgcs;
    audvd_pgc     *pgcs;
    int            nb_aob_files;
    char          *aob_path[AUDVD_MAX_AOB_FILES];
    uint32_t       aob_sectors[AUDVD_MAX_AOB_FILES];
    int64_t        aob_lba;
} audvd_ats;

typedef struct audvd_title_int {
    audvd_title pub;
    audvd_pgc  *pgc;          /* NULL for audio-with-video titles */
} audvd_title_int;

typedef struct audvd_aob_cache {
    FILE *fp;
    int   ats, index;
} audvd_aob_cache;

struct audvd_disc {
    audvd_log_cb log;
    void        *log_opaque;
    int          flags;

    char        *dir;         /* the AUDIO_TS directory */

    int              nb_ats;  /* highest ATS number present */
    audvd_ats        ats[AUDVD_MAX_ATS];

    int              nb_titles;
    audvd_title_int *titles;
    int              nb_groups;

    int               nb_samg_tracks;
    audvd_samg_track *samg_tracks;

    /* open AOB file for audvd_read_aob_sectors() */
    audvd_aob_cache aob;
};

/* log.c */
void audvd_log(const audvd_disc *d, audvd_log_level level,
               const char *fmt, ...) AUDVD_PRINTF(3, 4);

/* io.c */
char *audvd_find_file(const char *dir, const char *name);
char *audvd_find_audio_ts(const char *path);
int   audvd_load_file(const char *path, uint8_t **data, size_t *size);
int   audvd_load_ifo(audvd_disc *d, const char *base, int backup,
                     audvd_buf *out, uint8_t **owned);
int   audvd_file_sectors(const char *path, uint32_t *sectors);
char *audvd_strdup(const char *s);

/* aob.c */
void audvd_aob_cache_close(audvd_aob_cache *c);
int  audvd_aob_read(const audvd_disc *d, audvd_aob_cache *c, int ats,
                    uint32_t sector, int nb_sectors, uint8_t *buf);

/* attr.c */
void audvd_decode_attr_bytes(uint8_t quant, uint8_t rate, uint8_t ca,
                             audvd_audio_attr *attr);
void audvd_decode_downmix(const uint8_t *table18, audvd_downmix *out);

/* ifo_amg.c, ifo_samg.c, ifo_ats.c */
int  audvd_parse_amg(audvd_disc *d, const audvd_buf *b);
int  audvd_parse_samg(audvd_disc *d, const audvd_buf *b);
int  audvd_parse_ats(audvd_disc *d, audvd_ats *ats, const audvd_buf *b);
void audvd_free_ats(audvd_ats *ats);

#endif /* AUDVD_INTERNAL_H */
