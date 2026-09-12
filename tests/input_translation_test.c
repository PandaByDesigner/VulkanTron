#include "input/nebu_input_system.h"
#include "base/nebu_system.h"

#ifdef GLTRON_USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if SDL_MAJOR_VERSION >= 3
#define INPUT_KEY_DOWN SDL_EVENT_KEY_DOWN
#define INPUT_KEY_UP SDL_EVENT_KEY_UP
#define INPUT_JOY_AXIS SDL_EVENT_JOYSTICK_AXIS_MOTION
#define INPUT_JOY_BUTTON_DOWN SDL_EVENT_JOYSTICK_BUTTON_DOWN
#define INPUT_JOY_BUTTON_UP SDL_EVENT_JOYSTICK_BUTTON_UP
#define INPUT_MOUSE_BUTTON_DOWN SDL_EVENT_MOUSE_BUTTON_DOWN
#define INPUT_MOUSE_BUTTON_UP SDL_EVENT_MOUSE_BUTTON_UP
#define INPUT_MOUSE_MOTION SDL_EVENT_MOUSE_MOTION
#define INPUT_MOUSE_WHEEL SDL_EVENT_MOUSE_WHEEL
#define INPUT_JOY_REMOVED SDL_EVENT_JOYSTICK_REMOVED
#define INPUT_KEY_A SDLK_A
#define nativeKeyToStable SystemInputIdFromSDL3Key
#else
#define INPUT_KEY_DOWN SDL_KEYDOWN
#define INPUT_KEY_UP SDL_KEYUP
#define INPUT_JOY_AXIS SDL_JOYAXISMOTION
#define INPUT_JOY_BUTTON_DOWN SDL_JOYBUTTONDOWN
#define INPUT_JOY_BUTTON_UP SDL_JOYBUTTONUP
#define INPUT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define INPUT_MOUSE_BUTTON_UP SDL_MOUSEBUTTONUP
#define INPUT_MOUSE_MOTION SDL_MOUSEMOTION
#define INPUT_KEY_A SDLK_a
#if SDL_MAJOR_VERSION >= 2
#define INPUT_MOUSE_WHEEL SDL_MOUSEWHEEL
#define INPUT_JOY_REMOVED SDL_JOYDEVICEREMOVED
#define nativeKeyToStable SystemInputIdFromSDL2Key
#endif
#endif

#define CAPTURE_MAX 32

typedef struct {
	int state;
	int id;
} CapturedKey;

typedef struct {
	int button;
	int state;
	int x;
	int y;
} CapturedMouse;

static CapturedKey captured_keys[CAPTURE_MAX];
static CapturedMouse captured_mouse[CAPTURE_MAX];
static int captured_key_count;
static int captured_mouse_count;
static int captured_mouse_button;
static int captured_mouse_state;
static int captured_mouse_x;
static int captured_mouse_y;
static int captured_motion_count;
static int captured_motion_x;
static int captured_motion_y;

Callbacks *current;

static int fail(const char *message, int line) {
	fprintf(stderr, "FAIL line %d: %s\n", line, message);
	return 1;
}

#define CHECK(condition, message) \
	do { if(!(condition)) return fail((message), __LINE__); } while(0)

static void captureKeyboard(int state, int key, int x, int y) {
	(void)x;
	(void)y;
	if(captured_key_count < CAPTURE_MAX) {
		captured_keys[captured_key_count].state = state;
		captured_keys[captured_key_count].id = key;
		captured_key_count++;
	}
}

static void captureMouse(int button, int state, int x, int y) {
	if(captured_mouse_count < CAPTURE_MAX) {
		captured_mouse[captured_mouse_count].button = button;
		captured_mouse[captured_mouse_count].state = state;
		captured_mouse[captured_mouse_count].x = x;
		captured_mouse[captured_mouse_count].y = y;
	}
	captured_mouse_count++;
	captured_mouse_button = button;
	captured_mouse_state = state;
	captured_mouse_x = x;
	captured_mouse_y = y;
}

static void captureMouseMotion(int x, int y) {
	captured_motion_count++;
	captured_motion_x = x;
	captured_motion_y = y;
}

static void resetCapture(void) {
	memset(captured_keys, 0, sizeof(captured_keys));
	memset(captured_mouse, 0, sizeof(captured_mouse));
	captured_key_count = 0;
	captured_mouse_count = 0;
	captured_motion_count = 0;
}

static int checkNumericAbi(void) {
	CHECK(GLTRON_INPUT_INVALID == -1, "invalid sentinel changed");
	CHECK(GLTRON_INPUT_KEY_UNKNOWN == 0, "unknown key ID changed");
	CHECK('a' == 97 && 'd' == 100 && 'q' == 113 && 'e' == 101 &&
			'w' == 119, "player 1 binding IDs changed");
	CHECK('j' == 106 && 'k' == 107 && 'u' == 117 && 'i' == 105 &&
			'l' == 108, "player 2 binding IDs changed");
	CHECK(GLTRON_INPUT_KEY_DELETE == 127, "delete ID changed");
	CHECK(GLTRON_INPUT_KEY_KP4 == 260, "keypad 4 ID changed");
	CHECK(GLTRON_INPUT_KEY_KP5 == 261, "keypad 5 ID changed");
	CHECK(GLTRON_INPUT_KEY_KP6 == 262, "keypad 6 ID changed");
	CHECK(GLTRON_INPUT_KEY_KP7 == 263, "keypad 7 ID changed");
	CHECK(GLTRON_INPUT_KEY_KP9 == 265, "keypad 9 ID changed");
	CHECK(GLTRON_INPUT_KEY_DOWN == 274, "down ID changed");
	CHECK(GLTRON_INPUT_KEY_RIGHT == 275, "right ID changed");
	CHECK(GLTRON_INPUT_KEY_LEFT == 276, "left ID changed");
	CHECK(GLTRON_INPUT_KEY_END == 279, "end ID changed");
	CHECK(GLTRON_INPUT_KEY_F1 == 282, "F1 ID changed");
	CHECK(GLTRON_INPUT_KEY_F12 == 293, "F12 ID changed");
	CHECK(GLTRON_INPUT_KEY_UNDO == 322, "last keyboard ID changed");
	CHECK(GLTRON_INPUT_JOY_LEFT == 512, "joy0 left ID changed");
	CHECK(GLTRON_INPUT_JOY_BUTTON_19 == 535,
			"joy0 button 19 ID changed");
	CHECK(GLTRON_INPUT_JOY_LEFT + GLTRON_INPUT_JOY_OFFSET == 544,
			"joy1 left ID changed");
	CHECK(GLTRON_INPUT_JOY_BUTTON_19 + GLTRON_INPUT_JOY_OFFSET == 567,
			"joy1 button 19 ID changed");
	CHECK(SYSTEM_KEY_LEFT == 276 && SYSTEM_JOY_BUTTON_0 == 516,
			"legacy aliases changed");
	CHECK(SYSTEM_MOUSERELEASED == 0 && SYSTEM_MOUSEPRESSED == 1 &&
			SYSTEM_MOUSEBUTTON_LEFT == 1 && SYSTEM_MOUSEBUTTON_RIGHT == 3,
			"project-owned mouse callback ABI changed");
	return 0;
}

