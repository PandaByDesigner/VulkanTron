#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "video/display_layout.h"
#include "video/hud_layout.h"
#include "video/video.h"

static void fail(const char *message, int width, int height,
                 int layout, int viewport) {
  fprintf(stderr, "FAIL: %s at %dx%d layout=%d viewport=%d\n",
          message, width, height, layout, viewport);
  exit(EXIT_FAILURE);
}

static void initScreen(Visual *screen, int x, int y, int width, int height) {
  screen->win_id = 0;
  screen->w = width;
  screen->h = height;
  screen->vp_x = x;
  screen->vp_y = y;
  screen->vp_w = width;
  screen->vp_h = height;
  screen->onScreen = 1;
  screen->textures = NULL;
}

static void initReferenceDisplay(Visual *display, int layout, int viewport) {
  Visual screen;

  initScreen(&screen, 0, 0, 800, 600);
  DisplayLayout_Viewport(display, &screen,
                         vp_x[layout][viewport], vp_y[layout][viewport],
                         vp_w[layout][viewport], vp_h[layout][viewport]);
}

static void initTestDisplay(Visual *display, const Visual *screen,
                            int layout, int viewport) {
  DisplayLayout_Viewport(display, screen,
                         vp_x[layout][viewport], vp_y[layout][viewport],
                         vp_w[layout][viewport], vp_h[layout][viewport]);
}

static void initPaneCanvas(HudCanvas *canvas, const Visual *display,
                           const Visual *screen, int layout, int viewport) {
  Visual reference;

  initReferenceDisplay(&reference, layout, viewport);
  HudLayout_Canvas(canvas, display, screen,
                   (float)reference.vp_w, (float)reference.vp_h);
}

static void checkText(const HudTextPlacement *text, const HudCanvas *canvas,
                      const char *name, int width, int height,
                      int layout, int viewport) {
  if(text->size <= 0 || text->width <= 0 || text->height <= 0)
    fail(name, width, height, layout, viewport);
  if(text->x < 0 || text->y < 0 ||
     (float)(text->x + text->width) > canvas->width + 1.0f ||
     (float)(text->y + text->height) > canvas->height + 1.0f)
    fail("HUD text leaves its logical canvas", width, height,
         layout, viewport);
}

static void checkVisualInside(const Visual *inner, const Visual *outer,
                              const char *name, int width, int height,
                              int layout, int viewport) {
  if(inner->vp_x < outer->vp_x || inner->vp_y < outer->vp_y ||
     inner->vp_x + inner->vp_w > outer->vp_x + outer->vp_w ||
     inner->vp_y + inner->vp_h > outer->vp_y + outer->vp_h)
    fail(name, width, height, layout, viewport);
}

static void checkCanvas(const HudCanvas *canvas, const Visual *display,
                        int reference_width, int reference_height,
                        int width, int height, int layout, int viewport) {
  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0 ||
     canvas->scale <= 0.0f)
    fail("HUD canvas is empty", width, height, layout, viewport);
  if(fabsf(canvas->width - (float)reference_width) > 0.001f ||
     fabsf(canvas->height - (float)reference_height) > 0.001f)
    fail("HUD logical dimensions changed", width, height, layout, viewport);
  checkVisualInside(&canvas->viewport, display,
                    "HUD canvas leaves its player viewport",
                    width, height, layout, viewport);
  if(abs((canvas->viewport.vp_x - display->vp_x) -
         (display->vp_x + display->vp_w - canvas->viewport.vp_x -
          canvas->viewport.vp_w)) > 1 ||
     abs((canvas->viewport.vp_y - display->vp_y) -
         (display->vp_y + display->vp_h - canvas->viewport.vp_y -
          canvas->viewport.vp_h)) > 1)
    fail("HUD canvas is not centered", width, height, layout, viewport);
  if(fabsf((float)canvas->viewport.vp_w - canvas->width * canvas->scale) >
       0.501f ||
     fabsf((float)canvas->viewport.vp_h - canvas->height * canvas->scale) >
       0.501f)
    fail("HUD canvas is not uniformly scaled", width, height,
         layout, viewport);
}

