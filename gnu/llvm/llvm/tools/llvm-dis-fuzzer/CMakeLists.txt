set(LLVM_LINK_COMPONENTS
  BitReader
  Core
  Support
)
add_llvm_fuzzer(llvm-dis-fuzzer
  llvm-dis-fuzzer.cpp
  )
