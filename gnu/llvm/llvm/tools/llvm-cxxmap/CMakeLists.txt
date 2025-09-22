set(LLVM_LINK_COMPONENTS
  Core
  Support
  ProfileData
  )

add_llvm_tool(llvm-cxxmap
  llvm-cxxmap.cpp
  )
