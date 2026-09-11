package sndlib

import "core:c"

foreign import lib {"libsnd.a", "system:xmp", "system:SDL3"}

MAX_CHANNELS :: 64

ChannelState :: struct {
	pitchbend:  c.int16_t,
	note:       c.uint8_t,
	instrument: c.uint8_t,
	sample:     c.uint8_t,
	volume:     c.uint8_t,
	pan:        c.uint8_t,
}

TickEvent :: struct {
	channels:      [MAX_CHANNELS]ChannelState,
	timestamp:     c.uint32_t,
	channel_count: c.uint8_t,
}

Context :: distinct rawptr

@(default_calling_convention = "c", link_prefix = "snd")
foreign lib {
	CreateContext :: proc(sample_rate: c.uint32_t) -> Context ---

	@(link_name = "sndModuleLoad")
	ModuleLoadRaw :: proc(ctx: Context, module: [^]c.uint8_t, size: c.size_t) -> bool ---

	PlaybackGetClock :: proc(ctx: Context) -> c.uint32_t ---

	PlaybackIsPaused :: proc(ctx: Context) -> bool ---
	PlaybackResume :: proc(ctx: Context) ---
	PlaybackPause :: proc(ctx: Context) ---
	PlaybackSetOrder :: proc(ctx: Context) ---

	@(link_name = "sndEventPop")
	EventPopRaw :: proc(ctx: Context, out: [^]TickEvent, cap: c.size_t) -> c.size_t ---

	DestroyContext :: proc(ctx: Context) ---
}

ModuleLoad :: proc(ctx: Context, module: []byte) -> bool {
	return ModuleLoadRaw(ctx, raw_data(module), len(module))
}

EventPop :: proc(ctx: Context, out: []TickEvent) -> uint {
	return EventPopRaw(ctx, raw_data(out), len(out))
}
