cmake_minimum_required(VERSION 3.15)
project(SteamworksMinimalServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Locate Steamworks SDK ---
# STEAMWORKS_SDK_PATH is defined in the parent CMakeLists.txt
include_directories(${STEAMWORKS_SDK_PATH}/public) # For steam_gameserver.h if it's there

# --- Executable ---
add_executable(SteamworksMinimalServer
    server_main.cpp
    server.cpp
    server.h
)

# --- Link Libraries ---
target_link_libraries(SteamworksMinimalServer PRIVATE spdlog::spdlog)

# Platform-specific linking for Steamworks GameServer
if(WIN32)
    # For Windows, link steam_api64.lib (assuming 64-bit build)
    # The actual .dll must be next to the executable at runtime.
    # Servers typically also need steamclient64.dll (or equivalent) from Steam Client runtime.
    target_link_directories(SteamworksMinimalServer PRIVATE ${STEAMWORKS_SDK_PATH}/redistributable_bin/win64)
    target_link_libraries(SteamworksMinimalServer PRIVATE steam_api64) # For GameServer API

    # Copy steam_api64.dll (for game server)
    add_custom_command(TARGET SteamworksMinimalServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${STEAMWORKS_SDK_PATH}/redistributable_bin/win64/steam_api64.dll"
        $<TARGET_FILE_DIR:SteamworksMinimalServer>)

    # Servers on Windows often also need steamclient64.dll. This usually comes from the Steam client installation
    # or a dedicated server package. For local testing, having Steam client installed is enough.
    # For dedicated servers, you might need to include it from the SDK's tools/SteamPipeGUI/... or similar
    # if you are packaging a standalone dedicated server. This example assumes Steam client context.

    # Copy steam_appid.txt (if it exists, otherwise user creates it)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/steam_appid.txt")
        add_custom_command(TARGET SteamworksMinimalServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/steam_appid.txt"
            $<TARGET_FILE_DIR:SteamworksMinimalServer>)
    else()
        message(WARNING "Server: steam_appid.txt not found in ${CMAKE_CURRENT_SOURCE_DIR}. Please create it in your build output directory.")
    endif()

elseif(UNIX AND NOT APPLE)
    # For Linux, link libsteam_api.so
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/server")

    target_link_directories(SteamworksMinimalServer PRIVATE ${STEAMWORKS_SDK_PATH}/redistributable_bin/linux64)
    target_link_libraries(SteamworksMinimalServer PRIVATE steam_api) # CMake finds libsteam_api.so
    target_link_libraries(SteamworksMinimalServer PRIVATE pthread dl)

    # Copy .so and steam_appid.txt post-build
    add_custom_command(TARGET SteamworksMinimalServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${STEAMWORKS_SDK_PATH}/redistributable_bin/linux64/libsteam_api.so"
        $<TARGET_FILE_DIR:SteamworksMinimalServer>)
    # Linux servers might also need steamclient.so. Similar to Windows, this usually comes from
    # the Steam client runtime or specific server packages.
    # You might need to copy ${STEAMWORKS_SDK_PATH}/linux64/steamclient.so as well for dedicated servers.

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/steam_appid.txt")
        add_custom_command(TARGET SteamworksMinimalServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/steam_appid.txt"
            $<TARGET_FILE_DIR:SteamworksMinimalServer>)
    else()
        message(WARNING "Server: steam_appid.txt not found in ${CMAKE_CURRENT_SOURCE_DIR}. Please create it in your build output directory.")
    endif()
else()
    message(WARNING "Platform not fully configured for Steamworks GameServer linking (only Win64 and Linux64 are explicitly set up).")
endif()

set_target_properties(SteamworksMinimalServer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/server"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/server"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/server"
)