//===- LLVMContextImpl.cpp - Implement LLVMContextImpl --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the opaque LLVMContextImpl.
//
//===----------------------------------------------------------------------===//

#include "LLVMContextImpl.h"
#include "AttributeImpl.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMapEntry.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/Remarks/RemarkStreamer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TypeSize.h"
#include <cassert>
#include <utility>

using namespace llvm;

LLVMContextImpl::LLVMContextImpl(LLVMContext &C)
    : DiagHandler(std::make_unique<DiagnosticHandler>()),
      VoidTy(C, Type::VoidTyID), LabelTy(C, Type::LabelTyID),
      HalfTy(C, Type::HalfTyID), BFloatTy(C, Type::BFloatTyID),
      FloatTy(C, Type::FloatTyID), DoubleTy(C, Type::DoubleTyID),
      MetadataTy(C, Type::MetadataTyID), TokenTy(C, Type::TokenTyID),
      X86_FP80Ty(C, Type::X86_FP80TyID), FP128Ty(C, Type::FP128TyID),
      PPC_FP128Ty(C, Type::PPC_FP128TyID), X86_MMXTy(C, Type::X86_MMXTyID),
      X86_AMXTy(C, Type::X86_AMXTyID), Int1Ty(C, 1), Int8Ty(C, 8),
      Int16Ty(C, 16), Int32Ty(C, 32), Int64Ty(C, 64), Int128Ty(C, 128) {}

LLVMContextImpl::~LLVMContextImpl() {
#ifndef NDEBUG
  // Check that any variable location records that fell off the end of a block
  // when it's terminator was removed were eventually replaced. This assertion
  // firing indicates that DbgVariableRecords went missing during the lifetime
  // of the LLVMContext.
  assert(TrailingDbgRecords.empty() && "DbgRecords in blocks not cleaned");
#endif

  // NOTE: We need to delete the contents of OwnedModules, but Module's dtor
  // will call LLVMContextImpl::removeModule, thus invalidating iterators into
  // the container. Avoid iterators during this operation:
  while (!OwnedModules.empty())
    delete *OwnedModules.begin();

#ifndef NDEBUG
  // Check for metadata references from leaked Values.
  for (auto &Pair : ValueMetadata)
    Pair.first->dump();
  assert(ValueMetadata.empty() && "Values with metadata have been leaked");
#endif

  // Drop references for MDNodes.  Do this before Values get deleted to avoid
  // unnecessary RAUW when nodes are still unresolved.
  for (auto *I : DistinctMDNodes)
    I->dropAllReferences();
#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  for (auto *I : CLASS##s)                                                     \
    I->dropAllReferences();
#include "llvm/IR/Metadata.def"

  // Also drop references that come from the Value bridges.
  for (auto &Pair : ValuesAsMetadata)
    Pair.second->dropUsers();
  for (auto &Pair : MetadataAsValues)
    Pair.second->dropUse();
  // Do not untrack ValueAsMetadata references for DIArgLists, as they have
  // already been more efficiently untracked above.
  for (DIArgList *AL : DIArgLists) {
    AL->dropAllReferences(/* Untrack */ false);
    delete AL;
  }
  DIArgLists.clear();

  // Destroy MDNodes.
  for (MDNode *I : DistinctMDNodes)
    I->deleteAsSubclass();

  for (auto *ConstantRangeListAttribute : ConstantRangeListAttributes)
    ConstantRangeListAttribute->~ConstantRangeListAttributeImpl();
#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  for (CLASS * I : CLASS##s)                                                   \
    delete I;
#include "llvm/IR/Metadata.def"

  // Free the constants.
  for (auto *I : ExprConstants)
    I->dropAllReferences();
  for (auto *I : ArrayConstants)
    I->dropAllReferences();
  for (auto *I : StructConstants)
    I->dropAllReferences();
  for (auto *I : VectorConstants)
    I->dropAllReferences();
  ExprConstants.freeConstants();
  ArrayConstants.freeConstants();
  StructConstants.freeConstants();
  VectorConstants.freeConstants();
  InlineAsms.freeConstants();

  CAZConstants.clear();
  CPNConstants.clear();
  CTNConstants.clear();
  UVConstants.clear();
  PVConstants.clear();
  IntZeroConstants.clear();
  IntOneConstants.clear();
  IntConstants.clear();
  IntSplatConstants.clear();
  FPConstants.clear();
  FPSplatConstants.clear();
  CDSConstants.clear();

  // Destroy attribute node lists.
  for (FoldingSetIterator<AttributeSetNode> I = AttrsSetNodes.begin(),
         E = AttrsSetNodes.end(); I != E; ) {
    FoldingSetIterator<AttributeSetNode> Elem = I++;
    delete &*Elem;
  }

  // Destroy MetadataAsValues.
  {
    SmallVector<MetadataAsValue *, 8> MDVs;
    MDVs.reserve(MetadataAsValues.size());
    for (auto &Pair : MetadataAsValues)
      MDVs.push_back(Pair.second);
    MetadataAsValues.clear();
    for (auto *V : MDVs)
      delete V;
  }

  // Destroy ValuesAsMetadata.
  for (auto &Pair : ValuesAsMetadata)
    delete Pair.second;
}

