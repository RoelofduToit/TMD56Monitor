#ifndef TMD_COMPAT_H
#define TMD_COMPAT_H

/*
 * Small differences between Linux and MinGW. The rest of the program
 * stays in ordinary C.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#define tmd_fsync(fd) _commit(fd)
static inline int tmd_mkdir(const char *path)
{
    if (_mkdir(path) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}
static inline int tmd_localtime(const time_t *when, struct tm *out)
{
    return localtime_s(out, when) == 0 ? 0 : -1;
}
#else
#include <sys/stat.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#define tmd_fsync(fd) fsync(fd)
static inline int tmd_mkdir(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}
static inline int tmd_localtime(const time_t *when, struct tm *out)
{
    return localtime_r(when, out) == NULL ? -1 : 0;
}
#endif

/* Create an empty directory and write its path to buf. */
static inline int tmd_temp_dir(char *buf, size_t len, const char *tag)
{
#if defined(_WIN32)
    char root[MAX_PATH];
    char candidate[MAX_PATH];
    unsigned i;
    DWORD n = GetTempPathA((DWORD)sizeof root, root);
    if (n == 0 || n >= sizeof root) {
        return -1;
    }
    for (i = 0; i < 100u; i++) {
        int written = snprintf(candidate, sizeof candidate, "%s%s-%lu-%u",
                               root, tag, (unsigned long)GetCurrentProcessId(), i);
        if (written < 0 || (size_t)written >= sizeof candidate || (size_t)written + 1u > len) {
            return -1;
        }
        if (_mkdir(candidate) == 0) {
            memcpy(buf, candidate, (size_t)written + 1u);
            return 0;
        }
    }
    return -1;
#else
    int written = snprintf(buf, len, "/tmp/%s-XXXXXX", tag);
    if (written < 0 || (size_t)written >= len) {
        return -1;
    }
    return mkdtemp(buf) == NULL ? -1 : 0;
#endif
}

#endif
