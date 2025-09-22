//===- DXContainerGlobals.cpp - DXContainer global generator pass ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DXContainerGlobalsPass implementation.
//
//===----------------------------------------------------------------------===//

#include "DXILShaderFlags.h"
#include "DirectX.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/DXContainerPSVInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/MD5.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace llvm::dxil;
using namespace llvm::mcdxbc;

namespace {
class DXContainerGlobals : public llvm::ModulePass {

  GlobalVariable *buildContainerGlobal(Module &M, Constant *Content,
                                       StringRef Name, StringRef SectionName);
  GlobalVariable *getFeatureFlags(Module &M);
  GlobalVariable *computeShaderHash(Module &M);
  GlobalVariable *buildSignature(Module &M, Signature &Sig, StringRef Name,
                                 StringRef SectionName);
  void addSignature(Module &M, SmallVector<GlobalValue *> &Globals);
  void addPipelineStateValidationInfo(Module &M,
                                      SmallVector<GlobalValue *> &Globals);

public:
  static char ID; // Pass identification, replacement for typeid
  DXContainerGlobals() : ModulePass(ID) {
    initializeDXContainerGlobalsPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "DXContainer Global Emitter";
  }

  bool runOnModule(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<ShaderFlagsAnalysisWrapper>();
  }
};

} // namespace

bool DXContainerGlobals::runOnModule(Module &M) {
  llvm::SmallVector<GlobalValue *> Globals;
  Globals.push_back(getFeatureFlags(M));
  Globals.push_back(computeShaderHash(M));
  addSignature(M, Globals);
  addPipelineStateValidationInfo(M, Globals);
  appendToCompilerUsed(M, Globals);
  return true;
}

GlobalVariable *DXContainerGlobals::getFeatureFlags(Module &M) {
  const uint64_t FeatureFlags =
      static_cast<uint64_t>(getAnalysis<ShaderFlagsAnalysisWrapper>()
                                .getShaderFlags()
                                .getFeatureFlags());

  Constant *FeatureFlagsConstant =
      ConstantInt::get(M.getContext(), APInt(64, FeatureFlags));
  return buildContainerGlobal(M, FeatureFlagsConstant, "dx.sfi0", "SFI0");
}

GlobalVariable *DXContainerGlobals::computeShaderHash(Module &M) {
  auto *DXILConstant =
      cast<ConstantDataArray>(M.getNamedGlobal("dx.dxil")->getInitializer());
  MD5 Digest;
  Digest.update(DXILConstant->getRawDataValues());
  MD5::MD5Result Result = Digest.final();

  dxbc::ShaderHash HashData = {0, {0}};
  // The Hash's IncludesSource flag gets set whenever the hashed shader includes
  // debug information.
  if (M.debug_compile_units_begin() != M.debug_compile_units_end())
    HashData.Flags = static_cast<uint32_t>(dxbc::HashFlags::IncludesSource);

  memcpy(reinterpret_cast<void *>(&HashData.Digest), Result.data(), 16);
  if (sys::IsBigEndianHost)
    HashData.swapBytes();
  StringRef Data(reinterpret_cast<char *>(&HashData), sizeof(dxbc::ShaderHash));

  Constant *ModuleConstant =
      ConstantDataArray::get(M.getContext(), arrayRefFromStringRef(Data));
  return buildContainerGlobal(M, ModuleConstant, "dx.hash", "HASH");
}

GlobalVariable *DXContainerGlobals::buildContainerGlobal(
    Module &M, Constant *Content, StringRef Name, StringRef SectionName) {
  auto *GV = new llvm::GlobalVariable(
      M, Content->getType(), true, GlobalValue::PrivateLinkage, Content, Name);
  GV->setSection(SectionName);
  GV->setAlignment(Align(4));
  return GV;
}

GlobalVariable *DXContainerGlobals::buildSignature(Module &M, Signature &Sig,
                                                   StringRef Name,
                                                   StringRef SectionName) {
  SmallString<256> Data;
  raw_svector_ostream OS(Data);
  Sig.write(OS);
  Constant *Constant =
      ConstantDataArray::getString(M.getContext(), Data, /*AddNull*/ false);
  return buildContainerGlobal(M, Constant, Name, SectionName);
}

void DXContainerGlobals::addSignature(Module &M,
                                      SmallVector<GlobalValue *> &Globals) {
  // FIXME: support graphics shader.
  //  see issue https://github.com/llvm/llvm-project/issues/90504.

  Signature InputSig;
  Globals.emplace_back(buildSignature(M, InputSig, "dx.isg1", "ISG1"));

  Signature OutputSig;
  Globals.emplace_back(buildSignature(M, OutputSig, "dx.osg1", "OSG1"));
}

void DXContainerGlobals::addPipelineStateValidationInfo(
    Module &M, SmallVector<GlobalValue *> &Globals) {
  SmallString<256> Data;
  raw_svector_ostream OS(Data);
  PSVRuntimeInfo PSV;
  Triple TT(M.getTargetTriple());
  PSV.BaseData.MinimumWaveLaneCount = 0;
  PSV.BaseData.MaximumWaveLaneCount = std::numeric_limits<uint32_t>::max();
  PSV.BaseData.ShaderStage =
      static_cast<uint8_t>(TT.getEnvironment() - Triple::Pixel);

  // Hardcoded values here to unblock loading the shader into D3D.
  //
  // TODO: Lots more stuff to do here!
  //
  // See issue https://github.com/llvm/llvm-project/issues/96674.
  PSV.BaseData.NumThreadsX = 1;
  PSV.BaseData.NumThreadsY = 1;
  PSV.BaseData.NumThreadsZ = 1;
  PSV.EntryName = "main";

  PSV.finalize(TT.getEnvironment());
  PSV.write(OS);
  Constant *Constant =
      ConstantDataArray::getString(M.getContext(), Data, /*AddNull*/ false);
  Globals.emplace_back(buildContainerGlobal(M, Constant, "dx.psv0", "PSV0"));
}

char DXContainerGlobals::ID = 0;
INITIALIZE_PASS_BEGIN(DXContainerGlobals, "dxil-globals",
                      "DXContainer Global Emitter", false, true)
INITIALIZE_PASS_DEPENDENCY(ShaderFlagsAnalysisWrapper)
INITIALIZE_PASS_END(DXContainerGlobals, "dxil-globals",
                    "DXContainer Global Emitter", false, true)

ModulePass *llvm::createDXContainerGlobalsPass() {
  return new DXContainerGlobals();
}
