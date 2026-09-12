execute_process(COMMAND "${PROBE}"
  "${ASSET_ROOT}/data/game_crash.wav" "${ASSET_ROOT}/data/game_engine.wav"
  "${ASSET_ROOT}/data/game_recognizer.wav" "${ASSET_ROOT}/music/song_revenge_of_cats.it"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE diagnostics TIMEOUT 100)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Production audio probe failed (${result}):\n${output}\n${diagnostics}")
endif()
# Canonical PCM and production-mixer results from the retained SDL1 reference.
foreach(expected
    crash_bytes=277928 crash_fnv1a=5510bbe128637a7a
    engine_bytes=231052 engine_fnv1a=f8f9e0744d68dc8c
    recognizer_bytes=394848 recognizer_fnv1a=3d30a780f00049ac
    music_prefix_bytes=4194304 music_prefix_fnv1a=cdc7967bc48612e9
    reset_prefix_fnv1a=e15d2b883819f9cb loop_mixed_bytes=51208192
    sample_full_fnv1a=009755c4be87b8b3 sample_half_fnv1a=8aa0d2f0f61a4ff2
    copy_first_fnv1a=75082e4d082e486d copy_overlap_fnv1a=b0e910e17f34df90
    one_shot_fnv1a=bfa06d69a754e68b loop_boundary_fnv1a=8047ea38c1496720
    copy_independent=1 one_shot_reset=1 sample_loop_boundary=1
    loop_reset=1 eof_stop=1 wrappers=1
    spatial_fnv1a=e6f032ebaff3e61d engine_mix_fnv1a=e8f916760eb7a382
    spatial_cursor=193352 engine_cursor=224864)
  if(NOT output MATCHES "(^|[ \n])${expected}([ \n]|$)")
    message(FATAL_ERROR "Audio reference mismatch: expected ${expected}\n${output}")
  endif()
endforeach()
message(STATUS "Original PCM, mixing, source lifecycle, music loop and EOF checks passed")
