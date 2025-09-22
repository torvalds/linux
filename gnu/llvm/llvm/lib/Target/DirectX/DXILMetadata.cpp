//===- DXILMetadata.cpp - DXIL Metadata helper objects --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains helper objects for working with DXIL metadata.
///
//===----------------------------------------------------------------------===//

#include "DXILMetadata.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;
using namespace llvm::dxil;

ValidatorVersionMD::ValidatorVersionMD(Module &M)
    : Entry(M.getOrInsertNamedMetadata("dx.valver")) {}

void ValidatorVersionMD::update(VersionTuple ValidatorVer) {
  auto &Ctx = Entry->getParent()->getContext();
  IRBuilder<> B(Ctx);
  Metadata *MDVals[2];
  MDVals[0] = ConstantAsMetadata::get(B.getInt32(ValidatorVer.getMajor()));
  MDVals[1] =
      ConstantAsMetadata::get(B.getInt32(ValidatorVer.getMinor().value_or(0)));

  if (isEmpty())
    Entry->addOperand(MDNode::get(Ctx, MDVals));
  else
    Entry->setOperand(0, MDNode::get(Ctx, MDVals));
}

bool ValidatorVersionMD::isEmpty() { return Entry->getNumOperands() == 0; }

VersionTuple ValidatorVersionMD::getAsVersionTuple() {
  if (isEmpty())
    return VersionTuple(1, 0);
  auto *ValVerMD = cast<MDNode>(Entry->getOperand(0));
  auto *MajorMD = mdconst::extract<ConstantInt>(ValVerMD->getOperand(0));
  auto *MinorMD = mdconst::extract<ConstantInt>(ValVerMD->getOperand(1));
  return VersionTuple(MajorMD->getZExtValue(), MinorMD->getZExtValue());
}

static StringRef getShortShaderStage(Triple::EnvironmentType Env) {
  switch (Env) {
  case Triple::Pixel:
    return "ps";
  case Triple::Vertex:
    return "vs";
  case Triple::Geometry:
    return "gs";
  case Triple::Hull:
    return "hs";
  case Triple::Domain:
    return "ds";
  case Triple::Compute:
    return "cs";
  case Triple::Library:
    return "lib";
  case Triple::Mesh:
    return "ms";
  case Triple::Amplification:
    return "as";
  default:
    break;
  }
  llvm_unreachable("Unsupported environment for DXIL generation.");
  return "";
}

void dxil::createShaderModelMD(Module &M) {
  NamedMDNode *Entry = M.getOrInsertNamedMetadata("dx.shaderModel");
  Triple TT(M.getTargetTriple());
  VersionTuple Ver = TT.getOSVersion();
  LLVMContext &Ctx = M.getContext();
  IRBuilder<> B(Ctx);

  Metadata *Vals[3];
  Vals[0] = MDString::get(Ctx, getShortShaderStage(TT.getEnvironment()));
  Vals[1] = ConstantAsMetadata::get(B.getInt32(Ver.getMajor()));
  Vals[2] = ConstantAsMetadata::get(B.getInt32(Ver.getMinor().value_or(0)));
  Entry->addOperand(MDNode::get(Ctx, Vals));
}

void dxil::createDXILVersionMD(Module &M) {
  Triple TT(Triple::normalize(M.getTargetTriple()));
  VersionTuple Ver = TT.getDXILVersion();
  LLVMContext &Ctx = M.getContext();
  IRBuilder<> B(Ctx);
  NamedMDNode *Entry = M.getOrInsertNamedMetadata("dx.version");
  Metadata *Vals[2];
  Vals[0] = ConstantAsMetadata::get(B.getInt32(Ver.getMajor()));
  Vals[1] = ConstantAsMetadata::get(B.getInt32(Ver.getMinor().value_or(0)));
  Entry->addOperand(MDNode::get(Ctx, Vals));
}

static uint32_t getShaderStage(Triple::EnvironmentType Env) {
  return (uint32_t)Env - (uint32_t)llvm::Triple::Pixel;
}

namespace {

struct EntryProps {
  Triple::EnvironmentType ShaderKind;
  // FIXME: support more shader profiles.
  // See https://github.com/llvm/llvm-project/issues/57927.
  struct {
    unsigned NumThreads[3];
  } CS;

