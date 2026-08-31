#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "video/display_layout.h"
#include "video/video.h"

/* Independent half-grid specifications for the classic production layouts. */
static const int expected_x2[3][4] = {
  { 0, 0, 0, 0 },
  { 2, 2, 0, 0 },
  { 2, 32, 2, 32 }
};

static const int expected_y2[3][4] = {
  { 0, 0, 0, 0 },
  { 1, 25, 0, 0 },
  { 2, 2, 25, 25 }
};

static const int expected_w2[3][4] = {
  { 64, 0, 0, 0 },
  { 60, 60, 0, 0 },
  { 28, 28, 28, 28 }
};

static const int expected_h2[3][4] = {
  { 48, 0, 0, 0 },
  { 23, 23, 0, 0 },
  { 21, 21, 21, 21 }
};

static const int expected_count[3] = { 1, 2, 4 };

static void fail(const char *message, int width, int height,
                 int layout, int viewport) {
  fprintf(stderr, "FAIL: %s at %dx%d layout=%d viewport=%d\n",
          message, width, height, layout, viewport);
  exit(EXIT_FAILURE);
}

static void testResolution(int width, int height) {
  Visual screen;
  Visual gui;
  int layout;

  screen.w = width;
  screen.h = height;
  screen.vp_x = 0;
  screen.vp_y = 0;
  screen.vp_w = width;
  screen.vp_h = height;

  DisplayLayout_AspectFit(&gui, &screen, 4.0f / 3.0f);
  if(gui.vp_w <= 0 || gui.vp_h <= 0 ||
     gui.vp_x < 0 || gui.vp_y < 0 ||
     gui.vp_x + gui.vp_w > width || gui.vp_y + gui.vp_h > height)
    fail("4:3 GUI safe canvas is outside framebuffer", width, height, -1, 0);
  if(fabsf(DisplayLayout_Aspect(&gui) - (4.0f / 3.0f)) > 0.002f)
    fail("4:3 GUI safe canvas has wrong aspect", width, height, -1, 0);
  if(abs((gui.vp_x - screen.vp_x) -
         (screen.vp_x + screen.vp_w - gui.vp_x - gui.vp_w)) > 1 ||
     abs((gui.vp_y - screen.vp_y) -
         (screen.vp_y + screen.vp_h - gui.vp_y - gui.vp_h)) > 1)
    fail("4:3 GUI safe canvas is not centered", width, height, -1, 0);

  for(layout = 0; layout < 3; layout++) {
    Visual displays[4] = { { 0 } };
    int viewport;
    int first;
    int second;

    if(vp_max[layout] != expected_count[layout])
      fail("production viewport count changed", width, height, layout, -1);

    for(viewport = 0; viewport < vp_max[layout]; viewport++) {
      Visual *display = &displays[viewport];
      int expected_left = screen.vp_x +
        expected_x2[layout][viewport] * screen.vp_w / 64;
      int expected_bottom = screen.vp_y +
        expected_y2[layout][viewport] * screen.vp_h / 48;
      int expected_right = screen.vp_x +
        (expected_x2[layout][viewport] + expected_w2[layout][viewport]) *
        screen.vp_w / 64;
      int expected_top = screen.vp_y +
        (expected_y2[layout][viewport] + expected_h2[layout][viewport]) *
        screen.vp_h / 48;

      DisplayLayout_Viewport(display, &screen,
                             vp_x[layout][viewport],
                             vp_y[layout][viewport],
                             vp_w[layout][viewport],
                             vp_h[layout][viewport]);

      if(display->vp_x != expected_left || display->vp_y != expected_bottom ||
         display->vp_x + display->vp_w != expected_right ||
         display->vp_y + display->vp_h != expected_top)
        fail("viewport edges differ from classic topology",
             width, height, layout, viewport);
      if(display->vp_w <= 0 || display->vp_h <= 0)
        fail("non-positive viewport", width, height, layout, viewport);
      if(display->vp_x < screen.vp_x || display->vp_y < screen.vp_y)
        fail("viewport begins outside framebuffer", width, height,
             layout, viewport);
      if(display->vp_x + display->vp_w > screen.vp_x + screen.vp_w ||
         display->vp_y + display->vp_h > screen.vp_y + screen.vp_h)
        fail("viewport ends outside framebuffer", width, height,
             layout, viewport);
      if(!isfinite(DisplayLayout_Aspect(display)) ||
         DisplayLayout_Aspect(display) <= 0.0f)
        fail("invalid viewport aspect", width, height, layout, viewport);

      if(layout == 0 &&
         (display->vp_x != 0 || display->vp_y != 0 ||
          display->vp_w != width || display->vp_h != height))
        fail("single-player viewport does not fill framebuffer",
             width, height, layout, viewport);
    }

    for(first = 0; first < vp_max[layout]; first++) {
      for(second = first + 1; second < vp_max[layout]; second++) {
        Visual *a = &displays[first];
        Visual *b = &displays[second];
        int overlap_x = a->vp_x < b->vp_x + b->vp_w &&
                        b->vp_x < a->vp_x + a->vp_w;
        int overlap_y = a->vp_y < b->vp_y + b->vp_h &&
                        b->vp_y < a->vp_y + a->vp_h;
        if(overlap_x && overlap_y)
          fail("production viewports overlap", width, height, layout, second);
      }
    }

    if(layout == VP_SPLIT &&
       displays[0].vp_y + displays[0].vp_h > displays[1].vp_y)
      fail("split viewports are not ordered bottom to top",
           width, height, layout, 1);
    if(layout == VP_FOURWAY &&
       (displays[0].vp_x + displays[0].vp_w > displays[1].vp_x ||
        displays[2].vp_x + displays[2].vp_w > displays[3].vp_x ||
        displays[0].vp_y + displays[0].vp_h > displays[2].vp_y ||
        displays[1].vp_y + displays[1].vp_h > displays[3].vp_y))
      fail("four-way viewports are not in classic quadrant order",
           width, height, layout, 0);
  }
}

