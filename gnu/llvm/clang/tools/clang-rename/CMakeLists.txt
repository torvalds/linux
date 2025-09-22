set(LLVM_LINK_COMPONENTS
  Option
  Support
  )

add_clang_tool(clang-rename
  ClangRename.cpp
  )

clang_target_link_libraries(clang-rename
  PRIVATE
  clangBasic
  clangFrontend
  clangRewrite
  clangSerialization
  clangTooling
  clangToolingCore
  clangToolingRefactoring
  )

install(FILES clang-rename.py
  DESTINATION "${CMAKE_INSTALL_DATADIR}/clang"
  COMPONENT clang-rename)
install(FILES clang-rename.el
  DESTINATION "${CMAKE_INSTALL_DATADIR}/clang"
  COMPONENT clang-rename)
