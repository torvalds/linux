if(HAVE_LIBEDIT)
  set(link_libs LibEdit::LibEdit)
endif()

add_llvm_component_library(LLVMLineEditor
  LineEditor.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/LineEditor

  LINK_LIBS
  ${link_libs}

  LINK_COMPONENTS
  Support
  )
