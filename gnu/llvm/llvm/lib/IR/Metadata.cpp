//===- Metadata.cpp - Implement Metadata classes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Metadata classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Metadata.h"
#include "LLVMContextImpl.h"
#include "MetadataImpl.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

using namespace llvm;

MetadataAsValue::MetadataAsValue(Type *Ty, Metadata *MD)
    : Value(Ty, MetadataAsValueVal), MD(MD) {
  track();
}

MetadataAsValue::~MetadataAsValue() {
  getType()->getContext().pImpl->MetadataAsValues.erase(MD);
  untrack();
}

/// Canonicalize metadata arguments to intrinsics.
///
/// To support bitcode upgrades (and assembly semantic sugar) for \a
/// MetadataAsValue, we need to canonicalize certain metadata.
///
///   - nullptr is replaced by an empty MDNode.
///   - An MDNode with a single null operand is replaced by an empty MDNode.
///   - An MDNode whose only operand is a \a ConstantAsMetadata gets skipped.
///
/// This maintains readability of bitcode from when metadata was a type of
/// value, and these bridges were unnecessary.
static Metadata *canonicalizeMetadataForValue(LLVMContext &Context,
                                              Metadata *MD) {
  if (!MD)
    // !{}
    return MDNode::get(Context, std::nullopt);

  // Return early if this isn't a single-operand MDNode.
  auto *N = dyn_cast<MDNode>(MD);
  if (!N || N->getNumOperands() != 1)
    return MD;

  if (!N->getOperand(0))
    // !{}
    return MDNode::get(Context, std::nullopt);

  if (auto *C = dyn_cast<ConstantAsMetadata>(N->getOperand(0)))
    // Look through the MDNode.
    return C;

  return MD;
}

MetadataAsValue *MetadataAsValue::get(LLVMContext &Context, Metadata *MD) {
  MD = canonicalizeMetadataForValue(Context, MD);
  auto *&Entry = Context.pImpl->MetadataAsValues[MD];
  if (!Entry)
    Entry = new MetadataAsValue(Type::getMetadataTy(Context), MD);
  return Entry;
}

MetadataAsValue *MetadataAsValue::getIfExists(LLVMContext &Context,
                                              Metadata *MD) {
  MD = canonicalizeMetadataForValue(Context, MD);
  auto &Store = Context.pImpl->MetadataAsValues;
  return Store.lookup(MD);
}

void MetadataAsValue::handleChangedMetadata(Metadata *MD) {
  LLVMContext &Context = getContext();
  MD = canonicalizeMetadataForValue(Context, MD);
  auto &Store = Context.pImpl->MetadataAsValues;

  // Stop tracking the old metadata.
  Store.erase(this->MD);
  untrack();
  this->MD = nullptr;

  // Start tracking MD, or RAUW if necessary.
  auto *&Entry = Store[MD];
  if (Entry) {
    replaceAllUsesWith(Entry);
    delete this;
    return;
  }

  this->MD = MD;
  track();
  Entry = this;
}

void MetadataAsValue::track() {
  if (MD)
    MetadataTracking::track(&MD, *MD, *this);
}

void MetadataAsValue::untrack() {
  if (MD)
    MetadataTracking::untrack(MD);
}

DbgVariableRecord *DebugValueUser::getUser() {
  return static_cast<DbgVariableRecord *>(this);
}
const DbgVariableRecord *DebugValueUser::getUser() const {
  return static_cast<const DbgVariableRecord *>(this);
}

void DebugValueUser::handleChangedValue(void *Old, Metadata *New) {
  // NOTE: We could inform the "owner" that a value has changed through
  // getOwner, if needed.
  auto OldMD = static_cast<Metadata **>(Old);
  ptrdiff_t Idx = std::distance(&*DebugValues.begin(), OldMD);
  // If replacing a ValueAsMetadata with a nullptr, replace it with a
  // PoisonValue instead.
  if (OldMD && isa<ValueAsMetadata>(*OldMD) && !New) {
    auto *OldVAM = cast<ValueAsMetadata>(*OldMD);
    New = ValueAsMetadata::get(PoisonValue::get(OldVAM->getValue()->getType()));
  }
  resetDebugValue(Idx, New);
}

void DebugValueUser::trackDebugValue(size_t Idx) {
  assert(Idx < 3 && "Invalid debug value index.");
  Metadata *&MD = DebugValues[Idx];
  if (MD)
    MetadataTracking::track(&MD, *MD, *this);
}

void DebugValueUser::trackDebugValues() {
  for (Metadata *&MD : DebugValues)
    if (MD)
      MetadataTracking::track(&MD, *MD, *this);
}

void DebugValueUser::untrackDebugValue(size_t Idx) {
  assert(Idx < 3 && "Invalid debug value index.");
  Metadata *&MD = DebugValues[Idx];
  if (MD)
    MetadataTracking::untrack(MD);
}

void DebugValueUser::untrackDebugValues() {
  for (Metadata *&MD : DebugValues)
    if (MD)
      MetadataTracking::untrack(MD);
}

void DebugValueUser::retrackDebugValues(DebugValueUser &X) {
  assert(DebugValueUser::operator==(X) && "Expected values to match");
  for (const auto &[MD, XMD] : zip(DebugValues, X.DebugValues))
    if (XMD)
      MetadataTracking::retrack(XMD, MD);
  X.DebugValues.fill(nullptr);
}

bool MetadataTracking::track(void *Ref, Metadata &MD, OwnerTy Owner) {
  assert(Ref && "Expected live reference");
  assert((Owner || *static_cast<Metadata **>(Ref) == &MD) &&
         "Reference without owner must be direct");
  if (auto *R = ReplaceableMetadataImpl::getOrCreate(MD)) {
    R->addRef(Ref, Owner);
    return true;
  }
  if (auto *PH = dyn_cast<DistinctMDOperandPlaceholder>(&MD)) {
    assert(!PH->Use && "Placeholders can only be used once");
    assert(!Owner && "Unexpected callback to owner");
    PH->Use = static_cast<Metadata **>(Ref);
    return true;
  }
  return false;
}

void MetadataTracking::untrack(void *Ref, Metadata &MD) {
  assert(Ref && "Expected live reference");
  if (auto *R = ReplaceableMetadataImpl::getIfExists(MD))
    R->dropRef(Ref);
  else if (auto *PH = dyn_cast<DistinctMDOperandPlaceholder>(&MD))
    PH->Use = nullptr;
}

bool MetadataTracking::retrack(void *Ref, Metadata &MD, void *New) {
  assert(Ref && "Expected live reference");
  assert(New && "Expected live reference");
  assert(Ref != New && "Expected change");
  if (auto *R = ReplaceableMetadataImpl::getIfExists(MD)) {
    R->moveRef(Ref, New, MD);
    return true;
  }
  assert(!isa<DistinctMDOperandPlaceholder>(MD) &&
         "Unexpected move of an MDOperand");
  assert(!isReplaceable(MD) &&
         "Expected un-replaceable metadata, since we didn't move a reference");
  return false;
}

bool MetadataTracking::isReplaceable(const Metadata &MD) {
  return ReplaceableMetadataImpl::isReplaceable(MD);
}

SmallVector<Metadata *> ReplaceableMetadataImpl::getAllArgListUsers() {
  SmallVector<std::pair<OwnerTy, uint64_t> *> MDUsersWithID;
  for (auto Pair : UseMap) {
    OwnerTy Owner = Pair.second.first;
    if (Owner.isNull())
      continue;
    if (!isa<Metadata *>(Owner))
      continue;
    Metadata *OwnerMD = cast<Metadata *>(Owner);
    if (OwnerMD->getMetadataID() == Metadata::DIArgListKind)
      MDUsersWithID.push_back(&UseMap[Pair.first]);
  }
  llvm::sort(MDUsersWithID, [](auto UserA, auto UserB) {
    return UserA->second < UserB->second;
  });
  SmallVector<Metadata *> MDUsers;
  for (auto *UserWithID : MDUsersWithID)
    MDUsers.push_back(cast<Metadata *>(UserWithID->first));
  return MDUsers;
}

