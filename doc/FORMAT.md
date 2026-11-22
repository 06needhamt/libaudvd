# DVD-Audio navigation structures, as parsed by libaudvd

All multi-byte values are big-endian. "Sector" is a 2048-byte logical
block. Offsets are in bytes from the start of the structure.

Every field below is tagged with where it comes from:

- **[disc]**: checked against the IFOs of a commercial disc (one
  audio-only title, 18 MLP tracks, one audio+video title). For example,
  the track lengths add up to the PGC length exactly, and the SAMG and
  ATS sector ranges agree after a constant offset.
- **[patent]**: US 6,580,671 / US 2001/0008578.
- **[libdvdread]**: descriptions in libdvdread's 2025–2026 DVD-Audio
  work (only commit messages were consulted, no code was copied).
- **[guess]**: inferred, and not yet confirmed by a second source.

The parsers rely only on the first three kinds. A **[guess]** field is
either exposed as a raw value or used with a fallback.

## Files in `AUDIO_TS/`

| File | Identifier | Content |
|------|------------|---------|
| `AUDIO_TS.IFO` / `.BUP` | `DVDAUDIO-AMG` | Audio Manager: the disc's title list |
| `AUDIO_PP.IFO` | `DVDAUDIOSAPP` | Simple Audio Manager (SAMG): flat track list for audio-only players |
| `AUDIO_SV.IFO` | `DVDAUDIOASVS` | Audio still video set (slideshow pictures), not parsed yet |
| `ATS_xx_0.IFO` / `.BUP` | `DVDAUDIO-ATS` | Audio Title Set information |
| `ATS_xx_1..9.AOB` | – | Audio objects: MPEG-2 PS packs, one address space |

## AMGI (`AUDIO_TS.IFO`)

| Offset | Size | Field | Source |
|-------:|-----:|-------|--------|
| 0x00 | 12 | `DVDAUDIO-AMG` | disc |
| 0x0C | 4 | last sector of the AMG | disc |
| 0x1C | 4 | last sector of the AMGI (file size / 2048 − 1) | disc |
| 0x20 | 2 | specification version (0x0011) | disc |
| 0x30 | 4 | start sector of the ASVS (AUDIO_SV) | guess |
| 0xC0 | 4 | start sector of the AMGM_VOBS (menu) | libdvdread, disc |
| 0xC4 | 4 | start sector of the ATT_SRPT, in this file | libdvdread, disc |
| 0xC8 | 4 | start sector of the AOTT_SRPT, in this file | libdvdread, disc |
| 0xCC | 4 | start sector of the AMGM_PGCI_UT | libdvdread, disc |

**ATT_SRPT** (all titles, audio-only and audio-with-video): a 4-byte
header (u16 title count, u16 last byte), then 14-byte entries:

| Offset | Size | Field | Source |
|-------:|-----:|-------|--------|
| 0 | 1 | type: bit 7 = audio-only title (AOTT); low nibble = group (guess) | disc |
| 1 | 1 | number of tracks | disc |
| 2 | 2 | ? (0 for AOTT, 0x0100 for the AVTT) | – |
| 4 | 4 | title length, 90 kHz | disc |
| 8 | 1 | ATS number (VTS number for an AVTT) | disc |
| 9 | 1 | title number within the ATS / VTS | disc |
| 10 | 4 | a sector address, meaning unclear (not used) | guess |

The group is first taken from the low nibble of the type byte. It is
then replaced by the SAMG's group for the title's first track whenever
the ATS can be located in the SAMG (see `aob_lba`).

## SAMG (`AUDIO_PP.IFO`)

A 16-byte header: identifier, then a u16 track count at 0x0C and a u16
at 0x0E (0x0200, perhaps a version). After it come 52-byte records
starting at 0x10. The file seen was 64 sectors, with the table repeated 8
times.

| Offset | Size | Field | Source |
|-------:|-----:|-------|--------|
| 2 | 1 | group | disc |
| 3 | 1 | track number in the group | disc |
| 4 | 4 | first PTS of the track | disc (= ATS PGI) |
| 8 | 4 | track length, 90 kHz | disc (= ATS PGI) |
| 16 | 1 | record code: bit 7 = first track of a group | libdvdread, disc |
| 17 | 1 | quantization, group 1 / 2 nibbles | libdvdread, disc |
| 18 | 1 | sampling frequency, group 1 / 2 nibbles | libdvdread, disc |
| 19 | 1 | channel assignment | libdvdread, disc |
| 40 | 4 | first sector (absolute on the disc) | disc |
| 44 | 4 | first sector again (index / pre-gap?) | disc |
| 48 | 4 | last sector (absolute) | disc |

