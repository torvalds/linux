set(LLVM_LINK_COMPONENTS
    AllTargetsAsmParsers
    AllTargetsDescs
    AllTargetsInfos
    MC
    MCParser
    Support
    TargetParser
)

add_llvm_fuzzer(llvm-mc-assemble-fuzzer
  llvm-mc-assemble-fuzzer.cpp
  )
