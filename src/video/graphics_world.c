#include "video/video.h"
#include "game/game.h"

/* Original arena architecture. Everything stands outside the collision square;
 * it is scenery, not an invisible extension of the gameplay boundary. */
static void obsidianTower(float x, float y, float w, float h, int warm) {
  const float half = w * .5f;
  const float light[2][3] = {{.14f,.68f,.83f}, {.92f,.39f,.09f}};
  const float *color = light[warm];
  glBegin(GL_QUADS);
  glColor3f(.026f,.043f,.061f);
  glVertex3f(x-half,y-half,0); glVertex3f(x+half,y-half,0);
  glVertex3f(x+half,y-half,h); glVertex3f(x-half,y-half,h);
  glColor3f(.043f,.066f,.083f);
  glVertex3f(x+half,y-half,0); glVertex3f(x+half,y+half,0);
  glVertex3f(x+half,y+half,h); glVertex3f(x+half,y-half,h);
  glColor3f(.021f,.038f,.054f);
  glVertex3f(x+half,y+half,0); glVertex3f(x-half,y+half,0);
  glVertex3f(x-half,y+half,h); glVertex3f(x+half,y+half,h);
  glColor3f(.034f,.053f,.071f);
  glVertex3f(x-half,y+half,0); glVertex3f(x-half,y-half,0);
  glVertex3f(x-half,y-half,h); glVertex3f(x-half,y+half,h);
  glColor3f(.065f,.096f,.119f);
  glVertex3f(x-half,y-half,h); glVertex3f(x+half,y-half,h);
  glVertex3f(x+half,y+half,h); glVertex3f(x-half,y+half,h);
  /* Broad inset slots survive distance and subpixel sampling. */
  glColor3fv(color);
  /* Half a world unit separates the strips from their tower faces. A .02
   * offset loses the depth comparison at the arena's far viewing distances. */
  glVertex3f(x-half-.5f,y-w*.14f,h*.20f); glVertex3f(x-half-.5f,y-w*.07f,h*.20f);
  glVertex3f(x-half-.5f,y-w*.07f,h*.89f); glVertex3f(x-half-.5f,y-w*.14f,h*.89f);
  glVertex3f(x+half+.5f,y+w*.07f,h*.20f); glVertex3f(x+half+.5f,y+w*.14f,h*.20f);
  glVertex3f(x+half+.5f,y+w*.14f,h*.89f); glVertex3f(x+half+.5f,y+w*.07f,h*.89f);
  glVertex3f(x-w*.14f,y+half+.5f,h*.20f); glVertex3f(x-w*.07f,y+half+.5f,h*.20f);
  glVertex3f(x-w*.07f,y+half+.5f,h*.89f); glVertex3f(x-w*.14f,y+half+.5f,h*.89f);
  glVertex3f(x+w*.07f,y-half-.5f,h*.20f); glVertex3f(x+w*.14f,y-half-.5f,h*.20f);
  glVertex3f(x+w*.14f,y-half-.5f,h*.89f); glVertex3f(x+w*.07f,y-half-.5f,h*.89f);
  glEnd();
  polycount += 18;
}

static void drawObsidianWalls(void) {
  const float size = game2->rules.grid_size;
  const float scale = size / 720.0f;
  int side, i;
  static const float heights[9] = {140, 214, 116, 176, 270, 148, 224, 128, 180};
  glDisable(GL_LIGHTING);
  glDisable(GL_BLEND);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);
  for(side = 0; side < 4; side++) {
    glPushMatrix();
    glTranslatef(size*.5f,size*.5f,0);
    glRotatef(side*90.0f,0,0,1);
    glTranslatef(-size*.5f,-size*.5f,0);
    for(i = 0; i < 9; i++)
      obsidianTower((i+.5f)*size/9, -scale*(60+(i%3)*38),
                    scale*(27+(i%2)*14), heights[(i+side*2)%9]*scale, side%2);

    /* Tall slotted pylons frame an uninterrupted, readable arena boundary. */
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, gScreen->textures[TEX_WALL1+side]);
    glColor3f(1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex3f(0,0,0);
    glTexCoord2f(size/240,0); glVertex3f(size,0,0);
    glTexCoord2f(size/240,1); glVertex3f(size,0,16);
    glTexCoord2f(0,1); glVertex3f(0,0,16);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    /* A fine high-contrast top rail communicates the actual collision edge. */
    if(side%2) glColor3f(1,.54f,.19f);
    else glColor3f(.20f,.82f,.97f);
    glBegin(GL_QUADS);
    glVertex3f(0,-.08f,16); glVertex3f(size,-.08f,16);
    glVertex3f(size,.20f,16); glVertex3f(0,.20f,16);
    glVertex3f(0,0,.12f); glVertex3f(size,0,.12f);
    glVertex3f(size,.35f,.12f); glVertex3f(0,.35f,.12f);
    glEnd();
    glPopMatrix();
    polycount += 6;
  }
}

