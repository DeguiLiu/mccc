cmake_minimum_required(VERSION 3.14)
project(mccc VERSION 0.2.1 LANGUAGES CXX)

# --- Core: header-only interface library ---
add_library(mccc INTERFACE)
target_include_directories(mccc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(mccc INTERFACE cxx_std_17)

# --- Extras: optional components (header-only + data_token.cpp) ---
add_library(mccc_extras
    extras/data_token.cpp)
target_include_directories(mccc_extras PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extras>)
target_link_libraries(mccc_extras PUBLIC mccc)
target_compile_features(mccc_extras PUBLIC cxx_std_17)

# --- Examples ---
option(MCCC_BUILD_EXAMPLES "Build examples" ON)
if(MCCC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Tests ---
option(MCCC_BUILD_TESTS "Build tests" ON)
if(MCCC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Install ---
include(CMakePackageConfigHelpers)
install(TARGETS mccc EXPORT mcccTargets)
install(DIRECTORY include/mccc DESTINATION include)
install(EXPORT mcccTargets
    FILE mcccConfig.cmake
    NAMESPACE mccc::
    DESTINATION lib/cmake/mccc)
