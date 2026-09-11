#include "snd.h"
#include "snd_ring_buffer.h"

#include <libopenmpt/libopenmpt.h>
#include <libopenmpt/libopenmpt_stream_callbacks_buffer.h>

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_mutex.h>

static int16_t SND_ZEROARRAY[1024] = {0};

struct sndContext {
  SDL_AudioStream *stream;
  void *callback_buffer;
  size_t callback_buffer_cap;
  SDL_Mutex *mod_mutex;
  openmpt_module *mod;
  openmpt_stream_buffer2 mod_data;
  SDL_AtomicU32 stream_written_samples;
  int32_t mod_last_order;
  int32_t mod_last_row;
  int32_t mod_channels;
  uint32_t audio_sample_rate;
  sndRingBuffer event_buffer;
  uint8_t audio_channels;
  bool is_mod_data_file;
};

size_t sndEventPop(sndContext *ctx, sndNote* out, size_t cap) {
  return sndRingBufferPop(&ctx->event_buffer, out, cap);
}

size_t sndRenderChunk(sndContext *ctx, float *dst, size_t n_frames) {
  size_t rendered_frames = openmpt_module_read_interleaved_float_stereo(
      ctx->mod, ctx->audio_sample_rate, n_frames, dst);

  if (rendered_frames == 0)
    return 0;

  // TODO:
  double playback_time = openmpt_module_get_position_seconds(ctx->mod);
  int32_t order = openmpt_module_get_current_order(ctx->mod);
  int32_t row = openmpt_module_get_current_row(ctx->mod);

  if (order != ctx->mod_last_order || row != ctx->mod_last_row) {
    ctx->mod_last_order = order;
    ctx->mod_last_row = row;

    int32_t pattern = openmpt_module_get_current_pattern(ctx->mod);
    // double tempo = openmpt_module_get_current_tempo2(ctx->mod);
    // int32_t speed = openmpt_module_get_current_speed(ctx->mod);

    for (int32_t channel = 0; channel < ctx->mod_channels; channel++) {
      uint8_t note = openmpt_module_get_pattern_row_channel_command(
          ctx->mod, pattern, row, channel, OPENMPT_MODULE_COMMAND_NOTE);

      // note == 0 means "no note" in MOD/XM/S3M/IT; skip it so the ring
      // buffer only carries actual note events.
      if (note == 0)
        continue;

      sndNote event = {
          .note = note,
          .channel = channel,
          .audible_timestamp = playback_time,
      };
      sndRingBufferPush(&ctx->event_buffer, &event);
    }
  }

  return rendered_frames;
}

// Subdivide rendering into slices shorter than one tracker tick so that no row
// transition (and thus no note event) is skipped between state polls. A tick
// is 60/(tempo*24) seconds for MOD/XM/S3M/IT, so half a tick is always smaller
// than a row (row = speed*ticks, speed >= 1).
static uint32_t sndTickSliceFrames(openmpt_module *mod, uint32_t sample_rate) {
  double tempo = openmpt_module_get_current_tempo2(mod);
  if (tempo <= 0.0)
    tempo = 125.0;
  double tick =
      (double)sample_rate * 60.0 / (tempo * 24.0); // tick duration, frames
  uint32_t slice = (uint32_t)(tick / 2.0);
  return slice < 1 ? 1 : slice;
}

static void sndFeedSilence(SDL_AudioStream *stream, int additional_amount) {
  int needed = additional_amount;
  while (needed > 0) {
    int chunk = needed < (int)sizeof(SND_ZEROARRAY)
                    ? needed
                    : (int)sizeof(SND_ZEROARRAY);
    SDL_PutAudioStreamData(stream, SND_ZEROARRAY, chunk);
    needed -= chunk;
  }
}

