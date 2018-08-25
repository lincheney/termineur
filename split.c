#include "split.h"
#include "terminal.h"

#define RESIZE TRUE
#define SHRINK FALSE

void gtk_paned_get_children(GtkPaned* paned, GtkWidget** child1, GtkWidget** child2) {
    *child1 = gtk_paned_get_child1(paned);
    *child2 = gtk_paned_get_child2(paned);
}

GtkWidget* split_new_root() {
    GtkWidget* paned =  gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE);
    g_object_set_data(G_OBJECT(paned), TERMINAL_FOCUS_KEY, NULL);

    GtkStyleContext* context = gtk_widget_get_style_context(paned);
    gtk_style_context_add_class(context, ROOT_SPLIT_CLASS);

    // label
    GtkWidget* label = gtk_label_new("");
    g_object_set_data(G_OBJECT(paned), "label", label);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
    g_object_ref(label);

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

GtkWidget* split(GtkWidget* dest, GtkWidget* src, GtkOrientation orientation, gboolean after) {
    GtkPaned* dest_split = GTK_PANED(gtk_widget_get_parent(dest));
    GtkWidget *child1, *child2;
    gtk_paned_get_children(dest_split, &child1, &child2);

    g_object_ref(dest);
    gtk_container_remove(GTK_CONTAINER(dest_split), dest);

    GtkPaned* new_split;
    if ((child1 == NULL) ^ (child2 == NULL)) {
        // single child, so just reuse this split
        new_split = dest_split;
        gtk_orientable_set_orientation(GTK_ORIENTABLE(dest_split), orientation);
    } else {
        // make a new split and swap it into the old split
        new_split = GTK_PANED(gtk_paned_new(orientation));
        gtk_paned_set_wide_handle(GTK_PANED(new_split), TRUE);
        (child1 == dest ? gtk_paned_pack1 : gtk_paned_pack2)(dest_split, GTK_WIDGET(new_split), RESIZE, SHRINK);
    }

    gtk_paned_pack1(new_split, after ? dest : src, RESIZE, SHRINK);
    gtk_paned_pack2(new_split, after ? src : dest, RESIZE, SHRINK);
    g_object_unref(dest);

    gtk_widget_show_all(GTK_WIDGET(new_split));
    gtk_widget_show_all(GTK_WIDGET(dest));
    gtk_widget_show_all(GTK_WIDGET(src));
    return GTK_WIDGET(new_split);
}

void split_cleanup(GtkWidget* paned) {
    GtkWidget *child1, *child2;
    gtk_paned_get_children(GTK_PANED(paned), &child1, &child2);

    if (!child1 && !child2) {
        // no children, this can only be the root, so destroy everything
        GtkWidget* label = g_object_get_data(G_OBJECT(paned), "label");
        g_object_unref(label);
        gtk_widget_destroy(paned);
        return;
    }

    if (!child1 || !child2) {
        // exactly one child
        GtkWidget* parent = gtk_widget_get_parent(paned);
        if (GTK_IS_PANED(parent)) {
            GtkWidget* widget = child1 ? child1 : child2;
            g_object_ref(widget);
            gtk_paned_get_children(GTK_PANED(parent), &child1, &child2);
            // remove the middle pane
            gtk_container_remove(GTK_CONTAINER(paned), widget);
            gtk_container_remove(GTK_CONTAINER(parent), paned);
            (child1 == paned ? gtk_paned_pack1 : gtk_paned_pack2)(GTK_PANED(parent), widget, RESIZE, SHRINK);
            g_object_unref(widget);
        }
        return;
    }

    // 2 children
}

gboolean split_move(GtkWidget* widget, GtkOrientation orientation, gboolean forward) {
    GtkWidget* paned = gtk_widget_get_parent(widget);
    GtkWidget *child1, *child2;
    gtk_paned_get_children(GTK_PANED(paned), &child1, &child2);

    if (!child1 || !child2) {
        // one child , must be only widget
        return FALSE;
    }

    GtkWidget* current = widget;
    GtkWidget* parent = paned;

    // find a parent in the wrong position
    while (1) {
        if (gtk_orientable_get_orientation(GTK_ORIENTABLE(parent)) != orientation) {
            // wrong orientation
            break;
        }

        if (current != (forward ? child2 : child1)) {
            // wrong position
            current = (current == child1 ? child2 : child1);
            break;
        }

        // ascend
        current = parent;
        parent = gtk_widget_get_parent(parent);
        if (! GTK_IS_PANED(parent)) {
            // hit root, can't move
            return FALSE;
        }
        gtk_paned_get_children(GTK_PANED(parent), &child1, &child2);
    }

    if (current == widget) {
        current = (widget == child1 ? child2 : child1);
    }

    // descend in opposite direction
    // find child closest to the widget
    while (GTK_IS_PANED(current)) {
        parent = current;
        current = (forward ? gtk_paned_get_child1 : gtk_paned_get_child2)(GTK_PANED(parent));
        if (gtk_orientable_get_orientation(GTK_ORIENTABLE(parent)) != orientation) {
            // wrong orientation, merge it in here instead
            forward = !forward;
            break;
        }
    }

    g_object_ref(widget);
    gtk_container_remove(GTK_CONTAINER(paned), widget);
    split_cleanup(paned);
    split(current, widget, orientation, forward);
    g_object_unref(widget);
    return TRUE;
}

gboolean split_move_focus(GtkWidget* widget, GtkOrientation orientation, gboolean forward) {
    /*
     * instead of navigating the split tree,
     * loop through the terminals and find the one
     * with the closest coords in the given
     * direction
     */

    GtkWidget* root = split_get_root(widget);
    GtkWidget* closest = NULL;
    GdkRectangle base, other;
    int best_dist = -1, other_dist;

    gtk_widget_get_allocation(widget, &base);

    for (GSList* node = g_object_get_data(G_OBJECT(root), TERMINAL_FOCUS_KEY); node; node = node->next) {
        GtkWidget* other_widget = term_get_grid(VTE_TERMINAL(node->data));
        if (widget == other_widget) {
            continue;
        }

        gtk_widget_get_allocation(other_widget, &other);
        gtk_widget_translate_coordinates(other_widget, widget, other.x, other.y, &other.x, &other.y);

        if (orientation == GTK_ORIENTATION_VERTICAL) {
            other_dist = (other.y - base.y + (forward ? -base.height : other.height));
        } else {
            other_dist = (other.x - base.x + (forward ? -base.width : other.width));
        }
        other_dist *= forward ? 1 : -1;

        if (0 <= other_dist && (other_dist < best_dist || !closest)) {
            closest = node->data;
            best_dist = other_dist;
        }
    }

    if (closest) {
        gtk_widget_grab_focus(closest);
        return TRUE;
    }
    return FALSE;
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

int split_get_separator_size(GtkWidget* paned) {
    int size = 0;
    gtk_widget_style_get(paned, "handle-size", &size, NULL);
    return size;
}
