#include <vte/vte.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include "callback.h"
#include "window.h"
#include "terminal.h"
#include "config.h"
#include "split.h"

GtkWidget* detaching_tab = NULL;

CallbackFunc select_all = (CallbackFunc)vte_terminal_select_all;
CallbackFunc unselect_all = (CallbackFunc)vte_terminal_unselect_all;

void paste_text(VteTerminal* terminal, void* data) {
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

void feed_data(VteTerminal* terminal, gchar* data) {
    vte_terminal_feed_child_binary(terminal, (guint8*)data, strlen(data));
}

void feed_term(VteTerminal* terminal, char* data) {
    vte_terminal_feed(terminal, (char*)data, -1);
}

GtkWidget* new_term(gchar* data) {
    gint argc;
    char* cwd = NULL;
    char **original, **argv = shell_split(data, &argc);
    if (data && ! argv) {
        g_warning("Failed to parse: %s", data);
        return NULL;
    }

    original = argv;
    if (argc > 0 && strncmp(argv[0], "cwd=", 4) == 0) {
        cwd = argv[0] + 4;
        argc --;
        argv ++;
    }

    GtkWidget* grid = make_terminal(cwd, argc, argv);
    if (original) g_strfreev(original);
    return grid;
}

void new_tab(VteTerminal* terminal, gchar* data) {
    GtkWidget* widget = new_term(data);
    add_tab_to_window(GTK_WIDGET(get_active_window()), widget, -1);
}

void new_window(VteTerminal* terminal, gchar* data) {
    GtkWidget* widget = new_term(data);
    GtkWidget* window = make_window();
    add_tab_to_window(window, widget, -1);
}

void make_split(VteTerminal* terminal, gchar* data, GtkOrientation orientation, gboolean after) {
    GtkWidget* dest = term_get_grid(terminal);
    GtkWidget* src = new_term(data);
    split(dest, src, orientation, after);

    // focus the new terminal
    terminal = g_object_get_data(G_OBJECT(src), "terminal");
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
}

void split_left(VteTerminal* terminal, gchar* data) {
    make_split(terminal, data, GTK_ORIENTATION_HORIZONTAL, FALSE);
}

void split_right(VteTerminal* terminal, gchar* data) {
    make_split(terminal, data, GTK_ORIENTATION_HORIZONTAL, TRUE);
}

void split_above(VteTerminal* terminal, gchar* data) {
    make_split(terminal, data, GTK_ORIENTATION_VERTICAL, FALSE);
}

void split_below(VteTerminal* terminal, gchar* data) {
    make_split(terminal, data, GTK_ORIENTATION_VERTICAL, TRUE);
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
    if (! detaching_tab) return;

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

void switch_to_tab(VteTerminal* terminal, int data) {
    GtkNotebook* notebook = GTK_NOTEBOOK(term_get_notebook(terminal));
    int n = gtk_notebook_get_n_pages(notebook);
    gtk_notebook_set_current_page(notebook, data >= n ? -1 : data);
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
    if (callbacks) {
        g_array_remove_range(callbacks, 0, callbacks->len);
    }
    load_config(filename);
}

void subprocess_finish(GObject* proc, GAsyncResult* res, void* data) {
    GError* error = NULL;
    GBytes* stdout_buf;
    if (! g_subprocess_communicate_finish(G_SUBPROCESS(proc), res, &stdout_buf, NULL, &error)) {
        g_warning("IO failed (%s): %s\n", error->message, data ? (char*)data : "");
        g_error_free(error);
        return;
    }

    gsize size;
    const char* buf_data = g_bytes_get_data(stdout_buf, &size);
    VteTerminal* terminal = g_object_get_data(proc, "terminal");
    vte_terminal_feed_child_binary(terminal, (guint8*)buf_data, size);
}

void spawn_subprocess(VteTerminal* terminal, gchar* data_, GBytes* stdin_bytes, void** result) {
    gint argc;
    char* data = data_ ? strdup(data_) : NULL;
    char** argv = shell_split(data, &argc);

    if (argc == 0) {
        if (stdin_bytes && result) {
            // put in result instead
            gsize size;
            *result = (void*)g_bytes_get_data(stdin_bytes, &size);
        }
        return;
    }

    GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDOUT_PIPE;
    if (stdin_bytes) {
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
    Window winid = gdk_x11_window_get_xid(gtk_widget_get_window(GTK_WIDGET(terminal)));

#define SET_ENVIRON(name, format, value) \
    ( sprintf(buffer, format, value), g_subprocess_launcher_setenv(launcher, APP_PREFIX "_" #name, buffer, TRUE) )

    // figure out actual row by looking at the adjustment
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));
    cursory -= gtk_adjustment_get_value(adj);

    SET_ENVIRON(PID, "%i", get_pid(terminal));
    SET_ENVIRON(FGPID, "%i", get_foreground_pid(terminal));
    SET_ENVIRON(CURSORX, "%li", cursorx);
    SET_ENVIRON(CURSORY, "%li", cursory);
    if (hyperlink) SET_ENVIRON(HYPERLINK, "%s", hyperlink);
    if (winid) SET_ENVIRON(WINID, "0x%lx", winid);

    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, (const char**)argv, &error);
    if (!proc) {
        g_warning("Failed to run (%s): %s\n", error->message, data);
        g_error_free(error);
        return;
    }

    g_object_set_data(G_OBJECT(proc), "terminal", g_object_ref(terminal));
    g_subprocess_communicate_async(proc, stdin_bytes, NULL, subprocess_finish, data);
}

void run(VteTerminal* terminal, gchar* data) {
    spawn_subprocess(terminal, data, NULL, NULL);
}

void pipe_screen(VteTerminal* terminal, gchar* data, void* result) {
    char* stdin_buf = vte_terminal_get_text_include_trailing_spaces(terminal, NULL, NULL, NULL);
    GBytes* stdin_bytes = g_bytes_new_take(stdin_buf, strlen(stdin_buf));
    spawn_subprocess(terminal, data, stdin_bytes, result);
}

void pipe_line(VteTerminal* terminal, gchar* data, void* result) {
    glong col, row;
    vte_terminal_get_cursor_position(terminal, &col, &row);
    char* stdin_buf = vte_terminal_get_text_range(terminal, row, 0, row+1, -1, NULL, NULL, NULL);
    GBytes* stdin_bytes = g_bytes_new_take(stdin_buf, strlen(stdin_buf));
    spawn_subprocess(terminal, data, stdin_bytes, result);
}

void pipe_all(VteTerminal* terminal, gchar* data, void* result) {
    GError* error = NULL;
    GOutputStream* stream = g_memory_output_stream_new_resizable();
    gboolean success = vte_terminal_write_contents_sync(terminal, stream, VTE_WRITE_DEFAULT, NULL, &error);
    if (!success) {
        g_warning("Failed to get data: %s\n", error->message);
        g_error_free(error);
        return;
    }
    g_output_stream_close(stream, NULL, NULL);
    GBytes* stdin_bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(stream));

    spawn_subprocess(terminal, data, stdin_bytes, result);
}

void scrollback_lines(VteTerminal* terminal, int value, void** result) {
    if (value >= -1) {
        g_object_set(G_OBJECT(terminal), "scrollback-lines", value, NULL);
    }

    if (result) {
        // return the value
        g_object_get(G_OBJECT(terminal), "scrollback-lines", &value, NULL);
        *result = malloc(sizeof(char) * 8);
        sprintf(*result, "%i", value);
    }
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

Callback make_callback(char* name, char* arg) {
    Callback callback = {NULL, NULL, NULL};

#define MATCH_CALLBACK_WITH_DATA_DEFAULT(_name, processor, _cleanup, default) \
    if (strcmp(name, #_name) == 0) { \
        callback.func = (CallbackFunc)_name; \
        if (arg) { \
            arg = str_unescape(arg); \
            callback.data = processor; \
            callback.cleanup = _cleanup; \
        } else { \
            callback.data = default; \
        } \
        break; \
    }
#define MATCH_CALLBACK_WITH_DATA(name, processor, _cleanup) MATCH_CALLBACK_WITH_DATA_DEFAULT(name, processor, _cleanup, NULL)
#define MATCH_CALLBACK(name) MATCH_CALLBACK_WITH_DATA(name, NULL, NULL)

    while (1) {
        MATCH_CALLBACK_WITH_DATA(paste_text, strdup(arg), free);
        MATCH_CALLBACK(copy_text);
        MATCH_CALLBACK_WITH_DATA(change_font_size, strdup(arg), free);
        MATCH_CALLBACK(reset_terminal);
        MATCH_CALLBACK(scroll_up);
        MATCH_CALLBACK(scroll_down);
        MATCH_CALLBACK(scroll_page_up);
        MATCH_CALLBACK(scroll_page_down);
        MATCH_CALLBACK(scroll_top);
        MATCH_CALLBACK(scroll_bottom);
        MATCH_CALLBACK(select_all);
        MATCH_CALLBACK(unselect_all);
        MATCH_CALLBACK_WITH_DATA(feed_data, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(feed_term, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(new_tab, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(new_window, strdup(arg), free);
        MATCH_CALLBACK(prev_tab);
        MATCH_CALLBACK(next_tab);
        MATCH_CALLBACK(move_tab_prev);
        MATCH_CALLBACK(move_tab_next);
        MATCH_CALLBACK(detach_tab);
        MATCH_CALLBACK(cut_tab);
        MATCH_CALLBACK(paste_tab);
        MATCH_CALLBACK_WITH_DATA_DEFAULT(switch_to_tab, GINT_TO_POINTER(atoi(arg)), NULL, 0);
        MATCH_CALLBACK(tab_popup_menu);
        MATCH_CALLBACK_WITH_DATA(reload_config, strdup(arg), free);
        MATCH_CALLBACK(close_tab);
        MATCH_CALLBACK_WITH_DATA(add_label_class, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(remove_label_class, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(run, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(pipe_screen, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(pipe_line, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(pipe_all, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA_DEFAULT(scrollback_lines, GINT_TO_POINTER(atoi(arg)), NULL, GINT_TO_POINTER(-2));
        MATCH_CALLBACK_WITH_DATA(split_right, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(split_left, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(split_above, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(split_below, strdup(arg), free);
        break;
    }
    return callback;
}