  EntryProps(Function &F, Triple::EnvironmentType ModuleShaderKind)
      : ShaderKind(ModuleShaderKind) {

    if (ShaderKind == Triple::EnvironmentType::Library) {
      Attribute EntryAttr = F.getFnAttribute("hlsl.shader");
      StringRef EntryProfile = EntryAttr.getValueAsString();
      Triple T("", "", "", EntryProfile);
      ShaderKind = T.getEnvironment();
    }

    if (ShaderKind == Triple::EnvironmentType::Compute) {
      auto NumThreadsStr =
          F.getFnAttribute("hlsl.numthreads").getValueAsString();
      SmallVector<StringRef> NumThreads;
      NumThreadsStr.split(NumThreads, ',');
      assert(NumThreads.size() == 3 && "invalid numthreads");
      auto Zip =
          llvm::zip(NumThreads, MutableArrayRef<unsigned>(CS.NumThreads));
      for (auto It : Zip) {
        StringRef Str = std::get<0>(It);
        APInt V;
        [[maybe_unused]] bool Result = Str.getAsInteger(10, V);
        assert(!Result && "Failed to parse numthreads");

        unsigned &Num = std::get<1>(It);
        Num = V.getLimitedValue();
      }
    }
  }

  MDTuple *emitDXILEntryProps(uint64_t RawShaderFlag, LLVMContext &Ctx,
                              bool IsLib) {
    std::vector<Metadata *> MDVals;

    if (RawShaderFlag != 0)
      appendShaderFlags(MDVals, RawShaderFlag, Ctx);

    // Add shader kind for lib entrys.
    if (IsLib && ShaderKind != Triple::EnvironmentType::Library)
      appendShaderKind(MDVals, Ctx);

    if (ShaderKind == Triple::EnvironmentType::Compute)
      appendNumThreads(MDVals, Ctx);
    // FIXME: support more props.
    // See https://github.com/llvm/llvm-project/issues/57948.
    return MDNode::get(Ctx, MDVals);
  }

  static MDTuple *emitEntryPropsForEmptyEntry(uint64_t RawShaderFlag,
                                              LLVMContext &Ctx) {
    if (RawShaderFlag == 0)
      return nullptr;

    std::vector<Metadata *> MDVals;

    appendShaderFlags(MDVals, RawShaderFlag, Ctx);
    // FIXME: support more props.
    // See https://github.com/llvm/llvm-project/issues/57948.
    return MDNode::get(Ctx, MDVals);
  }

private:
  enum EntryPropsTag {
    ShaderFlagsTag = 0,
    GSStateTag,
    DSStateTag,
    HSStateTag,
    NumThreadsTag,
    AutoBindingSpaceTag,
    RayPayloadSizeTag,
    RayAttribSizeTag,
    ShaderKindTag,
    MSStateTag,
    ASStateTag,
    WaveSizeTag,
    EntryRootSigTag,
  };

  void appendNumThreads(std::vector<Metadata *> &MDVals, LLVMContext &Ctx) {
    MDVals.emplace_back(ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt32Ty(Ctx), NumThreadsTag)));

    std::vector<Metadata *> NumThreadVals;
    for (auto Num : ArrayRef<unsigned>(CS.NumThreads))
      NumThreadVals.emplace_back(ConstantAsMetadata::get(
          ConstantInt::get(Type::getInt32Ty(Ctx), Num)));
    MDVals.emplace_back(MDNode::get(Ctx, NumThreadVals));
  }

  static void appendShaderFlags(std::vector<Metadata *> &MDVals,
                                uint64_t RawShaderFlag, LLVMContext &Ctx) {
    MDVals.emplace_back(ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt32Ty(Ctx), ShaderFlagsTag)));
    MDVals.emplace_back(ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt64Ty(Ctx), RawShaderFlag)));
  }

  void appendShaderKind(std::vector<Metadata *> &MDVals, LLVMContext &Ctx) {
    MDVals.emplace_back(ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt32Ty(Ctx), ShaderKindTag)));
    MDVals.emplace_back(ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt32Ty(Ctx), getShaderStage(ShaderKind))));
  }
};

class EntryMD {
  Function &F;
  LLVMContext &Ctx;
  EntryProps Props;

public:
  EntryMD(Function &F, Triple::EnvironmentType ModuleShaderKind)
      : F(F), Ctx(F.getContext()), Props(F, ModuleShaderKind) {}

