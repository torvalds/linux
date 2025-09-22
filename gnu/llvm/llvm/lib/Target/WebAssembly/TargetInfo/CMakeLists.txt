include_directories( ${CMAKE_CURRENT_BINARY_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/.. )

add_llvm_component_library(LLVMWebAssemblyInfo
  WebAssemblyTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  WebAssembly
  )

add_dependencies(LLVMWebAssemblyInfo WebAssemblyCommonTableGen)
