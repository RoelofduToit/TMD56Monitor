#ifdef NDEBUG
#undef NDEBUG
#endif
/* Exercise the actual GTK callbacks with an explicitly identified simulator.
 * Including the implementation keeps its internal widget state private to this
 * test translation unit rather than adding a production testing API. */
#define GLIB_DISABLE_DEPRECATION_WARNINGS
#define GTK_DISABLE_DEPRECATION_WARNINGS
#define GDK_DISABLE_DEPRECATION_WARNINGS
#include "ui/main_window.c"
#include <assert.h>
#include <glib/gstdio.h>
#include "sources/replay_parser.h"

static gboolean finish_wait(gpointer data)
{
    g_main_loop_quit(data);
    return G_SOURCE_REMOVE;
}

static void settle(unsigned milliseconds)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(milliseconds, finish_wait, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}

static void capture(Ui *ui, const char *name)
{
    const char *dir = g_getenv("TMD_UI_CAPTURE_DIR");
    if (!dir) return;
    GdkPaintable *paintable = gtk_widget_paintable_new(ui->window);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot),
                          gtk_widget_get_width(ui->window), gtk_widget_get_height(ui->window));
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    assert(node);
    GdkTexture *texture = gsk_renderer_render_texture(
        gtk_native_get_renderer(GTK_NATIVE(ui->window)), node, NULL);
    char *path = g_build_filename(dir, name, NULL);
    assert(gdk_texture_save_to_png(texture, path));
    g_free(path); g_object_unref(texture); gsk_render_node_unref(node); g_object_unref(paintable);
}

static GtkWidget *file_chooser(void)
{
    GListModel *windows = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
        GtkWidget *widget = g_list_model_get_item(windows, i);
        gboolean chooser = GTK_IS_FILE_CHOOSER(widget);
        g_object_unref(widget);
        if (chooser) return widget;
    }
    return NULL;
}

