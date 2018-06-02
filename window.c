#include <gtk/gtk.h>
#include <vte/vte.h>
#include "window.h"
#include "config.h"
#include "terminal.h"

GtkWidget* make_window();

VteTerminal* get_nth_terminal(GtkWidget* window, int index) {
    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    GtkWidget* grid = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), index);
    GtkWidget* terminal = g_object_get_data(G_OBJECT(grid), "terminal");
    return VTE_TERMINAL(terminal);
}

VteTerminal* get_active_terminal(GtkWidget* window) {
    if (! window) window = GTK_WIDGET(get_active_window());
    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
    if (index < 0) return NULL;
    return get_nth_terminal(window, index);
}

gboolean key_pressed(GtkWidget* window, GdkEventKey* event, gpointer data) {
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    gboolean handled = FALSE;
    for (int i = 0; i < keyboard_shortcuts->len; i++) {
        KeyCombo* combo = &g_array_index(keyboard_shortcuts, KeyCombo, i);
        if (combo->key == event->keyval && combo->modifiers == modifiers) {
            VteTerminal* terminal = get_active_terminal(window);
            combo->callback(terminal, combo->data);
            handled = TRUE;
        }
    }
    return handled;
}

gint get_tab_number(VteTerminal* terminal) {
    GtkWidget* tab = gtk_widget_get_parent(GTK_WIDGET(terminal));
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_widget_get_parent(tab));
    return gtk_notebook_page_num(notebook, tab);
}

void notebook_tab_removed(GtkWidget* notebook, GtkWidget *child, guint page_num) {
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) == 0) {
        gtk_widget_destroy(gtk_widget_get_toplevel(notebook));
    }
}

GtkNotebook* notebook_create_window(GtkNotebook* notebook, GtkWidget* page, gint x, gint y) {
    GtkWidget* window = make_window();
    return GTK_NOTEBOOK(g_object_get_data(G_OBJECT(window), "notebook"));
}

void notebook_switch_page(GtkNotebook* notebook, GtkWidget* tab, guint num) {
    GtkWidget* term = g_object_get_data(G_OBJECT(tab), "terminal");
    gtk_widget_grab_focus(term);
}

void notebook_pages_changed(GtkNotebook* notebook) {
    refresh_all_terminals(NULL);
}

void add_tab_to_window(GtkWidget* window, GtkWidget* tab, int position) {
    GtkNotebook* notebook = GTK_NOTEBOOK(g_object_get_data(G_OBJECT(window), "notebook"));
    GtkWidget* terminal = g_object_get_data(G_OBJECT(tab), "terminal");
    GtkWidget* label = GTK_WIDGET(g_object_get_data(G_OBJECT(terminal), "label"));
    int page = gtk_notebook_insert_page(notebook, tab, label, position);
    configure_tab(GTK_CONTAINER(notebook), tab);

    gtk_widget_show_all(tab);
    gtk_widget_realize(terminal);
    gtk_notebook_set_current_page(notebook, page);
    gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), tab, TRUE);
    update_terminal_ui(VTE_TERMINAL(terminal));
}

void add_terminal(GtkWidget* window) {
    GtkWidget* tab = gtk_grid_new();
    make_terminal(tab, 0, NULL);
    add_tab_to_window(window, tab, -1);
}

GtkWidget* make_window() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_application_add_window(app, GTK_WINDOW(window));

    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_can_focus(notebook, FALSE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(notebook), "terminals");
    g_object_set_data(G_OBJECT(window), "notebook", notebook);

    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
    g_signal_connect(notebook, "page-removed", G_CALLBACK(notebook_tab_removed), NULL);
    g_signal_connect(notebook, "create-window", G_CALLBACK(notebook_create_window), NULL);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(notebook_switch_page), NULL);
    // make sure term titles update whenever they are reordered
    g_signal_connect(notebook, "page-reordered", G_CALLBACK(notebook_pages_changed), NULL);
    g_signal_connect(notebook, "page-added", G_CALLBACK(notebook_pages_changed), NULL);
    g_signal_connect(notebook, "page-removed", G_CALLBACK(notebook_pages_changed), NULL);

    g_signal_connect(window, "key-press-event", G_CALLBACK(key_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(window), notebook);

    configure_window(GTK_WINDOW(window));
    gtk_widget_show_all(window);
    return window;
}

GtkWidget* make_new_window(GtkWidget* tab) {
    GtkWidget* window = make_window();
    if (tab) {
        add_tab_to_window(window, tab, -1);
    } else {
        add_terminal(window);
    }
    return window;
}

void foreach_window(GFunc callback, gpointer data) {
    GList* windows = gtk_application_get_windows(app);
    g_list_foreach(windows, callback, data);
}
void foreach_terminal_in_window(GtkWidget* window, GFunc callback, gpointer data) {
    GtkWidget* terminal;
    GtkNotebook* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    int n = gtk_notebook_get_n_pages(notebook);
    for (int i = 0; i < n; i ++) {
        terminal = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(notebook, i)), "terminal");
        callback(terminal, data);
    }
}
void foreach_terminal(GFunc callback, gpointer data) {
    for (GList* windows = gtk_application_get_windows(app); windows; windows = windows->next) {
        foreach_terminal_in_window(GTK_WIDGET(windows->data), callback, data);
    }
}
