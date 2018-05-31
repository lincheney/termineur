#ifndef WINDOW_H
#define WINDOW_H

#define DEFAULT_SHELL "/bin/sh"

GtkApplication* app;

#define get_active_window() gtk_application_get_active_window(app)
VteTerminal* get_active_terminal();

GtkWidget* make_new_window(GtkWidget*);
void add_terminal(GtkWidget*);
void add_tab_to_window(GtkWidget*, GtkWidget*, int);

#endif
