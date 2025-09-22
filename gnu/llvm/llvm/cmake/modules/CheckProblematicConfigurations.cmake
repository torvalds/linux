
option(LLVM_ALLOW_PROBLEMATIC_CONFIGURATIONS OFF "Set this option to ON to allow problematic toolchain configurations. Use on your own risk.")

macro(log_problematic MESSAGE)
  if(LLVM_ALLOW_PROBLEMATIC_CONFIGURATIONS)
    message(WARNING "${MESSAGE}")
  else()
    message(FATAL_ERROR "${MESSAGE}\nYou can force usage of this configuration by passing -DLLVM_ALLOW_PROBLEMATIC_CONFIGURATIONS=ON")
  endif()
endmacro()

# MSVC and /arch:AVX is untested and have created problems before. See:
# https://github.com/llvm/llvm-project/issues/54645
if(${CMAKE_CXX_COMPILER_ID} STREQUAL MSVC)
  string(TOLOWER "${CMAKE_CXX_FLAGS} ${CMAKE_C_FLAGS}" _FLAGS)
  if(_FLAGS MATCHES "/arch:avx[0-9]*")
    log_problematic("Compiling LLVM with MSVC and the /arch:AVX flag is known to cause issues with parts of LLVM.\nSee https://github.com/llvm/llvm-project/issues/54645 for details.\nUse clang-cl if you want to enable AVX instructions.")
  endif()
endif()
