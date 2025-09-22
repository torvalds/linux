//===- NameAnonGlobals.cpp - ThinLTO Support: Name Unnamed Globals --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements naming anonymous globals to make sure they can be
// referred to by ThinLTO.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/NameAnonGlobals.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MD5.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

namespace {
// Compute a "unique" hash for the module based on the name of the public
// globals.
class ModuleHasher {
  Module &TheModule;
  std::string TheHash;

public:
  ModuleHasher(Module &M) : TheModule(M) {}

  /// Return the lazily computed hash.
  std::string &get() {
    if (!TheHash.empty())
      // Cache hit :)
      return TheHash;

    MD5 Hasher;
    for (auto &F : TheModule) {
      if (F.isDeclaration() || F.hasLocalLinkage() || !F.hasName())
        continue;
      auto Name = F.getName();
      Hasher.update(Name);
    }
    for (auto &GV : TheModule.globals()) {
      if (GV.isDeclaration() || GV.hasLocalLinkage() || !GV.hasName())
        continue;
      auto Name = GV.getName();
      Hasher.update(Name);
    }

    // Now return the result.
    MD5::MD5Result Hash;
    Hasher.final(Hash);
    SmallString<32> Result;
    MD5::stringifyResult(Hash, Result);
    TheHash = std::string(Result);
    return TheHash;
  }
};
} // end anonymous namespace

// Rename all the anon globals in the module
bool llvm::nameUnamedGlobals(Module &M) {
  bool Changed = false;
  ModuleHasher ModuleHash(M);
  int count = 0;
  auto RenameIfNeed = [&](GlobalValue &GV) {
    if (GV.hasName())
      return;
    GV.setName(Twine("anon.") + ModuleHash.get() + "." + Twine(count++));
    Changed = true;
  };
  for (auto &GO : M.global_objects())
    RenameIfNeed(GO);
  for (auto &GA : M.aliases())
    RenameIfNeed(GA);

  return Changed;
}

PreservedAnalyses NameAnonGlobalPass::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  if (!nameUnamedGlobals(M))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}
