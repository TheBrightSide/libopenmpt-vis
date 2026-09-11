#include "snd_ring_buffer.h"

#include <SDL3/SDL_atomic.h>

// NOTE: Producer -- only to be called from audio thread/callback (single
//       producer)
bool sndRingBufferPush(sndRingBuffer *buf, sndTickEvent *event) {
  uint32_t w = SDL_GetAtomicU32(&buf->write_idx); // our own idx
  uint32_t r = SDL_GetAtomicU32(&buf->read_idx);  // see freed slots
  // Ensure we observe the consumer's completed reads of any slot we are about
  // to reuse. Without this acquire, reusing a freed slot races with the
  // consumer's earlier read of that same slot.
  SDL_MemoryBarrierAcquire();
  if (w - r == SND_RING_CAP)
    return false;

  buf->data[w & SND_RING_MASK] = *event;    // 1. write
  SDL_MemoryBarrierRelease();               // 2. order
  SDL_SetAtomicU32(&buf->write_idx, w + 1); // 3. publish

  return true;
}

// NOTE: Consumer -- only to be called from user thread (e.g. renderer thread)
//       (single consumer)
size_t sndRingBufferPop(sndRingBuffer *buf, sndTickEvent *events, size_t cap,
                        uint32_t current_time) {
  uint32_t w = SDL_GetAtomicU32(&buf->write_idx); // see published slots
  uint32_t r = SDL_GetAtomicU32(&buf->read_idx);  // our own idx

  uint32_t avail = w - r;
  if (avail == 0)
    return 0;

  SDL_MemoryBarrierAcquire(); // 1.1. order: see published data before reading
                              // it
  size_t n = (size_t)(avail < cap ? avail : cap);
  size_t actual_read = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (buf->data[(r + i) & SND_RING_MASK].timestamp > current_time)
      break;

    events[i] = buf->data[(r + i) & SND_RING_MASK]; // 1.2.*. subsequent reads
    actual_read += 1;
  }

  SDL_MemoryBarrierRelease(); // 2.1. order: done copying before freeing the
                              // slot
  SDL_SetAtomicU32(&buf->read_idx,
                   r + (uint32_t)actual_read); // 2.2. publish: mark free slots
  return actual_read;
}
