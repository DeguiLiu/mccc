# Contributing to MCCC

Thank you for your interest in contributing to MCCC (Message-Centric Component Communication)! This document provides guidelines for contributing to the project.

## Table of Contents

- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Code Style](#code-style)
- [Building and Testing](#building-and-testing)
- [Submitting Changes](#submitting-changes)
- [Reporting Issues](#reporting-issues)

## Getting Started

1. Fork the repository on GitHub
2. Clone your fork locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/mccc.git
   cd mccc
   ```
3. Add the upstream repository:
   ```bash
   git remote add upstream https://github.com/DeguiLiu/mccc.git
   ```

## Development Setup

### Prerequisites

- CMake 3.14 or higher
- C++17 compatible compiler:
  - GCC 7+ (Linux)
  - Clang 6+ (macOS/Linux)
- clang-format (for code formatting)

### Building the Project

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DMCCC_BUILD_TESTS=ON -DMCCC_BUILD_EXAMPLES=ON
cmake --build . -j
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

Or run specific test executables:
```bash
./tests/mccc_tests
./tests/mccc_tests_spsc
```

### Running Examples

```bash
./examples/simple_demo
./examples/priority_demo
./examples/hsm_demo
./examples/mccc_benchmark
```

## Code Style

MCCC follows a consistent code style enforced by clang-format:

### Formatting Your Code

Before submitting a pull request, format your code:

```bash
find include examples tests \( -name '*.hpp' -o -name '*.cpp' \) | xargs clang-format -i
```

### Style Guidelines

- **Indentation**: 2 spaces, no tabs
- **Line Length**: Maximum 120 characters
- **Naming Conventions**:
  - Classes: PascalCase (e.g., `AsyncBus`, `MessageEnvelope`)
  - Functions: PascalCase (e.g., `ProcessBatch()`)
  - Variables: snake_case (e.g., `sender_id`, `queue_depth`)
  - Constants: UPPER_SNAKE_CASE (e.g., `MCCC_QUEUE_DEPTH`)
  - Template parameters: PascalCase (e.g., `PayloadVariant`)
- **Comments**: Use Doxygen-style comments for public APIs
- **Header Guards**: Use `#ifndef`/`#define`/`#endif` pattern

### Code Quality

- Write clear, self-documenting code
- Keep functions focused and concise
- Avoid unnecessary complexity
- Use const correctness
- Prefer stack allocation over heap allocation
- Use standard library features when appropriate

## Building and Testing

### Build Types

- **Debug**: For development and debugging
  ```bash
  cmake .. -DCMAKE_BUILD_TYPE=Debug
  ```
- **Release**: For production and benchmarking
  ```bash
  cmake .. -DCMAKE_BUILD_TYPE=Release
  ```

### Build Options

- `MCCC_BUILD_TESTS`: Build test suite (default: ON)
- `MCCC_BUILD_EXAMPLES`: Build examples (default: ON)

### Compile-Time Configuration

You can customize MCCC behavior with compile-time flags:

```bash
cmake .. -DCMAKE_CXX_FLAGS="-DMCCC_QUEUE_DEPTH=4096 -DMCCC_SINGLE_PRODUCER=1"
```

See README.md for available configuration macros.

### Testing Guidelines

1. **Add tests for new features**: Every new feature should include tests
2. **Update existing tests**: When modifying functionality, update relevant tests
3. **Run all tests**: Ensure all tests pass before submitting
4. **Test different configurations**: Test with different compile-time options when relevant

### Writing Tests

MCCC uses Catch2 for testing. Example test structure:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>

TEST_CASE("Your feature description", "[tag]") {
  // Arrange
  // ... setup code ...
  
  // Act
  // ... code under test ...
  
  // Assert
  REQUIRE(condition);
  CHECK(another_condition);
}
```

## Submitting Changes

### Pull Request Process

1. **Create a feature branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes**:
   - Write clean, well-documented code
   - Follow the code style guidelines
   - Add tests for new functionality
   - Update documentation if needed

3. **Commit your changes**:
   ```bash
   git add .
   git commit -m "Brief description of your changes"
   ```
   
   Commit message format:
   - Use present tense ("Add feature" not "Added feature")
   - Use imperative mood ("Move cursor to..." not "Moves cursor to...")
   - Keep first line under 72 characters
   - Reference issues and PRs where appropriate

4. **Push to your fork**:
   ```bash
   git push origin feature/your-feature-name
   ```

5. **Create a Pull Request**:
   - Go to the GitHub repository
   - Click "New Pull Request"
   - Select your branch
   - Fill in the PR template with:
     - Description of changes
     - Related issues
     - Testing performed
     - Screenshots (if applicable)

6. **Address review feedback**:
   - Respond to reviewer comments
   - Make requested changes
   - Push updates to your branch

### Pull Request Checklist

Before submitting, ensure:

- [ ] Code follows the project's code style
- [ ] All tests pass locally
- [ ] New tests added for new functionality
- [ ] Documentation updated (if needed)
- [ ] No compiler warnings
- [ ] Commit messages are clear and descriptive
- [ ] Code is formatted with clang-format

## Reporting Issues

### Bug Reports

When reporting a bug, include:

1. **Description**: Clear description of the issue
2. **Environment**:
   - OS and version
   - Compiler and version
   - MCCC version/commit
3. **Steps to Reproduce**: Minimal code example
4. **Expected Behavior**: What should happen
5. **Actual Behavior**: What actually happens
6. **Additional Context**: Logs, screenshots, etc.

### Feature Requests

When requesting a feature:

1. **Description**: Clear description of the feature
2. **Use Case**: Why is this feature needed?
3. **Proposed Solution**: How should it work?
4. **Alternatives**: What alternatives have you considered?

## Communication

- **Issues**: Use GitHub Issues for bug reports and feature requests
- **Pull Requests**: Use PR comments for code review discussions
- **Questions**: Open a GitHub Discussion or Issue

## Code of Conduct

- Be respectful and inclusive
- Welcome newcomers
- Focus on constructive feedback
- Assume good intentions

## License

By contributing to MCCC, you agree that your contributions will be licensed under the MIT License.

## Thank You!

Your contributions make MCCC better for everyone. Thank you for taking the time to contribute!
