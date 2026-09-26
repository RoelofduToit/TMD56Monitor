#include "ui/main_window.h"

#include "ui/live_plot.h"
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

static const char *const SOURCE_ITEMS[] = {
    "Simulator",
    "Replay File",
    "TMD-56",
    NULL
};

static const char *const SCENARIO_ITEMS[] = {
    "Stable",
    "Heating",
    "Cooling",
    "Thermal gradient",
    "Noise",
    "Occasional spike",
    NULL
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
    "1 MIN",
    "5 MIN",
    "10 MIN",
    "30 MIN",
    "60 MIN",
    "ALL"
};

static const char *const INTERVAL_ITEMS[] = {
    "250 ms",
    "500 ms",
    "1 s",
    "2 s",
    NULL
};

static const double SPEED_VALUES[] = {0.5, 1.0, 2.0, 5.0, 10.0};
static const double WINDOW_VALUES[] = {60.0, 300.0, 600.0, 1800.0, 3600.0, 0.0};
static const double INTERVAL_VALUES[] = {0.25, 0.5, 1.0, 2.0};

typedef struct {
    App *app;
    GtkWidget *window;
    GtkWidget *source_dropdown;
    GtkWidget *scenario_box;
    GtkWidget *scenario_dropdown;
    GtkWidget *interval_box;
    GtkWidget *interval_dropdown;
    GtkWidget *context_row;
    GtkWidget *file_button;
    GtkWidget *file_label;
    GtkWidget *speed_box;
    GtkWidget *speed_dropdown;
    GtkWidget *port_box;
    GtkWidget *port_entry;
    GtkWidget *banner;
    GtkWidget *start_button;
    GtkWidget *stop_button;
    GtkWidget *pause_button;
    GtkWidget *restart_button;
    GtkWidget *mode_label;
    GtkWidget *acquire_label;
    GtkWidget *about_button;
    GtkWidget *t1_value;
    GtkWidget *t2_value;
    GtkWidget *dt_value;
    GtkWidget *spike_mark;
    GtkWidget *t1_stats;
    GtkWidget *t2_stats;
    GtkWidget *samples_value;
    GtkWidget *elapsed_value;
    GtkWidget *interval_value;
    GtkWidget *view_buttons[6];
    GtkWidget *drawing;
    GtkWidget *session_entry;
    GtkWidget *log_start;
    GtkWidget *log_stop;
    GtkWidget *state_label;
    GtkWidget *spike_check;
    GtkWidget *spike_spin;
    LivePlot plot;
    guint timer_id;
    bool syncing;
} Ui;

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

