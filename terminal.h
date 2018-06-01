#ifndef TERMINAL_H
#define TERMINAL_H

GtkWidget* make_terminal(GtkWidget* grid, int argc, char** argv);
void create_timer(guint interval);
void set_tab_title_format(char*);

#endif
