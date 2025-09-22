include(CompilerRTCompile)

clang_compiler_add_cxx_check()

# FIXME: use SANITIZER_COMMON_SUPPORTED_ARCH here
filter_available_targets(SANITIZER_UNITTEST_SUPPORTED_ARCH x86_64 i386 mips64 mips64el riscv64 sparcv9 sparc)
if(APPLE)
  list(APPEND SANITIZER_UNITTEST_SUPPORTED_ARCH arm64)
  darwin_filter_host_archs(SANITIZER_UNITTEST_SUPPORTED_ARCH SANITIZER_UNITTEST_SUPPORTED_ARCH)
endif()

set(SANITIZER_UNITTESTS
  sanitizer_addrhashmap_test.cpp
  sanitizer_allocator_test.cpp
  sanitizer_array_ref_test.cpp
  sanitizer_atomic_test.cpp
  sanitizer_bitvector_test.cpp
  sanitizer_block_signals.cpp
  sanitizer_bvgraph_test.cpp
  sanitizer_chained_origin_depot_test.cpp
  sanitizer_common_test.cpp
  sanitizer_deadlock_detector_test.cpp
  sanitizer_dense_map_test.cpp
  sanitizer_flags_test.cpp
  sanitizer_flat_map_test.cpp
  sanitizer_format_interceptor_test.cpp
  sanitizer_hash_test.cpp
  sanitizer_ioctl_test.cpp
  sanitizer_leb128_test.cpp
  sanitizer_libc_test.cpp
  sanitizer_linux_test.cpp
  sanitizer_list_test.cpp
  sanitizer_lzw_test.cpp
  sanitizer_mac_test.cpp
  sanitizer_module_uuid_size.cpp
  sanitizer_mutex_test.cpp
  sanitizer_nolibc_test.cpp
  sanitizer_posix_test.cpp
  sanitizer_printf_test.cpp
  sanitizer_procmaps_test.cpp
  sanitizer_procmaps_mac_test.cpp
  sanitizer_range_test.cpp
  sanitizer_ring_buffer_test.cpp
  sanitizer_quarantine_test.cpp
  sanitizer_stack_store_test.cpp
  sanitizer_stackdepot_test.cpp
  sanitizer_stacktrace_printer_test.cpp
  sanitizer_stacktrace_test.cpp
  sanitizer_stoptheworld_test.cpp
  sanitizer_suppressions_test.cpp
  sanitizer_symbolizer_test.cpp
  sanitizer_test_main.cpp
  sanitizer_thread_arg_retval_test.cpp
  sanitizer_thread_registry_test.cpp
  sanitizer_type_traits_test.cpp
  sanitizer_vector_test.cpp
  )

set(SANITIZER_TEST_HEADERS
  sanitizer_pthread_wrappers.h
  sanitizer_test_config.h
  sanitizer_test_utils.h
  )
foreach(header ${SANITIZER_IMPL_HEADERS})
  list(APPEND SANITIZER_TEST_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../${header})
endforeach()

set(SANITIZER_TEST_CFLAGS_COMMON
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  ${COMPILER_RT_GMOCK_CFLAGS}
  ${SANITIZER_TEST_CXX_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/include
  -I${COMPILER_RT_SOURCE_DIR}/lib
  -I${COMPILER_RT_SOURCE_DIR}/lib/sanitizer_common
  -DSANITIZER_COMMON_NO_REDEFINE_BUILTINS
  -fno-rtti
  -O2
  -Werror=sign-compare
  -Wno-gnu-zero-variadic-macro-arguments
  )

set(SANITIZER_TEST_LINK_FLAGS_COMMON
  ${COMPILER_RT_UNITTEST_LINK_FLAGS}
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_TEST_CXX_LIBRARIES})

# -gline-tables-only must be enough for these tests, so use it if possible.
if(COMPILER_RT_TEST_COMPILER_ID MATCHES "Clang")
  list(APPEND SANITIZER_TEST_CFLAGS_COMMON -gline-tables-only)
else()
  list(APPEND SANITIZER_TEST_CFLAGS_COMMON -g)
