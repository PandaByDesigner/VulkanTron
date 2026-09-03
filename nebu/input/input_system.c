#include "input/nebu_input_system.h"
#include "input/nebu_system_keynames.h"
#include "base/nebu_system.h"

#include "SDL.h"
#include <stdlib.h>
#include <string.h>

static float joystick_threshold = 0;
static SystemJoystickTranslator joystick_translator;

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
}

int SystemResetJoySlot(int slot) {
	return SystemJoystickTranslatorResetSlot(&joystick_translator, slot);
}

void SystemGrabInput() {
  SDL_WM_GrabInput(SDL_GRAB_ON);
}

void SystemUngrabInput() {
  SDL_WM_GrabInput(SDL_GRAB_OFF);
}

void SystemWarpPointer(int x, int y) {
  SDL_WarpMouse(x, y);
}

void SystemHidePointer() {
  SDL_ShowCursor(SDL_DISABLE);
}

void SystemUnhidePointer() {
  SDL_ShowCursor(SDL_ENABLE);
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

void SystemHandleInput(SDL_Event *event) {
	int count;
	int i;
	int state;
	SystemInputId key;
	SystemInputTransition transitions[2];

	if(event == NULL)
		return;

	switch(event->type) {
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		if(event->type == SDL_KEYDOWN) {
			state = SYSTEM_KEYSTATE_DOWN;
		} else {
			state = SYSTEM_KEYSTATE_UP;
		}
 
		key = SystemInputIdFromSDL1Key(event->key.keysym.sym);
		if(key != GLTRON_INPUT_INVALID)
			dispatchKeyboard(state, key);
		break;
	case SDL_JOYAXISMOTION:
		count = SystemTranslateJoystickAxis(&joystick_translator,
			(int)event->jaxis.which, (int)event->jaxis.axis,
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
		
		if(SystemTranslateJoystickButton((int)event->jbutton.which,
				(int)event->jbutton.button, state, &transitions[0]))
			dispatchKeyboardTransition(&transitions[0]);
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		SystemMouse(event->button.button, event->button.state, 
								event->button.x, event->button.y);
		break;
	case SDL_MOUSEMOTION:
		SystemMouseMotion(event->motion.x, event->motion.y);
		break;
	}
}

void SystemSetJoyThreshold(float f) { 
	joystick_threshold = SystemClampJoyThreshold(f);
}
