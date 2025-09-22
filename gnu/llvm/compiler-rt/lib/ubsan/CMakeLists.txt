# Build for the undefined behavior sanitizer runtime support library.

set(UBSAN_SOURCES
  ubsan_diag.cpp
  ubsan_init.cpp
  ubsan_flags.cpp
  ubsan_handlers.cpp
  ubsan_monitor.cpp
  ubsan_value.cpp
  )

set(UBSAN_STANDALONE_SOURCES
  ubsan_diag_standalone.cpp
  ubsan_init_standalone.cpp
  ubsan_signals_standalone.cpp
  )

set(UBSAN_CXXABI_SOURCES
  ubsan_handlers_cxx.cpp
  ubsan_type_hash.cpp
  ubsan_type_hash_itanium.cpp
  ubsan_type_hash_win.cpp
 )

set(UBSAN_HEADERS
  ubsan_checks.inc
  ubsan_diag.h
  ubsan_flags.h
  ubsan_flags.inc
  ubsan_handlers.h
  ubsan_handlers_cxx.h
  ubsan_init.h
  ubsan_interface.inc
  ubsan_monitor.h
  ubsan_platform.h
  ubsan_signals_standalone.h
  ubsan_type_hash.h
  ubsan_value.h
  )

include_directories(..)

set(UBSAN_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_list_if(MSVC /Zl UBSAN_CFLAGS)
append_rtti_flag(OFF UBSAN_CFLAGS)
append_list_if(SANITIZER_CAN_USE_CXXABI -DUBSAN_CAN_USE_CXXABI UBSAN_CFLAGS)

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format UBSAN_CFLAGS)

set(UBSAN_STANDALONE_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF UBSAN_STANDALONE_CFLAGS)
append_list_if(SANITIZER_CAN_USE_CXXABI -DUBSAN_CAN_USE_CXXABI UBSAN_STANDALONE_CFLAGS)

set(UBSAN_CXXFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(ON UBSAN_CXXFLAGS)
append_list_if(SANITIZER_CAN_USE_CXXABI -DUBSAN_CAN_USE_CXXABI UBSAN_CXXFLAGS)

# Silence warnings in system headers with MSVC.
if(NOT CLANG_CL)
  append_list_if(COMPILER_RT_HAS_EXTERNAL_FLAG "/experimental:external;/external:W0;/external:anglebrackets" UBSAN_CXXFLAGS)
endif()

set(UBSAN_LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS})

set(UBSAN_DYNAMIC_LIBS
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_CXX_ABI_LIBRARIES}
  ${SANITIZER_COMMON_LINK_LIBS})

append_list_if(COMPILER_RT_HAS_LIBDL dl UBSAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBLOG log UBSAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBRT rt UBSAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD pthread UBSAN_DYNAMIC_LIBS)
if (COMPILER_RT_ENABLE_INTERNAL_SYMBOLIZER)
  append_list_if(COMPILER_RT_HAS_LIBM m UBSAN_DYNAMIC_LIBS)
endif()

add_compiler_rt_component(ubsan)