static int checkThresholds(void) {
	CHECK(SystemClampJoyThreshold(-1.0f) == 0.0f,
			"negative threshold was not clamped");
	CHECK(SystemClampJoyThreshold(NAN) == 0.0f,
			"NaN threshold was not made finite");
	CHECK(SystemClampJoyThreshold(-INFINITY) == 0.0f,
			"negative infinite threshold was not clamped");
	CHECK(SystemClampJoyThreshold(0.4f) == 0.4f,
			"valid threshold changed");
	CHECK(SystemClampJoyThreshold(0.95f) == 0.95f,
			"maximum valid threshold changed");
	CHECK(SystemClampJoyThreshold(1.0f) == 0.95f,
			"large threshold was not clamped");
	CHECK(SystemClampJoyThreshold(INFINITY) == 0.95f,
			"infinite threshold was not clamped");
	return 0;
}

static int checkPureJoystickTranslation(void) {
	SystemJoystickTranslator translator;
	SystemInputTransition transition[2];
	int axis;
	int button;
	int count;
	int expected;
	int slot;

	memset(&translator, 0x7f, sizeof(translator));
	SystemJoystickTranslatorReset(&translator);
	CHECK(translator.direction[0][0] == 0 &&
			translator.direction[1][1] == 0, "translator reset failed");
	CHECK(!SystemJoystickTranslatorResetSlot(NULL, 0),
			"NULL translator reset succeeded");
	CHECK(!SystemJoystickTranslatorResetSlot(&translator, -1),
			"negative slot reset succeeded");
	CHECK(!SystemJoystickTranslatorResetSlot(&translator, 2),
			"out-of-range slot reset succeeded");

	count = SystemTranslateJoystickAxis(&translator, 0, 0, -3277,
			0.10f, transition);
	CHECK(count == 1, "left press did not emit one transition");
	CHECK(transition[0].state == SYSTEM_KEYSTATE_DOWN &&
			transition[0].id == SYSTEM_JOY_LEFT, "left press mapped incorrectly");
	CHECK(SystemTranslateJoystickAxis(&translator, 0, 0, -20000,
			0.10f, transition) == 0, "repeated direction was not suppressed");

	count = SystemTranslateJoystickAxis(&translator, 0, 0, 20000,
			0.10f, transition);
	CHECK(count == 2, "direct reversal did not emit two transitions");
	CHECK(transition[0].state == SYSTEM_KEYSTATE_UP &&
			transition[0].id == SYSTEM_JOY_LEFT,
			"direct reversal did not release old direction first");
	CHECK(transition[1].state == SYSTEM_KEYSTATE_DOWN &&
			transition[1].id == SYSTEM_JOY_RIGHT,
			"direct reversal did not press new direction second");

	count = SystemTranslateJoystickAxis(&translator, 0, 0, 3276,
			0.10f, transition);
	CHECK(count == 1 && transition[0].state == SYSTEM_KEYSTATE_UP &&
			transition[0].id == SYSTEM_JOY_RIGHT,
			"dead-zone return mapped incorrectly");
	CHECK(SystemTranslateJoystickAxis(&translator, 0, 0, 3276,
			0.10f, transition) == 0, "center state repeated a release");

	count = SystemTranslateJoystickAxis(&translator, 1, 1, -32768,
			0.10f, transition);
	CHECK(count == 1 && transition[0].id == SYSTEM_JOY_UP + SYSTEM_JOY_OFFSET,
			"joy1 up mapped incorrectly");
	count = SystemTranslateJoystickAxis(&translator, 1, 1, 32767,
			0.10f, transition);
	CHECK(count == 2 && transition[0].id == SYSTEM_JOY_UP + SYSTEM_JOY_OFFSET &&
			transition[1].id == SYSTEM_JOY_DOWN + SYSTEM_JOY_OFFSET,
			"joy1 vertical reversal mapped incorrectly");
	CHECK(SystemJoystickTranslatorResetSlot(&translator, 1),
			"valid slot reset failed");
	CHECK(translator.direction[1][1] == 0,
			"slot reset retained axis state");

	CHECK(SystemTranslateJoystickAxis(&translator, -1, 0, 32767,
			0.10f, transition) == 0, "negative slot was accepted");
	CHECK(SystemTranslateJoystickAxis(&translator, 2, 0, 32767,
			0.10f, transition) == 0, "third slot was accepted");
	CHECK(SystemTranslateJoystickAxis(&translator, 0, -1, 32767,
			0.10f, transition) == 0, "negative axis was accepted");
	CHECK(SystemTranslateJoystickAxis(&translator, 0, 2, 32767,
			0.10f, transition) == 0, "third axis was accepted");
	CHECK(SystemTranslateJoystickAxis(NULL, 0, 0, 32767,
			0.10f, transition) == 0, "NULL translator was accepted");
	CHECK(SystemTranslateJoystickAxis(&translator, 0, 0, 32767,
			0.10f, NULL) == 0, "NULL transition array was accepted");

	CHECK(SystemTranslateJoystickButton(0, 0, SYSTEM_KEYSTATE_DOWN,
			&transition[0]) == 1 && transition[0].id == SYSTEM_JOY_BUTTON_0,
			"joy0 button 0 mapped incorrectly");
	CHECK(SystemTranslateJoystickButton(0, 19, SYSTEM_KEYSTATE_UP,
			&transition[0]) == 1 && transition[0].id == SYSTEM_JOY_BUTTON_19,
			"joy0 button 19 mapped incorrectly");
	CHECK(SystemTranslateJoystickButton(1, 0, SYSTEM_KEYSTATE_DOWN,
			&transition[0]) == 1 &&
			transition[0].id == SYSTEM_JOY_BUTTON_0 + SYSTEM_JOY_OFFSET,
			"joy1 button 0 mapped incorrectly");
	CHECK(SystemTranslateJoystickButton(1, 19, SYSTEM_KEYSTATE_UP,
			&transition[0]) == 1 &&
			transition[0].id == SYSTEM_JOY_BUTTON_19 + SYSTEM_JOY_OFFSET,
			"joy1 button 19 mapped incorrectly");
	CHECK(!SystemTranslateJoystickButton(2, 0, SYSTEM_KEYSTATE_DOWN,
			&transition[0]), "third joystick button was accepted");
	CHECK(!SystemTranslateJoystickButton(0, 20, SYSTEM_KEYSTATE_DOWN,
			&transition[0]), "button 20 was accepted");
	CHECK(!SystemTranslateJoystickButton(0, 28, SYSTEM_KEYSTATE_DOWN,
			&transition[0]), "colliding button 28 was accepted");
	CHECK(!SystemTranslateJoystickButton(0, 0, 2, &transition[0]),
			"invalid button state was accepted");
	CHECK(!SystemTranslateJoystickButton(0, 0, SYSTEM_KEYSTATE_DOWN, NULL),
			"NULL button transition was accepted");

	for(slot = 0; slot < GLTRON_INPUT_JOY_SLOT_COUNT; slot++) {
		for(axis = 0; axis < GLTRON_INPUT_JOY_AXIS_COUNT; axis++) {
			SystemJoystickTranslatorReset(&translator);
			expected = GLTRON_INPUT_JOY_LEFT +
				slot * GLTRON_INPUT_JOY_OFFSET + axis * 2;
			count = SystemTranslateJoystickAxis(&translator, slot, axis,
				-32768, 0.0f, transition);
			CHECK(count == 1 && transition[0].id == expected &&
					transition[0].state == GLTRON_INPUT_STATE_DOWN,
					"valid axis negative mapping changed");
			count = SystemTranslateJoystickAxis(&translator, slot, axis,
				32767, 0.0f, transition);
			CHECK(count == 2 && transition[0].id == expected &&
					transition[0].state == GLTRON_INPUT_STATE_UP &&
					transition[1].id == expected + 1 &&
					transition[1].state == GLTRON_INPUT_STATE_DOWN,
					"valid axis positive mapping or reversal order changed");
		}
		for(button = 0; button < GLTRON_INPUT_JOY_BUTTON_COUNT; button++) {
			expected = GLTRON_INPUT_JOY_BUTTON_0 +
				slot * GLTRON_INPUT_JOY_OFFSET + button;
			CHECK(SystemTranslateJoystickButton(slot, button,
					GLTRON_INPUT_STATE_DOWN, &transition[0]) == 1 &&
					transition[0].id == expected,
					"valid joystick button mapping changed");
		}
	}
	for(slot = GLTRON_INPUT_JOY_SLOT_COUNT; slot <= 255; slot++) {
		CHECK(SystemTranslateJoystickAxis(&translator, slot, 0, 32767,
				0.0f, transition) == 0,
				"unsupported joystick slot produced an axis transition");
		CHECK(SystemTranslateJoystickButton(slot, 0,
				GLTRON_INPUT_STATE_DOWN, &transition[0]) == 0,
				"unsupported joystick slot produced a button transition");
	}
	for(axis = GLTRON_INPUT_JOY_AXIS_COUNT; axis <= 255; axis++) {
		CHECK(SystemTranslateJoystickAxis(&translator, 0, axis, 32767,
				0.0f, transition) == 0,
				"unsupported joystick axis produced a transition");
	}
	for(button = GLTRON_INPUT_JOY_BUTTON_COUNT; button <= 255; button++) {
		CHECK(SystemTranslateJoystickButton(0, button,
				GLTRON_INPUT_STATE_DOWN, &transition[0]) == 0,
				"unsupported joystick button produced a transition");
	}
	return 0;
}

