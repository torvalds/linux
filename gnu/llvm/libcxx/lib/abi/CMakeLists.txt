
# This function generates a "unique" identifier based on various properties
# given as arguments. The idea is to encode all ABI-affecting properties
# in that identifier, so that we can store ABI information and associate it
# to a specific ABI configuration.
#
# Right now, this is done by using the ABI identifier as the filename containing
# the list of symbols exported by libc++ for that configuration, however we could
# make it more sophisticated if the number of ABI-affecting parameters grew.
function(cxx_abi_list_identifier result triple abi_library abi_version unstable exceptions new_delete_in_libcxx)
  set(abi_properties)

  if ("${triple}" MATCHES "darwin")
    # Ignore the major, minor, and patchlevel versions of darwin targets.
    string(REGEX REPLACE "darwin[0-9]+\\.[0-9]+\\.[0-9]+" "darwin" triple "${triple}")
  elseif("${triple}" MATCHES "freebsd")
    # Ignore the major and minor versions of freebsd targets.
    string(REGEX REPLACE "freebsd[0-9]+\\.[0-9]+" "freebsd" triple "${triple}")
  endif()
  list(APPEND abi_properties "${triple}")
  list(APPEND abi_properties "${abi_library}")
  list(APPEND abi_properties "v${abi_version}")
  if (${unstable})
    list(APPEND abi_properties "unstable")
  else()
    list(APPEND abi_properties "stable")
  endif()
  if (${exceptions})
    list(APPEND abi_properties "exceptions")
  else()
    list(APPEND abi_properties "noexceptions")
  endif()
  if (${new_delete_in_libcxx})
    list(APPEND abi_properties "new")
  else()
    list(APPEND abi_properties "nonew")
  endif()

  list(JOIN abi_properties "." tmp)
  set(${result} "${tmp}" PARENT_SCOPE)
endfunction()

if (CMAKE_CXX_COMPILER_TARGET)
  set(triple "${CMAKE_CXX_COMPILER_TARGET}")
else()
  set(triple "${LLVM_DEFAULT_TARGET_TRIPLE}")
endif()
cxx_abi_list_identifier(abi_list_identifier
  "${triple}"
  "${LIBCXX_CXX_ABI}"
  "${LIBCXX_ABI_VERSION}"
  "${LIBCXX_ABI_UNSTABLE}"
  "${LIBCXX_ENABLE_EXCEPTIONS}"
  "${LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS}"
)

if (TARGET cxx_shared)
  set(abi_list_file "${CMAKE_CURRENT_SOURCE_DIR}/${abi_list_identifier}.abilist")

  if (EXISTS "${abi_list_file}")
    add_custom_target(check-cxx-abilist
      "${Python3_EXECUTABLE}" "${LIBCXX_SOURCE_DIR}/utils/sym_diff.py"
          --only-stdlib-symbols
          --strict "${abi_list_file}"
          $<TARGET_FILE:cxx_shared>
      DEPENDS cxx_shared
      COMMENT "Testing libc++'s exported symbols against the ABI list")
  else()
    message(STATUS "ABI list file not generated for configuration ${abi_list_identifier}, `check-cxx-abilist` will not be available.")
  endif()

  add_custom_target(generate-cxx-abilist
    COMMAND "${Python3_EXECUTABLE}" "${LIBCXX_SOURCE_DIR}/utils/generate_abi_list.py"
            --output "${abi_list_file}"
            "$<TARGET_FILE:cxx_shared>"
    DEPENDS cxx_shared
    COMMENT "Generating the ABI list file for configuration ${abi_list_identifier}")
else()
  message(STATUS "Not building a shared library for libc++ -- the ABI list targets will not be available.")
endif()
