#ifndef NEBU_INPUT_SYSTEM_H
#define NEBU_INPUT_SYSTEM_H

#include "input/nebu_input_ids.h"

#define SYSTEM_KEY_DOWN GLTRON_INPUT_KEY_DOWN
#define SYSTEM_KEY_UP GLTRON_INPUT_KEY_UP
#define SYSTEM_KEY_LEFT GLTRON_INPUT_KEY_LEFT
#define SYSTEM_KEY_RIGHT GLTRON_INPUT_KEY_RIGHT
#define SYSTEM_KEY_F1 GLTRON_INPUT_KEY_F1
#define SYSTEM_KEY_F2 GLTRON_INPUT_KEY_F2
#define SYSTEM_KEY_F3 GLTRON_INPUT_KEY_F3
#define SYSTEM_KEY_F4 GLTRON_INPUT_KEY_F4
#define SYSTEM_KEY_F5 GLTRON_INPUT_KEY_F5
#define SYSTEM_KEY_F6 GLTRON_INPUT_KEY_F6
#define SYSTEM_KEY_F7 GLTRON_INPUT_KEY_F7
#define SYSTEM_KEY_F10 GLTRON_INPUT_KEY_F10
#define SYSTEM_KEY_F11 GLTRON_INPUT_KEY_F11
#define SYSTEM_KEY_F12 GLTRON_INPUT_KEY_F12

#define SYSTEM_KEY_ENTER GLTRON_INPUT_KEY_RETURN
#define SYSTEM_KEY_RETURN GLTRON_INPUT_KEY_RETURN

/* Project-owned mouse callback ABI.  The values intentionally retain SDL 1.2. */
#define SYSTEM_MOUSEDOWN 5
#define SYSTEM_MOUSEUP 6
#define SYSTEM_MOUSERELEASED 0
#define SYSTEM_MOUSEPRESSED 1
#define SYSTEM_MOUSEBUTTON_LEFT 1
#define SYSTEM_MOUSEBUTTON_RIGHT 3

#define SYSTEM_KEY_TAB GLTRON_INPUT_KEY_TAB

#define SYSTEM_JOY_AXIS_MAX GLTRON_INPUT_JOY_AXIS_MAX

#define SYSTEM_KEYSTATE_DOWN GLTRON_INPUT_STATE_DOWN
#define SYSTEM_KEYSTATE_UP GLTRON_INPUT_STATE_UP

#define SYSTEM_JOY_OFFSET GLTRON_INPUT_JOY_OFFSET
#define SYSTEM_CUSTOM_KEYS GLTRON_INPUT_CUSTOM_FIRST
#define SYSTEM_JOY_LEFT GLTRON_INPUT_JOY_LEFT
#define SYSTEM_JOY_RIGHT GLTRON_INPUT_JOY_RIGHT
#define SYSTEM_JOY_UP GLTRON_INPUT_JOY_UP
#define SYSTEM_JOY_DOWN GLTRON_INPUT_JOY_DOWN
#define SYSTEM_JOY_BUTTON_0 GLTRON_INPUT_JOY_BUTTON_0
#define SYSTEM_JOY_BUTTON_1 GLTRON_INPUT_JOY_BUTTON_1
#define SYSTEM_JOY_BUTTON_2 GLTRON_INPUT_JOY_BUTTON_2
#define SYSTEM_JOY_BUTTON_3 GLTRON_INPUT_JOY_BUTTON_3
#define SYSTEM_JOY_BUTTON_4 GLTRON_INPUT_JOY_BUTTON_4
#define SYSTEM_JOY_BUTTON_5 GLTRON_INPUT_JOY_BUTTON_5
#define SYSTEM_JOY_BUTTON_6 GLTRON_INPUT_JOY_BUTTON_6
#define SYSTEM_JOY_BUTTON_7 GLTRON_INPUT_JOY_BUTTON_7
#define SYSTEM_JOY_BUTTON_8 GLTRON_INPUT_JOY_BUTTON_8
#define SYSTEM_JOY_BUTTON_9 GLTRON_INPUT_JOY_BUTTON_9
#define SYSTEM_JOY_BUTTON_10 GLTRON_INPUT_JOY_BUTTON_10
#define SYSTEM_JOY_BUTTON_11 GLTRON_INPUT_JOY_BUTTON_11
#define SYSTEM_JOY_BUTTON_12 GLTRON_INPUT_JOY_BUTTON_12
#define SYSTEM_JOY_BUTTON_13 GLTRON_INPUT_JOY_BUTTON_13
#define SYSTEM_JOY_BUTTON_14 GLTRON_INPUT_JOY_BUTTON_14
#define SYSTEM_JOY_BUTTON_15 GLTRON_INPUT_JOY_BUTTON_15
#define SYSTEM_JOY_BUTTON_16 GLTRON_INPUT_JOY_BUTTON_16
#define SYSTEM_JOY_BUTTON_17 GLTRON_INPUT_JOY_BUTTON_17
#define SYSTEM_JOY_BUTTON_18 GLTRON_INPUT_JOY_BUTTON_18
#define SYSTEM_JOY_BUTTON_19 GLTRON_INPUT_JOY_BUTTON_19

char* SystemGetKeyName(int key);
SystemInputId SystemInputIdFromSDL1Key(int key);
SystemInputId SystemInputIdFromSDL2Key(int key);

/* Native SDL events remain private to the selected platform implementation. */
void SystemHandleInputEvent(const void *native_event);
void SystemInputInit(void);
void SystemInputShutdown(void);

/* SDL2 relative motion is translated back to the centered SDL1 callback ABI. */
void SystemInputSetRelativeMouseMode(int enabled);
void SystemInputSetMouseAnchor(int x, int y);
void SystemInputTranslateMouseMotion(int x, int y, int xrel, int yrel,
											int *translated_x, int *translated_y);

/* SDL 2 joystick events carry instance IDs rather than fixed device slots. */
int SystemInputAddJoystickInstance(int instance_id);
int SystemInputRemoveJoystickInstance(int instance_id);
int SystemInputJoystickSlotForInstance(int instance_id);

void SystemMouse(int buttons, int state, int x, int y);
void SystemMouseMotion(int x, int y);

void SystemSetJoyThreshold(float f);
void SystemResetJoyState(void);
int SystemResetJoySlot(int slot);

#endif
