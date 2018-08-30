#include <gtk/gtk.h>
#include <vte/vte.h>
#include <ctype.h>
#include <errno.h>
#include <gio/gunixoutputstream.h>
#include "config.h"
#include "window.h"
#include "terminal.h"
#include "split.h"
#include "utils.h"
#include "tab_title_ui.h"

guint timer_id = 0;
char* config_filename = NULL;
char* app_id = NULL;

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

GtkPolicyType scrollbar_policy = GTK_POLICY_ALWAYS;
GtkCssProvider* css_provider = NULL;
char** default_args = NULL;
char* window_icon = NULL;
char* default_open_action = "new_window";
char* tab_label_format = NULL;
char* tab_title_ui_format = NULL;

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
guint terminal_default_scrollback_lines = 0;
char* terminal_word_char_exceptions = NULL;

gboolean tab_expand = TRUE;
gboolean tab_fill = TRUE;
gboolean notebook_enable_popup = FALSE;
gboolean notebook_scrollable = FALSE;
gboolean notebook_show_tabs = FALSE;
GtkPositionType notebook_tab_pos = GTK_POS_TOP;
int ui_refresh_interval = 5000;
PangoEllipsizeMode tab_label_ellipsize_mode = PANGO_ELLIPSIZE_END;
gfloat tab_label_alignment = 0.5;
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
            NULL
    );
    vte_terminal_set_word_char_exceptions(VTE_TERMINAL(terminal), terminal_word_char_exceptions);
    // populate palette
    vte_terminal_set_colors(VTE_TERMINAL(terminal), &FOREGROUND, &BACKGROUND, palette+2, PALETTE_SIZE);

    configure_terminal_scrollbar(VTE_TERMINAL(terminal), scrollbar_policy);
}

void configure_tab(GtkContainer* notebook, GtkWidget* tab) {
    gtk_container_child_set(GTK_CONTAINER(notebook), tab,
            "tab-expand", tab_expand,
            "tab-fill",   tab_fill,
            NULL);

    GtkWidget* ui = g_object_get_data(G_OBJECT(tab), "tab_title");
    if (! ui) {
        make_tab_title_ui(tab);
    }
}

void configure_window(GtkWindow* window) {
    gtk_window_set_icon_name(window, window_icon);
    GtkWidget* notebook = window_get_notebook(GTK_WIDGET(window));
    g_object_set(notebook,
            "enable-popup", notebook_enable_popup,
            "scrollable",   notebook_scrollable,
            "show-tabs",    notebook_show_tabs,
            "tab-pos",      notebook_tab_pos,
            NULL);
}

void reconfigure_window(GtkWidget* window) {
    configure_window(GTK_WINDOW(window));

    GtkContainer* notebook = GTK_CONTAINER(window_get_notebook(GTK_WIDGET(window)));
    FOREACH_TAB(tab, GTK_WIDGET(window)) {
        configure_tab(notebook, tab);
        update_tab_titles(VTE_TERMINAL(split_get_active_term(tab)));

        FOREACH_TERMINAL(terminal, tab) {
            configure_terminal(GTK_WIDGET(terminal));
            update_terminal_css_class(terminal);
        }
    }
    update_window_title(GTK_WINDOW(window), NULL);
}

Action lookup_action(char* value) {
    char* arg = strchr(value, ':');
    if (arg) {
        *arg = '\0';
        arg++;
    }
    return make_action(value, arg);
}

void unset_action(guint key, int metadata) {
    for (int i = actions->len - 1; i >= 0; i--) {
        ActionData* action = &g_array_index(actions, ActionData, i);
        if (action->key == key && action->metadata == metadata) {
            g_array_remove_index(actions, i);
        }
    }
}

int handle_config(char* line, size_t len, char** result) {
    char* tmp;
    char* value = strchr(line, '=');

    if (value) {
        *value = '\0';
        value ++;
        g_strstrip(value);
    }

    g_strstrip(line);

#define LINE_EQUALS(string) (STR_EQUAL(line, (#string)))
#define MAP_LINE(string, body) \
    if (LINE_EQUALS(string)) { \
        body; \
        return 1; \
    }
#define MAP_VALUE(type, var, ...) do { \
        struct mapping {char* name; type value; } map[] = {__VA_ARGS__}; \
        for(int i = 0; i < sizeof(map) / sizeof(struct mapping); i++) { \
            if (value && g_ascii_strcasecmp(value, map[i].name) == 0) { \
                var = map[i].value; \
                break; \
            } else if (! value && var == map[i].value) { \
                *result = strdup(map[i].name); \
                break; \
            } \
        } \
    } while(0)
