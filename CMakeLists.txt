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

# --- macOS app bundle --------------------------------------------------------
# The difference between a build for the machine that made it and one that can
# be handed to somebody else.
#
#   scripts/fetch-sdl2-mac.sh
#   cmake -S . -B build-mac -DBB_MAC_BUNDLE=ON && cmake --build build-mac -j
#
# An ordinary desktop build links whatever SDL2 the machine has, which is
# Homebrew's, which is one architecture at an absolute path -- /usr/local on an
# Intel Mac, /opt/homebrew on an Apple Silicon one. That binary does not
# travel: on a Mac without Homebrew dyld cannot find the library at all, and on
# a Mac with the *other* Homebrew it finds one built for the wrong CPU. Neither
# failure looks like anything from the outside; the app just does not open.
#
# So the bundle does not use the machine's SDL2. It links libsdl.org's own
# universal framework, embeds a copy inside itself, and builds both
# architectures, which between them are the three things that make an app open
# on a Mac that has never heard of this one.
#
# Set here, above every target, because CMAKE_OSX_ARCHITECTURES is read as each
# one is created -- the static libraries have to be universal too or there is
# nothing for the arm64 slice of the app to link against.
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
# Two ways to get one, behind the same interface target: the machine's, which
# is what a developer builds against, and the vendored universal framework,
# which is what ships. Nothing below this block knows which it got.
if(NOT BB_DREAMCAST)
  if(BB_MAC_BUNDLE)
    add_library(bb_sdl2 INTERFACE)
    # The framework's headers are reached as <SDL.h> like everywhere else,
    # rather than the <SDL2/SDL.h> a framework include would give, so that not
    # one line of the port has to know which SDL2 it was built against.
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
    src/platform/BootFont.cpp
    src/platform/DataFiles.cpp
    src/platform/ImportScreen.cpp
  )
  # Finding the game's data, and asking for it when it is not there. No
  # console needs any of this -- a Dreamcast is handed a disc or it is not
  # (src/platform/DcMain.cpp) -- so it lives with the desktop entry point.
  #
  # The file picker is the one piece with no portable answer: AppKit's panel
  # is Objective-C, Windows has a common dialog, and on a free desktop it is
  # whichever helper the environment installed. So macOS gets a translation
  # unit of its own and the rest share one.
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
  # stb_image is the port's only image decoder, and only the SDL app needs it:
  # the device frames are PNGs at window resolution, not .tc out of data.pak.
  target_include_directories(blackbeard PRIVATE third_party/stb)
  # stb_image carries entry points we do not call; -Wall would bury the port's
  # own warnings under them.
  set_source_files_properties(src/platform/SdlFrames.cpp PROPERTIES
    COMPILE_OPTIONS -Wno-unused-function)
  target_link_libraries(blackbeard PRIVATE bb_game bb_platform bb_sdl2)
  message(STATUS "SDL2 found: building 'blackbeard' app target")

  # --- ...and, on request, wrapped up as an app somebody else can open -------
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
      # The embedded copy, and only the embedded copy. BUILD_WITH_INSTALL_RPATH
      # puts this in the binary at link time rather than at install time, so
      # what comes out of the build directory is already the thing that ships:
      # there is no `make install` step here that could be the one that made it
      # work, and no way to hand somebody a half-fixed app by mistake.
      BUILD_WITH_INSTALL_RPATH TRUE
      INSTALL_RPATH "@executable_path/../Frameworks")

    set(BB_APP "$<TARGET_BUNDLE_DIR:blackbeard>")
    set(BB_APP_CONTENTS "$<TARGET_BUNDLE_CONTENT_DIR:blackbeard>")
    add_custom_command(TARGET blackbeard POST_BUILD
      VERBATIM
      COMMAND ${CMAKE_COMMAND} -E make_directory "${BB_APP_CONTENTS}/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E rm -rf
              "${BB_APP_CONTENTS}/Frameworks/SDL2.framework"
      # `cp -R` rather than `cmake -E copy_directory`: a framework is a tree of
      # symlinks around one real version, and copying it by following them
      # produces a directory the loader does not recognise as a framework.
      COMMAND cp -R "${BB_MAC_SDL2}" "${BB_APP_CONTENTS}/Frameworks/"
      # The port's own art, which is not in data.pak and so has to travel too.
      # SDL_GetBasePath() answers Contents/Resources inside a bundle, which is
      # the first place SdlMain's AssetPaths looks -- so this lands exactly
      # where an installed build already expects to find it.
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/frames"
              "${BB_APP_CONTENTS}/Resources/frames"
      COMMAND ${CMAKE_COMMAND} -E copy
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons.pak"
              "${BB_APP_CONTENTS}/Resources/icons.pak"
      # The app icon, which Info.plist names by CFBundleIconFile. Not to be
      # confused with icons.pak just above it -- that one is the soft-key hints
      # the game draws, this one is what the Dock shows.
      COMMAND ${CMAKE_COMMAND} -E copy
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/Blackbeard.icns"
              "${BB_APP_CONTENTS}/Resources/Blackbeard.icns"
      # Finder leaves one of these in every directory it has been asked to
      # show, and copy_directory copies whatever it finds. Swept before the
      # signature is taken, since anything added afterwards invalidates it.
      COMMAND find "${BB_APP_CONTENTS}/Resources" -name .DS_Store -delete
      # Signed last, and the framework before the app that contains it. Ad-hoc
      # ("-") is not a Developer ID and Gatekeeper will still ask before a
      # downloaded copy is opened the first time, but an arm64 binary that is
      # not signed at all does not run *anywhere*, and copying a framework in
      # after the linker signed the executable invalidates what it did.
      COMMAND codesign --force --sign - --timestamp=none
              "${BB_APP_CONTENTS}/Frameworks/SDL2.framework"
      COMMAND codesign --force --sign - --timestamp=none "${BB_APP}"
      COMMENT "Bundling Blackbeard.app (SDL2, assets, ad-hoc signature)")
  endif()
elseif(NOT BB_DREAMCAST)
  message(STATUS "SDL2 not found: the libraries will build, but no app target")
endif()
