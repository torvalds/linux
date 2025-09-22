foreach(TARGET ${LLVM_TARGETS_TO_BUILD})
  set(OCAML_LLVM_TARGET ${TARGET})

  foreach( ext ml mli )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/llvm_backend.${ext}.in"
        "${CMAKE_CURRENT_BINARY_DIR}/llvm_${TARGET}.${ext}")
  endforeach()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/backend_ocaml.c"
    "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_ocaml.c")

  add_ocaml_library(llvm_${TARGET}
    OCAML    llvm_${TARGET}
    C        ${TARGET}_ocaml
    CFLAGS   -DTARGET=${TARGET}
    LLVM     ${TARGET}
    NOCOPY)

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/META.llvm_backend.in"
    "${LLVM_LIBRARY_DIR}/ocaml/META.llvm_${TARGET}")

  install(FILES "${LLVM_LIBRARY_DIR}/ocaml/META.llvm_${TARGET}"
          DESTINATION "${LLVM_OCAML_INSTALL_PATH}")
endforeach()
