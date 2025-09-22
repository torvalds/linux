add_llvm_component_library(LLVMSymbolize
  DIPrinter.cpp
  Markup.cpp
  MarkupFilter.cpp
  SymbolizableObjectFile.cpp
  Symbolize.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/DebugInfo/Symbolize

  LINK_COMPONENTS
  DebugInfoDWARF
  DebugInfoPDB
  DebugInfoBTF
  Object
  Support
  Demangle
  TargetParser
  )
