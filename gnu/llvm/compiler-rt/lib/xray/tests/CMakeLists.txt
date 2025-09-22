include_directories(..)

add_custom_target(XRayUnitTests)
set_target_properties(XRayUnitTests PROPERTIES FOLDER "Compiler-RT/Tests")

# Sanity check XRAY_ALL_SOURCE_FILES_ABS_PATHS
list(LENGTH XRAY_ALL_SOURCE_FILES_ABS_PATHS XASFAP_LENGTH)
if (${XASFAP_LENGTH} EQUAL 0)
  message(FATAL_ERROR "XRAY_ALL_SOURCE_FILES_ABS_PATHS cannot be empty")
endif()
unset(XASFAP_LENGTH)
foreach (src_file ${XRAY_ALL_SOURCE_FILES_ABS_PATHS})
  if (NOT EXISTS "${src_file}")
    message(FATAL_ERROR "Source file \"${src_file}\" does not exist")
  endif()
endforeach()

set(XRAY_UNITTEST_CFLAGS
  ${XRAY_CFLAGS}
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  ${COMPILER_RT_GMOCK_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/include
  -I${COMPILER_RT_SOURCE_DIR}/lib/xray
  -I${COMPILER_RT_SOURCE_DIR}/lib
  )

# We add the include directories one at a time in our CFLAGS.
foreach (DIR ${LLVM_INCLUDE_DIR} ${LLVM_MAIN_INCLUDE_DIR})
  list(APPEND XRAY_UNITTEST_CFLAGS -I${DIR})
endforeach()

function(add_xray_lib library)
  add_library(${library} STATIC ${ARGN})
  set_target_properties(${library} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    FOLDER "Compiler-RT/Tests/Runtime")
endfunction()

function(get_xray_lib_for_arch arch lib)
  if(APPLE)
    set(tgt_name "RTXRay.test.osx")
  else()
    set(tgt_name "RTXRay.test.${arch}")
  endif()
  set(${lib} "${tgt_name}" PARENT_SCOPE)
endfunction()

set(XRAY_TEST_ARCH ${XRAY_SUPPORTED_ARCH})
set(XRAY_UNITTEST_LINK_FLAGS
  ${COMPILER_RT_UNITTEST_LINK_FLAGS}
  ${CMAKE_THREAD_LIBS_INIT}
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${COMPILER_RT_CXX_LINK_LIBS})

if (NOT APPLE)
  # We add the library directories one at a time in our CFLAGS.
  foreach (DIR ${LLVM_LIBRARY_DIR})
    list(APPEND XRAY_UNITTEST_LINK_FLAGS -L${DIR})
  endforeach()

  if (COMPILER_RT_STANDALONE_BUILD)
    if (COMPILER_RT_HAS_LLVMXRAY OR COMPILER_RT_HAS_LLVMTESTINGSUPPORT)
      if (LLVM_LINK_LLVM_DYLIB)
        list(APPEND XRAY_UNITTEST_LINK_FLAGS -lLLVM)
      endif()
    else()
      if (COMPILER_RT_HAS_LLVMXRAY)
        list(APPEND XRAY_UNITTEST_LINK_FLAGS -lLLVMXRay)
      endif()
      if (COMPILER_RT_HAS_TESTINGSUPPORT)
        list(APPEND XRAY_UNITTEST_LINK_FLAGS -lLLVMTestingSupport)
      endif()
      list(APPEND XRAY_UNITTEST_LINK_FLAGS -lLLVMSupport -lLLVMDemangle)
    endif()
  else()
    # We also add the actual libraries to link as dependencies.
    list(APPEND XRAY_UNITTEST_LINK_FLAGS -lLLVMXRay -lLLVMSupport -lLLVMDemangle -lLLVMTestingSupport)
  endif()

  append_list_if(COMPILER_RT_HAS_LIBM -lm XRAY_UNITTEST_LINK_FLAGS)
  append_list_if(COMPILER_RT_HAS_LIBRT -lrt XRAY_UNITTEST_LINK_FLAGS)
  append_list_if(COMPILER_RT_HAS_LIBDL -ldl XRAY_UNITTEST_LINK_FLAGS)
  append_list_if(COMPILER_RT_HAS_LIBPTHREAD -pthread XRAY_UNITTEST_LINK_FLAGS)
  append_list_if(COMPILER_RT_HAS_LIBEXECINFO -lexecinfo XRAY_UNITTEST_LINK_FLAGS)
endif()

macro(add_xray_unittest testname)
  cmake_parse_arguments(TEST "" "" "SOURCES;HEADERS" ${ARGN})
  if(UNIX AND NOT APPLE)
    set(CMAKE_DL_LIBS_INIT "")
    foreach(arch ${XRAY_TEST_ARCH})
      set(TEST_OBJECTS)
      get_xray_lib_for_arch(${arch} XRAY_RUNTIME_LIBS)
      generate_compiler_rt_tests(TEST_OBJECTS
        XRayUnitTests "${testname}-${arch}-Test" "${arch}"
        SOURCES ${TEST_SOURCES} ${COMPILER_RT_GTEST_SOURCE}
                ${COMPILER_RT_GMOCK_SOURCE}

        # Note that any change in the implementations will cause all the unit
        # tests to be re-built. This is by design, but may be cumbersome during
        # the build/test cycle.
        COMPILE_DEPS ${TEST_SOURCES} ${COMPILER_RT_GTEST_SOURCE}
        ${XRAY_HEADERS} ${XRAY_ALL_SOURCE_FILES_ABS_PATHS}
        "test_helpers.h"
        RUNTIME "${XRAY_RUNTIME_LIBS}"
        DEPS xray llvm-xray LLVMXRay LLVMTestingSupport
        CFLAGS ${XRAY_UNITTEST_CFLAGS}
        LINK_FLAGS ${TARGET_LINK_FLAGS} ${XRAY_UNITTEST_LINK_FLAGS}
        )
      set_target_properties(XRayUnitTests
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endforeach()
  endif()
endmacro()

if(COMPILER_RT_CAN_EXECUTE_TESTS)
  if (APPLE)
    add_xray_lib("RTXRay.test.osx"
      $<TARGET_OBJECTS:RTXray.osx>
      $<TARGET_OBJECTS:RTXrayFDR.osx>
      $<TARGET_OBJECTS:RTXrayPROFILING.osx>
      $<TARGET_OBJECTS:RTSanitizerCommon.osx>
      $<TARGET_OBJECTS:RTSanitizerCommonLibc.osx>)
  else()
  foreach(arch ${XRAY_SUPPORTED_ARCH})
    add_xray_lib("RTXRay.test.${arch}"
      $<TARGET_OBJECTS:RTXray.${arch}>
      $<TARGET_OBJECTS:RTXrayFDR.${arch}>
      $<TARGET_OBJECTS:RTXrayPROFILING.${arch}>
      $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
      $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>)
  endforeach()
  endif()
  add_subdirectory(unit)
endif()