void sndSDLAudioProcess(void *userdata, SDL_AudioStream *stream,
                        int additional_amount, int total_amount) {
  (void)total_amount;

  sndContext *ctx = (sndContext *)userdata;

  // if we fail to acquire, the loader thread owns the mutex, therefore feed
  // silence and retry next callback.
  if (!SDL_TryLockMutex(ctx->mod_mutex)) {
    sndFeedSilence(stream, additional_amount);
    return;
  }

  // from here on we hold the mutex.
  if (ctx->mod == NULL) {
    SDL_UnlockMutex(ctx->mod_mutex);
    SDL_PauseAudioStreamDevice(stream);
    return;
  }

  const int bytes_per_frame = ctx->audio_channels * (int)sizeof(float);
  int frames_needed = additional_amount / bytes_per_frame;
  uint32_t total_frames_written = SDL_GetAtomicU32(&ctx->stream_written_samples);

  while (frames_needed > 0) {
    // slice by at most half a tracker tick so that each sndRenderChunk call
    // advances less than one row so no row is skipped between 2 state polls.
    uint32_t slice = sndTickSliceFrames(ctx->mod, ctx->audio_sample_rate);
    size_t n =
        (size_t)(frames_needed < (int)slice ? frames_needed : (int)slice);

    if (n > ctx->callback_buffer_cap) {
      int16_t *nb = realloc(ctx->callback_buffer, n * bytes_per_frame);
      if (!nb) {
        SDL_PauseAudioStreamDevice(stream);
        SDL_UnlockMutex(ctx->mod_mutex);
        return;
      }

      ctx->callback_buffer = nb;
      ctx->callback_buffer_cap = n;
    }

    size_t got = sndRenderChunk(ctx, ctx->callback_buffer, n);
    if (got == 0) {
      // end of song
      SDL_PauseAudioStreamDevice(stream);
      SDL_UnlockMutex(ctx->mod_mutex);
      return;
    }
    SDL_PutAudioStreamData(stream, ctx->callback_buffer,
                           (int)(got * bytes_per_frame));

    total_frames_written += (uint32_t)got;
    frames_needed -= (int)got;
  }

  SDL_SetAtomicU32(&ctx->stream_written_samples, total_frames_written);
  SDL_UnlockMutex(ctx->mod_mutex);
}

sndContext *sndCreateContext(const sndAudioSpec *spec) {
  if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && !SDL_Init(SDL_INIT_AUDIO)) {
    return NULL; // failed to init SDL
  }

  SDL_Mutex *mod_mutex = SDL_CreateMutex();
  if (mod_mutex == NULL) {
    return NULL;
  }

  SDL_AudioSpec sdlSpec = {
      .channels = 2, // NOTE: for now only stereo supported
      .freq = spec->sample_rate,
      .format = SDL_AUDIO_F32,
  };

  sndContext *ctx = calloc(1, sizeof(sndContext));
  if (ctx == NULL) {
    SDL_DestroyMutex(mod_mutex);
    return NULL;
  }

  // 50 ms render slices (silent fallback)
  size_t predictedChunkFrames = spec->sample_rate / 20;
  ctx->callback_buffer = calloc(predictedChunkFrames * 2 * sizeof(int16_t), 1);
  ctx->callback_buffer_cap = predictedChunkFrames;

  SDL_SetAtomicU32(&ctx->stream_written_samples, 0);
  SDL_SetAtomicU32(&ctx->event_buffer.write_idx, 0);
  SDL_SetAtomicU32(&ctx->event_buffer.read_idx, 0);
  SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &sdlSpec, sndSDLAudioProcess, ctx);

  if (stream == NULL) {
    SDL_DestroyMutex(mod_mutex);
    free(ctx);
    return NULL; // failed to open stream
  }

  ctx->mod_mutex = mod_mutex;
  ctx->stream = stream;
  ctx->mod_last_order = -1;
  ctx->mod_last_row = -1;
  ctx->audio_sample_rate = spec->sample_rate;
  ctx->audio_channels = 2; // NOTE: for now only two channels are supported

  return ctx;
}

void sndFreeStream(sndContext *ctx) {
  if (ctx->stream != NULL) {
    SDL_PauseAudioStreamDevice(ctx->stream);
    SDL_DestroyAudioStream(ctx->stream);
    ctx->stream = NULL;
  }
}

void sndFreeModule(sndContext *ctx) {
  if (ctx->mod != NULL) {
    ctx->mod_channels = 0;
    ctx->mod_last_order = -1;
    ctx->mod_last_row = -1;

    openmpt_module_destroy(ctx->mod);
    ctx->mod = NULL;
  }

  if (ctx->is_mod_data_file) {
    ctx->is_mod_data_file = false;

    if (ctx->mod_data.file_data != NULL) {
      SDL_free((void *)ctx->mod_data.file_data);
    }
  }

  openmpt_stream_buffer2 empty = {0};
  ctx->mod_data = empty;
}

