set(LLVM_LINK_COMPONENTS
  ObjectYAML
  Support
  )

add_llvm_utility(yaml2obj
  yaml2obj.cpp
  )
