# Build libc++abi and libc++ closely resembling what is shipped in the Android
# NDK.

# The NDK names the libraries libc++_shared.so and libc++_static.a. Using the
# libc++_shared.so soname ensures that the library doesn't interact with the
# libc++.so in /system/lib[64].
set(LIBCXX_SHARED_OUTPUT_NAME c++_shared CACHE STRING "")
set(LIBCXX_STATIC_OUTPUT_NAME c++_static CACHE STRING "")

# The NDK libc++ uses a special namespace to help isolate its symbols from those
# in the platform's STL (e.g. /system/lib[64]/libc++.so, but possibly stlport on
# older versions of Android).
set(LIBCXX_ABI_VERSION 1 CACHE STRING "")
set(LIBCXX_ABI_NAMESPACE __ndk1 CACHE STRING "")

# CMake doesn't add a version suffix to an Android shared object filename,
# (because CMAKE_PLATFORM_NO_VERSIONED_SONAME is set), so it writes both a
# libc++_shared.so ELF file and a libc++_shared.so linker script to the same
# output path (the script clobbers the binary). Turn off the linker script.
set(LIBCXX_ENABLE_ABI_LINKER_SCRIPT OFF CACHE BOOL "")

set(LIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY ON CACHE BOOL "")
set(LIBCXXABI_ENABLE_SHARED OFF CACHE BOOL "")

# Android uses its own unwinder library
set(LIBCXXABI_USE_LLVM_UNWINDER OFF CACHE BOOL "")

# Clang links libc++ by default, but it doesn't exist yet. The libc++ CMake
# files specify -nostdlib++ to avoid this problem, but CMake's default "compiler
# works" testing doesn't pass that flag, so force those tests to pass.
set(CMAKE_C_COMPILER_WORKS ON CACHE BOOL "")
set(CMAKE_CXX_COMPILER_WORKS ON CACHE BOOL "")

# Use adb to push tests to a locally-connected device (e.g. emulator) and run
# them.
set(LIBCXX_TEST_CONFIG "llvm-libc++-android-ndk.cfg.in" CACHE STRING "")
set(LIBCXXABI_TEST_CONFIG "llvm-libc++abi-android-ndk.cfg.in" CACHE STRING "")
