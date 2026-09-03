#include "input/nebu_input_system.h"
#include "input/nebu_system_keynames.h"
#include "base/nebu_system.h"

#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float joystick_threshold = 0;
static SystemJoystickTranslator joystick_translator;
static unsigned char joystick_button_down[GLTRON_INPUT_JOY_SLOT_COUNT]
	[GLTRON_INPUT_JOY_BUTTON_COUNT];
static SDL_Joystick *joystick_handles[GLTRON_INPUT_JOY_SLOT_COUNT];
static int joystick_instance_ids[GLTRON_INPUT_JOY_SLOT_COUNT];
static unsigned char joystick_instance_used[GLTRON_INPUT_JOY_SLOT_COUNT];
static int relative_mouse_mode;
static int mouse_anchor_x;
static int mouse_anchor_y;

float SystemClampJoyThreshold(float threshold) {
	if(threshold != threshold || threshold < 0.0f)
		return 0.0f;
	if(threshold > 0.95f)
		return 0.95f;
	return threshold;
}

void SystemJoystickTranslatorReset(SystemJoystickTranslator *translator) {
	if(translator != NULL)
		memset(translator, 0, sizeof(*translator));
}

int SystemJoystickTranslatorResetSlot(SystemJoystickTranslator *translator,
											int slot) {
	int axis;

	if(translator == NULL || slot < 0 ||
		 slot >= GLTRON_INPUT_JOY_SLOT_COUNT)
		return 0;

	for(axis = 0; axis < GLTRON_INPUT_JOY_AXIS_COUNT; axis++)
		translator->direction[slot][axis] = 0;
	return 1;
}

static int joystickAxisId(int slot, int axis, int direction,
							SystemInputId *id) {
	if(id == NULL || slot < 0 || slot >= GLTRON_INPUT_JOY_SLOT_COUNT ||
		 axis < 0 || axis >= GLTRON_INPUT_JOY_AXIS_COUNT ||
		 (direction != -1 && direction != 1))
		return 0;

	*id = GLTRON_INPUT_JOY_LEFT + slot * GLTRON_INPUT_JOY_OFFSET +
		axis * 2 + (direction > 0 ? 1 : 0);
	return 1;
}

int SystemTranslateJoystickAxis(SystemJoystickTranslator *translator,
								int slot, int axis, int value,
								float threshold,
								SystemInputTransition transitions[2]) {
	int old_direction;
	int new_direction;
	int count = 0;
	float cutoff;

	if(translator == NULL || transitions == NULL || slot < 0 ||
		 slot >= GLTRON_INPUT_JOY_SLOT_COUNT || axis < 0 ||
		 axis >= GLTRON_INPUT_JOY_AXIS_COUNT)
		return 0;

	threshold = SystemClampJoyThreshold(threshold);
	cutoff = threshold * GLTRON_INPUT_JOY_AXIS_MAX;
	if((float)value >= -cutoff && (float)value <= cutoff)
		new_direction = 0;
	else
		new_direction = value < 0 ? -1 : 1;

	old_direction = translator->direction[slot][axis];
	if(old_direction == new_direction)
		return 0;

	if(old_direction != 0) {
		transitions[count].state = GLTRON_INPUT_STATE_UP;
		if(!joystickAxisId(slot, axis, old_direction,
								 &transitions[count].id))
			return 0;
		count++;
	}

	if(new_direction != 0) {
		transitions[count].state = GLTRON_INPUT_STATE_DOWN;
		if(!joystickAxisId(slot, axis, new_direction,
								 &transitions[count].id))
			return 0;
		count++;
	}

	translator->direction[slot][axis] = (signed char)new_direction;
	return count;
}

int SystemTranslateJoystickButton(int slot, int button, int state,
									SystemInputTransition *transition) {
	if(transition == NULL || slot < 0 ||
		 slot >= GLTRON_INPUT_JOY_SLOT_COUNT || button < 0 ||
		 button >= GLTRON_INPUT_JOY_BUTTON_COUNT ||
		 (state != GLTRON_INPUT_STATE_DOWN && state != GLTRON_INPUT_STATE_UP))
		return 0;

	transition->state = state;
	transition->id = GLTRON_INPUT_JOY_BUTTON_0 +
		slot * GLTRON_INPUT_JOY_OFFSET + button;
	return 1;
}

void SystemResetJoyState(void) {
	SystemJoystickTranslatorReset(&joystick_translator);
	memset(joystick_button_down, 0, sizeof(joystick_button_down));
}

