#include <gtk/gtk.h>
#include <sys/param.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "terminal.h"
#include "window.h"
#include "split.h"
#include "label.h"

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

#define SELECTION_SCROLLOFF 5

void update_terminal_ui(VteTerminal* terminal);
void update_terminal_title(VteTerminal* terminal);
void update_terminal_label_class(VteTerminal* terminal);

GtkWidget* term_get_grid(VteTerminal* terminal) {
    return gtk_widget_get_ancestor(GTK_WIDGET(terminal), GTK_TYPE_GRID);
}

GtkWidget* term_get_notebook(VteTerminal* terminal) {
    return split_get_container(gtk_widget_get_parent(term_get_grid(terminal)));
}

GtkWidget* term_get_tab(VteTerminal* terminal) {
    return split_get_root(gtk_widget_get_parent(term_get_grid(terminal)));
}

void grid_cleanup(GtkWidget* grid) {
    GtkWidget* paned = gtk_widget_get_parent(grid);
    gtk_container_remove(GTK_CONTAINER(paned), grid);
    split_cleanup(paned);
}

void term_exited(VteTerminal* terminal, gint status, GtkWidget* grid) {
    split_remove_term_from_chain(terminal);
    GtkWidget* active_terminal = split_get_active_term(term_get_tab(terminal));
    grid_cleanup(grid);

    if (active_terminal) {
        // focus next terminal
        gtk_widget_grab_focus(GTK_WIDGET(active_terminal));
    }
}

void term_destroyed(VteTerminal* terminal, GtkWidget* grid) {
    GSource* inactivity_timer = g_object_get_data(G_OBJECT(terminal), "inactivity_timer");
    if (inactivity_timer) {
        g_source_destroy(inactivity_timer);
        g_source_unref(inactivity_timer);
    }
}

void terminal_bell(VteTerminal* terminal) {
    trigger_callback(terminal, -1, BELL_EVENT);
}

void terminal_hyperlink_hover(VteTerminal* terminal) {
    char* uri;
    g_object_get(G_OBJECT(terminal), "hyperlink-hover-uri", &uri, NULL);
    if (uri) {
        trigger_callback(terminal, -1, HYPERLINK_HOVER_EVENT);
    }
}

gboolean terminal_button_press_event(VteTerminal* terminal, GdkEvent* event) {
    if (gdk_event_get_event_type(event) == GDK_BUTTON_PRESS) {
        char* uri;
        g_object_get(G_OBJECT(terminal), "hyperlink-hover-uri", &uri, NULL);
        if (uri) {
            trigger_callback(terminal, -1, HYPERLINK_CLICK_EVENT);
        }
    }
    return FALSE;
}

