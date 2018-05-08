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

#define DEFAULT_SHELL "/bin/sh"
const gint ERROR_EXIT_CODE = 127;

#define GET_ENV(x) (getenv("POPUP_TERM_" x))

#define PALETTE_SIZE (16)
GdkRGBA palette[PALETTE_SIZE] = {
    { 0., 0., 0., 1. }, // black
    { .5, 0., 0., 1. }, // red
    { 0., .5, 0., 1. }, // green
    { .5, .5, 0., 1. }, // yellow
    { 0., 0., .5, 1. }, // blue
    { .5, 0., .5, 1. }, // magenta
    { 0., .5, .5, 1. }, // cyan
    { 1., 1., 1., 1. }, // light grey
    { .5, .5, .5, 1. }, // dark grey
    { 1., 0., 0., 1. }, // light red
    { 0., 1., 0., 1. }, // light green
    { 1., 1., 0., 1. }, // light yellow
    { 0., 0., 1., 1. }, // light blue
    { 1., 0., 1., 1. }, // light magenta
    { 0., 1., 1., 1. }, // light cyan
    { 1., 1., 1., 1. }  // white
};

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
    if ((modifiers & GDK_CONTROL_MASK) && (modifiers & GDK_SHIFT_MASK)) {
        switch (event->keyval) {
            case GDK_KEY_V:
                vte_terminal_paste_clipboard(VTE_TERMINAL(terminal));
                return TRUE;
            case GDK_KEY_C:
                vte_terminal_copy_clipboard_format(VTE_TERMINAL(terminal), VTE_FORMAT_TEXT);
                return TRUE;
        }
    }
    return FALSE;
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

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_icon_name(GTK_WINDOW(window), "utilities-terminal");
    g_signal_connect(window, "delete-event", gtk_main_quit, NULL);

    terminal = vte_terminal_new();
    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), &status);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal), VTE_CURSOR_BLINK_OFF);

    // populate palette
    vte_terminal_set_colors(VTE_TERMINAL(terminal), NULL, NULL, palette, PALETTE_SIZE);

    char **args;
    char *default_args[] = {NULL, NULL};
    char *user_shell = NULL;

    if (argc > 1) {
        args = argv + 1;
    } else {
        user_shell = vte_get_user_shell();
        default_args[0] = user_shell ? user_shell : DEFAULT_SHELL;
        args = default_args;
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

    g_signal_connect(terminal, "key-press-event", G_CALLBACK(key_pressed), NULL);
    g_object_set(terminal, "scrollback-lines", 0, NULL);
    gtk_container_add(GTK_CONTAINER(window), terminal);

    gtk_widget_show_all(window);
    gtk_main();

    exit(status);
}