static GtkWidget *control_button(const char *text, gboolean primary)
{
    GtkWidget *button = gtk_button_new_with_label(text);
    gtk_widget_add_css_class(button, primary ? "primary-control" : "secondary-control");
    return button;
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

static GtkWidget *make_channel(const char *name, gboolean rule, GtkWidget **number_out)
{
    GtkWidget *channel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title = gtk_label_new(name);
    GtkWidget *number = gtk_label_new("    ----");
    GtkWidget *unit = gtk_label_new("°C");

    gtk_widget_add_css_class(channel, "measurement-channel");
    if (rule) {
        gtk_widget_add_css_class(channel, "with-rule");
    }
    gtk_widget_set_hexpand(channel, TRUE);
    gtk_widget_add_css_class(title, "measurement-name");
    gtk_widget_add_css_class(number, "measurement-number");
    gtk_widget_add_css_class(unit, "measurement-unit");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_widget_set_halign(number, GTK_ALIGN_START);
    gtk_widget_set_halign(unit, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(number), TRUE);
    gtk_label_set_width_chars(GTK_LABEL(number), 8);
    gtk_label_set_xalign(GTK_LABEL(number), 0.0f);
    gtk_box_append(GTK_BOX(channel), title);
    gtk_box_append(GTK_BOX(channel), number);
    gtk_box_append(GTK_BOX(channel), unit);
    *number_out = number;
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

static void format_channel_stats(char *buf, size_t len, const char *channel, bool have,
                                 double min_v, double max_v, double sum, unsigned long count)
{
    char min_text[16];
    char max_text[16];
    char avg_text[16];

    if (!have || count == 0ul) {
        snprintf(buf, len, "%s   MIN     —   MAX     —   AVG     —", channel);
        return;
    }
    format_tenth(min_text, sizeof min_text, min_v);
    format_tenth(max_text, sizeof max_text, max_v);
    format_tenth(avg_text, sizeof avg_text, sum / (double)count);
    snprintf(buf, len, "%s   MIN %s   MAX %s   AVG %s", channel, min_text, max_text, avg_text);
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

static void update_file_label(Ui *ui)
{
    const char *path = app_replay_path(ui->app);
    size_t count = app_replay_sample_count(ui->app);
    if (path == NULL || path[0] == '\0') {
        gtk_label_set_text(GTK_LABEL(ui->file_label), "NO FILE");
        return;
    }
    char *base = g_path_get_basename(path);
    char text[160];
    snprintf(text, sizeof text, "%s (%zu)", base, count);
    gtk_label_set_text(GTK_LABEL(ui->file_label), text);
    g_free(base);
}

static const char *overlay_for(const Ui *ui)
{
    if (history_buffer_count(app_history(ui->app)) > 0u) {
        return NULL;
    }
    if (app_source(ui->app) == APP_SOURCE_TMD56) {
        return "Experimental / unverified — no temperatures are decoded";
    }
    if (!app_is_acquiring(ui->app)) {
        return "START to acquire";
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

static void refresh_ui(Ui *ui)
{
    const MeasurementStats *stats = app_stats(ui->app);
    const TemperatureMeasurement *latest = app_latest(ui->app);
    bool running = app_is_acquiring(ui->app);
    bool logging = app_is_logging(ui->app);
    bool simulator = app_source(ui->app) == APP_SOURCE_SIMULATOR;
    bool replay = app_source(ui->app) == APP_SOURCE_REPLAY;
    bool tmd = app_source(ui->app) == APP_SOURCE_TMD56;
    bool show_numbers = latest != NULL && latest->valid;
    bool spike = show_numbers && latest->suspicious;
    char text[192];
    char grouped[48];
    const char *session;
    const char *log_path;
    const char *interval_text;
    AppStatusLevel level = app_status_level(ui->app);
    size_t samples = history_buffer_count(app_history(ui->app));

    if (show_numbers) {
        char t1_text[16];
        char t2_text[16];
        char delta_text[16];
        measurement_format_triple(t1_text, sizeof t1_text, t2_text, sizeof t2_text,
                                  delta_text, sizeof delta_text, latest->t1, latest->t2,
                                  true);
        set_label(ui->t1_value, t1_text);
        set_label(ui->t2_value, t2_text);
        set_label(ui->dt_value, delta_text);
    } else {
        set_label(ui->t1_value, "    ----");
        set_label(ui->t2_value, "    ----");
        set_label(ui->dt_value, "    ----");
    }
    gtk_widget_set_visible(ui->spike_mark, spike);

    format_channel_stats(text, sizeof text, "T1", stats->have_t1,
                         measurement_display_tenth(stats->t1_min),
                         measurement_display_tenth(stats->t1_max),
                         measurement_display_tenth(stats->have_t1 && stats->t1_count > 0
                                                       ? stats->t1_sum / (double)stats->t1_count
                                                       : 0.0),
                         stats->have_t1 ? 1ul : 0ul);
    set_label(ui->t1_stats, text);
    format_channel_stats(text, sizeof text, "T2", stats->have_t2,
                         measurement_display_tenth(stats->t2_min),
                         measurement_display_tenth(stats->t2_max),
                         measurement_display_tenth(stats->have_t2 && stats->t2_count > 0
                                                       ? stats->t2_sum / (double)stats->t2_count
                                                       : 0.0),
                         stats->have_t2 ? 1ul : 0ul);
    set_label(ui->t2_stats, text);

    format_grouped(grouped, sizeof grouped, (unsigned long)samples);
    set_label(ui->samples_value, grouped);
    format_clock(text, sizeof text, latest != NULL ? latest->elapsed_seconds : 0.0);
    set_label(ui->elapsed_value, text);

    if (replay) {
        interval_text = selected_text(ui->speed_dropdown, SPEED_ITEMS, 5);
    } else if (tmd) {
        interval_text = "—";
    } else {
        interval_text = selected_text(ui->interval_dropdown, INTERVAL_ITEMS, 4);
    }
    set_label(ui->interval_value, interval_text);

    if (tmd) {
        set_label(ui->mode_label, "TMD-56");
    } else if (replay) {
        set_label(ui->mode_label, "REPLAY");
    } else {
        set_label(ui->mode_label, "SIMULATOR");
    }

    {
        const char *state_word = "IDLE";
        const char *state_class = "status-offline";
        char line[384];
        char logging_part[160];

        logging_part[0] = '\0';
        if (tmd && running) {
            state_word = "UNVERIFIED";
            state_class = "status-warning";
        } else if (replay && running && app_replay_paused(ui->app)) {
            state_word = "PAUSED";
            state_class = "status-warning";
        } else if (replay && running) {
            state_word = "REPLAYING";
            state_class = "status-connected";
        } else if (replay && app_replay_finished(ui->app)) {
            state_word = "COMPLETE";
            state_class = "status-offline";
        } else if (running) {
            state_word = "ACQUIRING";
            state_class = "status-connected";
        } else if (tmd) {
            state_word = "OFFLINE";
        }
        set_label(ui->acquire_label, tmd && running ? "● UNVERIFIED" :
                                      replay && running && app_replay_paused(ui->app) ? "● PAUSED" :
                                      replay && running ? "● REPLAYING" :
                                      replay && app_replay_finished(ui->app) ? "● COMPLETE" :
                                      running ? "● ACQUIRING" :
                                      tmd ? "● OFFLINE" : "● IDLE");
        set_status_class(ui->acquire_label, state_class);

        log_path = app_log_path(ui->app);
        if (logging && log_path != NULL && log_path[0] != '\0') {
            char *base = g_path_get_basename(log_path);
            snprintf(logging_part, sizeof logging_part, "    LOGGING %s", base);
            g_free(base);
            state_class = "status-recording";
        }
        if (level == APP_LEVEL_ERROR) {
            snprintf(line, sizeof line,
                     "● %s    %s    SOURCE %s    %s SAMPLES    %s%s",
                     state_word, app_status(ui->app),
                     tmd ? "TMD-56" : replay ? "REPLAY" : "SIMULATOR",
                     grouped, interval_text, logging_part);
            if (!logging) {
                state_class = "status-warning";
            }
        } else if (replay) {
            const char *path = app_replay_path(ui->app);
            const char *summary = app_replay_summary(ui->app);
            char *base = (path != NULL && path[0] != '\0') ? g_path_get_basename(path) : NULL;
            const char *shown = (base != NULL && base[0] != '\0') ? base : "—";
            bool complete = !running && app_replay_finished(ui->app);
            if (complete) {
                snprintf(line, sizeof line, "● COMPLETE    FILE %s    %s SAMPLES",
                         shown, grouped);
            } else if (running) {
                snprintf(line, sizeof line, "● %s    FILE %s    %s SAMPLES    %s",
                         state_word, shown, grouped, interval_text);
            } else if (summary != NULL && summary[0] != '\0') {
                snprintf(line, sizeof line, "● IDLE    FILE %s    %s SAMPLES    %s",
                         shown, grouped, summary);
            } else {
                snprintf(line, sizeof line, "● IDLE    FILE %s    %s SAMPLES",
                         shown, grouped);
            }
            g_free(base);
        } else {
            snprintf(line, sizeof line, "● %s    SOURCE %s    %s SAMPLES    %s%s",
                     state_word,
                     tmd ? "TMD-56" : "SIMULATOR",
                     grouped, interval_text, logging_part);
        }
        set_label(ui->state_label, line);
        set_status_class(ui->state_label, state_class);
    }

    gtk_widget_set_visible(ui->scenario_box, simulator);
    gtk_widget_set_visible(ui->interval_box, simulator);
    gtk_widget_set_visible(ui->context_row, replay || tmd);
    gtk_widget_set_visible(ui->file_button, replay);
    gtk_widget_set_visible(ui->file_label, replay);
    gtk_widget_set_visible(ui->speed_box, replay);
    gtk_widget_set_visible(ui->pause_button, replay);
    gtk_widget_set_visible(ui->restart_button, replay);
    gtk_widget_set_visible(ui->port_box, tmd);
    gtk_widget_set_visible(ui->banner, tmd);

    gtk_button_set_label(GTK_BUTTON(ui->start_button),
                         tmd ? "CONNECT" : replay ? "PLAY" : "START");
    gtk_widget_set_sensitive(ui->start_button, !running || (replay && app_replay_paused(ui->app)));
    gtk_widget_set_sensitive(ui->stop_button, running);
    gtk_widget_set_sensitive(ui->source_dropdown, !running);
    gtk_widget_set_sensitive(ui->file_button, !running);
    gtk_widget_set_sensitive(ui->port_entry, !running);
    gtk_widget_set_sensitive(ui->pause_button, running && replay);
    gtk_widget_set_sensitive(ui->restart_button,
                             replay && (running || app_replay_finished(ui->app)));
    gtk_widget_set_sensitive(ui->spike_check, !tmd);
    gtk_widget_set_sensitive(ui->spike_spin, !tmd);

    ui->syncing = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->pause_button), app_replay_paused(ui->app));
    gtk_button_set_label(GTK_BUTTON(ui->pause_button),
                         app_replay_paused(ui->app) ? "RESUME" : "PAUSE");
    ui->syncing = false;

    session = gtk_editable_get_text(GTK_EDITABLE(ui->session_entry));
    gtk_widget_set_sensitive(ui->session_entry, !logging);
    gtk_widget_set_sensitive(ui->log_start,
                             running && !replay && !logging && session_name_usable(session));
    gtk_widget_set_sensitive(ui->log_stop, logging);
    update_file_label(ui);
}

static gboolean on_tick(gpointer user_data)
{
    Ui *ui = user_data;
    int produced = app_poll(ui->app);
    refresh_ui(ui);
    if (produced > 0) {
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
        gtk_drop_down_set_selected(GTK_DROP_DOWN(object), (guint)app_source(ui->app));
        ui->syncing = false;
        return;
    }
    app_set_source(ui->app, (AppSourceKind)selected);
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
    app_set_scenario(ui->app, (SimulatorScenario)selected);
    refresh_ui(ui);
}

static void on_interval_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    Ui *ui = user_data;
    guint selected;
    (void)pspec;
    selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION || selected > 3u) {
        return;
    }
    app_set_sample_interval(ui->app, INTERVAL_VALUES[selected]);
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
        ui->plot.window_seconds = WINDOW_VALUES[index];
    }
    gtk_widget_queue_draw(ui->drawing);
}

