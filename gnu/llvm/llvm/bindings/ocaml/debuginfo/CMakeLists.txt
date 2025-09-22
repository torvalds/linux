add_ocaml_library(llvm_debuginfo
  OCAML    llvm_debuginfo
  OCAMLDEP llvm
  C        debuginfo_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/../llvm"
  LLVM     Core)
