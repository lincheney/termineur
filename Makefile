CC=gcc
DEPS=gtk+-3.0 vte-2.91 gdk-3.0
CFLAGS=-O3 $(shell pkg-config --cflags $(DEPS)) -Wall
LIBS=$(shell pkg-config --libs $(DEPS))
HEADERS=config.h window.h terminal.h socket.h split.h callback.h

%.o: %.c $(HEADERS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

popup-term: main.o config.o window.o terminal.o socket.o split.o callback.o
	gcc -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f *.o *~ popup-term
