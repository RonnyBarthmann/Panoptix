/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Linux-Implementierung der Plattform-Schnittstelle (platform.h). */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "core/util.h"
#include "platform/platform.h"

bool platform_file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool platform_make_dir(const char *path) {
    char tmp[512];
    str_copy(tmp, sizeof tmp, path);
    size_t len = strlen(tmp);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return dir_exists(path);
}

bool platform_get_save_dir(char *out, size_t size) {
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) {
        str_format(out, size, "%s/panoptix", xdg);
    } else if (home && *home) {
        str_format(out, size, "%s/.local/share/panoptix", home);
    } else {
        str_copy(out, size, "./save");
    }
    return platform_make_dir(out);
}

bool platform_get_data_dir(char *out, size_t size) {
    const char *env = getenv("PANOPTIX_DATA");
    if (env && *env && dir_exists(env)) {
        str_copy(out, size, env);
        return true;
    }
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            char cand[560];
            str_format(cand, sizeof cand, "%s/data", exe);
            if (dir_exists(cand)) {
                str_copy(out, size, cand);
                return true;
            }
            /* Entwicklungs-Layout: build/linux/panoptix -> ../../data */
            str_format(cand, sizeof cand, "%s/../../data", exe);
            if (dir_exists(cand)) {
                str_copy(out, size, cand);
                return true;
            }
        }
    }
    if (dir_exists("./data")) {
        str_copy(out, size, "./data");
        return true;
    }
#ifdef PNX_DATADIR
    if (dir_exists(PNX_DATADIR)) {
        str_copy(out, size, PNX_DATADIR);
        return true;
    }
#endif
    return false;
}

bool platform_replace_file(const char *src, const char *dest) {
    return rename(src, dest) == 0;
}

i64 platform_unix_time(void) {
    return (i64)time(NULL);
}

void platform_local_date(int *year, int *month, int *day, int *weekday, int *hour) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    if (year) {
        *year = tmv.tm_year + 1900;
    }
    if (month) {
        *month = tmv.tm_mon + 1;
    }
    if (day) {
        *day = tmv.tm_mday;
    }
    if (weekday) {
        *weekday = (tmv.tm_wday + 6) % 7; /* 0 = Montag */
    }
    if (hour) {
        *hour = tmv.tm_hour;
    }
}

double platform_monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
