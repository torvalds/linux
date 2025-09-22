# Make lldb-dotest a custom target.
add_custom_target(lldb-dotest)
add_dependencies(lldb-dotest lldb-test-depends)
set_target_properties(lldb-dotest PROPERTIES FOLDER "LLDB/Utils")

get_property(LLDB_TEST_USER_ARGS GLOBAL PROPERTY LLDB_TEST_USER_ARGS_PROPERTY)
get_property(LLDB_TEST_COMMON_ARGS GLOBAL PROPERTY LLDB_TEST_COMMON_ARGS_PROPERTY)
set(LLDB_LIBS_DIR "${LLVM_LIBRARY_OUTPUT_INTDIR}")

llvm_canonicalize_cmake_booleans(
  LLDB_BUILD_INTEL_PT
  LLDB_HAS_LIBCXX
)

if ("libcxx" IN_LIST LLVM_ENABLE_RUNTIMES)
  set(LLDB_HAS_LIBCXX ON)
  if(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR AND NOT APPLE)
    set(LIBCXX_LIBRARY_DIR ${LLVM_LIBRARY_OUTPUT_INTDIR}/${LLVM_DEFAULT_TARGET_TRIPLE})
    set(LIBCXX_GENERATED_INCLUDE_DIR "${LLVM_BINARY_DIR}/include/c++/v1")
    set(LIBCXX_GENERATED_INCLUDE_TARGET_DIR "${LLVM_BINARY_DIR}/include/${LLVM_DEFAULT_TARGET_TRIPLE}/c++/v1")
  else()
    set(LIBCXX_LIBRARY_DIR ${CMAKE_BINARY_DIR}/lib${LIBCXX_LIBDIR_SUFFIX})
    set(LIBCXX_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/include/c++/v1")
  endif()
endif()

set(LLVM_TOOLS_DIR "${LLVM_TOOLS_BINARY_DIR}")
set(vars
  LLDB_TEST_COMMON_ARGS
  LLDB_TEST_USER_ARGS
  LLDB_SOURCE_DIR
  LLDB_FRAMEWORK_DIR
  LLDB_TEST_BUILD_DIRECTORY
  LLDB_TEST_EXECUTABLE
  LLDB_TEST_COMPILER
  LLDB_TEST_DSYMUTIL
  LLDB_LIBS_DIR
  LLVM_TOOLS_DIR
  LIBCXX_LIBRARY_DIR
  LIBCXX_GENERATED_INCLUDE_DIR
  LIBCXX_GENERATED_INCLUDE_TARGET_DIR
  )

llvm_canonicalize_cmake_booleans(LLDB_HAS_LIBCXX)

# Generate lldb-dotest Python driver script for each build mode.
if(LLDB_BUILT_STANDALONE)
  set(config_types ".")
  if(CMAKE_CONFIGURATION_TYPES)
    set(config_types ${CMAKE_CONFIGURATION_TYPES})
  endif()
  foreach(config_type ${config_types})
    # In paths to our build-tree, replace CMAKE_CFG_INTDIR with our actual configuration names.
    string(REPLACE ${CMAKE_CFG_INTDIR} ${config_type} config_runtime_output_dir ${LLVM_RUNTIME_OUTPUT_INTDIR})
    foreach(var ${vars})
      string(REPLACE ${LLVM_RUNTIME_OUTPUT_INTDIR} ${config_runtime_output_dir} "${var}_CONFIGURED" "${${var}}")
    endforeach()

    # Remaining ones must be paths to the provided LLVM build-tree.
    if(${config_type} IN_LIST LLVM_CONFIGURATION_TYPES)
      # Multi-configuration generator like Xcode (with a matching config).
      foreach(var ${vars})
        string(REPLACE ${CMAKE_CFG_INTDIR} ${config_type} "${var}_CONFIGURED" "${${var}}")
      endforeach()
    else()
      # Single-configuration generator like Ninja.
      foreach(var ${vars})
        string(REPLACE ${CMAKE_CFG_INTDIR} "." "${var}_CONFIGURED" "${${var}}")
      endforeach()
    endif()

    configure_file(
      lldb-dotest.in
      ${config_runtime_output_dir}/lldb-dotest @ONLY
    )
  endforeach()
elseif(NOT "${CMAKE_CFG_INTDIR}" STREQUAL ".")
  foreach(LLVM_BUILD_MODE ${CMAKE_CONFIGURATION_TYPES})
    string(REPLACE ${CMAKE_CFG_INTDIR} ${LLVM_BUILD_MODE} LLDB_DOTEST_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
    foreach(var ${vars})
      string(REPLACE ${CMAKE_CFG_INTDIR} ${LLVM_BUILD_MODE} "${var}_CONFIGURED" "${${var}}")
    endforeach()

    configure_file(
      lldb-dotest.in
      ${LLDB_DOTEST_DIR}/lldb-dotest
      )
  endforeach()
else()
  foreach(var ${vars})
    set("${var}_CONFIGURED" "${${var}}")
  endforeach()

  configure_file(
    lldb-dotest.in
    ${LLVM_RUNTIME_OUTPUT_INTDIR}/lldb-dotest
    )
endif()
