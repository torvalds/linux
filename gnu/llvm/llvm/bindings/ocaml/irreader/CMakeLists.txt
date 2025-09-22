add_ocaml_library(llvm_irreader
  OCAML    llvm_irreader
  OCAMLDEP llvm
  C        irreader_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/../llvm"
  LLVM     IRReader)
