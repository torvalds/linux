include_directories(
  ${LLVM_MAIN_SRC_DIR}/lib/Target/AArch64
  ${LLVM_BINARY_DIR}/lib/Target/AArch64
  )

set(LLVM_LINK_COMPONENTS
  AArch64
  CodeGenTypes
  Core
  Exegesis
  MC
  Support
  )

add_llvm_library(LLVMExegesisAArch64
  DISABLE_LLVM_LINK_LLVM_DYLIB
  STATIC
  Target.cpp

  DEPENDS
  intrinsics_gen
  AArch64CommonTableGen
  )
