#ifndef TMD56_SIMULATOR_SOURCE_H
#define TMD56_SIMULATOR_SOURCE_H

#include "sources/measurement_source.h"

typedef enum {
    SIM_STABLE = 0,
    SIM_HEATING,
    SIM_COOLING,
    SIM_GRADIENT,
    SIM_NOISE,
    SIM_SPIKE,
    SIM_SCENARIO_COUNT
} SimulatorScenario;

typedef struct SimulatorSource SimulatorSource;

SimulatorSource *simulator_source_create(void);
void simulator_source_destroy(SimulatorSource *source);
MeasurementSource *simulator_source_base(SimulatorSource *source);

void simulator_source_set_scenario(SimulatorSource *source, SimulatorScenario scenario);
SimulatorScenario simulator_source_scenario(const SimulatorSource *source);
const char *simulator_scenario_name(SimulatorScenario scenario);

/* Sample period in seconds. Values outside 0.1–5 s are ignored. */
void simulator_source_set_period(SimulatorSource *source, double seconds);
double simulator_source_period(const SimulatorSource *source);

#endif
