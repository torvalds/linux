# Build for the Memory Profiler runtime support library.

set(MEMPROF_SOURCES
  memprof_allocator.cpp
  memprof_descriptions.cpp
  memprof_flags.cpp
  memprof_interceptors.cpp
  memprof_interceptors_memintrinsics.cpp
  memprof_linux.cpp
  memprof_malloc_linux.cpp
  memprof_mibmap.cpp
  memprof_posix.cpp
  memprof_rawprofile.cpp
  memprof_rtl.cpp
  memprof_shadow_setup.cpp
  memprof_stack.cpp
  memprof_stats.cpp
  memprof_thread.cpp
  )

set(MEMPROF_CXX_SOURCES
  memprof_new_delete.cpp
  )

set(MEMPROF_PREINIT_SOURCES
  memprof_preinit.cpp
  )

SET(MEMPROF_HEADERS
  memprof_allocator.h
  memprof_descriptions.h
  memprof_flags.h
  memprof_flags.inc
  memprof_init_version.h
  memprof_interceptors.h
  memprof_interceptors_memintrinsics.h
  memprof_interface_internal.h
  memprof_internal.h
  memprof_mapping.h
  memprof_mibmap.h
  memprof_rawprofile.h
  memprof_stack.h
  memprof_stats.h
  memprof_thread.h
  )

include_directories(..)
include_directories(../../include)

set(MEMPROF_CFLAGS ${SANITIZER_COMMON_CFLAGS})
set(MEMPROF_COMMON_DEFINITIONS "")

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format MEMPROF_CFLAGS)

append_rtti_flag(OFF MEMPROF_CFLAGS)

set(MEMPROF_DYNAMIC_LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS})

set(MEMPROF_DYNAMIC_DEFINITIONS
  ${MEMPROF_COMMON_DEFINITIONS} MEMPROF_DYNAMIC=1)

set(MEMPROF_DYNAMIC_CFLAGS ${MEMPROF_CFLAGS})
append_list_if(COMPILER_RT_HAS_FTLS_MODEL_INITIAL_EXEC
  -ftls-model=initial-exec MEMPROF_DYNAMIC_CFLAGS)

set(MEMPROF_DYNAMIC_LIBS
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_CXX_ABI_LIBRARIES}
  ${SANITIZER_COMMON_LINK_LIBS})

append_list_if(COMPILER_RT_HAS_LIBDL dl MEMPROF_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBRT rt MEMPROF_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBM m MEMPROF_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD pthread MEMPROF_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBLOG log MEMPROF_DYNAMIC_LIBS)

# Compile MemProf sources into an object library.

add_compiler_rt_object_libraries(RTMemprof_dynamic
  OS ${SANITIZER_COMMON_SUPPORTED_OS}
  ARCHS ${MEMPROF_SUPPORTED_ARCH}
  SOURCES ${MEMPROF_SOURCES} ${MEMPROF_CXX_SOURCES}
  ADDITIONAL_HEADERS ${MEMPROF_HEADERS}
  CFLAGS ${MEMPROF_DYNAMIC_CFLAGS}
  DEFS ${MEMPROF_DYNAMIC_DEFINITIONS}
  DEPS ${MEMPROF_DEPS})

add_compiler_rt_object_libraries(RTMemprof
  ARCHS ${MEMPROF_SUPPORTED_ARCH}
  SOURCES ${MEMPROF_SOURCES}
  ADDITIONAL_HEADERS ${MEMPROF_HEADERS}
  CFLAGS ${MEMPROF_CFLAGS}
  DEFS ${MEMPROF_COMMON_DEFINITIONS}
  DEPS ${MEMPROF_DEPS})
add_compiler_rt_object_libraries(RTMemprof_cxx
  ARCHS ${MEMPROF_SUPPORTED_ARCH}
  SOURCES ${MEMPROF_CXX_SOURCES}
  ADDITIONAL_HEADERS ${MEMPROF_HEADERS}
  CFLAGS ${MEMPROF_CFLAGS}
  DEFS ${MEMPROF_COMMON_DEFINITIONS}
  DEPS ${MEMPROF_DEPS})
