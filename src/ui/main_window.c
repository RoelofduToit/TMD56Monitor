#include "ui/main_window.h"

#include "sources/tmd56_source.h"
#include "ui/live_plot.h"
#include "ui/toolbar_mode.h"
#include "version.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <gtk/gtk.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const DEVICE_ITEMS[] = {
    "TMD-56",
    "Replay file",
    NULL
};

static const char *const SCENARIO_ITEMS[] = {
    "Stable",
    "Heating",
    "Cooling",
    "Thermal gradient",
    "Noise",
    "Spike test",
    NULL
};

static const char *const PROFILE_IDS[] = {
    "stable",
    "heating",
    "cooling",
    "gradient",
    "noise",
    "spike"
};

static const char *const SPEED_ITEMS[] = {
    "0.5×",
    "1×",
    "2×",
    "5×",
    "10×",
    NULL
};

static const char *const VIEW_LABELS[] = {
    "1 min",
    "5 min",
    "10 min",
    "30 min",
    "60 min",
    "All"
};

static const double SPEED_VALUES[] = {0.5, 1.0, 2.0, 5.0, 10.0};
static const double WINDOW_VALUES[] = {60.0, 300.0, 600.0, 1800.0, 3600.0, 0.0};

typedef struct {
    App *app;
    GtkWidget *window;
    GtkWidget *device_box;
    GtkWidget *source_dropdown;
    GtkWidget *sim_badge;
    GtkWidget *scenario_box;
    GtkWidget *scenario_dropdown;
    GtkWidget *interval_box;
    GtkWidget *interval_dropdown;
    GtkWidget *action_row;
    GtkWidget *file_button;
    GtkWidget *file_box;
    GtkWidget *file_label;
    GtkWidget *speed_box;
    GtkWidget *speed_dropdown;
    GtkWidget *port_box;
    GtkWidget *port_dropdown;
    char port_name[256];
    char port_signature[4096];
    int port_scan_ticks;
    bool ports_scanned;
    GSimpleAction *simulation_action;
    GSimpleAction *profile_action;
    TmdUiDevice device_choice;
    GtkWidget *start_button;
    GtkWidget *stop_button;
    GtkWidget *play_button;
    GtkWidget *pause_button;
    GtkWidget *restart_button;
    GtkWidget *session_box;
    GtkWidget *session_hint;
    GtkWidget *about_button;
    GtkWidget *t1_value;
    GtkWidget *t2_value;
    GtkWidget *dt_value;
    GtkWidget *spike_mark;
    GtkWidget *t1_stats;
    GtkWidget *t2_stats;
    GtkWidget *samples_value;
    GtkWidget *elapsed_value;
    GtkWidget *view_buttons[6];
    GtkWidget *drawing;
    GtkWidget *session_entry;
    GtkWidget *log_button;
    GtkWidget *state_label;
    GtkWidget *spike_box;
    GtkWidget *spike_check;
    GtkWidget *spike_spin;
    GtkWidget *export_button, *log_path, *connection, *cursor;
    GtkWidget *channel_checks[3], *auto_y, *y_low, *y_high;
    GtkWidget *refresh_button;
    GtkWidget *notes, *reading_state, *review_button;
    double drag_start;
    AppSourceKind history_source;
    bool allow_reset, exported, allow_close;
    GtkWidget *recording_state;
    gint64 recording_started;
    double recording_duration;
    bool was_logging;
    bool cursor_active;
    double cursor_x;
    guint cursor_idle_id;
    LivePlot plot;
    guint timer_id;
    bool syncing;
    bool session_hint_on;
    char session_message[384];
} Ui;

static void on_motion(GtkEventControllerMotion *controller, double x, double y, gpointer data);
static gboolean on_cursor_idle(gpointer data);

static void reset_plot_cursor(Ui *ui)
{
    ui->cursor_active = false;
    gtk_label_set_text(GTK_LABEL(ui->cursor), "Scroll to zoom · drag to pan · hover to inspect samples");
}

static const char *interval_items[8];

static void ensure_interval_items(void)
{
    size_t index;
    size_t count = tmd_interval_count();
    if (interval_items[0] != NULL) {
        return;
    }
    if (count > 7u) {
        count = 7u;
    }
    for (index = 0; index < count; index++) {
        interval_items[index] = tmd_interval_label(index);
    }
    interval_items[count] = NULL;
}

static GtkWidget *make_dropdown(const char *const *items, guint selected)
{
    GtkWidget *dropdown = gtk_drop_down_new_from_strings(items);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), selected);
    return dropdown;
}

static GtkWidget *inline_field(const char *caption, GtkWidget *control)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *label = gtk_label_new(caption);
    gtk_widget_add_css_class(label, "field-label");
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), control);
    return box;
}

static GtkWidget *control_button(const char *text, const char *width_class)
{
    GtkWidget *button = gtk_button_new_with_label(text);
    gtk_widget_add_css_class(button, "secondary-control");
    if (width_class != NULL) {
        gtk_widget_add_css_class(button, width_class);
    }
    return button;
}

static void set_emphasis(GtkWidget *button, gboolean primary)
{
    gboolean is_primary = gtk_widget_has_css_class(button, "primary-control");
    if (is_primary == primary) {
        return;
    }
    gtk_widget_remove_css_class(button, "primary-control");
    gtk_widget_remove_css_class(button, "secondary-control");
    gtk_widget_add_css_class(button, primary ? "primary-control" : "secondary-control");
}

static void set_button_label(GtkWidget *button, const char *text)
{
    const char *current = gtk_button_get_label(GTK_BUTTON(button));
    if (current != NULL && text != NULL && strcmp(current, text) == 0) {
        return;
    }
    gtk_button_set_label(GTK_BUTTON(button), text);
}

static GtkWidget *fact_column(const char *caption, GtkWidget *value)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *label = gtk_label_new(caption);
    gtk_widget_add_css_class(label, "fact-label");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_halign(value, GTK_ALIGN_START);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), value);
    return box;
}

static GtkWidget *make_channel(const char *name, const char *channel_class,
                               GtkWidget **number_out, GtkWidget **stats_out)
{
    GtkWidget *channel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *title = gtk_label_new(name);
    GtkWidget *reading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *number = gtk_label_new("—");
    GtkWidget *unit = gtk_label_new("°C");
    GtkWidget *statistics = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *stats = gtk_label_new("MIN —    MAX —    AVG —");

    gtk_widget_add_css_class(channel, "measurement-channel");
    gtk_widget_add_css_class(channel, channel_class);
    gtk_widget_set_hexpand(channel, TRUE);
    gtk_widget_set_size_request(channel, -1, 96);
    gtk_widget_add_css_class(title, "measurement-name");
    gtk_widget_add_css_class(number, "measurement-number");
    gtk_widget_add_css_class(unit, "measurement-unit");
    gtk_widget_add_css_class(statistics, "channel-statistics");
    gtk_widget_add_css_class(stats, "stat-value");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_halign(number, GTK_ALIGN_START);
    gtk_widget_set_valign(unit, GTK_ALIGN_CENTER);
    gtk_label_set_selectable(GTK_LABEL(number), TRUE);
    gtk_label_set_width_chars(GTK_LABEL(number), 6);
    gtk_label_set_xalign(GTK_LABEL(number), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(stats), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(stats), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(stats, TRUE);
    gtk_box_append(GTK_BOX(reading), number);
    gtk_box_append(GTK_BOX(reading), unit);
    gtk_box_append(GTK_BOX(statistics), stats);
    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *indicator = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(indicator, "channel-indicator");
    gtk_widget_set_size_request(indicator, 3, 12);
    gtk_widget_set_valign(indicator, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(title_row), indicator);
    gtk_box_append(GTK_BOX(title_row), title);
    gtk_box_append(GTK_BOX(channel), title_row);
    gtk_box_append(GTK_BOX(channel), reading);
    gtk_box_append(GTK_BOX(channel), statistics);
    *number_out = number;
    *stats_out = stats;
    return channel;
}

static void format_grouped(char *buf, size_t len, unsigned long value)
{
    char digits[32];
    char out[48];
    int count;
    int index;
    int written = 0;

    snprintf(digits, sizeof digits, "%lu", value);
    count = (int)strlen(digits);
    for (index = 0; index < count && written + 1 < (int)sizeof out; index++) {
        int remaining = count - index;
        if (index > 0 && remaining % 3 == 0) {
            out[written++] = ',';
        }
        out[written++] = digits[index];
    }
    out[written] = '\0';
    snprintf(buf, len, "%s", out);
}

static void format_clock(char *buf, size_t len, double seconds)
{
    int total;
    int hours;
    int minutes;
    int secs;

    if (!(seconds > 0.0)) {
        snprintf(buf, len, "00:00:00");
        return;
    }
    total = (int)seconds;
    hours = total / 3600;
    minutes = (total % 3600) / 60;
    secs = total % 60;
    snprintf(buf, len, "%02d:%02d:%02d", hours, minutes, secs);
}

static void format_tenth(char *buf, size_t len, double value)
{
    double shown = measurement_display_tenth(value);
    if (!isfinite(shown)) {
        snprintf(buf, len, "     —");
        return;
    }
    snprintf(buf, len, "%6.1f", shown);
}

/* The shared formatter pads with spaces for a fixed column. The face is left aligned. */
static const char *display_reading(const char *formatted)
{
    const char *text = formatted + strspn(formatted, " ");
    if (strcmp(text, "----") == 0) {
        return "——";
    }
    return text;
}

static void format_channel_stats(char *buf, size_t len, bool have, double min_v, double max_v,
                                 double sum, unsigned long count)
{
    char min_text[16];
    char max_text[16];
    char avg_text[16];

    if (!have || count == 0ul) {
        snprintf(buf, len, "MIN     —    MAX     —    AVG     —");
        return;
    }
    format_tenth(min_text, sizeof min_text, min_v);
    format_tenth(max_text, sizeof max_text, max_v);
    format_tenth(avg_text, sizeof avg_text, sum / (double)count);
    snprintf(buf, len, "MIN %s    MAX %s    AVG %s", min_text, max_text, avg_text);
}

static bool session_name_usable(const char *text)
{
    const char *cursor;
    if (text == NULL) {
        return false;
    }
    for (cursor = text; *cursor != '\0'; cursor++) {
        if (isalnum((unsigned char)*cursor)) {
            return true;
        }
    }
    return false;
}

static void set_status_class(GtkWidget *label, const char *klass)
{
    gtk_widget_remove_css_class(label, "status-connected");
    gtk_widget_remove_css_class(label, "status-recording");
    gtk_widget_remove_css_class(label, "status-warning");
    gtk_widget_remove_css_class(label, "status-offline");
    gtk_widget_add_css_class(label, klass);
}

static void set_label(GtkWidget *label, const char *text);

static void update_file_label(Ui *ui)
{
    const char *path = app_replay_path(ui->app);
    if (path == NULL || path[0] == '\0') {
        set_label(ui->file_label, "No replay file loaded");
        gtk_widget_set_tooltip_text(ui->file_label, NULL);
        return;
    }
    char *base = g_path_get_basename(path);
    set_label(ui->file_label, base);
    gtk_widget_set_tooltip_text(ui->file_label, path);
    g_free(base);
}

static const char *overlay_for(const Ui *ui)
{
    if (history_buffer_count(app_history(ui->app)) > 0u) {
        return NULL;
    }
    if (app_source(ui->app) == APP_SOURCE_REPLAY) {
        if (app_replay_path(ui->app) == NULL || app_replay_path(ui->app)[0] == '\0') {
            return "Open a replay file";
        }
        return "Play the replay file";
    }
    if (!app_is_acquiring(ui->app)) {
        return "Start acquisition";
    }
    return "Waiting for samples";
}

static void load_plot_color(GtkWidget *widget, const char *name, double dest[3],
                            double red, double green, double blue)
{
    GdkRGBA rgba;
    gboolean found = FALSE;

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    found = gtk_style_context_lookup_color(gtk_widget_get_style_context(widget), name, &rgba);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (!found) {
        dest[0] = red;
        dest[1] = green;
        dest[2] = blue;
        return;
    }
    dest[0] = rgba.red;
    dest[1] = rgba.green;
    dest[2] = rgba.blue;
}

static void set_label(GtkWidget *label, const char *text)
{
    const char *current = gtk_label_get_text(GTK_LABEL(label));
    if (current != NULL && text != NULL && strcmp(current, text) == 0) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(label), text);
}

