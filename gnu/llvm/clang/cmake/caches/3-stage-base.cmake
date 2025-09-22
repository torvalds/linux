set(CMAKE_BUILD_TYPE RELEASE CACHE STRING "")
set(CLANG_ENABLE_BOOTSTRAP ON CACHE BOOL "")

set(LLVM_ENABLE_PROJECTS "clang;lld" CACHE STRING "")
set(LLVM_ENABLE_RUNTIMES "compiler-rt;libcxx;libcxxabi" CACHE STRING "")

if(APPLE)
  # Use LLD to have fewer requirements on system linker, unless we're on an apple
  # platform where the system compiler is to be preferred.
  set(BOOTSTRAP_LLVM_ENABLE_LLD OFF CACHE BOOL "")
  set(BOOTSTRAP_LLVM_ENABLE_LTO ON CACHE BOOL "")
elseif(CMAKE_HOST_UNIX)
  # s390/SystemZ is unsupported by LLD, so don't try to enable LTO if it
  # cannot work.
  # We do our own uname business here since the appropriate variables from CMake
  # and llvm are not yet available.
  find_program(CMAKE_UNAME uname /bin /usr/bin /usr/local/bin )
  if(CMAKE_UNAME)
    exec_program(${CMAKE_UNAME} ARGS -m OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_PROCESSOR
        RETURN_VALUE val)
  endif(CMAKE_UNAME)

  if("${CMAKE_HOST_SYSTEM_PROCESSOR}" MATCHES "s390")
    set(BOOTSTRAP_LLVM_ENABLE_LTO OFF CACHE BOOL "")
    set(BOOTSTRAP_LLVM_ENABLE_LLD OFF CACHE BOOL "")
  else()
    set(BOOTSTRAP_LLVM_ENABLE_LTO ON CACHE BOOL "")
    set(BOOTSTRAP_LLVM_ENABLE_LLD ON CACHE BOOL "")
  endif()

else()
  set(BOOTSTRAP_LLVM_ENABLE_LTO ON CACHE BOOL "")
  set(BOOTSTRAP_LLVM_ENABLE_LLD ON CACHE BOOL "")
endif()


set(CLANG_BOOTSTRAP_TARGETS
  clang
  check-all
  check-llvm
  check-clang
  test-suite CACHE STRING "")

set(CLANG_BOOTSTRAP_CMAKE_ARGS
  -C ${CMAKE_CURRENT_LIST_DIR}/3-stage-base.cmake
  CACHE STRING "")
