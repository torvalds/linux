//===-- ClangDeclVendor.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGDECLVENDOR_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGDECLVENDOR_H

#include "lldb/Symbol/DeclVendor.h"

namespace clang {
class NamedDecl;
}

namespace lldb_private {

// A clang specialized extension to DeclVendor.
class ClangDeclVendor : public DeclVendor {
public:
  ClangDeclVendor(DeclVendorKind kind) : DeclVendor(kind) {}

  ~ClangDeclVendor() override = default;

  using DeclVendor::FindDecls;

  uint32_t FindDecls(ConstString name, bool append, uint32_t max_matches,
                     std::vector<clang::NamedDecl *> &decls);

  static bool classof(const DeclVendor *vendor) {
    return vendor->GetKind() >= eClangDeclVendor &&
           vendor->GetKind() < eLastClangDeclVendor;
  }

private:
  ClangDeclVendor(const ClangDeclVendor &) = delete;
  const ClangDeclVendor &operator=(const ClangDeclVendor &) = delete;
};
} // namespace lldb_private

#endif
