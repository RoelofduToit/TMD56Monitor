#ifndef TMD_COMPAT_H
#define TMD_COMPAT_H

/*
 * Small differences between Linux and MinGW. The rest of the program
 * stays in ordinary C.
 */

#include <errno.h>
#include <stdio.h>
#include <time.h>

#if defined(_WIN32)
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

#endif
