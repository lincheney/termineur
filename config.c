#include <gtk/gtk.h>
#include <vte/vte.h>
#include <ctype.h>
#include <errno.h>
#include <gio/gunixoutputstream.h>
#include "config.h"
#include "window.h"
#include "terminal.h"

char* config_filename = NULL;
char* app_id = NULL;

#define PALETTE_SIZE (16)
extern GdkRGBA palette[PALETTE_SIZE+2];
GdkRGBA palette[PALETTE_SIZE+2] = {
    { 0., 0., 0., 1. }, // background
    { 1., 1., 1., 1. }, // foreground
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
    { 1., 1., 1., 1. }, // white
};
GtkWidget* detaching_tab = NULL;
gboolean show_scrollbar = 1;
GtkCssProvider* css_provider = NULL;
char** default_args = NULL;
char* window_icon = NULL;

// terminal props
VteCursorBlinkMode terminal_cursor_blink_mode = VTE_CURSOR_BLINK_SYSTEM;
VteCursorShape terminal_cursor_shape = VTE_CURSOR_SHAPE_BLOCK;
gchar* terminal_encoding = NULL;
PangoFontDescription* terminal_font = NULL;
gdouble terminal_font_scale = 1;
gboolean terminal_audible_bell = FALSE;
gboolean terminal_allow_hyperlink = FALSE;
gboolean terminal_pointer_autohide = FALSE;
gboolean terminal_rewrap_on_resize = TRUE;
gboolean terminal_scroll_on_keystroke = TRUE;
gboolean terminal_scroll_on_output = TRUE;
guint terminal_scrollback_lines = 0;
char* terminal_word_char_exceptions = NULL;

gboolean tab_expand = TRUE;
gboolean tab_fill = TRUE;
gboolean tab_title_markup = FALSE;
gboolean notebook_enable_popup = FALSE;
gboolean notebook_scrollable = FALSE;
gboolean notebook_show_tabs = FALSE;
GtkPositionType notebook_tab_pos = GTK_POS_TOP;
int ui_refresh_interval = 5000;
PangoEllipsizeMode tab_title_ellipsize_mode = PANGO_ELLIPSIZE_END;
gfloat tab_title_alignment = 0.5;
int inactivity_duration = 10000;
gboolean window_close_confirm = TRUE;
gint tab_close_confirm = CLOSE_CONFIRM_SMART;

char** shell_split(char* string, gint* argc) {
    if (! string || *string == '\0') {
        if (argc) *argc = 0;
        return NULL;
    }

    char** array = NULL;
    if (! g_shell_parse_argv(string, argc, &array, NULL) ) {
        g_warning("Failed to shell split: %s", string);
    }
    return array;
}

void* float_to_ptr(float x) {
    void* ptr;
    memcpy(&ptr, &x, sizeof(float));
    return ptr;
}
float ptr_to_float(void* x) {
    float flt;
    memcpy(&flt, &x, sizeof(float));
    return flt;
}

/* CALLBACKS */

KeyComboCallbackFunc \
    paste_clipboard = (KeyComboCallbackFunc)vte_terminal_paste_clipboard
    , select_all = (KeyComboCallbackFunc)vte_terminal_select_all
    , unselect_all = (KeyComboCallbackFunc)vte_terminal_unselect_all
;

void copy_clipboard(VteTerminal* terminal) {
    vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
}

