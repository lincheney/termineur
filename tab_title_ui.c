#include <gmodule.h>
#include "tab_title_ui.h"
#include "terminal.h"
#include "utils.h"
#include "label.h"
#include "split.h"

char* tab_title_ui = NULL;

typedef struct {
    GtkWidget* widget;
    GtkWidget* root_split;
    char* property;
    TitleFormat format;
    gboolean escaped;
} FormatObject;
GArray* widget_formatters = NULL;

void set_tab_title_ui(char* string) {
    free(tab_title_ui);
    tab_title_ui = strdup(string);
}

void parse_title_format(char* string, TitleFormat* dest) {
    free(dest->format);

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

    dest->format = g_strjoinv(NULL, pieces);
    dest->flags = flags;
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
    for (int i = widget_formatters->len; i >= 0; i --) {
        FormatObject* fo = &g_array_index(widget_formatters, FormatObject, i);
        if (fo->widget == widget) {
            free(fo->property);
            free(fo->format.format);
            g_array_remove_index_fast(widget_formatters, i);
        }
    }
}

void register_widget(GtkWidget* widget, GtkWidget* root_split, const char* prop, const char* format, gboolean escaped) {
    if (! widget_formatters) {
        widget_formatters = g_array_new(FALSE, FALSE, sizeof(FormatObject));
    }

    char* fmt = strdup(format);
    TitleFormat tf = {0, NULL};
    parse_title_format(fmt, &tf);
    free(fmt);

    FormatObject fo = {widget, root_split, strdup(prop), tf, escaped};
    g_array_append_val(widget_formatters, fo);
    g_signal_connect(widget, "destroy", G_CALLBACK(unregister_widget), NULL);
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
    // find first widget with no parent ; any other top levels will get dropped
    for ( ; list; list = list->next) {
        GtkWidget* w = GTK_WIDGET(list->data);
        if (gtk_widget_get_parent(w) == NULL) {
            widget = w;
            break;
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
    return widget;
}

GtkWidget* make_tab_title_label(GtkWidget* paned) {
    GtkWidget* widget = label_new(NULL);
    // markup is always enabled
    g_object_set(G_OBJECT(widget), "use-markup", TRUE, NULL);
    register_widget(widget, paned, "widget", "", TRUE);
    g_object_ref(widget);
    return widget;
}
