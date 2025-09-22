add_ocaml_library(llvm_bitreader
  OCAML    llvm_bitreader
  OCAMLDEP llvm
  C        bitreader_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/../llvm"
  LLVM     BitReader)
