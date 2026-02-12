/**
 * @file log_macro.hpp
 * @brief Compile-time logging macros for zero-overhead logging control
 *
 * Features:
 * - Compile-time log level filtering (zero runtime overhead)
 * - Multiple log levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
 * - Automatic file, line, and function information
 * - Thread-safe output
 * - Minimal performance impact when disabled
 *
 * Usage:
 *   Define LOG_LEVEL before including this header:
 *   #define LOG_LEVEL LOG_LEVEL_INFO
 *   #include "log_macro.hpp"
 *
 *   Then use:
 *   LOG_INFO("Server started on port %d", port);
 *   LOG_ERROR("Failed to connect: %s", error_msg);
 */

#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <chrono>
#include <mutex>

// ============================================================================
// Log Level Definitions
// ============================================================================

#define LOG_LEVEL_TRACE 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_WARN 3
#define LOG_LEVEL_ERROR 4
#define LOG_LEVEL_FATAL 5
#define LOG_LEVEL_OFF 6

// Default log level if not specified
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

// ============================================================================
// Internal Implementation
// ============================================================================

namespace log_internal {

// Thread-safe logging mutex
inline std::mutex& get_log_mutex() {
  static std::mutex mtx;
  return mtx;
}

// Get current timestamp in microseconds
inline uint64_t get_timestamp_us() noexcept {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
}

// Format and print log message (thread-safe)
template <typename... Args>
inline void log_print(const char* level, const char* file, int32_t line, const char* func, const char* fmt,
                      Args&&... args) noexcept {
  std::lock_guard<std::mutex> lock(get_log_mutex());

  uint64_t timestamp = get_timestamp_us();

  // Print: [timestamp] [LEVEL] [file:line:func] message
  std::fprintf(stderr, "[%" PRIu64 "] [%s] [%s:%d:%s] ", timestamp, level, file, line, func);
  std::fprintf(stderr, fmt, std::forward<Args>(args)...);
  std::fprintf(stderr, "\n");
  std::fflush(stderr);
}

// No-args version (for simple messages without formatting)
inline void log_print(const char* level, const char* file, int32_t line, const char* func, const char* msg) noexcept {
  std::lock_guard<std::mutex> lock(get_log_mutex());

  uint64_t timestamp = get_timestamp_us();

  std::fprintf(stderr, "[%" PRIu64 "] [%s] [%s:%d:%s] %s\n", timestamp, level, file, line, func, msg);
  std::fflush(stderr);
}

}  // namespace log_internal

// ============================================================================
// Public Logging Macros
// ============================================================================

// TRACE level (most verbose)
#if LOG_LEVEL <= LOG_LEVEL_TRACE
#define LOG_TRACE(fmt, ...) log_internal::log_print("TRACE", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
#define LOG_TRACE(fmt, ...) ((void)0)
#endif

// DEBUG level
#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(fmt, ...) log_internal::log_print("DEBUG", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif

// INFO level
#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(fmt, ...) log_internal::log_print("INFO", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...) ((void)0)
#endif

// WARN level
#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(fmt, ...) log_internal::log_print("WARN", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
#define LOG_WARN(fmt, ...) ((void)0)
#endif

// ERROR level
#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(fmt, ...) log_internal::log_print("ERROR", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
#define LOG_ERROR(fmt, ...) ((void)0)
#endif

// FATAL level (always enabled unless LOG_LEVEL_OFF)
#if LOG_LEVEL <= LOG_LEVEL_FATAL
#define LOG_FATAL(fmt, ...)                                                             \
  do {                                                                                  \
    log_internal::log_print("FATAL", __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
    std::abort();                                                                       \
  } while (0)
#else
#define LOG_FATAL(fmt, ...) ((void)0)
#endif

// ============================================================================
// Conditional Logging (only log if condition is true)
// ============================================================================

#if LOG_LEVEL <= LOG_LEVEL_TRACE
#define LOG_TRACE_IF(cond, fmt, ...) \
  do {                               \
    if (cond)                        \
      LOG_TRACE(fmt, ##__VA_ARGS__); \
  } while (0)
#else
#define LOG_TRACE_IF(cond, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG_IF(cond, fmt, ...) \
  do {                               \
    if (cond)                        \
      LOG_DEBUG(fmt, ##__VA_ARGS__); \
  } while (0)
#else
#define LOG_DEBUG_IF(cond, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO_IF(cond, fmt, ...) \
  do {                              \
    if (cond)                       \
      LOG_INFO(fmt, ##__VA_ARGS__); \
  } while (0)
#else
#define LOG_INFO_IF(cond, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN_IF(cond, fmt, ...) \
  do {                              \
    if (cond)                       \
      LOG_WARN(fmt, ##__VA_ARGS__); \
  } while (0)
#else
#define LOG_WARN_IF(cond, fmt, ...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR_IF(cond, fmt, ...) \
  do {                               \
    if (cond)                        \
      LOG_ERROR(fmt, ##__VA_ARGS__); \
  } while (0)
#else
#define LOG_ERROR_IF(cond, fmt, ...) ((void)0)
#endif

// ============================================================================
// Performance Measurement Macros
// ============================================================================

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_PERF_START(name) auto __log_perf_start_##name = std::chrono::high_resolution_clock::now()

#define LOG_PERF_END(name)                                                                                       \
  do {                                                                                                           \
    auto __log_perf_end = std::chrono::high_resolution_clock::now();                                             \
    auto __log_perf_duration =                                                                                   \
        std::chrono::duration_cast<std::chrono::microseconds>(__log_perf_end - __log_perf_start_##name).count(); \
    LOG_DEBUG("Performance [%s]: %ld us", #name, __log_perf_duration);                                           \
  } while (0)
#else
#define LOG_PERF_START(name) ((void)0)
#define LOG_PERF_END(name) ((void)0)
#endif

// ============================================================================
// Assert Macros (only in debug builds)
// ============================================================================

#ifndef NDEBUG
#define LOG_ASSERT(cond, fmt, ...)                                                                          \
  do {                                                                                                      \
    if (!(cond)) {                                                                                          \
      log_internal::log_print("ASSERT", __FILE__, __LINE__, __func__, "Assertion failed: " #cond " - " fmt, \
                              ##__VA_ARGS__);                                                               \
      std::abort();                                                                                         \
    }                                                                                                       \
  } while (0)
#else
#define LOG_ASSERT(cond, fmt, ...) ((void)0)
#endif

// ============================================================================
// Usage Examples (in comments)
// ============================================================================

/*
Example 1: Basic logging
    LOG_INFO("Server started on port %d", 8080);
    LOG_ERROR("Connection failed: %s", strerror(errno));

Example 2: Conditional logging
    LOG_DEBUG_IF(verbose_mode, "Processing item %d", item_id);

Example 3: Performance measurement
    LOG_PERF_START(database_query);
    // ... do work ...
    LOG_PERF_END(database_query);

Example 4: Compile-time control
    // In release builds, set LOG_LEVEL to ERROR or OFF
    // #define LOG_LEVEL LOG_LEVEL_ERROR
    // All DEBUG and INFO logs will be completely removed at compile time

Example 5: Assertions
    LOG_ASSERT(ptr != nullptr, "Pointer must not be null");
*/
