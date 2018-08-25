#ifndef TERMINAL_H
#define TERMINAL_H

#include <vte/vte.h>
#include <termios.h>

#define get_pid(terminal) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "pid"))

GtkWidget* make_terminal(const char* cwd, int argc, char** argv);
void create_timer(guint interval);
void set_tab_title_format(char*);
void set_window_title_format(char*);
int get_foreground_pid(VteTerminal* terminal);
struct termios get_term_attr(VteTerminal* terminal);
gboolean get_foreground_name(VteTerminal* terminal, char* buffer, size_t length);
int is_running_foreground_process(VteTerminal* terminal);
void update_terminal_ui(VteTerminal* terminal);
void update_window_title(GtkWindow*, VteTerminal* terminal);
gboolean refresh_all_terminals(gpointer);
gboolean term_hide_message_bar(VteTerminal* terminal);
void term_show_message_bar(VteTerminal* terminal, const char* message, int timeout);
void enable_terminal_scrollbar(VteTerminal* terminal, gboolean enable);
void term_change_css_class(VteTerminal* terminal, char* class, gboolean add);
GtkWidget* term_get_grid(VteTerminal* terminal);
GtkWidget* term_get_notebook(VteTerminal* terminal);
GtkWidget* term_get_tab(VteTerminal* terminal);
#define term_get_window(terminal) gtk_widget_get_toplevel(GTK_WIDGET(terminal))

#endif
