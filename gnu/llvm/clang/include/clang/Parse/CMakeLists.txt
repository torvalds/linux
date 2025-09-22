clang_tablegen(AttrParserStringSwitches.inc -gen-clang-attr-parser-string-switches
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../../
  SOURCE ../Basic/Attr.td
  TARGET ClangAttrParserStringSwitches)

clang_tablegen(AttrSubMatchRulesParserStringSwitches.inc
  -gen-clang-attr-subject-match-rules-parser-string-switches
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../../
  SOURCE ../Basic/Attr.td
  TARGET ClangAttrSubMatchRulesParserStringSwitches)
