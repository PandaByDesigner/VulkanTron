#include "video/video.h"
#include "video/display_layout.h"
#include "game/game.h"

#include "video/skybox.h"
#include "video/recognizer.h"
#include "video/explosion.h"

// static float arena[] = { 1.0, 1.2, 1, 0.0 };

#define MAX_LOD_LEVEL 3
static int lod_dist[MAX_LOD_LEVEL + 1][LC_LOD + 1] = { 
  { 1000, 1000, 1000 }, /* insane */
  { 100, 200, 400 }, /* high */
  { 30, 100, 200 }, /* low */
  { 10, 30, 150 } /* ugly */
};

/* spoke colors */
static float SpokeColor[4] = {1.0, 1.0, 1.0, 1.0};
static float NoSpokeColor[4] = {0.0, 0.0, 0.0, 1.0};

static void initHudCanvas(HudCanvas *canvas, const Visual *display,
                          int viewport) {
  Visual reference_screen = { 0 };
  Visual reference_display = { 0 };

  reference_screen.w = reference_screen.vp_w =
    (int)HUD_LAYOUT_REFERENCE_WIDTH;
  reference_screen.h = reference_screen.vp_h =
    (int)HUD_LAYOUT_REFERENCE_HEIGHT;
  DisplayLayout_Viewport(&reference_display, &reference_screen,
                         vp_x[gViewportType][viewport],
                         vp_y[gViewportType][viewport],
                         vp_w[gViewportType][viewport],
                         vp_h[gViewportType][viewport]);
  HudLayout_Canvas(canvas, display, gScreen,
                   (float)reference_display.vp_w,
                   (float)reference_display.vp_h);
}

static void drawMinimap(const HudCanvas *canvas) {
  Visual minimap;

  if(gSettingsCache.map_ratio_w <= 0 || gSettingsCache.map_ratio_h <= 0)
    return;

  HudLayout_Minimap(&minimap, canvas, gSettingsCache.map_ratio_w,
                    gSettingsCache.map_ratio_h);
  if(minimap.vp_w <= 0 || minimap.vp_h <= 0)
    return;

  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  draw2D(&minimap);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}

void drawGame(void) {
  GLint i;

  polycount = 0;

  glEnable(GL_DEPTH_TEST);

  glClearColor(gSettingsCache.clear_color[0], 
               gSettingsCache.clear_color[1], 
               gSettingsCache.clear_color[2],
               gSettingsCache.clear_color[3]);

  if(gSettingsCache.use_stencil) {
    glClearStencil(0);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
  } else {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
  }

  for(i = 0; i < vp_max[gViewportType]; i++) {
			Player *p = game->player + viewport_content[i];
			PlayerVisual *pV = gPlayerVisuals + viewport_content[i];
			Visual *d = & pV->display;
			HudCanvas hud;

    if(d->onScreen == 1) {
			initHudCanvas(&hud, d, i);
      glViewport(d->vp_x, d->vp_y, d->vp_w, d->vp_h);
				drawCam(p, pV);
#ifdef GLTRON_DIRECT_VULKAN
      if(gSettingsCache.obsidian_arena && gSettingsCache.show_glow)
        VT_BloomViewport(d->vp_x, d->vp_y, d->vp_w, d->vp_h, .28f);
#endif
				drawMinimap(&hud);
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      if (gSettingsCache.show_scores)
					drawScore(p, &hud);
      if (gSettingsCache.show_ai_status)
					if(p->ai->active == AI_COMPUTER)
						drawAI(&hud);
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
  }

  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  if (gSettingsCache.show_fps)
    drawFPS(gScreen);

		if(gSettingsCache.show_console)
			drawConsole(gScreen);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);

  /* printf("%d polys\n", polycount); */
}
 
float GetDistance(float *v, float *p, float *d) {
  float diff[3];
  float tmp[3];
  float t;
  vsub(v, p, diff);
  t = scalarprod(d, diff) / scalarprod(d, d);
  vcopy(d, tmp);
  vmul(tmp, t);
  vsub(diff, tmp, tmp);
  return sqrtf( scalarprod(tmp, tmp) );
}

static float dirangles[] = { 0, -90, -180, 90, 180, -270 };

