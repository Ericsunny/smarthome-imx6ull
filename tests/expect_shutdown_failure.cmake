execute_process(COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
    MONITOR_TEST_SHUTDOWN_FAILURE=1 "${MONITOR_BINARY}" --simulate --smoke-test
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result STREQUAL "1" OR NOT output MATCHES "shutdown_output_error")
    message(FATAL_ERROR "Expected exit 1 and shutdown_output_error; result=${result}\n${output}\n${errors}")
endif()
message(STATUS "Shutdown output failure reported and process returned 1")
