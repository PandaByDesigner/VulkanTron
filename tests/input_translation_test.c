#include "input/nebu_input_system.h"
#include "base/nebu_system.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_MAX 32

typedef struct {
	int state;
	int id;
} CapturedKey;

static CapturedKey captured_keys[CAPTURE_MAX];
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
	CHECK(SystemInputIdFromSDL1Key(SDLK_a) == 97, "letter mapping changed");
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

static void makeKeyEvent(SDL_Event *event, Uint8 type, SDLKey key) {
	memset(event, 0, sizeof(*event));
	event->type = type;
	event->key.keysym.sym = key;
}

static int checkSystemDispatch(void) {
	Callbacks callbacks;
	SDL_Event event;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.keyboard = captureKeyboard;
	callbacks.mouse = captureMouse;
	callbacks.mouseMotion = captureMouseMotion;

	current = NULL;
	makeKeyEvent(&event, SDL_KEYDOWN, SDLK_a);
	SystemHandleInput(NULL);
	SystemHandleInput(&event);

	current = &callbacks;
	resetCapture();
	makeKeyEvent(&event, SDL_KEYDOWN, (SDLKey)-1);
	SystemHandleInput(&event);
	CHECK(captured_key_count == 0,
			"out-of-domain SDL1 key emitted a stable ID");

	makeKeyEvent(&event, SDL_KEYDOWN, SDLK_UNKNOWN);
	SystemHandleInput(&event);
	CHECK(captured_key_count == 1 &&
			captured_keys[0].id == GLTRON_INPUT_KEY_UNKNOWN,
			"classic SDL unknown-key dispatch changed");
	resetCapture();
	makeKeyEvent(&event, SDL_KEYDOWN, SDLK_a);
	SystemHandleInput(&event);
	makeKeyEvent(&event, SDL_KEYUP, SDLK_a);
	SystemHandleInput(&event);
	makeKeyEvent(&event, SDL_KEYDOWN, SDLK_LEFT);
	SystemHandleInput(&event);
	CHECK(captured_key_count == 3, "keyboard dispatch count changed");
	CHECK(captured_keys[0].state == SYSTEM_KEYSTATE_DOWN &&
			captured_keys[0].id == 97, "keyboard down dispatch changed");
	CHECK(captured_keys[1].state == SYSTEM_KEYSTATE_UP &&
			captured_keys[1].id == 97, "keyboard up dispatch changed");
	CHECK(captured_keys[2].id == SYSTEM_KEY_LEFT,
			"special keyboard dispatch bypassed stable adapter");

	resetCapture();
	SystemResetJoyState();
	SystemSetJoyThreshold(0.10f);
	memset(&event, 0, sizeof(event));
	event.type = SDL_JOYAXISMOTION;
	event.jaxis.which = 0;
	event.jaxis.axis = 0;
	event.jaxis.value = -20000;
	SystemHandleInput(&event);
	event.jaxis.value = 20000;
	SystemHandleInput(&event);
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
	SystemHandleInput(&event);
	CHECK(captured_key_count == 1 && captured_keys[0].id == SYSTEM_JOY_LEFT,
			"runtime slot reset retained direction");

	resetCapture();
	event.jaxis.which = 2;
	SystemHandleInput(&event);
	event.jaxis.which = 0;
	event.jaxis.axis = 2;
	SystemHandleInput(&event);
	CHECK(captured_key_count == 0, "invalid runtime axis produced an event");

	memset(&event, 0, sizeof(event));
	event.type = SDL_JOYBUTTONDOWN;
	event.jbutton.which = 0;
	event.jbutton.button = 20;
	SystemHandleInput(&event);
	CHECK(captured_key_count == 0, "invalid runtime button produced an event");
	event.jbutton.which = 1;
	event.jbutton.button = 19;
	SystemHandleInput(&event);
	CHECK(captured_key_count == 1 &&
			captured_keys[0].id == SYSTEM_JOY_BUTTON_19 + SYSTEM_JOY_OFFSET,
			"valid runtime button mapped incorrectly");

	resetCapture();
	memset(&event, 0, sizeof(event));
	event.type = SDL_MOUSEBUTTONDOWN;
	event.button.button = SDL_BUTTON_LEFT;
	event.button.state = SDL_PRESSED;
	event.button.x = 17;
	event.button.y = 23;
	SystemHandleInput(&event);
	CHECK(captured_mouse_count == 1 &&
			captured_mouse_button == SDL_BUTTON_LEFT &&
			captured_mouse_state == SDL_PRESSED &&
			captured_mouse_x == 17 && captured_mouse_y == 23,
			"mouse button semantics changed");

	memset(&event, 0, sizeof(event));
	event.type = SDL_MOUSEMOTION;
	event.motion.x = 31;
	event.motion.y = 47;
	SystemHandleInput(&event);
	CHECK(captured_motion_count == 1 && captured_motion_x == 31 &&
			captured_motion_y == 47, "mouse motion semantics changed");

	callbacks.keyboard = NULL;
	resetCapture();
	makeKeyEvent(&event, SDL_KEYDOWN, SDLK_F1);
	SystemHandleInput(&event);
	CHECK(captured_key_count == 0, "NULL keyboard callback was invoked");

	callbacks.mouse = NULL;
	memset(&event, 0, sizeof(event));
	event.type = SDL_MOUSEBUTTONUP;
	SystemHandleInput(&event);
	CHECK(captured_mouse_count == 0, "NULL mouse callback was invoked");

	callbacks.mouseMotion = NULL;
	event.type = SDL_MOUSEMOTION;
	SystemHandleInput(&event);
	CHECK(captured_motion_count == 0,
			"NULL mouse-motion callback was invoked");
	current = NULL;
	return 0;
}

int main(void) {
	if(SDL_Init(0) != 0) {
		fprintf(stderr, "FAIL: SDL initialization: %s\n", SDL_GetError());
		return 1;
	}
	if(checkNumericAbi() || checkThresholds() ||
		 checkPureJoystickTranslation() || checkKeyboardAdapter() ||
		 checkSystemDispatch()) {
		SDL_Quit();
		return 1;
	}
	SDL_Quit();
	printf("PASS: stable SDL1 IDs, checked two-slot controller translation, "
			 "null-safe dispatch, and mouse parity\n");
	return 0;
}