int SystemResetJoySlot(int slot) {
	if(!SystemJoystickTranslatorResetSlot(&joystick_translator, slot))
		return 0;
	memset(joystick_button_down[slot], 0,
		sizeof(joystick_button_down[slot]));
	return 1;
}

void SystemMouse(int buttons, int state, int x, int y) {
  if(current)
    if(current->mouse != NULL)
      current->mouse(buttons, state, x, y);
}

void SystemMouseMotion(int x, int y) {
  if(current)
    if(current->mouseMotion != NULL)
      current->mouseMotion(x, y);
}

#if SDL_MAJOR_VERSION >= 2
static int legacyMouseButtonFromSDL2(int button) {
	/* SDL 1 reserves buttons 4 and 5 for its synthetic wheel clicks. */
	if(button > SDL_BUTTON_RIGHT)
		return button + 2;
	return button;
}

static void dispatchSDL2MouseWheel(const SDL_MouseWheelEvent *wheel) {
	int button;
	int mouse_x;
	int mouse_y;

	if(wheel == NULL || wheel->y == 0)
		return;

	button = wheel->y > 0 ? 4 : 5;
	/* SDL 1 has no wheel-direction metadata or magnitude.  Match
	 * sdl12-compat by producing one classic click for each SDL2 event. */
	SDL_GetMouseState(&mouse_x, &mouse_y);
	SystemMouse(button, SYSTEM_MOUSEPRESSED, mouse_x, mouse_y);
	SystemMouse(button, SYSTEM_MOUSERELEASED, mouse_x, mouse_y);
}
#endif

void SystemInputSetRelativeMouseMode(int enabled) {
	relative_mouse_mode = enabled ? 1 : 0;
}

void SystemInputSetMouseAnchor(int x, int y) {
	mouse_anchor_x = x;
	mouse_anchor_y = y;
}

void SystemInputTranslateMouseMotion(int x, int y, int xrel, int yrel,
											int *translated_x, int *translated_y) {
	if(translated_x == NULL || translated_y == NULL)
		return;

	if(relative_mouse_mode) {
		*translated_x = mouse_anchor_x + xrel;
		*translated_y = mouse_anchor_y + yrel;
	} else {
		*translated_x = x;
		*translated_y = y;
	}
}

static const char *const digit_key_names[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};

static const char *const letter_key_names[] = {
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
	"n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"
};

#define WORLD_KEY_NAME(number) "world " #number
static const char *const world_key_names[] = {
	WORLD_KEY_NAME(0), WORLD_KEY_NAME(1), WORLD_KEY_NAME(2), WORLD_KEY_NAME(3),
	WORLD_KEY_NAME(4), WORLD_KEY_NAME(5), WORLD_KEY_NAME(6), WORLD_KEY_NAME(7),
	WORLD_KEY_NAME(8), WORLD_KEY_NAME(9), WORLD_KEY_NAME(10), WORLD_KEY_NAME(11),
	WORLD_KEY_NAME(12), WORLD_KEY_NAME(13), WORLD_KEY_NAME(14), WORLD_KEY_NAME(15),
	WORLD_KEY_NAME(16), WORLD_KEY_NAME(17), WORLD_KEY_NAME(18), WORLD_KEY_NAME(19),
	WORLD_KEY_NAME(20), WORLD_KEY_NAME(21), WORLD_KEY_NAME(22), WORLD_KEY_NAME(23),
	WORLD_KEY_NAME(24), WORLD_KEY_NAME(25), WORLD_KEY_NAME(26), WORLD_KEY_NAME(27),
	WORLD_KEY_NAME(28), WORLD_KEY_NAME(29), WORLD_KEY_NAME(30), WORLD_KEY_NAME(31),
	WORLD_KEY_NAME(32), WORLD_KEY_NAME(33), WORLD_KEY_NAME(34), WORLD_KEY_NAME(35),
	WORLD_KEY_NAME(36), WORLD_KEY_NAME(37), WORLD_KEY_NAME(38), WORLD_KEY_NAME(39),
	WORLD_KEY_NAME(40), WORLD_KEY_NAME(41), WORLD_KEY_NAME(42), WORLD_KEY_NAME(43),
	WORLD_KEY_NAME(44), WORLD_KEY_NAME(45), WORLD_KEY_NAME(46), WORLD_KEY_NAME(47),
	WORLD_KEY_NAME(48), WORLD_KEY_NAME(49), WORLD_KEY_NAME(50), WORLD_KEY_NAME(51),
	WORLD_KEY_NAME(52), WORLD_KEY_NAME(53), WORLD_KEY_NAME(54), WORLD_KEY_NAME(55),
	WORLD_KEY_NAME(56), WORLD_KEY_NAME(57), WORLD_KEY_NAME(58), WORLD_KEY_NAME(59),
	WORLD_KEY_NAME(60), WORLD_KEY_NAME(61), WORLD_KEY_NAME(62), WORLD_KEY_NAME(63),
	WORLD_KEY_NAME(64), WORLD_KEY_NAME(65), WORLD_KEY_NAME(66), WORLD_KEY_NAME(67),
	WORLD_KEY_NAME(68), WORLD_KEY_NAME(69), WORLD_KEY_NAME(70), WORLD_KEY_NAME(71),
	WORLD_KEY_NAME(72), WORLD_KEY_NAME(73), WORLD_KEY_NAME(74), WORLD_KEY_NAME(75),
	WORLD_KEY_NAME(76), WORLD_KEY_NAME(77), WORLD_KEY_NAME(78), WORLD_KEY_NAME(79),
	WORLD_KEY_NAME(80), WORLD_KEY_NAME(81), WORLD_KEY_NAME(82), WORLD_KEY_NAME(83),
	WORLD_KEY_NAME(84), WORLD_KEY_NAME(85), WORLD_KEY_NAME(86), WORLD_KEY_NAME(87),
	WORLD_KEY_NAME(88), WORLD_KEY_NAME(89), WORLD_KEY_NAME(90), WORLD_KEY_NAME(91),
	WORLD_KEY_NAME(92), WORLD_KEY_NAME(93), WORLD_KEY_NAME(94), WORLD_KEY_NAME(95)
};
#undef WORLD_KEY_NAME

