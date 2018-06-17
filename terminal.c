#include <gtk/gtk.h>
#include <vte/vte.h>
#include <sys/param.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "config.h"
#include "terminal.h"
#include "window.h"

guint timer_id = 0;
const gint ERROR_EXIT_CODE = 127;
#define DEFAULT_SHELL "/bin/sh"

typedef struct {
    char* data;
    int length;
} TitleFormat;
TitleFormat tab_title_format = {NULL, 0};
TitleFormat window_title_format = {NULL, 0};

#define TERMINAL_NO_STATE 0
#define TERMINAL_ACTIVE 1
#define TERMINAL_INACTIVE 2

void update_terminal_ui(VteTerminal* terminal);
void update_terminal_title(VteTerminal* terminal);
void update_terminal_label_class(VteTerminal* terminal);

void term_exited(VteTerminal* terminal, gint status, GtkWidget* container) {
    gtk_widget_destroy(GTK_WIDGET(container));
}

void term_destroyed(VteTerminal* terminal) {
    GSource* inactivity_timer = g_object_get_data(G_OBJECT(terminal), "inactivity_timer");
    if (inactivity_timer) {
        g_source_destroy(inactivity_timer);
        g_source_unref(inactivity_timer);
    }
    GtkLabel* label = GTK_LABEL(g_object_get_data(G_OBJECT(terminal), "label"));
    g_object_unref(label);
}

void term_spawn_callback(GtkWidget* terminal, GPid pid, GError *error, gpointer user_data) {
    if (error) {
        fprintf(stderr, "Could not start terminal: %s\n", error->message);
        g_error_free(error);
        // TODO don't flat out exit
        exit(ERROR_EXIT_CODE);
    }
    g_object_set_data(G_OBJECT(terminal), "pid", GINT_TO_POINTER(pid));

    update_terminal_ui(VTE_TERMINAL(terminal));
    update_window_title(GTK_WINDOW(gtk_widget_get_toplevel(terminal)), NULL);
}

void change_terminal_state(VteTerminal* terminal, int new_state) {
    int old_state = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "activity_state"));
    if (old_state != new_state) {
        g_object_set_data(G_OBJECT(terminal), "activity_state", GINT_TO_POINTER(new_state));
        update_terminal_label_class(terminal);
    }
}

gboolean term_focus_event(GtkWidget* terminal, GdkEvent* event, gpointer data) {
    // clear activity once terminal is focused
    change_terminal_state(VTE_TERMINAL(terminal), TERMINAL_NO_STATE);
    return FALSE;
}

gboolean terminal_inactivity(VteTerminal* terminal) {
    // don't track activity while we have focus
    if (gtk_widget_has_focus(GTK_WIDGET(terminal))) return FALSE;

    change_terminal_state(terminal, TERMINAL_INACTIVE);
    g_object_set_data(G_OBJECT(terminal), "inactivity_timer", NULL);
    return FALSE;
}

void terminal_activity(VteTerminal* terminal) {
    // don't track activity while we have focus
    if (gtk_widget_has_focus(GTK_WIDGET(terminal))) return;

    change_terminal_state(terminal, TERMINAL_ACTIVE);
    GSource* inactivity_timer = g_object_get_data(G_OBJECT(terminal), "inactivity_timer");
    if (inactivity_timer) {
        // delay inactivity timer some more
        g_source_set_ready_time(inactivity_timer, g_source_get_time(inactivity_timer) + inactivity_duration*1000);
    } else {
        inactivity_timer = g_timeout_source_new(inactivity_duration);
        g_source_set_callback(inactivity_timer, (GSourceFunc)terminal_inactivity, terminal, NULL);
        g_object_set_data(G_OBJECT(terminal), "inactivity_timer", inactivity_timer);
        g_source_attach(inactivity_timer, NULL);
    }
}

#define get_pid(terminal) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "pid"))

gboolean get_current_dir(VteTerminal* terminal, char* buffer, size_t length) {
    int pid = get_pid(terminal);
    if (pid <= 0) return FALSE;

    char fname[100];
    snprintf(fname, 100, "/proc/%i/cwd", pid);
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
    int pid = get_foreground_pid(terminal);
    if (pid <= 0) return FALSE;

    char fname[100];
    snprintf(fname, 100, "/proc/%i/status", pid);

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
    buffer[nl-start-1] = '\0';
    return TRUE;
}

int is_running_foreground_process(VteTerminal* terminal) {
    return get_pid(terminal) != get_foreground_pid(terminal);
}

gboolean construct_title(TitleFormat format, VteTerminal* terminal, gboolean escape_markup, char* buffer, size_t length) {
    if (! format.data) return FALSE;

    char dir[1024] = "", name[1024] = "", number[4] = "";
    char* title = NULL;

    int len;
#define APPEND_TO_BUFFER(val) \
    len = strlen(val); \
    strncpy(buffer, val, length); \
    if (length <= len) return FALSE; \
    length -= len; \
    buffer += len;

    APPEND_TO_BUFFER(format.data);

    /*
     * loop through and repeatedly append segments to buffer
     * all except the first segment actually begin with a % format specifier
     * that got replaced with a \0 in set_tab_title_format()
     * so check the first char and insert extra text as appropriate
     */
    char* p = format.data + len + 1;
    while (p <= format.data+format.length) {
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
            case 'N':
                if (*number == '\0') {
                    int n = get_tab_number(terminal);
#pragma GCC diagnostic ignored "-Wformat-truncation"
                    if (n >= 0) snprintf(number, sizeof(number), "%i", n+1);
#pragma GCC diagnostic pop
                }
                val = number;
                break;
            default:
                val = "%";
                p--;
                break;
        }

        if (escape_markup) val = g_markup_escape_text(val, -1);
        APPEND_TO_BUFFER(val);
        if (escape_markup) g_free(val);
        APPEND_TO_BUFFER(p+1);
        p += len+2;
    }
