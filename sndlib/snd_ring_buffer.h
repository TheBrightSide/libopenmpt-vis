#ifndef SND_RING_BUFFER_H
#define SND_RING_BUFFER_H

#include "snd.h"

#include <SDL3/SDL_atomic.h>

#include <stdbool.h>

#define SND_RING_CAP (1u << 5 /* 32 */)
#define SND_RING_MASK (SND_RING_CAP - 1)

typedef struct sndRingBuffer {
  sndTickEvent data[SND_RING_CAP];
  SDL_AtomicU32 write_idx; // producer-only
  SDL_AtomicU32 read_idx;  // consumer-only
} sndRingBuffer;

bool sndRingBufferPush(sndRingBuffer *buf, sndTickEvent *event);
size_t sndRingBufferPop(sndRingBuffer *buf, sndTickEvent *events, size_t cap,
                        uint32_t time);

#endif // SND_RING_BUFFER_H