add_compiler_rt_object_libraries(RTMemprof_preinit
  ARCHS ${MEMPROF_SUPPORTED_ARCH}
  SOURCES ${MEMPROF_PREINIT_SOURCES}
  ADDITIONAL_HEADERS ${MEMPROF_HEADERS}
  CFLAGS ${MEMPROF_CFLAGS}
  DEFS ${MEMPROF_COMMON_DEFINITIONS}
  DEPS ${MEMPROF_DEPS})

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
add_compiler_rt_object_libraries(RTMemprof_dynamic_version_script_dummy
  ARCHS ${MEMPROF_SUPPORTED_ARCH}
  SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
  CFLAGS ${MEMPROF_DYNAMIC_CFLAGS}
  DEFS ${MEMPROF_DYNAMIC_DEFINITIONS}
  DEPS ${MEMPROF_DEPS})

# Build MemProf runtimes shipped with Clang.
add_compiler_rt_component(memprof)

# Build separate libraries for each target.

set(MEMPROF_COMMON_RUNTIME_OBJECT_LIBS
  RTInterception
  RTSanitizerCommon
  RTSanitizerCommonLibc
  RTSanitizerCommonCoverage
  RTSanitizerCommonSymbolizer
  # FIXME: hangs.
  # RTSanitizerCommonSymbolizerInternal
)

add_compiler_rt_runtime(clang_rt.memprof
  STATIC
  ARCHS ${MEMPROF_SUPPORTED_ARCH}
  OBJECT_LIBS RTMemprof_preinit
              RTMemprof
              ${MEMPROF_COMMON_RUNTIME_OBJECT_LIBS}
  CFLAGS ${MEMPROF_CFLAGS}
  DEFS ${MEMPROF_COMMON_DEFINITIONS}
  PARENT_TARGET memprof)

add_compiler_rt_runtime(clang_rt.memprof_cxx
  STATIC
  ARCHS ${MEMPROF_SUPPORTED_ARCH}
  OBJECT_LIBS RTMemprof_cxx
  CFLAGS ${MEMPROF_CFLAGS}
  DEFS ${MEMPROF_COMMON_DEFINITIONS}
  PARENT_TARGET memprof)

add_compiler_rt_runtime(clang_rt.memprof-preinit
  STATIC
  ARCHS ${MEMPROF_SUPPORTED_ARCH}
  OBJECT_LIBS RTMemprof_preinit
  CFLAGS ${MEMPROF_CFLAGS}
  DEFS ${MEMPROF_COMMON_DEFINITIONS}
  PARENT_TARGET memprof)

foreach(arch ${MEMPROF_SUPPORTED_ARCH})
  if (UNIX)
    add_sanitizer_rt_version_list(clang_rt.memprof-dynamic-${arch}
                                  LIBS clang_rt.memprof-${arch} clang_rt.memprof_cxx-${arch}
                                  EXTRA memprof.syms.extra)
    set(VERSION_SCRIPT_FLAG
         -Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/clang_rt.memprof-dynamic-${arch}.vers)
    set_property(SOURCE
      ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
      APPEND PROPERTY
      OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/clang_rt.memprof-dynamic-${arch}.vers)
  else()
    set(VERSION_SCRIPT_FLAG)
  endif()

  set(MEMPROF_DYNAMIC_WEAK_INTERCEPTION)

  add_compiler_rt_runtime(clang_rt.memprof
    SHARED
    ARCHS ${arch}
    OBJECT_LIBS ${MEMPROF_COMMON_RUNTIME_OBJECT_LIBS}
            RTMemprof_dynamic
            # The only purpose of RTMemprof_dynamic_version_script_dummy is to
            # carry a dependency of the shared runtime on the version script.
            # Replacing it with a straightforward
            # add_dependencies(clang_rt.memprof-dynamic-${arch} clang_rt.memprof-dynamic-${arch}-version-list)
            # generates an order-only dependency in ninja.
            RTMemprof_dynamic_version_script_dummy
            ${MEMPROF_DYNAMIC_WEAK_INTERCEPTION}
    CFLAGS ${MEMPROF_DYNAMIC_CFLAGS}
    LINK_FLAGS ${MEMPROF_DYNAMIC_LINK_FLAGS}
              ${VERSION_SCRIPT_FLAG}
    LINK_LIBS ${MEMPROF_DYNAMIC_LIBS}
    DEFS ${MEMPROF_DYNAMIC_DEFINITIONS}
    PARENT_TARGET memprof)

  if (SANITIZER_USE_SYMBOLS)
    add_sanitizer_rt_symbols(clang_rt.memprof_cxx
      ARCHS ${arch})
    add_dependencies(memprof clang_rt.memprof_cxx-${arch}-symbols)
    add_sanitizer_rt_symbols(clang_rt.memprof
      ARCHS ${arch}
      EXTRA memprof.syms.extra)
    add_dependencies(memprof clang_rt.memprof-${arch}-symbols)
  endif()
endforeach()


if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
