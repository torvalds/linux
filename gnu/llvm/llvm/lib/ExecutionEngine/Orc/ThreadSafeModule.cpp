//===-- ThreadSafeModule.cpp - Thread safe Module, Context, and Utilities
//h-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Transforms/Utils/Cloning.h"

namespace llvm {
namespace orc {

ThreadSafeModule cloneToNewContext(const ThreadSafeModule &TSM,
                                   GVPredicate ShouldCloneDef,
                                   GVModifier UpdateClonedDefSource) {
  assert(TSM && "Can not clone null module");

  if (!ShouldCloneDef)
    ShouldCloneDef = [](const GlobalValue &) { return true; };

  return TSM.withModuleDo([&](Module &M) {
    SmallVector<char, 1> ClonedModuleBuffer;

    {
      std::set<GlobalValue *> ClonedDefsInSrc;
      ValueToValueMapTy VMap;
      auto Tmp = CloneModule(M, VMap, [&](const GlobalValue *GV) {
        if (ShouldCloneDef(*GV)) {
          ClonedDefsInSrc.insert(const_cast<GlobalValue *>(GV));
          return true;
        }
        return false;
      });

      if (UpdateClonedDefSource)
        for (auto *GV : ClonedDefsInSrc)
          UpdateClonedDefSource(*GV);

      BitcodeWriter BCWriter(ClonedModuleBuffer);

      BCWriter.writeModule(*Tmp);
      BCWriter.writeSymtab();
      BCWriter.writeStrtab();
    }

    MemoryBufferRef ClonedModuleBufferRef(
        StringRef(ClonedModuleBuffer.data(), ClonedModuleBuffer.size()),
        "cloned module buffer");
    ThreadSafeContext NewTSCtx(std::make_unique<LLVMContext>());

    auto ClonedModule = cantFail(
        parseBitcodeFile(ClonedModuleBufferRef, *NewTSCtx.getContext()));
    ClonedModule->setModuleIdentifier(M.getName());
    return ThreadSafeModule(std::move(ClonedModule), std::move(NewTSCtx));
  });
}

} // end namespace orc
} // end namespace llvm
