cmake_minimum_required(VERSION 3.22)
project(afterline_summarizer C CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/EngineCommon.cmake)

# llama.cpp b12248 (MIT)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(llama
  GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
  GIT_TAG b12248
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(llama)
afterline_verify_commit(llama.cpp "${llama_SOURCE_DIR}" 3837bf0bc02ea5d08f8ee3585fc8c7784a81bce8)
afterline_stage_libraries("${llama_SOURCE_DIR}")

afterline_engine_executable(afterline-summarizer main.cpp Notes.cpp)
target_link_libraries(afterline-summarizer PRIVATE llama ggml)