static const char *selected_text(GtkWidget *dropdown, const char *const *items, guint limit)
{
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (selected == GTK_INVALID_LIST_POSITION || selected >= limit || items[selected] == NULL) {
        return "—";
    }
    return items[selected];
}

static void field_visible(GtkWidget *widget, gboolean visible)
{
    gtk_widget_set_visible(widget, visible);
    GtkWidget *parent = gtk_widget_get_parent(widget);
    if (GTK_IS_FLOW_BOX_CHILD(parent)) gtk_widget_set_visible(parent, visible);
}

static void refresh_ui(Ui *ui)
{
    const MeasurementStats *stats = app_stats(ui->app);
    const TemperatureMeasurement *latest = app_latest(ui->app);
    bool running = app_is_acquiring(ui->app);
    bool logging = app_is_logging(ui->app);
    bool simulator = app_source(ui->app) == APP_SOURCE_SIMULATOR;
    bool replay = app_source(ui->app) == APP_SOURCE_REPLAY;
    bool tmd = app_source(ui->app) == APP_SOURCE_TMD56;
    bool spike = latest != NULL && latest->suspicious;
    char text[192];
    char grouped[48];
    const char *session;
    const char *replay_path;
    bool replay_loaded;
    bool replay_paused;
    AppStatusLevel level = app_status_level(ui->app);
    size_t samples = history_buffer_count(app_history(ui->app));

    {
        char t1_text[16];
        char t2_text[16];
        char delta_text[16];
        measurement_format_reading(t1_text, sizeof t1_text, latest != NULL ? latest->t1 : nan(""));
        measurement_format_reading(t2_text, sizeof t2_text, latest != NULL ? latest->t2 : nan(""));
        if (latest != NULL && isfinite(latest->t1) && isfinite(latest->t2)) {
            double shown_t1 = measurement_display_tenth(latest->t1);
            double shown_t2 = measurement_display_tenth(latest->t2);
            measurement_format_reading(delta_text, sizeof delta_text, shown_t1 - shown_t2);
        } else {
            measurement_format_reading(delta_text, sizeof delta_text, nan(""));
        }
        set_label(ui->t1_value, display_reading(t1_text));
        set_label(ui->t2_value, display_reading(t2_text));
        set_label(ui->dt_value, display_reading(delta_text));
    }
    gtk_widget_set_visible(ui->spike_mark, spike);

    format_channel_stats(text, sizeof text, stats->have_t1,
                         measurement_display_tenth(stats->t1_min),
                         measurement_display_tenth(stats->t1_max),
                         measurement_display_tenth(stats->have_t1 && stats->t1_count > 0
                                                       ? stats->t1_sum / (double)stats->t1_count
                                                       : 0.0),
                         stats->have_t1 ? 1ul : 0ul);
    set_label(ui->t1_stats, text);
    gtk_widget_set_tooltip_text(ui->t1_stats, text);
    format_channel_stats(text, sizeof text, stats->have_t2,
                         measurement_display_tenth(stats->t2_min),
                         measurement_display_tenth(stats->t2_max),
                         measurement_display_tenth(stats->have_t2 && stats->t2_count > 0
                                                       ? stats->t2_sum / (double)stats->t2_count
                                                       : 0.0),
                         stats->have_t2 ? 1ul : 0ul);
    set_label(ui->t2_stats, text);
    gtk_widget_set_tooltip_text(ui->t2_stats, text);

    format_grouped(grouped, sizeof grouped, (unsigned long)samples);
    set_label(ui->samples_value, grouped);
    format_clock(text, sizeof text, latest != NULL ? latest->elapsed_seconds : 0.0);
    set_label(ui->elapsed_value, text);

    replay_path = app_replay_path(ui->app);
    replay_loaded = replay_path != NULL && replay_path[0] != '\0';
    replay_paused = app_replay_paused(ui->app);
    {
        const char *state_class = "status-offline";
        char line[512];
        const char *speed_text = selected_text(ui->speed_dropdown, SPEED_ITEMS, 5);
        const char *sample_text = selected_text(ui->interval_dropdown, interval_items,
                                                (guint)tmd_interval_count());
        const char *device_name = simulator ? "SIMULATOR" : tmd ? "TMD-56" : "REPLAY FILE";

        if ((level == APP_LEVEL_ERROR || level == APP_LEVEL_WARNING) &&
            app_status(ui->app)[0] != '\0') {
            snprintf(line, sizeof line, "● %s    %s",
                     logging ? "LOGGING" : running ? "ACQUIRING" : "IDLE",
                     app_status(ui->app));
            state_class = logging ? "status-recording" : "status-warning";
        } else if (replay) {
            if (!replay_loaded) {
                snprintf(line, sizeof line, "● IDLE    DEVICE REPLAY FILE");
            } else {
                char *base = g_path_get_basename(replay_path);
                if (!running && app_replay_finished(ui->app)) {
                    snprintf(line, sizeof line, "● COMPLETE    %s    %s SAMPLES", base, grouped);
                    state_class = "status-offline";
                } else if (running && replay_paused) {
                    snprintf(line, sizeof line, "● PAUSED    %s    %s SAMPLES    %s",
                             base, grouped, speed_text);
                    state_class = "status-warning";
                } else if (running) {
                    snprintf(line, sizeof line, "● REPLAYING    %s    %s SAMPLES    %s",
                             base, grouped, speed_text);
                    state_class = "status-connected";
                } else {
                    snprintf(line, sizeof line, "● IDLE    %s    %s SAMPLES", base, grouped);
                }
                g_free(base);
            }
        } else if (logging) {
            const char *log_path = app_log_path(ui->app);
            char *base = (log_path != NULL && log_path[0] != '\0')
                             ? g_path_get_basename(log_path) : NULL;
            if (simulator) {
                snprintf(line, sizeof line,
                         "● LOGGING    SIMULATION ACTIVE    %s SAMPLES    %s    %s",
                         grouped, sample_text, base != NULL ? base : "");
            } else {
                snprintf(line, sizeof line, "● LOGGING    DEVICE %s    %s SAMPLES    %s    %s",
                         device_name, grouped, sample_text, base != NULL ? base : "");
            }
            g_free(base);
            state_class = "status-recording";
        } else if (running) {
            if (simulator) {
                snprintf(line, sizeof line, "● ACQUIRING    SIMULATION ACTIVE    %s SAMPLES    %s",
                         grouped, sample_text);
            } else {
                snprintf(line, sizeof line, "● ACQUIRING    DEVICE %s    %s SAMPLES    %s",
                         device_name, grouped, sample_text);
            }
            state_class = "status-connected";
        } else if (simulator) {
            snprintf(line, sizeof line, "● IDLE    SIMULATION ACTIVE");
            state_class = "status-warning";
        } else {
            snprintf(line, sizeof line, "● IDLE    DEVICE %s", device_name);
        }
        set_label(ui->state_label, line);
        set_status_class(ui->state_label, state_class);
    }

    {
        TmdToolbar bar;
        tmd_toolbar_for(simulator, ui->device_choice, &bar);
        field_visible(ui->device_box, bar.show_device);
        field_visible(ui->sim_badge, bar.show_badge);
        field_visible(ui->scenario_box, bar.show_profile);
        field_visible(ui->interval_box, bar.show_interval);
        field_visible(ui->port_box, bar.show_port);
        field_visible(ui->file_button, bar.show_replay);
        field_visible(ui->file_box, bar.show_replay);
        field_visible(ui->speed_box, bar.show_replay);
        field_visible(ui->play_button, bar.show_replay);
        field_visible(ui->pause_button, bar.show_replay);
        field_visible(ui->restart_button, bar.show_replay);
        field_visible(ui->start_button, bar.show_acquire);
        field_visible(ui->stop_button, bar.show_acquire);
        field_visible(ui->session_box, bar.show_logging);
        field_visible(ui->log_button, bar.show_logging);
        if (ui->simulation_action != NULL) {
            g_simple_action_set_enabled(ui->simulation_action, !running);
        }
        if (ui->profile_action != NULL) {
            g_simple_action_set_enabled(ui->profile_action, simulator && !running);
        }
    }

    gtk_widget_set_sensitive(ui->start_button, (tmd || simulator) && !running);
    set_emphasis(ui->start_button, (tmd || simulator) && !running);
    gtk_widget_set_sensitive(ui->stop_button, (tmd || simulator) && running);
    set_emphasis(ui->stop_button, FALSE);
    gtk_widget_set_sensitive(ui->source_dropdown, !running);
    gtk_widget_set_sensitive(ui->port_dropdown, tmd && !running);
    gtk_widget_set_sensitive(ui->spike_check, TRUE);
    gtk_widget_set_sensitive(ui->spike_spin, gtk_check_button_get_active(GTK_CHECK_BUTTON(ui->spike_check)));
    gtk_widget_set_sensitive(ui->file_button, replay && !running);
    set_emphasis(ui->file_button, replay && !running && !replay_loaded);
    {
        gboolean play_ready = replay && replay_loaded && (!running || replay_paused);
        gboolean pause_ready = replay && running && !replay_paused;
        gtk_widget_set_sensitive(ui->play_button, play_ready);
        set_emphasis(ui->play_button, play_ready);
        gtk_widget_set_sensitive(ui->pause_button, pause_ready);
        set_emphasis(ui->pause_button, pause_ready);
    }
    gtk_widget_set_sensitive(ui->restart_button,
                             replay && replay_loaded &&
                                 (running || app_replay_finished(ui->app)));
    set_emphasis(ui->restart_button, FALSE);

    session = gtk_editable_get_text(GTK_EDITABLE(ui->session_entry));
    if (session_name_usable(session) || replay) {
        ui->session_hint_on = false;
    }
    gtk_widget_set_sensitive(ui->session_entry, (tmd || simulator) && !logging);
    if (logging) {
        set_button_label(ui->log_button, "Stop logging");
        gtk_widget_set_sensitive(ui->log_button, TRUE);
        set_emphasis(ui->log_button, FALSE);
        gtk_widget_set_tooltip_text(ui->log_button, "Stop recording and close the CSV file.");
    } else {
        set_button_label(ui->log_button, "Start logging");
        gtk_widget_set_sensitive(ui->log_button, (tmd || simulator) && running);
        set_emphasis(ui->log_button, (tmd || simulator) && running);
        gtk_widget_set_tooltip_text(ui->log_button, "Record incoming measurements to a CSV file.");
    }
    if (ui->session_hint_on) {
        const char *hint = ui->session_message[0] != '\0'
                               ? ui->session_message
                               : "Enter a session name before logging.";
        set_label(ui->session_hint, hint);
        gtk_widget_set_visible(ui->session_hint, TRUE);
        gtk_widget_add_css_class(ui->session_entry, "entry-invalid");
    } else {
        gtk_widget_set_visible(ui->session_hint, FALSE);
        gtk_widget_remove_css_class(ui->session_entry, "entry-invalid");
    }
    if (logging && !ui->was_logging) ui->recording_started = g_get_monotonic_time();
    if (logging || ui->was_logging)
        ui->recording_duration = (g_get_monotonic_time() - ui->recording_started) / 1000000.0;
    ui->was_logging = logging;
    char record_time[48], record_status[96];
    format_clock(record_time, sizeof record_time, ui->recording_duration);
    snprintf(record_status, sizeof record_status, "%s · %s", logging ? "● Recording CSV" :
             ui->recording_started ? "CSV closed" : "Ready to record", record_time);
    set_label(ui->recording_state, replay ? "Historical review · read-only" : record_status);
    set_status_class(ui->recording_state, logging ? "status-recording" : "status-offline");
    gtk_widget_set_sensitive(ui->export_button, samples > 0 && !running);
    gtk_widget_set_tooltip_text(ui->export_button, "Stop acquisition before exporting a consistent snapshot of all retained raw samples.");
    gtk_widget_set_sensitive(ui->refresh_button, !running);
    gtk_widget_set_sensitive(ui->review_button, !running);
    set_button_label(ui->start_button, simulator ? "Start simulation" : "Connect / Acquire");
    set_button_label(ui->stop_button, simulator ? "Stop simulation" : "Disconnect");
    set_label(ui->log_path, replay ? (replay_loaded ? replay_path : "Open a historical recording") :
              app_log_path(ui->app)[0] ? app_log_path(ui->app) : "Start logging to create a CSV file");
    set_label(ui->connection, simulator ? "● Simulation" : replay ? "● File review" : running ? "● Connected" : "○ Disconnected");
    set_status_class(ui->connection, simulator ? "status-warning" : running ? "status-connected" : "status-offline");
    gtk_widget_set_tooltip_text(ui->log_path, gtk_label_get_text(GTK_LABEL(ui->log_path)));
    const char *origin = ui->history_source == APP_SOURCE_SIMULATOR ? "SIMULATION" :
                         ui->history_source == APP_SOURCE_REPLAY ? "Replay" : "TMD-56";
    char reading_caption[160];
    snprintf(reading_caption, sizeof reading_caption, "%s · %s",
             running ? "Acquiring" : samples ? "Last session readings" : "No measurements",
             samples || running ? origin : "Connect the instrument to begin");
    set_label(ui->reading_state, reading_caption);
    update_file_label(ui);
}

