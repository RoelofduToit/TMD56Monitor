#include "platform/user_paths.h"

#include <stdio.h>
#include <string.h>

#include <glib.h>

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static char *logs_dir;

const char *user_logs_dir(void)
{
    const char *override;
    const char *base;

    if (logs_dir != NULL) {
        return logs_dir;
    }
    override = g_getenv("TMD56_DATA_DIR");
    if (override != NULL && override[0] != '\0') {
        logs_dir = g_build_filename(override, "logs", NULL);
        return logs_dir;
    }
#ifdef G_OS_WIN32
    base = g_getenv("LOCALAPPDATA");
    if (base == NULL || base[0] == '\0') {
        base = g_get_user_data_dir();
    }
    logs_dir = g_build_filename(base, "TMD56Monitor", "logs", NULL);
#else
    base = g_get_user_data_dir();
    logs_dir = g_build_filename(base, "TMD56Monitor", "logs", NULL);
#endif
    return logs_dir;
}

int user_logs_ensure(char *err, size_t err_len)
{
    const char *dir = user_logs_dir();
    if (dir == NULL || dir[0] == '\0') {
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "%s", "Could not choose a logs directory");
        }
        return -1;
    }
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        fprintf(stderr, "tmd56: could not create logs directory %s\n", dir);
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "%s", "Could not create the logs directory");
        }
        return -1;
    }
    return 0;
}

#ifdef G_OS_WIN32
static int win_is_dir(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int win_is_file(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* The loader ABI directory is usually 2.10.0. Accept a sibling if it changes. */
static char *pixbuf_cache_path(const char *exe_dir)
{
    char *hardcoded;
    char *root;
    GDir *dir;
    const char *name;
    char *found = NULL;

    hardcoded = g_build_filename(exe_dir, "lib", "gdk-pixbuf-2.0", "2.10.0", "loaders.cache", NULL);
    if (g_file_test(hardcoded, G_FILE_TEST_EXISTS)) {
        return hardcoded;
    }
    g_free(hardcoded);
    root = g_build_filename(exe_dir, "lib", "gdk-pixbuf-2.0", NULL);
    dir = g_dir_open(root, 0, NULL);
    if (dir != NULL) {
        while ((name = g_dir_read_name(dir)) != NULL) {
            char *candidate = g_build_filename(root, name, "loaders.cache", NULL);
            if (g_file_test(candidate, G_FILE_TEST_EXISTS)) {
                found = candidate;
                break;
            }
            g_free(candidate);
        }
        g_dir_close(dir);
    }
    g_free(root);
    return found;
}
#endif

void tmd_prepare_runtime(void)
{
#ifdef G_OS_WIN32
    char module[4096];
    DWORD length;
    char *dir;
    char *share;
    char *schemas;
    char *pixbuf;
    char *gio_modules;
    char *fonts_conf;
    const char *existing;
    char *data_dirs;

    length = GetModuleFileNameA(NULL, module, (DWORD)sizeof module);
    if (length == 0 || length >= (DWORD)sizeof module) {
        return;
    }
    dir = g_path_get_dirname(module);

    /* Pixbuf loaders live under lib/ and import DLLs that sit beside the exe.
       Windows does not search the exe directory for those imports. */
    SetDllDirectoryA(dir);
    {
        const char *old_path = g_getenv("PATH");
        char *new_path = (old_path != NULL && old_path[0] != '\0')
                             ? g_strjoin(";", dir, old_path, NULL)
                             : g_strdup(dir);
        g_setenv("PATH", new_path, TRUE);
        g_free(new_path);
    }

    /* Point GIO at the bundle before any GLib file call can scan modules. */
    gio_modules = g_build_filename(dir, "lib", "gio", "modules", NULL);
    if (win_is_dir(gio_modules)) {
        g_setenv("GIO_MODULE_DIR", gio_modules, TRUE);
    }
    g_free(gio_modules);
    fonts_conf = g_build_filename(dir, "etc", "fonts", "fonts.conf", NULL);
    if (win_is_file(fonts_conf)) {
        g_setenv("FONTCONFIG_FILE", fonts_conf, TRUE);
    }
    g_free(fonts_conf);

    share = g_build_filename(dir, "share", NULL);
    schemas = g_build_filename(share, "glib-2.0", "schemas", NULL);
    pixbuf = pixbuf_cache_path(dir);
    if (g_file_test(schemas, G_FILE_TEST_IS_DIR)) {
        g_setenv("GSETTINGS_SCHEMA_DIR", schemas, TRUE);
    }
    if (pixbuf != NULL) {
        g_setenv("GDK_PIXBUF_MODULE_FILE", pixbuf, TRUE);
    }
    existing = g_getenv("XDG_DATA_DIRS");
    if (existing != NULL && existing[0] != '\0') {
        data_dirs = g_strjoin(";", share, existing, NULL);
    } else {
        data_dirs = g_strdup(share);
    }
    g_setenv("XDG_DATA_DIRS", data_dirs, TRUE);
    g_free(data_dirs);
    g_free(pixbuf);
    g_free(schemas);
    g_free(share);
    g_free(dir);
#else
    /* Linux packages and the AppImage supply their own data directories. */
#endif
}
