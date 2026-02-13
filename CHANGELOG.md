# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- GitHub Actions CI/CD workflows
  - Multi-platform build and test (Ubuntu, macOS, Windows)
  - Build with various compile-time configurations
  - Sanitizer tests (address, thread, undefined behavior)
  - Code formatting checks
  - Code coverage reporting
  - Documentation checks
- Development configuration files
  - `.clang-format` for consistent code formatting
  - `.editorconfig` for editor settings
  - Enhanced `.gitignore` with comprehensive C++ patterns
- Contributing guidelines in `CONTRIBUTING.md`
- GitHub templates
  - Pull request template
  - Bug report issue template
  - Feature request issue template
- `CHANGELOG.md` for tracking project changes

### Changed
- Enhanced `.gitignore` with more comprehensive patterns for C++ development

## [1.0.0] - 2024

### Added
- Initial release of MCCC (Message-Centric Component Communication)
- Lock-free MPSC message bus
- Priority-based admission control (HIGH/MEDIUM/LOW)
- Zero heap allocation in hot path
- Header-only library with optional extras
- Comprehensive test suite (70 test cases, 203 assertions)
- Performance benchmarks and examples
- Documentation in English and Chinese

[Unreleased]: https://github.com/DeguiLiu/mccc/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/DeguiLiu/mccc/releases/tag/v1.0.0
