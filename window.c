#include <gtk/gtk.h>
#include <vte/vte.h>
#include "window.h"
#include "config.h"
#include "terminal.h"

GtkWidget* make_window();
GList* toplevel_windows = NULL;

VteTerminal* get_nth_terminal(GtkWidget* window, int index) {
    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    GtkWidget* grid = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), index);
    GtkWidget* terminal = g_object_get_data(G_OBJECT(grid), "terminal");
    return VTE_TERMINAL(terminal);
}

GtkWidget* get_active_window() {
    return toplevel_windows ? toplevel_windows->data : NULL;
    /* for (GList* node = toplevel_windows; node; node = node->next) { */
        /* if (gtk_window_is_active(GTK_WINDOW(node->data))) { */
            /* return node->data; */
        /* } */
    /* } */
    /* return NULL; */
}

VteTerminal* get_active_terminal(GtkWidget* window) {
    if (! window) window = get_active_window();
    if (! window) return NULL;

    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
    if (index < 0) return NULL;
    return get_nth_terminal(window, index);
}

gboolean key_pressed(GtkWidget* window, GdkEventKey* event, gpointer data) {
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    VteTerminal* terminal = get_active_terminal(window);
    return trigger_callback(terminal, event->keyval, modifiers);
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
    VteTerminal* terminal = g_object_get_data(G_OBJECT(tab), "terminal");
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
    update_window_title(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(notebook))), terminal);
}

void notebook_pages_changed(GtkNotebook* notebook) {
    refresh_all_terminals(NULL);
}

gint run_confirm_close_dialog(GtkWidget* window, char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
            message);
    gtk_window_set_title(GTK_WINDOW(dialog), "Confirm close");

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response;
}

gboolean prevent_window_close(GtkWidget* window, GdkEvent* event, gpointer data) {
    if (! window_close_confirm) return FALSE;

    GtkNotebook* notebook = GTK_NOTEBOOK(g_object_get_data(G_OBJECT(window), "notebook"));
    int npages = gtk_notebook_get_n_pages(notebook);
    char message[1024];
    snprintf(message, sizeof(message), "You have %i tab(s) open.\nAre you sure you want to quit?", npages);

    gint response = run_confirm_close_dialog(window, message);
    return response != GTK_RESPONSE_YES;
}

void window_destroyed(GtkWindow* window) {
    toplevel_windows = g_list_remove(toplevel_windows, window);
    if (toplevel_windows == NULL) {
        gtk_main_quit();
    }
}

gboolean window_focus_event(GtkWindow* window) {
    // move to start of list
    toplevel_windows = g_list_remove(toplevel_windows, window);
    toplevel_windows = g_list_prepend(toplevel_windows, window);
    return FALSE;
}

gboolean prevent_tab_close(VteTerminal* terminal) {
    if (! tab_close_confirm) return FALSE;
    if (tab_close_confirm == CLOSE_CONFIRM_SMART && !is_running_foreground_process(terminal)) {
        return FALSE;
    }

    char message[1024], name[512];
    get_foreground_name(terminal, name, sizeof(name));
    snprintf(message, sizeof(message), "%s is still running.\nAre you sure you want to close it?", name);

    gint response = run_confirm_close_dialog(gtk_widget_get_toplevel(GTK_WIDGET(terminal)), message);
    return response != GTK_RESPONSE_YES;
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

void add_terminal_full(GtkWidget* window, const char* cwd, int argc, char** argv) {
    GtkWidget* tab = gtk_grid_new();
    make_terminal(tab, cwd, argc, argv);
    add_tab_to_window(window, tab, -1);
}

GtkWidget* make_window() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    /* toplevel_windows = g_list_prepend(toplevel_windows, window); */

    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_can_focus(notebook, FALSE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(notebook), "terminals");
    g_object_set_data(G_OBJECT(window), "notebook", notebook);
    g_signal_connect(window, "delete-event", G_CALLBACK(prevent_window_close), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(window_destroyed), NULL);
    g_signal_connect(window, "focus-in-event", G_CALLBACK(window_focus_event), NULL);

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

GtkWidget* make_new_window_full(GtkWidget* tab, const char* cwd, int argc, char** argv) {
    GtkWidget* window = make_window();
    if (tab) {
        add_tab_to_window(window, tab, -1);
    } else {
        add_terminal_full(window, cwd, argc, argv);
    }
    return window;
}

void foreach_window(GFunc callback, gpointer data) {
    g_list_foreach(toplevel_windows, callback, data);
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
    for (GList* node = toplevel_windows; node; node = node->next) {
        foreach_terminal_in_window(GTK_WIDGET(node->data), callback, data);
    }
}
