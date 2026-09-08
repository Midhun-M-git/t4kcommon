#This script was sloppily frankensteined from a handful of tux4kids'
#other CMakeLists.txt and is not safe for human consumption. 
#Please hack responsibly.

# TODO - add cmake build support for t4k_test program - DSB

cmake_minimum_required(VERSION 3.5)

include(ConfigureChecks)
configure_file(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)
include(T4K_Sugar)

#Cmake module contains the source files for this library
include(${CMAKE_SOURCE_DIR}/cmake-modules/Files.cmake)

# Use pkg-config for libxml2 so we always get the SHARED library flags (-lxml2).
# find_package(LibXml2) picks up libxml2.a (static, no -fPIC) on Debian/Ubuntu,
# which causes a linker error when building libt4k_common.so.
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)

#Look here for headers
include_directories(
    ${SDL3_INCLUDE_DIRS}
    ${SDL3_IMAGE_INCLUDE_DIRS}
    ${SDL3_TTF_INCLUDE_DIRS}
    ${SDL3_MIXER_INCLUDE_DIRS}
    ${SDL3_NET_INCLUDE_DIRS}
    ${LIBXML2_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/gettext
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/src
    ${CMAKE_BINARY_DIR}/gettext
    ${CMAKE_BINARY_DIR}/src/linebreak
    ${CMAKE_SOURCE_DIR}/src/linebreak
    )

# On MinGW/Windows, auto-detect librsvg, cairo, glib-2.0, and libintl
# using pkg-config so paths are never hardcoded (works on any MSYS2 install).
if(MINGW)
    pkg_check_modules(LIBRSVG REQUIRED librsvg-2.0)
    pkg_check_modules(CAIRO   REQUIRED cairo)
    pkg_check_modules(GLIB    REQUIRED glib-2.0)
    # libintl has no .pc file on MinGW — use find_library with MSYS2 prefix hint.
    # Users can set MSYS2_PREFIX env var to override (e.g. D:/msys2/ucrt64).
    if(DEFINED ENV{MSYS2_PREFIX})
        set(_msys2_prefix "$ENV{MSYS2_PREFIX}")
    else()
        # Detect from compiler location: strip /bin/gcc -> parent is the prefix
        get_filename_component(_msys2_prefix "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(_msys2_prefix "${_msys2_prefix}" DIRECTORY)
    endif()
    find_library(LIBINTL_LIBRARIES  NAMES intl libintl
        HINTS "${_msys2_prefix}/lib" REQUIRED)
    find_library(LIBICONV_LIBRARIES NAMES iconv libiconv
        HINTS "${_msys2_prefix}/lib" REQUIRED)
    find_library(LIBPNG_LIBRARIES   NAMES png libpng png16 libpng16
        HINTS "${_msys2_prefix}/lib" REQUIRED)
    find_library(ZLIB_LIBRARIES     NAMES z zlib
        HINTS "${_msys2_prefix}/lib" REQUIRED)
    include_directories(
        ${LIBRSVG_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
    )
    link_directories(
        ${LIBRSVG_LIBRARY_DIRS}
    )
endif()

link_directories(/usr/local/lib)

#Look here for optional headers
t4k_include_directory(SDLPANGO)
t4k_include_directory(SDLTTF)
t4k_include_directory(PNG)
t4k_include_directory(RSVG)

#include_directories( ${T4KCOMMON_INCLUDE_DIRS} )

set( T4K_COMMON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR} )
if(T4K_DEVELOPER_WARNINGS)
    list(APPEND CMAKE_C_FLAGS -Wall) 
endif()

#Declare the library as a SO 
add_library( ${LIB_NAME} SHARED
    ${T4K_COMMON_SOURCES} ${T4K_COMMON_HEADERS} )

#Avoid linking to both libraries
if(SDLPANGO_FOUND)
    set(SDLTTF_LIBRARY "")
else()
    set(SDLPANGO_LIBRARY "")
endif()

#Depend on these libraries. For Pango or TTF, whichever is found will be used
target_link_libraries(${LIB_NAME}
    PkgConfig::SDL3
    PkgConfig::SDL3_IMAGE
    PkgConfig::SDL3_TTF
    PkgConfig::SDL3_MIXER
    ${LIBXML2_LIBRARIES}
    ${RSVG_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${LINEBREAK_BINARY_DIR}/liblinebreak.a
    )

# On MinGW, explicitly link gettext (libintl), librsvg, libiconv, libpng, zlib
if(MINGW)
    target_link_libraries(${LIB_NAME}
        ${LIBINTL_LIBRARIES}
        ${LIBRSVG_LIBRARIES}
        ${LIBICONV_LIBRARIES}
        ${LIBPNG_LIBRARIES}
        ${ZLIB_LIBRARIES}
    )
endif()

if(SDL3_NET_FOUND)
    link_directories(${SDL3_NET_LIBRARY_DIRS})
    target_link_libraries(${LIB_NAME} ${SDL3_NET_LIBRARIES})
endif()

if(WITH_ESPEAK)
    if(WITH_ESPEAK_NG)
        include_directories(${ESPEAK_NG_INCLUDE_DIRS})
        target_link_libraries(${LIB_NAME} ${ESPEAK_NG_LIBRARIES})
        add_definitions(-DWITH_ESPEAK=1 -DWITH_ESPEAK_NG=1)
    else()
        include_directories(${ESPEAK_INCLUDE_DIRS})
        target_link_libraries(${LIB_NAME} ${ESPEAK_LIBRARIES})
        add_definitions(-DWITH_ESPEAK=1 -DWITH_ESPEAK_NG=0)
    endif()
endif()

t4k_include_definition(HAVE_LIBSDL_PANGO)
t4k_include_definition(HAVE_LIBPNG)
t4k_include_definition(HAVE_LIBSDL_NET)
set_target_properties (${LIB_NAME} PROPERTIES 
    COMPILE_FLAGS "${_rsvg_def} ${_pango_def} ${_rsvg_cflags} ${_cairo_cflags}"
    COMPILE_DEFINITIONS
    "COMMON_DATA_PREFIX=\"${DATA_PREFIX}\";USE_T4K_PREFIX=1;PACKAGE_STRING=\"${PACKAGE_NAME}\""
    LINK_INTERFACE_LIBRARIES ""
    )

add_dependencies(${LIB_NAME} linebreak)

install( TARGETS ${LIB_NAME}
    DESTINATION lib ) 
install(FILES ${T4K_COMMON_SOURCE_DIR}/t4k_common.h
    ${T4K_COMMON_SOURCE_DIR}/t4k_alphasort.h
    ${T4K_COMMON_SOURCE_DIR}/t4k_scandir.h
    ${T4K_COMMON_SOURCE_DIR}/t4k_globals.h
    ${T4K_COMMON_SOURCE_DIR}/t4k_compiler.h
    DESTINATION include)

include(UseDoxygen OPTIONAL)

#process subdirectories
add_subdirectory(linebreak)

#Build t4k_test app
set(T4K_TESTAPP t4k_test)
add_executable(${T4K_TESTAPP} ${T4K_TEST_SOURCES})
TARGET_LINK_LIBRARIES(${T4K_TESTAPP}
    ${LIB_NAME}
    PkgConfig::SDL3
    PkgConfig::SDL3_IMAGE
    PkgConfig::SDL3_TTF
    PkgConfig::SDL3_MIXER
    ${LIBXML2_LIBRARIES}
    ${RSVG_LIBRARIES}
    ${LINEBREAK_BINARY_DIR}/liblinebreak.a
    )
