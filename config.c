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
guint message_bar_animation_duration = 250;

// search options
int search_case_sensitive = REGEX_CASE_SMART;
gboolean search_use_regex = FALSE;
gboolean search_wrap_around = TRUE;
guint search_bar_animation_duration = 250;

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

void init_palette() {
    for (int i = 0; i < PALETTE_SIZE; i++) {
        if (i < 8) {
            palette[i].red = (i & 1) ? 0.5 : 0;
            palette[i].green = (i & 2) ? 0.5 : 0;
            palette[i].blue = (i & 4) ? 0.5 : 0;
        } else if (i < 16) {
            palette[i].red = (i & 1) ? 1 : 0;
            palette[i].green = (i & 2) ? 1 : 0;
            palette[i].blue = (i & 4) ? 1 : 0;
        } else if (i < 232) {
            int j = i - 16;
            palette[i].red = ((double)(j / 36)) / 5.;
            palette[i].green = ((double)((j / 6) % 6)) / 5.;
            palette[i].blue = ((double)(j % 6)) / 5.;
        } else {
            palette[i].red = palette[i].green = palette[i].blue = ((double)(i - 232)) / (255 - 232);
        }
    }
    FOREGROUND.red = FOREGROUND.green = FOREGROUND.blue = 1;
    BACKGROUND.red = BACKGROUND.green = BACKGROUND.blue = 0;
}

void configure_terminal(VteTerminal* terminal) {
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
    vte_terminal_set_word_char_exceptions(terminal, terminal_word_char_exceptions);
    vte_terminal_search_set_wrap_around(terminal, search_wrap_around);
    // populate palette
    vte_terminal_set_colors(terminal, &FOREGROUND, &BACKGROUND, palette, PALETTE_SIZE);

    GtkWidget* grid = term_get_grid(terminal);

    GtkWidget* searchbar = g_object_get_data(G_OBJECT(grid), "searchbar");
    GtkWidget* revealer = gtk_bin_get_child(GTK_BIN(searchbar));
    if (GTK_IS_REVEALER(revealer)) {
        gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), search_bar_animation_duration);
    }

    GtkWidget* msg_bar = g_object_get_data(G_OBJECT(grid), "msg_bar");
    gtk_revealer_set_transition_duration(GTK_REVEALER(msg_bar), message_bar_animation_duration);
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
            configure_terminal(terminal);
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
    if (STR_EQUAL(line, "")) {
        return 1;
    }

#define LINE_EQUALS(string) (STR_EQUAL(line, (string)))
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
#define TRY_MAP_VALUE(var, _value, string, matcher) \
    if (value && ((matcher) || STR_IEQUAL(value, (string)))) { \
        var = (_value); \
        return 1; \
    } else if (!value && var == (_value)) { \
        *result = strdup(string); \
    }

#define MAP_LINE_VALUE(string, type, ...) MAP_LINE(string, MAP_VALUE(type, __VA_ARGS__))

#define PARSE_BOOL(string) ( ! ( \
    STR_IEQUAL((string), "no") \
    || STR_IEQUAL((string), "n") \
    || STR_IEQUAL((string), "false") \
    || STR_IEQUAL((string), "off") \
    || STR_EQUAL((string), "") \
    || STR_EQUAL((string), "0") \
    ))
#define MAP_BOOL(var) \
    if (value) { var = PARSE_BOOL(value); } \
    else { *result = strdup(var ? "1" : "0"); }
#define MAP_INT(var) \
    if (value) { var = atoi(value); } \
    else { *result = g_strdup_printf("%i", var); }
#define MAP_FLOAT(var) \
    if (value) { var = strtod(value, NULL); } \
    else { *result = g_strdup_printf("%f", var); }
#define MAP_STR(var) \
    if (value && ! (var && STR_EQUAL(var, value))) { free(var); var = strdup(value); } \
    else if (!value && var) { *result = strdup(var); }
