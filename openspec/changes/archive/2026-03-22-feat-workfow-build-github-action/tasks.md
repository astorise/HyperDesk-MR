# Tasks

- [x] Task 1: Create the `.github/workflows/` directory at the root of the repository if it does not exist.
- [x] Task 2: Create a `build.yml` file inside the workflows directory.
- [x] Task 3: Configure the workflow trigger (`on: [push, pull_request]` for `main`).
- [x] Task 4: Define the `build` job: check out the code, set up JDK 17, make the Gradle wrapper executable (`chmod +x gradlew`), and run `./gradlew assembleDebug`.
- [x] Task 5: Add the `actions/upload-artifact@v4` step to upload the resulting `.apk` file from the build outputs.
- [x] Task 6: Open the existing `README.md` file.
- [x] Task 7: Append a new section titled "Installation (Sideloading)" explaining the Quest 3 Developer Mode prerequisite.
- [x] Task 8: Add the ADB CLI installation instructions to the README.
- [x] Task 9: Add the MQDH GUI installation instructions to the README.