SmallVector<DbgVariableRecord *>
ReplaceableMetadataImpl::getAllDbgVariableRecordUsers() {
  SmallVector<std::pair<OwnerTy, uint64_t> *> DVRUsersWithID;
  for (auto Pair : UseMap) {
    OwnerTy Owner = Pair.second.first;
    if (Owner.isNull())
      continue;
    if (!Owner.is<DebugValueUser *>())
      continue;
    DVRUsersWithID.push_back(&UseMap[Pair.first]);
  }
  // Order DbgVariableRecord users in reverse-creation order. Normal dbg.value
  // users of MetadataAsValues are ordered by their UseList, i.e. reverse order
  // of when they were added: we need to replicate that here. The structure of
  // debug-info output depends on the ordering of intrinsics, thus we need
  // to keep them consistent for comparisons sake.
  llvm::sort(DVRUsersWithID, [](auto UserA, auto UserB) {
    return UserA->second > UserB->second;
  });
  SmallVector<DbgVariableRecord *> DVRUsers;
  for (auto UserWithID : DVRUsersWithID)
    DVRUsers.push_back(UserWithID->first.get<DebugValueUser *>()->getUser());
  return DVRUsers;
}

void ReplaceableMetadataImpl::addRef(void *Ref, OwnerTy Owner) {
  bool WasInserted =
      UseMap.insert(std::make_pair(Ref, std::make_pair(Owner, NextIndex)))
          .second;
  (void)WasInserted;
  assert(WasInserted && "Expected to add a reference");

  ++NextIndex;
  assert(NextIndex != 0 && "Unexpected overflow");
}

void ReplaceableMetadataImpl::dropRef(void *Ref) {
  bool WasErased = UseMap.erase(Ref);
  (void)WasErased;
  assert(WasErased && "Expected to drop a reference");
}

void ReplaceableMetadataImpl::moveRef(void *Ref, void *New,
                                      const Metadata &MD) {
  auto I = UseMap.find(Ref);
  assert(I != UseMap.end() && "Expected to move a reference");
  auto OwnerAndIndex = I->second;
  UseMap.erase(I);
  bool WasInserted = UseMap.insert(std::make_pair(New, OwnerAndIndex)).second;
  (void)WasInserted;
  assert(WasInserted && "Expected to add a reference");

  // Check that the references are direct if there's no owner.
  (void)MD;
  assert((OwnerAndIndex.first || *static_cast<Metadata **>(Ref) == &MD) &&
         "Reference without owner must be direct");
  assert((OwnerAndIndex.first || *static_cast<Metadata **>(New) == &MD) &&
         "Reference without owner must be direct");
}

void ReplaceableMetadataImpl::SalvageDebugInfo(const Constant &C) {
  if (!C.isUsedByMetadata()) {
    return;
  }

  LLVMContext &Context = C.getType()->getContext();
  auto &Store = Context.pImpl->ValuesAsMetadata;
  auto I = Store.find(&C);
  ValueAsMetadata *MD = I->second;
  using UseTy =
      std::pair<void *, std::pair<MetadataTracking::OwnerTy, uint64_t>>;
  // Copy out uses and update value of Constant used by debug info metadata with undef below
  SmallVector<UseTy, 8> Uses(MD->UseMap.begin(), MD->UseMap.end());

  for (const auto &Pair : Uses) {
    MetadataTracking::OwnerTy Owner = Pair.second.first;
    if (!Owner)
      continue;
    if (!isa<Metadata *>(Owner))
      continue;
    auto *OwnerMD = dyn_cast_if_present<MDNode>(cast<Metadata *>(Owner));
    if (!OwnerMD)
      continue;
    if (isa<DINode>(OwnerMD)) {
      OwnerMD->handleChangedOperand(
          Pair.first, ValueAsMetadata::get(UndefValue::get(C.getType())));
    }
  }
}

void ReplaceableMetadataImpl::replaceAllUsesWith(Metadata *MD) {
  if (UseMap.empty())
    return;

  // Copy out uses since UseMap will get touched below.
  using UseTy = std::pair<void *, std::pair<OwnerTy, uint64_t>>;
  SmallVector<UseTy, 8> Uses(UseMap.begin(), UseMap.end());
  llvm::sort(Uses, [](const UseTy &L, const UseTy &R) {
    return L.second.second < R.second.second;
  });
  for (const auto &Pair : Uses) {
    // Check that this Ref hasn't disappeared after RAUW (when updating a
    // previous Ref).
    if (!UseMap.count(Pair.first))
      continue;

    OwnerTy Owner = Pair.second.first;
    if (!Owner) {
      // Update unowned tracking references directly.
      Metadata *&Ref = *static_cast<Metadata **>(Pair.first);
      Ref = MD;
      if (MD)
        MetadataTracking::track(Ref);
      UseMap.erase(Pair.first);
      continue;
    }

    // Check for MetadataAsValue.
    if (isa<MetadataAsValue *>(Owner)) {
      cast<MetadataAsValue *>(Owner)->handleChangedMetadata(MD);
      continue;
    }

    if (Owner.is<DebugValueUser *>()) {
      Owner.get<DebugValueUser *>()->handleChangedValue(Pair.first, MD);
      continue;
    }

    // There's a Metadata owner -- dispatch.
    Metadata *OwnerMD = cast<Metadata *>(Owner);
    switch (OwnerMD->getMetadataID()) {
#define HANDLE_METADATA_LEAF(CLASS)                                            \
  case Metadata::CLASS##Kind:                                                  \
    cast<CLASS>(OwnerMD)->handleChangedOperand(Pair.first, MD);                \
    continue;
#include "llvm/IR/Metadata.def"
    default:
      llvm_unreachable("Invalid metadata subclass");
    }
  }
  assert(UseMap.empty() && "Expected all uses to be replaced");
}

void ReplaceableMetadataImpl::resolveAllUses(bool ResolveUsers) {
  if (UseMap.empty())
    return;

  if (!ResolveUsers) {
    UseMap.clear();
    return;
  }

  // Copy out uses since UseMap could get touched below.
  using UseTy = std::pair<void *, std::pair<OwnerTy, uint64_t>>;
  SmallVector<UseTy, 8> Uses(UseMap.begin(), UseMap.end());
  llvm::sort(Uses, [](const UseTy &L, const UseTy &R) {
    return L.second.second < R.second.second;
  });
  UseMap.clear();
  for (const auto &Pair : Uses) {
    auto Owner = Pair.second.first;
    if (!Owner)
      continue;
    if (!Owner.is<Metadata *>())
      continue;

    // Resolve MDNodes that point at this.
    auto *OwnerMD = dyn_cast_if_present<MDNode>(cast<Metadata *>(Owner));
    if (!OwnerMD)
      continue;
    if (OwnerMD->isResolved())
      continue;
    OwnerMD->decrementUnresolvedOperandCount();
  }
}

// Special handing of DIArgList is required in the RemoveDIs project, see
// commentry in DIArgList::handleChangedOperand for details. Hidden behind
// conditional compilation to avoid a compile time regression.
ReplaceableMetadataImpl *ReplaceableMetadataImpl::getOrCreate(Metadata &MD) {
  if (auto *N = dyn_cast<MDNode>(&MD)) {
    return !N->isResolved() || N->isAlwaysReplaceable()
               ? N->Context.getOrCreateReplaceableUses()
               : nullptr;
  }
  if (auto ArgList = dyn_cast<DIArgList>(&MD))
    return ArgList;
  return dyn_cast<ValueAsMetadata>(&MD);
}

ReplaceableMetadataImpl *ReplaceableMetadataImpl::getIfExists(Metadata &MD) {
  if (auto *N = dyn_cast<MDNode>(&MD)) {
    return !N->isResolved() || N->isAlwaysReplaceable()
               ? N->Context.getReplaceableUses()
               : nullptr;
  }
  if (auto ArgList = dyn_cast<DIArgList>(&MD))
    return ArgList;
  return dyn_cast<ValueAsMetadata>(&MD);
}

bool ReplaceableMetadataImpl::isReplaceable(const Metadata &MD) {
  if (auto *N = dyn_cast<MDNode>(&MD))
    return !N->isResolved() || N->isAlwaysReplaceable();
  return isa<ValueAsMetadata>(&MD) || isa<DIArgList>(&MD);
}

static DISubprogram *getLocalFunctionMetadata(Value *V) {
  assert(V && "Expected value");
  if (auto *A = dyn_cast<Argument>(V)) {
    if (auto *Fn = A->getParent())
      return Fn->getSubprogram();
    return nullptr;
  }

  if (BasicBlock *BB = cast<Instruction>(V)->getParent()) {
    if (auto *Fn = BB->getParent())
      return Fn->getSubprogram();
    return nullptr;
  }

  return nullptr;
}

