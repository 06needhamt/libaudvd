/*
 * mkdisc: build a small DVD-Audio zone from real audio, to test players
 * and demuxers end to end (not installed).
 *
 *   mkdisc OUTDIR [-s FIRST:COUNT] [-p] TRACK...
 *
 * TRACK is one of
 *   lpcm:FILE:CA:RATE:BITS[:DM]  FILE: raw signed big-endian 24-bit samples,
 *                                interleaved in FFmpeg's native channel
 *                                order for channel assignment CA
 *                                (ffmpeg ... -f s24be FILE)
 *   mlp:FILE:CA:RATE             FILE: a raw MLP stream (ffmpeg -c:a mlp -f mlp)
 * DM selects the downmix flags of an LPCM track: "dm" (table 1, valid,
 * the default), "nodm" (coefficients not valid) or "prohibited".
 * -s marks AOB sectors FIRST..FIRST+COUNT-1 as CPPM-scrambled (only the
 * PES flag; the data stays clear). -p puts a PTS only on every other LPCM
 * pack, as some discs do. Up to two distinct audio attributes (codec and
 * parameters) can be used on one disc.
 *
 * Downmix table 1: L -> L and R -> R at 0 dB, Ls -> L and Rs -> R at -6 dB,
 * C -> L and R at -3 dB, LFE and S muted.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audvd/audvd.h"
#include "synth.h"

#define SECTOR      2048
#define PTS_START   94
#define MAX_SECTORS (1u << 20)

typedef struct track_spec {
    int         mlp;
    const char *file;
    int         ca, rate, bits;
    int         dm_valid, dm_prohibited;
} track_spec;

static uint8_t *aob;
static uint32_t aob_sectors;
static uint32_t scr_first = UINT32_MAX, scr_count;
static int sparse_pts;

static void w16(uint8_t *p, unsigned v) { p[0] = v >> 8; p[1] = v; }

static void put_pts(uint8_t *q, uint64_t pts)
{
    q[0] = 0x21 | ((pts >> 29) & 0x0e);
    q[1] = (uint8_t)(pts >> 22);
    q[2] = ((pts >> 14) & 0xfe) | 1;
    q[3] = (uint8_t)(pts >> 7);
    q[4] = ((pts << 1) & 0xfe) | 1;
}

static uint8_t *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long len;

    if (!f || fseek(f, 0, SEEK_END) || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET)) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    *size = (size_t)len;
    return buf;
}

/*
 * Start a pack: pack header and private stream 1 PES header with a PTS
 * (if pts >= 0) and the substream id. Returns the payload pointer; the
 * caller finishes with end_pack().
 */
static uint8_t *begin_pack(uint8_t *p, int64_t pts, uint8_t substream)
{
    static const uint8_t pack_header[14] = {
        0x00, 0x00, 0x01, 0xba, 0x44, 0x00, 0x04, 0x00, 0x04, 0x01,
        0x01, 0x89, 0xc3, 0xf8,
    };
    uint32_t sector = (uint32_t)((p - aob) / SECTOR);
    uint8_t *q;

    memset(p, 0, SECTOR);
    memcpy(p, pack_header, 14);
    q = p + 14;
    q[2] = 0x01;
    q[3] = 0xbd;
    q[6] = 0x81;
    if (sector >= scr_first && sector - scr_first < scr_count)
        q[6] |= 0x10;
    q[7] = pts >= 0 ? 0x80 : 0x00;
    q[8] = pts >= 0 ? 5 : 0;
    q += 9;
    if (pts >= 0) {
        put_pts(q, (uint64_t)pts);
        q += 5;
    }
    *q++ = substream;
    return q;
}

/* Close the PES at end, and pad the rest of the pack. */
static void end_pack(uint8_t *p, uint8_t *end)
{
    size_t left = (size_t)(p + SECTOR - end);
    size_t pes_len;

    /* too little room for a padding packet: zeros in the audio PES, which
     * the LPCM decoder ignores as a partial block (MLP callers avoid it) */
    if (left < 6) {
        end += left;
        left = 0;
    }
    pes_len = (size_t)(end - (p + 20));
    w16(p + 18, (unsigned)pes_len);
    if (left) {
        end[2] = 0x01;
        end[3] = 0xbe;
        w16(end + 4, (unsigned)(left - 6));
        memset(end + 6, 0xff, left - 6);
    }
}

static uint8_t *next_sector(void)
{
    if (aob_sectors >= MAX_SECTORS) {
        fprintf(stderr, "too much audio\n");
        exit(1);
    }
    return aob + (size_t)aob_sectors++ * SECTOR;
}

