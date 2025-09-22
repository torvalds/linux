add_llvm_library(Attribute MODULE Attribute.cpp PLUGIN_TOOL clang)

if(WIN32 OR CYGWIN)
  target_link_libraries(Attribute PRIVATE
    clangAST
    clangBasic
    clangFrontend
    clangLex
    LLVMSupport
    )
endif()
