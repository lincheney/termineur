#ifndef SEARCH_BAR_H
#define SEARCH_BAR_H

#include <vte/vte.h>

GtkWidget* search_bar_new(VteTerminal* terminal);
void search_bar_show(GtkWidget* bar);
#define search_bar_hide(bar) gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(bar), FALSE)

#endif
