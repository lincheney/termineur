#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <vte/vte.h>
#include <unistd.h>
#include <fcntl.h>
#include "config.h"

const gint ERROR_EXIT_CODE = 127;

#define GET_ENV(x) (getenv("POPUP_TERM_" x))

#define DEFAULT_SHELL "/bin/sh"

void term_exited(VteTerminal* terminal, gint status, gint* dest)
{
    if (WIFEXITED(status)) {
        *dest = WEXITSTATUS(status);
    } else {
        *dest = ERROR_EXIT_CODE;
    }
    gtk_main_quit();
}

gboolean key_pressed(GtkWidget* terminal, GdkEventKey* event, gpointer data)
{
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    gboolean handled = FALSE;
    for (int i = 0; i < keyboard_shortcuts->len; i++) {
        KeyCombo* combo = &g_array_index(keyboard_shortcuts, KeyCombo, i);
        if (combo->key == event->keyval && combo->modifiers == modifiers) {
            combo->callback(VTE_TERMINAL(terminal), combo->data);
            handled = TRUE;
        }
    }
    return handled;
}

void term_spawn_callback(GtkWidget* terminal, GPid pid, GError *error, gpointer user_data)
{
    if (error) {
        fprintf(stderr, "Could not start terminal: %s\n", error->message);
        exit(ERROR_EXIT_CODE);
    }
}

int main(int argc, char *argv[])
{
    int status = 0;
    GtkWidget *window;
    GtkWidget *terminal;
    GtkWidget *grid;
    GtkWidget *scrollbar;

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "delete-event", gtk_main_quit, NULL);

    grid = gtk_grid_new();

    terminal = vte_terminal_new();
    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), &status);
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
    g_strfreev(default_args);
    free(user_shell);

    g_signal_connect(terminal, "key-press-event", G_CALLBACK(key_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(terminal));

    if (show_scrollbar) {
        scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal)));
        gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(scrollbar));
    }

    gtk_widget_show_all(window);
    gtk_main();

    exit(status);
}
