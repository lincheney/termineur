#ifndef WINDOW_H
#define WINDOW_H

#define DEFAULT_SHELL "/bin/sh"

GtkApplication* app;
GtkWidget* new_window(GtkWidget*);

#define get_active_window() gtk_application_get_active_window(app)
VteTerminal* get_active_terminal();
void add_terminal(GtkWidget*);

#endif
