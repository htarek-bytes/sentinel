cmake_minimum_required(VERSION 3.16)
project(sentinel CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sentinel src/main.cpp)
add_executable(worker examples/worker.cpp)

enable_testing()

set(EXPECT bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_status.sh)

# normal exits: sentinel hands back whatever the child returned
add_test(NAME exit_zero    COMMAND ${EXPECT} 0 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker> ok)
add_test(NAME exit_nonzero COMMAND ${EXPECT} 3 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker> fail)

# death by signal is a different path, 128 + 6 for SIGABRT
add_test(NAME killed_by_signal COMMAND ${EXPECT} 134 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker> crash)

# our own error paths
add_test(NAME exec_not_found COMMAND ${EXPECT} 127 $<TARGET_FILE:sentinel> ./no-such-program-here)
add_test(NAME missing_args   COMMAND ${EXPECT} 1   $<TARGET_FILE:sentinel>)

add_test(NAME signal_forwarding
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/signal_forwarding.sh
                 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker>)

add_test(NAME sigkill_escalation
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/sigkill_escalation.sh
                 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker>)

add_test(NAME signal_mask
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/signal_mask.sh
                 $<TARGET_FILE:sentinel>)

add_test(NAME orphan_reaping
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/orphan_reaping.sh
                 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker>)

add_test(NAME restart
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/restart.sh
                 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker>)

add_test(NAME backoff
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/backoff.sh
                 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker>)

add_test(NAME counters
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/counters.sh
                 $<TARGET_FILE:sentinel> $<TARGET_FILE:worker>)

# these two block waiting on a child, so cap them rather than let a
# regression hang the runner for the default 1500 seconds
set_tests_properties(signal_forwarding PROPERTIES TIMEOUT 30)
set_tests_properties(sigkill_escalation PROPERTIES TIMEOUT 30)
set_tests_properties(orphan_reaping PROPERTIES TIMEOUT 30)
set_tests_properties(restart PROPERTIES TIMEOUT 30)
set_tests_properties(backoff PROPERTIES TIMEOUT 40)
set_tests_properties(counters PROPERTIES TIMEOUT 40)
