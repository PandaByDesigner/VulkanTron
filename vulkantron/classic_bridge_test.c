#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "classic_bridge.h"

static void check_snapshot(void) {
    const VTSnapshot *state = vt_classic_snapshot();
    size_t i;
    assert(state && state->grid_size == 720 && state->trail_count >= 4);
    for(i = 0; i < state->trail_count; ++i) {
        const VTTrail *trail = &state->trails[i];
        assert(trail->player >= 0 && trail->player < 4);
        assert(isfinite(trail->start[0]) && isfinite(trail->start[1]));
        assert(isfinite(trail->end[0]) && isfinite(trail->end[1]));
        assert(isfinite(trail->height));
    }
    for(i = 0; i < 4; ++i) {
        int axis;
        for(axis = 0; axis < 3; ++axis) {
            assert(isfinite(state->players[i].eye[axis]));
            assert(isfinite(state->players[i].target[axis]));
        }
    }
}

int main(void) {
    uint64_t initial, advanced, unchanged;
    unsigned int time;
    int original_direction, i;
    float unboosted_speed;
    VTTrail saved;
    assert(vt_classic_snapshot() == NULL);
    assert(vt_classic_state_hash() == 0);
    vt_classic_shutdown();
    assert(vt_classic_init(12313, 0));
    check_snapshot();
    initial = vt_classic_state_hash();
    original_direction = vt_classic_snapshot()->players[0].direction;
    vt_classic_turn(1);
    assert(vt_classic_state_hash() != initial); /* Event is pending, not a teleport. */
    assert(vt_classic_snapshot()->players[0].direction == original_direction);
    vt_classic_step(20);
    assert(vt_classic_snapshot()->players[0].direction == (original_direction + 1) % 4);
    vt_classic_turn(0);
    vt_classic_step(20);
    assert(vt_classic_snapshot()->players[0].direction == original_direction);
    unboosted_speed = vt_classic_snapshot()->players[0].speed;
    vt_classic_boost(1);
    vt_classic_step(20);
    assert(vt_classic_snapshot()->players[0].speed > unboosted_speed);
    vt_classic_boost(0);
    for(i = 0; i < 25; ++i) vt_classic_step(20);
    assert(vt_classic_snapshot()->players[0].speed == unboosted_speed);
    unchanged = vt_classic_state_hash();
    for(i = 0; i < 4; ++i) {
        vt_classic_cycle_camera();
        check_snapshot();
        assert(vt_classic_state_hash() == unchanged);
    }
    vt_classic_reset(12313, 0);
    assert(vt_classic_state_hash() == initial);
    /* Exceed the original fixed allocation through actual queued turn events.
     * No copied turn/physics function and no private-state writes are used. */
    for(i = 0; i < 1200; ++i) vt_classic_turn(i & 1);
    vt_classic_step(1);
    check_snapshot();
    assert(vt_classic_snapshot()->trail_count >= 1204);
    saved = vt_classic_snapshot()->trails[0];
    vt_classic_reset(12313, 0);
    assert(vt_classic_state_hash() == initial);
    assert(saved.height == 3.5f); /* A caller's value copy outlives bridge reset. */
    for(i = 0; i < 20; ++i) {
        vt_classic_reset(12313, 1);
        initial = vt_classic_state_hash();
        vt_classic_turn(1);
        vt_classic_boost(1);
        assert(vt_classic_state_hash() == initial); /* Demo remains AI-owned. */
        for(int frame = 0; frame < 100; ++frame) vt_classic_step(20);
        if(i == 0) advanced = vt_classic_state_hash();
        assert(vt_classic_state_hash() == advanced);
        check_snapshot();
    }
    for(i = 0; i < 20000 && vt_classic_snapshot()->running; ++i)
        vt_classic_step(20);
    assert(!vt_classic_snapshot()->running);
    unchanged = vt_classic_state_hash();
    vt_classic_step(20);
    assert(vt_classic_state_hash() == unchanged);
    vt_classic_reset(12313, 0);
    time = vt_classic_snapshot()->time_ms;
    vt_classic_step(~0U);
    assert(vt_classic_snapshot()->time_ms == time + 250); /* Stall cap. */
    vt_classic_shutdown();
    vt_classic_shutdown();
    assert(vt_classic_snapshot() == NULL);
    assert(vt_classic_init(12313, 1));
    assert(vt_classic_state_hash() == initial);
    vt_classic_shutdown();
    puts("PASS: production bridge reset, turns, boost, cameras, 1200-turn growth, deterministic AI, and lifecycle");
    return 0;
}
