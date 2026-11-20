/*
 * Synthetic DVD-Audio zone generator for the tests.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef AUDVD_TESTS_SYNTH_H
#define AUDVD_TESTS_SYNTH_H

#include <stddef.h>
#include <stdint.h>

#define SYNTH_MAX_TRACKS 8
#define SYNTH_MAX_CELLS  16

typedef struct synth_track {
    uint32_t length_pts;
    uint32_t first_pts;
    int      nb_cells;      /* cells of the track, >= 1 */
    uint32_t cell_sectors;  /* sectors per cell */
    int      attr;          /* ATRN */
    int      dm_table;
    int      dm_valid;
} synth_track;

typedef struct synth_disc {
    int         nb_tracks;
    synth_track tracks[SYNTH_MAX_TRACKS];
    uint8_t     attr[2][5];  /* codec, 0, quant, rate, ca */
    uint8_t     dm_codes[16][16]; /* 8 x (L, R) codes per table */
    uint32_t    aob_lba;     /* absolute sector of the first AOB sector */
    int         samg_group;
    uint8_t     att_type;    /* ATT_SRPT title type byte */
    int         with_video_title;
    int         lowercase;   /* lowercase file and directory names */
    uint32_t    aob_file_sectors[2]; /* sizes of ATS_01_1/2.AOB, 0 = none */
    uint32_t    scrambled_first, scrambled_count; /* CPPM-flagged sectors */
} synth_disc;

/*
 * AOB sectors are MPEG-2 PS packs holding one private stream 1 PES with
 * the track's substream (0xa0 LPCM with a DVD-Audio header, or 0xa1 MLP).
 * Even sectors of a track carry a PTS: first_pts + index * synth_pack_pts().
 * The last 4 bytes of every pack are its AOB sector number.
 */
uint32_t synth_pack_pts(const synth_disc *s, int track);
uint32_t synth_sector_number(const uint8_t *pack);
void     synth_pack(const synth_disc *s, uint32_t sector, uint8_t *pack);

/** A disc with three tracks; see synth.c for the values. */
void synth_default(synth_disc *s);

/* IFO images; the caller frees them */
uint8_t *synth_amg(const synth_disc *s, size_t *size);
uint8_t *synth_samg(const synth_disc *s, size_t *size);
uint8_t *synth_ats(const synth_disc *s, size_t *size);

/**
 * Write the zone to <root>/AUDIO_TS (created). Pass a NULL image to skip a
 * file.
 */
int synth_write_file(const char *path, const uint8_t *data, size_t size);
int synth_write(const synth_disc *s, const char *root);

/** Path helpers: mkdtemp()-based scratch directories. */
char *synth_tmpdir(void);
void  synth_rmtree(const char *path);

#endif
