set(CMAKE_BUILD_TYPE RELEASE CACHE STRING "")
set(CLANG_ENABLE_BOOTSTRAP ON CACHE BOOL "")

set(LLVM_ENABLE_PROJECTS "clang;lld" CACHE STRING "")
set(LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx;libcxxabi;libunwind" CACHE STRING "")

set(LLVM_TARGETS_TO_BUILD Native CACHE STRING "")
set(BOOTSTRAP_LLVM_BUILD_INSTRUMENTED ON CACHE BOOL "")
set(CLANG_BOOTSTRAP_TARGETS
  generate-profdata
  stage2
  stage2-distribution
  stage2-install-distribution
  stage2-install-distribution-toolchain
  stage2-check-all
  stage2-check-llvm
  stage2-check-clang
  stage2-test-suite CACHE STRING "")

if(PGO_INSTRUMENT_LTO)
  set(BOOTSTRAP_LLVM_ENABLE_LTO ${PGO_INSTRUMENT_LTO} CACHE BOOL "")
  set(BOOTSTRAP_BOOTSTRAP_LLVM_ENABLE_LTO ${PGO_INSTRUMENT_LTO} CACHE BOOL "")
endif()

if(PGO_BUILD_CONFIGURATION)
  set(EXTRA_ARGS -DPGO_BUILD_CONFIGURATION=${PGO_BUILD_CONFIGURATION})
endif()

set(CLANG_BOOTSTRAP_CMAKE_ARGS
  ${EXTRA_ARGS}
  -C ${CMAKE_CURRENT_LIST_DIR}/PGO-stage2-instrumented.cmake
  CACHE STRING "")