static void testClassicGoldens(void) {
  static const int expected_ai_x[3] = { 200, 187, 87 };
  static const int expected_ai_size[3] = { 26, 25, 11 };
  static const int expected_map_x[3] = { 20, 45, 45 };
  static const int expected_map_y[3] = { 20, 32, 45 };
  static const int expected_map_w[3] = { 266, 249, 116 };
  static const int expected_map_h[3] = { 199, 95, 87 };
  Visual screen;
  HudCanvas root;
  HudTextPlacement text;
  int layout;

  initScreen(&screen, 0, 0, 800, 600);
  HudLayout_RootCanvas(&root, &screen);
  if(root.viewport.vp_x != 0 || root.viewport.vp_y != 0 ||
     root.viewport.vp_w != 800 || root.viewport.vp_h != 600 ||
     fabsf(root.scale - 1.0f) > 0.0001f)
    fail("800x600 root HUD canvas changed", 800, 600, -1, 0);

  HudLayout_Banner(&text, &root, 14);
  if(text.x != 133 || text.y != 20 || text.size != 38)
    fail("classic pause placement changed", 800, 600, -1, 0);
  HudLayout_Banner(&text, &root, 12);
  if(text.x != 133 || text.y != 20 || text.size != 44)
    fail("classic no-winner placement changed", 800, 600, -1, 0);

  HudLayout_FpsLine(&text, &root, 0, 16);
  if(text.x != 620 || text.y != 580 || text.size != 10)
    fail("classic first FPS line changed", 800, 600, -1, 0);
  HudLayout_FpsLine(&text, &root, 1, 16);
  if(text.x != 620 || text.y != 565 || text.size != 10)
    fail("classic second FPS line changed", 800, 600, -1, 0);
  HudLayout_FpsLine(&text, &root, 2, 16);
  if(text.x != 620 || text.y != 550 || text.size != 10)
    fail("classic third FPS line changed", 800, 600, -1, 0);

  HudLayout_ConsoleLine(&text, &root, 0, 12);
  if(text.x != 20 || text.y != 580 || text.size != 15)
    fail("classic console placement changed", 800, 600, -1, 0);
  HudLayout_ConsoleLine(&text, &root, 0, 79);
  if(text.size != 4)
    fail("classic long console fitting changed", 800, 600, -1, 0);
  if(HudLayout_ConsoleLineCount(&root, 0) != 5 ||
     HudLayout_ConsoleLineCount(&root, 1) != 1)
    fail("classic console line count changed", 800, 600, -1, 0);

  HudLayout_CreditLine(&text, &root, 0, 10);
  if(text.x != 10 || text.y != 576 || text.size != 16)
    fail("classic credit placement changed", 800, 600, -1, 0);

  for(layout = 0; layout < 3; layout++) {
    Visual display;
    Visual reference;
    Visual minimap;
    HudCanvas canvas;

    initTestDisplay(&display, &screen, layout, 0);
    initReferenceDisplay(&reference, layout, 0);
    initPaneCanvas(&canvas, &display, &screen, layout, 0);
    if(canvas.viewport.vp_x != display.vp_x ||
       canvas.viewport.vp_y != display.vp_y ||
       canvas.viewport.vp_w != display.vp_w ||
       canvas.viewport.vp_h != display.vp_h)
      fail("800x600 player HUD canvas changed", 800, 600, layout, 0);

    HudLayout_Score(&text, &canvas, 1);
    if(text.x != 5 || text.y != 5 || text.size != 32)
      fail("classic score placement changed", 800, 600, layout, 0);
    HudLayout_AI(&text, &canvas, 15);
    if(text.x != expected_ai_x[layout] ||
       text.y != 10 || text.size != expected_ai_size[layout])
      fail("classic AI placement changed", 800, 600, layout, 0);
    HudLayout_Minimap(&minimap, &canvas, 0.333f, 0.333f);
    if(minimap.vp_x != expected_map_x[layout] ||
       minimap.vp_y != expected_map_y[layout] ||
       minimap.vp_w != expected_map_w[layout] ||
       minimap.vp_h != expected_map_h[layout])
      fail("classic minimap placement changed", 800, 600, layout, 0);
    checkVisualInside(&minimap, &reference,
                      "classic minimap leaves reference pane",
                      800, 600, layout, 0);
  }
}