static const char *const keypad_key_names[] = {
	"[0]", "[1]", "[2]", "[3]", "[4]", "[5]", "[6]", "[7]", "[8]", "[9]"
};

#define FUNCTION_KEY_NAME(number) "f" #number
static const char *const function_key_names[] = {
	FUNCTION_KEY_NAME(1), FUNCTION_KEY_NAME(2), FUNCTION_KEY_NAME(3),
	FUNCTION_KEY_NAME(4), FUNCTION_KEY_NAME(5), FUNCTION_KEY_NAME(6),
	FUNCTION_KEY_NAME(7), FUNCTION_KEY_NAME(8), FUNCTION_KEY_NAME(9),
	FUNCTION_KEY_NAME(10), FUNCTION_KEY_NAME(11), FUNCTION_KEY_NAME(12),
	FUNCTION_KEY_NAME(13), FUNCTION_KEY_NAME(14), FUNCTION_KEY_NAME(15)
};
#undef FUNCTION_KEY_NAME

static const char *stableKeyboardName(SystemInputId key) {
	if(key >= '0' && key <= '9')
		return digit_key_names[key - '0'];
	if(key >= 'a' && key <= 'z')
		return letter_key_names[key - 'a'];
	if(key >= GLTRON_INPUT_KEY_WORLD_FIRST &&
		 key <= GLTRON_INPUT_KEY_WORLD_LAST)
		return world_key_names[key - GLTRON_INPUT_KEY_WORLD_FIRST];
	if(key >= GLTRON_INPUT_KEY_KP0 && key <= GLTRON_INPUT_KEY_KP9)
		return keypad_key_names[key - GLTRON_INPUT_KEY_KP0];
	if(key >= GLTRON_INPUT_KEY_F1 && key <= GLTRON_INPUT_KEY_F15)
		return function_key_names[key - GLTRON_INPUT_KEY_F1];

	switch(key) {
	case GLTRON_INPUT_KEY_UNKNOWN: return "unknown key";
	case GLTRON_INPUT_KEY_BACKSPACE: return "backspace";
	case GLTRON_INPUT_KEY_TAB: return "tab";
	case GLTRON_INPUT_KEY_CLEAR: return "clear";
	case GLTRON_INPUT_KEY_RETURN: return "return";
	case GLTRON_INPUT_KEY_PAUSE: return "pause";
	case GLTRON_INPUT_KEY_ESCAPE: return "escape";
	case GLTRON_INPUT_KEY_SPACE: return "space";
	case 33: return "!";
	case 34: return "\"";
	case 35: return "#";
	case 36: return "$";
	case 38: return "&";
	case 39: return "'";
	case 40: return "(";
	case 41: return ")";
	case 42: return "*";
	case 43: return "+";
	case 44: return ",";
	case 45: return "-";
	case 46: return ".";
	case 47: return "/";
	case 58: return ":";
	case 59: return ";";
	case 60: return "<";
	case 61: return "=";
	case 62: return ">";
	case 63: return "?";
	case 64: return "@";
	case 91: return "[";
	case 92: return "\\";
	case 93: return "]";
	case 94: return "^";
	case 95: return "_";
	case 96: return "`";
	case GLTRON_INPUT_KEY_DELETE: return "delete";
	case GLTRON_INPUT_KEY_KP_PERIOD: return "[.]";
	case GLTRON_INPUT_KEY_KP_DIVIDE: return "[/]";
	case GLTRON_INPUT_KEY_KP_MULTIPLY: return "[*]";
	case GLTRON_INPUT_KEY_KP_MINUS: return "[-]";
	case GLTRON_INPUT_KEY_KP_PLUS: return "[+]";
	case GLTRON_INPUT_KEY_KP_ENTER: return "enter";
	case GLTRON_INPUT_KEY_KP_EQUALS: return "equals";
	case GLTRON_INPUT_KEY_UP: return "up";
	case GLTRON_INPUT_KEY_DOWN: return "down";
	case GLTRON_INPUT_KEY_RIGHT: return "right";
	case GLTRON_INPUT_KEY_LEFT: return "left";
	case GLTRON_INPUT_KEY_INSERT: return "insert";
	case GLTRON_INPUT_KEY_HOME: return "home";
	case GLTRON_INPUT_KEY_END: return "end";
	case GLTRON_INPUT_KEY_PAGEUP: return "page up";
	case GLTRON_INPUT_KEY_PAGEDOWN: return "page down";
	case GLTRON_INPUT_KEY_NUMLOCK: return "numlock";
	case GLTRON_INPUT_KEY_CAPSLOCK: return "caps lock";
	case GLTRON_INPUT_KEY_SCROLLOCK: return "scroll lock";
	case GLTRON_INPUT_KEY_RSHIFT: return "right shift";
	case GLTRON_INPUT_KEY_LSHIFT: return "left shift";
	case GLTRON_INPUT_KEY_RCTRL: return "right ctrl";
	case GLTRON_INPUT_KEY_LCTRL: return "left ctrl";
	case GLTRON_INPUT_KEY_RALT: return "right alt";
	case GLTRON_INPUT_KEY_LALT: return "left alt";
	case GLTRON_INPUT_KEY_RMETA: return "right meta";
	case GLTRON_INPUT_KEY_LMETA: return "left meta";
	case GLTRON_INPUT_KEY_LSUPER: return "left super";
	case GLTRON_INPUT_KEY_RSUPER: return "right super";
	case GLTRON_INPUT_KEY_MODE: return "alt gr";
	case GLTRON_INPUT_KEY_COMPOSE: return "compose";
	case GLTRON_INPUT_KEY_HELP: return "help";
	case GLTRON_INPUT_KEY_PRINT: return "print screen";
	case GLTRON_INPUT_KEY_SYSREQ: return "sys req";
	case GLTRON_INPUT_KEY_BREAK: return "break";
	case GLTRON_INPUT_KEY_MENU: return "menu";
	case GLTRON_INPUT_KEY_POWER: return "power";
	case GLTRON_INPUT_KEY_EURO: return "euro";
	case GLTRON_INPUT_KEY_UNDO: return "undo";
	default: return "unknown key";
	}
}

