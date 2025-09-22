# This file sets up a CMakeCache for Apple-style stage2 ThinLTO bootstrap. It is
# specified by the stage1 build.


set(LLVM_ENABLE_LTO THIN CACHE BOOL "")
include(${CMAKE_CURRENT_LIST_DIR}/Apple-stage2.cmake)
