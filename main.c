#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <vte/vte.h>
#include <unistd.h>
#include <fcntl.h>

#define C1 (1./3.)
#define C2 (2./3.)
#define PALETTE_SIZE (16)

const GdkRGBA bg = { .1, 0., 0., 1. };
const GdkRGBA fg = { 1., 1., 1., 1. };

const GdkRGBA palette[PALETTE_SIZE] = {
    { 0., 0., 0., 1. }, // black
    { C2, 0., 0., 1. }, // red
    { 0., C2, 0., 1. }, // green
    { C2, C1, 0., 1. }, // yellow
    { 0., 0., C2, 1. }, // blue
    { C2, 0., C2, 1. }, // agenta
    { 0., C2, C2, 1. }, // cyan
    { C2, C2, C2, 1. }, // light grey
    { C1, C1, C1, 1. }, // dark grey
    { 1., C1, C1, 1. }, // light red
    { C1, 1., C1, 1. }, // light green
    { 1., 1., C2, 1. }, // light yellow
    { C1, C1, 1., 1. }, // light blue
    { 1., C1, 1., 1. }, // light magenta
    { C1, 1., 1., 1. }, // light cyan
    { 1., 1., 1., 1. }  // white
};

#define GET_ENV(x) (getenv("POPUP_TERM_" x))

char *DEFAULT_ARGS[] = {"bash"};
const gint STDIN_FD = 3;
const gint STDOUT_FD = 4;
const gint ERROR_EXIT_CODE = 127;
const gint BORDER_WIDTH = 2;

void term_exited(VteTerminal* terminal, gint status, gint* dest)
{
    if (WIFEXITED(status)) {
        *dest = WEXITSTATUS(status);
    } else {
        *dest = ERROR_EXIT_CODE;
    }
    gtk_main_quit();
}

void child_setup(void* data)
{
    int flags;
    const char* reset_env = GET_ENV("RESET_FD");

    if (reset_env != NULL && strncmp(reset_env, "1", 1) == 0) {
        dup2(STDIN_FD, 0);
        dup2(STDOUT_FD, 1);
    }

    flags = fcntl(STDIN_FD, F_GETFD);
    fcntl(STDIN_FD, F_SETFD, flags & ~FD_CLOEXEC);

    flags = fcntl(STDOUT_FD, F_GETFD);
    fcntl(STDOUT_FD, F_SETFD, flags & ~FD_CLOEXEC);
}

int main(int argc, char *argv[])
{
    int status = 0;
    GtkWidget *window;
    GtkWidget *terminal;
    GdkScreen *screen;

    dup2(0, STDIN_FD);
    dup2(1, STDOUT_FD);

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "delete-event", gtk_main_quit, NULL);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_container_set_border_width(GTK_CONTAINER(window), BORDER_WIDTH);

    screen = gtk_window_get_screen(GTK_WINDOW(window));
    gint width = gdk_screen_get_width(screen);
    gint height = gdk_screen_get_height(screen);
    gtk_window_set_default_size(GTK_WINDOW(window), width/2, height/2);

    terminal = vte_terminal_new();
    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), &status);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal), VTE_CURSOR_BLINK_OFF);
    vte_terminal_set_colors(VTE_TERMINAL(terminal), &fg, &bg, palette, PALETTE_SIZE);

    char **args = NULL;
    if (argc <= 1) {
        args = DEFAULT_ARGS;
    } else {
        args = argv + 1;
    }

    vte_terminal_spawn_sync(
            VTE_TERMINAL(terminal),
            VTE_PTY_DEFAULT, //pty flags
            NULL, // pwd
            args, // args
            NULL, // env
            G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN, // g spawn flags
            child_setup, // child setup
            NULL, // child setup data
            NULL, // child pid
            NULL, // cancellable
            NULL // error
        );
    gtk_container_add(GTK_CONTAINER(window), terminal);

    gtk_widget_show_all(window);
    gtk_main();

    return status;
}
