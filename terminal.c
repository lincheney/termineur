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
#include "utils.h"
#include "tab_title_ui.h"

const gint ERROR_EXIT_CODE = 127;
#define DEFAULT_SHELL "/bin/sh"

TitleFormat window_title_format = {0, NULL};
char* tab_ui_definition = NULL;

#define TERMINAL_NO_STATE 0
#define TERMINAL_ACTIVE 1
#define TERMINAL_INACTIVE 2

#define SELECTION_SCROLLOFF 5

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
    trigger_action(terminal, -1, BELL_EVENT);
}

void terminal_hyperlink_hover(VteTerminal* terminal) {
    char* uri;
    g_object_get(G_OBJECT(terminal), "hyperlink-hover-uri", &uri, NULL);
    if (uri) {
        trigger_action(terminal, -1, HYPERLINK_HOVER_EVENT);
    }
}

gboolean terminal_button_press_event(VteTerminal* terminal, GdkEvent* event) {
    if (gdk_event_get_event_type(event) == GDK_BUTTON_PRESS) {
        char* uri;
        g_object_get(G_OBJECT(terminal), "hyperlink-hover-uri", &uri, NULL);
        if (uri) {
            trigger_action(terminal, -1, HYPERLINK_CLICK_EVENT);
        }
    }
    return FALSE;
}

void term_spawn_callback(VteTerminal* terminal, GPid pid, GError *error, GtkWidget* grid) {
    // close any left over fds
    int* fds = g_object_get_data(G_OBJECT(terminal), "child_fds");
    if (fds) {
        if (fds[0] >= 0) close(fds[0]);
        if (fds[1] >= 0) close(fds[1]);
        free(fds);
    }
    g_object_set_data(G_OBJECT(terminal), "child_fds", NULL);

    if (error) {
        g_warning("Could not start terminal: %s", error->message);
        grid_cleanup(grid);
        return;
    }
    g_object_set_data(G_OBJECT(terminal), "pid", GINT_TO_POINTER(pid));

    update_tab_titles(terminal);
    update_terminal_css_class(terminal);
    update_window_title(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(terminal))), NULL);
}

