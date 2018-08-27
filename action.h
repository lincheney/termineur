#ifndef ACTION_H
#define ACTION_H

typedef void(*ActionFunc)(VteTerminal*, char**, void*);

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

#endif
