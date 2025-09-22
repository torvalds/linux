add_ocaml_library(llvm_linker
  OCAML    llvm_linker
  OCAMLDEP llvm
  C        linker_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/../llvm"
  LLVM     Linker)