int main(void)
{
    g_set_prgname("tmd56-ui-validation");
    g_set_application_name("TMD-56 UI validation");
    char *directory = g_dir_make_tmp("tmd-ui-XXXXXX", NULL);
    const char *xdg_names[] = {"XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"};
    for (guint i = 0; i < G_N_ELEMENTS(xdg_names); i++) {
        char *path = g_build_filename(directory, xdg_names[i], NULL);
        g_mkdir_with_parents(path, 0700); g_setenv(xdg_names[i], path, TRUE); g_free(path);
    }
    if (!gtk_init_check()) { g_free(directory); return 77; }
    assert(directory); g_setenv("TMD56_DATA_DIR", directory, TRUE);
    App *app = app_create(); assert(app);
    GtkApplication *gtk_app = gtk_application_new("com.tmd56.UiTest", G_APPLICATION_NON_UNIQUE);
    assert(g_application_register(G_APPLICATION(gtk_app), NULL, NULL));
    on_activate(gtk_app, app); settle(250);
    GtkWindow *window = gtk_application_get_windows(gtk_app)->data;
    Ui *ui = g_object_get_data(G_OBJECT(window), "tmd-ui"); assert(ui);
    assert(app_source(app) == APP_SOURCE_TMD56);
    GtkWidget *t1_card = gtk_widget_get_parent(gtk_widget_get_parent(ui->t1_value));
    GtkWidget *t2_card = gtk_widget_get_parent(gtk_widget_get_parent(ui->t2_value));
    assert(gtk_widget_has_css_class(t1_card, "channel-t1"));
    assert(gtk_widget_has_css_class(t2_card, "channel-t2"));
    assert(gtk_widget_is_ancestor(ui->t1_stats, t1_card));
    assert(gtk_widget_is_ancestor(ui->t2_stats, t2_card));

    assert(!app_is_acquiring(app));
    on_refresh(NULL, ui);
    capture(ui, "disconnected.png");
    g_action_change_state(G_ACTION(ui->simulation_action), g_variant_new_boolean(TRUE));
    assert(app_source(app) == APP_SOURCE_SIMULATOR);
    assert(gtk_widget_get_visible(ui->sim_badge));
    for (guint i = 0; i < 6; i++) gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->scenario_dropdown), i);
    for (guint i = 0; i < tmd_interval_count(); i++) gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->interval_dropdown), i);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->interval_dropdown), 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->scenario_dropdown), 1);
    on_start(NULL, ui); assert(app_is_acquiring(app));
    on_log_clicked(NULL, ui); assert(!app_is_logging(app));
    assert(gtk_widget_get_visible(ui->session_hint));
    gtk_editable_set_text(GTK_EDITABLE(ui->session_entry), "UI validation");
    on_log_clicked(NULL, ui); assert(app_is_logging(app));
    assert(strstr(app_log_path(app), "SIMULATION_"));
    settle(1150); assert(history_buffer_count(app_history(app)) > 0);
    for (int i = 0; i < 6; i++) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->view_buttons[i]), TRUE);
        assert(ui->plot.window_seconds == WINDOW_VALUES[i]);
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->channel_checks[0]), FALSE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->channel_checks[2]), TRUE);
    assert(ui->plot.hide_t1 && ui->plot.show_delta);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->channel_checks[0]), TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->auto_y), FALSE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->y_low), 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->y_high), 50);
    assert(ui->plot.manual_y && ui->plot.y_min == 10 && ui->plot.y_max == 50);
    settle(50); on_zoom(NULL, 0, -1, ui); assert(ui->plot.manual_time);
    on_drag_begin(NULL, 100, 100, ui); on_drag(NULL, -30, 0, ui);
    on_motion(NULL, 200, 100, ui);
    assert(on_plot_key(NULL, GDK_KEY_plus, 0, 0, ui));
    assert(on_plot_key(NULL, GDK_KEY_Right, 0, 0, ui));
    assert(on_plot_key(NULL, GDK_KEY_Left, 0, 0, ui));
    assert(on_plot_key(NULL, GDK_KEY_Home, 0, 0, ui));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->spike_check), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spike_spin), 30);
    assert(app_spike_filter_enabled(app));
    on_fit(NULL, ui); assert(!ui->plot.manual_time && !ui->plot.manual_y);
    settle(100); capture(ui, "simulation.png");
    on_log_clicked(NULL, ui); assert(!app_is_logging(app));
    on_stop(NULL, ui); assert(!app_is_acquiring(app));
    assert(gtk_widget_get_sensitive(ui->export_button));
    assert(g_file_test(app_log_path(app), G_FILE_TEST_IS_REGULAR));
    assert(retain_session_first(ui)); /* Replacement requires an explicit choice. */
    settle(100);
    GListModel *alerts = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(alerts); i++) {
        GtkWindow *alert = g_list_model_get_item(alerts, i);
        if (alert != window && GTK_IS_DIALOG(alert)) gtk_window_close(alert);
        g_object_unref(alert);
    }
    settle(100); assert(!app_is_acquiring(app));

    gtk_editable_set_text(GTK_EDITABLE(ui->notes), "Heating verification");
    GString *csv = session_csv(ui);
    assert(strstr(csv->str, "# Source: SIMULATION"));
    assert(strstr(csv->str, "# Notes: Heating verification"));
    char *export_path = g_build_filename(directory, "export.csv", NULL);
    assert(g_file_set_contents(export_path, csv->str, csv->len, NULL));
    App *review = app_create();
    char export_error[384];
    assert(app_set_replay_file(review, export_path, export_error, sizeof export_error) == 0);
    assert(app_replay_sample_count(review) == history_buffer_count(app_history(app)));
    app_destroy(review); g_string_free(csv, TRUE); g_free(export_path);
        on_export(NULL, ui); settle(200);
    GtkWidget *chooser = file_chooser();
    assert(chooser);
    GFile *folder = g_file_new_for_path(directory);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), folder, NULL);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "saved-via-dialog.csv");
    g_object_unref(folder); settle(800);
    gtk_dialog_response(GTK_DIALOG(chooser), GTK_RESPONSE_ACCEPT); settle(500);
    char *saved = g_build_filename(directory, "saved-via-dialog.csv", NULL);
    assert(g_file_test(saved, G_FILE_TEST_IS_REGULAR)); assert(ui->exported); g_free(saved);
        gtk_window_set_default_size(window, 1000, 720); settle(200);
    assert(gtk_widget_get_width(GTK_WIDGET(window)) <= 1000);
    assert(gtk_widget_get_height(GTK_WIDGET(window)) <= 720);
    capture(ui, "compact.png");
    on_open_file(NULL, ui); settle(200);
    chooser = file_chooser(); assert(chooser);
    gtk_dialog_response(GTK_DIALOG(chooser), GTK_RESPONSE_CANCEL); settle(100);
    on_about(NULL, ui); settle(100);
    GListModel *windows = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(windows); i++) {
        GtkWindow *other = g_list_model_get_item(windows, i);
        if (GTK_IS_ABOUT_DIALOG(other)) gtk_window_destroy(other);
        g_object_unref(other);
    }
        g_action_change_state(G_ACTION(ui->simulation_action), g_variant_new_boolean(FALSE));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->source_dropdown), 1);
    assert(app_source(app) == APP_SOURCE_REPLAY);
    csv = session_csv(ui);
    assert(strstr(csv->str, "# Source: SIMULATION")); /* Provenance survives switching controls. */
    g_string_free(csv, TRUE);
    on_open_file(NULL, ui); settle(250);
    chooser = file_chooser(); assert(chooser);
    GFile *fixture = g_file_new_for_path(TMD_EXAMPLE_PATH);
    assert(gtk_file_chooser_set_file(GTK_FILE_CHOOSER(chooser), fixture, NULL));
    g_object_unref(fixture); settle(500);
    gtk_dialog_response(GTK_DIALOG(chooser), GTK_RESPONSE_ACCEPT); settle(250);
    assert(app_replay_sample_count(app) > 0);
    ui->allow_reset = true;
    refresh_ui(ui); on_play(NULL, ui); assert(app_is_acquiring(app));
    on_pause(NULL, ui); assert(app_replay_paused(app));
    for (int i = 0; i < 5; i++) gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->speed_dropdown), i);
    on_play(NULL, ui); assert(!app_replay_paused(app));
    on_restart(NULL, ui);
    assert(!ui->cursor_active);
    assert(strstr(gtk_label_get_text(GTK_LABEL(ui->cursor)), "hover to inspect"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->channel_checks[2]), FALSE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->spike_check), FALSE);
    gtk_window_set_default_size(window, 1360, 820);
    settle(3500); assert(app_replay_finished(app));
    assert(history_buffer_count(app_history(app)) == app_replay_sample_count(app));
    capture(ui, "replay.png");
    on_stop(NULL, ui);
    gtk_window_destroy(window); settle(20);
    g_object_unref(gtk_app); app_destroy(app); g_free(ui); g_free(directory);
    return 0;
}