bool sndInnerModuleLoadMemory(sndContext *ctx, const uint8_t *module,
                              size_t size) {
  if (module == NULL || size == 0)
    return false;

  // remember old mod data for rollback
  openmpt_stream_buffer2 old_mod_data = ctx->mod_data;

  openmpt_stream_buffer2 new_mod_data = {
      .file_data = module,
      .file_size = size,
      .file_pos = 0,
  };

  // store new mod data for initial loading
  ctx->mod_data = new_mod_data;
  openmpt_module *new_mod = openmpt_module_create2(
      openmpt_stream_get_buffer_callbacks2(), &ctx->mod_data,
      openmpt_log_func_silent, NULL, NULL, NULL, NULL, NULL, NULL);

  if (new_mod == NULL) {
    // upon failure rollback to old mod data
    ctx->mod_data = old_mod_data;

    return false;
  }

  // upon success point mod_data back at the old buffer so freeModule can
  // release it
  ctx->mod_data = old_mod_data;
  sndFreeModule(ctx);

  // ... then store the new mod and reset timers
  ctx->mod_data = new_mod_data;
  ctx->mod = new_mod;
  ctx->mod_channels = openmpt_module_get_num_channels(ctx->mod);
  SDL_SetAtomicU32(&ctx->stream_written_samples, 0);

  return true;
}

bool sndModuleLoadFile(sndContext *ctx, const char *file_path) {
  if (ctx == NULL)
    return false;

  size_t file_size;
  const uint8_t *data = SDL_LoadFile(file_path, &file_size);
  if (data == NULL) {
    return false;
  }

  SDL_LockMutex(ctx->mod_mutex);
  bool ok = sndInnerModuleLoadMemory(ctx, data, file_size);
  if (ok) {
    ctx->is_mod_data_file = true;
  } else {
    SDL_free((void *)data);
  }
  SDL_UnlockMutex(ctx->mod_mutex);
  return ok;
}

bool sndModuleLoadMemory(sndContext *ctx, const uint8_t *module, size_t size) {
  if (ctx == NULL)
    return false;

  SDL_LockMutex(ctx->mod_mutex);
  bool success = sndInnerModuleLoadMemory(ctx, module, size);
  ctx->is_mod_data_file =
      false; // NOTE: sndFreeModule already resets the flag to false,
             //       this is a precaution.
  SDL_UnlockMutex(ctx->mod_mutex);

  return success;
}

int32_t sndModuleGetChannelCount(sndContext *ctx) {
  if (ctx == NULL)
    return false;

  return ctx->mod_channels;
}

float sndPlaybackGetClock(sndContext *ctx) {
  int bytes_per_frame = ctx->audio_channels * (int)sizeof(int16_t);
  uint32_t written_frames = SDL_GetAtomicU32(&ctx->stream_written_samples);
  int available_bytes = SDL_GetAudioStreamAvailable(ctx->stream);
  if (available_bytes < 0)
    available_bytes = 0;

  uint32_t queued_frames = (uint32_t)(available_bytes / bytes_per_frame);
  return (float)(written_frames - queued_frames) /
         (float)ctx->audio_sample_rate;
}

bool sndPlaybackIsPaused(sndContext *ctx) {
  return SDL_AudioStreamDevicePaused(ctx->stream);
}

void sndPlaybackResume(sndContext *ctx) {
  if (ctx == NULL)
    return;

  SDL_ResumeAudioStreamDevice(ctx->stream);
}

void sndPlaybackPause(sndContext *ctx) {
  if (ctx == NULL)
    return;

  SDL_PauseAudioStreamDevice(ctx->stream);
}

void sndPlaybackSetOrder(sndContext *ctx) {
  (void)ctx;
  // TODO:
}

void sndDestroyContext(sndContext *ctx) {
  if (ctx == NULL)
    return;

  sndFreeStream(ctx);
  sndFreeModule(ctx);

  SDL_DestroyMutex(ctx->mod_mutex);

  free(ctx->callback_buffer);
  ctx->callback_buffer = NULL;
  ctx->callback_buffer_cap = 0;

  free(ctx);
}
