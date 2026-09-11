CC      ?= clang
CFLAGS  ?= -g -fsanitize=address -O3 -Wall -Wextra $(shell pkg-config --cflags libxmp sdl3)
LDLIBS  := $(shell pkg-config --libs libxmp sdl3)

export LSAN_OPTIONS := suppressions=$(CURDIR)/lsan.supp

sndlib: snd_xmp.c snd_ring_buffer.c
	$(CC) $(CFLAGS) -o player snd_xmp.c snd_ring_buffer.c $(LDLIBS)

# test: test.c snd.c snd.h
# 	$(CC) $(CFLAGS) -o test test.c snd.c $(LDLIBS)

clean:
	# rm -f player test
	rm player

.PHONY: clean