float getDirAngle(int time, Player *p) {
  int last_dir;
  float dirAngle;

  if(time < TURN_LENGTH) {
    last_dir = p->data->last_dir;
    if(p->data->dir == 3 && last_dir == 2)
      last_dir = 4;
    if(p->data->dir == 2 && last_dir == 3)
      last_dir = 5;
    dirAngle = ((TURN_LENGTH - time) * dirangles[last_dir] +
		time * dirangles[p->data->dir]) / TURN_LENGTH;
  } else
    dirAngle = dirangles[p->data->dir];

  return dirAngle;
}

void doCycleTurnRotation(PlayerVisual *pV, Player *p) {
  int neigung_dir = -1;
	int time = game2->time.current - p->data->turn_time;
  float dirAngle = getDirAngle(time, p);

  glRotatef(dirAngle, 0, 0, 1);

#define neigung 25
  if(time < TURN_LENGTH && p->data->last_dir != p->data->dir) {
    float axis = 1.0f;
    if(p->data->dir < p->data->last_dir && p->data->last_dir != 3)
      axis = -1.0;
    else if((p->data->last_dir == 3 && p->data->dir == 2) ||
	    (p->data->last_dir == 0 && p->data->dir == 3))
      axis = -1.0;
    glRotated(neigung * sin(PI * time / TURN_LENGTH),
	      0.0, axis * neigung_dir, 0.0);
  }
#undef neigung
}

void drawCycleShadow(PlayerVisual *pV, Player *p, int lod, int drawTurn) {
  Mesh *cycle;
  int turn_time = game2->time.current - p->data->turn_time;
      
  if(turn_time < TURN_LENGTH && !drawTurn)
    return;

  if(pV->exp_radius != 0)
    return;

  cycle = lightcycle[lod];

  /* states */

  glEnable(GL_CULL_FACE);

  if(gSettingsCache.use_stencil) {
    glEnable(GL_STENCIL_TEST);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
    glStencilFunc(GL_GREATER, 1, 1);
    glEnable(GL_BLEND);
    glColor4fv(shadow_color);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glColor3f(0, 0, 0);
    glDisable(GL_BLEND);
  }

  /* transformations */

  glPushMatrix();
	{
		float x, y;
		getPositionFromData(&x, &y, p->data);
		glTranslatef(x,y, 0.0);
	}
  glMultMatrixf(shadow_matrix);
  if (gSettingsCache.turn_cycle) {
    doCycleTurnRotation(pV, p);
  } else if (pV->exp_radius == 0) {
    glRotatef(dirangles[p->data->dir], 0.0, 0.0, 1.0);
  }
  glTranslatef(0, 0, cycle->BBox.vSize.v[2] / 2);

  /* render */

  drawModel(cycle, TRI_MESH);

  /* restore */

  if(gSettingsCache.use_stencil)
    glDisable(GL_STENCIL_TEST);

  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glPopMatrix();
}

/* Emissive strips are distinct mesh material groups, so the wheel rings stay
 * bright without flattening the lighting on the graphite body. */
static void drawObsidianCycleMesh(Mesh *mesh, const PlayerVisual *visual,
                                  float reflection) {
  int material;
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_COLOR_MATERIAL);
  glVertexPointer(3, GL_FLOAT, 0, mesh->pVertices);
  glNormalPointer(GL_FLOAT, 0, mesh->pNormals);
  for(material = 0; material < mesh->nMaterials; material++) {
    Material *m = mesh->pMaterials + material;
    int light = strcmp(m->name, "Hull") == 0;
    if(reflection > 0 || light || !gSettingsCache.light_cycles) {
      const float *color = light ? visual->pColorDiffuse : m->diffuse;
      glDisable(GL_LIGHTING);
      glColor4f(color[0],color[1],color[2],reflection > 0 ? reflection : 1);
    } else {
      glEnable(GL_LIGHTING);
      glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,m->ambient);
      glMaterialfv(GL_FRONT_AND_BACK,GL_DIFFUSE,m->diffuse);
      glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,m->specular);
      glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,m->shininess);
    }
    glDrawElements(GL_TRIANGLES,3*mesh->pnFaces[material],GL_UNSIGNED_SHORT,
                   mesh->ppIndices[material]);
    polycount += mesh->pnFaces[material];
  }
  glDisableClientState(GL_VERTEX_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisable(GL_LIGHTING);
}

