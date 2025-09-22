set(LLVM_LINK_COMPONENTS
  DebugInfoDWARF
  Object
  Support
  )

add_llvm_fuzzer(llvm-dwarfdump-fuzzer
  EXCLUDE_FROM_ALL
  llvm-dwarfdump-fuzzer.cpp
  )