void LLVMContextImpl::dropTriviallyDeadConstantArrays() {
  SmallSetVector<ConstantArray *, 4> WorkList;

  // When ArrayConstants are of substantial size and only a few in them are
  // dead, starting WorkList with all elements of ArrayConstants can be
  // wasteful. Instead, starting WorkList with only elements that have empty
  // uses.
  for (ConstantArray *C : ArrayConstants)
    if (C->use_empty())
      WorkList.insert(C);

  while (!WorkList.empty()) {
    ConstantArray *C = WorkList.pop_back_val();
    if (C->use_empty()) {
      for (const Use &Op : C->operands()) {
        if (auto *COp = dyn_cast<ConstantArray>(Op))
          WorkList.insert(COp);
      }
      C->destroyConstant();
    }
  }
}

void Module::dropTriviallyDeadConstantArrays() {
  Context.pImpl->dropTriviallyDeadConstantArrays();
}

namespace llvm {

/// Make MDOperand transparent for hashing.
///
/// This overload of an implementation detail of the hashing library makes
/// MDOperand hash to the same value as a \a Metadata pointer.
///
/// Note that overloading \a hash_value() as follows:
///
/// \code
///     size_t hash_value(const MDOperand &X) { return hash_value(X.get()); }
/// \endcode
///
/// does not cause MDOperand to be transparent.  In particular, a bare pointer
/// doesn't get hashed before it's combined, whereas \a MDOperand would.
static const Metadata *get_hashable_data(const MDOperand &X) { return X.get(); }

} // end namespace llvm

unsigned MDNodeOpsKey::calculateHash(MDNode *N, unsigned Offset) {
  unsigned Hash = hash_combine_range(N->op_begin() + Offset, N->op_end());
#ifndef NDEBUG
  {
    SmallVector<Metadata *, 8> MDs(drop_begin(N->operands(), Offset));
    unsigned RawHash = calculateHash(MDs);
    assert(Hash == RawHash &&
           "Expected hash of MDOperand to equal hash of Metadata*");
  }
#endif
  return Hash;
}

unsigned MDNodeOpsKey::calculateHash(ArrayRef<Metadata *> Ops) {
  return hash_combine_range(Ops.begin(), Ops.end());
}

StringMapEntry<uint32_t> *LLVMContextImpl::getOrInsertBundleTag(StringRef Tag) {
  uint32_t NewIdx = BundleTagCache.size();
  return &*(BundleTagCache.insert(std::make_pair(Tag, NewIdx)).first);
}

void LLVMContextImpl::getOperandBundleTags(SmallVectorImpl<StringRef> &Tags) const {
  Tags.resize(BundleTagCache.size());
  for (const auto &T : BundleTagCache)
    Tags[T.second] = T.first();
}

uint32_t LLVMContextImpl::getOperandBundleTagID(StringRef Tag) const {
  auto I = BundleTagCache.find(Tag);
  assert(I != BundleTagCache.end() && "Unknown tag!");
  return I->second;
}

SyncScope::ID LLVMContextImpl::getOrInsertSyncScopeID(StringRef SSN) {
  auto NewSSID = SSC.size();
  assert(NewSSID < std::numeric_limits<SyncScope::ID>::max() &&
         "Hit the maximum number of synchronization scopes allowed!");
  return SSC.insert(std::make_pair(SSN, SyncScope::ID(NewSSID))).first->second;
}

void LLVMContextImpl::getSyncScopeNames(
    SmallVectorImpl<StringRef> &SSNs) const {
  SSNs.resize(SSC.size());
  for (const auto &SSE : SSC)
    SSNs[SSE.second] = SSE.first();
}

/// Gets the OptPassGate for this LLVMContextImpl, which defaults to the
/// singleton OptBisect if not explicitly set.
OptPassGate &LLVMContextImpl::getOptPassGate() const {
  if (!OPG)
    OPG = &getGlobalPassGate();
  return *OPG;
}

void LLVMContextImpl::setOptPassGate(OptPassGate& OPG) {
  this->OPG = &OPG;
}