static void drawObsidianFragments(const PlayerVisual *visual) {
  const float radius = visual->exp_radius;
  const float fade = 1.0f - radius / EXP_RADIUS_MAX;
  const float *color = visual->pColorDiffuse;
  int i;
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE);
  glDepthMask(GL_FALSE);
  glBegin(GL_TRIANGLES);
  for(i = 0; i < 48; i++) {
    float angle = i * 2.399963f;
    float travel = radius * (.35f + (i%7)*.085f);
    float x = cosf(angle)*travel;
    float y = sinf(angle)*travel;
    float z = .3f + radius*(.45f+(i%5)*.10f) - radius*radius*.012f;
    float w = (.08f+(i%4)*.055f)*fade;
    float h = (.35f+(i%6)*.13f)*fade;
    glColor4f(color[0]+(1-color[0])*.45f,
              color[1]+(1-color[1])*.45f,
              color[2]+(1-color[2])*.45f,fade*.9f);
    glVertex3f(x-w,y,z); glVertex3f(x+w,y,z);
    glColor4f(color[0],color[1],color[2],0);
    glVertex3f(x+cosf(angle)*h,y+sinf(angle)*h,z+h);
  }
  glEnd();
  polycount += 48;
  glDepthMask(GL_TRUE);
}

void drawCycle(Player *p, PlayerVisual *pV, int lod, int drawTurn) {
  Mesh *cycle = lightcycle[lod];

  unsigned int spoke_time = game2->time.current - pV->spoke_time;
	int turn_time = game2->time.current - p->data->turn_time;

  if(turn_time < TURN_LENGTH && !drawTurn)
    return;
  
  glPushMatrix();
	{
		float x, y;
		getPositionFromData(&x, &y, p->data);
		glTranslatef(x, y, 0.0);
	}

  if (pV->exp_radius == 0 && gSettingsCache.turn_cycle == 0) {
    glRotatef(dirangles[p->data->dir], 0.0, 0.0, 1.0);
  }

  if (gSettingsCache.turn_cycle) { 
    doCycleTurnRotation(pV, p);
  }

	setupLights(eCycles);
	
  SetMaterialColor(cycle, "Hull", eDiffuse, pV->pColorDiffuse); 
  SetMaterialColor(cycle, "Hull", eSpecular, pV->pColorSpecular); 

  if (gSettingsCache.light_cycles) {
    glEnable(GL_LIGHTING);
  }

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);

  if (pV->exp_radius == 0) {
    glEnable(GL_NORMALIZE);

    glTranslatef(0, 0, cycle->BBox.vSize.v[2] / 2);

    /* draw spoke animation */
    if (spoke_time > 140 - (p->data->speed * 10) 
        && game->pauseflag == PAUSE_GAME_RUNNING) {
      if (pV->spoke_state == 1) {
        pV->spoke_state = 0;
        SetMaterialColor(cycle, "Spoke", eSpecular, SpokeColor);
        SetMaterialColor(cycle, "Spoke", eAmbient, SpokeColor);
      } else {
        pV->spoke_state = 1;
        SetMaterialColor(cycle, "Spoke", eSpecular, NoSpokeColor);
        SetMaterialColor(cycle, "Spoke", eAmbient, NoSpokeColor);
      }
      pV->spoke_time = game2->time.current;
    }
    
    glEnable(GL_CULL_FACE);
    if(gSettingsCache.obsidian_arena)
      drawObsidianCycleMesh(cycle, pV, 0);
    else
      drawModel(cycle, TRI_MESH);
    glDisable(GL_CULL_FACE);

  } else if(pV->exp_radius < EXP_RADIUS_MAX) {
   
    glEnable(GL_BLEND);

    if (gSettingsCache.show_impact) {
      drawImpact(pV);
    }

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glTranslatef(0, 0, cycle->BBox.vSize.v[2] / 2);

    if(gSettingsCache.obsidian_arena)
      drawObsidianFragments(pV);
    else
      drawModelExplosion(cycle, pV->exp_radius);
  }
  glDisable(GL_BLEND);
  glDisable(GL_LIGHTING);
  glPopMatrix();
}
 
