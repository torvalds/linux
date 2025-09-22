# HandleLibcxxFlags - A set of macros used to setup the flags used to compile
# and link libc++abi. These macros add flags to the following CMake variables.
# - LIBUNWIND_COMPILE_FLAGS: flags used to compile libunwind
# - LIBUNWIND_LINK_FLAGS: flags used to link libunwind
# - LIBUNWIND_LIBRARIES: libraries to link libunwind to.

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(HandleFlags)

unset(add_flag_if_supported)

# Add a list of flags to 'LIBUNWIND_COMPILE_FLAGS'.
macro(add_compile_flags)
  foreach(f ${ARGN})
    list(APPEND LIBUNWIND_COMPILE_FLAGS ${f})
  endforeach()
endmacro()

# If 'condition' is true then add the specified list of flags to
# 'LIBUNWIND_COMPILE_FLAGS'
macro(add_compile_flags_if condition)
  if (${condition})
    add_compile_flags(${ARGN})
  endif()
endmacro()

# For each specified flag, add that flag to 'LIBUNWIND_COMPILE_FLAGS' if the
# flag is supported by the C++ compiler.
macro(add_compile_flags_if_supported)
  foreach(flag ${ARGN})
      mangle_name("${flag}" flagname)
      check_cxx_compiler_flag("${flag}" "CXX_SUPPORTS_${flagname}_FLAG")
      add_compile_flags_if(CXX_SUPPORTS_${flagname}_FLAG ${flag})
  endforeach()
endmacro()

# Add a list of flags to 'LIBUNWIND_C_FLAGS'.
macro(add_c_flags)
  foreach(f ${ARGN})
    list(APPEND LIBUNWIND_C_FLAGS ${f})
  endforeach()
endmacro()

# If 'condition' is true then add the specified list of flags to
# 'LIBUNWIND_C_FLAGS'
macro(add_c_flags_if condition)
  if (${condition})
    add_c_flags(${ARGN})
  endif()
endmacro()

# Add a list of flags to 'LIBUNWIND_CXX_FLAGS'.
macro(add_cxx_flags)
  foreach(f ${ARGN})
    list(APPEND LIBUNWIND_CXX_FLAGS ${f})
  endforeach()
endmacro()

# If 'condition' is true then add the specified list of flags to
# 'LIBUNWIND_CXX_FLAGS'
macro(add_cxx_flags_if condition)
  if (${condition})
    add_cxx_flags(${ARGN})
  endif()
endmacro()

# For each specified flag, add that flag to 'LIBUNWIND_CXX_FLAGS' if the
# flag is supported by the C compiler.
macro(add_cxx_compile_flags_if_supported)
  foreach(flag ${ARGN})
      mangle_name("${flag}" flagname)
      check_cxx_compiler_flag("${flag}" "CXX_SUPPORTS_${flagname}_FLAG")
      add_cxx_flags_if(CXX_SUPPORTS_${flagname}_FLAG ${flag})
  endforeach()
endmacro()

# Add a list of flags to 'LIBUNWIND_LINK_FLAGS'.
macro(add_link_flags)
  foreach(f ${ARGN})
    list(APPEND LIBUNWIND_LINK_FLAGS ${f})
  endforeach()
endmacro()

# If 'condition' is true then add the specified list of flags to
# 'LIBUNWIND_LINK_FLAGS'
macro(add_link_flags_if condition)
  if (${condition})
    add_link_flags(${ARGN})
  endif()
endmacro()

# For each specified flag, add that flag to 'LIBUNWIND_LINK_FLAGS' if the
# flag is supported by the C++ compiler.
macro(add_link_flags_if_supported)
  foreach(flag ${ARGN})
    mangle_name("${flag}" flagname)
    check_cxx_compiler_flag("${flag}" "CXX_SUPPORTS_${flagname}_FLAG")
    add_link_flags_if(CXX_SUPPORTS_${flagname}_FLAG ${flag})
  endforeach()
endmacro()

# Add a list of libraries or link flags to 'LIBUNWIND_LIBRARIES'.
macro(add_library_flags)
  foreach(lib ${ARGN})
    list(APPEND LIBUNWIND_LIBRARIES ${lib})
  endforeach()
endmacro()

# if 'condition' is true then add the specified list of libraries and flags
# to 'LIBUNWIND_LIBRARIES'.
macro(add_library_flags_if condition)
  if(${condition})
    add_library_flags(${ARGN})
  endif()
endmacro()
