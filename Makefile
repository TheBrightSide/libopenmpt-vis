CC      ?= clang
CFLAGS  ?= -fsanitize=address -O2 -Wall -Wextra $(shell pkg-config --cflags libopenmpt sdl3)
LDLIBS  := $(shell pkg-config --libs libopenmpt sdl3)

player: main.c
	$(CC) $(CFLAGS) -o player main.c snd.c $(LDLIBS)

# test: test.c snd.c snd.h
# 	$(CC) $(CFLAGS) -o test test.c snd.c $(LDLIBS)

clean:
	# rm -f player test
	rm player

.PHONY: clean