void change_font_size(VteTerminal* terminal, void* delta) {
    vte_terminal_set_font_scale(terminal, vte_terminal_get_font_scale(terminal)+ptr_to_float(delta));
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
void new_tab(VteTerminal* terminal, gchar* data) {
    gint argc;
    char** argv = shell_split(data, &argc);
    add_terminal_full(GTK_WIDGET(get_active_window()), NULL, argc, argv);
}
void new_window(VteTerminal* terminal, gchar* data) {
    gint argc;
    char** argv = shell_split(data, &argc);
    make_new_window_full(NULL, NULL, argc, argv);
}
void jump_tab(VteTerminal* terminal, int delta) {
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(terminal))));
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
    GtkWidget* tab = gtk_widget_get_parent(GTK_WIDGET(terminal));
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
    GtkWidget* tab = gtk_widget_get_parent(GTK_WIDGET(terminal));
    GtkContainer* notebook = GTK_CONTAINER(gtk_widget_get_parent(tab));

    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) > 1) {
        g_object_ref(tab);
        gtk_container_remove(notebook, tab);
        make_new_window(tab);
        g_object_unref(tab);
    }
}
void cut_tab(VteTerminal* terminal) {
    if (detaching_tab) g_object_remove_weak_pointer(G_OBJECT(detaching_tab), (void*)&detaching_tab);
    detaching_tab = gtk_widget_get_parent(GTK_WIDGET(terminal));
    g_object_add_weak_pointer(G_OBJECT(detaching_tab), (void*)&detaching_tab);
}
void paste_tab(VteTerminal* terminal) {
    if (! detaching_tab) return;

    GtkNotebook* src_notebook = GTK_NOTEBOOK(gtk_widget_get_parent(detaching_tab));
    GtkWidget* dest_window = gtk_widget_get_toplevel(GTK_WIDGET(terminal));
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
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(terminal))));
    int n = gtk_notebook_get_n_pages(notebook);
    gtk_notebook_set_current_page(notebook, data >= n ? -1 : data);
}
void tab_popup_menu(VteTerminal* terminal) {
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(terminal))));
    gboolean value;
    g_signal_emit_by_name(notebook, "popup-menu", &value);
}
void close_tab(VteTerminal* terminal) {
    if (! prevent_tab_close(terminal)) {
        GtkWidget* tab = gtk_widget_get_parent(GTK_WIDGET(terminal));
        GtkContainer* notebook = GTK_CONTAINER(gtk_widget_get_parent(tab));
        gtk_container_remove(notebook, tab);
    }
}
void reload_config() {
    if (keyboard_shortcuts) {
        g_array_remove_range(keyboard_shortcuts, 0, keyboard_shortcuts->len);
    }
    load_config();
}
void spawn_subprocess(VteTerminal* terminal, gchar* data, GBytes* stdin_bytes, void** result) {
    gint argc;
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
    GBytes* stdout_buf;
    GSubprocessLauncher* launcher = g_subprocess_launcher_new(flags);

    char buffer[1024];
    sprintf(buffer, "%i", get_pid(terminal));
    g_subprocess_launcher_setenv(launcher, APP_PREFIX "_PID", buffer, TRUE);
    sprintf(buffer, "%i", get_foreground_pid(terminal));
    g_subprocess_launcher_setenv(launcher, APP_PREFIX "_FGPID", buffer, TRUE);

    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, (const char**)argv, &error);
    if (!proc) {
        g_warning("Failed to run (%s): %s\n", error->message, data);
        g_error_free(error);
        return;
    }

    error = NULL;
    if (! g_subprocess_communicate(proc, stdin_bytes, NULL, &stdout_buf, NULL, &error)) {
        g_warning("IO failed (%s): %s\n", error->message, data);
        g_error_free(error);
    } else {
        gsize size;
        const char* buf_data = g_bytes_get_data(stdout_buf, &size);
        vte_terminal_feed_child_binary(terminal, (guint8*)buf_data, size);
    }

    g_subprocess_wait(proc, NULL, NULL);
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

char* str_unescape(char* string) {
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

void configure_terminal(GtkWidget* terminal) {
    g_object_set(G_OBJECT(terminal),
            "cursor-blink-mode",   terminal_cursor_blink_mode,
            "cursor-shape",        terminal_cursor_shape,
            "encoding",            terminal_encoding,
            "font-desc",           terminal_font,
            "font-scale",          terminal_font_scale,
            "audible-bell",        terminal_audible_bell,
            "allow-hyperlink",     terminal_allow_hyperlink,
            "pointer-autohide",    terminal_pointer_autohide,
            "rewrap-on-resize",    terminal_rewrap_on_resize,
            "scroll-on-keystroke", terminal_scroll_on_keystroke,
            "scroll-on-output",    terminal_scroll_on_output,
            "scrollback-lines",    terminal_scrollback_lines,
            NULL
    );
    vte_terminal_set_word_char_exceptions(VTE_TERMINAL(terminal), terminal_word_char_exceptions);
    // populate palette
    vte_terminal_set_colors(VTE_TERMINAL(terminal), palette+1, palette, palette+2, PALETTE_SIZE);

    GtkWidget* label = g_object_get_data(G_OBJECT(terminal), "label");
    g_object_set(G_OBJECT(label),
            "ellipsize",  tab_title_ellipsize_mode,
            "xalign",     tab_title_alignment,
            "use-markup", tab_title_markup,
            NULL);

    enable_terminal_scrollbar(terminal, show_scrollbar);
}

void configure_tab(GtkContainer* notebook, GtkWidget* tab) {
    gtk_container_child_set(GTK_CONTAINER(notebook), tab,
            "tab-expand", tab_expand,
            "tab-fill",   tab_fill,
            NULL);
}

void configure_window(GtkWindow* window) {
    gtk_window_set_icon_name(window, window_icon);
    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    g_object_set(notebook,
            "enable-popup", notebook_enable_popup,
            "scrollable",   notebook_scrollable,
            "show-tabs",    notebook_show_tabs,
            "tab-pos",      notebook_tab_pos,
            NULL);
}

void reconfigure_window(GtkWindow* window) {
    configure_window(window);

    GtkWidget *terminal, *tab;
    GtkNotebook* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    int n = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n; i ++) {
        tab = gtk_notebook_get_nth_page(notebook, i);
        configure_tab(GTK_CONTAINER(notebook), tab);
        terminal = g_object_get_data(G_OBJECT(tab), "terminal");
        configure_terminal(terminal);
        update_terminal_ui(VTE_TERMINAL(terminal));
    }
}

