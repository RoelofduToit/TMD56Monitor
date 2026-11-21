#ifndef TMD56_LIVE_PLOT_H
#define TMD56_LIVE_PLOT_H

#include "history_buffer.h"

#include <cairo.h>
#include <stdbool.h>

/*
 * Colours are filled from the GTK stylesheet (@define-color in style.css)
 * so a later theme does not require a second copy of the plot code.
 */
typedef struct {
    double bg[3];
    double grid[3];
    double axis[3];
    double t1[3];
    double t2[3];
    double mark[3];
} PlotPalette;

/* View of the acquisition history. Rendering never writes back into it. */
typedef struct {
    const HistoryBuffer *history;
    double window_seconds;
    bool hide_suspicious;
    bool hide_t1, hide_t2, show_delta;
    bool manual_time, manual_y;
    double time_start, time_end, y_min, y_max;
    double drawn_t0, drawn_t1, drawn_y0, drawn_y1;
    const char *overlay;
    PlotPalette palette;
    bool have_palette;
    /* Last Y limits. Held so a 0.01 °C wiggle does not slide the axis. */
    bool have_span;
    double span_y0;
    double span_y1;
    double span_window;
    size_t span_count;
} LivePlot;

void live_plot_draw(LivePlot *plot, cairo_t *cr, int width, int height);

/*
 * Visible elapsed-time span. A positive window_seconds is the width of
 * the time axis. A shorter run leaves the rest of that axis empty.
 * Once the series is longer than the window, the axis rolls with the
 * newest sample. A non-positive window shows the whole series.
 */
void live_plot_visible_time(double first_s, double last_s, double window_seconds,
                            double *t0, double *t1);

#endif