void change_terminal_state(VteTerminal* terminal, int new_state) {
    int old_state = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "activity_state"));
    if (old_state != new_state) {
        g_object_set_data(G_OBJECT(terminal), "activity_state", GINT_TO_POINTER(new_state));
        update_terminal_css_class(terminal);
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

    trigger_action(terminal, -1, FOCUS_IN_EVENT);
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

gboolean get_foreground_info(VteTerminal* terminal, int pid, char* name, int* ppid) {
    pid = pid ? pid : get_foreground_pid(terminal);
    if (pid <= 0) return FALSE;

    char fname[100];
    snprintf(fname, 100, "/proc/%i/status", pid);

    char file_buffer[1024];
    int fd = open(fname, O_RDONLY);
    if (fd < 0) return FALSE;
    int length = read(fd, file_buffer, sizeof(file_buffer)-1);
    if (length < 0) return FALSE;
    close(fd);
    file_buffer[length] = '\0';

    char* value;
    char* ptr = file_buffer - 1;

#define GET_FIELD(key, x) \
    while (ptr && strncmp(ptr+1, key, sizeof(key)-1) != 0) { \
        ptr = strchr(ptr+1, '\n'); \
    } \
    if (! ptr) return FALSE; \
    value = ptr + sizeof(key); \
    ptr = strchr(value+1, '\n'); \
    if (! ptr) return FALSE; \
    *ptr = '\0';

    if (name) {
        GET_FIELD("Name:\t", 0);
        strcpy(name, value);
    }

    if (ppid) {
        GET_FIELD("PPid:\t", 1);
        *ppid = atoi(value);
    }

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

gboolean term_construct_title(const char* format, int flags, VteTerminal* terminal, gboolean escape_markup, char* buffer, size_t length) {
    if (! format) return FALSE;

    // get title
    char* title = NULL;
    g_object_get(G_OBJECT(terminal), "window-title", &title, NULL);
    title = title ? title : "";

    char *dir = NULL, *name = NULL;
    char dirbuffer[1024] = "", namebuffer[1024] = "";

    // get name
    name = namebuffer;
    if (flags & TITLE_FORMAT_NAME) {
        get_foreground_info(terminal, 0, namebuffer, NULL);
    }

    // get cwd
    dir = dirbuffer;
    if ((flags & TITLE_FORMAT_CWD) && get_current_dir(terminal, dirbuffer, sizeof(dirbuffer))) {
        // basename but leave slash if top level
        char* base = strrchr(dir, '/');
        if (base && base != dirbuffer) {
            dir = base+1;
        }
    }

    // tab number
    int tab_number = get_tab_number(terminal)+1;

    if (escape_markup) {
        title = g_markup_escape_text(title, -1);
        name = g_markup_escape_text(name, -1);
        dir = g_markup_escape_text(dir, -1);
    }

    int result = snprintf(buffer, length, format, title, name, dir, tab_number) >= 0;

    if (escape_markup) {
        g_free(title);
        g_free(name);
        g_free(dir);
    }

    return result;
}

GtkStyleContext* get_tab_title_context(GtkWidget* terminal) {
    GtkWidget* tab = term_get_tab(VTE_TERMINAL(terminal));
    if (terminal == split_get_active_term(tab)) {
        GtkWidget* label = g_object_get_data(G_OBJECT(tab), "tab_title");
        if (label) {
            GtkStyleContext* context = gtk_widget_get_style_context(label);
            return context;
        }
    }
    return NULL;
}

void term_change_css_class(VteTerminal* terminal, char* class, gboolean add) {
    void(*func)(GtkStyleContext*, const gchar*) = add ? gtk_style_context_add_class : gtk_style_context_remove_class;

    GtkStyleContext* context = get_tab_title_context(GTK_WIDGET(terminal));
    if (context && (add ^ gtk_style_context_has_class(context, class))) {
        func(context, class);
    }

    context = gtk_widget_get_style_context(term_get_grid(terminal));
    if (add ^ gtk_style_context_has_class(context, class)) {
        func(context, class);
    }
}

void update_terminal_css_class(VteTerminal* terminal) {
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

void update_window_title(GtkWindow* window, VteTerminal* terminal) {
    terminal = terminal ? terminal : get_active_terminal(GTK_WIDGET(window));
    if (terminal) {
        char buffer[1024] = "";
        if (term_construct_title(window_title_format.format, window_title_format.flags, terminal, FALSE, buffer, sizeof(buffer)-1)) {
            const char* old_title = gtk_window_get_title(window);
            if (! old_title || ! STR_EQUAL(old_title, buffer)) {
                gtk_window_set_title(window, buffer);
            }
        }
    }
}

void set_window_title_format(char* string) {
    free(window_title_format.format);
    window_title_format = parse_title_format(string);
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

void check_full_scrollbar(GtkAdjustment* adjustment, VteTerminal* terminal) {
    double value = gtk_adjustment_get_value(adjustment);
    double page_size = gtk_adjustment_get_page_size(adjustment);
    double lower = gtk_adjustment_get_lower(adjustment);
    double upper = gtk_adjustment_get_upper(adjustment);
    term_change_css_class(terminal, "full-width", (value == lower && (value + page_size) == upper));
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
        g_signal_connect(scrollbar, "enter-notify-event", G_CALLBACK(scrollbar_hover), GINT_TO_POINTER(TRUE));
        g_signal_connect(scrollbar, "leave-notify-event", G_CALLBACK(scrollbar_hover), GINT_TO_POINTER(FALSE));

        g_signal_connect(adjustment, "changed", G_CALLBACK(check_full_scrollbar), terminal);
        g_signal_connect(adjustment, "value-changed", G_CALLBACK(check_full_scrollbar), terminal);

    } else { /* GTK_POLICY_ALWAYS */
        gtk_grid_attach(GTK_GRID(grid), scrollbar, 1, 0, 1, 1);
    }

    gtk_widget_show(scrollbar);
}

/*
 * DRAWING
 * 1. draw terminal widget css bg in overlay draw handler
 * 2. draw terminal bg in overlay draw handler
 * 3. draw overlay bg in overlay post draw handler
 */

gboolean draw_overlay_widget_post(GtkWidget* widget, cairo_t* cr, GtkWidget* terminal) {
    /*
     * draw the overlay background on top of everything
     */

    GdkRectangle rect;
    gtk_widget_get_allocation(widget, &rect);
    GtkStyleContext* context = gtk_widget_get_style_context(widget);
    gtk_render_background(context, cr, rect.x, rect.y, rect.width, rect.height);

    return FALSE;
}

gboolean draw_overlay_widget(GtkWidget* widget, cairo_t* cr, GtkWidget* terminal) {
    /*
     * draw the terminal backgrounds here
     * since this draw handler gets called first
     */

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, BACKGROUND.red, BACKGROUND.green, BACKGROUND.blue, BACKGROUND.alpha);
    cairo_paint(cr);
    return FALSE;
}

gboolean overlay_position_term(GtkWidget* overlay, GtkWidget* widget, GdkRectangle* rect) {
    if (VTE_IS_TERMINAL(widget)) {
        gtk_widget_get_allocation(overlay, rect);
        int min, natural;
        gtk_widget_get_preferred_height(widget, &min, &natural);

        if (rect->height > min) {
            // shrink only if > 1 row
            int margin = rect->height % vte_terminal_get_char_height(VTE_TERMINAL(widget));
            rect->y = margin;
            rect->x = 0;
        }
        return TRUE;
    }
    if (GTK_IS_SCROLLBAR(widget)) {
        gtk_widget_get_allocation(overlay, rect);
        int min, natural;
        gtk_widget_get_preferred_width(widget, &min, &natural);
        int width = MAX(min, MIN(natural, rect->width));

        gtk_widget_get_preferred_height(widget, &min, &natural);
        rect->height = MAX(min, rect->height);

        rect->x = rect->width - width;
        rect->width = width;
        return TRUE;
    }
    return FALSE;
}

void term_select_range(VteTerminal* terminal, double start_col, double start_row, double end_col, double end_row, int modifiers) {
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
        g_warning("No GdkWindow found for terminal");
        return;
    }

    /* printf("start=%f end=%f value=%f lower=%f upper=%f page=%f\n", start_row, end_row, value, lower, upper, page_size); */

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
        modifiers,
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
        modifiers,
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

void term_setup_pipes(int* pipes) {
    if (pipes[0] >= 0) {
        dup2(pipes[0], 0);
        close(pipes[0]);
    }
    if (pipes[1] >= 0) {
        dup2(pipes[1], 1);
        close(pipes[1]);
    }
}

void term_get_row_positions(VteTerminal* terminal, int* screen_lower, int* screen_upper, int* lower, int* upper) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));
    int value = gtk_adjustment_get_value(adj);

    if (screen_lower) *screen_lower = value;
    if (screen_upper) *screen_upper = value + gtk_adjustment_get_page_size(adj);
    if (lower) *lower = gtk_adjustment_get_lower(adj);
    if (upper) *upper = gtk_adjustment_get_upper(adj);
}

