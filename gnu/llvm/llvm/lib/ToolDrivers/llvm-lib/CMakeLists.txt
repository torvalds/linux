set(LLVM_LINK_COMPONENTS
  BinaryFormat
  BitReader
  Object
  Option
  Support
  )

set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(LibOptionsTableGen)

add_llvm_component_library(LLVMLibDriver
  LibDriver.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  BinaryFormat
  BitReader
  Object
  Option
  Support
  TargetParser
  )
add_dependencies(LLVMLibDriver LibOptionsTableGen)
