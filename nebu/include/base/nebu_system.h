#ifndef NEBU_SYSTEM_H
#define NEBU_SYSTEM_H

#include "nebu_callbacks.h"

/* system specific functions (basically, an SDL/glut wrapper) */
extern unsigned int SystemGetElapsedTime();
extern void SystemDelay(unsigned int milliseconds);

extern int SystemMainLoop();
extern void SystemExitLoop(int return_code);
extern void SystemRegisterCallbacks(Callbacks* callbacks);

typedef void (*SystemShutdownCallback)(void);
extern void SystemSetShutdownCallback(SystemShutdownCallback callback);
extern void SystemExit();

extern Callbacks* current;

#endif
