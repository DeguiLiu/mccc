# CI/CD Setup Guide

This document describes the continuous integration and continuous deployment (CI/CD) setup for the MCCC project.

## Overview

The MCCC project uses GitHub Actions for automated testing, building, and code quality checks. The CI/CD pipeline ensures code quality and compatibility across multiple platforms and configurations.

## Workflows

### 1. CI Workflow (`.github/workflows/ci.yml`)

The main CI workflow runs on every push to `master`/`main` branches and on pull requests.

#### Build and Test Matrix

Tests the project across multiple platforms and build types:

| OS | Compiler | Build Types |
|----|----------|-------------|
| Ubuntu | GCC | Debug, Release |
| macOS | Clang | Debug, Release |

**Total configurations**: 4 (2 OS × 2 build types)

#### Build with Compile-Time Options

Tests various compile-time configurations:

1. **SPSC Mode**: Single-producer single-consumer mode
   - Flag: `-DMCCC_SINGLE_PRODUCER=1`
   - Purpose: Test wait-free fast path optimization

2. **Different Queue Depth**: Test with custom queue size
   - Flag: `-DMCCC_QUEUE_DEPTH=4096`
   - Purpose: Verify configurability

3. **Custom Limits**: Test with increased type/callback limits
   - Flags: `-DMCCC_MAX_MESSAGE_TYPES=16 -DMCCC_MAX_CALLBACKS_PER_TYPE=32`
   - Purpose: Verify scalability

#### Sanitizer Tests

Runs tests with various sanitizers to detect memory and threading issues:

- **Address Sanitizer**: Detects memory errors (buffer overflows, use-after-free, etc.)
- **Thread Sanitizer**: Detects data races and threading issues
- **Undefined Behavior Sanitizer**: Detects undefined behavior

#### Code Formatting Check

Verifies that all code follows the project's style guide:
- Uses `clang-format` to check C++ code formatting
- Fails if any files don't match the `.clang-format` configuration

### 2. Code Coverage Workflow (`.github/workflows/coverage.yml`)

Measures test coverage and reports to Codecov:

- Runs on Ubuntu with Debug build
- Uses gcov/lcov for coverage analysis
- Excludes system headers, tests, and examples from coverage
- Uploads results to Codecov for visualization

### 3. Documentation Workflow (`.github/workflows/docs.yml`)

Checks documentation quality:

- **Markdown Link Checker**: Verifies all links in markdown files are valid
- **Example Reference Check**: Ensures all examples are documented
- **TODO/FIXME Scanner**: Lists remaining work items

## Configuration Files

### `.clang-format`

Defines the C++ code style for the project:
- Based on Google style with customizations
- 2-space indentation
- 120 character line limit
- Pointer alignment: left
- C++17 standard

To format your code:
```bash
find include examples tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i
```

### `.editorconfig`

Provides editor-agnostic configuration:
- Charset: UTF-8
- Line endings: LF (Unix-style)
- Indentation: 2 spaces for C++, CMake, YAML
- Trim trailing whitespace

### `.gitignore`

Comprehensive ignore patterns for:
- Build directories
- IDE files (.vscode, .idea, .vs)
- Compiled artifacts (*.o, *.so, *.exe)
- Test results
- Coverage reports
- Package manager files

## GitHub Templates

### Pull Request Template

Located at `.github/pull_request_template.md`, includes:
- Description section
- Type of change checklist
- Testing checklist
- Code quality checklist

### Issue Templates

Located in `.github/ISSUE_TEMPLATE/`:

1. **Bug Report** (`bug_report.md`): For reporting bugs with environment details
2. **Feature Request** (`feature_request.md`): For suggesting new features

## Badge Status

The README includes badges showing:
- CI build status
- Code coverage status
- License

Example badges:
```markdown
[![CI](https://github.com/DeguiLiu/mccc/actions/workflows/ci.yml/badge.svg)](https://github.com/DeguiLiu/mccc/actions/workflows/ci.yml)
[![Code Coverage](https://github.com/DeguiLiu/mccc/actions/workflows/coverage.yml/badge.svg)](https://github.com/DeguiLiu/mccc/actions/workflows/coverage.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
```

## Running CI Locally

### Prerequisites

Install required tools:
```bash
# Ubuntu/Debian
sudo apt-get install cmake clang-format lcov

# macOS
brew install cmake clang-format lcov
```

### Running Tests Locally

```bash
# Configure
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DMCCC_BUILD_TESTS=ON

# Build
cmake --build . -j

# Test
ctest --output-on-failure
```

### Running with Sanitizers

```bash
# Address Sanitizer
CC=clang CXX=clang++ cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
cmake --build .
ctest --output-on-failure

# Thread Sanitizer
CC=clang CXX=clang++ cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
cmake --build .
ctest --output-on-failure
```

### Checking Code Format

```bash
# Check formatting (dry-run)
find include examples tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format --dry-run --Werror

# Apply formatting
find include examples tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i
```

### Generating Coverage Report

```bash
# Configure with coverage flags
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMCCC_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage"

# Build and test
cmake --build .
ctest

# Generate report
lcov --directory . --capture --output-file coverage.info
lcov --remove coverage.info '/usr/*' '*/tests/*' '*/examples/*' --output-file coverage.info
lcov --list coverage.info
```

## Troubleshooting

### Build Failures

1. **Missing dependencies**: Ensure CMake and C++17 compiler are installed
2. **Test failures**: Check if tests pass locally before pushing
3. **Format failures**: Run clang-format before committing

### Workflow Failures

1. Check the Actions tab in GitHub for detailed logs
2. Look for red X marks indicating failed jobs
3. Click on failed jobs to see error messages
4. Fix issues locally and push again

### Coverage Issues

- Coverage may be low initially; aim to add tests for new code
- Exclude test files and examples from coverage calculations
- Focus on testing core functionality

## Best Practices

1. **Always run tests locally** before pushing
2. **Format code** with clang-format before committing
3. **Keep PRs focused** on a single feature or fix
4. **Write tests** for new functionality
5. **Update documentation** when changing APIs
6. **Check CI status** before merging PRs

## Future Enhancements

Potential improvements to the CI/CD pipeline:

- [ ] Add static analysis tools (cppcheck, clang-tidy)
- [ ] Add performance regression testing
- [ ] Add automatic documentation generation (Doxygen)
- [ ] Add automatic release creation
- [ ] Add dependency scanning for security vulnerabilities
- [ ] Add benchmark comparison between commits

## Resources

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [CMake Documentation](https://cmake.org/documentation/)
- [Catch2 Documentation](https://github.com/catchorg/Catch2)
- [clang-format Documentation](https://clang.llvm.org/docs/ClangFormat.html)
- [Codecov Documentation](https://docs.codecov.com/)
