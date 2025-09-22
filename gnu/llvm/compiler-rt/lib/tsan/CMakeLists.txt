# Build for the ThreadSanitizer runtime support library.

set(TSAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})
# SANITIZER_COMMON_CFLAGS contains -fPIC, but it's performance-critical for
# TSan runtime to be built with -fPIE to reduce the number of register spills.
# On FreeBSD however it provokes linkage issue thus we disable it.
if(NOT CMAKE_SYSTEM MATCHES "FreeBSD")
  append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE TSAN_CFLAGS)
endif()
append_rtti_flag(OFF TSAN_CFLAGS)

if(COMPILER_RT_TSAN_DEBUG_OUTPUT)
  # Add extra debug information to TSan runtime. This configuration is rarely
  # used, but we need to support it so that debug output will not bitrot.
  list(APPEND TSAN_CFLAGS -DTSAN_DEBUG_OUTPUT=2)
endif()

# Add the actual runtime library.
add_subdirectory(rtl)

# Build libcxx instrumented with TSan.
if(COMPILER_RT_LIBCXX_PATH AND
   COMPILER_RT_LIBCXXABI_PATH AND
   COMPILER_RT_TEST_COMPILER_ID STREQUAL "Clang" AND
   NOT ANDROID)
  set(libcxx_tsan_deps)
  foreach(arch ${TSAN_SUPPORTED_ARCH})
    get_target_flags_for_arch(${arch} TARGET_CFLAGS)
    set(LIBCXX_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libcxx_tsan_${arch})
    add_custom_libcxx(libcxx_tsan_${arch} ${LIBCXX_PREFIX}
      DEPS ${TSAN_RUNTIME_LIBRARIES}
      CFLAGS ${TARGET_CFLAGS} -fsanitize=thread
      USE_TOOLCHAIN)
    list(APPEND libcxx_tsan_deps libcxx_tsan_${arch}-build)
  endforeach()

  add_custom_target(libcxx_tsan DEPENDS ${libcxx_tsan_deps})
  set_target_properties(libcxx_tsan PROPERTIES FOLDER "Compiler-RT/Metatargets")
endif()

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
