#include <gtk/gtk.h>
#include <vte/vte.h>
#include <ctype.h>
#include <errno.h>
#include "config.h"
#include "window.h"
#include "terminal.h"

char* config_filename = NULL;

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

// notebook props
gboolean tab_expand = FALSE;
gboolean tab_fill = TRUE;
gboolean tab_title_markup = FALSE;
gboolean notebook_enable_popup = FALSE;
gboolean notebook_scrollable = FALSE;
gboolean notebook_show_tabs = TRUE;
GtkPositionType notebook_tab_pos = GTK_POS_TOP;
int ui_refresh_interval = 2000;
PangoEllipsizeMode tab_title_ellipsize_mode = PANGO_ELLIPSIZE_END;
gfloat tab_title_alignment = 0.5;
int inactivity_duration = 2000;

/* CALLBACKS */

KeyComboCallback \
    paste_clipboard = (KeyComboCallback)vte_terminal_paste_clipboard
    , select_all = (KeyComboCallback)vte_terminal_select_all
    , unselect_all = (KeyComboCallback)vte_terminal_unselect_all
    , reload_config = (KeyComboCallback)load_config
;

void copy_clipboard(VteTerminal* terminal) {
    vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
}

void increase_font_size(VteTerminal* terminal) {
    vte_terminal_set_font_scale(terminal, vte_terminal_get_font_scale(terminal)+0.2);
}
void decrease_font_size(VteTerminal* terminal) {
    vte_terminal_set_font_scale(terminal, vte_terminal_get_font_scale(terminal)-0.2);
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
void new_tab(VteTerminal* terminal) {
    add_terminal(GTK_WIDGET(get_active_window()));
}
void new_window(VteTerminal* terminal) {
    make_new_window(NULL);
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

    g_object_ref(tab);
    gtk_container_remove(notebook, tab);
    make_new_window(tab);
    g_object_unref(tab);
}
void cut_tab(VteTerminal* terminal) {
    if (detaching_tab) g_object_remove_weak_pointer(G_OBJECT(detaching_tab), (void*)&detaching_tab);
    detaching_tab = gtk_widget_get_parent(GTK_WIDGET(terminal));
    g_object_add_weak_pointer(G_OBJECT(detaching_tab), (void*)&detaching_tab);
}
void paste_tab(VteTerminal* terminal) {
    if (detaching_tab) {
        GtkContainer* src_notebook = GTK_CONTAINER(gtk_widget_get_parent(detaching_tab));
        GtkWidget* dest_window = gtk_widget_get_toplevel(GTK_WIDGET(terminal));
        GtkNotebook* dest_notebook = g_object_get_data(G_OBJECT(dest_window), "notebook");

        g_object_ref(detaching_tab);
        gtk_container_remove(src_notebook, detaching_tab);
        add_tab_to_window(dest_window, detaching_tab, gtk_notebook_get_current_page(dest_notebook)+1);
        g_object_unref(detaching_tab);
        detaching_tab = NULL;
    }
}
void switch_to_tab(VteTerminal* terminal, int data) {
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(terminal))));
    gtk_notebook_set_current_page(notebook, data);
}
void tab_popup_menu(VteTerminal* terminal) {
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(terminal))));
    gboolean value;
    g_signal_emit_by_name(notebook, "popup-menu", &value);
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
    // populate palette
    vte_terminal_set_colors(VTE_TERMINAL(terminal), palette+1, palette, palette+2, PALETTE_SIZE);

    GtkWidget* label = g_object_get_data(G_OBJECT(terminal), "label");
    g_object_set(G_OBJECT(label),
            "ellipsize",  tab_title_ellipsize_mode,
            "xalign",     tab_title_alignment,
            "use-markup", tab_title_markup,
            NULL);
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

