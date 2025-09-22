//===-- DeclVendor.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/DeclVendor.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/TypeSystem.h"

#include <vector>

using namespace lldb;
using namespace lldb_private;

std::vector<CompilerType> DeclVendor::FindTypes(ConstString name,
                                                uint32_t max_matches) {
  std::vector<CompilerType> ret;
  std::vector<CompilerDecl> decls;
  if (FindDecls(name, /*append*/ true, max_matches, decls))
    for (auto decl : decls)
      if (auto type =
              decl.GetTypeSystem()->GetTypeForDecl(decl.GetOpaqueDecl()))
        ret.push_back(type);
  return ret;
}