int playerVisible(Player *eye, Player *target) {
  float v1[3];
  float v2[3];
  float tmp[3];
  float s;
  float d;
  int i;
  int lod_level;
	float x, y;

  vsub(eye->camera->target, eye->camera->cam, v1);
  normalize(v1);
	
	getPositionFromData(&x, &y, target->data);
	tmp[0] = x;
  tmp[1] = y;
  tmp[2] = 0;
	
  lod_level = (gSettingsCache.lod > MAX_LOD_LEVEL) ? 
    MAX_LOD_LEVEL : gSettingsCache.lod;

  /* calculate lod */
  vsub(eye->camera->cam, tmp, v2);
  d = length(v2);
  for(i = 0; i < LC_LOD && d >= lod_dist[lod_level][i]; i++);
  if(i >= LC_LOD)
    return -1;

  vsub(tmp, eye->camera->cam, v2);
  normalize(v2);
  s = scalarprod(v1, v2);
  /* maybe that's not exactly correct, but I didn't notice anything */
  d = cosf((gSettingsCache.fov / 2) * 2 * PI / 360.0);
  /*
    printf("v1: %.2f %.2f %.2f\nv2: %.2f %.2f %.2f\ns: %.2f d: %.2f\n\n",
    v1[0], v1[1], v1[2], v2[0], v2[1], v2[2],
    s, d);
  */
  if(s < d-(lightcycle[i]->BBox.fRadius*2))
    return -1;
  else
    return i;
}

void drawPlayers(Player *p, PlayerVisual *pV) {
  int i;

  for(i = 0; i < game->players; i++) {
		int lod;
		int drawTurn = 1;

		if (gSettingsCache.camType == CAM_TYPE_COCKPIT && 
				p == &game->player[i])
			drawTurn = 0;

		lod = playerVisible(p, &(game->player[i]));
		if (lod >= 0) { 
			drawCycle(game->player + i, gPlayerVisuals + i, lod, drawTurn);
		}
	}
}

/* Deliberate graphic reflections, drawn onto the floor before opaque scenery.
 * They are shortened and dimmed, avoiding a second bright field of obstacles.
 * No simulation, camera, input, or trail-state data is changed. */
static void drawObsidianReflections(Player *eye) {
  int i;
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  for(i = 0; i < game->players; i++) {
    Player *p = game->player+i;
    PlayerVisual *visual = gPlayerVisuals+i;
    int lod = playerVisible(eye,p);
    if(lod >= 0 && visual->exp_radius == 0) {
      float x,y;
      getPositionFromData(&x,&y,p->data);
      glPushMatrix();
      glScalef(1,1,-.56f);
      glTranslatef(x,y,lightcycle[lod]->BBox.vSize.v[2]/2);
      if(gSettingsCache.turn_cycle) doCycleTurnRotation(visual,p);
      else glRotatef(dirangles[p->data->dir],0,0,1);
      drawObsidianCycleMesh(lightcycle[lod],visual,.20f);
      glPopMatrix();
    }
    if(p->data->trail_height > 0) {
      int segment;
      const float *color = visual->pColorAlpha;
      glBegin(GL_QUADS);
      for(segment = 0; segment <= p->data->trailOffset; segment++) {
        segment2 *s = p->data->trails+segment;
        float x = s->vStart.v[0], y = s->vStart.v[1];
        float end_x = x+s->vDirection.v[0], end_y = y+s->vDirection.v[1];
        if(segment == p->data->trailOffset) {
          end_x = getSegmentEndX(p->data,0);
          end_y = getSegmentEndY(p->data,0);
        }
        glColor4f(color[0],color[1],color[2],.15f);
        glVertex3f(x,y,-.02f); glVertex3f(end_x,end_y,-.02f);
        glColor4f(color[0],color[1],color[2],0);
        glVertex3f(end_x,end_y,-p->data->trail_height*.7f);
        glVertex3f(x,y,-p->data->trail_height*.7f);
        polycount += 2;
      }
      glEnd();
    }
  }
  glDisable(GL_BLEND);
  /* The projected shadows still belong to the floor pass. Leave depth tests
   * and writes disabled until drawCam starts drawing opaque world geometry. */
}

