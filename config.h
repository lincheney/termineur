#ifndef CONFIG_H
#define CONFIG_H

#include <vte/vte.h>
#include "action.h"
#include "split.h"

#define APP_PREFIX "VTE_TERMINAL"

#define PALETTE_SIZE (16)
GdkRGBA palette[PALETTE_SIZE+2];
#define BACKGROUND (palette[0])
#define FOREGROUND (palette[1])

// make sure terminal background is transparent by default
#define GLOBAL_CSS \
    "vte-terminal { background: none; }\n" \
    "notebook ." ROOT_SPLIT_CLASS " { background: black; }\n"

char* app_id;
char* config_filename;
char** default_args;
int inactivity_duration;
char* default_open_action;
guint terminal_default_scrollback_lines;
gboolean show_scrollbar;

#define CLOSE_CONFIRM_NO 0
#define CLOSE_CONFIRM_YES 1
#define CLOSE_CONFIRM_SMART 2
gboolean window_close_confirm;
gint tab_close_confirm;

#define BELL_EVENT 1
#define HYPERLINK_HOVER_EVENT 2
#define HYPERLINK_CLICK_EVENT 3

GArray* actions;

char** shell_split(char* string, gint* argc);
int set_config_from_str(char* line, size_t len);
Action lookup_action(char* value);
void reconfigure_all();
void* execute_line(char* line, int size, gboolean reconfigure);
void load_config(char* filename);
void configure_terminal(GtkWidget*);
void configure_tab(GtkContainer*, GtkWidget*);
void configure_window(GtkWindow*);
int trigger_action(VteTerminal* terminal, guint key, int metadata);

#endif
