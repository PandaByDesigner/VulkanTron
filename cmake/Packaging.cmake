install(TARGETS gltron RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY art data music scripts DESTINATION "${CMAKE_INSTALL_DATADIR}/gltron"
  FILES_MATCHING
  PATTERN "*.png" PATTERN "*.lua" PATTERN "*.wav" PATTERN "*.ogg"
  PATTERN "*.obj" PATTERN "*.mtl" PATTERN "*.ftx" PATTERN "*.fbmp"
  PATTERN "*.txt" PATTERN "*.md" PATTERN "*.it"
  PATTERN "COPYING" PATTERN "LICENSE" PATTERN "NOTICE")
install(FILES packaging/gltron.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES art/default/gltron.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps")
install(FILES COPYING "${PROJECT_BINARY_DIR}/build-info.txt"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/gltron")
install(FILES packaging/LUA-COPYRIGHT.txt DESTINATION "${CMAKE_INSTALL_DATADIR}/gltron/licenses")
install(FILES README README.md COPYING
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gltron")
install(DIRECTORY docs DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gltron"
  FILES_MATCHING PATTERN "*.md")
install(FILES packaging/LUA-COPYRIGHT.txt
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/gltron/packaging")
configure_file(packaging/README.txt.in package-readme.txt @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/package-readme.txt"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/gltron" RENAME README.txt)

set(CPACK_PACKAGE_NAME gltron-faithful)
set(CPACK_VERBATIM_VARIABLES TRUE)
set(CPACK_PACKAGE_VENDOR "GLTron contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GLTron Faithful Remaster with original OpenGL gameplay")
set(CPACK_PACKAGE_VERSION "${GLTRON_DISPLAY_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/COPYING")
string(TOLOWER "${GLTRON_SDL_BACKEND}" GLTRON_PACKAGE_BACKEND)
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${GLTRON_DISPLAY_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${GLTRON_PACKAGE_BACKEND}")
if(NOT GLTRON_ENABLE_AUDIO)
  string(APPEND CPACK_PACKAGE_FILE_NAME "-nosound")
endif()
set(CPACK_GENERATOR TGZ)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_CHECKSUM SHA256)
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${GLTRON_DISPLAY_VERSION}-source")
set(CPACK_SOURCE_IGNORE_FILES
  "/[.]git/" "/[.]agents/" "/[.]codex/" "/_build/" "/build/"
  "/autom4te[.]cache/" "/[.]deps/" "[.]o$" "[.]a$"
  "/config[.]log$" "/config[.]status$" "/Makefile$"
  "/CMakeFiles/" "/CMakeCache[.]txt$" "/_CPack_Packages/"
  "/CMakeUserPresets[.]json$"
  "/compile_commands[.]json$"
  "/__pycache__/" "[.]py[co]$"
  "/[.]gltronrc$" "/gltron[.]ini$" "/gltronPrefs[.]txt$"
  "/gltron-[0-9][^/]*-[0-9]+[.](png|bmp)$"
  "[.]tar[.]gz$" "[.]tar[.]gz[.]sha256$" "/gltron$")
include(CPack)
