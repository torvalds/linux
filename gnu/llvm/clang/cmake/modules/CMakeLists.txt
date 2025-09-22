include(GNUInstallPackageDir)
include(ExtendPath)
include(LLVMDistributionSupport)
include(FindPrefixFromConfig)

# Generate a list of CMake library targets so that other CMake projects can
# link against them. LLVM calls its version of this file LLVMExports.cmake, but
# the usual CMake convention seems to be ${Project}Targets.cmake.
set(CLANG_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/clang" CACHE STRING
  "Path for CMake subdirectory for Clang (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/clang')")
# CMAKE_INSTALL_PACKAGEDIR might be absolute, so don't reuse below.
set(clang_cmake_builddir "${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX}/cmake/clang")

# Keep this in sync with llvm/cmake/CMakeLists.txt!
set(LLVM_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/llvm" CACHE STRING
  "Path for CMake subdirectory for LLVM (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/llvm')")
# CMAKE_INSTALL_PACKAGEDIR might be absolute, so don't reuse below.
string(REPLACE "${CMAKE_CFG_INTDIR}" "." llvm_cmake_builddir "${LLVM_LIBRARY_DIR}")
set(llvm_cmake_builddir "${llvm_cmake_builddir}/cmake/llvm")

get_property(CLANG_EXPORTS GLOBAL PROPERTY CLANG_EXPORTS)
export(TARGETS ${CLANG_EXPORTS} FILE ${clang_cmake_builddir}/ClangTargets.cmake)

# Generate ClangConfig.cmake for the build tree.
set(CLANG_CONFIG_CMAKE_DIR "${clang_cmake_builddir}")
set(CLANG_CONFIG_LLVM_CMAKE_DIR "${llvm_cmake_builddir}")
set(CLANG_CONFIG_INCLUDE_EXPORTS "include(\"${clang_cmake_builddir}/ClangTargets.cmake\")")
set(CLANG_CONFIG_INCLUDE_DIRS
  "${CLANG_SOURCE_DIR}/include"
  "${CLANG_BINARY_DIR}/include"
  )
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/ClangConfig.cmake.in
  ${clang_cmake_builddir}/ClangConfig.cmake
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/ClangConfigVersion.cmake.in
  ${clang_cmake_builddir}/ClangConfigVersion.cmake
  @ONLY)
set(CLANG_CONFIG_CMAKE_DIR)
set(CLANG_CONFIG_LLVM_CMAKE_DIR)

# For compatibility with projects that include(ClangConfig)
# via CMAKE_MODULE_PATH, place API modules next to it.
# Copy without source permissions because the source could be read-only,
# but we need to write into the copied folder.
file(COPY .
  DESTINATION ${clang_cmake_builddir}
  NO_SOURCE_PERMISSIONS
  FILES_MATCHING PATTERN *.cmake
  PATTERN CMakeFiles EXCLUDE
  )

# Generate ClangConfig.cmake for the install tree.
find_prefix_from_config(CLANG_CONFIG_CODE CLANG_INSTALL_PREFIX "${CLANG_INSTALL_PACKAGE_DIR}")
extend_path(CLANG_CONFIG_CMAKE_DIR "\${CLANG_INSTALL_PREFIX}" "${CLANG_INSTALL_PACKAGE_DIR}")
extend_path(CLANG_CONFIG_LLVM_CMAKE_DIR "\${CLANG_INSTALL_PREFIX}" "${LLVM_INSTALL_PACKAGE_DIR}")
get_config_exports_includes(Clang CLANG_CONFIG_INCLUDE_EXPORTS)
extend_path(base_includedir "\${CLANG_INSTALL_PREFIX}" "${CMAKE_INSTALL_INCLUDEDIR}")
set(CLANG_CONFIG_INCLUDE_DIRS
  "${base_includedir}"
  )
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/ClangConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ClangConfig.cmake
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/ClangConfigVersion.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ClangConfigVersion.cmake
  @ONLY)
set(CLANG_CONFIG_CODE)
set(CLANG_CONFIG_CMAKE_DIR)

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install_distribution_exports(Clang)

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ClangConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/ClangConfigVersion.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/AddClang.cmake
    DESTINATION ${CLANG_INSTALL_PACKAGE_DIR}
    COMPONENT clang-cmake-exports)

  if(NOT LLVM_ENABLE_IDE)
    # Add a dummy target so this can be used with LLVM_DISTRIBUTION_COMPONENTS
    add_custom_target(clang-cmake-exports)
    add_llvm_install_targets(install-clang-cmake-exports
                             COMPONENT clang-cmake-exports)
  endif()
endif()
