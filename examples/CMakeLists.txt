add_executable(mccc_simple_demo simple_demo.cpp)
target_link_libraries(mccc_simple_demo mccc_extras pthread)

add_executable(mccc_priority_demo priority_demo.cpp)
target_link_libraries(mccc_priority_demo mccc_extras pthread)

add_executable(mccc_benchmark benchmark.cpp)
target_link_libraries(mccc_benchmark mccc_extras pthread)
target_compile_options(mccc_benchmark PRIVATE -O3 -march=native)

add_executable(mccc_hsm_demo hsm_demo.cpp)
target_link_libraries(mccc_hsm_demo mccc_extras pthread)
