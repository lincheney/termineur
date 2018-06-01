#include <gtk/gtk.h>
#include <vte/vte.h>
#include <ctype.h>
#include "config.h"
#include "window.h"

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
GArray* terminal_prop_names = NULL;
GArray* terminal_prop_values = NULL;
GtkCssProvider* css_provider = NULL;
char** default_args = NULL;
char* window_icon = NULL;

// notebook props
gboolean tab_expand = FALSE;
gboolean tab_fill = TRUE;
gboolean notebook_enable_popup = FALSE;
gboolean notebook_scrollable = FALSE;
gboolean notebook_show_tabs = TRUE;
GtkPositionType notebook_tab_pos = GTK_POS_TOP;

/* CALLBACKS */

KeyComboCallback \
    paste_clipboard = (KeyComboCallback)vte_terminal_paste_clipboard
    , select_all = (KeyComboCallback)vte_terminal_select_all
    , unselect_all = (KeyComboCallback)vte_terminal_unselect_all
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
    n += delta;
    gtk_notebook_reorder_child(notebook, tab, n > 0 ? n : 0);
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

#define STORE_PROPERTY(name, type, TYPE, value) { \
        GValue _val = G_VALUE_INIT; \
        g_value_init(&_val, TYPE); \
        g_value_set_ ## type (&_val, value); \
        char* _name = strdup(name); \
        g_array_append_val(terminal_prop_names, _name); \
        g_array_append_val(terminal_prop_values, _val); \
    }

void configure_terminal(GtkWidget* terminal) {
    g_object_setv(G_OBJECT(terminal), terminal_prop_names->len, (const char**)terminal_prop_names->data, (GValue*)terminal_prop_values->data);
}

void configure_tab(GtkContainer* notebook, GtkWidget* tab) {
    gtk_container_child_set(GTK_CONTAINER(notebook), tab,
            "tab-expand", tab_expand,
            "tab-fill", tab_fill,
            NULL
    );
}

void configure_window(GtkWindow* window) {
    gtk_window_set_icon_name(window, window_icon);
    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    g_object_set(notebook,
            "enable-popup", notebook_enable_popup,
            "scrollable", notebook_scrollable,
            "show-tabs", notebook_show_tabs,
            "tab-pos", notebook_tab_pos,
        NULL);
    // css
    GtkStyleContext* context = gtk_widget_get_style_context(notebook);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
}

