#include <gmodule.h>
#include "tab_title_ui.h"
#include "terminal.h"
#include "utils.h"
#include "label.h"
#include "split.h"
#include "config.h"
#include "window.h"

const char* DEFAULT_UI =
    "<interface>"
        "<object class='GtkLabel'>"
            "<property name='label'>%s</property>"
            "<property name='use-markup'>yes</property>"
            "<property name='ellipsize'>%s</property>"
            "<property name='xalign'>%f</property>"
            "<signal name='event' handler='format:escaped:label' />"
        "</object>"
    "</interface>"
;

char* tab_title_ui = NULL;

typedef struct {
    GtkWidget* widget;
    GtkWidget* root_split;
    char* property;
    TitleFormat format;
    gboolean escaped;
} FormatObject;
GArray* widget_formatters = NULL;

void set_tab_label_format(char* string, PangoEllipsizeMode ellipsize, float xalign) {
    GError* error = NULL;
    if (! pango_parse_markup(string, -1, 0, NULL, NULL, NULL, &error)) {
        g_warning("Invalid markup, %s: %s", error->message, string);
        g_error_free(error);
        return;
    }

    char* ellipsize_str;
    switch (ellipsize) {
        case PANGO_ELLIPSIZE_END:
            ellipsize_str = "end"; break;
        case PANGO_ELLIPSIZE_MIDDLE:
            ellipsize_str = "middle"; break;
        case PANGO_ELLIPSIZE_START:
            ellipsize_str = "start"; break;
        default:
            ellipsize_str = "none"; break;
    }

    set_tab_title_ui(g_markup_printf_escaped(DEFAULT_UI, string, ellipsize_str, xalign));
}

void set_tab_title_ui(char* string) {
    if (tab_title_ui) {
        free(tab_title_ui);
        tab_title_ui = NULL;
    }

    tab_title_ui = strdup(string);
}

void destroy_all_tab_title_uis() {
    FOREACH_WINDOW(window) {
        GtkWidget* notebook = window_get_notebook(GTK_WIDGET(window));
        FOREACH_TAB(tab, window) {
            GtkWidget* ui = g_object_get_data(G_OBJECT(tab), "tab_title");
            if (ui) {
                gtk_notebook_set_tab_label(GTK_NOTEBOOK(notebook), tab, NULL);
                gtk_widget_destroy(ui);
                g_object_unref(ui);
                g_object_set_data(G_OBJECT(tab), "tab_title", NULL);
            }
        }
    }
}

TitleFormat parse_title_format(char* string) {
    char* pieces[256] = {0};
    int flags = 0;

    int i = 0;
    while (1) {
        char* end = strchr(string, '%');
        pieces[i] = string;
        i ++;

        if (! end) break;

        *end = '\0';
        switch (*(end+1)) {
            case 't': // title
                pieces[i] = "%1$s";
                flags |= TITLE_FORMAT_TITLE;
                break;
            case 'n': // process name
                pieces[i] = "%2$s";
                flags |= TITLE_FORMAT_NAME;
                break;
            case 'd': // cwd
                pieces[i] = "%3$s";
                flags |= TITLE_FORMAT_CWD;
                break;
            case 'N': // tab numer
                pieces[i] = "%4$i";
                flags |= TITLE_FORMAT_NUM;
                break;
            case '\0':
                end --; // back out one so we don't go past end of array
            case '%':
                pieces[i] = "%%";
                break;
            default:
                pieces[i] = "%%";
                end --; // back out one to include the extra char next round
                break;
        }
        // skip the %X
        string = end + 2;
        i ++;
    }

    TitleFormat fmt = {flags, g_strjoinv(NULL, pieces)};
    return fmt;
}

void update_tab_titles(VteTerminal* terminal) {
    if (! widget_formatters) return;

    char buffer[1024] = "";

    for (int i = 0; i < widget_formatters->len; i ++) {
        FormatObject* fo = &g_array_index(widget_formatters, FormatObject, i);
        VteTerminal* current_term = VTE_TERMINAL(split_get_active_term(fo->root_split));

        if (!current_term || (terminal && terminal != current_term)) {
            continue;
        }

        if (term_construct_title(fo->format.format, fo->format.flags, current_term, fo->escaped, buffer, sizeof(buffer)-1)) {
            char* old;
            g_object_get(G_OBJECT(fo->widget), fo->property, &old, NULL);
            if (! STR_EQUAL(old, buffer)) {
                g_object_set(G_OBJECT(fo->widget), fo->property, buffer, NULL);
            }
        }
    }
}

