set(LLVM_LINK_COMPONENTS support)

add_clang_library(clangFormat
  AffectedRangeManager.cpp
  BreakableToken.cpp
  ContinuationIndenter.cpp
  DefinitionBlockSeparator.cpp
  Format.cpp
  FormatToken.cpp
  FormatTokenLexer.cpp
  IntegerLiteralSeparatorFixer.cpp
  MacroCallReconstructor.cpp
  MacroExpander.cpp
  MatchFilePath.cpp
  NamespaceEndCommentsFixer.cpp
  ObjCPropertyAttributeOrderFixer.cpp
  QualifierAlignmentFixer.cpp
  SortJavaScriptImports.cpp
  TokenAnalyzer.cpp
  TokenAnnotator.cpp
  UnwrappedLineFormatter.cpp
  UnwrappedLineParser.cpp
  UsingDeclarationsSorter.cpp
  WhitespaceManager.cpp

  LINK_LIBS
  clangBasic
  clangLex
  clangToolingCore
  clangToolingInclusions
  )

file(GLOB_RECURSE files
  *.cpp
  *.h
  ../../include/clang/Format/*.h
  ../../tools/clang-format/*.cpp
  ../../unittests/Format/*.cpp
  ../../unittests/Format/*.h
  )

set(check_format_depends)
set(i 0)
foreach (file IN LISTS files)
  add_custom_command(OUTPUT clang-format-check-format${i}
    COMMAND clang-format ${file} | diff -u ${file} -
    VERBATIM
    COMMENT "Checking format of ${file}..."
  )
  list(APPEND check_format_depends "clang-format-check-format${i}")

  math(EXPR i ${i}+1)
endforeach ()

add_custom_target(clang-format-check-format DEPENDS ${check_format_depends})