extern char* SystemGetKeyName(int key) {
	int i;

	if(key < GLTRON_INPUT_CUSTOM_FIRST)
		return (char *)stableKeyboardName(key);

	for(i = 0; i < CUSTOM_KEY_COUNT; i++) {
		if(custom_keys.key[i].key == key)
			return custom_keys.key[i].name;
	}
	return "unknown custom key";
}

SystemInputId SystemInputIdFromSDL1Key(int key) {
	if(key < GLTRON_INPUT_KEY_UNKNOWN || key > GLTRON_INPUT_KEY_LAST)
		return GLTRON_INPUT_INVALID;

	/* SDL 1.2 key values are the numeric ABI retained by GLTron. */
	return (SystemInputId)key;
}

#if SDL_MAJOR_VERSION >= 2
static SystemInputId stableSDL1KeyFromUnicode(int key) {
	switch(key) {
#include "sdl12_unicode_keymap.inc"
	default: return GLTRON_INPUT_INVALID;
	}
}
#endif

SystemInputId SystemInputIdFromSDL2Key(int key) {
#if SDL_MAJOR_VERSION >= 2
	SystemInputId stable_key;

	/* SDL 2 uses Unicode values directly for ASCII and Latin-1 keys. */
	if(key >= GLTRON_INPUT_KEY_UNKNOWN &&
		 key <= GLTRON_INPUT_KEY_WORLD_LAST)
		return (SystemInputId)key;

	stable_key = stableSDL1KeyFromUnicode(key);
	if(stable_key != GLTRON_INPUT_INVALID)
		return stable_key;

#define MAP_SDL2_KEY(native_key, stable_key) \
	case native_key: return stable_key
	switch(key) {
	MAP_SDL2_KEY(SDLK_CLEAR, GLTRON_INPUT_KEY_CLEAR);
	MAP_SDL2_KEY(SDLK_PAUSE, GLTRON_INPUT_KEY_PAUSE);
	MAP_SDL2_KEY(SDLK_KP_0, GLTRON_INPUT_KEY_KP0);
	MAP_SDL2_KEY(SDLK_KP_1, GLTRON_INPUT_KEY_KP1);
	MAP_SDL2_KEY(SDLK_KP_2, GLTRON_INPUT_KEY_KP2);
	MAP_SDL2_KEY(SDLK_KP_3, GLTRON_INPUT_KEY_KP3);
	MAP_SDL2_KEY(SDLK_KP_4, GLTRON_INPUT_KEY_KP4);
	MAP_SDL2_KEY(SDLK_KP_5, GLTRON_INPUT_KEY_KP5);
	MAP_SDL2_KEY(SDLK_KP_6, GLTRON_INPUT_KEY_KP6);
	MAP_SDL2_KEY(SDLK_KP_7, GLTRON_INPUT_KEY_KP7);
	MAP_SDL2_KEY(SDLK_KP_8, GLTRON_INPUT_KEY_KP8);
	MAP_SDL2_KEY(SDLK_KP_9, GLTRON_INPUT_KEY_KP9);
	MAP_SDL2_KEY(SDLK_KP_PERIOD, GLTRON_INPUT_KEY_KP_PERIOD);
	MAP_SDL2_KEY(SDLK_KP_DIVIDE, GLTRON_INPUT_KEY_KP_DIVIDE);
	MAP_SDL2_KEY(SDLK_KP_MULTIPLY, GLTRON_INPUT_KEY_KP_MULTIPLY);
	MAP_SDL2_KEY(SDLK_KP_MINUS, GLTRON_INPUT_KEY_KP_MINUS);
	MAP_SDL2_KEY(SDLK_KP_PLUS, GLTRON_INPUT_KEY_KP_PLUS);
	MAP_SDL2_KEY(SDLK_KP_ENTER, GLTRON_INPUT_KEY_KP_ENTER);
	MAP_SDL2_KEY(SDLK_KP_EQUALS, GLTRON_INPUT_KEY_KP_EQUALS);
	MAP_SDL2_KEY(SDLK_UP, GLTRON_INPUT_KEY_UP);
	MAP_SDL2_KEY(SDLK_DOWN, GLTRON_INPUT_KEY_DOWN);
	MAP_SDL2_KEY(SDLK_RIGHT, GLTRON_INPUT_KEY_RIGHT);
	MAP_SDL2_KEY(SDLK_LEFT, GLTRON_INPUT_KEY_LEFT);
	MAP_SDL2_KEY(SDLK_INSERT, GLTRON_INPUT_KEY_INSERT);
	MAP_SDL2_KEY(SDLK_HOME, GLTRON_INPUT_KEY_HOME);
	MAP_SDL2_KEY(SDLK_END, GLTRON_INPUT_KEY_END);
	MAP_SDL2_KEY(SDLK_PAGEUP, GLTRON_INPUT_KEY_PAGEUP);
	MAP_SDL2_KEY(SDLK_PAGEDOWN, GLTRON_INPUT_KEY_PAGEDOWN);
	MAP_SDL2_KEY(SDLK_F1, GLTRON_INPUT_KEY_F1);
	MAP_SDL2_KEY(SDLK_F2, GLTRON_INPUT_KEY_F2);
	MAP_SDL2_KEY(SDLK_F3, GLTRON_INPUT_KEY_F3);
	MAP_SDL2_KEY(SDLK_F4, GLTRON_INPUT_KEY_F4);
	MAP_SDL2_KEY(SDLK_F5, GLTRON_INPUT_KEY_F5);
	MAP_SDL2_KEY(SDLK_F6, GLTRON_INPUT_KEY_F6);
	MAP_SDL2_KEY(SDLK_F7, GLTRON_INPUT_KEY_F7);
	MAP_SDL2_KEY(SDLK_F8, GLTRON_INPUT_KEY_F8);
	MAP_SDL2_KEY(SDLK_F9, GLTRON_INPUT_KEY_F9);
	MAP_SDL2_KEY(SDLK_F10, GLTRON_INPUT_KEY_F10);
	MAP_SDL2_KEY(SDLK_F11, GLTRON_INPUT_KEY_F11);
	MAP_SDL2_KEY(SDLK_F12, GLTRON_INPUT_KEY_F12);
	MAP_SDL2_KEY(SDLK_F13, GLTRON_INPUT_KEY_F13);
	MAP_SDL2_KEY(SDLK_F14, GLTRON_INPUT_KEY_F14);
	MAP_SDL2_KEY(SDLK_F15, GLTRON_INPUT_KEY_F15);
	MAP_SDL2_KEY(SDLK_NUMLOCKCLEAR, GLTRON_INPUT_KEY_NUMLOCK);
	MAP_SDL2_KEY(SDLK_CAPSLOCK, GLTRON_INPUT_KEY_CAPSLOCK);
	MAP_SDL2_KEY(SDLK_SCROLLLOCK, GLTRON_INPUT_KEY_SCROLLOCK);
	MAP_SDL2_KEY(SDLK_RSHIFT, GLTRON_INPUT_KEY_RSHIFT);
	MAP_SDL2_KEY(SDLK_LSHIFT, GLTRON_INPUT_KEY_LSHIFT);
	MAP_SDL2_KEY(SDLK_RCTRL, GLTRON_INPUT_KEY_RCTRL);
	MAP_SDL2_KEY(SDLK_LCTRL, GLTRON_INPUT_KEY_LCTRL);
	MAP_SDL2_KEY(SDLK_RALT, GLTRON_INPUT_KEY_RALT);
	MAP_SDL2_KEY(SDLK_LALT, GLTRON_INPUT_KEY_LALT);
	/* Match the translated-key path used by sdl12-compat on this baseline. */
	MAP_SDL2_KEY(SDLK_RGUI, GLTRON_INPUT_KEY_RMETA);
	MAP_SDL2_KEY(SDLK_LGUI, GLTRON_INPUT_KEY_LMETA);
	MAP_SDL2_KEY(SDLK_MODE, GLTRON_INPUT_KEY_MODE);
	MAP_SDL2_KEY(SDLK_APPLICATION, GLTRON_INPUT_KEY_MENU);
	MAP_SDL2_KEY(SDLK_HELP, GLTRON_INPUT_KEY_HELP);
	MAP_SDL2_KEY(SDLK_PRINTSCREEN, GLTRON_INPUT_KEY_PRINT);
	MAP_SDL2_KEY(SDLK_SYSREQ, GLTRON_INPUT_KEY_SYSREQ);
	MAP_SDL2_KEY(SDLK_MENU, GLTRON_INPUT_KEY_MENU);
	MAP_SDL2_KEY(SDLK_POWER, GLTRON_INPUT_KEY_POWER);
	MAP_SDL2_KEY(SDLK_UNDO, GLTRON_INPUT_KEY_UNDO);
	/* SDL 2 represents the Euro character by its Unicode code point. */
	MAP_SDL2_KEY(0x20ac, GLTRON_INPUT_KEY_EURO);
	default: return GLTRON_INPUT_INVALID;
	}
#undef MAP_SDL2_KEY
#else
	(void)key;
	return GLTRON_INPUT_INVALID;
#endif
}

