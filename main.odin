package main

import "core:fmt"
import "core:os"
import "core:sys/posix"
import "core:time"
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
	if len(os.args) < 2 {
		return
	}

	posix.signal(.SIGINT, sigint_handler)

	d, err := os.read_entire_file(os.args[len(os.args) - 1], context.allocator)
	defer delete(d)

	snd_ctx := snd.CreateContext(48000)
	defer snd.DestroyContext(snd_ctx)

	if err != nil {
		panic("couldn't read file")
	}

	snd.ModuleLoad(snd_ctx, d)

	rl.SetConfigFlags({.WINDOW_RESIZABLE})
	rl.InitWindow(1600, 900, "test")
	defer rl.CloseWindow()

	rl.SetTargetFPS(60)

	visualiser := vis.VisStateImpl {
		note_font = rl.LoadFontEx("font.ttf", 30, nil, 0),
	}
	defer vis.Reset(vis.VisState(&visualiser))

	events: [16]snd.TickEvent
	snd.PlaybackResume(snd_ctx)
	last_event_time := time.now()
	event_delta := time.Duration(0)
	for !rl.WindowShouldClose() && !snd.PlaybackIsPaused(snd_ctx) && !g_exit {
		got_events := snd.EventPop(snd_ctx, events[:])
		if got_events > 0 {
			now := time.now()
			event_delta = time.diff(last_event_time, now)
			last_event_time = now

			vis.Update(vis.VisState(&visualiser), events[:got_events])
		}

		rl.BeginDrawing()
		vis.Draw(vis.VisState(&visualiser))
		rl.DrawFPS(10, 10)
		rl.DrawText(
			rl.TextFormat("%v UPS", event_delta == 0 ? 0 : u32(time.Second / event_delta)),
			10,
			30,
			20,
			rl.BLACK,
		)
		rl.DrawText(
			rl.TextFormat(
				"%v",
				time.Second * time.Duration(snd.PlaybackGetClock(snd_ctx) / 48000),
			),
			10,
			50,
			20,
			rl.BLACK,
		)
		rl.EndDrawing()
	}
}
