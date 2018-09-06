#ifndef CONFIG_H
#define CONFIG_H

#include <vte/vte.h>
#include "action.h"
#include "split.h"

#define APP_PREFIX "TERMINEUR"
#define APP_PREFIX_LOWER "termineur"
#define TERM_ENV_VAR "xterm256-color" // this is actually set inside vte

#define PALETTE_SIZE (256)
GdkRGBA palette[PALETTE_SIZE+2];
#define FOREGROUND (palette[PALETTE_SIZE])
#define BACKGROUND (palette[PALETTE_SIZE+1])

// make sure terminal background is transparent by default
#define GLOBAL_CSS \
    "." APP_PREFIX_LOWER " vte-terminal { background: none; }\n" \
    "." APP_PREFIX_LOWER " notebook ." ROOT_SPLIT_CLASS " { background: black; }\n" \
    "." APP_PREFIX_LOWER " notebook header, ." APP_PREFIX_LOWER " notebook tabs tab { padding: 0; margin: 0; border: none; }\n"

char* app_path;
char* app_id;
char* config_filename;
char** default_args;
int inactivity_duration;
char* default_open_action;
gboolean tab_expand;
guint terminal_default_scrollback_lines;
gboolean show_scrollbar;

#define OPTION_NO 0
#define OPTION_YES 1
#define OPTION_SMART 2
gint tab_close_confirm;
gboolean window_close_confirm;
int notebook_show_tabs;

// search options
#define REGEX_CASE_SENSITIVE 0
#define REGEX_CASE_INSENSITIVE 1
#define REGEX_CASE_SMART 2
int search_case_sensitive;
gboolean search_use_regex;
char* search_pattern;

#define EVENT_KEY 0
#define BELL_EVENT 1
#define HYPERLINK_HOVER_EVENT 2
#define HYPERLINK_CLICK_EVENT 3
#define FOCUS_IN_EVENT 4
#define START_EVENT 5

char** shell_split(char* string, gint* argc);
int set_config_from_str(char* line, size_t len);
Action lookup_action(char* value);
void reconfigure_all();
void* execute_line(char* line, int size, gboolean reconfigure, gboolean do_actions);
void load_config(char* filename, gboolean reset);
void configure_terminal(VteTerminal*);
void configure_tab(GtkContainer*, GtkWidget*);
void configure_window(GtkWindow*);

#endif
