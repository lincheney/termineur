CC=gcc
DEPS=gtk+-3.0 vte-2.91 gdk-3.0
CFLAGS=-O3 $(shell pkg-config --cflags $(DEPS))
LIBS=$(shell pkg-config --libs $(DEPS))

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)

popup-term: main.o
	gcc -o $@ $^ $(CFLAGS) $(LIBS)

.PHONY: clean

clean:
	rm -f *.o *~ popup-term
