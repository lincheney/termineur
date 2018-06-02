#ifndef TERMINAL_H
#define TERMINAL_H

GtkWidget* make_terminal(GtkWidget* grid, const char* cwd, int argc, char** argv);
void create_timer(guint interval);
void set_tab_title_format(char*);
void set_window_title_format(char*);
void update_terminal_ui(VteTerminal* terminal);
void update_window_title(GtkWindow*, gpointer);
gboolean refresh_all_terminals(gpointer);

#endif