void load_config(const char* filename) {
    FILE* config = fopen(filename, "r");
    if (!config) {
        /* if (error) g_warning("Error loading key file: %s", error->message); */
        return;
    }

    if (! css_provider) css_provider = gtk_css_provider_new();

#define CLEAR_ARRAY(array, type) \
    if (array) g_array_remove_range(array, 0, array->len); \
    else array = g_array_new(FALSE, FALSE, sizeof(type))

    CLEAR_ARRAY(keyboard_shortcuts, KeyCombo);
    CLEAR_ARRAY(terminal_prop_names, char*);
    g_array_set_clear_func(terminal_prop_names, free);
    CLEAR_ARRAY(terminal_prop_values, GValue);

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
        for (char* c = value-1; isspace(*c); c--) *c = '\0';
        value++;
        while( isspace(*value) ) value++;
        for (char* c = line+read-1; isspace(*c); c--) *c = '\0';

#define TRY_SET_PALETTE_COL(n) \
    if (strcmp(line, "col" #n) == 0) { \
        gdk_rgba_parse(palette+2+(n), value); \
        continue; \
    }

        TRY_SET_PALETTE_COL(0);
        TRY_SET_PALETTE_COL(1);
        TRY_SET_PALETTE_COL(2);
        TRY_SET_PALETTE_COL(3);
        TRY_SET_PALETTE_COL(4);
        TRY_SET_PALETTE_COL(5);
        TRY_SET_PALETTE_COL(6);
        TRY_SET_PALETTE_COL(7);
        TRY_SET_PALETTE_COL(8);
        TRY_SET_PALETTE_COL(9);
        TRY_SET_PALETTE_COL(10);
        TRY_SET_PALETTE_COL(11);
        TRY_SET_PALETTE_COL(12);
        TRY_SET_PALETTE_COL(13);
        TRY_SET_PALETTE_COL(14);
        TRY_SET_PALETTE_COL(15);

#undef TRY_SET_PALETTE_COL

        if (strcmp(line, "background") == 0) {
            gdk_rgba_parse(palette, value);
            continue;
        }

        if (strcmp(line, "foreground") == 0) {
            gdk_rgba_parse(palette+1, value);
            continue;
        }

       if (strcmp(line, "css-file") == 0) {
           gtk_css_provider_load_from_path(css_provider, value, NULL);
           continue;
       }

#define TRY_SET_INT_PROP(name, var) \
    if (strcmp(line, name) == 0) { \
        var = atoi(value); \
        continue; \
    }

       TRY_SET_INT_PROP("tab-fill", tab_fill)
       TRY_SET_INT_PROP("tab-expand", tab_expand)
       TRY_SET_INT_PROP("tab-enable-popup", notebook_enable_popup)
       TRY_SET_INT_PROP("tab-scrollable", notebook_scrollable)
       TRY_SET_INT_PROP("show-tabs", notebook_show_tabs)

#undef TRY_SET_INT_PROP

       if (strcmp(line, "tab-pos") == 0) {
            int attr =
                strcmp(value, "TOP")    == 0 ? GTK_POS_TOP    :
                strcmp(value, "BOTTOM") == 0 ? GTK_POS_BOTTOM :
                strcmp(value, "LEFT")   == 0 ? GTK_POS_LEFT   :
                strcmp(value, "RIGHT")  == 0 ? GTK_POS_RIGHT  :
                    -1;
            if (attr != -1) notebook_tab_pos = attr;
            continue;
        }

       if (strcmp(line, "cursor-blink-mode") == 0) {
            int attr =
                strcmp(value, "SYSTEM") == 0 ? VTE_CURSOR_BLINK_SYSTEM :
                strcmp(value, "ON")     == 0 ? VTE_CURSOR_BLINK_ON     :
                strcmp(value, "OFF")    == 0 ? VTE_CURSOR_BLINK_OFF    :
                    -1;
            if (attr != -1) STORE_PROPERTY(line, int, G_TYPE_INT, attr);
            continue;
        }

        if (strcmp(line, "cursor-shape") == 0) {
            int attr =
                strcmp(value, "BLOCK")     == 0 ? VTE_CURSOR_SHAPE_BLOCK     :
                strcmp(value, "IBEAM")     == 0 ? VTE_CURSOR_SHAPE_IBEAM     :
                strcmp(value, "UNDERLINE") == 0 ? VTE_CURSOR_SHAPE_UNDERLINE :
                    -1;
            if (attr != -1) STORE_PROPERTY(line, int, G_TYPE_INT, attr);
            continue;
        }

        if (strcmp(line, "encoding") == 0) {
            STORE_PROPERTY(line, string, G_TYPE_STRING, value);
            continue;
        }

        if (strcmp(line, "word-char-exceptions") == 0) {
            STORE_PROPERTY(line, string, G_TYPE_STRING, value);
            continue;
        }

        if (strcmp(line, "font") == 0) {
            PangoFontDescription* font = pango_font_description_from_string(value);
            STORE_PROPERTY("font-desc", boxed, PANGO_TYPE_FONT_DESCRIPTION, font);
            continue;
        }

        if (strcmp(line, "font-scale") == 0) {
            STORE_PROPERTY(line, double, G_TYPE_DOUBLE, strtod(value, NULL));
            continue;
        }

#define TRY_SET_INT_PROP(name) \
    if (strcmp(line, name) == 0) { \
        STORE_PROPERTY(line, int, G_TYPE_INT, atoi(value)); \
        continue; \
    }

        TRY_SET_INT_PROP("audible-bell")
        TRY_SET_INT_PROP("allow-hyperlink")
        TRY_SET_INT_PROP("pointer-autohide")
        TRY_SET_INT_PROP("rewrap-on-resize")
        TRY_SET_INT_PROP("scroll-on-keystroke")
        TRY_SET_INT_PROP("scroll-on-output")
        TRY_SET_INT_PROP("scrollback-lines")

#undef TRY_SET_INT_PROP

        if (strcmp(line, "show-scrollbar") == 0) {
            show_scrollbar = atoi(value);
            continue;
        }

        if (strcmp(line, "window-icon") == 0) {
            window_icon = strdup(value);
            continue;
        }

        if (strcmp(line, "default-args") == 0) {
            g_strfreev(default_args);
            if (strlen(value) == 0) {
                default_args = NULL;
            } else {
                if (! g_shell_parse_argv(value, NULL, &default_args, NULL) ) {
                    g_warning("Failed to parse arg for %s: %s", line, value);
                }
            }
            continue;
        }

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
            }
#define TRY_SET_SHORTCUT(name) TRY_SET_SHORTCUT_WITH_DATA(name, NULL)

            TRY_SET_SHORTCUT(paste_clipboard)
            else TRY_SET_SHORTCUT(copy_clipboard)
            else TRY_SET_SHORTCUT(increase_font_size)
            else TRY_SET_SHORTCUT(decrease_font_size)
            else TRY_SET_SHORTCUT(reset_terminal)
            else TRY_SET_SHORTCUT(scroll_up)
            else TRY_SET_SHORTCUT(scroll_down)
            else TRY_SET_SHORTCUT(scroll_page_up)
            else TRY_SET_SHORTCUT(scroll_page_down)
            else TRY_SET_SHORTCUT(scroll_top)
            else TRY_SET_SHORTCUT(scroll_bottom)
            else TRY_SET_SHORTCUT(select_all)
            else TRY_SET_SHORTCUT(unselect_all)
            else TRY_SET_SHORTCUT_WITH_DATA(feed_data, strdup(arg))
            else TRY_SET_SHORTCUT(new_tab)
            else TRY_SET_SHORTCUT(new_window)
            else TRY_SET_SHORTCUT(prev_tab)
            else TRY_SET_SHORTCUT(next_tab)
            else TRY_SET_SHORTCUT(move_tab_prev)
            else TRY_SET_SHORTCUT(move_tab_next)
            else TRY_SET_SHORTCUT(detach_tab)
            else TRY_SET_SHORTCUT(cut_tab)
            else TRY_SET_SHORTCUT(paste_tab)
            else TRY_SET_SHORTCUT_WITH_DATA(switch_to_tab, GINT_TO_POINTER(atoi(arg)))
            else TRY_SET_SHORTCUT(tab_popup_menu)

#undef TRY_SET_SHORTCUT

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
}