ValueAsMetadata *ValueAsMetadata::get(Value *V) {
  assert(V && "Unexpected null Value");

  auto &Context = V->getContext();
  auto *&Entry = Context.pImpl->ValuesAsMetadata[V];
  if (!Entry) {
    assert((isa<Constant>(V) || isa<Argument>(V) || isa<Instruction>(V)) &&
           "Expected constant or function-local value");
    assert(!V->IsUsedByMD && "Expected this to be the only metadata use");
    V->IsUsedByMD = true;
    if (auto *C = dyn_cast<Constant>(V))
      Entry = new ConstantAsMetadata(C);
    else
      Entry = new LocalAsMetadata(V);
  }

  return Entry;
}

ValueAsMetadata *ValueAsMetadata::getIfExists(Value *V) {
  assert(V && "Unexpected null Value");
  return V->getContext().pImpl->ValuesAsMetadata.lookup(V);
}

void ValueAsMetadata::handleDeletion(Value *V) {
  assert(V && "Expected valid value");

  auto &Store = V->getType()->getContext().pImpl->ValuesAsMetadata;
  auto I = Store.find(V);
  if (I == Store.end())
    return;

  // Remove old entry from the map.
  ValueAsMetadata *MD = I->second;
  assert(MD && "Expected valid metadata");
  assert(MD->getValue() == V && "Expected valid mapping");
  Store.erase(I);

  // Delete the metadata.
  MD->replaceAllUsesWith(nullptr);
  delete MD;
}

void ValueAsMetadata::handleRAUW(Value *From, Value *To) {
  assert(From && "Expected valid value");
  assert(To && "Expected valid value");
  assert(From != To && "Expected changed value");
  assert(&From->getContext() == &To->getContext() && "Expected same context");

  LLVMContext &Context = From->getType()->getContext();
  auto &Store = Context.pImpl->ValuesAsMetadata;
  auto I = Store.find(From);
  if (I == Store.end()) {
    assert(!From->IsUsedByMD && "Expected From not to be used by metadata");
    return;
  }

  // Remove old entry from the map.
  assert(From->IsUsedByMD && "Expected From to be used by metadata");
  From->IsUsedByMD = false;
  ValueAsMetadata *MD = I->second;
  assert(MD && "Expected valid metadata");
  assert(MD->getValue() == From && "Expected valid mapping");
  Store.erase(I);

  if (isa<LocalAsMetadata>(MD)) {
    if (auto *C = dyn_cast<Constant>(To)) {
      // Local became a constant.
      MD->replaceAllUsesWith(ConstantAsMetadata::get(C));
      delete MD;
      return;
    }
    if (getLocalFunctionMetadata(From) && getLocalFunctionMetadata(To) &&
        getLocalFunctionMetadata(From) != getLocalFunctionMetadata(To)) {
      // DISubprogram changed.
      MD->replaceAllUsesWith(nullptr);
      delete MD;
      return;
    }
  } else if (!isa<Constant>(To)) {
    // Changed to function-local value.
    MD->replaceAllUsesWith(nullptr);
    delete MD;
    return;
  }

  auto *&Entry = Store[To];
  if (Entry) {
    // The target already exists.
    MD->replaceAllUsesWith(Entry);
    delete MD;
    return;
  }

  // Update MD in place (and update the map entry).
  assert(!To->IsUsedByMD && "Expected this to be the only metadata use");
  To->IsUsedByMD = true;
  MD->V = To;
  Entry = MD;
}

//===----------------------------------------------------------------------===//
// MDString implementation.
//

MDString *MDString::get(LLVMContext &Context, StringRef Str) {
  auto &Store = Context.pImpl->MDStringCache;
  auto I = Store.try_emplace(Str);
  auto &MapEntry = I.first->getValue();
  if (!I.second)
    return &MapEntry;
  MapEntry.Entry = &*I.first;
  return &MapEntry;
}

StringRef MDString::getString() const {
  assert(Entry && "Expected to find string map entry");
  return Entry->first();
}

//===----------------------------------------------------------------------===//
// MDNode implementation.
//

