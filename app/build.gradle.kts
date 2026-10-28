plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.example.stratumcppdemo"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.example.stratumcppdemo"
        minSdk = 24
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        // NDK & C++ Configuration
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets {
        getByName("main") {
            java.srcDir("src/main/cpp/stratum/java") // Stratum runtime + adapters
        }
    }

    buildTypes {
        release {
            optimization {
                enable = false
            }
        }
    }

    compileOptions {
        // Updated to Java 17 as requested
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation(project(":stratum-runtime"))
    implementation(libs.activity.ktx)
    implementation(libs.appcompat) // Reuses your version catalog appcompat
    implementation(libs.constraintlayout)
    implementation(libs.material)

    testImplementation(libs.junit)
    androidTestImplementation(libs.espresso.core)
    androidTestImplementation(libs.ext.junit)
}