#define MAP_LINE_VALUE(string, type, ...) MAP_LINE(string, MAP_VALUE(type, __VA_ARGS__))

#define PARSE_BOOL(string) ( ! ( \
    g_ascii_strcasecmp((string), "no") == 0 \
    || g_ascii_strcasecmp((string), "n") == 0 \
    || g_ascii_strcasecmp((string), "false") == 0 \
    || STR_EQUAL((string), "") \
    || STR_EQUAL((string), "0") \
    ))
#define MAP_BOOL(var) \
    if (value) { var = PARSE_BOOL(value); } \
    else { *result = strdup(var ? "1" : "0"); }
#define MAP_INT(var) \
    if (value) { var = atoi(value); } \
    else { *result = g_strdup_printf("%i", var); }
#define MAP_STR(var) \
    if (value) { free(var); var = strdup(value); } \
    else { *result = strdup(var); }
#define MAP_COLOUR(val) \
    if (value) { gdk_rgba_parse((val), value); } \
    else { *result = gdk_rgba_to_string(val); } \

    // palette colours
    if ((tmp = STR_STRIP_PREFIX(line, "col"))) {
        errno = 0;
        char* endptr = NULL;
        int n = strtol(tmp, &endptr, 10);
        if (!errno && *endptr == '\0' && 0 <= n && n < 16) {
            MAP_COLOUR(palette+2+n);
            return 1;
        }
    }

    if (LINE_EQUALS(css)) {
        if (value) {
            gtk_css_provider_load_from_data(css_provider, value, -1, NULL);
            GdkScreen* screen = gdk_screen_get_default();
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        } else {
            *result = gtk_css_provider_to_string(css_provider);
        }
        return 1;
    }

    MAP_LINE(background,                MAP_COLOUR(palette));
    MAP_LINE(foreground,                MAP_COLOUR(palette+1));
    MAP_LINE(window-title-format,       if (value) set_window_title_format(value));
    MAP_LINE(tab-label-format,          MAP_STR(tab_label_format); if (value) { free(tab_title_ui_format); tab_title_ui_format = NULL; } );
    MAP_LINE(tab-title-ui,              MAP_STR(tab_title_ui_format); if (value) { free(tab_label_format); tab_label_format= NULL; } );
    MAP_LINE(tab-fill,                  MAP_BOOL(tab_fill));
    MAP_LINE(tab-expand,                MAP_BOOL(tab_expand));
    MAP_LINE(tab-enable-popup,          MAP_BOOL(notebook_enable_popup));
    MAP_LINE(tab-scrollable,            MAP_BOOL(notebook_scrollable));
    MAP_LINE(show-tabs,                 MAP_BOOL(notebook_show_tabs));
    MAP_LINE(ui-refresh-interval,       MAP_INT(ui_refresh_interval));
    MAP_LINE(inactivity-duration,       MAP_INT(inactivity_duration));
    MAP_LINE(encoding,                  MAP_STR(terminal_encoding));
    MAP_LINE(font,                      terminal_font                 = pango_font_description_from_string(value));
    MAP_LINE(font-scale,                terminal_font_scale           = strtod(value, NULL));
    MAP_LINE(audible-bell,              MAP_BOOL(terminal_audible_bell));
    MAP_LINE(allow-hyperlink,           MAP_BOOL(terminal_allow_hyperlink));
    MAP_LINE(pointer-autohide,          MAP_BOOL(terminal_pointer_autohide));
    MAP_LINE(rewrap-on-resize,          MAP_BOOL(terminal_rewrap_on_resize));
    MAP_LINE(scroll-on-keystroke,       MAP_BOOL(terminal_scroll_on_keystroke));
    MAP_LINE(scroll-on-output,          MAP_BOOL(terminal_scroll_on_output));
    MAP_LINE(default-scrollback-lines,  MAP_INT(terminal_default_scrollback_lines));
    MAP_LINE(word-char-exceptions,      MAP_STR(terminal_word_char_exceptions));
    MAP_LINE(window-icon,               MAP_STR(window_icon));
    MAP_LINE(window-close-confirm,      MAP_BOOL(window_close_confirm));

    if (LINE_EQUALS(scrollback-lines)) {
        // this only affects the *current* terminal
        VteTerminal* terminal = get_active_terminal(NULL);
        if (terminal) {
            if (value) {
                g_object_set(G_OBJECT(terminal), "scrollback-lines", atoi(value), NULL);
            } else {
                int lines;
                g_object_get(G_OBJECT(terminal), "scrollback-lines", &lines, NULL);
                *result = g_strdup_printf("%i", lines);
            }
        }
        return 1;
    }

    if (LINE_EQUALS(tab-close-confirm)) {
        if (value) {
            if (g_ascii_strcasecmp(value, "smart") == 0) {
                tab_close_confirm = CLOSE_CONFIRM_SMART;
            } else if (PARSE_BOOL(value)) {
                tab_close_confirm = CLOSE_CONFIRM_YES;
            } else {
                tab_close_confirm = CLOSE_CONFIRM_NO;
            }
        } else {
            switch (tab_close_confirm) {
                case CLOSE_CONFIRM_SMART:
                    *result = strdup("smart"); break;
                case CLOSE_CONFIRM_YES:
                    *result = strdup("1"); break;
                case CLOSE_CONFIRM_NO:
                    *result = strdup("0"); break;
            }
        }
        return 1;
    }

    if (LINE_EQUALS(default-args)) {
        // TODO
        if (value) {
            g_strfreev(default_args);
            default_args = shell_split(value, NULL);
        } else if (default_args) {
            int n;
            for (n = 0; default_args[n]; n++) ;

            char* args[n+1];
            args[n] = NULL;
            for (n = 0; default_args[n]; n++) {
                args[n] = g_shell_quote(default_args[n]);
            }

            *result = g_strjoinv(NULL, args);
            g_strfreev(args);
        }
        return 1;
    }

    if (LINE_EQUALS(show-scrollbar)) {
        if (value) {
            if (STR_EQUAL(value, "auto") || STR_EQUAL(value, "automatic")) {
                scrollbar_policy = GTK_POLICY_AUTOMATIC;
            } else if (! PARSE_BOOL(value) || STR_EQUAL(value, "never")) {
                scrollbar_policy = GTK_POLICY_NEVER;
            } else {
                scrollbar_policy = GTK_POLICY_ALWAYS;
            }
        } else {
            switch (scrollbar_policy) {
                case GTK_POLICY_AUTOMATIC:
                    *result = strdup("auto"); break;
                case GTK_POLICY_NEVER:
                    *result = strdup("never"); break;
                case GTK_POLICY_ALWAYS:
                    *result = strdup("always"); break;
                default: break;
            }
        }
        return 1;
    }

    MAP_LINE_VALUE(tab-pos, int, notebook_tab_pos,
            {"top",    GTK_POS_TOP},
            {"bottom", GTK_POS_BOTTOM},
            {"left",   GTK_POS_LEFT},
            {"right",  GTK_POS_RIGHT},
    );

    MAP_LINE_VALUE(tab-title-ellipsize-mode, int, tab_label_ellipsize_mode,
            {"start",  PANGO_ELLIPSIZE_START},
            {"middle", PANGO_ELLIPSIZE_MIDDLE},
            {"end",    PANGO_ELLIPSIZE_END},
    );

    MAP_LINE_VALUE(tab-label-alignment, int, tab_label_alignment,
            {"left",   0},
            {"right",  1},
            {"center", 0.5},
    );

    MAP_LINE_VALUE(cursor-blink-mode, int, terminal_cursor_blink_mode,
            {"system", VTE_CURSOR_BLINK_SYSTEM},
            {"on",     VTE_CURSOR_BLINK_ON},
            {"off",    VTE_CURSOR_BLINK_OFF},
    );

    MAP_LINE_VALUE(cursor-shape, int, terminal_cursor_shape,
            {"block",     VTE_CURSOR_SHAPE_BLOCK},
            {"ibeam",     VTE_CURSOR_SHAPE_IBEAM},
            {"underline", VTE_CURSOR_SHAPE_UNDERLINE},
    );

    MAP_LINE_VALUE(cursor-shape, int, terminal_cursor_shape,
            {"block",     VTE_CURSOR_SHAPE_BLOCK},
            {"ibeam",     VTE_CURSOR_SHAPE_IBEAM},
            {"underline", VTE_CURSOR_SHAPE_UNDERLINE},
    );

    MAP_LINE_VALUE(default-open-action, char*, default_open_action,
            {"tab",       "new_tab"},
            {"window",    "new_window"},
    );

    // ONLY events from here on
    // events must take a value
    if (value) {

        ActionData action = {0, 0, {NULL, NULL, NULL}};

        tmp = STR_STRIP_PREFIX(line, "on-");
        if (tmp) {
            char* event = tmp;
            action.key = -1;

#define MAP_EVENT(string, value) \
            if (STR_EQUAL(event, #string)) { \
                action.metadata = value; \
                break; \
            }

            while (1) {
                MAP_EVENT(bell, BELL_EVENT);
                MAP_EVENT(hyperlink-hover, HYPERLINK_HOVER_EVENT);
                MAP_EVENT(hyperlink-click, HYPERLINK_CLICK_EVENT);
                MAP_EVENT(focus, FOCUS_IN_EVENT);
                MAP_EVENT(start, START_EVENT);
                break;
            }

        }

        tmp = STR_STRIP_PREFIX(line, "key-");
        if (tmp) {
            char* shortcut = tmp;

            gtk_accelerator_parse(shortcut, &(action.key), (GdkModifierType*) &(action.metadata));
            if (action.metadata & GDK_SHIFT_MASK) {
                action.key = gdk_keyval_to_upper(action.key);
            }
        }

        if (action.key) {
            if (STR_EQUAL(value, "")) {
                // unset this shortcut
                unset_action(action.key, action.metadata);
                return 1;
            } else {
                Action a = lookup_action(value);
                if (a.func) {
                    action.action = a;
                    g_array_append_val(actions, action);
                } else {
                    g_warning("Unrecognised action: %s", value);
                }
                return 1;
            }
        }
    }

    /* g_warning("Unrecognised key: %s", line); */
    return 0;
}

