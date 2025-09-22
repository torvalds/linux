set(LLVM_LINK_COMPONENTS
    Core
    Support
    )

add_llvm_library(LLVMDiff
  STATIC
  DiffConsumer.cpp
  DiffConsumer.h
  DifferenceEngine.cpp
  DifferenceEngine.h
  DiffLog.cpp
  DiffLog.h
  )
