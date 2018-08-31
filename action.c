#include <vte/vte.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <errno.h>
#include "action.h"
#include "window.h"
#include "terminal.h"
#include "config.h"
#include "split.h"
#include "utils.h"
#include "search_bar.h"

GtkWidget* detaching_tab = NULL;

ActionFunc select_all = (ActionFunc)vte_terminal_select_all;
ActionFunc unselect_all = (ActionFunc)vte_terminal_unselect_all;

void add_css_class(VteTerminal* terminal, char* data) {
    term_change_css_class(terminal, data, 1);
}

void remove_css_class(VteTerminal* terminal, char* data) {
    term_change_css_class(terminal, data, 0);
}

void paste_text(VteTerminal* terminal, char* data) {
    GtkClipboard* clipboard = NULL;
    char* original = NULL;

    if (data) {
        clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        original = gtk_clipboard_wait_for_text(clipboard);
        gtk_clipboard_set_text(clipboard, data, -1);
    }

    vte_terminal_paste_clipboard(terminal);

    if (original) {
        gtk_clipboard_set_text(clipboard, original, -1);
    }
}

void copy_text(VteTerminal* terminal) {
    vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
}

void change_font_size(VteTerminal* terminal, char* delta) {
    float value = strtof(delta, NULL);
    if (delta[0] == '+' || value < 0) {
        value = vte_terminal_get_font_scale(terminal) + value;
    }
    vte_terminal_set_font_scale(terminal, value);
}

void reset_terminal(VteTerminal* terminal) {
    vte_terminal_reset(terminal, 1, 1);
    vte_terminal_feed_child_binary(terminal, (guint8*)"\x0c", 1); // control-l = clear
}

#define SCROLL(terminal, value) \
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal)); \
    gtk_adjustment_set_value(adj, value)

void scroll_up(VteTerminal* terminal) {
    SCROLL(terminal, gtk_adjustment_get_value(adj) - gtk_adjustment_get_step_increment(adj));
}

void scroll_down(VteTerminal* terminal) {
    SCROLL(terminal, gtk_adjustment_get_value(adj) + gtk_adjustment_get_step_increment(adj));
}

void scroll_page_up(VteTerminal* terminal) {
    SCROLL(terminal, gtk_adjustment_get_value(adj) - gtk_adjustment_get_page_size(adj));
}

void scroll_page_down(VteTerminal* terminal) {
    SCROLL(terminal, gtk_adjustment_get_value(adj) + gtk_adjustment_get_page_size(adj));
}

void scroll_top(VteTerminal* terminal) {
    SCROLL(terminal, gtk_adjustment_get_lower(adj));
}

void scroll_bottom(VteTerminal* terminal) {
    SCROLL(terminal, gtk_adjustment_get_upper(adj));
}

void feed_data(VteTerminal* terminal, char* data) {
    vte_terminal_feed_child_binary(terminal, (guint8*)data, strlen(data));
}

void feed_term(VteTerminal* terminal, char* data) {
    vte_terminal_feed(terminal, data, -1);
}

GtkWidget* new_term(gchar* data, char** size, int** pipes) {
    gint argc;
    char* cwd = NULL;
    char **original, **argv = shell_split(data, &argc);
    if (data && ! STR_EQUAL(data, "") && ! argv) {
        g_warning("Failed to parse: %s", data);
        return NULL;
    }

    original = argv;
    while (argc > 0) {
        char* tmp;
        if ((tmp = STR_STRIP_PREFIX(argv[0], "cwd="))) {
            cwd = tmp;
        } else if ((tmp = STR_STRIP_PREFIX(argv[0], "size="))) {
            if (size) *size = strdup(tmp);
        } else {
            break;
        }

        argc --;
        argv ++;
    }

    GtkWidget* grid = NULL;
    if (pipes == NULL || *pipes == NULL) {
        grid = make_terminal(cwd, argc, argv);
    } else {
        /* read stdin, write stdin, read stdout, write stdout */
        int fds[] = {-1, -1, -1, -1};

        if ( ( !(*pipes)[0] || pipe(fds) >= 0 ) && ( !(*pipes)[1] || pipe(fds+2) >= 0 ) ) {
            (*pipes)[0] = fds[1]; // stdin
            (*pipes)[1] = fds[2]; // stdout

            int* child_fds = malloc(sizeof(int) * 2);
            child_fds[0] = fds[0];
            child_fds[1] = fds[3];
            grid = make_terminal_full(cwd, argc, argv, (GSpawnChildSetupFunc)term_setup_pipes, child_fds, NULL);

            GtkWidget* terminal = g_object_get_data(G_OBJECT(grid), "terminal");
            g_object_set_data(G_OBJECT(terminal), "child_fds", child_fds);
        } else {
            g_warning("Failed to create pipes: %s", strerror(errno));
            for (int i = 0; i < sizeof(fds)/sizeof(int); i ++) {
                if (fds[i] >= 0) close(fds[i]);
            }
        }
    }

    if (original) g_strfreev(original);
    return grid;
}