// Assert that the MDNode types will not be unaligned by the objects
// prepended to them.
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  static_assert(                                                               \
      alignof(uint64_t) >= alignof(CLASS),                                     \
      "Alignment is insufficient after objects prepended to " #CLASS);
#include "llvm/IR/Metadata.def"

void *MDNode::operator new(size_t Size, size_t NumOps, StorageType Storage) {
  // uint64_t is the most aligned type we need support (ensured by static_assert
  // above)
  size_t AllocSize =
      alignTo(Header::getAllocSize(Storage, NumOps), alignof(uint64_t));
  char *Mem = reinterpret_cast<char *>(::operator new(AllocSize + Size));
  Header *H = new (Mem + AllocSize - sizeof(Header)) Header(NumOps, Storage);
  return reinterpret_cast<void *>(H + 1);
}

void MDNode::operator delete(void *N) {
  Header *H = reinterpret_cast<Header *>(N) - 1;
  void *Mem = H->getAllocation();
  H->~Header();
  ::operator delete(Mem);
}

MDNode::MDNode(LLVMContext &Context, unsigned ID, StorageType Storage,
               ArrayRef<Metadata *> Ops1, ArrayRef<Metadata *> Ops2)
    : Metadata(ID, Storage), Context(Context) {
  unsigned Op = 0;
  for (Metadata *MD : Ops1)
    setOperand(Op++, MD);
  for (Metadata *MD : Ops2)
    setOperand(Op++, MD);

  if (!isUniqued())
    return;

  // Count the unresolved operands.  If there are any, RAUW support will be
  // added lazily on first reference.
  countUnresolvedOperands();
}

TempMDNode MDNode::clone() const {
  switch (getMetadataID()) {
  default:
    llvm_unreachable("Invalid MDNode subclass");
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case CLASS##Kind:                                                            \
    return cast<CLASS>(this)->cloneImpl();
#include "llvm/IR/Metadata.def"
  }
}

MDNode::Header::Header(size_t NumOps, StorageType Storage) {
  IsLarge = isLarge(NumOps);
  IsResizable = isResizable(Storage);
  SmallSize = getSmallSize(NumOps, IsResizable, IsLarge);
  if (IsLarge) {
    SmallNumOps = 0;
    new (getLargePtr()) LargeStorageVector();
    getLarge().resize(NumOps);
    return;
  }
  SmallNumOps = NumOps;
  MDOperand *O = reinterpret_cast<MDOperand *>(this) - SmallSize;
  for (MDOperand *E = O + SmallSize; O != E;)
    (void)new (O++) MDOperand();
}

MDNode::Header::~Header() {
  if (IsLarge) {
    getLarge().~LargeStorageVector();
    return;
  }
  MDOperand *O = reinterpret_cast<MDOperand *>(this);
  for (MDOperand *E = O - SmallSize; O != E; --O)
    (void)(O - 1)->~MDOperand();
}

void *MDNode::Header::getSmallPtr() {
  static_assert(alignof(MDOperand) <= alignof(Header),
                "MDOperand too strongly aligned");
  return reinterpret_cast<char *>(const_cast<Header *>(this)) -
         sizeof(MDOperand) * SmallSize;
}

void MDNode::Header::resize(size_t NumOps) {
  assert(IsResizable && "Node is not resizable");
  if (operands().size() == NumOps)
    return;

  if (IsLarge)
    getLarge().resize(NumOps);
  else if (NumOps <= SmallSize)
    resizeSmall(NumOps);
  else
    resizeSmallToLarge(NumOps);
}

void MDNode::Header::resizeSmall(size_t NumOps) {
  assert(!IsLarge && "Expected a small MDNode");
  assert(NumOps <= SmallSize && "NumOps too large for small resize");

  MutableArrayRef<MDOperand> ExistingOps = operands();
  assert(NumOps != ExistingOps.size() && "Expected a different size");

  int NumNew = (int)NumOps - (int)ExistingOps.size();
  MDOperand *O = ExistingOps.end();
  for (int I = 0, E = NumNew; I < E; ++I)
    (O++)->reset();
  for (int I = 0, E = NumNew; I > E; --I)
    (--O)->reset();
  SmallNumOps = NumOps;
  assert(O == operands().end() && "Operands not (un)initialized until the end");
}

void MDNode::Header::resizeSmallToLarge(size_t NumOps) {
  assert(!IsLarge && "Expected a small MDNode");
  assert(NumOps > SmallSize && "Expected NumOps to be larger than allocation");
  LargeStorageVector NewOps;
  NewOps.resize(NumOps);
  llvm::move(operands(), NewOps.begin());
  resizeSmall(0);
  new (getLargePtr()) LargeStorageVector(std::move(NewOps));
  IsLarge = true;
}

static bool isOperandUnresolved(Metadata *Op) {
  if (auto *N = dyn_cast_or_null<MDNode>(Op))
    return !N->isResolved();
  return false;
}

void MDNode::countUnresolvedOperands() {
  assert(getNumUnresolved() == 0 && "Expected unresolved ops to be uncounted");
  assert(isUniqued() && "Expected this to be uniqued");
  setNumUnresolved(count_if(operands(), isOperandUnresolved));
}

void MDNode::makeUniqued() {
  assert(isTemporary() && "Expected this to be temporary");
  assert(!isResolved() && "Expected this to be unresolved");

  // Enable uniquing callbacks.
  for (auto &Op : mutable_operands())
    Op.reset(Op.get(), this);

  // Make this 'uniqued'.
  Storage = Uniqued;
  countUnresolvedOperands();
  if (!getNumUnresolved()) {
    dropReplaceableUses();
    assert(isResolved() && "Expected this to be resolved");
  }

  assert(isUniqued() && "Expected this to be uniqued");
}

void MDNode::makeDistinct() {
  assert(isTemporary() && "Expected this to be temporary");
  assert(!isResolved() && "Expected this to be unresolved");

  // Drop RAUW support and store as a distinct node.
  dropReplaceableUses();
  storeDistinctInContext();

  assert(isDistinct() && "Expected this to be distinct");
  assert(isResolved() && "Expected this to be resolved");
}

void MDNode::resolve() {
  assert(isUniqued() && "Expected this to be uniqued");
  assert(!isResolved() && "Expected this to be unresolved");

  setNumUnresolved(0);
  dropReplaceableUses();

  assert(isResolved() && "Expected this to be resolved");
}

void MDNode::dropReplaceableUses() {
  assert(!getNumUnresolved() && "Unexpected unresolved operand");

  // Drop any RAUW support.
  if (Context.hasReplaceableUses())
    Context.takeReplaceableUses()->resolveAllUses();
}

void MDNode::resolveAfterOperandChange(Metadata *Old, Metadata *New) {
  assert(isUniqued() && "Expected this to be uniqued");
  assert(getNumUnresolved() != 0 && "Expected unresolved operands");

  // Check if an operand was resolved.
  if (!isOperandUnresolved(Old)) {
    if (isOperandUnresolved(New))
      // An operand was un-resolved!
      setNumUnresolved(getNumUnresolved() + 1);
  } else if (!isOperandUnresolved(New))
    decrementUnresolvedOperandCount();
}

void MDNode::decrementUnresolvedOperandCount() {
  assert(!isResolved() && "Expected this to be unresolved");
  if (isTemporary())
    return;

  assert(isUniqued() && "Expected this to be uniqued");
  setNumUnresolved(getNumUnresolved() - 1);
  if (getNumUnresolved())
    return;

  // Last unresolved operand has just been resolved.
  dropReplaceableUses();
  assert(isResolved() && "Expected this to become resolved");
}

void MDNode::resolveCycles() {
  if (isResolved())
    return;

  // Resolve this node immediately.
  resolve();

  // Resolve all operands.
  for (const auto &Op : operands()) {
    auto *N = dyn_cast_or_null<MDNode>(Op);
    if (!N)
      continue;

    assert(!N->isTemporary() &&
           "Expected all forward declarations to be resolved");
    if (!N->isResolved())
      N->resolveCycles();
  }
}

static bool hasSelfReference(MDNode *N) {
  return llvm::is_contained(N->operands(), N);
}

MDNode *MDNode::replaceWithPermanentImpl() {
  switch (getMetadataID()) {
  default:
    // If this type isn't uniquable, replace with a distinct node.
    return replaceWithDistinctImpl();

#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  case CLASS##Kind:                                                            \
    break;
#include "llvm/IR/Metadata.def"
  }

  // Even if this type is uniquable, self-references have to be distinct.
  if (hasSelfReference(this))
    return replaceWithDistinctImpl();
  return replaceWithUniquedImpl();
}

MDNode *MDNode::replaceWithUniquedImpl() {
  // Try to uniquify in place.
  MDNode *UniquedNode = uniquify();

  if (UniquedNode == this) {
    makeUniqued();
    return this;
  }

  // Collision, so RAUW instead.
  replaceAllUsesWith(UniquedNode);
  deleteAsSubclass();
  return UniquedNode;
}

MDNode *MDNode::replaceWithDistinctImpl() {
  makeDistinct();
  return this;
}

void MDTuple::recalculateHash() {
  setHash(MDTupleInfo::KeyTy::calculateHash(this));
}

void MDNode::dropAllReferences() {
  for (unsigned I = 0, E = getNumOperands(); I != E; ++I)
    setOperand(I, nullptr);
  if (Context.hasReplaceableUses()) {
    Context.getReplaceableUses()->resolveAllUses(/* ResolveUsers */ false);
    (void)Context.takeReplaceableUses();
  }
}

void MDNode::handleChangedOperand(void *Ref, Metadata *New) {
  unsigned Op = static_cast<MDOperand *>(Ref) - op_begin();
  assert(Op < getNumOperands() && "Expected valid operand");

  if (!isUniqued()) {
    // This node is not uniqued.  Just set the operand and be done with it.
    setOperand(Op, New);
    return;
  }

  // This node is uniqued.
  eraseFromStore();

  Metadata *Old = getOperand(Op);
  setOperand(Op, New);

  // Drop uniquing for self-reference cycles and deleted constants.
  if (New == this || (!New && Old && isa<ConstantAsMetadata>(Old))) {
    if (!isResolved())
      resolve();
    storeDistinctInContext();
    return;
  }

  // Re-unique the node.
  auto *Uniqued = uniquify();
  if (Uniqued == this) {
    if (!isResolved())
      resolveAfterOperandChange(Old, New);
    return;
  }

  // Collision.
  if (!isResolved()) {
    // Still unresolved, so RAUW.
    //
    // First, clear out all operands to prevent any recursion (similar to
    // dropAllReferences(), but we still need the use-list).
    for (unsigned O = 0, E = getNumOperands(); O != E; ++O)
      setOperand(O, nullptr);
    if (Context.hasReplaceableUses())
      Context.getReplaceableUses()->replaceAllUsesWith(Uniqued);
    deleteAsSubclass();
    return;
  }

  // Store in non-uniqued form if RAUW isn't possible.
  storeDistinctInContext();
}

void MDNode::deleteAsSubclass() {
  switch (getMetadataID()) {
  default:
    llvm_unreachable("Invalid subclass of MDNode");
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case CLASS##Kind:                                                            \
    delete cast<CLASS>(this);                                                  \
    break;
#include "llvm/IR/Metadata.def"
  }
}

template <class T, class InfoT>
static T *uniquifyImpl(T *N, DenseSet<T *, InfoT> &Store) {
  if (T *U = getUniqued(Store, N))
    return U;

  Store.insert(N);
  return N;
}

template <class NodeTy> struct MDNode::HasCachedHash {
  using Yes = char[1];
  using No = char[2];
  template <class U, U Val> struct SFINAE {};

  template <class U>
  static Yes &check(SFINAE<void (U::*)(unsigned), &U::setHash> *);
  template <class U> static No &check(...);

  static const bool value = sizeof(check<NodeTy>(nullptr)) == sizeof(Yes);
};

MDNode *MDNode::uniquify() {
  assert(!hasSelfReference(this) && "Cannot uniquify a self-referencing node");

  // Try to insert into uniquing store.
  switch (getMetadataID()) {
  default:
    llvm_unreachable("Invalid or non-uniquable subclass of MDNode");
#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  case CLASS##Kind: {                                                          \
    CLASS *SubclassThis = cast<CLASS>(this);                                   \
    std::integral_constant<bool, HasCachedHash<CLASS>::value>                  \
        ShouldRecalculateHash;                                                 \
    dispatchRecalculateHash(SubclassThis, ShouldRecalculateHash);              \
    return uniquifyImpl(SubclassThis, getContext().pImpl->CLASS##s);           \
  }
#include "llvm/IR/Metadata.def"
  }
}

void MDNode::eraseFromStore() {
  switch (getMetadataID()) {
  default:
    llvm_unreachable("Invalid or non-uniquable subclass of MDNode");
#define HANDLE_MDNODE_LEAF_UNIQUABLE(CLASS)                                    \
  case CLASS##Kind:                                                            \
    getContext().pImpl->CLASS##s.erase(cast<CLASS>(this));                     \
    break;
#include "llvm/IR/Metadata.def"
  }
}

MDTuple *MDTuple::getImpl(LLVMContext &Context, ArrayRef<Metadata *> MDs,
                          StorageType Storage, bool ShouldCreate) {
  unsigned Hash = 0;
  if (Storage == Uniqued) {
    MDTupleInfo::KeyTy Key(MDs);
    if (auto *N = getUniqued(Context.pImpl->MDTuples, Key))
      return N;
    if (!ShouldCreate)
      return nullptr;
    Hash = Key.getHash();
  } else {
    assert(ShouldCreate && "Expected non-uniqued nodes to always be created");
  }

  return storeImpl(new (MDs.size(), Storage)
                       MDTuple(Context, Storage, Hash, MDs),
                   Storage, Context.pImpl->MDTuples);
}

void MDNode::deleteTemporary(MDNode *N) {
  assert(N->isTemporary() && "Expected temporary node");
  N->replaceAllUsesWith(nullptr);
  N->deleteAsSubclass();
}

void MDNode::storeDistinctInContext() {
  assert(!Context.hasReplaceableUses() && "Unexpected replaceable uses");
  assert(!getNumUnresolved() && "Unexpected unresolved nodes");
  Storage = Distinct;
  assert(isResolved() && "Expected this to be resolved");

  // Reset the hash.
  switch (getMetadataID()) {
  default:
    llvm_unreachable("Invalid subclass of MDNode");
#define HANDLE_MDNODE_LEAF(CLASS)                                              \
  case CLASS##Kind: {                                                          \
    std::integral_constant<bool, HasCachedHash<CLASS>::value> ShouldResetHash; \
    dispatchResetHash(cast<CLASS>(this), ShouldResetHash);                     \
    break;                                                                     \
  }
#include "llvm/IR/Metadata.def"
  }

  getContext().pImpl->DistinctMDNodes.push_back(this);
}

