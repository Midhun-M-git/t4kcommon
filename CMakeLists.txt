#This script was sloppily frankensteined from a handful of tux4kids'
#other CMakeLists.txt and is not safe for human consumption. 
#Please hack responsibly.

cmake_minimum_required(VERSION 3.5)

set(PACKAGE_NAME "Tux4Kids_common")
set(PACKAGE_VERSION 1.0.0)
set(T4K_COMMON_PACKAGE_STRING "${PACKAGE_NAME}-${PACKAGE_VERSION}" )
set(LIB_NAME "t4k_common")
set(VERSION ${PACKAGE_VERSION})
project(${PACKAGE_NAME})

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake-modules")
include(T4K_Sugar)

set(SDL_BUILDING_LIBRARY 1) #indicate we don't need to (and shouldn't) link SDLmain

set(T4K_DEVELOPER_WARNINGS OFF CACHE STRING "Change or override to enable warnings")
if(CMAKE_BUILD_TYPE MATCHES Release)
    set(T4K_DEVELOPER_WARNINGS ON)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(SDL3_IMAGE REQUIRED IMPORTED_TARGET sdl3-image)
pkg_check_modules(SDL3_TTF REQUIRED IMPORTED_TARGET sdl3-ttf)
pkg_check_modules(SDL3_MIXER REQUIRED IMPORTED_TARGET sdl3-mixer)
pkg_check_modules(SDL3_NET sdl3-net)

pkg_check_modules(ESPEAK_NG espeak-ng)
if (ESPEAK_NG_FOUND)
    set(WITH_ESPEAK 1)
    set(WITH_ESPEAK_NG 1)
else()
    pkg_check_modules(ESPEAK espeak)
    if (ESPEAK_FOUND)
        set(WITH_ESPEAK 1)
        set(WITH_ESPEAK_NG 0)
    endif()
endif()

if (SDL3_NET_FOUND)
    set(CLEAN_SDL3_NET_INCLUDE_DIRS "")
    foreach(dir ${SDL3_NET_INCLUDE_DIRS})
        if (EXISTS ${dir})
            list(APPEND CLEAN_SDL3_NET_INCLUDE_DIRS ${dir})
        endif()
    endforeach()
    set(SDL3_NET_INCLUDE_DIRS ${CLEAN_SDL3_NET_INCLUDE_DIRS})
    set(HAVE_LIBSDL_NET 1)
endif ()

# needed for SVG loading and caching
find_package(PNG)
find_package(RSVG)
set(HAVE_RSVG RSVG_FOUND)
set(HAVE_LIBPNG PNG_FOUND)

message("SDL_Pango:  ${SDLPANGO_FOUND}")



# based on tuxmath's CMakeLists.txt
if (APPLE)
    set(MACOSX_BUNDLE_NAME ${PACKAGE_NAME})
    set(DATA_PREFIX
	${T4K_COMMON_MACOSX_BUNDLE_NAME}.app/Contents/Resources)
elseif(UNIX)
    set (DATA_PREFIX
	${CMAKE_INSTALL_PREFIX}/share/${LIB_NAME})
elseif(WIN32)
    t4k_to_unix_path(DATA_PREFIX "\$ENV{APPDATA}/${PACKAGE_NAME}")
    # Activate existing MinGW-specific code paths (e.g. single-arg mkdir)
    if(MINGW)
        add_definitions(-DBUILD_MINGW32)
    endif()
else()
    message(SEND_ERROR "What OS are you on?")
endif()

set(LINEBREAK_BINARY_DIR ${CMAKE_BINARY_DIR}/src/linebreak)
set(LINEBREAK_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/linebreak)

#process subdirectories
add_subdirectory(src)
add_subdirectory(data)

include(CPack)

# FIXME - below are "dumb" assignments to the variables
# required for t4k_common.pc.in to generate a working t4k_common.pc
# We should make these variables reflect what libs we actually found,
# particularly SDL_Pango vs. SDL_ttf - DSB
set(DEP_PKGS "sdl3 sdl3-image sdl3-ttf sdl3-mixer librsvg-2.0 libxml-2.0") 
set(DEP_LIBS "") 
set(DEP_LIBS_PRIVATE "-mwindows -lmikmod -lsmpeg -lstdc++ -lmingw32 -lSDLmain -liconv -luser32 -lgdi32 -lwinmm -ldxguid -lSDL -lpthread -lvorbisfile -lvorbis -lm -logg -lfreetype") 
