set(GLTRON_REVISION "source-archive")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE GLTRON_GIT_DESCRIPTION
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE GLTRON_GIT_RESULT)
  if(GLTRON_GIT_RESULT EQUAL 0)
    set(GLTRON_REVISION "${GLTRON_GIT_DESCRIPTION}")
  endif()
endif()
configure_file(packaging/gltron_build_info.h.in generated/gltron_build_info.h @ONLY)
configure_file(packaging/build-info.txt.in build-info.txt @ONLY)
