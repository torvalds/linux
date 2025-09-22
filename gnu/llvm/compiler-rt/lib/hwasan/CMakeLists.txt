include_directories(..)

# Runtime library sources and build flags.
set(HWASAN_RTL_SOURCES
  hwasan.cpp
  hwasan_allocator.cpp
  hwasan_allocation_functions.cpp
  hwasan_dynamic_shadow.cpp
  hwasan_exceptions.cpp
  hwasan_fuchsia.cpp
  hwasan_globals.cpp
  hwasan_interceptors.cpp
  hwasan_interceptors_vfork.S
  hwasan_linux.cpp
  hwasan_memintrinsics.cpp
  hwasan_poisoning.cpp
  hwasan_report.cpp
  hwasan_thread.cpp
  hwasan_thread_list.cpp
  hwasan_type_test.cpp
  )

foreach(arch ${HWASAN_SUPPORTED_ARCH})
  if(${arch} MATCHES "aarch64")
    list(APPEND HWASAN_RTL_SOURCES
      hwasan_setjmp_aarch64.S
      hwasan_tag_mismatch_aarch64.S)
  endif()
  if(${arch} MATCHES "riscv64")
    list(APPEND HWASAN_RTL_SOURCES
      hwasan_setjmp_riscv64.S
      hwasan_tag_mismatch_riscv64.S)
  endif()
  if(${arch} MATCHES "x86_64")
    list(APPEND HWASAN_RTL_SOURCES
      hwasan_setjmp_x86_64.S)
  endif()
endforeach()

set(HWASAN_RTL_CXX_SOURCES
  hwasan_new_delete.cpp
  )

set(HWASAN_RTL_PREINIT_SOURCES
  hwasan_preinit.cpp
  )

set(HWASAN_RTL_HEADERS
  hwasan.h
  hwasan_allocator.h
  hwasan_dynamic_shadow.h
  hwasan_flags.h
  hwasan_flags.inc
  hwasan_globals.h
  hwasan_interface_internal.h
  hwasan_malloc_bisect.h
  hwasan_mapping.h
  hwasan_poisoning.h
  hwasan_report.h
  hwasan_thread.h
  hwasan_thread_list.h
  )

set(HWASAN_DEFINITIONS)
append_list_if(COMPILER_RT_HWASAN_WITH_INTERCEPTORS HWASAN_WITH_INTERCEPTORS=1 HWASAN_DEFINITIONS)

if(FUCHSIA)
  # Set this explicitly on Fuchsia, otherwise the default value is set to HWASAN_WITH_INTERCEPTORS.
  list(APPEND HWASAN_DEFINITIONS HWASAN_REPLACE_OPERATORS_NEW_AND_DELETE=1)
endif()

set(HWASAN_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF HWASAN_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FPIC_FLAG -fPIC HWASAN_RTL_CFLAGS)
# Prevent clang from generating libc calls.
append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding HWASAN_RTL_CFLAGS)

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format HWASAN_RTL_CFLAGS)

set(HWASAN_DYNAMIC_LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS})

if(ANDROID)
# Put most Sanitizer shared libraries in the global group. For more details, see
# android-changes-for-ndk-developers.md#changes-to-library-search-order
  if (COMPILER_RT_HAS_Z_GLOBAL)
    list(APPEND HWASAN_DYNAMIC_LINK_FLAGS -Wl,-z,global)
  endif()
endif()

set(HWASAN_DYNAMIC_CFLAGS ${HWASAN_RTL_CFLAGS})
append_list_if(COMPILER_RT_HAS_FTLS_MODEL_INITIAL_EXEC
  -ftls-model=initial-exec HWASAN_DYNAMIC_CFLAGS)
append_list_if(MSVC /DEBUG HWASAN_DYNAMIC_LINK_FLAGS)

set(HWASAN_DYNAMIC_LIBS
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_CXX_ABI_LIBRARIES}
  ${SANITIZER_COMMON_LINK_LIBS})

