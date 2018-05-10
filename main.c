#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <vte/vte.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

const gint ERROR_EXIT_CODE = 127;

#define GET_ENV(x) (getenv("POPUP_TERM_" x))

#define PALETTE_SIZE (16)
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
#define DEFAULT_SHELL "/bin/sh"
char* default_shell = NULL;

void term_exited(VteTerminal* terminal, gint status, gint* dest)
{
    if (WIFEXITED(status)) {
        *dest = WEXITSTATUS(status);
    } else {
        *dest = ERROR_EXIT_CODE;
    }
    gtk_main_quit();
}

typedef struct {
    char* name;
    guint key;
    GdkModifierType modifiers;
    void(*callback)(VteTerminal*);
} KeyCombo;

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

KeyCombo keyboard_shortcuts[] = {
    {"paste-clipboard", 0, 0, vte_terminal_paste_clipboard},
    {"copy-clipboard",  0, 0, copy_clipboard},
    {"increase-font-size", 0, 0, increase_font_size},
    {"decrease-font-size", 0, 0, decrease_font_size},
    {"reset", 0, 0, reset_terminal},
    {"scroll-up", 0, 0, scroll_up},
    {"scroll-down", 0, 0, scroll_down},
    {"scroll-page-up", 0, 0, scroll_page_up},
    {"scroll-page-down", 0, 0, scroll_page_down},
    {"select-all", 0, 0, vte_terminal_select_all},
    {"unselect-all", 0, 0, vte_terminal_unselect_all},
};

gboolean key_pressed(GtkWidget* terminal, GdkEventKey* event, gpointer data)
{
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    for (int i = 0; i < sizeof(keyboard_shortcuts)/sizeof(KeyCombo); i++) {
        KeyCombo* combo = keyboard_shortcuts+i;
        if (combo->key == event->keyval && combo->modifiers == modifiers) {
            combo->callback(VTE_TERMINAL(terminal));
            return TRUE;
        }
    }
    return FALSE;
}

void term_spawn_callback(GtkWidget* terminal, GPid pid, GError *error, gpointer user_data)
{
    if (error) {
        fprintf(stderr, "Could not start terminal: %s\n", error->message);
        exit(ERROR_EXIT_CODE);
    }
}

void load_config(const char* filename, GtkWidget* terminal, GtkWidget* window) {
    FILE* config = fopen(filename, "r");
    if (!config) {
        /* if (error) g_warning("Error loading key file: %s", error->message); */
        return;
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

        if (strcmp(line, "default-shell") == 0) {
            free(default_shell);
            if (strlen(value) == 0) {
                default_shell = NULL;
            } else {
                default_shell = strdup(value);
            }
            continue;
        }

        for (int i = 0; i < sizeof(keyboard_shortcuts)/sizeof(KeyCombo); i++) {
            KeyCombo* combo = keyboard_shortcuts+i;
            if (strcmp(line, combo->name) == 0) {
                gtk_accelerator_parse(value, &(combo->key), &(combo->modifiers));
                if (combo->modifiers & GDK_SHIFT_MASK) {
                    combo->key = gdk_keyval_to_upper(combo->key);
                }
                break;
            }
        }
    }

    fclose(config);
    if (line) free(line);
}

int main(int argc, char *argv[])
{
    int status = 0;
    GtkWidget *window;
    GtkWidget *terminal;
    GtkWidget *grid;
    GtkWidget *scrollbar;

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "delete-event", gtk_main_quit, NULL);

    grid = gtk_grid_new();

    terminal = vte_terminal_new();
    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), &status);
    g_object_set(terminal, "expand", 1, NULL);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal), VTE_CURSOR_BLINK_OFF);

    // populate palette
    vte_terminal_set_colors(VTE_TERMINAL(terminal), palette+1, palette, palette+2, PALETTE_SIZE);

    char **args;
    char *default_args[] = {NULL, NULL};
    char *user_shell = NULL;

    if (argc > 1) {
        args = argv + 1;
    } else {
        user_shell = vte_get_user_shell();
        default_args[0] =
            default_shell ?
                default_shell :
            user_shell ?
                user_shell :
                DEFAULT_SHELL;
        args = default_args;
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
    free(default_shell);
    free(user_shell);

    g_signal_connect(terminal, "key-press-event", G_CALLBACK(key_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(terminal));

    if (show_scrollbar) {
        scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal)));
        gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(scrollbar));
    }

    gtk_widget_show_all(window);
    gtk_main();

    exit(status);
}