void load_config() {
    if (! config_filename) return;

    FILE* config = fopen(config_filename, "r");
    if (!config) {
        /* if (error) g_warning("Error loading key file: %s", error->message); */
        return;
    }

#define CLEAR_ARRAY(array, type) \
    if (array) g_array_remove_range(array, 0, array->len); \
    else array = g_array_new(FALSE, FALSE, sizeof(type))

    // reset some things
    CLEAR_ARRAY(keyboard_shortcuts, KeyCombo);
    if (! css_provider) css_provider = gtk_css_provider_new();

    char* line = NULL;
    char* value;
    size_t len = 0;
    ssize_t read;
    while ((read = getline(&line, &len, config)) != -1) {
        if (line[0] == '#') continue; // comment
        if (line[0] == ';') continue; // comment

        value = strchr(line, '=');
        if (! value) continue; // invalid line

        *value = '\0';
        // whitespace trimming
        char* c;
        for (c = value-1; isspace(*c); c--) ;; c[1] = '\0';
        value++;
        while( isspace(*value) ) value++;
        for (c = line+read-1; isspace(*c); c--) ;; c[1] = '\0';

#define LINE_EQUALS(string) (strcmp(line, (#string)) == 0)
#define MAP_LINE(string, body) \
        if (LINE_EQUALS(string)) { \
            body; \
            continue; \
        }
#define MAP_VALUE(var, ...) do { \
            struct mapping {char* name; int value; } map[] = {__VA_ARGS__}; \
            for(int i = 0; i < sizeof(map) / sizeof(struct mapping); i++) { \
                if (strcmp(value, map[i].name) == 0) { \
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
                continue;
            }
        }

        if (LINE_EQUALS(css-file)) {
            gtk_css_provider_load_from_path(css_provider, value, NULL);
            GdkScreen* screen = gdk_screen_get_default();
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
            continue;
        }

        MAP_LINE(background,          gdk_rgba_parse(palette, value));
        MAP_LINE(foreground,          gdk_rgba_parse(palette+1, value));
        MAP_LINE(tab-title-format,    set_tab_title_format(value));
        MAP_LINE(tab-fill,            tab_fill                     = atoi(value));
        MAP_LINE(tab-expand,          tab_expand                   = atoi(value));
        MAP_LINE(tab-enable-popup,    notebook_enable_popup        = atoi(value));
        MAP_LINE(tab-scrollable,      notebook_scrollable          = atoi(value));
        MAP_LINE(show-tabs,           notebook_show_tabs           = atoi(value));
        MAP_LINE(ui-refresh-interval, ui_refresh_interval          = atoi(value));
        MAP_LINE(tab-title-markup,    tab_title_markup             = atoi(value));
        MAP_LINE(inactivity-duration, inactivity_duration          = atoi(value));
        MAP_LINE(encoding,            terminal_encoding            = strdup(value));
        MAP_LINE(font,                terminal_font                = pango_font_description_from_string(value));
        MAP_LINE(font-scale,          terminal_font_scale          = strtod(value, NULL));
        MAP_LINE(audible-bell,        terminal_audible_bell        = atoi(value));
        MAP_LINE(allow-hyperlink,     terminal_allow_hyperlink     = atoi(value));
        MAP_LINE(pointer-autohide,    terminal_pointer_autohide    = atoi(value));
        MAP_LINE(rewrap-on-resize,    terminal_rewrap_on_resize    = atoi(value));
        MAP_LINE(scroll-on-keystroke, terminal_scroll_on_keystroke = atoi(value));
        MAP_LINE(scroll-on-output,    terminal_scroll_on_output    = atoi(value));
        MAP_LINE(scrollback-lines,    terminal_scrollback_lines    = atoi(value));
        MAP_LINE(show-scrollbar,      show_scrollbar               = atoi(value));
        MAP_LINE(window-icon,         window_icon                  = strdup(value));

        if (LINE_EQUALS(default-args)) {
            g_strfreev(default_args);
            if (*value == '\0') {
                default_args = NULL;
            } else {
                if (! g_shell_parse_argv(value, NULL, &default_args, NULL) ) {
                    g_warning("Failed to parse arg for %s: %s", line, value);
                }
            }
            continue;
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
            KeyComboCallback callback = NULL;

            char* arg = strchr(value, ':');
            if (arg) {
                *arg = '\0';
                arg++;
            }
            void* data = NULL;

#define TRY_SET_SHORTCUT_WITH_DATA(name, processor) \
            if (strcmp(value, #name) == 0) { \
                callback = (KeyComboCallback)name; \
                if (arg) { \
                    arg = str_unescape(arg); \
                    data = processor; \
                } \
                break; \
            }
#define TRY_SET_SHORTCUT(name) TRY_SET_SHORTCUT_WITH_DATA(name, NULL)

            while (1) {
                TRY_SET_SHORTCUT(paste_clipboard);
                TRY_SET_SHORTCUT(copy_clipboard);
                TRY_SET_SHORTCUT(increase_font_size);
                TRY_SET_SHORTCUT(decrease_font_size);
                TRY_SET_SHORTCUT(reset_terminal);
                TRY_SET_SHORTCUT(scroll_up);
                TRY_SET_SHORTCUT(scroll_down);
                TRY_SET_SHORTCUT(scroll_page_up);
                TRY_SET_SHORTCUT(scroll_page_down);
                TRY_SET_SHORTCUT(scroll_top);
                TRY_SET_SHORTCUT(scroll_bottom);
                TRY_SET_SHORTCUT(select_all);
                TRY_SET_SHORTCUT(unselect_all);
                TRY_SET_SHORTCUT_WITH_DATA(feed_data, strdup(arg));
                TRY_SET_SHORTCUT(new_tab);
                TRY_SET_SHORTCUT(new_window);
                TRY_SET_SHORTCUT(prev_tab);
                TRY_SET_SHORTCUT(next_tab);
                TRY_SET_SHORTCUT(move_tab_prev);
                TRY_SET_SHORTCUT(move_tab_next);
                TRY_SET_SHORTCUT(detach_tab);
                TRY_SET_SHORTCUT(cut_tab);
                TRY_SET_SHORTCUT(paste_tab);
                TRY_SET_SHORTCUT_WITH_DATA(switch_to_tab, GINT_TO_POINTER(atoi(arg)));
                TRY_SET_SHORTCUT(tab_popup_menu);
                TRY_SET_SHORTCUT(reload_config);
                break;
            }

            if (callback) {
                KeyCombo combo = {0, 0, callback, NULL};
                gtk_accelerator_parse(shortcut, &(combo.key), &(combo.modifiers));
                if (combo.modifiers & GDK_SHIFT_MASK) {
                    combo.key = gdk_keyval_to_upper(combo.key);
                }
                combo.data = data;
                g_array_append_val(keyboard_shortcuts, combo);
            } else {
                g_warning("Unrecognised action: %s", value);
            }
            continue;
        }
    }

    fclose(config);
    if (line) free(line);

    // reload config everywhere
    create_timer(ui_refresh_interval);
}
