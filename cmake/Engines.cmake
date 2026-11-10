# The transcription and notes engines are separate executables, each built as
# its own CMake project with its own copy of ggml (whisper.cpp and llama.cpp
# pin different ggml versions, and a single project cannot hold both).
#
# Running them as processes also isolates the recorder from them: a model that
# runs out of memory or crashes cannot take the window or a recording with it,
# and cancelling is simply stopping the process.
include(ExternalProject)

set(_engine_args
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
  -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
  -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
  -DAFTERLINE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
  -DAFTERLINE_VERSION=${PROJECT_VERSION}
  -DAFTERLINE_GGML_NATIVE=${AFTERLINE_GGML_NATIVE})
# Offline/distribution builds can point at pre-fetched sources:
#   -DFETCHCONTENT_SOURCE_DIR_WHISPER=... -DFETCHCONTENT_SOURCE_DIR_LLAMA=...
foreach(dep WHISPER LLAMA)
  if(FETCHCONTENT_SOURCE_DIR_${dep})
    list(APPEND _engine_args -DFETCHCONTENT_SOURCE_DIR_${dep}=${FETCHCONTENT_SOURCE_DIR_${dep}})
  endif()
endforeach()

set(AFTERLINE_ENGINE_STAGE ${CMAKE_CURRENT_BINARY_DIR}/engines)
foreach(engine transcriber summarizer)
  ExternalProject_Add(engine_${engine}
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engines/${engine}
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/engines/${engine}-build
    CMAKE_ARGS ${_engine_args}
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_BUILD ON)
  # Everything the engine needs (executable, libwhisper/libllama, ggml and
  # its CPU backend variants) is staged in one directory with RPATH $ORIGIN.
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/engines/${engine}-build/stage/
          DESTINATION ${AFTERLINE_ENGINE_RELDIR}/${engine}
          USE_SOURCE_PERMISSIONS)
endforeach()

if(TARGET afterline)
  add_dependencies(afterline engine_transcriber engine_summarizer)
endif()
