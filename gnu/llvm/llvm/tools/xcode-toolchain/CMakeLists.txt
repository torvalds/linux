# OS X 10.11 El Capitan has just been released. One of the new features, System
# Integrity Protection, prevents modifying the base OS install, even with sudo.
# This prevents LLVM developers on OS X from being able to easily install new
# system compilers. The feature can be disabled, but to make it easier for
# developers to work without disabling SIP, this file can generate an Xcode
# toolchain. Xcode toolchains are a mostly-undocumented feature that allows
# multiple copies of low level tools to be installed to different locations, and
# users can easily switch between them.

# Setting an environment variable TOOLCHAINS to the toolchain's identifier will
# result in /usr/bin/<tool> or xcrun <tool> to find the tool in the toolchain.

# To make this work with Xcode 7.1 and later you can install the toolchain this
# file generates anywhere on your system and set EXTERNAL_TOOLCHAINS_DIR to the
# path specified by $CMAKE_INSTALL_PREFIX/Toolchains

# This file generates a custom install-xcode-toolchain target which constructs
# and installs a toolchain with the identifier in the pattern:
# org.llvm.${PACKAGE_VERSION}. This toolchain can then be used to override the
# system compiler by setting TOOLCHAINS=org.llvm.${PACKAGE_VERSION} in the
# in the environment.

# Example usage:
# cmake -G Ninja -DLLVM_CREATE_XCODE_TOOLCHAIN=On
#   -DCMAKE_INSTALL_PREFIX=$PWD/install
# ninja install-xcode-toolchain
# export EXTERNAL_TOOLCHAINS_DIR=$PWD/install/Toolchains
# export TOOLCHAINS=org.llvm.3.8.0svn

# `xcrun -find clang` should return the installed clang, and `clang --version`
# should show 3.8.0svn.

if(NOT APPLE)
  return()
endif()

option(LLVM_CREATE_XCODE_TOOLCHAIN "Create a target to install LLVM into an Xcode toolchain" Off)

if(NOT LLVM_CREATE_XCODE_TOOLCHAIN)
  return()
endif()

# XCODE_VERSION is set by CMake when using the Xcode generator, otherwise we need
# to detect it manually here.
if(NOT XCODE_VERSION)
  execute_process(
    COMMAND xcodebuild -version
    OUTPUT_VARIABLE xcodebuild_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_FILE /dev/null
  )
  string(REGEX MATCH "Xcode ([0-9][0-9]?([.][0-9])+)" version_match ${xcodebuild_version})
  if(version_match)
    message(STATUS "Identified Xcode Version: ${CMAKE_MATCH_1}")
    set(XCODE_VERSION ${CMAKE_MATCH_1})
  else()
    # If detecting Xcode version failed, set a crazy high version so we default
    # to the newest.
    set(XCODE_VERSION 99)
    message(WARNING "Failed to detect the version of an installed copy of Xcode, falling back to highest supported version. Set XCODE_VERSION to override.")
  endif()
endif()

# Xcode 8 requires CompatibilityVersion 2
set(COMPAT_VERSION 2)
if(XCODE_VERSION VERSION_LESS 8.0.0)
  # Xcode 7.3 (the first version supporting external toolchains) requires
  # CompatibilityVersion 1
  set(COMPAT_VERSION 1)
endif()

execute_process(
  COMMAND xcrun -find otool
  OUTPUT_VARIABLE clang_path
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_FILE /dev/null
)
string(REGEX MATCH "(.*/Toolchains)/.*" toolchains_match ${clang_path})
if(NOT toolchains_match)
  message(FATAL_ERROR "Could not identify toolchain dir")
endif()
set(toolchains_dir ${CMAKE_MATCH_1})

set(LLVMToolchainDir "${CMAKE_INSTALL_PREFIX}/Toolchains/LLVM${PACKAGE_VERSION}.xctoolchain/")

add_custom_command(OUTPUT ${LLVMToolchainDir}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${LLVMToolchainDir})

add_custom_command(OUTPUT ${LLVMToolchainDir}/Info.plist
                  DEPENDS ${LLVMToolchainDir}
                  COMMAND ${CMAKE_COMMAND} -E remove ${LLVMToolchainDir}/Info.plist
                  COMMAND /usr/libexec/PlistBuddy -c "Add:CFBundleIdentifier string org.llvm.${PACKAGE_VERSION}" "${LLVMToolchainDir}/Info.plist"
                  COMMAND /usr/libexec/PlistBuddy -c "Add:CompatibilityVersion integer ${COMPAT_VERSION}" "${LLVMToolchainDir}/Info.plist"
                  )

add_custom_target(build-xcode-toolchain
                  COMMAND "${CMAKE_COMMAND}" --build ${CMAKE_BINARY_DIR} --target all)
add_llvm_install_targets(install-xcode-toolchain
                         DEPENDS ${LLVMToolchainDir}/Info.plist build-xcode-toolchain
                         PREFIX ${LLVMToolchainDir}/usr/)

if(LLVM_DISTRIBUTION_COMPONENTS)
  if(LLVM_ENABLE_IDE)
    message(FATAL_ERROR "LLVM_DISTRIBUTION_COMPONENTS cannot be specified with multi-configuration generators (i.e. Xcode or Visual Studio)")
  endif()

  add_custom_target(install-distribution-toolchain
                  DEPENDS ${LLVMToolchainDir}/Info.plist distribution)

  foreach(target ${LLVM_DISTRIBUTION_COMPONENTS})
    add_llvm_install_targets(install-distribution-${target}
                             DEPENDS ${target}
                             COMPONENT ${target}
                             PREFIX ${LLVMToolchainDir}/usr/)
    add_dependencies(install-distribution-toolchain install-distribution-${target})
  endforeach()
endif()
