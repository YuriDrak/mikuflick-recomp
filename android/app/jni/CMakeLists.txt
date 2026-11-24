# Native part of the Android build: SDL2 + SDL2_image + SDL2_ttf from source
# (android/third_party, fetched by `make -C android toolchain`) and the port
# itself as libmain.so, which SDLActivity loads and whose SDL_main it calls.
cmake_minimum_required(VERSION 3.18)
project(mikuflick C)

set(TP   "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party")
set(PORT "${CMAKE_CURRENT_SOURCE_DIR}/../../../port")

set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

# SDL2
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/SDL2" SDL2)

# SDL2_image: PNG through the built-in stb_image backend, nothing else needed
set(SDL2IMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
foreach(fmt AVIF JPG JXL TIF WEBP SVG XCF XPM XV LBM PCX PNM QOI TGA GIF BMP)
  set(SDL2IMAGE_${fmt} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL2IMAGE_PNG ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_PNG_SAVE ON CACHE BOOL "" FORCE)
add_subdirectory("${TP}/SDL2_image" SDL2_image)

# SDL2_ttf with its bundled FreeType (no HarfBuzz needed for these menus)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/SDL2_ttf" SDL2_ttf)

# The game: the same sources as the Linux build
file(GLOB MF_SRC CONFIGURE_DEPENDS "${PORT}/src/*.c")
add_library(main SHARED ${MF_SRC})
set_target_properties(main PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(main PRIVATE "${PORT}/third_party")
target_compile_definitions(main PRIVATE _GNU_SOURCE)
target_compile_options(main PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(main PRIVATE SDL2::SDL2 SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf android log m)
