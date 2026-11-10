cmake_minimum_required(VERSION 3.20)

# Static CRT: players (and the bot) don't need the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(TLDCoop LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The Long Dark is 64-bit; configure from an x64 developer environment.")
endif()

# MinHook, built here so its trampoline builder can be ours (src/minhook_trampoline.c): it jumps back into a hooked
# function past the bytes another mod's hook may overwrite when it hooks the same function after us.
add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/hde/hde64.c
  src/minhook_trampoline.c
)
target_include_directories(minhook PUBLIC third_party/minhook/include PRIVATE third_party/minhook/src)

# Everything below is our code.
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
# TLDCoop_DLC: the build's commit, shown on the co-op screens (so everyone can see they run the same dll)
execute_process(COMMAND git rev-parse --short=7 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE TLDCOOP_BUILD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT TLDCOOP_BUILD)
  set(TLDCOOP_BUILD "local")
endif()
add_compile_definitions(TLDCOOP_BUILD="${TLDCOOP_BUILD}")
add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zi)
add_link_options(/DEBUG /OPT:REF /OPT:ICF)

# Protocol + UDP session, shared by the mod and the bot.
add_library(tldnet STATIC src/net/session.cpp src/net/udp_transport.cpp src/net/steam_transport.cpp)
target_include_directories(tldnet PUBLIC src)
target_link_libraries(tldnet PUBLIC ws2_32)

# TLDCoop_DLC: ClothingExpanded's pack, as resources in the DLL
set(CE_DIR ${CMAKE_SOURCE_DIR}/third_party/clothing-expanded)
configure_file(src/clothing_expanded.rc.in ${CMAKE_BINARY_DIR}/clothing_expanded.rc @ONLY)

# The mod. The game loads it as winhttp.dll.
add_library(tldcoop SHARED
  src/dllmain.cpp
  src/ambient_lights.cpp
  src/house_lights.cpp  # TLDCoop_DLC: House Lights (DemonBunnyBon / Xpazeman), with permission
  src/slot_machine.cpp  # TLDCoop_DLC: the automat (slot machine), test builds
  src/automat_items.cpp  # TLDCoop_DLC: the automat's items (board, mechanism, automat)
  src/coop_features.cpp
  src/dynamic_trees.cpp
  src/seamless_interiors.cpp  # TLDCoop_DLC: Seamless Interiors (karsontr13), with permission
  src/custom_pictures.cpp
  src/night_sky.cpp  # TLDCoop_DLC: Better Night Sky (built in)
  src/fuel_transfer.cpp  # TLDCoop_DLC: drain a kerosene lamp into a canister
  src/coop_summon.cpp  # TLDCoop_DLC: F1 teleportplayer (host calls a friend)
  src/coop_train.cpp   # TLDCoop_DLC: Iron Road's steam train in co-op
  src/custom_text.cpp  # TLDCoop_DLC: our entries in TLD's string table
  src/clothing_expanded.cpp  # TLDCoop_DLC: ClothingExpanded (TonisGaming)
  src/item_rarity.cpp  # TLDCoop_DLC: item rarities (our own)
  ${CMAKE_BINARY_DIR}/clothing_expanded.rc  # (its bundles and catalog)
  src/food_tweaker.cpp  # TLDCoop_DLC: Food Tweaker (built in)
  src/coop_activity.cpp  # TLDCoop_DLC: what a player's body is busy with (avatar motion capture)
  src/loading_screen.cpp  # TLDCoop_DLC: the start screen (Karol's picture)
  src/coop_chat.cpp  # TLDCoop_DLC: chat (ENTER) and the bug report's description box
  src/coop_save.cpp  # TLDCoop_DLC: ZAPISZ GRĘ in ESC's menu
  src/coop_report.cpp  # TLDCoop_DLC: a bug report with one key (F11)
  src/just_zoom.cpp  # TLDCoop_DLC: JustZoom (WillWunderhorn), with permission
  src/dev_console.cpp  # TLDCoop_DLC: for testing; to be removed (src/dev_console.h)
  src/avatar.cpp
  src/avatar_tracks.cpp  # TLDCoop_DLC: the others' footprints in the snow
  src/config.cpp
  src/coop_death.cpp
  src/coop_give.cpp
  src/coop_hud.cpp
  src/coop_emotes.cpp
  src/coop_lapse.cpp
  src/coop_sleep.cpp
  src/coop_menu.cpp
  src/coop_notices.cpp
  src/core.cpp
  src/dev_commands.cpp
  src/dumper.cpp
  src/game_state.cpp
  src/hooks.cpp
  src/il2cpp_api.cpp
  src/log.cpp
  src/net_runtime.cpp
  src/optional_content.cpp
  src/paths.cpp
  src/steam_dispatch.cpp
  src/test_harness.cpp
  src/ui_inspect.cpp
  src/wildlife_carcass.cpp
  src/wildlife_hits.cpp
  src/wildlife_sync.cpp
  src/wildlife_targets.cpp
  src/world_containers.cpp
  src/world_objects.cpp
  src/world_fires.cpp
  src/world_items.cpp
  src/world_join.cpp
  src/world_keep.cpp
  src/world_mode.cpp
  src/world_sync.cpp
  src/world_water.cpp
  src/world_blood.cpp
  src/coop_backup.cpp
  src/world_spray.cpp
)
# TLDCoop_DLC: TLD's developer console, for testing; to be removed (src/dev_console.h)
target_compile_definitions(tldcoop PRIVATE TLDCOOP_DEV_CONSOLE)
set_target_properties(tldcoop PROPERTIES OUTPUT_NAME winhttp)
# TLDCoop_DLC: a map of the functions (winhttp.map, in the dll-latest release): a crash's "winhttp.dll+0x..." in a
# player's coop.log can be looked up.
target_link_options(tldcoop PRIVATE "/MAP:${CMAKE_BINARY_DIR}/winhttp.map")
target_link_libraries(tldcoop PRIVATE minhook tldnet)

