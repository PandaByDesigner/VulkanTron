/* VulkanTron's platform adapter; simulation remains in the original C sources.
 * This first slice uses scripts/config.lua's speed, grid, booster and AI values,
 * and scripts/artpack.lua's colors. Lua, sound and recognizer flight are deferred.
 * Fast finish is disabled so the four-AI demonstration advances in real time.
 * This is a single-instance, single-threaded API. Snapshot storage belongs to
 * the bridge and is valid until the next mutating call or shutdown. NULL means
 * uninitialized state or an allocation/validation failure; reset can retry.
 */
#include "classic_bridge.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/gltron.h"
#include "game/timesystem.h"

static Player players[PLAYERS];
static Data player_data[PLAYERS];
static AI player_ai[PLAYERS];
static Camera player_camera[PLAYERS];
static PlayerVisual player_visual[PLAYERS];
static int initialized, demo_mode, round_ended, snapshot_valid;
static unsigned int elapsed_ms, round_seed;
static VTSnapshot snapshot;
static VTTrail *snapshot_trails;
static size_t snapshot_capacity;
static float original_camera_defaults[CAM_COUNT][3];
static int saved_camera_defaults;

static const float palette[3][PLAYERS][4] = {
    {{1, .550f, .140f, 1}, {.750f, .020f, .020f, 1},
     {.120f, .520f, .600f, 1}, {.800f, .800f, .800f, 1}},
    {{.500f, .500f, 0, 1}, {.750f, .020f, .020f, 1},
     {.120f, .520f, .600f, 1}, {1, 1, 1, 1}},
    {{1, .850f, .140f, .600f}, {.750f, .020f, .020f, .600f},
     {.120f, .520f, .600f, .600f}, {.700f, .700f, .700f, .600f}}
};
static const float *script_color = palette[0][0];