static int checkKeyboardAdapter(void) {
#if SDL_MAJOR_VERSION >= 2
	static const struct {
		int native_key;
		SystemInputId stable_key;
	} special_keys[] = {
		{ SDLK_CLEAR, GLTRON_INPUT_KEY_CLEAR },
		{ SDLK_PAUSE, GLTRON_INPUT_KEY_PAUSE },
		{ SDLK_KP_0, GLTRON_INPUT_KEY_KP0 },
		{ SDLK_KP_1, GLTRON_INPUT_KEY_KP1 },
		{ SDLK_KP_2, GLTRON_INPUT_KEY_KP2 },
		{ SDLK_KP_3, GLTRON_INPUT_KEY_KP3 },
		{ SDLK_KP_4, GLTRON_INPUT_KEY_KP4 },
		{ SDLK_KP_5, GLTRON_INPUT_KEY_KP5 },
		{ SDLK_KP_6, GLTRON_INPUT_KEY_KP6 },
		{ SDLK_KP_7, GLTRON_INPUT_KEY_KP7 },
		{ SDLK_KP_8, GLTRON_INPUT_KEY_KP8 },
		{ SDLK_KP_9, GLTRON_INPUT_KEY_KP9 },
		{ SDLK_KP_PERIOD, GLTRON_INPUT_KEY_KP_PERIOD },
		{ SDLK_KP_DIVIDE, GLTRON_INPUT_KEY_KP_DIVIDE },
		{ SDLK_KP_MULTIPLY, GLTRON_INPUT_KEY_KP_MULTIPLY },
		{ SDLK_KP_MINUS, GLTRON_INPUT_KEY_KP_MINUS },
		{ SDLK_KP_PLUS, GLTRON_INPUT_KEY_KP_PLUS },
		{ SDLK_KP_ENTER, GLTRON_INPUT_KEY_KP_ENTER },
		{ SDLK_KP_EQUALS, GLTRON_INPUT_KEY_KP_EQUALS },
		{ SDLK_UP, GLTRON_INPUT_KEY_UP },
		{ SDLK_DOWN, GLTRON_INPUT_KEY_DOWN },
		{ SDLK_RIGHT, GLTRON_INPUT_KEY_RIGHT },
		{ SDLK_LEFT, GLTRON_INPUT_KEY_LEFT },
		{ SDLK_INSERT, GLTRON_INPUT_KEY_INSERT },
		{ SDLK_HOME, GLTRON_INPUT_KEY_HOME },
		{ SDLK_END, GLTRON_INPUT_KEY_END },
		{ SDLK_PAGEUP, GLTRON_INPUT_KEY_PAGEUP },
		{ SDLK_PAGEDOWN, GLTRON_INPUT_KEY_PAGEDOWN },
		{ SDLK_F1, GLTRON_INPUT_KEY_F1 },
		{ SDLK_F2, GLTRON_INPUT_KEY_F2 },
		{ SDLK_F3, GLTRON_INPUT_KEY_F3 },
		{ SDLK_F4, GLTRON_INPUT_KEY_F4 },
		{ SDLK_F5, GLTRON_INPUT_KEY_F5 },
		{ SDLK_F6, GLTRON_INPUT_KEY_F6 },
		{ SDLK_F7, GLTRON_INPUT_KEY_F7 },
		{ SDLK_F8, GLTRON_INPUT_KEY_F8 },
		{ SDLK_F9, GLTRON_INPUT_KEY_F9 },
		{ SDLK_F10, GLTRON_INPUT_KEY_F10 },
		{ SDLK_F11, GLTRON_INPUT_KEY_F11 },
		{ SDLK_F12, GLTRON_INPUT_KEY_F12 },
		{ SDLK_F13, GLTRON_INPUT_KEY_F13 },
		{ SDLK_F14, GLTRON_INPUT_KEY_F14 },
		{ SDLK_F15, GLTRON_INPUT_KEY_F15 },
		{ SDLK_NUMLOCKCLEAR, GLTRON_INPUT_KEY_NUMLOCK },
		{ SDLK_CAPSLOCK, GLTRON_INPUT_KEY_CAPSLOCK },
		{ SDLK_SCROLLLOCK, GLTRON_INPUT_KEY_SCROLLOCK },
		{ SDLK_RSHIFT, GLTRON_INPUT_KEY_RSHIFT },
		{ SDLK_LSHIFT, GLTRON_INPUT_KEY_LSHIFT },
		{ SDLK_RCTRL, GLTRON_INPUT_KEY_RCTRL },
		{ SDLK_LCTRL, GLTRON_INPUT_KEY_LCTRL },
		{ SDLK_RALT, GLTRON_INPUT_KEY_RALT },
		{ SDLK_LALT, GLTRON_INPUT_KEY_LALT },
		{ SDLK_RGUI, GLTRON_INPUT_KEY_RMETA },
		{ SDLK_LGUI, GLTRON_INPUT_KEY_LMETA },
		{ SDLK_MODE, GLTRON_INPUT_KEY_MODE },
		{ SDLK_APPLICATION, GLTRON_INPUT_KEY_MENU },
		{ SDLK_HELP, GLTRON_INPUT_KEY_HELP },
		{ SDLK_PRINTSCREEN, GLTRON_INPUT_KEY_PRINT },
		{ SDLK_SYSREQ, GLTRON_INPUT_KEY_SYSREQ },
		{ SDLK_MENU, GLTRON_INPUT_KEY_MENU },
		{ SDLK_POWER, GLTRON_INPUT_KEY_POWER },
		{ 0x20ac, GLTRON_INPUT_KEY_EURO },
		{ SDLK_UNDO, GLTRON_INPUT_KEY_UNDO }
	};
	static const struct {
		int unicode_key;
		SystemInputId stable_key;
	} world_keys[] = {
		{ 0x0104, 0xa1 }, /* Latin-2 */
		{ 0x0141, 0xa3 },
		{ 0x0100, 0xc0 }, /* Latin-4 */
		{ 0x3002, 0xa1 }, /* Katakana */
		{ 0x060c, 0xac }, /* Arabic */
		{ 0x0430, 0xc1 }, /* Cyrillic */
		{ 0x0391, 0xc1 }, /* Greek */
		{ 0x2264, 0xbc }, /* Technical */
		{ 0x2003, 0xa1 }, /* Publishing */
		{ 0x05d0, 0xe0 }, /* Hebrew */
		{ 0x0e01, 0xa1 }  /* Thai */
	};
	size_t i;
	int key;

	for(key = GLTRON_INPUT_KEY_UNKNOWN;
		key <= GLTRON_INPUT_KEY_WORLD_LAST; key++) {
		CHECK(nativeKeyToStable(key) == key,
			"SDL2 ASCII/Latin-1 mapping changed");
	}
	for(i = 0; i < sizeof(special_keys) / sizeof(special_keys[0]); i++) {
		CHECK(nativeKeyToStable(special_keys[i].native_key) ==
				special_keys[i].stable_key,
			"SDL2 special-key mapping changed");
	}
	for(i = 0; i < sizeof(world_keys) / sizeof(world_keys[0]); i++) {
		CHECK(nativeKeyToStable(world_keys[i].unicode_key) ==
				world_keys[i].stable_key,
			"SDL2 international WORLD-key mapping changed");
	}
	CHECK(nativeKeyToStable(-1) == GLTRON_INPUT_INVALID,
			"negative SDL2 key was accepted");
	CHECK(nativeKeyToStable(SDLK_CURRENCYUNIT) == GLTRON_INPUT_INVALID,
			"generic currency key was guessed as the Euro key");
	CHECK(nativeKeyToStable(0x1f600) == GLTRON_INPUT_INVALID,
			"unmapped SDL2 Unicode key entered the classic ID space");
	CHECK(nativeKeyToStable(SDLK_F16) == GLTRON_INPUT_INVALID,
			"SDL2-only function key entered the classic ID space");
	CHECK(nativeKeyToStable(SDLK_CANCEL) == GLTRON_INPUT_INVALID,
			"SDL2 cancel key was guessed as the classic break key");
	CHECK(nativeKeyToStable(INT_MAX) == GLTRON_INPUT_INVALID,
			"out-of-range SDL2 key was accepted");
#if SDL_MAJOR_VERSION >= 3
	CHECK(nativeKeyToStable(UINT32_MAX) == GLTRON_INPUT_INVALID &&
			nativeKeyToStable(0x80000000u) == GLTRON_INPUT_INVALID,
			"SDL3 unsigned key overflow entered the classic ID space");
	CHECK(nativeKeyToStable(SDLK_MULTI_KEY_COMPOSE) == GLTRON_INPUT_KEY_COMPOSE &&
			nativeKeyToStable(SDLK_LMETA) == GLTRON_INPUT_KEY_LMETA &&
			nativeKeyToStable(SDLK_RMETA) == GLTRON_INPUT_KEY_RMETA,
			"SDL3 explicit extended keys did not retain classic IDs");
#endif
#else
	CHECK(SystemInputIdFromSDL1Key(INPUT_KEY_A) == 97, "letter mapping changed");
	CHECK(SystemInputIdFromSDL1Key(SDLK_SPACE) == 32, "space mapping changed");
	CHECK(SystemInputIdFromSDL1Key(SDLK_ESCAPE) == 27,
			"escape mapping changed");
	CHECK(SystemInputIdFromSDL1Key(SDLK_RETURN) == 13,
			"return mapping changed");
	CHECK(SystemInputIdFromSDL1Key(SDLK_DELETE) == 127,
			"delete mapping changed");
	CHECK(SystemInputIdFromSDL1Key(SDLK_KP4) == 260,
			"keypad mapping changed");
	CHECK(SystemInputIdFromSDL1Key(SDLK_LEFT) == 276,
			"arrow mapping changed");
	CHECK(SystemInputIdFromSDL1Key(SDLK_F1) == 282,
			"function-key mapping changed");
	CHECK(SystemInputIdFromSDL1Key(SDLK_UNKNOWN) == GLTRON_INPUT_KEY_UNKNOWN,
			"SDL unknown key mapping changed");
	CHECK(SystemInputIdFromSDL1Key(-1) == GLTRON_INPUT_INVALID,
			"negative backend key was accepted");
	CHECK(SystemInputIdFromSDL1Key(323) == GLTRON_INPUT_INVALID,
			"out-of-range backend key was accepted");
#endif
	CHECK(strcmp(SystemGetKeyName(97), "a") == 0,
			"letter name is not project-owned SDL1 text");
	CHECK(strcmp(SystemGetKeyName(GLTRON_INPUT_KEY_SPACE), "space") == 0,
			"space name changed");
	CHECK(strcmp(SystemGetKeyName(GLTRON_INPUT_KEY_WORLD_FIRST), "world 0") == 0,
			"first world-key name changed");
	CHECK(strcmp(SystemGetKeyName(GLTRON_INPUT_KEY_WORLD_LAST), "world 95") == 0,
			"last world-key name changed");
	CHECK(strcmp(SystemGetKeyName(GLTRON_INPUT_KEY_KP4), "[4]") == 0,
			"keypad name changed");
	CHECK(strcmp(SystemGetKeyName(GLTRON_INPUT_KEY_LEFT), "left") == 0,
			"arrow name changed");
	CHECK(strcmp(SystemGetKeyName(GLTRON_INPUT_KEY_F12), "f12") == 0,
			"function-key name changed");
	CHECK(strcmp(SystemGetKeyName(GLTRON_INPUT_KEY_MODE), "alt gr") == 0,
			"modifier name changed");
	CHECK(strcmp(SystemGetKeyName(59), ";") == 0,
			"canonical SDL1 semicolon name changed");
	CHECK(strcmp(SystemGetKeyName(37), "unknown key") == 0,
			"unused SDL1 key gap received a name");
	CHECK(strcmp(SystemGetKeyName(123), "unknown key") == 0 &&
			strcmp(SystemGetKeyName(124), "unknown key") == 0 &&
			strcmp(SystemGetKeyName(125), "unknown key") == 0 &&
			strcmp(SystemGetKeyName(126), "unknown key") == 0,
			"non-SDL1 printable gaps received names");
	CHECK(strcmp(SystemGetKeyName(SYSTEM_JOY_LEFT), "joy0 left") == 0,
			"joy0 name changed");
	CHECK(strcmp(SystemGetKeyName(SYSTEM_JOY_BUTTON_19 + SYSTEM_JOY_OFFSET),
			"joy1 button 19") == 0, "joy1 name changed");
	CHECK(strcmp(SystemGetKeyName(400), "unknown key") == 0,
			"unknown legacy key fallback changed");
	return 0;
}

