#ifndef TERMINAL_H
#define TERMINAL_H

GtkWidget* make_terminal(GtkWidget* grid, const char* cwd, int argc, char** argv);
void create_timer(guint interval);
void set_tab_title_format(char*);
void set_window_title_format(char*);
gboolean get_foreground_name(VteTerminal* terminal, char* buffer, size_t length);
int is_running_foreground_process(VteTerminal* terminal);
void update_terminal_ui(VteTerminal* terminal);
void update_window_title(GtkWindow*, gpointer);
gboolean refresh_all_terminals(gpointer);

#endif
