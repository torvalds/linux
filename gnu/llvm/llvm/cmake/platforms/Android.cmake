# Toolchain config for Android NDK.
# This is expected to be used with a standalone Android toolchain (see
# docs/STANDALONE-TOOLCHAIN.html in the NDK on how to get one).
#
# Usage:
# mkdir build; cd build
# cmake ..; make
# mkdir android; cd android
# cmake -DLLVM_ANDROID_TOOLCHAIN_DIR=/path/to/android/ndk \
#   -DCMAKE_TOOLCHAIN_FILE=../../cmake/platforms/Android.cmake ../..
# make <target>

SET(CMAKE_SYSTEM_NAME Linux)

IF(NOT CMAKE_C_COMPILER)
 SET(CMAKE_C_COMPILER ${CMAKE_BINARY_DIR}/../bin/clang)
ENDIF()

IF(NOT CMAKE_CXX_COMPILER)
 SET(CMAKE_CXX_COMPILER ${CMAKE_BINARY_DIR}/../bin/clang++)
ENDIF()

SET(ANDROID "1" CACHE STRING "ANDROID" FORCE)

SET(ANDROID_COMMON_FLAGS "-target arm-linux-androideabi --sysroot=${LLVM_ANDROID_TOOLCHAIN_DIR}/sysroot -B${LLVM_ANDROID_TOOLCHAIN_DIR}")
SET(CMAKE_C_FLAGS "${ANDROID_COMMON_FLAGS}" CACHE STRING "toolchain_cflags" FORCE)
SET(CMAKE_CXX_FLAGS "${ANDROID_COMMON_FLAGS}" CACHE STRING "toolchain_cxxflags" FORCE)
SET(CMAKE_EXE_LINKER_FLAGS "-pie" CACHE STRING "toolchain_exelinkflags" FORCE)