int main(void) {
  static const int resolutions[][2] = {
    { 320, 240 },
    { 800, 600 },
    { 1280, 720 },
    { 1600, 900 },
    { 1920, 1080 },
    { 2560, 1440 },
    { 3440, 1440 },
    { 1080, 1920 }
  };
  Visual classic_screen = { 0 };
  Visual classic_split = { 0 };
  Visual widescreen = { 0 };
  Visual widescreen_view = { 0 };
  Visual offset_screen = { 0 };
  Visual offset_view = { 0 };
  Visual empty_screen = { 0 };
  Visual empty_view = { 0 };
  unsigned int i;

  for(i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++)
    testResolution(resolutions[i][0], resolutions[i][1]);

  classic_screen.w = classic_screen.vp_w = 800;
  classic_screen.h = classic_screen.vp_h = 600;
  DisplayLayout_Viewport(&classic_split, &classic_screen,
                         1.0f, 0.5f, 30.0f, 11.5f);
  if(classic_split.vp_x != 25 || classic_split.vp_y != 12 ||
     classic_split.vp_w != 750 || classic_split.vp_h != 288)
    fail("800x600 split layout does not match remaster baseline",
         800, 600, 1, 0);

  widescreen.w = widescreen.vp_w = 1920;
  widescreen.h = widescreen.vp_h = 1080;
  DisplayLayout_Viewport(&widescreen_view, &widescreen,
                         0.0f, 0.0f, 32.0f, 24.0f);
  if(fabsf(DisplayLayout_Aspect(&widescreen_view) - (16.0f / 9.0f)) > 0.0001f)
    fail("1920x1080 aspect is not 16:9", 1920, 1080, 0, 0);

  offset_screen.w = 1920;
  offset_screen.h = 1080;
  offset_screen.vp_x = 120;
  offset_screen.vp_y = 80;
  offset_screen.vp_w = 1280;
  offset_screen.vp_h = 720;
  DisplayLayout_Viewport(&offset_view, &offset_screen,
                         0.0f, 0.0f, 32.0f, 24.0f);
  if(offset_view.vp_x != 120 || offset_view.vp_y != 80 ||
     offset_view.vp_w != 1280 || offset_view.vp_h != 720)
    fail("viewport origin was not preserved", 1280, 720, 0, 0);
  DisplayLayout_AspectFit(&offset_view, &offset_screen, 4.0f / 3.0f);
  if(offset_view.vp_x != 280 || offset_view.vp_y != 80 ||
     offset_view.vp_w != 960 || offset_view.vp_h != 720)
    fail("offset 4:3 GUI safe canvas is incorrect", 1280, 720, -1, 0);

  empty_screen.vp_x = 7;
  empty_screen.vp_y = 9;
  DisplayLayout_AspectFit(&empty_view, &empty_screen, 4.0f / 3.0f);
  if(empty_view.vp_x != 7 || empty_view.vp_y != 9 ||
     empty_view.vp_w != 0 || empty_view.vp_h != 0)
    fail("empty framebuffer was not handled safely", 0, 0, -1, 0);

  printf("PASS: aspect-correct single, split, and four-way layouts at "
         "4:3, 16:9, ultrawide, and portrait resolutions\n");
  return EXIT_SUCCESS;
}
