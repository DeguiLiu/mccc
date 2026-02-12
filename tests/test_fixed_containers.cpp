/**
 * @file test_fixed_containers.cpp
 * @brief Unit tests for FixedString and FixedVector.
 */

#include <catch2/catch_test_macros.hpp>
#include <mccc/mccc.hpp>

using namespace mccc;

// ============================================================================
// FixedString Tests
// ============================================================================

TEST_CASE("FixedString default constructor", "[FixedString]") {
  FixedString<32> s;
  REQUIRE(s.size() == 0U);
  REQUIRE(s.empty());
  REQUIRE(s.c_str()[0] == '\0');
}

TEST_CASE("FixedString from string literal", "[FixedString]") {
  FixedString<32> s("hello");
  REQUIRE(s.size() == 5U);
  REQUIRE_FALSE(s.empty());
  REQUIRE(std::string(s.c_str()) == "hello");
}

TEST_CASE("FixedString truncation from C string", "[FixedString]") {
  FixedString<5> s(TruncateToCapacity, "hello world");
  REQUIRE(s.size() == 5U);
  REQUIRE(std::string(s.c_str()) == "hello");
}

TEST_CASE("FixedString truncation from std::string", "[FixedString]") {
  std::string long_str = "this is a very long string";
  FixedString<10> s(TruncateToCapacity, long_str);
  REQUIRE(s.size() == 10U);
  REQUIRE(std::string(s.c_str()) == "this is a ");
}

TEST_CASE("FixedString truncation with count", "[FixedString]") {
  FixedString<32> s(TruncateToCapacity, "hello world", 5U);
  REQUIRE(s.size() == 5U);
  REQUIRE(std::string(s.c_str()) == "hello");
}

TEST_CASE("FixedString null C string", "[FixedString]") {
  FixedString<32> s(TruncateToCapacity, static_cast<const char*>(nullptr));
  REQUIRE(s.size() == 0U);
  REQUIRE(s.empty());
}

TEST_CASE("FixedString equality", "[FixedString]") {
  FixedString<32> a("hello");
  FixedString<32> b("hello");
  FixedString<32> c("world");
  FixedString<64> d("hello");

  REQUIRE(a == b);
  REQUIRE(a != c);
  REQUIRE(a == d);
  REQUIRE(a == "hello");
}

TEST_CASE("FixedString assign from literal", "[FixedString]") {
  FixedString<32> s;
  s = "test";
  REQUIRE(s.size() == 4U);
  REQUIRE(std::string(s.c_str()) == "test");
}

TEST_CASE("FixedString assign truncating", "[FixedString]") {
  FixedString<5> s;
  s.assign(TruncateToCapacity, "hello world");
  REQUIRE(s.size() == 5U);
  REQUIRE(std::string(s.c_str()) == "hello");
}

TEST_CASE("FixedString clear", "[FixedString]") {
  FixedString<32> s("hello");
  s.clear();
  REQUIRE(s.size() == 0U);
  REQUIRE(s.empty());
}

TEST_CASE("FixedString capacity", "[FixedString]") {
  REQUIRE(FixedString<16>::capacity() == 16U);
  REQUIRE(FixedString<64>::capacity() == 64U);
}

TEST_CASE("FixedString exact capacity", "[FixedString]") {
  FixedString<5> s("hello");
  REQUIRE(s.size() == 5U);
  REQUIRE(std::string(s.c_str()) == "hello");
}

// ============================================================================
// FixedVector Tests
// ============================================================================

TEST_CASE("FixedVector default constructor", "[FixedVector]") {
  FixedVector<int, 8> v;
  REQUIRE(v.size() == 0U);
  REQUIRE(v.empty());
  REQUIRE_FALSE(v.full());
  REQUIRE(v.capacity() == 8U);
}

TEST_CASE("FixedVector push_back and access", "[FixedVector]") {
  FixedVector<int, 4> v;
  REQUIRE(v.push_back(10));
  REQUIRE(v.push_back(20));
  REQUIRE(v.push_back(30));

  REQUIRE(v.size() == 3U);
  REQUIRE(v[0] == 10);
  REQUIRE(v[1] == 20);
  REQUIRE(v[2] == 30);
  REQUIRE(v.front() == 10);
  REQUIRE(v.back() == 30);
}

TEST_CASE("FixedVector full boundary", "[FixedVector]") {
  FixedVector<int, 2> v;
  REQUIRE(v.push_back(1));
  REQUIRE(v.push_back(2));
  REQUIRE(v.full());
  REQUIRE_FALSE(v.push_back(3));
  REQUIRE(v.size() == 2U);
}

TEST_CASE("FixedVector pop_back", "[FixedVector]") {
  FixedVector<int, 4> v;
  v.push_back(10);
  v.push_back(20);

  REQUIRE(v.pop_back());
  REQUIRE(v.size() == 1U);
  REQUIRE(v.back() == 10);

  REQUIRE(v.pop_back());
  REQUIRE(v.empty());
  REQUIRE_FALSE(v.pop_back());
}

TEST_CASE("FixedVector erase_unordered", "[FixedVector]") {
  FixedVector<int, 8> v;
  v.push_back(10);
  v.push_back(20);
  v.push_back(30);

  REQUIRE(v.erase_unordered(0));
  REQUIRE(v.size() == 2U);
  REQUIRE(v[0] == 30);
  REQUIRE(v[1] == 20);

  REQUIRE_FALSE(v.erase_unordered(5));
}

TEST_CASE("FixedVector clear", "[FixedVector]") {
  FixedVector<int, 4> v;
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);

  v.clear();
  REQUIRE(v.empty());
  REQUIRE(v.size() == 0U);
}

TEST_CASE("FixedVector copy constructor", "[FixedVector]") {
  FixedVector<int, 4> v1;
  v1.push_back(10);
  v1.push_back(20);

  FixedVector<int, 4> v2(v1);
  REQUIRE(v2.size() == 2U);
  REQUIRE(v2[0] == 10);
  REQUIRE(v2[1] == 20);
}

TEST_CASE("FixedVector move constructor", "[FixedVector]") {
  FixedVector<int, 4> v1;
  v1.push_back(10);
  v1.push_back(20);

  FixedVector<int, 4> v2(std::move(v1));
  REQUIRE(v2.size() == 2U);
  REQUIRE(v2[0] == 10);
  REQUIRE(v1.empty());
}

TEST_CASE("FixedVector iterators", "[FixedVector]") {
  FixedVector<int, 4> v;
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);

  int sum = 0;
  for (auto it = v.begin(); it != v.end(); ++it) {
    sum += *it;
  }
  REQUIRE(sum == 6);
}

TEST_CASE("FixedVector non-trivial type", "[FixedVector]") {
  FixedVector<std::string, 4> v;
  REQUIRE(v.emplace_back("hello"));
  REQUIRE(v.emplace_back("world"));
  REQUIRE(v.size() == 2U);
  REQUIRE(v[0] == "hello");
  REQUIRE(v[1] == "world");

  v.clear();
  REQUIRE(v.empty());
}