GtkWidget* new_tab(VteTerminal* terminal, char* data, int** pipes) {
    GtkWidget* widget = new_term(data, NULL, pipes);
    if (widget) add_tab_to_window(GTK_WIDGET(get_active_window()), widget, -1);
    return widget;
}

GtkWidget* new_window(VteTerminal* terminal, char* data, int** pipes) {
    GtkWidget* widget = new_term(data, NULL, pipes);
    if (widget) add_tab_to_window(make_window(), widget, -1);
    return widget;
}

void on_split_resize(GtkWidget* paned, GdkRectangle *rect, int value) {
    gtk_paned_set_position(GTK_PANED(paned), value);
    g_signal_handlers_disconnect_by_func(paned, on_split_resize, GINT_TO_POINTER(value));
}

GtkWidget* make_split(VteTerminal* terminal, char* data, GtkOrientation orientation, gboolean after, int** pipes) {
    GtkWidget* dest = term_get_grid(terminal);
    char* size_str = NULL;
    GtkWidget* grid = new_term(data, &size_str, pipes);
    if (! grid) {
        return NULL;
    }

    // get the available size before splitting
    GdkRectangle rect;
    gtk_widget_get_allocation(dest, &rect);
    int split_size = orientation == GTK_ORIENTATION_HORIZONTAL ? rect.width : rect.height;

    GtkWidget* paned = split(dest, grid, orientation, after);

    if (size_str) {
        char* suffix;
        int size = strtol(size_str, &suffix, 10);
        if (size) {
            if (STR_EQUAL(suffix, "%")) {
                size = split_size*size/100;
            } else if (STR_EQUAL(suffix, "px")) {
                //
            } else {
                size *= orientation == GTK_ORIENTATION_HORIZONTAL ? vte_terminal_get_char_width(terminal) : vte_terminal_get_char_height(terminal);
            }

            if (show_scrollbar && orientation == GTK_ORIENTATION_HORIZONTAL) {
                GtkWidget* scrollbar = g_object_get_data(G_OBJECT(dest), "scrollbar");
                gtk_widget_get_allocation(scrollbar, &rect);
                size += rect.width;
            }

            if (after) {
                size = split_size - size - split_get_separator_size(paned);
            }
            g_signal_connect(paned, "size-allocate", G_CALLBACK(on_split_resize), GINT_TO_POINTER(size));
        }
        free(size_str);
    }

    // focus the new terminal
    terminal = g_object_get_data(G_OBJECT(grid), "terminal");
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
    return grid;
}

GtkWidget* split_left(VteTerminal* terminal, char* data, int** pipes) {
    return make_split(terminal, data, GTK_ORIENTATION_HORIZONTAL, FALSE, pipes);
}

GtkWidget* split_right(VteTerminal* terminal, char* data, int** pipes) {
    return make_split(terminal, data, GTK_ORIENTATION_HORIZONTAL, TRUE, pipes);
}

GtkWidget* split_above(VteTerminal* terminal, char* data, int** pipes) {
    return make_split(terminal, data, GTK_ORIENTATION_VERTICAL, FALSE, pipes);
}

GtkWidget* split_below(VteTerminal* terminal, char* data, int** pipes) {
    return make_split(terminal, data, GTK_ORIENTATION_VERTICAL, TRUE, pipes);
}

