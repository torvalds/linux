# Build system for the common Sanitizer runtime support library components.
# These components are shared between AddressSanitizer and ThreadSanitizer.

set(SANITIZER_SOURCES_NOTERMINATION
  sanitizer_allocator.cpp
  sanitizer_common.cpp
  sanitizer_deadlock_detector1.cpp
  sanitizer_deadlock_detector2.cpp
  sanitizer_errno.cpp
  sanitizer_file.cpp
  sanitizer_flags.cpp
  sanitizer_flag_parser.cpp
  sanitizer_fuchsia.cpp
  sanitizer_libc.cpp
  sanitizer_libignore.cpp
  sanitizer_linux.cpp
  sanitizer_linux_s390.cpp
  sanitizer_mac.cpp
  sanitizer_mutex.cpp
  sanitizer_netbsd.cpp
  sanitizer_platform_limits_freebsd.cpp
  sanitizer_platform_limits_linux.cpp
  sanitizer_platform_limits_netbsd.cpp
  sanitizer_platform_limits_posix.cpp
  sanitizer_platform_limits_solaris.cpp
  sanitizer_posix.cpp
  sanitizer_printf.cpp
  sanitizer_procmaps_common.cpp
  sanitizer_procmaps_bsd.cpp
  sanitizer_procmaps_fuchsia.cpp
  sanitizer_procmaps_linux.cpp
  sanitizer_procmaps_mac.cpp
  sanitizer_procmaps_solaris.cpp
  sanitizer_range.cpp
  sanitizer_solaris.cpp
  sanitizer_stoptheworld_fuchsia.cpp
  sanitizer_stoptheworld_mac.cpp
  sanitizer_stoptheworld_win.cpp
  sanitizer_suppressions.cpp
  sanitizer_tls_get_addr.cpp
  sanitizer_thread_arg_retval.cpp
  sanitizer_thread_registry.cpp
  sanitizer_type_traits.cpp
  sanitizer_win.cpp
  )

set(SANITIZER_SOURCES
  ${SANITIZER_SOURCES_NOTERMINATION}
  sanitizer_termination.cpp
  )

# Libc functions stubs. These sources should be linked instead of
# SANITIZER_LIBCDEP_SOURCES when sanitizer_common library must not depend on
# libc.
set(SANITIZER_NOLIBC_SOURCES
  sanitizer_common_nolibc.cpp
  )

set(SANITIZER_LIBCDEP_SOURCES
  sanitizer_common_libcdep.cpp
  sanitizer_allocator_checks.cpp
  sanitizer_dl.cpp
  sanitizer_linux_libcdep.cpp
  sanitizer_mac_libcdep.cpp
  sanitizer_posix_libcdep.cpp
  sanitizer_stoptheworld_linux_libcdep.cpp
  sanitizer_stoptheworld_netbsd_libcdep.cpp
  )

set(SANITIZER_COVERAGE_SOURCES
  sancov_flags.cpp
  sanitizer_coverage_fuchsia.cpp
  sanitizer_coverage_libcdep_new.cpp
  sanitizer_coverage_win_sections.cpp
  )

set(SANITIZER_SYMBOLIZER_SOURCES
  sanitizer_allocator_report.cpp
  sanitizer_chained_origin_depot.cpp
  sanitizer_stack_store.cpp
  sanitizer_stackdepot.cpp
  sanitizer_stacktrace.cpp
  sanitizer_stacktrace_libcdep.cpp
  sanitizer_stacktrace_printer.cpp
  sanitizer_stacktrace_sparc.cpp
  sanitizer_symbolizer.cpp
  sanitizer_symbolizer_libbacktrace.cpp
  sanitizer_symbolizer_libcdep.cpp
  sanitizer_symbolizer_mac.cpp
  sanitizer_symbolizer_markup.cpp
  sanitizer_symbolizer_markup_fuchsia.cpp
  sanitizer_symbolizer_posix_libcdep.cpp
  sanitizer_symbolizer_report.cpp
  sanitizer_symbolizer_report_fuchsia.cpp
  sanitizer_symbolizer_win.cpp
  sanitizer_unwind_linux_libcdep.cpp
  sanitizer_unwind_fuchsia.cpp
  sanitizer_unwind_win.cpp
  )

