cmake_minimum_required(VERSION 3.15)
project(SteamworksMinimalClientServer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Steamworks SDK Path ---
# User must provide this path, e.g., via -DSTEAMWORKS_SDK_PATH=/path/to/sdk
if(NOT DEFINED STEAMWORKS_SDK_PATH)
    if(DEFINED ENV{STEAMWORKS_SDK_PATH})
        set(STEAMWORKS_SDK_PATH "$ENV{STEAMWORKS_SDK_PATH}")
        message(STATUS "Using STEAMWORKS_SDK_PATH from environment: ${STEAMWORKS_SDK_PATH}")
    else()
        message(FATAL_ERROR "STEAMWORKS_SDK_PATH is not defined. Please set it, e.g., -DSTEAMWORKS_SDK_PATH=/path/to/your/steamworks_sdk")
    endif()
endif()

if(NOT EXISTS "${STEAMWORKS_SDK_PATH}/public/steam/steam_api.h")
    message(FATAL_ERROR "Steamworks SDK not found or incomplete at STEAMWORKS_SDK_PATH: ${STEAMWORKS_SDK_PATH}. Check the path and SDK contents.")
endif()

message(STATUS "Found Steamworks SDK at: ${STEAMWORKS_SDK_PATH}")

# --- spdlog (using FetchContent) ---
include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0 # Specify the version
)
FetchContent_MakeAvailable(spdlog)

# --- Global include directories ---
# This allows #include <spdlog/spdlog.h>
include_directories(${spdlog_SOURCE_DIR}/include)

# --- Subdirectories for client and server ---
add_subdirectory(client)
add_subdirectory(server)

# --- Copy steam_appid.txt placeholder (optional, user should create their own) ---
# This is more of a reminder; actual app ID file needs to be in the runtime directory.
# You could add a custom command to copy a template if desired, but it's often
# better for the user to manage this specific to their AppID.

# --- Platform-specific settings ---
if(WIN32)
    # Common Windows settings if any
    add_definitions(-DWIN32)
elseif(UNIX AND NOT APPLE)
    # Common Linux settings if any
    add_definitions(-DLINUX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread") # For std::thread
endif()