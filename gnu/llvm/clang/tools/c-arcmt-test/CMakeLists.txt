add_clang_executable(c-arcmt-test
  c-arcmt-test.c
  )

if (LLVM_BUILD_STATIC)
  target_link_libraries(c-arcmt-test
    PRIVATE
    libclang_static
    )
else()
  target_link_libraries(c-arcmt-test
    PRIVATE
    libclang
    )
endif()

set_target_properties(c-arcmt-test
  PROPERTIES
  LINKER_LANGUAGE CXX)