void drawCam(Player *p, PlayerVisual* pV) {
  int i;
  float up[3] = { 0, 0, 1 };
	Visual *d = & pV->display;
  
  glColor3f(0.0, 1.0, 0.0);
	
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  doPerspective(gSettingsCache.fov, DisplayLayout_Aspect(d),
                gSettingsCache.znear, game2->rules.grid_size * 6.5f);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  /* set positions for GL lights in world coordinates */
  glLightfv(GL_LIGHT1, GL_POSITION, p->camera->cam);

  doLookAt(p->camera->cam, p->camera->target, up);
  glDisable(GL_LIGHTING);
  glDisable(GL_BLEND);

  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);

  /* skybox */
  if (gSettingsCache.show_skybox) {
    drawSkybox(game2->rules.grid_size);
  }

  /* fixme: clear z-buffer handling */
  /* glDepthMask(GL_TRUE); */
  
  /* floor */
  if (gSettingsCache.show_floor_texture) {
    drawFloorTextured(game2->rules.grid_size,
                      gScreen->textures[TEX_FLOOR]);
  } else {
    /* should this be an artpack setting? */
    float line_color[] = {1.0, 1.0, 1.0};
    
    drawFloorGrid(game2->rules.grid_size,
                  gSettingsCache.line_spacing,
                  line_color,
                  gSettingsCache.clear_color);
  }
  
  /* glDepthMask(GL_FALSE); */

  if(gSettingsCache.obsidian_arena && gSettingsCache.show_floor_texture)
    drawObsidianReflections(p);

  /* shadows on the floor: cycle, recognizer, trails */
  if (gSettingsCache.show_recognizer) {
    drawRecognizerShadow();
  }

  for(i = 0; !gSettingsCache.obsidian_arena && i < game->players; i++) {
    int lod = playerVisible(p, game->player + i);
		if (lod >= 0) {
			int drawTurn = 1;
			if (! gSettingsCache.camType == CAM_TYPE_COCKPIT ||
	 			p != &game->player[i])
				drawTurn = 0;
			drawCycleShadow(gPlayerVisuals + i, game->player + i, lod, drawTurn);
		}
		if (game->player[i].data->trail_height > 0 )
			drawTrailShadow(game->player + i, gPlayerVisuals + i);
	}
	
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);

  if (gSettingsCache.show_recognizer && p->data->speed != SPEED_GONE) {
    drawRecognizer();
  }

  if (gSettingsCache.show_wall == 1) {
    drawWalls();
  }

  drawPlayers(p, pV);

	setupLights(eWorld);

  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1,1);

	{
		TrailMesh mesh;
		int longestTrail = 0;
		for(i = 0; i < game->players; i++)
			if(game->player[i].data->trail_height > 0 &&
			   game->player[i].data->trailOffset > longestTrail)
				longestTrail = game->player[i].data->trailOffset;

		if(trailMeshAllocate(&mesh, longestTrail)) {
			for(i = 0; i < game->players; i++) {
				if (game->player[i].data->trail_height > 0 ) {
					int vOffset = 0;
					int iOffset = 0;
					mesh.iUsed = 0;
					trailGeometry(game->player + i, gPlayerVisuals + i,
												&mesh, &vOffset, &iOffset);
					bowGeometry(game->player + i, gPlayerVisuals + i,
											&mesh, &vOffset, &iOffset);
					trailStatesNormal(game->player + i, gScreen->textures[TEX_DECAL]);
					trailRender(&mesh);
					trailStatesRestore();
				}
			}
			trailMeshFree(&mesh);
		}
	}

  glDisable(GL_POLYGON_OFFSET_FILL);

  for(i = 0; i < game->players; i++)
    if (game->player[i].data->trail_height > 0 )
			drawTrailLines(game->player + i, gPlayerVisuals + i);

  /* transparent stuff */
  /* draw the glow around the other players: */
  if (gSettingsCache.show_glow == 1) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    for (i = 0; i < game->players; i++) {
      if (p != game->player + i && PLAYER_IS_ACTIVE(game->player + i)) {
	      drawGlow(p->camera, game->player + i, gPlayerVisuals + i,
								 d, TRAIL_HEIGHT * 4);
      }
      
    glDisable(GL_BLEND);
    }
  }
}

void initGLGame(void) {
  glShadeModel( GL_SMOOTH );
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}
