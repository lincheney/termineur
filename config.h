#ifndef CONFIG_H
#define CONFIG_H

#include <vte/vte.h>

char* config_filename;
gboolean show_scrollbar;
char** default_args;
int inactivity_duration;

gboolean window_close_confirm;
#define CLOSE_CONFIRM_NO 0
#define CLOSE_CONFIRM_YES 1
#define CLOSE_CONFIRM_SMART 2
gint tab_close_confirm;

typedef void(*KeyComboCallback)(VteTerminal*, gpointer);
typedef struct {
    guint key;
    GdkModifierType modifiers;
    KeyComboCallback callback;
    gpointer data;
} KeyCombo;

GArray* keyboard_shortcuts;

void set_config_from_str(char* line, size_t len);
void reconfigure_all();
void load_config();
void configure_terminal(GtkWidget*);
void configure_tab(GtkContainer*, GtkWidget*);
void configure_window(GtkWindow*);

#endif
