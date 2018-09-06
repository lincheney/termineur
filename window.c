#include <gtk/gtk.h>
#include <vte/vte.h>
#include "window.h"
#include "config.h"
#include "terminal.h"
#include "tab_title_ui.h"
#include "split.h"
#include "utils.h"

GList* toplevel_windows = NULL;

GtkWidget* window_get_notebook(GtkWidget* window) {
    return GTK_WIDGET(g_object_get_data(G_OBJECT(window), "notebook"));
}

VteTerminal* get_nth_terminal(GtkWidget* window, int index) {
    GtkWidget* notebook = window_get_notebook(window);
    GtkWidget* tab = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), index);
    GtkWidget* terminal = split_get_active_term(tab);
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

    GtkWidget* notebook = window_get_notebook(window);
    int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
    if (index < 0) return NULL;
    return get_nth_terminal(window, index);
}

void refresh_ui_window(GtkWidget* window) {
    update_window_title(GTK_WINDOW(window), NULL);

    FOREACH_TAB(tab, window) {
        update_tab_titles(VTE_TERMINAL(split_get_active_term(tab)));

        FOREACH_TERMINAL(terminal, tab) {
            update_terminal_css_class(terminal);
        }
    }
}

gboolean key_pressed(GtkWidget* window, GdkEventKey* event, gpointer data) {
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    VteTerminal* terminal = get_active_terminal(window);
    return trigger_action(terminal, event->keyval, modifiers);
}

gint get_tab_number(VteTerminal* terminal) {
    GtkWidget* tab = term_get_tab(terminal);
    GtkNotebook* notebook = GTK_NOTEBOOK(gtk_widget_get_parent(tab));
    return gtk_notebook_page_num(notebook, tab);
}

void notebook_size_allocate(GtkNotebook* notebook, GdkRectangle* alloc) {
    GtkPositionType pos = gtk_notebook_get_tab_pos(notebook);
    gboolean vertical = (pos == GTK_POS_LEFT || pos == GTK_POS_RIGHT);
    int width = alloc->width;
    int height = alloc->height;

    GtkWidget* action_widget = gtk_notebook_get_action_widget(notebook, GTK_PACK_END);
    if (action_widget && gtk_widget_is_visible(action_widget)) {
        GdkRectangle rect;
        gtk_widget_get_allocation(action_widget, &rect);

        if (rect.width > 1 && rect.height > 1) {
            if (vertical) {
                rect.y -= rect.width - rect.height;
                rect.height = rect.width;
            } else {
                rect.x -= rect.height- rect.width;
                rect.width = rect.height;
            }
            gtk_widget_size_allocate(action_widget, &rect);

            width -= rect.width;
            height -= rect.height;
        }
    }

    // don't bother sizing tabs if they don't expand
    if (! tab_expand) return;

    int n = gtk_notebook_get_n_pages(notebook);
    if (n == 0) return;

    GtkStateFlags state = gtk_widget_get_state_flags(GTK_WIDGET(notebook));
    GtkStyleContext* style = gtk_widget_get_style_context(GTK_WIDGET(notebook));
    GtkBorder border, padding, margin;
    gtk_style_context_get_border(style, state, &border);
    gtk_style_context_get_padding(style, state, &padding);
    gtk_style_context_get_margin(style, state, &margin);

    width -= border.left + padding.left + margin.left + border.right + padding.right + margin.right;
    height -= border.top + padding.top + margin.top + border.bottom + padding.bottom + margin.bottom;

    // resize tab titles to be all the same size
    for (int i = 0; i < n; i ++) {
        GtkWidget* page = gtk_notebook_get_nth_page(notebook, i);
        GtkWidget* label = gtk_notebook_get_tab_label(notebook, page);
        if (label) {
            double value;
            if (vertical) {
                value = ((double)height) / n;
                gtk_widget_set_size_request(label, -1, (int)(value*(i+1)) - (int)(value*i));
            } else {
                value = ((double)width) / n;
                gtk_widget_set_size_request(label, (int)(value*(i+1)) - (int)(value*i), -1);
            }
        }
    }
}

void notebook_tab_removed(GtkWidget* notebook, GtkWidget *child, guint page_num) {
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) == 0) {
        gtk_widget_destroy(gtk_widget_get_toplevel(notebook));
    } else {
        GdkRectangle rect;
        gtk_widget_get_allocation(notebook, &rect);
        notebook_size_allocate(GTK_NOTEBOOK(notebook), &rect);
    }
}

GtkNotebook* notebook_create_window(GtkNotebook* notebook, GtkWidget* page, gint x, gint y) {
    GtkWidget* window = make_window();
    return GTK_NOTEBOOK(window_get_notebook(window));
}

void notebook_switch_page(GtkNotebook* notebook, GtkWidget* tab, guint num) {
    GtkWidget* terminal = split_get_active_term(tab);
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
    update_window_title(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(notebook))), VTE_TERMINAL(terminal));
}

