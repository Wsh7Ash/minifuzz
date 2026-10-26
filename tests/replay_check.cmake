# Runs `minifuzz --replay` on every crash file found by e2e_finds_crash.
# Fails unless at least one crash file exists and every one of them reproduces.
# Invoked as: cmake -DMINIFUZZ_BIN=... -DCRASH_DIR=... -P replay_check.cmake

file(GLOB crashes "${CRASH_DIR}/*.bin")
list(LENGTH crashes crash_count)

if(crash_count EQUAL 0)
  message(FATAL_ERROR "no crash files in ${CRASH_DIR} (e2e_finds_crash must save one)")
endif()

foreach(crash IN LISTS crashes)
  execute_process(
    COMMAND "${MINIFUZZ_BIN}" --replay "${crash}"
    RESULT_VARIABLE replay_result
    OUTPUT_VARIABLE replay_output
    ERROR_VARIABLE replay_error
  )
  if(NOT replay_result EQUAL 1)
    message(FATAL_ERROR "replay of ${crash} exited ${replay_result}: ${replay_output}${replay_error}")
  endif()
  if(NOT replay_output MATCHES "CRASH")
    message(FATAL_ERROR "replay of ${crash} did not report a crash: ${replay_output}")
  endif()
  message(STATUS "reproduced ${crash}: ${replay_output}")
endforeach()

message(STATUS "reproduced ${crash_count} crash file(s)")
