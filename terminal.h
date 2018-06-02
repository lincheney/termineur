#ifndef TERMINAL_H
#define TERMINAL_H

GtkWidget* make_terminal(GtkWidget* grid, char* cwd, int argc, char** argv);
void create_timer(guint interval);
void set_tab_title_format(char*);
void update_terminal_ui(VteTerminal* terminal);
gboolean refresh_all_terminals(gpointer);

#endif
