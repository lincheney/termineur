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

    GtkWidget* label = g_object_get_data(G_OBJECT(tab), "label");
    g_object_set(G_OBJECT(label),
            "ellipsize",  tab_title_ellipsize_mode,
            "xalign",     tab_title_alignment,
            "use-markup", TRUE,
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
        terminal = split_get_active_term(tab);
        configure_terminal(terminal);
        update_terminal_ui(VTE_TERMINAL(terminal));
    }
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

int set_config_from_str(char* line, size_t len) {
    char* tmp;
    char* value = strchr(line, '=');
    if (! value) return 0; // invalid line

    *value = '\0';
    value ++;
    // whitespace trimming
    g_strstrip(line);
    g_strstrip(value);

#define LINE_EQUALS(string) (STR_EQUAL(line, (#string)))
#define MAP_LINE(string, body) \
    if (LINE_EQUALS(string)) { \
        body; \
        return 1; \
    }
#define MAP_VALUE(type, var, ...) do { \
        struct mapping {char* name; type value; } map[] = {__VA_ARGS__}; \
        for(int i = 0; i < sizeof(map) / sizeof(struct mapping); i++) { \
            if (g_ascii_strcasecmp(value, map[i].name) == 0) { \
                var = map[i].value; \
                break; \
            } \
        } \
    } while(0)
#define MAP_LINE_VALUE(string, type, ...) MAP_LINE(string, MAP_VALUE(type, __VA_ARGS__))

    // palette colours
    tmp = STR_STRIP_PREFIX(line, "col");
    if (tmp) {
        errno = 0;
        char* endptr = NULL;
        int n = strtol(tmp, &endptr, 10);
        if (!errno && *endptr == '\0' && 0 <= n && n < 16) {
            gdk_rgba_parse(palette+2+n, value);
            return 1;
        }
    }

#define PARSE_BOOL(string) ( ! ( \
    g_ascii_strcasecmp((string), "no") == 0 \
    || g_ascii_strcasecmp((string), "n") == 0 \
    || g_ascii_strcasecmp((string), "false") == 0 \
    || STR_EQUAL((string), "") \
    || STR_EQUAL((string), "0") \
    ))

    if (LINE_EQUALS(css)) {
        gtk_css_provider_load_from_data(css_provider, value, -1, NULL);
        GdkScreen* screen = gdk_screen_get_default();
        gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        return 1;
    }

    MAP_LINE(background,               gdk_rgba_parse(palette, value));
    MAP_LINE(foreground,               gdk_rgba_parse(palette+1, value));
    MAP_LINE(window-title-format,      set_window_title_format(value));
    MAP_LINE(tab-title-format,         set_tab_title_format(value));
    MAP_LINE(tab-fill,                 tab_fill                      = PARSE_BOOL(value));
    MAP_LINE(tab-expand,               tab_expand                    = PARSE_BOOL(value));
    MAP_LINE(tab-enable-popup,         notebook_enable_popup         = PARSE_BOOL(value));
    MAP_LINE(tab-scrollable,           notebook_scrollable           = PARSE_BOOL(value));
    MAP_LINE(show-tabs,                notebook_show_tabs            = PARSE_BOOL(value));
    MAP_LINE(ui-refresh-interval,      ui_refresh_interval           = atoi(value));
    MAP_LINE(inactivity-duration,      inactivity_duration           = atoi(value));
    MAP_LINE(encoding,                 free(terminal_encoding); terminal_encoding = strdup(value));
    MAP_LINE(font,                     terminal_font                 = pango_font_description_from_string(value));
    MAP_LINE(font-scale,               terminal_font_scale           = strtod(value, NULL));
    MAP_LINE(audible-bell,             terminal_audible_bell         = PARSE_BOOL(value));
    MAP_LINE(allow-hyperlink,          terminal_allow_hyperlink      = PARSE_BOOL(value));
    MAP_LINE(pointer-autohide,         terminal_pointer_autohide     = PARSE_BOOL(value));
    MAP_LINE(rewrap-on-resize,         terminal_rewrap_on_resize     = PARSE_BOOL(value));
    MAP_LINE(scroll-on-keystroke,      terminal_scroll_on_keystroke  = PARSE_BOOL(value));
    MAP_LINE(scroll-on-output,         terminal_scroll_on_output     = PARSE_BOOL(value));
    MAP_LINE(default-scrollback-lines, terminal_default_scrollback_lines     = atoi(value));
    MAP_LINE(word-char-exceptions,     free(terminal_word_char_exceptions); terminal_word_char_exceptions = strdup(value));
    MAP_LINE(window-icon,              free(window_icon);  window_icon = strdup(value));
    MAP_LINE(window-close-confirm,     window_close_confirm          = PARSE_BOOL(value));

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

    if (LINE_EQUALS(show-scrollbar)) {
        if (STR_EQUAL(value, "auto") || STR_EQUAL(value, "automatic")) {
            scrollbar_policy = GTK_POLICY_AUTOMATIC;
        } else if (! PARSE_BOOL(value) || STR_EQUAL(value, "never")) {
            scrollbar_policy = GTK_POLICY_NEVER;
        } else {
            scrollbar_policy = GTK_POLICY_ALWAYS;
        }
        return 1;
    }

    MAP_LINE_VALUE(tab-pos, int, notebook_tab_pos,
            {"top",    GTK_POS_TOP},
            {"bottom", GTK_POS_BOTTOM},
            {"left",   GTK_POS_LEFT},
            {"right",  GTK_POS_RIGHT},
    );

    MAP_LINE_VALUE(tab-title-ellipsize-mode, int, tab_title_ellipsize_mode,
            {"start",  PANGO_ELLIPSIZE_START},
            {"middle", PANGO_ELLIPSIZE_MIDDLE},
            {"end",    PANGO_ELLIPSIZE_END},
    );

    MAP_LINE_VALUE(tab-title-alignment, int, tab_title_alignment,
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

    // ONLY events/actions from here on

    ActionData action = {0, 0, {NULL, NULL, NULL}};

    tmp = STR_STRIP_PREFIX(line, "on-");
    if (tmp) {
        char* event = tmp;
        action.key = -1;

#define MAP_EVENT(string, value) \
        if (STR_EQUAL(event, #string)) { \
            action.metadata = value ## _EVENT; \
            break; \
        }

        while (1) {
            MAP_EVENT(bell, BELL);
            MAP_EVENT(hyperlink-hover, HYPERLINK_HOVER);
            MAP_EVENT(hyperlink-click, HYPERLINK_CLICK);
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

    /* g_warning("Unrecognised key: %s", line); */
    return 0;
}

void reconfigure_all() {
    // reload config everywhere
    foreach_window((GFunc)reconfigure_window, NULL);
    create_timer(ui_refresh_interval);
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

void* execute_line(char* line, int size, gboolean reconfigure) {
    char* line_copy;

    line_copy = strdup(line);
    int result = set_config_from_str(line_copy, size);
    free(line_copy);
    if (result) {
        if (reconfigure) reconfigure_all();
        return NULL;
    }

    line_copy = strdup(line);
    Action action = lookup_action(line_copy);
    free(line_copy);
    if (action.func) {
        VteTerminal* terminal = get_active_terminal(NULL);
        if (terminal) {
            char* data = NULL;
            action.func(terminal, action.data, &data);
            if (action.cleanup) {
                action.cleanup(action.data);
            }
            return data;
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
        free(execute_line(line, len, FALSE));
    }

    fclose(config);
    if (line) free(line);
    if (buffer) free(buffer);

    reconfigure_all();
}
