set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  )

set(BUILDVARIABLES_SRCPATH ${CMAKE_CURRENT_SOURCE_DIR}/BuildVariables.inc.in)
set(BUILDVARIABLES_OBJPATH ${CMAKE_CURRENT_BINARY_DIR}/BuildVariables.inc)

# Add the llvm-config tool.
add_llvm_tool(llvm-config
  llvm-config.cpp
  # This utility doesn't use much of LLVM, so linking a shared library for the
  # entire thing is overkill. Avoiding that especially saves on build time when cross
  # compiling LLVM and building both cross and native `llvm-config`s. We don't
  # want to build an entire native libLLVM.so in addition to the cross one just
  # for the native `llvm-config`!
  DISABLE_LLVM_LINK_LLVM_DYLIB
  )

# Compute the substitution values for various items.
get_property(SUPPORT_SYSTEM_LIBS TARGET LLVMSupport PROPERTY LLVM_SYSTEM_LIBS)
get_property(WINDOWSMANIFEST_SYSTEM_LIBS TARGET LLVMWindowsManifest PROPERTY LLVM_SYSTEM_LIBS)

foreach(l ${SUPPORT_SYSTEM_LIBS} ${WINDOWSMANIFEST_SYSTEM_LIBS})
  if(MSVC)
    if(IS_ABSOLUTE ${l})
      set(SYSTEM_LIBS ${SYSTEM_LIBS} "${l}")
    else()
      set(SYSTEM_LIBS ${SYSTEM_LIBS} "${l}.lib")
    endif()
  else()
    if (l MATCHES "^-")
      # If it's an option, pass it without changes.
      set(SYSTEM_LIBS ${SYSTEM_LIBS} "${l}")
    else()
      # Otherwise assume it's a library name we need to link with.
      if(IS_ABSOLUTE ${l})
        set(SYSTEM_LIBS ${SYSTEM_LIBS} "${l}")
      else()
        set(SYSTEM_LIBS ${SYSTEM_LIBS} "-l${l}")
      endif()
    endif()
  endif()
endforeach()

string(REPLACE ";" " " SYSTEM_LIBS "${SYSTEM_LIBS}")

# Fetch target specific compile options, e.g. RTTI option
get_property(COMPILE_FLAGS TARGET llvm-config PROPERTY COMPILE_FLAGS)

# NOTE: We don't want to start extracting any random C/CXX flags that the
# user may add that could affect the ABI.  We only want to extract flags
# that have been added by the LLVM build system.
string(REGEX MATCH "-stdlib=[^ ]+" LLVM_CXX_STDLIB_FLAG ${CMAKE_CXX_FLAGS})
string(REGEX MATCH "-std=[^ ]+" LLVM_C_STD_FLAG ${CMAKE_C_FLAGS})

# Use configure_file to create BuildVariables.inc.
set(LLVM_SRC_ROOT ${LLVM_MAIN_SRC_DIR})
set(LLVM_OBJ_ROOT ${LLVM_BINARY_DIR})
set(LLVM_CPPFLAGS "${LLVM_DEFINITIONS}")
set(LLVM_CFLAGS "${LLVM_C_STD_FLAG} ${LLVM_DEFINITIONS}")
# The language standard potentially affects the ABI/API of LLVM, so we want
# to make sure it is reported by llvm-config.
set(LLVM_CXXFLAGS "${CMAKE_CXX${CMAKE_CXX_STANDARD}_STANDARD_COMPILE_OPTION} ${LLVM_CXX_STDLIB_FLAG} ${COMPILE_FLAGS} ${LLVM_DEFINITIONS}")
set(LLVM_BUILD_SYSTEM cmake)
set(LLVM_HAS_RTTI ${LLVM_CONFIG_HAS_RTTI})
set(LLVM_DYLIB_VERSION "${LLVM_VERSION_MAJOR}${LLVM_VERSION_SUFFIX}")

# Use the C++ link flags, since they should be a superset of C link flags.
set(LLVM_LDFLAGS "${CMAKE_CXX_LINK_FLAGS}")
set(LLVM_BUILDMODE ${CMAKE_BUILD_TYPE})
set(LLVM_SYSTEM_LIBS ${SYSTEM_LIBS})
string(REPLACE ";" " " LLVM_TARGETS_BUILT "${LLVM_TARGETS_TO_BUILD}")
llvm_canonicalize_cmake_booleans(
  LLVM_BUILD_LLVM_DYLIB
  LLVM_LINK_LLVM_DYLIB
  LLVM_HAS_RTTI
  BUILD_SHARED_LIBS)
llvm_expand_pseudo_components(LLVM_DYLIB_COMPONENTS_expanded "${LLVM_DYLIB_COMPONENTS}")
configure_file(${BUILDVARIABLES_SRCPATH} ${BUILDVARIABLES_OBJPATH} @ONLY)

# Set build-time environment(s).
add_compile_definitions(CMAKE_CFG_INTDIR="$<CONFIG>")

if(LLVM_ENABLE_MODULES)
  target_compile_options(llvm-config PUBLIC
    "-fmodules-ignore-macro=CMAKE_CFG_INTDIR"
    )
endif()

# Add the dependency on the generation step.
add_file_dependencies(${CMAKE_CURRENT_SOURCE_DIR}/llvm-config.cpp ${BUILDVARIABLES_OBJPATH})

if(CMAKE_CROSSCOMPILING)
  if (LLVM_NATIVE_TOOL_DIR AND NOT LLVM_CONFIG_PATH)
    if (EXISTS "${LLVM_NATIVE_TOOL_DIR}/llvm-config${LLVM_HOST_EXECUTABLE_SUFFIX}")
      set(LLVM_CONFIG_PATH "${LLVM_NATIVE_TOOL_DIR}/llvm-config${LLVM_HOST_EXECUTABLE_SUFFIX}")
    endif()
  endif()

  if (NOT LLVM_CONFIG_PATH)
    build_native_tool(llvm-config LLVM_CONFIG_PATH)
    set(LLVM_CONFIG_PATH "${LLVM_CONFIG_PATH}" CACHE STRING "")

    add_custom_target(NativeLLVMConfig DEPENDS ${LLVM_CONFIG_PATH})
    add_dependencies(llvm-config NativeLLVMConfig)
  endif()
endif()