void unregister_widget(GtkWidget* widget) {
    // start from end as we are modifying while iterating
    for (int i = widget_formatters->len - 1; i >= 0; i --) {
        FormatObject* fo = &g_array_index(widget_formatters, FormatObject, i);
        if (fo->widget == widget) {
            free(fo->property);
            free(fo->format.format);
            g_array_remove_index_fast(widget_formatters, i);
        }
    }
}

void register_widget_parsed(GtkWidget* widget, GtkWidget* root_split, const char* prop, TitleFormat format, gboolean escaped) {
    if (! widget_formatters) {
        widget_formatters = g_array_new(FALSE, FALSE, sizeof(FormatObject));
    }

    FormatObject fo = {widget, root_split, strdup(prop), format, escaped};
    g_array_append_val(widget_formatters, fo);
    g_signal_connect(widget, "destroy", G_CALLBACK(unregister_widget), NULL);
}

void register_widget(GtkWidget* widget, GtkWidget* root_split, const char* prop, const char* format, gboolean escaped) {
    char* fmt = strdup(format);
    register_widget_parsed(widget, root_split, prop, parse_title_format(fmt), escaped);
    free(fmt);
}

void builder_widget_connector(
        GtkBuilder* builder,
        GObject* object,
        const char* signal,
        const char* handler,
        void* connect_object,
        GConnectFlags flags,
        GtkWidget* paned
) {
    const char* prop;

    if (GTK_IS_WIDGET(object) && (prop = STR_STRIP_PREFIX(handler, "format:"))) {
        gboolean escaped = FALSE;

        const char* end;
        if ((end = STR_STRIP_PREFIX(prop, "escaped:"))) {
            prop = end;
            escaped = TRUE;
        }

        char* format;
        g_object_get(object, prop, &format, NULL);
        register_widget(GTK_WIDGET(object), paned, prop, format, escaped);
        g_object_set(object, prop, "", NULL);

    } else {
        // signals not really supported ; maybe another day
        return;

        // default signal connection
        /*
        GCallback func = gtk_builder_lookup_callback_symbol(builder, handler);

        GModule* module = g_module_open(NULL, G_MODULE_BIND_LAZY);
        if (!func && (!g_module_supported() || !g_module_symbol(module, handler, (void*)&func))) {
            g_warning("Could not find signal handler %s", handler);
            return;
        }

        if (connect_object) {
            g_signal_connect_object(object, signal, func, connect_object, flags);
        } else {
            g_signal_connect_data (object, signal, func, NULL, NULL, flags);
        }
        */
    }
}

GtkWidget* make_tab_title_ui(GtkWidget* paned) {
    GtkBuilder* builder = gtk_builder_new();
    GError* error = NULL;

    if (! gtk_builder_add_from_string(builder, tab_title_ui, -1, &error)) {
        g_warning("Invalid UI definition: %s", error->message);
        g_object_unref(builder);
        return NULL;
    }

    GSList* list = gtk_builder_get_objects(builder);
    GtkWidget* widget = NULL;
    for ( ; list; list = list->next) {
        GtkWidget* w = GTK_WIDGET(list->data);

        // find first widget with no parent ; any other top levels will get dropped
        if (!widget && gtk_widget_get_parent(w) == NULL) {
            widget = w;
        }
    }
    g_slist_free(list);

    if (! widget) {
        g_warning("No widgets found in UI definition");
        g_object_unref(builder);
        return NULL;
    }

    gtk_builder_connect_signals_full(builder, (GtkBuilderConnectFunc)builder_widget_connector, paned);
    g_object_ref(widget); // ref or builder will destroy it
    g_object_unref(builder);

    g_object_set_data(G_OBJECT(paned), "tab_title", widget);

    GtkWidget* notebook = gtk_widget_get_parent(paned);
    if (notebook) {
        gtk_notebook_set_tab_label(GTK_NOTEBOOK(notebook), paned, widget);
        GtkWidget* terminal = split_get_active_term(paned);
        if (terminal) {
            update_tab_titles(VTE_TERMINAL(terminal));
        }
        gtk_widget_show_all(widget);
    }

    return widget;
}
