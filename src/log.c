/*
 * libaudvd - DVD-Audio navigation library
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdarg.h>
#include <stdio.h>

#include "internal.h"

void audvd_log(const audvd_disc *d, audvd_log_level level, const char *fmt, ...)
{
    char msg[512];
    va_list ap;

    if (!d || !d->log)
        return;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    d->log(d->log_opaque, level, msg);
}

const char *audvd_strerror(int err)
{
    switch (err) {
    case AUDVD_OK:              return "success";
    case AUDVD_ERR_IO:          return "I/O error";
    case AUDVD_ERR_NOMEM:       return "out of memory";
    case AUDVD_ERR_INVALID:     return "invalid or corrupt DVD-Audio structure";
    case AUDVD_ERR_NOTFOUND:    return "no DVD-Audio zone found";
    case AUDVD_ERR_RANGE:       return "index out of range";
    case AUDVD_ERR_UNSUPPORTED: return "unsupported feature";
    case AUDVD_ERR_EOF:         return "end of data";
    default:                    return "unknown error";
    }
}
