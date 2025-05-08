cmake_minimum_required(VERSION 3.15)
project(SteamworksMinimalClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Locate Steamworks SDK ---
# STEAMWORKS_SDK_PATH is defined in the parent CMakeLists.txt
include_directories(${STEAMWORKS_SDK_PATH}/public)

# --- Executable ---
add_executable(SteamworksMinimalClient
    client_main.cpp
    client.cpp
    client.h
)

# --- Link Libraries ---
target_link_libraries(SteamworksMinimalClient PRIVATE spdlog::spdlog)

# Platform-specific linking for Steamworks
if(WIN32)
    # For Windows, link steam_api64.lib (assuming 64-bit build)
    # The actual .dll must be next to the executable at runtime.
    target_link_directories(SteamworksMinimalClient PRIVATE ${STEAMWORKS_SDK_PATH}/redistributable_bin/win64) # For MSVC to find .lib
    target_link_libraries(SteamworksMinimalClient PRIVATE steam_api64)
    # If using MinGW, you might need to create an import lib or directly link if possible.
    # For MSVC, it's typical to copy the DLL post-build.
    add_custom_command(TARGET SteamworksMinimalClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${STEAMWORKS_SDK_PATH}/redistributable_bin/win64/steam_api64.dll"
        $<TARGET_FILE_DIR:SteamworksMinimalClient>)
    # Copy steam_appid.txt (if it exists, otherwise user creates it)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/steam_appid.txt")
        add_custom_command(TARGET SteamworksMinimalClient POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/steam_appid.txt"
            $<TARGET_FILE_DIR:SteamworksMinimalClient>)
    else()
        message(WARNING "Client: steam_appid.txt not found in ${CMAKE_CURRENT_SOURCE_DIR}. Please create it in your build output directory.")
    endif()

elseif(UNIX AND NOT APPLE)
    # For Linux, link libsteam_api.so
    # The .so must be next to the executable or in LD_LIBRARY_PATH at runtime.
    # CMake RPATH handling can help find the .so if it's copied to the build dir.
    # Ensure you're building for the correct architecture (e.g., linux64).
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE) # Add link directories to RPATH
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/client") # Where libsteam_api.so might be copied

    target_link_directories(SteamworksMinimalClient PRIVATE ${STEAMWORKS_SDK_PATH}/redistributable_bin/linux64) # Path for linker
    target_link_libraries(SteamworksMinimalClient PRIVATE steam_api) #CMake will find libsteam_api.so
    target_link_libraries(SteamworksMinimalClient PRIVATE pthread dl) # Common Linux dependencies

    # Copy .so and steam_appid.txt post-build
     add_custom_command(TARGET SteamworksMinimalClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${STEAMWORKS_SDK_PATH}/redistributable_bin/linux64/libsteam_api.so"
        $<TARGET_FILE_DIR:SteamworksMinimalClient>)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/steam_appid.txt")
        add_custom_command(TARGET SteamworksMinimalClient POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/steam_appid.txt"
            $<TARGET_FILE_DIR:SteamworksMinimalClient>)
    else()
        message(WARNING "Client: steam_appid.txt not found in ${CMAKE_CURRENT_SOURCE_DIR}. Please create it in your build output directory.")
    endif()
else()
    message(WARNING "Platform not fully configured for Steamworks linking (only Win64 and Linux64 are explicitly set up).")
endif()

set_target_properties(SteamworksMinimalClient PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client"
)