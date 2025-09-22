include(CompilerRTCompile)

set(LIBFUZZER_UNITTEST_CFLAGS
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/lib/fuzzer
  -fno-rtti
  -O2)

if (APPLE)
  set(FUZZER_SUPPORTED_OS osx)
endif()

add_custom_target(FuzzerUnitTests)
set_target_properties(FuzzerUnitTests PROPERTIES FOLDER "Compiler-RT/Tests")

add_custom_target(FuzzedDataProviderUnitTests)
set_target_properties(FuzzedDataProviderUnitTests PROPERTIES FOLDER "Compiler-RT/Tests")

set(LIBFUZZER_UNITTEST_LINK_FLAGS ${COMPILER_RT_UNITTEST_LINK_FLAGS})
list(APPEND LIBFUZZER_UNITTEST_LINK_FLAGS --driver-mode=g++)

if(MSVC)
  list(APPEND LIBFUZZER_UNITTEST_LINK_FLAGS -Wl,-defaultlib:libcmt,-defaultlib:oldnames)
else()
  if (APPLE)
    list(APPEND LIBFUZZER_UNITTEST_CFLAGS -isysroot ${DARWIN_osx_SYSROOT})
    list(APPEND LIBFUZZER_UNITTEST_LINK_FLAGS -isysroot ${DARWIN_osx_SYSROOT})
  endif()
  list(APPEND LIBFUZZER_UNITTEST_LINK_FLAGS -lpthread)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   COMPILER_RT_LIBCXX_PATH AND
   COMPILER_RT_LIBCXXABI_PATH)
  list(APPEND LIBFUZZER_UNITTEST_CFLAGS -nostdinc++ -fno-exceptions)
  list(APPEND LIBFUZZER_UNITTEST_LINK_FLAGS -nostdlib++ -fno-exceptions)
endif()

if ("-fvisibility=hidden" IN_LIST LIBFUZZER_CFLAGS)
  # Match visibility settings.
  list(APPEND LIBFUZZER_UNITTEST_CFLAGS "-fvisibility=hidden")
endif()

if(COMPILER_RT_DEFAULT_TARGET_ARCH IN_LIST FUZZER_SUPPORTED_ARCH)
  # libFuzzer unit tests are only run on the host machine.
  set(arch ${COMPILER_RT_DEFAULT_TARGET_ARCH})

  set(LIBFUZZER_TEST_RUNTIME RTFuzzerTest.${arch})
  if(APPLE)
    set(LIBFUZZER_TEST_RUNTIME_OBJECTS
      $<TARGET_OBJECTS:RTfuzzer.osx>)
  else()
    set(LIBFUZZER_TEST_RUNTIME_OBJECTS
      $<TARGET_OBJECTS:RTfuzzer.${arch}>)
  endif()
  add_library(${LIBFUZZER_TEST_RUNTIME} STATIC
    ${LIBFUZZER_TEST_RUNTIME_OBJECTS})
  set_target_properties(${LIBFUZZER_TEST_RUNTIME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    FOLDER "Compiler-RT/Tests/Runtime")

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
     COMPILER_RT_LIBCXX_PATH AND
     COMPILER_RT_LIBCXXABI_PATH)
    file(GLOB libfuzzer_headers ../*.h)
    set(LIBFUZZER_TEST_RUNTIME_DEPS libcxx_fuzzer_${arch}-build ${libfuzzer_headers})
    set(LIBFUZZER_TEST_RUNTIME_CFLAGS -isystem ${LIBCXX_${arch}_PREFIX}/include/c++/v1)
    set(LIBFUZZER_TEST_RUNTIME_LINK_FLAGS ${LIBCXX_${arch}_PREFIX}/lib/libc++.a)
  endif()

  set(FuzzerTestObjects)
  generate_compiler_rt_tests(FuzzerTestObjects
    FuzzerUnitTests "Fuzzer-${arch}-Test" ${arch}
    SOURCES FuzzerUnittest.cpp ${COMPILER_RT_GTEST_SOURCE}
    RUNTIME ${LIBFUZZER_TEST_RUNTIME}
    DEPS ${LIBFUZZER_TEST_RUNTIME_DEPS}
    CFLAGS ${LIBFUZZER_UNITTEST_CFLAGS} ${LIBFUZZER_TEST_RUNTIME_CFLAGS}
    LINK_FLAGS ${LIBFUZZER_UNITTEST_LINK_FLAGS} ${LIBFUZZER_TEST_RUNTIME_LINK_FLAGS})
  set_target_properties(FuzzerUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  set(FuzzedDataProviderTestObjects)
  generate_compiler_rt_tests(FuzzedDataProviderTestObjects
    FuzzedDataProviderUnitTests "FuzzerUtils-${arch}-Test" ${arch}
    SOURCES FuzzedDataProviderUnittest.cpp ${COMPILER_RT_GTEST_SOURCE}
    DEPS ${LIBFUZZER_TEST_RUNTIME_DEPS} ${COMPILER_RT_SOURCE_DIR}/include/fuzzer/FuzzedDataProvider.h
    CFLAGS ${LIBFUZZER_UNITTEST_CFLAGS} ${LIBFUZZER_TEST_RUNTIME_CFLAGS}
    LINK_FLAGS ${LIBFUZZER_UNITTEST_LINK_FLAGS} ${LIBFUZZER_TEST_RUNTIME_LINK_FLAGS})
  set_target_properties(FuzzedDataProviderUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