gboolean refresh_ui() {
    FOREACH_WINDOW(window) {
        refresh_ui_window(window);
    }
    return TRUE;
}

void reconfigure_all() {
    gboolean tab_titles_changed = FALSE;
    if (tab_title_ui_format) {
        tab_titles_changed = set_tab_title_ui(tab_title_ui_format);
    } else {
        tab_titles_changed = set_tab_label_format(tab_label_format ? tab_label_format : "%t", tab_label_ellipsize_mode, tab_label_alignment);
    }

    if (tab_titles_changed) {
        destroy_all_tab_title_uis();
    }

    // reload config everywhere
    FOREACH_WINDOW(window) {
        reconfigure_window(window);
    }

    if (timer_id) g_source_remove(timer_id);
    timer_id = g_timeout_add(ui_refresh_interval, refresh_ui, NULL);
}

int trigger_action(VteTerminal* terminal, guint key, int metadata) {
    int handled = 0;
    if (actions) {
        for (int i = 0; i < actions->len; i++) {
            ActionData* action = &g_array_index(actions, ActionData, i);
            if (action->key == key && action->metadata == metadata) {
                action->action.func(terminal, action->action.data, NULL);
                handled = 1;
            }
        }
    }
    return handled;
}

void free_action_data(ActionData* c) {
    if (c->action.cleanup)
        c->action.cleanup(c->action.data);
}

