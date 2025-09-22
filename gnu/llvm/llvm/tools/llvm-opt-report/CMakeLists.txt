set(LLVM_LINK_COMPONENTS
  Demangle
  Remarks
  Support
  )

add_llvm_tool(llvm-opt-report
  OptReport.cpp
  )

