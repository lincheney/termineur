#ifndef CONFIG_H
#define CONFIG_H

#include <vte/vte.h>

#define APP_PREFIX "VTE_TERMINAL"

char* app_id;
char* config_filename;
char** default_args;
int inactivity_duration;
char* default_open_action;

gboolean window_close_confirm;
#define CLOSE_CONFIRM_NO 0
#define CLOSE_CONFIRM_YES 1
#define CLOSE_CONFIRM_SMART 2
gint tab_close_confirm;

typedef void(*CallbackFunc)(VteTerminal*, gpointer, gpointer);

#define BELL_EVENT 1
#define HYPERLINK_HOVER_EVENT 2
#define HYPERLINK_CLICK_EVENT 3

typedef struct {
    CallbackFunc func;
    gpointer data;
    GDestroyNotify cleanup;
} Callback;

typedef struct {
    guint key;
    int metadata;
    Callback callback;
} CallbackData;

GArray* callbacks;

int set_config_from_str(char* line, size_t len);
Callback lookup_callback(char* value);
void reconfigure_all();
void* execute_line(char* line, int size, gboolean reconfigure);
void load_config();
void configure_terminal(GtkWidget*);
void configure_tab(GtkContainer*, GtkWidget*);
void configure_window(GtkWindow*);
int trigger_callback(VteTerminal* terminal, guint key, int metadata);

#endif
