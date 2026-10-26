cmake_minimum_required(VERSION 3.16)
project(minifuzz VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  set(MINIFUZZ_WARNINGS /W4 /permissive-)
else()
  set(MINIFUZZ_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Optional: build with AddressSanitizer when the compiler supports it.
option(MINIFUZZ_ASAN "Build with -fsanitize=address" OFF)
if(MINIFUZZ_ASAN)
  if(MSVC)
    add_compile_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()
endif()

add_library(minifuzz_lib STATIC
  src/fuzz.cpp
  src/target_svfc.cpp
)
target_include_directories(minifuzz_lib PUBLIC src)
target_compile_options(minifuzz_lib PRIVATE ${MINIFUZZ_WARNINGS})

add_executable(minifuzz src/main.cpp)
target_link_libraries(minifuzz PRIVATE minifuzz_lib)
target_compile_options(minifuzz PRIVATE ${MINIFUZZ_WARNINGS})

add_executable(minifuzz_tests tests/test_minifuzz.cpp)
target_link_libraries(minifuzz_tests PRIVATE minifuzz_lib)
target_compile_options(minifuzz_tests PRIVATE ${MINIFUZZ_WARNINGS})

enable_testing()
add_test(NAME unit COMMAND minifuzz_tests)

# End-to-end: a fixed seed must find the bundled bugs within the budget, and
# the saved crash must reproduce with --replay.
add_test(NAME e2e_clean_crash_dir
         COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/e2e-crashes)
set_tests_properties(e2e_clean_crash_dir PROPERTIES FIXTURES_SETUP e2e-crashes)

add_test(NAME e2e_finds_crash
         COMMAND minifuzz --seed 20261026 --iterations 200000
                 --crashes-dir ${CMAKE_BINARY_DIR}/e2e-crashes)
set_tests_properties(e2e_finds_crash PROPERTIES
                     WILL_FAIL TRUE
                     FIXTURES_REQUIRED e2e-crashes
                     DEPENDS e2e_clean_crash_dir)

add_test(NAME e2e_replay_reproduces
         COMMAND ${CMAKE_COMMAND}
                 -DMINIFUZZ_BIN=$<TARGET_FILE:minifuzz>
                 -DCRASH_DIR=${CMAKE_BINARY_DIR}/e2e-crashes
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_check.cmake)
set_tests_properties(e2e_replay_reproduces PROPERTIES
                     DEPENDS e2e_finds_crash
                     FIXTURES_REQUIRED e2e-crashes)