static void on_port_changed(GtkEditable *editable, gpointer user_data)
{
    Ui *ui = user_data;
    app_set_serial_port(ui->app, gtk_editable_get_text(editable));
    refresh_ui(ui);
}

static void on_start(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    char error[384];
    (void)button;
    if (app_source(ui->app) == APP_SOURCE_REPLAY && app_is_acquiring(ui->app) &&
        app_replay_paused(ui->app)) {
        app_replay_pause(ui->app, false);
        refresh_ui(ui);
        return;
    }
    if (app_start(ui->app, error, sizeof error) != 0) {
        refresh_ui(ui);
        return;
    }
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

static void on_pause(GtkToggleButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    if (ui->syncing) {
        return;
    }
    app_replay_pause(ui->app, gtk_toggle_button_get_active(button));
    refresh_ui(ui);
}

static void on_restart(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    (void)button;
    app_replay_restart(ui->app);
    refresh_ui(ui);
    gtk_widget_queue_draw(ui->drawing);
}

static void on_file_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Ui *ui = user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(dialog, result, &error);
    char *path;
    char message[384];

    if (file == NULL) {
        if (error != NULL &&
            !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
            app_report_error(ui->app, error->message);
            refresh_ui(ui);
        }
        g_clear_error(&error);
        g_object_unref(dialog);
        return;
    }

    path = g_file_get_path(file);
    g_object_unref(file);
    if (path == NULL) {
        app_report_error(ui->app, "Choose a local file");
        refresh_ui(ui);
        g_object_unref(dialog);
        return;
    }
    app_set_source(ui->app, APP_SOURCE_REPLAY);
    ui->syncing = true;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->source_dropdown), APP_SOURCE_REPLAY);
    ui->syncing = false;
    app_set_replay_file(ui->app, path, message, sizeof message);
    refresh_ui(ui);
    g_free(path);
    g_object_unref(dialog);
    gtk_widget_queue_draw(ui->drawing);
}