char* term_get_text(VteTerminal* terminal, glong start_row, glong start_col, glong end_row, glong end_col, gboolean ansi) {
    GArray* attrs = NULL;
    if (ansi) {
        attrs = g_array_new(FALSE, FALSE, sizeof(VteCharAttributes));
    }

    char* text = vte_terminal_get_text_range(terminal, start_row, start_col, end_row, end_col, NULL, NULL, attrs);
    if (ansi) {
        GArray* output = g_array_new(TRUE, TRUE, sizeof(char));
        VteCharAttributes prev = { 0,0, {0,0,0}, {0,0,0}, 0, 0 };
        char buf[32];

        const char* c;
        for (c = text; *c; c++) {
            VteCharAttributes a = g_array_index(attrs, VteCharAttributes, c - text);
            /* no bold or italic */

            // foreground
            if (c == text || a.fore.red != prev.fore.red || a.fore.green != prev.fore.green || a.fore.blue != prev.fore.blue) {
                int n = sprintf(buf, "\x1b[38;2;%i;%i;%im", a.fore.red>>8, a.fore.green>>8, a.fore.blue>>8);
                g_array_append_vals(output, buf, n);
            }
            // background
            if (c == text || a.back.red != prev.back.red || a.back.green != prev.back.green || a.back.blue != prev.back.blue) {
                int n = sprintf(buf, "\x1b[48;2;%i;%i;%im", a.back.red>>8, a.back.green>>8, a.back.blue>>8);
                g_array_append_vals(output, buf, n);
            }
            // underline
            if (c == text || a.underline != prev.underline) {
                g_array_append_vals(output, a.underline ? "\x1b[4m" : "\x1b[24m", a.underline ? 4 : 5);
            }
            g_array_append_val(output, *c);
            prev = a;
        }

        free(text);
        text = output->data;
        g_array_free(output, FALSE);
        g_array_free(attrs, TRUE);
    }

    return text;
}

GtkWidget* make_terminal(const char* cwd, int argc, char** argv) {
    return make_terminal_full(cwd, argc, argv, NULL, NULL, NULL);
}

GtkWidget* make_terminal_full(const char* cwd, int argc, char** argv, GSpawnChildSetupFunc child_setup, void* child_setup_data, GDestroyNotify child_setup_destroy) {
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
    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), grid);
    g_signal_connect(terminal, "destroy", G_CALLBACK(term_destroyed), grid);
    g_signal_connect(terminal, "window-title-changed", G_CALLBACK(update_tab_titles), NULL);
    g_signal_connect(terminal, "contents-changed", G_CALLBACK(terminal_activity), NULL);
    g_signal_connect(terminal, "bell", G_CALLBACK(terminal_bell), NULL);
    g_signal_connect(terminal, "hyperlink-hover-uri-changed", G_CALLBACK(terminal_hyperlink_hover), NULL);
    g_signal_connect(terminal, "button-press-event", G_CALLBACK(terminal_button_press_event), NULL);
    g_signal_connect(overlay, "draw", G_CALLBACK(draw_overlay_widget), terminal);

    g_signal_connect_after(overlay, "draw", G_CALLBACK(draw_overlay_widget_post), terminal);
    gtk_widget_set_app_paintable(overlay, TRUE);

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
            child_setup, // child setup
            child_setup_data, // child setup data
            child_setup_destroy, // child setup data destroy
            -1, // timeout
            NULL, // cancellable
            (VteTerminalSpawnAsyncCallback)term_spawn_callback, // callback
            grid // user data
    );
    free(user_shell);

    return grid;
}
