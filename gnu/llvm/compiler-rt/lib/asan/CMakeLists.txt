# Build for the AddressSanitizer runtime support library.

set(ASAN_SOURCES
  asan_allocator.cpp
  asan_activation.cpp
  asan_debugging.cpp
  asan_descriptions.cpp
  asan_errors.cpp
  asan_fake_stack.cpp
  asan_flags.cpp
  asan_fuchsia.cpp
  asan_globals.cpp
  asan_globals_win.cpp
  asan_interceptors.cpp
  asan_interceptors_memintrinsics.cpp
  asan_linux.cpp
  asan_mac.cpp
  asan_malloc_linux.cpp
  asan_malloc_mac.cpp
  asan_malloc_win.cpp
  asan_memory_profile.cpp
  asan_poisoning.cpp
  asan_posix.cpp
  asan_premap_shadow.cpp
  asan_report.cpp
  asan_rtl.cpp
  asan_shadow_setup.cpp
  asan_stack.cpp
  asan_stats.cpp
  asan_suppressions.cpp
  asan_thread.cpp
  asan_win.cpp
  )

if (NOT WIN32 AND NOT APPLE)
  list(APPEND ASAN_SOURCES
    asan_interceptors_vfork.S
    )
endif()

set(ASAN_CXX_SOURCES
  asan_new_delete.cpp
  )

set(ASAN_STATIC_SOURCES
  asan_rtl_static.cpp
  )

if ("x86_64" IN_LIST ASAN_SUPPORTED_ARCH AND NOT WIN32 AND NOT APPLE)
  list(APPEND ASAN_STATIC_SOURCES
    asan_rtl_x86_64.S
  )
endif()

set(ASAN_PREINIT_SOURCES
  asan_preinit.cpp
  )

SET(ASAN_HEADERS
  asan_activation.h
  asan_activation_flags.inc
  asan_allocator.h
  asan_descriptions.h
  asan_errors.h
  asan_fake_stack.h
  asan_flags.h
  asan_flags.inc
  asan_init_version.h
  asan_interceptors.h
  asan_interceptors_memintrinsics.h
  asan_interface.inc
  asan_interface_internal.h
  asan_internal.h
  asan_mapping.h
  asan_poisoning.h
  asan_premap_shadow.h
  asan_report.h
  asan_scariness_score.h
  asan_stack.h
  asan_stats.h
  asan_suppressions.h
  asan_thread.h
  )

include_directories(..)

set(ASAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})

append_list_if(MSVC /Zl ASAN_CFLAGS)

set(ASAN_COMMON_DEFINITIONS "")

append_rtti_flag(OFF ASAN_CFLAGS)

# Silence warnings in system headers with MSVC.
if(NOT CLANG_CL)
  append_list_if(COMPILER_RT_HAS_EXTERNAL_FLAG "/experimental:external;/external:W0;/external:anglebrackets" ASAN_CFLAGS)
endif()

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format ASAN_CFLAGS)

set(ASAN_DYNAMIC_LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS})

if(ANDROID)
# Put most Sanitizer shared libraries in the global group. For more details, see
# android-changes-for-ndk-developers.md#changes-to-library-search-order
  if (COMPILER_RT_HAS_Z_GLOBAL)
    list(APPEND ASAN_DYNAMIC_LINK_FLAGS -Wl,-z,global)
  endif()
endif()

set(ASAN_DYNAMIC_DEFINITIONS
  ${ASAN_COMMON_DEFINITIONS} ASAN_DYNAMIC=1)
append_list_if(WIN32 INTERCEPTION_DYNAMIC_CRT ASAN_DYNAMIC_DEFINITIONS)

set(ASAN_DYNAMIC_CFLAGS ${ASAN_CFLAGS})
append_list_if(COMPILER_RT_HAS_FTLS_MODEL_INITIAL_EXEC
  -ftls-model=initial-exec ASAN_DYNAMIC_CFLAGS)
append_list_if(MSVC /DEBUG ASAN_DYNAMIC_LINK_FLAGS)

set(ASAN_DYNAMIC_LIBS
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_CXX_ABI_LIBRARIES}
  ${SANITIZER_COMMON_LINK_LIBS})

append_list_if(COMPILER_RT_HAS_LIBDL dl ASAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBRT rt ASAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBM m ASAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD pthread ASAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBLOG log ASAN_DYNAMIC_LIBS)
append_list_if(MINGW "${MINGW_LIBRARIES}" ASAN_DYNAMIC_LIBS)

# Compile ASan sources into an object library.

add_compiler_rt_object_libraries(RTAsan_dynamic
  OS ${SANITIZER_COMMON_SUPPORTED_OS}
  ARCHS ${ASAN_SUPPORTED_ARCH}
  SOURCES ${ASAN_SOURCES} ${ASAN_CXX_SOURCES}
  ADDITIONAL_HEADERS ${ASAN_HEADERS}
  CFLAGS ${ASAN_DYNAMIC_CFLAGS}
  DEFS ${ASAN_DYNAMIC_DEFINITIONS})

