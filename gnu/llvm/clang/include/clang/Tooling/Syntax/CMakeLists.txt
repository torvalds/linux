clang_tablegen(Nodes.inc -gen-clang-syntax-node-list
  SOURCE Nodes.td
  TARGET ClangSyntaxNodeList)

clang_tablegen(NodeClasses.inc -gen-clang-syntax-node-classes
  SOURCE Nodes.td
  TARGET ClangSyntaxNodeClasses)
