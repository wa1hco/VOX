cmake_minimum_required(VERSION 3.16)

# Functional tests
add_executable(test_vox_basic functional/test_vox_basic.c)
target_link_libraries(test_vox_basic PRIVATE vox_core)
target_link_libraries(test_vox_basic PRIVATE m)
add_test(NAME vox_basic COMMAND test_vox_basic)

# Regression tests
add_executable(test_vox_regression regression/test_vox_regression.c)
target_link_libraries(test_vox_regression PRIVATE vox_core)
add_test(NAME vox_regression COMMAND test_vox_regression)