void term_spawn_callback(GtkWidget* terminal, GPid pid, GError *error, GtkWidget* grid) {
    if (error) {
        g_warning("Could not start terminal: %s", error->message);
        grid_cleanup(grid);
        return;
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

gboolean term_focus_in_event(VteTerminal* terminal, GdkEvent* event, gpointer data) {
    // set some css
    GtkWidget* tab = term_get_tab(terminal);
    VteTerminal* old_focus = VTE_TERMINAL(split_get_active_term(tab));
    if (old_focus && old_focus != terminal) {
        term_change_css_class(old_focus, "selected", 0);
    }
    term_change_css_class(terminal, "selected", 1);

    split_set_active_term(terminal);
    // clear activity once terminal is focused
    change_terminal_state(terminal, TERMINAL_NO_STATE);
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

struct termios get_term_attr(VteTerminal* terminal) {
    VtePty* pty = vte_terminal_get_pty(terminal);
    int pty_fd = vte_pty_get_fd(pty);
    struct termios termios;
    tcgetattr(pty_fd, &termios);
    return termios;
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
    GtkWidget* tab = term_get_tab(terminal);
    GtkLabel* label = GTK_LABEL(g_object_get_data(G_OBJECT(tab), "label"));
    gboolean escape_markup = gtk_label_get_use_markup(label);
    if (construct_title(tab_title_format, terminal, escape_markup, buffer, sizeof(buffer)-1)) {
        gtk_label_set_label(label, buffer);
    }
}

GtkStyleContext* get_label_context(GtkWidget* terminal) {
    GtkWidget* tab = term_get_tab(VTE_TERMINAL(terminal));
    if (terminal == split_get_active_term(tab)) {
        GtkWidget* label = g_object_get_data(G_OBJECT(tab), "label");
        GtkStyleContext* context = gtk_widget_get_style_context(label);
        return context;
    }
    return NULL;
}

void term_change_css_class(VteTerminal* terminal, char* class, gboolean add) {
    void(*func)(GtkStyleContext*, const gchar*) = add ? gtk_style_context_add_class : gtk_style_context_remove_class;

    GtkStyleContext* context = get_label_context(GTK_WIDGET(terminal));
    if (context && (add ^ gtk_style_context_has_class(context, class))) {
        func(context, class);
    }
    context = gtk_widget_get_style_context(term_get_grid(terminal));
    if (add ^ gtk_style_context_has_class(context, class)) {
        func(context, class);
    }
}

void update_terminal_label_class(VteTerminal* terminal) {
    int state = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "activity_state"));
    switch (state) {
        case TERMINAL_ACTIVE:
            term_change_css_class(terminal, "inactive", 0);
            term_change_css_class(terminal, "active", 1);
            break;
        case TERMINAL_INACTIVE:
            term_change_css_class(terminal, "active", 0);
            term_change_css_class(terminal, "inactive", 1);
            break;
        default:
            term_change_css_class(terminal, "active", 0);
            term_change_css_class(terminal, "inactive", 0);
            break;
    }
}

void update_terminal_ui(VteTerminal* terminal) {
    GtkWidget* root = term_get_tab(terminal);
    if (split_get_active_term(root) == GTK_WIDGET(terminal)) {
        update_terminal_title(terminal);
        update_terminal_label_class(terminal);
    }
}

void update_window_title(GtkWindow* window, VteTerminal* terminal) {
    terminal = terminal ? terminal : get_active_terminal(GTK_WIDGET(window));
    if (terminal) {
        char buffer[1024] = "";
        if (construct_title(window_title_format, terminal, FALSE, buffer, sizeof(buffer)-1)) {
            gtk_window_set_title(window, buffer);
        }
    }
}

gboolean refresh_all_terminals(gpointer data) {
    foreach_terminal((GFunc)update_terminal_ui, data);
    foreach_window((GFunc)update_window_title, NULL);
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

gboolean term_hide_message_bar(VteTerminal* terminal) {
    GtkWidget* grid = term_get_grid(terminal);
    GtkWidget* msg_bar = GTK_WIDGET(g_object_get_data(G_OBJECT(grid), "msg_bar"));
    if (gtk_widget_get_parent(msg_bar)) {
        gtk_container_remove(GTK_CONTAINER(grid), msg_bar);
    }
    return G_SOURCE_REMOVE;
}

void term_show_message_bar(VteTerminal* terminal, const char* message, int timeout) {
    GtkWidget* grid = term_get_grid(terminal);
    GtkWidget* msg_bar = GTK_WIDGET(g_object_get_data(G_OBJECT(grid), "msg_bar"));

    if (! gtk_widget_get_parent(msg_bar)) {
        gtk_grid_attach(GTK_GRID(grid), msg_bar, 0, -1, 2, 1);
    }
    GtkWidget* label = gtk_bin_get_child(GTK_BIN(msg_bar));
    gtk_label_set_markup(GTK_LABEL(label), message);

    if (timeout > 0) {
        g_timeout_add(timeout, (GSourceFunc)gtk_button_clicked, msg_bar);
    }

    gtk_widget_show(msg_bar);
}

gboolean
scrollbar_hover(GtkWidget* scrollbar, GdkEvent* event, gboolean inside) {
    GtkStyleContext* context = gtk_widget_get_style_context(scrollbar);
    if (inside) {
        gtk_style_context_add_class(context, "hovering");
    } else {
        gtk_style_context_remove_class(context, "hovering");
    }
    return FALSE;
}

void check_full_scrollbar(GtkAdjustment* adjustment, GtkWidget* scrollbar) {
    double value = gtk_adjustment_get_value(adjustment);
    double page_size = gtk_adjustment_get_page_size(adjustment);
    double lower = gtk_adjustment_get_lower(adjustment);
    double upper = gtk_adjustment_get_upper(adjustment);

    GtkStyleContext* context = gtk_widget_get_style_context(scrollbar);
    if (value == lower && (value + page_size) == upper) {
        gtk_style_context_add_class(context, "full");
    } else {
        gtk_style_context_remove_class(context, "full");
    }
}

void configure_terminal_scrollbar(VteTerminal* terminal, GtkPolicyType scrollbar_policy) {
    GtkWidget* grid = term_get_grid(terminal);
    GtkWidget* scrollbar = g_object_get_data(G_OBJECT(grid), "scrollbar");
    GtkAdjustment* adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));

    if (scrollbar) {
        g_signal_handlers_disconnect_by_data(adjustment, scrollbar);
        // destroy old scrollbar
        gtk_widget_destroy(scrollbar);
        g_object_set_data(G_OBJECT(grid), "scrollbar", NULL);
    }

    if (scrollbar_policy == GTK_POLICY_NEVER) {
        return;
    }

    scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, adjustment);
    g_object_set_data(G_OBJECT(grid), "scrollbar", scrollbar);

    if (scrollbar_policy == GTK_POLICY_AUTOMATIC) {
        GtkWidget* overlay = gtk_widget_get_parent(GTK_WIDGET(terminal));
        // overlay on top of terminal
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), scrollbar);

        GtkStyleContext* context = gtk_widget_get_style_context(scrollbar);
        gtk_style_context_add_class(context, "overlay-indicator");
        g_signal_connect(scrollbar, "motion-notify-event", G_CALLBACK(scrollbar_hover), GINT_TO_POINTER(TRUE));
        g_signal_connect(scrollbar, "leave-notify-event", G_CALLBACK(scrollbar_hover), GINT_TO_POINTER(FALSE));

        g_signal_connect(adjustment, "changed", G_CALLBACK(check_full_scrollbar), scrollbar);
        g_signal_connect(adjustment, "value-changed", G_CALLBACK(check_full_scrollbar), scrollbar);

    } else { /* GTK_POLICY_ALWAYS */
        gtk_grid_attach(GTK_GRID(grid), scrollbar, 1, 0, 1, 1);
    }

    gtk_widget_show(scrollbar);
}

