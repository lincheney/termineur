#include "search_bar.h"
#include "terminal.h"

void search_bar_size_allocate(GtkWidget* bar, GdkRectangle* alloc, GtkWidget* grid) {
    GdkRectangle rect;
    gtk_widget_get_allocation(grid, &rect);
    rect.x = 0;
    rect.width = alloc->width;
    gtk_widget_size_allocate(grid, &rect);
}

void search_match(GtkWidget* entry, int direction) {
    VteTerminal* terminal = g_object_get_data(G_OBJECT(entry), "terminal");
    term_search(terminal, gtk_entry_get_text(GTK_ENTRY(entry)), direction);
}

gboolean search_key_pressed(GtkWidget* entry, GdkEventKey* event) {
    if (event->keyval == GDK_KEY_Return) {
        guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
        if (modifiers & GDK_SHIFT_MASK) {
            search_match(entry, 1);
        } else {
            search_match(entry, -1);
        }
        return TRUE;
    }
    return FALSE;
}

void search_up_clicked(GtkWidget* button, GtkWidget* entry) {
    search_match(entry, -1);
}

void search_down_clicked(GtkWidget* button, GtkWidget* entry) {
    search_match(entry, 1);
}

gboolean focus_widget(GtkWidget* widget) {
    if (GTK_IS_ENTRY(widget)) {
        gtk_entry_grab_focus_without_selecting(GTK_ENTRY(widget));
    } else {
        gtk_widget_grab_focus(widget);
    }
    return FALSE;
}

GtkWidget* search_bar_new(VteTerminal* terminal) {
    GtkWidget* entry = gtk_search_entry_new();
    g_signal_connect(entry, "key-press-event", G_CALLBACK(search_key_pressed), NULL);
    g_signal_connect(entry, "next-match", G_CALLBACK(search_match), GINT_TO_POINTER(-1));
    g_signal_connect(entry, "previous-match", G_CALLBACK(search_match), GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(entry), "terminal", terminal);
    g_signal_connect_swapped(entry, "stop-search", G_CALLBACK(focus_widget), terminal);

    GtkWidget* down = gtk_button_new_from_icon_name("go-down", GTK_ICON_SIZE_LARGE_TOOLBAR);
    GtkWidget* up = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_LARGE_TOOLBAR);
    g_signal_connect_swapped(down, "focus-in-event", G_CALLBACK(focus_widget), entry);
    g_signal_connect_swapped(up, "focus-in-event", G_CALLBACK(focus_widget), entry);
    g_signal_connect(up, "clicked", G_CALLBACK(search_up_clicked), entry);
    g_signal_connect(down, "clicked", G_CALLBACK(search_down_clicked), entry);

    GtkWidget* grid = gtk_grid_new();
    gtk_widget_set_halign(grid, GTK_ALIGN_START);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_container_add(GTK_CONTAINER(grid), entry);
    gtk_container_add(GTK_CONTAINER(grid), up);
    gtk_container_add(GTK_CONTAINER(grid), down);

    GtkWidget* bar = gtk_search_bar_new();
    gtk_container_add(GTK_CONTAINER(bar), grid);
    gtk_search_bar_connect_entry(GTK_SEARCH_BAR(bar), GTK_ENTRY(entry));
    g_signal_connect(bar, "size-allocate", G_CALLBACK(search_bar_size_allocate), grid);
    g_object_set(bar, "search-mode-enabled", FALSE, NULL);
    g_object_set_data(G_OBJECT(bar), "entry", entry);
    return bar;
}
