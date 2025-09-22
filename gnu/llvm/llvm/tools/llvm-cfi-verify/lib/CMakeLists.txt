set(LLVM_LINK_COMPONENTS
    DebugInfoDWARF
    MC
    MCParser
    Object
    Support
    Symbolize
    TargetParser
    )

add_llvm_library(LLVMCFIVerify
  STATIC
  FileAnalysis.cpp
  FileAnalysis.h
  GraphBuilder.cpp
  GraphBuilder.h
  )
