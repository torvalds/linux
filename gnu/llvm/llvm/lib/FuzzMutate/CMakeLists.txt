# Generic helper for fuzzer binaries.
# This should not depend on LLVM IR etc.
add_llvm_component_library(LLVMFuzzerCLI
  FuzzerCLI.cpp
  PARTIAL_SOURCES_INTENDED

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/FuzzMutate

  LINK_COMPONENTS
  Support
  TargetParser
  )

# Library for using LLVM IR together with fuzzers.
add_llvm_component_library(LLVMFuzzMutate
  IRMutator.cpp
  OpDescriptor.cpp
  Operations.cpp
  RandomIRBuilder.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/FuzzMutate

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  BitReader
  BitWriter
  Core
  Scalar
  Support
  TargetParser
  TransformUtils
  )
