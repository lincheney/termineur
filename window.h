#ifndef WINDOW_H
#define WINDOW_H

GtkApplication* app;

#define get_active_window() gtk_application_get_active_window(app)
VteTerminal* get_active_terminal();
gint get_tab_number(VteTerminal* terminal);

GtkWidget* make_new_window(GtkWidget*);
void add_terminal(GtkWidget*);
void add_tab_to_window(GtkWidget*, GtkWidget*, int);

void foreach_window(GFunc, gpointer);
void foreach_terminal_in_window(GtkWidget* window, GFunc, gpointer);
void foreach_terminal(GFunc, gpointer);

#endif
