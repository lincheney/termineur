#ifndef WINDOW_H
#define WINDOW_H

#define DEFAULT_SHELL "/bin/sh"

GtkApplication* app;
GtkWidget* make_window(GtkWidget* terminal);

#define get_active_window() gtk_application_get_active_window(app)
VteTerminal* get_active_terminal();
void add_terminal(GtkWidget*, GtkWidget*);

#endif