static void testResolution(int width, int height) {
  static const float ratios[][2] = {
    { 0.333f, 0.333f }, { 0.5f, 0.5f }, { 0.9f, 0.9f },
    { 1.0f, 1.0f }, { 1.5f, 2.0f },
    { 0.9f, 0.2f }, { 0.2f, 0.9f }
  };
  Visual screen;
  HudCanvas root;
  HudTextPlacement text;
  int layout;

  initScreen(&screen, 0, 0, width, height);
  HudLayout_RootCanvas(&root, &screen);
  checkCanvas(&root, &screen, 800, 600, width, height, -1, 0);

  HudLayout_Banner(&text, &root, 14);
  checkText(&text, &root, "invalid pause placement",
            width, height, -1, 0);
  HudLayout_FpsLine(&text, &root, 0, 16);
  checkText(&text, &root, "invalid FPS placement",
            width, height, -1, 0);
  HudLayout_ConsoleLine(&text, &root, 0, 79);
  checkText(&text, &root, "invalid console placement",
            width, height, -1, 0);
  HudLayout_CreditLine(&text, &root, 1, 38);
  checkText(&text, &root, "invalid credit placement",
            width, height, -1, 0);

  for(layout = 0; layout < 3; layout++) {
    int viewport;

    for(viewport = 0; viewport < vp_max[layout]; viewport++) {
      Visual display;
      Visual reference;
      HudCanvas canvas;
      unsigned int ratio;

      initTestDisplay(&display, &screen, layout, viewport);
      initReferenceDisplay(&reference, layout, viewport);
      initPaneCanvas(&canvas, &display, &screen, layout, viewport);
      checkCanvas(&canvas, &display, reference.vp_w, reference.vp_h,
                  width, height, layout, viewport);

      HudLayout_Score(&text, &canvas, 3);
      checkText(&text, &canvas, "invalid score placement",
                width, height, layout, viewport);
      HudLayout_AI(&text, &canvas, 15);
      checkText(&text, &canvas, "invalid AI placement",
                width, height, layout, viewport);

      for(ratio = 0; ratio < sizeof(ratios) / sizeof(ratios[0]); ratio++) {
        Visual minimap;
        HudLayout_Minimap(&minimap, &canvas,
                          ratios[ratio][0], ratios[ratio][1]);
        if(minimap.vp_w <= 0 || minimap.vp_h <= 0)
          fail("minimap unexpectedly empty", width, height,
               layout, viewport);
        checkVisualInside(&minimap, &canvas.viewport,
                          "minimap leaves HUD canvas",
                          width, height, layout, viewport);
        checkVisualInside(&minimap, &display,
                          "minimap intrudes outside player viewport",
                          width, height, layout, viewport);
      }
    }
  }
}

static void testGeometryGoldens(void) {
  static const int resolutions[][2] = {
    { 1280, 720 }, { 1920, 1080 }, { 1080, 1920 }
  };
  static const int expected_root[][4] = {
    { 160, 0, 960, 720 }, { 240, 0, 1440, 1080 },
    { 0, 555, 1080, 810 }
  };
  static const int expected_split[][4] = {
    { 191, 15, 898, 345 }, { 285, 22, 1349, 518 },
    { 33, 305, 1013, 389 }
  };
  static const int expected_four[][4] = {
    { 110, 30, 420, 314 }, { 165, 45, 630, 472 },
    { 33, 323, 473, 354 }
  };
  unsigned int i;

  for(i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++) {
    Visual screen;
    Visual display;
    HudCanvas canvas;

    initScreen(&screen, 0, 0, resolutions[i][0], resolutions[i][1]);
    HudLayout_RootCanvas(&canvas, &screen);
    if(canvas.viewport.vp_x != expected_root[i][0] ||
       canvas.viewport.vp_y != expected_root[i][1] ||
       canvas.viewport.vp_w != expected_root[i][2] ||
       canvas.viewport.vp_h != expected_root[i][3])
      fail("root HUD geometry golden changed", resolutions[i][0],
           resolutions[i][1], -1, 0);

    initTestDisplay(&display, &screen, VP_SPLIT, 0);
    initPaneCanvas(&canvas, &display, &screen, VP_SPLIT, 0);
    if(canvas.viewport.vp_x != expected_split[i][0] ||
       canvas.viewport.vp_y != expected_split[i][1] ||
       canvas.viewport.vp_w != expected_split[i][2] ||
       canvas.viewport.vp_h != expected_split[i][3])
      fail("split HUD geometry golden changed", resolutions[i][0],
           resolutions[i][1], VP_SPLIT, 0);

    initTestDisplay(&display, &screen, VP_FOURWAY, 0);
    initPaneCanvas(&canvas, &display, &screen, VP_FOURWAY, 0);
    if(canvas.viewport.vp_x != expected_four[i][0] ||
       canvas.viewport.vp_y != expected_four[i][1] ||
       canvas.viewport.vp_w != expected_four[i][2] ||
       canvas.viewport.vp_h != expected_four[i][3])
      fail("four-way HUD geometry golden changed", resolutions[i][0],
           resolutions[i][1], VP_FOURWAY, 0);
  }
}

