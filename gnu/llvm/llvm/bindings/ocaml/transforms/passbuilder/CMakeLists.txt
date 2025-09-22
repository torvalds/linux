add_ocaml_library(llvm_passbuilder
  OCAML    llvm_passbuilder
  OCAMLDEP llvm llvm_target
  C        passbuilder_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/../../llvm -I${CMAKE_CURRENT_SOURCE_DIR}/../../target"
  LLVM     Passes)
