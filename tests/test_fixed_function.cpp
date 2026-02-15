#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>
#include <memory>

TEST_CASE("FixedFunction default is empty", "[FixedFunction]") {
  mccc::FixedFunction<void()> fn;
  REQUIRE_FALSE(static_cast<bool>(fn));
}

TEST_CASE("FixedFunction nullptr construct", "[FixedFunction]") {
  mccc::FixedFunction<void()> fn(nullptr);
  REQUIRE_FALSE(static_cast<bool>(fn));
}

TEST_CASE("FixedFunction lambda invoke", "[FixedFunction]") {
  int called = 0;
  mccc::FixedFunction<void()> fn([&called]() noexcept { ++called; });
  REQUIRE(static_cast<bool>(fn));
  fn();
  REQUIRE(called == 1);
}

TEST_CASE("FixedFunction with return value", "[FixedFunction]") {
  mccc::FixedFunction<int(int, int)> fn([](int a, int b) noexcept { return a + b; });
  REQUIRE(fn(3, 4) == 7);
}

TEST_CASE("FixedFunction with captures", "[FixedFunction]") {
  int x = 10;
  int y = 20;
  mccc::FixedFunction<int(), 48U> fn([x, y]() noexcept { return x + y; });
  REQUIRE(fn() == 30);
}

TEST_CASE("FixedFunction move construct", "[FixedFunction]") {
  int value = 0;
  mccc::FixedFunction<void()> fn([&value]() noexcept { value = 42; });

  mccc::FixedFunction<void()> moved(std::move(fn));
  REQUIRE_FALSE(static_cast<bool>(fn));  // NOLINT
  REQUIRE(static_cast<bool>(moved));
  moved();
  REQUIRE(value == 42);
}

TEST_CASE("FixedFunction move assign", "[FixedFunction]") {
  int a = 0;
  int b = 0;
  mccc::FixedFunction<void()> fn1([&a]() noexcept { a = 1; });
  mccc::FixedFunction<void()> fn2([&b]() noexcept { b = 2; });

  fn1 = std::move(fn2);
  REQUIRE_FALSE(static_cast<bool>(fn2));  // NOLINT
  fn1();
  REQUIRE(a == 0);  // fn1's original lambda was destroyed
  REQUIRE(b == 2);
}

TEST_CASE("FixedFunction nullptr assign clears", "[FixedFunction]") {
  mccc::FixedFunction<void()> fn([]() noexcept {});
  REQUIRE(static_cast<bool>(fn));
  fn = nullptr;
  REQUIRE_FALSE(static_cast<bool>(fn));
}

TEST_CASE("FixedFunction destructor calls captured dtor", "[FixedFunction]") {
  static int dtor_count = 0;
  struct Counter {
    Counter() noexcept = default;
    Counter(const Counter&) noexcept = default;
    Counter(Counter&&) noexcept = default;
    Counter& operator=(const Counter&) noexcept = default;
    Counter& operator=(Counter&&) noexcept = default;
    ~Counter() { ++dtor_count; }
  };

  dtor_count = 0;
  {
    Counter c;
    mccc::FixedFunction<void(), 48U> fn([c]() noexcept { (void)c; });
    // c copied into lambda
  }
  // fn destroyed -> lambda destroyed -> Counter copy destroyed
  // + original c destroyed
  REQUIRE(dtor_count >= 2);
}

TEST_CASE("FixedFunction with weak_ptr capture (SubscribeSafe sim)", "[FixedFunction]") {
  auto shared = std::make_shared<int>(42);
  std::weak_ptr<int> weak = shared;

  mccc::FixedFunction<int(), 64U> fn([weak]() noexcept -> int {
    auto locked = weak.lock();
    return locked ? *locked : -1;
  });

  REQUIRE(fn() == 42);
  shared.reset();
  REQUIRE(fn() == -1);
}

TEST_CASE("FixedFunction empty invoke returns default", "[FixedFunction]") {
  mccc::FixedFunction<int()> fn;
  REQUIRE(fn() == 0);  // R{} for empty

  mccc::FixedFunction<void()> vfn;
  vfn();  // should not crash
}

TEST_CASE("FixedFunction function pointer", "[FixedFunction]") {
  static int global_val = 0;
  auto fptr = +[](int v) noexcept { global_val = v; };
  mccc::FixedFunction<void(int)> fn(fptr);
  fn(99);
  REQUIRE(global_val == 99);
}
