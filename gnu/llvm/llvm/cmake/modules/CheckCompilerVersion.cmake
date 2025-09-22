# Check if the host compiler is new enough.
# These versions are updated based on the following policy:
#   llvm.org/docs/DeveloperPolicy.html#toolchain

include(CheckCXXSourceCompiles)

set(GCC_MIN 7.4)
set(GCC_SOFT_ERROR 7.4)
set(CLANG_MIN 5.0)
set(CLANG_SOFT_ERROR 5.0)
set(APPLECLANG_MIN 10.0)
set(APPLECLANG_SOFT_ERROR 10.0)

# https://en.wikipedia.org/wiki/Microsoft_Visual_C#Internal_version_numbering
# _MSC_VER == 1927 MSVC++ 14.27 Visual Studio 2019 Version 16.7
set(MSVC_MIN 19.27)
set(MSVC_SOFT_ERROR 19.27)

set(LIBSTDCXX_MIN 7)
set(LIBSTDCXX_SOFT_ERROR 7)


if(DEFINED LLVM_COMPILER_CHECKED)
  return()
endif()
set(LLVM_COMPILER_CHECKED ON)

if(LLVM_FORCE_USE_OLD_TOOLCHAIN)
  return()
endif()

function(check_compiler_version NAME NICE_NAME MINIMUM_VERSION SOFT_ERROR_VERSION)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL NAME)
    return()
  endif()
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS MINIMUM_VERSION)
    message(FATAL_ERROR "Host ${NICE_NAME} version must be at least ${MINIMUM_VERSION}, your version is ${CMAKE_CXX_COMPILER_VERSION}.")
  elseif(CMAKE_CXX_COMPILER_VERSION VERSION_LESS SOFT_ERROR_VERSION)
    if(LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN)
      message(WARNING "Host ${NICE_NAME} version should be at least ${SOFT_ERROR_VERSION} because LLVM will soon use new C++ features which your toolchain version doesn't support. Your version is ${CMAKE_CXX_COMPILER_VERSION}. Ignoring because you've set LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN, but very soon your toolchain won't be supported.")
    else()
      message(FATAL_ERROR "Host ${NICE_NAME} version should be at least ${SOFT_ERROR_VERSION} because LLVM will soon use new C++ features which your toolchain version doesn't support. Your version is ${CMAKE_CXX_COMPILER_VERSION}. You can temporarily opt out using LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN, but very soon your toolchain won't be supported.")
    endif()
  endif()
endfunction(check_compiler_version)

check_compiler_version("GNU" "GCC" ${GCC_MIN} ${GCC_SOFT_ERROR})
check_compiler_version("Clang" "Clang" ${CLANG_MIN} ${CLANG_SOFT_ERROR})
check_compiler_version("AppleClang" "Apple Clang" ${APPLECLANG_MIN} ${APPLECLANG_SOFT_ERROR})
check_compiler_version("MSVC" "Visual Studio" ${MSVC_MIN} ${MSVC_SOFT_ERROR})

# See https://developercommunity.visualstudio.com/content/problem/845933/miscompile-boolean-condition-deduced-to-be-always.html
# and thread "[llvm-dev] Longstanding failing tests - clang-tidy, MachO, Polly"
# on llvm-dev Jan 21-23 2020.
if ((${CMAKE_CXX_COMPILER_ID} STREQUAL MSVC) AND
    (19.24 VERSION_LESS_EQUAL ${CMAKE_CXX_COMPILER_VERSION}) AND
    (${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 19.25))
  if(LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN)
    message(WARNING "Host Visual Studio version 16.4 is known to miscompile part of LLVM")
  else()
    message(FATAL_ERROR "Host Visual Studio version 16.4 is known to miscompile part of LLVM, please use clang-cl or upgrade to 16.5 or above (use -DLLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN=ON to ignore)")
  endif()
endif()


if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if (CMAKE_CXX_SIMULATE_ID MATCHES "MSVC")
    if (CMAKE_CXX_SIMULATE_VERSION VERSION_LESS MSVC_MIN)
      message(FATAL_ERROR "Host Clang must have at least -fms-compatibility-version=${MSVC_MIN}, your version is ${CMAKE_CXX_SIMULATE_VERSION}.")
    endif()
    set(CLANG_CL 1)
  elseif(NOT LLVM_ENABLE_LIBCXX)
    # Test that we aren't using too old of a version of libstdc++.
    set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
    set(OLD_CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++0x")
    check_cxx_source_compiles("
#include <iosfwd>
#if defined(__GLIBCXX__)
#if !defined(_GLIBCXX_RELEASE) || _GLIBCXX_RELEASE < ${LIBSTDCXX_MIN}
#error Unsupported libstdc++ version
#endif
#endif
int main() { return 0; }
"
      LLVM_LIBSTDCXX_MIN)
    if(NOT LLVM_LIBSTDCXX_MIN)
      message(FATAL_ERROR "libstdc++ version must be at least ${GCC_MIN}.")
    endif()
    check_cxx_source_compiles("
#include <iosfwd>
#if defined(__GLIBCXX__)
#if !defined(_GLIBCXX_RELEASE) || _GLIBCXX_RELEASE < ${LIBSTDCXX_SOFT_ERROR}
#error Unsupported libstdc++ version
#endif
#endif
int main() { return 0; }
"
      LLVM_LIBSTDCXX_SOFT_ERROR)
    if(NOT LLVM_LIBSTDCXX_SOFT_ERROR)
      if(LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN)
        message(WARNING "libstdc++ version should be at least ${LIBSTDCXX_SOFT_ERROR} because LLVM will soon use new C++ features which your toolchain version doesn't support. Ignoring because you've set LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN, but very soon your toolchain won't be supported.")
      else()
        message(FATAL_ERROR "libstdc++ version should be at least ${LIBSTDCXX_SOFT_ERROR} because LLVM will soon use new C++ features which your toolchain version doesn't support. You can temporarily opt out using LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN, but very soon your toolchain won't be supported.")
      endif()
    endif()
    set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
    set(CMAKE_REQUIRED_LIBRARIES ${OLD_CMAKE_REQUIRED_LIBRARIES})
  endif()
endif()