## ATSI (`ATS_xx_0.IFO`)

### ATSI_MAT (sector 0)

| Offset | Size | Field | Source |
|-------:|-----:|-------|--------|
| 0x00 | 12 | `DVDAUDIO-ATS` | disc |
| 0x0C | 4 | last sector of the ATS | disc |
| 0x1C | 4 | last sector of the ATSI | disc |
| 0x20 | 2 | specification version | disc |
| 0xC4 | 4 | start sector of the AOBs, relative to the ATS | disc |
| 0xCC | 4 | start sector of the ATS_PGCIT, in this file | disc |
| 0x100 | 8×16 | audio attributes (ATS_AST_ATR) | disc |
| 0x180 | 16×18 | downmix coefficient tables (ATS_DM_COEFT) | libdvdread, patent |

**Audio attribute (16 bytes):**
- byte 0 is the coding mode: 0x01 = MLP [disc]. 0x00 = LPCM [guess].
  The disc's 24/96 5.1 attribute uses 0x01, and that format only fits
  the bitrate if it is MLP.
- byte 2 is quantization, byte 3 is sampling frequency, and byte 4 is
  the channel assignment. All three are encoded the same way as in the
  LPCM packet header [disc].

**Quantization nibble:** 0 = 16, 1 = 20, 2 = 24 bits.
**Frequency nibble:** 0 = 48, 1 = 96, 2 = 192, 8 = 44.1, 9 = 88.2,
0xA = 176.4 kHz.
For both, 0xF means "no second group".

**Downmix table (18 bytes):** 2 reserved bytes, then 8 (left, right)
gain-code pairs, one per source channel slot [libdvdread].
- **Gain codes:** 0x00..0xC8 drop 0.2 dB per step (0 to −40 dB).
  0xC9..0xFE drop 0.4 dB per step below −40 dB. 0xFF mutes.
  libdvdread's text says −61.8 dB at 0xFE, but the stated steps give
  −61.6 dB; libaudvd follows the steps.
- **Slot order:** libdvdread describes it as "stream order with LFE
  last", but its own 5.1 example (L R C Ls Rs LFE) does not match the
  5.1 stream order (L R Ls Rs C LFE). libaudvd maps slot n to stream
  channel n. **This needs checking against a disc that has valid
  tables**; the reference disc has them all zero and marks them invalid.
- **Phase:** no phase or polarity bits are documented, so the API
  exposes none.

### ATS_PGCIT (from the sector at 0xCC)

The header is u16 PGC count, u16 reserved and u32 last byte (relative
to the table). Then come 8-byte search pointers: 4 category bytes (`81
01 01 00` seen) and a u32 PGC offset relative to the table.

**PGC header (16 bytes):**

| Offset | Size | Field | Source |
|-------:|-----:|-------|--------|
| 2 | 1 | number of tracks (programs) | disc |
| 3 | 1 | number of cells | disc |
| 4 | 4 | length, 90 kHz | disc |
| 10 | 2 | offset of the track (PGI) table, relative to the PGC | disc |
| 12 | 2 | offset of the cell sector table | disc |
| 14 | 2 | offset of the still-picture (ASV) table, not parsed | disc |

**Track / ATS_PGI (20 bytes):**

| Offset | Bits | Field | Source |
|-------:|-----:|-------|--------|
| 0 | b7 | relation to the previous program | libdvdread |
| 0 | b6 | STC discontinuity (PTS restart) | libdvdread, disc (set on every track) |
| 0 | b5–b3 | ATRN: audio attribute index | libdvdread, disc (the stereo track has 1) |
| 0 | b2–b0 | bit shift of channel group 2 | libdvdread |
| 1 | b5 | downmix prohibited | libdvdread |
| 1 | b4 | downmix coefficients not valid | libdvdread, disc (set, tables zero) |
| 1 | b3–b0 | DM_COEFTN: downmix table index | libdvdread |
| 2 | 16 | RTI (real-time text) flags | libdvdread |
| 4 | 8 | first cell number (1-based) | disc |
| 6 | 32 | first PTS of the track in the AOB stream | disc |
| 10 | 32 | track length, 90 kHz | disc |
| 14 | 32 | pause length | libdvdread |

