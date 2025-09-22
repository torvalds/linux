# This file sets up a CMakeCache for a simple build with multiple distributions.
# Note that for a real distribution, you likely want to perform a bootstrap
# build; see clang/cmake/caches/DistributionExample.cmake and the
# BuildingADistribution documentation for details. This cache file doesn't
# demonstrate bootstrapping so it can focus on the configuration details
# specific to multiple distributions instead.

# Build an optimized toolchain for an example set of targets.
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(LLVM_TARGETS_TO_BUILD
      AArch64
      ARM
      X86
    CACHE STRING "")

# Enable the LLVM projects and runtimes.
set(LLVM_ENABLE_PROJECTS
      clang
      lld
    CACHE STRING "")
set(LLVM_ENABLE_RUNTIMES
      compiler-rt
      libcxx
      libcxxabi
    CACHE STRING "")

# We'll build two distributions: Toolchain, which just holds the tools
# (intended for most end users), and Development, which has libraries (for end
# users who wish to develop their own tooling using those libraries). This will
# produce the install-toolchain-distribution and install-development-distribution
# targets to install the distributions.
set(LLVM_DISTRIBUTIONS
      Toolchain
      Development
    CACHE STRING "")

# We want to include the C++ headers in our distribution.
set(LLVM_RUNTIME_DISTRIBUTION_COMPONENTS
      cxx-headers
    CACHE STRING "")

# You likely want more tools; this is just an example :) Note that we need to
# include cxx-headers explicitly here (in addition to it being added to
# LLVM_RUNTIME_DISTRIBUTION_COMPONENTS above).
set(LLVM_Toolchain_DISTRIBUTION_COMPONENTS
      builtins
      clang
      clang-resource-headers
      cxx-headers
      lld
      llvm-objdump
    CACHE STRING "")

# Note that we need to include the CMake exports targets for the distribution
# (development-cmake-exports and clang-development-cmake-exports), as well as
# the general CMake exports target for each project (cmake-exports and
# clang-cmake-exports), in our list of targets. The distribution CMake exports
# targets just install the CMake exports file for the distribution's targets,
# whereas the project CMake exports targets install the rest of the project's
# CMake exports (which are needed in order to import the project from other
# CMake_projects via find_package, and include the distribution's CMake exports
# file to get the exported targets).
set(LLVM_Development_DISTRIBUTION_COMPONENTS
      # LLVM
      cmake-exports
      development-cmake-exports
      llvm-headers
      llvm-libraries
      # Clang
      clang-cmake-exports
      clang-development-cmake-exports
      clang-headers
      clang-libraries
    CACHE STRING "")