static const char *dropdown_string(GtkWidget *dropdown)
{
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    GtkStringList *model;
    const char *text;

    if (selected == GTK_INVALID_LIST_POSITION) {
        return "";
    }
    model = GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(dropdown)));
    if (model == NULL) {
        return "";
    }
    text = gtk_string_list_get_string(model, selected);
    return text != NULL ? text : "";
}

static void apply_selected_port(Ui *ui)
{
    const char *name = dropdown_string(ui->port_dropdown);
    snprintf(ui->port_name, sizeof ui->port_name, "%s", name);
    app_set_serial_port(ui->app, ui->port_name);
}

static void refresh_ports(Ui *ui, bool force)
{
    TmdPort ports[16];
    size_t count;
    size_t index;
    char signature[4096];
    size_t used = 0;
    GtkStringList *model;
    guint select = GTK_INVALID_LIST_POSITION;
    guint only_usb = GTK_INVALID_LIST_POSITION;
    int usb_count = 0;

    if (!force) {
        ui->port_scan_ticks++;
        if (ui->port_scan_ticks < 20) {
            return;
        }
    }
    ui->port_scan_ticks = 0;
    if (ui->port_dropdown == NULL || app_is_acquiring(ui->app) ||
        app_source(ui->app) != APP_SOURCE_TMD56) {
        return;
    }

    count = tmd56_list_ports(ports, sizeof ports / sizeof ports[0]);
    signature[0] = '\0';
    for (index = 0; index < count; index++) {
        int wrote = snprintf(signature + used, sizeof signature - used, "%s\n", ports[index].name);
        if (wrote < 0 || (size_t)wrote >= sizeof signature - used) {
            break;
        }
        used += (size_t)wrote;
    }
    if (ui->ports_scanned && strcmp(signature, ui->port_signature) == 0) {
        return;
    }
    ui->ports_scanned = true;
    snprintf(ui->port_signature, sizeof ui->port_signature, "%s", signature);

    model = gtk_string_list_new(NULL);
    for (index = 0; index < count; index++) {
        gtk_string_list_append(model, ports[index].name);
        if (ui->port_name[0] != '\0' && strcmp(ui->port_name, ports[index].name) == 0) {
            select = (guint)index;
        }
        if (ports[index].usb) {
            usb_count++;
            only_usb = (guint)index;
        }
    }
    if (select == GTK_INVALID_LIST_POSITION) {
        if (count == 1u) {
            select = 0;
        } else if (usb_count == 1) {
            select = only_usb;
        }
    }

    ui->syncing = true;
    gtk_drop_down_set_model(GTK_DROP_DOWN(ui->port_dropdown), G_LIST_MODEL(model));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->port_dropdown), select);
    ui->syncing = false;
    g_object_unref(model);
    apply_selected_port(ui);
    gtk_widget_set_tooltip_text(
        ui->port_dropdown,
        count == 0
            ? "Plug in the TMD-56. Set it to °C. Hold Power and T1−T2 at startup until it shows channel 0."
            : "Serial port for the TMD-56. Set the meter to °C, and start it on channel 0.");
}

static void on_port_selected(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    if (((Ui *)user_data)->syncing) {
        return;
    }
    apply_selected_port(user_data);
}

static gboolean on_tick(gpointer user_data)
{
    Ui *ui = user_data;
    int produced = app_poll(ui->app);
    if (!app_is_acquiring(ui->app) && app_source(ui->app) == APP_SOURCE_TMD56) {
        refresh_ports(ui, false);
    }
    refresh_ui(ui);
    if (produced > 0) {
        ui->exported = false;
        gtk_widget_queue_draw(ui->drawing);
    }
    return G_SOURCE_CONTINUE;
}

static void on_source_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    Ui *ui = user_data;
    guint selected;
    (void)pspec;
    if (ui->syncing) {
        return;
    }
    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION) {
        return;
    }
    if (app_is_acquiring(ui->app)) {
        ui->syncing = true;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(object),
                                   ui->device_choice == TMD_UI_DEVICE_REPLAY ? 1u : 0u);
        ui->syncing = false;
        return;
    }
    ui->device_choice = selected == 1u ? TMD_UI_DEVICE_REPLAY : TMD_UI_DEVICE_TMD56;
    app_set_source(ui->app, ui->device_choice == TMD_UI_DEVICE_REPLAY ? APP_SOURCE_REPLAY
                                                                      : APP_SOURCE_TMD56);
    if (ui->device_choice == TMD_UI_DEVICE_TMD56) {
        refresh_ports(ui, true);
    }
    refresh_ui(ui);
    gtk_widget_queue_draw(ui->drawing);
}

static void on_scenario_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    Ui *ui = user_data;
    guint selected;
    (void)pspec;
    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION || selected >= (guint)SIM_SCENARIO_COUNT) {
        return;
    }
    if (ui->syncing) {
        return;
    }
    app_set_scenario(ui->app, (SimulatorScenario)selected);
    if (ui->profile_action != NULL && selected < 6u) {
        ui->syncing = true;
        g_simple_action_set_state(ui->profile_action, g_variant_new_string(PROFILE_IDS[selected]));
        ui->syncing = false;
    }
    refresh_ui(ui);
}

static void on_interval_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    Ui *ui = user_data;
    guint selected;
    (void)pspec;
    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION || selected >= tmd_interval_count()) {
        return;
    }
    app_set_sample_interval(ui->app, tmd_interval_seconds(selected));
    refresh_ui(ui);
}

static void on_speed_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    Ui *ui = user_data;
    guint selected;
    (void)pspec;
    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION || selected > 4u) {
        return;
    }
    app_set_replay_speed(ui->app, SPEED_VALUES[selected]);
    refresh_ui(ui);
}

