add_ocaml_library(llvm
  OCAML llvm
  C     llvm_ocaml
  CFLAGS   "-I${CMAKE_CURRENT_SOURCE_DIR}/"
  LLVM  Core Support)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/META.llvm.in"
  "${LLVM_LIBRARY_DIR}/ocaml/META.llvm")

install(FILES "${LLVM_LIBRARY_DIR}/ocaml/META.llvm"
        DESTINATION "${LLVM_OCAML_INSTALL_PATH}")