static void on_open_file(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    GtkFileFilter *filter = gtk_file_filter_new();
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    (void)button;

    gtk_file_filter_set_name(filter, "TMD export or CSV");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_filter_add_pattern(filter, "*.tsv");
    gtk_file_filter_add_pattern(filter, "*.csv");
    gtk_file_filter_add_pattern(filter, "*.TXT");
    gtk_file_filter_add_pattern(filter, "*.TSV");
    gtk_file_filter_add_pattern(filter, "*.CSV");
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_title(dialog, "Open TMD-56 export");
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filter);
    g_object_unref(filters);
    gtk_file_dialog_open(dialog, GTK_WINDOW(ui->window), NULL, on_file_ready, ui);
}

static void on_session_changed(GtkEditable *editable, gpointer user_data)
{
    (void)editable;
    refresh_ui(user_data);
}

static void on_log_start(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    char error[384];
    const char *session = gtk_editable_get_text(GTK_EDITABLE(ui->session_entry));
    (void)button;
    if (app_start_logging(ui->app, session, error, sizeof error) != 0) {
        refresh_ui(ui);
        return;
    }
    refresh_ui(ui);
}

static void on_log_stop(GtkButton *button, gpointer user_data)
{
    Ui *ui = user_data;
    (void)button;
    app_stop_logging(ui->app);
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
    load_plot_color(ui->drawing, "tmd_red", ui->plot.palette.t1, 0.843, 0.098, 0.125);
    load_plot_color(ui->drawing, "tmd_cyan", ui->plot.palette.t2, 0.235, 0.706, 0.839);
    load_plot_color(ui->drawing, "tmd_yellow", ui->plot.palette.mark, 0.898, 0.718, 0.184);
    ui->plot.have_palette = true;
    ui->plot.history = app_history(ui->app);
    ui->plot.hide_suspicious = app_spike_filter_enabled(ui->app) &&
                               app_source(ui->app) != APP_SOURCE_TMD56;
    ui->plot.overlay = overlay_for(ui);
    live_plot_draw(&ui->plot, cr, width, height);
}