int SystemInputJoystickSlotForInstance(int instance_id) {
	int slot;

	for(slot = 0; slot < GLTRON_INPUT_JOY_SLOT_COUNT; slot++) {
		if(joystick_instance_used[slot] &&
			 joystick_instance_ids[slot] == instance_id)
			return slot;
	}
	return -1;
}

int SystemInputAddJoystickInstance(int instance_id) {
	int slot;

	if(instance_id < 0)
		return -1;

	slot = SystemInputJoystickSlotForInstance(instance_id);
	if(slot >= 0)
		return slot;

	for(slot = 0; slot < GLTRON_INPUT_JOY_SLOT_COUNT; slot++) {
		if(!joystick_instance_used[slot]) {
			SystemResetJoySlot(slot);
			joystick_instance_ids[slot] = instance_id;
			joystick_instance_used[slot] = 1;
			return slot;
		}
	}
	return -1;
}

static void dispatchKeyboardTransition(const SystemInputTransition *transition) {
	if(transition != NULL && current != NULL && current->keyboard != NULL)
		current->keyboard(transition->state, transition->id, 0, 0);
}

static void dispatchKeyboard(int state, SystemInputId id) {
	SystemInputTransition transition;

	transition.state = state;
	transition.id = id;
	dispatchKeyboardTransition(&transition);
}

