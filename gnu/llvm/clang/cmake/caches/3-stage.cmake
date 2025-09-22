set(CLANG_BOOTSTRAP_TARGETS
  clang
  check-all
  check-llvm
  check-clang
  test-suite
  stage3
  stage3-clang
  stage3-check-all
  stage3-check-llvm
  stage3-check-clang
  stage3-test-suite CACHE STRING "")

set(LLVM_TARGETS_TO_BUILD Native CACHE STRING "")

include(${CMAKE_CURRENT_LIST_DIR}/3-stage-base.cmake)
