add_ocaml_library(llvm_all_backends
  OCAML    llvm_all_backends
  OCAMLDEP llvm
  C        all_backends_ocaml
  LLVM     ${LLVM_TARGETS_TO_BUILD})
