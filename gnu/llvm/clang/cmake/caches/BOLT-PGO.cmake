set(LLVM_ENABLE_PROJECTS "bolt;clang;lld" CACHE STRING "")

set(CLANG_BOOTSTRAP_TARGETS
  stage2-clang-bolt
  stage2-distribution
  stage2-install-distribution
  CACHE STRING "")
set(BOOTSTRAP_CLANG_BOOTSTRAP_TARGETS
  clang-bolt
  distribution
  install-distribution
  CACHE STRING "")

set(PGO_BUILD_CONFIGURATION
  ${CMAKE_CURRENT_LIST_DIR}/BOLT.cmake
  CACHE STRING "")
include(${CMAKE_CURRENT_LIST_DIR}/PGO.cmake)
