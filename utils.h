#ifndef UTILS_H
#define UTILS_H

#include <glib.h>

#define STR_EQUAL(x, y) (strcmp((x), (y)) == 0)
#define STR_IEQUAL(x, y) (g_ascii_strcasecmp((x), (y)) == 0)

// y must be static
#define STR_STARTSWITH(x, y) (strncmp((x), (y), sizeof(y)-1) == 0)

#define STR_STRIP_PREFIX(x, y) (STR_STARTSWITH((x), (y)) ? (x)+sizeof(y)-1 : NULL)

#define ADD_CSS_CLASS(widget, class) gtk_style_context_add_class(gtk_widget_get_style_context(widget), class)
#define REMOVE_CSS_CLASS(widget, class) gtk_style_context_remove_class(gtk_widget_get_style_context(widget), class)

#define strto10l(x, y) strtol(x, y, 10)
#define PARSE_DELTA(string, sign_var, suffix_var, func) \
    ( sign_var = (string[0] == '+') ? 1 : (string[0] == '-') ? -1 : 0, func(string, suffix_var)*(sign_var ? sign_var : 1) )
#define PARSE_FLOAT_DELTA(string, sign_var, suffix_var) PARSE_DELTA(string, sign_var, suffix_var, strtof)
#define PARSE_INT_DELTA(string, sign_var, suffix_var) PARSE_DELTA(string, sign_var, suffix_var, strto10l)

#endif
