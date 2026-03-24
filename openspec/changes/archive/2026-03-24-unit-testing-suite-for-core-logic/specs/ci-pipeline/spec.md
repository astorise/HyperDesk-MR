## ADDED Requirements

### Requirement: CI pipeline includes a dedicated unit-test job that runs GTest suites via CTest
The GitHub Actions workflow SHALL include a `test` job that checks out the code, installs the required native build tools (CMake and a C++ compiler), configures the project with CMake (fetching Google Test via `FetchContent`), builds all test targets, and executes `ctest --output-on-failure` to run the GTest suites. The job MUST report pass/fail status so that any regression fails the CI run.

#### Scenario: Unit-test CI job runs on push and all tests pass
- **WHEN** a commit is pushed and the `test` job executes `ctest --output-on-failure`
- **THEN** all registered GTest suites complete with zero failures and the job exits with code 0

#### Scenario: Unit-test CI job fails when a regression is introduced
- **WHEN** a code change causes a GTest assertion to fail
- **THEN** `ctest` exits with a non-zero code, the CI job is marked as failed, and the failure output is visible in the workflow log