float getSettingf(const char *name) {
    static const struct { const char *name; float value; } values[] = {
        {"speed", 8.5f}, {"grid_size", 720}, {"erase_crashed", 1},
        {"booster_on", 1}, {"booster_min", 1}, {"booster_max", 6.5f},
        {"booster_use", 1}, {"booster_decrease", .8f},
        {"booster_regenerate", .4f}, {"wall_accel_on", 0},
        {"wall_accel_limit", 20}, {"wall_accel_use", 1},
        {"wall_accel_decrease", .8f}, {"debug_output", 0}
    };
    size_t i;
    if(strcmp(name, "camType") == 0) return (float)gSettingsCache.camType;
    for(i = 0; i < PLAYERS; ++i) {
        char key[] = "ai_player1";
        key[9] = (char)('1' + i);
        if(strcmp(name, key) == 0)
            return (float)((i == 0 && !demo_mode) ? AI_HUMAN : AI_COMPUTER);
    }
    for(i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if(strcmp(name, values[i].name) == 0) return values[i].value;
    fprintf(stderr, "VulkanTron: unsupported classic setting '%s'\n", name);
    abort();
}

int getSettingi(const char *name) { return (int)getSettingf(name); }

void setSettingi(const char *name, int value) {
    if(strcmp(name, "camType") == 0 && value >= 0 && value < CAM_COUNT) {
        gSettingsCache.camType = value;
        return;
    }
    fprintf(stderr, "VulkanTron: unsupported classic setting write '%s'\n", name);
    abort();
}

unsigned int SystemGetElapsedTime(void) { return elapsed_ms; }
void SystemExitLoop(int code) { (void)code; round_ended = 1; }
void Audio_CrashPlayer(int player) { (void)player; }
void Audio_StopEngine(int player) { (void)player; }
void displayMessage(outloc_e where, const char *format, ...) {
    (void)where; (void)format;
}

int scripting_GetGlobal(const char *global, const char *format, ...) {
    static const char *prefixes[] = {
        "model_diffuse_", "model_specular_", "trail_diffuse_"
    };
    size_t group;
    (void)format;
    for(group = 0; group < 3; ++group) {
        size_t n = strlen(prefixes[group]);
        if(strncmp(global, prefixes[group], n) == 0 &&
           global[n] >= '0' && global[n] <= '3' && global[n + 1] == '\0') {
            script_color = palette[group][global[n] - '0'];
            return 0;
        }
    }
    fprintf(stderr, "VulkanTron: unsupported classic color '%s'\n", global);
    abort();
}

void scripting_GetFloatArrayResult(float *values, int count) {
    int i;
    for(i = 0; i < count; ++i) values[i] = i < 4 ? script_color[i] : 0;
}

/* Placeholder spectator platform only: the production camera still computes
 * its view, but recognizer flight/mesh is not part of this first arena slice. */
void doRecognizerMovement(void) { }
void getRecognizerPositionVelocity(vec2 *position, vec2 *velocity) {
    position->v[0] = game2->rules.grid_size * .5f;
    position->v[1] = game2->rules.grid_size * .5f;
    velocity->v[0] = 1;
    velocity->v[1] = 0;
}

static int refresh_snapshot(void) {
    VTSnapshot next;
    size_t count = 0, offset = 0;
    int i, j;
    snapshot_valid = 0;
    memset(&next, 0, sizeof(next));
    for(i = 0; i < PLAYERS; ++i) {
        const Data *data = players[i].data;
        size_t segments;
        if(data->trailOffset < 0 || data->trailOffset >= data->trailCapacity)
            return 0;
        segments = (size_t)data->trailOffset + 1;
        if(segments > SIZE_MAX - count) return 0;
        count += segments;
    }
    if(count > SIZE_MAX / sizeof(*snapshot_trails)) return 0;
    if(count > snapshot_capacity) {
        size_t capacity = snapshot_capacity ? snapshot_capacity : 64;
        VTTrail *grown;
        while(capacity < count) {
            if(capacity > SIZE_MAX / 2 / sizeof(*snapshot_trails)) {
                capacity = count;
                break;
            }
            capacity *= 2;
        }
        grown = realloc(snapshot_trails, capacity * sizeof(*snapshot_trails));
        if(!grown) return 0; /* Keep old allocation owned; reset may retry. */
        snapshot_trails = grown;
        snapshot_capacity = capacity;
    }
    for(i = 0; i < PLAYERS; ++i) {
        const Data *data = players[i].data;
        const Camera *camera = players[i].camera;
        VTPlayer *out = &next.players[i];
        getPositionFromData(&out->x, &out->y, players[i].data);
        out->height = data->trail_height;
        out->speed = data->speed;
        out->direction = data->dir;
        out->alive = data->speed > 0;
        out->score = data->score;
        memcpy(out->eye, camera->cam, sizeof(out->eye));
        memcpy(out->target, camera->target, sizeof(out->target));
        if(!isfinite(out->x) || !isfinite(out->y) ||
           !isfinite(out->height) || !isfinite(out->speed)) return 0;
        for(j = 0; j < 3; ++j)
            if(!isfinite(out->eye[j]) || !isfinite(out->target[j])) return 0;
        for(j = 0; j <= data->trailOffset; ++j) {
            const segment2 *segment = &data->trails[j];
            VTTrail *trail = &snapshot_trails[offset++];
            int axis;
            for(axis = 0; axis < 2; ++axis) {
                trail->start[axis] = segment->vStart.v[axis];
                trail->end[axis] = segment->vStart.v[axis] +
                                   segment->vDirection.v[axis];
                if(!isfinite(trail->start[axis]) || !isfinite(trail->end[axis]))
                    return 0;
            }
            trail->height = data->trail_height;
            trail->player = i;
        }
    }
    next.trails = snapshot_trails;
    next.trail_count = count;
    next.grid_size = (float)game2->rules.grid_size;
    next.time_ms = game2->time.current;
    next.running = !round_ended && game->pauseflag == PAUSE_GAME_RUNNING;
    next.winner = game->winner;
    next.camera = players[0].camera->type.type;
    snapshot = next;
    snapshot_valid = 1;
    return 1;
}

void vt_classic_reset(unsigned int seed, int demo) {
    int i;
    if(!initialized) return;
    demo_mode = demo != 0;
    elapsed_ms = 0;
    round_seed = seed;
    round_ended = 0;
    memset(&game2->time, 0, sizeof(game2->time));
    memset(&gInput, 0, sizeof(gInput));
    memset(&gSettingsCache, 0, sizeof(gSettingsCache));
    gSettingsCache.ai_level = 2;
    gSettingsCache.camType = CAM_FOLLOW;
    gSettingsCache.alpha_trails = 1;
    gSettingsCache.fast_finish = 0;
    memcpy(cam_defaults, original_camera_defaults, sizeof(original_camera_defaults));
    for(i = 0; i < PLAYERS; ++i) {
        memset(&player_ai[i], 0, sizeof(player_ai[i]));
        memset(&player_camera[i], 0, sizeof(player_camera[i]));
        memset(&player_visual[i], 0, sizeof(player_visual[i]));
    }
    resetScores();
    tsrand(seed);
    initData();
    doCameraMovement();
    (void)refresh_snapshot();
}

int vt_classic_init(unsigned int seed, int demo) {
    segment2 *allocated[PLAYERS] = {0};
    int i;
    if(initialized) {
        vt_classic_reset(seed, demo);
        return snapshot_valid;
    }
    /* Allocate plain ownership structures here because legacy
     * initGameStructures() cannot report a partially failed allocation.
     * Reset, starts, rules, AI, physics, events and cameras remain production.
     * Those production routines retain their existing fatal/unchecked OOM
     * behavior; this adapter does not claim to harden that separate code. */
    for(i = 0; i < PLAYERS; ++i) {
        allocated[i] = calloc(MAX_TRAIL, sizeof(*allocated[i]));
        if(!allocated[i]) {
            int j;
            for(j = 0; j <= i; ++j) free(allocated[j]);
            return 0;
        }
    }
    if(!saved_camera_defaults) {
        memcpy(original_camera_defaults, cam_defaults, sizeof(original_camera_defaults));
        saved_camera_defaults = 1;
    }
    memset(&main_game, 0, sizeof(main_game));
    memset(&main_game2, 0, sizeof(main_game2));
    memset(players, 0, sizeof(players));
    memset(player_data, 0, sizeof(player_data));
    game = &main_game;
    game2 = &main_game2;
    game->player = players;
    game->players = PLAYERS;
    game->winner = -1;
    game2->mode = GAME_SINGLE;
    gPlayerVisuals = player_visual;
    for(i = 0; i < PLAYERS; ++i) {
        players[i].data = &player_data[i];
        players[i].ai = &player_ai[i];
        players[i].camera = &player_camera[i];
        player_data[i].trails = allocated[i];
        player_data[i].trailCapacity = MAX_TRAIL;
    }
    initialized = 1;
    vt_classic_reset(seed, demo);
    if(!snapshot_valid) {
        vt_classic_shutdown();
        return 0;
    }
    return 1;
}

void vt_classic_shutdown(void) {
    int i;
    if(initialized) {
        clearEventQueue();
        for(i = 0; i < PLAYERS; ++i) {
            free(player_data[i].trails);
            player_data[i].trails = NULL;
            player_data[i].trailCapacity = 0;
        }
    }
    free(snapshot_trails);
    snapshot_trails = NULL;
    snapshot_capacity = 0;
    snapshot_valid = initialized = 0;
    memset(&snapshot, 0, sizeof(snapshot));
    game = NULL;
    game2 = NULL;
    gPlayerVisuals = NULL;
}

void vt_classic_step(unsigned int milliseconds) {
    if(!initialized || round_ended || milliseconds == 0) return;
    /* A suspended window must not spend minutes catching up or overflow the
     * legacy signed dt. Normal calls retain Game_Idle's exact 20 ms slicing. */
    if(milliseconds > 250) milliseconds = 250;
    elapsed_ms += milliseconds;
    Time_Idle();
    Game_Idle();
    (void)refresh_snapshot();
}

void vt_classic_turn(int right) {
    if(!initialized || demo_mode || round_ended || !PLAYER_IS_ACTIVE(&players[0]))
        return;
    createEvent(0, right ? EVENT_TURN_RIGHT : EVENT_TURN_LEFT);
}

void vt_classic_boost(int pressed) {
    if(!initialized || demo_mode || round_ended || !PLAYER_IS_ACTIVE(&players[0]))
        return;
    player_data[0].boost_enabled = pressed != 0;
}

void vt_classic_cycle_camera(void) {
    if(!initialized) return;
    nextCameraType();
    /* The original helper changes humans only. Allow viewing the AI demo
     * through the same production camera types without changing its AI. */
    if(demo_mode)
        initCamera(players[0].camera, players[0].data, gSettingsCache.camType);
    /* Avoid advancing circling animation merely because a key was pressed. */
    {
        unsigned int dt = game2->time.dt;
        game2->time.dt = 0;
        doCameraMovement();
        game2->time.dt = dt;
    }
    (void)refresh_snapshot();
}

const VTSnapshot *vt_classic_snapshot(void) {
    return initialized && snapshot_valid ? &snapshot : NULL;
}

static void hash_u32(uint64_t *hash, uint32_t value) {
    int byte;
    for(byte = 0; byte < 4; ++byte) {
        *hash ^= value & 255U;
        *hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
}

static void hash_float(uint64_t *hash, float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    hash_u32(hash, bits);
}

uint64_t vt_classic_state_hash(void) {
    uint64_t hash = UINT64_C(14695981039346656037);
    List *node;
    int i, segment, axis;
    if(!initialized) return 0;
    hash_u32(&hash, round_seed);
    hash_float(&hash, game2->rules.speed);
    hash_u32(&hash, (uint32_t)game2->rules.eraseCrashed);
    hash_u32(&hash, (uint32_t)game2->rules.grid_size);
    hash_u32(&hash, game2->time.current);
    hash_u32(&hash, game2->time.lastFrame);
    hash_u32(&hash, game2->time.dt);
    hash_u32(&hash, (uint32_t)game->running);
    hash_u32(&hash, (uint32_t)game->winner);
    hash_u32(&hash, (uint32_t)game->pauseflag);
    for(i = 0; i < PLAYERS; ++i) {
        const Data *data = players[i].data;
        const AI *ai = players[i].ai;
        hash_u32(&hash, (uint32_t)data->dir);
        hash_u32(&hash, (uint32_t)data->last_dir);
        hash_u32(&hash, (uint32_t)data->score);
        hash_float(&hash, data->speed);
        hash_float(&hash, data->booster);
        hash_u32(&hash, (uint32_t)data->boost_enabled);
        hash_float(&hash, data->trail_height);
        hash_u32(&hash, data->turn_time);
        hash_u32(&hash, (uint32_t)data->trailOffset);
        for(segment = 0; segment <= data->trailOffset; ++segment)
            for(axis = 0; axis < 2; ++axis) {
                hash_float(&hash, data->trails[segment].vStart.v[axis]);
                hash_float(&hash, data->trails[segment].vDirection.v[axis]);
            }
        hash_u32(&hash, (uint32_t)ai->active);
        hash_u32(&hash, (uint32_t)ai->tdiff);
        hash_u32(&hash, ai->lasttime);
    }
    for(node = &game2->events; node->next; node = node->next) {
        const GameEvent *event = node->data;
        hash_u32(&hash, (uint32_t)event->type);
        hash_u32(&hash, (uint32_t)event->player);
        hash_float(&hash, event->x);
        hash_float(&hash, event->y);
        hash_u32(&hash, event->timestamp);
    }
    return hash;
}
