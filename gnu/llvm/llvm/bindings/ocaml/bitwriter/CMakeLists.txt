add_ocaml_library(llvm_bitwriter
  OCAML    llvm_bitwriter
  OCAMLDEP llvm
  C        bitwriter_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/../llvm"
  PKG      unix
  LLVM     BitWriter)
