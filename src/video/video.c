#include "video/video.h"
#include "video/display_layout.h"
#include "game/game.h"
#include "filesystem/path.h"

#include "Nebu_video.h"
#include "Nebu_input.h"
#include "Nebu_scripting.h"

void displayGame(void) {
  drawGame();
  SystemSwapBuffers();
}

int initWindow(void) {
  int win_id;
  int flags;
  unsigned char fullscreen = 0;
  /* char buf[20]; */

  SystemInitWindow(0, 0, getSettingi("width"), getSettingi("height"));

  if(getSettingi("windowMode") == 0) {
    fullscreen = SYSTEM_FULLSCREEN;
  }

  flags = SYSTEM_RGBA | SYSTEM_DOUBLE | SYSTEM_DEPTH;
  if(getSettingi("bitdepth_32"))
    flags |= SYSTEM_32_BIT;

  if(getSettingi("use_stencil"))
    flags |= SYSTEM_STENCIL;

  SystemInitDisplayMode(flags, fullscreen);

  win_id = SystemCreateWindow("GLTron Faithful Remaster");

  if (win_id < 0) { 
    if( getSettingi("use_stencil") ) {
      flags &= ~SYSTEM_STENCIL;
      SystemInitDisplayMode(flags, fullscreen);
      win_id = SystemCreateWindow("GLTron Faithful Remaster");
      if(win_id >= 0) {
	setSettingi("use_stencil", 0);
	goto SKIP;
      }
    }
    printf("[fatal] could not create window...exiting\n");
    exit(1); /* OK: critical, no visual */
  }

 SKIP:

  if(getSettingi("windowMode") == 0 || getSettingi("mouse_warp") == 1) {
    SystemGrabInput();
  } else {
    SystemUngrabInput();
  }

  return win_id;
}

void reshape(int x, int y) {
  int window_width, window_height;
  if(gScreen == NULL || x <= 0 || y <= 0)
    return;
  gScreen->w = gScreen->vp_w = x;
  gScreen->h = gScreen->vp_h = y;
  gScreen->vp_x = gScreen->vp_y = 0;
  setSettingi("windowMode", SystemIsFullscreen() ? 0 : 1);
  if(!SystemIsFullscreen()) {
    SystemGetWindowSize(&window_width, &window_height);
    if(window_width > 0 && window_height > 0) {
      setSettingi("width", window_width);
      setSettingi("height", window_height);
    }
  }
  changeDisplay(-1);
}

int applyWindowSettings(void) {
  int flags = SYSTEM_RGBA | SYSTEM_DOUBLE | SYSTEM_DEPTH;
  unsigned char full = getSettingi("windowMode") == 0 ? SYSTEM_FULLSCREEN : 0;
  if(getSettingi("bitdepth_32")) flags |= SYSTEM_32_BIT;
  if(getSettingi("use_stencil")) flags |= SYSTEM_STENCIL;
  if(!SystemApplyWindow(getSettingi("width"), getSettingi("height"), flags, full))
    return 0;
  if(full || getSettingi("mouse_warp")) SystemGrabInput();
  else SystemUngrabInput();
  return 1;
}

void shutdownDisplay(Visual *d) {
  VideoCancelPendingScreenshots();
  deleteTextures(d);
  deleteFonts();
  SystemDestroyWindow(d->win_id);
  // printf("[video] window destroyed\n");
}

void setupDisplay(Visual *d) {
  // fprintf(stderr, "[video] trying to create window\n");
  d->win_id = initWindow();
  // fprintf(stderr, "[video] window created\n");
  // initRenderer();
  // printRendererInfo();
  // printf("win_id is %d\n", d->win_id);
  // fprintf(stderr, "[status] loading art\n");
  loadArt();

  SystemReshapeFunc(reshape);
}

