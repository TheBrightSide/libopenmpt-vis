#ifndef SND_H
#define SND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_CHANNELS 64

typedef struct sndChannelState {
  int16_t pitchbend;
  uint8_t note;
  uint8_t instrument;
  uint8_t sample;
  uint8_t volume;
  uint8_t pan;
} sndChannelState;

typedef struct sndTickEvent {
  sndChannelState channels[MAX_CHANNELS];
  uint32_t timestamp;
  uint8_t channel_count;
} sndTickEvent;

typedef struct sndContext sndContext;

sndContext *sndCreateContext(uint32_t sample_rate);

bool sndModuleLoad(sndContext *ctx, const uint8_t *module, size_t size);

uint32_t sndPlaybackGetClock(sndContext *ctx);

bool sndPlaybackIsPaused(sndContext *ctx);
void sndPlaybackResume(sndContext *ctx);
void sndPlaybackPause(sndContext *ctx);
void sndPlaybackSetOrder(sndContext *ctx);

size_t sndEventPop(sndContext *ctx, sndTickEvent* out, size_t cap);

void sndDestroyContext(sndContext *ctx);

#endif // SND_H
