# Basic utilities which is the strict minimum needed to build
# llvm-min-tblgen.
add_subdirectory(Basic)
# Common utilities are all of the reusable components and helper
# code needed by the backends.
add_subdirectory(Common)

set(LLVM_LINK_COMPONENTS Support)

# llvm-min-tablegen only contains a subset of backends necessary to
# build llvm/include. It must not depend on TableGenCommon, as
# TableGenCommon depends on this already to generate things such as
# ValueType definitions.
add_tablegen(llvm-min-tblgen LLVM_HEADERS
  TableGen.cpp
  ARMTargetDefEmitter.cpp
  Attributes.cpp
  DirectiveEmitter.cpp
  IntrinsicEmitter.cpp
  RISCVTargetDefEmitter.cpp
  VTEmitter.cpp
  $<TARGET_OBJECTS:obj.LLVMTableGenBasic>

  PARTIAL_SOURCES_INTENDED
  )

set(LLVM_LINK_COMPONENTS
  CodeGenTypes
  Support
  )

add_tablegen(llvm-tblgen LLVM
  DESTINATION "${LLVM_TOOLS_INSTALL_DIR}"
  EXPORT LLVM
  ARMTargetDefEmitter.cpp
  AsmMatcherEmitter.cpp
  AsmWriterEmitter.cpp
  Attributes.cpp
  CallingConvEmitter.cpp
  CodeEmitterGen.cpp
  CodeGenMapTable.cpp
  CompressInstEmitter.cpp
  CTagsEmitter.cpp
  DAGISelEmitter.cpp
  DAGISelMatcherEmitter.cpp
  DAGISelMatcherGen.cpp
  DAGISelMatcherOpt.cpp
  DecoderEmitter.cpp
  DFAEmitter.cpp
  DFAPacketizerEmitter.cpp
  DirectiveEmitter.cpp
  DisassemblerEmitter.cpp
  DXILEmitter.cpp
  ExegesisEmitter.cpp
  FastISelEmitter.cpp
  GlobalISelCombinerEmitter.cpp
  GlobalISelEmitter.cpp
  InstrDocsEmitter.cpp
  InstrInfoEmitter.cpp
  IntrinsicEmitter.cpp
  MacroFusionPredicatorEmitter.cpp
  OptParserEmitter.cpp
  OptRSTEmitter.cpp
  PseudoLoweringEmitter.cpp
  RegisterBankEmitter.cpp
  RegisterInfoEmitter.cpp
  RISCVTargetDefEmitter.cpp
  SearchableTableEmitter.cpp
  SubtargetEmitter.cpp
  TableGen.cpp
  VTEmitter.cpp
  WebAssemblyDisassemblerEmitter.cpp
  X86InstrMappingEmitter.cpp
  X86DisassemblerTables.cpp
  X86FoldTablesEmitter.cpp
  X86MnemonicTables.cpp
  X86ModRMFilters.cpp
  X86RecognizableInstr.cpp
  $<TARGET_OBJECTS:obj.LLVMTableGenBasic>
  $<TARGET_OBJECTS:obj.LLVMTableGenCommon>

  DEPENDS
  intrinsics_gen # via llvm-min-tablegen
  )