static void makeKeyEvent(SDL_Event *event, int type, int key, int repeat) {
	memset(event, 0, sizeof(*event));
	event->type = (Uint32)type;
#if SDL_MAJOR_VERSION >= 3
	event->key.key = (SDL_Keycode)key;
	event->key.repeat = repeat != 0;
#elif SDL_MAJOR_VERSION >= 2
	event->key.keysym.sym = (SDL_Keycode)key;
	event->key.repeat = (Uint8)repeat;
#else
	event->key.keysym.sym = (SDLKey)key;
	(void)repeat;
#endif
}

static int checkSystemDispatch(void) {
	Callbacks callbacks;
	SDL_Event event;
	int joy0 = 0;
	int joy1 = 1;
	int invalid_joy = 2;
#if SDL_MAJOR_VERSION >= 2
	int joy2;
#endif

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.keyboard = captureKeyboard;
	callbacks.mouse = captureMouse;
	callbacks.mouseMotion = captureMouseMotion;

	current = NULL;
	makeKeyEvent(&event, INPUT_KEY_DOWN, INPUT_KEY_A, 0);
	SystemHandleInputEvent(NULL);
	SystemHandleInputEvent(&event);

	current = &callbacks;
	resetCapture();
	makeKeyEvent(&event, INPUT_KEY_DOWN, -1, 0);
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 0,
			"out-of-domain SDL1 key emitted a stable ID");

	makeKeyEvent(&event, INPUT_KEY_DOWN, SDLK_UNKNOWN, 0);
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 1 &&
			captured_keys[0].id == GLTRON_INPUT_KEY_UNKNOWN,
			"classic SDL unknown-key dispatch changed");
	resetCapture();
	makeKeyEvent(&event, INPUT_KEY_DOWN, INPUT_KEY_A, 0);
	SystemHandleInputEvent(&event);
	makeKeyEvent(&event, INPUT_KEY_UP, INPUT_KEY_A, 0);
	SystemHandleInputEvent(&event);
	makeKeyEvent(&event, INPUT_KEY_DOWN, SDLK_LEFT, 0);
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 3, "keyboard dispatch count changed");
	CHECK(captured_keys[0].state == SYSTEM_KEYSTATE_DOWN &&
			captured_keys[0].id == 97, "keyboard down dispatch changed");
	CHECK(captured_keys[1].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[1].id == 97, "keyboard up dispatch changed");
	CHECK(captured_keys[2].id == SYSTEM_KEY_LEFT,
			"special keyboard dispatch bypassed stable adapter");