static void dispatchJoystickButton(int slot, int button, int state) {
	SystemInputTransition transition;

	if(SystemTranslateJoystickButton(slot, button, state, &transition)) {
		joystick_button_down[slot][button] =
			(unsigned char)(state == SYSTEM_KEYSTATE_DOWN);
		dispatchKeyboardTransition(&transition);
	}
}

static void releaseJoystickSlot(int slot) {
	SystemInputTransition transitions[2];
	int axis;
	int button;
	int count;
	int i;

	if(slot < 0 || slot >= GLTRON_INPUT_JOY_SLOT_COUNT)
		return;

	for(axis = 0; axis < GLTRON_INPUT_JOY_AXIS_COUNT; axis++) {
		count = SystemTranslateJoystickAxis(&joystick_translator, slot, axis,
			0, joystick_threshold, transitions);
		for(i = 0; i < count; i++)
			dispatchKeyboardTransition(&transitions[i]);
	}

	for(button = 0; button < GLTRON_INPUT_JOY_BUTTON_COUNT; button++) {
		if(joystick_button_down[slot][button])
			dispatchJoystickButton(slot, button, SYSTEM_KEYSTATE_UP);
	}
	SystemResetJoySlot(slot);
}

int SystemInputRemoveJoystickInstance(int instance_id) {
	int slot = SystemInputJoystickSlotForInstance(instance_id);

	if(slot < 0)
		return -1;

	releaseJoystickSlot(slot);
	if(joystick_handles[slot] != NULL) {
		SDL_JoystickClose(joystick_handles[slot]);
		joystick_handles[slot] = NULL;
	}
	joystick_instance_ids[slot] = -1;
	joystick_instance_used[slot] = 0;
	return slot;
}