# Explicitly list all sanitizer_common headers. Not all of these are
# included in sanitizer_common source files, but we need to depend on
# headers when building our custom unit tests.
set(SANITIZER_IMPL_HEADERS
  sancov_flags.h
  sancov_flags.inc
  sanitizer_addrhashmap.h
  sanitizer_allocator.h
  sanitizer_allocator_checks.h
  sanitizer_allocator_combined.h
  sanitizer_allocator_dlsym.h
  sanitizer_allocator_interface.h
  sanitizer_allocator_internal.h
  sanitizer_allocator_local_cache.h
  sanitizer_allocator_primary32.h
  sanitizer_allocator_primary64.h
  sanitizer_allocator_report.h
  sanitizer_allocator_secondary.h
  sanitizer_allocator_size_class_map.h
  sanitizer_allocator_stats.h
  sanitizer_array_ref.h
  sanitizer_asm.h
  sanitizer_atomic.h
  sanitizer_atomic_clang.h
  sanitizer_atomic_msvc.h
  sanitizer_bitvector.h
  sanitizer_bvgraph.h
  sanitizer_chained_origin_depot.h
  sanitizer_common.h
  sanitizer_common_interceptors.inc
  sanitizer_common_interceptors_format.inc
  sanitizer_common_interceptors_ioctl.inc
  sanitizer_common_interceptors_memintrinsics.inc
  sanitizer_common_interface.inc
  sanitizer_common_interface_posix.inc
  sanitizer_common_syscalls.inc
  sanitizer_coverage_interface.inc
  sanitizer_dbghelp.h
  sanitizer_deadlock_detector.h
  sanitizer_deadlock_detector_interface.h
  sanitizer_dense_map.h
  sanitizer_dense_map_info.h
  sanitizer_dl.h
  sanitizer_errno.h
  sanitizer_errno_codes.h
  sanitizer_file.h
  sanitizer_flag_parser.h
  sanitizer_flags.h
  sanitizer_flags.inc
  sanitizer_flat_map.h
  sanitizer_fuchsia.h
  sanitizer_getauxval.h
  sanitizer_hash.h
  sanitizer_interceptors_ioctl_netbsd.inc
  sanitizer_interface_internal.h
  sanitizer_internal_defs.h
  sanitizer_leb128.h
  sanitizer_lfstack.h
  sanitizer_libc.h
  sanitizer_libignore.h
  sanitizer_linux.h
  sanitizer_list.h
  sanitizer_local_address_space_view.h
  sanitizer_lzw.h
  sanitizer_mac.h
  sanitizer_malloc_mac.inc
  sanitizer_mutex.h
  sanitizer_placement_new.h
  sanitizer_platform.h
  sanitizer_platform_interceptors.h
  sanitizer_platform_limits_netbsd.h
  sanitizer_platform_limits_posix.h
  sanitizer_platform_limits_solaris.h
  sanitizer_posix.h
  sanitizer_procmaps.h
  sanitizer_ptrauth.h
  sanitizer_quarantine.h
  sanitizer_range.h
  sanitizer_redefine_builtins.h
  sanitizer_report_decorator.h
  sanitizer_ring_buffer.h
  sanitizer_signal_interceptors.inc
  sanitizer_stack_store.h
  sanitizer_stackdepot.h
  sanitizer_stackdepotbase.h
  sanitizer_stacktrace.h
  sanitizer_stacktrace_printer.h
  sanitizer_stoptheworld.h
  sanitizer_suppressions.h
  sanitizer_symbolizer.h
  sanitizer_symbolizer_markup_constants.h
  sanitizer_symbolizer_internal.h
  sanitizer_symbolizer_libbacktrace.h
  sanitizer_symbolizer_mac.h
  sanitizer_symbolizer_markup.h
  sanitizer_syscall_generic.inc
  sanitizer_syscall_linux_aarch64.inc
  sanitizer_syscall_linux_arm.inc
  sanitizer_syscall_linux_x86_64.inc
  sanitizer_syscall_linux_riscv64.inc
  sanitizer_syscall_linux_loongarch64.inc
  sanitizer_syscalls_netbsd.inc
  sanitizer_thread_registry.h
  sanitizer_thread_safety.h
  sanitizer_tls_get_addr.h
  sanitizer_vector.h
  sanitizer_win.h
  sanitizer_win_defs.h
  sanitizer_win_dll_thunk.h
  sanitizer_win_weak_interception.h
  )

include_directories(..)

set(SANITIZER_COMMON_DEFINITIONS
  HAVE_RPC_XDR_H=${HAVE_RPC_XDR_H})

# note: L not I, this is nodefaultlibs for msvc
append_list_if(MSVC /Zl SANITIZER_COMMON_CFLAGS)
set(SANITIZER_CFLAGS ${SANITIZER_COMMON_CFLAGS})

# Too many existing bugs, needs cleanup.
append_list_if(COMPILER_RT_HAS_WNO_FORMAT -Wno-format SANITIZER_CFLAGS)

append_rtti_flag(OFF SANITIZER_CFLAGS)

append_list_if(SANITIZER_LIMIT_FRAME_SIZE -Wframe-larger-than=570
               SANITIZER_CFLAGS)
append_list_if(COMPILER_RT_HAS_WGLOBAL_CONSTRUCTORS_FLAG -Wglobal-constructors
               SANITIZER_CFLAGS)

if(APPLE)
  set(OS_OPTION OS ${SANITIZER_COMMON_SUPPORTED_OS})
endif()

