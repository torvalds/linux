include(CheckCXXCompilerFlag)
include(CompilerRTCompile)
include(CompilerRTLink)

include_directories(..)
include_directories(../..)

set(MSAN_LIBCXX_CFLAGS
  -fsanitize=memory
  -fsanitize-memory-track-origins
  -fno-sanitize-memory-param-retval  # unittests test mostly this mode.
  -Wno-pedantic
    # Do not instrument __gxx_personality_v0 (part of libcxxabi)
  -fsanitize-ignorelist=${COMPILER_RT_OUTPUT_DIR}/share/msan_ignorelist.txt
  )

# Unittest sources and build flags.
set(MSAN_UNITTEST_SOURCES
  msan_test.cpp
  msan_test_main.cpp
  )
set(MSAN_LOADABLE_SOURCE
  msan_loadable.cpp
  )
set(MSAN_UNITTEST_HEADERS
  msan_test_config.h
  ../../../include/sanitizer/msan_interface.h
  )
set(MSAN_UNITTEST_COMMON_CFLAGS
  -nostdinc++
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/include
  -I${COMPILER_RT_SOURCE_DIR}/lib
  -I${COMPILER_RT_SOURCE_DIR}/lib/msan
  -g
  -O2
  -fno-omit-frame-pointer
  -mno-omit-leaf-frame-pointer
  -Wno-deprecated-declarations
  -Wno-unused-variable
  -Wno-zero-length-array
  -Wno-uninitialized
  -Werror=sign-compare
  -Wno-gnu-zero-variadic-macro-arguments
  -fno-sanitize-memory-param-retval  # unittests test mostly this mode.
)
# Remove -stdlib= which is unused when passing -nostdinc++.
string(REGEX REPLACE "-stdlib=[a-zA-Z+]*" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})

set(MSAN_UNITTEST_INSTRUMENTED_CFLAGS
  ${MSAN_UNITTEST_COMMON_CFLAGS}
  -fsanitize=memory
  -fsanitize-memory-track-origins
  -mllvm -msan-keep-going=1
)
set(MSAN_UNITTEST_LINK_FLAGS
  -nostdlib++
  ${COMPILER_RT_UNITTEST_LINK_FLAGS}
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  -fsanitize=memory
  # Don't need -stdlib=libc++ because we explicitly list libc++.a in the linker
  # inputs.
)

append_list_if(COMPILER_RT_HAS_LIBDL -ldl MSAN_UNITTEST_LINK_FLAGS)

macro(msan_compile obj_list source arch kind cflags)
  sanitizer_test_compile(
    ${obj_list} ${source} ${arch}
    KIND ${kind}
    COMPILE_DEPS ${MSAN_UNITTEST_HEADERS} libcxx_msan_${arch}-build
    DEPS msan
    CFLAGS -isystem ${MSAN_LIBCXX_DIR}/../include/c++/v1
           ${MSAN_UNITTEST_INSTRUMENTED_CFLAGS} ${cflags}
  )
endmacro()

macro(msan_link_shared so_list so_name arch kind)
  cmake_parse_arguments(SOURCE "" "" "OBJECTS;LINK_FLAGS;DEPS" ${ARGN})
  set(output_so "${CMAKE_CURRENT_BINARY_DIR}/${so_name}.${arch}${kind}.so")
  get_target_flags_for_arch(${arch} TARGET_LINK_FLAGS)
  list(APPEND SOURCE_DEPS msan)
  clang_link_shared(${output_so}
                OBJECTS ${SOURCE_OBJECTS}
                LINK_FLAGS ${COMPILER_RT_UNITTEST_LINK_FLAGS} ${TARGET_LINK_FLAGS} ${SOURCE_LINK_FLAGS}
                DEPS ${SOURCE_DEPS})
  list(APPEND ${so_list} ${output_so})
endmacro()

# Main MemorySanitizer unit tests.
add_custom_target(MsanUnitTests)
set_target_properties(MsanUnitTests PROPERTIES FOLDER "MSan unit tests")

# Adds MSan unit tests and benchmarks for architecture.
macro(add_msan_tests_for_arch arch kind cflags)
  # Build gtest instrumented with MSan.
  set(MSAN_INST_GTEST)
  msan_compile(MSAN_INST_GTEST ${COMPILER_RT_GTEST_SOURCE} ${arch} "${kind}"
                               "${cflags}")

  # Instrumented tests.
  set(MSAN_INST_TEST_OBJECTS)
  foreach (SOURCE ${MSAN_UNITTEST_SOURCES})
    msan_compile(MSAN_INST_TEST_OBJECTS ${SOURCE} ${arch} "${kind}" "${cflags}")
  endforeach(SOURCE)

  # Instrumented loadable module objects.
  set(MSAN_INST_LOADABLE_OBJECTS)
  msan_compile(MSAN_INST_LOADABLE_OBJECTS ${MSAN_LOADABLE_SOURCE} ${arch} "${kind}"
               "-fPIC;${cflags}")

  # Instrumented loadable library tests.
  set(MSAN_LOADABLE_SO)
  msan_link_shared(MSAN_LOADABLE_SO "libmsan_loadable" ${arch} "${kind}"
                   OBJECTS ${MSAN_INST_LOADABLE_OBJECTS}
                   DEPS ${MSAN_INST_LOADABLE_OBJECTS})

  set(MSAN_TEST_OBJECTS ${MSAN_INST_TEST_OBJECTS} ${MSAN_INST_GTEST})
  set(MSAN_TEST_DEPS ${MSAN_TEST_OBJECTS} libcxx_msan_${arch}-build
                     ${MSAN_LOADABLE_SO}
		     "${MSAN_LIBCXX_DIR}/libc++.a" "${MSAN_LIBCXX_DIR}/libc++abi.a")
  list(APPEND MSAN_TEST_DEPS msan libcxx_msan_${arch}-build)
  get_target_flags_for_arch(${arch} TARGET_LINK_FLAGS)
  add_compiler_rt_test(MsanUnitTests "Msan-${arch}${kind}-Test" ${arch}
    OBJECTS ${MSAN_TEST_OBJECTS} "${MSAN_LIBCXX_DIR}/libc++.a" "${MSAN_LIBCXX_DIR}/libc++abi.a"
    DEPS ${MSAN_TEST_DEPS}
    LINK_FLAGS ${MSAN_UNITTEST_LINK_FLAGS}
               ${TARGET_LINK_FLAGS})
endmacro()

# We should only build MSan unit tests if we can build instrumented libcxx.
if(COMPILER_RT_CAN_EXECUTE_TESTS AND
   COMPILER_RT_LIBCXX_PATH AND
   COMPILER_RT_LIBCXXABI_PATH)
  foreach(arch ${MSAN_SUPPORTED_ARCH})
    get_target_flags_for_arch(${arch} TARGET_CFLAGS)
    set(LIBCXX_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/../libcxx_msan_${arch})
    add_custom_libcxx(libcxx_msan_${arch} ${LIBCXX_PREFIX}
      DEPS ${MSAN_RUNTIME_LIBRARIES}
      CFLAGS ${MSAN_LIBCXX_CFLAGS} ${TARGET_CFLAGS}
      USE_TOOLCHAIN)
    set(MSAN_LIBCXX_DIR ${LIBCXX_PREFIX}/lib/)

    add_msan_tests_for_arch(${arch} "" "")
    add_msan_tests_for_arch(${arch} "-with-call"
                            "-mllvm;-msan-instrumentation-with-call-threshold=0")
  endforeach()
endif()
