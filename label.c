#include <gtk/gtk.h>

gboolean label_draw(GtkWidget* widget, cairo_t* cr) {
    GtkLabel* label = GTK_LABEL(widget);
    if (gtk_label_get_use_markup(label)) {
        PangoLayout* layout = gtk_label_get_layout(label);
        PangoAttrList* attrs = pango_layout_get_attributes(layout);
        PangoAttrIterator* iter = pango_attr_list_get_iterator(attrs);

        // find starting and ending background
        int end_index = strlen(pango_layout_get_text(layout));
        PangoColor *start = NULL, *end = NULL;
        do {
            PangoAttrColor* attr = (PangoAttrColor*)pango_attr_iterator_get(iter, PANGO_ATTR_BACKGROUND);
            if (attr && attr->attr.start_index == 0) {
                start = &(attr->color);
            }
            if (attr && attr->attr.end_index == end_index) {
                end = &(attr->color);
            }
        } while((!start || !end) && pango_attr_iterator_next(iter));

        GdkRectangle rect;
        gtk_widget_get_allocation(widget, &rect);

#define SCALE_UINT16(x) ((float)(x) / (float)G_MAXUINT16)
        if (start) {
            cairo_set_source_rgb(cr, SCALE_UINT16(start->red), SCALE_UINT16(start->green), SCALE_UINT16(start->blue));
            cairo_rectangle(cr, 0, 0, rect.width/2, rect.height);
            cairo_fill(cr);
        }

        if (end) {
            cairo_set_source_rgb(cr, SCALE_UINT16(end->red), SCALE_UINT16(end->green), SCALE_UINT16(end->blue));
            cairo_rectangle(cr, rect.width/2, 0, rect.width/2, rect.height);
            cairo_fill(cr);
        }
    }
    return FALSE;
}

GtkWidget* label_new(GtkWidget* label) {
    if (!label) label = gtk_label_new("");
    g_signal_connect(label, "draw", G_CALLBACK(label_draw), NULL);
    return label;
}