static void loadModels(void) {
	char *path;
	int i;
	/* load recognizer model */
	path = getPath(PATH_DATA, "recognizer.obj");
	if(path != NULL) {
		recognizer = readMeshFromFile(path, TRI_MESH);
		/* old code did normalize & invert normals & rescale to size = 60 */
	} else {
		fprintf(stderr, "fatal: could not load recognizer model - exiting...\n");
		exit(1); /* OK: critical, installation corrupt */
	}
	free(path);
 
	/* load recognizer quad model (for recognizer outlines) */
	path = getPath(PATH_DATA, "recognizer_quad.obj");
	if(path != NULL) {
		recognizer_quad = readMeshFromFile(path, QUAD_MESH);
		/* old code did normalize & invert normals & rescale to size = 60 */
	} else {
		fprintf(stderr, "fatal: could not load recognizer model - exiting...\n");
		exit(1); /* OK: critical, installation corrupt */
	}
	free(path);

	/* load lightcycle models */
	for(i = 0; i < LC_LOD; i++) {
		path = getPath(PATH_DATA, lc_lod_names[i]);
		if(path != NULL) {
			lightcycle[i] = readMeshFromFile(path, TRI_MESH);
		} else {
			fprintf(stderr, "fatal: could not load model %s - exiting...\n", lc_lod_names[i]);
			exit(1); /* OK: critical, installation corrupt */
		}
		free(path);
	}
}

void initVideoData(void) {
  gScreen = (Visual*) malloc(sizeof(Visual));
  gViewportType = getSettingi("display_type"); 
  
	{
		Visual *d = gScreen;
		d->w = getSettingi("width"); 
		d->h = getSettingi("height"); 
		d->vp_x = 0; d->vp_y = 0;
		d->vp_w = d->w; d->vp_h = d->h;
		d->onScreen = -1;
		d->textures = (unsigned int*) malloc(game_textures * sizeof(unsigned int));
	}

	gPlayerVisuals = (PlayerVisual*) malloc(MAX_PLAYERS * sizeof(PlayerVisual));

	loadModels();

  changeDisplay(-1);
}

void initGameScreen(void) {
  Visual *d;
  d = gScreen;
  d->w = getSettingi("width");
  d->h = getSettingi("height"); 
  d->vp_x = 0; d->vp_y = 0;
  d->vp_w = d->w; d->vp_h = d->h;
}


void initDisplay(Visual *d, int type, int p, int onScreen) {
  DisplayLayout_Viewport(d, gScreen, vp_x[type][p], vp_y[type][p],
                         vp_w[type][p], vp_h[type][p]);
  d->onScreen = onScreen;
}  

static void defaultViewportPositions(void) {
  viewport_content[0] = 0;
  viewport_content[1] = 1;
  viewport_content[2] = 2;
  viewport_content[3] = 3;
}


/*
  autoConfigureDisplay - configure viewports so every human player has one
 */
static void autoConfigureDisplay(void) {
  int n_humans = 0;
  int i;
  int vp;

  defaultViewportPositions();

  /* loop thru players and find the humans */
  for (i=0; i < game->players; i++) {
    if (game->player[i].ai->active == AI_HUMAN) {
      viewport_content[n_humans] = i;
      n_humans++;
    }    
  }
 
  switch(n_humans) {
    case 0 :
      /*
         Not sure what the default should be for
         a game without human players. For now 
         just show a single viewport.
       */
      /* fall thru */
    case 1 :
      vp = VP_SINGLE;
      break;
    case 2 :
      vp = VP_SPLIT;
      break;
    default :
      defaultViewportPositions();
      vp = VP_FOURWAY;
  }  

  updateDisplay(vp);
}

void changeDisplay(int view) {

  /* passing -1 to changeDisplay tells it to use the view from settings */
  if (view == -1) {
    view = getSettingi("display_type");
  }
  
  if (view == 3) {
    autoConfigureDisplay(); 
  } else {
    defaultViewportPositions(); 
    updateDisplay(view);
  }

  // displayMessage(TO_STDOUT, "set display to %d", view);
  setSettingi("display_type", view);
}

void updateDisplay(int vpType) {
  int i;

  gViewportType = vpType;

  for (i = 0; i < game->players; i++) {
    gPlayerVisuals[i].display.onScreen = 0;
  }
  for (i = 0; i < vp_max[vpType]; i++) {
       initDisplay(& gPlayerVisuals[ viewport_content[i] ].display, 
		   vpType, i, 1);
  }

}

void Video_Idle(void) { }
