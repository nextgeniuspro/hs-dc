cmake_minimum_required(VERSION 3.16)
project(blackbeard LANGUAGES CXX)

# The Dreamcast build comes in through KallistiOS's own toolchain file, which
# sets CMAKE_SYSTEM_NAME from $KOS_ARCH. Nothing else distinguishes it: the
# shim, the game and the software renderer are the same sources on both
# targets, and only the Host backend and the entry point differ.
#
#   scripts/dc-build.sh     builds it
#   cmake -S . -B build     still builds the desktop app, unchanged
if(CMAKE_SYSTEM_NAME STREQUAL "dreamcast")
  set(BB_DREAMCAST ON)
  message(STATUS "target: Sega Dreamcast (KallistiOS)")
endif()

set(CMAKE_CXX_STANDARD 20)  # vfspp requires C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

if(BB_DREAMCAST)
  # One name libstdc++ leaves out on newlib. Force-included so that no game
  # source has to know which C library it is being compiled against; the
  # header explains itself.
  add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/DcCompat.h)
endif()

# --- zlib (system, or kos-ports on the Dreamcast) ----------------------------
# The pak's entries are deflated, so zlib is not optional on either target.
if(BB_DREAMCAST)
  # kos-ports keeps each port's headers in a directory of its own
  # (<zlib/zlib.h>). Putting that directory on the include path is what lets
  # the pak reader go on saying <zlib.h> like every other build of it does.
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
# The difference between a build for a machine on a desk and one for a
# television. Off by default, so what someone plays is the game rather than the
# workshop.
#
#   cmake -S . -B build -DBB_DEV=ON
#
# What it turns on:
#   * the mission browser row, which reaches any single-player map without
#     playing the campaign chain to get there (game/MenuScreens.cpp);
#   * on the Dreamcast, quitting returns to the loader instead of the console's
#     menu, so dc-tool can send the next build straight back down the wire
#     (platform/DcMain.cpp).
#
# PUBLIC, and it has to be: the rows are compiled into bb_game, and the usage
# requirement is what carries the definition out to the entry point in the app
# target too.
option(BB_DEV "Include the port's development-only menu entries" OFF)
if(BB_DEV)
  target_compile_definitions(bb_game PUBLIC BB_DEV)
  message(STATUS "BB_DEV: development-only menu entries are in")
endif()

# --- verbose logging ---------------------------------------------------------
# What the port says for itself: errors always, a handful of startup lines by
# default, and the running commentary only when asked for (src/shim/Log.h). The
# desktop build takes --verbose on the command line, so this is really for the
# Dreamcast, where there is no command line to take it on.
#
#   scripts/dc-build.sh -DBB_VERBOSE=ON
option(BB_VERBOSE "Log the running commentary, not just errors" OFF)
if(BB_VERBOSE)
  target_compile_definitions(bb_shim PUBLIC BB_VERBOSE)
  message(STATUS "BB_VERBOSE: the running commentary is on by default")
endif()

# --- Dreamcast app -----------------------------------------------------------
# Produces an ELF; scripts/dc-cdi.sh turns it and the data into a .cdi image.
if(BB_DREAMCAST)
  # KOS's toolchain gives executables an .elf suffix, so this lands as
  # blackbeard.elf -- the file mkdcdisc turns into 1ST_READ.BIN.
  add_executable(blackbeard
    src/platform/DcMain.cpp
    src/platform/KosHost.cpp
    src/platform/VmuStorage.cpp
    src/platform/AudioKeepalive.cpp
  )
  target_link_libraries(blackbeard PRIVATE bb_game bb_platform)
endif()

# --- SDL2 app (optional; skipped if SDL2 absent) -----------------------------
find_package(SDL2 QUIET)
if(SDL2_FOUND AND NOT BB_DREAMCAST)
  add_executable(blackbeard
    src/platform/SdlMain.cpp
    src/platform/SdlHost.cpp
    src/platform/SdlFrames.cpp
  )
  # stb_image is the port's only image decoder, and only the SDL app needs it:
  # the device frames are PNGs at window resolution, not .tc out of data.pak.
  target_include_directories(blackbeard PRIVATE third_party/stb)
  # stb_image carries entry points we do not call; -Wall would bury the port's
  # own warnings under them.
  set_source_files_properties(src/platform/SdlFrames.cpp PROPERTIES
    COMPILE_OPTIONS -Wno-unused-function)
  target_link_libraries(blackbeard PRIVATE bb_game bb_platform SDL2::SDL2)
  message(STATUS "SDL2 found: building 'blackbeard' app target")
elseif(NOT BB_DREAMCAST)
  message(STATUS "SDL2 not found: the libraries will build, but no app target")
endif()
