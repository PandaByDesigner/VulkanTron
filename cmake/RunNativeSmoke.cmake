# Reset only the exact private CTest state directories we created. Production
# preferences, screenshots, and the source artpacks are never cleanup targets.
if(NOT IS_ABSOLUTE "${STATE_ROOT}" OR
   NOT STATE_ROOT MATCHES "/test-state/native/(default|faithful)$")
  message(FATAL_ERROR "Refusing unexpected native smoke state path: ${STATE_ROOT}")
endif()
file(REMOVE_RECURSE "${STATE_ROOT}/config" "${STATE_ROOT}/screenshots")
file(MAKE_DIRECTORY "${STATE_ROOT}/config" "${STATE_ROOT}/screenshots")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
  "GLTRON_CONFIG_DIR=${STATE_ROOT}/config"
  "GLTRON_SCREENSHOT_DIR=${STATE_ROOT}/screenshots"
  "GLTRON_DATA_DIR=${ASSET_ROOT}"
  "${SMOKE}" "${ARTPACK}" RESULT_VARIABLE result TIMEOUT 110)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Native ${ARTPACK} game smoke failed (${result}); captures remain in ${STATE_ROOT}/screenshots")
endif()