append_list_if(COMPILER_RT_HAS_LIBDL dl HWASAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBRT rt HWASAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBM m HWASAN_DYNAMIC_LIBS)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD pthread HWASAN_DYNAMIC_LIBS)

# Static runtime library.
add_compiler_rt_component(hwasan)

add_compiler_rt_object_libraries(RTHwasan
  ARCHS ${HWASAN_SUPPORTED_ARCH}
  SOURCES ${HWASAN_RTL_SOURCES}
  ADDITIONAL_HEADERS ${HWASAN_RTL_HEADERS}
  CFLAGS ${HWASAN_RTL_CFLAGS}
  DEFS ${HWASAN_DEFINITIONS})
add_compiler_rt_object_libraries(RTHwasan_cxx
  ARCHS ${HWASAN_SUPPORTED_ARCH}
  SOURCES ${HWASAN_RTL_CXX_SOURCES}
  ADDITIONAL_HEADERS ${HWASAN_RTL_HEADERS}
  CFLAGS ${HWASAN_RTL_CFLAGS}
  DEFS ${HWASAN_DEFINITIONS})
add_compiler_rt_object_libraries(RTHwasan_dynamic
  ARCHS ${HWASAN_SUPPORTED_ARCH}
  SOURCES ${HWASAN_RTL_SOURCES} ${HWASAN_RTL_CXX_SOURCES}
  ADDITIONAL_HEADERS ${HWASAN_RTL_HEADERS}
  CFLAGS ${HWASAN_DYNAMIC_CFLAGS}
  DEFS ${HWASAN_DEFINITIONS})
add_compiler_rt_object_libraries(RTHwasan_preinit
  ARCHS ${HWASAN_SUPPORTED_ARCH}
  SOURCES ${HWASAN_RTL_PREINIT_SOURCES}
  ADDITIONAL_HEADERS ${HWASAN_RTL_HEADERS}
  CFLAGS ${HWASAN_RTL_CFLAGS}
  DEFS ${HWASAN_DEFINITIONS})

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "")
add_compiler_rt_object_libraries(RTHwasan_dynamic_version_script_dummy
  ARCHS ${HWASAN_SUPPORTED_ARCH}
  SOURCES ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
  CFLAGS ${HWASAN_DYNAMIC_CFLAGS}
  DEFS ${HWASAN_DEFINITIONS})

