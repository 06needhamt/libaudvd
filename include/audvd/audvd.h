/*
 * libaudvd - DVD-Audio navigation library
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#ifndef AUDVD_AUDVD_H
#define AUDVD_AUDVD_H

#include <stddef.h>
#include <stdint.h>

#include "audvd_version.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(AUDVD_SHARED)
#  ifdef AUDVD_BUILDING
#    define AUDVD_API __declspec(dllexport)
#  else
#    define AUDVD_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define AUDVD_API __attribute__((visibility("default")))
#else
#  define AUDVD_API
#endif

/** Size of a DVD logical block (sector) in bytes. */
#define AUDVD_SECTOR_SIZE 2048

/** Clock of all PTS values reported by the library. */
#define AUDVD_PTS_HZ 90000

/** Audio attribute slots in an ATS, downmix tables in an ATS. */
#define AUDVD_MAX_ATTRS          8
#define AUDVD_MAX_DOWNMIX_TABLES 16
/** Channel slots of a downmix table (up to 6 are used). */
#define AUDVD_DOWNMIX_CHANNELS   8

/* ------------------------------------------------------------------ */
/* Errors                                                             */
/* ------------------------------------------------------------------ */

enum {
    AUDVD_OK              =  0,
    AUDVD_ERR_IO          = -1, /**< file could not be opened or read      */
    AUDVD_ERR_NOMEM       = -2,
    AUDVD_ERR_INVALID     = -3, /**< structure is corrupt or inconsistent  */
    AUDVD_ERR_NOTFOUND    = -4, /**< no DVD-Audio zone / file missing      */
    AUDVD_ERR_RANGE       = -5, /**< index argument out of range           */
    AUDVD_ERR_UNSUPPORTED = -6, /**< valid but not handled (e.g. CPPM)     */
    AUDVD_ERR_EOF         = -7, /**< end of the data                        */
};

/** Return a static, human-readable description of an AUDVD_ERR_* code. */
AUDVD_API const char *audvd_strerror(int err);

/* ------------------------------------------------------------------ */
/* Opening                                                            */
/* ------------------------------------------------------------------ */

typedef enum audvd_log_level {
    AUDVD_LOG_ERROR,
    AUDVD_LOG_WARNING,
    AUDVD_LOG_INFO,
    AUDVD_LOG_DEBUG,
} audvd_log_level;

typedef void (*audvd_log_cb)(void *opaque, audvd_log_level level,
                             const char *msg);

/** Ignore the AMG (AUDIO_TS.IFO) and derive titles from the ATS IFOs. */
#define AUDVD_OPEN_IGNORE_AMG 0x0001
/** Never fall back to the .BUP backup copies of the IFO files. */
#define AUDVD_OPEN_NO_BUP     0x0002

typedef struct audvd_open_opts {
    audvd_log_cb log;     /**< NULL: messages are dropped               */
    void        *log_opaque;
    int          flags;   /**< AUDVD_OPEN_*                              */
} audvd_open_opts;

typedef struct audvd_disc audvd_disc;

/**
 * Open a DVD-Audio zone.
 *
 * @param path either the AUDIO_TS directory itself or a directory that
 *             contains it (the root of a mounted disc or of a rip). File
 *             names are matched case-insensitively.
 * @param opts may be NULL
 * @param out  receives the handle on success
 * @return AUDVD_OK or a negative AUDVD_ERR_* code
 */
AUDVD_API int  audvd_open(const char *path, const audvd_open_opts *opts,
                          audvd_disc **out);
AUDVD_API void audvd_close(audvd_disc *disc);

/* ------------------------------------------------------------------ */
/* Audio attributes and channel layouts                               */
/* ------------------------------------------------------------------ */

typedef enum audvd_codec {
    AUDVD_CODEC_LPCM    = 0,
    AUDVD_CODEC_MLP     = 1,
    AUDVD_CODEC_UNKNOWN = 0xff,
} audvd_codec;

