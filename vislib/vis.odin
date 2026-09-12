package vislib

import snd "../sndlib"
import "core:fmt"
import "core:math"
import "core:strings"
import rl "vendor:raylib"

VisState :: distinct rawptr

ChannelNote :: struct {
	note:  f32,
	alpha: f32,
}

VisStateImpl :: struct {
	channel_notes: []ChannelNote,
	note_font:     rl.Font,
}

Reset :: proc(state: VisState) {
	if state == nil {return}

	state := (^VisStateImpl)(state)

	if state.channel_notes != nil {
		delete(state.channel_notes)
		state.channel_notes = nil
	}

	if rl.IsFontValid(state.note_font) {
		rl.UnloadFont(state.note_font)
	}
}

Update :: proc(state: VisState, events: []snd.TickEvent) {
	if state == nil {return}

	// TODO: when TickEvent marks the start of a new note, all events must be processed
	last_event := events[len(events) - 1]
	state := (^VisStateImpl)(state)

	if len(state.channel_notes) != int(last_event.channel_count) {
		state.channel_notes = make([]ChannelNote, last_event.channel_count)
	}

	for ch, ch_idx in last_event.channels[:last_event.channel_count] {
		state.channel_notes[ch_idx].note = f32(ch.note) + f32(ch.pitchbend) / 100.
		state.channel_notes[ch_idx].alpha = math.log2((f32(ch.volume) / 64.) + 1)
	}
}

Draw :: proc(state: VisState) {
	@(static) note_fmts := []cstring {
		"C-%d",
		"C#%d",
		"D-%d",
		"D#%d",
		"E-%d",
		"F-%d",
		"F#%d",
		"G-%d",
		"G#%d",
		"A-%d",
		"A#%d",
		"B-%d",
	}

	if state == nil {return}

	state := (^VisStateImpl)(state)

	rl.ClearBackground(rl.BLACK)
	if len(state.channel_notes) == 0 {
		return
	}

	segment_width := f32(rl.GetRenderWidth()) / f32(len(state.channel_notes))
	segment_height := f32(rl.GetRenderHeight())
	fmt.printf("%.2f\t%.2f\n", segment_width, segment_height)

	for ch_note, ch_index in state.channel_notes {
		rl.DrawRectangle(
			i32(segment_width * f32(ch_index)),
			0,
			i32(segment_width),
			i32(segment_height),
			rl.ColorFromHSV(
				360. * (f32(ch_index) / f32(len(state.channel_notes))),
				1. - ch_note.alpha,
				1.,
			),
		)

		if ch_note.alpha == 0. {
			continue
		}

		note_text_color := rl.Color{0, 0, 0, u8(ch_note.alpha * 255.)}

		rl.DrawRectangle(
			i32(segment_width * f32(ch_index)),
			i32((ch_note.note / 120.) * f32(segment_height)),
			i32(segment_width),
			10,
			note_text_color,
		)

		if !rl.IsFontValid(state.note_font) {
			continue
		}

		note_text: cstring
		if ch_note.note < 0. || ch_note.note > 255. {
			note_text = "baby carrots"
		} else {
			note_text = rl.TextFormat(note_fmts[u8(ch_note.note) % 12], u8(ch_note.note) / 12)
		}

		note_text_dimensions := rl.Vector2 {
			f32(rl.MeasureText(note_text, state.note_font.baseSize)),
			f32(state.note_font.baseSize),
		}

		note_text_position :=
			rl.Vector2{f32(segment_width), f32(segment_height)} / 2. +
			rl.Vector2{segment_width * f32(ch_index), 0}

		rl.DrawTextPro(
			state.note_font,
			note_text,
			note_text_position,
			note_text_dimensions / 2.,
			90.,
			f32(state.note_font.baseSize),
			1.,
			note_text_color,
		)
	}
}