# Fake second player for solo testing.
add_executable(tldbot src/bot/main.cpp)
target_link_libraries(tldbot PRIVATE tldnet)

# Offline tests (no game needed).
add_executable(interp_test tests/interp_test.cpp)
target_link_libraries(interp_test PRIVATE tldnet)
add_executable(protocol_test tests/protocol_test.cpp)
target_link_libraries(protocol_test PRIVATE tldnet)
add_executable(claims_test tests/claims_test.cpp)
target_link_libraries(claims_test PRIVATE tldnet)
add_executable(harvests_test tests/harvests_test.cpp)
target_link_libraries(harvests_test PRIVATE tldnet)
add_executable(session_test tests/session_test.cpp)
target_link_libraries(session_test PRIVATE tldnet)
add_executable(lapse_test tests/lapse_test.cpp)
target_link_libraries(lapse_test PRIVATE tldnet)
add_executable(uniques_test tests/uniques_test.cpp)
target_link_libraries(uniques_test PRIVATE tldnet)
add_executable(sleep_vote_test tests/sleep_vote_test.cpp)
target_link_libraries(sleep_vote_test PRIVATE tldnet)
add_executable(held_test tests/held_test.cpp)
target_link_libraries(held_test PRIVATE tldnet)
add_executable(held_pose_test tests/held_pose_test.cpp)
target_link_libraries(held_pose_test PRIVATE tldnet)
add_executable(hud_test tests/hud_test.cpp)
target_link_libraries(hud_test PRIVATE tldnet)
add_executable(handover_test tests/handover_test.cpp)
target_link_libraries(handover_test PRIVATE tldnet)
add_executable(content_test tests/content_test.cpp)
target_link_libraries(content_test PRIVATE tldnet)
add_executable(water_test tests/water_test.cpp)
target_link_libraries(water_test PRIVATE tldnet)
add_executable(locks_test tests/locks_test.cpp)
target_link_libraries(locks_test PRIVATE tldnet)
add_executable(emotes_test tests/emotes_test.cpp)
target_link_libraries(emotes_test PRIVATE tldnet)
add_executable(blood_test tests/blood_test.cpp)
target_link_libraries(blood_test PRIVATE tldnet)
add_executable(afflictions_test tests/afflictions_test.cpp)
target_link_libraries(afflictions_test PRIVATE tldnet)
add_executable(ambient_test tests/ambient_test.cpp)
target_link_libraries(ambient_test PRIVATE tldnet)
add_executable(trees_test tests/trees_test.cpp)
target_link_libraries(trees_test PRIVATE tldnet)
add_executable(wildlife_own_test tests/wildlife_own_test.cpp)
target_link_libraries(wildlife_own_test PRIVATE tldnet)