typedef enum audvd_speaker {
    AUDVD_SPK_L,   /**< front left            */
    AUDVD_SPK_R,   /**< front right           */
    AUDVD_SPK_C,   /**< front centre          */
    AUDVD_SPK_LFE, /**< low frequency effects */
    AUDVD_SPK_S,   /**< surround (mono)       */
    AUDVD_SPK_LS,  /**< left surround         */
    AUDVD_SPK_RS,  /**< right surround        */
} audvd_speaker;

/**
 * Speaker layout of one of the 21 DVD-Audio channel assignments, in
 * stream order (channel group 1 first).
 */
typedef struct audvd_channel_layout {
    int           nb_channels;
    int           group_channels[2]; /**< 0 for an absent group 2 */
    audvd_speaker speakers[6];
} audvd_channel_layout;

/**
 * @return AUDVD_OK, or AUDVD_ERR_RANGE if the assignment is not 0..20.
 */
AUDVD_API int audvd_channel_assignment_layout(int channel_assignment,
                                              audvd_channel_layout *out);

/** An audio stream attribute (ATS_AST_ATR / SAMG track attribute). */
typedef struct audvd_audio_attr {
    audvd_codec codec;
    int group_bits[2];      /**< 16, 20 or 24; 0 = group absent         */
    int group_rate[2];      /**< Hz; 0 = group absent                   */
    int channel_assignment; /**< 0..20, -1 if invalid                    */
    audvd_channel_layout layout;
    uint8_t raw[16];        /**< the bytes it was decoded from          */
} audvd_audio_attr;

/* ------------------------------------------------------------------ */
/* Mixing metadata                                                    */
/* ------------------------------------------------------------------ */

/** Gain code value that mutes a channel. */
#define AUDVD_DOWNMIX_MUTE 0xff

/**
 * One ATS_DM_COEFT downmix coefficient table: how each source channel
 * (stream order, see audvd_channel_layout) feeds the stereo output.
 */
typedef struct audvd_downmix {
    uint8_t gain_code[AUDVD_DOWNMIX_CHANNELS][2]; /**< [ch][0=L, 1=R]   */
    float   gain[AUDVD_DOWNMIX_CHANNELS][2];      /**< linear, 0 = mute */
} audvd_downmix;

/**
 * Convert a downmix gain code to an attenuation in dB (<= 0).
 * 0x00..0xc8: 0.2 dB steps; 0xc9..0xfe: 0.4 dB steps; 0xff mutes, for
 * which -INFINITY is returned.
 */
AUDVD_API double audvd_downmix_code_to_db(uint8_t code);
/** Convert a downmix gain code to a linear factor in 0..1. */
AUDVD_API double audvd_downmix_code_to_gain(uint8_t code);

/* ------------------------------------------------------------------ */
/* Navigation tree                                                    */
/* ------------------------------------------------------------------ */

/** Information on an Audio Title Set (ATS_xx_0.IFO + ATS_xx_n.AOB). */
typedef struct audvd_ats_info {
    int      number;           /**< 1-based, the xx in ATS_xx_0.IFO     */
    int      nb_titles;        /**< audio-only titles (PGCs) in the ATS */
    uint16_t version;          /**< specification version, e.g. 0x0011  */
    int      nb_attrs;         /**< audio attributes in use             */
    audvd_audio_attr attrs[AUDVD_MAX_ATTRS];
    int      nb_aob_files;     /**< ATS_xx_1.AOB .. ATS_xx_n.AOB found  */
    uint64_t aob_sectors;      /**< total size of the AOB files         */
    /**
     * Absolute disc sector of the first AOB sector, derived from the SAMG
     * when it lists tracks of this ATS, -1 when unknown.
     */
    int64_t  aob_lba;
} audvd_ats_info;

