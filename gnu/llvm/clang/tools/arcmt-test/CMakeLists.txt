set(LLVM_LINK_COMPONENTS
  support
  )

add_clang_executable(arcmt-test
  arcmt-test.cpp
  )

clang_target_link_libraries(arcmt-test
  PRIVATE
  clangARCMigrate
  clangBasic
  clangFrontend
  clangLex
  clangSerialization
  )