#define MAP_COLOUR(val) \
    if (value) { gdk_rgba_parse((val), value); } \
    else { *result = gdk_rgba_to_string(val); } \

    // palette colours
    if ((tmp = STR_STRIP_PREFIX(line, "col"))) {
        errno = 0;
        char* endptr = NULL;
        int n = strtol(tmp, &endptr, 10);
        if (!errno && *endptr == '\0' && 0 <= n && n < PALETTE_SIZE) {
            MAP_COLOUR(palette+n);
            return 1;
        }
    }

    if (LINE_EQUALS("css")) {
        if (value) {
            gtk_css_provider_load_from_data(css_provider, value, -1, NULL);
            GdkScreen* screen = gdk_screen_get_default();
            gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
        } else {
            *result = gtk_css_provider_to_string(css_provider);
        }
        return 1;
    }

    MAP_LINE("background",              MAP_COLOUR(&BACKGROUND));
    MAP_LINE("foreground",              MAP_COLOUR(&FOREGROUND));
    MAP_LINE("window-title-format",     if (value) set_window_title_format(value)); // TODO
    MAP_LINE("tab-label-format",        MAP_STR(tab_label_format); if (value) { free(tab_title_ui_format); tab_title_ui_format = NULL; } );
    MAP_LINE("tab-title-ui",            MAP_STR(tab_title_ui_format); if (value) { free(tab_label_format); tab_label_format= NULL; } );
    MAP_LINE("tab-fill",                MAP_BOOL(tab_fill));
    MAP_LINE("tab-expand",              MAP_BOOL(tab_expand));
    MAP_LINE("tab-enable-popup",        MAP_BOOL(notebook_enable_popup));
    MAP_LINE("tab-scrollable",          MAP_BOOL(notebook_scrollable));
    MAP_LINE("show-tabs",               MAP_BOOL(notebook_show_tabs));
    MAP_LINE("ui-refresh-interval",     MAP_INT(ui_refresh_interval));
    MAP_LINE("inactivity-duration",     MAP_INT(inactivity_duration));
    MAP_LINE("encoding",                MAP_STR(terminal_encoding));
    MAP_LINE("font-scale",              MAP_FLOAT(terminal_font_scale));
    MAP_LINE("audible-bell",            MAP_BOOL(terminal_audible_bell));
    MAP_LINE("allow-hyperlink",         MAP_BOOL(terminal_allow_hyperlink));
    MAP_LINE("pointer-autohide",        MAP_BOOL(terminal_pointer_autohide));
    MAP_LINE("rewrap-on-resize",        MAP_BOOL(terminal_rewrap_on_resize));
    MAP_LINE("scroll-on-keystroke",     MAP_BOOL(terminal_scroll_on_keystroke));
    MAP_LINE("scroll-on-output",        MAP_BOOL(terminal_scroll_on_output));
    MAP_LINE("default-scrollback-lines",MAP_INT(terminal_default_scrollback_lines));
    MAP_LINE("word-char-exceptions",    MAP_STR(terminal_word_char_exceptions));
    MAP_LINE("window-icon",             MAP_STR(window_icon));
    MAP_LINE("window-close-confirm",    MAP_BOOL(window_close_confirm));
    MAP_LINE("search-use-regex",        MAP_BOOL(search_use_regex));
    MAP_LINE("search-wrap-around",      MAP_BOOL(search_wrap_around));
    MAP_LINE("search-bar-animation-duration", MAP_INT(search_bar_animation_duration));
    MAP_LINE("message-bar-animation-duration", MAP_INT(message_bar_animation_duration));

    if (LINE_EQUALS("font")) {
        if (value) {
            pango_font_description_free(terminal_font);
            terminal_font = pango_font_description_from_string(value);
        } else if (terminal_font) {
            *result = pango_font_description_to_string(terminal_font);
        }
        return 1;
    }

    if (LINE_EQUALS("search-pattern")) {
        // this only affects the *current* terminal
        VteTerminal* terminal = get_active_terminal(NULL);
        if (terminal) {
            if (value) {
                term_search(terminal, value, 0);
            } else {
                char* old = g_object_get_data(G_OBJECT(terminal), "search-pattern");
                if (old) *result = strdup(old);
            }
        }
        return 1;
    }

    if (LINE_EQUALS("scrollback-lines")) {
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

    if (LINE_EQUALS("tab-close-confirm")) {
        TRY_MAP_VALUE(tab_close_confirm, CLOSE_CONFIRM_SMART, "smart", FALSE);
        TRY_MAP_VALUE(tab_close_confirm, CLOSE_CONFIRM_YES, "1", PARSE_BOOL(value));
        TRY_MAP_VALUE(tab_close_confirm, CLOSE_CONFIRM_NO, "0", TRUE);
        return 1;
    }

    if (LINE_EQUALS("default-args")) {
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

    if (LINE_EQUALS("show-scrollbar")) {
        TRY_MAP_VALUE(scrollbar_policy, GTK_POLICY_AUTOMATIC, "auto", FALSE);
        TRY_MAP_VALUE(scrollbar_policy, GTK_POLICY_AUTOMATIC, "automatic", FALSE);
        TRY_MAP_VALUE(scrollbar_policy, GTK_POLICY_NEVER, "never", FALSE);
        TRY_MAP_VALUE(scrollbar_policy, GTK_POLICY_NEVER, "never", !PARSE_BOOL(value));
        TRY_MAP_VALUE(scrollbar_policy, GTK_POLICY_ALWAYS, "always", TRUE);
        return 1;
    }

    if (LINE_EQUALS("search-case-sensitive")) {
        TRY_MAP_VALUE(search_case_sensitive, REGEX_CASE_SMART, "smart", FALSE);
        TRY_MAP_VALUE(search_case_sensitive, REGEX_CASE_SENSITIVE, "1", PARSE_BOOL(value));
        TRY_MAP_VALUE(search_case_sensitive, REGEX_CASE_INSENSITIVE, "0", TRUE);
        return 1;
    }

    if (LINE_EQUALS("cursor-blink-mode")) {
        TRY_MAP_VALUE(terminal_cursor_blink_mode, VTE_CURSOR_BLINK_SYSTEM, "system", FALSE);
        TRY_MAP_VALUE(terminal_cursor_blink_mode, VTE_CURSOR_BLINK_ON, "1", PARSE_BOOL(value));
        TRY_MAP_VALUE(terminal_cursor_blink_mode, VTE_CURSOR_BLINK_OFF, "0", TRUE);
        return 1;
    }

    MAP_LINE_VALUE("tab-pos", int, notebook_tab_pos,
            {"top",    GTK_POS_TOP},
            {"bottom", GTK_POS_BOTTOM},
            {"left",   GTK_POS_LEFT},
            {"right",  GTK_POS_RIGHT},
    );

    MAP_LINE_VALUE("tab-title-ellipsize-mode", int, tab_label_ellipsize_mode,
            {"start",  PANGO_ELLIPSIZE_START},
            {"middle", PANGO_ELLIPSIZE_MIDDLE},
            {"end",    PANGO_ELLIPSIZE_END},
    );

    MAP_LINE_VALUE("tab-label-alignment", int, tab_label_alignment,
            {"left",   0},
            {"right",  1},
            {"center", 0.5},
    );

    MAP_LINE_VALUE("cursor-shape", int, terminal_cursor_shape,
            {"block",     VTE_CURSOR_SHAPE_BLOCK},
            {"ibeam",     VTE_CURSOR_SHAPE_IBEAM},
            {"underline", VTE_CURSOR_SHAPE_UNDERLINE},
    );

    MAP_LINE_VALUE("default-open-action", char*, default_open_action,
            {"tab",       "new_tab"},
            {"window",    "new_window"},
    );

    // ONLY events from here on
    // events must take a value
    char* event;
    if (value && (event = STR_STRIP_PREFIX(line, "on-"))) {

        ActionData action = {0, 0, {NULL, NULL, NULL}};

#define MAP_EVENT(string, value) \
        if (STR_EQUAL(event, #string)) { \
            action.key = -1; \
            action.metadata = value; \
            break; \
        }

        while (1) {
            MAP_EVENT(bell, BELL_EVENT);
            MAP_EVENT(hyperlink-hover, HYPERLINK_HOVER_EVENT);
            MAP_EVENT(hyperlink-click, HYPERLINK_CLICK_EVENT);
            MAP_EVENT(focus, FOCUS_IN_EVENT);
            MAP_EVENT(start, START_EVENT);

            char* shortcut;
            if ((shortcut = STR_STRIP_PREFIX(event, "key-"))) {
                gtk_accelerator_parse(shortcut, &(action.key), (GdkModifierType*) &(action.metadata));
                if (action.metadata & GDK_SHIFT_MASK) {
                    action.key = gdk_keyval_to_upper(action.key);
                }
                break;
            }

            break;
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

    if (do_actions) {
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
    }

    g_warning("Invalid input: %s", line);
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

    init_palette();

    if (filename) {
        char* final = filename;
        if (! final) final = config_filename;
        if (! final) final = g_build_filename(g_get_user_config_dir(), "vte_terminal", "config.ini", NULL);
        FILE* file = fopen(final, "r");

        if (file) {
            char* line = NULL;
            char* buffer = NULL;
            size_t size = 0, bufsize = 0;
            ssize_t len, l;
            while ((len = getline(&line, &size, file)) != -1) {
                // multilines
                if (len >= 4 && STR_EQUAL(line+len-4, "\"\"\"\n")) {
                    len -= 4;
                    while ((l = getline(&buffer, &bufsize, file)) != -1) {
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
            fclose(file);
            if (line) free(line);
            if (buffer) free(buffer);

        } else if (errno == ENOENT && !filename && !config_filename) {
            // do nothing, if no file
        } else {
            g_warning("Failed to open %s: %s", final, strerror(errno));
        }
    }

    reconfigure_all();
}