gboolean draw_terminal_overlay(GtkWidget* widget, cairo_t* cr, GdkRectangle* rect) {
    GdkRectangle dim;
    if (rect == NULL) {
        rect = &dim;
        gtk_widget_get_allocation(widget, rect);
    }

    GtkStyleContext* context = gtk_widget_get_style_context(widget);
    gtk_render_background(context, cr, rect->x, rect->y, rect->width, rect->height);
    return FALSE;
}

gboolean draw_terminal_background(GtkWidget* widget, cairo_t* cr, GtkWidget* terminal) {
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, BACKGROUND.red, BACKGROUND.green, BACKGROUND.blue, BACKGROUND.alpha);
    cairo_paint(cr);

    GdkRectangle rect;
    gtk_widget_get_allocation(widget, &rect);
    rect.height = rect.height % vte_terminal_get_char_height(VTE_TERMINAL(terminal));
    draw_terminal_overlay(terminal, cr, &rect);
    return FALSE;
}

gboolean overlay_position_term(GtkWidget* overlay, GtkWidget* widget, GdkRectangle* rect) {
    if (VTE_IS_TERMINAL(widget)) {
        gtk_widget_get_allocation(overlay, rect);
        int margin = rect->height % vte_terminal_get_char_height(VTE_TERMINAL(widget));
        rect->y += margin;
        return TRUE;
    }
    if (GTK_IS_SCROLLBAR(widget)) {
        gtk_widget_get_allocation(overlay, rect);
        int min, natural;
        gtk_widget_get_preferred_width(widget, &min, &natural);
        int width = MAX(min, MIN(natural, rect->width));

        rect->x = rect->width - width;
        rect->width = width;
        return TRUE;
    }
    return FALSE;
}

