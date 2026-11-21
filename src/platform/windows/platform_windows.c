/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Windows-Implementierung der Plattform-Schnittstelle (platform.h). */
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <windows.h>

#include "core/util.h"
#include "platform/platform.h"

bool platform_file_exists(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES;
}

static bool dir_exists(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool platform_make_dir(const char *path) {
    char tmp[512];
    str_copy(tmp, sizeof tmp, path);
    size_t len = strlen(tmp);
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char c = tmp[i];
            tmp[i] = '\0';
            if (i > 2 && !dir_exists(tmp)) {
                _mkdir(tmp);
            }
            tmp[i] = c;
        }
    }
    if (!dir_exists(tmp)) {
        _mkdir(tmp);
    }
    return dir_exists(path);
}

bool platform_get_save_dir(char *out, size_t size) {
    const char *appdata = getenv("APPDATA");
    if (appdata && *appdata) {
        str_format(out, size, "%s\\Panoptix", appdata);
    } else {
        str_copy(out, size, ".\\save");
    }
    return platform_make_dir(out);
}

bool platform_get_data_dir(char *out, size_t size) {
    const char *env = getenv("PANOPTIX_DATA");
    if (env && *env && dir_exists(env)) {
        str_copy(out, size, env);
        return true;
    }
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe);
    if (n > 0 && n < sizeof exe) {
        char *slash = strrchr(exe, '\\');
        if (slash) {
            *slash = '\0';
            char cand[MAX_PATH + 16];
            str_format(cand, sizeof cand, "%s\\data", exe);
            if (dir_exists(cand)) {
                str_copy(out, size, cand);
                return true;
            }
        }
    }
    if (dir_exists(".\\data")) {
        str_copy(out, size, ".\\data");
        return true;
    }
    return false;
}

bool platform_replace_file(const char *src, const char *dest) {
    return MoveFileExA(src, dest, MOVEFILE_REPLACE_EXISTING) != 0;
}

i64 platform_unix_time(void) {
    return (i64)time(NULL);
}

void platform_local_date(int *year, int *month, int *day, int *weekday, int *hour) {
    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    if (!tmv) {
        return;
    }
    if (year) {
        *year = tmv->tm_year + 1900;
    }
    if (month) {
        *month = tmv->tm_mon + 1;
    }
    if (day) {
        *day = tmv->tm_mday;
    }
    if (weekday) {
        *weekday = (tmv->tm_wday + 6) % 7;
    }
    if (hour) {
        *hour = tmv->tm_hour;
    }
}

double platform_monotonic_seconds(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