void MDNode::replaceOperandWith(unsigned I, Metadata *New) {
  if (getOperand(I) == New)
    return;

  if (!isUniqued()) {
    setOperand(I, New);
    return;
  }

  handleChangedOperand(mutable_begin() + I, New);
}

void MDNode::setOperand(unsigned I, Metadata *New) {
  assert(I < getNumOperands());
  mutable_begin()[I].reset(New, isUniqued() ? this : nullptr);
}

/// Get a node or a self-reference that looks like it.
///
/// Special handling for finding self-references, for use by \a
/// MDNode::concatenate() and \a MDNode::intersect() to maintain behaviour from
/// when self-referencing nodes were still uniqued.  If the first operand has
/// the same operands as \c Ops, return the first operand instead.
static MDNode *getOrSelfReference(LLVMContext &Context,
                                  ArrayRef<Metadata *> Ops) {
  if (!Ops.empty())
    if (MDNode *N = dyn_cast_or_null<MDNode>(Ops[0]))
      if (N->getNumOperands() == Ops.size() && N == N->getOperand(0)) {
        for (unsigned I = 1, E = Ops.size(); I != E; ++I)
          if (Ops[I] != N->getOperand(I))
            return MDNode::get(Context, Ops);
        return N;
      }

  return MDNode::get(Context, Ops);
}

MDNode *MDNode::concatenate(MDNode *A, MDNode *B) {
  if (!A)
    return B;
  if (!B)
    return A;

  SmallSetVector<Metadata *, 4> MDs(A->op_begin(), A->op_end());
  MDs.insert(B->op_begin(), B->op_end());

  // FIXME: This preserves long-standing behaviour, but is it really the right
  // behaviour?  Or was that an unintended side-effect of node uniquing?
  return getOrSelfReference(A->getContext(), MDs.getArrayRef());
}

MDNode *MDNode::intersect(MDNode *A, MDNode *B) {
  if (!A || !B)
    return nullptr;

  SmallSetVector<Metadata *, 4> MDs(A->op_begin(), A->op_end());
  SmallPtrSet<Metadata *, 4> BSet(B->op_begin(), B->op_end());
  MDs.remove_if([&](Metadata *MD) { return !BSet.count(MD); });

  // FIXME: This preserves long-standing behaviour, but is it really the right
  // behaviour?  Or was that an unintended side-effect of node uniquing?
  return getOrSelfReference(A->getContext(), MDs.getArrayRef());
}

MDNode *MDNode::getMostGenericAliasScope(MDNode *A, MDNode *B) {
  if (!A || !B)
    return nullptr;

  // Take the intersection of domains then union the scopes
  // within those domains
  SmallPtrSet<const MDNode *, 16> ADomains;
  SmallPtrSet<const MDNode *, 16> IntersectDomains;
  SmallSetVector<Metadata *, 4> MDs;
  for (const MDOperand &MDOp : A->operands())
    if (const MDNode *NAMD = dyn_cast<MDNode>(MDOp))
      if (const MDNode *Domain = AliasScopeNode(NAMD).getDomain())
        ADomains.insert(Domain);

  for (const MDOperand &MDOp : B->operands())
    if (const MDNode *NAMD = dyn_cast<MDNode>(MDOp))
      if (const MDNode *Domain = AliasScopeNode(NAMD).getDomain())
        if (ADomains.contains(Domain)) {
          IntersectDomains.insert(Domain);
          MDs.insert(MDOp);
        }

  for (const MDOperand &MDOp : A->operands())
    if (const MDNode *NAMD = dyn_cast<MDNode>(MDOp))
      if (const MDNode *Domain = AliasScopeNode(NAMD).getDomain())
        if (IntersectDomains.contains(Domain))
          MDs.insert(MDOp);

  return MDs.empty() ? nullptr
                     : getOrSelfReference(A->getContext(), MDs.getArrayRef());
}

MDNode *MDNode::getMostGenericFPMath(MDNode *A, MDNode *B) {
  if (!A || !B)
    return nullptr;

  APFloat AVal = mdconst::extract<ConstantFP>(A->getOperand(0))->getValueAPF();
  APFloat BVal = mdconst::extract<ConstantFP>(B->getOperand(0))->getValueAPF();
  if (AVal < BVal)
    return A;
  return B;
}

// Call instructions with branch weights are only used in SamplePGO as
// documented in
/// https://llvm.org/docs/BranchWeightMetadata.html#callinst).
MDNode *MDNode::mergeDirectCallProfMetadata(MDNode *A, MDNode *B,
                                            const Instruction *AInstr,
                                            const Instruction *BInstr) {
  assert(A && B && AInstr && BInstr && "Caller should guarantee");
  auto &Ctx = AInstr->getContext();
  MDBuilder MDHelper(Ctx);

  // LLVM IR verifier verifies !prof metadata has at least 2 operands.
  assert(A->getNumOperands() >= 2 && B->getNumOperands() >= 2 &&
         "!prof annotations should have no less than 2 operands");
  MDString *AMDS = dyn_cast<MDString>(A->getOperand(0));
  MDString *BMDS = dyn_cast<MDString>(B->getOperand(0));
  // LLVM IR verfier verifies first operand is MDString.
  assert(AMDS != nullptr && BMDS != nullptr &&
         "first operand should be a non-null MDString");
  StringRef AProfName = AMDS->getString();
  StringRef BProfName = BMDS->getString();
  if (AProfName == "branch_weights" && BProfName == "branch_weights") {
    ConstantInt *AInstrWeight = mdconst::dyn_extract<ConstantInt>(
        A->getOperand(getBranchWeightOffset(A)));
    ConstantInt *BInstrWeight = mdconst::dyn_extract<ConstantInt>(
        B->getOperand(getBranchWeightOffset(B)));
    assert(AInstrWeight && BInstrWeight && "verified by LLVM verifier");
    return MDNode::get(Ctx,
                       {MDHelper.createString("branch_weights"),
                        MDHelper.createConstant(ConstantInt::get(
                            Type::getInt64Ty(Ctx),
                            SaturatingAdd(AInstrWeight->getZExtValue(),
                                          BInstrWeight->getZExtValue())))});
  }
  return nullptr;
}

