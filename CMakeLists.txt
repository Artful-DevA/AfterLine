cmake_minimum_required(VERSION 3.22)
project(AfterLine VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AFTERLINE_BUILD_GUI "Build the Qt application" ON)
option(AFTERLINE_BUILD_TESTS "Build the test suite" ON)
option(AFTERLINE_BUILD_ENGINES "Build the transcription and notes engines (whisper.cpp, llama.cpp)" ON)
option(AFTERLINE_GGML_NATIVE "Optimise the engines for this CPU only (development builds)" OFF)

include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(OGG REQUIRED IMPORTED_TARGET ogg)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

# Engines live in their own directory next to their private ggml libraries:
#   <prefix>/<libdir>/afterline/{transcriber,summarizer}/
# The app finds them relative to its own executable, so the same layout works
# from /usr, /usr/local and inside the AppImage.
set(AFTERLINE_ENGINE_RELDIR "${CMAKE_INSTALL_LIBDIR}/afterline")
file(RELATIVE_PATH AFTERLINE_BIN_TO_ENGINES
     "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
     "${CMAKE_INSTALL_PREFIX}/${AFTERLINE_ENGINE_RELDIR}")

set(AFTERLINE_WARNINGS -Wall -Wextra -Wno-unused-parameter)

# ---------------------------------------------------------------------------
# Core: recording format and audio engine. No GUI dependency, so the engines
# and tests link it too.
# ---------------------------------------------------------------------------
add_library(afterline_codec STATIC src/core/OggOpus.cpp)
target_include_directories(afterline_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(afterline_codec PUBLIC PkgConfig::OPUS PkgConfig::OGG)
target_compile_options(afterline_codec PRIVATE ${AFTERLINE_WARNINGS})
set_target_properties(afterline_codec PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(CMAKE_AUTOMOC ON)

add_library(afterline_core STATIC
  src/core/Transcript.cpp
  src/core/Notes.cpp
  src/core/Export.cpp
  src/core/Paths.cpp
  src/core/Json.cpp
  src/audio/PipeWireGraph.cpp
  src/audio/CaptureStream.cpp
  src/audio/TrackWriter.cpp
  src/audio/Recorder.cpp
  src/audio/Player.cpp
  src/app/Database.cpp
  src/app/MeetingStore.cpp
  src/app/ModelCatalog.cpp
)
target_include_directories(afterline_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(afterline_core PUBLIC afterline_codec Qt6::Core Threads::Threads
  PkgConfig::PIPEWIRE PkgConfig::SQLITE)
target_compile_options(afterline_core PRIVATE ${AFTERLINE_WARNINGS})
target_compile_definitions(afterline_core PUBLIC QT_NO_KEYWORDS
  AFTERLINE_VERSION="${PROJECT_VERSION}"
  AFTERLINE_BIN_TO_ENGINES="${AFTERLINE_BIN_TO_ENGINES}")

# ---------------------------------------------------------------------------
# Engines (separate processes, each with its own ggml build)
# ---------------------------------------------------------------------------
if(AFTERLINE_BUILD_ENGINES)
  include(cmake/Engines.cmake)
endif()

# ---------------------------------------------------------------------------
# Qt application
# ---------------------------------------------------------------------------
if(AFTERLINE_BUILD_GUI)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Gui Widgets Network Svg)

  add_executable(afterline
    src/main.cpp
    src/app/Application.cpp
    src/app/Settings.cpp
    src/app/ModelManager.cpp
    src/app/Processing.cpp
    src/app/LiveTranscriber.cpp
    src/app/Tasks.cpp
    src/app/RecordingController.cpp
    src/ui/Theme.cpp
    src/ui/Icons.cpp
    src/ui/Widgets.cpp
    src/ui/Dialogs.cpp
    src/ui/TasksView.cpp
    src/ui/MainWindow.cpp
    src/ui/LibraryView.cpp
    src/ui/RecordView.cpp
    src/ui/MeetingView.cpp
    src/ui/TranscriptView.cpp
    src/ui/NotesView.cpp
    src/ui/AudioView.cpp
    src/ui/AskPanel.cpp
    src/ui/SettingsView.cpp
    src/ui/SetupDialog.cpp
    resources/resources.qrc
  )
  set_target_properties(afterline PROPERTIES AUTORCC ON)
  target_compile_options(afterline PRIVATE ${AFTERLINE_WARNINGS})
  target_link_libraries(afterline PRIVATE afterline_core
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg)

  install(TARGETS afterline RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/io.github.AfterLine.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/io.github.AfterLine.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(FILES resources/icons/afterline.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
          RENAME io.github.AfterLine.svg)
  foreach(size 16 24 32 48 64 128 256 512)
    install(FILES resources/icons/hicolor/afterline-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME io.github.AfterLine.png)
  endforeach()
  install(FILES LICENSE THIRD_PARTY_NOTICES.md
          DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/afterline)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(AFTERLINE_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
  function(afterline_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE afterline_core Qt6::Test)
    target_compile_options(${name} PRIVATE ${AFTERLINE_WARNINGS})
    add_test(NAME ${name} COMMAND ${name})
  endfunction()
  afterline_test(test_oggopus)
  afterline_test(test_transcript)
  # The notes checks live in the summarizer engine; they are pure Qt Core.
  target_sources(test_transcript PRIVATE engines/summarizer/Notes.cpp)
  target_include_directories(test_transcript PRIVATE engines/summarizer)
  afterline_test(test_storage)
  afterline_test(test_models)
  # Needs a running PipeWire + WirePlumber session; tests/run-pipewire-tests.sh
  # starts a private one with virtual devices. Skips itself when none exists.
  afterline_test(test_pipewire)
  add_executable(afterline-fake-app tests/tools/fake_app.cpp)
  target_include_directories(afterline-fake-app PRIVATE src)
  target_link_libraries(afterline-fake-app PRIVATE PkgConfig::PIPEWIRE m)
  target_compile_definitions(test_pipewire PRIVATE AFTERLINE_FAKE_APP="$<TARGET_FILE:afterline-fake-app>")
  add_dependencies(test_pipewire afterline-fake-app)
  set_tests_properties(test_pipewire PROPERTIES LABELS pipewire TIMEOUT 300 SKIP_RETURN_CODE 77)
endif()

# ---------------------------------------------------------------------------
# Packages: cpack -G DEB / cpack -G RPM from a build configured with
# -DCMAKE_INSTALL_PREFIX=/usr. Build on each distribution release you ship for.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME afterline)
set(CPACK_PACKAGE_VENDOR "AfterLine")
set(CPACK_PACKAGE_CONTACT "Artful-DevA <246381290+Artful-DevA@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Artful-DevA/AfterLine")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Private, offline meeting recorder, transcripts and notes")
set(CPACK_PACKAGE_DESCRIPTION
  "AfterLine records your meetings from a whole computer or a single\napplication (Firefox, Chrome, Zoom, Teams, Discord...), optionally with your\nmicrophone, then transcribes them and writes meeting notes entirely on your\ncomputer. No account, no cloud, no telemetry.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION sound)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS
  "${CMAKE_INSTALL_PREFIX}/${AFTERLINE_ENGINE_RELDIR}/transcriber"
  "${CMAKE_INSTALL_PREFIX}/${AFTERLINE_ENGINE_RELDIR}/summarizer")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "pipewire, libqt6svg6, qt6-qpa-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "wireplumber | pipewire-media-session, qt6-wayland")
string(REPLACE "\n" "\n " _afterline_deb_desc "${CPACK_PACKAGE_DESCRIPTION}")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}\n ${_afterline_deb_desc}")

set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_REQUIRES "pipewire, wireplumber, qt6-qtsvg")
set(CPACK_RPM_PACKAGE_SUGGESTS "qt6-qtwayland")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")
# The engines' private ggml libraries are not system libraries.
set(CPACK_RPM_SPEC_MORE_DEFINE "%global __requires_exclude ^lib(ggml|whisper|llama)")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
  /usr/share/applications /usr/share/metainfo /usr/share/icons /usr/share/icons/hicolor
  /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps
  /usr/lib64 /usr/share/doc)
foreach(size 16 24 32 48 64 128 256 512)
  list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/icons/hicolor/${size}x${size} /usr/share/icons/hicolor/${size}x${size}/apps)
endforeach()

include(CPack)
