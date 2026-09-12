#ifndef VULKANTRON_CLASSIC_BRIDGE_H
#define VULKANTRON_CLASSIC_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct VTPlayer {
    float x, y, height, speed;
    int direction, alive, score;
    float eye[3], target[3];
} VTPlayer;
typedef struct VTTrail {
    float start[2], end[2], height;
    int player;
} VTTrail;
typedef struct VTSnapshot {
    VTPlayer players[4];
    const VTTrail* trails;
    size_t trail_count;
    float grid_size;
    unsigned int time_ms;
    int running, winner, camera;
} VTSnapshot;

int vt_classic_init(unsigned int seed, int demo);
void vt_classic_shutdown(void);
void vt_classic_reset(unsigned int seed, int demo);
void vt_classic_step(unsigned int milliseconds);
// Turn left/right relative to player one's current heading using production events.
void vt_classic_turn(int right);
void vt_classic_boost(int pressed);
void vt_classic_cycle_camera(void);
const VTSnapshot* vt_classic_snapshot(void);
uint64_t vt_classic_state_hash(void);

#ifdef __cplusplus
}
#endif
#endif