add_compiler_rt_object_libraries(RTSanitizerCommon
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_SOURCES}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})
add_compiler_rt_object_libraries(RTSanitizerCommonNoTermination
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_SOURCES_NOTERMINATION}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})
add_compiler_rt_object_libraries(RTSanitizerCommonNoLibc
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_NOLIBC_SOURCES}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})
add_compiler_rt_object_libraries(RTSanitizerCommonLibc
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_LIBCDEP_SOURCES}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})
add_compiler_rt_object_libraries(RTSanitizerCommonCoverage
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_COVERAGE_SOURCES}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})
add_compiler_rt_object_libraries(RTSanitizerCommonSymbolizer
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_SYMBOLIZER_SOURCES}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})

set(SANITIZER_NO_WEAK_HOOKS_CFLAGS ${SANITIZER_CFLAGS})
list(APPEND SANITIZER_NO_WEAK_HOOKS_CFLAGS "-DSANITIZER_SUPPORTS_WEAK_HOOKS=0")
add_compiler_rt_object_libraries(RTSanitizerCommonNoHooks
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_SOURCES}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_NO_WEAK_HOOKS_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})
add_compiler_rt_object_libraries(RTSanitizerCommonLibcNoHooks
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_LIBCDEP_SOURCES}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_NO_WEAK_HOOKS_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})
add_compiler_rt_object_libraries(RTSanitizerCommonSymbolizerNoHooks
  ${OS_OPTION}
  ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
  SOURCES ${SANITIZER_SYMBOLIZER_SOURCES}
  ADDITIONAL_HEADERS ${SANITIZER_IMPL_HEADERS}
  CFLAGS ${SANITIZER_NO_WEAK_HOOKS_CFLAGS}
  DEFS ${SANITIZER_COMMON_DEFINITIONS})

if(WIN32)
  add_compiler_rt_object_libraries(SanitizerCommonWeakInterception
    ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
    SOURCES
      sanitizer_win_weak_interception.cpp
    CFLAGS ${SANITIZER_CFLAGS} -DSANITIZER_DYNAMIC
    DEFS ${SANITIZER_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(SancovWeakInterception
    ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
    SOURCES
      sanitizer_coverage_win_weak_interception.cpp
    CFLAGS ${SANITIZER_CFLAGS} -DSANITIZER_DYNAMIC
    DEFS ${SANITIZER_COMMON_DEFINITIONS})

  add_compiler_rt_object_libraries(SanitizerCommonDllThunk
    ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
    SOURCES
      sanitizer_win_dll_thunk.cpp
    CFLAGS ${SANITIZER_CFLAGS} -DSANITIZER_DLL_THUNK
    DEFS ${SANITIZER_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(SancovDllThunk
    ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
    SOURCES
      sanitizer_coverage_win_dll_thunk.cpp
      sanitizer_coverage_win_sections.cpp
    CFLAGS ${SANITIZER_CFLAGS} -DSANITIZER_DLL_THUNK
    DEFS ${SANITIZER_COMMON_DEFINITIONS})

  set(DYNAMIC_RUNTIME_THUNK_CFLAGS "-DSANITIZER_DYNAMIC_RUNTIME_THUNK")
  if(MSVC)
    list(APPEND DYNAMIC_RUNTIME_THUNK_CFLAGS "-Zl")
  elseif(CMAKE_C_COMPILER_ID MATCHES Clang)
    list(APPEND DYNAMIC_RUNTIME_THUNK_CFLAGS "-nodefaultlibs")
  endif()
  add_compiler_rt_object_libraries(SanitizerCommonDynamicRuntimeThunk
    ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
    SOURCES
      sanitizer_win_dynamic_runtime_thunk.cpp
    CFLAGS ${SANITIZER_CFLAGS} ${DYNAMIC_RUNTIME_THUNK_CFLAGS}
    DEFS ${SANITIZER_COMMON_DEFINITIONS})
  add_compiler_rt_object_libraries(SancovDynamicRuntimeThunk
    ${SANITIZER_COMMON_SUPPORTED_OS}
    ARCHS ${SANITIZER_COMMON_SUPPORTED_ARCH}
    SOURCES
      sanitizer_coverage_win_dynamic_runtime_thunk.cpp
      sanitizer_coverage_win_sections.cpp
    CFLAGS ${SANITIZER_CFLAGS} ${DYNAMIC_RUNTIME_THUNK_CFLAGS}
    DEFS ${SANITIZER_COMMON_DEFINITIONS})
endif()

# Unit tests for common sanitizer runtime.
if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()


foreach(arch ${SANITIZER_COMMON_SUPPORTED_ARCH})
  # Library implementing InternalSymbolizer. Empty by default.
  add_library(RTSanitizerCommonSymbolizerInternal.${arch}
              OBJECT IMPORTED GLOBAL)
endforeach()

if (COMPILER_RT_ENABLE_INTERNAL_SYMBOLIZER)
  add_subdirectory(symbolizer)
endif()
