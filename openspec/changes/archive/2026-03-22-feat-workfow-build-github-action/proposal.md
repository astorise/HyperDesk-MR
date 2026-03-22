# Change Proposal: GitHub Actions CI/CD Pipeline & Installation Guide

## Description
Set up a GitHub Actions workflow to automatically build the C++/NDK application into an APK file on every code change. Additionally, update the `README.md` file to include detailed instructions for sideloading the APK onto the Meta Quest 3.

## Motivation
Currently, testing the project requires a complete local Android/C++ development environment, which creates friction for testing and collaboration. Continuous Integration (CI) will ensure the code always successfully compiles for the `arm64-v8a` architecture and will provide a ready-to-deploy artifact. Clear installation instructions will allow non-developer contributors or testers to easily install the app.

## User Goals
- Download an automatically compiled `.apk` artifact directly from the GitHub "Actions" tab.
- Understand how to sideload this APK onto a Meta Quest 3 using ADB or the Meta Quest Developer Hub (MQDH).

## Acceptance Criteria
- [ ] The GitHub Actions workflow triggers on `push` and `pull_request` events targeting the `main` branch.
- [ ] The workflow properly configures the Android SDK/NDK and CMake, builds the project, and exposes the `.apk` as a downloadable artifact.
- [ ] The `README.md` file contains a new "Installation (Sideloading)" section with exact ADB commands and MQDH instructions.