// Pass in both instructions and nodes. Instruction information (e.g.,
// instruction type) helps interpret profiles and make implementation clearer.
MDNode *MDNode::getMergedProfMetadata(MDNode *A, MDNode *B,
                                      const Instruction *AInstr,
                                      const Instruction *BInstr) {
  if (!(A && B)) {
    return A ? A : B;
  }

  assert(AInstr->getMetadata(LLVMContext::MD_prof) == A &&
         "Caller should guarantee");
  assert(BInstr->getMetadata(LLVMContext::MD_prof) == B &&
         "Caller should guarantee");

  const CallInst *ACall = dyn_cast<CallInst>(AInstr);
  const CallInst *BCall = dyn_cast<CallInst>(BInstr);

  // Both ACall and BCall are direct callsites.
  if (ACall && BCall && ACall->getCalledFunction() &&
      BCall->getCalledFunction())
    return mergeDirectCallProfMetadata(A, B, AInstr, BInstr);

  // The rest of the cases are not implemented but could be added
  // when there are use cases.
  return nullptr;
}

static bool isContiguous(const ConstantRange &A, const ConstantRange &B) {
  return A.getUpper() == B.getLower() || A.getLower() == B.getUpper();
}

static bool canBeMerged(const ConstantRange &A, const ConstantRange &B) {
  return !A.intersectWith(B).isEmptySet() || isContiguous(A, B);
}

static bool tryMergeRange(SmallVectorImpl<ConstantInt *> &EndPoints,
                          ConstantInt *Low, ConstantInt *High) {
  ConstantRange NewRange(Low->getValue(), High->getValue());
  unsigned Size = EndPoints.size();
  APInt LB = EndPoints[Size - 2]->getValue();
  APInt LE = EndPoints[Size - 1]->getValue();
  ConstantRange LastRange(LB, LE);
  if (canBeMerged(NewRange, LastRange)) {
    ConstantRange Union = LastRange.unionWith(NewRange);
    Type *Ty = High->getType();
    EndPoints[Size - 2] =
        cast<ConstantInt>(ConstantInt::get(Ty, Union.getLower()));
    EndPoints[Size - 1] =
        cast<ConstantInt>(ConstantInt::get(Ty, Union.getUpper()));
    return true;
  }
  return false;
}

static void addRange(SmallVectorImpl<ConstantInt *> &EndPoints,
                     ConstantInt *Low, ConstantInt *High) {
  if (!EndPoints.empty())
    if (tryMergeRange(EndPoints, Low, High))
      return;

  EndPoints.push_back(Low);
  EndPoints.push_back(High);
}

MDNode *MDNode::getMostGenericRange(MDNode *A, MDNode *B) {
  // Given two ranges, we want to compute the union of the ranges. This
  // is slightly complicated by having to combine the intervals and merge
  // the ones that overlap.

  if (!A || !B)
    return nullptr;

  if (A == B)
    return A;

  // First, walk both lists in order of the lower boundary of each interval.
  // At each step, try to merge the new interval to the last one we added.
  SmallVector<ConstantInt *, 4> EndPoints;
  unsigned AI = 0;
  unsigned BI = 0;
  unsigned AN = A->getNumOperands() / 2;
  unsigned BN = B->getNumOperands() / 2;
  while (AI < AN && BI < BN) {
    ConstantInt *ALow = mdconst::extract<ConstantInt>(A->getOperand(2 * AI));
    ConstantInt *BLow = mdconst::extract<ConstantInt>(B->getOperand(2 * BI));

    if (ALow->getValue().slt(BLow->getValue())) {
      addRange(EndPoints, ALow,
               mdconst::extract<ConstantInt>(A->getOperand(2 * AI + 1)));
      ++AI;
    } else {
      addRange(EndPoints, BLow,
               mdconst::extract<ConstantInt>(B->getOperand(2 * BI + 1)));
      ++BI;
    }
  }
  while (AI < AN) {
    addRange(EndPoints, mdconst::extract<ConstantInt>(A->getOperand(2 * AI)),
             mdconst::extract<ConstantInt>(A->getOperand(2 * AI + 1)));
    ++AI;
  }
  while (BI < BN) {
    addRange(EndPoints, mdconst::extract<ConstantInt>(B->getOperand(2 * BI)),
             mdconst::extract<ConstantInt>(B->getOperand(2 * BI + 1)));
    ++BI;
  }

  // We haven't handled wrap in the previous merge,
  // if we have at least 2 ranges (4 endpoints) we have to try to merge
  // the last and first ones.
  unsigned Size = EndPoints.size();
  if (Size > 2) {
    ConstantInt *FB = EndPoints[0];
    ConstantInt *FE = EndPoints[1];
    if (tryMergeRange(EndPoints, FB, FE)) {
      for (unsigned i = 0; i < Size - 2; ++i) {
        EndPoints[i] = EndPoints[i + 2];
      }
      EndPoints.resize(Size - 2);
    }
  }

  // If in the end we have a single range, it is possible that it is now the
  // full range. Just drop the metadata in that case.
  if (EndPoints.size() == 2) {
    ConstantRange Range(EndPoints[0]->getValue(), EndPoints[1]->getValue());
    if (Range.isFullSet())
      return nullptr;
  }

  SmallVector<Metadata *, 4> MDs;
  MDs.reserve(EndPoints.size());
  for (auto *I : EndPoints)
    MDs.push_back(ConstantAsMetadata::get(I));
  return MDNode::get(A->getContext(), MDs);
}

MDNode *MDNode::getMostGenericAlignmentOrDereferenceable(MDNode *A, MDNode *B) {
  if (!A || !B)
    return nullptr;

  ConstantInt *AVal = mdconst::extract<ConstantInt>(A->getOperand(0));
  ConstantInt *BVal = mdconst::extract<ConstantInt>(B->getOperand(0));
  if (AVal->getZExtValue() < BVal->getZExtValue())
    return A;
  return B;
}

//===----------------------------------------------------------------------===//
// NamedMDNode implementation.
//

static SmallVector<TrackingMDRef, 4> &getNMDOps(void *Operands) {
  return *(SmallVector<TrackingMDRef, 4> *)Operands;
}

NamedMDNode::NamedMDNode(const Twine &N)
    : Name(N.str()), Operands(new SmallVector<TrackingMDRef, 4>()) {}

NamedMDNode::~NamedMDNode() {
  dropAllReferences();
  delete &getNMDOps(Operands);
}

unsigned NamedMDNode::getNumOperands() const {
  return (unsigned)getNMDOps(Operands).size();
}

MDNode *NamedMDNode::getOperand(unsigned i) const {
  assert(i < getNumOperands() && "Invalid Operand number!");
  auto *N = getNMDOps(Operands)[i].get();
  return cast_or_null<MDNode>(N);
}

void NamedMDNode::addOperand(MDNode *M) { getNMDOps(Operands).emplace_back(M); }

void NamedMDNode::setOperand(unsigned I, MDNode *New) {
  assert(I < getNumOperands() && "Invalid operand number");
  getNMDOps(Operands)[I].reset(New);
}

void NamedMDNode::eraseFromParent() { getParent()->eraseNamedMetadata(this); }

void NamedMDNode::clearOperands() { getNMDOps(Operands).clear(); }

StringRef NamedMDNode::getName() const { return StringRef(Name); }

//===----------------------------------------------------------------------===//
// Instruction Metadata method implementations.
//

MDNode *MDAttachments::lookup(unsigned ID) const {
  for (const auto &A : Attachments)
    if (A.MDKind == ID)
      return A.Node;
  return nullptr;
}

void MDAttachments::get(unsigned ID, SmallVectorImpl<MDNode *> &Result) const {
  for (const auto &A : Attachments)
    if (A.MDKind == ID)
      Result.push_back(A.Node);
}

void MDAttachments::getAll(
    SmallVectorImpl<std::pair<unsigned, MDNode *>> &Result) const {
  for (const auto &A : Attachments)
    Result.emplace_back(A.MDKind, A.Node);

  // Sort the resulting array so it is stable with respect to metadata IDs. We
  // need to preserve the original insertion order though.
  if (Result.size() > 1)
    llvm::stable_sort(Result, less_first());
}

