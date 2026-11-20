# libaudvd

A small C library for navigating DVD-Audio discs. It reads the IFO files
in `AUDIO_TS/` (the AMG, the SAMG and the Audio Title Sets) and exposes:

- the disc's titles, grouped as on the disc, and each title's tracks and
  cells, with timing on the title timeline and sector ranges in the AOBs;
- the audio attributes of each track (LPCM / MLP, bit depth and sample
  rate per channel group, the channel assignment and its speaker layout);
- the mixing metadata: the 16 downmix coefficient tables of each title
  set, decoded to linear gains, and the table each track selects, with
  its valid / prohibited flags;
- the SAMG track list used by audio-only players;
- raw sector reads across the `ATS_xx_n.AOB` files.

It has no dependencies, is LGPL-2.1-or-later, and was written from
scratch. See [doc/FORMAT.md](doc/FORMAT.md) for the structure layouts and
the sources they come from. CPPM-protected content is not decrypted.

## Building

```sh
meson setup build
ninja -C build
meson test -C build
```

Options: `-Dtools=false`, `-Dtests=false`, `-Dfuzzer=true` (a libFuzzer
target, needs clang with compiler-rt), and `-Dtest_disc=/path/to/disc`.
The last one runs structural consistency checks on a real disc or rip.

## Usage

```c
#include <audvd/audvd.h>

audvd_disc *disc;
if (audvd_open("/media/dvd", NULL, &disc) == AUDVD_OK) {
    for (int i = 0; i < audvd_num_titles(disc); i++) {
        audvd_title t;
        audvd_get_title(disc, i, &t);
        for (int k = 0; k < t.nb_tracks; k++) {
            audvd_track tr;
            if (audvd_get_track(disc, i, k, &tr) == AUDVD_OK)
                printf("%d.%d: %u sectors\n", t.number, tr.number,
                       tr.last_sector - tr.first_sector + 1);
        }
    }
    audvd_close(disc);
}
```

`audvd-info [-v] <disc>` prints the whole structure.
