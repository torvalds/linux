clang_tablegen(AttrPCHRead.inc -gen-clang-attr-pch-read
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../../
  SOURCE ../Basic/Attr.td
  TARGET ClangAttrPCHRead)

clang_tablegen(AttrPCHWrite.inc -gen-clang-attr-pch-write
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../../
  SOURCE ../Basic/Attr.td
  TARGET ClangAttrPCHWrite)
