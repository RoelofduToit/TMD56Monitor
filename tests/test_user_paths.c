#include "platform/user_paths.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

static void expect_true(int condition, const char *file, int line, const char *text)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s:%d %s\n", file, line, text);
        failures++;
    }
}

#define EXPECT(condition) expect_true((condition), __FILE__, __LINE__, #condition)

static int run_default_root(const char *exe, const char *root)
{
    char *argv[] = {(char *)exe, "--default", (char *)root, NULL};
    int status = 1;
    GError *error = NULL;
    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, &status, &error)) {
        fprintf(stderr, "FAIL spawn: %s\n", error != NULL ? error->message : "unknown");
        g_clear_error(&error);
        return 1;
    }
    return status;
}

static int check_default(const char *root)
{
    const char *logs;
    g_unsetenv("TMD56_DATA_DIR");
#ifdef G_OS_WIN32
    g_setenv("LOCALAPPDATA", root, TRUE);
#else
    g_setenv("XDG_DATA_HOME", root, TRUE);
#endif
    logs = user_logs_dir();
    if (logs == NULL || strstr(logs, root) == NULL || strstr(logs, "TMD56Monitor") == NULL ||
        strstr(logs, "logs") == NULL) {
        fprintf(stderr, "FAIL default logs path: %s\n", logs != NULL ? logs : "(null)");
        return 1;
    }
    if (user_logs_ensure(NULL, 0) != 0 || !g_file_test(logs, G_FILE_TEST_IS_DIR)) {
        fprintf(stderr, "FAIL could not create %s\n", logs);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char dir[] = "/tmp/tmd56-paths-XXXXXX";
    char child_root[] = "/tmp/tmd56-xdg-XXXXXX";
    char exe[4096];
    const char *logs;
    char saved[1024];
    char cwd[1024];

    if (argc > 2 && strcmp(argv[1], "--default") == 0) {
        return check_default(argv[2]);
    }
    if (mkdtemp(dir) == NULL || mkdtemp(child_root) == NULL) {
        fprintf(stderr, "FAIL could not create temporary directories\n");
        return EXIT_FAILURE;
    }
    if (g_path_is_absolute(argv[0])) {
        snprintf(exe, sizeof exe, "%s", argv[0]);
    } else {
        snprintf(exe, sizeof exe, "%s/%s", g_get_current_dir(), argv[0]);
    }

    EXPECT(run_default_root(exe, child_root) == 0);

    g_setenv("TMD56_DATA_DIR", dir, TRUE);
    logs = user_logs_dir();
    EXPECT(logs != NULL);
    EXPECT(strstr(logs, dir) != NULL);
    EXPECT(strstr(logs, "logs") != NULL);
    EXPECT(!g_path_is_absolute(logs) || strstr(logs, dir) != NULL);
    EXPECT(user_logs_ensure(NULL, 0) == 0);
    EXPECT(g_file_test(logs, G_FILE_TEST_IS_DIR));
    snprintf(saved, sizeof saved, "%s", logs);
    if (getcwd(cwd, sizeof cwd) != NULL && chdir("/") == 0) {
        EXPECT(strcmp(user_logs_dir(), saved) == 0);
        EXPECT(chdir(cwd) == 0);
    }
    EXPECT(strstr(saved, "./logs") == NULL);

    if (failures != 0) {
        fprintf(stderr, "%d path check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("user path tests passed\n");
    return EXIT_SUCCESS;
}
