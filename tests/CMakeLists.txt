include(FetchContent)
set(CATCH2_VERSION v3.5.2)
FetchContent_Declare(
  Catch2
  URL https://ghfast.top/https://github.com/catchorg/Catch2/archive/refs/tags/${CATCH2_VERSION}.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(Catch2)

include(Catch)

find_package(Threads REQUIRED)

add_executable(mccc_tests
    test_fixed_containers.cpp
    test_ring_buffer.cpp
    test_priority.cpp
    test_backpressure.cpp
    test_subscribe.cpp
    test_multithread.cpp
    test_stability.cpp
    test_edge_cases.cpp
    test_copy_move.cpp
    test_fixed_function.cpp
    test_visitor_dispatch.cpp
    test_static_component.cpp)
target_link_libraries(mccc_tests Catch2::Catch2WithMain mccc Threads::Threads)
catch_discover_tests(mccc_tests)

# SPSC mode test target (excludes multi-producer tests)
add_executable(mccc_tests_spsc
    test_fixed_containers.cpp
    test_ring_buffer.cpp
    test_priority.cpp
    test_backpressure.cpp
    test_subscribe.cpp
    test_stability.cpp
    test_edge_cases.cpp
    test_copy_move.cpp
    test_fixed_function.cpp
    test_visitor_dispatch.cpp
    test_static_component.cpp)
target_link_libraries(mccc_tests_spsc Catch2::Catch2WithMain mccc Threads::Threads)
target_compile_definitions(mccc_tests_spsc PRIVATE MCCC_SINGLE_PRODUCER=1)
catch_discover_tests(mccc_tests_spsc)
