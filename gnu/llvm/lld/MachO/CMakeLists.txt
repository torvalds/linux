set(LLVM_TARGET_DEFINITIONS Options.td)
tablegen(LLVM Options.inc -gen-opt-parser-defs)
add_public_tablegen_target(MachOOptionsTableGen)

include_directories(${LLVM_MAIN_SRC_DIR}/../libunwind/include)

add_lld_library(lldMachO
  Arch/ARM64.cpp
  Arch/ARM64Common.cpp
  Arch/ARM64_32.cpp
  Arch/X86_64.cpp
  ConcatOutputSection.cpp
  Driver.cpp
  DriverUtils.cpp
  Dwarf.cpp
  EhFrame.cpp
  ExportTrie.cpp
  ICF.cpp
  InputFiles.cpp
  InputSection.cpp
  LTO.cpp
  MapFile.cpp
  MarkLive.cpp
  ObjC.cpp
  OutputSection.cpp
  OutputSegment.cpp
  Relocations.cpp
  SectionPriorities.cpp
  SymbolTable.cpp
  Symbols.cpp
  SyntheticSections.cpp
  Target.cpp
  UnwindInfoSection.cpp
  Writer.cpp

  LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  BinaryFormat
  BitReader
  BitWriter
  Core
  DebugInfoDWARF
  Demangle
  LTO
  MC
  ObjCARCOpts
  Object
  Option
  Passes
  Support
  TargetParser
  TextAPI

  LINK_LIBS
  lldCommon
  ${LLVM_PTHREAD_LIB}

  DEPENDS
  MachOOptionsTableGen
  ${tablegen_deps}
  )