void* execute_line(char* line, int size, gboolean reconfigure, gboolean do_actions) {
    char* line_copy;
    char* result = NULL;

    line_copy = strdup(line);
    int handled = handle_config(line_copy, size, &result);
    free(line_copy);

    if (handled) {
        if (reconfigure && !result) reconfigure_all();
        return result;
    }

    if (! do_actions) {
        return NULL;
    }

    line_copy = strdup(line);
    Action action = lookup_action(line_copy);
    free(line_copy);
    if (action.func) {
        VteTerminal* terminal = get_active_terminal(NULL);
        if (terminal) {
            action.func(terminal, action.data, &result);
            if (action.cleanup) {
                action.cleanup(action.data);
            }
            return result;
        }
        return NULL;
    }

    return NULL;
}

void load_config(char* filename) {
    // init some things
    if (! actions) {
        actions = g_array_new(FALSE, FALSE, sizeof(ActionData));
        g_array_set_clear_func(actions, (GDestroyNotify)free_action_data);
    }
    if (! css_provider) {
        css_provider = gtk_css_provider_new();
    }

    if (! filename) filename = config_filename;
    if (! filename) return;
    FILE* config = fopen(filename, "r");
    if (!config) {
        g_warning("Failed to open %s: %s", filename, strerror(errno));
        return;
    }

    char* line = NULL;
    char* buffer = NULL;
    size_t size = 0, bufsize = 0;
    ssize_t len, l;
    while ((len = getline(&line, &size, config)) != -1) {
        // multilines
        if (len >= 4 && STR_EQUAL(line+len-4, "\"\"\"\n")) {
            len -= 4;
            while ((l = getline(&buffer, &bufsize, config)) != -1) {
                len += l;
                if (len >= size) {
                    size = len+1;
                    line = realloc(line, size);
                }
                memcpy(line+len-l, buffer, l);

                if (STR_STARTSWITH(line+len-4, "\"\"\"\n")) {
                    line[len-4] = '\0';
                    break;
                }
            }
        }

        if (line[0] == '#' || line[0] == ';') continue; // comment
        free(execute_line(line, len, FALSE, FALSE));
    }

    fclose(config);
    if (line) free(line);
    if (buffer) free(buffer);

    reconfigure_all();
}
