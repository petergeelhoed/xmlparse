cmake_minimum_required(VERSION 3.16)

# Project setup
project(xmline
    VERSION 0.1.0
    LANGUAGES C CXX
)

# Require C11 and C++20
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# For clangd/IDE integrations
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Global optimization flag (equivalent to -O2)
#add_compile_options(-O2)

set(C_WARNINGS
  -Wall
  -Wextra
  -Winit-self
  -Wuninitialized
  -Wmaybe-uninitialized
)

# C++ warnings: common useful warnings
set(CXX_WARNINGS
  -Wall
  -Wextra
  -Wpedantic
  -Wold-style-cast
  -Wconversion
)

# clang-tidy integration: use separate config files for C and C++
find_program(CLANG_TIDY_EXE NAMES clang-tidy)

option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis" ON)

if(CLANG_TIDY_EXE)
  message(STATUS "Found clang-tidy: ${CLANG_TIDY_EXE}")

    if(ENABLE_CLANG_TIDY)
      set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy-c")
      set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
    endif()
else()
  message(STATUS "clang-tidy not found; static analysis will be skipped")
endif()

# Use pkg-config to get libxml2 cflags/libs
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)
message(STATUS "LIBXML2_INCLUDE_DIRS='${LIBXML2_INCLUDE_DIRS}'")
message(STATUS "LIBXML2_CFLAGS_OTHER='${LIBXML2_CFLAGS_OTHER}'")
message(STATUS "LIBXML2_LIBRARIES='${LIBXML2_LIBRARIES}'")
message(STATUS "LIBXML2_LIBRARY_DIRS='${LIBXML2_LIBRARY_DIRS}'")
# Define the executable targets
add_executable(xmline xmline.cpp)
add_executable(cxml cxml.c)

# Apply pkg-config include dirs, compile defs, and options to both targets
foreach(tgt IN LISTS CMAKE_PROJECT_NAME)
  # noop - kept for readability; per-target config below
endforeach()

# Configure xmline target
target_include_directories(xmline PRIVATE ${LIBXML2_INCLUDE_DIRS})
target_compile_options(xmline PRIVATE ${LIBXML2_CFLAGS_OTHER}${CXX_WARNINGS})
target_link_libraries(xmline PRIVATE ${LIBXML2_LINK_LIBRARIES})

# Configure cxml target (C implementation)
target_include_directories(cxml PRIVATE ${LIBXML2_INCLUDE_DIRS})
target_compile_options(cxml PRIVATE ${LIBXML2_CFLAGS_OTHER} ${C_WARNINGS})
target_link_libraries(cxml PRIVATE ${LIBXML2_LINK_LIBRARIES})

# If pkg-config provided library dirs, expose them (optional)
if(LIBXML2_LIBRARY_DIRS)
    link_directories(${LIBXML2_LIBRARY_DIRS})
endif()