void refresh_ui_notebook(GtkWidget* notebook) {
    GtkWidget* window = gtk_widget_get_toplevel(notebook);
    if (gtk_widget_is_toplevel(window)) {
        refresh_ui_window(window);
    }

    if (notebook_show_tabs == OPTION_SMART) {
        int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
        g_object_set(G_OBJECT(notebook), "show-tabs", n > 1, NULL);
    } else {
        g_object_set(G_OBJECT(notebook), "show-tabs", notebook_show_tabs, NULL);
    }
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

    GtkNotebook* notebook = GTK_NOTEBOOK(window_get_notebook(window));
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

gboolean notebook_focus_event(GtkWindow* notebook, GdkEvent* event, GtkWidget* window) {
    // always pass focus to the terminal
    VteTerminal* terminal = get_active_terminal(window);
    gtk_widget_grab_focus(GTK_WIDGET(terminal));
    return FALSE;
}

gboolean prevent_tab_close(VteTerminal* terminal) {
    if (! tab_close_confirm) return FALSE;
    if (tab_close_confirm == OPTION_SMART && !is_running_foreground_process(terminal)) {
        return FALSE;
    }

    char message[1024], name[512];
    get_foreground_info(terminal, 0, name, NULL, NULL);
    snprintf(message, sizeof(message), "%s is still running.\nAre you sure you want to close it?", name);

    gint response = run_confirm_close_dialog(gtk_widget_get_toplevel(GTK_WIDGET(terminal)), message);
    return response != GTK_RESPONSE_YES;
}

void add_tab_to_window(GtkWidget* window, GtkWidget* widget, int position) {
    GtkWidget *terminal, *tab;
    if (GTK_IS_PANED(widget)) {
        tab = widget;
        terminal = split_get_active_term(tab);
    } else {
        terminal = g_object_get_data(G_OBJECT(widget), "terminal");

        tab = split_new_root();
        gtk_paned_pack1(GTK_PANED(tab), widget, TRUE, TRUE);
        split_set_active_term(VTE_TERMINAL(terminal));
    }

    GtkWidget* title_widget = g_object_get_data(G_OBJECT(tab), "tab_title");
    GtkNotebook* notebook = GTK_NOTEBOOK(window_get_notebook(window));
    int page = gtk_notebook_insert_page(notebook, tab, title_widget, position);
    configure_tab(GTK_CONTAINER(notebook), tab);

    gtk_widget_show_all(tab);
    if (title_widget) {
        gtk_widget_show_all(title_widget);
    }
    gtk_widget_realize(terminal);
    gtk_notebook_set_current_page(notebook, page);
    gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), tab, TRUE);

    update_tab_titles(VTE_TERMINAL(terminal));
    update_terminal_css_class(VTE_TERMINAL(terminal));

    // reallocate the sizes of the tab titles
    GdkRectangle rect;
    gtk_widget_get_allocation(GTK_WIDGET(notebook), &rect);
    notebook_size_allocate(notebook, &rect);
}

void add_terminal_full(GtkWidget* window, const char* cwd, int argc, char** argv) {
    GtkWidget* grid = make_terminal(cwd, argc, argv);
    add_tab_to_window(window, grid, -1);
}

GtkWidget* make_window() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    ADD_CSS_CLASS(window, APP_PREFIX_LOWER);

    // first window
    if (! toplevel_windows) {
        toplevel_windows = g_list_prepend(toplevel_windows, window);
    }

    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_can_focus(notebook, FALSE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(notebook), "terminals");

    GtkWidget* action_widget = gtk_button_new_with_label("+");
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(notebook), action_widget, GTK_PACK_END);
    ADD_CSS_CLASS(action_widget, "new-tab-button");
    g_signal_connect(action_widget, "focus-in-event", G_CALLBACK(notebook_focus_event), window);
    g_signal_connect(action_widget, "clicked", G_CALLBACK(new_tab), NULL);

    g_object_set_data(G_OBJECT(window), "notebook", notebook);
    g_signal_connect(window, "delete-event", G_CALLBACK(prevent_window_close), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(window_destroyed), NULL);
    g_signal_connect(window, "focus-in-event", G_CALLBACK(window_focus_event), NULL);

    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
    g_signal_connect(notebook, "focus-in-event", G_CALLBACK(notebook_focus_event), window);
    g_signal_connect(notebook, "size-allocate", G_CALLBACK(notebook_size_allocate), NULL);
    g_signal_connect(notebook, "page-removed", G_CALLBACK(notebook_tab_removed), NULL);
    g_signal_connect(notebook, "create-window", G_CALLBACK(notebook_create_window), NULL);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(notebook_switch_page), NULL);
    // make sure term titles update whenever they are reordered
    g_signal_connect(notebook, "page-reordered", G_CALLBACK(refresh_ui_notebook), NULL);
    g_signal_connect(notebook, "page-added", G_CALLBACK(refresh_ui_notebook), NULL);
    g_signal_connect(notebook, "page-removed", G_CALLBACK(refresh_ui_notebook), NULL);

    g_signal_connect(window, "key-press-event", G_CALLBACK(key_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(window), notebook);

    gtk_widget_show_all(window);
    configure_window(GTK_WINDOW(window));
    return window;
}

GtkWidget* make_new_window_full(GtkWidget* tab, const char* cwd, int argc, char** argv) {
    GtkWidget* window = make_window();
    if (! tab) {
        tab = make_terminal(cwd, argc, argv);
    }
    add_tab_to_window(window, tab, -1);
    return window;
}
