include(ExtendPath)
include(LLVMDistributionSupport)
include(FindPrefixFromConfig)

# CMAKE_INSTALL_PACKAGEDIR might be absolute, so don't reuse below.
string(REPLACE "${CMAKE_CFG_INTDIR}" "." llvm_cmake_builddir "${LLVM_LIBRARY_DIR}")
set(llvm_cmake_builddir "${llvm_cmake_builddir}/cmake/llvm")

# First for users who use an installed LLVM, create the LLVMExports.cmake file.
set(LLVM_EXPORTS_FILE ${llvm_cmake_builddir}/LLVMExports.cmake)
get_property(LLVM_EXPORTS GLOBAL PROPERTY LLVM_EXPORTS)
export(TARGETS ${LLVM_EXPORTS} FILE ${LLVM_EXPORTS_FILE})

# Then for users who want to link against the LLVM build tree, provide the
# normal targets and the build tree only targets.
set(LLVM_BUILDTREEONLY_EXPORTS_FILE ${llvm_cmake_builddir}/LLVMBuildTreeOnlyTargets.cmake)
get_property(LLVM_EXPORTS_BUILDTREE_ONLY GLOBAL PROPERTY LLVM_EXPORTS_BUILDTREE_ONLY)
export(TARGETS ${LLVM_EXPORTS_BUILDTREE_ONLY} FILE ${LLVM_BUILDTREEONLY_EXPORTS_FILE})

get_property(LLVM_AVAILABLE_LIBS GLOBAL PROPERTY LLVM_LIBS)

foreach(lib ${LLVM_AVAILABLE_LIBS})
  get_property(llvm_lib_deps GLOBAL PROPERTY LLVMBUILD_LIB_DEPS_${lib})
  set(all_llvm_lib_deps
    "${all_llvm_lib_deps}\nset_property(GLOBAL PROPERTY LLVMBUILD_LIB_DEPS_${lib} ${llvm_lib_deps})")
endforeach(lib)

# CMake requires that all targets expressed as dependencies exist, so we can't
# have intrinsics_gen in LLVM_COMMON_DEPENDS when it is written out, otherwise
# projects building out of tree will have CMake errors. This only gets hit when
# LLVM_ENABLE_MODULES=On. Eventually we should come up with a better solution to
# this, but there is no easy solution.
if(intrinsics_gen IN_LIST LLVM_COMMON_DEPENDS)
  list(REMOVE_ITEM LLVM_COMMON_DEPENDS intrinsics_gen)
endif()
if(omp_gen IN_LIST LLVM_COMMON_DEPENDS)
  list(REMOVE_ITEM LLVM_COMMON_DEPENDS omp_gen)
endif()

#
# Generate LLVMConfig.cmake for the build tree.
#

