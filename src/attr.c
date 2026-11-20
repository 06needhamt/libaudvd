/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Audio attribute and downmix coefficient decoding.
 */

#include <math.h>
#include <string.h>

#include "internal.h"

#define L   AUDVD_SPK_L
#define R   AUDVD_SPK_R
#define C   AUDVD_SPK_C
#define LFE AUDVD_SPK_LFE
#define S   AUDVD_SPK_S
#define LS  AUDVD_SPK_LS
#define RS  AUDVD_SPK_RS

/*
 * The 21 channel assignments: channels of group 1, of group 2, and the
 * speaker of each channel in stream order (US 6,580,671 FIG. 26; the same
 * table drives FFmpeg's pcm_dvda decoder).
 */
static const struct {
    uint8_t g1, g2;
    uint8_t spk[6];
} assignments[21] = {
    [ 0] = { 1, 0, { C } },
    [ 1] = { 2, 0, { L, R } },
    [ 2] = { 2, 1, { L, R, S } },
    [ 3] = { 2, 2, { L, R, LS, RS } },
    [ 4] = { 2, 1, { L, R, LFE } },
    [ 5] = { 2, 2, { L, R, LFE, S } },
    [ 6] = { 2, 3, { L, R, LFE, LS, RS } },
    [ 7] = { 2, 1, { L, R, C } },
    [ 8] = { 2, 2, { L, R, C, S } },
    [ 9] = { 2, 3, { L, R, C, LS, RS } },
    [10] = { 2, 2, { L, R, C, LFE } },
    [11] = { 2, 3, { L, R, C, LFE, S } },
    [12] = { 2, 4, { L, R, C, LFE, LS, RS } },
    [13] = { 3, 1, { L, R, C, S } },
    [14] = { 3, 2, { L, R, C, LS, RS } },
    [15] = { 3, 1, { L, R, C, LFE } },
    [16] = { 3, 2, { L, R, C, LFE, S } },
    [17] = { 3, 3, { L, R, C, LFE, LS, RS } },
    [18] = { 4, 1, { L, R, LS, RS, LFE } },
    [19] = { 4, 1, { L, R, LS, RS, C } },
    [20] = { 4, 2, { L, R, LS, RS, C, LFE } },
};

int audvd_channel_assignment_layout(int ca, audvd_channel_layout *out)
{
    memset(out, 0, sizeof(*out));
    if (ca < 0 || ca > 20)
        return AUDVD_ERR_RANGE;
    out->group_channels[0] = assignments[ca].g1;
    out->group_channels[1] = assignments[ca].g2;
    out->nb_channels       = assignments[ca].g1 + assignments[ca].g2;
    for (int i = 0; i < out->nb_channels; i++)
        out->speakers[i] = (audvd_speaker)assignments[ca].spk[i];
    return AUDVD_OK;
}

static int quant_bits(int q)
{
    switch (q) {
    case 0: return 16;
    case 1: return 20;
    case 2: return 24;
    default: return 0;
    }
}

static int sample_rate(int fs)
{
    switch (fs) {
    case 0x0: return 48000;
    case 0x1: return 96000;
    case 0x2: return 192000;
    case 0x8: return 44100;
    case 0x9: return 88200;
    case 0xa: return 176400;
    default:  return 0;
    }
}

/*
 * Decode the quantization, sampling frequency and channel assignment
 * bytes, which every DVD-Audio structure encodes like the LPCM packet
 * header: one nibble per channel group, group 1 in the high nibble,
 * 0xf for an absent group 2.
 */
void audvd_decode_attr_bytes(uint8_t quant, uint8_t rate, uint8_t ca,
                             audvd_audio_attr *attr)
{
    attr->group_bits[0] = quant_bits(quant >> 4);
    attr->group_rate[0] = sample_rate(rate >> 4);
    if (ca <= 20 && assignments[ca].g2) {
        attr->group_bits[1] = quant_bits(quant & 0xf);
        attr->group_rate[1] = sample_rate(rate & 0xf);
    } else {
        attr->group_bits[1] = 0;
        attr->group_rate[1] = 0;
    }
    attr->channel_assignment = ca <= 20 ? ca : -1;
    audvd_channel_assignment_layout(ca, &attr->layout);
}

double audvd_downmix_code_to_db(uint8_t code)
{
    if (code == AUDVD_DOWNMIX_MUTE)
        return -INFINITY;
    if (code <= 0xc8)
        return -0.2 * code;
    return -40.0 - 0.4 * (code - 0xc8);
}

double audvd_downmix_code_to_gain(uint8_t code)
{
    if (code == AUDVD_DOWNMIX_MUTE)
        return 0.0;
    return pow(10.0, audvd_downmix_code_to_db(code) / 20.0);
}

/*
 * An 18-byte ATS_DM_COEFT: 2 reserved bytes, then a (left, right) gain
 * code pair for each of 8 channel slots in stream order.
 */
void audvd_decode_downmix(const uint8_t *t, audvd_downmix *out)
{
    for (int ch = 0; ch < AUDVD_DOWNMIX_CHANNELS; ch++) {
        for (int lr = 0; lr < 2; lr++) {
            uint8_t code = t[2 + 2 * ch + lr];
            out->gain_code[ch][lr] = code;
            out->gain[ch][lr]      = (float)audvd_downmix_code_to_gain(code);
        }
    }
}
