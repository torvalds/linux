# Workaround for MSVC ARM64 performance regression:
# https://developercommunity.visualstudio.com/t/Compiling-a-specific-code-for-ARM64-with/10444970
# Since /O1 and /O2 represent a set of optimizations,
# our goal is to disable the /Og flag while retaining the other optimizations from the /O1|/O2 set
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES Clang
    AND MSVC_VERSION VERSION_GREATER_EQUAL 1932
    AND MSVC_VERSION VERSION_LESS 1939
    AND CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")

  string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)
  string(REGEX MATCHALL "/[Oo][12]" opt_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${uppercase_CMAKE_BUILD_TYPE}}")
  if (opt_flags)
    if(opt_flags MATCHES "1$")
      set(opt_flags "/Od;/Os;/Oy;/Ob2;/GF;/Gy")
    elseif (opt_flags MATCHES "2$")
      set(opt_flags "/Od;/Oi;/Ot;/Oy;/Ob2;/GF;/Gy")
    endif()
    set_source_files_properties(StandardLibrary.cpp PROPERTIES COMPILE_OPTIONS "${opt_flags}")
  endif()
endif()

add_clang_library(clangToolingInclusionsStdlib
  StandardLibrary.cpp

  LINK_LIBS
  clangAST
  )
