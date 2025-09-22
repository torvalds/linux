# Plain options configure the first build.
# BOOTSTRAP_* options configure the second build.
# BOOTSTRAP_BOOTSTRAP_* options configure the third build.
# PGO Builds have 3 stages (stage1, stage2-instrumented, stage2)
# non-PGO Builds have 2 stages (stage1, stage2)


function (set_final_stage_var name value type)
  if (LLVM_RELEASE_ENABLE_PGO)
    set(BOOTSTRAP_BOOTSTRAP_${name} ${value} CACHE ${type} "")
  else()
    set(BOOTSTRAP_${name} ${value} CACHE ${type} "")
  endif()
endfunction()

function (set_instrument_and_final_stage_var name value type)
  # This sets the varaible for the final stage in non-PGO builds and in
  # the stage2-instrumented stage for PGO builds.
  set(BOOTSTRAP_${name} ${value} CACHE ${type} "")
  if (LLVM_RELEASE_ENABLE_PGO)
    # Set the variable in the final stage for PGO builds.
    set(BOOTSTRAP_BOOTSTRAP_${name} ${value} CACHE ${type} "")
  endif()
endfunction()

# General Options:
# If you want to override any of the LLVM_RELEASE_* variables you can set them
# on the command line via -D, but you need to do this before you pass this
# cache file to CMake via -C. e.g.
#
# cmake -D LLVM_RELEASE_ENABLE_PGO=ON -C Release.cmake
set (DEFAULT_RUNTIMES "compiler-rt;libcxx")
if (NOT WIN32)
  list(APPEND DEFAULT_RUNTIMES "libcxxabi" "libunwind")
endif()
set(LLVM_RELEASE_ENABLE_LTO THIN CACHE STRING "")
set(LLVM_RELEASE_ENABLE_PGO ON CACHE BOOL "")
set(LLVM_RELEASE_ENABLE_RUNTIMES ${DEFAULT_RUNTIMES} CACHE STRING "")
set(LLVM_RELEASE_ENABLE_PROJECTS "clang;lld;lldb;clang-tools-extra;bolt;polly;mlir;flang" CACHE STRING "")
# Note we don't need to add install here, since it is one of the pre-defined
# steps.
set(LLVM_RELEASE_FINAL_STAGE_TARGETS "clang;package;check-all;check-llvm;check-clang" CACHE STRING "")
set(CMAKE_BUILD_TYPE RELEASE CACHE STRING "")

# Stage 1 Options
set(LLVM_TARGETS_TO_BUILD Native CACHE STRING "")
set(CLANG_ENABLE_BOOTSTRAP ON CACHE BOOL "")

set(STAGE1_PROJECTS "clang")

# Building Flang on Windows requires compiler-rt, so we need to build it in
# stage1.  compiler-rt is also required for building the Flang tests on
# macOS.
set(STAGE1_RUNTIMES "compiler-rt")

if (LLVM_RELEASE_ENABLE_PGO)
  list(APPEND STAGE1_PROJECTS "lld")
  set(CLANG_BOOTSTRAP_TARGETS
    generate-profdata
    stage2-package
    stage2-clang
    stage2-install
    stage2-check-all
    stage2-check-llvm
    stage2-check-clang CACHE STRING "")

  # Configuration for stage2-instrumented
  set(BOOTSTRAP_CLANG_ENABLE_BOOTSTRAP ON CACHE STRING "")
  # This enables the build targets for the final stage which is called stage2.
  set(BOOTSTRAP_CLANG_BOOTSTRAP_TARGETS ${LLVM_RELEASE_FINAL_STAGE_TARGETS} CACHE STRING "")
  set(BOOTSTRAP_LLVM_BUILD_INSTRUMENTED IR CACHE STRING "")
  set(BOOTSTRAP_LLVM_ENABLE_RUNTIMES "compiler-rt" CACHE STRING "")
  set(BOOTSTRAP_LLVM_ENABLE_PROJECTS "clang;lld" CACHE STRING "")

else()
  if (LLVM_RELEASE_ENABLE_LTO)
    list(APPEND STAGE1_PROJECTS "lld")
  endif()
  # Any targets added here will be given the target name stage2-${target}, so
  # if you want to run them you can just use:
  # ninja -C $BUILDDIR stage2-${target}
  set(CLANG_BOOTSTRAP_TARGETS ${LLVM_RELEASE_FINAL_STAGE_TARGETS} CACHE STRING "")
endif()

# Stage 1 Common Config
set(LLVM_ENABLE_RUNTIMES ${STAGE1_RUNTIMES} CACHE STRING "")
set(LLVM_ENABLE_PROJECTS ${STAGE1_PROJECTS} CACHE STRING "")

# stage2-instrumented and Final Stage Config:
# Options that need to be set in both the instrumented stage (if we are doing
# a pgo build) and the final stage.
set_instrument_and_final_stage_var(CMAKE_POSITION_INDEPENDENT_CODE "ON" STRING)
set_instrument_and_final_stage_var(LLVM_ENABLE_LTO "${LLVM_RELEASE_ENABLE_LTO}" STRING)
if (LLVM_RELEASE_ENABLE_LTO)
  set_instrument_and_final_stage_var(LLVM_ENABLE_LLD "ON" BOOL)
endif()

# Final Stage Config (stage2)
set_final_stage_var(LLVM_ENABLE_RUNTIMES "${LLVM_RELEASE_ENABLE_RUNTIMES}" STRING)
set_final_stage_var(LLVM_ENABLE_PROJECTS "${LLVM_RELEASE_ENABLE_PROJECTS}" STRING)
set_final_stage_var(CPACK_GENERATOR "TXZ" STRING)
set_final_stage_var(CPACK_ARCHIVE_THREADS "0" STRING)

if(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Darwin")
  set_final_stage_var(LLVM_USE_STATIC_ZSTD "ON" BOOL)
endif()