typedef struct audvd_title {
    int      index;            /**< 0-based, for audvd_get_track()      */
    int      number;           /**< 1-based number on the disc          */
    int      group;            /**< 1-based DVD-Audio group             */
    int      audio_only;       /**< AOTT; 0 for an audio-with-video one */
    int      ats;              /**< ATS number (VTS number if !audio_only) */
    int      ats_title;        /**< 1-based title within the ATS/VTS    */
    int      nb_tracks;        /**< tracks; only audio-only ones can be
                                    queried with audvd_get_track()   */
    uint64_t length_pts;
    uint8_t  type_raw;         /**< title type byte of the ATT_SRPT     */
} audvd_title;

typedef struct audvd_track {
    int      number;           /**< 1-based within the title            */
    int      first_cell;       /**< 0-based index for audvd_get_cell()  */
    int      nb_cells;
    /** Sector range, relative to the start of ATS_xx_1.AOB. */
    uint32_t first_sector, last_sector;
    /** Start of the track on the title timeline (sum of previous lengths) */
    uint64_t start_pts;
    uint32_t length_pts;
    /** PTS of the first audio unit in the AOB, which restarts per track */
    uint32_t first_pts;
    uint32_t pause_pts;
    int      attr_index;       /**< ATRN, index into audvd_ats_info.attrs */
    audvd_audio_attr attr;
    int      downmix_table;    /**< DM_COEFTN, 0..15                    */
    int      downmix_valid;    /**< the coefficient table may be used   */
    int      downmix_prohibited; /**< the producer forbids downmixing   */
    int      stc_discontinuity;
    uint8_t  raw_flags[2];
} audvd_track;

typedef struct audvd_cell {
    int      id;               /**< cell id byte                         */
    uint32_t first_sector, last_sector; /**< relative to ATS_xx_1.AOB    */
} audvd_cell;

AUDVD_API int audvd_num_ats(const audvd_disc *disc);
AUDVD_API int audvd_get_ats(const audvd_disc *disc, int ats,
                            audvd_ats_info *out);
/**
 * @param ats   1-based ATS number
 * @param table 0..15
 */
AUDVD_API int audvd_get_downmix(const audvd_disc *disc, int ats, int table,
                                audvd_downmix *out);

AUDVD_API int audvd_num_groups(const audvd_disc *disc);
AUDVD_API int audvd_num_titles(const audvd_disc *disc);
AUDVD_API int audvd_get_title(const audvd_disc *disc, int title,
                              audvd_title *out);
/** @param title and track are 0-based */
AUDVD_API int audvd_get_track(const audvd_disc *disc, int title, int track,
                              audvd_track *out);
AUDVD_API int audvd_get_cell(const audvd_disc *disc, int title, int cell,
                             audvd_cell *out);
AUDVD_API int audvd_title_num_cells(const audvd_disc *disc, int title);

/* ------------------------------------------------------------------ */
/* Simple Audio Manager (AUDIO_PP.IFO)                                */
/* ------------------------------------------------------------------ */

/** A track of the SAMG, the flat list read by audio-only players. */
typedef struct audvd_samg_track {
    int      group;
    int      track;
    int      first_in_group;
    uint32_t first_pts;
    uint32_t length_pts;
    audvd_audio_attr attr;     /**< codec is AUDVD_CODEC_UNKNOWN        */
    /** Absolute disc sectors */
    uint32_t first_sector, first_sector_2, last_sector;
} audvd_samg_track;

AUDVD_API int audvd_samg_num_tracks(const audvd_disc *disc);
AUDVD_API int audvd_samg_get_track(const audvd_disc *disc, int index,
                                   audvd_samg_track *out);

/* ------------------------------------------------------------------ */
/* Raw sector access                                                  */
/* ------------------------------------------------------------------ */

/**
 * Read sectors of an ATS's AOB files, which form one address space.
 *
 * @param ats    1-based ATS number
 * @param sector first sector, relative to the start of ATS_xx_1.AOB
 * @return the number of sectors read (short at the end of the data),
 *         or a negative AUDVD_ERR_* code
 */
