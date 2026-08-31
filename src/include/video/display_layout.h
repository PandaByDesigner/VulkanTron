#ifndef DISPLAY_LAYOUT_H
#define DISPLAY_LAYOUT_H

#include "video/nebu_video_types.h"

#define DISPLAY_LAYOUT_WIDTH 32.0f
#define DISPLAY_LAYOUT_HEIGHT 24.0f

void DisplayLayout_Viewport(Visual *display, const Visual *screen,
                            float x, float y, float width, float height);
void DisplayLayout_AspectFit(Visual *display, const Visual *screen,
                             float aspect);
float DisplayLayout_Aspect(const Visual *display);

#endif
