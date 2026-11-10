cmake_minimum_required(VERSION 3.22)
project(afterline_transcriber C CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/EngineCommon.cmake)

# whisper.cpp v1.9.5 (MIT)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2 OFF CACHE BOOL "" FORCE)
FetchContent_Declare(whisper
  GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
  GIT_TAG v1.9.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(whisper)
afterline_verify_commit(whisper.cpp "${whisper_SOURCE_DIR}" d1be6fde11ac6e0407606b4e42fe72d34add8037)
afterline_stage_libraries("${whisper_SOURCE_DIR}")

afterline_engine_executable(afterline-transcriber
  main.cpp
  ${AFTERLINE_SOURCE_DIR}/src/core/OggOpus.cpp)
target_link_libraries(afterline-transcriber PRIVATE whisper ggml PkgConfig::OPUS PkgConfig::OGG)