#if SDL_MAJOR_VERSION >= 2
static int openSDL2Joystick(int device_index) {
	SDL_Joystick *handle;
	int instance_id;
	int slot;

	handle = SDL_JoystickOpen(device_index);
	if(handle == NULL)
		return -1;

	instance_id = (int)SDL_JoystickInstanceID(handle);
	if(instance_id < 0) {
		SDL_JoystickClose(handle);
		return -1;
	}

	slot = SystemInputJoystickSlotForInstance(instance_id);
	if(slot >= 0) {
		SDL_JoystickClose(handle);
		return slot;
	}

	slot = SystemInputAddJoystickInstance(instance_id);
	if(slot < 0) {
		SDL_JoystickClose(handle);
		return -1;
	}
	joystick_handles[slot] = handle;
	return slot;
}
#endif

void SystemInputShutdown(void) {
	int slot;

	for(slot = 0; slot < GLTRON_INPUT_JOY_SLOT_COUNT; slot++) {
		if(joystick_handles[slot] != NULL) {
			SDL_JoystickClose(joystick_handles[slot]);
			joystick_handles[slot] = NULL;
		}
		joystick_instance_ids[slot] = -1;
		joystick_instance_used[slot] = 0;
	}
	SystemInputSetRelativeMouseMode(0);
	SystemInputSetMouseAnchor(0, 0);
	SystemResetJoyState();
}