static void on_view_toggled(GtkToggleButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    int index;
    int other;

    if (ui->syncing) {
        return;
    }
    index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "view-index"));
    if (!gtk_toggle_button_get_active(button)) {
        ui->syncing = true;
        gtk_toggle_button_set_active(button, TRUE);
        ui->syncing = false;
        return;
    }
    ui->syncing = true;
    for (other = 0; other < 6; other++) {
        if (other != index) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->view_buttons[other]), FALSE);
        }
    }
    ui->syncing = false;
    if (index >= 0 && index < 6) {
        ui->plot.manual_time = false;
        ui->plot.window_seconds = WINDOW_VALUES[index];
    }
    gtk_widget_queue_draw(ui->drawing);
}

static void on_start(GtkButton *button, gpointer user_data);
static void on_play(GtkButton *button, gpointer user_data);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static void on_new_session_choice(GtkDialog *dialog, int response, gpointer data)
{
    Ui *ui = data;
    gtk_window_destroy(GTK_WINDOW(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        ui->allow_reset = true;
        if (app_source(ui->app) == APP_SOURCE_REPLAY) on_play(NULL, ui);
        else on_start(NULL, ui);
    }
}

static gboolean retain_session_first(Ui *ui)
{
    if (ui->allow_reset) { ui->allow_reset = false; return FALSE; }
    if (!history_buffer_count(app_history(ui->app)) || ui->exported ||
        ui->history_source == APP_SOURCE_REPLAY) return FALSE;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Start a new session?");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
        "Starting replaces the retained plot history. Export the session CSV first if you need all retained samples. Existing CSV recordings stay saved on disk.");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Keep current session", GTK_RESPONSE_CANCEL,
                           "Start new session", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    g_signal_connect(dialog, "response", G_CALLBACK(on_new_session_choice), ui);
    gtk_window_present(GTK_WINDOW(dialog));
    return TRUE;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void on_start(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    char error[384];
    (void)button;
    if (app_source(ui->app) == APP_SOURCE_REPLAY) {
        return;
    }
    if (retain_session_first(ui)) return;
    if (app_start(ui->app, error, sizeof error) != 0) {
        refresh_ui(ui);
        return;
    }
    reset_plot_cursor(ui);
    ui->exported = false;
    ui->history_source = app_source(ui->app);
    refresh_ui(ui);
    gtk_widget_queue_draw(ui->drawing);
}

static void on_play(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    char error[384];
    (void)button;
    if (app_is_acquiring(ui->app) && app_replay_paused(ui->app)) {
        app_replay_pause(ui->app, false);
        refresh_ui(ui);
        return;
    }
    if (retain_session_first(ui)) return;
    if (app_start(ui->app, error, sizeof error) != 0) {
        refresh_ui(ui);
        return;
    }
    reset_plot_cursor(ui);
    ui->exported = false;
    ui->history_source = app_source(ui->app);
    refresh_ui(ui);
    gtk_widget_queue_draw(ui->drawing);
}

static void on_stop(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    (void)button;
    app_stop(ui->app);
    refresh_ui(ui);
}

static void on_pause(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    (void)button;
    if (app_is_acquiring(ui->app) && !app_replay_paused(ui->app)) {
        app_replay_pause(ui->app, true);
    }
    refresh_ui(ui);
}

static void on_restart(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    (void)button;
    app_replay_restart(ui->app);
    reset_plot_cursor(ui);
    refresh_ui(ui);
    gtk_widget_queue_draw(ui->drawing);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static void on_file_response(GtkNativeDialog *dialog, int response, gpointer data)
{
    Ui *ui = data;
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        char *path = file ? g_file_get_path(file) : NULL;
        if (!path) {
            app_report_error(ui->app, "Choose a local file");
        } else {
            char message[384];
            ui->device_choice = TMD_UI_DEVICE_REPLAY;
            g_simple_action_set_state(ui->simulation_action, g_variant_new_boolean(FALSE));
            app_set_source(ui->app, APP_SOURCE_REPLAY);
            ui->syncing = true;
            gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->source_dropdown), 1u);
            ui->syncing = false;
            app_set_replay_file(ui->app, path, message, sizeof message);
        }
        g_free(path); g_clear_object(&file);
        refresh_ui(ui); gtk_widget_queue_draw(ui->drawing);
    }
    gtk_native_dialog_destroy(dialog); g_object_unref(dialog);
}

static void on_open_file(GtkButton *button, gpointer data)
{
    Ui *ui = data; (void)button;
    GtkFileChooserNative *dialog = gtk_file_chooser_native_new("Open TMD-56 recording",
        GTK_WINDOW(ui->window), GTK_FILE_CHOOSER_ACTION_OPEN, "Open recording", "Cancel");
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "TMD export or CSV");
    const char *patterns[] = {"*.txt", "*.tsv", "*.csv", "*.TXT", "*.TSV", "*.CSV"};
    for (guint i = 0; i < G_N_ELEMENTS(patterns); i++) gtk_file_filter_add_pattern(filter, patterns[i]);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    g_object_unref(filter);
    g_signal_connect(dialog, "response", G_CALLBACK(on_file_response), ui);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void on_session_changed(GtkEditable *editable, gpointer user_data)
{
    (void)editable;
    refresh_ui(user_data);
}

static void on_log_clicked(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    char error[384];
    const char *session = gtk_editable_get_text(GTK_EDITABLE(ui->session_entry));
    (void)button;
    if (app_is_logging(ui->app)) {
        ui->session_hint_on = false;
        ui->session_message[0] = '\0';
        app_stop_logging(ui->app);
        refresh_ui(ui);
        return;
    }
    if (!session_name_usable(session)) {
        ui->session_hint_on = true;
        snprintf(ui->session_message, sizeof ui->session_message,
                 "Enter a session name before logging.");
        refresh_ui(ui);
        gtk_widget_grab_focus(ui->session_entry);
        return;
    }
    ui->session_hint_on = false;
    ui->session_message[0] = '\0';
    char marked_session[256];
    if (app_source(ui->app) == APP_SOURCE_SIMULATOR) {
        snprintf(marked_session, sizeof marked_session, "SIMULATION_%s", session);
        session = marked_session;
    }
    if (app_start_logging(ui->app, session, error, sizeof error) != 0) {
        ui->session_hint_on = true;
        snprintf(ui->session_message, sizeof ui->session_message, "%s",
                 error[0] != '\0' ? error : "Logging did not start.");
        refresh_ui(ui);
        return;
    }
    gtk_widget_grab_focus(ui->log_button);
    refresh_ui(ui);
}

static void on_spike_toggled(GtkCheckButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    app_set_spike_filter(ui->app, gtk_check_button_get_active(button));
    refresh_ui(ui);
    gtk_widget_queue_draw(ui->drawing);
}

static void on_threshold_changed(GtkSpinButton *spin, gpointer user_data)
{
    Ui *ui = user_data;
    app_set_spike_threshold(ui->app, gtk_spin_button_get_value(spin));
    refresh_ui(ui);
    gtk_widget_queue_draw(ui->drawing);
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    Ui *ui = user_data;
    (void)area;
    load_plot_color(ui->drawing, "tmd_plot", ui->plot.palette.bg, 0.086, 0.090, 0.098);
    load_plot_color(ui->drawing, "tmd_grid", ui->plot.palette.grid, 0.216, 0.227, 0.247);
    load_plot_color(ui->drawing, "tmd_axis", ui->plot.palette.axis, 0.663, 0.678, 0.702);
    load_plot_color(ui->drawing, "tmd_t1", ui->plot.palette.t1, 0.843, 0.098, 0.125);
    load_plot_color(ui->drawing, "tmd_cyan", ui->plot.palette.t2, 0.235, 0.706, 0.839);
    load_plot_color(ui->drawing, "tmd_delta", ui->plot.palette.mark, 0.898, 0.718, 0.184);
    ui->plot.have_palette = true;
    ui->plot.history = app_history(ui->app);
    ui->plot.hide_suspicious = app_spike_filter_enabled(ui->app);
    ui->plot.overlay = overlay_for(ui);
    live_plot_draw(&ui->plot, cr, width, height);
    if (ui->cursor_active && ui->cursor_idle_id == 0)
        ui->cursor_idle_id = g_idle_add(on_cursor_idle, ui);
}

static void stop_window_work(Ui *ui)
{
    if (ui->cursor_idle_id != 0) {
        g_source_remove(ui->cursor_idle_id);
        ui->cursor_idle_id = 0;
    }
    if (ui->timer_id != 0u) {
        g_source_remove(ui->timer_id);
        ui->timer_id = 0u;
    }
    /* Stop acquisition and close the log before the process tears the window down. */
    app_stop(ui->app);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static void on_close_choice(GtkDialog *dialog, int response, gpointer data)
{
    Ui *ui = data;
    gtk_window_destroy(GTK_WINDOW(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        ui->allow_close = true;
        gtk_window_close(GTK_WINDOW(ui->window));
    }
}

static gboolean on_close_request(GtkWindow *window, gpointer user_data)
{
    Ui *ui = user_data;
    if (!ui->allow_close && !ui->exported && ui->history_source != APP_SOURCE_REPLAY &&
        history_buffer_count(app_history(ui->app)) > 0) {
        app_stop(ui->app); refresh_ui(ui);
        GtkWidget *dialog = gtk_message_dialog_new(window, GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Close this session?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
            "Acquisition has stopped and CSV recordings are saved. Retained plot samples have not been exported. Keep the session open to export them before closing.");
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Keep session open", GTK_RESPONSE_CANCEL,
                               "Close application", GTK_RESPONSE_ACCEPT, NULL);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
        g_signal_connect(dialog, "response", G_CALLBACK(on_close_choice), ui);
        gtk_window_present(GTK_WINDOW(dialog));
        return TRUE;
    }
    stop_window_work(ui);
    return FALSE;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void on_window_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    stop_window_work(user_data);
}

static void on_about(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    GdkTexture *logo = NULL;
    (void)button;
    logo = gdk_texture_new_from_resource("/com/tmd56/Monitor/icons/tmd56-monitor-128.png");
    gtk_show_about_dialog(GTK_WINDOW(ui->window),
                          "program-name", "TMD-56 Temperature Logger",
                          "version", TMD_VERSION_STRING,
                          "comments", "Dual-input temperature logger.\n"
                                      "Not an official Amprobe product.",
                          "logo", logo,
                          "logo-icon-name", "tmd56-monitor",
                          NULL);
    if (logo != NULL) {
        g_object_unref(logo);
    }
}

static void apply_css(void)
{
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/tmd56/Monitor/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/tmd56/Monitor/figcraft-components.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    g_object_unref(provider);
    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/tmd56/Monitor/workstation.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    g_object_unref(provider);
}

static int profile_index(const char *id)
{
    guint index;
    if (id == NULL) {
        return 0;
    }
    for (index = 0; index < 6u; index++) {
        if (strcmp(id, PROFILE_IDS[index]) == 0) {
            return (int)index;
        }
    }
    return 0;
}

static void on_simulation_state(GSimpleAction *action, GVariant *value, gpointer user_data)
{
    Ui *ui = user_data;
    gboolean on = g_variant_get_boolean(value);
    if (app_is_acquiring(ui->app)) {
        return;
    }
    g_simple_action_set_state(action, value);
    if (on) {
        app_set_source(ui->app, APP_SOURCE_SIMULATOR);
    } else if (ui->device_choice == TMD_UI_DEVICE_REPLAY) {
        app_set_source(ui->app, APP_SOURCE_REPLAY);
    } else {
        app_set_source(ui->app, APP_SOURCE_TMD56);
        refresh_ports(ui, true);
    }
    refresh_ui(ui);
    gtk_widget_queue_draw(ui->drawing);
}

static void on_profile_state(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    Ui *ui = user_data;
    const char *id;
    int index;
    if (parameter == NULL || !g_variant_is_of_type(parameter, G_VARIANT_TYPE_STRING)) {
        return;
    }
    if (ui->syncing || !g_action_get_enabled(G_ACTION(action))) {
        return;
    }
    id = g_variant_get_string(parameter, NULL);
    index = profile_index(id);
    g_simple_action_set_state(action, parameter);
    ui->syncing = true;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->scenario_dropdown), (guint)index);
    ui->syncing = false;
    app_set_scenario(ui->app, (SimulatorScenario)index);
    refresh_ui(ui);
}

