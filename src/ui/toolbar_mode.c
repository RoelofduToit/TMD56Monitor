#include "ui/toolbar_mode.h"

#include <math.h>

static const double INTERVAL_SECONDS[] = {0.5, 1.0, 2.0, 5.0};
static const char *const INTERVAL_LABELS[] = {"500 ms", "1 s", "2 s", "5 s"};

void tmd_toolbar_for(bool simulation, TmdUiDevice device, TmdToolbar *out)
{
    bool replay;

    if (out == NULL) {
        return;
    }
    if (simulation) {
        device = TMD_UI_DEVICE_TMD56;
    }
    replay = !simulation && device == TMD_UI_DEVICE_REPLAY;
    out->simulation = simulation;
    out->show_device = !simulation;
    out->show_profile = simulation;
    out->show_badge = simulation;
    out->show_interval = !replay;
    out->show_port = !simulation && device == TMD_UI_DEVICE_TMD56;
    out->show_replay = replay;
    out->show_acquire = !replay;
    out->show_logging = !replay;
}

size_t tmd_interval_count(void)
{
    return sizeof INTERVAL_SECONDS / sizeof INTERVAL_SECONDS[0];
}

double tmd_interval_seconds(size_t index)
{
    if (index >= tmd_interval_count()) {
        return 0.5;
    }
    return INTERVAL_SECONDS[index];
}

const char *tmd_interval_label(size_t index)
{
    if (index >= tmd_interval_count()) {
        return "500 ms";
    }
    return INTERVAL_LABELS[index];
}

bool tmd_interval_offered(double seconds)
{
    size_t index;
    for (index = 0; index < tmd_interval_count(); index++) {
        if (fabs(INTERVAL_SECONDS[index] - seconds) < 1e-9) {
            return true;
        }
    }
    return false;
}
