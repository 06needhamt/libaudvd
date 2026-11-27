/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * File system access: case-insensitive lookup of the AUDIO_TS files and
 * loading of IFO files, with fallback to their .BUP backups.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "internal.h"

char *audvd_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    if (r)
        memcpy(r, s, n);
    return r;
}

static int name_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return !*a && !*b;
}

static char *join(const char *dir, const char *name)
{
    size_t ld = strlen(dir), ln = strlen(name);
    int sep = ld && dir[ld - 1] != '/';
    char *r = malloc(ld + sep + ln + 1);
    if (!r)
        return NULL;
    memcpy(r, dir, ld);
    if (sep)
        r[ld] = '/';
    memcpy(r + ld + sep, name, ln + 1);
    return r;
}

/* Return the path of the entry of dir named name (any case), or NULL. */
char *audvd_find_file(const char *dir, const char *name)
{
    DIR *dh = opendir(dir);
    struct dirent *de;
    char *r = NULL;

    if (!dh)
        return NULL;
    while ((de = readdir(dh))) {
        if (name_eq(de->d_name, name)) {
            r = join(dir, de->d_name);
            break;
        }
    }
    closedir(dh);
    return r;
}

static int is_dir(const char *p)
{
    struct stat st;
    return !stat(p, &st) && S_ISDIR(st.st_mode);
}

char *audvd_find_audio_ts(const char *path)
{
    char *sub, *ifo;

    if (!is_dir(path))
        return NULL;
    sub = audvd_find_file(path, "AUDIO_TS");
    if (sub && is_dir(sub))
        return sub;
    free(sub);

    /* path may be the AUDIO_TS directory itself */
    ifo = audvd_find_file(path, "AUDIO_TS.IFO");
    if (!ifo)
        ifo = audvd_find_file(path, "AUDIO_PP.IFO");
    if (!ifo)
        ifo = audvd_find_file(path, "ATS_01_0.IFO");
    if (ifo) {
        free(ifo);
        return audvd_strdup(path);
    }
    return NULL;
}

int audvd_file_sectors(const char *path, uint32_t *sectors)
{
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode))
        return AUDVD_ERR_IO;
    *sectors = (uint32_t)((uint64_t)st.st_size / AUDVD_SECTOR_SIZE);
    return AUDVD_OK;
}

int audvd_load_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long len;

    if (!f)
        return AUDVD_ERR_IO;
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return AUDVD_ERR_IO;
    }
    if (len == 0 || len > AUDVD_MAX_IFO_SIZE) {
        fclose(f);
        return AUDVD_ERR_INVALID;
    }
    buf = malloc((size_t)len);
    if (!buf) {
        fclose(f);
        return AUDVD_ERR_NOMEM;
    }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return AUDVD_ERR_IO;
    }
    fclose(f);
    *data = buf;
    *size = (size_t)len;
    return AUDVD_OK;
}

/*
 * Load "<base>.IFO" (backup = 0) or its copy "<base>.BUP" (backup = 1)
 * from the AUDIO_TS directory.
 */
int audvd_load_ifo(audvd_disc *d, const char *base, int backup,
                   audvd_buf *out, uint8_t **owned)
{
    char name[32];
    char *p;
    size_t size;
    int ret;

    snprintf(name, sizeof(name), "%s%s", base, backup ? ".BUP" : ".IFO");
    p = audvd_find_file(d->dir, name);
    if (!p)
        return AUDVD_ERR_NOTFOUND;
    ret = audvd_load_file(p, owned, &size);
    if (ret == AUDVD_OK) {
        out->data = *owned;
        out->size = size;
    } else {
        audvd_log(d, AUDVD_LOG_WARNING, "cannot load %s: %s", p,
                  audvd_strerror(ret));
    }
    free(p);
    return ret;
}