static void stop_window_work(Ui *ui)
{
    if (ui->timer_id != 0u) {
        g_source_remove(ui->timer_id);
        ui->timer_id = 0u;
    }
    /* Stop acquisition and close the log before the process tears the window down. */
    app_stop(ui->app);
}

static gboolean on_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    stop_window_work(user_data);
    return FALSE;
}

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
                          "comments", "Dual-input temperature logger for simulation and replay.\n"
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
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/tmd56/Monitor/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
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
    GtkWidget *header_tools;
    GtkWidget *body;
    GtkWidget *rail;
    GtkWidget *column;
    GtkWidget *display;
    GtkWidget *channels;
    GtkWidget *stats_row;
    GtkWidget *plot;
    GtkWidget *plot_bar;
    GtkWidget *plot_title;
    GtkWidget *view_group;
    GtkWidget *facts;
    GtkWidget *controls;
    GtkWidget *status;
    GtkAdjustment *threshold;
    int view;

    ui->app = app;
    ui->plot.window_seconds = 300.0;
    ui->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(ui->window), "TMD-56 Temperature Logger");
    gtk_window_set_default_size(GTK_WINDOW(ui->window), 1280, 720);
    gtk_widget_set_size_request(ui->window, 1000, 650);
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
    line = gtk_label_new("DUAL INPUT TEMPERATURE LOGGER");
    gtk_widget_add_css_class(mark, "product-mark");
    gtk_widget_add_css_class(line, "product-line");
    gtk_widget_set_halign(mark, GTK_ALIGN_START);
    gtk_widget_set_halign(line, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(identity), mark);
    gtk_box_append(GTK_BOX(identity), line);

    ui->source_dropdown = make_dropdown(SOURCE_ITEMS, 0);
    gtk_widget_set_size_request(ui->source_dropdown, 148, 28);
    ui->scenario_dropdown = make_dropdown(SCENARIO_ITEMS, 0);
    gtk_widget_set_size_request(ui->scenario_dropdown, 168, 28);
    ui->scenario_box = inline_field("SCENARIO", ui->scenario_dropdown);
    ui->interval_dropdown = make_dropdown(INTERVAL_ITEMS, 1);
    gtk_widget_set_size_request(ui->interval_dropdown, 96, 28);
    ui->interval_box = inline_field("INTERVAL", ui->interval_dropdown);
    ui->mode_label = gtk_label_new("SIMULATOR");
    gtk_widget_add_css_class(ui->mode_label, "fact-value");
    ui->acquire_label = gtk_label_new("● IDLE");
    gtk_widget_add_css_class(ui->acquire_label, "status-offline");

    header_tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_valign(header_tools, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(header_tools), inline_field("SOURCE", ui->source_dropdown));
    gtk_box_append(GTK_BOX(header_tools), ui->scenario_box);
    gtk_box_append(GTK_BOX(header_tools), ui->interval_box);
    {
        GtkWidget *rule = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(rule, "toolbar-rule");
        gtk_box_append(GTK_BOX(header_tools), rule);
    }
    gtk_box_append(GTK_BOX(header_tools), ui->mode_label);
    gtk_box_append(GTK_BOX(header_tools), ui->acquire_label);
    ui->about_button = control_button("ABOUT", FALSE);
    gtk_box_append(GTK_BOX(header_tools), ui->about_button);

    gtk_box_append(GTK_BOX(header), identity);
    {
        GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(GTK_BOX(header), spacer);
    }
    gtk_box_append(GTK_BOX(header), header_tools);

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

    ui->context_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(ui->context_row, "context-row");
    ui->file_button = control_button("OPEN FILE", FALSE);
    ui->file_label = gtk_label_new("NO FILE");
    gtk_widget_add_css_class(ui->file_label, "fact-value");
    gtk_label_set_ellipsize(GTK_LABEL(ui->file_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(ui->file_label), 28);
    gtk_label_set_xalign(GTK_LABEL(ui->file_label), 0.0f);
    ui->speed_dropdown = make_dropdown(SPEED_ITEMS, 1);
    gtk_widget_set_size_request(ui->speed_dropdown, 84, 28);
    ui->speed_box = inline_field("SPEED", ui->speed_dropdown);
    ui->pause_button = gtk_toggle_button_new_with_label("PAUSE");
    ui->restart_button = control_button("RESTART", FALSE);
    gtk_widget_add_css_class(ui->pause_button, "secondary-control");
    ui->port_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->port_entry), "/dev/ttyUSB0");
    gtk_widget_set_size_request(ui->port_entry, 180, 28);
    ui->port_box = inline_field("PORT", ui->port_entry);
    ui->banner = gtk_label_new("UNVERIFIED  19200 8E1  decoder off");
    gtk_widget_add_css_class(ui->banner, "status-warning");
    gtk_box_append(GTK_BOX(ui->context_row), ui->file_button);
    gtk_box_append(GTK_BOX(ui->context_row), ui->file_label);
    gtk_box_append(GTK_BOX(ui->context_row), ui->speed_box);
    gtk_box_append(GTK_BOX(ui->context_row), ui->pause_button);
    gtk_box_append(GTK_BOX(ui->context_row), ui->restart_button);
    gtk_box_append(GTK_BOX(ui->context_row), ui->port_box);
    gtk_box_append(GTK_BOX(ui->context_row), ui->banner);
    gtk_box_append(GTK_BOX(column), ui->context_row);

    display = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(display, "measurement-display");
    gtk_widget_set_margin_start(display, 8);
    gtk_widget_set_margin_end(display, 8);
    gtk_widget_set_margin_top(display, 8);
    channels = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(channels), TRUE);
    gtk_box_append(GTK_BOX(channels), make_channel("T1", FALSE, &ui->t1_value));
    gtk_box_append(GTK_BOX(channels), make_channel("T2", TRUE, &ui->t2_value));
    gtk_box_append(GTK_BOX(channels), make_channel("T1−T2", TRUE, &ui->dt_value));
    stats_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(stats_row, "lcd-stats");
    ui->t1_stats = gtk_label_new("T1   MIN     —   MAX     —   AVG     —");
    ui->t2_stats = gtk_label_new("T2   MIN     —   MAX     —   AVG     —");
    ui->spike_mark = gtk_label_new("SPIKE");
    gtk_widget_add_css_class(ui->t1_stats, "stat-value");
    gtk_widget_add_css_class(ui->t2_stats, "stat-value");
    gtk_widget_add_css_class(ui->spike_mark, "spike-mark");
    gtk_widget_set_halign(ui->t1_stats, GTK_ALIGN_START);
    gtk_widget_set_halign(ui->t2_stats, GTK_ALIGN_START);
    gtk_widget_set_hexpand(ui->t2_stats, TRUE);
    gtk_label_set_xalign(GTK_LABEL(ui->t2_stats), 0.0f);
    gtk_box_append(GTK_BOX(stats_row), ui->t1_stats);
    gtk_box_append(GTK_BOX(stats_row), ui->t2_stats);
    gtk_box_append(GTK_BOX(stats_row), ui->spike_mark);
    gtk_box_append(GTK_BOX(display), channels);
    gtk_box_append(GTK_BOX(display), stats_row);
    gtk_box_append(GTK_BOX(column), display);

    plot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(plot, "plot-container");
    gtk_widget_set_vexpand(plot, TRUE);
    gtk_widget_set_margin_start(plot, 8);
    gtk_widget_set_margin_end(plot, 8);
    gtk_widget_set_margin_top(plot, 8);
    gtk_widget_set_margin_bottom(plot, 8);
    plot_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(plot_bar, "plot-toolbar");
    plot_title = gtk_label_new("LIVE TEMPERATURE");
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
    gtk_box_append(GTK_BOX(plot_bar), plot_title);
    gtk_box_append(GTK_BOX(plot_bar), inline_field("VIEW", view_group));
    ui->drawing = gtk_drawing_area_new();
    gtk_widget_add_css_class(ui->drawing, "plot-canvas");
    gtk_widget_set_vexpand(ui->drawing, TRUE);
    gtk_widget_set_hexpand(ui->drawing, TRUE);
    gtk_widget_set_size_request(ui->drawing, -1, 220);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(ui->drawing), on_draw, ui, NULL);
    gtk_box_append(GTK_BOX(plot), plot_bar);
    gtk_box_append(GTK_BOX(plot), ui->drawing);
    gtk_box_append(GTK_BOX(column), plot);

    facts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(facts, "fact-strip");
    ui->session_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->session_entry), "Session name");
    gtk_widget_set_size_request(ui->session_entry, 200, 28);
    ui->elapsed_value = gtk_label_new("00:00:00");
    ui->samples_value = gtk_label_new("0");
    ui->interval_value = gtk_label_new("500 ms");
    gtk_widget_add_css_class(ui->elapsed_value, "fact-value");
    gtk_widget_add_css_class(ui->samples_value, "fact-value");
    gtk_widget_add_css_class(ui->interval_value, "fact-value");
    gtk_box_append(GTK_BOX(facts), fact_column("SESSION", ui->session_entry));
    gtk_box_append(GTK_BOX(facts), fact_column("ELAPSED", ui->elapsed_value));
    gtk_box_append(GTK_BOX(facts), fact_column("SAMPLES", ui->samples_value));
    gtk_box_append(GTK_BOX(facts), fact_column("INTERVAL", ui->interval_value));
    gtk_box_append(GTK_BOX(column), facts);

    controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(controls, "control-strip");
    ui->start_button = control_button("START", TRUE);
    ui->stop_button = control_button("STOP", FALSE);
    ui->log_start = control_button("START LOGGING", TRUE);
    ui->log_stop = control_button("STOP LOGGING", FALSE);
    ui->spike_check = gtk_check_button_new_with_label("EXCLUDE SPIKES");
    threshold = gtk_adjustment_new(TMD_DEFAULT_SPIKE_THRESHOLD_C, 0.5, 500.0, 0.5, 5.0, 0.0);
    ui->spike_spin = gtk_spin_button_new(threshold, 0.5, 1);
    gtk_widget_set_size_request(ui->spike_spin, 88, 28);
    gtk_box_append(GTK_BOX(controls), ui->start_button);
    gtk_box_append(GTK_BOX(controls), ui->stop_button);
    gtk_box_append(GTK_BOX(controls), ui->log_start);
    gtk_box_append(GTK_BOX(controls), ui->log_stop);
    {
        GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(GTK_BOX(controls), spacer);
    }
    gtk_box_append(GTK_BOX(controls), ui->spike_check);
    gtk_box_append(GTK_BOX(controls), inline_field("°C", ui->spike_spin));
    gtk_box_append(GTK_BOX(column), controls);

    status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status, "status-bar");
    ui->state_label = gtk_label_new("● IDLE    SOURCE SIMULATOR    0 SAMPLES    500 ms");
    gtk_widget_add_css_class(ui->state_label, "status-offline");
    gtk_widget_set_hexpand(ui->state_label, TRUE);
    gtk_widget_set_valign(ui->state_label, GTK_ALIGN_CENTER);
    gtk_label_set_xalign(GTK_LABEL(ui->state_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(ui->state_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(status), ui->state_label);
    gtk_box_append(GTK_BOX(column), status);

    gtk_widget_set_tooltip_text(
        ui->spike_check,
        "A sample is suspicious when the absolute change from the last accepted "
        "temperature exceeds the threshold. Raw logged values are never replaced.");
    gtk_widget_set_tooltip_text(
        ui->start_button,
        "Simulator and replay start acquisition. TMD-56 only opens the serial port; "
        "it does not decode temperatures.");

    g_signal_connect(ui->source_dropdown, "notify::selected", G_CALLBACK(on_source_changed), ui);
    g_signal_connect(ui->scenario_dropdown, "notify::selected", G_CALLBACK(on_scenario_changed), ui);
    g_signal_connect(ui->interval_dropdown, "notify::selected", G_CALLBACK(on_interval_changed), ui);
    g_signal_connect(ui->speed_dropdown, "notify::selected", G_CALLBACK(on_speed_changed), ui);
    for (view = 0; view < 6; view++) {
        g_signal_connect(ui->view_buttons[view], "toggled", G_CALLBACK(on_view_toggled), ui);
    }
    g_signal_connect(ui->port_entry, "changed", G_CALLBACK(on_port_changed), ui);
    g_signal_connect(ui->start_button, "clicked", G_CALLBACK(on_start), ui);
    g_signal_connect(ui->stop_button, "clicked", G_CALLBACK(on_stop), ui);
    g_signal_connect(ui->pause_button, "toggled", G_CALLBACK(on_pause), ui);
    g_signal_connect(ui->restart_button, "clicked", G_CALLBACK(on_restart), ui);
    g_signal_connect(ui->file_button, "clicked", G_CALLBACK(on_open_file), ui);
    g_signal_connect(ui->session_entry, "changed", G_CALLBACK(on_session_changed), ui);
    g_signal_connect(ui->log_start, "clicked", G_CALLBACK(on_log_start), ui);
    g_signal_connect(ui->log_stop, "clicked", G_CALLBACK(on_log_stop), ui);
    g_signal_connect(ui->spike_check, "toggled", G_CALLBACK(on_spike_toggled), ui);
    g_signal_connect(ui->spike_spin, "value-changed", G_CALLBACK(on_threshold_changed), ui);
    g_signal_connect(ui->about_button, "clicked", G_CALLBACK(on_about), ui);
    g_signal_connect(ui->window, "close-request", G_CALLBACK(on_close_request), ui);
    g_signal_connect(ui->window, "destroy", G_CALLBACK(on_window_destroy), ui);

    apply_css();
    app_set_sample_interval(app, 0.5);
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
