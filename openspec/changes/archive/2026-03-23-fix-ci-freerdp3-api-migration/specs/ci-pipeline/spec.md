## MODIFIED Requirements

### Requirement: GitHub Actions workflow builds the Android APK on push and pull request
The repository SHALL include a `.github/workflows/build.yml` workflow that triggers on `push` and `pull_request` events targeting the `main` branch. The workflow SHALL: check out the code; set up JDK 17; install Android NDK r29; build third-party dependencies (OpenXR loader, FreeRDP 3.x static libraries, and OpenSSL prebuilt `.so` files for arm64-v8a) from source or prebuilt into the `third_party/` directory; generate the Gradle wrapper jar; make the Gradle wrapper executable; run `./gradlew assembleDebug`; and upload the resulting APK as a build artifact using `actions/upload-artifact@v4`.

#### Scenario: Workflow runs on push to main and produces a downloadable APK artifact
- **WHEN** a commit is pushed to the `main` branch
- **THEN** the GitHub Actions `build` job completes successfully and the debug APK is available as a downloadable workflow artifact

#### Scenario: Workflow runs on pull request and validates the build
- **WHEN** a pull request targeting `main` is opened or updated
- **THEN** the `build` job executes `./gradlew assembleDebug` without error and the APK artifact is uploaded

#### Scenario: Third-party dependencies include OpenSSL shared libraries
- **WHEN** the "Build third-party dependencies" step completes
- **THEN** `third_party/openssl/libs/android.arm64-v8a/libssl_3.so` and `libcrypto_3.so` exist alongside the FreeRDP `.a` files and OpenXR `.so`
