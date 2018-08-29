#ifndef TAB_TITLE_UI_H
#define TAB_TITLE_UI_H

#include <gtk/gtk.h>
#include <vte/vte.h>

#define TITLE_FORMAT_TITLE 1
#define TITLE_FORMAT_NAME 2
#define TITLE_FORMAT_CWD 3
#define TITLE_FORMAT_NUM 4

typedef struct {
    int flags;
    char* format;
} TitleFormat;

void update_tab_titles(VteTerminal* terminal);
gboolean set_tab_label_format(char* string, PangoEllipsizeMode ellipsize, float xalign);
gboolean set_tab_title_ui(char* string);
void destroy_all_tab_title_uis();
TitleFormat parse_title_format(char* string);
GtkWidget* make_tab_title_ui(GtkWidget* paned);

#endif
