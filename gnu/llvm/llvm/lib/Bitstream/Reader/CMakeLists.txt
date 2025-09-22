add_llvm_component_library(LLVMBitstreamReader
  BitstreamReader.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Bitcode
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Bitstream

  LINK_COMPONENTS
  Support
  )