void MDAttachments::set(unsigned ID, MDNode *MD) {
  erase(ID);
  if (MD)
    insert(ID, *MD);
}

void MDAttachments::insert(unsigned ID, MDNode &MD) {
  Attachments.push_back({ID, TrackingMDNodeRef(&MD)});
}

bool MDAttachments::erase(unsigned ID) {
  if (empty())
    return false;

  // Common case is one value.
  if (Attachments.size() == 1 && Attachments.back().MDKind == ID) {
    Attachments.pop_back();
    return true;
  }

  auto OldSize = Attachments.size();
  llvm::erase_if(Attachments,
                 [ID](const Attachment &A) { return A.MDKind == ID; });
  return OldSize != Attachments.size();
}

MDNode *Value::getMetadata(StringRef Kind) const {
  if (!hasMetadata())
    return nullptr;
  unsigned KindID = getContext().getMDKindID(Kind);
  return getMetadataImpl(KindID);
}

MDNode *Value::getMetadataImpl(unsigned KindID) const {
  const LLVMContext &Ctx = getContext();
  const MDAttachments &Attachements = Ctx.pImpl->ValueMetadata.at(this);
  return Attachements.lookup(KindID);
}

void Value::getMetadata(unsigned KindID, SmallVectorImpl<MDNode *> &MDs) const {
  if (hasMetadata())
    getContext().pImpl->ValueMetadata.at(this).get(KindID, MDs);
}

void Value::getMetadata(StringRef Kind, SmallVectorImpl<MDNode *> &MDs) const {
  if (hasMetadata())
    getMetadata(getContext().getMDKindID(Kind), MDs);
}

void Value::getAllMetadata(
    SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs) const {
  if (hasMetadata()) {
    assert(getContext().pImpl->ValueMetadata.count(this) &&
           "bit out of sync with hash table");
    const MDAttachments &Info = getContext().pImpl->ValueMetadata.at(this);
    Info.getAll(MDs);
  }
}

void Value::setMetadata(unsigned KindID, MDNode *Node) {
  assert(isa<Instruction>(this) || isa<GlobalObject>(this));

  // Handle the case when we're adding/updating metadata on a value.
  if (Node) {
    MDAttachments &Info = getContext().pImpl->ValueMetadata[this];
    assert(!Info.empty() == HasMetadata && "bit out of sync with hash table");
    if (Info.empty())
      HasMetadata = true;
    Info.set(KindID, Node);
    return;
  }

  // Otherwise, we're removing metadata from an instruction.
  assert((HasMetadata == (getContext().pImpl->ValueMetadata.count(this) > 0)) &&
         "bit out of sync with hash table");
  if (!HasMetadata)
    return; // Nothing to remove!
  MDAttachments &Info = getContext().pImpl->ValueMetadata.find(this)->second;

  // Handle removal of an existing value.
  Info.erase(KindID);
  if (!Info.empty())
    return;
  getContext().pImpl->ValueMetadata.erase(this);
  HasMetadata = false;
}

void Value::setMetadata(StringRef Kind, MDNode *Node) {
  if (!Node && !HasMetadata)
    return;
  setMetadata(getContext().getMDKindID(Kind), Node);
}

void Value::addMetadata(unsigned KindID, MDNode &MD) {
  assert(isa<Instruction>(this) || isa<GlobalObject>(this));
  if (!HasMetadata)
    HasMetadata = true;
  getContext().pImpl->ValueMetadata[this].insert(KindID, MD);
}

void Value::addMetadata(StringRef Kind, MDNode &MD) {
  addMetadata(getContext().getMDKindID(Kind), MD);
}

bool Value::eraseMetadata(unsigned KindID) {
  // Nothing to unset.
  if (!HasMetadata)
    return false;

  MDAttachments &Store = getContext().pImpl->ValueMetadata.find(this)->second;
  bool Changed = Store.erase(KindID);
  if (Store.empty())
    clearMetadata();
  return Changed;
}

void Value::eraseMetadataIf(function_ref<bool(unsigned, MDNode *)> Pred) {
  if (!HasMetadata)
    return;

  auto &MetadataStore = getContext().pImpl->ValueMetadata;
  MDAttachments &Info = MetadataStore.find(this)->second;
  assert(!Info.empty() && "bit out of sync with hash table");
  Info.remove_if([Pred](const MDAttachments::Attachment &I) {
    return Pred(I.MDKind, I.Node);
  });

  if (Info.empty())
    clearMetadata();
}

void Value::clearMetadata() {
  if (!HasMetadata)
    return;
  assert(getContext().pImpl->ValueMetadata.count(this) &&
         "bit out of sync with hash table");
  getContext().pImpl->ValueMetadata.erase(this);
  HasMetadata = false;
}

void Instruction::setMetadata(StringRef Kind, MDNode *Node) {
  if (!Node && !hasMetadata())
    return;
  setMetadata(getContext().getMDKindID(Kind), Node);
}

MDNode *Instruction::getMetadataImpl(StringRef Kind) const {
  const LLVMContext &Ctx = getContext();
  unsigned KindID = Ctx.getMDKindID(Kind);
  if (KindID == LLVMContext::MD_dbg)
    return DbgLoc.getAsMDNode();
  return Value::getMetadata(KindID);
}

void Instruction::eraseMetadataIf(function_ref<bool(unsigned, MDNode *)> Pred) {
  if (DbgLoc && Pred(LLVMContext::MD_dbg, DbgLoc.getAsMDNode()))
    DbgLoc = {};

  Value::eraseMetadataIf(Pred);
}

void Instruction::dropUnknownNonDebugMetadata(ArrayRef<unsigned> KnownIDs) {
  if (!Value::hasMetadata())
    return; // Nothing to remove!

  SmallSet<unsigned, 32> KnownSet;
  KnownSet.insert(KnownIDs.begin(), KnownIDs.end());

  // A DIAssignID attachment is debug metadata, don't drop it.
  KnownSet.insert(LLVMContext::MD_DIAssignID);

  Value::eraseMetadataIf([&KnownSet](unsigned MDKind, MDNode *Node) {
    return !KnownSet.count(MDKind);
  });
}

void Instruction::updateDIAssignIDMapping(DIAssignID *ID) {
  auto &IDToInstrs = getContext().pImpl->AssignmentIDToInstrs;
  if (const DIAssignID *CurrentID =
          cast_or_null<DIAssignID>(getMetadata(LLVMContext::MD_DIAssignID))) {
    // Nothing to do if the ID isn't changing.
    if (ID == CurrentID)
      return;

    // Unmap this instruction from its current ID.
    auto InstrsIt = IDToInstrs.find(CurrentID);
    assert(InstrsIt != IDToInstrs.end() &&
           "Expect existing attachment to be mapped");

    auto &InstVec = InstrsIt->second;
    auto *InstIt = llvm::find(InstVec, this);
    assert(InstIt != InstVec.end() &&
           "Expect instruction to be mapped to attachment");
    // The vector contains a ptr to this. If this is the only element in the
    // vector, remove the ID:vector entry, otherwise just remove the
    // instruction from the vector.
    if (InstVec.size() == 1)
      IDToInstrs.erase(InstrsIt);
    else
      InstVec.erase(InstIt);
  }

  // Map this instruction to the new ID.
  if (ID)
    IDToInstrs[ID].push_back(this);
}

void Instruction::setMetadata(unsigned KindID, MDNode *Node) {
  if (!Node && !hasMetadata())
    return;

  // Handle 'dbg' as a special case since it is not stored in the hash table.
  if (KindID == LLVMContext::MD_dbg) {
    DbgLoc = DebugLoc(Node);
    return;
  }

  // Update DIAssignID to Instruction(s) mapping.
  if (KindID == LLVMContext::MD_DIAssignID) {
    // The DIAssignID tracking infrastructure doesn't support RAUWing temporary
    // nodes with DIAssignIDs. The cast_or_null below would also catch this, but
    // having a dedicated assert helps make this obvious.
    assert((!Node || !Node->isTemporary()) &&
           "Temporary DIAssignIDs are invalid");
    updateDIAssignIDMapping(cast_or_null<DIAssignID>(Node));
  }

  Value::setMetadata(KindID, Node);
}

