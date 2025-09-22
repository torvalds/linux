include(CompilerRTCompile)

set(GWP_ASAN_UNITTEST_CFLAGS
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  ${SANITIZER_TEST_CXX_CFLAGS}
  -std=c++17
  -I${COMPILER_RT_SOURCE_DIR}/lib/
  -O2
  -g
  -fno-omit-frame-pointer)

file(GLOB GWP_ASAN_HEADERS ../*.h)
set(GWP_ASAN_UNITTESTS
  platform_specific/printf_sanitizer_common.cpp
  alignment.cpp
  backtrace.cpp
  basic.cpp
  compression.cpp
  iterate.cpp
  crash_handler_api.cpp
  driver.cpp
  mutex_test.cpp
  slot_reuse.cpp
  thread_contention.cpp
  harness.cpp
  enable_disable.cpp
  late_init.cpp
  options.cpp
  recoverable.cpp
  never_allocated.cpp)

set(GWP_ASAN_UNIT_TEST_HEADERS
  ${GWP_ASAN_HEADERS}
  harness.h)

add_custom_target(GwpAsanUnitTests)
set_target_properties(GwpAsanUnitTests PROPERTIES FOLDER "Compiler-RT/Tests")

set(GWP_ASAN_UNITTEST_LINK_FLAGS
  ${COMPILER_RT_UNITTEST_LINK_FLAGS} -ldl
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_TEST_CXX_LIBRARIES})
list(APPEND GWP_ASAN_UNITTEST_LINK_FLAGS --driver-mode=g++)
if(NOT WIN32)
  list(APPEND GWP_ASAN_UNITTEST_LINK_FLAGS -pthread)
endif()

if(COMPILER_RT_DEFAULT_TARGET_ARCH IN_LIST GWP_ASAN_SUPPORTED_ARCH)
  # GWP-ASan unit tests are only run on the host machine.
  set(arch ${COMPILER_RT_DEFAULT_TARGET_ARCH})

  set(GWP_ASAN_TEST_RUNTIME RTGwpAsanTest.${arch})

  set(GWP_ASAN_TEST_RUNTIME_OBJECTS
    $<TARGET_OBJECTS:RTGwpAsan.${arch}>
    $<TARGET_OBJECTS:RTGwpAsanBacktraceSanitizerCommon.${arch}>
    $<TARGET_OBJECTS:RTGwpAsanSegvHandler.${arch}>
    $<TARGET_OBJECTS:RTGwpAsanOptionsParser.${arch}>
    $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
    $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
    $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
    $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>)

  add_library(${GWP_ASAN_TEST_RUNTIME} STATIC
    ${GWP_ASAN_TEST_RUNTIME_OBJECTS})

  set_target_properties(${GWP_ASAN_TEST_RUNTIME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    FOLDER "Compiler-RT/Tests/Runtime")

  set(GwpAsanTestObjects)
  generate_compiler_rt_tests(GwpAsanTestObjects
    GwpAsanUnitTests "GwpAsan-${arch}-Test" ${arch}
    SOURCES ${GWP_ASAN_UNITTESTS} ${COMPILER_RT_GTEST_SOURCE}
    RUNTIME ${GWP_ASAN_TEST_RUNTIME}
    DEPS ${GWP_ASAN_UNIT_TEST_HEADERS}
    CFLAGS ${GWP_ASAN_UNITTEST_CFLAGS}
    LINK_FLAGS ${GWP_ASAN_UNITTEST_LINK_FLAGS})
  set_target_properties(GwpAsanUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
