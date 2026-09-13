#ifndef VT_FAITHFUL_PLATFORM_H
#define VT_FAITHFUL_PLATFORM_H
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Window;
int VT_FaithfulInitialize(struct SDL_Window *window);
void VT_FaithfulSwap(void);
/* Finalizes Vulkan before SDL destroys its window. Safe to call twice. */
int VT_FaithfulShutdown(void);
unsigned int VT_FaithfulValidationErrors(void);
const char *VT_FaithfulDefaultDirectory(int screenshots);
#ifdef __cplusplus
}
#endif
#endif
