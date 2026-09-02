#ifndef HUD_LAYOUT_H
#define HUD_LAYOUT_H

#include "video/nebu_video_types.h"

#define HUD_LAYOUT_REFERENCE_WIDTH 800.0f
#define HUD_LAYOUT_REFERENCE_HEIGHT 600.0f

typedef struct {
  Visual viewport;
  float scale;
  float width;
  float height;
} HudCanvas;

typedef struct {
  int x;
  int y;
  int size;
  int width;
  int height;
} HudTextPlacement;

void HudLayout_Canvas(HudCanvas *canvas, const Visual *display,
                      const Visual *screen, float reference_width,
                      float reference_height);
void HudLayout_RootCanvas(HudCanvas *canvas, const Visual *screen);
void HudLayout_Score(HudTextPlacement *placement, const HudCanvas *canvas,
                     int text_length);
void HudLayout_AI(HudTextPlacement *placement, const HudCanvas *canvas,
                  int text_length);
void HudLayout_Banner(HudTextPlacement *placement, const HudCanvas *canvas,
                      int text_length);
void HudLayout_FpsLine(HudTextPlacement *placement, const HudCanvas *canvas,
                       int line, int text_length);
void HudLayout_ConsoleLine(HudTextPlacement *placement,
                           const HudCanvas *canvas, int line,
                           int text_length);
int HudLayout_ConsoleLineCount(const HudCanvas *canvas,
                               int software_rendering);
void HudLayout_CreditLine(HudTextPlacement *placement,
                          const HudCanvas *canvas, int line,
                          int text_length);
void HudLayout_Minimap(Visual *minimap, const HudCanvas *canvas,
                       float width_ratio, float height_ratio);

#endif
