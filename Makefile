CC      ?= clang
CFLAGS  ?= -O2 -Wall -Wextra $(shell pkg-config --cflags libopenmpt sdl3)
LDLIBS  := $(shell pkg-config --libs libopenmpt sdl3)

player: main.c
	$(CC) $(CFLAGS) -o player main.c $(LDLIBS)

clean:
	rm -f player

.PHONY: clean
