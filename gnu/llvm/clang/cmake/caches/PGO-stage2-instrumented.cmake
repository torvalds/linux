set(CLANG_ENABLE_BOOTSTRAP ON CACHE BOOL "")
set(CLANG_BOOTSTRAP_TARGETS
  distribution
  install-distribution
  install-distribution-toolchain
  check-all
  check-llvm
  check-clang
  test-suite CACHE STRING "")

if(PGO_BUILD_CONFIGURATION)
  include(${PGO_BUILD_CONFIGURATION})
  set(CLANG_BOOTSTRAP_CMAKE_ARGS
    -C ${PGO_BUILD_CONFIGURATION}
    CACHE STRING "")
else()
  include(${CMAKE_CURRENT_LIST_DIR}/PGO-stage2.cmake)

  set(CLANG_BOOTSTRAP_CMAKE_ARGS
    -C ${CMAKE_CURRENT_LIST_DIR}/PGO-stage2.cmake
    CACHE STRING "")
endif()
