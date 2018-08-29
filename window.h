#ifndef WINDOW_H
#define WINDOW_H

GList* toplevel_windows;

GtkWidget* get_active_window();
VteTerminal* get_active_terminal();
gint get_tab_number(VteTerminal* terminal);
GtkWidget* window_get_notebook(GtkWidget*);

GtkWidget* make_window();
GtkWidget* make_new_window_full(GtkWidget*, const char*, int, char**);
#define make_new_window(widget) make_new_window_full(widget, NULL, 0, NULL)
#define add_terminal(widget) add_terminal_full(widget, NULL, 0, NULL)
void add_tab_to_window(GtkWidget*, GtkWidget*, int);
gboolean prevent_tab_close(VteTerminal*);
void refresh_ui_window(GtkWidget* window);

#define FOREACH_WINDOW(var) \
    for (GList* _l = toplevel_windows; _l; _l = _l->next) \
    for (GtkWidget* var = _l->data; var; var = NULL)

#define FOREACH_TAB(var, window) \
    for (GtkNotebook* _notebook = GTK_NOTEBOOK(window_get_notebook(window)); _notebook; _notebook = NULL) \
    for (int _i = 0, _n = gtk_notebook_get_n_pages(_notebook); _i < _n; _i ++) \
    for (GtkWidget* var = gtk_notebook_get_nth_page(_notebook, _i); var; var = NULL)

#define FOREACH_TERMINAL(var, tab) \
    for (GSList* _sl = g_object_get_data(G_OBJECT(tab), TERMINAL_FOCUS_KEY); _sl; _sl = _sl->next) \
    for (VteTerminal* var = VTE_TERMINAL(_sl->data); var; var = NULL)

#endif
