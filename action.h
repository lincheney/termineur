#ifndef ACTION_H
#define ACTION_H

typedef void(*ActionFunc)(VteTerminal*, void*, char**);
typedef GtkWidget*(*ConnectActionFunc)(VteTerminal*, void*, int* pipes);

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

Action make_action(char*, char*);

GtkWidget* new_tab(VteTerminal* terminal, char* data, int* pipes);

#endif
