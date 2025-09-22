include_directories(../..)

set(TSAN_RTL_CFLAGS ${TSAN_CFLAGS})
append_list_if(COMPILER_RT_HAS_MSSE4_2_FLAG -msse4.2 TSAN_RTL_CFLAGS)
append_list_if(SANITIZER_LIMIT_FRAME_SIZE -Wframe-larger-than=530
               TSAN_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_WGLOBAL_CONSTRUCTORS_FLAG -Wglobal-constructors
               TSAN_RTL_CFLAGS)
append_list_if(COMPILER_RT_INTERCEPT_LIBDISPATCH ${COMPILER_RT_LIBDISPATCH_CFLAGS}
               TSAN_RTL_CFLAGS)

set(TSAN_RTL_DYNAMIC_CFLAGS ${TSAN_RTL_CFLAGS})
list(REMOVE_ITEM TSAN_RTL_DYNAMIC_CFLAGS -fPIE)

set(TSAN_DYNAMIC_LINK_LIBS
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_CXX_ABI_LIBRARIES}
  ${SANITIZER_COMMON_LINK_LIBS})

append_list_if(COMPILER_RT_HAS_LIBDL dl TSAN_DYNAMIC_LINK_LIBS)
append_list_if(COMPILER_RT_HAS_LIBM m TSAN_DYNAMIC_LINK_LIBS)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD pthread TSAN_DYNAMIC_LINK_LIBS)

set(TSAN_SOURCES
  tsan_debugging.cpp
  tsan_external.cpp
  tsan_fd.cpp
  tsan_flags.cpp
  tsan_ignoreset.cpp
  tsan_interceptors_memintrinsics.cpp
  tsan_interceptors_posix.cpp
  tsan_interface.cpp
  tsan_interface_ann.cpp
  tsan_interface_atomic.cpp
  tsan_interface_java.cpp
  tsan_malloc_mac.cpp
  tsan_md5.cpp
  tsan_mman.cpp
  tsan_mutexset.cpp
  tsan_report.cpp
  tsan_rtl.cpp
  tsan_rtl_access.cpp
  tsan_rtl_mutex.cpp
  tsan_rtl_proc.cpp
  tsan_rtl_report.cpp
  tsan_rtl_thread.cpp
  tsan_stack_trace.cpp
  tsan_suppressions.cpp
  tsan_symbolize.cpp
  tsan_sync.cpp
  tsan_vector_clock.cpp
  )

set(TSAN_CXX_SOURCES
  tsan_new_delete.cpp
  )

set(TSAN_PREINIT_SOURCES
  tsan_preinit.cpp
  )

if(APPLE)
  list(APPEND TSAN_SOURCES
    tsan_interceptors_mac.cpp
    tsan_interceptors_mach_vm.cpp
    tsan_platform_mac.cpp
    tsan_platform_posix.cpp
    )
elseif(UNIX)
  # Assume Linux
  list(APPEND TSAN_SOURCES
    tsan_platform_linux.cpp
    tsan_platform_posix.cpp
    )
endif()

if(COMPILER_RT_INTERCEPT_LIBDISPATCH)
  list(APPEND TSAN_SOURCES
    tsan_interceptors_libdispatch.cpp
    )
endif()

set(TSAN_HEADERS
  tsan_defs.h
  tsan_dense_alloc.h
  tsan_fd.h
  tsan_flags.h
  tsan_flags.inc
  tsan_ignoreset.h
  tsan_ilist.h
  tsan_interceptors.h
  tsan_interface.h
  tsan_interface.inc
  tsan_interface_ann.h
  tsan_interface_java.h
  tsan_mman.h
  tsan_mutexset.h
  tsan_platform.h
  tsan_ppc_regs.h
  tsan_report.h
  tsan_rtl.h
  tsan_shadow.h
  tsan_stack_trace.h
  tsan_suppressions.h
  tsan_symbolize.h
  tsan_sync.h
  tsan_trace.h
  tsan_vector_clock.h
  )

set(TSAN_RUNTIME_LIBRARIES)
add_compiler_rt_component(tsan)

if("${CMAKE_C_FLAGS}" MATCHES "-Wno-(error=)?unused-command-line-argument")
  set(EXTRA_CFLAGS "-Wno-error=unused-command-line-argument ${EXTRA_CFLAGS}")
endif()

