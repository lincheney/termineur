#include <gtk/gtk.h>
#include <vte/vte.h>
#include "config.h"

const gint ERROR_EXIT_CODE = 127;
#define DEFAULT_SHELL "/bin/sh"

void term_exited(VteTerminal* terminal, gint status, GtkWidget* container) {
    gtk_widget_destroy(GTK_WIDGET(container));
}

void term_spawn_callback(GtkWidget* terminal, GPid pid, GError *error, gpointer user_data) {
    if (error) {
        fprintf(stderr, "Could not start terminal: %s\n", error->message);
        exit(ERROR_EXIT_CODE);
    }
}

GtkWidget* make_terminal(GtkWidget* grid, int argc, char** argv) {
    GtkWidget *terminal;
    GtkWidget *scrollbar;
    GtkWidget *label;

    terminal = vte_terminal_new();
    label = gtk_label_new("");
    g_object_set_data(G_OBJECT(terminal), "label", label);
    g_object_set_data(G_OBJECT(grid), "terminal", terminal);
    gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(terminal));

    configure_terminal(terminal);

    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), grid);
    g_object_set(terminal, "expand", 1, NULL);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal), VTE_CURSOR_BLINK_OFF);

    // populate palette
    vte_terminal_set_colors(VTE_TERMINAL(terminal), palette+1, palette, palette+2, PALETTE_SIZE);

    char **args;
    char *fallback_args[] = {NULL, NULL};
    char *user_shell = NULL;

    if (argc > 1) {
        args = argv + 1;
    } else if (default_args) {
        args = default_args;
    } else {
        user_shell = vte_get_user_shell();
        fallback_args[0] = user_shell ? user_shell : DEFAULT_SHELL;
        args = fallback_args;
    }

    vte_terminal_spawn_async(
            VTE_TERMINAL(terminal),
            VTE_PTY_DEFAULT, //pty flags
            NULL, // pwd
            args, // args
            NULL, // env
            G_SPAWN_SEARCH_PATH, // g spawn flags
            NULL, // child setup
            NULL, // child setup data
            NULL, // child setup data destroy
            -1, // timeout
            NULL, // cancellable
            (VteTerminalSpawnAsyncCallback) term_spawn_callback, // callback
            NULL // user data
    );
    free(user_shell);

    if (show_scrollbar) {
        scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal)));
        gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(scrollbar));
    }

    return terminal;
}
