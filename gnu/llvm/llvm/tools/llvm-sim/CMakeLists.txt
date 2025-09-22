set(LLVM_LINK_COMPONENTS
  Core
  Support
  Analysis
  IRReader)

add_llvm_tool(llvm-sim
  llvm-sim.cpp
)