endif()
if(MSVC)
  list(APPEND SANITIZER_TEST_CFLAGS_COMMON -gcodeview)
endif()
list(APPEND SANITIZER_TEST_LINK_FLAGS_COMMON -g)

if(NOT MSVC)
  list(APPEND SANITIZER_TEST_LINK_FLAGS_COMMON --driver-mode=g++)
endif()

if(ANDROID)
  list(APPEND SANITIZER_TEST_LINK_FLAGS_COMMON -pie)
endif()

if(APPLE)
  list(APPEND SANITIZER_TEST_CFLAGS_COMMON ${DARWIN_osx_CFLAGS})
  list(APPEND SANITIZER_TEST_LINK_FLAGS_COMMON ${DARWIN_osx_LINK_FLAGS})

  add_weak_symbols("sanitizer_common" WEAK_SYMBOL_LINK_FLAGS)
  list(APPEND SANITIZER_TEST_LINK_FLAGS_COMMON ${WEAK_SYMBOL_LINK_FLAGS})
  # For c++17 sanitizer_allocator_test requires language features introduced in macos 10.13
  list(APPEND SANITIZER_TEST_CFLAGS_COMMON "-mmacosx-version-min=10.13")
endif()

# MSVC linker is allocating 1M for the stack by default, which is not
# enough for the unittests. Some unittests require more than 2M.
# The default stack size for clang is 8M.
if(MSVC)
  list(APPEND SANITIZER_TEST_LINK_FLAGS_COMMON -Wl,/STACK:0xC00000)
elseif (MINGW)
  list(APPEND SANITIZER_TEST_LINK_FLAGS_COMMON -Wl,--stack=0xC00000)
endif()

set(SANITIZER_TEST_LINK_LIBS)
append_list_if(COMPILER_RT_HAS_LIBLOG log SANITIZER_TEST_LINK_LIBS)

append_list_if(COMPILER_RT_HAS_LIBDL -ldl SANITIZER_TEST_LINK_FLAGS_COMMON)
append_list_if(COMPILER_RT_HAS_LIBRT -lrt SANITIZER_TEST_LINK_FLAGS_COMMON)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD -pthread SANITIZER_TEST_LINK_FLAGS_COMMON)
# x86_64 FreeBSD 9.2 additionally requires libc++ to build the tests. Also,
# 'libm' shall be specified explicitly to build i386 tests.
if(CMAKE_SYSTEM MATCHES "FreeBSD-9.2-RELEASE")
  list(APPEND SANITIZER_TEST_LINK_FLAGS_COMMON "-lc++ -lm")
endif()

include_directories(..)
include_directories(../..)

# Adds static library which contains sanitizer_common object file
# (universal binary on Mac and arch-specific object files on Linux).
macro(add_sanitizer_common_lib library)
  add_library(${library} STATIC ${ARGN})
  set_target_properties(${library} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    FOLDER "Compiler-RT/Tests/Runtime")
endmacro()

function(get_sanitizer_common_lib_for_arch arch lib)
  if(APPLE)
    set(tgt_name "RTSanitizerCommon.test.osx")
  else()
    set(tgt_name "RTSanitizerCommon.test.${arch}")
  endif()
  set(${lib} "${tgt_name}" PARENT_SCOPE)
endfunction()

# Sanitizer_common unit tests testsuite.
add_custom_target(SanitizerUnitTests)
set_target_properties(SanitizerUnitTests PROPERTIES FOLDER "Compiler-RT/Tests")

