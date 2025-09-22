# If we don't need RTTI or EH, there's no reason to export anything
# from the plugin.
if(NOT MSVC) # MSVC mangles symbols differently, and
    # PrintLLVMFunctionNames.export contains C++ symbols.
  if(NOT LLVM_REQUIRES_RTTI)
    if(NOT LLVM_REQUIRES_EH)
      set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/LLVMPrintFunctionNames.exports)
    endif()
  endif()
endif()

add_llvm_library(LLVMPrintFunctionNames MODULE LLVMPrintFunctionNames.cpp PLUGIN_TOOL clang)

if(WIN32 OR CYGWIN)
  set(LLVM_LINK_COMPONENTS
    Support
  )
  clang_target_link_libraries(LLVMPrintFunctionNames PRIVATE
    clangAST
    clangBasic
    clangFrontend
    )
endif()
