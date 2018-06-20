#ifndef CONFIG_H
#define CONFIG_H

#include <vte/vte.h>

#define APP_PREFIX "VTE_TERMINAL"

const char* app_id;
char* config_filename;
gboolean show_scrollbar;
char** default_args;
int inactivity_duration;

gboolean window_close_confirm;
#define CLOSE_CONFIRM_NO 0
#define CLOSE_CONFIRM_YES 1
#define CLOSE_CONFIRM_SMART 2
gint tab_close_confirm;

typedef void(*KeyComboCallbackFunc)(VteTerminal*, gpointer);

typedef struct {
    KeyComboCallbackFunc func;
    gpointer data;
    GDestroyNotify cleanup;
} KeyComboCallback;

typedef struct {
    guint key;
    GdkModifierType modifiers;
    KeyComboCallback callback;
} KeyCombo;

GArray* keyboard_shortcuts;

int set_config_from_str(char* line, size_t len);
KeyComboCallback lookup_callback(char* value);
void reconfigure_all();
void load_config();
void configure_terminal(GtkWidget*);
void configure_tab(GtkContainer*, GtkWidget*);
void configure_window(GtkWindow*);

#endif
