#!/bin/bash
set -x
cd /home/Justice/droidosaur
sudo apt-get install -y -q openjdk-21-jdk-headless unzip >/dev/null 2>&1 && echo JDK_OK
git submodule update --init --recursive --depth 1 && echo SUBMODULES_OK
git clone -q --depth 1 -b release-3.2.8 https://github.com/libsdl-org/SDL.git extern/SDL && echo SDL_OK
mkdir -p android/app/src/main/java/org/libsdl/app && cp extern/SDL/android-project/app/src/main/java/org/libsdl/app/*.java android/app/src/main/java/org/libsdl/app/ && echo BRIDGE_OK
SDK=/home/Justice/android-sdk; mkdir -p $SDK/cmdline-tools; cd $SDK
[ -d cmdline-tools/latest ] || { curl -sSL -o ct.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip && unzip -q ct.zip -d cmdline-tools && mv cmdline-tools/cmdline-tools cmdline-tools/latest && rm ct.zip; }
export ANDROID_HOME=$SDK JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
yes | $SDK/cmdline-tools/latest/bin/sdkmanager --licenses >/dev/null 2>&1
$SDK/cmdline-tools/latest/bin/sdkmanager "platform-tools" "platforms;android-35" "build-tools;35.0.0" "cmake;3.22.1" "ndk;27.2.12479018" >/dev/null 2>&1 && echo SDK_OK
ls $SDK; echo SETUP_DONE
