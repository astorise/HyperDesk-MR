# Technical Specifications: CI/CD & Installation Docs

## CI/CD Pipeline Architecture (GitHub Actions)
- **Environment:** `ubuntu-latest`.
- **Triggers:** `push` and `pull_request` on the `main` branch.
- **System Dependencies:**
  - Java JDK 17 (via `actions/setup-java`).
  - Android SDK & NDK (using the default tools provided by the GitHub Ubuntu runner or `android-actions/setup-android`).
  - CMake and Ninja (for compiling the native C++ code).
- **Build Process:** Execution of the Gradle wrapper (`chmod +x gradlew` followed by `./gradlew assembleDebug`).
- **Artifacts:** Use the `actions/upload-artifact@v4` action to upload the generated APK (typically located at `app/build/outputs/apk/debug/`) so it can be downloaded by users.

## README Documentation Updates
A new section named `## 📦 Installation (Sideloading)` must be appended to the `README.md`.
It must cover the following:
1. **Prerequisite:** Instructions to enable Developer Mode on the Meta Quest 3 headset.
2. **Command Line Method (ADB):**
   - Provide the exact command: `adb install -r <path_to_hyperdesk-mr.apk>`
3. **GUI Method (Meta Quest Developer Hub - MQDH):**
   - Briefly explain how to drag and drop the APK into the MQDH Device Manager interface.