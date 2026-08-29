#include "snd.h"

#include <stdio.h>
#include <SDL3/SDL_timer.h>

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "rollingdownthestreet.xm";

  sndAudioSpec spec = {.sample_rate = 44100, .channels = 2};
  sndContext *ctx = sndCreateContext(&spec);

  if (ctx == NULL) {
    fprintf(stderr, "FAIL: sndCreateContext returned NULL\n");
    return 1;
  }

  if (!sndModuleLoadFile(ctx, path)) {
    fprintf(stderr, "WARN: could not load '%s' (missing file?)\n", path);
    return 1;
  }

  sndPlaybackResume(ctx);
  while (!sndPlaybackIsPaused(ctx)) {
    sndNote notes[64];
    size_t n = sndEventPop(ctx, notes, 64);
    printf("recv\n");
    for (size_t i = 0; i < n && i < 8; i++) {
      printf("  ch=%d note=%u t=%f\n", notes[i].channel, notes[i].note,
             (double)notes[i].audible_timestamp);
    }

    SDL_Delay(10);
  }

  sndDestroyContext(ctx);
  printf("OK: destroyed\n");

  return 0;
}
