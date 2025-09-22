# HandleLibcxxFlags - A set of macros used to setup the flags used to compile
# and link libc++. These macros add flags to the following CMake variables.
# - LIBCXX_COMPILE_FLAGS: flags used to compile libc++
# - LIBCXX_LINK_FLAGS: flags used to link libc++
# - LIBCXX_LIBRARIES: libraries to link libc++ to.

include(CheckCXXCompilerFlag)
include(HandleFlags)

unset(add_flag_if_supported)

# Add a specified list of flags to both 'LIBCXX_COMPILE_FLAGS' and
# 'LIBCXX_LINK_FLAGS'.
macro(add_flags)
  foreach(value ${ARGN})
    list(APPEND LIBCXX_COMPILE_FLAGS ${value})
    list(APPEND LIBCXX_LINK_FLAGS ${value})
  endforeach()
endmacro()

# If the specified 'condition' is true then add a list of flags to both
# 'LIBCXX_COMPILE_FLAGS' and 'LIBCXX_LINK_FLAGS'.
macro(add_flags_if condition)
  if (${condition})
    add_flags(${ARGN})
  endif()
endmacro()

# Add each flag in the list to LIBCXX_COMPILE_FLAGS and LIBCXX_LINK_FLAGS
# if that flag is supported by the current compiler.
macro(add_flags_if_supported)
  foreach(flag ${ARGN})
      mangle_name("${flag}" flagname)
      check_cxx_compiler_flag("${flag}" "CXX_SUPPORTS_${flagname}_FLAG")
      add_flags_if(CXX_SUPPORTS_${flagname}_FLAG ${flag})
  endforeach()
endmacro()

# Add a list of flags to 'LIBCXX_LINK_FLAGS'.
macro(add_link_flags)
  foreach(f ${ARGN})
    list(APPEND LIBCXX_LINK_FLAGS ${f})
  endforeach()
endmacro()

# Add a list of libraries or link flags to 'LIBCXX_LIBRARIES'.
macro(add_library_flags)
  foreach(lib ${ARGN})
    list(APPEND LIBCXX_LIBRARIES ${lib})
  endforeach()
endmacro()

# if 'condition' is true then add the specified list of libraries and flags
# to 'LIBCXX_LIBRARIES'.
macro(add_library_flags_if condition)
  if(${condition})
    add_library_flags(${ARGN})
  endif()
endmacro()

# For each specified flag, add that link flag to the provided target.
# The flags are added with the given visibility, i.e. PUBLIC|PRIVATE|INTERFACE.
function(target_add_link_flags_if_supported target visibility)
  foreach(flag ${ARGN})
    mangle_name("${flag}" flagname)
    check_cxx_compiler_flag("${flag}" "CXX_SUPPORTS_${flagname}_FLAG")
    if (CXX_SUPPORTS_${flagname}_FLAG)
      target_link_libraries(${target} ${visibility} ${flag})
    endif()
  endforeach()
endfunction()
