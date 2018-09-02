# termineur

[VTE](https://developer.gnome.org/vte/unstable/) based terminal driven by a UNIX socket API.

Features (or lack thereof):
* anything that VTE supports (24-bit colour, italics, scrollback, line wrap etc)
* tabs and splits
* UNIX socket API from which you can drive 90% of all terminal features.
   (You could even drive terminal input, but that could be getting silly).
* [lots of configuration options](example.ini), which can also all be set with the API
* no default keybindings.
* bare bones GUI.
   There is a terminal, an optional tab bar and an optional scrollbar.
   No GUI preferences dialog, no right-click menus etc.

## Commands

There are 3 types of commands:
* simple `key = value` settings
* actions, e.g. `new_tab: vim`
* event bindings `on-event = action`, e.g. `on-key-<control><shift>v = new_tab: vim`

These can be specified in the configuration file when `termineur` starts up
or over the UNIX socket API while it is running.

See the [example configuration](example.ini) for a list of all current configuration options.

## Interacting over the UNIX socket

`termineur` listens on an abstract UNIX socket and accepts newline-delimited commands
in the same syntax as the configuration file (almost; multiline is not supported).
You can trigger actions, or change any settings/event bindings.

The socket name can be specified with the `--id` flag
or retrieved from the `$TERMINEUR_ID` environment variable inside the terminal.

You can use `termineur` itself as the socket client, or anything else that supports
UNIX sockets (e.g. `socat`).

Both the following commands are equivalent:
```bash
termineur -c new_tab -c new_window
(echo new_tab; echo new_window) | socat - ABSTRACT-CONNECT:$TERMINEUR_ID
```

Any output is returned back over the socket appended with a null byte
(sometimes the output may have newlines).
```bash
$ # query a setting
$ termineur -c scrollback-lines
1000
$ # pipe terminal text to less
$ echo pipe_all | socat - ABSTRACT-CONNECT:$TERMINEUR_ID | less
```

### Opening a terminal connection

You can open a new terminal and connect up stdin/stdout over the socket.

Here's a quick example to hook up [fzf](https://github.com/junegunn/fzf) running in a new terminal:
```bash
$ file="$(find | termineur --connect new_tab:fzf)"
$ # OR with socat
$ file="$( (echo 'CONNECT_SOCK:3:new_tab:fzf'; find) | socat -t9999999 - ABSTRACT-CONNECT:$TERMINEUR_ID)"
```

The client should send one line with `CONNECT_SOCK:flags:action` (plus a newline),
where flags is `3` (connect stdin+stdout), `2` (connect stdout only),
`1` (connect stdin only) or `0` (don't connect any file descriptors)
and action is any action that would open a terminal,
i.e. `new_tab`, `new_window`, `split_left`, `split_right`, `split_above`, `split_below`.

In the above example, both stdin and stdout are connected to `fzf` running in a `new_tab`.
Some programs expect stdin/stdout to be a tty, so you should disable those connections:
```bash
$ cat file | termineur --no-connect-stdout --connect new_tab:less
$ (echo 'CONNECT_SOCK:1:new_tab:less'; cat file) | socat -t9999999 - ABSTRACT-CONNECT:$TERMINEUR_ID)"
```

In the case that no file descriptors are connected (`0`),
the socket connection will merely hang until the terminal is closed.

You can run any commands before `CONNECT_SOCK:` but none after (since anything afterwards ends up piped to stdin).

## CSS

Widgets styled using [GTK+ CSS](https://developer.gnome.org/gtk3/stable/chap-css-overview.html).
You can do this in the standard CSS files (e.g. `~/.config/gtk-3.0/gtk.css`) or with the `css` setting.

Nodes are as follows:
```
window.termineur
└── notebook
    ├── header.top
    │   └── tabs
    │       ├── tab
    │       │   └── label[.active][.inactive][.selected][.no-scrollback]
    │       ├── tab
    │       │   └── label[.active][.inactive][.selected][.no-scrollback]
    │       ·
    │       ·
    │       ·
    │       └── button.new-tab-button
    │
    └── stack
        ├── paned.split-root
        │   ├── [paned]*
        │   │   ├── grid[.active][.inactive][.selected][.no-scrollback]
        │   │   │   ├── searchbar[.not-found]
        ·   ·   ·   │   └── grid
        ·   ·   ·   │       ├── entry
        ·   ·   ·   │       ├── button
        ·   ·   ·   │       └── button
                    ├── #messagebar
                    ├── overlay
                    │   ├── vte-terminal
                    │   └── [scrollbar]
                    └── [scrollbar]
```

The position of scrollbar depends on the `show-scrollbar` setting.

Tab title widgets may not always be `label` and depends on the `tab-title-ui` setting.

The `.active` class is applied when there is terminal activity.
The `.inactive` class is applied when there *was* terminal activity but not since the last `inactivity-duration` milliseconds.
The `.selected` class is applied to the active terminal of a tab, even if it is not focused.
The `.no-scrollback` class is applied when the terminal *currently* has no scrollback history,
i.e. `scrollback-lines` may be set, but there is no scrollback history yet,
or it has been cleared, or the terminal is on the alternate screen.

## Tips and tricks

#### Save output dialog

```
on-key-... = pipe_all: sh -c 'file="$(zenity --file-selection)" && cat >"$file"'
```

#### Open terminal buffer in less

```
on-key-... = pipe_all_ansi: termineur --no-connect-stdout --connect 'new_tab:less -R'
```

#### Select a URL

```
on-key-... = pipe_screen: sh -c "egrep -o '[[:alpha:]]+://[^[:space:]]*' | termineur --connect split_above:fzf"
```

#### Increase scrollback limit

```
on-key-... = run: sh -c 'termineur -c "scrollback-lines = $(( $(termineur -c scrollback-lines) + 1000 ))" '
```

#### Background image

Make the terminal background transparent:
```
background = rgba(0x11, 0x11, 0x11, 0.8)
```

Then apply the following CSS:
```css
.termineur .split-root {
    background: url("/path/to/image") center / cover;
}
```

#### Dim inactive terminals

Apply the following CSS:
```css
overlay {
    background: rgba(64, 64, 64, 0.3);
}
.selected overlay {
    background: none;
}
```
