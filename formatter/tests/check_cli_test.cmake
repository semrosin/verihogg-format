cmake_minimum_required(VERSION 3.20)

# Exercise the executable so source-loader errors cannot bypass file checking.
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef test_id)
set(test_dir "${TEST_ROOT}/${test_id}")
file(MAKE_DIRECTORY "${test_dir}")
set(dirty "${test_dir}/dirty.sv")
set(clean "${test_dir}/clean.sv")
set(missing "${test_dir}/missing.sv")
file(WRITE "${dirty}" "module m;endmodule\n")
file(WRITE "${clean}" "module m;\nendmodule\n")
file(WRITE "${test_dir}/stdin.sv" "module stdin_module;endmodule\n")
# Avoid inheriting a developer's formatting configuration.
file(WRITE "${test_dir}/config.yaml" "version: \"0.1.0\"\nindentation_spaces: 2\n")

function(check_cli expected_status expected_dirty)
  execute_process(
    COMMAND "${FORMATTER}" --check --config "${test_dir}/config.yaml" ${ARGN}
    WORKING_DIRECTORY "${test_dir}"
    INPUT_FILE "${test_dir}/stdin.sv"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 10)
  if(NOT "${status}" STREQUAL "${expected_status}")
    message(FATAL_ERROR "Expected exit ${expected_status}, got ${status}: ${errors}")
  endif()
  if(NOT "${output}" STREQUAL "")
    message(FATAL_ERROR "--check wrote to stdout: ${output}")
  endif()
  string(FIND "${errors}" "Needs formatting: ${dirty}" dirty_position)
  if(expected_dirty AND dirty_position EQUAL -1)
    message(FATAL_ERROR "Valid dirty file was not checked: ${errors}")
  elseif(NOT expected_dirty AND NOT dirty_position EQUAL -1)
    message(FATAL_ERROR "Unexpected dirty-file diagnostic: ${errors}")
  endif()
  set(last_errors "${errors}" PARENT_SCOPE)
endfunction()

foreach(bad_input IN ITEMS "${missing}" "${test_dir}/absent/another.sv")
  check_cli(1 TRUE "${bad_input}" "${dirty}")
  string(FIND "${last_errors}" "Error:" error_position)
  if(error_position EQUAL -1)
    message(FATAL_ERROR "Missing source-loader error: ${last_errors}")
  endif()
  check_cli(1 TRUE "${dirty}" "${bad_input}")
  check_cli(1 FALSE "${clean}" "${bad_input}")
  check_cli(1 FALSE "${bad_input}")
endforeach()
check_cli(1 FALSE "${test_dir}/unmatched-*.sv")
check_cli(0 FALSE "${clean}")
check_cli(1 TRUE "${dirty}")
check_cli(1 FALSE)
file(READ "${dirty}" dirty_content)
file(READ "${clean}" clean_content)
if(NOT "${dirty_content}" STREQUAL "module m;endmodule\n" OR
   NOT "${clean_content}" STREQUAL "module m;\nendmodule\n")
  message(FATAL_ERROR "--check modified an input file")
endif()
file(REMOVE_RECURSE "${test_dir}")
