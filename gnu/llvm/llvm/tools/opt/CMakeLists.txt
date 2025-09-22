set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  AggressiveInstCombine
  Analysis
  AsmParser
  BitWriter
  CFGuard
  CodeGen
  Core
  Coroutines
  Extensions
  IPO
  IRReader
  IRPrinter
  InstCombine
  Instrumentation
  MC
  ObjCARCOpts
  Remarks
  ScalarOpts
  Support
  Target
  TargetParser
  TransformUtils
  Vectorize
  Passes
  )

# We don't want to link this into libLLVM
add_llvm_library(LLVMOptDriver
  STATIC
  NewPMDriver.cpp
  optdriver.cpp
  PARTIAL_SOURCES_INTENDED
  DEPENDS
  intrinsics_gen
)

add_llvm_tool(opt
  PARTIAL_SOURCES_INTENDED
  opt.cpp
  DEPENDS
  intrinsics_gen
  SUPPORT_PLUGINS

  )
target_link_libraries(opt PRIVATE LLVMOptDriver)

export_executable_symbols_for_plugins(opt)