if(APPLE)
  set(UBSAN_COMMON_SOURCES ${UBSAN_SOURCES})
  if(SANITIZER_CAN_USE_CXXABI)
    list(APPEND UBSAN_COMMON_SOURCES ${UBSAN_CXXABI_SOURCES})
  endif()

  # Common parts of UBSan runtime.
  add_compiler_rt_object_libraries(RTUbsan
    OS ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${UBSAN_COMMON_SUPPORTED_ARCH}
    SOURCES ${UBSAN_COMMON_SOURCES}
    ADDITIONAL_HEADERS ${UBSAN_HEADERS}
    CFLAGS ${UBSAN_CXXFLAGS})

  if(COMPILER_RT_HAS_UBSAN)
    # Initializer of standalone UBSan runtime.
    add_compiler_rt_object_libraries(RTUbsan_standalone
      OS ${UBSAN_SUPPORTED_OS}
      ARCHS ${UBSAN_SUPPORTED_ARCH}
      SOURCES ${UBSAN_STANDALONE_SOURCES}
      ADDITIONAL_HEADERS ${UBSAN_HEADERS}
      CFLAGS ${UBSAN_STANDALONE_CFLAGS})

    add_weak_symbols("ubsan" WEAK_SYMBOL_LINK_FLAGS)
    add_weak_symbols("sanitizer_common" WEAK_SYMBOL_LINK_FLAGS)

    add_compiler_rt_runtime(clang_rt.ubsan
      SHARED
      OS ${UBSAN_SUPPORTED_OS}
      ARCHS ${UBSAN_SUPPORTED_ARCH}
      OBJECT_LIBS RTUbsan
                  RTUbsan_standalone
                  RTSanitizerCommon
                  RTSanitizerCommonLibc
                  RTSanitizerCommonCoverage
                  RTSanitizerCommonSymbolizer
                  RTInterception
      LINK_FLAGS ${WEAK_SYMBOL_LINK_FLAGS}
      PARENT_TARGET ubsan)

    if (NOT APPLE)
      add_compiler_rt_runtime(clang_rt.ubsan
        STATIC
        OS ${UBSAN_SUPPORTED_OS}
        ARCHS ${UBSAN_SUPPORTED_ARCH}
        OBJECT_LIBS RTUbsan
                    RTUbsan_standalone
                    RTSanitizerCommonNoHooks
                    RTSanitizerCommonLibcNoHooks
                    RTSanitizerCommonCoverage
                    RTSanitizerCommonSymbolizerNoHooks
                    RTInterception
        LINK_FLAGS ${WEAK_SYMBOL_LINK_FLAGS}
        PARENT_TARGET ubsan)
    endif()
  endif()