#if SDL_MAJOR_VERSION >= 2
	resetCapture();
	makeKeyEvent(&event, INPUT_KEY_DOWN, INPUT_KEY_A, 1);
	SystemHandleInputEvent(&event);
	makeKeyEvent(&event, INPUT_KEY_UP, INPUT_KEY_A, 1);
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 0, "SDL2 key repeat was not suppressed");

	SystemInputShutdown();
	joy0 = 1001;
	joy1 = 4107;
	joy2 = 9998;
	invalid_joy = 9999;
	CHECK(SystemInputAddJoystickInstance(-1) == -1,
			"negative SDL2 instance ID was accepted");
	CHECK(SystemInputAddJoystickInstance(INT64_MAX) == -1 &&
			SystemInputAddJoystickInstance((int64_t)UINT32_MAX + 1) == -1,
			"out-of-domain joystick instance ID was accepted");
#if SDL_MAJOR_VERSION >= 3
	CHECK(SystemInputAddJoystickInstance(0) == -1,
			"SDL3 invalid zero instance ID was accepted");
#endif
	CHECK(SystemInputJoystickSlotForInstance(-1) == -1 &&
			SystemInputRemoveJoystickInstance(-1) == -1,
			"negative SDL2 instance ID matched or removed a slot");
	CHECK(SystemInputAddJoystickInstance(joy0) == 0,
			"first SDL2 instance did not receive slot 0");
	CHECK(SystemInputAddJoystickInstance(joy1) == 1,
			"second SDL2 instance did not receive slot 1");
	CHECK(SystemInputAddJoystickInstance(joy0) == 0,
			"duplicate SDL2 instance changed slots");
	CHECK(SystemInputAddJoystickInstance(joy2) == -1,
			"third SDL2 instance displaced an active slot");
	CHECK(SystemInputJoystickSlotForInstance(joy0) == 0 &&
			SystemInputJoystickSlotForInstance(joy1) == 1 &&
			SystemInputJoystickSlotForInstance(invalid_joy) == -1,
			"SDL2 instance lookup changed");
