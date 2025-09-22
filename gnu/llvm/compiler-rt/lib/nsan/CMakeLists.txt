add_compiler_rt_component(nsan)

include_directories(..)

set(NSAN_SOURCES
  nsan.cpp
  nsan_flags.cpp
  nsan_interceptors.cpp
  nsan_malloc_linux.cpp
  nsan_stats.cpp
  nsan_suppressions.cpp
)

set(NSAN_PREINIT_SOURCES
  nsan_preinit.cpp)

set(NSAN_HEADERS
  nsan.h
  nsan_flags.h
  nsan_flags.inc
  nsan_platform.h
  nsan_stats.h
  nsan_suppressions.h
)

set(NSAN_DYNAMIC_LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS})

set(NSAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF NSAN_CFLAGS)

set(NSAN_DYNAMIC_CFLAGS ${NSAN_CFLAGS})

set(NSAN_COMMON_RUNTIME_OBJECT_LIBS
  RTInterception
  RTSanitizerCommon
  RTSanitizerCommonLibc
  RTSanitizerCommonCoverage
  RTSanitizerCommonSymbolizer
  RTSanitizerCommonSymbolizerInternal
  RTUbsan)

set(NSAN_DYNAMIC_LIBS
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_CXX_ABI_LIBRARIES}
  ${SANITIZER_COMMON_LINK_LIBS})

append_list_if(COMPILER_RT_HAS_LIBDL dl NSAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBRT rt NSAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBM m NSAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD pthread NSAN_DYNAMIC_LIBS)

# Compile sources into an object library.

add_compiler_rt_object_libraries(RTNsan_dynamic
  ARCHS ${NSAN_SUPPORTED_ARCH}
  SOURCES ${NSAN_SOURCES}
  ADDITIONAL_HEADERS ${NSAN_HEADERS}
  CFLAGS ${NSAN_CFLAGS})

if(NOT APPLE)
  add_compiler_rt_object_libraries(RTNsan
      ARCHS ${NSAN_SUPPORTED_ARCH}
      SOURCES ${NSAN_SOURCES}
      ADDITIONAL_HEADERS ${NSAN_HEADERS}
      CFLAGS ${NSAN_CFLAGS})

  add_compiler_rt_object_libraries(RTNsan_preinit
    ARCHS ${NSAN_SUPPORTED_ARCH}
    SOURCES ${NSAN_PREINIT_SOURCES}
    ADDITIONAL_HEADERS ${NSAN_HEADERS}
    CFLAGS ${NSAN_CFLAGS})

  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
  add_compiler_rt_object_libraries(RTNsan_dynamic_version_script_dummy
    ARCHS ${NSAN_SUPPORTED_ARCH}
    SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
    CFLAGS ${NSAN_DYNAMIC_CFLAGS})
endif()

add_compiler_rt_runtime(
  clang_rt.nsan
  STATIC
  ARCHS ${NSAN_SUPPORTED_ARCH}
  OBJECT_LIBS RTNsan_preinit RTNsan
              ${NSAN_COMMON_RUNTIME_OBJECT_LIBS}
  CFLAGS ${NSAN_CFLAGS}
  PARENT_TARGET nsan)

if(NOT APPLE)
  foreach(arch ${NSAN_SUPPORTED_ARCH})
    if (COMPILER_RT_HAS_VERSION_SCRIPT)
      add_sanitizer_rt_version_list(clang_rt.nsan-dynamic-${arch}
                                    LIBS clang_rt.nsan-${arch}
                                    EXTRA nsan.syms.extra)
      set(VERSION_SCRIPT_FLAG
           -Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/clang_rt.nsan-dynamic-${arch}.vers)
      # The Solaris 11.4 linker supports a subset of GNU ld version scripts,
      # but requires a special option to enable it.
      if (COMPILER_RT_HAS_GNU_VERSION_SCRIPT_COMPAT)
          list(APPEND VERSION_SCRIPT_FLAG -Wl,-z,gnu-version-script-compat)
      endif()
      set_property(SOURCE
        ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
        APPEND PROPERTY
        OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/clang_rt.nsan-dynamic-${arch}.vers)
    else()
      set(VERSION_SCRIPT_FLAG)
    endif()

    add_compiler_rt_runtime(
      clang_rt.nsan
      SHARED
      ARCHS ${arch}
      OBJECT_LIBS ${NSAN_COMMON_RUNTIME_OBJECT_LIBS}
        RTNsan_dynamic
        # The only purpose of RTNsan_dynamic_version_script_dummy is to
        # carry a dependency of the shared runtime on the version script.
        # Replacing it with a straightforward
        # add_dependencies(clang_rt.nsan-dynamic-${arch} clang_rt.nsan-dynamic-${arch}-version-list)
        # generates an order-only dependency in ninja.
        RTNsan_dynamic_version_script_dummy
      CFLAGS ${NSAN_DYNAMIC_CFLAGS}
      LINK_FLAGS ${NSAN_DYNAMIC_LINK_FLAGS}
        ${VERSION_SCRIPT_FLAG}
      LINK_LIBS ${NSAN_DYNAMIC_LIBS}
      PARENT_TARGET nsan)
  endforeach()
endif()

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
