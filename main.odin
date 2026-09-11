package main

import "core:fmt"
import "core:math"
import "core:os"
import "core:sys/posix"
import "core:time"
import snd "sndlib"

g_exit: bool = false

sigint_handler :: proc "c" (sig: posix.Signal) {
	if (sig == posix.Signal.SIGINT) {
		g_exit = true
	}
}

main :: proc() {
	posix.signal(.SIGINT, sigint_handler)

	snd_ctx := snd.CreateContext(48000)
	defer snd.DestroyContext(snd_ctx)

	d, err := os.read_entire_file("testxms/river347_-_bunny_hop_back.xm", context.allocator)
	defer delete(d)
	if err != nil {
		panic("couldn't read file")
	}

	snd.ModuleLoad(snd_ctx, d)

	events: [16]snd.TickEvent
	snd.PlaybackResume(snd_ctx)
	for !snd.PlaybackIsPaused(snd_ctx) && !g_exit {
		got_events := snd.EventPop(snd_ctx, events[:])
	}

	fmt.println("length:", len(d))
}
