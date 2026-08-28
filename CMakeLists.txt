cmake_minimum_required(VERSION 3.16)
project(blackbeard LANGUAGES CXX)


if(CMAKE_SYSTEM_NAME STREQUAL "dreamcast")
  set(BB_DREAMCAST ON)
  message(STATUS "target: Sega Dreamcast (KallistiOS)")
endif()


option(BB_MAC_BUNDLE "Build a self-contained, universal Blackbeard.app" OFF)
if(BB_MAC_BUNDLE)
  if(NOT APPLE)
    message(FATAL_ERROR "BB_MAC_BUNDLE builds a macOS .app; this is not macOS")
  endif()
  set(BB_MAC_SDL2 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mac/SDL2.framework")
  if(NOT EXISTS "${BB_MAC_SDL2}")
    message(FATAL_ERROR
      "No universal SDL2 at ${BB_MAC_SDL2}.\n"
      "Fetch it first: scripts/fetch-sdl2-mac.sh")
  endif()
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
  # 11.0 is where arm64 starts, so a universal build cannot promise less.
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
  endif()
  message(STATUS "BB_MAC_BUNDLE: universal (${CMAKE_OSX_ARCHITECTURES}), "
                 "macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}+")
endif()

set(CMAKE_CXX_STANDARD 20)  # vfspp requires C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

if(BB_DREAMCAST)
  add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/DcCompat.h)
endif()


if(BB_DREAMCAST)
  if(NOT KOS_PORTS_DIR)
    set(KOS_PORTS_DIR "${CMAKE_SYSROOT}")
  endif()
  if(NOT EXISTS "${KOS_PORTS_DIR}/include/zlib/zlib.h")
    message(FATAL_ERROR
      "zlib not found in kos-ports (${KOS_PORTS_DIR}/include/zlib/zlib.h).\n"
      "Install it with: cd $ENV{KOS_PORTS}/zlib && make install")
  endif()
  add_library(bb_zlib INTERFACE)
  target_include_directories(bb_zlib INTERFACE "${KOS_PORTS_DIR}/include/zlib")
  target_link_directories(bb_zlib INTERFACE "${KOS_PORTS_DIR}/lib")
  target_link_libraries(bb_zlib INTERFACE z)
  add_library(ZLIB::ZLIB ALIAS bb_zlib)
else()
  find_package(ZLIB REQUIRED)
endif()

# --- shim: mini-Symbian + platform-independent engine services ---------------
add_library(bb_shim STATIC
  src/shim/Pak.cpp
  src/shim/Descriptors.cpp
  src/shim/Resources.cpp
  src/shim/Log.cpp
)
target_include_directories(bb_shim PUBLIC
  src
  third_party/vfspp/include        # header-only VFS (native/memory backends only; zip needs its submodule)
)
target_link_libraries(bb_shim PUBLIC ZLIB::ZLIB)

# --- platform: software rendering & the host seam (no SDL, stays testable) ---
add_library(bb_platform STATIC
  src/platform/Framebuffer.cpp
  src/platform/Surface.cpp
  src/platform/FileStorage.cpp
  src/platform/BootFont.cpp
  src/platform/BootScreen.cpp
)
target_link_libraries(bb_platform PUBLIC bb_shim)

# --- game: decompiled game logic (grows function-by-function) ----------------
add_library(bb_game STATIC
  src/game/TcTexture.cpp
  src/game/Palette.cpp
  src/game/PaletteFlags.cpp
  src/game/ResourceTable.cpp
  src/game/TextureCache.cpp
  src/game/Font.cpp
  src/game/ConfigFile.cpp
  src/game/SpcAudio.cpp
  src/game/Mixer.cpp
  src/game/SoundManager.cpp
  src/game/LapAnimation.cpp
  src/game/Subtitles.cpp
  src/game/Cutscene.cpp
  src/game/NdLevel.cpp
  src/game/BattleData.cpp
  src/game/Commanders.cpp
  src/game/BattleField.cpp
  src/game/BattleInfo.cpp
  src/game/BattleRenderer.cpp
  src/game/BattlePanels.cpp
  src/game/CaptureHoist.cpp
  src/game/BuildMenu.cpp
  src/game/CellBoard.cpp
  src/game/TextBox.cpp
  src/game/LoadingScreen.cpp
  src/game/Flock.cpp
  src/game/TravelMap.cpp
  src/game/TravelMenu.cpp
  src/game/TravelWorld.cpp
  src/game/SkillTree.cpp
  src/game/Perks.cpp
  src/game/PerkFlash.cpp
  src/game/FightAnimation.cpp
  src/game/ScreenShake.cpp
  src/game/MissionFlow.cpp
  src/game/TriggerScript.cpp
  src/game/TriggerRunner.cpp
  src/game/BattleDialogs.cpp
  src/game/CampaignStates.cpp
  src/game/BattleAi.cpp
  src/game/BattleScreen.cpp
  src/game/MissionDatabase.cpp
  src/game/MenuList.cpp
  src/game/MenuChrome.cpp
  src/game/CardPicker.cpp
  src/game/MenuScreen.cpp
  src/game/MenuScreens.cpp
  src/game/Settings.cpp
  src/game/SaveGame.cpp
  src/game/Water.cpp
  src/game/SeaSurface.cpp
  src/game/Strings.cpp
  src/game/StateMachine.cpp
  src/game/Boot.cpp
  src/game/MenuStates.cpp
  src/game/Game.cpp
)
target_link_libraries(bb_game PUBLIC bb_shim bb_platform)

# --- development build -------------------------------------------------------
#   cmake -S . -B build -DBB_DEV=ON
option(BB_DEV "Include the port's development-only menu entries" OFF)
if(BB_DEV)
  target_compile_definitions(bb_game PUBLIC BB_DEV)
  message(STATUS "BB_DEV: development-only menu entries are in")
