include_directories(../rtl)

add_custom_target(TsanUnitTests)
set_target_properties(TsanUnitTests PROPERTIES
  FOLDER "Compiler-RT Tests")

set(TSAN_UNITTEST_CFLAGS
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  ${SANITIZER_TEST_CXX_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/include
  -I${COMPILER_RT_SOURCE_DIR}/lib
  -I${COMPILER_RT_SOURCE_DIR}/lib/tsan/rtl
  -DSANITIZER_COMMON_NO_REDEFINE_BUILTINS
  -DGTEST_HAS_RTTI=0
  -fno-rtti
)

if(COMPILER_RT_TSAN_DEBUG_OUTPUT)
  # Need to match these flags with the runtime.
  list(APPEND TSAN_UNITTEST_CFLAGS -DTSAN_COLLECT_STATS=1
                                   -DTSAN_DEBUG_OUTPUT=2)
endif()

append_list_if(COMPILER_RT_HAS_MSSE4_2_FLAG -msse4.2 TSAN_UNITTEST_CFLAGS)

set(TSAN_TEST_ARCH ${TSAN_SUPPORTED_ARCH})

set(TSAN_UNITTEST_LINK_FLAGS
  ${COMPILER_RT_UNITTEST_LINK_FLAGS}
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_TEST_CXX_LIBRARIES})

if(APPLE)

  # Create a static library for test dependencies.
  set(TSAN_TEST_RUNTIME_OBJECTS
    $<TARGET_OBJECTS:RTTsan_dynamic.osx>
    $<TARGET_OBJECTS:RTInterception.osx>
    $<TARGET_OBJECTS:RTSanitizerCommon.osx>
    $<TARGET_OBJECTS:RTSanitizerCommonLibc.osx>
    $<TARGET_OBJECTS:RTSanitizerCommonCoverage.osx>
    $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.osx>
    $<TARGET_OBJECTS:RTUbsan.osx>)
  set(TSAN_TEST_RUNTIME RTTsanTest)
  add_library(${TSAN_TEST_RUNTIME} STATIC ${TSAN_TEST_RUNTIME_OBJECTS})
  set_target_properties(${TSAN_TEST_RUNTIME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  darwin_filter_host_archs(TSAN_SUPPORTED_ARCH TSAN_TEST_ARCH)
  list(APPEND TSAN_UNITTEST_CFLAGS ${DARWIN_osx_CFLAGS})

  list(APPEND TSAN_UNITTEST_LINK_FLAGS ${DARWIN_osx_LINK_FLAGS})
  add_weak_symbols("ubsan" TSAN_UNITTEST_LINK_FLAGS)
  add_weak_symbols("sanitizer_common" TSAN_UNITTEST_LINK_FLAGS)
else()
  list(APPEND TSAN_UNITTEST_LINK_FLAGS -fsanitize=thread)
  list(APPEND TSAN_UNITTEST_LINK_FLAGS -lm)
  list(APPEND TSAN_UNITTEST_LINK_FLAGS ${COMPILER_RT_TEST_LIBDISPATCH_CFLAGS})
endif()

set(TSAN_RTL_HEADERS)
foreach (header ${TSAN_HEADERS})
  list(APPEND TSAN_RTL_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../${header})
endforeach()

set(TSAN_DEPS tsan)
# TSan uses C++ standard library headers.
if (TARGET cxx-headers OR HAVE_LIBCXX)
  list(APPEND TSAN_DEPS cxx-headers)
endif()

# add_tsan_unittest(<name>
#                   SOURCES <sources list>
#                   HEADERS <extra headers list>)
macro(add_tsan_unittest testname)
  cmake_parse_arguments(TEST "" "" "SOURCES;HEADERS" ${ARGN})
  if(UNIX)
    foreach(arch ${TSAN_TEST_ARCH})
      set(TsanUnitTestsObjects)
      generate_compiler_rt_tests(TsanUnitTestsObjects TsanUnitTests
        "${testname}-${arch}-Test" ${arch}
        SOURCES ${TEST_SOURCES} ${COMPILER_RT_GTEST_SOURCE}
        RUNTIME ${TSAN_TEST_RUNTIME}
        COMPILE_DEPS ${TEST_HEADERS} ${TSAN_RTL_HEADERS}
        DEPS ${TSAN_DEPS}
        CFLAGS ${TSAN_UNITTEST_CFLAGS}
        LINK_FLAGS ${TSAN_UNITTEST_LINK_FLAGS})
    endforeach()
  endif()
endmacro()

if(COMPILER_RT_CAN_EXECUTE_TESTS AND NOT ANDROID)
  add_subdirectory(rtl)
  add_subdirectory(unit)
endif()