void jump_tab(VteTerminal* terminal, int delta) {
    GtkNotebook* notebook = GTK_NOTEBOOK(term_get_notebook(terminal));
    int n = gtk_notebook_get_current_page(notebook);
    int pages = gtk_notebook_get_n_pages(notebook);
    gtk_notebook_set_current_page(notebook, (n+delta) % pages);
}

void prev_tab(VteTerminal* terminal) {
    jump_tab(terminal, -1);
}

void next_tab(VteTerminal* terminal) {
    jump_tab(terminal, 1);
}

void move_tab(VteTerminal* terminal, int delta) {
    GtkWidget* tab = term_get_tab(terminal);
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_widget_get_parent(tab));

    int n = gtk_notebook_get_current_page(notebook);
    int pages = gtk_notebook_get_n_pages(notebook);
    gtk_notebook_reorder_child(notebook, tab, (n+delta) % pages);
}

void move_tab_prev(VteTerminal* terminal) {
    move_tab(terminal, -1);
}

void move_tab_next(VteTerminal* terminal) {
    move_tab(terminal, 1);
}

void detach_tab(VteTerminal* terminal) {
    GtkWidget* tab = term_get_tab(terminal);
    GtkWidget* notebook = gtk_widget_get_parent(tab);

    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) > 1) {
        g_object_ref(tab);
        gtk_container_remove(GTK_CONTAINER(notebook), tab);
        make_new_window(tab);
        g_object_unref(tab);
    }
}

void cut_tab(VteTerminal* terminal) {
    if (detaching_tab) g_object_remove_weak_pointer(G_OBJECT(detaching_tab), (void*)&detaching_tab);
    detaching_tab = term_get_tab(terminal);
    g_object_add_weak_pointer(G_OBJECT(detaching_tab), (void*)&detaching_tab);
}

void paste_tab(VteTerminal* terminal) {
    if (! detaching_tab) {
        g_warning("No tab to paste");
        return;
    }

    GtkNotebook* src_notebook = GTK_NOTEBOOK(gtk_widget_get_parent(detaching_tab));
    GtkWidget* dest_window = term_get_window(terminal);
    GtkNotebook* dest_notebook = g_object_get_data(G_OBJECT(dest_window), "notebook");

    int index = gtk_notebook_get_current_page(dest_notebook)+1;
    if (src_notebook == dest_notebook) {
        gtk_notebook_reorder_child(dest_notebook, detaching_tab, index);
    } else {
        g_object_ref(detaching_tab);
        gtk_container_remove(GTK_CONTAINER(src_notebook), detaching_tab);
        add_tab_to_window(dest_window, detaching_tab, index);
        g_object_unref(detaching_tab);
    }
    gtk_notebook_set_current_page(dest_notebook, index);

    detaching_tab = NULL;
}

void switch_to_tab(VteTerminal* terminal, int num) {
    GtkNotebook* notebook = GTK_NOTEBOOK(term_get_notebook(terminal));
    int n = gtk_notebook_get_n_pages(notebook);
    gtk_notebook_set_current_page(notebook, num >= n ? -1 : num);
}

void tab_popup_menu(VteTerminal* terminal) {
    GtkNotebook* notebook = GTK_NOTEBOOK(term_get_notebook(terminal));
    gboolean value;
    g_signal_emit_by_name(notebook, "popup-menu", &value);
}

void close_tab(VteTerminal* terminal) {
    if (! prevent_tab_close(terminal)) {
        GtkWidget* tab = term_get_tab(terminal);
        GtkContainer* notebook = GTK_CONTAINER(gtk_widget_get_parent(tab));
        gtk_container_remove(notebook, tab);
    }
}

void reload_config(VteTerminal* terminal, char* filename) {
    if (actions) {
        g_array_remove_range(actions, 0, actions->len);
    }
    load_config(filename);
}

void subprocess_finish(GObject* proc, GAsyncResult* res, void* data) {
    GError* error = NULL;
    GBytes* stdout_buf;
    if (! g_subprocess_communicate_finish(G_SUBPROCESS(proc), res, &stdout_buf, NULL, &error)) {
        if (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_BROKEN_PIPE) {
            g_warning("IO failed (%s): %s", error->message, data ? (char*)data : "");
        }
        g_error_free(error);
        return;
    }

    gsize size;
    const char* buf_data = g_bytes_get_data(stdout_buf, &size);
    VteTerminal* terminal = g_object_get_data(proc, "terminal");
    vte_terminal_feed_child_binary(terminal, (guint8*)buf_data, size);
}

