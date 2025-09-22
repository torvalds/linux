add_llvm_utility(FileCheck
  FileCheck.cpp
  )

target_link_libraries(FileCheck PRIVATE LLVMFileCheck LLVMSupport)
