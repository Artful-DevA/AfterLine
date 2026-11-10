# Shared settings for the engine projects (engines/*/CMakeLists.txt).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT AFTERLINE_SOURCE_DIR)
  get_filename_component(AFTERLINE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

# Everything lands in one directory and finds its libraries next to itself.
set(AFTERLINE_STAGE ${CMAKE_BINARY_DIR}/stage)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${AFTERLINE_STAGE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${AFTERLINE_STAGE})
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN")

set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
if(AFTERLINE_GGML_NATIVE)
  set(GGML_NATIVE ON CACHE BOOL "" FORCE)
else()
  # Portable packages: build every x86-64 CPU variant (SSE4.2 ... AVX-512)
  # and let ggml load the best one for the machine at run time.
  set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
  set(GGML_BACKEND_DL ON CACHE BOOL "" FORCE)
  set(GGML_CPU_ALL_VARIANTS ON CACHE BOOL "" FORCE)
endif()
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)  # no libgomp dependency; ggml has its own pool
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(OGG REQUIRED IMPORTED_TARGET ogg)

include(FetchContent)

# Fails the configure step if a fetched dependency is not the exact pinned
# commit (a moved tag, a man-in-the-middle). Skipped for local source overrides.
function(afterline_verify_commit name dir expected)
  if(EXISTS "${dir}/.git")
    execute_process(COMMAND git -C "${dir}" rev-parse HEAD OUTPUT_VARIABLE head
                    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0 OR NOT head STREQUAL expected)
      message(FATAL_ERROR "${name}: expected commit ${expected}, got '${head}'")
    endif()
  endif()
endfunction()

function(afterline_engine_executable target)
  add_executable(${target} ${ARGN}
    ${AFTERLINE_SOURCE_DIR}/engines/common/Protocol.cpp)
  target_include_directories(${target} PRIVATE ${AFTERLINE_SOURCE_DIR}/src ${AFTERLINE_SOURCE_DIR}/engines/common)
  target_compile_definitions(${target} PRIVATE AFTERLINE_VERSION="${AFTERLINE_VERSION}" QT_NO_KEYWORDS)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(${target} PRIVATE Qt6::Core)
endfunction()

# Dependencies set their own output directories; move every shared library
# and dynamically loaded backend they define next to the engine executable.
function(afterline_stage_libraries dir)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(t IN LISTS targets)
    get_target_property(type ${t} TYPE)
    if(type STREQUAL "SHARED_LIBRARY" OR type STREQUAL "MODULE_LIBRARY")
      set_target_properties(${t} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${AFTERLINE_STAGE}
        RUNTIME_OUTPUT_DIRECTORY ${AFTERLINE_STAGE}
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_RPATH "$ORIGIN")
    endif()
  endforeach()
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(sub IN LISTS subdirs)
    afterline_stage_libraries("${sub}")
  endforeach()
endfunction()
