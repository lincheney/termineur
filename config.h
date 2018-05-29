#ifndef CONFIG_H
#define CONFIG_H

#include <vte/vte.h>

#define PALETTE_SIZE (16)
extern GdkRGBA palette[PALETTE_SIZE+2];
gboolean show_scrollbar;
char** default_args;

typedef void(*KeyComboCallback)(VteTerminal*, gpointer);
typedef struct {
    guint key;
    GdkModifierType modifiers;
    KeyComboCallback callback;
    gpointer data;
} KeyCombo;

GArray* keyboard_shortcuts;
GObject* dummy_terminal;
GHashTable* terminal_properties;
char* window_icon;

void copy_properties(GObject* src, GObject* dest);
void load_config(const char*, GApplication*);

#endif
