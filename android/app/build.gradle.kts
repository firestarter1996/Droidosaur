plugins {
    id("com.android.application")
}

android {
    namespace = "io.jor.nanosaur"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.jor.nanosaur"
        minSdk = 24        // Android 7.0 – supports GLES 3.0
        targetSdk = 35
        versionCode = 1
        versionName = "1.4.5"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64", "x86")
        }

        externalNativeBuild {
            cmake {
                arguments(
                    "-DANDROID=TRUE",
                    "-DBUILD_SDL_FROM_SOURCE=ON",
                    "-DSDL_STATIC=OFF",
                    "-DANDROID_STL=c++_shared"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            assets.srcDirs("../../Data")
        }
    }
}
