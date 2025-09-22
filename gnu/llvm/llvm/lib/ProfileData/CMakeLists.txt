add_llvm_component_library(LLVMProfileData
  GCOV.cpp
  InstrProf.cpp
  InstrProfCorrelator.cpp
  InstrProfReader.cpp
  InstrProfWriter.cpp
  ItaniumManglingCanonicalizer.cpp
  MemProf.cpp
  MemProfReader.cpp
  PGOCtxProfReader.cpp
  PGOCtxProfWriter.cpp
  ProfileSummaryBuilder.cpp
  SampleProf.cpp
  SampleProfReader.cpp
  SampleProfWriter.cpp
  SymbolRemappingReader.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ProfileData

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  BitstreamReader
  Core
  Object
  Support
  Demangle
  Symbolize
  DebugInfoDWARF
  TargetParser
  )

add_subdirectory(Coverage)
