# CMake toolchain for the PS5 native title. tools/ps5-cc and tools/ps5-c++ compile for the
# console and link into PS5 modules, so configure checks run real links. Libraries are found in
# .deps/ps5, where tools/build-deps.sh installs them.
set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_VERSION 9)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(PS5 TRUE)

get_filename_component(PS5_PORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PS5_PREFIX "${PS5_PORT_ROOT}/.deps/ps5")

set(CMAKE_C_COMPILER "${PS5_PORT_ROOT}/tools/ps5-cc")
set(CMAKE_CXX_COMPILER "${PS5_PORT_ROOT}/tools/ps5-c++")
set(CMAKE_ASM_COMPILER "${PS5_PORT_ROOT}/tools/ps5-cc")
set(CMAKE_AR llvm-ar-18 CACHE FILEPATH "")
set(CMAKE_RANLIB llvm-ranlib-18 CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH "${PS5_PREFIX}")
set(CMAKE_PREFIX_PATH "${PS5_PREFIX}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_LIBDIR} "${PS5_PREFIX}/lib/pkgconfig:${PS5_PREFIX}/libdata/pkgconfig:${PS5_PREFIX}/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(CMAKE_C_FLAGS_INIT "-ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "-ffunction-sections -fdata-sections")
