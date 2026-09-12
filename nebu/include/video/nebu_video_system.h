#ifndef NEBU_VIDEO_SYSTEM_H
#define NEBU_VIDEO_SYSTEM_H

#define SYSTEM_RGBA 1
#define SYSTEM_DOUBLE 2
#define SYSTEM_DEPTH 4
#define SYSTEM_FULLSCREEN 8
#define SYSTEM_32_BIT 16
#define SYSTEM_STENCIL 32

extern void SystemPostRedisplay();

extern void SystemSwapBuffers();
extern void SystemGrabInput();
extern void SystemUngrabInput();
extern void SystemWarpPointer(int x, int y);
extern void SystemHidePointer();
extern void SystemUnhidePointer();

extern void SystemSetGamma(float r, float g, float b);
extern void SystemInitWindow(int x, int y, int w, int h);
extern void SystemInitDisplayMode(int flags, unsigned char fullscreen);
extern int SystemCreateWindow(char *name);
extern void SystemDestroyWindow(int id);
extern void SystemReshapeFunc(void(*reshape)(int, int));
/* Dimensions exposed to the renderer are drawable pixels. Mouse coordinates
 * and saved windowed dimensions remain logical window coordinates. */
extern void SystemGetDrawableSize(int *width, int *height);
extern void SystemGetWindowSize(int *width, int *height);
extern int SystemIsFullscreen(void);
extern int SystemApplyWindow(int width, int height, int flags,
                             unsigned char fullscreen);
extern int SystemHandleWindowEvent(const void *event);
/* Runs once after the next complete frame, before swapping the back buffer. */
extern void SystemCaptureNextFrame(void (*capture)(void));

extern int SystemWriteBMP(char *filename, int x, int y, unsigned char *pixels);

#endif
