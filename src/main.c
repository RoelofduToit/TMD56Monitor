#include "app.h"
#include "platform/user_paths.h"
#include "self_test.h"
#include "ui/main_window.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(_WIN32)
/* Release builds have no console. Attach one only for diagnostics. */
static void connect_stdio(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD type = FILE_TYPE_UNKNOWN;

    if (out != NULL && out != INVALID_HANDLE_VALUE) {
        type = GetFileType(out);
    }
    if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK || type == FILE_TYPE_CHAR) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *stream = NULL;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}
#endif

static int diagnostics(int argc, char **argv)
{
    if (argc < 2 || argv[1] == NULL) {
        return -1;
    }
    if (strcmp(argv[1], "--version") == 0) {
        printf("TMD-56 Temperature Logger %s\n", TMD_VERSION_STRING);
        return 0;
    }
    if (strcmp(argv[1], "--self-test") == 0) {
        return tmd_self_test();
    }
    return -1;
}

int main(int argc, char **argv)
{
    App *app;
    int status;
    int diagnostic;

#if defined(_WIN32)
    if (argc >= 2 && argv[1] != NULL && argv[1][0] == '-') {
        connect_stdio();
    }
#endif
    diagnostic = diagnostics(argc, argv);
    if (diagnostic >= 0) {
        return diagnostic;
    }

    tmd_prepare_runtime();
    app = app_create();
    if (app == NULL) {
        return EXIT_FAILURE;
    }
    status = main_window_run(app, argc, argv);
    app_destroy(app);
    return status;
}