if(NOT APPLE)
  add_compiler_rt_object_libraries(RTAsan
    ARCHS ${ASAN_SUPPORTED_ARCH}
    SOURCES ${ASAN_SOURCES}
    ADDITIONAL_HEADERS ${ASAN_HEADERS}
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTAsan_cxx
    ARCHS ${ASAN_SUPPORTED_ARCH}
    SOURCES ${ASAN_CXX_SOURCES}
    ADDITIONAL_HEADERS ${ASAN_HEADERS}
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTAsan_static
    ARCHS ${ASAN_SUPPORTED_ARCH}
    SOURCES ${ASAN_STATIC_SOURCES}
    ADDITIONAL_HEADERS ${ASAN_HEADERS}
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(RTAsan_preinit
    ARCHS ${ASAN_SUPPORTED_ARCH}
    SOURCES ${ASAN_PREINIT_SOURCES}
    ADDITIONAL_HEADERS ${ASAN_HEADERS}
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS})

  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
  add_compiler_rt_object_libraries(RTAsan_dynamic_version_script_dummy
    ARCHS ${ASAN_SUPPORTED_ARCH}
    SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
    CFLAGS ${ASAN_DYNAMIC_CFLAGS}
    DEFS ${ASAN_DYNAMIC_DEFINITIONS})
endif()

# Build ASan runtimes shipped with Clang.
add_compiler_rt_component(asan)

if(APPLE)
  add_weak_symbols("asan" WEAK_SYMBOL_LINK_FLAGS)
  add_weak_symbols("lsan" WEAK_SYMBOL_LINK_FLAGS)
  add_weak_symbols("ubsan" WEAK_SYMBOL_LINK_FLAGS)
  add_weak_symbols("sanitizer_common" WEAK_SYMBOL_LINK_FLAGS)
  add_weak_symbols("xray" WEAK_SYMBOL_LINK_FLAGS)

  add_compiler_rt_runtime(clang_rt.asan
    SHARED
    OS ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${ASAN_SUPPORTED_ARCH}
    OBJECT_LIBS RTAsan_dynamic
                RTInterception
                RTSanitizerCommon
                RTSanitizerCommonLibc
                RTSanitizerCommonCoverage
                RTSanitizerCommonSymbolizer
                RTLSanCommon
                RTUbsan
    CFLAGS ${ASAN_DYNAMIC_CFLAGS}
    LINK_FLAGS ${WEAK_SYMBOL_LINK_FLAGS}
    DEFS ${ASAN_DYNAMIC_DEFINITIONS}
    PARENT_TARGET asan)

  add_compiler_rt_runtime(clang_rt.asan_static
    STATIC
    ARCHS ${ASAN_SUPPORTED_ARCH}
    OBJECT_LIBS RTAsan_static
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS}
    PARENT_TARGET asan)
