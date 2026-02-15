/**
 * @file example_types.hpp
 * @brief Example message types for MCCC demos and benchmarks.
 *
 * Users should define their own message types and PayloadVariant.
 * This file is NOT part of the MCCC library - it's only used by examples.
 */

#ifndef MCCC_EXAMPLE_TYPES_HPP_
#define MCCC_EXAMPLE_TYPES_HPP_

#include <mccc/component.hpp>

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace example {

/**
 * Motion data payload (small object, direct value passing).
 */
struct MotionData {
  float x;
  float y;
  float z;
  float velocity;

  MotionData() noexcept : x(0.0f), y(0.0f), z(0.0f), velocity(0.0f) {}
  MotionData(float x_, float y_, float z_, float vel_) noexcept : x(x_), y(y_), z(z_), velocity(vel_) {}
};

/**
 * Camera frame payload (large object, internal shared pointer for zero-copy).
 */
struct CameraFrame {
  int32_t width;
  int32_t height;
  mccc::FixedString<16> format;
  std::shared_ptr<std::vector<uint8_t>> raw_data;

  CameraFrame() noexcept : width(0), height(0), format("RGB"), raw_data(nullptr) {}
  CameraFrame(int32_t w, int32_t h, const char* fmt) noexcept
      : width(w), height(h), format(mccc::TruncateToCapacity, fmt), raw_data(nullptr) {}

  void AllocateBuffer(uint32_t buffer_size) {
    raw_data = std::make_shared<std::vector<uint8_t>>(buffer_size);
  }
};

/**
 * System log payload.
 */
struct SystemLog {
  int32_t level;
  mccc::FixedString<64> content;

  SystemLog() noexcept : level(0) {}
  SystemLog(int32_t lvl, const char* msg) noexcept : level(lvl), content(mccc::TruncateToCapacity, msg) {}
};

/**
 * Example payload variant containing all demo message types.
 */
using ExamplePayload = std::variant<MotionData, CameraFrame, SystemLog>;

/**
 * Convenience type aliases for examples.
 */
using ExampleBus = mccc::AsyncBus<ExamplePayload>;
using ExampleComponent = mccc::Component<ExamplePayload>;
using ExampleEnvelope = mccc::MessageEnvelope<ExamplePayload>;

}  // namespace example

#endif  // MCCC_EXAMPLE_TYPES_HPP_