void SystemInputInit(void) {
	int i;
	int joysticks;
	int opened = 0;

	SystemInputShutdown();

#if SDL_MAJOR_VERSION < 2
	/* SDL 1 synthesizes repeats when enabled; classic GLTron disables them. */
	SDL_EnableKeyRepeat(0, 0);
#endif

	if(SDL_Init(SDL_INIT_JOYSTICK) < 0) {
		fprintf(stderr, "[init] couldn't initialize joysticks: %s\n",
			SDL_GetError());
		return;
	}

	joysticks = SDL_NumJoysticks();
	if(joysticks < 0)
		joysticks = 0;

#if SDL_MAJOR_VERSION >= 2
	/* A failed early device must not hide a later usable controller. */
	for(i = 0; i < joysticks &&
		 opened < GLTRON_INPUT_JOY_SLOT_COUNT; i++) {
		if(openSDL2Joystick(i) >= 0)
			opened++;
	}
#else
	if(joysticks > GLTRON_INPUT_JOY_SLOT_COUNT)
		joysticks = GLTRON_INPUT_JOY_SLOT_COUNT;
	for(i = 0; i < joysticks; i++) {
		joystick_handles[i] = SDL_JoystickOpen(i);
		if(joystick_handles[i] != NULL)
			opened++;
	}
#endif
	if(opened > 0)
		SDL_JoystickEventState(SDL_ENABLE);
}

void SystemHandleInputEvent(const void *native_event) {
	const SDL_Event *event = (const SDL_Event *)native_event;
	int button;
	int count;
	int i;
	int mouse_x;
	int mouse_y;
	int slot;
	int state;
	SystemInputId key;
	SystemInputTransition transitions[2];

	if(event == NULL)
		return;

	switch(event->type) {
	case SDL_KEYDOWN:
	case SDL_KEYUP:
#if SDL_MAJOR_VERSION >= 2
		if(event->key.repeat)
			break;
#endif
		if(event->type == SDL_KEYDOWN) {
			state = SYSTEM_KEYSTATE_DOWN;
		} else {
			state = SYSTEM_KEYSTATE_UP;
		}

#if SDL_MAJOR_VERSION >= 2
		key = SystemInputIdFromSDL2Key((int)event->key.keysym.sym);
#else
		key = SystemInputIdFromSDL1Key(event->key.keysym.sym);
#endif
		if(key != GLTRON_INPUT_INVALID)
			dispatchKeyboard(state, key);
		break;
	case SDL_JOYAXISMOTION:
#if SDL_MAJOR_VERSION >= 2
		slot = SystemInputJoystickSlotForInstance((int)event->jaxis.which);
#else
		slot = (int)event->jaxis.which;
#endif
		count = SystemTranslateJoystickAxis(&joystick_translator,
			slot, (int)event->jaxis.axis,
			(int)event->jaxis.value, joystick_threshold, transitions);
		for(i = 0; i < count; i++)
			dispatchKeyboardTransition(&transitions[i]);
		break;
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		if(event->type == SDL_JOYBUTTONDOWN)
			state = SYSTEM_KEYSTATE_DOWN;
		else
			state = SYSTEM_KEYSTATE_UP;

#if SDL_MAJOR_VERSION >= 2
		slot = SystemInputJoystickSlotForInstance((int)event->jbutton.which);
#else
		slot = (int)event->jbutton.which;
#endif
		dispatchJoystickButton(slot, (int)event->jbutton.button, state);
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		state = event->type == SDL_MOUSEBUTTONDOWN ?
			SYSTEM_MOUSEPRESSED : SYSTEM_MOUSERELEASED;
		button = (int)event->button.button;
#if SDL_MAJOR_VERSION >= 2
		button = legacyMouseButtonFromSDL2(button);
#endif
		SystemMouse(button, state,
							event->button.x, event->button.y);
		break;
	case SDL_MOUSEMOTION:
		SystemInputTranslateMouseMotion(event->motion.x, event->motion.y,
			event->motion.xrel, event->motion.yrel, &mouse_x, &mouse_y);
		SystemMouseMotion(mouse_x, mouse_y);
		break;
#if SDL_MAJOR_VERSION >= 2
	case SDL_MOUSEWHEEL:
		dispatchSDL2MouseWheel(&event->wheel);
		break;
	case SDL_JOYDEVICEADDED:
		openSDL2Joystick((int)event->jdevice.which);
		break;
	case SDL_JOYDEVICEREMOVED:
		SystemInputRemoveJoystickInstance((int)event->jdevice.which);
		break;
#endif
	default:
		break;
	}
}

void SystemSetJoyThreshold(float f) { 
	joystick_threshold = SystemClampJoyThreshold(f);
}
