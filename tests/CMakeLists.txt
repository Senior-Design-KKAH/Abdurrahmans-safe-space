cmake_minimum_required(VERSION 3.16)
project(acoustic_checks C)
enable_testing()
add_executable(acoustic_checks test_pipeline.c
    ../main/signal_processing.c ../main/mfcc.c ../main/quantization.c
    ../main/delta_codec.c ../main/payload.c)
target_include_directories(acoustic_checks PRIVATE include ../main)
target_compile_features(acoustic_checks PRIVATE c_std_11)
target_compile_options(acoustic_checks PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_link_libraries(acoustic_checks PRIVATE m)
add_test(NAME acoustic_checks COMMAND acoustic_checks)
