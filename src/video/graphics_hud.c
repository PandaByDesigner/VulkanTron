#include <stdio.h>
#include <string.h>

#include "video/video.h"
#include "game/game.h"

static HudCanvas consoleCanvas;

void drawAI(const HudCanvas *canvas) {
  char ai[] = "computer player";
  HudTextPlacement placement;

  HudLayout_AI(&placement, canvas, strlen(ai));
  if(placement.size <= 0)
    return;
  rasonlyHud(canvas);
  glColor3f(1.0, 1.0, 1.0);
  drawText(gameFtx, placement.x, placement.y, placement.size, ai);
  /* glRasterPos2i(100, 0); */
}

void drawPause(Visual *display) {
  char pause[] = "Game is paused";
  char winner[] = "Player %d wins!";
  char nowinner[] = "No one wins!";
  char buf[100];
  char *message;
  static float d = 0;
  static float lt = 0;
  float delta;
  int now;
  HudCanvas canvas;
  HudTextPlacement placement;

  now = SystemGetElapsedTime();
  delta = now - lt;
  lt = now;
  delta /= 500.0;
  d += delta;
  /* printf("%.5f\n", delta); */
  
  if (d > 2 * PI) { 
    d -= 2 * PI;
  }

  if ((game->pauseflag & PAUSE_GAME_FINISHED) && game->winner != -1) {
    if (game->winner >= -1) {

      float* player_color = gPlayerVisuals[game->winner].pColorAlpha;

      /* 
         make the 'Player wins' message oscillate between 
         white and the winning bike's color 
       */
      glColor3f((player_color[0] + ((sinf(d) + 1) / 2) * (1 - player_color[0])),
                (player_color[1] + ((sinf(d) + 1) / 2) * (1 - player_color[1])),
                (player_color[2] + ((sinf(d) + 1) / 2) * (1 - player_color[2]))); 

      message = buf;
      sprintf(message, winner, game->winner + 1);
    } else {
      glColor3d(1.0, (sin(d) + 1) / 2, (sin(d) + 1) / 2);
      message = nowinner;
    }
  } else {
    glColor3d(1.0, (sin(d) + 1) / 2, (sin(d) + 1) / 2);
    message = pause;
  }

  HudLayout_Canvas(&canvas, display, gScreen, HUD_LAYOUT_REFERENCE_WIDTH,
                   HUD_LAYOUT_REFERENCE_HEIGHT);
  HudLayout_Banner(&placement, &canvas, strlen(message));
  if(placement.size <= 0)
    return;
  rasonlyHud(&canvas);
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  drawText(gameFtx, placement.x, placement.y, placement.size, message);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}

void drawScore(Player *p, const HudCanvas *canvas) {
  char tmp[10]; /* hey, they won't reach such a score */
  HudTextPlacement placement;

  sprintf(tmp, "%d", p->data->score);
  HudLayout_Score(&placement, canvas, strlen(tmp));
  if(placement.size <= 0)
    return;
  rasonlyHud(canvas);
  glColor4f(1.0, 1.0, 0.2f, 1.0);
  if(gSettingsCache.obsidian_arena)
    glColor4f(.68f, .90f, .97f, 1);
  drawText(gameFtx, placement.x, placement.y, placement.size, tmp);
}

  
void drawFPS(Visual *d) {
#define FPS_HSIZE 20
  /* draws FPS in upper right corner of Display d */
  static int fps_h[FPS_HSIZE];
  static int pos = -FPS_HSIZE;
  static int fps_min = 0;
  static int fps_avg = 0;

  char tmp[20];
  int diff;
  HudCanvas canvas;
  HudTextPlacement placement;

  HudLayout_Canvas(&canvas, d, gScreen, HUD_LAYOUT_REFERENCE_WIDTH,
                   HUD_LAYOUT_REFERENCE_HEIGHT);
  if(canvas.viewport.vp_w <= 0 || canvas.viewport.vp_h <= 0)
    return;
  rasonlyHud(&canvas);
  diff = (game2->time.dt > 0) ? game2->time.dt : 1;

  if(pos < 0) {
    fps_avg = 1000 / diff;
    fps_min = 1000 / diff;
    fps_h[pos + FPS_HSIZE] = 1000 / diff;
    pos++;
  } else {
    fps_h[pos] = 1000 / diff;
    pos = (pos + 1) % FPS_HSIZE;
    if(pos % 10 == 0) {
      int i;
      int sum = 0;
      int min = 1000;
      for(i = 0; i < FPS_HSIZE; i++) {
	sum += fps_h[i];
	if(fps_h[i] < min)
	  min = fps_h[i];
      }
      fps_min = min;
      fps_avg = sum / FPS_HSIZE;
    }
  }

  sprintf(tmp, "average FPS: %d", fps_avg);
  glColor4f(1.0, 0.4f, 0.2f, 1.0);
  HudLayout_FpsLine(&placement, &canvas, 0, strlen(tmp));
  drawText(gameFtx, placement.x, placement.y, placement.size, tmp);
  sprintf(tmp, "minimum FPS: %d", fps_min);
  HudLayout_FpsLine(&placement, &canvas, 1, strlen(tmp));
  drawText(gameFtx, placement.x, placement.y, placement.size, tmp);
  sprintf(tmp, "triangles: %d", polycount);
  HudLayout_FpsLine(&placement, &canvas, 2, strlen(tmp));
  drawText(gameFtx, placement.x, placement.y, placement.size, tmp);
}


void drawConsoleLines(char *line, int call) {
  int length;
  HudTextPlacement placement;
  /* fprintf(stdout, "%s\n", line); */
  length = strlen(line);
  HudLayout_ConsoleLine(&placement, &consoleCanvas, call, length);

  if(*line != 0 && placement.size > 0)
    drawText(gameFtx, placement.x, placement.y, placement.size, line);
}

void drawConsole(Visual *d) {
  int lines;
  HudLayout_Canvas(&consoleCanvas, d, gScreen, HUD_LAYOUT_REFERENCE_WIDTH,
                   HUD_LAYOUT_REFERENCE_HEIGHT);
  if(consoleCanvas.viewport.vp_w <= 0 || consoleCanvas.viewport.vp_h <= 0)
    return;
  rasonlyHud(&consoleCanvas);
  glColor3f(1.0, 0.3f, 0.3f);
  lines = HudLayout_ConsoleLineCount(&consoleCanvas,
                                     gSettingsCache.softwareRendering);
  consoleDisplay(drawConsoleLines, lines);
}
