#ifndef TMD56_TOOLBAR_MODE_H
#define TMD56_TOOLBAR_MODE_H

#include <stdbool.h>
#include <stddef.h>

/* The device the operator picked. Simulation is a separate mode. */
typedef enum {
    TMD_UI_DEVICE_TMD56 = 0,
    TMD_UI_DEVICE_REPLAY = 1
} TmdUiDevice;

typedef struct {
    bool simulation;
    bool show_device;
    bool show_profile;
    bool show_interval;
    bool show_port;
    bool show_replay;
    bool show_acquire;
    bool show_logging;
    bool show_badge;
} TmdToolbar;

void tmd_toolbar_for(bool simulation, TmdUiDevice device, TmdToolbar *out);

/* Production sample intervals. 500 ms is the fastest offered. */
size_t tmd_interval_count(void);
double tmd_interval_seconds(size_t index);
const char *tmd_interval_label(size_t index);
bool tmd_interval_offered(double seconds);

#endif
