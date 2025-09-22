if (LLVM_INCLUDE_UTILS AND LLVM_INCLUDE_TOOLS)
  add_custom_command(
    OUTPUT ${LLVM_TOOLS_BINARY_DIR}/llvm-locstats
    DEPENDS ${LLVM_MAIN_SRC_DIR}/utils/llvm-locstats/llvm-locstats.py
    DEPENDS llvm-dwarfdump
    COMMAND ${CMAKE_COMMAND} -E copy ${LLVM_MAIN_SRC_DIR}/utils/llvm-locstats/llvm-locstats.py ${LLVM_TOOLS_BINARY_DIR}/llvm-locstats
    COMMENT "Copying llvm-locstats into ${LLVM_TOOLS_BINARY_DIR}"
    )
  add_custom_target(llvm-locstats ALL
    DEPENDS ${LLVM_TOOLS_BINARY_DIR}/llvm-locstats
    )
  if (NOT LLVM_BUILD_TOOLS)
    set_target_properties(llvm-locstats PROPERTIES EXCLUDE_FROM_ALL ON)
  endif()
  set_target_properties(llvm-locstats PROPERTIES FOLDER "LLVM/Tools")
endif()