void term_select_range(VteTerminal* terminal, double start_col, double start_row, double end_col, double end_row) {
    /*
     * hacks
     * vte doesn't have a direct API for changing selection
     * and the ATK interface doesn't work if there is scrollback
     * so we fake mouse events instead
     */

    vte_terminal_unselect_all(terminal);

    GtkAdjustment* adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));
    double value = gtk_adjustment_get_value(adjustment);
    double page_size = gtk_adjustment_get_page_size(adjustment);
    double lower = gtk_adjustment_get_lower(adjustment);
    double upper = gtk_adjustment_get_upper(adjustment);
    double original = value;
    double scrolloff = MIN(SELECTION_SCROLLOFF, floor(page_size / 4));

    long columns = vte_terminal_get_column_count(terminal);
    double rows = upper - lower;
    /* handle negative offsets */
    if (end_col < 0) end_col += columns + 1;
    if (start_col < 0) start_col += columns;
    if (end_row < 0) end_row += rows;
    if (start_row < 0) start_row += rows;

    if (start_row > end_row || (start_row == end_row && start_col >= end_col)) {
        return;
    }
    start_row += lower;
    end_row += lower;

    /* have to find the right GdkWindow or the terminal won't accept the events */
    GdkWindow* window = gtk_widget_get_window(GTK_WIDGET(terminal));
    GdkDisplay* display = gdk_window_get_display(window);
    GdkSeat* seat = gdk_display_get_default_seat(display);
    GdkDevice* device = gdk_seat_get_pointer(seat);
    GList* window_list = gdk_window_get_children_with_user_data(window, terminal);

    window = window_list->data;
    g_list_free(window_list);
    if (! window) {
        g_warning("No GdkWindow found for terminal\n");
        return;
    }

    /* printf("start=%f end=%f value=%f lower=%f upper=%f page=%f\n", start_row, end_row, value, lower, upper, page_size); */

    guint state = GDK_SHIFT_MASK;
    int width = vte_terminal_get_char_width(terminal);
    int height = vte_terminal_get_char_height(terminal);

    /* scroll until start row is in view */
    if (value > start_row || value+page_size < start_row) {
        value = CLAMP(start_row - scrolloff, lower, upper - page_size);
        gtk_adjustment_set_value(adjustment, value);
    }

    /* press */
    GdkEventButton button = {
        GDK_BUTTON_PRESS,
        window, TRUE, 0,
        start_col*width, (start_row-value)*height,
        NULL,
        state,
        1, /* left mouse button */
        device, 0., 0.,
    };
    GTK_WIDGET_GET_CLASS(terminal)->button_press_event(GTK_WIDGET(terminal), &button);

    /* first motion to start selection before we scroll */
    GdkEventMotion motion = {
        GDK_MOTION_NOTIFY,
        window, TRUE, 0,
        button.x+width, button.y,
        NULL,
        state,
        FALSE, device, 0, 0,
    };
    GTK_WIDGET_GET_CLASS(terminal)->motion_notify_event(GTK_WIDGET(terminal), &motion);

    /* scroll until end row is in view */
    if (value+page_size < end_row) {
        value = CLAMP(end_row + scrolloff - page_size, lower, upper - page_size);
        gtk_adjustment_set_value(adjustment, value);
    }

    /* select to end */
    motion.x = end_col*width;
    motion.y = (end_row-value)*height;
    GTK_WIDGET_GET_CLASS(terminal)->motion_notify_event(GTK_WIDGET(terminal), &motion);

    /* release */
    button.type = GDK_BUTTON_RELEASE;
    button.x = motion.x;
    button.y = motion.y;
    GTK_WIDGET_GET_CLASS(terminal)->button_release_event(GTK_WIDGET(terminal), &button);

    /* scroll back to original if possible */
    if (original < end_row && original+page_size >= start_row) {
        gtk_adjustment_set_value(adjustment, original);
    }
}

GtkWidget* make_terminal(const char* cwd, int argc, char** argv) {
    GtkWidget* terminal = vte_terminal_new();

    GtkWidget* msg_bar = gtk_button_new_with_label("");
    label_new(gtk_bin_get_child(GTK_BIN(msg_bar)));
    g_object_ref(msg_bar);
    g_signal_connect(msg_bar, "clicked", G_CALLBACK(term_hide_message_bar), terminal);

    // just a wrapper container to move the terminal about
    GtkWidget* overlay = gtk_overlay_new();
    g_signal_connect(overlay, "get-child-position", G_CALLBACK(overlay_position_term), NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), terminal);

    GtkWidget* grid = gtk_grid_new();
    g_object_set_data(G_OBJECT(grid), "terminal", terminal);
    g_object_set_data(G_OBJECT(grid), "msg_bar", msg_bar);
    gtk_container_add(GTK_CONTAINER(grid), overlay);
    g_signal_connect_swapped(grid, "destroy", G_CALLBACK(g_object_unref), msg_bar);
    g_signal_connect_swapped(msg_bar, "focus-in-event", G_CALLBACK(gtk_widget_grab_focus), terminal);

    configure_terminal(terminal);
    g_object_set(terminal, "expand", TRUE, "scrollback-lines", terminal_default_scrollback_lines, NULL);
    g_object_set_data(G_OBJECT(terminal), "activity_state", GINT_TO_POINTER(TERMINAL_NO_STATE));
    vte_terminal_set_clear_background(VTE_TERMINAL(terminal), FALSE);

    g_signal_connect(terminal, "focus-in-event", G_CALLBACK(term_focus_in_event), NULL);
    g_signal_connect_swapped(terminal, "focus-out-event", G_CALLBACK(gtk_widget_queue_draw), overlay); // redraw overlay on focus out
    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), grid);
    g_signal_connect(terminal, "destroy", G_CALLBACK(term_destroyed), grid);
    g_signal_connect(terminal, "window-title-changed", G_CALLBACK(update_terminal_title), NULL);
    g_signal_connect(terminal, "contents-changed", G_CALLBACK(terminal_activity), NULL);
    g_signal_connect(terminal, "bell", G_CALLBACK(terminal_bell), NULL);
    g_signal_connect(terminal, "hyperlink-hover-uri-changed", G_CALLBACK(terminal_hyperlink_hover), NULL);
    g_signal_connect(terminal, "button-press-event", G_CALLBACK(terminal_button_press_event), NULL);
    g_signal_connect(overlay, "draw", G_CALLBACK(draw_terminal_background), terminal);
    g_signal_connect_after(terminal, "draw", G_CALLBACK(draw_terminal_overlay), NULL);

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
            grid // user data
    );
    free(user_shell);

    return grid;
}
