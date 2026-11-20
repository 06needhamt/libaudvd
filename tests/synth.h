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
} synth_disc;

/** A disc with three tracks; see synth.c for the values. */
void synth_default(synth_disc *s);

/* IFO images; the caller frees them */
uint8_t *synth_amg(const synth_disc *s, size_t *size);
uint8_t *synth_samg(const synth_disc *s, size_t *size);
uint8_t *synth_ats(const synth_disc *s, size_t *size);

/**
 * Write the zone to <root>/AUDIO_TS (created). Pass a NULL image to skip a
 * file. AOB sector n starts with the big-endian 32-bit value n.
 */
int synth_write_file(const char *path, const uint8_t *data, size_t size);
int synth_write(const synth_disc *s, const char *root);

/** Path helpers: mkdtemp()-based scratch directories. */
char *synth_tmpdir(void);
void  synth_rmtree(const char *path);

#endif
