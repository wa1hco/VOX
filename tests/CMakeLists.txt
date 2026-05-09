cmake_minimum_required(VERSION 3.16)

# Functional tests
add_executable(test_vox_basic functional/test_vox_basic.c)
target_link_libraries(test_vox_basic PRIVATE vox_core)
target_link_libraries(test_vox_basic PRIVATE m)
add_test(NAME vox_basic COMMAND test_vox_basic)

# Dongle wire-protocol round-trip + edge cases
add_executable(test_dongle_proto functional/test_dongle_proto.c)
target_link_libraries(test_dongle_proto PRIVATE vox_dongle_proto)
add_test(NAME dongle_proto COMMAND test_dongle_proto)

# Regression tests
add_executable(test_vox_regression regression/test_vox_regression.c)
target_link_libraries(test_vox_regression PRIVATE vox_core)
add_test(NAME vox_regression COMMAND test_vox_regression)
