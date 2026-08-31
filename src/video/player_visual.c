#include <stdio.h>

#include "video/video.h"
#include "game/game.h"
#include "Nebu_scripting.h"

void resetVideoData(void) {
  int i;

  for(i = 0; i < game->players; i++) {
    PlayerVisual *pV = gPlayerVisuals + i;
    char name[32];

    sprintf(name, "model_diffuse_%d", i);
    scripting_GetGlobal(name, NULL);
    scripting_GetFloatArrayResult(pV->pColorDiffuse, 4);
    sprintf(name, "model_specular_%d", i);
    scripting_GetGlobal(name, NULL);
    scripting_GetFloatArrayResult(pV->pColorSpecular, 4);
    sprintf(name, "trail_diffuse_%d", i);
    scripting_GetGlobal(name, NULL);
    scripting_GetFloatArrayResult(pV->pColorAlpha, 4);

    pV->spoke_time = 0;
    pV->spoke_state = 0;
    pV->impact_radius = 0.0f;
    if(game->player[i].ai->active != AI_NONE)
      pV->exp_radius = 0.0f;
    else
      pV->exp_radius = EXP_RADIUS_MAX;
  }
}
