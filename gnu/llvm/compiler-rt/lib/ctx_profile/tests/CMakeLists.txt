include(CheckCXXCompilerFlag)
include(CompilerRTCompile)
include(CompilerRTLink)

set(CTX_PROFILE_UNITTEST_CFLAGS
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  ${COMPILER_RT_GMOCK_CFLAGS}
  ${SANITIZER_TEST_CXX_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/lib/
  -DSANITIZER_COMMON_NO_REDEFINE_BUILTINS
  -O2
  -g
  -fno-rtti
  -Wno-pedantic
  -fno-omit-frame-pointer)

# Suppress warnings for gmock variadic macros for clang and gcc respectively.
append_list_if(SUPPORTS_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS_FLAG -Wno-gnu-zero-variadic-macro-arguments CTX_PROFILE_UNITTEST_CFLAGS)
append_list_if(COMPILER_RT_HAS_WVARIADIC_MACROS_FLAG -Wno-variadic-macros CTX_PROFILE_UNITTEST_CFLAGS)

file(GLOB CTX_PROFILE_HEADERS ../*.h)

set(CTX_PROFILE_SOURCES
  ../CtxInstrProfiling.cpp)

set(CTX_PROFILE_UNITTESTS
  CtxInstrProfilingTest.cpp
  driver.cpp)

include_directories(../../../include)

set(CTX_PROFILE_UNITTEST_HEADERS
  ${CTX_PROFILE_HEADERS})

set(CTX_PROFILE_UNITTEST_LINK_FLAGS
  ${COMPILER_RT_UNITTEST_LINK_FLAGS})

list(APPEND CTX_PROFILE_UNITTEST_LINK_FLAGS -pthread)

set(CTX_PROFILE_UNITTEST_DEPS)
if (TARGET cxx-headers OR HAVE_LIBCXX)
  list(APPEND CTX_PROFILE_UNITTEST_DEPS cxx-headers)
endif()

set(CTX_PROFILE_UNITTEST_LINK_LIBRARIES
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_TEST_CXX_LIBRARIES})
append_list_if(COMPILER_RT_HAS_LIBDL -ldl CTX_PROFILE_UNITTEST_LINK_LIBRARIES)

macro (add_ctx_profile_tests_for_arch arch)
  set(CTX_PROFILE_TEST_RUNTIME_OBJECTS
    $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
    $<TARGET_OBJECTS:RTSanitizerCommonCoverage.${arch}>
    $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
    $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
    $<TARGET_OBJECTS:RTSanitizerCommonSymbolizerInternal.${arch}>
  )
  set(CTX_PROFILE_TEST_RUNTIME RTCtxProfileTest.${arch})
  add_library(${CTX_PROFILE_TEST_RUNTIME} STATIC ${CTX_PROFILE_TEST_RUNTIME_OBJECTS})
  set_target_properties(${CTX_PROFILE_TEST_RUNTIME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    FOLDER "Compiler-RT Runtime tests")
  set(CTX_PROFILE_TEST_OBJECTS)
  generate_compiler_rt_tests(CTX_PROFILE_TEST_OBJECTS
    CtxProfileUnitTests "CtxProfile-${arch}-UnitTest" ${arch}
    RUNTIME ${CTX_PROFILE_TEST_RUNTIME}
    DEPS ${CTX_PROFILE_UNITTEST_DEPS}
    SOURCES ${CTX_PROFILE_UNITTESTS} ${CTX_PROFILE_SOURCES} ${COMPILER_RT_GTEST_SOURCE}
    COMPILE_DEPS ${CTX_PROFILE_UNITTEST_HEADERS}
    CFLAGS ${CTX_PROFILE_UNITTEST_CFLAGS}
    LINK_FLAGS ${CTX_PROFILE_UNITTEST_LINK_FLAGS} ${CTX_PROFILE_UNITTEST_LINK_LIBRARIES})
endmacro()

add_custom_target(CtxProfileUnitTests)
set_target_properties(CtxProfileUnitTests PROPERTIES FOLDER "Compiler-RT Tests")
if(COMPILER_RT_CAN_EXECUTE_TESTS AND COMPILER_RT_DEFAULT_TARGET_ARCH IN_LIST CTX_PROFILE_SUPPORTED_ARCH)
  # CtxProfile unit tests are only run on the host machine.
  foreach(arch ${COMPILER_RT_DEFAULT_TARGET_ARCH})
    add_ctx_profile_tests_for_arch(${arch})
  endforeach()
endif()
