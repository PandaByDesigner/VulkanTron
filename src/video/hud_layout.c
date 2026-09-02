#include "video/hud_layout.h"

static float minimum(float first, float second) {
  return first < second ? first : second;
}

static int roundPositive(float value) {
  if(value <= 0.0f)
    return 0;
  return (int)(value + 0.5f);
}

static int clampInt(int value, int minimum_value, int maximum_value) {
  if(value < minimum_value)
    return minimum_value;
  if(value > maximum_value)
    return maximum_value;
  return value;
}

static void clearTextPlacement(HudTextPlacement *placement) {
  placement->x = 0;
  placement->y = 0;
  placement->size = 0;
  placement->width = 0;
  placement->height = 0;
}

static void setTextPlacement(HudTextPlacement *placement, int x, int y,
                             int size, int text_length) {
  if(size <= 0 || text_length <= 0) {
    clearTextPlacement(placement);
    return;
  }

  placement->x = x;
  placement->y = y;
  placement->size = size;
  placement->width = size * text_length;
  placement->height = size;
}

void HudLayout_Canvas(HudCanvas *canvas, const Visual *display,
                      const Visual *screen, float reference_width,
                      float reference_height) {
  float root_scale_x;
  float root_scale_y;
  float display_scale_x;
  float display_scale_y;
  int width;
  int height;

  canvas->viewport = *display;
  canvas->scale = 0.0f;
  canvas->width = 0.0f;
  canvas->height = 0.0f;

  if(display->vp_w <= 0 || display->vp_h <= 0 ||
     screen->vp_w <= 0 || screen->vp_h <= 0 ||
     reference_width <= 0.0f || reference_height <= 0.0f) {
    canvas->viewport.vp_w = 0;
    canvas->viewport.vp_h = 0;
    return;
  }

  root_scale_x = (float)screen->vp_w / HUD_LAYOUT_REFERENCE_WIDTH;
  root_scale_y = (float)screen->vp_h / HUD_LAYOUT_REFERENCE_HEIGHT;
  display_scale_x = (float)display->vp_w / reference_width;
  display_scale_y = (float)display->vp_h / reference_height;
  canvas->scale = minimum(minimum(root_scale_x, root_scale_y),
                          minimum(display_scale_x, display_scale_y));
  if(canvas->scale <= 0.0f) {
    canvas->viewport.vp_w = 0;
    canvas->viewport.vp_h = 0;
    return;
  }

  canvas->width = reference_width;
  canvas->height = reference_height;
  width = roundPositive(canvas->width * canvas->scale);
  height = roundPositive(canvas->height * canvas->scale);
  width = clampInt(width, 0, display->vp_w);
  height = clampInt(height, 0, display->vp_h);

  canvas->viewport.vp_x = display->vp_x + (display->vp_w - width) / 2;
  canvas->viewport.vp_y = display->vp_y + (display->vp_h - height) / 2;
  canvas->viewport.vp_w = width;
  canvas->viewport.vp_h = height;
}

void HudLayout_RootCanvas(HudCanvas *canvas, const Visual *screen) {
  HudLayout_Canvas(canvas, screen, screen, HUD_LAYOUT_REFERENCE_WIDTH,
                   HUD_LAYOUT_REFERENCE_HEIGHT);
}

void HudLayout_Score(HudTextPlacement *placement, const HudCanvas *canvas,
                     int text_length) {
  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0) {
    clearTextPlacement(placement);
    return;
  }
  setTextPlacement(placement, 5, 5, 32, text_length);
}

void HudLayout_AI(HudTextPlacement *placement, const HudCanvas *canvas,
                  int text_length) {
  int size;

  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0 ||
     text_length <= 0) {
    clearTextPlacement(placement);
    return;
  }

  size = (int)(canvas->width / (2.0f * (float)text_length));
  setTextPlacement(placement, (int)(canvas->width / 4.0f), 10,
                   size, text_length);
}

void HudLayout_Banner(HudTextPlacement *placement, const HudCanvas *canvas,
                      int text_length) {
  int size;

  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0 ||
     text_length <= 0) {
    clearTextPlacement(placement);
    return;
  }

  size = (int)(canvas->width / (1.5f * (float)text_length));
  setTextPlacement(placement, (int)(canvas->width / 6.0f), 20,
                   size, text_length);
}

void HudLayout_FpsLine(HudTextPlacement *placement, const HudCanvas *canvas,
                       int line, int text_length) {
  int x;
  int y;

  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0 || line < 0) {
    clearTextPlacement(placement);
    return;
  }

  x = (int)canvas->width - 180;
  y = (int)canvas->height - 20 - 15 * line;
  if(x < 0)
    x = 0;
  if(y < 0)
    y = 0;
  setTextPlacement(placement, x, y, 10, text_length);
}

void HudLayout_ConsoleLine(HudTextPlacement *placement,
                           const HudCanvas *canvas, int line,
                           int text_length) {
  int available;
  int size = 15;
  int y;

  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0 ||
     line < 0 || text_length <= 0) {
    clearTextPlacement(placement);
    return;
  }

  available = (int)(canvas->width / 2.0f) - 20;
  while(size > 1 && text_length * size > available)
    size--;
  y = (int)canvas->height - 20 * (line + 1);
  if(y < 0)
    y = 0;
  setTextPlacement(placement, 20, y, size, text_length);
}

int HudLayout_ConsoleLineCount(const HudCanvas *canvas,
                               int software_rendering) {
  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0)
    return 0;
  if(software_rendering)
    return 1;
  if(canvas->viewport.vp_h < (int)HUD_LAYOUT_REFERENCE_HEIGHT)
    return 3;
  return 5;
}

void HudLayout_CreditLine(HudTextPlacement *placement,
                          const HudCanvas *canvas, int line,
                          int text_length) {
  int size;
  int y;

  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0 || line < 0) {
    clearTextPlacement(placement);
    return;
  }

  size = (int)canvas->height / (24 * 3 / 2);
  y = (int)canvas->height - 3 * size * (line + 1) / 2;
  if(y < 0)
    y = 0;
  setTextPlacement(placement, 10, y, size, text_length);
}

void HudLayout_Minimap(Visual *minimap, const HudCanvas *canvas,
                       float width_ratio, float height_ratio) {
  int margin;
  int width;
  int height;
  int available_width;
  int available_height;

  *minimap = canvas->viewport;
  minimap->vp_w = 0;
  minimap->vp_h = 0;

  if(canvas->viewport.vp_w <= 0 || canvas->viewport.vp_h <= 0 ||
     width_ratio <= 0.0f || height_ratio <= 0.0f)
    return;

  if(width_ratio > 1.0f)
    width_ratio = 1.0f;
  if(height_ratio > 1.0f)
    height_ratio = 1.0f;

  margin = roundPositive(20.0f * canvas->scale);
  margin = clampInt(margin, 0,
                    canvas->viewport.vp_w < canvas->viewport.vp_h ?
                    canvas->viewport.vp_w : canvas->viewport.vp_h);
  available_width = canvas->viewport.vp_w - margin;
  available_height = canvas->viewport.vp_h - margin;
  width = (int)(canvas->width * width_ratio * canvas->scale);
  height = (int)(canvas->height * height_ratio * canvas->scale);
  width = clampInt(width, 0, available_width);
  height = clampInt(height, 0, available_height);

  minimap->vp_x = canvas->viewport.vp_x + margin;
  minimap->vp_y = canvas->viewport.vp_y + margin;
  minimap->vp_w = width;
  minimap->vp_h = height;
}
