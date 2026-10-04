# Included into OpenRCT2's project (CMAKE_PROJECT_INCLUDE) by tools/build-ps5.sh: compiles against
# the static ICU (with 16-bit wchar_t, as on the console) and adds the PS5 port's sources to the openrct2 target once OpenRCT2's CMake
# files have defined it.
add_compile_definitions(U_STATIC_IMPLEMENTATION U_SIZEOF_WCHAR_T=2)

function(ps5_port)
    file(GLOB port_sources CONFIGURE_DEPENDS "${PS5_PORT_ROOT}/src/port/*.cpp" "${PS5_PORT_ROOT}/src/port/*.c")
    target_sources(openrct2 PRIVATE ${port_sources})
    target_include_directories(openrct2 PRIVATE "${PS5_PORT_ROOT}/src/runtime")
    # tools/ps5-link adds these to every link; relink when they change.
    set_property(TARGET openrct2 APPEND PROPERTY LINK_DEPENDS
        "${PS5_PREFIX}/lib/libps5runtime.a" "${PS5_PREFIX}/lib/ps5-crt.o"
        "${PS5_PORT_ROOT}/src/runtime/ps5.ld" "${PS5_PORT_ROOT}/tools/ps5-link")
    # Libraries that pkg-config and FindICU name only for static builds.
    foreach(library icudata vorbis ogg)
        target_link_libraries(openrct2 "${PS5_PREFIX}/lib/lib${library}.a")
    endforeach()
endfunction()
cmake_language(DEFER CALL ps5_port)