static void install_tools_menu(GtkApplication *gtk_app, GtkWidget *header_bar, Ui *ui)
{
    GMenu *tools = g_menu_new();
    GMenu *simulation = g_menu_new();
    GMenu *profiles = g_menu_new();
    GtkWidget *tools_button;
    guint index;

    ui->simulation_action = g_simple_action_new_stateful("simulation", NULL,
                                                         g_variant_new_boolean(FALSE));
    g_signal_connect(ui->simulation_action, "change-state", G_CALLBACK(on_simulation_state), ui);
    g_action_map_add_action(G_ACTION_MAP(gtk_app), G_ACTION(ui->simulation_action));
    g_object_unref(ui->simulation_action);

    ui->profile_action = g_simple_action_new_stateful("sim-profile", G_VARIANT_TYPE_STRING,
                                                      g_variant_new_string("stable"));
    g_simple_action_set_enabled(ui->profile_action, FALSE);
    g_signal_connect(ui->profile_action, "change-state", G_CALLBACK(on_profile_state), ui);
    g_action_map_add_action(G_ACTION_MAP(gtk_app), G_ACTION(ui->profile_action));
    g_object_unref(ui->profile_action);

    g_menu_append(simulation, "Enable Simulation Mode", "app.simulation");
    for (index = 0; index < 6u; index++) {
        char action[64];
        snprintf(action, sizeof action, "app.sim-profile::%s", PROFILE_IDS[index]);
        g_menu_append(profiles, SCENARIO_ITEMS[index], action);
    }
    g_menu_append_submenu(simulation, "Profile", G_MENU_MODEL(profiles));
    g_menu_append_submenu(tools, "Simulation", G_MENU_MODEL(simulation));

    tools_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(tools_button), "Tools");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(tools_button), G_MENU_MODEL(tools));
    gtk_widget_add_css_class(tools_button, "secondary-control");
    gtk_widget_set_tooltip_text(tools_button, "Simulation and other tools.");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header_bar), tools_button);
    g_object_unref(profiles);
    g_object_unref(simulation);
    g_object_unref(tools);
}


static void on_refresh(GtkButton *button, gpointer data)
{
    (void)button; refresh_ports(data, true);
}

static void on_channels(GtkCheckButton *button, gpointer data)
{
    Ui *ui = data; (void)button;
    ui->plot.hide_t1 = !gtk_check_button_get_active(GTK_CHECK_BUTTON(ui->channel_checks[0]));
    ui->plot.hide_t2 = !gtk_check_button_get_active(GTK_CHECK_BUTTON(ui->channel_checks[1]));
    ui->plot.show_delta = gtk_check_button_get_active(GTK_CHECK_BUTTON(ui->channel_checks[2]));
    ui->plot.have_span = false;
    gtk_widget_queue_draw(ui->drawing);
}

static void on_range(GtkWidget *widget, gpointer data)
{
    Ui *ui = data; (void)widget;
    double low = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->y_low));
    double high = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->y_high));
    gboolean automatic = gtk_check_button_get_active(GTK_CHECK_BUTTON(ui->auto_y));
    gtk_widget_set_sensitive(ui->y_low, !automatic);
    gtk_widget_set_sensitive(ui->y_high, !automatic);
    ui->plot.manual_y = !automatic && high > low;
    ui->plot.y_min = low; ui->plot.y_max = high;
    gtk_widget_set_tooltip_text(ui->y_high, high > low ? "Upper temperature limit (°C)" : "Upper limit must exceed lower limit; autoscale is active");
    ui->plot.have_span = false;
    gtk_widget_queue_draw(ui->drawing);
}

static void on_fit(GtkButton *button, gpointer data)
{
    Ui *ui = data; (void)button;
    ui->plot.manual_time = false; ui->plot.have_span = false;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->auto_y), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->view_buttons[5]), TRUE);
    gtk_widget_queue_draw(ui->drawing);
}

static gboolean on_zoom(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
    Ui *ui = data; (void)controller; (void)dx;
    double start = ui->plot.drawn_t0, end = ui->plot.drawn_t1;
    double span = fmax(1.0, fmin(864000.0, (end - start) * (dy > 0 ? 1.25 : 0.8)));
    ui->plot.manual_time = true;
    ui->plot.time_start = fmax(0.0, (start + end - span) / 2.0);
    ui->plot.time_end = ui->plot.time_start + span;
    gtk_widget_queue_draw(ui->drawing); return TRUE;
}

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
    Ui *ui = data; (void)gesture; (void)x; (void)y;
    ui->drag_start = ui->plot.drawn_t0;
}

static void on_drag(GtkGestureDrag *gesture, double x, double y, gpointer data)
{
    Ui *ui = data; (void)gesture; (void)y;
    double span = ui->plot.drawn_t1 - ui->plot.drawn_t0;
    double width = fmax(1.0, gtk_widget_get_width(ui->drawing) - 92.0);
    ui->plot.manual_time = true;
    ui->plot.time_start = fmax(0.0, ui->drag_start - x / width * span);
    ui->plot.time_end = ui->plot.time_start + span;
    gtk_widget_queue_draw(ui->drawing);
}

static void on_motion(GtkEventControllerMotion *controller, double x, double y, gpointer data)
{
    Ui *ui = data; (void)controller; (void)y;
    ui->cursor_active = true;
    ui->cursor_x = x;
    double fraction = (x - 76.0) / fmax(1.0, gtk_widget_get_width(ui->drawing) - 92.0);
    double time = ui->plot.drawn_t0 + fraction * (ui->plot.drawn_t1 - ui->plot.drawn_t0);
    const HistoryBuffer *history = app_history(ui->app);
    size_t count = history_buffer_count(history);
    if (fraction < 0 || fraction > 1 || !count) { reset_plot_cursor(ui); return; }
    size_t index = history_buffer_lower_bound(history, time);
    if (index >= count) index = count - 1;
    if (index > 0 && fabs(history_buffer_at(history, index - 1)->elapsed_seconds - time) <
                     fabs(history_buffer_at(history, index)->elapsed_seconds - time)) index--;
    const TemperatureMeasurement *sample = history_buffer_at(history, index);
    char a[16], b[16], d[16], text[192];
    measurement_format_reading(a, sizeof a, sample->t1);
    measurement_format_reading(b, sizeof b, sample->t2);
    measurement_format_reading(d, sizeof d, sample->t1 - sample->t2);
    snprintf(text, sizeof text, "Cursor  %.2f s     T1 %s °C     T2 %s °C     ΔT %s °C",
             sample->elapsed_seconds, display_reading(a), display_reading(b), display_reading(d));
    set_label(ui->cursor, text);
}

static gboolean on_cursor_idle(gpointer data)
{
    Ui *ui = data;
    ui->cursor_idle_id = 0;
    if (ui->cursor_active) on_motion(NULL, ui->cursor_x, 0, ui);
    return G_SOURCE_REMOVE;
}

static void on_plot_leave(GtkEventControllerMotion *controller, gpointer data)
{
    (void)controller;
    reset_plot_cursor(data);
}

static GString *session_csv(Ui *ui)
{
    GString *csv = g_string_new("# Source: ");
    g_string_append(csv, ui->history_source == APP_SOURCE_SIMULATOR ? "SIMULATION" :
                       ui->history_source == APP_SOURCE_REPLAY ? "REPLAY" : "TMD-56");
    g_string_append(csv, "\n# Notes: ");
    const char *notes = gtk_editable_get_text(GTK_EDITABLE(ui->notes));
    for (const char *c = notes; *c; c++) g_string_append_c(csv, (*c == '\n' || *c == '\r') ? ' ' : *c);
    g_string_append(csv, "\nelapsed_s,t1_c,t2_c,delta_t_c,valid\n");
    const HistoryBuffer *history = app_history(ui->app);
    for (size_t i = 0; i < history_buffer_count(history); i++) {
        const TemperatureMeasurement *m = history_buffer_at(history, i);
        double values[] = {m->elapsed_seconds, m->t1, m->t2, m->t1 - m->t2};
        for (int j = 0; j < 4; j++) {
            char value[G_ASCII_DTOSTR_BUF_SIZE];
            if (isfinite(values[j])) g_string_append(csv, g_ascii_dtostr(value, sizeof value, values[j]));
            g_string_append_c(csv, ',');
        }
        g_string_append_printf(csv, "%d\n", m->valid ? 1 : 0);
    }
    return csv;
}

