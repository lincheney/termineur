#ifndef SPLIT_H
#define SPLIT_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#define TERMINAL_FOCUS_KEY "focus_chain"

GtkWidget* split_new();
GtkWidget* split_get_root(GtkWidget* paned);
GtkWidget* split_get_container(GtkWidget* widget);
GtkWidget* split(GtkWidget* dest, GtkWidget* src, GtkOrientation orientation, gboolean after);
void split_cleanup(GtkWidget* paned);
gboolean split_move(GtkWidget* widget, GtkOrientation orientation, gboolean forward);
gboolean split_move_focus(GtkWidget* widget, GtkOrientation orientation, gboolean forward);

GtkWidget* split_get_active_term(GtkWidget* paned);
void split_remove_term_from_chain(VteTerminal* terminal);
void split_set_active_term(VteTerminal* terminal);

int split_get_separator_size(GtkWidget* paned);

#endif
