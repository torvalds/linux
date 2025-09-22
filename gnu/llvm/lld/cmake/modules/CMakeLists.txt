include(GNUInstallPackageDir)
include(ExtendPath)
include(FindPrefixFromConfig)

# Generate a list of CMake library targets so that other CMake projects can
# link against them. LLVM calls its version of this file LLVMExports.cmake, but
# the usual CMake convention seems to be ${Project}Targets.cmake.
set(LLD_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/lld" CACHE STRING
  "Path for CMake subdirectory for LLD (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/lld')")
# CMAKE_INSTALL_PACKAGEDIR might be absolute, so don't reuse below.
set(lld_cmake_builddir "${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX}/cmake/lld")

# Keep this in sync with llvm/cmake/CMakeLists.txt!
set(LLVM_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/llvm" CACHE STRING
  "Path for CMake subdirectory for LLVM (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/llvm')")
# CMAKE_INSTALL_PACKAGEDIR might be absolute, so don't reuse below.
string(REPLACE "${CMAKE_CFG_INTDIR}" "." llvm_cmake_builddir "${LLVM_LIBRARY_DIR}")
set(llvm_cmake_builddir "${llvm_cmake_builddir}/cmake/llvm")

get_property(LLD_EXPORTS GLOBAL PROPERTY LLD_EXPORTS)
export(TARGETS ${LLD_EXPORTS} FILE ${lld_cmake_builddir}/LLDTargets.cmake)

# Generate LLDConfig.cmake for the build tree.
set(LLD_CONFIG_CMAKE_DIR "${lld_cmake_builddir}")
set(LLD_CONFIG_LLVM_CMAKE_DIR "${llvm_cmake_builddir}")
set(LLD_CONFIG_INCLUDE_EXPORTS "include(\"${lld_cmake_builddir}/LLDTargets.cmake\")")
set(LLD_CONFIG_INCLUDE_DIRS
  "${LLD_SOURCE_DIR}/include"
  "${LLD_BINARY_DIR}/include"
  )
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/LLDConfig.cmake.in
  ${lld_cmake_builddir}/LLDConfig.cmake
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/LLDConfigVersion.cmake.in
  ${lld_cmake_builddir}/LLDConfigVersion.cmake
  @ONLY)
set(LLD_CONFIG_CMAKE_DIR)
set(LLD_CONFIG_LLVM_CMAKE_DIR)

# Generate LLDConfig.cmake for the install tree.
find_prefix_from_config(LLD_CONFIG_CODE LLD_INSTALL_PREFIX "${LLD_INSTALL_PACKAGE_DIR}")
extend_path(LLD_CONFIG_CMAKE_DIR "\${LLD_INSTALL_PREFIX}" "${LLD_INSTALL_PACKAGE_DIR}")
extend_path(LLD_CONFIG_LLVM_CMAKE_DIR "\${LLD_INSTALL_PREFIX}" "${LLVM_INSTALL_PACKAGE_DIR}")
get_config_exports_includes(LLD LLD_CONFIG_INCLUDE_EXPORTS)
extend_path(LLD_CONFIG_INCLUDE_DIRS "\${LLD_INSTALL_PREFIX}" "${CMAKE_INSTALL_INCLUDEDIR}")
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/LLDConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/LLDConfig.cmake
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/LLDConfigVersion.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/LLDConfigVersion.cmake
  @ONLY)
set(LLD_CONFIG_CODE)
set(LLD_CONFIG_CMAKE_DIR)

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install_distribution_exports(LLD)

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/LLDConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/LLDConfigVersion.cmake
    DESTINATION ${LLD_INSTALL_PACKAGE_DIR}
    COMPONENT lld-cmake-exports)

  if(NOT LLVM_ENABLE_IDE)
    # Add a dummy target so this can be used with LLVM_DISTRIBUTION_COMPONENTS
    add_custom_target(lld-cmake-exports)
    add_llvm_install_targets(install-lld-cmake-exports
                             COMPONENT lld-cmake-exports)
  endif()
endif()
