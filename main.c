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

void activate(GtkApplication* app, gpointer data) {
    new_window();
}

void startup(GtkApplication* app, gpointer data) {
    load_config("config.ini");
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    app = gtk_application_new ("org.gnome.example", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
