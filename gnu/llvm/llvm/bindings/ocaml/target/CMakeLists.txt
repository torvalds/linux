add_ocaml_library(llvm_target
  OCAML    llvm_target
  OCAMLDEP llvm
  C        target_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/ -I${CMAKE_CURRENT_SOURCE_DIR}/../llvm"
  LLVM     Target)
