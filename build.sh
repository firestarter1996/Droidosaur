#!/bin/bash
cd /home/Justice/droidosaur/android
export ANDROID_HOME=/home/Justice/android-sdk ANDROID_SDK_ROOT=/home/Justice/android-sdk JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
echo "sdk.dir=/home/Justice/android-sdk" > local.properties
chmod +x gradlew
./gradlew assembleDebug --no-daemon --stacktrace -Dorg.gradle.jvmargs=-Xmx3g 2>&1
echo "BUILD_EXIT=$?"
ls -la app/build/outputs/apk/debug/ 2>/dev/null
