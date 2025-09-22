set(LLVM_LINK_COMPONENTS
  Support
  )

add_llvm_fuzzer(lldb-target-fuzzer
  EXCLUDE_FROM_ALL
  lldb-target-fuzzer.cpp
  )

if(TARGET lldb-target-fuzzer)
  target_include_directories(lldb-target-fuzzer PRIVATE ..)
  target_link_libraries(lldb-target-fuzzer
    PRIVATE
    liblldb
    lldbFuzzerUtils
    )

  add_custom_command(TARGET lldb-target-fuzzer PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/fuzzer-artifacts/target-artifacts
    )

  add_custom_target(fuzz-lldb-target
    COMMENT "Running the LLDB target fuzzer..."
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/fuzzer-artifacts/target-artifacts
    COMMAND $<TARGET_FILE:lldb-target-fuzzer> -artifact_prefix=target-
    USES_TERMINAL
    )
  set_target_properties(fuzz-lldb-target PROPERTIES FOLDER "LLDB/Fuzzer")  
endif()
