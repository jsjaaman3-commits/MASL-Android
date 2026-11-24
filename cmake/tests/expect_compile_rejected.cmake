if(NOT DEFINED MASLC OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "MASLC, SOURCE, and OUTPUT must be provided")
endif()
file(REMOVE "${OUTPUT}")
execute_process(
  COMMAND "${MASLC}" compile "${SOURCE}" -o "${OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(result EQUAL 0)
  message(FATAL_ERROR "Compilation unexpectedly succeeded for unsupported hardware source.\n${stdout}\n${stderr}")
endif()
set(combined "${stdout}\n${stderr}")
if(NOT combined MATCHES "MASL-B6023")
  message(FATAL_ERROR "Compilation failed for an unexpected reason. Expected MASL-B6023.\n${combined}")
endif()
if(EXISTS "${OUTPUT}")
  message(FATAL_ERROR "Compiler left an output file after rejecting the source.")
endif()
message(STATUS "Unsupported hardware source was explicitly rejected with MASL-B6023.")
