#include <gtk/gtk.h>
#include <vte/vte.h>
#include <ctype.h>
#include "config.h"

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
gboolean show_scrollbar = 1;
char** default_args = NULL;

/* CALLBACKS */

KeyComboCallback \
    paste_clipboard = vte_terminal_paste_clipboard
    , select_all = vte_terminal_select_all
    , unselect_all = vte_terminal_unselect_all
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
void scroll_up(VteTerminal* terminal) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));
    gdouble delta = gtk_adjustment_get_step_increment(adj);
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj)-delta);
}
void scroll_down(VteTerminal* terminal) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));
    gdouble delta = gtk_adjustment_get_step_increment(adj);
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj)+delta);
}
void scroll_page_up(VteTerminal* terminal) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));
    gdouble delta = gtk_adjustment_get_page_size(adj);
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj)-delta);
}
void scroll_page_down(VteTerminal* terminal) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal));
    gdouble delta = gtk_adjustment_get_page_size(adj);
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj)+delta);
}

void load_config(const char* filename, GtkWidget* terminal, GtkWidget* window) {
    FILE* config = fopen(filename, "r");
    if (!config) {
        /* if (error) g_warning("Error loading key file: %s", error->message); */
        return;
    }

    // reallocate keyboard_shortcuts
    if (keyboard_shortcuts) {
        g_array_remove_range(keyboard_shortcuts, 0, keyboard_shortcuts->len);
    } else {
        keyboard_shortcuts = g_array_new(FALSE, FALSE, sizeof(KeyCombo));
    }

    char* line = NULL;
    char* value;
    size_t len = 0;
    ssize_t read;
    while ((read = getline(&line, &len, config)) != -1) {
        if (line[0] == '#') continue; // comment

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

        if (strcmp(line, "background") == 0) {
            gdk_rgba_parse(palette, value);
            continue;
        }

        if (strcmp(line, "foreground") == 0) {
            gdk_rgba_parse(palette+1, value);
            continue;
        }

        if (strcmp(line, "cursor-blink-mode") == 0) {
            int attr =
                strcmp(value, "SYSTEM") == 0 ?
                    VTE_CURSOR_BLINK_SYSTEM :
                strcmp(value, "ON") == 0 ?
                    VTE_CURSOR_BLINK_ON :
                strcmp(value, "OFF") == 0 ?
                    VTE_CURSOR_BLINK_OFF :
                    -1;
            if (attr != -1) g_object_set(terminal, line, attr, NULL);
            continue;
        }

        if (strcmp(line, "cursor-shape") == 0) {
            int attr =
                strcmp(value, "BLOCK") == 0 ?
                    VTE_CURSOR_SHAPE_BLOCK :
                strcmp(value, "IBEAM") == 0 ?
                    VTE_CURSOR_SHAPE_IBEAM :
                strcmp(value, "UNDERLINE") == 0 ?
                    VTE_CURSOR_SHAPE_UNDERLINE :
                    -1;
            if (attr != -1) g_object_set(terminal, line, attr, NULL);
            continue;
        }

        if (strcmp(line, "encoding") == 0) {
            g_object_set(terminal, line, value, NULL);
            continue;
        }

        if (strcmp(line, "font") == 0) {
            PangoFontDescription* font = pango_font_description_from_string(value);
            g_object_set(terminal, "font-desc", font, NULL);
            continue;
        }

        if (strcmp(line, "font-scale") == 0) {
            g_object_set(terminal, line, strtod(value, NULL), NULL);
            continue;
        }

#define TRY_SET_INT_PROP(name) \
    if (strcmp(line, name) == 0) { \
        g_object_set(terminal, line, atoi(value), NULL); \
        continue; \
    }

        TRY_SET_INT_PROP("allow-hyperlink")
        TRY_SET_INT_PROP("pointer-autohide")
        TRY_SET_INT_PROP("rewrap-on-resize")
        TRY_SET_INT_PROP("scroll-on-keystroke")
        TRY_SET_INT_PROP("scroll-on-output")
        TRY_SET_INT_PROP("scrollback-lines")

        if (strcmp(line, "show-scrollbar") == 0) {
            show_scrollbar = 0;
            continue;
        }

        if (strcmp(line, "window-icon") == 0) {
            gtk_window_set_icon_name(GTK_WINDOW(window), value);
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

        KeyComboCallback callback = NULL;
#define TRY_SET_SHORTCUT(name) \
    if (strcmp(line, #name) == 0) { \
        callback = name; \
    }

        TRY_SET_SHORTCUT(paste_clipboard)
        else TRY_SET_SHORTCUT(copy_clipboard)
        else TRY_SET_SHORTCUT(increase_font_size)
        else TRY_SET_SHORTCUT(decrease_font_size)
        else TRY_SET_SHORTCUT(reset_terminal)
        else TRY_SET_SHORTCUT(scroll_up)
        else TRY_SET_SHORTCUT(scroll_down)
        else TRY_SET_SHORTCUT(scroll_page_up)
        else TRY_SET_SHORTCUT(scroll_page_down)
        else TRY_SET_SHORTCUT(select_all)
        else TRY_SET_SHORTCUT(unselect_all)

        if (callback) {
            KeyCombo combo = {0, 0, callback};
            gtk_accelerator_parse(value, &(combo.key), &(combo.modifiers));
            if (combo.modifiers & GDK_SHIFT_MASK) {
                combo.key = gdk_keyval_to_upper(combo.key);
            }

            g_array_append_val(keyboard_shortcuts, combo);
        }
    }

    fclose(config);
    if (line) free(line);
}