#endif

	resetCapture();
	SystemResetJoyState();
	SystemSetJoyThreshold(0.10f);
	memset(&event, 0, sizeof(event));
	event.type = INPUT_JOY_AXIS;
	event.jaxis.which = joy0;
	event.jaxis.axis = 0;
	event.jaxis.value = -20000;
	SystemHandleInputEvent(&event);
	event.jaxis.value = 20000;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 3, "runtime reversal dispatch count changed");
	CHECK(captured_keys[0].id == SYSTEM_JOY_LEFT &&
			captured_keys[0].state == SYSTEM_KEYSTATE_DOWN,
			"runtime left press changed");
	CHECK(captured_keys[1].id == SYSTEM_JOY_LEFT &&
			captured_keys[1].state == SYSTEM_KEYSTATE_UP,
			"runtime reversal release changed");
	CHECK(captured_keys[2].id == SYSTEM_JOY_RIGHT &&
			captured_keys[2].state == SYSTEM_KEYSTATE_DOWN,
			"runtime reversal press changed");

	CHECK(SystemResetJoySlot(0), "runtime valid slot reset failed");
	CHECK(!SystemResetJoySlot(2), "runtime invalid slot reset succeeded");
	resetCapture();
	event.jaxis.value = -20000;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 1 && captured_keys[0].id == SYSTEM_JOY_LEFT,
			"runtime slot reset retained direction");

	resetCapture();
	event.jaxis.which = invalid_joy;
	SystemHandleInputEvent(&event);
	event.jaxis.which = joy0;
	event.jaxis.axis = 2;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 0, "invalid runtime axis produced an event");

	memset(&event, 0, sizeof(event));
	event.type = INPUT_JOY_BUTTON_DOWN;
	event.jbutton.which = joy0;
	event.jbutton.button = 20;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 0, "invalid runtime button produced an event");
	event.jbutton.which = joy1;
	event.jbutton.button = 19;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 1 &&
			captured_keys[0].id == SYSTEM_JOY_BUTTON_19 + SYSTEM_JOY_OFFSET,
			"valid runtime button mapped incorrectly");

#if SDL_MAJOR_VERSION >= 2
	SystemResetJoyState();
	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = INPUT_JOY_AXIS;
	event.jaxis.which = joy0;
	event.jaxis.axis = 0;
	event.jaxis.value = -20000;
	SystemHandleInputEvent(&event);
	event.type = INPUT_JOY_BUTTON_DOWN;
	event.jbutton.which = joy0;
	event.jbutton.button = 3;
	SystemHandleInputEvent(&event);
	resetCapture();
	CHECK(SystemInputRemoveJoystickInstance(joy0) == 0,
			"SDL2 instance removal returned the wrong slot");
	CHECK(captured_key_count == 2,
			"SDL2 removal did not release every held control");
	CHECK(captured_keys[0].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[0].id == SYSTEM_JOY_LEFT,
			"SDL2 removal did not release its active axis first");
	CHECK(captured_keys[1].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[1].id == SYSTEM_JOY_BUTTON_3,
			"SDL2 removal did not release its active button");
	CHECK(SystemInputJoystickSlotForInstance(joy1) == 1,
			"SDL2 removal renumbered the other controller");
	CHECK(SystemInputAddJoystickInstance(joy2) == 0,
			"SDL2 hotplug did not reuse the first free slot");
	CHECK(SystemInputRemoveJoystickInstance(joy0) == -1,
			"removed SDL2 instance remained registered");

	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = INPUT_JOY_AXIS;
	event.jaxis.which = joy0;
	event.jaxis.axis = 0;
	event.jaxis.value = -20000;
	SystemHandleInputEvent(&event);
	event.type = INPUT_JOY_BUTTON_DOWN;
	event.jbutton.which = joy0;
	event.jbutton.button = 3;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 0,
			"stale events from a removed SDL2 instance were dispatched");

	event.type = INPUT_JOY_AXIS;
	event.jaxis.which = joy2;
	event.jaxis.axis = 0;
	event.jaxis.value = -20000;
	SystemHandleInputEvent(&event);
	event.type = INPUT_JOY_BUTTON_DOWN;
	event.jbutton.which = joy2;
	event.jbutton.button = 3;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 2 &&
			captured_keys[0].state == SYSTEM_KEYSTATE_DOWN &&
			captured_keys[0].id == SYSTEM_JOY_LEFT &&
			captured_keys[1].state == SYSTEM_KEYSTATE_DOWN &&
			captured_keys[1].id == SYSTEM_JOY_BUTTON_3,
			"reused SDL2 slot retained stale state or routed incorrectly");

	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = INPUT_JOY_REMOVED;
	event.jdevice.which = joy2;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 2 &&
			captured_keys[0].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[0].id == SYSTEM_JOY_LEFT &&
			captured_keys[1].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[1].id == SYSTEM_JOY_BUTTON_3,
			"SDL2 removal event did not release reused-slot controls");
	CHECK(SystemInputJoystickSlotForInstance(joy2) == -1 &&
			SystemInputJoystickSlotForInstance(joy1) == 1,
			"SDL2 removal event changed the wrong instance slot");
#endif

	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = INPUT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_LEFT;
#if SDL_MAJOR_VERSION >= 3
	event.button.down = true;
#else
	event.button.state = SDL_PRESSED;
#endif
	event.button.x = 17;
	event.button.y = 23;
	SystemHandleInputEvent(&event);
	CHECK(captured_mouse_count == 1 &&
			captured_mouse_button == SDL_BUTTON_LEFT &&
			captured_mouse_state == SYSTEM_MOUSEPRESSED &&
			captured_mouse_x == 17 && captured_mouse_y == 23,
			"mouse button semantics changed");

