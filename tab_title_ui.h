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
void set_tab_title_ui(char* string);
void parse_title_format(char* string, TitleFormat* dest);
GtkWidget* make_tab_title_ui(GtkWidget* paned);
GtkWidget* make_tab_title_label(GtkWidget* paned);

#endif