**Cell (12 bytes):** byte 0 is the cell id (1 on every cell of the
reference disc). It is followed by u32 first and u32 last sector at 4
and 8, relative to the start of `ATS_xx_1.AOB` [disc].

Each track spans the cells from its first cell up to the next track's
first cell. The library computes each track's start on the title
timeline by adding up the earlier track lengths. The AOB timestamps
restart at every track.

## AOB sector address space

`ATS_xx_1.AOB` … `ATS_xx_9.AOB` are concatenated, and each file is a
whole number of sectors. The absolute disc address of AOB sector 0
(`aob_lba`) is not stored anywhere that libaudvd reads. It is recovered
by matching the ATS tracks against SAMG entries with the same length and
first PTS. The reference disc gives 1707.

File sizes are not fixed at 1 GiB. On the reference disc, `ATS_01_4.AOB`
starts at AOB sector 1572816, so the first three files are 524272 sectors
each (1 GiB − 32 KiB). libaudvd therefore takes every file's size from the
file system rather than assuming one. All four files together end exactly
at the title's last cell sector.

## AOB pack structure (`src/pack.c`)

Each 2048-byte AOB sector is an MPEG-2 (or MPEG-1) program stream pack,
as on DVD-Video:

- a `00 00 01 BA` pack header, 14 bytes for an MPEG-2 pack (`4 bits
  '01', SCR, mux rate, stuffing length in the low 3 bits of the last
  byte`) or 12 bytes for MPEG-1 (`4 bits '0010'`, SCR, mux rate) [disc,
  standard MPEG-2 PS layout];
- optionally a `00 00 01 BB` system header;
- one or more PES packets: `00 00 01 <stream id>`, u16 length, then for
  an MPEG-2 PES (`'10'` in the top 2 bits of the next byte) the usual
  flags, header-data length and optional PTS/DTS. Private stream 1
  (`0xBD`, the LPCM/MLP audio) and padding (`0xBE`) are the only ones
  libaudvd needs to walk past; a `0xB9` MPEG program end code stops the
  walk.

**Scrambling** [disc, matches the DVD-Video `PES_scrambling_control`
field]: bits 5–4 of the PES flags byte (the byte right after the 16-bit
PES length) are `00` for clear content and non-zero (`01`/`10`/`11`,
depending on the key period) when the pack is CPPM-scrambled.
`audvd_pack_is_scrambled()` reports any non-zero value; libaudvd never
attempts to decrypt a scrambled pack.

**PTS** [standard MPEG-2 PES, same 5-byte 33-bit layout as DVD-Video]:
present when bit 7 of the flags byte is set and the header-data length
is at least 5. The library reads the first PES in the pack that carries
one. On the reference disc, every other audio pack omits the PTS
(`audvd_pack_pts()` returns -1 for those); `audvd_reader_seek_pts()`
probes forward up to 16 sectors to find one.

Because a track's PTS restarts near its `first_pts` on every track (the
PGI's STC-discontinuity bit, see above, is set on every track observed),
a PTS read from a pack must be rebased onto the title timeline with
`audvd_pos_title_pts()`: `title_pts = track_start_pts + ((pack_pts -
track_first_pts) mod 2^33)`, taking the shorter way around the 33-bit
wrap. This has not been exercised against a real wrap-around, since the
reference disc's tracks are all far too short.

On the reference disc's `ATS_01_4.AOB` (tracks 15 end, 16–18), no pack is
scrambled. Each track's PTS restarts at 94 (85 on the 48 kHz stereo
track 18), and the PTS restarts sit exactly on the IFO's cell boundaries.
The PES PTS values are rounded to whole 90 kHz ticks, so FFmpeg's MLP
parser, which interpolates 75 ticks per 80-sample unit, sometimes runs
one tick ahead of the next coded PTS. That is a property of the stream
(the stock `mpeg` demuxer shows the same) and does not affect the decoded
audio.
