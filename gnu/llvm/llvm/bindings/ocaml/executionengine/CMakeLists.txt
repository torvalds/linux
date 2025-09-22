add_ocaml_library(llvm_executionengine
  OCAML    llvm_executionengine
  OCAMLDEP llvm llvm_target
  C        executionengine_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/../llvm"
  LLVM     ExecutionEngine MCJIT native
  PKG      ctypes)
