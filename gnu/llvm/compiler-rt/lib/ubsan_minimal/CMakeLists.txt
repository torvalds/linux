# Build for the minimal undefined behavior sanitizer runtime support library.

set(UBSAN_MINIMAL_SOURCES
  ubsan_minimal_handlers.cpp
  )

include_directories(..)

set(UBSAN_CFLAGS
  ${SANITIZER_COMMON_CFLAGS}
  -DSANITIZER_COMMON_NO_REDEFINE_BUILTINS)
append_rtti_flag(OFF UBSAN_CFLAGS)

set(UBSAN_LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS})

set(UBSAN_DYNAMIC_LIBS ${SANITIZER_COMMON_LINK_LIBS})

add_compiler_rt_component(ubsan-minimal)

# Common parts of minimal UBSan runtime.
add_compiler_rt_object_libraries(RTUbsan_minimal
  OS ${SANITIZER_COMMON_SUPPORTED_OS}
  ARCHS ${UBSAN_COMMON_SUPPORTED_ARCH}
  SOURCES ${UBSAN_MINIMAL_SOURCES} CFLAGS ${UBSAN_CFLAGS})


# Standalone minimal UBSan runtimes.
add_compiler_rt_runtime(clang_rt.ubsan_minimal
  STATIC
  OS ${UBSAN_SUPPORTED_OS}
  ARCHS ${UBSAN_SUPPORTED_ARCH}
  OBJECT_LIBS RTUbsan_minimal
  CFLAGS ${UBSAN_CFLAGS}
  PARENT_TARGET ubsan-minimal)

add_compiler_rt_runtime(clang_rt.ubsan_minimal
  SHARED
  OS ${UBSAN_SUPPORTED_OS}
  ARCHS ${UBSAN_SUPPORTED_ARCH}
  OBJECT_LIBS RTUbsan_minimal
  CFLAGS ${UBSAN_CFLAGS}
  LINK_FLAGS ${UBSAN_LINK_FLAGS}
  LINK_LIBS ${UBSAN_DYNAMIC_LIBS}
  PARENT_TARGET ubsan-minimal)

if (SANITIZER_USE_SYMBOLS AND NOT APPLE)
  set(ARCHS_FOR_SYMBOLS ${UBSAN_SUPPORTED_ARCH})
  list(REMOVE_ITEM ARCHS_FOR_SYMBOLS i386 i686)
  add_sanitizer_rt_symbols(clang_rt.ubsan_minimal
    ARCHS ${ARCHS_FOR_SYMBOLS}
    PARENT_TARGET ubsan-minimal
    EXTRA ubsan.syms.extra)
endif()
