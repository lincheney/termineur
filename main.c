#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "window.h"

char* id = NULL;

void activate(GtkApplication* app, gpointer data) {
    make_new_window(NULL);
}

void startup(GtkApplication* app, gpointer data) {
    if (! config_filename) {
        config_filename = g_build_filename(g_get_user_config_dir(), "vte_terminal", "config.ini", NULL);
    }
    char* path = realpath(config_filename, NULL);
    g_free(config_filename);
    config_filename = path;
    load_config();
}

gint handle_local_options(GApplication* app, GVariantDict* options, gpointer data) {
    char id_buffer[256];
    if (! id) {
        const char* display = gdk_display_get_name(gdk_display_get_default());
        snprintf(id_buffer, sizeof(id_buffer), "vte_terminal.x%s", display+1);
        id = id_buffer;
    }
    g_application_set_application_id(app, id);
    return -1;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GOptionEntry entries[] = {
        {"id",     'i', 0, G_OPTION_ARG_STRING,   &id,              "Application ID", "ID"},
        {"config", 'c', 0, G_OPTION_ARG_FILENAME, &config_filename, "Config file",    "FILE"},
        {NULL}
    };

    app = gtk_application_new(NULL, G_APPLICATION_FLAGS_NONE);
    g_application_add_main_option_entries(G_APPLICATION(app), entries);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app, "handle-local-options", G_CALLBACK(handle_local_options), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
