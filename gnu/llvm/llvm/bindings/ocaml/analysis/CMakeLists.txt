add_ocaml_library(llvm_analysis
  OCAML    llvm_analysis
  OCAMLDEP llvm
  C        analysis_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/../llvm"
  LLVM     Analysis)
