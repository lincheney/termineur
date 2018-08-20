#ifndef TERMINAL_H
#define TERMINAL_H

#define get_pid(terminal) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "pid"))

GtkWidget* make_terminal(GtkWidget* grid, const char* cwd, int argc, char** argv);
void create_timer(guint interval);
void set_tab_title_format(char*);
void set_window_title_format(char*);
int get_foreground_pid(VteTerminal* terminal);
gboolean get_foreground_name(VteTerminal* terminal, char* buffer, size_t length);
int is_running_foreground_process(VteTerminal* terminal);
void update_terminal_ui(VteTerminal* terminal);
void update_window_title(GtkWindow*, VteTerminal* terminal);
gboolean refresh_all_terminals(gpointer);
void enable_terminal_scrollbar(GtkWidget* terminal, gboolean enable);
void add_label_class(GtkWidget* terminal, char* class);
void remove_label_class(GtkWidget* terminal, char* class);

#endif
