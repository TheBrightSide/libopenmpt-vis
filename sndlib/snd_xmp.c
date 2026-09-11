#include "snd.h"
#include "snd_ring_buffer.h"

#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_mutex.h>

#include <stdio.h>
#include <stdlib.h>

#include <xmp.h>

static int16_t SND_ZEROARRAY[1024] = {0};

struct sndContext {
  xmp_context xmp;
  SDL_Mutex *xmp_mutex;
  SDL_AudioStream *audio_stream;
  SDL_AtomicU32 audio_written_samples;
  uint32_t audio_sample_rate;
  struct xmp_frame_info xmp_current_frame_info;
  sndRingBuffer event_buffer;
};

size_t sndEventPop(sndContext *ctx, sndTickEvent *out, size_t cap) {
  return sndRingBufferPop(&ctx->event_buffer, out, cap, sndPlaybackGetClock(ctx));
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

  if (ctx == NULL) {
    return;
  }

  if (!SDL_TryLockMutex(ctx->xmp_mutex)) {
    sndFeedSilence(stream, additional_amount);
    return;
  }

  if (ctx->xmp == NULL) {
    SDL_UnlockMutex(ctx->xmp_mutex);
    SDL_PauseAudioStreamDevice(stream);
    return;
  }

  const int bytes_per_frame = 2 * (int)sizeof(int32_t);
  int frames_needed = additional_amount / bytes_per_frame;
  uint32_t total_frames_written = SDL_GetAtomicU32(&ctx->audio_written_samples);
  while (frames_needed > 0) {
    xmp_play_frame(ctx->xmp);
    xmp_get_frame_info(ctx->xmp, &ctx->xmp_current_frame_info);

    if (ctx->xmp_current_frame_info.loop_count > 0) {
      SDL_PauseAudioStreamDevice(stream);
      SDL_UnlockMutex(ctx->xmp_mutex);
      return;
    }

    sndTickEvent event = {0};
    event.channel_count = ctx->xmp_current_frame_info.virt_channels;
    if (event.channel_count > MAX_CHANNELS) {
      event.channel_count = MAX_CHANNELS;
    }

    for (size_t ch = 0; ch < (size_t)event.channel_count;
         ch += 1) {
      event.channels[ch].pitchbend =
          ctx->xmp_current_frame_info.channel_info[ch].pitchbend;
      event.channels[ch].note =
          ctx->xmp_current_frame_info.channel_info[ch].note;
      event.channels[ch].instrument =
          ctx->xmp_current_frame_info.channel_info[ch].instrument;
      event.channels[ch].sample =
          ctx->xmp_current_frame_info.channel_info[ch].sample;
      event.channels[ch].volume =
          ctx->xmp_current_frame_info.channel_info[ch].volume;
      event.channels[ch].pan = ctx->xmp_current_frame_info.channel_info[ch].pan;
    }

    sndRingBufferPush(&ctx->event_buffer, &event);

    void *buffer = ctx->xmp_current_frame_info.buffer;
    uint32_t buffer_size = ctx->xmp_current_frame_info.buffer_size;
    uint32_t frame_amount = buffer_size / 2 / sizeof(int32_t);
    SDL_PutAudioStreamData(stream, buffer, buffer_size);

    total_frames_written += frame_amount;
    frames_needed -= frame_amount;
  }

  SDL_SetAtomicU32(&ctx->audio_written_samples, total_frames_written);
  SDL_UnlockMutex(ctx->xmp_mutex);
}

sndContext *sndCreateContext(uint32_t sample_rate) {
  if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && !SDL_Init(SDL_INIT_AUDIO)) {
    return NULL;
  }

  SDL_Mutex *xmp_mutex = SDL_CreateMutex();
  if (xmp_mutex == NULL) {
    return NULL;
  }

  SDL_AudioSpec sdl_spec = {
      .channels = 2,
      .freq = sample_rate,
      .format = SDL_AUDIO_S32,
  };

  sndContext *ctx = calloc(1, sizeof(sndContext));
  if (ctx == NULL) {
    SDL_DestroyMutex(xmp_mutex);
    return NULL;
  }
  ctx->xmp = xmp_create_context();
  SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &sdl_spec, sndSDLAudioProcess, ctx);

  if (stream == NULL) {
    SDL_DestroyMutex(xmp_mutex);
    free(ctx);
    return NULL;
  }

  ctx->xmp_mutex = xmp_mutex;
  ctx->audio_stream = stream;
  ctx->audio_sample_rate = sample_rate;

  return ctx;
}

void sndDestroyContext(sndContext *ctx) {
  if (ctx == NULL)
    return;

  SDL_PauseAudioStreamDevice(ctx->audio_stream);
  SDL_DestroyAudioStream(ctx->audio_stream);
  ctx->audio_stream = NULL;

  SDL_DestroyMutex(ctx->xmp_mutex);
  ctx->xmp_mutex = NULL;

  xmp_free_context(ctx->xmp);
  ctx->xmp = NULL;

  free(ctx);
}

bool sndModuleLoad(sndContext *ctx, const uint8_t *module, size_t size) {
  if (ctx == NULL)
    return false;

  SDL_LockMutex(ctx->xmp_mutex);
  bool result = xmp_load_module_from_memory(ctx->xmp, module, size) == 0;
  SDL_UnlockMutex(ctx->xmp_mutex);

  return result;
}

uint32_t sndPlaybackGetClock(sndContext *ctx) {
  if (ctx == NULL)
    return 0;

  int bytes_per_frame = 2 * (int)sizeof(int16_t);
  uint32_t written_frames = SDL_GetAtomicU32(&ctx->audio_written_samples);
  int available_bytes = SDL_GetAudioStreamAvailable(ctx->audio_stream);
  if (available_bytes < 0) {
    available_bytes = 0;
  }

  uint32_t queued_frames = (uint32_t)(available_bytes / bytes_per_frame);
  return (uint32_t)(written_frames - queued_frames);
}

bool sndPlaybackIsPaused(sndContext *ctx) {
  if (ctx == NULL)
    return true;
  return SDL_AudioStreamDevicePaused(ctx->audio_stream);
}

void sndPlaybackResume(sndContext *ctx) {
  if (ctx == NULL)
    return;

  int player_state = xmp_get_player(ctx->xmp, XMP_PLAYER_STATE);
  if (player_state != XMP_STATE_PLAYING) {
    if (player_state == XMP_STATE_UNLOADED)
      return;

    if (xmp_start_player(ctx->xmp, ctx->audio_sample_rate, XMP_FORMAT_32BIT) !=
        0)
      return;
  }

  SDL_ResumeAudioStreamDevice(ctx->audio_stream);
}

void sndPlaybackPause(sndContext *ctx) {
  if (ctx == NULL)
    return;

  SDL_PauseAudioStreamDevice(ctx->audio_stream);
}

void sndPlaybackSetOrder(sndContext *ctx) {
  (void)ctx;

  // TODO:
}