void drawWalls(void) {
#undef WALL_H
#define WALL_H 48
  float t;
  float h;

  if(gSettingsCache.obsidian_arena) {
    drawObsidianWalls();
    return;
  }

  t = game2->rules.grid_size / 240.0f;
  if (gSettingsCache.stretch_textures) {
    h = t * WALL_H;
    t = 1.0;
  } else h = WALL_H;

  glColor4f(1.0, 1.0, 1.0, 1.0);

  /*
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  */
  glEnable(GL_CULL_FACE);
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
#define T_TOP 1.0f
  glBindTexture(GL_TEXTURE_2D, gScreen->textures[TEX_WALL1]);
  glBegin(GL_QUADS);
  glTexCoord2f(t, 0.0); glVertex3f(0.0, 0.0, 0.0);
  glTexCoord2f(t, T_TOP); glVertex3f(0.0, 0.0, h);
  glTexCoord2f(0.0, T_TOP); glVertex3f(game2->rules.grid_size, 0.0, h);
  glTexCoord2f(0.0, 0.0); glVertex3f(game2->rules.grid_size, 0.0, 0.0);
  glEnd();
  
  glBindTexture(GL_TEXTURE_2D, gScreen->textures[TEX_WALL2]);
  glBegin(GL_QUADS);
  glTexCoord2f(t, 0.0); glVertex3f(game2->rules.grid_size, 0.0, 0.0);
  glTexCoord2f(t, T_TOP); glVertex3f(game2->rules.grid_size, 0.0, h);
  glTexCoord2f(0.0, T_TOP); 
  glVertex3f(game2->rules.grid_size, game2->rules.grid_size, h);
  glTexCoord2f(0.0, 0.0); 
  glVertex3f(game2->rules.grid_size, game2->rules.grid_size, 0.0);
  glEnd();
  
  glBindTexture(GL_TEXTURE_2D, gScreen->textures[TEX_WALL3]);
  glBegin (GL_QUADS);
  glTexCoord2f(t, 0.0); 
  glVertex3f(game2->rules.grid_size, game2->rules.grid_size, 0.0);
  glTexCoord2f(t, T_TOP); 
  glVertex3f(game2->rules.grid_size, game2->rules.grid_size, h);
  glTexCoord2f(0.0, T_TOP); glVertex3f(0.0, game2->rules.grid_size, h);
  glTexCoord2f(0.0, 0.0); glVertex3f(0.0, game2->rules.grid_size, 0.0);
  glEnd();
  
  glBindTexture(GL_TEXTURE_2D, gScreen->textures[TEX_WALL4]);
  glBegin(GL_QUADS);
  glTexCoord2f(t, 0.0); glVertex3f(0.0, game2->rules.grid_size, 0.0);
  glTexCoord2f(t, T_TOP); glVertex3f(0.0, game2->rules.grid_size, h);
  glTexCoord2f(0.0, T_TOP); glVertex3f(0.0, 0.0, h);
  glTexCoord2f(0.0, 0.0); glVertex3f(0.0, 0.0, 0.0); 
#undef T_TOP
  glEnd();
  polycount += 8;

  glDisable(GL_BLEND);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);
}

/*!
 * drawFloorTextured 
 *
 * \return number of polygons drawn
 *
 * Draws the arena floor covered with a repeating floor texture
 */

int drawFloorTextured(int grid_size, GLuint texture) {
  int i, j, l, t;

  if(gSettingsCache.obsidian_arena) {
    float repeats = grid_size / 48.0f;
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor4f(1,1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(0,0);
    glTexCoord2f(repeats,0); glVertex2f(grid_size,0);
    glTexCoord2f(repeats,repeats); glVertex2f(grid_size,grid_size);
    glTexCoord2f(0,repeats); glVertex2f(0,grid_size);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    polycount += 2;
    return 2;
  }
  
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture);

    /* there are some strange clipping artefacts on some renderers */
    /* try subdividing things... */

    glColor4f(1.0, 1.0, 1.0, 1.0);
    l = grid_size / 4;
    t = l / 12;
    
    //  glBegin(GL_QUADS);
    for (i = 0; i < grid_size; i += l) {
      glBegin(GL_QUADS);
      for (j = 0; j < grid_size; j += l) {
        glTexCoord2i(0, 0);
        glVertex2i(i, j);
        glTexCoord2i(t, 0);
        glVertex2i(i + l, j);
        glTexCoord2i(t, t);
        glVertex2i(i + l, j + l);
        glTexCoord2i(0, t);
        glVertex2i(i, j + l);
      }
      glEnd();
    }
    // glEnd();
   
    glDisable(GL_TEXTURE_2D);
    return grid_size * grid_size;
}

void drawFloorGrid(int grid_size,  int line_spacing, 
                   float line_color[3], float square_color[4]) {
  int i, j;

  glColor3fv(line_color);
  
  glFogfv(GL_FOG_COLOR, square_color);
  glFogi(GL_FOG_MODE, GL_LINEAR);
  glFogi(GL_FOG_START, 100);
  glFogi(GL_FOG_END, 350);

  glEnable(GL_FOG);

  glBegin(GL_LINES);
  for (i = 0; i < grid_size; i += line_spacing) {
    for (j = 0; j < grid_size; j += line_spacing) {
      glVertex3i(i, j, 0);
      glVertex3i(i + line_spacing, j, 0);
      glVertex3i(i, j, 0);
      glVertex3i(i, j + line_spacing, 0);
    }
  }
  glEnd();

  glDisable(GL_FOG);
}
