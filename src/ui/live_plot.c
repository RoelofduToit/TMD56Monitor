#include "ui/live_plot.h"

#include <math.h>
#include <stdio.h>

static const double PLOT_PI = 3.14159265358979323846;

/* Used only if the stylesheet colours could not be read. Matches style.css. */
static const PlotPalette PLOT_FALLBACK = {
    .bg = {0.086, 0.090, 0.098},
    .grid = {0.216, 0.227, 0.247},
    .axis = {0.663, 0.678, 0.702},
    .t1 = {0.843, 0.098, 0.125},
    .t2 = {0.235, 0.706, 0.839},
    .mark = {0.898, 0.718, 0.184},
};

void live_plot_visible_time(double first_s, double last_s, double window_seconds,
                            double *t0, double *t1)
{
    double start = first_s;
    double end = last_s;

    if (!isfinite(start)) {
        start = 0.0;
    }
    if (!isfinite(end) || end < start) {
        end = start;
    }
    if (window_seconds > 0.0) {
        if ((end - start) + 1e-9 >= window_seconds) {
            start = end - window_seconds;
        } else {
            end = start + window_seconds;
        }
    } else if (end - start < 1.0) {
        end = start + 1.0;
    }
    if (t0 != NULL) {
        *t0 = start;
    }
    if (t1 != NULL) {
        *t1 = end;
    }
}

static const PlotPalette *palette_of(const LivePlot *plot)
{
    if (plot != NULL && plot->have_palette) {
        return &plot->palette;
    }
    return &PLOT_FALLBACK;
}

static void paint(cairo_t *cr, const double rgb[3])
{
    cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]);
}

static double nice_step(double span, int ticks)
{
    double rough;
    double magnitude;
    double residual;
    double nice;

    if (!(span > 0.0)) {
        return 1.0;
    }
    if (ticks < 1) {
        ticks = 1;
    }
    rough = span / (double)ticks;
    magnitude = pow(10.0, floor(log10(rough)));
    if (!(magnitude > 0.0) || !isfinite(magnitude)) {
        return 1.0;
    }
    residual = rough / magnitude;
    if (residual <= 1.0) {
        nice = 1.0;
    } else if (residual <= 2.0) {
        nice = 2.0;
    } else if (residual <= 5.0) {
        nice = 5.0;
    } else {
        nice = 10.0;
    }
    return nice * magnitude;
}

static void format_tick(char *buf, size_t len, double value, double step)
{
    double abs_step = fabs(step);
    if (value == 0.0) {
        value = 0.0;
    }
    if (abs_step >= 1.0) {
        snprintf(buf, len, "%.0f", value);
    } else if (abs_step >= 0.1) {
        snprintf(buf, len, "%.1f", value);
    } else {
        snprintf(buf, len, "%.2f", value);
    }
}

/* Keep a centred or right-aligned label inside the widget when the plot is resized. */
static double place_label(cairo_t *cr, double x, const char *text, int anchor,
                          double min_x, double max_x)
{
    cairo_text_extents_t extents;
    double left;
    double right;

    cairo_text_extents(cr, text, &extents);
    if (anchor == 1) {
        left = x - extents.width / 2.0;
    } else if (anchor == 2) {
        left = x - extents.width;
    } else {
        left = x;
    }
    right = left + extents.width;
    if (left < min_x) {
        x += min_x - left;
    } else if (right > max_x) {
        x -= right - max_x;
    }
    return x;
}

static void draw_text(cairo_t *cr, double x, double y, const char *text, int anchor)
{
    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);
    if (anchor == 1) {
        x -= extents.width / 2.0;
    } else if (anchor == 2) {
        x -= extents.width;
    }
    cairo_move_to(cr, x - extents.x_bearing, y);
    cairo_show_text(cr, text);
}

static double channel_value(const TemperatureMeasurement *s, int channel)
{
    return channel == 0 ? s->t1 : channel == 1 ? s->t2 : s->t1 - s->t2;
}

static bool channel_drawn(const TemperatureMeasurement *sample, int channel, bool hide_suspicious)
{
    double value = channel_value(sample, channel);
    if (!isfinite(value)) {
        return false;
    }
    if (hide_suspicious && sample->suspicious) {
        return false;
    }
    return true;
}

static void lift_pen(cairo_t *cr, bool *pen_down)
{
    if (*pen_down) {
        cairo_stroke(cr);
        *pen_down = false;
    }
}