/* Native chooser response keeps the selected file and the write in one
 * callback. GTK 4 retains this API for platform-native desktop dialogs. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static void on_export_response(GtkNativeDialog *dialog, int response, gpointer data)
{
    Ui *ui = data;
    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        GError *error = NULL;
        GString *csv = session_csv(ui);
        if (!file || !g_file_replace_contents(file, csv->str, csv->len, NULL, FALSE,
                                              G_FILE_CREATE_NONE, NULL, NULL, &error)) {
            app_report_error(ui->app, error ? error->message : "Choose a local export file");
        } else ui->exported = true;
        g_string_free(csv, TRUE);
        g_clear_error(&error); g_clear_object(&file);
    }
    gtk_native_dialog_destroy(dialog);
    g_object_unref(dialog);
    refresh_ui(ui);
}

static void on_export(GtkButton *button, gpointer data)
{
    Ui *ui = data; (void)button;
    GtkFileChooserNative *dialog = gtk_file_chooser_native_new("Export raw session measurements",
        GTK_WINDOW(ui->window), GTK_FILE_CHOOSER_ACTION_SAVE, "Export CSV", "Cancel");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
        ui->history_source == APP_SOURCE_SIMULATOR ? "SIMULATION-temperatures.csv" : "temperatures.csv");
    g_signal_connect(dialog, "response", G_CALLBACK(on_export_response), ui);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static gboolean on_plot_key(GtkEventControllerKey *controller, guint key, guint code,
                            GdkModifierType state, gpointer data)
{
    Ui *ui = data; (void)controller; (void)code; (void)state;
    if (key == GDK_KEY_plus || key == GDK_KEY_equal || key == GDK_KEY_KP_Add)
        return on_zoom(NULL, 0, -1, ui);
    if (key == GDK_KEY_minus || key == GDK_KEY_KP_Subtract)
        return on_zoom(NULL, 0, 1, ui);
    if (key == GDK_KEY_Home) { on_fit(NULL, ui); return TRUE; }
    if (key == GDK_KEY_Left || key == GDK_KEY_Right) {
        on_drag_begin(NULL, 0, 0, ui);
        on_drag(NULL, (key == GDK_KEY_Left ? 1 : -1) *
                      (gtk_widget_get_width(ui->drawing) - 92) / 10.0, 0, ui);
        return TRUE;
    }
    return FALSE;
}

static void move_box_child(GtkWidget *widget, GtkWidget *destination)
{
    g_object_ref(widget);
    gtk_box_remove(GTK_BOX(gtk_widget_get_parent(widget)), widget);
    gtk_box_append(GTK_BOX(destination), widget);
    g_object_unref(widget);
}

static void on_activate(GtkApplication *gtk_app, gpointer user_data)
{
    App *app = user_data;
    Ui *ui = g_new0(Ui, 1);
    GtkWidget *chrome;
    GtkWidget *header_rail;
    GtkWidget *header;
    GtkWidget *identity;
    GtkWidget *mark;
    GtkWidget *line;
    GtkWidget *body;
    GtkWidget *rail;
    GtkWidget *column;
    GtkWidget *display;
    GtkWidget *channels;
    GtkWidget *delta_details;
    GtkWidget *plot;
    GtkWidget *plot_bar;
    GtkWidget *plot_title;
    GtkWidget *view_group;
    GtkWidget *facts;
    GtkWidget *status;
    GtkAdjustment *threshold;
    GtkWidget *workspace, *sidebar, *side_content;
    int view;

    ui->app = app;
    ui->plot.window_seconds = 300.0;
    ui->window = gtk_application_window_new(gtk_app);
    g_object_set_data(G_OBJECT(ui->window), "tmd-ui", ui);
    gtk_window_set_title(GTK_WINDOW(ui->window), "TMD-56 Temperature Logger");
    gtk_window_set_icon_name(GTK_WINDOW(ui->window), "tmd56-monitor");
    gtk_window_set_default_size(GTK_WINDOW(ui->window), 1360, 820);
    gtk_widget_set_size_request(ui->window, 980, 680);
    gtk_widget_add_css_class(ui->window, "amprobe-shell");

    chrome = gtk_header_bar_new();
    gtk_widget_add_css_class(chrome, "chrome");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(chrome), TRUE);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(chrome),
                                    gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));

    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(header, "instrument-header");
    gtk_widget_set_hexpand(header, TRUE);
    header_rail = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(header_rail, "brand-rail");
    identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    mark = gtk_label_new("TMD-56");
    line = gtk_label_new("Dual-input temperature logger");
    gtk_widget_add_css_class(mark, "product-mark");
    gtk_widget_add_css_class(line, "product-line");
    gtk_widget_set_halign(mark, GTK_ALIGN_START);
    gtk_widget_set_halign(line, GTK_ALIGN_START);
    gtk_widget_set_valign(identity, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(identity), mark);
    gtk_box_append(GTK_BOX(identity), line);

    ui->about_button = control_button("Help & About", NULL);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(chrome), ui->about_button);
    install_tools_menu(gtk_app, chrome, ui);

    {
        GdkTexture *icon = gdk_texture_new_from_resource("/com/tmd56/Monitor/icons/tmd56-monitor-128.png");
        GtkWidget *mark_icon = gtk_image_new_from_paintable(GDK_PAINTABLE(icon));
        g_object_unref(icon);
        gtk_image_set_pixel_size(GTK_IMAGE(mark_icon), 36);
        gtk_widget_add_css_class(mark_icon, "product-icon");
        gtk_widget_set_valign(mark_icon, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(header), mark_icon);
    }
    gtk_box_append(GTK_BOX(header), identity);

    {
        GtkWidget *header_wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(header_wrap, TRUE);
        gtk_box_append(GTK_BOX(header_wrap), header_rail);
        gtk_box_append(GTK_BOX(header_wrap), header);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(chrome), header_wrap);
    }
    gtk_window_set_titlebar(GTK_WINDOW(ui->window), chrome);

    body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(body, TRUE);
    rail = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(rail, "brand-rail");
    gtk_widget_set_vexpand(rail, TRUE);
    column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(column, TRUE);
    gtk_widget_set_vexpand(column, TRUE);
    gtk_box_append(GTK_BOX(body), rail);
    gtk_box_append(GTK_BOX(body), column);
    gtk_window_set_child(GTK_WINDOW(ui->window), body);

    {
        GtkWidget *control_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *settings_row = gtk_flow_box_new();
        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(settings_row), GTK_SELECTION_NONE);
        gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(settings_row), 8);
        gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(settings_row), 8);
        gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(settings_row), 6);
        GtkWidget *sim_device;

        gtk_widget_add_css_class(control_area, "control-area");
        gtk_widget_set_valign(settings_row, GTK_ALIGN_CENTER);
        ui->action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_valign(ui->action_row, GTK_ALIGN_CENTER);

        ensure_interval_items();
        ui->source_dropdown = make_dropdown(DEVICE_ITEMS, 0);
        gtk_widget_set_size_request(ui->source_dropdown, 148, 28);
        ui->device_box = inline_field("Source", ui->source_dropdown);
        gtk_widget_set_tooltip_text(ui->source_dropdown, "Choose the instrument or a replay file.");

        ui->sim_badge = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(ui->sim_badge, 16);
        {
            GtkWidget *badge = gtk_label_new("SIMULATION ACTIVE");
            gtk_widget_add_css_class(badge, "simulation-badge");
            gtk_box_append(GTK_BOX(ui->sim_badge), badge);
        }
        sim_device = gtk_label_new("Simulator");
        gtk_widget_add_css_class(sim_device, "fact-value");
        gtk_box_append(GTK_BOX(ui->sim_badge), inline_field("Device", sim_device));

        ui->scenario_dropdown = make_dropdown(SCENARIO_ITEMS, 0);
        gtk_widget_set_size_request(ui->scenario_dropdown, 188, 28);
        ui->scenario_box = inline_field("Profile", ui->scenario_dropdown);
        gtk_widget_set_margin_start(ui->scenario_box, 16);
        gtk_widget_set_tooltip_text(ui->scenario_dropdown,
                                    "Choose how the simulator changes T1 and T2.");

        ui->interval_dropdown = make_dropdown(interval_items, 0);
        gtk_widget_set_size_request(ui->interval_dropdown, 96, 28);
        ui->interval_box = inline_field("Sample interval", ui->interval_dropdown);
        gtk_widget_set_margin_start(ui->interval_box, 16);
        gtk_widget_set_tooltip_text(ui->interval_dropdown, "Time between measurements.");

        ui->file_button = control_button("Open recording…", "file-action");
        gtk_widget_set_margin_start(ui->file_button, 16);
        gtk_widget_set_tooltip_text(ui->file_button,
                                    "Load a previously recorded TMD-56 or CSV measurement file.");
        ui->file_label = gtk_label_new("No replay file loaded");
        gtk_widget_add_css_class(ui->file_label, "fact-value");
        gtk_label_set_ellipsize(GTK_LABEL(ui->file_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(ui->file_label), 28);
        gtk_label_set_xalign(GTK_LABEL(ui->file_label), 0.0f);
        ui->file_box = inline_field("File", ui->file_label);
        ui->speed_dropdown = make_dropdown(SPEED_ITEMS, 1);
        gtk_widget_set_size_request(ui->speed_dropdown, 84, 28);
        ui->speed_box = inline_field("Playback speed", ui->speed_dropdown);
        gtk_widget_set_margin_start(ui->speed_box, 16);
        gtk_widget_set_tooltip_text(ui->speed_dropdown, "How fast the replay file is played.");

        /* gtk_drop_down_new takes ownership of the model. */
        ui->port_dropdown = gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(NULL)), NULL);
        gtk_widget_set_size_request(ui->port_dropdown, 150, 28);
        gtk_widget_set_tooltip_text(
            ui->port_dropdown,
            "Plug in the TMD-56. Set it to °C. Hold Power and T1−T2 at startup until it shows channel 0.");
        ui->port_box = inline_field("Port", ui->port_dropdown);
        gtk_widget_set_margin_start(ui->port_box, 16);

        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->device_box, -1);
        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->sim_badge, -1);
        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->scenario_box, -1);
        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->port_box, -1);
        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->interval_box, -1);
        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->file_button, -1);
        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->file_box, -1);
        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->speed_box, -1);

        ui->start_button = control_button("Connect / Acquire", "acquire-action");
        ui->stop_button = control_button("Disconnect", "acquire-action");
        gtk_widget_set_tooltip_text(ui->start_button,
                                    "Begin reading measurements from the selected source.");
        gtk_widget_set_tooltip_text(ui->stop_button, "Stop reading measurements.");

        ui->session_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(ui->session_entry), "Enter session name");
        gtk_widget_set_size_request(ui->session_entry, 180, 28);
        gtk_widget_set_tooltip_text(ui->session_entry, "Name used for the CSV file.");
        ui->session_box = inline_field("Session name", ui->session_entry);
        gtk_widget_set_margin_start(ui->session_box, 20);
        ui->session_hint = gtk_label_new("");
        gtk_widget_add_css_class(ui->session_hint, "inline-hint");
        gtk_widget_set_halign(ui->session_hint, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(ui->session_hint), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(ui->session_hint), 42);
        gtk_widget_set_visible(ui->session_hint, FALSE);

        ui->log_button = control_button("Start logging", "log-action");
        ui->play_button = control_button("Play", "replay-action");
        ui->pause_button = control_button("Pause", "replay-action");
        ui->restart_button = control_button("Restart", "replay-action");
        gtk_widget_set_tooltip_text(ui->play_button, "Play the loaded replay file.");
        gtk_widget_set_tooltip_text(ui->pause_button, "Pause replay.");
        gtk_widget_set_tooltip_text(ui->restart_button, "Return replay to the first sample.");

        gtk_box_append(GTK_BOX(ui->action_row), ui->start_button);
        gtk_box_append(GTK_BOX(ui->action_row), ui->stop_button);
        gtk_box_append(GTK_BOX(ui->action_row), ui->play_button);
        gtk_box_append(GTK_BOX(ui->action_row), ui->pause_button);
        gtk_box_append(GTK_BOX(ui->action_row), ui->restart_button);
        gtk_box_append(GTK_BOX(ui->action_row), ui->session_box);
        gtk_box_append(GTK_BOX(ui->action_row), ui->log_button);
        gtk_box_append(GTK_BOX(ui->action_row), ui->session_hint);

        gtk_box_append(GTK_BOX(control_area), settings_row);
        gtk_flow_box_insert(GTK_FLOW_BOX(settings_row), ui->action_row, -1);
        gtk_box_append(GTK_BOX(column), control_area);
    }

    display = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(display, "measurement-display");
    gtk_widget_set_margin_start(display, 16);
    gtk_widget_set_margin_end(display, 16);
    gtk_widget_set_margin_top(display, 12);
    ui->reading_state = gtk_label_new("No live device readings");
    gtk_widget_add_css_class(ui->reading_state, "path-label");
    gtk_widget_set_halign(ui->reading_state, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(display), ui->reading_state);
    channels = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(channels), TRUE);
    gtk_box_append(GTK_BOX(channels), make_channel("T1 · Thermocouple 1", "channel-t1", &ui->t1_value, &ui->t1_stats));
    gtk_box_append(GTK_BOX(channels), make_channel("T2 · Thermocouple 2", "channel-t2", &ui->t2_value, &ui->t2_stats));
    gtk_box_append(GTK_BOX(channels), make_channel("ΔT · T1 − T2", "channel-delta", &ui->dt_value, &delta_details));
    gtk_label_set_text(GTK_LABEL(delta_details), "Difference between channels");
    ui->spike_mark = gtk_label_new("SPIKE");
    gtk_widget_add_css_class(ui->spike_mark, "spike-mark");
    gtk_widget_set_tooltip_text(ui->spike_mark, "Latest sample exceeds the selected spike threshold. Raw CSV values are preserved.");
    gtk_box_append(GTK_BOX(gtk_widget_get_parent(delta_details)), ui->spike_mark);
    gtk_box_append(GTK_BOX(display), channels);
    workspace = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(workspace, TRUE);
    gtk_paned_set_position(GTK_PANED(workspace), 530);
    gtk_paned_set_shrink_start_child(GTK_PANED(workspace), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(workspace), FALSE);
    GtkWidget *analysis = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(analysis, 600, -1);
    gtk_paned_set_start_child(GTK_PANED(workspace), analysis);
    sidebar = gtk_scrolled_window_new();
    gtk_widget_set_size_request(sidebar, -1, 128);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    side_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(side_content, "session-panel");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar), side_content);
    gtk_paned_set_end_child(GTK_PANED(workspace), sidebar);
    gtk_box_append(GTK_BOX(column), workspace);
    gtk_box_append(GTK_BOX(analysis), display);

    plot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(plot, "plot-container");
    gtk_widget_set_vexpand(plot, TRUE);
    gtk_widget_set_margin_start(plot, 16);
    gtk_widget_set_margin_end(plot, 16);
    gtk_widget_set_margin_top(plot, 12);
    gtk_widget_set_margin_bottom(plot, 12);
    plot_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(plot_bar, "plot-toolbar");
    plot_title = gtk_label_new("Temperature over time");
    gtk_widget_add_css_class(plot_title, "instrument-section-title");
    gtk_widget_set_halign(plot_title, GTK_ALIGN_START);
    gtk_widget_set_hexpand(plot_title, TRUE);
    view_group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(view_group, "view-group");
    for (view = 0; view < 6; view++) {
        ui->view_buttons[view] = gtk_toggle_button_new_with_label(VIEW_LABELS[view]);
        gtk_widget_add_css_class(ui->view_buttons[view], "view-option");
        g_object_set_data(G_OBJECT(ui->view_buttons[view]), "view-index", GINT_TO_POINTER(view));
        gtk_box_append(GTK_BOX(view_group), ui->view_buttons[view]);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->view_buttons[1]), TRUE);
    gtk_widget_set_tooltip_text(view_group, "Amount of recent history shown on the graph.");
    gtk_box_append(GTK_BOX(plot_bar), plot_title);
    gtk_box_append(GTK_BOX(plot_bar), inline_field("Time range", view_group));
    ui->drawing = gtk_drawing_area_new();
    gtk_widget_add_css_class(ui->drawing, "plot-canvas");
    gtk_widget_set_focusable(ui->drawing, TRUE);
    gtk_widget_set_vexpand(ui->drawing, TRUE);
    gtk_widget_set_hexpand(ui->drawing, TRUE);
    gtk_widget_set_size_request(ui->drawing, -1, 80);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui->drawing), on_draw, ui, NULL);
    gtk_box_append(GTK_BOX(plot), plot_bar);
    gtk_box_append(GTK_BOX(plot), ui->drawing);
    GtkWidget *plot_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    for (int i = 0; i < 3; i++) {
        ui->channel_checks[i] = gtk_check_button_new_with_label(i == 0 ? "T1" : i == 1 ? "T2" : "ΔT");
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->channel_checks[i]), i < 2);
        gtk_widget_add_css_class(ui->channel_checks[i], i == 0 ? "trace-t1" : i == 1 ? "trace-t2" : "trace-delta");
        g_signal_connect(ui->channel_checks[i], "toggled", G_CALLBACK(on_channels), ui);
        gtk_box_append(GTK_BOX(plot_controls), ui->channel_checks[i]);
    }
    GtkWidget *fit = control_button("Reset view", NULL);
    gtk_box_append(GTK_BOX(plot_controls), fit);
    g_signal_connect(fit, "clicked", G_CALLBACK(on_fit), ui);
    gtk_box_append(GTK_BOX(plot), plot_controls);
    ui->cursor = gtk_label_new("Scroll to zoom · drag to pan · hover to inspect samples");
    gtk_label_set_xalign(GTK_LABEL(ui->cursor), 0);
    gtk_widget_add_css_class(ui->cursor, "path-label");
    gtk_label_set_ellipsize(GTK_LABEL(ui->cursor), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(ui->cursor, TRUE);
    gtk_widget_set_margin_start(ui->cursor, 12);
    gtk_box_append(GTK_BOX(plot_controls), ui->cursor);
    GtkEventController *scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_zoom), ui);
    gtk_widget_add_controller(ui->drawing, scroll);
    GtkGesture *drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), ui);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag), ui);
    gtk_widget_add_controller(ui->drawing, GTK_EVENT_CONTROLLER(drag));
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), ui);
    g_signal_connect(motion, "leave", G_CALLBACK(on_plot_leave), ui);
    gtk_widget_add_controller(ui->drawing, motion);
    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_plot_key), ui);
    gtk_widget_add_controller(ui->drawing, keys);
    ui->auto_y = gtk_check_button_new_with_label("Autoscale temperature");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->auto_y), TRUE);
    gtk_box_append(GTK_BOX(side_content), ui->auto_y);
    ui->y_low = gtk_spin_button_new_with_range(-4000, 4000, 1);
    ui->y_high = gtk_spin_button_new_with_range(-4000, 4000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->y_low), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->y_high), 100);
    gtk_box_append(GTK_BOX(side_content), inline_field("Min °C", ui->y_low));
    gtk_box_append(GTK_BOX(side_content), inline_field("Max °C", ui->y_high));
    g_signal_connect(ui->auto_y, "toggled", G_CALLBACK(on_range), ui);
    g_signal_connect(ui->y_low, "value-changed", G_CALLBACK(on_range), ui);
    g_signal_connect(ui->y_high, "value-changed", G_CALLBACK(on_range), ui);
    on_range(NULL, ui);
    gtk_box_append(GTK_BOX(analysis), plot);

    facts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_add_css_class(facts, "fact-strip");
    ui->elapsed_value = gtk_label_new("00:00:00");
    ui->samples_value = gtk_label_new("0");
    gtk_widget_add_css_class(ui->elapsed_value, "fact-value");
    gtk_widget_add_css_class(ui->samples_value, "fact-value");
    gtk_box_append(GTK_BOX(facts), fact_column("Elapsed", ui->elapsed_value));
    gtk_box_append(GTK_BOX(facts), fact_column("Samples", ui->samples_value));
    {
        GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(GTK_BOX(facts), spacer);
    }
    ui->spike_check = gtk_check_button_new_with_label("Exclude spikes");
    threshold = gtk_adjustment_new(TMD_DEFAULT_SPIKE_THRESHOLD_C, 0.5, 500.0, 0.5, 5.0, 0.0);
    ui->spike_spin = gtk_spin_button_new(threshold, 0.5, 1);
    gtk_widget_set_size_request(ui->spike_spin, 72, 28);
    ui->spike_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_valign(ui->spike_box, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(ui->spike_box), ui->spike_check);
    gtk_box_append(GTK_BOX(ui->spike_box), inline_field("°C", ui->spike_spin));
    gtk_box_append(GTK_BOX(facts), ui->spike_box);
    gtk_box_append(GTK_BOX(side_content), facts);

    status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status, "status-bar");
    ui->state_label = gtk_label_new("● IDLE    DEVICE TMD-56");
    gtk_widget_add_css_class(ui->state_label, "status-offline");
    gtk_widget_set_hexpand(ui->state_label, TRUE);
    gtk_widget_set_valign(ui->state_label, GTK_ALIGN_CENTER);
    gtk_label_set_xalign(GTK_LABEL(ui->state_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(ui->state_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(status), ui->state_label);
    gtk_box_append(GTK_BOX(column), status);
    /* Move recording controls into the resizable session inspector. */
    g_object_ref(ui->session_box);
    gtk_box_remove(GTK_BOX(ui->action_row), ui->session_box);
    gtk_box_append(GTK_BOX(side_content), ui->session_box); g_object_unref(ui->session_box);
    g_object_ref(ui->log_button);
    gtk_box_remove(GTK_BOX(ui->action_row), ui->log_button);
    gtk_box_append(GTK_BOX(side_content), ui->log_button); g_object_unref(ui->log_button);
    g_object_ref(ui->session_hint);
    gtk_box_remove(GTK_BOX(ui->action_row), ui->session_hint);
    gtk_box_append(GTK_BOX(side_content), ui->session_hint); g_object_unref(ui->session_hint);
    gtk_widget_set_margin_start(ui->session_box, 0);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(ui->session_box), GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_halign(gtk_widget_get_first_child(ui->session_box), GTK_ALIGN_START);
    ui->log_path = gtk_label_new("Start logging to create a CSV file");
    gtk_label_set_wrap(GTK_LABEL(ui->log_path), FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(ui->log_path), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(ui->log_path), 48);
    gtk_label_set_selectable(GTK_LABEL(ui->log_path), TRUE);
    gtk_label_set_xalign(GTK_LABEL(ui->log_path), 0);
    gtk_widget_add_css_class(ui->log_path, "path-label");
    gtk_box_append(GTK_BOX(side_content), fact_column("Recording location", ui->log_path));
    ui->export_button = control_button("Export session CSV…", NULL);
    gtk_box_append(GTK_BOX(side_content), ui->export_button);
    g_signal_connect(ui->export_button, "clicked", G_CALLBACK(on_export), ui);
    GtkWidget *review = control_button("Open historical recording…", NULL);
    ui->review_button = review;
    gtk_box_append(GTK_BOX(side_content), review);
    g_signal_connect(review, "clicked", G_CALLBACK(on_open_file), ui);
    GtkWidget *help = gtk_label_new("Device setup\nSet the meter to °C. Hold Power and T1−T2 at startup until channel 0 appears.\n\nPlot navigation\nScroll to zoom · drag to pan\nHover to inspect a recorded sample.\nKeyboard: Tab to plot, +/− zoom, arrows pan, Home fits all.");
    gtk_label_set_wrap(GTK_LABEL(help), TRUE);
    gtk_label_set_xalign(GTK_LABEL(help), 0);
    gtk_widget_add_css_class(help, "path-label");
    GtkWidget *help_expander = gtk_expander_new("Device setup & plot help");
    gtk_expander_set_child(GTK_EXPANDER(help_expander), help);
    gtk_box_append(GTK_BOX(side_content), help_expander);
    ui->notes = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->notes), "Experiment notes (CSV export)");
    gtk_widget_set_tooltip_text(ui->notes, "Included as metadata when you export this session.");
    gtk_box_append(GTK_BOX(side_content), fact_column("Experiment notes", ui->notes));
    GtkWidget *session_title = gtk_label_new("Session");
    gtk_widget_add_css_class(session_title, "instrument-section-title");
    gtk_widget_set_halign(session_title, GTK_ALIGN_START);
    ui->recording_state = gtk_label_new("Ready to record");
    gtk_label_set_xalign(GTK_LABEL(ui->recording_state), 0);
    gtk_box_prepend(GTK_BOX(side_content), ui->recording_state);
    gtk_box_prepend(GTK_BOX(side_content), session_title);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(facts), GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *plot_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *range_fields[] = {ui->auto_y, gtk_widget_get_parent(ui->y_low), gtk_widget_get_parent(ui->y_high), ui->spike_box};
    for (guint i = 0; i < G_N_ELEMENTS(range_fields); i++) {
        GtkWidget *field = range_fields[i];
        g_object_ref(field);
        gtk_box_remove(GTK_BOX(gtk_widget_get_parent(field)), field);
        gtk_box_append(GTK_BOX(plot_settings), field); g_object_unref(field);
    }
    GtkWidget *settings_expander = gtk_expander_new("Plot settings & spike filter");
    gtk_expander_set_child(GTK_EXPANDER(settings_expander), plot_settings);
    gtk_box_append(GTK_BOX(side_content), settings_expander);

    /* Instrument workflow: a full-width plot and a resizable session dock. */
    GtkWidget *overview = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *recording = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *utilities = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *session_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *notes_help = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *notes_expander = gtk_expander_new("Notes & device help");
    gtk_widget_add_css_class(overview, "dock-overview");
    gtk_widget_add_css_class(recording, "dock-recording");
    gtk_widget_add_css_class(utilities, "dock-utilities");
    move_box_child(session_title, overview);
    move_box_child(ui->recording_state, overview);
    move_box_child(facts, overview);
    move_box_child(ui->session_box, session_controls);
    gtk_widget_set_hexpand(ui->session_box, TRUE);
    move_box_child(ui->log_button, session_controls);
    gtk_widget_set_valign(ui->log_button, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(recording), session_controls);
    move_box_child(ui->session_hint, recording);
    move_box_child(gtk_widget_get_parent(ui->log_path), recording);
    move_box_child(ui->export_button, utilities);
    move_box_child(ui->review_button, utilities);
    move_box_child(gtk_widget_get_parent(ui->notes), notes_help);
    move_box_child(help_expander, notes_help);
    gtk_expander_set_child(GTK_EXPANDER(notes_expander), notes_help);
    gtk_box_append(GTK_BOX(utilities), notes_expander);
    move_box_child(settings_expander, utilities);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(side_content), GTK_ORIENTATION_HORIZONTAL);
    gtk_box_set_spacing(GTK_BOX(side_content), 24);
    gtk_widget_set_size_request(overview, 180, -1);
    gtk_widget_set_size_request(recording, 350, -1);
    gtk_widget_set_size_request(utilities, 300, -1);
    gtk_widget_set_hexpand(recording, TRUE);
    gtk_box_append(GTK_BOX(side_content), overview);
    gtk_box_append(GTK_BOX(side_content), recording);
    gtk_box_append(GTK_BOX(side_content), utilities);



    ui->connection = gtk_label_new("Disconnected");
    gtk_widget_add_css_class(ui->connection, "connection-label");
    gtk_box_prepend(GTK_BOX(ui->action_row), ui->connection);
    ui->refresh_button = control_button("↻ Refresh ports", NULL);
    gtk_box_append(GTK_BOX(ui->port_box), ui->refresh_button);
    g_signal_connect(ui->refresh_button, "clicked", G_CALLBACK(on_refresh), ui);


    gtk_widget_set_tooltip_text(ui->spike_spin, "Spike threshold in degrees Celsius.");
    gtk_widget_set_tooltip_text(ui->spike_check,
                                "Hide samples whose temperature jumps by more than the threshold. "
                                "Logged values are not changed.");

    g_signal_connect(ui->port_dropdown, "notify::selected", G_CALLBACK(on_port_selected), ui);
    g_signal_connect(ui->source_dropdown, "notify::selected", G_CALLBACK(on_source_changed), ui);
    g_signal_connect(ui->scenario_dropdown, "notify::selected", G_CALLBACK(on_scenario_changed), ui);
    g_signal_connect(ui->interval_dropdown, "notify::selected", G_CALLBACK(on_interval_changed), ui);
    g_signal_connect(ui->speed_dropdown, "notify::selected", G_CALLBACK(on_speed_changed), ui);
    for (view = 0; view < 6; view++) {
        g_signal_connect(ui->view_buttons[view], "toggled", G_CALLBACK(on_view_toggled), ui);
    }
    g_signal_connect(ui->start_button, "clicked", G_CALLBACK(on_start), ui);
    g_signal_connect(ui->stop_button, "clicked", G_CALLBACK(on_stop), ui);
    g_signal_connect(ui->play_button, "clicked", G_CALLBACK(on_play), ui);
    g_signal_connect(ui->pause_button, "clicked", G_CALLBACK(on_pause), ui);
    g_signal_connect(ui->restart_button, "clicked", G_CALLBACK(on_restart), ui);
    g_signal_connect(ui->file_button, "clicked", G_CALLBACK(on_open_file), ui);
    g_signal_connect(ui->session_entry, "changed", G_CALLBACK(on_session_changed), ui);
    g_signal_connect(ui->log_button, "clicked", G_CALLBACK(on_log_clicked), ui);
    g_signal_connect(ui->spike_check, "toggled", G_CALLBACK(on_spike_toggled), ui);
    g_signal_connect(ui->spike_spin, "value-changed", G_CALLBACK(on_threshold_changed), ui);
    g_signal_connect(ui->about_button, "clicked", G_CALLBACK(on_about), ui);
    g_signal_connect(ui->window, "close-request", G_CALLBACK(on_close_request), ui);
    g_signal_connect(ui->window, "destroy", G_CALLBACK(on_window_destroy), ui);

    apply_css();
    ui->history_source = APP_SOURCE_TMD56;
    ui->device_choice = TMD_UI_DEVICE_TMD56;
    app_set_source(app, APP_SOURCE_TMD56);
    app_set_sample_interval(app, 0.5);
    refresh_ports(ui, true);
    refresh_ui(ui);
    /* One poll timer for the life of the window. Acquisition does not add another. */
    if (ui->timer_id == 0u) {
        ui->timer_id = g_timeout_add(100, on_tick, ui);
    }
    gtk_window_present(GTK_WINDOW(ui->window));
}

int main_window_run(App *app, int argc, char **argv)
{
    GtkApplication *gtk_app = gtk_application_new("com.tmd56.Monitor", G_APPLICATION_DEFAULT_FLAGS);
    int status;

    g_set_prgname("tmd56-monitor");
    g_set_application_name("TMD-56 Temperature Logger");
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), app);
    status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