# Adds sanitizer tests for architecture.
macro(add_sanitizer_tests_for_arch arch)
  set(extra_flags)
  if( CMAKE_SIZEOF_VOID_P EQUAL 4 )
    list(APPEND extra_flags "-D_LARGEFILE_SOURCE=1")
    list(APPEND extra_flags "-D_FILE_OFFSET_BITS=64")
  endif()
  get_sanitizer_common_lib_for_arch(${arch} SANITIZER_COMMON_LIB)
  set(TARGET_LINK_FLAGS)
  get_target_link_flags_for_arch(${arch} TARGET_LINK_FLAGS)

  set(SANITIZER_TEST_OBJECTS)
  generate_compiler_rt_tests(SANITIZER_TEST_OBJECTS SanitizerUnitTests
    "Sanitizer-${arch}-Test" ${arch}
    RUNTIME "${SANITIZER_COMMON_LIB}"
    SOURCES ${SANITIZER_UNITTESTS} ${COMPILER_RT_GTEST_SOURCE} ${COMPILER_RT_GMOCK_SOURCE}
    COMPILE_DEPS ${SANITIZER_TEST_HEADERS}
    CFLAGS  ${SANITIZER_TEST_CFLAGS_COMMON} ${extra_flags}
    LINK_FLAGS ${SANITIZER_TEST_LINK_FLAGS_COMMON} ${TARGET_LINK_FLAGS} ${extra_flags})

  if("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" AND "${arch}" STREQUAL "x86_64")
    # Test that the libc-independent part of sanitizer_common is indeed
    # independent of libc, by linking this binary without libc (here) and
    # executing it (unit test in sanitizer_nolibc_test.cpp).
    get_target_flags_for_arch(${arch} TARGET_FLAGS)
    clang_compile(sanitizer_nolibc_test_main.${arch}.o
                  sanitizer_nolibc_test_main.cpp
                  CFLAGS ${SANITIZER_TEST_CFLAGS_COMMON} ${TARGET_FLAGS}
                  DEPS ${SANITIZER_TEST_COMPILE_DEPS})
    add_compiler_rt_test(SanitizerUnitTests "Sanitizer-${arch}-Test-Nolibc" ${arch}
                         OBJECTS sanitizer_nolibc_test_main.${arch}.o
                                 -Wl,-whole-archive
                                 libRTSanitizerCommon.test.nolibc.${arch}.a
                                 -Wl,-no-whole-archive
                         DEPS sanitizer_nolibc_test_main.${arch}.o
                              RTSanitizerCommon.test.nolibc.${arch}
                         LINK_FLAGS -static -nostdlib ${TARGET_FLAGS})
  endif()
endmacro()

if(COMPILER_RT_CAN_EXECUTE_TESTS AND NOT ANDROID)
  # We use just-built clang to build sanitizer_common unittests, so we must
  # be sure that produced binaries would work.
  if(APPLE)
    add_sanitizer_common_lib("RTSanitizerCommon.test.osx"
                             $<TARGET_OBJECTS:RTSanitizerCommon.osx>
                             $<TARGET_OBJECTS:RTSanitizerCommonLibc.osx>
                             $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.osx>)
  else()
    if(CAN_TARGET_x86_64)
      add_sanitizer_common_lib("RTSanitizerCommon.test.nolibc.x86_64"
                               $<TARGET_OBJECTS:RTSanitizerCommon.x86_64>
                               $<TARGET_OBJECTS:RTSanitizerCommonNoLibc.x86_64>)
    endif()
    foreach(arch ${SANITIZER_UNITTEST_SUPPORTED_ARCH})
      add_sanitizer_common_lib("RTSanitizerCommon.test.${arch}"
                               $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
                               $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
                               $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
                               $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>)
    endforeach()
  endif()
  foreach(arch ${SANITIZER_UNITTEST_SUPPORTED_ARCH})
    add_sanitizer_tests_for_arch(${arch})
  endforeach()
endif()

if(ANDROID)
  foreach(arch ${SANITIZER_COMMON_SUPPORTED_ARCH})
    add_executable(SanitizerTest
      ${SANITIZER_UNITTESTS}
      ${COMPILER_RT_GTEST_SOURCE}
      ${COMPILER_RT_GMOCK_SOURCE}
      $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
      $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
      $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
      $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>)
    set_target_compile_flags(SanitizerTest ${SANITIZER_TEST_CFLAGS_COMMON})
    # Setup correct output directory and link flags.
    set_target_properties(SanitizerTest PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_target_link_flags(SanitizerTest ${SANITIZER_TEST_LINK_FLAGS_COMMON})
    target_link_libraries(SanitizerTest ${SANITIZER_TEST_LINK_LIBS})
    # Add unit test to test suite.
    add_dependencies(SanitizerUnitTests SanitizerTest)
  endforeach()
endif()