else()
  # Common parts of UBSan runtime.
  add_compiler_rt_object_libraries(RTUbsan
    ARCHS ${UBSAN_COMMON_SUPPORTED_ARCH}
    SOURCES ${UBSAN_SOURCES}
    ADDITIONAL_HEADERS ${UBSAN_HEADERS}
    CFLAGS ${UBSAN_CFLAGS})

  if(SANITIZER_CAN_USE_CXXABI)
    # C++-specific parts of UBSan runtime. Requires a C++ ABI library.
    set(UBSAN_CXX_SOURCES ${UBSAN_CXXABI_SOURCES})
  else()
    # Dummy target if we don't have C++ ABI library.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/cxx_dummy.cpp "")
    set(UBSAN_CXX_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/cxx_dummy.cpp)
  endif()

  add_compiler_rt_object_libraries(RTUbsan_cxx
    ARCHS ${UBSAN_COMMON_SUPPORTED_ARCH}
    SOURCES ${UBSAN_CXX_SOURCES}
    ADDITIONAL_HEADERS ${UBSAN_HEADERS}
    CFLAGS ${UBSAN_CXXFLAGS})

  if (WIN32)
    add_compiler_rt_object_libraries(UbsanWeakInterception
      ${SANITIZER_COMMON_SUPPORTED_OS}
      ARCHS ${UBSAN_SUPPORTED_ARCH}
      SOURCES
        ubsan_win_weak_interception.cpp
      CFLAGS ${UBSAN_CFLAGS} -DSANITIZER_DYNAMIC
      DEFS ${UBSAN_COMMON_DEFINITIONS})

    add_compiler_rt_object_libraries(UbsanDllThunk
      ${SANITIZER_COMMON_SUPPORTED_OS}
      ARCHS ${UBSAN_SUPPORTED_ARCH}
      SOURCES
        ubsan_win_dll_thunk.cpp
      CFLAGS ${UBSAN_CFLAGS} -DSANITIZER_DLL_THUNK
      DEFS ${UBSAN_COMMON_DEFINITIONS})

    set(DYNAMIC_RUNTIME_THUNK_CFLAGS "-DSANITIZER_DYNAMIC_RUNTIME_THUNK")
    if(MSVC)
      list(APPEND DYNAMIC_RUNTIME_THUNK_CFLAGS "-Zl")
    elseif(CMAKE_C_COMPILER_ID MATCHES Clang)
      list(APPEND DYNAMIC_RUNTIME_THUNK_CFLAGS "-nodefaultlibs")
    endif()
    add_compiler_rt_object_libraries(UbsanDynamicRuntimeThunk
      ${SANITIZER_COMMON_SUPPORTED_OS}
      ARCHS ${UBSAN_SUPPORTED_ARCH}
      SOURCES
        ubsan_win_dynamic_runtime_thunk.cpp
      CFLAGS ${UBSAN_CFLAGS} ${DYNAMIC_RUNTIME_THUNK_CFLAGS}
      DEFS ${UBSAN_COMMON_DEFINITIONS})
  endif()

  if(COMPILER_RT_HAS_UBSAN)
    add_compiler_rt_object_libraries(RTUbsan_standalone
      ARCHS ${UBSAN_SUPPORTED_ARCH}
      SOURCES ${UBSAN_STANDALONE_SOURCES}
      ADDITIONAL_HEADERS ${UBSAN_HEADERS}
      CFLAGS ${UBSAN_STANDALONE_CFLAGS})

    # Standalone UBSan runtimes.
    add_compiler_rt_runtime(clang_rt.ubsan_standalone
      STATIC
      ARCHS ${UBSAN_SUPPORTED_ARCH}
      SOURCES
        ubsan_init_standalone_preinit.cpp
      ADDITIONAL_HEADERS ${UBSAN_HEADERS}
      OBJECT_LIBS RTSanitizerCommon
              RTSanitizerCommonLibc
              RTSanitizerCommonCoverage
              RTSanitizerCommonSymbolizer
              RTSanitizerCommonSymbolizerInternal
              RTUbsan
              RTUbsan_standalone
              RTInterception
      CFLAGS ${UBSAN_CFLAGS}
      PARENT_TARGET ubsan)

    add_compiler_rt_runtime(clang_rt.ubsan_standalone_cxx
      STATIC
      ARCHS ${UBSAN_SUPPORTED_ARCH}
      OBJECT_LIBS RTUbsan_cxx
      CFLAGS ${UBSAN_CXXFLAGS}
      PARENT_TARGET ubsan)

    if (COMPILER_RT_HAS_VERSION_SCRIPT)
      file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
      add_compiler_rt_object_libraries(RTUbsan_dynamic_version_script_dummy
        ARCHS ${UBSAN_SUPPORTED_ARCH}
        SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
        CFLAGS ${UBSAN_CFLAGS})

      foreach(arch ${UBSAN_SUPPORTED_ARCH})
        add_sanitizer_rt_version_list(clang_rt.ubsan_standalone-dynamic-${arch}
                                      LIBS clang_rt.ubsan_standalone-${arch}
                                           clang_rt.ubsan_standalone_cxx-${arch}
                                      EXTRA ubsan.syms.extra)
        set(VERSION_SCRIPT_FLAG
            -Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/clang_rt.ubsan_standalone-dynamic-${arch}.vers)
        # The Solaris 11.4 linker supports a subset of GNU ld version scripts,
        # but requires a special option to enable it.
        if (COMPILER_RT_HAS_GNU_VERSION_SCRIPT_COMPAT)
            list(APPEND VERSION_SCRIPT_FLAG -Wl,-z,gnu-version-script-compat)
        endif()
        set_property(SOURCE
          ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
          APPEND PROPERTY
          OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/clang_rt.ubsan_standalone-dynamic-${arch}.vers)

        add_compiler_rt_runtime(clang_rt.ubsan_standalone
          SHARED
          ARCHS ${arch}
          OBJECT_LIBS RTSanitizerCommon
                RTSanitizerCommonLibc
                RTSanitizerCommonCoverage
                RTSanitizerCommonSymbolizer
                RTSanitizerCommonSymbolizerInternal
                RTUbsan
                RTUbsan_cxx
                RTUbsan_standalone
                RTInterception
                RTUbsan_dynamic_version_script_dummy
          CFLAGS ${UBSAN_CFLAGS}
          LINK_FLAGS ${UBSAN_LINK_FLAGS} ${VERSION_SCRIPT_FLAG}
          LINK_LIBS ${UBSAN_DYNAMIC_LIBS}
          PARENT_TARGET ubsan)
      endforeach()

      set(ARCHS_FOR_SYMBOLS ${UBSAN_SUPPORTED_ARCH})
      list(REMOVE_ITEM ARCHS_FOR_SYMBOLS i386)
      add_sanitizer_rt_symbols(clang_rt.ubsan_standalone
        ARCHS ${ARCHS_FOR_SYMBOLS}
        PARENT_TARGET ubsan
        EXTRA ubsan.syms.extra)
      add_sanitizer_rt_symbols(clang_rt.ubsan_standalone_cxx
        ARCHS ${ARCHS_FOR_SYMBOLS}
        PARENT_TARGET ubsan
        EXTRA ubsan.syms.extra)
    endif()
  endif()
endif()