# If use_aliases is TRUE, adds the HWASan runtime built with alias support.
# Otherwise adds the runtime without alias support.
function(add_hwasan_runtimes arch use_aliases)
  set(hwasan_object_lib RTHwasan)
  set(hwasan_object_dyn_lib RTHwasan_dynamic)
  set(hwasan_runtime clang_rt.hwasan)
  set(hwasan_rtl_flags ${HWASAN_RTL_CFLAGS})
  set(hwasan_dyn_flags ${HWASAN_DYNAMIC_CFLAGS})
  if(use_aliases)
    list(APPEND hwasan_rtl_flags -DHWASAN_ALIASING_MODE)
    list(APPEND hwasan_dyn_flags -DHWASAN_ALIASING_MODE)
    add_compiler_rt_object_libraries(RTHwasanAliases
      ARCHS ${arch}
      SOURCES ${HWASAN_RTL_SOURCES}
      ADDITIONAL_HEADERS ${HWASAN_RTL_HEADERS}
      CFLAGS ${hwasan_rtl_flags}
      DEFS ${HWASAN_DEFINITIONS})
    add_compiler_rt_object_libraries(RTHwasanAliases_dynamic
      ARCHS ${arch}
      SOURCES ${HWASAN_RTL_SOURCES} ${HWASAN_RTL_CXX_SOURCES}
      ADDITIONAL_HEADERS ${HWASAN_RTL_HEADERS}
      CFLAGS ${hwasan_dyn_flags}
      DEFS ${HWASAN_DEFINITIONS})

    set(hwasan_object_lib RTHwasanAliases)
    set(hwasan_object_dyn_lib RTHwasanAliases_dynamic)
    set(hwasan_runtime clang_rt.hwasan_aliases)
  endif()
  add_compiler_rt_runtime(${hwasan_runtime}
    STATIC
    ARCHS ${arch}
    OBJECT_LIBS ${hwasan_object_lib}
                RTHwasan_preinit
                RTInterception
                RTSanitizerCommon
                RTSanitizerCommonLibc
                RTSanitizerCommonCoverage
                RTSanitizerCommonSymbolizer
                RTSanitizerCommonSymbolizerInternal
                RTLSanCommon
                RTUbsan
    CFLAGS ${hwasan_rtl_flags}
    PARENT_TARGET hwasan)
  add_compiler_rt_runtime(${hwasan_runtime}_cxx
    STATIC
    ARCHS ${arch}
    OBJECT_LIBS RTHwasan_cxx
                RTUbsan_cxx
    CFLAGS ${hwasan_rtl_flags}
    PARENT_TARGET hwasan)

  if (UNIX)
    add_sanitizer_rt_version_list(${hwasan_runtime}-dynamic-${arch}
                                  LIBS ${hwasan_runtime}-${arch} ${hwasan_runtime}_cxx-${arch}
                                  EXTRA hwasan.syms.extra)
    set(VERSION_SCRIPT_FLAG
         -Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/${hwasan_runtime}-dynamic-${arch}.vers)
    set_property(SOURCE
      ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
      APPEND PROPERTY
      OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${hwasan_runtime}-dynamic-${arch}.vers)
  else()
    set(VERSION_SCRIPT_FLAG)
  endif()


  add_compiler_rt_runtime(${hwasan_runtime}
    SHARED
    ARCHS ${arch}
    OBJECT_LIBS
            ${hwasan_object_dyn_lib}
            RTInterception
            RTSanitizerCommon
            RTSanitizerCommonLibc
            RTSanitizerCommonCoverage
            RTSanitizerCommonSymbolizer
            RTSanitizerCommonSymbolizerInternal
            RTLSanCommon
            RTUbsan
            RTUbsan_cxx
            # The only purpose of RTHWAsan_dynamic_version_script_dummy is to
            # carry a dependency of the shared runtime on the version script.
            # Replacing it with a straightforward
            # add_dependencies(clang_rt.asan-dynamic-${arch} clang_rt.asan-dynamic-${arch}-version-list)
            # generates an order-only dependency in ninja.
            RTHwasan_dynamic_version_script_dummy
    CFLAGS ${hwasan_dyn_flags}
    LINK_FLAGS ${HWASAN_DYNAMIC_LINK_FLAGS}
              ${VERSION_SCRIPT_FLAG}
    LINK_LIBS ${HWASAN_DYNAMIC_LIBS}
    DEFS ${ASAN_DYNAMIC_DEFINITIONS}
    PARENT_TARGET hwasan)

  if(SANITIZER_USE_SYMBOLS)
    add_sanitizer_rt_symbols(${hwasan_runtime}
      ARCHS ${arch}
      EXTRA hwasan.syms.extra)
    add_sanitizer_rt_symbols(${hwasan_runtime}_cxx
      ARCHS ${arch}
      EXTRA hwasan.syms.extra)
    add_dependencies(hwasan ${hwasan_runtime}-${arch}-symbols
                          ${hwasan_runtime}_cxx-${arch}-symbols)
  endif()
endfunction()

foreach(arch ${HWASAN_SUPPORTED_ARCH})
  add_hwasan_runtimes(${arch} FALSE)
  if(${arch} MATCHES "x86_64")
    add_hwasan_runtimes(${arch} TRUE)
  endif()
endforeach()

add_compiler_rt_runtime(clang_rt.hwasan-preinit
  STATIC
  ARCHS ${HWASAN_SUPPORTED_ARCH}
  OBJECT_LIBS RTHwasan_preinit
  CFLAGS ${HWASAN_RTL_CFLAGS}
  PARENT_TARGET hwasan)

add_compiler_rt_resource_file(hwasan_ignorelist hwasan_ignorelist.txt hwasan)

add_subdirectory("scripts")

# if(COMPILER_RT_INCLUDE_TESTS)
#   add_subdirectory(tests)
# endif()