static void testTranslatedOrigin(void) {
  Visual base_screen;
  Visual offset_screen;
  Visual base_display;
  Visual offset_display;
  Visual base_map;
  Visual offset_map;
  HudCanvas base_canvas;
  HudCanvas offset_canvas;
  HudTextPlacement base_text;
  HudTextPlacement offset_text;

  initScreen(&base_screen, 0, 0, 1280, 720);
  initScreen(&offset_screen, 120, 80, 1280, 720);
  initTestDisplay(&base_display, &base_screen, VP_FOURWAY, 3);
  initTestDisplay(&offset_display, &offset_screen, VP_FOURWAY, 3);
  initPaneCanvas(&base_canvas, &base_display, &base_screen, VP_FOURWAY, 3);
  initPaneCanvas(&offset_canvas, &offset_display, &offset_screen,
                 VP_FOURWAY, 3);

  HudLayout_AI(&base_text, &base_canvas, 15);
  HudLayout_AI(&offset_text, &offset_canvas, 15);
  if(base_text.x != offset_text.x || base_text.y != offset_text.y ||
     base_text.size != offset_text.size)
    fail("translated origin changed local HUD text", 1280, 720,
         VP_FOURWAY, 3);

  HudLayout_Minimap(&base_map, &base_canvas, 0.5f, 0.5f);
  HudLayout_Minimap(&offset_map, &offset_canvas, 0.5f, 0.5f);
  if(offset_map.vp_x - base_map.vp_x != 120 ||
     offset_map.vp_y - base_map.vp_y != 80 ||
     offset_map.vp_w != base_map.vp_w || offset_map.vp_h != base_map.vp_h)
    fail("translated origin did not translate minimap", 1280, 720,
         VP_FOURWAY, 3);
}

static void testDegenerateInputs(void) {
  Visual empty;
  Visual minimap;
  Visual capped;
  HudCanvas canvas;
  HudTextPlacement text;

  initScreen(&empty, 7, 9, 0, 0);
  HudLayout_RootCanvas(&canvas, &empty);
  if(canvas.viewport.vp_x != 7 || canvas.viewport.vp_y != 9 ||
     canvas.viewport.vp_w != 0 || canvas.viewport.vp_h != 0 ||
     canvas.scale != 0.0f)
    fail("empty screen did not produce empty HUD canvas", 0, 0, -1, 0);
  HudLayout_Banner(&text, &canvas, 14);
  if(text.size != 0 || text.width != 0 || text.height != 0)
    fail("empty canvas produced text", 0, 0, -1, 0);
  HudLayout_Minimap(&minimap, &canvas, 0.9f, 0.9f);
  if(minimap.vp_w != 0 || minimap.vp_h != 0)
    fail("empty canvas produced minimap", 0, 0, -1, 0);

  initScreen(&empty, 0, 0, 800, 600);
  HudLayout_RootCanvas(&canvas, &empty);
  HudLayout_Minimap(&minimap, &canvas, 0.0f, 0.5f);
  if(minimap.vp_w != 0 || minimap.vp_h != 0)
    fail("zero-width ratio produced minimap", 800, 600, -1, 0);
  HudLayout_Minimap(&minimap, &canvas, 0.5f, -0.1f);
  if(minimap.vp_w != 0 || minimap.vp_h != 0)
    fail("negative-height ratio produced minimap", 800, 600, -1, 0);
  HudLayout_Minimap(&minimap, &canvas, 1.0f, 1.0f);
  HudLayout_Minimap(&capped, &canvas, 1.5f, 2.0f);
  if(capped.vp_x != minimap.vp_x || capped.vp_y != minimap.vp_y ||
     capped.vp_w != minimap.vp_w || capped.vp_h != minimap.vp_h)
    fail("oversized minimap ratios did not cap at one", 800, 600, -1, 0);

  initScreen(&empty, 0, 0, 320, 240);
  HudLayout_RootCanvas(&canvas, &empty);
  if(HudLayout_ConsoleLineCount(&canvas, 0) != 3)
    fail("low-resolution console line count changed", 320, 240, -1, 0);
}

int main(void) {
  static const int resolutions[][2] = {
    { 320, 240 }, { 512, 384 }, { 640, 480 }, { 800, 600 },
    { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 },
    { 3440, 1440 }, { 1080, 1920 }
  };
  unsigned int i;

  testClassicGoldens();
  for(i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++)
    testResolution(resolutions[i][0], resolutions[i][1]);
  testGeometryGoldens();
  testTranslatedOrigin();
  testDegenerateInputs();

  printf("PASS: classic HUD parity and bounded uniform scaling across "
         "single, split, four-way, widescreen, ultrawide, and portrait layouts\n");
  return EXIT_SUCCESS;
}