else()
  # Build separate libraries for each target.

  set(ASAN_COMMON_RUNTIME_OBJECT_LIBS
    RTInterception
    RTSanitizerCommon
    RTSanitizerCommonLibc
    RTSanitizerCommonCoverage
    RTSanitizerCommonSymbolizer
    RTSanitizerCommonSymbolizerInternal
    RTLSanCommon
    RTUbsan)

  add_compiler_rt_runtime(clang_rt.asan
    STATIC
    ARCHS ${ASAN_SUPPORTED_ARCH}
    OBJECT_LIBS RTAsan_preinit
                RTAsan
                ${ASAN_COMMON_RUNTIME_OBJECT_LIBS}
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS}
    PARENT_TARGET asan)

  add_compiler_rt_runtime(clang_rt.asan_cxx
    STATIC
    ARCHS ${ASAN_SUPPORTED_ARCH}
    OBJECT_LIBS RTAsan_cxx
                RTUbsan_cxx
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS}
    PARENT_TARGET asan)

  add_compiler_rt_runtime(clang_rt.asan_static
    STATIC
    ARCHS ${ASAN_SUPPORTED_ARCH}
    OBJECT_LIBS RTAsan_static
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS}
    PARENT_TARGET asan)

  add_compiler_rt_runtime(clang_rt.asan-preinit
    STATIC
    ARCHS ${ASAN_SUPPORTED_ARCH}
    OBJECT_LIBS RTAsan_preinit
    CFLAGS ${ASAN_CFLAGS}
    DEFS ${ASAN_COMMON_DEFINITIONS}
    PARENT_TARGET asan)

  foreach(arch ${ASAN_SUPPORTED_ARCH})
    if (COMPILER_RT_HAS_VERSION_SCRIPT)
      add_sanitizer_rt_version_list(clang_rt.asan-dynamic-${arch}
                                    LIBS clang_rt.asan-${arch} clang_rt.asan_cxx-${arch}
                                    EXTRA asan.syms.extra)
      set(VERSION_SCRIPT_FLAG
           -Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/clang_rt.asan-dynamic-${arch}.vers)
      # The Solaris 11.4 linker supports a subset of GNU ld version scripts,
      # but requires a special option to enable it.
      if (COMPILER_RT_HAS_GNU_VERSION_SCRIPT_COMPAT)
          list(APPEND VERSION_SCRIPT_FLAG -Wl,-z,gnu-version-script-compat)
      endif()
      set_property(SOURCE
        ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
        APPEND PROPERTY
        OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/clang_rt.asan-dynamic-${arch}.vers)
    else()
      set(VERSION_SCRIPT_FLAG)
    endif()

    set(ASAN_DYNAMIC_WEAK_INTERCEPTION)
    if (WIN32)
      add_compiler_rt_object_libraries(AsanWeakInterception
        ${SANITIZER_COMMON_SUPPORTED_OS}
        ARCHS ${arch}
        SOURCES
          asan_win_weak_interception.cpp
        CFLAGS ${ASAN_CFLAGS} -DSANITIZER_DYNAMIC
        DEFS ${ASAN_COMMON_DEFINITIONS})
      set(ASAN_DYNAMIC_WEAK_INTERCEPTION
          AsanWeakInterception
          UbsanWeakInterception
          SancovWeakInterception
          SanitizerCommonWeakInterception)
    endif()

    add_compiler_rt_runtime(clang_rt.asan
      SHARED
      ARCHS ${arch}
      OBJECT_LIBS ${ASAN_COMMON_RUNTIME_OBJECT_LIBS}
              RTAsan_dynamic
              # The only purpose of RTAsan_dynamic_version_script_dummy is to
              # carry a dependency of the shared runtime on the version script.
              # Replacing it with a straightforward
              # add_dependencies(clang_rt.asan-dynamic-${arch} clang_rt.asan-dynamic-${arch}-version-list)
              # generates an order-only dependency in ninja.
              RTAsan_dynamic_version_script_dummy
              RTUbsan_cxx
              ${ASAN_DYNAMIC_WEAK_INTERCEPTION}
      CFLAGS ${ASAN_DYNAMIC_CFLAGS}
      LINK_FLAGS ${ASAN_DYNAMIC_LINK_FLAGS}
                ${VERSION_SCRIPT_FLAG}
      LINK_LIBS ${ASAN_DYNAMIC_LIBS}
      DEFS ${ASAN_DYNAMIC_DEFINITIONS}
      PARENT_TARGET asan)

    if (SANITIZER_USE_SYMBOLS AND NOT ${arch} STREQUAL "i386")
      add_sanitizer_rt_symbols(clang_rt.asan_cxx
        ARCHS ${arch})
      add_dependencies(asan clang_rt.asan_cxx-${arch}-symbols)
      add_sanitizer_rt_symbols(clang_rt.asan
        ARCHS ${arch}
        EXTRA asan.syms.extra)
      add_dependencies(asan clang_rt.asan-${arch}-symbols)
    endif()

    if (WIN32)
      add_compiler_rt_object_libraries(AsanDllThunk
        ${SANITIZER_COMMON_SUPPORTED_OS}
        ARCHS ${arch}
        SOURCES asan_globals_win.cpp
                asan_win_dll_thunk.cpp
        CFLAGS ${ASAN_CFLAGS} -DSANITIZER_DLL_THUNK
        DEFS ${ASAN_COMMON_DEFINITIONS})

      add_compiler_rt_runtime(clang_rt.asan_dll_thunk
        STATIC
        ARCHS ${arch}
        OBJECT_LIBS AsanDllThunk
                    UbsanDllThunk
                    SancovDllThunk
                    SanitizerCommonDllThunk
        SOURCES $<TARGET_OBJECTS:RTInterception.${arch}>
        PARENT_TARGET asan)

      set(DYNAMIC_RUNTIME_THUNK_CFLAGS "-DSANITIZER_DYNAMIC_RUNTIME_THUNK")
      if(MSVC)
        list(APPEND DYNAMIC_RUNTIME_THUNK_CFLAGS "-Zl")
      elseif(CMAKE_C_COMPILER_ID MATCHES Clang)
        list(APPEND DYNAMIC_RUNTIME_THUNK_CFLAGS "-nodefaultlibs")
      endif()

      add_compiler_rt_object_libraries(AsanDynamicRuntimeThunk
        ${SANITIZER_COMMON_SUPPORTED_OS}
        ARCHS ${arch}
        SOURCES asan_globals_win.cpp
                asan_win_dynamic_runtime_thunk.cpp
        CFLAGS ${ASAN_CFLAGS} ${DYNAMIC_RUNTIME_THUNK_CFLAGS}
        DEFS ${ASAN_COMMON_DEFINITIONS})

      add_compiler_rt_runtime(clang_rt.asan_dynamic_runtime_thunk
        STATIC
        ARCHS ${arch}
        OBJECT_LIBS AsanDynamicRuntimeThunk
                    UbsanDynamicRuntimeThunk
                    SancovDynamicRuntimeThunk
                    SanitizerCommonDynamicRuntimeThunk
        CFLAGS ${ASAN_CFLAGS} ${DYNAMIC_RUNTIME_THUNK_CFLAGS}
        DEFS ${ASAN_COMMON_DEFINITIONS}
        PARENT_TARGET asan)
    endif()
  endforeach()
endif()

add_compiler_rt_resource_file(asan_ignorelist asan_ignorelist.txt asan)

add_subdirectory(scripts)

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