#if SDL_MAJOR_VERSION >= 2
	{
		int mouse_x;
		int mouse_y;

#if SDL_MAJOR_VERSION >= 3
		{
			float x;
			float y;
			SDL_GetMouseState(&x, &y);
			mouse_x = (int)x;
			mouse_y = (int)y;
		}
#else
		SDL_GetMouseState(&mouse_x, &mouse_y);
#endif
		resetCapture();
		memset(&event, 0, sizeof(event));
		event.type = INPUT_MOUSE_WHEEL;
		event.wheel.x = 3;
		event.wheel.y = 2;
		event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
		SystemHandleInputEvent(&event);
		CHECK(captured_mouse_count == 2,
				"upward wheel event did not emit one legacy click");
		CHECK(captured_mouse[0].button == 4 &&
				captured_mouse[0].state == SYSTEM_MOUSEPRESSED &&
				captured_mouse[1].button == 4 &&
				captured_mouse[1].state == SYSTEM_MOUSERELEASED,
				"upward wheel clicks did not use classic button 4 ordering");
		CHECK(captured_mouse[0].x == mouse_x &&
				captured_mouse[0].y == mouse_y &&
				captured_mouse[1].x == mouse_x &&
				captured_mouse[1].y == mouse_y,
				"wheel clicks did not use the current pointer position");

		resetCapture();
		memset(&event, 0, sizeof(event));
		event.type = INPUT_MOUSE_WHEEL;
		event.wheel.y = -1;
		SystemHandleInputEvent(&event);
		CHECK(captured_mouse_count == 2 &&
				captured_mouse[0].button == 5 &&
				captured_mouse[0].state == SYSTEM_MOUSEPRESSED &&
				captured_mouse[1].button == 5 &&
				captured_mouse[1].state == SYSTEM_MOUSERELEASED,
				"downward wheel notch did not emit a classic button 5 click");

		resetCapture();
		memset(&event, 0, sizeof(event));
		event.type = INPUT_MOUSE_WHEEL;
		event.wheel.x = -2;
		event.wheel.y = 0;
		SystemHandleInputEvent(&event);
		CHECK(captured_mouse_count == 0,
				"horizontal-only SDL2 wheel motion reached SDL1 callbacks");
	}

	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = INPUT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_X1;
	event.button.x = 53;
	event.button.y = 59;
	SystemHandleInputEvent(&event);
	event.type = INPUT_MOUSE_BUTTON_UP;
	SystemHandleInputEvent(&event);
	event.type = INPUT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_X2;
	event.button.x = 61;
	event.button.y = 67;
	SystemHandleInputEvent(&event);
	event.type = INPUT_MOUSE_BUTTON_UP;
	SystemHandleInputEvent(&event);
	event.type = INPUT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_X2 + 1;
	event.button.x = 71;
	event.button.y = 73;
	SystemHandleInputEvent(&event);
	event.type = INPUT_MOUSE_BUTTON_UP;
	SystemHandleInputEvent(&event);
	CHECK(captured_mouse_count == 6 &&
			captured_mouse[0].button == 6 &&
			captured_mouse[0].state == SYSTEM_MOUSEPRESSED &&
			captured_mouse[0].x == 53 && captured_mouse[0].y == 59 &&
			captured_mouse[1].button == 6 &&
			captured_mouse[1].state == SYSTEM_MOUSERELEASED &&
			captured_mouse[2].button == 7 &&
			captured_mouse[2].state == SYSTEM_MOUSEPRESSED &&
			captured_mouse[2].x == 61 && captured_mouse[2].y == 67 &&
			captured_mouse[3].button == 7 &&
			captured_mouse[3].state == SYSTEM_MOUSERELEASED &&
			captured_mouse[4].button == 8 &&
			captured_mouse[4].state == SYSTEM_MOUSEPRESSED &&
			captured_mouse[4].x == 71 && captured_mouse[4].y == 73 &&
			captured_mouse[5].button == 8 &&
			captured_mouse[5].state == SYSTEM_MOUSERELEASED,
			"SDL2 extra buttons did not leave classic wheel IDs reserved");
#endif

	resetCapture();
	SystemInputSetRelativeMouseMode(0);
	memset(&event, 0, sizeof(event));
	event.type = INPUT_MOUSE_MOTION;
	event.motion.x = 31;
	event.motion.y = 47;
	SystemHandleInputEvent(&event);
	CHECK(captured_motion_count == 1 && captured_motion_x == 31 &&
			captured_motion_y == 47, "mouse motion semantics changed");

#if SDL_MAJOR_VERSION >= 2
	resetCapture();
	SystemInputSetMouseAnchor(100, 100);
	SystemInputSetRelativeMouseMode(1);
	memset(&event, 0, sizeof(event));
	event.type = INPUT_MOUSE_MOTION;
	event.motion.x = 900;
	event.motion.y = 700;
	event.motion.xrel = 7;
	event.motion.yrel = -9;
	SystemHandleInputEvent(&event);
	CHECK(captured_motion_count == 1 && captured_motion_x == 107 &&
			captured_motion_y == 91,
			"SDL2 relative motion did not preserve centered callback semantics");
	SystemInputSetRelativeMouseMode(0);
#endif

	callbacks.keyboard = NULL;
	resetCapture();
	makeKeyEvent(&event, INPUT_KEY_DOWN, SDLK_F1, 0);
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 0, "NULL keyboard callback was invoked");

	callbacks.mouse = NULL;
	memset(&event, 0, sizeof(event));
	event.type = INPUT_MOUSE_BUTTON_UP;
	SystemHandleInputEvent(&event);
	CHECK(captured_mouse_count == 0, "NULL mouse callback was invoked");

	callbacks.mouseMotion = NULL;
	event.type = INPUT_MOUSE_MOTION;
	SystemHandleInputEvent(&event);
	CHECK(captured_motion_count == 0,
			"NULL mouse-motion callback was invoked");
	SystemInputShutdown();
	current = NULL;
	return 0;
}

#if SDL_MAJOR_VERSION >= 3
static int checkSDL3Input(void) {
	Callbacks callbacks;
	SDL_Event event;
	int x;
	int y;
	int i;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.keyboard = captureKeyboard;
	callbacks.mouse = captureMouse;
	callbacks.mouseMotion = captureMouseMotion;
	current = &callbacks;
	SystemInputShutdown();
	CHECK(SystemInputAddJoystickInstance(UINT32_MAX) == 0 &&
			SystemInputAddJoystickInstance(0x80000000u) == 1,
			"SDL3 unsigned instance IDs could not own distinct slots");
	CHECK(SystemInputJoystickSlotForInstance(UINT32_MAX) == 0 &&
			SystemInputJoystickSlotForInstance(0x80000000u) == 1 &&
			SystemInputJoystickSlotForInstance(-1) == -1,
			"SDL3 high instance IDs were narrowed or confused with errors");
	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
	event.jbutton.which = UINT32_MAX;
	event.jbutton.button = 0;
	SystemHandleInputEvent(&event);
	event.jbutton.which = 0x80000000u;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 2 &&
			captured_keys[0].id == SYSTEM_JOY_BUTTON_0 &&
			captured_keys[1].id == SYSTEM_JOY_BUTTON_0 + SYSTEM_JOY_OFFSET,
			"SDL3 event dispatch narrowed full-range joystick IDs");
	event.type = SDL_EVENT_JOYSTICK_REMOVED;
	event.jdevice.which = UINT32_MAX;
	SystemHandleInputEvent(&event);
	CHECK(captured_key_count == 3 &&
			captured_keys[2].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[2].id == SYSTEM_JOY_BUTTON_0 &&
			SystemInputJoystickSlotForInstance(0x80000000u) == 1,
			"SDL3 high-ID removal did not release only its own controls");
	SystemInputShutdown();
	SystemInputSetRelativeMouseMode(1);
	SystemInputSetMouseAnchor(100, 100);
	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.x = 900.5f;
	event.motion.y = 700.5f;
	event.motion.xrel = 0.25f;
	event.motion.yrel = -0.25f;
	for(i = 0; i < 3; i++) {
		SystemHandleInputEvent(&event);
		CHECK(captured_motion_x == 100 && captured_motion_y == 100,
				"SDL3 fractional motion moved before a whole logical pixel");
	}
	SystemHandleInputEvent(&event);
	CHECK(captured_motion_x == 101 && captured_motion_y == 99,
			"SDL3 slow relative movement lost its fractional deltas");
	SystemHandleInputEvent(&event);
	SystemInputSetRelativeMouseMode(0);
	SystemHandleInputEvent(&event);
	CHECK(captured_motion_x == 900 && captured_motion_y == 700,
			"SDL3 absolute logical coordinates changed");
	SystemInputSetRelativeMouseMode(1);
	event.motion.xrel = 0.75f;
	event.motion.yrel = -0.75f;
	SystemHandleInputEvent(&event);
	CHECK(captured_motion_x == 100 && captured_motion_y == 100,
			"SDL3 relative mode reused fractions from an earlier grab");
	SystemInputSetMouseAnchor(INT_MAX, INT_MIN);
	SystemInputTranslateMouseMotion(0, 0, INT_MAX, INT_MIN, &x, &y);
	CHECK(x == INT_MAX && y == INT_MIN,
			"relative mouse translation overflowed integer coordinates");
	SystemInputSetRelativeMouseMode(0);
	event.motion.x = NAN;
	event.motion.y = INFINITY;
	SystemHandleInputEvent(&event);
	CHECK(captured_motion_x == 0 && captured_motion_y == 0,
			"nonfinite SDL3 mouse positions reached integer conversion");
	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.y = NAN;
	SystemHandleInputEvent(&event);
	CHECK(captured_mouse_count == 0,
			"nonfinite SDL3 wheel movement generated a legacy click");
	SystemInputShutdown();
	current = NULL;
	return 0;
}

