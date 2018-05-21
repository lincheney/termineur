#ifndef CONFIG_H
#define CONFIG_H

#define PALETTE_SIZE (16)
extern GdkRGBA palette[PALETTE_SIZE+2];
gboolean show_scrollbar;
char** default_args;

typedef void(*KeyComboCallback)(VteTerminal*);
typedef struct {
    guint key;
    GdkModifierType modifiers;
    KeyComboCallback callback;
} KeyCombo;

GArray* keyboard_shortcuts;

void load_config(const char*, GtkWidget*, GtkWidget*);

#endif
