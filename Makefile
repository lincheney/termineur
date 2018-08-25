.SUFFIXES:

CC=gcc
DEPS=gtk+-3.0 vte-2.91 gdk-3.0
CFLAGS:=-O3 $(shell pkg-config --cflags $(DEPS)) -Wall
LIBS:=$(shell pkg-config --libs $(DEPS))
SOURCES:=$(shell find -name '*.c')
TARGET=popup-term

$(TARGET): $(SOURCES:%.c=%.o)
	@echo '>>> Compiling $@'
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)
	@echo

$(foreach file,$(SOURCES),$(eval $(shell $(CC) -MM $(file) | tr -d '\n\\' )))

%.o:
	@echo '>>> Compiling $@'
	$(CC) -c -o $@ $< $(CFLAGS) $(LIBS)
	@echo

.PHONY: clean

clean:
	rm -f *.o *~ popup-term