/* Exercise SDL3 device enumeration and native open/event/close, beyond the
 * synthetic instance registry tests. Only events from our virtual device are
 * dispatched, so a developer's physical controllers cannot affect assertions. */
static int checkSDL3VirtualJoystick(void) {
	SDL_VirtualJoystickDesc desc;
	SDL_JoystickID instance;
	SDL_Joystick *handle;
	SDL_Event event;
	Callbacks callbacks;
	int i;
	CHECK(SDL_InitSubSystem(SDL_INIT_JOYSTICK),
			"could not initialize SDL3 joystick test subsystem");
	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = 2;
	desc.nbuttons = 20;
	desc.name = "GLTron regression controller";
	instance = SDL_AttachVirtualJoystick(&desc);
	CHECK(instance != 0, "could not attach SDL3 virtual joystick");
	/* Repeated init/shutdown must balance only the subsystem reference it owns. */
	for(i = 0; i < 2; i++) {
		SystemInputInit();
		CHECK(strcmp(SDL_GetHint(SDL_HINT_KEYCODE_OPTIONS), "none") == 0,
				"SDL3 keycode defaults still rewrite classic layout bindings");
		SystemInputShutdown();
		CHECK(SDL_IsJoystickVirtual(instance),
				"input shutdown destroyed another owner's joystick subsystem");
	}
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.keyboard = captureKeyboard;
	current = &callbacks;
	resetCapture();
	SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_JOYSTICK_ADDED;
	event.jdevice.which = instance;
	SystemHandleInputEvent(&event);
	CHECK(SystemInputJoystickSlotForInstance(instance) == 0,
			"native SDL3 hotplug did not open the instance into a fixed slot");
	handle = SDL_GetJoystickFromID(instance);
	CHECK(handle != NULL, "input adapter did not retain a native joystick handle");
	SDL_SetJoystickEventsEnabled(true);
	CHECK(SDL_SetJoystickVirtualAxis(handle, 0, -20000) &&
			SDL_SetJoystickVirtualButton(handle, 19, true),
			"could not set SDL3 virtual controls");
	SDL_UpdateJoysticks();
	while(SDL_PollEvent(&event)) {
		if((event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION &&
				event.jaxis.which == instance) ||
			 (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
				event.jbutton.which == instance))
			SystemHandleInputEvent(&event);
	}
	CHECK(captured_key_count == 2 &&
			captured_keys[0].id == SYSTEM_JOY_LEFT &&
			captured_keys[1].id == SYSTEM_JOY_BUTTON_19,
			"real SDL3 joystick events did not reach classic control callbacks");
	CHECK(SDL_DetachVirtualJoystick(instance),
			"could not detach SDL3 virtual joystick");
	/* Disconnect also enqueues neutral events: route the whole device stream. */
	while(SDL_PollEvent(&event)) {
		if((event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION &&
				event.jaxis.which == instance) ||
			 (event.type == SDL_EVENT_JOYSTICK_BUTTON_UP &&
				event.jbutton.which == instance) ||
			 (event.type == SDL_EVENT_JOYSTICK_REMOVED &&
				event.jdevice.which == instance))
			SystemHandleInputEvent(&event);
	}
	CHECK(captured_key_count == 4 &&
			captured_keys[2].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[2].id == SYSTEM_JOY_LEFT &&
			captured_keys[3].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[3].id == SYSTEM_JOY_BUTTON_19 &&
			SystemInputJoystickSlotForInstance(instance) == -1,
			"native SDL3 disconnect did not release controls and remove the slot");
	CHECK(SDL_GetJoystickFromID(instance) == NULL,
			"SDL3 removed joystick handle was not closed");
	SystemInputShutdown();
	current = NULL;
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	CHECK((SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK) == 0,
			"SDL3 input initialization leaked a joystick subsystem reference");
	return 0;
}
#endif

int main(void) {
	#if SDL_MAJOR_VERSION >= 3
	if(!SDL_Init(0)) {
#else
	if(SDL_Init(0) != 0) {
#endif
		fprintf(stderr, "FAIL: SDL initialization: %s\n", SDL_GetError());
		return 1;
	}
	if(checkNumericAbi() || checkThresholds() ||
		 checkPureJoystickTranslation() || checkKeyboardAdapter() ||
		 checkSystemDispatch()) {
		SDL_Quit();
		return 1;
	}
#if SDL_MAJOR_VERSION >= 3
	if(checkSDL3Input() || checkSDL3VirtualJoystick()) {
		SDL_Quit();
		return 1;
	}
#endif
	SDL_Quit();
#if SDL_MAJOR_VERSION >= 3
	printf("PASS: SDL3 adapter preserves stable IDs, repeat filtering, "
			 "fixed controller slots, and fractional mouse parity\n");
#elif SDL_MAJOR_VERSION >= 2
	printf("PASS: SDL2 adapter preserves stable IDs, repeat filtering, "
			 "fixed controller slots, and mouse parity\n");
#else
	printf("PASS: SDL1 adapter preserves stable IDs, two-slot controller "
			 "translation, and mouse parity\n");
#endif
	return 0;
}
