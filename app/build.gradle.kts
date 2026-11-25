plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Release signing comes from the environment: .github/workflows/release.yml decodes the keystore
// from repository secrets into KEYSTORE_FILE and exports the passwords. A local build without them
// still works; the release APK is then simply unsigned.
val releaseKeystore = System.getenv("KEYSTORE_FILE")
val releaseStorePassword = System.getenv("KEYSTORE_PASSWORD")
val releaseKeyAlias = System.getenv("KEY_ALIAS")
val releaseKeyPassword = System.getenv("KEY_PASSWORD")
val releaseSigningReady = !releaseKeystore.isNullOrBlank() && !releaseStorePassword.isNullOrBlank() &&
    !releaseKeyAlias.isNullOrBlank() && !releaseKeyPassword.isNullOrBlank()

// The release workflow passes the version taken from its tag. versionCode defaults to the value in
// this file; the workflow passes the commit count so every release can be installed over the last.
val versionCodeOverride = (project.findProperty("versionCode") as String?)?.toIntOrNull()
val versionNameOverride = project.findProperty("versionName") as String?

android {
    namespace = "org.sapidroid.tts"
    compileSdk = 34

    defaultConfig {
        applicationId = "org.sapidroid.tts"
        minSdk = 26
        targetSdk = 34
        versionCode = versionCodeOverride ?: 13
        versionName = versionNameOverride ?: "0.10.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
            if (project.hasProperty("emulatorTests")) abiFilters += "x86_64"
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    signingConfigs {
        if (releaseSigningReady) {
            create("release") {
                storeFile = file(releaseKeystore!!)
                storePassword = releaseStorePassword
                keyAlias = releaseKeyAlias
                keyPassword = releaseKeyPassword
                enableV1Signing = false
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            if (releaseSigningReady) signingConfig = signingConfigs.getByName("release")
        }
        debug {
        }
    }

    // The release workflow asks for one APK per ABI plus a universal one (-PabiSplits); ordinary
    // builds keep the single APK that CI and `assembleDebug` expect.
    if (project.hasProperty("abiSplits")) {
        splits {
            abi {
                isEnable = true
                reset()
                include("arm64-v8a", "armeabi-v7a")
                isUniversalApk = true
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    ndkVersion = "28.0.13004108"

    buildFeatures {
        buildConfig = true
        aidl = true
    }
}

dependencies {
    implementation("androidx.core:core:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    testImplementation("junit:junit:4.13.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
}