static void add_point(cairo_t *cr, bool *pen_down, double x, double y)
{
    if (!*pen_down) {
        cairo_move_to(cr, x, y);
        *pen_down = true;
    } else {
        cairo_line_to(cr, x, y);
    }
}

static void draw_channel(cairo_t *cr, const HistoryBuffer *history, size_t begin, size_t end,
                         int channel, bool hide_suspicious, double t0, double t1,
                         double y0, double y1, double plot_x, double plot_y,
                         double plot_w, double plot_h)
{
    size_t visible = (end > begin) ? (end - begin) : 0u;
    size_t stride = (visible > 4000u) ? (visible / 4000u) : 1u;
    bool pen_down = false;
    size_t cursor = begin;

    if (!(t1 > t0) || !(y1 > y0) || !(plot_w > 0.0) || !(plot_h > 0.0)) {
        return;
    }

    cairo_save(cr);
    cairo_rectangle(cr, plot_x, plot_y, plot_w, plot_h);
    cairo_clip(cr);
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_new_path(cr);

    /*
     * Long sessions are drawn from the history in place. Each stride bucket
     * keeps its min and max, so peaks are not dropped, and nothing is copied.
     */
    while (cursor < end) {
        size_t bucket_end = cursor + (stride > 1u ? stride : 1u);
        size_t min_index = (size_t)-1;
        size_t max_index = (size_t)-1;
        double min_value = 0.0;
        double max_value = 0.0;
        size_t index;

        if (bucket_end > end) {
            bucket_end = end;
        }
        if (stride > 1u) {
            for (index = cursor; index < bucket_end; index++) {
                const TemperatureMeasurement *sample = history_buffer_at(history, index);
                double value;
                if (sample == NULL || !channel_drawn(sample, channel, hide_suspicious)) {
                    continue;
                }
                value = channel_value(sample, channel);
                if (min_index == (size_t)-1 || value < min_value) {
                    min_value = value;
                    min_index = index;
                }
                if (max_index == (size_t)-1 || value > max_value) {
                    max_value = value;
                    max_index = index;
                }
            }
        }
        for (index = cursor; index < bucket_end; index++) {
            const TemperatureMeasurement *sample = history_buffer_at(history, index);
            double x;
            double y;
            int emit;

            if (sample == NULL || !channel_drawn(sample, channel, hide_suspicious)) {
                lift_pen(cr, &pen_down);
                continue;
            }
            emit = stride <= 1u || index == begin || index + 1u == end ||
                   sample->suspicious || index == min_index || index == max_index;
            if (!emit) {
                continue;
            }
            x = plot_x + (sample->elapsed_seconds - t0) / (t1 - t0) * plot_w;
            y = plot_y + (y1 - (channel_value(sample, channel))) / (y1 - y0) * plot_h;
            add_point(cr, &pen_down, x, y);
        }
        cursor = bucket_end;
    }
    lift_pen(cr, &pen_down);
    cairo_restore(cr);
}

static void draw_spike_markers(cairo_t *cr, const LivePlot *plot, const HistoryBuffer *history, size_t begin,
                               size_t end, bool hide_suspicious, const double mark[3],
                               double t0, double t1, double y0, double y1,
                               double plot_x, double plot_y, double plot_w, double plot_h)
{
    size_t index;
    if (hide_suspicious) {
        return;
    }
    cairo_save(cr);
    cairo_rectangle(cr, plot_x, plot_y, plot_w, plot_h);
    cairo_clip(cr);
    /* One small ring per suspicious sample. Ordinary points stay unmarked. */
    paint(cr, mark);
    cairo_set_line_width(cr, 1.25);
    if (!(t1 > t0) || !(y1 > y0)) {
        cairo_restore(cr);
        return;
    }
    for (index = begin; index < end; index++) {
        const TemperatureMeasurement *sample = history_buffer_at(history, index);
        int channel;
        if (sample == NULL || !sample->suspicious) {
            continue;
        }
        for (channel = 0; channel < 2; channel++) {
            if ((channel == 0 && plot->hide_t1) || (channel == 1 && plot->hide_t2)) continue;
            double value = channel_value(sample, channel);
            double x;
            double y;
            if (!isfinite(value)) {
                continue;
            }
            x = plot_x + (sample->elapsed_seconds - t0) / (t1 - t0) * plot_w;
            y = plot_y + (y1 - value) / (y1 - y0) * plot_h;
            cairo_arc(cr, x, y, 3.0, 0.0, 2.0 * PLOT_PI);
            cairo_stroke(cr);
        }
    }
    cairo_restore(cr);
}

