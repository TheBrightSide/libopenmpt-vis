/*
 * libopenmpt playback + player-state logging demo (PipeWire output).
 *
 * Plays a module via native PipeWire (libpipewire + SPA), with a silent
 * fallback if no PipeWire daemon is reachable, and logs "note events" by
 * watching the pattern position advance, plus a live VU meter per channel.
 * This is a starting point for a visualiser: all player state is queried
 * from libopenmpt in the render path below.
 *
 * Build:  make
 * Run:    ./player path/to/module.xm
 */

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libopenmpt/libopenmpt.h>
#include <libopenmpt/libopenmpt_stream_callbacks_file.h>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#define SR 44100
#define CHUNK_FRAMES (SR / 20) /* 50 ms render slices (silent fallback) */

/* ---- globals shared between the render path and the PipeWire callback ---- */
static openmpt_module *g_mod = NULL;
static int g_num_channels = 0;
static int32_t g_last_order = -1, g_last_row = -1;
static double g_last_vu = -1.0;

/* A pattern note cell "C-4", "G#3", ... contains a digit; "---", "===", "" do
 * not. This is our cheap "is this a real note trigger?" test. */
static int is_note_string(const char *s) {
  if (!s)
    return 0;
  for (; *s; ++s)
    if (*s >= '0' && *s <= '9')
      return 1;
  return 0;
}

/* Render up to n_frames of audio into dst, then poll+log player state.
 * Returns the number of frames rendered (0 == end of song). */
static size_t render_chunk(int16_t *dst, size_t n_frames) {
  size_t r = openmpt_module_read_interleaved_stereo(g_mod, SR, n_frames, dst);
  if (r == 0)
    return 0;

  double t = openmpt_module_get_position_seconds(g_mod);
  int32_t order = openmpt_module_get_current_order(g_mod);
  int32_t row = openmpt_module_get_current_row(g_mod);

  /* --- NOTE EVENTS: detected by watching the playback position --- */
  if (order != g_last_order || row != g_last_row) {
    g_last_order = order;
    g_last_row = row;

    int32_t pattern = openmpt_module_get_current_pattern(g_mod);
    double tempo = openmpt_module_get_current_tempo2(g_mod);
    int32_t speed = openmpt_module_get_current_speed(g_mod);

    printf("[t=%.2fs order=%d row=%d pat=%d tempo=%.0f speed=%d] ", t, order,
           row, pattern, tempo, speed);

    for (int c = 0; c < g_num_channels; c++) {
      const char *note = openmpt_module_format_pattern_row_channel_command(
          g_mod, pattern, row, c, OPENMPT_MODULE_COMMAND_NOTE);
      printf("ch%d:%s ", c, note);
      // if (is_note_string(note)) {
      //     const char *inst =
      //     openmpt_module_format_pattern_row_channel_command(
      //         g_mod, pattern, row, c, OPENMPT_MODULE_COMMAND_INSTRUMENT);
      //     printf("ch%d:%s(i%s) ", c, note, inst ? inst : "?");
      //     openmpt_free_string(inst);
      // }
      openmpt_free_string(note);
    }
    printf("\n");
    fflush(stdout);
  }

  /* --- LIVE STATE: per-channel VU, sampled a few times a second --- */
  // if (t - g_last_vu >= 0.25) {
  //     g_last_vu = t;
  //     printf("  VU: ");
  //     for (int c = 0; c < g_num_channels; c++) {
  //         float v = openmpt_module_get_current_channel_vu_mono(g_mod, c);
  //         int bars = (int) (v * 10.0f + 0.5f);
  //         if (bars < 0) bars = 0;
  //         if (bars > 10) bars = 10;
  //         printf("%d:[%s%-*s] ", c, "##########" + (10 - bars), 10 - bars,
  //         "");
  //     }
  //     printf("\n");
  //     fflush(stdout);
  // }

  return r;
}

/* Subdivide rendering into slices shorter than one tracker tick so that no row
 * transition (and thus no note event) is skipped between state polls. A tick
 * is 60/(tempo*24) seconds for MOD/XM/S3M/IT, so half a tick is always smaller
 * than a row (row = speed*ticks, speed >= 1). */
static uint32_t tick_slice_frames(void) {
  double tempo = openmpt_module_get_current_tempo2(g_mod);
  if (tempo <= 0.0)
    tempo = 125.0;
  double tick = (double)SR * 60.0 / (tempo * 24.0); /* tick duration, frames */
  uint32_t slice = (uint32_t)(tick / 2.0);
  return slice < 1 ? 1 : slice;
}

void sdl_audio_process(void *_, SDL_AudioStream *stream, int additional_amount,
                       int total_amount) {
  // TODO:
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <module>\n", argv[0]);
    return 1;
  }

  FILE *file = fopen(argv[1], "rb");
  if (!file) {
    perror("fopen");
    return 1;
  }

  int err = 0;
  const char *errmsg = NULL;
  g_mod = openmpt_module_create2(openmpt_stream_get_file_callbacks2(), file,
                                 openmpt_log_func_silent, NULL, NULL, NULL,
                                 &err, &errmsg, NULL);
  if (!g_mod) {
    fprintf(stderr, "failed to load module %s: %s\n", argv[1],
            errmsg ? errmsg : "unknown error");
    if (errmsg)
      openmpt_free_string(errmsg);
    fclose(file);
    return 1;
  }

  g_num_channels = openmpt_module_get_num_channels(g_mod);

  if (!SDL_Init(SDL_INIT_AUDIO)) {
    fprintf(stderr, "could not initialize SDL: %s\n", SDL_GetError());
    goto cleanup;
  }

  SDL_AudioSpec spec = {
      .channels = 2,
      .format = SDL_AUDIO_S16,
      .freq = SR,
  };

  SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sdl_audio_process, NULL);

  if (stream == NULL) {
    fprintf(stderr, "could not open audio stream: %s\n", SDL_GetError());
    goto cleanup;
  }

  if (!SDL_ResumeAudioStreamDevice(stream)) {
    fprintf(stderr, "could not resume audio device: %s\n", SDL_GetError());
    SDL_DestroyAudioStream(stream);
    goto cleanup;
  }

  printf("done. position=%.2fs channels=%d\n",
         openmpt_module_get_position_seconds(g_mod), g_num_channels);

cleanup:
  openmpt_module_destroy(g_mod);
  fclose(file);
  return 0;
}