endif()

# --- verbose logging ---------------------------------------------------------
#   scripts/dc-build.sh -DBB_VERBOSE=ON
option(BB_VERBOSE "Log the running commentary, not just errors" OFF)
if(BB_VERBOSE)
  target_compile_definitions(bb_shim PUBLIC BB_VERBOSE)
  message(STATUS "BB_VERBOSE: the running commentary is on by default")
endif()

# --- Dreamcast app -----------------------------------------------------------
if(BB_DREAMCAST)
  add_executable(blackbeard
    src/platform/DcMain.cpp
    src/platform/KosHost.cpp
    src/platform/VmuStorage.cpp
    src/platform/AudioKeepalive.cpp
    src/platform/DcSdCard.cpp
  )
  
  target_link_libraries(blackbeard PRIVATE bb_game bb_platform kosfat)

  option(BB_SD_SCI "Also look for an SD reader on the SCI port (can hang)" OFF)
  if(BB_SD_SCI)
    target_compile_definitions(blackbeard PRIVATE BB_SD_SCI)
    message(STATUS "BB_SD_SCI: the SCI reader is probed too")
  endif()
endif()

if(NOT BB_DREAMCAST)
  if(BB_MAC_BUNDLE)
    add_library(bb_sdl2 INTERFACE)

    target_include_directories(bb_sdl2 INTERFACE "${BB_MAC_SDL2}/Headers")
    target_link_libraries(bb_sdl2 INTERFACE "${BB_MAC_SDL2}")
    set(BB_HAVE_SDL2 ON)
    message(STATUS "SDL2: vendored universal framework")
  else()
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
      add_library(bb_sdl2 INTERFACE)
      target_link_libraries(bb_sdl2 INTERFACE SDL2::SDL2)
      set(BB_HAVE_SDL2 ON)
    endif()
  endif()
endif()

if(BB_HAVE_SDL2)
  add_executable(blackbeard
    src/platform/SdlMain.cpp
    src/platform/SdlHost.cpp
    src/platform/SdlFrames.cpp
    src/platform/DataFiles.cpp
    src/platform/ImportScreen.cpp
  )
  # Finding the game's data, and asking for it when it is not there. No
  # console needs any of this -- a Dreamcast is handed a disc or it is not
  # (src/platform/DcMain.cpp) -- so it lives with the desktop entry point.
  if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    target_sources(blackbeard PRIVATE src/platform/FileDialogMac.mm)
    target_link_libraries(blackbeard PRIVATE "-framework AppKit")
  else()
    target_sources(blackbeard PRIVATE src/platform/FileDialog.cpp)
    if(WIN32)
      target_link_libraries(blackbeard PRIVATE comdlg32)
      # The application icon. On macOS this is a file in the bundle and the
      # plist names it; on Windows it is compiled into the executable, so it
      # comes in as a resource script instead (cmake/AppIcon.rc.in).
      enable_language(RC)
      set(BB_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/Blackbeard.ico")
      configure_file(cmake/AppIcon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.rc"
                     @ONLY)
      target_sources(blackbeard PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.rc")
    endif()
  endif()
  
  target_include_directories(blackbeard PRIVATE third_party/stb)

  set_source_files_properties(src/platform/SdlFrames.cpp PROPERTIES
    COMPILE_OPTIONS -Wno-unused-function)
  target_link_libraries(blackbeard PRIVATE bb_game bb_platform bb_sdl2)
  message(STATUS "SDL2 found: building 'blackbeard' app target")

  if(BB_MAC_BUNDLE)
    set_target_properties(blackbeard PROPERTIES
      MACOSX_BUNDLE TRUE
      OUTPUT_NAME "Blackbeard"
      MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
      MACOSX_BUNDLE_BUNDLE_NAME "Blackbeard"
      MACOSX_BUNDLE_EXECUTABLE_NAME "Blackbeard"
      MACOSX_BUNDLE_ICON_FILE "Blackbeard.icns"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.redlynx.blackbeard.port"
      MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
      BUILD_WITH_INSTALL_RPATH TRUE
      INSTALL_RPATH "@executable_path/../Frameworks")

    set(BB_APP "$<TARGET_BUNDLE_DIR:blackbeard>")
    set(BB_APP_CONTENTS "$<TARGET_BUNDLE_CONTENT_DIR:blackbeard>")
    add_custom_command(TARGET blackbeard POST_BUILD
      VERBATIM
      COMMAND ${CMAKE_COMMAND} -E make_directory "${BB_APP_CONTENTS}/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E rm -rf
              "${BB_APP_CONTENTS}/Frameworks/SDL2.framework"

      COMMAND cp -R "${BB_MAC_SDL2}" "${BB_APP_CONTENTS}/Frameworks/"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/frames"
              "${BB_APP_CONTENTS}/Resources/frames"
      COMMAND ${CMAKE_COMMAND} -E copy
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons.pak"
              "${BB_APP_CONTENTS}/Resources/icons.pak"
      COMMAND ${CMAKE_COMMAND} -E copy
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/Blackbeard.icns"
              "${BB_APP_CONTENTS}/Resources/Blackbeard.icns"
      COMMAND find "${BB_APP_CONTENTS}/Resources" -name .DS_Store -delete
      COMMAND codesign --force --sign - --timestamp=none
              "${BB_APP_CONTENTS}/Frameworks/SDL2.framework"
      COMMAND codesign --force --sign - --timestamp=none "${BB_APP}"
      COMMENT "Bundling Blackbeard.app (SDL2, assets, ad-hoc signature)")
  endif()
elseif(NOT BB_DREAMCAST)
  message(STATUS "SDL2 not found: the libraries will build, but no app target")
endif()
