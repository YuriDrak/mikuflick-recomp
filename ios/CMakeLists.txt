cmake_minimum_required(VERSION 3.18)
project(MikuFlick LANGUAGES C CXX OBJC)
set(deps "${CMAKE_CURRENT_SOURCE_DIR}/build/deps")

# signed device builds: the team applies to every target, so Xcode can also sign the static SDL/FreeType
# libraries it builds alongside the app (otherwise "Signing for \"freetype\" requires a development team")
set(MF_TEAM_ID "" CACHE STRING "Apple development team ID for signed device builds (optional)")
if(MF_TEAM_ID)
  set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MF_TEAM_ID}")
endif()

set(BUILD_SHARED_LIBS OFF)
set(SDL_SHARED OFF CACHE BOOL "")
set(SDL_STATIC ON CACHE BOOL "")
set(SDL_TEST OFF CACHE BOOL "")
add_subdirectory("${deps}/SDL2-2.30.10" SDL2 EXCLUDE_FROM_ALL)

set(SDL2IMAGE_VENDORED OFF CACHE BOOL "")
set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "")
set(SDL2IMAGE_BACKEND_IMAGEIO OFF CACHE BOOL "")
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "")
set(SDL2IMAGE_INSTALL OFF CACHE BOOL "")
foreach(format AVIF BMP GIF JPG JXL LBM PCX PNM QOI SVG TGA TIF WEBP XCF XPM XV)
  set(SDL2IMAGE_${format} OFF CACHE BOOL "")
endforeach()
set(SDL2IMAGE_PNG ON CACHE BOOL "")
set(SDL2IMAGE_PNG_SAVE ON CACHE BOOL "")
add_subdirectory("${deps}/SDL2_image-2.8.2" SDL2_image EXCLUDE_FROM_ALL)

set(SDL2TTF_VENDORED ON CACHE BOOL "")
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "")
set(SDL2TTF_SAMPLES OFF CACHE BOOL "")
set(SDL2TTF_INSTALL OFF CACHE BOOL "")
add_subdirectory("${deps}/SDL2_ttf-2.22.0" SDL2_ttf EXCLUDE_FROM_ALL)

set(port "${CMAKE_CURRENT_SOURCE_DIR}/../port")
file(GLOB sources CONFIGURE_DEPENDS "${port}/src/*.c")
set(font_resources "${port}/fonts/MPLUS1p-Bold.ttf" "${port}/fonts/OFL.txt")
set_source_files_properties(${font_resources} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/fonts)
# app icon: the iOS version of the Android launcher icon (App/AppIcon.svg), compiled by actool
set(app_assets "${CMAKE_CURRENT_SOURCE_DIR}/App/Assets.xcassets")
add_executable(MikuFlick MACOSX_BUNDLE ${sources} App/Platform.m App/ImportViewController.m App/DlcViewController.m App/Scene.m ${font_resources}
    ${app_assets})
target_compile_features(MikuFlick PRIVATE c_std_11)
target_compile_options(MikuFlick PRIVATE -Wall -Wextra -Wno-unused-parameter)   # as port/Makefile
set_source_files_properties("${port}/src/pl_mpeg_impl.c" PROPERTIES COMPILE_OPTIONS -Wno-shorten-64-to-32)   # vendored
target_compile_definitions(MikuFlick PRIVATE MF_IOS)
set_source_files_properties(App/Platform.m App/ImportViewController.m App/DlcViewController.m App/Scene.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
target_include_directories(MikuFlick PRIVATE App "${port}/third_party" "${port}/src")
target_link_libraries(MikuFlick PRIVATE SDL2_image::SDL2_image-static
    SDL2_ttf::SDL2_ttf-static SDL2::SDL2-static "-framework UniformTypeIdentifiers")
set(MF_BUNDLE_ID "io.github.tehcnological.mikuflick" CACHE STRING "iOS bundle identifier")
set_target_properties(MikuFlick PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/App/Info.plist.in"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${MF_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY 1
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
    RESOURCE "${app_assets}")
