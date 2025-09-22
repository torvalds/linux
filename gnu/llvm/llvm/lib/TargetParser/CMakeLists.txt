# Ensure that libLLVMTargetParser does not carry any static global initializer.
# ManagedStatic can be used to enable lazy-initialization of globals.
#
# HAS_WERROR_GLOBAL_CTORS and LLVM_HAS_NOGLOBAL_CTOR_MUTEX should have been set
# by llvm/lib/Support/CMakeLists.txt (which provides the required Support
# dependency).
if (HAS_WERROR_GLOBAL_CTORS AND NOT LLVM_HAS_NOGLOBAL_CTOR_MUTEX)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=global-constructors")
endif()

# Solaris code uses kstat, so specify dependency explicitly for shared builds.
if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
  set(system_libs kstat)
endif()

add_llvm_component_library(LLVMTargetParser
  AArch64TargetParser.cpp
  ARMTargetParserCommon.cpp
  ARMTargetParser.cpp
  CSKYTargetParser.cpp
  Host.cpp
  LoongArchTargetParser.cpp
  RISCVISAInfo.cpp
  RISCVTargetParser.cpp
  SubtargetFeature.cpp
  TargetParser.cpp
  Triple.cpp
  X86TargetParser.cpp

  ADDITIONAL_HEADER_DIRS
  Unix
  Windows

  LINK_LIBS
  ${system_libs}

  LINK_COMPONENTS
  Support

  DEPENDS
  ARMTargetParserTableGen
  AArch64TargetParserTableGen
  RISCVTargetParserTableGen
  )
