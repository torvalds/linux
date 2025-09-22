//===-- ClangDeclVendor.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ExpressionParser/Clang/ClangDeclVendor.h"
#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include "lldb/Utility/ConstString.h"

using namespace lldb_private;

uint32_t ClangDeclVendor::FindDecls(ConstString name, bool append,
                                    uint32_t max_matches,
                                    std::vector<clang::NamedDecl *> &decls) {
  if (!append)
    decls.clear();

  std::vector<CompilerDecl> compiler_decls;
  uint32_t ret = FindDecls(name, /*append*/ false, max_matches, compiler_decls);
  for (CompilerDecl compiler_decl : compiler_decls) {
    clang::Decl *d = ClangUtil::GetDecl(compiler_decl);
    clang::NamedDecl *nd = llvm::cast<clang::NamedDecl>(d);
    decls.push_back(nd);
  }
  return ret;
}
