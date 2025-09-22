#===============================================================================
# Define targets for linking against the selected C library
#
# After including this file, the following targets are defined:
# - libcxx-libc-headers: An interface target that allows getting access to the
#                        headers of the selected C library.
# - libcxx-libc-shared: A target representing the selected shared C library.
# - libcxx-libc-static: A target representing the selected static C library.
#===============================================================================

# Link against a system-provided libc
if (LIBCXX_LIBC STREQUAL "system")
  add_library(libcxx-libc-headers INTERFACE)

  add_library(libcxx-libc-static INTERFACE)
  add_library(libcxx-libc-shared INTERFACE)

# Link against the in-tree LLVM libc
elseif (LIBCXX_LIBC STREQUAL "llvm-libc")
  add_library(libcxx-libc-headers INTERFACE)
  target_link_libraries(libcxx-libc-headers INTERFACE libc-headers)
  if(CXX_SUPPORTS_NOSTDLIBINC_FLAG)
    target_compile_options(libcxx-libc-headers INTERFACE "-nostdlibinc")
  endif()

  add_library(libcxx-libc-static INTERFACE)
  if (TARGET libc)
    target_link_libraries(libcxx-libc-static INTERFACE libc)
  endif()
  if (TARGET libm)
    target_link_libraries(libcxx-libc-static INTERFACE libm)
  endif()
  if (CXX_SUPPORTS_NOLIBC_FLAG)
    target_link_options(libcxx-libc-static INTERFACE "-nolibc")
  endif()

  # TODO: There's no support for building LLVM libc as a shared library yet.
  add_library(libcxx-libc-shared INTERFACE)
endif()
