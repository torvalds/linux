# Build for all components of the ORC runtime support library.

# ORC runtime library common implementation files.
set(ORC_COMMON_SOURCES
  debug.cpp
  extensible_rtti.cpp
  log_error_to_stderr.cpp
  run_program_wrapper.cpp
  dlfcn_wrapper.cpp
  )

# Common implementation headers will go here.
set(ORC_COMMON_IMPL_HEADERS
  adt.h
  common.h
  compiler.h
  endianness.h
  error.h
  executor_address.h
  extensible_rtti.h
  simple_packed_serialization.h
  stl_extras.h
  wrapper_function_utils.h
  )

# Now put it all together...
include_directories(..)
include_directories(../../include)

set(ORC_CFLAGS
  ${COMPILER_RT_COMMON_CFLAGS}
  ${COMPILER_RT_CXX_CFLAGS})
set(ORC_LINK_FLAGS ${COMPILER_RT_COMMON_LINK_FLAGS})
set(ORC_LINK_LIBS
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${COMPILER_RT_CXX_LINK_LIBS})

# Allow the ORC runtime to reference LLVM headers.
foreach (DIR ${LLVM_INCLUDE_DIR} ${LLVM_MAIN_INCLUDE_DIR})
  list(APPEND ORC_CFLAGS -I${DIR})
endforeach()

add_compiler_rt_component(orc)

# ORC uses C++ standard library headers.
if (TARGET cxx-headers OR HAVE_LIBCXX)
  set(ORC_DEPS cxx-headers)
endif()

if (APPLE)
  set(ORC_ASM_SOURCES
    macho_tlv.x86-64.S
    macho_tlv.arm64.S
    )

  set(ORC_IMPL_HEADERS
    ${ORC_COMMON_IMPL_HEADERS}
    macho_platform.h
    )

  set(ORC_SOURCES
    ${ORC_COMMON_SOURCES}
    macho_platform.cpp
    )

  add_compiler_rt_object_libraries(RTOrc
    OS ${ORC_SUPPORTED_OS}
    ARCHS ${ORC_SUPPORTED_ARCH}
    SOURCES ${ORC_SOURCES} ${ORC_ASM_SOURCES}
    ADDITIONAL_HEADERS ${ORC_IMPL_HEADERS}
    CFLAGS ${ORC_CFLAGS}
    DEPS ${ORC_DEPS})

  add_compiler_rt_runtime(orc_rt
    STATIC
    OS ${ORC_SUPPORTED_OS}
    ARCHS ${ORC_SUPPORTED_ARCH}
    OBJECT_LIBS RTOrc
    CFLAGS ${ORC_CFLAGS}
    LINK_FLAGS ${ORC_LINK_FLAGS} ${WEAK_SYMBOL_LINK_FLAGS}
    LINK_LIBS ${ORC_LINK_LIBS}
    PARENT_TARGET orc)
else() # not Apple
  if (WIN32)
    set(ORC_BUILD_TYPE STATIC)

    set(ORC_IMPL_HEADERS
      ${ORC_COMMON_IMPL_HEADERS}
      coff_platform.h
      )

    set(ORC_SOURCES
      ${ORC_COMMON_SOURCES}
      coff_platform.cpp
      coff_platform.per_jd.cpp
      )

    if (MSVC)
      set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
      set(ORC_CFLAGS ${ORC_CFLAGS} /D_HAS_EXCEPTIONS=0 )
    endif()
  else()
    set(ORC_BUILD_TYPE STATIC)

    set(ORC_IMPL_HEADERS
      ${ORC_COMMON_IMPL_HEADERS}
      elfnix_platform.h
      )

    set(ORC_SOURCES
      ${ORC_COMMON_SOURCES}
      elfnix_platform.cpp
      )

    set(ORC_ASM_SOURCES
      elfnix_tls.x86-64.S
      elfnix_tls.aarch64.S
      elfnix_tls.ppc64.S
      )
  endif()

  foreach(arch ${ORC_SUPPORTED_ARCH})
    if(NOT CAN_TARGET_${arch})
      continue()
    endif()

    add_compiler_rt_object_libraries(RTOrc
      ARCHS ${arch}
      SOURCES ${ORC_SOURCES} ${ORC_ASM_SOURCES}
      ADDITIONAL_HEADERS ${ORC_IMPL_HEADERS}
      CFLAGS ${ORC_CFLAGS}
      DEPS ${ORC_DEPS})

    # Common ORC archive for instrumented binaries.
    add_compiler_rt_runtime(orc_rt
      ${ORC_BUILD_TYPE}
      ARCHS ${arch}
      CFLAGS ${ORC_CFLAGS}
      LINK_FLAGS ${ORC_LINK_FLAGS}
      LINK_LIBS ${ORC_LINK_LIBS}
      OBJECT_LIBS ${ORC_COMMON_RUNTIME_OBJECT_LIBS} RTOrc
      PARENT_TARGET orc)
  endforeach()
endif() # not Apple

add_subdirectory(tests)
