#include <raylib.h>

#include <assert.h>
#include <inttypes.h>
#include <raymath.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "snd.h"

static const char *NOTES[] = {
    "C-%" PRIu8 "\n", "C#%" PRIu8 "\n", "D-%" PRIu8 "\n", "D#%" PRIu8 "\n",
    "E-%" PRIu8 "\n", "F-%" PRIu8 "\n", "F#%" PRIu8 "\n", "G-%" PRIu8 "\n",
    "G#%" PRIu8 "\n", "A-%" PRIu8 "\n", "A#%" PRIu8 "\n", "B-%" PRIu8 "\n",
};

void signal_handler(int sig) {
  if (sig == SIGINT)
    exit(1);
}

typedef struct ChannelNote {
  float note;
  float glow;
  float text_alpha;
  bool show;
} ChannelNote;

typedef struct World {
  ChannelNote channel_notes[MAX_CHANNELS];
  size_t used_channel_notes;
  sndContext *sound_context;
  
} World;

int main(int argc, char **argv) {
  assert(argc > 1 && "No module path supplied");
  const char *path = argv[1];

  signal(SIGINT, signal_handler);
  sndContext *ctx = sndCreateContext(48000);
  size_t channel_notes_len = 1;
  ChannelNote channel_notes[MAX_CHANNELS];

  int mod_data_size;
  uint8_t *mod_data = LoadFileData(path, &mod_data_size);
  assert(mod_data != NULL && "Could not load file");

  assert(ctx != NULL && "Could not create sound context");
  assert(sndModuleLoad(ctx, mod_data, (size_t)mod_data_size));

  InitWindow(1600, 900, "smeshka 3");
  assert(IsWindowReady() && "Window is not ready");

  Font note_font = LoadFontEx("font.ttf", 30, NULL, 0);
  assert(IsFontValid(note_font) && "Font is invalid");

  SetTargetFPS(120);

  sndPlaybackResume(ctx);

  int segment_width = GetScreenWidth() / channel_notes_len;
  int segment_height = GetScreenHeight();
  while (!WindowShouldClose() && !sndPlaybackIsPaused(ctx)) {
    sndTickEvent events[16];
    size_t got = sndEventPop(ctx, events, 16);

    if (got > 0) {
      channel_notes_len = events[0].channel_count;
      segment_width = GetScreenWidth() / channel_notes_len;
      segment_height = GetScreenHeight();
    }

    for (size_t i = 0; i < got; i++) {
      for (size_t ch = 0; ch < events[i].channel_count; ch++) {
        printf("%d\t", events[i].channels[ch].pitchbend );

        if (events[i].channels[ch].note == 0xff) {
          channel_notes[ch].show = false;
          continue;
        }

        // channel_notes[ch].note =
        //     (float)events[i].channels[ch].note +
        //     (float)events[i].channels[ch].pitchbend / 256.f;
        channel_notes[ch].note = events[i].channels[ch].note + (float)events[i].channels[ch].pitchbend / 100.f;
        channel_notes[ch].glow = (float)events[i].channels[ch].volume / 64.f;
        channel_notes[ch].text_alpha =
            (float)events[i].channels[ch].volume / 64.f;
        channel_notes[ch].show = true;
      }
      printf("\n");
    }

    BeginDrawing();
    ClearBackground(BLACK);

    for (size_t i = 0; i < channel_notes_len; i += 1) {
      DrawRectangle(segment_width * i, 0, segment_width, segment_height,
                    ColorFromHSV(360.f * ((float)i / (float)channel_notes_len),
                                 (1. - channel_notes[i].glow), 1.));

      if (!channel_notes[i].show)
        continue;

      Color note_text_color = BLACK;
      note_text_color.a = (uint8_t)(channel_notes[i].text_alpha * 255.);

      const char *note_text =
          TextFormat(NOTES[(uint8_t)channel_notes[i].note % 12],
                     (uint8_t)channel_notes[i].note / 12);

      Vector2 note_text_dimensions = {0};
      note_text_dimensions.y = note_font.baseSize;
      note_text_dimensions.x =
          (float)MeasureText(note_text, note_text_dimensions.y);

      Vector2 note_text_origin = {.x = note_text_dimensions.x / 2,
                                  .y = note_text_dimensions.y / 2};

      Vector2 note_text_position = {.x = (segment_width / 2.) +
                                         segment_width * i,
                                    .y = segment_height / 2.};

      DrawRectangle(
          segment_width * i,
          (int)((channel_notes[i].note / 120.) * segment_height),
          segment_width, 10, note_text_color);

      DrawTextPro(note_font, note_text, note_text_position, note_text_origin,
                  90.f, note_font.baseSize, 1.f, note_text_color);
    }

    DrawFPS(10, 10);

    EndDrawing();
  }

  UnloadFont(note_font);
  CloseWindow();

  channel_notes_len = 0;

  sndDestroyContext(ctx);
  ctx = NULL;

  UnloadFileData(mod_data);
  mod_data_size = 0;

  return 0;
}
