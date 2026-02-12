/**
 * @file bench_utils.hpp
 * @brief Benchmark utilities: CPU affinity binding for stable measurements
 *
 * Binding benchmark threads to specific CPU cores eliminates:
 * - Core migration overhead
 * - Cache cold-start after migration
 * - Scheduling jitter from other processes
 *
 * Usage:
 *   bench::pin_thread_to_core(0);  // pin current thread to core 0
 */

#ifndef BENCH_UTILS_HPP
#define BENCH_UTILS_HPP

#include <cstdint>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace bench {

/**
 * @brief Pin current thread to a specific CPU core.
 * @param core_id Target core (0-based)
 * @return true if successful, false if unsupported or failed
 */
inline bool pin_thread_to_core(uint32_t core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    (void)core_id;
    return false;
#endif
}

/**
 * @brief Log CPU affinity status.
 * @param label Description for logging
 * @param core_id Target core
 */
template <typename LogFn>
inline void pin_and_log(uint32_t core_id, LogFn log_fn) {
    if (pin_thread_to_core(core_id)) {
        log_fn(core_id, true);
    } else {
        log_fn(core_id, false);
    }
}

}  // namespace bench

#endif  // BENCH_UTILS_HPP
