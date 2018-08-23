#include "split.h"
#include "terminal.h"

GtkWidget* split_new() {
    GtkWidget* paned =  gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    g_object_set_data(G_OBJECT(paned), TERMINAL_FOCUS_KEY, NULL);
    return paned;
}

GtkWidget* split_get_root(GtkWidget* paned) {
    // get last parent that is not a split
    while (1) {
        GtkWidget* parent = gtk_widget_get_parent(paned);
        if (! parent || ! GTK_IS_PANED(parent) ) return paned;
        paned = parent;
    }
}

GtkWidget* split_get_container(GtkWidget* widget) {
    return gtk_widget_get_parent(split_get_root(widget));
}

void split(GtkWidget* dest, GtkWidget* src, GtkOrientation orientation, gboolean after) {
    /*
     * swap dest for a new split
     * then add dest and src to the new split
     */

    GtkPaned* dest_split = GTK_PANED(gtk_widget_get_parent(dest));
    GtkWidget* child1 = gtk_paned_get_child1(dest_split);

    GtkPaned* new_split = GTK_PANED(gtk_paned_new(orientation));
    // swap dest with new split
    g_object_ref(dest);
    gtk_container_remove(GTK_CONTAINER(dest_split), dest);
    (child1 == dest ? gtk_paned_add1 : gtk_paned_add2)(dest_split, GTK_WIDGET(new_split));

    gtk_paned_pack1(new_split, after ? dest : src, TRUE, TRUE);
    gtk_paned_pack2(new_split, after ? src : dest, TRUE, TRUE);
    g_object_unref(dest);

    gtk_widget_show_all(GTK_WIDGET(new_split));
}

void split_cleanup(GtkWidget* paned) {
    // cleanup split tree structure

    GtkWidget* child1 = gtk_paned_get_child1(GTK_PANED(paned));
    GtkWidget* child2 = gtk_paned_get_child2(GTK_PANED(paned));
    GtkWidget* parent = gtk_widget_get_parent(paned);

    if (! child1 && ! child2) {
        // destroy empty split and cleanup parent
        gtk_widget_destroy(paned);
        if (GTK_IS_PANED(parent)) {
            split_cleanup(parent);
        }
        return;
    }

    if (! child1 || ! child2) {
        // exactly one child
        GtkWidget* widget = child1 ? child1 : child2;
        GtkWidget* current = paned;
        g_object_ref(widget);
        gtk_container_remove(GTK_CONTAINER(paned), widget);

        while (! child1 || ! child2) {
            parent = gtk_widget_get_parent(current);
            if (! GTK_IS_PANED(parent)) {
                // root split
                parent = current;
                break;
            }
            child1 = gtk_paned_get_child1(GTK_PANED(parent));
            child2 = gtk_paned_get_child2(GTK_PANED(parent));
            gtk_container_remove(GTK_CONTAINER(parent), current);
            current = parent;
        }

        (child2 ? gtk_paned_pack1 : gtk_paned_pack2)(GTK_PANED(parent), widget, TRUE, TRUE);
        g_object_unref(widget);
        return;
    }

    // 2 children
}

GtkWidget* split_get_active_term(GtkWidget* paned) {
    GSList* node = g_object_get_data(G_OBJECT(paned), TERMINAL_FOCUS_KEY);
    return node ? GTK_WIDGET(node->data) : NULL;
}

void split_remove_term_from_chain(VteTerminal* terminal) {
    GtkWidget* paned = term_get_tab(terminal);
    GSList* list = g_object_get_data(G_OBJECT(paned), TERMINAL_FOCUS_KEY);
    list = g_slist_remove(list, terminal);
    g_object_set_data(G_OBJECT(paned), TERMINAL_FOCUS_KEY, list);
}

void split_set_active_term(VteTerminal* terminal) {
    GtkWidget* paned = term_get_tab(terminal);
    GSList* list = g_object_get_data(G_OBJECT(paned), TERMINAL_FOCUS_KEY);
    list = g_slist_remove(list, terminal);
    list = g_slist_prepend(list, terminal);
    g_object_set_data(G_OBJECT(paned), TERMINAL_FOCUS_KEY, list);
}