void spawn_subprocess(VteTerminal* terminal, gchar* data_, char* text, char** result) {
    gint argc;
    char* data = data_ ? strdup(data_) : NULL;
    char** argv = shell_split(data, &argc);

    if (argc == 0) {
        if (text && result) {
            // put in result instead
            *result = text;
        }
        return;
    }

    GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDOUT_PIPE;
    if (text) {
        flags |= G_SUBPROCESS_FLAGS_STDIN_PIPE;
    }

    GError* error = NULL;
    GSubprocessLauncher* launcher = g_subprocess_launcher_new(flags);

    char buffer[1024];
    glong cursorx, cursory;
    char* hyperlink = NULL;

    // env vars
    vte_terminal_get_cursor_position(terminal, &cursorx, &cursory);
    g_object_get(G_OBJECT(terminal), "hyperlink-hover-uri", &hyperlink, NULL);

#define SET_ENVIRON(name, format, value) \
    ( sprintf(buffer, (format), (value)), g_subprocess_launcher_setenv(launcher, APP_PREFIX "_" #name, buffer, TRUE) )

    // figure out actual row by looking at the adjustment
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));
    cursory -= gtk_adjustment_get_value(adj);

    g_subprocess_launcher_setenv(launcher, "TERM", TERM_ENV_VAR, TRUE);
    SET_ENVIRON(PID, "%i", get_pid(terminal));
    SET_ENVIRON(FGPID, "%i", get_foreground_pid(terminal));
    SET_ENVIRON(CURSORX, "%li", cursorx);
    SET_ENVIRON(CURSORY, "%li", cursory);
    SET_ENVIRON(CONTROL_FLOW, "%i", get_term_attr(terminal).c_iflag & IXON ? 1 : 0);
    SET_ENVIRON(COLUMNS, "%li", vte_terminal_get_column_count(terminal));
    SET_ENVIRON(LINES, "%li", (long int)(gtk_adjustment_get_upper(adj) - gtk_adjustment_get_lower(adj)));
    SET_ENVIRON(ROWS, "%li", vte_terminal_get_row_count(terminal));
    /* TODO TERM? */
    if (hyperlink) SET_ENVIRON(HYPERLINK, "%s", hyperlink);

    // get x11 windowid
#ifdef GDK_WINDOWING_X11
    GdkWindow* window = gtk_widget_get_window(gtk_widget_get_toplevel(GTK_WIDGET(terminal)));
    GdkDisplay* display = gdk_window_get_display(window);
    if (GDK_IS_X11_DISPLAY(display)) {
        Window winid = gdk_x11_window_get_xid(window);
        SET_ENVIRON(XWINDOWID, "0x%lx", winid);
    }
#endif

    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, (const char**)argv, &error);
    if (!proc) {
        g_warning("Failed to run (%s): %s", error->message, data);
        g_error_free(error);
        return;
    }

    g_object_set_data(G_OBJECT(proc), "terminal", g_object_ref(terminal));

    GBytes* stdin_bytes = text ? g_bytes_new_take(text, strlen(text)) : NULL;
    g_subprocess_communicate_async(proc, stdin_bytes, NULL, subprocess_finish, data);
}

void run(VteTerminal* terminal, char* data) {
    spawn_subprocess(terminal, data, NULL, NULL);
}

void pipe_screen(VteTerminal* terminal, char* data, char**result) {
    int upper, lower;
    term_get_row_positions(terminal, &lower, &upper, NULL, NULL);
    char* text = term_get_text(terminal, lower, 0, upper, -1, FALSE);
    spawn_subprocess(terminal, data, text, result);
}

void pipe_screen_ansi(VteTerminal* terminal, char* data, char** result) {
    int upper, lower;
    term_get_row_positions(terminal, &lower, &upper, NULL, NULL);
    char* text = term_get_text(terminal, lower, 0, upper, -1, TRUE);
    spawn_subprocess(terminal, data, text, result);
}

