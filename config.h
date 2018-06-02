#ifndef CONFIG_H
#define CONFIG_H

#include <vte/vte.h>

char* config_filename;
gboolean show_scrollbar;
char** default_args;
int inactivity_duration;

typedef void(*KeyComboCallback)(VteTerminal*, gpointer);
typedef struct {
    guint key;
    GdkModifierType modifiers;
    KeyComboCallback callback;
    gpointer data;
} KeyCombo;

GArray* keyboard_shortcuts;

void load_config();
void configure_terminal(GtkWidget*);
void configure_tab(GtkContainer*, GtkWidget*);
void configure_window(GtkWindow*);

#endif
