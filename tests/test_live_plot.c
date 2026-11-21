#include "ui/live_plot.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void expect_span(const char *name, double first, double last, double window,
                        double want0, double want1)
{
    double t0 = -1.0;
    double t1 = -1.0;
    live_plot_visible_time(first, last, window, &t0, &t1);
    if (fabs(t0 - want0) > 1e-6 || fabs(t1 - want1) > 1e-6) {
        fprintf(stderr, "FAIL %s: got %.3f..%.3f, want %.3f..%.3f\n",
                name, t0, t1, want0, want1);
        failures++;
    }
}

int main(void)
{
    /* The selected range is the axis width, even before that much data exists. */
    expect_span("3 min inside 60 min", 0.0, 180.0, 3600.0, 0.0, 3600.0);
    expect_span("10 s inside 1 min", 0.0, 10.0, 60.0, 0.0, 60.0);
    expect_span("5 min inside 10 min", 0.0, 300.0, 600.0, 0.0, 600.0);
    expect_span("a few seconds inside 5 min", 0.0, 3.0, 300.0, 0.0, 300.0);

    /* Once the series is at least as long as the window, it rolls. */
    expect_span("roll 1 min", 0.0, 90.0, 60.0, 30.0, 90.0);
    expect_span("roll 60 min", 0.0, 4000.0, 3600.0, 400.0, 4000.0);
    expect_span("exact window", 0.0, 60.0, 60.0, 0.0, 60.0);

    /* One sample still uses the selected range. */
    expect_span("one sample in 5 min", 0.0, 0.0, 300.0, 0.0, 300.0);

    /* ALL keeps the whole series. */
    expect_span("all", 0.0, 8000.0, 0.0, 0.0, 8000.0);
    expect_span("all short", 2.0, 2.0, 0.0, 2.0, 3.0);

    HistoryBuffer history;
    if (history_buffer_init(&history) != 0) return EXIT_FAILURE;
    TemperatureMeasurement sample;
    measurement_set(&sample, 150.0, 25.0, 0.0, true);
    history_buffer_push(&history, &sample);
    measurement_set(&sample, 155.0, 26.0, 10.0, true);
    history_buffer_push(&history, &sample);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 480);
    cairo_t *cr = cairo_create(surface);
    LivePlot plot = {.history = &history, .hide_t1 = true};
    live_plot_draw(&plot, cr, 800, 480);
    if (!(plot.drawn_y1 < 100 && plot.drawn_y0 < 25)) failures++;
    plot.show_delta = true; plot.have_span = false;
    live_plot_draw(&plot, cr, 800, 480);
    if (!(plot.drawn_y1 >= 129)) failures++;
    plot.manual_time = true; plot.time_start = 3; plot.time_end = 7;
    plot.manual_y = true; plot.y_min = -10; plot.y_max = 80;
    live_plot_draw(&plot, cr, 800, 480);
    if (plot.drawn_t0 != 3 || plot.drawn_t1 != 7 || plot.drawn_y0 != -10 || plot.drawn_y1 != 80) failures++;
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) failures++;
    cairo_destroy(cr); cairo_surface_destroy(surface); history_buffer_free(&history);

    if (failures != 0) {
        fprintf(stderr, "%d live-plot checks failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