/* Native (FFmpeg mask order) position of each stream-order channel. */
static void native_map(const audvd_channel_layout *l, int *map)
{
    static const int bit[] = {
        [AUDVD_SPK_L] = 0, [AUDVD_SPK_R] = 1, [AUDVD_SPK_C] = 2, [AUDVD_SPK_LFE] = 3,
        [AUDVD_SPK_LS] = 4, [AUDVD_SPK_RS] = 5, [AUDVD_SPK_S] = 8,
    };
    uint32_t mask = 0;

    for (int i = 0; i < l->nb_channels; i++)
        mask |= 1u << bit[l->speakers[i]];
    for (int i = 0; i < l->nb_channels; i++) {
        uint32_t below = mask & ((1u << bit[l->speakers[i]]) - 1);
        map[i] = __builtin_popcount(below);
    }
}

/*
 * LPCM: sets of 2 samples per channel; within a set the second channel
 * group comes first, each group with the 16-bit main words of both samples
 * then, for 24 bits, the low bytes (the layout FFmpeg's pcm_dvda decodes).
 */
static uint32_t put_lpcm(const track_spec *t, int dm_table, uint32_t *length_pts)
{
    audvd_channel_layout l;
    int map[6], nch, bits = t->bits;
    size_t size, frames, f = 0;
    uint8_t *pcm = load(t->file, &size);
    int block, per_pack;
    uint32_t first = aob_sectors;
    uint8_t quant = bits == 24 ? 0x22 : 0x00;
    uint8_t rate = t->rate == 96000 ? 0x11 : t->rate == 192000 ? 0x22 : 0x00;

    audvd_channel_assignment_layout(t->ca, &l);
    native_map(&l, map);
    nch    = l.nb_channels;
    frames = size / (3 * (size_t)nch) & ~(size_t)1;
    block  = 2 * nch * bits / 8;
    /* room after headers: 2048 - 14 - 9 - 5 (PTS) - 1 (substream) - 11 */
    per_pack = 2008 / block;
    if (2008 - per_pack * block && 2008 - per_pack * block < 6)
        per_pack--;

    for (int pack = 0; f < frames; pack++) {
        uint8_t *p = next_sector();
        int64_t pts = sparse_pts && (pack & 1) ? -1
                    : PTS_START + (int64_t)(f * 90000 / (size_t)t->rate);
        uint8_t *q = begin_pack(p, pts, 0xa0);
        int nb = 0;

        q[2] = 8;                           /* private header length */
        q[5] = (uint8_t)dm_table;
        q[6] = quant;
        q[7] = rate;
        q[9] = (uint8_t)t->ca;
        q[10] = 0x80;
        q += 11;

        for (; nb < per_pack && f < frames; nb++, f += 2) {
            for (int g = 1; g >= 0; g--) {
                int c0 = g ? l.group_channels[0] : 0;
                int cn = l.group_channels[g];
                for (int n = 0; n < 2; n++)
                    for (int j = 0; j < cn; j++) {
                        const uint8_t *src = pcm + ((f + n) * nch + map[c0 + j]) * 3;
                        *q++ = src[0];
                        *q++ = src[1];
                    }
                if (bits == 24)
                    for (int n = 0; n < 2; n++)
                        for (int j = 0; j < cn; j++)
                            *q++ = pcm[((f + n) * nch + map[c0 + j]) * 3 + 2];
            }
        }
        end_pack(p, q);
    }
    free(pcm);
    *length_pts = (uint32_t)(frames * 90000 / (size_t)t->rate);
    return aob_sectors - first;
}

/*
 * MLP: the 9 bytes after the substream id that FFmpeg's mpegps demuxer
 * skips, then the bitstream. A PTS marks the first access unit starting
 * in the pack (1/1200 s each in the 48 kHz family).
 */
static uint32_t put_mlp(const track_spec *t, uint32_t *length_pts)
{
    size_t size, off = 0, au_next = 0, au_index = 0;
    uint8_t *mlp = load(t->file, &size);
    uint32_t first = aob_sectors;

    while (off < size) {
        uint8_t *p = next_sector();
        int64_t pts = -1;
        size_t room, n;
        uint8_t *q;

        /* the access unit that starts first within this pack */
        room = 2048 - 14 - 9 - 5 - 1 - 9;
        if (au_next < off + room && au_next < size)
            pts = PTS_START + (int64_t)au_index * 75;
        if (pts < 0)
            room += 5;
        q = begin_pack(p, pts, 0xa1);
        q += 9;
        n = size - off < room ? size - off : room;
        if (n < room && room - n < 6)
            n = room - 6 < size - off ? room - 6 : n;
        memcpy(q, mlp + off, n);
        off += n;
        while (au_next < off && au_next + 2 <= size) {
            size_t len = (size_t)((mlp[au_next] & 0x0f) << 8 | mlp[au_next + 1]) * 2;
            if (!len)
                break;
            au_next += len;
            au_index++;
        }
        end_pack(p, q + n);
    }
    free(mlp);
    *length_pts = (uint32_t)(au_index * 75);
    return aob_sectors - first;
}

