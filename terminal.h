#ifndef TERMINAL_H
#define TERMINAL_H

#include <vte/vte.h>
#include <termios.h>
#include <proc/readproc.h>

#define get_pid(terminal) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(terminal), "pid"))

void term_setup_pipes(int pipes[2]);
GtkWidget* make_terminal(const char* cwd, int argc, char** argv);
GtkWidget* make_terminal_full(const char* cwd, int argc, char** argv, GSpawnChildSetupFunc child_setup, void* child_setup_data, GDestroyNotify child_setup_destroy);
void set_window_title_format(char*);
gboolean term_construct_title(const char* format, int flags, VteTerminal* terminal, gboolean escape_markup, char* buffer, size_t length);
proc_t* get_foreground_process(VteTerminal* terminal);
struct termios get_term_attr(VteTerminal* terminal);
int get_immediate_child_pid(VteTerminal* terminal);
int is_running_foreground_process(VteTerminal* terminal);
void update_terminal_css_class(VteTerminal* terminal);
void update_window_title(GtkWindow*, VteTerminal* terminal);
gboolean term_hide_message_bar(VteTerminal* terminal);
void term_show_message_bar(VteTerminal* terminal, const char* message, int timeout);
void configure_terminal_scrollbar(VteTerminal* terminal, GtkPolicyType scrollbar_policy);
void term_set_focus(VteTerminal* terminal, gboolean grab);
void term_change_css_class(VteTerminal* terminal, char* class, gboolean add);
GtkWidget* term_get_grid(VteTerminal* terminal);
GtkWidget* term_get_notebook(VteTerminal* terminal);
GtkWidget* term_get_tab(VteTerminal* terminal);
#define term_get_window(terminal) gtk_widget_get_toplevel(GTK_WIDGET(terminal))
GtkWidget* term_remove(VteTerminal* terminal);
void term_select_range(VteTerminal* terminal, double start_col, double start_row, double end_col, double end_row, int modifiers, gboolean double_click);
void term_get_row_positions(VteTerminal* terminal, int* screen_lower, int* screen_upper, int* lower, int* upper);
char* term_get_text(VteTerminal* terminal, glong start_row, glong start_col, glong end_row, glong end_col, gboolean ansi);
gboolean term_search(VteTerminal* terminal, const char* data, int direction);

#endif
