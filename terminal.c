#include <gtk/gtk.h>
#include <vte/vte.h>
#include <sys/param.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "config.h"
#include "window.h"

guint timer_id = 0;
const gint ERROR_EXIT_CODE = 127;
#define DEFAULT_SHELL "/bin/sh"

struct {
    char* data;
    int length;
} tab_title_format = {NULL, 0};

void update_terminal_ui(VteTerminal* terminal);

void term_exited(VteTerminal* terminal, gint status, GtkWidget* container) {
    gtk_widget_destroy(GTK_WIDGET(container));
}

void term_spawn_callback(GtkWidget* terminal, GPid pid, GError *error, gpointer user_data) {
    if (error) {
        fprintf(stderr, "Could not start terminal: %s\n", error->message);
        // TODO don't flat out exit
        exit(ERROR_EXIT_CODE);
    }
    g_object_set_data(G_OBJECT(terminal), "pid", GINT_TO_POINTER(pid));
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

    update_terminal_ui(VTE_TERMINAL(terminal));
    return terminal;
}

#define get_pid(terminal) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "pid"))

gboolean get_current_dir(VteTerminal* terminal, char* buffer, size_t length) {
    char fname[100];
    snprintf(fname, 100, "/proc/%i/cwd", get_pid(terminal));
    length = readlink(fname, buffer, length);
    if (length == -1) return FALSE;
    buffer[length] = '\0';
    return TRUE;
}

int get_foreground_pid(VteTerminal* terminal) {
    VtePty* pty = vte_terminal_get_pty(terminal);
    int pty_fd = vte_pty_get_fd(pty);
    int fg_pid = tcgetpgrp(pty_fd);
    return fg_pid;
}

gboolean get_foreground_name(VteTerminal* terminal, char* buffer, size_t length) {
    char fname[100];
    snprintf(fname, 100, "/proc/%i/status", get_foreground_pid(terminal));

    char file_buffer[1024];
    int fd = open(fname, O_RDONLY);
    if (fd < 0) return FALSE;
    length = read(fd, file_buffer, sizeof(file_buffer)-1);
    if (length < 0) return FALSE;
    close(fd);
    file_buffer[length] = '\0';

    // second field
    char* start = strchr(file_buffer, '\t');
    if (! start) return FALSE;

    // name is on first line
    char* nl = strchr(start, '\n');
    if (! nl) nl = file_buffer + length; // set to end of buffer
    *nl = '\0';

    // don't touch buffer until the very end
    strncpy(buffer, start+1, nl-start-1);
    return TRUE;
}

gboolean construct_title(VteTerminal* terminal, char* buffer, size_t length) {
    if (! tab_title_format.data) return FALSE;

    char dir[1024] = "", name[1024] = "";
    char* title = NULL;

    int len;
#define APPEND_TO_BUFFER(val) \
    len = strlen(val); \
    strncpy(buffer, val, length); \
    if (length <= len) return FALSE; \
    length -= len; \
    buffer += len;

    APPEND_TO_BUFFER(tab_title_format.data)

    /*
     * loop through and repeatedly append segments to buffer
     * all except the first segment actually begin with a % format specifier
     * that got replaced with a \0 in set_tab_title_format()
     * so check the first char and insert extra text as appropriate
     */
    char* p = tab_title_format.data + len + 1;
    while (p <= tab_title_format.data+tab_title_format.length) {
        char* val;
        switch (*p) {
            case 'd':
                if (*dir == '\0') {
                    get_current_dir(terminal, dir, sizeof(dir));
                    // basename but leave slash if top level
                    char* base = strrchr(dir, '/');
                    if (base && base != dir)
                        memmove(dir, base+1, strlen(base));
                }
                val = dir;
                break;
            case 'n':
                if (*name == '\0') {
                    get_foreground_name(terminal, name, sizeof(name));
                }
                val = name;
                break;
            case 't':
                if (! title) {
                    g_object_get(G_OBJECT(terminal), "window-title", &title, NULL);
                    if (! title) title = "";
                }
                val = title;
                break;
            default:
                val = "%";
                p--;
                break;
        }

        APPEND_TO_BUFFER(val)
        APPEND_TO_BUFFER(p+1)
        p += len+2;
    }
#undef APPEND_TO_BUFFER
    return TRUE;
}

void update_terminal_ui(VteTerminal* terminal) {
    GtkLabel* label = GTK_LABEL(g_object_get_data(G_OBJECT(terminal), "label"));

    // set the title
    char buffer[1024] = "";
    if (construct_title(terminal, buffer, sizeof(buffer)-1)) {
        gtk_label_set_text(label, buffer);
    }
}

gboolean timer_callback(gpointer data) {
    // how to get all terminals
    foreach_terminal((GFunc)update_terminal_ui, data);
    return TRUE;
}

void create_timer(guint interval) {
    if (timer_id) g_source_remove(timer_id);
    timer_id = g_timeout_add(interval, timer_callback, NULL);
}

void set_tab_title_format(char* string) {
    free(tab_title_format.data);
    tab_title_format.length = strlen(string);
    tab_title_format.data = strdup(string);

    // just replace all % with \0
    char* p = tab_title_format.data;
    while (1) {
        p = strchr(p, '%');
        if (! p) break;
        *p = '\0';
        if (*(p+1) == '\0') break;
        p += 2;
    }
}