if(APPLE)
  # Ideally we would check the SDK version for the actual platform we are
  # building for here.  To make our lifes easier we assume the host SDK setup is
  # sane and use the macOS SDK version as a proxy for aligned SDKs.
  find_darwin_sdk_version(macosx_sdk_version "macosx")
  if ("${macosx_sdk_version}" VERSION_LESS 10.12)
    message(FATAL_ERROR "Building the TSan runtime requires at least macOS SDK 10.12 (or aligned SDK on other platforms)")
  endif()

  set(TSAN_ASM_SOURCES
    tsan_rtl_amd64.S
    tsan_rtl_aarch64.S
    )

  set(TSAN_LINK_LIBS ${SANITIZER_COMMON_LINK_LIBS})

  add_weak_symbols("ubsan" WEAK_SYMBOL_LINK_FLAGS)
  add_weak_symbols("sanitizer_common" WEAK_SYMBOL_LINK_FLAGS)

  add_compiler_rt_runtime(clang_rt.tsan
    SHARED
    OS ${TSAN_SUPPORTED_OS}
    ARCHS ${TSAN_SUPPORTED_ARCH}
    SOURCES ${TSAN_SOURCES} ${TSAN_CXX_SOURCES} ${TSAN_ASM_SOURCES}
    ADDITIONAL_HEADERS ${TSAN_HEADERS}
    OBJECT_LIBS RTInterception
                RTSanitizerCommon
                RTSanitizerCommonLibc
                RTSanitizerCommonCoverage
                RTSanitizerCommonSymbolizer
                RTUbsan
    CFLAGS ${TSAN_RTL_CFLAGS}
    LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS} ${WEAK_SYMBOL_LINK_FLAGS}
    LINK_LIBS ${TSAN_LINK_LIBS} objc
    PARENT_TARGET tsan)
  add_compiler_rt_object_libraries(RTTsan_dynamic
    OS ${TSAN_SUPPORTED_OS}
    ARCHS ${TSAN_SUPPORTED_ARCH}
    SOURCES ${TSAN_SOURCES} ${TSAN_CXX_SOURCES} ${TSAN_ASM_SOURCES}
    ADDITIONAL_HEADERS ${TSAN_HEADERS}
    CFLAGS ${TSAN_RTL_CFLAGS})

  # Build and check Go runtime.
  set(BUILDGO_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../go/buildgo.sh)
  add_custom_target(GotsanRuntimeCheck
    COMMAND env "CC=${CMAKE_C_COMPILER} ${OSX_SYSROOT_FLAG}"
            EXTRA_CFLAGS=${EXTRA_CFLAGS}
            IN_TMPDIR=1 SILENT=1 ${BUILDGO_SCRIPT}
    DEPENDS tsan ${BUILDGO_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../go
    COMMENT "Checking TSan Go runtime..."
    VERBATIM)
  set_target_properties(GotsanRuntimeCheck PROPERTIES FOLDER "Compiler-RT/Misc")
else()
  foreach(arch ${TSAN_SUPPORTED_ARCH})
    if(arch STREQUAL "x86_64")
      set(TSAN_ASM_SOURCES
        tsan_rtl_amd64.S
        )
      # Check for Go runtime.
      set(BUILDGO_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../go/buildgo.sh)
      add_custom_target(GotsanRuntimeCheck
        COMMAND env "CC=${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1}"
                EXTRA_CFLAGS=${EXTRA_CFLAGS}
                IN_TMPDIR=1 SILENT=1 ${BUILDGO_SCRIPT}
        DEPENDS clang_rt.tsan-${arch} ${BUILDGO_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../go
        COMMENT "Checking TSan Go runtime..."
        VERBATIM)
    elseif(arch STREQUAL "aarch64")
      set(TSAN_ASM_SOURCES
        tsan_rtl_aarch64.S
        )
      # Check for Go runtime.
      set(BUILDGO_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../go/buildgo.sh)
      add_custom_target(GotsanRuntimeCheck
	COMMAND env "CC=${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1}"
		EXTRA_CFLAGS=${EXTRA_CFLAGS}
		IN_TMPDIR=1 SILENT=1 ${BUILDGO_SCRIPT}
	DEPENDS clang_rt.tsan-${arch} ${BUILDGO_SCRIPT}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../go
	COMMENT "Checking TSan Go runtime..."
	VERBATIM)
    elseif(arch MATCHES "powerpc64|powerpc64le")
      set(TSAN_ASM_SOURCES
        tsan_rtl_ppc64.S
        )
      # Check for Go runtime.
      set(BUILDGO_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../go/buildgo.sh)
      add_custom_target(GotsanRuntimeCheck
	COMMAND env "CC=${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1}"
		EXTRA_CFLAGS=${EXTRA_CFLAGS}
		IN_TMPDIR=1 SILENT=1 ${BUILDGO_SCRIPT}
	DEPENDS clang_rt.tsan-${arch} ${BUILDGO_SCRIPT}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../go
	COMMENT "Checking TSan Go runtime..."
	VERBATIM)
    elseif(arch MATCHES "loongarch64")
      set(TSAN_ASM_SOURCES
        tsan_rtl_loongarch64.S
        )
    elseif(arch MATCHES "mips64|mips64le")
      set(TSAN_ASM_SOURCES
        tsan_rtl_mips64.S
        )
    elseif(arch MATCHES "riscv64")
      set(TSAN_ASM_SOURCES
        tsan_rtl_riscv64.S
        )
    elseif(arch MATCHES "s390x")
      set(TSAN_ASM_SOURCES
        tsan_rtl_s390x.S
        )
      # Check for Go runtime.
      set(BUILDGO_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../go/buildgo.sh)
      add_custom_target(GotsanRuntimeCheck
	COMMAND env "CC=${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1}"
		EXTRA_CFLAGS=${EXTRA_CFLAGS}
		IN_TMPDIR=1 SILENT=1 ${BUILDGO_SCRIPT}
	DEPENDS clang_rt.tsan-${arch} ${BUILDGO_SCRIPT}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../go
	COMMENT "Checking TSan Go runtime..."
	VERBATIM)
    else()
      set(TSAN_ASM_SOURCES)
    endif()
    add_compiler_rt_runtime(clang_rt.tsan
      STATIC
      ARCHS ${arch}
      SOURCES ${TSAN_SOURCES} ${TSAN_ASM_SOURCES} ${TSAN_PREINIT_SOURCES}
              $<TARGET_OBJECTS:RTInterception.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonCoverage.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>
              $<TARGET_OBJECTS:RTUbsan.${arch}>
      ADDITIONAL_HEADERS ${TSAN_HEADERS}
      CFLAGS ${TSAN_RTL_CFLAGS}
      PARENT_TARGET tsan)
    add_compiler_rt_runtime(clang_rt.tsan_cxx
      STATIC
      ARCHS ${arch}
      SOURCES ${TSAN_CXX_SOURCES}
              $<TARGET_OBJECTS:RTUbsan_cxx.${arch}>
      ADDITIONAL_HEADERS ${TSAN_HEADERS}
      CFLAGS ${TSAN_RTL_CFLAGS}
      PARENT_TARGET tsan)
    list(APPEND TSAN_RUNTIME_LIBRARIES clang_rt.tsan-${arch}
                                       clang_rt.tsan_cxx-${arch})
    add_compiler_rt_runtime(clang_rt.tsan
      SHARED
      ARCHS ${arch}
      SOURCES ${TSAN_SOURCES} ${TSAN_ASM_SOURCES}
              $<TARGET_OBJECTS:RTInterception.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonCoverage.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
              $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>
              $<TARGET_OBJECTS:RTUbsan.${arch}>
      ADDITIONAL_HEADERS ${TSAN_HEADERS}
      CFLAGS ${TSAN_RTL_DYNAMIC_CFLAGS}
      DEFS SANITIZER_SHARED
      LINK_LIBS ${TSAN_DYNAMIC_LINK_LIBS}
      LINK_FLAGS ${SANITIZER_COMMON_LINK_FLAGS}
      PARENT_TARGET tsan)
    add_sanitizer_rt_symbols(clang_rt.tsan
      ARCHS ${arch}
      EXTRA tsan.syms.extra)
    add_sanitizer_rt_symbols(clang_rt.tsan_cxx
      ARCHS ${arch}
      EXTRA tsan.syms.extra)
    add_dependencies(tsan clang_rt.tsan-${arch}
                          clang_rt.tsan_cxx-${arch}
                          clang_rt.tsan-${arch}-symbols
                          clang_rt.tsan_cxx-${arch}-symbols)
  endforeach()
endif()


