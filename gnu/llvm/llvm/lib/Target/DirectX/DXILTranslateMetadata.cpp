//===- DXILTranslateMetadata.cpp - Pass to emit DXIL metadata ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
//===----------------------------------------------------------------------===//

#include "DXILMetadata.h"
#include "DXILResource.h"
#include "DXILResourceAnalysis.h"
#include "DXILShaderFlags.h"
#include "DirectX.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;
using namespace llvm::dxil;

namespace {
class DXILTranslateMetadata : public ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  explicit DXILTranslateMetadata() : ModulePass(ID) {}

  StringRef getPassName() const override { return "DXIL Metadata Emit"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<DXILResourceWrapper>();
    AU.addRequired<ShaderFlagsAnalysisWrapper>();
  }

  bool runOnModule(Module &M) override;
};

} // namespace

bool DXILTranslateMetadata::runOnModule(Module &M) {

  dxil::ValidatorVersionMD ValVerMD(M);
  if (ValVerMD.isEmpty())
    ValVerMD.update(VersionTuple(1, 0));
  dxil::createShaderModelMD(M);
  dxil::createDXILVersionMD(M);

  const dxil::Resources &Res =
      getAnalysis<DXILResourceWrapper>().getDXILResource();
  Res.write(M);

  const uint64_t Flags = static_cast<uint64_t>(
      getAnalysis<ShaderFlagsAnalysisWrapper>().getShaderFlags());
  dxil::createEntryMD(M, Flags);

  return false;
}

char DXILTranslateMetadata::ID = 0;

ModulePass *llvm::createDXILTranslateMetadataPass() {
  return new DXILTranslateMetadata();
}

INITIALIZE_PASS_BEGIN(DXILTranslateMetadata, "dxil-metadata-emit",
                      "DXIL Metadata Emit", false, false)
INITIALIZE_PASS_DEPENDENCY(DXILResourceWrapper)
INITIALIZE_PASS_DEPENDENCY(ShaderFlagsAnalysisWrapper)
INITIALIZE_PASS_END(DXILTranslateMetadata, "dxil-metadata-emit",
                    "DXIL Metadata Emit", false, false)
