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

void activate(GtkApplication* app, GApplicationCommandLine* cmdline, gpointer data) {
    int argc;
    char** argv = g_application_command_line_get_arguments(cmdline, &argc);
    make_new_window_full(NULL, g_application_command_line_get_cwd(cmdline), argc, argv);
}

void startup(GtkApplication* app, gpointer data) {
    if (! config_filename) {
        config_filename = g_build_filename(g_get_user_config_dir(), "vte_terminal", "config.ini", NULL);
    }
    load_config();
}

gint handle_local_options(GApplication* app, GVariantDict* options, gpointer data) {
    char id_buffer[256];
    if (! id) {
        const char* display = gdk_display_get_name(gdk_display_get_default());
        snprintf(id_buffer, sizeof(id_buffer), "vte_terminal.x%s", display+1);
        id = id_buffer;
    } else if (strcmp(id, "") == 0) {
        id = NULL;
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

    app = gtk_application_new(NULL, G_APPLICATION_HANDLES_COMMAND_LINE);
    g_application_add_main_option_entries(G_APPLICATION(app), entries);
    g_signal_connect(app, "command-line", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app, "handle-local-options", G_CALLBACK(handle_local_options), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
