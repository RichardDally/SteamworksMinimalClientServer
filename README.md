# SteamworksMinimalClientServer

A minimal, simple C++17 client-server example using Steamworks.

## Prerequisites

1.  **Steamworks SDK**: Download from [https://partner.steamgames.com/downloads/list](https://partner.steamgames.com/downloads/list)
2.  **CMake**: Version 3.15 or higher.
3.  **C++17 Compiler**: (e.g., GCC, Clang, MSVC)
4.  **Steam Client**: Must be running and logged in for the client to initialize Steamworks and for the server to validate users.

## Setup

1.  **Place Steamworks SDK**:
    * Download and extract the Steamworks SDK.
    * When running CMake, you will need to set the `STEAMWORKS_SDK_PATH` variable to the root directory of the SDK (e.g., `path/to/steamworks_sdk`).

2.  **spdlog**:
    * This project uses CMake's `WorkspaceContent` to download and build `spdlog` v1.12.0 automatically. If you prefer to provide it manually, you can modify the root `CMakeLists.txt`.

3.  **Steam AppID**:
    * You need to create a file named `steam_appid.txt` in the directory where the client and server executables will run (typically your build output directory, e.g., `build/client/` and `build/server/`).
    * This file should contain only the Steam Application ID you want to use. For development and testing purposes, you can often use `480` (Spacewar).
    * Example `steam_appid.txt`:
        ```
        480
        ```

## Building

1.  Create a build directory:
    ```bash
    mkdir build
    cd build
    ```

2.  Run CMake (adjust paths and generator as needed):

    * **Linux:**
        ```bash
        cmake .. -DSTEAMWORKS_SDK_PATH=/path/to/your/steamworks_sdk
        ```
    * **Windows (Visual Studio):**
        ```bash
        cmake .. -G "Visual Studio 17 2022" -A x64 -DSTEAMWORKS_SDK_PATH="C:/path/to/your/steamworks_sdk"
        ```
        (Replace `"Visual Studio 17 2022"` with your VS version if different. Ensure you use x64 for Steamworks.)

3.  Build the project:
    * **Linux:**
        ```bash
        make -j$(nproc)
        ```
    * **Windows (Visual Studio):**
        ```bash
        cmake --build . --config Release
        ```

## Running

1.  **Ensure `steam_appid.txt` is in place** in `build/client` and `build/server` (or wherever your executables are).
2.  **Copy Steamworks Redistributables**:
    * **Linux**: Copy `steamworks_sdk/redistributable_bin/linux64/libsteam_api.so` to your executable directories (e.g., `build/client/` and `build/server/`).
    * **Windows**: Copy `steamworks_sdk/redistributable_bin/win64/steam_api64.dll` to your executable directories.
3.  **Run the Server**:
    ```bash
    cd build/server
    ./SteamworksMinimalServer
    ```
4.  **Run the Client**:
    ```bash
    cd build/client
    ./SteamworksMinimalClient
    ```

The server will log when it starts listening. The client will attempt to connect. If successful, they will exchange a simple message, and authentication will be attempted. Check the console logs for output.

## Notes

* This example uses `ISteamNetworkingSockets` for communication and `BeginAuthSession`/`EndAuthSession` for ticket-based authentication. It does not use the older P2P networking or P2P auth.
* The server listens on virtual port 1234 by default. The client connects to `127.0.0.1` on this port. These can be changed in the source code.
* Ensure the Steam client is running and you are logged in.
* This is a minimal example. Robust error handling, message framing, and application logic are beyond its scope.