#undef APPEND_TO_BUFFER
    return TRUE;
}

void update_terminal_title(VteTerminal* terminal) {
    char buffer[1024] = "";
    GtkLabel* label = GTK_LABEL(g_object_get_data(G_OBJECT(terminal), "label"));
    gboolean escape_markup = gtk_label_get_use_markup(label);
    if (construct_title(tab_title_format, terminal, escape_markup, buffer, sizeof(buffer)-1)) {
        gtk_label_set_label(label, buffer);
    }
}

void update_terminal_label_class(VteTerminal* terminal) {
    GtkWidget* label = GTK_WIDGET(g_object_get_data(G_OBJECT(terminal), "label"));
    GtkStyleContext* context = gtk_widget_get_style_context(label);
    int state = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "activity_state"));

    switch (state) {
        case TERMINAL_ACTIVE:
            gtk_style_context_remove_class(context, "inactive");
            gtk_style_context_add_class(context, "active");
            break;
        case TERMINAL_INACTIVE:
            gtk_style_context_remove_class(context, "active");
            gtk_style_context_add_class(context, "inactive");
            break;
        default:
            gtk_style_context_remove_class(context, "active");
            gtk_style_context_remove_class(context, "inactive");
            break;
    }
}

void update_terminal_ui(VteTerminal* terminal) {
    update_terminal_title(terminal);
    update_terminal_label_class(terminal);
}

void update_window_title(GtkWindow* window, gpointer data) {
    VteTerminal* terminal = get_active_terminal(GTK_WIDGET(window));
    if (terminal) {
        char buffer[1024] = "";
        if (construct_title(window_title_format, terminal, FALSE, buffer, sizeof(buffer)-1)) {
            gtk_window_set_title(window, buffer);
        }
    }
}

gboolean refresh_all_terminals(gpointer data) {
    foreach_terminal((GFunc)update_terminal_ui, data);
    foreach_window((GFunc)update_window_title, data);
    return TRUE;
}

void create_timer(guint interval) {
    if (timer_id) g_source_remove(timer_id);
    timer_id = g_timeout_add(interval, refresh_all_terminals, NULL);
}

void parse_title_format(char* string, TitleFormat* dest) {
    free(dest->data);
    dest->length = strlen(string);
    dest->data = strdup(string);

    // just replace all % with \0
    char* p = dest->data;
    while (1) {
        p = strchr(p, '%');
        if (! p) break;
        *p = '\0';
        if (*(p+1) == '\0') break;
        p += 2;
    }
}

void set_tab_title_format(char* string) {
    parse_title_format(string, &tab_title_format);
}

void set_window_title_format(char* string) {
    parse_title_format(string, &window_title_format);
}

GtkWidget* make_terminal(GtkWidget* grid, const char* cwd, int argc, char** argv) {
    GtkWidget *terminal;
    GtkWidget *scrollbar;
    GtkWidget *label;

    terminal = vte_terminal_new();
    g_object_set_data(G_OBJECT(grid), "terminal", terminal);
    gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(terminal));

    label = gtk_label_new("");
    g_object_ref(label);
    g_object_set_data(G_OBJECT(terminal), "label", label);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);

    configure_terminal(terminal);

    g_signal_connect(terminal, "focus-in-event", G_CALLBACK(term_focus_event), NULL);
    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), grid);
    g_signal_connect(terminal, "destroy", G_CALLBACK(term_destroyed), grid);
    g_signal_connect(terminal, "window-title-changed", G_CALLBACK(update_terminal_title), NULL);
    g_signal_connect(terminal, "contents-changed", G_CALLBACK(terminal_activity), NULL);
    g_object_set(terminal, "expand", 1, NULL);
    g_object_set_data(G_OBJECT(terminal), "activity_state", GINT_TO_POINTER(TERMINAL_NO_STATE));

    char **args;
    char *fallback_args[] = {NULL, NULL};
    char *user_shell = NULL;

    if (argc > 0) {
        args = argv;
    } else if (default_args) {
        args = default_args;
    } else {
        user_shell = vte_get_user_shell();
        fallback_args[0] = user_shell ? user_shell : DEFAULT_SHELL;
        args = fallback_args;
    }

    char current_dir[MAXPATHLEN+1] = "";
    if (! cwd) {
        VteTerminal* active_term = get_active_terminal(NULL);
        if (active_term && get_current_dir(active_term, current_dir, sizeof(current_dir)-1)) {
            cwd = current_dir;
        }
    }

    vte_terminal_spawn_async(
            VTE_TERMINAL(terminal),
            VTE_PTY_DEFAULT, //pty flags
            cwd, // pwd
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
