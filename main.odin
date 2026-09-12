package main

import "core:os"
import "core:sys/posix"
import snd "sndlib"
import rl "vendor:raylib"
import vis "vislib"

g_exit: bool = false

sigint_handler :: proc "c" (sig: posix.Signal) {
	if (sig == posix.Signal.SIGINT) {
		g_exit = true
	}
}

main :: proc() {
	posix.signal(.SIGINT, sigint_handler)

	d, err := os.read_entire_file("testxms/river347_-_bunny_hop_back.xm", context.allocator)
	defer delete(d)

	snd_ctx := snd.CreateContext(48000)
	defer snd.DestroyContext(snd_ctx)

	if err != nil {
		panic("couldn't read file")
	}

	snd.ModuleLoad(snd_ctx, d)

	rl.InitWindow(1920, 1080, "test")
	defer rl.CloseWindow()

	visualiser := vis.VisStateImpl {
		note_font = rl.LoadFontEx("font.ttf", 30, nil, 0),
	}
	defer vis.Reset(vis.VisState(&visualiser))

	events: [16]snd.TickEvent
	snd.PlaybackResume(snd_ctx)
	for !rl.WindowShouldClose() && !snd.PlaybackIsPaused(snd_ctx) && !g_exit {
		got_events := snd.EventPop(snd_ctx, events[:])
		if got_events > 0 {
			vis.Update(vis.VisState(&visualiser), events[:got_events])
		}

		rl.BeginDrawing()
		vis.Draw(vis.VisState(&visualiser))
		rl.EndDrawing()
	}
}
