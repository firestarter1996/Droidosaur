# How to build Nanosaur

## The easy way: build.py (automated build script)

`build.py` can produce a game executable from a fresh clone of the repo in a single command. It will work on macOS, Windows and Linux, provided that your system has Python 3, CMake, and an adequate C++ compiler.

```
git clone --recurse-submodules https://github.com/jorio/Nanosaur
cd Nanosaur
python3 build.py
```

If you want to build the game **manually** instead, the rest of this document describes how to do just that on each of the big 3 desktop operating systems.

## How to build the game manually on macOS

1. Install the prerequisites:
    - Xcode (preferably the latest version)
    - [CMake](https://formulae.brew.sh/formula/cmake) 3.21+ (installing via Homebrew is recommended)
1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/jorio/Nanosaur
    cd Nanosaur
    ```
1. Download [SDL3-3.2.8.dmg](https://libsdl.org/release/SDL3-3.2.8.dmg), open it, then browse to SDL3.xcframework/macos-arm64_x86_64. In that folder, copy **SDL3.framework** to the game's **extern** folder.
1. Prep the Xcode project:
    ```
    cmake -G Xcode -S . -B build
    ```
1. Now you can open `build/Nanosaur.xcodeproj` in Xcode, or you can just go ahead and build the game:
    ```
    cmake --build build --config RelWithDebInfo
    ```
1. The game gets built in `build/RelWithDebInfo/Nanosaur.app`. Enjoy!

## How to build the game manually on Windows

1. Install the prerequisites:
    - Visual Studio 2022 with the C++ toolchain
    - [CMake](https://cmake.org/download/) 3.21+
1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/jorio/Nanosaur
    cd Nanosaur
    ```
1. Download [SDL3-devel-3.2.8-VC.zip](https://libsdl.org/release/SDL3-devel-3.2.8-VC.zip), extract it, and copy **SDL3-3.2.8** to the **extern** folder. Rename **SDL3-3.2.8** to just **SDL3**.
1. Prep the Visual Studio solution:
    ```
    cmake -G "Visual Studio 17 2022" -A x64 -S . -B build
    ```
1. Now you can open `build/Nanosaur.sln` in Visual Studio, or you can just go ahead and build the game:
    ```
    cmake --build build --config Release
    ```
1. The game gets built in `build/Release/Nanosaur.exe`. Enjoy!

## How to build the game manually on Linux et al.

1. Install the prerequisites from your package manager:
    - Any C++20 compiler
    - CMake 3.21+
    - SDL3 development library (e.g. "libsdl3-dev" on Ubuntu, "sdl3" on Arch, "SDL3-devel" on Fedora)
    - OpenGL development libraries (e.g. "libgl1-mesa-dev" on Ubuntu)
1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/jorio/Nanosaur
    cd Nanosaur
    ```
1. Build the game:
    ```
    cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ```
    If you'd like to enable runtime sanitizers, append `-DSANITIZE=1` to the **first** `cmake` call above.
1. The game gets built in `build/Nanosaur`. Enjoy!

## How to build the Android APK

### Prerequisites

- [Android Studio](https://developer.android.com/studio) or the standalone Android SDK/NDK
- Android NDK r27 or later
- CMake 3.22+
- JDK 17
- Gradle 8.9+

### Steps

1. Clone the repo **recursively**:
    ```
    git clone --recurse-submodules https://github.com/LachlanBWWright/Nanosaur-android
    cd Nanosaur-android
    ```

1. Clone SDL3 into the `extern/SDL` directory:
    ```
    git clone --depth 1 https://github.com/libsdl-org/SDL.git extern/SDL
    ```

1. Copy the SDL Java bridge files into the Android project:
    ```
    mkdir -p android/app/src/main/java/org/libsdl/app
    cp extern/SDL/android-project/app/src/main/java/org/libsdl/app/*.java \
       android/app/src/main/java/org/libsdl/app/
    ```

1. Install the Android SDK, NDK, and CMake via `sdkmanager`:
    ```
    sdkmanager "cmake;3.22.1"
    sdkmanager "ndk;27.2.12479018"
    ```

1. Generate the Gradle wrapper (if not already present):
    ```
    cd android
    gradle wrapper --gradle-version=8.9
    ```

1. Build the debug APK:
    ```
    cd android
    ./gradlew assembleDebug
    ```

1. The APK is at `android/app/build/outputs/apk/debug/app-debug.apk`. Install it with:
    ```
    adb install android/app/build/outputs/apk/debug/app-debug.apk
    ```

### CI/CD

The repository includes a GitHub Actions workflow (`.github/workflows/android-build.yml`) that automatically builds and uploads the debug APK as an artifact on every push and pull request.