  MDTuple *emitEntryTuple(MDTuple *Resources, uint64_t RawShaderFlag) {
    // FIXME: add signature for profile other than CS.
    // See https://github.com/llvm/llvm-project/issues/57928.
    MDTuple *Signatures = nullptr;
    return emitDXILEntryPointTuple(
        &F, F.getName().str(), Signatures, Resources,
        Props.emitDXILEntryProps(RawShaderFlag, Ctx, /*IsLib*/ false), Ctx);
  }

  MDTuple *emitEntryTupleForLib(uint64_t RawShaderFlag) {
    // FIXME: add signature for profile other than CS.
    // See https://github.com/llvm/llvm-project/issues/57928.
    MDTuple *Signatures = nullptr;
    return emitDXILEntryPointTuple(
        &F, F.getName().str(), Signatures,
        /*entry in lib doesn't need resources metadata*/ nullptr,
        Props.emitDXILEntryProps(RawShaderFlag, Ctx, /*IsLib*/ true), Ctx);
  }

  // Library will have empty entry metadata which only store the resource table
  // metadata.
  static MDTuple *emitEmptyEntryForLib(MDTuple *Resources,
                                       uint64_t RawShaderFlag,
                                       LLVMContext &Ctx) {
    return emitDXILEntryPointTuple(
        nullptr, "", nullptr, Resources,
        EntryProps::emitEntryPropsForEmptyEntry(RawShaderFlag, Ctx), Ctx);
  }

private:
  static MDTuple *emitDXILEntryPointTuple(Function *Fn, const std::string &Name,
                                          MDTuple *Signatures,
                                          MDTuple *Resources,
                                          MDTuple *Properties,
                                          LLVMContext &Ctx) {
    Metadata *MDVals[5];
    MDVals[0] = Fn ? ValueAsMetadata::get(Fn) : nullptr;
    MDVals[1] = MDString::get(Ctx, Name.c_str());
    MDVals[2] = Signatures;
    MDVals[3] = Resources;
    MDVals[4] = Properties;
    return MDNode::get(Ctx, MDVals);
  }
};
} // namespace

void dxil::createEntryMD(Module &M, const uint64_t ShaderFlags) {
  SmallVector<Function *> EntryList;
  for (auto &F : M.functions()) {
    if (!F.hasFnAttribute("hlsl.shader"))
      continue;
    EntryList.emplace_back(&F);
  }

  auto &Ctx = M.getContext();
  // FIXME: generate metadata for resource.
  // See https://github.com/llvm/llvm-project/issues/57926.
  MDTuple *MDResources = nullptr;
  if (auto *NamedResources = M.getNamedMetadata("dx.resources"))
    MDResources = dyn_cast<MDTuple>(NamedResources->getOperand(0));

  std::vector<MDNode *> Entries;
  Triple T = Triple(M.getTargetTriple());
  switch (T.getEnvironment()) {
  case Triple::EnvironmentType::Library: {
    // Add empty entry to put resource metadata.
    MDTuple *EmptyEntry =
        EntryMD::emitEmptyEntryForLib(MDResources, ShaderFlags, Ctx);
    Entries.emplace_back(EmptyEntry);

    for (Function *Entry : EntryList) {
      EntryMD MD(*Entry, T.getEnvironment());
      Entries.emplace_back(MD.emitEntryTupleForLib(0));
    }
  } break;
  case Triple::EnvironmentType::Compute:
  case Triple::EnvironmentType::Amplification:
  case Triple::EnvironmentType::Mesh:
  case Triple::EnvironmentType::Vertex:
  case Triple::EnvironmentType::Hull:
  case Triple::EnvironmentType::Domain:
  case Triple::EnvironmentType::Geometry:
  case Triple::EnvironmentType::Pixel: {
    assert(EntryList.size() == 1 &&
           "non-lib profiles should only have one entry");
    EntryMD MD(*EntryList.front(), T.getEnvironment());
    Entries.emplace_back(MD.emitEntryTuple(MDResources, ShaderFlags));
  } break;
  default:
    assert(0 && "invalid profile");
    break;
  }

  NamedMDNode *EntryPointsNamedMD =
      M.getOrInsertNamedMetadata("dx.entryPoints");
  for (auto *Entry : Entries)
    EntryPointsNamedMD->addOperand(Entry);
}
