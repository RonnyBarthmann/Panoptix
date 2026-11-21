/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Einfaches Logging nach stderr. */
#include <stdarg.h>
#include <stdio.h>

#include "core/util.h"

static void log_write(const char *level, const char *fmt, va_list ap) {
    fprintf(stderr, "[%s] ", level);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("info", fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("warn", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_write("FEHLER", fmt, ap);
    va_end(ap);
}
