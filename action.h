#ifndef ACTION_H
#define ACTION_H

typedef void(*ActionFunc)(VteTerminal*, void*, char**);
typedef GtkWidget*(*ConnectActionFunc)(VteTerminal*, void*, int** pipes);

typedef struct {
    ActionFunc func;
    gpointer data;
    GDestroyNotify cleanup;
} Action;

typedef struct {
    guint key;
    int metadata;
    Action action;
} ActionData;

typedef guint32 ActionKey;
typedef guint32 ActionMetadata;

int trigger_action(VteTerminal* terminal, ActionKey key, ActionMetadata metadata);
void add_action_binding(ActionKey key, ActionMetadata metadata, Action action);
void remove_action_binding(ActionKey key, ActionMetadata metadata);
void remove_all_action_bindings();

Action make_action(char*, char*);
void free_action(Action* action);

GtkWidget* new_tab(VteTerminal* terminal, char* data, int** pipes);
GtkWidget* new_window(VteTerminal* terminal, char* data, int** pipes);
GtkWidget* split_left(VteTerminal* terminal, char* data, int** pipes);
GtkWidget* split_right(VteTerminal* terminal, char* data, int** pipes);
GtkWidget* split_above(VteTerminal* terminal, char* data, int** pipes);
GtkWidget* split_below(VteTerminal* terminal, char* data, int** pipes);

#endif
