include(CompilerRTCompile)

filter_available_targets(INTERCEPTION_UNITTEST_SUPPORTED_ARCH x86_64 i386 mips64 mips64el)

set(INTERCEPTION_UNITTESTS
  interception_linux_test.cpp
  interception_linux_foreign_test.cpp
  interception_test_main.cpp
  interception_win_test.cpp
  )

set(INTERCEPTION_TEST_HEADERS)

set(INTERCEPTION_TEST_CFLAGS_COMMON
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  ${SANITIZER_TEST_CXX_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/include
  -I${COMPILER_RT_SOURCE_DIR}/lib
  -I${COMPILER_RT_SOURCE_DIR}/lib/interception
  -DSANITIZER_COMMON_NO_REDEFINE_BUILTINS
  -fno-rtti
  -fno-builtin-isdigit
  -fno-builtin-isalpha
  -fno-builtin-isalnum
  -fno-builtin-islower
  -O2
  -Werror=sign-compare)

set(INTERCEPTION_TEST_LINK_FLAGS_COMMON
  ${COMPILER_RT_UNITTEST_LINK_FLAGS}
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_TEST_CXX_LIBRARIES})

# -gline-tables-only must be enough for these tests, so use it if possible.
if(COMPILER_RT_TEST_COMPILER_ID MATCHES "Clang")
  list(APPEND INTERCEPTION_TEST_CFLAGS_COMMON -gline-tables-only)
else()
  list(APPEND INTERCEPTION_TEST_CFLAGS_COMMON -g)
endif()
if(MSVC)
  list(APPEND INTERCEPTION_TEST_CFLAGS_COMMON -gcodeview)
  list(APPEND INTERCEPTION_TEST_LINK_FLAGS_COMMON
    -Wl,-largeaddressaware
    -Wl,-nodefaultlib:libcmt,-defaultlib:msvcrt,-defaultlib:oldnames
    )
endif()
if(MINGW)
  list(APPEND INTERCEPTION_TEST_LINK_FLAGS_COMMON
    -Wl,--large-address-aware
    )
endif()
list(APPEND INTERCEPTION_TEST_LINK_FLAGS_COMMON -g)

if(NOT MSVC)
  list(APPEND INTERCEPTION_TEST_LINK_FLAGS_COMMON --driver-mode=g++)
endif()

if(ANDROID)
  list(APPEND INTERCEPTION_TEST_LINK_FLAGS_COMMON -pie)
endif()

set(INTERCEPTION_TEST_LINK_LIBS)
append_list_if(COMPILER_RT_HAS_LIBLOG log INTERCEPTION_TEST_LINK_LIBS)

append_list_if(COMPILER_RT_HAS_LIBDL -ldl INTERCEPTION_TEST_LINK_FLAGS_COMMON)
append_list_if(COMPILER_RT_HAS_LIBRT -lrt INTERCEPTION_TEST_LINK_FLAGS_COMMON)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD -pthread INTERCEPTION_TEST_LINK_FLAGS_COMMON)
# x86_64 FreeBSD 9.2 additionally requires libc++ to build the tests. Also,
# 'libm' shall be specified explicitly to build i386 tests.
if(CMAKE_SYSTEM MATCHES "FreeBSD-9.2-RELEASE")
  list(APPEND INTERCEPTION_TEST_LINK_FLAGS_COMMON "-lc++ -lm")
endif()

include_directories(..)
include_directories(../..)

# Adds static library which contains interception object file
# (universal binary on Mac and arch-specific object files on Linux).
macro(add_interceptor_lib library)
  add_library(${library} STATIC ${ARGN})
  set_target_properties(${library} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    FOLDER "Compiler-RT/Tests/Runtime")
endmacro()

function(get_interception_lib_for_arch arch lib)
  if(APPLE)
    set(tgt_name "RTInterception.test.osx")
  else()
    set(tgt_name "RTInterception.test.${arch}")
  endif()
  set(${lib} "${tgt_name}" PARENT_SCOPE)
endfunction()

# Interception unit tests testsuite.
add_custom_target(InterceptionUnitTests)
set_target_properties(InterceptionUnitTests PROPERTIES
  FOLDER "Compiler-RT/Tests")

# Adds interception tests for architecture.
macro(add_interception_tests_for_arch arch)
  set(INTERCEPTION_TEST_OBJECTS)
  get_interception_lib_for_arch(${arch} INTERCEPTION_COMMON_LIB)
  generate_compiler_rt_tests(INTERCEPTION_TEST_OBJECTS
    InterceptionUnitTests "Interception-${arch}-Test" ${arch}
    RUNTIME ${INTERCEPTION_COMMON_LIB}
    SOURCES ${INTERCEPTION_UNITTESTS} ${COMPILER_RT_GTEST_SOURCE}
    COMPILE_DEPS ${INTERCEPTION_TEST_HEADERS}
    CFLAGS ${INTERCEPTION_TEST_CFLAGS_COMMON}
    LINK_FLAGS ${INTERCEPTION_TEST_LINK_FLAGS_COMMON})
endmacro()

if(COMPILER_RT_CAN_EXECUTE_TESTS AND NOT ANDROID AND NOT APPLE)
  # We use just-built clang to build interception unittests, so we must
  # be sure that produced binaries would work.
  if(APPLE)
    add_interceptor_lib("RTInterception.test.osx"
                        $<TARGET_OBJECTS:RTInterception.osx>)
  else()
    foreach(arch ${INTERCEPTION_UNITTEST_SUPPORTED_ARCH})
      add_interceptor_lib("RTInterception.test.${arch}"
                          $<TARGET_OBJECTS:RTInterception.${arch}>)
    endforeach()
  endif()
  foreach(arch ${INTERCEPTION_UNITTEST_SUPPORTED_ARCH})
    add_interception_tests_for_arch(${arch})
  endforeach()
endif()