void pipe_all(VteTerminal* terminal, char* data, char** result) {
    int upper, lower;
    term_get_row_positions(terminal, NULL, NULL, &lower, &upper);
    char* text = term_get_text(terminal, lower, 0, upper, -1, FALSE);
    spawn_subprocess(terminal, data, text, result);
}

void pipe_all_ansi(VteTerminal* terminal, char* data, char** result) {
    int upper, lower;
    term_get_row_positions(terminal, NULL, NULL, &lower, &upper);
    char* text = term_get_text(terminal, lower, 0, upper, -1, TRUE);
    spawn_subprocess(terminal, data, text, result);
}

void move_split_right(VteTerminal* terminal) {
    split_move(term_get_grid(terminal), GTK_ORIENTATION_HORIZONTAL, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
}

void move_split_left(VteTerminal* terminal) {
    split_move(term_get_grid(terminal), GTK_ORIENTATION_HORIZONTAL, FALSE);
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
}

void move_split_above(VteTerminal* terminal) {
    split_move(term_get_grid(terminal), GTK_ORIENTATION_VERTICAL, FALSE);
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
}

void move_split_below(VteTerminal* terminal) {
    split_move(term_get_grid(terminal), GTK_ORIENTATION_VERTICAL, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
}

void focus_split_right(VteTerminal* terminal) {
    split_move_focus(term_get_grid(terminal), GTK_ORIENTATION_HORIZONTAL, TRUE);
}

void focus_split_left(VteTerminal* terminal) {
    split_move_focus(term_get_grid(terminal), GTK_ORIENTATION_HORIZONTAL, FALSE);
}

void focus_split_above(VteTerminal* terminal) {
    split_move_focus(term_get_grid(terminal), GTK_ORIENTATION_VERTICAL, FALSE);
}

void focus_split_below(VteTerminal* terminal) {
    split_move_focus(term_get_grid(terminal), GTK_ORIENTATION_VERTICAL, TRUE);
}

void show_message_bar(VteTerminal* terminal, char* data) {
    int timeout = -1;
    if (strncmp(data, "timeout=", sizeof("timeout=")-1) == 0) {
        timeout = strtol(data + sizeof("timeout=") - 1, &data, 10);
        // find whitespace
        while (! g_ascii_isspace(*data) ) data++;
        data ++;
    }
    if (timeout) {
        term_show_message_bar(terminal, data, timeout);
    } else {
        term_hide_message_bar(terminal);
    }
}

ActionFunc hide_message_bar = (ActionFunc)term_hide_message_bar;

void do_terminal_select(VteTerminal* terminal, char* data, int modifiers) {
    /* x1,y1,x2,y2 */
    long args[4];
    char* string = data;

    for (int i = 0; i < 4; i++) {
        if (! string) {
            return;
        }

        args[i] = strtol(string, &string, 10);
        string = strchr(string, ',');
        if (string) string ++;
    }

    term_select_range(terminal, args[0], args[1], args[2], args[3], modifiers);
}

void select_range(VteTerminal* terminal, char* data) {
    do_terminal_select(terminal, data, GDK_SHIFT_MASK);
}

void select_block(VteTerminal* terminal, char* data) {
    do_terminal_select(terminal, data, GDK_CONTROL_MASK | GDK_MOD1_MASK);
}

#define SEARCH(dir) \
    *result = g_strdup_printf("%i\n", term_search(terminal, data, dir) ? 1 : 0);

void search_down(VteTerminal* terminal, char* data, char** result) {
    SEARCH(1);
}

void search_up(VteTerminal* terminal, char* data, char** result) {
    SEARCH(-1);
}

void search(VteTerminal* terminal, char* data, char** result) {
    SEARCH(0);
}

void focus_searchbar(VteTerminal* terminal) {
    GtkWidget* grid = term_get_grid(terminal);
    GtkWidget* bar = g_object_get_data(G_OBJECT(grid), "searchbar");
    search_bar_show(bar);
}

void hide_searchbar(VteTerminal* terminal) {
    GtkWidget* grid = term_get_grid(terminal);
    GtkWidget* bar = g_object_get_data(G_OBJECT(grid), "searchbar");
    search_bar_hide(bar);
}

char* str_unescape(char* string) {
    // modifies in place
    char* p = string;
    size_t len = strlen(string);
    int shift;

    while ((p = strchr(p, '\\'))) {
        shift = 1;
        switch (p[1]) {
            case '\\': break;
            case 'n': *p = '\n'; break;
            case 'r': *p = '\r'; break;
            case 't': *p = '\t'; break;
            case 'v': *p = '\v'; break;
            case 'a': *p = '\a'; break;

            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
                shift = sscanf(p+1, "%3o", (unsigned int*)p) + 1;
                break;

            case 'x':
                if (p+2 < string+len) {
                    shift = sscanf(p+2, "%2x", (unsigned int*)p) + 2;
                }
                break;

            default: shift = 0;
        }

        p++;
        if (shift) {
            memmove(p, p+shift, len-(p-string)-shift+1);
        }
    }
    return string;
}

Action make_action(char* name, char* arg) {
    Action action = {NULL, NULL, NULL};

#define MATCH_ACTION_WITH_DATA_DEFAULT(_name, processor, _cleanup, default) \
    if (STR_EQUAL(name, #_name)) { \
        action.func = (ActionFunc)_name; \
        if (arg) { \
            arg = str_unescape(arg); \
            action.data = processor; \
            action.cleanup = _cleanup; \
        } else { \
            action.data = default; \
        } \
        break; \
    }
#define MATCH_ACTION_WITH_DATA(name, processor, _cleanup) MATCH_ACTION_WITH_DATA_DEFAULT(name, processor, _cleanup, NULL)
#define MATCH_ACTION(name) MATCH_ACTION_WITH_DATA(name, NULL, NULL)

    while (1) {
        MATCH_ACTION_WITH_DATA(paste_text, strdup(arg), free);
        MATCH_ACTION(copy_text);
        MATCH_ACTION_WITH_DATA(change_font_size, strdup(arg), free);
        MATCH_ACTION(reset_terminal);
        MATCH_ACTION(scroll_up);
        MATCH_ACTION(scroll_down);
        MATCH_ACTION(scroll_page_up);
        MATCH_ACTION(scroll_page_down);
        MATCH_ACTION(scroll_top);
        MATCH_ACTION(scroll_bottom);
        MATCH_ACTION(select_all);
        MATCH_ACTION(unselect_all);
        MATCH_ACTION_WITH_DATA(feed_data, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(feed_term, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(new_tab, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(new_window, strdup(arg), free);
        MATCH_ACTION(prev_tab);
        MATCH_ACTION(next_tab);
        MATCH_ACTION(move_tab_prev);
        MATCH_ACTION(move_tab_next);
        MATCH_ACTION(detach_tab);
        MATCH_ACTION(cut_tab);
        MATCH_ACTION(paste_tab);
        MATCH_ACTION_WITH_DATA_DEFAULT(switch_to_tab, GINT_TO_POINTER(atoi(arg)), NULL, 0);
        MATCH_ACTION(tab_popup_menu);
        MATCH_ACTION_WITH_DATA(reload_config, strdup(arg), free);
        MATCH_ACTION(close_tab);
        MATCH_ACTION_WITH_DATA(add_css_class, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(remove_css_class, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(run, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(pipe_screen, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(pipe_screen_ansi, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(pipe_all, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(pipe_all_ansi, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(split_right, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(split_left, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(split_above, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(split_below, strdup(arg), free);
        MATCH_ACTION(move_split_right);
        MATCH_ACTION(move_split_left);
        MATCH_ACTION(move_split_above);
        MATCH_ACTION(move_split_below);
        MATCH_ACTION(focus_split_right);
        MATCH_ACTION(focus_split_left);
        MATCH_ACTION(focus_split_above);
        MATCH_ACTION(focus_split_below);
        MATCH_ACTION_WITH_DATA(show_message_bar, strdup(arg), free);
        MATCH_ACTION(hide_message_bar);
        MATCH_ACTION_WITH_DATA(select_range, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(select_block, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(search_down, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(search_up, strdup(arg), free);
        MATCH_ACTION_WITH_DATA(search, strdup(arg), free);
        MATCH_ACTION(focus_searchbar);
        MATCH_ACTION(hide_searchbar);
        break;
    }
    return action;
}
