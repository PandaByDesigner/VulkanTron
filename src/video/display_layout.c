#include "video/display_layout.h"

static int clampToRange(int value, int minimum, int maximum) {
  if(value < minimum)
    return minimum;
  if(value > maximum)
    return maximum;
  return value;
}

void DisplayLayout_Viewport(Visual *display, const Visual *screen,
                            float x, float y, float width, float height) {
  float scale_x = screen->vp_w / DISPLAY_LAYOUT_WIDTH;
  float scale_y = screen->vp_h / DISPLAY_LAYOUT_HEIGHT;
  int screen_right = screen->vp_x + screen->vp_w;
  int screen_top = screen->vp_y + screen->vp_h;
  int left = screen->vp_x + (int)(x * scale_x);
  int bottom = screen->vp_y + (int)(y * scale_y);
  int right = screen->vp_x + (int)((x + width) * scale_x);
  int top = screen->vp_y + (int)((y + height) * scale_y);

  left = clampToRange(left, screen->vp_x, screen_right);
  bottom = clampToRange(bottom, screen->vp_y, screen_top);
  right = clampToRange(right, left, screen_right);
  top = clampToRange(top, bottom, screen_top);

  display->w = screen->w;
  display->h = screen->h;
  display->vp_x = left;
  display->vp_y = bottom;
  display->vp_w = right - left;
  display->vp_h = top - bottom;
}

void DisplayLayout_AspectFit(Visual *display, const Visual *screen,
                             float aspect) {
  int width = screen->vp_w;
  int height = screen->vp_h;

  display->w = screen->w;
  display->h = screen->h;
  if(width <= 0 || height <= 0) {
    display->vp_x = screen->vp_x;
    display->vp_y = screen->vp_y;
    display->vp_w = 0;
    display->vp_h = 0;
    return;
  }

  if(aspect <= 0.0f)
    aspect = 1.0f;

  if((float) width / (float) height > aspect)
    width = (int)(height * aspect);
  else
    height = (int)(width / aspect);

  display->vp_x = screen->vp_x + (screen->vp_w - width) / 2;
  display->vp_y = screen->vp_y + (screen->vp_h - height) / 2;
  display->vp_w = width;
  display->vp_h = height;
}

float DisplayLayout_Aspect(const Visual *display) {
  if(display->vp_h <= 0)
    return 1.0f;
  return (float) display->vp_w / (float) display->vp_h;
}
