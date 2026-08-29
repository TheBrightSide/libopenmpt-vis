#ifndef SND_H
#define SND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct sndSpec {
  uint32_t sample_rate;
  uint8_t channels;
} sndAudioSpec;

typedef struct sndNote {
  float audible_timestamp;
  int32_t channel;
  uint8_t note;
} sndNote;

typedef struct sndContext sndContext;

sndContext *sndCreateContext(const sndAudioSpec *spec);

bool sndModuleLoadFile(sndContext *ctx, const char *file_path);
bool sndModuleLoadMemory(sndContext *ctx, const uint8_t *module, size_t size);

float sndPlaybackGetClock(sndContext *ctx);

bool sndPlaybackIsPaused(sndContext *ctx);
void sndPlaybackResume(sndContext *ctx);
void sndPlaybackPause(sndContext *ctx);
void sndPlaybackSetOrder(sndContext *ctx);

size_t sndEventPop(sndContext *ctx, sndNote* out, size_t cap);

void sndDestroyContext(sndContext *ctx);

#endif // SND_H
