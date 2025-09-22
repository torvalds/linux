add_compiler_rt_component(scudo_standalone)

include_directories(../.. include)

set(SCUDO_CFLAGS)

list(APPEND SCUDO_CFLAGS
  -Werror=conversion
  -Wall
  -Wextra
  -pedantic
  -g
  -nostdinc++)

# Remove -stdlib= which is unused when passing -nostdinc++.
string(REGEX REPLACE "-stdlib=[a-zA-Z+]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

append_list_if(COMPILER_RT_HAS_FVISIBILITY_HIDDEN_FLAG -fvisibility=hidden SCUDO_CFLAGS)

append_list_if(COMPILER_RT_HAS_FNO_EXCEPTIONS_FLAG -fno-exceptions SCUDO_CFLAGS)

append_list_if(COMPILER_RT_HAS_WNO_PEDANTIC -Wno-pedantic SCUDO_CFLAGS)

# FIXME: find cleaner way to agree with GWPAsan flags
append_list_if(COMPILER_RT_HAS_FNO_LTO_FLAG -fno-lto SCUDO_CFLAGS)

if(COMPILER_RT_DEBUG)
  list(APPEND SCUDO_CFLAGS -O0 -DSCUDO_DEBUG=1 -DSCUDO_ENABLE_HOOKS=1)
else()
  list(APPEND SCUDO_CFLAGS -O3)
endif()

append_list_if(COMPILER_RT_HAS_WTHREAD_SAFETY_FLAG -Werror=thread-safety
  SCUDO_CFLAGS)

set(SCUDO_LINK_FLAGS)

list(APPEND SCUDO_LINK_FLAGS -Wl,-z,defs,-z,now,-z,relro)

list(APPEND SCUDO_LINK_FLAGS -ffunction-sections -fdata-sections -Wl,--gc-sections)

# We don't use the C++ standard library, so avoid including it by mistake.
append_list_if(COMPILER_RT_HAS_NOSTDLIBXX_FLAG -nostdlib++ SCUDO_LINK_FLAGS)
append_list_if(CXX_SUPPORTS_UNWINDLIB_NONE_FLAG --unwindlib=none SCUDO_LINK_FLAGS)

if(COMPILER_RT_SCUDO_STANDALONE_SYSROOT_PATH)
  list(APPEND SCUDO_CFLAGS "--sysroot=${COMPILER_RT_SCUDO_STANDALONE_SYSROOT_PATH}")
endif()

if(ANDROID)
  list(APPEND SCUDO_CFLAGS -fno-emulated-tls)

# Put the shared library in the global group. For more details, see
# android-changes-for-ndk-developers.md#changes-to-library-search-order
  append_list_if(COMPILER_RT_HAS_Z_GLOBAL -Wl,-z,global SCUDO_LINK_FLAGS)
endif()

set(SCUDO_HEADERS
  allocator_common.h
  allocator_config.h
  allocator_config_wrapper.h
  atomic_helpers.h
  bytemap.h
  checksum.h
  chunk.h
  condition_variable.h
  condition_variable_base.h
  condition_variable_linux.h
  combined.h
  common.h
  flags_parser.h
  flags.h
  fuchsia.h
  internal_defs.h
  linux.h
  list.h
  local_cache.h
  memtag.h
  mem_map.h
  mem_map_base.h
  mem_map_fuchsia.h
  mem_map_linux.h
  mutex.h
  options.h
  platform.h
  primary32.h
  primary64.h
  quarantine.h
  release.h
  report.h
  report_linux.h
  secondary.h
  size_class_map.h
  stack_depot.h
  stats.h
  string_utils.h
  timing.h
  tsd_exclusive.h
  tsd_shared.h
  tsd.h
  vector.h
  wrappers_c_checks.h
  wrappers_c.h

  include/scudo/interface.h
  )

set(SCUDO_SOURCES
  checksum.cpp
  common.cpp
  condition_variable_linux.cpp
  crc32_hw.cpp
  flags_parser.cpp
  flags.cpp
  fuchsia.cpp
  linux.cpp
  mem_map.cpp
  mem_map_fuchsia.cpp
  mem_map_linux.cpp
  release.cpp
  report.cpp
  report_linux.cpp
  string_utils.cpp
  timing.cpp
  )

# Temporary hack until LLVM libc supports inttypes.h print format macros
# See: https://github.com/llvm/llvm-project/issues/63317#issuecomment-1591906241
if(LLVM_LIBC_INCLUDE_SCUDO)
  list(REMOVE_ITEM SCUDO_HEADERS timing.h)
  list(REMOVE_ITEM SCUDO_SOURCES timing.cpp)
endif()

# Enable the necessary instruction set for scudo_crc32.cpp, if available.
# Newer compiler versions use -mcrc32 rather than -msse4.2.
if (COMPILER_RT_HAS_MCRC32_FLAG)
  set_source_files_properties(crc32_hw.cpp PROPERTIES COMPILE_FLAGS -mcrc32)
elseif (COMPILER_RT_HAS_MSSE4_2_FLAG)
  set_source_files_properties(crc32_hw.cpp PROPERTIES COMPILE_FLAGS -msse4.2)
endif()

# Enable the AArch64 CRC32 feature for crc32_hw.cpp, if available.
# Note that it is enabled by default starting with armv8.1-a.
if (COMPILER_RT_HAS_MCRC_FLAG)
  set_source_files_properties(crc32_hw.cpp PROPERTIES COMPILE_FLAGS -mcrc)
endif()

set(SCUDO_SOURCES_C_WRAPPERS
  wrappers_c.cpp
  )

set(SCUDO_SOURCES_CXX_WRAPPERS
  wrappers_cpp.cpp
  )

set(SCUDO_OBJECT_LIBS)
set(SCUDO_LINK_LIBS)

if (COMPILER_RT_HAS_GWP_ASAN)
  if(COMPILER_RT_USE_LLVM_UNWINDER)
    list(APPEND SCUDO_LINK_LIBS ${COMPILER_RT_UNWINDER_LINK_LIBS} dl)
  elseif (COMPILER_RT_HAS_GCC_S_LIB)
    list(APPEND SCUDO_LINK_LIBS gcc_s)
  elseif (COMPILER_RT_HAS_GCC_LIB)
    list(APPEND SCUDO_LINK_LIBS gcc)
  elseif (NOT COMPILER_RT_USE_BUILTINS_LIBRARY)
    message(FATAL_ERROR "No suitable unwinder library")
  endif()

  add_dependencies(scudo_standalone gwp_asan)
  list(APPEND SCUDO_OBJECT_LIBS
       RTGwpAsan RTGwpAsanBacktraceLibc RTGwpAsanSegvHandler
       RTGwpAsanOptionsParser)

  append_list_if(COMPILER_RT_HAS_OMIT_FRAME_POINTER_FLAG -fno-omit-frame-pointer
                                                         -mno-omit-leaf-frame-pointer
                 SCUDO_CFLAGS)
  list(APPEND SCUDO_CFLAGS -DGWP_ASAN_HOOKS)

endif()

if(COMPILER_RT_BUILD_SCUDO_STANDALONE_WITH_LLVM_LIBC)
  include_directories(${COMPILER_RT_BINARY_DIR}/../libc/include/)

  set(SCUDO_DEPS libc-headers)

  list(APPEND SCUDO_CFLAGS "-ffreestanding")
endif()

append_list_if(COMPILER_RT_HAS_LIBPTHREAD -pthread SCUDO_LINK_FLAGS)

append_list_if(FUCHSIA zircon SCUDO_LINK_LIBS)

if(COMPILER_RT_DEFAULT_TARGET_ARCH MATCHES "mips|mips64|mipsel|mips64el")
  list(APPEND SCUDO_LINK_LIBS atomic)
endif()

add_compiler_rt_object_libraries(RTScudoStandalone
  ARCHS ${SCUDO_STANDALONE_SUPPORTED_ARCH}
  SOURCES ${SCUDO_SOURCES}
  ADDITIONAL_HEADERS ${SCUDO_HEADERS}
  CFLAGS ${SCUDO_CFLAGS}
  DEPS ${SCUDO_DEPS})
add_compiler_rt_object_libraries(RTScudoStandaloneCWrappers
  ARCHS ${SCUDO_STANDALONE_SUPPORTED_ARCH}
  SOURCES ${SCUDO_SOURCES_C_WRAPPERS}
  ADDITIONAL_HEADERS ${SCUDO_HEADERS}
  CFLAGS ${SCUDO_CFLAGS}
  DEPS ${SCUDO_DEPS})
add_compiler_rt_object_libraries(RTScudoStandaloneCxxWrappers
  ARCHS ${SCUDO_STANDALONE_SUPPORTED_ARCH}
  SOURCES ${SCUDO_SOURCES_CXX_WRAPPERS}
  ADDITIONAL_HEADERS ${SCUDO_HEADERS}
  CFLAGS ${SCUDO_CFLAGS}
  DEPS ${SCUDO_DEPS})

add_compiler_rt_runtime(clang_rt.scudo_standalone
  STATIC
  ARCHS ${SCUDO_STANDALONE_SUPPORTED_ARCH}
  SOURCES ${SCUDO_SOURCES} ${SCUDO_SOURCES_C_WRAPPERS}
  ADDITIONAL_HEADERS ${SCUDO_HEADERS}
  CFLAGS ${SCUDO_CFLAGS}
  DEPS ${SCUDO_DEPS}
  OBJECT_LIBS ${SCUDO_OBJECT_LIBS}
  PARENT_TARGET scudo_standalone)
add_compiler_rt_runtime(clang_rt.scudo_standalone_cxx
  STATIC
  ARCHS ${SCUDO_STANDALONE_SUPPORTED_ARCH}
  SOURCES ${SCUDO_SOURCES_CXX_WRAPPERS}
  ADDITIONAL_HEADERS ${SCUDO_HEADERS}
  CFLAGS ${SCUDO_CFLAGS}
  DEPS ${SCUDO_DEPS}
  PARENT_TARGET scudo_standalone)

if(COMPILER_RT_SCUDO_STANDALONE_BUILD_SHARED)
  add_compiler_rt_runtime(clang_rt.scudo_standalone
    SHARED
    ARCHS ${SCUDO_STANDALONE_SUPPORTED_ARCH}
    SOURCES ${SCUDO_SOURCES} ${SCUDO_SOURCES_C_WRAPPERS} ${SCUDO_SOURCES_CXX_WRAPPERS}
    ADDITIONAL_HEADERS ${SCUDO_HEADERS}
    CFLAGS ${SCUDO_CFLAGS}
    DEPS ${SCUDO_DEPS}
    OBJECT_LIBS ${SCUDO_OBJECT_LIBS}
    LINK_FLAGS ${SCUDO_LINK_FLAGS}
    LINK_LIBS ${SCUDO_LINK_LIBS}
    PARENT_TARGET scudo_standalone)
endif()

add_subdirectory(benchmarks)
if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
