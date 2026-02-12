add_executable(mccc_simple_demo simple_demo.cpp)
target_link_libraries(mccc_simple_demo mccc_extras pthread)

add_executable(mccc_priority_demo priority_demo.cpp)
target_link_libraries(mccc_priority_demo mccc_extras pthread)

add_executable(mccc_benchmark benchmark.cpp)
target_link_libraries(mccc_benchmark mccc_extras pthread)
target_compile_options(mccc_benchmark PRIVATE -O3 -march=native)

add_executable(mccc_benchmark_spsc benchmark.cpp)
target_link_libraries(mccc_benchmark_spsc mccc_extras pthread)
target_compile_options(mccc_benchmark_spsc PRIVATE -O3 -march=native)
target_compile_definitions(mccc_benchmark_spsc PRIVATE MCCC_SINGLE_PRODUCER=1)

add_executable(mccc_benchmark_sc benchmark.cpp)
target_link_libraries(mccc_benchmark_sc mccc_extras pthread)
target_compile_options(mccc_benchmark_sc PRIVATE -O3 -march=native)
target_compile_definitions(mccc_benchmark_sc PRIVATE MCCC_SINGLE_CORE=1 MCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1)

add_executable(mccc_benchmark_spsc_sc benchmark.cpp)
target_link_libraries(mccc_benchmark_spsc_sc mccc_extras pthread)
target_compile_options(mccc_benchmark_spsc_sc PRIVATE -O3 -march=native)
target_compile_definitions(mccc_benchmark_spsc_sc PRIVATE MCCC_SINGLE_PRODUCER=1 MCCC_SINGLE_CORE=1 MCCC_I_KNOW_SINGLE_CORE_IS_UNSAFE=1)

add_executable(mccc_hsm_demo hsm_demo.cpp)
target_link_libraries(mccc_hsm_demo mccc_extras pthread)

# --- Competitive Benchmark (optional, requires external dependencies) ---
option(MCCC_BUILD_COMPETITIVE_BENCH "Build competitive benchmark (downloads eventpp, EnTT, sigslot, ZeroMQ)" OFF)
if(MCCC_BUILD_COMPETITIVE_BENCH)
    include(FetchContent)

    # eventpp (fork with optimizations)
    FetchContent_Declare(eventpp
        GIT_REPOSITORY https://github.com/DeguiLiu/eventpp.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )

    # EnTT v3.12.2 (last C++17 version)
    FetchContent_Declare(entt
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG        v3.12.2
        GIT_SHALLOW    TRUE
    )

    # sigslot v1.2.3
    FetchContent_Declare(sigslot
        GIT_REPOSITORY https://github.com/palacaze/sigslot.git
        GIT_TAG        v1.2.3
        GIT_SHALLOW    TRUE
    )

    # libzmq v4.3.5
    set(ZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(WITH_DOCS OFF CACHE BOOL "" FORCE)
    set(WITH_PERF_TOOL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libzmq
        GIT_REPOSITORY https://github.com/zeromq/libzmq.git
        GIT_TAG        v4.3.5
        GIT_SHALLOW    TRUE
    )

    # cppzmq v4.10.0 (C++ binding for libzmq)
    set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(cppzmq
        GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
        GIT_TAG        v4.10.0
        GIT_SHALLOW    TRUE
    )

    # Local dev: use -DFETCHCONTENT_SOURCE_DIR_EVENTPP=<path> to override remote download
    FetchContent_MakeAvailable(eventpp entt sigslot libzmq cppzmq)

    # MPSC competitive benchmark
    add_executable(mccc_competitive_bench competitive_benchmark.cpp)
    target_link_libraries(mccc_competitive_bench PRIVATE mccc pthread libzmq-static)
    target_include_directories(mccc_competitive_bench PRIVATE
        ${eventpp_SOURCE_DIR}/include
        ${entt_SOURCE_DIR}/src
        ${sigslot_SOURCE_DIR}/include
        ${cppzmq_SOURCE_DIR}
        ${libzmq_SOURCE_DIR}/include
    )
    target_compile_options(mccc_competitive_bench PRIVATE -O3 -march=native)

    # SPSC competitive benchmark
    add_executable(mccc_competitive_bench_spsc competitive_benchmark.cpp)
    target_link_libraries(mccc_competitive_bench_spsc PRIVATE mccc pthread libzmq-static)
    target_include_directories(mccc_competitive_bench_spsc PRIVATE
        ${eventpp_SOURCE_DIR}/include
        ${entt_SOURCE_DIR}/src
        ${sigslot_SOURCE_DIR}/include
        ${cppzmq_SOURCE_DIR}
        ${libzmq_SOURCE_DIR}/include
    )
    target_compile_options(mccc_competitive_bench_spsc PRIVATE -O3 -march=native)
    target_compile_definitions(mccc_competitive_bench_spsc PRIVATE MCCC_SINGLE_PRODUCER=1)
endif()
