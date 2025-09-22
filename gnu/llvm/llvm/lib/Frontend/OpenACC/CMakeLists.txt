add_llvm_component_library(LLVMFrontendOpenACC
  ACC.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Frontend
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Frontend/OpenACC

  DEPENDS
  acc_gen
)

target_link_libraries(LLVMFrontendOpenACC LLVMSupport)

