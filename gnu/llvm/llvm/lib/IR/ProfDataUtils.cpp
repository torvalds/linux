//===- ProfDataUtils.cpp - Utility functions for MD_prof Metadata ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements utilities for working with Profiling Metadata.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ProfDataUtils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

namespace {

// MD_prof nodes have the following layout
//
// In general:
// { String name,         Array of i32   }
//
// In terms of Types:
// { MDString,            [i32, i32, ...]}
//
// Concretely for Branch Weights
// { "branch_weights",    [i32 1, i32 10000]}
//
// We maintain some constants here to ensure that we access the branch weights
// correctly, and can change the behavior in the future if the layout changes

// the minimum number of operands for MD_prof nodes with branch weights
constexpr unsigned MinBWOps = 3;

// the minimum number of operands for MD_prof nodes with value profiles
constexpr unsigned MinVPOps = 5;

// We may want to add support for other MD_prof types, so provide an abstraction
// for checking the metadata type.
bool isTargetMD(const MDNode *ProfData, const char *Name, unsigned MinOps) {
  // TODO: This routine may be simplified if MD_prof used an enum instead of a
  // string to differentiate the types of MD_prof nodes.
  if (!ProfData || !Name || MinOps < 2)
    return false;

  unsigned NOps = ProfData->getNumOperands();
  if (NOps < MinOps)
    return false;

  auto *ProfDataName = dyn_cast<MDString>(ProfData->getOperand(0));
  if (!ProfDataName)
    return false;

  return ProfDataName->getString() == Name;
}

template <typename T,
          typename = typename std::enable_if<std::is_arithmetic_v<T>>>
static void extractFromBranchWeightMD(const MDNode *ProfileData,
                                      SmallVectorImpl<T> &Weights) {
  assert(isBranchWeightMD(ProfileData) && "wrong metadata");

  unsigned NOps = ProfileData->getNumOperands();
  unsigned WeightsIdx = getBranchWeightOffset(ProfileData);
  assert(WeightsIdx < NOps && "Weights Index must be less than NOps.");
  Weights.resize(NOps - WeightsIdx);

  for (unsigned Idx = WeightsIdx, E = NOps; Idx != E; ++Idx) {
    ConstantInt *Weight =
        mdconst::dyn_extract<ConstantInt>(ProfileData->getOperand(Idx));
    assert(Weight && "Malformed branch_weight in MD_prof node");
    assert(Weight->getValue().getActiveBits() <= (sizeof(T) * 8) &&
           "Too many bits for MD_prof branch_weight");
    Weights[Idx - WeightsIdx] = Weight->getZExtValue();
  }
}

} // namespace

