# popup-term
Super simple VTE popup terminal

## Configuration

For `bool` values, `n`, `no`, `false`, `0`, ` ` (case insensitive) are considered false;
everything else is true.

## UI options

* `css = {string}`
* `show-tabs = {bool}`
* `tab-fill = {bool}`
* `tab-expand = {bool}`
* `tab-pos = top|bottom|left|right`
* `tab-enable-popup = {bool}`
    * enable the GtkNotebook popup
* `tab-scrollable = {bool}`
    * should the tab bar be scrollable
* `show-scrollbar = {bool}`
* `ui-refresh-interval = {int}`
    * interval in ms to refresh tab titles etc
* `inactivity-duration = {int}`
    * interval in ms after which a terminal is considered "silent"
* `window-title-format|tab-title-format = {string}`
    * %-style format string:
        * current working directory: `%d`
        * foreground process name: `%n`
        * terminal title: `%t`
        * tab number: `%N`
        * %: `%%`
* `tab-title-markup = {bool}`
    * parse Pango markup in the tab title
* `tab-title-alignment = left|center|right`
* `tab-title-ellipsize-mode = start|middle|end`
* `window-icon = {string}`
* `window-close-confirm = {bool}`
    * show a confirmation dialog first when closing windows
* `tab-close-confirm = {bool}|smart`
    * show a confirmation dialog first when closing tabs
    * for `smart`, the dialog is only shown if there is a foreground process
* `default-open-action = tab|window`
    * when launching new terminals, default to opening in new tab or new window

## Terminal options

* `background|foreground|col{0..15} = {string}`
    * anything handled by [gdk_rgba_parse](https://developer.gnome.org/gdk3/stable/gdk3-RGBA-Colors.html#gdk-rgba-parse)
* `encoding = {string}`
* `font = {string}`
* `font-scale = {float}`
* `audible-bell = {bool}`
* `allow-hyperlink = {bool}`
    * OSC 8 hyperlink support
* `pointer-autohide = {bool}`
    * autohide pointer when typing
* `rewrap-on-resize = {bool}`
* `scroll-on-keystroke = {bool}`
* `scroll-on-output = {bool}`
* `default-scrollback-lines = {int}`
* `word-char-exceptions = {string}`
    * https://developer.gnome.org/vte/unstable/VteTerminal.html#VteTerminal--word-char-exceptions
* `default-args = {string}`
    * default program to run in new tabs/windows
    * defaults to the user's shell or `/bin/sh`
* `cursor-blink-mode = system|off|on`
* `cursor-shape = block|ibeam|underline`

## Events

* `on-bell = {callback}`
* `on-hyperlink-hover = {callback}`
* `on-hyperlink-click = {callback}`
* `key-{keycombo} = {callback}`
* `{callback}`
    * one-off trigger of callback

`keycombo` are parsed by [gtk_accelerator_parse](https://developer.gnome.org/gtk3/stable/gtk3-Keyboard-Accelerators.html#gtk-accelerator-parse)
e.g. `<Control><Alt>Page_Down`

## Callbacks

* `paste_text [: {string}]`
    * if the argument is not given, text from clipboard is pasted
    * this is useful if you need to paste arbitrary text and need to respect bracketed paste
* `copy_text`
* `change_font_size : {float}`
* `reset_terminal`
* `scroll_up`
* `scroll_down`
* `scroll_page_up`
* `scroll_page_down`
* `scroll_top`
* `scroll_bottom`
* `select_all`
* `unselect_all`
* `feed_data : {string}`
    * feed user input to the terminal
* `feed_term : {string}`
    * feed data to terminal as if it came from the terminal (NOT the user)
* `new_tab|new_window [: {string}]`
    * if argument given, it is run in the new tab/window
    * if the first whitespace-delimited part matches `cwd=*`, the terminal is started in the given directory
* `prev_tab`
* `next_tab`
* `move_tab_prev`
* `move_tab_next`
* `detach_tab`
    * tab will move to a new window
* `cut_tab|paste_tab`
    * you can use this to move tabs between windows
* `switch_to_tab : {int}`
    * negative indices work from the end (e.g. -1 is the last tab)
* `tab_popup_menu`
    * trigger the tab bar popup menu
* `reload_config [: {string}]`
    * if argument given, this is the file to load config from
* `close_tab`
* `add_label_class|remove_label_class`
    * add/remove CSS class from the tab label
* `run|pipe_screen|pipe_line|pipe_all [: {string}]`
    * run a process with the following env vars set:
        * `VTE_TERMINAL_PID`: pid of the terminal process
        * `VTE_TERMINAL_FGPID`: pid of the foreground process
        * `VTE_TERMINAL_CURSORX`: column number of the cursor
        * `VTE_TERMINAL_CURSORY`: row number of the cursor
        * `VTE_TERMINAL_HYPERLINK`: hyperlink that the mouse is over (if any)
        * `VTE_TERMINAL_WINID`: X11 window id
    * `pipe_screen` pipes the visible text to stdin
    * `pipe_line` pipes the cursor's current line to stdin
    * `pipe_all` pipes all text including scrollback to stdin
    * anything from stdout is fed back to the terminal as user input
    * if no argument given, return the stdin instead
* `scrollback_lines [: {int}]`
    * change the scrollback for this terminal only
    * -1 means unlimited scrollback
    * with no argument, the scrollback is unchanged
    * returns the current scrollback