set(LLVM_CONFIG_CODE "
# LLVM_BUILD_* values available only from LLVM build tree.
set(LLVM_BUILD_BINARY_DIR \"${LLVM_BINARY_DIR}\")
set(LLVM_BUILD_LIBRARY_DIR \"${LLVM_LIBRARY_DIR}\")
set(LLVM_BUILD_MAIN_INCLUDE_DIR \"${LLVM_MAIN_INCLUDE_DIR}\")
set(LLVM_BUILD_MAIN_SRC_DIR \"${LLVM_MAIN_SRC_DIR}\")
")

set(LLVM_CONFIG_MAIN_INCLUDE_DIR "${LLVM_MAIN_INCLUDE_DIR}")
set(LLVM_CONFIG_INCLUDE_DIR "${LLVM_INCLUDE_DIR}")
set(LLVM_CONFIG_INCLUDE_DIRS
  "${LLVM_CONFIG_MAIN_INCLUDE_DIR}"
  "${LLVM_CONFIG_INCLUDE_DIR}"
  )
list(REMOVE_DUPLICATES LLVM_CONFIG_INCLUDE_DIRS)

set(LLVM_CONFIG_LIBRARY_DIR "${LLVM_LIBRARY_DIR}")
set(LLVM_CONFIG_LIBRARY_DIRS
  "${LLVM_CONFIG_LIBRARY_DIR}"
  # FIXME: Should there be other entries here?
  )
list(REMOVE_DUPLICATES LLVM_CONFIG_LIBRARY_DIRS)

set(LLVM_CONFIG_BINARY_DIR "${LLVM_BINARY_DIR}")
set(LLVM_CONFIG_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(LLVM_CONFIG_TOOLS_BINARY_DIR "${LLVM_TOOLS_BINARY_DIR}")

# Generate a default location for lit
if (LLVM_BUILD_UTILS)
  if (CMAKE_HOST_WIN32 AND NOT CYGWIN)
    set(LLVM_CONFIG_DEFAULT_EXTERNAL_LIT "${LLVM_CONFIG_TOOLS_BINARY_DIR}/llvm-lit.py")
  else()
    set(LLVM_CONFIG_DEFAULT_EXTERNAL_LIT "${LLVM_CONFIG_TOOLS_BINARY_DIR}/llvm-lit")
  endif()
endif()

if (LLVM_LINK_LLVM_DYLIB)
  set(LLVM_CONFIG_LINK_LLVM_DYLIB
      "set(LLVM_LINK_LLVM_DYLIB ${LLVM_LINK_LLVM_DYLIB})")
endif()

# We need to use the full path to the LLVM Exports file to make sure we get the
# one from the build tree. This is due to our cmake files being split between
# this source dir and the binary dir in the build tree configuration and the
# LLVM_CONFIG_CMAKE_DIR being the source directory. In contrast in the install
# tree, both the generated LLVMExports.cmake file and the rest of the cmake
# source files are put in the same cmake directory.
set(LLVM_CONFIG_EXPORTS "${LLVM_EXPORTS};${LLVM_EXPORTS_BUILDTREE_ONLY}")
set(LLVM_CONFIG_INCLUDE_EXPORTS "include(\"${LLVM_EXPORTS_FILE}\")")
set(llvm_config_include_buildtree_only_exports
"include(\"${LLVM_BUILDTREEONLY_EXPORTS_FILE}\")")
configure_file(
  LLVMConfig.cmake.in
  ${llvm_cmake_builddir}/LLVMConfig.cmake
  @ONLY)
set(llvm_config_include_buildtree_only_exports)

# For compatibility with projects that include(LLVMConfig)
# via CMAKE_MODULE_PATH, place API modules next to it.
# Copy without source permissions because the source could be read-only,
# but we need to write into the copied folder.
# This should be removed in the future.
file(COPY .
  DESTINATION ${llvm_cmake_builddir}
  NO_SOURCE_PERMISSIONS
  FILES_MATCHING PATTERN *.cmake
  PATTERN CMakeFiles EXCLUDE
  PATTERN llvm-driver-template.cpp.in
  )

#
# Generate LLVMConfig.cmake for the install tree.
#

find_prefix_from_config(LLVM_CONFIG_CODE LLVM_INSTALL_PREFIX "${LLVM_INSTALL_PACKAGE_DIR}")

extend_path(LLVM_CONFIG_MAIN_INCLUDE_DIR "\${LLVM_INSTALL_PREFIX}" "${CMAKE_INSTALL_INCLUDEDIR}")
# This is the same as the above because the handwritten and generated headers
# are combined in one directory at install time.
set(LLVM_CONFIG_INCLUDE_DIR "${LLVM_CONFIG_MAIN_INCLUDE_DIR}")
set(LLVM_CONFIG_INCLUDE_DIRS
  "${LLVM_CONFIG_MAIN_INCLUDE_DIR}"
  "${LLVM_CONFIG_INCLUDE_DIR}"
  )
list(REMOVE_DUPLICATES LLVM_CONFIG_INCLUDE_DIRS)

extend_path(LLVM_CONFIG_LIBRARY_DIR "\${LLVM_INSTALL_PREFIX}" "lib\${LLVM_LIBDIR_SUFFIX}")
set(LLVM_CONFIG_LIBRARY_DIRS
  "${LLVM_CONFIG_LIBRARY_DIR}"
  # FIXME: Should there be other entries here?
  )
list(REMOVE_DUPLICATES LLVM_CONFIG_LIBRARY_DIRS)

set(LLVM_CONFIG_BINARY_DIR "\${LLVM_INSTALL_PREFIX}")
extend_path(LLVM_CONFIG_CMAKE_DIR "\${LLVM_INSTALL_PREFIX}" "${LLVM_INSTALL_PACKAGE_DIR}")
extend_path(LLVM_CONFIG_TOOLS_BINARY_DIR "\${LLVM_INSTALL_PREFIX}" "${LLVM_TOOLS_INSTALL_DIR}")

# Generate a default location for lit
if (LLVM_INSTALL_UTILS AND LLVM_BUILD_UTILS)
  if (CMAKE_HOST_WIN32 AND NOT CYGWIN)
    set(LLVM_CONFIG_DEFAULT_EXTERNAL_LIT "${LLVM_CONFIG_TOOLS_BINARY_DIR}/llvm-lit.py")
  else()
    set(LLVM_CONFIG_DEFAULT_EXTERNAL_LIT "${LLVM_CONFIG_TOOLS_BINARY_DIR}/llvm-lit")
  endif()
endif()

get_config_exports_includes(LLVM LLVM_CONFIG_INCLUDE_EXPORTS)
set(LLVM_CONFIG_EXPORTS "${LLVM_EXPORTS}")
configure_file(
  LLVMConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/LLVMConfig.cmake
  @ONLY)

# Generate LLVMConfigVersion.cmake for build and install tree.
configure_file(
  LLVMConfigVersion.cmake.in
  ${llvm_cmake_builddir}/LLVMConfigVersion.cmake
  @ONLY)

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install_distribution_exports(LLVM)

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/LLVMConfig.cmake
    ${llvm_cmake_builddir}/LLVMConfigVersion.cmake
    LLVM-Config.cmake
    DESTINATION ${LLVM_INSTALL_PACKAGE_DIR}
    COMPONENT cmake-exports)

  install(DIRECTORY .
    DESTINATION ${LLVM_INSTALL_PACKAGE_DIR}
    COMPONENT cmake-exports
    FILES_MATCHING PATTERN *.cmake
    PATTERN LLVMConfig.cmake EXCLUDE
    PATTERN LLVMConfigExtensions.cmake EXCLUDE
    PATTERN LLVMConfigVersion.cmake EXCLUDE
    PATTERN LLVM-Config.cmake EXCLUDE
    PATTERN GetHostTriple.cmake EXCLUDE
    PATTERN llvm-driver-template.cpp.in)

  if (NOT LLVM_ENABLE_IDE)
    # Add a dummy target so this can be used with LLVM_DISTRIBUTION_COMPONENTS
    add_custom_target(cmake-exports)
    add_llvm_install_targets(install-cmake-exports
                             COMPONENT cmake-exports)
  endif()
endif()
