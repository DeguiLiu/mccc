# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.2.0] - 2026-11-15

### Added
- Extended error callback with user context (`SetErrorCallback(ErrorCallbackEx, void*)`)
- Deferred unsubscribe queue for callbacks that unsubscribe themselves
- Consumer progress cursor and `HasConsumerProgress()` for external watchdogs
- `Component` can bind to a standalone `AsyncBus` instance
- Compile-time assertions for variant size, nothrow payload moves, lock-free
  atomics, queue depth, and deferred queue capacity
- Safety regression tests for lifecycle, reentrancy, deferred unsubscribe,
  error context, and throwing callbacks

### Changed
- `Publish`, `Subscribe`, and `Unsubscribe` are now `[[nodiscard]]`
- Consumer progress is committed per message to avoid a wedged queue after a
  callback failure
- Queue overflow, callback-table overflow, and message overflows are always
  reported, including in `BARE_METAL` and `NO_STATS` modes
- Callback exceptions are converted to `PROCESSING_ERROR` diagnostics when
  exceptions are enabled, instead of terminating the consumer
- `BARE_METAL` and `NO_STATS` now require
  `MCCC_ALLOW_UNSAFE_PERFORMANCE_MODES=1`
- `AsyncBus` constructor is public so independent bus instances are possible

### Fixed
- Deadlock when a callback subscribed or unsubscribed from inside dispatch
- Silent message loss in performance modes
- Missing error reporting when the callback table was full
- Use-after-free risk when `Component` subscription storage was exhausted

### Migration
- Define `MCCC_ALLOW_UNSAFE_PERFORMANCE_MODES=1` if a project intentionally
  uses `BARE_METAL` or `NO_STATS`. Default builds keep full admission checks.

## [Unreleased]

### Added
- GitHub Actions CI/CD workflows
  - Multi-platform build and test (Ubuntu, macOS)
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

[Unreleased]: https://github.com/DeguiLiu/mccc/compare/v2.2.0...HEAD
[2.2.0]: https://github.com/DeguiLiu/mccc/compare/v2.1.0...v2.2.0
[1.0.0]: https://github.com/DeguiLiu/mccc/releases/tag/v1.0.0
