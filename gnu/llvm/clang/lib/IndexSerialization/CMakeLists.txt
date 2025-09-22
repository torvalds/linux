set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_library(clangIndexSerialization
  SerializablePathCollection.cpp

  LINK_LIBS
  clangBasic
  )