void Instruction::addAnnotationMetadata(SmallVector<StringRef> Annotations) {
  SmallVector<Metadata *, 4> Names;
  if (auto *Existing = getMetadata(LLVMContext::MD_annotation)) {
    SmallSetVector<StringRef, 2> AnnotationsSet(Annotations.begin(),
                                                Annotations.end());
    auto *Tuple = cast<MDTuple>(Existing);
    for (auto &N : Tuple->operands()) {
      if (isa<MDString>(N.get())) {
        Names.push_back(N);
        continue;
      }
      auto *MDAnnotationTuple = cast<MDTuple>(N);
      if (any_of(MDAnnotationTuple->operands(), [&AnnotationsSet](auto &Op) {
            return AnnotationsSet.contains(cast<MDString>(Op)->getString());
          }))
        return;
      Names.push_back(N);
    }
  }

  MDBuilder MDB(getContext());
  SmallVector<Metadata *> MDAnnotationStrings;
  for (StringRef Annotation : Annotations)
    MDAnnotationStrings.push_back(MDB.createString(Annotation));
  MDNode *InfoTuple = MDTuple::get(getContext(), MDAnnotationStrings);
  Names.push_back(InfoTuple);
  MDNode *MD = MDTuple::get(getContext(), Names);
  setMetadata(LLVMContext::MD_annotation, MD);
}

void Instruction::addAnnotationMetadata(StringRef Name) {
  SmallVector<Metadata *, 4> Names;
  if (auto *Existing = getMetadata(LLVMContext::MD_annotation)) {
    auto *Tuple = cast<MDTuple>(Existing);
    for (auto &N : Tuple->operands()) {
      if (isa<MDString>(N.get()) &&
          cast<MDString>(N.get())->getString() == Name)
        return;
      Names.push_back(N.get());
    }
  }

  MDBuilder MDB(getContext());
  Names.push_back(MDB.createString(Name));
  MDNode *MD = MDTuple::get(getContext(), Names);
  setMetadata(LLVMContext::MD_annotation, MD);
}

AAMDNodes Instruction::getAAMetadata() const {
  AAMDNodes Result;
  // Not using Instruction::hasMetadata() because we're not interested in
  // DebugInfoMetadata.
  if (Value::hasMetadata()) {
    const MDAttachments &Info = getContext().pImpl->ValueMetadata.at(this);
    Result.TBAA = Info.lookup(LLVMContext::MD_tbaa);
    Result.TBAAStruct = Info.lookup(LLVMContext::MD_tbaa_struct);
    Result.Scope = Info.lookup(LLVMContext::MD_alias_scope);
    Result.NoAlias = Info.lookup(LLVMContext::MD_noalias);
  }
  return Result;
}

void Instruction::setAAMetadata(const AAMDNodes &N) {
  setMetadata(LLVMContext::MD_tbaa, N.TBAA);
  setMetadata(LLVMContext::MD_tbaa_struct, N.TBAAStruct);
  setMetadata(LLVMContext::MD_alias_scope, N.Scope);
  setMetadata(LLVMContext::MD_noalias, N.NoAlias);
}

void Instruction::setNoSanitizeMetadata() {
  setMetadata(llvm::LLVMContext::MD_nosanitize,
              llvm::MDNode::get(getContext(), std::nullopt));
}

void Instruction::getAllMetadataImpl(
    SmallVectorImpl<std::pair<unsigned, MDNode *>> &Result) const {
  Result.clear();

  // Handle 'dbg' as a special case since it is not stored in the hash table.
  if (DbgLoc) {
    Result.push_back(
        std::make_pair((unsigned)LLVMContext::MD_dbg, DbgLoc.getAsMDNode()));
  }
  Value::getAllMetadata(Result);
}

bool Instruction::extractProfTotalWeight(uint64_t &TotalVal) const {
  assert(
      (getOpcode() == Instruction::Br || getOpcode() == Instruction::Select ||
       getOpcode() == Instruction::Call || getOpcode() == Instruction::Invoke ||
       getOpcode() == Instruction::IndirectBr ||
       getOpcode() == Instruction::Switch) &&
      "Looking for branch weights on something besides branch");

  return ::extractProfTotalWeight(*this, TotalVal);
}

void GlobalObject::copyMetadata(const GlobalObject *Other, unsigned Offset) {
  SmallVector<std::pair<unsigned, MDNode *>, 8> MDs;
  Other->getAllMetadata(MDs);
  for (auto &MD : MDs) {
    // We need to adjust the type metadata offset.
    if (Offset != 0 && MD.first == LLVMContext::MD_type) {
      auto *OffsetConst = cast<ConstantInt>(
          cast<ConstantAsMetadata>(MD.second->getOperand(0))->getValue());
      Metadata *TypeId = MD.second->getOperand(1);
      auto *NewOffsetMD = ConstantAsMetadata::get(ConstantInt::get(
          OffsetConst->getType(), OffsetConst->getValue() + Offset));
      addMetadata(LLVMContext::MD_type,
                  *MDNode::get(getContext(), {NewOffsetMD, TypeId}));
      continue;
    }
    // If an offset adjustment was specified we need to modify the DIExpression
    // to prepend the adjustment:
    // !DIExpression(DW_OP_plus, Offset, [original expr])
    auto *Attachment = MD.second;
    if (Offset != 0 && MD.first == LLVMContext::MD_dbg) {
      DIGlobalVariable *GV = dyn_cast<DIGlobalVariable>(Attachment);
      DIExpression *E = nullptr;
      if (!GV) {
        auto *GVE = cast<DIGlobalVariableExpression>(Attachment);
        GV = GVE->getVariable();
        E = GVE->getExpression();
      }
      ArrayRef<uint64_t> OrigElements;
      if (E)
        OrigElements = E->getElements();
      std::vector<uint64_t> Elements(OrigElements.size() + 2);
      Elements[0] = dwarf::DW_OP_plus_uconst;
      Elements[1] = Offset;
      llvm::copy(OrigElements, Elements.begin() + 2);
      E = DIExpression::get(getContext(), Elements);
      Attachment = DIGlobalVariableExpression::get(getContext(), GV, E);
    }
    addMetadata(MD.first, *Attachment);
  }
}

void GlobalObject::addTypeMetadata(unsigned Offset, Metadata *TypeID) {
  addMetadata(
      LLVMContext::MD_type,
      *MDTuple::get(getContext(),
                    {ConstantAsMetadata::get(ConstantInt::get(
                         Type::getInt64Ty(getContext()), Offset)),
                     TypeID}));
}

void GlobalObject::setVCallVisibilityMetadata(VCallVisibility Visibility) {
  // Remove any existing vcall visibility metadata first in case we are
  // updating.
  eraseMetadata(LLVMContext::MD_vcall_visibility);
  addMetadata(LLVMContext::MD_vcall_visibility,
              *MDNode::get(getContext(),
                           {ConstantAsMetadata::get(ConstantInt::get(
                               Type::getInt64Ty(getContext()), Visibility))}));
}

GlobalObject::VCallVisibility GlobalObject::getVCallVisibility() const {
  if (MDNode *MD = getMetadata(LLVMContext::MD_vcall_visibility)) {
    uint64_t Val = cast<ConstantInt>(
                       cast<ConstantAsMetadata>(MD->getOperand(0))->getValue())
                       ->getZExtValue();
    assert(Val <= 2 && "unknown vcall visibility!");
    return (VCallVisibility)Val;
  }
  return VCallVisibility::VCallVisibilityPublic;
}

void Function::setSubprogram(DISubprogram *SP) {
  setMetadata(LLVMContext::MD_dbg, SP);
}

DISubprogram *Function::getSubprogram() const {
  return cast_or_null<DISubprogram>(getMetadata(LLVMContext::MD_dbg));
}

bool Function::shouldEmitDebugInfoForProfiling() const {
  if (DISubprogram *SP = getSubprogram()) {
    if (DICompileUnit *CU = SP->getUnit()) {
      return CU->getDebugInfoForProfiling();
    }
  }
  return false;
}

void GlobalVariable::addDebugInfo(DIGlobalVariableExpression *GV) {
  addMetadata(LLVMContext::MD_dbg, *GV);
}

void GlobalVariable::getDebugInfo(
    SmallVectorImpl<DIGlobalVariableExpression *> &GVs) const {
  SmallVector<MDNode *, 1> MDs;
  getMetadata(LLVMContext::MD_dbg, MDs);
  for (MDNode *MD : MDs)
    GVs.push_back(cast<DIGlobalVariableExpression>(MD));
}