static int parse_track(char *arg, track_spec *t)
{
    char *f[6] = { 0 };
    int n = 0;

    for (char *tok = strtok(arg, ":"); tok && n < 6; tok = strtok(NULL, ":"))
        f[n++] = tok;
    memset(t, 0, sizeof(*t));
    t->dm_valid = 1;
    if (n >= 4 && !strcmp(f[0], "mlp")) {
        t->mlp = 1;
    } else if (n >= 5 && !strcmp(f[0], "lpcm")) {
        t->bits = atoi(f[4]);
        if (n == 6) {
            t->dm_valid      = strcmp(f[5], "nodm") != 0;
            t->dm_prohibited = !strcmp(f[5], "prohibited");
        }
        if (t->bits != 16 && t->bits != 24)
            return -1;
    } else {
        return -1;
    }
    t->file = f[1];
    t->ca   = atoi(f[2]);
    t->rate = atoi(f[3]);
    return t->ca >= 0 && t->ca <= 20 && (t->rate == 48000 || t->rate == 96000) ? 0 : -1;
}

int main(int argc, char **argv)
{
    static const uint8_t dm1[16] = {
        /* L      R      Ls     Rs     C      LFE */
        0x00, 0xff, 0xff, 0x00, 0x1e, 0xff, 0xff, 0x1e, 0x0f, 0x0f, 0xff, 0xff,
    };
    synth_disc s;
    track_spec tracks[SYNTH_MAX_TRACKS];
    int nb = 0, nb_attrs = 0;
    const char *out;
    char path[1024];
    int argi = 2;

    if (argc < 3)
        goto usage;
    out = argv[1];
    for (; argi < argc && argv[argi][0] == '-'; argi++) {
        if (!strcmp(argv[argi], "-s") && argi + 1 < argc) {
            if (sscanf(argv[++argi], "%u:%u", &scr_first, &scr_count) != 2)
                goto usage;
        } else if (!strcmp(argv[argi], "-p")) {
            sparse_pts = 1;
        } else {
            goto usage;
        }
    }
    for (; argi < argc; argi++) {
        if (nb == SYNTH_MAX_TRACKS || parse_track(argv[argi], &tracks[nb]) < 0)
            goto usage;
        nb++;
    }
    if (!nb)
        goto usage;

    aob = calloc(MAX_SECTORS, SECTOR);
    if (!aob)
        return 1;

    /* IFO description; attribute 0 is LPCM, 1 is MLP */
    synth_default(&s);
    s.nb_tracks           = nb;
    s.with_video_title    = 0;
    s.aob_file_sectors[0] = 0;
    s.aob_file_sectors[1] = 0;
    memset(s.dm_codes, 0, sizeof(s.dm_codes));
    memcpy(s.dm_codes[1], dm1, sizeof(dm1));

    for (int i = 0; i < nb; i++) {
        const track_spec *t = &tracks[i];
        synth_track *st = &s.tracks[i];
        int a;
        uint8_t attr[5] = {
            (uint8_t)t->mlp, 0,
            (uint8_t)(t->mlp || t->bits == 24 ? 0x22 : 0x00),
            (uint8_t)(t->rate == 96000 ? 0x11 : 0x00),
            (uint8_t)t->ca,
        };
        uint32_t len;

        for (a = 0; a < nb_attrs && memcmp(s.attr[a], attr, 5); a++)
            ;
        if (a == nb_attrs) {
            if (nb_attrs == 2) {
                fprintf(stderr, "at most two distinct audio attributes per disc\n");
                return 1;
            }
            memcpy(s.attr[nb_attrs++], attr, 5);
        }

        st->cell_sectors  = t->mlp ? put_mlp(t, &len) : put_lpcm(t, t->dm_valid ? 1 : 0, &len);
        st->length_pts    = len;
        st->first_pts     = PTS_START;
        st->nb_cells      = 1;
        st->attr          = a;
        st->dm_table      = 1;
        st->dm_valid      = !t->mlp && t->dm_valid;
        st->dm_prohibited = !t->mlp && t->dm_prohibited;
        printf("track %d: %s, %u sectors, %u PTS ticks\n", i + 1,
               t->mlp ? "MLP" : "LPCM", st->cell_sectors, len);
    }

    if (synth_write(&s, out)) {
        fprintf(stderr, "cannot write %s/AUDIO_TS\n", out);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/AUDIO_TS/ATS_01_1.AOB", out);
    if (synth_write_file(path, aob, (size_t)aob_sectors * SECTOR)) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
    }
    free(aob);
    return 0;

usage:
    fprintf(stderr, "usage: mkdisc OUTDIR [-s FIRST:COUNT] [-p] "
            "lpcm:FILE:CA:RATE:BITS[:dm|nodm|prohibited] | mlp:FILE:CA:RATE ...\n");
    return 2;
}
