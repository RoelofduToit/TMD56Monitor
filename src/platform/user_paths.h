#ifndef TMD_USER_PATHS_H
#define TMD_USER_PATHS_H

#include <stddef.h>

/*
 * Writable per-user locations. Logs are never placed next to the executable.
 *
 * TMD56_DATA_DIR, when set, replaces the platform data root. Tests use it.
 * Otherwise:
 *   Windows: %LOCALAPPDATA%\TMD56Monitor\logs
 *   Linux:   $XDG_DATA_HOME/TMD56Monitor/logs
 *            (GLib's usual ~/.local/share fallback)
 */

const char *user_logs_dir(void);
int user_logs_ensure(char *err, size_t err_len);

/* Windows bundles point GLib, gdk-pixbuf and fontconfig at the install directory before GTK starts. No-op on Linux. */
void tmd_prepare_runtime(void);

#endif
