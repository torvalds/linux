#===============================================================================
# Define targets for linking against the selected ABI library
#
# After including this file, the following targets are defined:
# - libcxx-abi-headers: An interface target that allows getting access to the
#                       headers of the selected ABI library.
# - libcxx-abi-shared: A target representing the selected shared ABI library.
# - libcxx-abi-static: A target representing the selected static ABI library.
#
# Furthermore, some ABI libraries also define the following target:
# - libcxx-abi-shared-objects: An object library representing a set of object files
#                              constituting the ABI library, suitable for bundling
#                              into a shared library.
# - libcxx-abi-static-objects: An object library representing a set of object files
#                              constituting the ABI library, suitable for bundling
#                              into a static library.
#===============================================================================

include(GNUInstallDirs)

# This function copies the provided headers to a private directory and adds that
# path to the given INTERFACE target. That target can then be linked against to
# get access to those headers (and only those).
#
# The problem this solves is that when building against a system-provided ABI library,
# the ABI headers might live side-by-side with an actual C++ Standard Library
# installation. For that reason, we can't just add `-I <path-to-ABI-headers>`,
# since we would end up also adding the system-provided C++ Standard Library to
# the search path. Instead, what we do is copy just the ABI library headers to
# a private directory and add just that path when we build libc++.
function(import_private_headers target include_dirs headers)
  foreach(header ${headers})
    set(found FALSE)
    foreach(incpath ${include_dirs})
      if (EXISTS "${incpath}/${header}")
        set(found TRUE)
        message(STATUS "Looking for ${header} in ${incpath} - found")
        get_filename_component(dstdir ${header} PATH)
        get_filename_component(header_file ${header} NAME)
        set(src ${incpath}/${header})
        set(dst "${LIBCXX_BINARY_DIR}/private-abi-headers/${dstdir}/${header_file}")

        add_custom_command(OUTPUT ${dst}
            DEPENDS ${src}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${LIBCXX_BINARY_DIR}/private-abi-headers/${dstdir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
            COMMENT "Copying C++ ABI header ${header}")
        list(APPEND abilib_headers "${dst}")
      else()
        message(STATUS "Looking for ${header} in ${incpath} - not found")
      endif()
    endforeach()
    if (NOT found)
      message(WARNING "Failed to find ${header} in ${include_dirs}")
    endif()
  endforeach()

  # Work around https://gitlab.kitware.com/cmake/cmake/-/issues/18399
  add_library(${target}-generate-private-headers OBJECT ${abilib_headers})
  set_target_properties(${target}-generate-private-headers PROPERTIES LINKER_LANGUAGE CXX)

  target_link_libraries(${target} INTERFACE ${target}-generate-private-headers)
  target_include_directories(${target} INTERFACE "${LIBCXX_BINARY_DIR}/private-abi-headers")
endfunction()

# This function creates an imported static library named <target>.
# It imports a library named <name> searched at the given <path>.
function(import_static_library target path name)
  add_library(${target} STATIC IMPORTED GLOBAL)
  find_library(file
    NAMES "${CMAKE_STATIC_LIBRARY_PREFIX}${name}${CMAKE_STATIC_LIBRARY_SUFFIX}"
    PATHS "${path}"
    NO_CACHE)
  set_target_properties(${target} PROPERTIES IMPORTED_LOCATION "${file}")
endfunction()

# This function creates an imported shared (interface) library named <target>
# for the given library <name>.
function(import_shared_library target name)
  add_library(${target} INTERFACE IMPORTED GLOBAL)
  set_target_properties(${target} PROPERTIES IMPORTED_LIBNAME "${name}")
endfunction()

# Link against a system-provided libstdc++
if ("${LIBCXX_CXX_ABI}" STREQUAL "libstdc++")
  if(NOT LIBCXX_CXX_ABI_INCLUDE_PATHS)
    message(FATAL_ERROR "LIBCXX_CXX_ABI_INCLUDE_PATHS must be set when selecting libstdc++ as an ABI library")
  endif()

  add_library(libcxx-abi-headers INTERFACE)
  import_private_headers(libcxx-abi-headers "${LIBCXX_CXX_ABI_INCLUDE_PATHS}"
    "cxxabi.h;bits/c++config.h;bits/os_defines.h;bits/cpu_defines.h;bits/cxxabi_tweaks.h;bits/cxxabi_forced.h")
  target_compile_definitions(libcxx-abi-headers INTERFACE "-DLIBSTDCXX" "-D__GLIBCXX__")

  import_shared_library(libcxx-abi-shared stdc++)
  target_link_libraries(libcxx-abi-shared INTERFACE libcxx-abi-headers)

  import_static_library(libcxx-abi-static "${LIBCXX_CXX_ABI_LIBRARY_PATH}" stdc++)
  target_link_libraries(libcxx-abi-static INTERFACE libcxx-abi-headers)

# Link against a system-provided libsupc++
elseif ("${LIBCXX_CXX_ABI}" STREQUAL "libsupc++")
  if(NOT LIBCXX_CXX_ABI_INCLUDE_PATHS)
    message(FATAL_ERROR "LIBCXX_CXX_ABI_INCLUDE_PATHS must be set when selecting libsupc++ as an ABI library")
  endif()

  add_library(libcxx-abi-headers INTERFACE)
  import_private_headers(libcxx-abi-headers "${LIBCXX_CXX_ABI_INCLUDE_PATHS}"
    "cxxabi.h;bits/c++config.h;bits/os_defines.h;bits/cpu_defines.h;bits/cxxabi_tweaks.h;bits/cxxabi_forced.h")
  target_compile_definitions(libcxx-abi-headers INTERFACE "-D__GLIBCXX__")

  import_shared_library(libcxx-abi-shared supc++)
  target_link_libraries(libcxx-abi-shared INTERFACE libcxx-abi-headers)

  import_static_library(libcxx-abi-static "${LIBCXX_CXX_ABI_LIBRARY_PATH}" supc++)
  target_link_libraries(libcxx-abi-static INTERFACE libcxx-abi-headers)

# Link against the in-tree libc++abi
elseif ("${LIBCXX_CXX_ABI}" STREQUAL "libcxxabi")
  add_library(libcxx-abi-headers INTERFACE)
  target_link_libraries(libcxx-abi-headers INTERFACE cxxabi-headers)
  target_compile_definitions(libcxx-abi-headers INTERFACE "-DLIBCXX_BUILDING_LIBCXXABI")

  if (TARGET cxxabi_shared)
    add_library(libcxx-abi-shared INTERFACE)
    target_link_libraries(libcxx-abi-shared INTERFACE cxxabi_shared)

    # When using the in-tree libc++abi as an ABI library, libc++ re-exports the
    # libc++abi symbols (on platforms where it can) because libc++abi is only an
    # implementation detail of libc++.
    target_link_libraries(libcxx-abi-shared INTERFACE cxxabi-reexports)

    # Populate the OUTPUT_NAME property of libcxx-abi-shared because that is used when
    # generating a linker script.
    get_target_property(_output_name cxxabi_shared OUTPUT_NAME)
    set_target_properties(libcxx-abi-shared PROPERTIES "OUTPUT_NAME" "${_output_name}")
  endif()

  if (TARGET cxxabi_static)
    add_library(libcxx-abi-static ALIAS cxxabi_static)
  endif()

  if (TARGET cxxabi_shared_objects)
    add_library(libcxx-abi-shared-objects ALIAS cxxabi_shared_objects)
  endif()

  if (TARGET cxxabi_static_objects)
    add_library(libcxx-abi-static-objects ALIAS cxxabi_static_objects)
  endif()

# Link against a system-provided libc++abi
elseif ("${LIBCXX_CXX_ABI}" STREQUAL "system-libcxxabi")
  if(NOT LIBCXX_CXX_ABI_INCLUDE_PATHS)
    message(FATAL_ERROR "LIBCXX_CXX_ABI_INCLUDE_PATHS must be set when selecting system-libcxxabi as an ABI library")
  endif()

  add_library(libcxx-abi-headers INTERFACE)
  import_private_headers(libcxx-abi-headers "${LIBCXX_CXX_ABI_INCLUDE_PATHS}" "cxxabi.h;__cxxabi_config.h")
  target_compile_definitions(libcxx-abi-headers INTERFACE "-DLIBCXX_BUILDING_LIBCXXABI")

  import_shared_library(libcxx-abi-shared c++abi)
  target_link_libraries(libcxx-abi-shared INTERFACE libcxx-abi-headers)

  import_static_library(libcxx-abi-static "${LIBCXX_CXX_ABI_LIBRARY_PATH}" c++abi)
  target_link_libraries(libcxx-abi-static INTERFACE libcxx-abi-headers)

# Link against a system-provided libcxxrt
elseif ("${LIBCXX_CXX_ABI}" STREQUAL "libcxxrt")
  if(NOT LIBCXX_CXX_ABI_INCLUDE_PATHS)
    message(STATUS "LIBCXX_CXX_ABI_INCLUDE_PATHS not set, using /usr/include/c++/v1")
    set(LIBCXX_CXX_ABI_INCLUDE_PATHS "/usr/include/c++/v1")
  endif()
  add_library(libcxx-abi-headers INTERFACE)
  import_private_headers(libcxx-abi-headers "${LIBCXX_CXX_ABI_INCLUDE_PATHS}"
    "cxxabi.h;unwind.h;unwind-arm.h;unwind-itanium.h")
  target_compile_definitions(libcxx-abi-headers INTERFACE "-DLIBCXXRT")

  import_shared_library(libcxx-abi-shared cxxrt)
  target_link_libraries(libcxx-abi-shared INTERFACE libcxx-abi-headers)

  import_static_library(libcxx-abi-static "${LIBCXX_CXX_ABI_LIBRARY_PATH}" cxxrt)
  target_link_libraries(libcxx-abi-static INTERFACE libcxx-abi-headers)

# Link against a system-provided vcruntime
# FIXME: Figure out how to configure the ABI library on Windows.
elseif ("${LIBCXX_CXX_ABI}" STREQUAL "vcruntime")
  add_library(libcxx-abi-headers INTERFACE)
  add_library(libcxx-abi-shared INTERFACE)
  add_library(libcxx-abi-static INTERFACE)

# Don't link against any ABI library
elseif ("${LIBCXX_CXX_ABI}" STREQUAL "none")
  add_library(libcxx-abi-headers INTERFACE)
  target_compile_definitions(libcxx-abi-headers INTERFACE "-D_LIBCPP_BUILDING_HAS_NO_ABI_LIBRARY")

  add_library(libcxx-abi-shared INTERFACE)
  target_link_libraries(libcxx-abi-shared INTERFACE libcxx-abi-headers)

  add_library(libcxx-abi-static INTERFACE)
  target_link_libraries(libcxx-abi-static INTERFACE libcxx-abi-headers)
endif()