KeyComboCallback lookup_callback(char* value) {
    char* arg = strchr(value, ':');
    if (arg) {
        *arg = '\0';
        arg++;
    }
    KeyComboCallback callback = {NULL, NULL, NULL};

#define MATCH_CALLBACK_WITH_DATA(name, processor, _cleanup) \
    if (strcmp(value, #name) == 0) { \
        callback.func = (KeyComboCallbackFunc)name; \
        if (arg) { \
            arg = str_unescape(arg); \
            callback.data = processor; \
            callback.cleanup = _cleanup; \
        } \
        break; \
    }
#define MATCH_CALLBACK(name) MATCH_CALLBACK_WITH_DATA(name, NULL, NULL)

    while (1) {
        MATCH_CALLBACK(paste_clipboard);
        MATCH_CALLBACK(copy_clipboard);
        MATCH_CALLBACK_WITH_DATA(change_font_size, float_to_ptr(strtof(arg, NULL)), NULL);
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
        MATCH_CALLBACK_WITH_DATA(new_tab, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(new_window, strdup(arg), free);
        MATCH_CALLBACK(prev_tab);
        MATCH_CALLBACK(next_tab);
        MATCH_CALLBACK(move_tab_prev);
        MATCH_CALLBACK(move_tab_next);
        MATCH_CALLBACK(detach_tab);
        MATCH_CALLBACK(cut_tab);
        MATCH_CALLBACK(paste_tab);
        MATCH_CALLBACK_WITH_DATA(switch_to_tab, GINT_TO_POINTER(atoi(arg)), NULL);
        MATCH_CALLBACK(tab_popup_menu);
        MATCH_CALLBACK(reload_config);
        MATCH_CALLBACK(close_tab);
        MATCH_CALLBACK_WITH_DATA(add_label_class, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(remove_label_class, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(run, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(pipe_screen, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(pipe_line, strdup(arg), free);
        MATCH_CALLBACK_WITH_DATA(pipe_all, strdup(arg), free);
        break;
    }
    return callback;
}

int set_config_from_str(char* line, size_t len) {
    char* value = strchr(line, '=');
    if (! value) return 0; // invalid line

    *value = '\0';
    // whitespace trimming
    char* c;
    for (c = value-1; isspace(*c); c--) ;; c[1] = '\0';
    value++;
    while( isspace(*value) ) value++;
    for (c = line+len-1; isspace(*c); c--) ;; c[1] = '\0';

#define LINE_EQUALS(string) (strcmp(line, (#string)) == 0)
#define MAP_LINE(string, body) \
    if (LINE_EQUALS(string)) { \
        body; \
        return 1; \
    }
#define MAP_VALUE(var, ...) do { \
        struct mapping {char* name; int value; } map[] = {__VA_ARGS__}; \
        for(int i = 0; i < sizeof(map) / sizeof(struct mapping); i++) { \
            if (g_ascii_strcasecmp(value, map[i].name) == 0) { \
                var = map[i].value; \
                break; \
            } \
        } \
    } while(0)
#define MAP_LINE_VALUE(string, ...) MAP_LINE(string, MAP_VALUE(__VA_ARGS__))

    // palette colours
    if (strncmp(line, "col", 3) == 0) {
        errno = 0;
        char* endptr = NULL;
        int n = strtol(line+3, &endptr, 0);
        if ((! errno) && *endptr == '\0' && 0 <= n && n < 16) {
            gdk_rgba_parse(palette+2+n, value);
            return 1;
        }
    }

#define PARSE_BOOL(string) ( ! ( \
    g_ascii_strcasecmp((string), "no") == 0 \
    || g_ascii_strcasecmp((string), "n") == 0 \
    || g_ascii_strcasecmp((string), "false") == 0 \
    || strcmp((string), "") == 0 \
    || strcmp((string), "0") == 0 \
    ))

    if (LINE_EQUALS(css)) {
        gtk_css_provider_load_from_data(css_provider, value, -1, NULL);
        GdkScreen* screen = gdk_screen_get_default();
        gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        return 1;
    }

    MAP_LINE(background,           gdk_rgba_parse(palette, value));
    MAP_LINE(foreground,           gdk_rgba_parse(palette+1, value));
    MAP_LINE(window-title-format,  set_window_title_format(value));
    MAP_LINE(tab-title-format,     set_tab_title_format(value));
    MAP_LINE(tab-fill,             tab_fill                      = PARSE_BOOL(value));
    MAP_LINE(tab-expand,           tab_expand                    = PARSE_BOOL(value));
    MAP_LINE(tab-enable-popup,     notebook_enable_popup         = PARSE_BOOL(value));
    MAP_LINE(tab-scrollable,       notebook_scrollable           = PARSE_BOOL(value));
    MAP_LINE(show-tabs,            notebook_show_tabs            = PARSE_BOOL(value));
    MAP_LINE(ui-refresh-interval,  ui_refresh_interval           = atoi(value));
    MAP_LINE(tab-title-markup,     tab_title_markup              = PARSE_BOOL(value));
    MAP_LINE(inactivity-duration,  inactivity_duration           = atoi(value));
    MAP_LINE(encoding,             free(terminal_encoding); terminal_encoding = strdup(value));
    MAP_LINE(font,                 terminal_font                 = pango_font_description_from_string(value));
    MAP_LINE(font-scale,           terminal_font_scale           = strtod(value, NULL));
    MAP_LINE(audible-bell,         terminal_audible_bell         = PARSE_BOOL(value));
    MAP_LINE(allow-hyperlink,      terminal_allow_hyperlink      = PARSE_BOOL(value));
    MAP_LINE(pointer-autohide,     terminal_pointer_autohide     = PARSE_BOOL(value));
    MAP_LINE(rewrap-on-resize,     terminal_rewrap_on_resize     = PARSE_BOOL(value));
    MAP_LINE(scroll-on-keystroke,  terminal_scroll_on_keystroke  = PARSE_BOOL(value));
    MAP_LINE(scroll-on-output,     terminal_scroll_on_output     = PARSE_BOOL(value));
    MAP_LINE(scrollback-lines,     terminal_scrollback_lines     = atoi(value));
    MAP_LINE(word-char-exceptions, free(terminal_word_char_exceptions); terminal_word_char_exceptions = strdup(value));
    MAP_LINE(show-scrollbar,       show_scrollbar                = PARSE_BOOL(value));
    MAP_LINE(window-icon,          free(window_icon);  window_icon = strdup(value));
    MAP_LINE(window-close-confirm, window_close_confirm          = PARSE_BOOL(value));

    if (LINE_EQUALS(tab-close-confirm)) {
        if (g_ascii_strcasecmp(value, "smart") == 0) {
            tab_close_confirm = CLOSE_CONFIRM_SMART;
        } else if (PARSE_BOOL(value)) {
            tab_close_confirm = CLOSE_CONFIRM_YES;
        } else {
            tab_close_confirm = CLOSE_CONFIRM_NO;
        }
        return 1;
    }

    if (LINE_EQUALS(default-args)) {
        g_strfreev(default_args);
        default_args = shell_split(value, NULL);
        return 1;
    }

    MAP_LINE_VALUE(tab-pos, notebook_tab_pos,
            {"top",    GTK_POS_TOP},
            {"bottom", GTK_POS_BOTTOM},
            {"left",   GTK_POS_LEFT},
            {"right",  GTK_POS_RIGHT},
    );

    MAP_LINE_VALUE(tab-title-ellipsize-mode, tab_title_ellipsize_mode,
            {"start",  PANGO_ELLIPSIZE_START},
            {"middle", PANGO_ELLIPSIZE_MIDDLE},
            {"end",    PANGO_ELLIPSIZE_END},
    );

    MAP_LINE_VALUE(tab-title-alignment, tab_title_alignment,
            {"left",   0},
            {"right",  1},
            {"center", 0.5},
    );

    MAP_LINE_VALUE(cursor-blink-mode, terminal_cursor_blink_mode,
            {"system", VTE_CURSOR_BLINK_SYSTEM},
            {"on",     VTE_CURSOR_BLINK_ON},
            {"off",    VTE_CURSOR_BLINK_OFF},
    );

    MAP_LINE_VALUE(cursor-shape, terminal_cursor_shape,
            {"block",     VTE_CURSOR_SHAPE_BLOCK},
            {"ibeam",     VTE_CURSOR_SHAPE_IBEAM},
            {"underline", VTE_CURSOR_SHAPE_UNDERLINE},
    );

    if (strncmp(line, "key-", sizeof("key-")-1) == 0) {
        char* shortcut = line + sizeof("key-")-1;

        KeyCombo combo = {0, 0, {NULL, NULL, NULL}};
        gtk_accelerator_parse(shortcut, &(combo.key), &(combo.modifiers));
        if (combo.modifiers & GDK_SHIFT_MASK) {
            combo.key = gdk_keyval_to_upper(combo.key);
        }

        if (strcmp(value, "") == 0) {
            // unset this shortcut
            for (int i = keyboard_shortcuts->len - 1; i >= 0; i--) {
                KeyCombo* kc = &g_array_index(keyboard_shortcuts, KeyCombo, i);
                if (kc->key == combo.key && kc->modifiers == combo.modifiers) {
                    g_array_remove_index(keyboard_shortcuts, i);
                }
            }
            return 1;
        }

        KeyComboCallback callback = lookup_callback(value);
        if (callback.func) {
            combo.callback = callback;
            g_array_append_val(keyboard_shortcuts, combo);
        } else {
            g_warning("Unrecognised action: %s", value);
        }
        return 1;
    }

    return 0;
}

void reconfigure_all() {
    // reload config everywhere
    foreach_window((GFunc)reconfigure_window, NULL);
    create_timer(ui_refresh_interval);
}

void free_key_combo(KeyCombo* kc) {
    if (kc->callback.cleanup)
        kc->callback.cleanup(kc->callback.data);
}

void* execute_line(char* line, int size, gboolean reconfigure) {
    if (set_config_from_str(line, size)) {
        if (reconfigure) {
            reconfigure_all();
        }
        return NULL;
    }

    KeyComboCallback callback = lookup_callback(line);
    if (callback.func) {
        VteTerminal* terminal = get_active_terminal(NULL);
        if (terminal) {
            void* data = NULL;
            callback.func(terminal, callback.data, &data);
            return data;
        }
        return NULL;
    }

    return NULL;
}

void load_config() {
    // init some things
    if (! keyboard_shortcuts) {
        keyboard_shortcuts = g_array_new(FALSE, FALSE, sizeof(KeyCombo));
        g_array_set_clear_func(keyboard_shortcuts, (GDestroyNotify)free_key_combo);
    }
    if (! css_provider) {
        css_provider = gtk_css_provider_new();
    }

    if (! config_filename) return;
    FILE* config = fopen(config_filename, "r");
    if (!config) {
        /* if (error) g_warning("Error loading key file: %s", error->message); */
        return;
    }

    char* line = NULL;
    char* buffer = NULL;
    size_t size = 0, bufsize = 0;
    ssize_t len, l;
    while ((len = getline(&line, &size, config)) != -1) {
        // multilines
        if (len >= 4 && strcmp(line+len-4, "\"\"\"\n") == 0) {
            len -= 4;
            while ((l = getline(&buffer, &bufsize, config)) != -1) {
                len += l;
                if (len >= size) {
                    size = len+1;
                    line = realloc(line, size);
                }
                memcpy(line+len-l, buffer, l);

                if (strncmp(line+len-4, "\"\"\"\n", 4) == 0) {
                    line[len-4] = '\0';
                    break;
                }
            }
        }

        if (line[0] == '#' || line[0] == ';') continue; // comment
        free(execute_line(line, len, FALSE));
    }

    fclose(config);
    if (line) free(line);
    if (buffer) free(buffer);

    reconfigure_all();
}
