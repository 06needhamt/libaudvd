/*
 * audvd-info: print the navigation structure of a DVD-Audio disc.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audvd/audvd.h"

static int verbose;

static void log_cb(void *opaque, audvd_log_level level, const char *msg)
{
    static const char *const names[] = { "error", "warning", "info", "debug" };
    (void)opaque;
    if (level <= AUDVD_LOG_WARNING || verbose > 1)
        fprintf(stderr, "audvd %s: %s\n", names[level], msg);
}

static const char *codec_name(audvd_codec c)
{
    switch (c) {
    case AUDVD_CODEC_LPCM: return "LPCM";
    case AUDVD_CODEC_MLP:  return "MLP";
    default:               return "?";
    }
}

static void print_layout(const audvd_channel_layout *l)
{
    static const char *const spk[] = { "L", "R", "C", "LFE", "S", "Ls", "Rs" };
    for (int i = 0; i < l->nb_channels; i++)
        printf("%s%s%s", i ? " " : "",
               i == l->group_channels[0] ? "| " : "", spk[l->speakers[i]]);
}

static void print_attr(const audvd_audio_attr *a)
{
    printf("%s %d/%d", codec_name(a->codec), a->group_bits[0], a->group_rate[0]);
    if (a->group_bits[1])
        printf(" + %d/%d", a->group_bits[1], a->group_rate[1]);
    printf(" ca=%d (", a->channel_assignment);
    print_layout(&a->layout);
    printf(")");
}

static void print_time(uint64_t pts)
{
    uint64_t ms = pts / 90;
    printf("%02u:%02u:%02u.%03u", (unsigned)(ms / 3600000),
           (unsigned)(ms / 60000 % 60), (unsigned)(ms / 1000 % 60),
           (unsigned)(ms % 1000));
}

static int downmix_used(const audvd_downmix *dm)
{
    for (int c = 0; c < AUDVD_DOWNMIX_CHANNELS; c++)
        if (dm->gain_code[c][0] || dm->gain_code[c][1])
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    audvd_open_opts opts = { log_cb, NULL, 0 };
    const char *path = NULL;
    audvd_disc *disc;
    int ret;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            verbose++;
        else if (!strcmp(argv[i], "--no-amg"))
            opts.flags |= AUDVD_OPEN_IGNORE_AMG;
        else if (!path && argv[i][0] != '-')
            path = argv[i];
        else
            path = NULL, i = argc;
    }
    if (!path) {
        fprintf(stderr, "usage: audvd-info [-v [-v]] [--no-amg] <disc or AUDIO_TS dir>\n");
        return 2;
    }

    ret = audvd_open(path, &opts, &disc);
    if (ret < 0) {
        fprintf(stderr, "%s: %s\n", path, audvd_strerror(ret));
        return 1;
    }

    printf("groups: %d, titles: %d, ATS: %d, SAMG tracks: %d\n",
           audvd_num_groups(disc), audvd_num_titles(disc), audvd_num_ats(disc),
           audvd_samg_num_tracks(disc));

    for (int n = 1; n <= audvd_num_ats(disc); n++) {
        audvd_ats_info ai;
        if (audvd_get_ats(disc, n, &ai) < 0)
            continue;
        printf("ATS %d: version %04x, %d title(s), %d AOB file(s), %llu sectors",
               n, ai.version, ai.nb_titles, ai.nb_aob_files,
               (unsigned long long)ai.aob_sectors);
        if (ai.aob_lba >= 0)
            printf(", AOB at LBA %lld", (long long)ai.aob_lba);
        printf("\n");
        for (int a = 0; a < ai.nb_attrs; a++) {
            printf("  attr %d: ", a);
            print_attr(&ai.attrs[a]);
            printf("\n");
        }
        for (int t = 0; t < AUDVD_MAX_DOWNMIX_TABLES; t++) {
            audvd_downmix dm;
            if (audvd_get_downmix(disc, n, t, &dm) < 0 || !downmix_used(&dm))
                continue;
            printf("  downmix %d:", t);
            for (int c = 0; c < 6; c++)
                printf(" [%.3f %.3f]", dm.gain[c][0], dm.gain[c][1]);
            printf("\n");
        }
    }

    for (int ti = 0; ti < audvd_num_titles(disc); ti++) {
        audvd_title t;
        audvd_get_title(disc, ti, &t);
        printf("title %d: group %d, %s, %s %d title %d, %d track(s), ",
               t.number, t.group, t.audio_only ? "audio-only" : "audio+video",
               t.audio_only ? "ATS" : "VTS", t.ats, t.ats_title, t.nb_tracks);
        print_time(t.length_pts);
        printf("\n");
        for (int k = 0; ; k++) {
            audvd_track tr;
            if (audvd_get_track(disc, ti, k, &tr) < 0)
                break;
            printf("  track %2d: start ", tr.number);
            print_time(tr.start_pts);
            printf(" length ");
            print_time(tr.length_pts);
            printf(" sectors %u-%u attr %d ", tr.first_sector, tr.last_sector,
                   tr.attr_index);
            print_attr(&tr.attr);
            if (tr.downmix_valid)
                printf(" downmix %d%s", tr.downmix_table,
                       tr.downmix_prohibited ? " (prohibited)" : "");
            printf("\n");
            if (verbose) {
                for (int c = tr.first_cell; c < tr.first_cell + tr.nb_cells; c++) {
                    audvd_cell cell;
                    audvd_get_cell(disc, ti, c, &cell);
                    printf("    cell %d: id %d sectors %u-%u\n", c + 1, cell.id,
                           cell.first_sector, cell.last_sector);
                }
            }
        }
    }

    if (verbose) {
        for (int i = 0; i < audvd_samg_num_tracks(disc); i++) {
            audvd_samg_track s;
            audvd_samg_get_track(disc, i, &s);
            printf("SAMG %2d: group %d track %2d%s length ", i + 1, s.group,
                   s.track, s.first_in_group ? "*" : " ");
            print_time(s.length_pts);
            printf(" LBA %u-%u ", s.first_sector, s.last_sector);
            print_attr(&s.attr);
            printf("\n");
        }
    }

    audvd_close(disc);
    return 0;
}
