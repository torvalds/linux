set(LLVM_LINK_COMPONENTS
  Support
  )

add_llvm_fuzzer(lldb-commandinterpreter-fuzzer
  EXCLUDE_FROM_ALL
  lldb-commandinterpreter-fuzzer.cpp
  )

if(TARGET lldb-commandinterpreter-fuzzer)
  target_include_directories(lldb-commandinterpreter-fuzzer PRIVATE ..)
  target_link_libraries(lldb-commandinterpreter-fuzzer
    PRIVATE
    liblldb
    )

  # A directory in the build directory is created to hold the fuzzer's
  # artifacts as a pre-build command for the command interpreter's executable
  # target. When the fuzzer exits the input artifact that caused it to exit
  # will be written to this directory.

  add_custom_command(TARGET lldb-commandinterpreter-fuzzer PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/fuzzer-artifacts/commandinterpreter-artifacts
    )

  add_custom_target(fuzz-lldb-commandinterpreter
    COMMENT "Running the LLDB command interpreter fuzzer..."
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/fuzzer-artifacts/commandinterpreter-artifacts
    COMMAND  $<TARGET_FILE:lldb-commandinterpreter-fuzzer> -dict=${CMAKE_CURRENT_SOURCE_DIR}/inputdictionary.txt  -only_ascii=1 -artifact_prefix=commandinterpreter-
    USES_TERMINAL
    )
  set_target_properties(fuzz-lldb-commandinterpreter PROPERTIES FOLDER "LLDB/Fuzzer")  
endif()
