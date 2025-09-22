add_lldb_tool(lldb-instr
  Instrument.cpp

  CLANG_LIBS
    clangAST
    clangBasic
    clangCodeGen
    clangFrontend
    clangLex
    clangRewrite
    clangSerialization
    clangTooling

  LINK_COMPONENTS
    Support
  )