namespace llvm {

bool hasProfMD(const Instruction &I) {
  return I.hasMetadata(LLVMContext::MD_prof);
}

bool isBranchWeightMD(const MDNode *ProfileData) {
  return isTargetMD(ProfileData, "branch_weights", MinBWOps);
}

bool isValueProfileMD(const MDNode *ProfileData) {
  return isTargetMD(ProfileData, "VP", MinVPOps);
}

bool hasBranchWeightMD(const Instruction &I) {
  auto *ProfileData = I.getMetadata(LLVMContext::MD_prof);
  return isBranchWeightMD(ProfileData);
}

bool hasCountTypeMD(const Instruction &I) {
  auto *ProfileData = I.getMetadata(LLVMContext::MD_prof);
  // Value profiles record count-type information.
  if (isValueProfileMD(ProfileData))
    return true;
  // Conservatively assume non CallBase instruction only get taken/not-taken
  // branch probability, so not interpret them as count.
  return isa<CallBase>(I) && !isBranchWeightMD(ProfileData);
}

bool hasValidBranchWeightMD(const Instruction &I) {
  return getValidBranchWeightMDNode(I);
}

bool hasBranchWeightOrigin(const Instruction &I) {
  auto *ProfileData = I.getMetadata(LLVMContext::MD_prof);
  return hasBranchWeightOrigin(ProfileData);
}

bool hasBranchWeightOrigin(const MDNode *ProfileData) {
  if (!isBranchWeightMD(ProfileData))
    return false;
  auto *ProfDataName = dyn_cast<MDString>(ProfileData->getOperand(1));
  // NOTE: if we ever have more types of branch weight provenance,
  // we need to check the string value is "expected". For now, we
  // supply a more generic API, and avoid the spurious comparisons.
  assert(ProfDataName == nullptr || ProfDataName->getString() == "expected");
  return ProfDataName != nullptr;
}

unsigned getBranchWeightOffset(const MDNode *ProfileData) {
  return hasBranchWeightOrigin(ProfileData) ? 2 : 1;
}

unsigned getNumBranchWeights(const MDNode &ProfileData) {
  return ProfileData.getNumOperands() - getBranchWeightOffset(&ProfileData);
}

MDNode *getBranchWeightMDNode(const Instruction &I) {
  auto *ProfileData = I.getMetadata(LLVMContext::MD_prof);
  if (!isBranchWeightMD(ProfileData))
    return nullptr;
  return ProfileData;
}

MDNode *getValidBranchWeightMDNode(const Instruction &I) {
  auto *ProfileData = getBranchWeightMDNode(I);
  if (ProfileData && getNumBranchWeights(*ProfileData) == I.getNumSuccessors())
    return ProfileData;
  return nullptr;
}

void extractFromBranchWeightMD32(const MDNode *ProfileData,
                                 SmallVectorImpl<uint32_t> &Weights) {
  extractFromBranchWeightMD(ProfileData, Weights);
}

void extractFromBranchWeightMD64(const MDNode *ProfileData,
                                 SmallVectorImpl<uint64_t> &Weights) {
  extractFromBranchWeightMD(ProfileData, Weights);
}

bool extractBranchWeights(const MDNode *ProfileData,
                          SmallVectorImpl<uint32_t> &Weights) {
  if (!isBranchWeightMD(ProfileData))
    return false;
  extractFromBranchWeightMD(ProfileData, Weights);
  return true;
}

bool extractBranchWeights(const Instruction &I,
                          SmallVectorImpl<uint32_t> &Weights) {
  auto *ProfileData = I.getMetadata(LLVMContext::MD_prof);
  return extractBranchWeights(ProfileData, Weights);
}

bool extractBranchWeights(const Instruction &I, uint64_t &TrueVal,
                          uint64_t &FalseVal) {
  assert((I.getOpcode() == Instruction::Br ||
          I.getOpcode() == Instruction::Select) &&
         "Looking for branch weights on something besides branch, select, or "
         "switch");

  SmallVector<uint32_t, 2> Weights;
  auto *ProfileData = I.getMetadata(LLVMContext::MD_prof);
  if (!extractBranchWeights(ProfileData, Weights))
    return false;

  if (Weights.size() > 2)
    return false;

  TrueVal = Weights[0];
  FalseVal = Weights[1];
  return true;
}

bool extractProfTotalWeight(const MDNode *ProfileData, uint64_t &TotalVal) {
  TotalVal = 0;
  if (!ProfileData)
    return false;

  auto *ProfDataName = dyn_cast<MDString>(ProfileData->getOperand(0));
  if (!ProfDataName)
    return false;

  if (ProfDataName->getString() == "branch_weights") {
    unsigned Offset = getBranchWeightOffset(ProfileData);
    for (unsigned Idx = Offset; Idx < ProfileData->getNumOperands(); ++Idx) {
      auto *V = mdconst::dyn_extract<ConstantInt>(ProfileData->getOperand(Idx));
      assert(V && "Malformed branch_weight in MD_prof node");
      TotalVal += V->getValue().getZExtValue();
    }
    return true;
  }

  if (ProfDataName->getString() == "VP" && ProfileData->getNumOperands() > 3) {
    TotalVal = mdconst::dyn_extract<ConstantInt>(ProfileData->getOperand(2))
                   ->getValue()
                   .getZExtValue();
    return true;
  }
  return false;
}

bool extractProfTotalWeight(const Instruction &I, uint64_t &TotalVal) {
  return extractProfTotalWeight(I.getMetadata(LLVMContext::MD_prof), TotalVal);
}

void setBranchWeights(Instruction &I, ArrayRef<uint32_t> Weights,
                      bool IsExpected) {
  MDBuilder MDB(I.getContext());
  MDNode *BranchWeights = MDB.createBranchWeights(Weights, IsExpected);
  I.setMetadata(LLVMContext::MD_prof, BranchWeights);
}

void scaleProfData(Instruction &I, uint64_t S, uint64_t T) {
  assert(T != 0 && "Caller should guarantee");
  auto *ProfileData = I.getMetadata(LLVMContext::MD_prof);
  if (ProfileData == nullptr)
    return;

  auto *ProfDataName = dyn_cast<MDString>(ProfileData->getOperand(0));
  if (!ProfDataName || (ProfDataName->getString() != "branch_weights" &&
                        ProfDataName->getString() != "VP"))
    return;

  if (!hasCountTypeMD(I))
    return;

  LLVMContext &C = I.getContext();

  MDBuilder MDB(C);
  SmallVector<Metadata *, 3> Vals;
  Vals.push_back(ProfileData->getOperand(0));
  APInt APS(128, S), APT(128, T);
  if (ProfDataName->getString() == "branch_weights" &&
      ProfileData->getNumOperands() > 0) {
    // Using APInt::div may be expensive, but most cases should fit 64 bits.
    APInt Val(128,
              mdconst::dyn_extract<ConstantInt>(
                  ProfileData->getOperand(getBranchWeightOffset(ProfileData)))
                  ->getValue()
                  .getZExtValue());
    Val *= APS;
    Vals.push_back(MDB.createConstant(ConstantInt::get(
        Type::getInt32Ty(C), Val.udiv(APT).getLimitedValue(UINT32_MAX))));
  } else if (ProfDataName->getString() == "VP")
    for (unsigned i = 1; i < ProfileData->getNumOperands(); i += 2) {
      // The first value is the key of the value profile, which will not change.
      Vals.push_back(ProfileData->getOperand(i));
      uint64_t Count =
          mdconst::dyn_extract<ConstantInt>(ProfileData->getOperand(i + 1))
              ->getValue()
              .getZExtValue();
      // Don't scale the magic number.
      if (Count == NOMORE_ICP_MAGICNUM) {
        Vals.push_back(ProfileData->getOperand(i + 1));
        continue;
      }
      // Using APInt::div may be expensive, but most cases should fit 64 bits.
      APInt Val(128, Count);
      Val *= APS;
      Vals.push_back(MDB.createConstant(ConstantInt::get(
          Type::getInt64Ty(C), Val.udiv(APT).getLimitedValue())));
    }
  I.setMetadata(LLVMContext::MD_prof, MDNode::get(C, Vals));
}

} // namespace llvm