AUDVD_API int audvd_read_aob_sectors(audvd_disc *disc, int ats,
                                     uint32_t sector, int nb_sectors,
                                     uint8_t *buf);

/* ------------------------------------------------------------------ */
/* Title reader                                                       */
/* ------------------------------------------------------------------ */

/** First sector of the selected track range. */
#define AUDVD_POS_TITLE_START   0x01
/** First sector of a track. */
#define AUDVD_POS_TRACK_START   0x02
/** First sector of a cell. */
#define AUDVD_POS_CELL_START    0x04
/**
 * Timestamps do not continue from the previous run: a track whose AOB
 * PTS restart (STC discontinuity), or the first run after a seek.
 */
#define AUDVD_POS_DISCONTINUITY 0x08
/** The run is CPPM-scrambled and must not be decoded. */
#define AUDVD_POS_ENCRYPTED     0x10

/** Where a run of sectors returned by audvd_reader_read() comes from. */
typedef struct audvd_pos {
    int      track;            /**< 0-based within the title            */
    int      cell;             /**< 0-based within the title            */
    uint32_t sector;           /**< AOB-relative sector of the first one */
    int      flags;            /**< AUDVD_POS_*, for the first sector   */
    uint64_t track_start_pts;  /**< track start on the title timeline   */
    uint32_t track_first_pts;  /**< PTS the track's AOB data starts at  */
} audvd_pos;

typedef struct audvd_reader audvd_reader;

/**
 * Open a reader on an audio-only title. Several readers may be used at
 * once; all must be closed before the disc.
 *
 * @param title 0-based title index
 */
AUDVD_API int  audvd_reader_open(audvd_disc *disc, int title,
                                 audvd_reader **out);
AUDVD_API void audvd_reader_close(audvd_reader *r);

/**
 * Restrict reading to tracks first..last (0-based, inclusive) and rewind
 * to the start of the first. The default is the whole title.
 */
AUDVD_API int audvd_reader_set_tracks(audvd_reader *r, int first, int last);

/**
 * Read the next run of sectors in play order. A run never crosses a cell
 * boundary and is either entirely scrambled or entirely clear, so pos
 * describes all of it.
 *
 * @return the number of sectors (1..max_sectors), AUDVD_ERR_EOF at the end
 *         of the range, or another negative AUDVD_ERR_* code
 */
AUDVD_API int audvd_reader_read(audvd_reader *r, uint8_t *buf,
                                int max_sectors, audvd_pos *pos);

/** Position of the next run, without reading it. */
AUDVD_API int audvd_reader_tell(const audvd_reader *r, audvd_pos *pos);

/** Continue reading at the start of a track (0-based) within the range. */
AUDVD_API int audvd_reader_seek_track(audvd_reader *r, int track);

/**
 * Continue reading at the last pack whose PTS is at or before a time on
 * the title timeline. Falls back to interpolating within the track if
 * its packs cannot be inspected (scrambled or unreadable).
 *
 * @param landed if not NULL, receives the title time of the pack landed on
 */
AUDVD_API int audvd_reader_seek_pts(audvd_reader *r, uint64_t title_pts,
                                    uint64_t *landed);

/* ------------------------------------------------------------------ */
/* Pack inspection                                                    */
/* ------------------------------------------------------------------ */

/** Non-zero if the first PES packet of a 2048-byte pack is scrambled. */
AUDVD_API int audvd_pack_is_scrambled(const uint8_t *pack);

/** PTS of the first PES packet of a pack that has one, or -1. */
AUDVD_API int64_t audvd_pack_pts(const uint8_t *pack);

/**
 * Convert a PTS found in the AOB data of a run to the title timeline,
 * taking the per-track restart and 33-bit wrap-around into account.
 * Returns -1 if pkt_pts is negative.
 */
AUDVD_API int64_t audvd_pos_title_pts(const audvd_pos *pos, int64_t pkt_pts);

#ifdef __cplusplus
}
#endif

#endif /* AUDVD_AUDVD_H */