void live_plot_draw(LivePlot *plot, cairo_t *cr, int width, int height)
{
    const PlotPalette *palette = palette_of(plot);
    const double left = 76.0;
    const double right = 16.0;
    const double top = 14.0;
    const double bottom = 48.0;
    const HistoryBuffer *history;
    size_t count;
    size_t begin = 0;
    size_t end = 0;
    double t0 = 0.0;
    double t1 = 60.0;
    double y0 = 20.0;
    double y1 = 30.0;
    double plot_x;
    double plot_y;
    double plot_w;
    double plot_h;
    bool have_y = false;
    bool minutes;
    double x_span;
    double x_step;
    double y_step;
    double tick;

    paint(cr, palette->bg);
    cairo_paint(cr);
    if (plot == NULL || width < 120 || height < 120) {
        return;
    }

    plot_x = left;
    plot_y = top;
    plot_w = (double)width - left - right;
    plot_h = (double)height - top - bottom;
    if (plot_w < 40.0 || plot_h < 40.0) {
        return;
    }

    history = plot->history;
    count = (history != NULL) ? history_buffer_count(history) : 0u;
    if (count > 0u) {
        double last = history_buffer_at(history, count - 1u)->elapsed_seconds;
        double first = history_buffer_at(history, 0)->elapsed_seconds;
        live_plot_visible_time(first, last, plot->window_seconds, &t0, &t1);
        begin = history_buffer_lower_bound(history, t0);
        if (begin > 0u) {
            begin--;
        }
        end = history_buffer_lower_bound(history, t1);
        if (end < count) {
            const TemperatureMeasurement *edge = history_buffer_at(history, end);
            if (edge != NULL && edge->elapsed_seconds <= t1 + 1e-9) {
                end++;
            }
        }
    } else {
        live_plot_visible_time(0.0, 0.0, plot->window_seconds, &t0, &t1);
    }

    if (history != NULL && plot->manual_time && plot->time_end > plot->time_start) {
        t0 = plot->time_start; t1 = plot->time_end;
        begin = history_buffer_lower_bound(history, t0);
        if (begin > 0) begin--;
        end = history_buffer_lower_bound(history, t1);
        if (end < count) end++;
    }
    if (history != NULL) {
        size_t index;
        for (index = begin; index < end; index++) {
            const TemperatureMeasurement *sample = history_buffer_at(history, index);
            if (sample == NULL) {
                break;
            }
            if (sample->elapsed_seconds < t0 || sample->elapsed_seconds > t1 + 1e-9) {
                continue;
            }
            for (int channel = 0; channel < 3; channel++) {
                if ((channel == 0 && plot->hide_t1) || (channel == 1 && plot->hide_t2) ||
                    (channel == 2 && !plot->show_delta)) continue;
                if (!channel_drawn(sample, channel, plot->hide_suspicious)) continue;
                double v = channel_value(sample, channel);
                y0 = have_y ? fmin(y0, v) : v;
                y1 = have_y ? fmax(y1, v) : v;
                have_y = true;
            }
        }
    }
    if (!have_y) {
        y0 = 20.0;
        y1 = 30.0;
    }
    double data_lo = y0;
    double data_hi = y1;
    if (y1 - y0 < 1.0) {
        double mid = 0.5 * (y0 + y1);
        y0 = mid - 0.5;
        y1 = mid + 0.5;
    }
    {
        double pad = 0.10 * (y1 - y0);
        if (pad < 0.4) {
            pad = 0.4;
        }
        y0 -= pad;
        y1 += pad;
    }
    {
        /* Snap to the tick grid so a 0.01 °C change does not slide the axis. */
        double step = nice_step(y1 - y0, 5);
        if (step > 0.0 && isfinite(step)) {
            y0 = floor(y0 / step) * step;
            y1 = ceil(y1 / step) * step;
            if (!(y1 > y0)) {
                y1 = y0 + step;
            }
        }
    }
    if (have_y && plot->have_span && plot->span_window == plot->window_seconds &&
        count >= plot->span_count && plot->span_y1 > plot->span_y0) {
        double held_span = plot->span_y1 - plot->span_y0;
        int outside = data_lo < plot->span_y0 || data_hi > plot->span_y1;
        int shrink = (y1 - y0) < held_span * 0.6;
        if (!outside && !shrink) {
            y0 = plot->span_y0;
            y1 = plot->span_y1;
        }
    }
    if (have_y) {
        plot->have_span = true;
        plot->span_y0 = y0;
        plot->span_y1 = y1;
        plot->span_window = plot->window_seconds;
        plot->span_count = count;
    } else {
        plot->have_span = false;
    }

    if (plot->manual_y && plot->y_max > plot->y_min) {
        y0 = plot->y_min; y1 = plot->y_max;
    }
    plot->drawn_t0 = t0; plot->drawn_t1 = t1;
    plot->drawn_y0 = y0; plot->drawn_y1 = y1;
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);

    minutes = (t1 - t0) >= 120.0;
    x_span = minutes ? (t1 - t0) / 60.0 : (t1 - t0);
    x_step = nice_step(x_span, 6);
    y_step = nice_step(y1 - y0, 5);

    cairo_set_source_rgba(cr, palette->grid[0], palette->grid[1], palette->grid[2], 0.45);
    cairo_set_line_width(cr, 1.0);
    {
        int guard = 0;
        for (tick = ceil((minutes ? t0 / 60.0 : t0) / (x_step / 2.0)) * (x_step / 2.0);
             x_step > 0.0 && tick <= (minutes ? t1 / 60.0 : t1) + x_step * 0.001 && guard < 48;
             tick += x_step / 2.0, guard++) {
        double major = tick / x_step;
        double elapsed;
        double x;
        if (!isfinite(tick) || fabs(major - round(major)) < 1e-3) {
            continue;
        }
        elapsed = minutes ? tick * 60.0 : tick;
        x = plot_x + (elapsed - t0) / (t1 - t0) * plot_w;
        if (x < plot_x - 0.5 || x > plot_x + plot_w + 0.5) {
            continue;
        }
        cairo_move_to(cr, x, plot_y);
        cairo_line_to(cr, x, plot_y + plot_h);
        }
    }
    {
        int guard = 0;
        for (tick = ceil(y0 / (y_step / 2.0)) * (y_step / 2.0);
             y_step > 0.0 && tick <= y1 + y_step * 0.001 && guard < 48;
             tick += y_step / 2.0, guard++) {
        double major = tick / y_step;
        double y;
        if (!isfinite(tick) || fabs(major - round(major)) < 1e-3) {
            continue;
        }
        y = plot_y + (y1 - tick) / (y1 - y0) * plot_h;
        if (y < plot_y - 0.5 || y > plot_y + plot_h + 0.5) {
            continue;
        }
        cairo_move_to(cr, plot_x, y);
        cairo_line_to(cr, plot_x + plot_w, y);
        }
    }
    cairo_stroke(cr);

    paint(cr, palette->grid);
    cairo_set_line_width(cr, 1.0);
    {
        int guard = 0;
        for (tick = ceil((minutes ? t0 / 60.0 : t0) / x_step) * x_step;
             x_step > 0.0 && tick <= (minutes ? t1 / 60.0 : t1) + x_step * 0.001 && guard < 48;
             tick += x_step, guard++) {
        double elapsed = minutes ? tick * 60.0 : tick;
        double x = plot_x + (elapsed - t0) / (t1 - t0) * plot_w;
        if (x < plot_x - 0.5 || x > plot_x + plot_w + 0.5) {
            continue;
        }
        cairo_move_to(cr, x, plot_y);
        cairo_line_to(cr, x, plot_y + plot_h);
        }
    }
    {
        int guard = 0;
        for (tick = ceil(y0 / y_step) * y_step;
             y_step > 0.0 && tick <= y1 + y_step * 0.001 && guard < 48;
             tick += y_step, guard++) {
        double y = plot_y + (y1 - tick) / (y1 - y0) * plot_h;
        if (!isfinite(tick) || y < plot_y - 0.5 || y > plot_y + plot_h + 0.5) {
            continue;
        }
        cairo_move_to(cr, plot_x, y);
        cairo_line_to(cr, plot_x + plot_w, y);
        }
    }
    cairo_stroke(cr);

    paint(cr, palette->grid);
    cairo_rectangle(cr, plot_x, plot_y, plot_w, plot_h);
    cairo_stroke(cr);

    paint(cr, palette->axis);
    {
        int guard = 0;
        for (tick = ceil((minutes ? t0 / 60.0 : t0) / x_step) * x_step;
             x_step > 0.0 && tick <= (minutes ? t1 / 60.0 : t1) + x_step * 0.001 && guard < 48;
             tick += x_step, guard++) {
        double elapsed = minutes ? tick * 60.0 : tick;
        double x = plot_x + (elapsed - t0) / (t1 - t0) * plot_w;
        char label[32];
        if (!isfinite(tick) || x < plot_x - 0.5 || x > plot_x + plot_w + 0.5) {
            continue;
        }
        format_tick(label, sizeof label, tick, x_step);
        x = place_label(cr, x, label, 1, 4.0, (double)width - 4.0);
        draw_text(cr, x, plot_y + plot_h + 18.0, label, 1);
        }
    }
    {
        int guard = 0;
        for (tick = ceil(y0 / y_step) * y_step;
             y_step > 0.0 && tick <= y1 + y_step * 0.001 && guard < 48;
             tick += y_step, guard++) {
        double y = plot_y + (y1 - tick) / (y1 - y0) * plot_h;
        double x;
        char label[32];
        if (!isfinite(tick) || y < plot_y - 0.5 || y > plot_y + plot_h + 0.5) {
            continue;
        }
        format_tick(label, sizeof label, tick, y_step);
        x = place_label(cr, plot_x - 8.0, label, 2, 2.0, plot_x - 4.0);
        draw_text(cr, x, y + 4.0, label, 2);
        }
    }

    {
        const char *axis_name = minutes ? "Elapsed time (min)" : "Elapsed time (s)";
        double axis_x = place_label(cr, plot_x + plot_w / 2.0, axis_name, 1, 4.0,
                                    (double)width - 4.0);
        draw_text(cr, axis_x, plot_y + plot_h + 34.0, axis_name, 1);
    }

    cairo_save(cr);
    cairo_translate(cr, 18.0, plot_y + plot_h / 2.0);
    cairo_rotate(cr, -PLOT_PI / 2.0);
    draw_text(cr, 0.0, 0.0, "Temperature (°C)", 1);
    cairo_restore(cr);

    if (history != NULL && end > begin) {
        paint(cr, palette->t1);
        if (!plot->hide_t1) draw_channel(cr, history, begin, end, 0, plot->hide_suspicious,
                     t0, t1, y0, y1, plot_x, plot_y, plot_w, plot_h);
        paint(cr, palette->t2);
        if (!plot->hide_t2) draw_channel(cr, history, begin, end, 1, plot->hide_suspicious,
                     t0, t1, y0, y1, plot_x, plot_y, plot_w, plot_h);
        if (plot->show_delta) {
            paint(cr, palette->mark);
            draw_channel(cr, history, begin, end, 2, plot->hide_suspicious,
                         t0, t1, y0, y1, plot_x, plot_y, plot_w, plot_h);
        }
        draw_spike_markers(cr, plot, history, begin, end, plot->hide_suspicious, palette->mark,
                           t0, t1, y0, y1, plot_x, plot_y, plot_w, plot_h);
    }

    /* Legend follows channel visibility, including the derived trace. */
    int legend_row = 0;
    for (int channel = 0; channel < 3; channel++) {
        if ((channel == 0 && plot->hide_t1) || (channel == 1 && plot->hide_t2) ||
            (channel == 2 && !plot->show_delta)) continue;
        double row_y = plot_y + 20.0 + legend_row++ * 20.0;
        paint(cr, palette->bg);
        cairo_rectangle(cr, plot_x + plot_w - 94, row_y - 10, 88, 20);
        cairo_fill(cr);
        paint(cr, channel == 0 ? palette->t1 : channel == 1 ? palette->t2 : palette->mark);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, plot_x + plot_w - 84, row_y);
        cairo_line_to(cr, plot_x + plot_w - 64, row_y); cairo_stroke(cr);
        paint(cr, palette->axis);
        draw_text(cr, plot_x + plot_w - 56, row_y + 4,
                  channel == 0 ? "T1" : channel == 1 ? "T2" : "ΔT", 0);
    }

    if (plot->overlay != NULL && plot->overlay[0] != '\0' && count == 0u) {
        paint(cr, palette->axis);
        draw_text(cr, plot_x + plot_w / 2.0, plot_y + plot_h / 2.0, plot->overlay, 1);
    }
}
