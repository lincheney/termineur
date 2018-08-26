#ifndef CALLBACK_H
#define CALLBACK_H

typedef void(*CallbackFunc)(VteTerminal*, char**, void*);

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

Callback make_callback(char*, char*);

#endif
