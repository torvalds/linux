include_directories(..)

# Runtime library sources and build flags.
set(MSAN_RTL_SOURCES
  msan.cpp
  msan_allocator.cpp
  msan_chained_origin_depot.cpp
  msan_dl.cpp
  msan_interceptors.cpp
  msan_linux.cpp
  msan_report.cpp
  msan_thread.cpp
  msan_poisoning.cpp
  )

set(MSAN_RTL_CXX_SOURCES
  msan_new_delete.cpp
  )

set(MSAN_RTL_HEADERS
  msan.h
  msan_allocator.h
  msan_chained_origin_depot.h
  msan_dl.h
  msan_flags.h
  msan_flags.inc
  msan_interface_internal.h
  msan_origin.h
  msan_poisoning.h
  msan_report.h
  msan_thread.h
  )

set(MSAN_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS})
if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  append_list_if(COMPILER_RT_HAS_FTLS_MODEL_INITIAL_EXEC -ftls-model=initial-exec MSAN_RTL_CFLAGS)
endif()
append_rtti_flag(OFF MSAN_RTL_CFLAGS)
if(NOT CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
  append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE MSAN_RTL_CFLAGS)
endif()
# Prevent clang from generating libc calls.
append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding MSAN_RTL_CFLAGS)

set(MSAN_RUNTIME_LIBRARIES)

# Static runtime library.
add_compiler_rt_component(msan)

foreach(arch ${MSAN_SUPPORTED_ARCH})
  add_compiler_rt_runtime(clang_rt.msan
    STATIC
    ARCHS ${arch}
    SOURCES ${MSAN_RTL_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonCoverage.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>
            $<TARGET_OBJECTS:RTUbsan.${arch}>
    ADDITIONAL_HEADERS ${MSAN_RTL_HEADERS}
    CFLAGS ${MSAN_RTL_CFLAGS}
    PARENT_TARGET msan)
  add_compiler_rt_runtime(clang_rt.msan_cxx
    STATIC
    ARCHS ${arch}
    SOURCES ${MSAN_RTL_CXX_SOURCES}
            $<TARGET_OBJECTS:RTUbsan_cxx.${arch}>
    ADDITIONAL_HEADERS ${MSAN_RTL_HEADERS}
    CFLAGS ${MSAN_RTL_CFLAGS}
    PARENT_TARGET msan)
  list(APPEND MSAN_RUNTIME_LIBRARIES clang_rt.msan-${arch}
                                     clang_rt.msan_cxx-${arch})
  if(SANITIZER_USE_SYMBOLS)
    add_sanitizer_rt_symbols(clang_rt.msan
      ARCHS ${arch}
      EXTRA msan.syms.extra)
    add_sanitizer_rt_symbols(clang_rt.msan_cxx
      ARCHS ${arch}
      EXTRA msan.syms.extra)
    add_dependencies(msan clang_rt.msan-${arch}-symbols
                          clang_rt.msan_cxx-${arch}-symbols)
  endif()
endforeach()

add_compiler_rt_resource_file(msan_ignorelist msan_ignorelist.txt msan)
list(APPEND MSAN_RUNTIME_LIBRARIES msan_ignorelist)

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
