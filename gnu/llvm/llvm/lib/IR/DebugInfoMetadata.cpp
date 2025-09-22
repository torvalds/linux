//===- DebugInfoMetadata.cpp - Implement debug info metadata --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the debug info Metadata classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/DebugInfoMetadata.h"
#include "LLVMContextImpl.h"
#include "MetadataImpl.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

#include <numeric>
#include <optional>

using namespace llvm;

namespace llvm {
// Use FS-AFDO discriminator.
cl::opt<bool> EnableFSDiscriminator(
    "enable-fs-discriminator", cl::Hidden,
    cl::desc("Enable adding flow sensitive discriminators"));
} // namespace llvm

uint32_t DIType::getAlignInBits() const {
  return (getTag() == dwarf::DW_TAG_LLVM_ptrauth_type ? 0 : SubclassData32);
}

const DIExpression::FragmentInfo DebugVariable::DefaultFragment = {
    std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::min()};

DebugVariable::DebugVariable(const DbgVariableIntrinsic *DII)
    : Variable(DII->getVariable()),
      Fragment(DII->getExpression()->getFragmentInfo()),
      InlinedAt(DII->getDebugLoc().getInlinedAt()) {}

DebugVariable::DebugVariable(const DbgVariableRecord *DVR)
    : Variable(DVR->getVariable()),
      Fragment(DVR->getExpression()->getFragmentInfo()),
      InlinedAt(DVR->getDebugLoc().getInlinedAt()) {}

DebugVariableAggregate::DebugVariableAggregate(const DbgVariableIntrinsic *DVI)
    : DebugVariable(DVI->getVariable(), std::nullopt,
                    DVI->getDebugLoc()->getInlinedAt()) {}

DILocation::DILocation(LLVMContext &C, StorageType Storage, unsigned Line,
                       unsigned Column, ArrayRef<Metadata *> MDs,
                       bool ImplicitCode)
    : MDNode(C, DILocationKind, Storage, MDs) {
  assert((MDs.size() == 1 || MDs.size() == 2) &&
         "Expected a scope and optional inlined-at");

  // Set line and column.
  assert(Column < (1u << 16) && "Expected 16-bit column");

  SubclassData32 = Line;
  SubclassData16 = Column;

  setImplicitCode(ImplicitCode);
}

static void adjustColumn(unsigned &Column) {
  // Set to unknown on overflow.  We only have 16 bits to play with here.
  if (Column >= (1u << 16))
    Column = 0;
}

DILocation *DILocation::getImpl(LLVMContext &Context, unsigned Line,
                                unsigned Column, Metadata *Scope,
                                Metadata *InlinedAt, bool ImplicitCode,
                                StorageType Storage, bool ShouldCreate) {
  // Fixup column.
  adjustColumn(Column);

  if (Storage == Uniqued) {
    if (auto *N = getUniqued(Context.pImpl->DILocations,
                             DILocationInfo::KeyTy(Line, Column, Scope,
                                                   InlinedAt, ImplicitCode)))
      return N;
    if (!ShouldCreate)
      return nullptr;
  } else {
    assert(ShouldCreate && "Expected non-uniqued nodes to always be created");
  }

  SmallVector<Metadata *, 2> Ops;
  Ops.push_back(Scope);
  if (InlinedAt)
    Ops.push_back(InlinedAt);
  return storeImpl(new (Ops.size(), Storage) DILocation(
                       Context, Storage, Line, Column, Ops, ImplicitCode),
                   Storage, Context.pImpl->DILocations);
}

DILocation *DILocation::getMergedLocations(ArrayRef<DILocation *> Locs) {
  if (Locs.empty())
    return nullptr;
  if (Locs.size() == 1)
    return Locs[0];
  auto *Merged = Locs[0];
  for (DILocation *L : llvm::drop_begin(Locs)) {
    Merged = getMergedLocation(Merged, L);
    if (Merged == nullptr)
      break;
  }
  return Merged;
}

DILocation *DILocation::getMergedLocation(DILocation *LocA, DILocation *LocB) {
  if (!LocA || !LocB)
    return nullptr;

  if (LocA == LocB)
    return LocA;

  LLVMContext &C = LocA->getContext();

  using LocVec = SmallVector<const DILocation *>;
  LocVec ALocs;
  LocVec BLocs;
  SmallDenseMap<std::pair<const DISubprogram *, const DILocation *>, unsigned,
                4>
      ALookup;

  // Walk through LocA and its inlined-at locations, populate them in ALocs and
  // save the index for the subprogram and inlined-at pair, which we use to find
  // a matching starting location in LocB's chain.
  for (auto [L, I] = std::make_pair(LocA, 0U); L; L = L->getInlinedAt(), I++) {
    ALocs.push_back(L);
    auto Res = ALookup.try_emplace(
        {L->getScope()->getSubprogram(), L->getInlinedAt()}, I);
    assert(Res.second && "Multiple <SP, InlinedAt> pairs in a location chain?");
    (void)Res;
  }

  LocVec::reverse_iterator ARIt = ALocs.rend();
  LocVec::reverse_iterator BRIt = BLocs.rend();

  // Populate BLocs and look for a matching starting location, the first
  // location with the same subprogram and inlined-at location as in LocA's
  // chain. Since the two locations have the same inlined-at location we do
  // not need to look at those parts of the chains.
  for (auto [L, I] = std::make_pair(LocB, 0U); L; L = L->getInlinedAt(), I++) {
    BLocs.push_back(L);

    if (ARIt != ALocs.rend())
      // We have already found a matching starting location.
      continue;

    auto IT = ALookup.find({L->getScope()->getSubprogram(), L->getInlinedAt()});
    if (IT == ALookup.end())
      continue;

    // The + 1 is to account for the &*rev_it = &(it - 1) relationship.
    ARIt = LocVec::reverse_iterator(ALocs.begin() + IT->second + 1);
    BRIt = LocVec::reverse_iterator(BLocs.begin() + I + 1);

    // If we have found a matching starting location we do not need to add more
    // locations to BLocs, since we will only look at location pairs preceding
    // the matching starting location, and adding more elements to BLocs could
    // invalidate the iterator that we initialized here.
    break;
  }

  // Merge the two locations if possible, using the supplied
  // inlined-at location for the created location.
  auto MergeLocPair = [&C](const DILocation *L1, const DILocation *L2,
                           DILocation *InlinedAt) -> DILocation * {
    if (L1 == L2)
      return DILocation::get(C, L1->getLine(), L1->getColumn(), L1->getScope(),
                             InlinedAt);

    // If the locations originate from different subprograms we can't produce
    // a common location.
    if (L1->getScope()->getSubprogram() != L2->getScope()->getSubprogram())
      return nullptr;

    // Return the nearest common scope inside a subprogram.
    auto GetNearestCommonScope = [](DIScope *S1, DIScope *S2) -> DIScope * {
      SmallPtrSet<DIScope *, 8> Scopes;
      for (; S1; S1 = S1->getScope()) {
        Scopes.insert(S1);
        if (isa<DISubprogram>(S1))
          break;
      }

      for (; S2; S2 = S2->getScope()) {
        if (Scopes.count(S2))
          return S2;
        if (isa<DISubprogram>(S2))
          break;
      }

      return nullptr;
    };

    auto Scope = GetNearestCommonScope(L1->getScope(), L2->getScope());
    assert(Scope && "No common scope in the same subprogram?");

    bool SameLine = L1->getLine() == L2->getLine();
    bool SameCol = L1->getColumn() == L2->getColumn();
    unsigned Line = SameLine ? L1->getLine() : 0;
    unsigned Col = SameLine && SameCol ? L1->getColumn() : 0;

    return DILocation::get(C, Line, Col, Scope, InlinedAt);
  };

  DILocation *Result = ARIt != ALocs.rend() ? (*ARIt)->getInlinedAt() : nullptr;

  // If we have found a common starting location, walk up the inlined-at chains
  // and try to produce common locations.
  for (; ARIt != ALocs.rend() && BRIt != BLocs.rend(); ++ARIt, ++BRIt) {
    DILocation *Tmp = MergeLocPair(*ARIt, *BRIt, Result);

    if (!Tmp)
      // We have walked up to a point in the chains where the two locations
      // are irreconsilable. At this point Result contains the nearest common
      // location in the inlined-at chains of LocA and LocB, so we break here.
      break;

    Result = Tmp;
  }

  if (Result)
    return Result;

  // We ended up with LocA and LocB as irreconsilable locations. Produce a
  // location at 0:0 with one of the locations' scope. The function has
  // historically picked A's scope, and a nullptr inlined-at location, so that
  // behavior is mimicked here but I am not sure if this is always the correct
  // way to handle this.
  return DILocation::get(C, 0, 0, LocA->getScope(), nullptr);
}

std::optional<unsigned>
DILocation::encodeDiscriminator(unsigned BD, unsigned DF, unsigned CI) {
  std::array<unsigned, 3> Components = {BD, DF, CI};
  uint64_t RemainingWork = 0U;
  // We use RemainingWork to figure out if we have no remaining components to
  // encode. For example: if BD != 0 but DF == 0 && CI == 0, we don't need to
  // encode anything for the latter 2.
  // Since any of the input components is at most 32 bits, their sum will be
  // less than 34 bits, and thus RemainingWork won't overflow.
  RemainingWork =
      std::accumulate(Components.begin(), Components.end(), RemainingWork);

  int I = 0;
  unsigned Ret = 0;
  unsigned NextBitInsertionIndex = 0;
  while (RemainingWork > 0) {
    unsigned C = Components[I++];
    RemainingWork -= C;
    unsigned EC = encodeComponent(C);
    Ret |= (EC << NextBitInsertionIndex);
    NextBitInsertionIndex += encodingBits(C);
  }

  // Encoding may be unsuccessful because of overflow. We determine success by
  // checking equivalence of components before & after encoding. Alternatively,
  // we could determine Success during encoding, but the current alternative is
  // simpler.
  unsigned TBD, TDF, TCI = 0;
  decodeDiscriminator(Ret, TBD, TDF, TCI);
  if (TBD == BD && TDF == DF && TCI == CI)
    return Ret;
  return std::nullopt;
}

void DILocation::decodeDiscriminator(unsigned D, unsigned &BD, unsigned &DF,
                                     unsigned &CI) {
  BD = getUnsignedFromPrefixEncoding(D);
  DF = getUnsignedFromPrefixEncoding(getNextComponentInDiscriminator(D));
  CI = getUnsignedFromPrefixEncoding(
      getNextComponentInDiscriminator(getNextComponentInDiscriminator(D)));
}
dwarf::Tag DINode::getTag() const { return (dwarf::Tag)SubclassData16; }

DINode::DIFlags DINode::getFlag(StringRef Flag) {
  return StringSwitch<DIFlags>(Flag)
#define HANDLE_DI_FLAG(ID, NAME) .Case("DIFlag" #NAME, Flag##NAME)
#include "llvm/IR/DebugInfoFlags.def"
      .Default(DINode::FlagZero);
}

StringRef DINode::getFlagString(DIFlags Flag) {
  switch (Flag) {
#define HANDLE_DI_FLAG(ID, NAME)                                               \
  case Flag##NAME:                                                             \
    return "DIFlag" #NAME;
#include "llvm/IR/DebugInfoFlags.def"
  }
  return "";
}

DINode::DIFlags DINode::splitFlags(DIFlags Flags,
                                   SmallVectorImpl<DIFlags> &SplitFlags) {
  // Flags that are packed together need to be specially handled, so
  // that, for example, we emit "DIFlagPublic" and not
  // "DIFlagPrivate | DIFlagProtected".
  if (DIFlags A = Flags & FlagAccessibility) {
    if (A == FlagPrivate)
      SplitFlags.push_back(FlagPrivate);
    else if (A == FlagProtected)
      SplitFlags.push_back(FlagProtected);
    else
      SplitFlags.push_back(FlagPublic);
    Flags &= ~A;
  }
  if (DIFlags R = Flags & FlagPtrToMemberRep) {
    if (R == FlagSingleInheritance)
      SplitFlags.push_back(FlagSingleInheritance);
    else if (R == FlagMultipleInheritance)
      SplitFlags.push_back(FlagMultipleInheritance);
    else
      SplitFlags.push_back(FlagVirtualInheritance);
    Flags &= ~R;
  }
  if ((Flags & FlagIndirectVirtualBase) == FlagIndirectVirtualBase) {
    Flags &= ~FlagIndirectVirtualBase;
    SplitFlags.push_back(FlagIndirectVirtualBase);
  }

#define HANDLE_DI_FLAG(ID, NAME)                                               \
  if (DIFlags Bit = Flags & Flag##NAME) {                                      \
    SplitFlags.push_back(Bit);                                                 \
    Flags &= ~Bit;                                                             \
  }
#include "llvm/IR/DebugInfoFlags.def"
  return Flags;
}

DIScope *DIScope::getScope() const {
  if (auto *T = dyn_cast<DIType>(this))
    return T->getScope();

  if (auto *SP = dyn_cast<DISubprogram>(this))
    return SP->getScope();

  if (auto *LB = dyn_cast<DILexicalBlockBase>(this))
    return LB->getScope();

  if (auto *NS = dyn_cast<DINamespace>(this))
    return NS->getScope();

  if (auto *CB = dyn_cast<DICommonBlock>(this))
    return CB->getScope();

  if (auto *M = dyn_cast<DIModule>(this))
    return M->getScope();

  assert((isa<DIFile>(this) || isa<DICompileUnit>(this)) &&
         "Unhandled type of scope.");
  return nullptr;
}

StringRef DIScope::getName() const {
  if (auto *T = dyn_cast<DIType>(this))
    return T->getName();
  if (auto *SP = dyn_cast<DISubprogram>(this))
    return SP->getName();
  if (auto *NS = dyn_cast<DINamespace>(this))
    return NS->getName();
  if (auto *CB = dyn_cast<DICommonBlock>(this))
    return CB->getName();
  if (auto *M = dyn_cast<DIModule>(this))
    return M->getName();
  assert((isa<DILexicalBlockBase>(this) || isa<DIFile>(this) ||
          isa<DICompileUnit>(this)) &&
         "Unhandled type of scope.");
  return "";
}

#ifndef NDEBUG
static bool isCanonical(const MDString *S) {
  return !S || !S->getString().empty();
}
#endif

dwarf::Tag GenericDINode::getTag() const { return (dwarf::Tag)SubclassData16; }
GenericDINode *GenericDINode::getImpl(LLVMContext &Context, unsigned Tag,
                                      MDString *Header,
                                      ArrayRef<Metadata *> DwarfOps,
                                      StorageType Storage, bool ShouldCreate) {
  unsigned Hash = 0;
  if (Storage == Uniqued) {
    GenericDINodeInfo::KeyTy Key(Tag, Header, DwarfOps);
    if (auto *N = getUniqued(Context.pImpl->GenericDINodes, Key))
      return N;
    if (!ShouldCreate)
      return nullptr;
    Hash = Key.getHash();
  } else {
    assert(ShouldCreate && "Expected non-uniqued nodes to always be created");
  }

  // Use a nullptr for empty headers.
  assert(isCanonical(Header) && "Expected canonical MDString");
  Metadata *PreOps[] = {Header};
  return storeImpl(new (DwarfOps.size() + 1, Storage) GenericDINode(
                       Context, Storage, Hash, Tag, PreOps, DwarfOps),
                   Storage, Context.pImpl->GenericDINodes);
}

void GenericDINode::recalculateHash() {
  setHash(GenericDINodeInfo::KeyTy::calculateHash(this));
}

#define UNWRAP_ARGS_IMPL(...) __VA_ARGS__
#define UNWRAP_ARGS(ARGS) UNWRAP_ARGS_IMPL ARGS
#define DEFINE_GETIMPL_LOOKUP(CLASS, ARGS)                                     \
  do {                                                                         \
    if (Storage == Uniqued) {                                                  \
      if (auto *N = getUniqued(Context.pImpl->CLASS##s,                        \
                               CLASS##Info::KeyTy(UNWRAP_ARGS(ARGS))))         \
        return N;                                                              \
      if (!ShouldCreate)                                                       \
        return nullptr;                                                        \
    } else {                                                                   \
      assert(ShouldCreate &&                                                   \
             "Expected non-uniqued nodes to always be created");               \
    }                                                                          \
  } while (false)
#define DEFINE_GETIMPL_STORE(CLASS, ARGS, OPS)                                 \
  return storeImpl(new (std::size(OPS), Storage)                               \
                       CLASS(Context, Storage, UNWRAP_ARGS(ARGS), OPS),        \
                   Storage, Context.pImpl->CLASS##s)
#define DEFINE_GETIMPL_STORE_NO_OPS(CLASS, ARGS)                               \
  return storeImpl(new (0u, Storage)                                           \
                       CLASS(Context, Storage, UNWRAP_ARGS(ARGS)),             \
                   Storage, Context.pImpl->CLASS##s)
#define DEFINE_GETIMPL_STORE_NO_CONSTRUCTOR_ARGS(CLASS, OPS)                   \
  return storeImpl(new (std::size(OPS), Storage) CLASS(Context, Storage, OPS), \
                   Storage, Context.pImpl->CLASS##s)
#define DEFINE_GETIMPL_STORE_N(CLASS, ARGS, OPS, NUM_OPS)                      \
  return storeImpl(new (NUM_OPS, Storage)                                      \
                       CLASS(Context, Storage, UNWRAP_ARGS(ARGS), OPS),        \
                   Storage, Context.pImpl->CLASS##s)

DISubrange::DISubrange(LLVMContext &C, StorageType Storage,
                       ArrayRef<Metadata *> Ops)
    : DINode(C, DISubrangeKind, Storage, dwarf::DW_TAG_subrange_type, Ops) {}
DISubrange *DISubrange::getImpl(LLVMContext &Context, int64_t Count, int64_t Lo,
                                StorageType Storage, bool ShouldCreate) {
  auto *CountNode = ConstantAsMetadata::get(
      ConstantInt::getSigned(Type::getInt64Ty(Context), Count));
  auto *LB = ConstantAsMetadata::get(
      ConstantInt::getSigned(Type::getInt64Ty(Context), Lo));
  return getImpl(Context, CountNode, LB, nullptr, nullptr, Storage,
                 ShouldCreate);
}

DISubrange *DISubrange::getImpl(LLVMContext &Context, Metadata *CountNode,
                                int64_t Lo, StorageType Storage,
                                bool ShouldCreate) {
  auto *LB = ConstantAsMetadata::get(
      ConstantInt::getSigned(Type::getInt64Ty(Context), Lo));
  return getImpl(Context, CountNode, LB, nullptr, nullptr, Storage,
                 ShouldCreate);
}

DISubrange *DISubrange::getImpl(LLVMContext &Context, Metadata *CountNode,
                                Metadata *LB, Metadata *UB, Metadata *Stride,
                                StorageType Storage, bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DISubrange, (CountNode, LB, UB, Stride));
  Metadata *Ops[] = {CountNode, LB, UB, Stride};
  DEFINE_GETIMPL_STORE_NO_CONSTRUCTOR_ARGS(DISubrange, Ops);
}

DISubrange::BoundType DISubrange::getCount() const {
  Metadata *CB = getRawCountNode();
  if (!CB)
    return BoundType();

  assert((isa<ConstantAsMetadata>(CB) || isa<DIVariable>(CB) ||
          isa<DIExpression>(CB)) &&
         "Count must be signed constant or DIVariable or DIExpression");

  if (auto *MD = dyn_cast<ConstantAsMetadata>(CB))
    return BoundType(cast<ConstantInt>(MD->getValue()));

  if (auto *MD = dyn_cast<DIVariable>(CB))
    return BoundType(MD);

  if (auto *MD = dyn_cast<DIExpression>(CB))
    return BoundType(MD);

  return BoundType();
}

DISubrange::BoundType DISubrange::getLowerBound() const {
  Metadata *LB = getRawLowerBound();
  if (!LB)
    return BoundType();

  assert((isa<ConstantAsMetadata>(LB) || isa<DIVariable>(LB) ||
          isa<DIExpression>(LB)) &&
         "LowerBound must be signed constant or DIVariable or DIExpression");

  if (auto *MD = dyn_cast<ConstantAsMetadata>(LB))
    return BoundType(cast<ConstantInt>(MD->getValue()));

  if (auto *MD = dyn_cast<DIVariable>(LB))
    return BoundType(MD);

  if (auto *MD = dyn_cast<DIExpression>(LB))
    return BoundType(MD);

  return BoundType();
}

DISubrange::BoundType DISubrange::getUpperBound() const {
  Metadata *UB = getRawUpperBound();
  if (!UB)
    return BoundType();

  assert((isa<ConstantAsMetadata>(UB) || isa<DIVariable>(UB) ||
          isa<DIExpression>(UB)) &&
         "UpperBound must be signed constant or DIVariable or DIExpression");

  if (auto *MD = dyn_cast<ConstantAsMetadata>(UB))
    return BoundType(cast<ConstantInt>(MD->getValue()));

  if (auto *MD = dyn_cast<DIVariable>(UB))
    return BoundType(MD);

  if (auto *MD = dyn_cast<DIExpression>(UB))
    return BoundType(MD);

  return BoundType();
}

DISubrange::BoundType DISubrange::getStride() const {
  Metadata *ST = getRawStride();
  if (!ST)
    return BoundType();

  assert((isa<ConstantAsMetadata>(ST) || isa<DIVariable>(ST) ||
          isa<DIExpression>(ST)) &&
         "Stride must be signed constant or DIVariable or DIExpression");

  if (auto *MD = dyn_cast<ConstantAsMetadata>(ST))
    return BoundType(cast<ConstantInt>(MD->getValue()));

  if (auto *MD = dyn_cast<DIVariable>(ST))
    return BoundType(MD);

  if (auto *MD = dyn_cast<DIExpression>(ST))
    return BoundType(MD);

  return BoundType();
}
DIGenericSubrange::DIGenericSubrange(LLVMContext &C, StorageType Storage,
                                     ArrayRef<Metadata *> Ops)
    : DINode(C, DIGenericSubrangeKind, Storage, dwarf::DW_TAG_generic_subrange,
             Ops) {}

DIGenericSubrange *DIGenericSubrange::getImpl(LLVMContext &Context,
                                              Metadata *CountNode, Metadata *LB,
                                              Metadata *UB, Metadata *Stride,
                                              StorageType Storage,
                                              bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DIGenericSubrange, (CountNode, LB, UB, Stride));
  Metadata *Ops[] = {CountNode, LB, UB, Stride};
  DEFINE_GETIMPL_STORE_NO_CONSTRUCTOR_ARGS(DIGenericSubrange, Ops);
}

DIGenericSubrange::BoundType DIGenericSubrange::getCount() const {
  Metadata *CB = getRawCountNode();
  if (!CB)
    return BoundType();

  assert((isa<DIVariable>(CB) || isa<DIExpression>(CB)) &&
         "Count must be signed constant or DIVariable or DIExpression");

  if (auto *MD = dyn_cast<DIVariable>(CB))
    return BoundType(MD);

  if (auto *MD = dyn_cast<DIExpression>(CB))
    return BoundType(MD);

  return BoundType();
}

DIGenericSubrange::BoundType DIGenericSubrange::getLowerBound() const {
  Metadata *LB = getRawLowerBound();
  if (!LB)
    return BoundType();

  assert((isa<DIVariable>(LB) || isa<DIExpression>(LB)) &&
         "LowerBound must be signed constant or DIVariable or DIExpression");

  if (auto *MD = dyn_cast<DIVariable>(LB))
    return BoundType(MD);

  if (auto *MD = dyn_cast<DIExpression>(LB))
    return BoundType(MD);

  return BoundType();
}

DIGenericSubrange::BoundType DIGenericSubrange::getUpperBound() const {
  Metadata *UB = getRawUpperBound();
  if (!UB)
    return BoundType();

  assert((isa<DIVariable>(UB) || isa<DIExpression>(UB)) &&
         "UpperBound must be signed constant or DIVariable or DIExpression");

  if (auto *MD = dyn_cast<DIVariable>(UB))
    return BoundType(MD);

  if (auto *MD = dyn_cast<DIExpression>(UB))
    return BoundType(MD);

  return BoundType();
}

DIGenericSubrange::BoundType DIGenericSubrange::getStride() const {
  Metadata *ST = getRawStride();
  if (!ST)
    return BoundType();

  assert((isa<DIVariable>(ST) || isa<DIExpression>(ST)) &&
         "Stride must be signed constant or DIVariable or DIExpression");

  if (auto *MD = dyn_cast<DIVariable>(ST))
    return BoundType(MD);

  if (auto *MD = dyn_cast<DIExpression>(ST))
    return BoundType(MD);

  return BoundType();
}

DIEnumerator::DIEnumerator(LLVMContext &C, StorageType Storage,
                           const APInt &Value, bool IsUnsigned,
                           ArrayRef<Metadata *> Ops)
    : DINode(C, DIEnumeratorKind, Storage, dwarf::DW_TAG_enumerator, Ops),
      Value(Value) {
  SubclassData32 = IsUnsigned;
}
DIEnumerator *DIEnumerator::getImpl(LLVMContext &Context, const APInt &Value,
                                    bool IsUnsigned, MDString *Name,
                                    StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIEnumerator, (Value, IsUnsigned, Name));
  Metadata *Ops[] = {Name};
  DEFINE_GETIMPL_STORE(DIEnumerator, (Value, IsUnsigned), Ops);
}

DIBasicType *DIBasicType::getImpl(LLVMContext &Context, unsigned Tag,
                                  MDString *Name, uint64_t SizeInBits,
                                  uint32_t AlignInBits, unsigned Encoding,
                                  DIFlags Flags, StorageType Storage,
                                  bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIBasicType,
                        (Tag, Name, SizeInBits, AlignInBits, Encoding, Flags));
  Metadata *Ops[] = {nullptr, nullptr, Name};
  DEFINE_GETIMPL_STORE(DIBasicType,
                       (Tag, SizeInBits, AlignInBits, Encoding, Flags), Ops);
}

std::optional<DIBasicType::Signedness> DIBasicType::getSignedness() const {
  switch (getEncoding()) {
  case dwarf::DW_ATE_signed:
  case dwarf::DW_ATE_signed_char:
    return Signedness::Signed;
  case dwarf::DW_ATE_unsigned:
  case dwarf::DW_ATE_unsigned_char:
    return Signedness::Unsigned;
  default:
    return std::nullopt;
  }
}

DIStringType *DIStringType::getImpl(LLVMContext &Context, unsigned Tag,
                                    MDString *Name, Metadata *StringLength,
                                    Metadata *StringLengthExp,
                                    Metadata *StringLocationExp,
                                    uint64_t SizeInBits, uint32_t AlignInBits,
                                    unsigned Encoding, StorageType Storage,
                                    bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIStringType,
                        (Tag, Name, StringLength, StringLengthExp,
                         StringLocationExp, SizeInBits, AlignInBits, Encoding));
  Metadata *Ops[] = {nullptr,      nullptr,         Name,
                     StringLength, StringLengthExp, StringLocationExp};
  DEFINE_GETIMPL_STORE(DIStringType, (Tag, SizeInBits, AlignInBits, Encoding),
                       Ops);
}
DIType *DIDerivedType::getClassType() const {
  assert(getTag() == dwarf::DW_TAG_ptr_to_member_type);
  return cast_or_null<DIType>(getExtraData());
}
uint32_t DIDerivedType::getVBPtrOffset() const {
  assert(getTag() == dwarf::DW_TAG_inheritance);
  if (auto *CM = cast_or_null<ConstantAsMetadata>(getExtraData()))
    if (auto *CI = dyn_cast_or_null<ConstantInt>(CM->getValue()))
      return static_cast<uint32_t>(CI->getZExtValue());
  return 0;
}
Constant *DIDerivedType::getStorageOffsetInBits() const {
  assert(getTag() == dwarf::DW_TAG_member && isBitField());
  if (auto *C = cast_or_null<ConstantAsMetadata>(getExtraData()))
    return C->getValue();
  return nullptr;
}

Constant *DIDerivedType::getConstant() const {
  assert((getTag() == dwarf::DW_TAG_member ||
          getTag() == dwarf::DW_TAG_variable) &&
         isStaticMember());
  if (auto *C = cast_or_null<ConstantAsMetadata>(getExtraData()))
    return C->getValue();
  return nullptr;
}
Constant *DIDerivedType::getDiscriminantValue() const {
  assert(getTag() == dwarf::DW_TAG_member && !isStaticMember());
  if (auto *C = cast_or_null<ConstantAsMetadata>(getExtraData()))
    return C->getValue();
  return nullptr;
}

DIDerivedType *DIDerivedType::getImpl(
    LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
    unsigned Line, Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
    uint32_t AlignInBits, uint64_t OffsetInBits,
    std::optional<unsigned> DWARFAddressSpace,
    std::optional<PtrAuthData> PtrAuthData, DIFlags Flags, Metadata *ExtraData,
    Metadata *Annotations, StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIDerivedType,
                        (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                         AlignInBits, OffsetInBits, DWARFAddressSpace,
                         PtrAuthData, Flags, ExtraData, Annotations));
  Metadata *Ops[] = {File, Scope, Name, BaseType, ExtraData, Annotations};
  DEFINE_GETIMPL_STORE(DIDerivedType,
                       (Tag, Line, SizeInBits, AlignInBits, OffsetInBits,
                        DWARFAddressSpace, PtrAuthData, Flags),
                       Ops);
}

std::optional<DIDerivedType::PtrAuthData>
DIDerivedType::getPtrAuthData() const {
  return getTag() == dwarf::DW_TAG_LLVM_ptrauth_type
             ? std::optional<PtrAuthData>(PtrAuthData(SubclassData32))
             : std::nullopt;
}

DICompositeType *DICompositeType::getImpl(
    LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *File,
    unsigned Line, Metadata *Scope, Metadata *BaseType, uint64_t SizeInBits,
    uint32_t AlignInBits, uint64_t OffsetInBits, DIFlags Flags,
    Metadata *Elements, unsigned RuntimeLang, Metadata *VTableHolder,
    Metadata *TemplateParams, MDString *Identifier, Metadata *Discriminator,
    Metadata *DataLocation, Metadata *Associated, Metadata *Allocated,
    Metadata *Rank, Metadata *Annotations, StorageType Storage,
    bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");

  // Keep this in sync with buildODRType.
  DEFINE_GETIMPL_LOOKUP(DICompositeType,
                        (Tag, Name, File, Line, Scope, BaseType, SizeInBits,
                         AlignInBits, OffsetInBits, Flags, Elements,
                         RuntimeLang, VTableHolder, TemplateParams, Identifier,
                         Discriminator, DataLocation, Associated, Allocated,
                         Rank, Annotations));
  Metadata *Ops[] = {File,          Scope,        Name,           BaseType,
                     Elements,      VTableHolder, TemplateParams, Identifier,
                     Discriminator, DataLocation, Associated,     Allocated,
                     Rank,          Annotations};
  DEFINE_GETIMPL_STORE(
      DICompositeType,
      (Tag, Line, RuntimeLang, SizeInBits, AlignInBits, OffsetInBits, Flags),
      Ops);
}

DICompositeType *DICompositeType::buildODRType(
    LLVMContext &Context, MDString &Identifier, unsigned Tag, MDString *Name,
    Metadata *File, unsigned Line, Metadata *Scope, Metadata *BaseType,
    uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
    DIFlags Flags, Metadata *Elements, unsigned RuntimeLang,
    Metadata *VTableHolder, Metadata *TemplateParams, Metadata *Discriminator,
    Metadata *DataLocation, Metadata *Associated, Metadata *Allocated,
    Metadata *Rank, Metadata *Annotations) {
  assert(!Identifier.getString().empty() && "Expected valid identifier");
  if (!Context.isODRUniquingDebugTypes())
    return nullptr;
  auto *&CT = (*Context.pImpl->DITypeMap)[&Identifier];
  if (!CT)
    return CT = DICompositeType::getDistinct(
               Context, Tag, Name, File, Line, Scope, BaseType, SizeInBits,
               AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang,
               VTableHolder, TemplateParams, &Identifier, Discriminator,
               DataLocation, Associated, Allocated, Rank, Annotations);

  if (CT->getTag() != Tag)
    return nullptr;

  // Only mutate CT if it's a forward declaration and the new operands aren't.
  assert(CT->getRawIdentifier() == &Identifier && "Wrong ODR identifier?");
  if (!CT->isForwardDecl() || (Flags & DINode::FlagFwdDecl))
    return CT;

  // Mutate CT in place.  Keep this in sync with getImpl.
  CT->mutate(Tag, Line, RuntimeLang, SizeInBits, AlignInBits, OffsetInBits,
             Flags);
  Metadata *Ops[] = {File,          Scope,        Name,           BaseType,
                     Elements,      VTableHolder, TemplateParams, &Identifier,
                     Discriminator, DataLocation, Associated,     Allocated,
                     Rank,          Annotations};
  assert((std::end(Ops) - std::begin(Ops)) == (int)CT->getNumOperands() &&
         "Mismatched number of operands");
  for (unsigned I = 0, E = CT->getNumOperands(); I != E; ++I)
    if (Ops[I] != CT->getOperand(I))
      CT->setOperand(I, Ops[I]);
  return CT;
}

DICompositeType *DICompositeType::getODRType(
    LLVMContext &Context, MDString &Identifier, unsigned Tag, MDString *Name,
    Metadata *File, unsigned Line, Metadata *Scope, Metadata *BaseType,
    uint64_t SizeInBits, uint32_t AlignInBits, uint64_t OffsetInBits,
    DIFlags Flags, Metadata *Elements, unsigned RuntimeLang,
    Metadata *VTableHolder, Metadata *TemplateParams, Metadata *Discriminator,
    Metadata *DataLocation, Metadata *Associated, Metadata *Allocated,
    Metadata *Rank, Metadata *Annotations) {
  assert(!Identifier.getString().empty() && "Expected valid identifier");
  if (!Context.isODRUniquingDebugTypes())
    return nullptr;
  auto *&CT = (*Context.pImpl->DITypeMap)[&Identifier];
  if (!CT) {
    CT = DICompositeType::getDistinct(
        Context, Tag, Name, File, Line, Scope, BaseType, SizeInBits,
        AlignInBits, OffsetInBits, Flags, Elements, RuntimeLang, VTableHolder,
        TemplateParams, &Identifier, Discriminator, DataLocation, Associated,
        Allocated, Rank, Annotations);
  } else {
    if (CT->getTag() != Tag)
      return nullptr;
  }
  return CT;
}

DICompositeType *DICompositeType::getODRTypeIfExists(LLVMContext &Context,
                                                     MDString &Identifier) {
  assert(!Identifier.getString().empty() && "Expected valid identifier");
  if (!Context.isODRUniquingDebugTypes())
    return nullptr;
  return Context.pImpl->DITypeMap->lookup(&Identifier);
}
DISubroutineType::DISubroutineType(LLVMContext &C, StorageType Storage,
                                   DIFlags Flags, uint8_t CC,
                                   ArrayRef<Metadata *> Ops)
    : DIType(C, DISubroutineTypeKind, Storage, dwarf::DW_TAG_subroutine_type, 0,
             0, 0, 0, Flags, Ops),
      CC(CC) {}

DISubroutineType *DISubroutineType::getImpl(LLVMContext &Context, DIFlags Flags,
                                            uint8_t CC, Metadata *TypeArray,
                                            StorageType Storage,
                                            bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DISubroutineType, (Flags, CC, TypeArray));
  Metadata *Ops[] = {nullptr, nullptr, nullptr, TypeArray};
  DEFINE_GETIMPL_STORE(DISubroutineType, (Flags, CC), Ops);
}

DIFile::DIFile(LLVMContext &C, StorageType Storage,
               std::optional<ChecksumInfo<MDString *>> CS, MDString *Src,
               ArrayRef<Metadata *> Ops)
    : DIScope(C, DIFileKind, Storage, dwarf::DW_TAG_file_type, Ops),
      Checksum(CS), Source(Src) {}

// FIXME: Implement this string-enum correspondence with a .def file and macros,
// so that the association is explicit rather than implied.
static const char *ChecksumKindName[DIFile::CSK_Last] = {
    "CSK_MD5",
    "CSK_SHA1",
    "CSK_SHA256",
};

StringRef DIFile::getChecksumKindAsString(ChecksumKind CSKind) {
  assert(CSKind <= DIFile::CSK_Last && "Invalid checksum kind");
  // The first space was originally the CSK_None variant, which is now
  // obsolete, but the space is still reserved in ChecksumKind, so we account
  // for it here.
  return ChecksumKindName[CSKind - 1];
}

std::optional<DIFile::ChecksumKind>
DIFile::getChecksumKind(StringRef CSKindStr) {
  return StringSwitch<std::optional<DIFile::ChecksumKind>>(CSKindStr)
      .Case("CSK_MD5", DIFile::CSK_MD5)
      .Case("CSK_SHA1", DIFile::CSK_SHA1)
      .Case("CSK_SHA256", DIFile::CSK_SHA256)
      .Default(std::nullopt);
}

DIFile *DIFile::getImpl(LLVMContext &Context, MDString *Filename,
                        MDString *Directory,
                        std::optional<DIFile::ChecksumInfo<MDString *>> CS,
                        MDString *Source, StorageType Storage,
                        bool ShouldCreate) {
  assert(isCanonical(Filename) && "Expected canonical MDString");
  assert(isCanonical(Directory) && "Expected canonical MDString");
  assert((!CS || isCanonical(CS->Value)) && "Expected canonical MDString");
  // We do *NOT* expect Source to be a canonical MDString because nullptr
  // means none, so we need something to represent the empty file.
  DEFINE_GETIMPL_LOOKUP(DIFile, (Filename, Directory, CS, Source));
  Metadata *Ops[] = {Filename, Directory, CS ? CS->Value : nullptr, Source};
  DEFINE_GETIMPL_STORE(DIFile, (CS, Source), Ops);
}
DICompileUnit::DICompileUnit(LLVMContext &C, StorageType Storage,
                             unsigned SourceLanguage, bool IsOptimized,
                             unsigned RuntimeVersion, unsigned EmissionKind,
                             uint64_t DWOId, bool SplitDebugInlining,
                             bool DebugInfoForProfiling, unsigned NameTableKind,
                             bool RangesBaseAddress, ArrayRef<Metadata *> Ops)
    : DIScope(C, DICompileUnitKind, Storage, dwarf::DW_TAG_compile_unit, Ops),
      SourceLanguage(SourceLanguage), RuntimeVersion(RuntimeVersion),
      DWOId(DWOId), EmissionKind(EmissionKind), NameTableKind(NameTableKind),
      IsOptimized(IsOptimized), SplitDebugInlining(SplitDebugInlining),
      DebugInfoForProfiling(DebugInfoForProfiling),
      RangesBaseAddress(RangesBaseAddress) {
  assert(Storage != Uniqued);
}

DICompileUnit *DICompileUnit::getImpl(
    LLVMContext &Context, unsigned SourceLanguage, Metadata *File,
    MDString *Producer, bool IsOptimized, MDString *Flags,
    unsigned RuntimeVersion, MDString *SplitDebugFilename,
    unsigned EmissionKind, Metadata *EnumTypes, Metadata *RetainedTypes,
    Metadata *GlobalVariables, Metadata *ImportedEntities, Metadata *Macros,
    uint64_t DWOId, bool SplitDebugInlining, bool DebugInfoForProfiling,
    unsigned NameTableKind, bool RangesBaseAddress, MDString *SysRoot,
    MDString *SDK, StorageType Storage, bool ShouldCreate) {
  assert(Storage != Uniqued && "Cannot unique DICompileUnit");
  assert(isCanonical(Producer) && "Expected canonical MDString");
  assert(isCanonical(Flags) && "Expected canonical MDString");
  assert(isCanonical(SplitDebugFilename) && "Expected canonical MDString");

  Metadata *Ops[] = {File,
                     Producer,
                     Flags,
                     SplitDebugFilename,
                     EnumTypes,
                     RetainedTypes,
                     GlobalVariables,
                     ImportedEntities,
                     Macros,
                     SysRoot,
                     SDK};
  return storeImpl(new (std::size(Ops), Storage) DICompileUnit(
                       Context, Storage, SourceLanguage, IsOptimized,
                       RuntimeVersion, EmissionKind, DWOId, SplitDebugInlining,
                       DebugInfoForProfiling, NameTableKind, RangesBaseAddress,
                       Ops),
                   Storage);
}

std::optional<DICompileUnit::DebugEmissionKind>
DICompileUnit::getEmissionKind(StringRef Str) {
  return StringSwitch<std::optional<DebugEmissionKind>>(Str)
      .Case("NoDebug", NoDebug)
      .Case("FullDebug", FullDebug)
      .Case("LineTablesOnly", LineTablesOnly)
      .Case("DebugDirectivesOnly", DebugDirectivesOnly)
      .Default(std::nullopt);
}

std::optional<DICompileUnit::DebugNameTableKind>
DICompileUnit::getNameTableKind(StringRef Str) {
  return StringSwitch<std::optional<DebugNameTableKind>>(Str)
      .Case("Default", DebugNameTableKind::Default)
      .Case("GNU", DebugNameTableKind::GNU)
      .Case("Apple", DebugNameTableKind::Apple)
      .Case("None", DebugNameTableKind::None)
      .Default(std::nullopt);
}

const char *DICompileUnit::emissionKindString(DebugEmissionKind EK) {
  switch (EK) {
  case NoDebug:
    return "NoDebug";
  case FullDebug:
    return "FullDebug";
  case LineTablesOnly:
    return "LineTablesOnly";
  case DebugDirectivesOnly:
    return "DebugDirectivesOnly";
  }
  return nullptr;
}

const char *DICompileUnit::nameTableKindString(DebugNameTableKind NTK) {
  switch (NTK) {
  case DebugNameTableKind::Default:
    return nullptr;
  case DebugNameTableKind::GNU:
    return "GNU";
  case DebugNameTableKind::Apple:
    return "Apple";
  case DebugNameTableKind::None:
    return "None";
  }
  return nullptr;
}
DISubprogram::DISubprogram(LLVMContext &C, StorageType Storage, unsigned Line,
                           unsigned ScopeLine, unsigned VirtualIndex,
                           int ThisAdjustment, DIFlags Flags, DISPFlags SPFlags,
                           ArrayRef<Metadata *> Ops)
    : DILocalScope(C, DISubprogramKind, Storage, dwarf::DW_TAG_subprogram, Ops),
      Line(Line), ScopeLine(ScopeLine), VirtualIndex(VirtualIndex),
      ThisAdjustment(ThisAdjustment), Flags(Flags), SPFlags(SPFlags) {
  static_assert(dwarf::DW_VIRTUALITY_max < 4, "Virtuality out of range");
}
DISubprogram::DISPFlags
DISubprogram::toSPFlags(bool IsLocalToUnit, bool IsDefinition, bool IsOptimized,
                        unsigned Virtuality, bool IsMainSubprogram) {
  // We're assuming virtuality is the low-order field.
  static_assert(int(SPFlagVirtual) == int(dwarf::DW_VIRTUALITY_virtual) &&
                    int(SPFlagPureVirtual) ==
                        int(dwarf::DW_VIRTUALITY_pure_virtual),
                "Virtuality constant mismatch");
  return static_cast<DISPFlags>(
      (Virtuality & SPFlagVirtuality) |
      (IsLocalToUnit ? SPFlagLocalToUnit : SPFlagZero) |
      (IsDefinition ? SPFlagDefinition : SPFlagZero) |
      (IsOptimized ? SPFlagOptimized : SPFlagZero) |
      (IsMainSubprogram ? SPFlagMainSubprogram : SPFlagZero));
}

DISubprogram *DILocalScope::getSubprogram() const {
  if (auto *Block = dyn_cast<DILexicalBlockBase>(this))
    return Block->getScope()->getSubprogram();
  return const_cast<DISubprogram *>(cast<DISubprogram>(this));
}

DILocalScope *DILocalScope::getNonLexicalBlockFileScope() const {
  if (auto *File = dyn_cast<DILexicalBlockFile>(this))
    return File->getScope()->getNonLexicalBlockFileScope();
  return const_cast<DILocalScope *>(this);
}

DILocalScope *DILocalScope::cloneScopeForSubprogram(
    DILocalScope &RootScope, DISubprogram &NewSP, LLVMContext &Ctx,
    DenseMap<const MDNode *, MDNode *> &Cache) {
  SmallVector<DIScope *> ScopeChain;
  DIScope *CachedResult = nullptr;

  for (DIScope *Scope = &RootScope; !isa<DISubprogram>(Scope);
       Scope = Scope->getScope()) {
    if (auto It = Cache.find(Scope); It != Cache.end()) {
      CachedResult = cast<DIScope>(It->second);
      break;
    }
    ScopeChain.push_back(Scope);
  }

  // Recreate the scope chain, bottom-up, starting at the new subprogram (or a
  // cached result).
  DIScope *UpdatedScope = CachedResult ? CachedResult : &NewSP;
  for (DIScope *ScopeToUpdate : reverse(ScopeChain)) {
    TempMDNode ClonedScope = ScopeToUpdate->clone();
    cast<DILexicalBlockBase>(*ClonedScope).replaceScope(UpdatedScope);
    UpdatedScope =
        cast<DIScope>(MDNode::replaceWithUniqued(std::move(ClonedScope)));
    Cache[ScopeToUpdate] = UpdatedScope;
  }

  return cast<DILocalScope>(UpdatedScope);
}

DISubprogram::DISPFlags DISubprogram::getFlag(StringRef Flag) {
  return StringSwitch<DISPFlags>(Flag)
#define HANDLE_DISP_FLAG(ID, NAME) .Case("DISPFlag" #NAME, SPFlag##NAME)
#include "llvm/IR/DebugInfoFlags.def"
      .Default(SPFlagZero);
}

StringRef DISubprogram::getFlagString(DISPFlags Flag) {
  switch (Flag) {
  // Appease a warning.
  case SPFlagVirtuality:
    return "";
#define HANDLE_DISP_FLAG(ID, NAME)                                             \
  case SPFlag##NAME:                                                           \
    return "DISPFlag" #NAME;
#include "llvm/IR/DebugInfoFlags.def"
  }
  return "";
}

DISubprogram::DISPFlags
DISubprogram::splitFlags(DISPFlags Flags,
                         SmallVectorImpl<DISPFlags> &SplitFlags) {
  // Multi-bit fields can require special handling. In our case, however, the
  // only multi-bit field is virtuality, and all its values happen to be
  // single-bit values, so the right behavior just falls out.
#define HANDLE_DISP_FLAG(ID, NAME)                                             \
  if (DISPFlags Bit = Flags & SPFlag##NAME) {                                  \
    SplitFlags.push_back(Bit);                                                 \
    Flags &= ~Bit;                                                             \
  }
#include "llvm/IR/DebugInfoFlags.def"
  return Flags;
}

DISubprogram *DISubprogram::getImpl(
    LLVMContext &Context, Metadata *Scope, MDString *Name,
    MDString *LinkageName, Metadata *File, unsigned Line, Metadata *Type,
    unsigned ScopeLine, Metadata *ContainingType, unsigned VirtualIndex,
    int ThisAdjustment, DIFlags Flags, DISPFlags SPFlags, Metadata *Unit,
    Metadata *TemplateParams, Metadata *Declaration, Metadata *RetainedNodes,
    Metadata *ThrownTypes, Metadata *Annotations, MDString *TargetFuncName,
    StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  assert(isCanonical(LinkageName) && "Expected canonical MDString");
  assert(isCanonical(TargetFuncName) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DISubprogram,
                        (Scope, Name, LinkageName, File, Line, Type, ScopeLine,
                         ContainingType, VirtualIndex, ThisAdjustment, Flags,
                         SPFlags, Unit, TemplateParams, Declaration,
                         RetainedNodes, ThrownTypes, Annotations,
                         TargetFuncName));
  SmallVector<Metadata *, 13> Ops = {
      File,           Scope,          Name,        LinkageName,
      Type,           Unit,           Declaration, RetainedNodes,
      ContainingType, TemplateParams, ThrownTypes, Annotations,
      TargetFuncName};
  if (!TargetFuncName) {
    Ops.pop_back();
    if (!Annotations) {
      Ops.pop_back();
      if (!ThrownTypes) {
        Ops.pop_back();
        if (!TemplateParams) {
          Ops.pop_back();
          if (!ContainingType)
            Ops.pop_back();
        }
      }
    }
  }
  DEFINE_GETIMPL_STORE_N(
      DISubprogram,
      (Line, ScopeLine, VirtualIndex, ThisAdjustment, Flags, SPFlags), Ops,
      Ops.size());
}

bool DISubprogram::describes(const Function *F) const {
  assert(F && "Invalid function");
  return F->getSubprogram() == this;
}
DILexicalBlockBase::DILexicalBlockBase(LLVMContext &C, unsigned ID,
                                       StorageType Storage,
                                       ArrayRef<Metadata *> Ops)
    : DILocalScope(C, ID, Storage, dwarf::DW_TAG_lexical_block, Ops) {}

DILexicalBlock *DILexicalBlock::getImpl(LLVMContext &Context, Metadata *Scope,
                                        Metadata *File, unsigned Line,
                                        unsigned Column, StorageType Storage,
                                        bool ShouldCreate) {
  // Fixup column.
  adjustColumn(Column);

  assert(Scope && "Expected scope");
  DEFINE_GETIMPL_LOOKUP(DILexicalBlock, (Scope, File, Line, Column));
  Metadata *Ops[] = {File, Scope};
  DEFINE_GETIMPL_STORE(DILexicalBlock, (Line, Column), Ops);
}

DILexicalBlockFile *DILexicalBlockFile::getImpl(LLVMContext &Context,
                                                Metadata *Scope, Metadata *File,
                                                unsigned Discriminator,
                                                StorageType Storage,
                                                bool ShouldCreate) {
  assert(Scope && "Expected scope");
  DEFINE_GETIMPL_LOOKUP(DILexicalBlockFile, (Scope, File, Discriminator));
  Metadata *Ops[] = {File, Scope};
  DEFINE_GETIMPL_STORE(DILexicalBlockFile, (Discriminator), Ops);
}

DINamespace::DINamespace(LLVMContext &Context, StorageType Storage,
                         bool ExportSymbols, ArrayRef<Metadata *> Ops)
    : DIScope(Context, DINamespaceKind, Storage, dwarf::DW_TAG_namespace, Ops) {
  SubclassData1 = ExportSymbols;
}
DINamespace *DINamespace::getImpl(LLVMContext &Context, Metadata *Scope,
                                  MDString *Name, bool ExportSymbols,
                                  StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DINamespace, (Scope, Name, ExportSymbols));
  // The nullptr is for DIScope's File operand. This should be refactored.
  Metadata *Ops[] = {nullptr, Scope, Name};
  DEFINE_GETIMPL_STORE(DINamespace, (ExportSymbols), Ops);
}

DICommonBlock::DICommonBlock(LLVMContext &Context, StorageType Storage,
                             unsigned LineNo, ArrayRef<Metadata *> Ops)
    : DIScope(Context, DICommonBlockKind, Storage, dwarf::DW_TAG_common_block,
              Ops) {
  SubclassData32 = LineNo;
}
DICommonBlock *DICommonBlock::getImpl(LLVMContext &Context, Metadata *Scope,
                                      Metadata *Decl, MDString *Name,
                                      Metadata *File, unsigned LineNo,
                                      StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DICommonBlock, (Scope, Decl, Name, File, LineNo));
  // The nullptr is for DIScope's File operand. This should be refactored.
  Metadata *Ops[] = {Scope, Decl, Name, File};
  DEFINE_GETIMPL_STORE(DICommonBlock, (LineNo), Ops);
}

DIModule::DIModule(LLVMContext &Context, StorageType Storage, unsigned LineNo,
                   bool IsDecl, ArrayRef<Metadata *> Ops)
    : DIScope(Context, DIModuleKind, Storage, dwarf::DW_TAG_module, Ops) {
  SubclassData1 = IsDecl;
  SubclassData32 = LineNo;
}
DIModule *DIModule::getImpl(LLVMContext &Context, Metadata *File,
                            Metadata *Scope, MDString *Name,
                            MDString *ConfigurationMacros,
                            MDString *IncludePath, MDString *APINotesFile,
                            unsigned LineNo, bool IsDecl, StorageType Storage,
                            bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIModule, (File, Scope, Name, ConfigurationMacros,
                                   IncludePath, APINotesFile, LineNo, IsDecl));
  Metadata *Ops[] = {File,        Scope,       Name, ConfigurationMacros,
                     IncludePath, APINotesFile};
  DEFINE_GETIMPL_STORE(DIModule, (LineNo, IsDecl), Ops);
}
DITemplateTypeParameter::DITemplateTypeParameter(LLVMContext &Context,
                                                 StorageType Storage,
                                                 bool IsDefault,
                                                 ArrayRef<Metadata *> Ops)
    : DITemplateParameter(Context, DITemplateTypeParameterKind, Storage,
                          dwarf::DW_TAG_template_type_parameter, IsDefault,
                          Ops) {}

DITemplateTypeParameter *
DITemplateTypeParameter::getImpl(LLVMContext &Context, MDString *Name,
                                 Metadata *Type, bool isDefault,
                                 StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DITemplateTypeParameter, (Name, Type, isDefault));
  Metadata *Ops[] = {Name, Type};
  DEFINE_GETIMPL_STORE(DITemplateTypeParameter, (isDefault), Ops);
}

DITemplateValueParameter *DITemplateValueParameter::getImpl(
    LLVMContext &Context, unsigned Tag, MDString *Name, Metadata *Type,
    bool isDefault, Metadata *Value, StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DITemplateValueParameter,
                        (Tag, Name, Type, isDefault, Value));
  Metadata *Ops[] = {Name, Type, Value};
  DEFINE_GETIMPL_STORE(DITemplateValueParameter, (Tag, isDefault), Ops);
}

DIGlobalVariable *
DIGlobalVariable::getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name,
                          MDString *LinkageName, Metadata *File, unsigned Line,
                          Metadata *Type, bool IsLocalToUnit, bool IsDefinition,
                          Metadata *StaticDataMemberDeclaration,
                          Metadata *TemplateParams, uint32_t AlignInBits,
                          Metadata *Annotations, StorageType Storage,
                          bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  assert(isCanonical(LinkageName) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(
      DIGlobalVariable,
      (Scope, Name, LinkageName, File, Line, Type, IsLocalToUnit, IsDefinition,
       StaticDataMemberDeclaration, TemplateParams, AlignInBits, Annotations));
  Metadata *Ops[] = {Scope,
                     Name,
                     File,
                     Type,
                     Name,
                     LinkageName,
                     StaticDataMemberDeclaration,
                     TemplateParams,
                     Annotations};
  DEFINE_GETIMPL_STORE(DIGlobalVariable,
                       (Line, IsLocalToUnit, IsDefinition, AlignInBits), Ops);
}

DILocalVariable *
DILocalVariable::getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name,
                         Metadata *File, unsigned Line, Metadata *Type,
                         unsigned Arg, DIFlags Flags, uint32_t AlignInBits,
                         Metadata *Annotations, StorageType Storage,
                         bool ShouldCreate) {
  // 64K ought to be enough for any frontend.
  assert(Arg <= UINT16_MAX && "Expected argument number to fit in 16-bits");

  assert(Scope && "Expected scope");
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DILocalVariable, (Scope, Name, File, Line, Type, Arg,
                                          Flags, AlignInBits, Annotations));
  Metadata *Ops[] = {Scope, Name, File, Type, Annotations};
  DEFINE_GETIMPL_STORE(DILocalVariable, (Line, Arg, Flags, AlignInBits), Ops);
}

DIVariable::DIVariable(LLVMContext &C, unsigned ID, StorageType Storage,
                       signed Line, ArrayRef<Metadata *> Ops,
                       uint32_t AlignInBits)
    : DINode(C, ID, Storage, dwarf::DW_TAG_variable, Ops), Line(Line) {
  SubclassData32 = AlignInBits;
}
std::optional<uint64_t> DIVariable::getSizeInBits() const {
  // This is used by the Verifier so be mindful of broken types.
  const Metadata *RawType = getRawType();
  while (RawType) {
    // Try to get the size directly.
    if (auto *T = dyn_cast<DIType>(RawType))
      if (uint64_t Size = T->getSizeInBits())
        return Size;

    if (auto *DT = dyn_cast<DIDerivedType>(RawType)) {
      // Look at the base type.
      RawType = DT->getRawBaseType();
      continue;
    }

    // Missing type or size.
    break;
  }

  // Fail gracefully.
  return std::nullopt;
}

DILabel::DILabel(LLVMContext &C, StorageType Storage, unsigned Line,
                 ArrayRef<Metadata *> Ops)
    : DINode(C, DILabelKind, Storage, dwarf::DW_TAG_label, Ops) {
  SubclassData32 = Line;
}
DILabel *DILabel::getImpl(LLVMContext &Context, Metadata *Scope, MDString *Name,
                          Metadata *File, unsigned Line, StorageType Storage,
                          bool ShouldCreate) {
  assert(Scope && "Expected scope");
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DILabel, (Scope, Name, File, Line));
  Metadata *Ops[] = {Scope, Name, File};
  DEFINE_GETIMPL_STORE(DILabel, (Line), Ops);
}

DIExpression *DIExpression::getImpl(LLVMContext &Context,
                                    ArrayRef<uint64_t> Elements,
                                    StorageType Storage, bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DIExpression, (Elements));
  DEFINE_GETIMPL_STORE_NO_OPS(DIExpression, (Elements));
}
bool DIExpression::isEntryValue() const {
  if (auto singleLocElts = getSingleLocationExpressionElements()) {
    return singleLocElts->size() > 0 &&
           (*singleLocElts)[0] == dwarf::DW_OP_LLVM_entry_value;
  }
  return false;
}
bool DIExpression::startsWithDeref() const {
  if (auto singleLocElts = getSingleLocationExpressionElements())
    return singleLocElts->size() > 0 &&
           (*singleLocElts)[0] == dwarf::DW_OP_deref;
  return false;
}
bool DIExpression::isDeref() const {
  if (auto singleLocElts = getSingleLocationExpressionElements())
    return singleLocElts->size() == 1 &&
           (*singleLocElts)[0] == dwarf::DW_OP_deref;
  return false;
}

DIAssignID *DIAssignID::getImpl(LLVMContext &Context, StorageType Storage,
                                bool ShouldCreate) {
  // Uniqued DIAssignID are not supported as the instance address *is* the ID.
  assert(Storage != StorageType::Uniqued && "uniqued DIAssignID unsupported");
  return storeImpl(new (0u, Storage) DIAssignID(Context, Storage), Storage);
}

unsigned DIExpression::ExprOperand::getSize() const {
  uint64_t Op = getOp();

  if (Op >= dwarf::DW_OP_breg0 && Op <= dwarf::DW_OP_breg31)
    return 2;

  switch (Op) {
  case dwarf::DW_OP_LLVM_convert:
  case dwarf::DW_OP_LLVM_fragment:
  case dwarf::DW_OP_LLVM_extract_bits_sext:
  case dwarf::DW_OP_LLVM_extract_bits_zext:
  case dwarf::DW_OP_bregx:
    return 3;
  case dwarf::DW_OP_constu:
  case dwarf::DW_OP_consts:
  case dwarf::DW_OP_deref_size:
  case dwarf::DW_OP_plus_uconst:
  case dwarf::DW_OP_LLVM_tag_offset:
  case dwarf::DW_OP_LLVM_entry_value:
  case dwarf::DW_OP_LLVM_arg:
  case dwarf::DW_OP_regx:
    return 2;
  default:
    return 1;
  }
}

bool DIExpression::isValid() const {
  for (auto I = expr_op_begin(), E = expr_op_end(); I != E; ++I) {
    // Check that there's space for the operand.
    if (I->get() + I->getSize() > E->get())
      return false;

    uint64_t Op = I->getOp();
    if ((Op >= dwarf::DW_OP_reg0 && Op <= dwarf::DW_OP_reg31) ||
        (Op >= dwarf::DW_OP_breg0 && Op <= dwarf::DW_OP_breg31))
      return true;

    // Check that the operand is valid.
    switch (Op) {
    default:
      return false;
    case dwarf::DW_OP_LLVM_fragment:
      // A fragment operator must appear at the end.
      return I->get() + I->getSize() == E->get();
    case dwarf::DW_OP_stack_value: {
      // Must be the last one or followed by a DW_OP_LLVM_fragment.
      if (I->get() + I->getSize() == E->get())
        break;
      auto J = I;
      if ((++J)->getOp() != dwarf::DW_OP_LLVM_fragment)
        return false;
      break;
    }
    case dwarf::DW_OP_swap: {
      // Must be more than one implicit element on the stack.

      // FIXME: A better way to implement this would be to add a local variable
      // that keeps track of the stack depth and introduce something like a
      // DW_LLVM_OP_implicit_location as a placeholder for the location this
      // DIExpression is attached to, or else pass the number of implicit stack
      // elements into isValid.
      if (getNumElements() == 1)
        return false;
      break;
    }
    case dwarf::DW_OP_LLVM_entry_value: {
      // An entry value operator must appear at the beginning or immediately
      // following `DW_OP_LLVM_arg 0`, and the number of operations it cover can
      // currently only be 1, because we support only entry values of a simple
      // register location. One reason for this is that we currently can't
      // calculate the size of the resulting DWARF block for other expressions.
      auto FirstOp = expr_op_begin();
      if (FirstOp->getOp() == dwarf::DW_OP_LLVM_arg && FirstOp->getArg(0) == 0)
        ++FirstOp;
      return I->get() == FirstOp->get() && I->getArg(0) == 1;
    }
    case dwarf::DW_OP_LLVM_implicit_pointer:
    case dwarf::DW_OP_LLVM_convert:
    case dwarf::DW_OP_LLVM_arg:
    case dwarf::DW_OP_LLVM_tag_offset:
    case dwarf::DW_OP_LLVM_extract_bits_sext:
    case dwarf::DW_OP_LLVM_extract_bits_zext:
    case dwarf::DW_OP_constu:
    case dwarf::DW_OP_plus_uconst:
    case dwarf::DW_OP_plus:
    case dwarf::DW_OP_minus:
    case dwarf::DW_OP_mul:
    case dwarf::DW_OP_div:
    case dwarf::DW_OP_mod:
    case dwarf::DW_OP_or:
    case dwarf::DW_OP_and:
    case dwarf::DW_OP_xor:
    case dwarf::DW_OP_shl:
    case dwarf::DW_OP_shr:
    case dwarf::DW_OP_shra:
    case dwarf::DW_OP_deref:
    case dwarf::DW_OP_deref_size:
    case dwarf::DW_OP_xderef:
    case dwarf::DW_OP_lit0:
    case dwarf::DW_OP_not:
    case dwarf::DW_OP_dup:
    case dwarf::DW_OP_regx:
    case dwarf::DW_OP_bregx:
    case dwarf::DW_OP_push_object_address:
    case dwarf::DW_OP_over:
    case dwarf::DW_OP_consts:
    case dwarf::DW_OP_eq:
    case dwarf::DW_OP_ne:
    case dwarf::DW_OP_gt:
    case dwarf::DW_OP_ge:
    case dwarf::DW_OP_lt:
    case dwarf::DW_OP_le:
      break;
    }
  }
  return true;
}

bool DIExpression::isImplicit() const {
  if (!isValid())
    return false;

  if (getNumElements() == 0)
    return false;

  for (const auto &It : expr_ops()) {
    switch (It.getOp()) {
    default:
      break;
    case dwarf::DW_OP_stack_value:
      return true;
    }
  }

  return false;
}

bool DIExpression::isComplex() const {
  if (!isValid())
    return false;

  if (getNumElements() == 0)
    return false;

  // If there are any elements other than fragment or tag_offset, then some
  // kind of complex computation occurs.
  for (const auto &It : expr_ops()) {
    switch (It.getOp()) {
    case dwarf::DW_OP_LLVM_tag_offset:
    case dwarf::DW_OP_LLVM_fragment:
    case dwarf::DW_OP_LLVM_arg:
      continue;
    default:
      return true;
    }
  }

  return false;
}

bool DIExpression::isSingleLocationExpression() const {
  if (!isValid())
    return false;

  if (getNumElements() == 0)
    return true;

  auto ExprOpBegin = expr_ops().begin();
  auto ExprOpEnd = expr_ops().end();
  if (ExprOpBegin->getOp() == dwarf::DW_OP_LLVM_arg) {
    if (ExprOpBegin->getArg(0) != 0)
      return false;
    ++ExprOpBegin;
  }

  return !std::any_of(ExprOpBegin, ExprOpEnd, [](auto Op) {
    return Op.getOp() == dwarf::DW_OP_LLVM_arg;
  });
}

std::optional<ArrayRef<uint64_t>>
DIExpression::getSingleLocationExpressionElements() const {
  // Check for `isValid` covered by `isSingleLocationExpression`.
  if (!isSingleLocationExpression())
    return std::nullopt;

  // An empty expression is already non-variadic.
  if (!getNumElements())
    return ArrayRef<uint64_t>();

  // If Expr does not have a leading DW_OP_LLVM_arg then we don't need to do
  // anything.
  if (getElements()[0] == dwarf::DW_OP_LLVM_arg)
    return getElements().drop_front(2);
  return getElements();
}

const DIExpression *
DIExpression::convertToUndefExpression(const DIExpression *Expr) {
  SmallVector<uint64_t, 3> UndefOps;
  if (auto FragmentInfo = Expr->getFragmentInfo()) {
    UndefOps.append({dwarf::DW_OP_LLVM_fragment, FragmentInfo->OffsetInBits,
                     FragmentInfo->SizeInBits});
  }
  return DIExpression::get(Expr->getContext(), UndefOps);
}

const DIExpression *
DIExpression::convertToVariadicExpression(const DIExpression *Expr) {
  if (any_of(Expr->expr_ops(), [](auto ExprOp) {
        return ExprOp.getOp() == dwarf::DW_OP_LLVM_arg;
      }))
    return Expr;
  SmallVector<uint64_t> NewOps;
  NewOps.reserve(Expr->getNumElements() + 2);
  NewOps.append({dwarf::DW_OP_LLVM_arg, 0});
  NewOps.append(Expr->elements_begin(), Expr->elements_end());
  return DIExpression::get(Expr->getContext(), NewOps);
}

std::optional<const DIExpression *>
DIExpression::convertToNonVariadicExpression(const DIExpression *Expr) {
  if (!Expr)
    return std::nullopt;

  if (auto Elts = Expr->getSingleLocationExpressionElements())
    return DIExpression::get(Expr->getContext(), *Elts);

  return std::nullopt;
}

void DIExpression::canonicalizeExpressionOps(SmallVectorImpl<uint64_t> &Ops,
                                             const DIExpression *Expr,
                                             bool IsIndirect) {
  // If Expr is not already variadic, insert the implied `DW_OP_LLVM_arg 0`
  // to the existing expression ops.
  if (none_of(Expr->expr_ops(), [](auto ExprOp) {
        return ExprOp.getOp() == dwarf::DW_OP_LLVM_arg;
      }))
    Ops.append({dwarf::DW_OP_LLVM_arg, 0});
  // If Expr is not indirect, we only need to insert the expression elements and
  // we're done.
  if (!IsIndirect) {
    Ops.append(Expr->elements_begin(), Expr->elements_end());
    return;
  }
  // If Expr is indirect, insert the implied DW_OP_deref at the end of the
  // expression but before DW_OP_{stack_value, LLVM_fragment} if they are
  // present.
  for (auto Op : Expr->expr_ops()) {
    if (Op.getOp() == dwarf::DW_OP_stack_value ||
        Op.getOp() == dwarf::DW_OP_LLVM_fragment) {
      Ops.push_back(dwarf::DW_OP_deref);
      IsIndirect = false;
    }
    Op.appendToVector(Ops);
  }
  if (IsIndirect)
    Ops.push_back(dwarf::DW_OP_deref);
}

bool DIExpression::isEqualExpression(const DIExpression *FirstExpr,
                                     bool FirstIndirect,
                                     const DIExpression *SecondExpr,
                                     bool SecondIndirect) {
  SmallVector<uint64_t> FirstOps;
  DIExpression::canonicalizeExpressionOps(FirstOps, FirstExpr, FirstIndirect);
  SmallVector<uint64_t> SecondOps;
  DIExpression::canonicalizeExpressionOps(SecondOps, SecondExpr,
                                          SecondIndirect);
  return FirstOps == SecondOps;
}

std::optional<DIExpression::FragmentInfo>
DIExpression::getFragmentInfo(expr_op_iterator Start, expr_op_iterator End) {
  for (auto I = Start; I != End; ++I)
    if (I->getOp() == dwarf::DW_OP_LLVM_fragment) {
      DIExpression::FragmentInfo Info = {I->getArg(1), I->getArg(0)};
      return Info;
    }
  return std::nullopt;
}

std::optional<uint64_t> DIExpression::getActiveBits(DIVariable *Var) {
  std::optional<uint64_t> InitialActiveBits = Var->getSizeInBits();
  std::optional<uint64_t> ActiveBits = InitialActiveBits;
  for (auto Op : expr_ops()) {
    switch (Op.getOp()) {
    default:
      // We assume the worst case for anything we don't currently handle and
      // revert to the initial active bits.
      ActiveBits = InitialActiveBits;
      break;
    case dwarf::DW_OP_LLVM_extract_bits_zext:
    case dwarf::DW_OP_LLVM_extract_bits_sext: {
      // We can't handle an extract whose sign doesn't match that of the
      // variable.
      std::optional<DIBasicType::Signedness> VarSign = Var->getSignedness();
      bool VarSigned = (VarSign == DIBasicType::Signedness::Signed);
      bool OpSigned = (Op.getOp() == dwarf::DW_OP_LLVM_extract_bits_sext);
      if (!VarSign || VarSigned != OpSigned) {
        ActiveBits = InitialActiveBits;
        break;
      }
      [[fallthrough]];
    }
    case dwarf::DW_OP_LLVM_fragment:
      // Extract or fragment narrows the active bits
      if (ActiveBits)
        ActiveBits = std::min(*ActiveBits, Op.getArg(1));
      else
        ActiveBits = Op.getArg(1);
      break;
    }
  }
  return ActiveBits;
}

void DIExpression::appendOffset(SmallVectorImpl<uint64_t> &Ops,
                                int64_t Offset) {
  if (Offset > 0) {
    Ops.push_back(dwarf::DW_OP_plus_uconst);
    Ops.push_back(Offset);
  } else if (Offset < 0) {
    Ops.push_back(dwarf::DW_OP_constu);
    // Avoid UB when encountering LLONG_MIN, because in 2's complement
    // abs(LLONG_MIN) is LLONG_MAX+1.
    uint64_t AbsMinusOne = -(Offset+1);
    Ops.push_back(AbsMinusOne + 1);
    Ops.push_back(dwarf::DW_OP_minus);
  }
}

bool DIExpression::extractIfOffset(int64_t &Offset) const {
  auto SingleLocEltsOpt = getSingleLocationExpressionElements();
  if (!SingleLocEltsOpt)
    return false;
  auto SingleLocElts = *SingleLocEltsOpt;

  if (SingleLocElts.size() == 0) {
    Offset = 0;
    return true;
  }

  if (SingleLocElts.size() == 2 &&
      SingleLocElts[0] == dwarf::DW_OP_plus_uconst) {
    Offset = SingleLocElts[1];
    return true;
  }

  if (SingleLocElts.size() == 3 && SingleLocElts[0] == dwarf::DW_OP_constu) {
    if (SingleLocElts[2] == dwarf::DW_OP_plus) {
      Offset = SingleLocElts[1];
      return true;
    }
    if (SingleLocElts[2] == dwarf::DW_OP_minus) {
      Offset = -SingleLocElts[1];
      return true;
    }
  }

  return false;
}

bool DIExpression::extractLeadingOffset(
    int64_t &OffsetInBytes, SmallVectorImpl<uint64_t> &RemainingOps) const {
  OffsetInBytes = 0;
  RemainingOps.clear();

  auto SingleLocEltsOpt = getSingleLocationExpressionElements();
  if (!SingleLocEltsOpt)
    return false;

  auto ExprOpEnd = expr_op_iterator(SingleLocEltsOpt->end());
  auto ExprOpIt = expr_op_iterator(SingleLocEltsOpt->begin());
  while (ExprOpIt != ExprOpEnd) {
    uint64_t Op = ExprOpIt->getOp();
    if (Op == dwarf::DW_OP_deref || Op == dwarf::DW_OP_deref_size ||
        Op == dwarf::DW_OP_deref_type || Op == dwarf::DW_OP_LLVM_fragment ||
        Op == dwarf::DW_OP_LLVM_extract_bits_zext ||
        Op == dwarf::DW_OP_LLVM_extract_bits_sext) {
      break;
    } else if (Op == dwarf::DW_OP_plus_uconst) {
      OffsetInBytes += ExprOpIt->getArg(0);
    } else if (Op == dwarf::DW_OP_constu) {
      uint64_t Value = ExprOpIt->getArg(0);
      ++ExprOpIt;
      if (ExprOpIt->getOp() == dwarf::DW_OP_plus)
        OffsetInBytes += Value;
      else if (ExprOpIt->getOp() == dwarf::DW_OP_minus)
        OffsetInBytes -= Value;
      else
        return false;
    } else {
      // Not a const plus/minus operation or deref.
      return false;
    }
    ++ExprOpIt;
  }
  RemainingOps.append(ExprOpIt.getBase(), ExprOpEnd.getBase());
  return true;
}

bool DIExpression::hasAllLocationOps(unsigned N) const {
  SmallDenseSet<uint64_t, 4> SeenOps;
  for (auto ExprOp : expr_ops())
    if (ExprOp.getOp() == dwarf::DW_OP_LLVM_arg)
      SeenOps.insert(ExprOp.getArg(0));
  for (uint64_t Idx = 0; Idx < N; ++Idx)
    if (!SeenOps.contains(Idx))
      return false;
  return true;
}

const DIExpression *DIExpression::extractAddressClass(const DIExpression *Expr,
                                                      unsigned &AddrClass) {
  // FIXME: This seems fragile. Nothing that verifies that these elements
  // actually map to ops and not operands.
  auto SingleLocEltsOpt = Expr->getSingleLocationExpressionElements();
  if (!SingleLocEltsOpt)
    return nullptr;
  auto SingleLocElts = *SingleLocEltsOpt;

  const unsigned PatternSize = 4;
  if (SingleLocElts.size() >= PatternSize &&
      SingleLocElts[PatternSize - 4] == dwarf::DW_OP_constu &&
      SingleLocElts[PatternSize - 2] == dwarf::DW_OP_swap &&
      SingleLocElts[PatternSize - 1] == dwarf::DW_OP_xderef) {
    AddrClass = SingleLocElts[PatternSize - 3];

    if (SingleLocElts.size() == PatternSize)
      return nullptr;
    return DIExpression::get(
        Expr->getContext(),
        ArrayRef(&*SingleLocElts.begin(), SingleLocElts.size() - PatternSize));
  }
  return Expr;
}

DIExpression *DIExpression::prepend(const DIExpression *Expr, uint8_t Flags,
                                    int64_t Offset) {
  SmallVector<uint64_t, 8> Ops;
  if (Flags & DIExpression::DerefBefore)
    Ops.push_back(dwarf::DW_OP_deref);

  appendOffset(Ops, Offset);
  if (Flags & DIExpression::DerefAfter)
    Ops.push_back(dwarf::DW_OP_deref);

  bool StackValue = Flags & DIExpression::StackValue;
  bool EntryValue = Flags & DIExpression::EntryValue;

  return prependOpcodes(Expr, Ops, StackValue, EntryValue);
}

DIExpression *DIExpression::appendOpsToArg(const DIExpression *Expr,
                                           ArrayRef<uint64_t> Ops,
                                           unsigned ArgNo, bool StackValue) {
  assert(Expr && "Can't add ops to this expression");

  // Handle non-variadic intrinsics by prepending the opcodes.
  if (!any_of(Expr->expr_ops(),
              [](auto Op) { return Op.getOp() == dwarf::DW_OP_LLVM_arg; })) {
    assert(ArgNo == 0 &&
           "Location Index must be 0 for a non-variadic expression.");
    SmallVector<uint64_t, 8> NewOps(Ops.begin(), Ops.end());
    return DIExpression::prependOpcodes(Expr, NewOps, StackValue);
  }

  SmallVector<uint64_t, 8> NewOps;
  for (auto Op : Expr->expr_ops()) {
    // A DW_OP_stack_value comes at the end, but before a DW_OP_LLVM_fragment.
    if (StackValue) {
      if (Op.getOp() == dwarf::DW_OP_stack_value)
        StackValue = false;
      else if (Op.getOp() == dwarf::DW_OP_LLVM_fragment) {
        NewOps.push_back(dwarf::DW_OP_stack_value);
        StackValue = false;
      }
    }
    Op.appendToVector(NewOps);
    if (Op.getOp() == dwarf::DW_OP_LLVM_arg && Op.getArg(0) == ArgNo)
      NewOps.insert(NewOps.end(), Ops.begin(), Ops.end());
  }
  if (StackValue)
    NewOps.push_back(dwarf::DW_OP_stack_value);

  return DIExpression::get(Expr->getContext(), NewOps);
}

DIExpression *DIExpression::replaceArg(const DIExpression *Expr,
                                       uint64_t OldArg, uint64_t NewArg) {
  assert(Expr && "Can't replace args in this expression");

  SmallVector<uint64_t, 8> NewOps;

  for (auto Op : Expr->expr_ops()) {
    if (Op.getOp() != dwarf::DW_OP_LLVM_arg || Op.getArg(0) < OldArg) {
      Op.appendToVector(NewOps);
      continue;
    }
    NewOps.push_back(dwarf::DW_OP_LLVM_arg);
    uint64_t Arg = Op.getArg(0) == OldArg ? NewArg : Op.getArg(0);
    // OldArg has been deleted from the Op list, so decrement all indices
    // greater than it.
    if (Arg > OldArg)
      --Arg;
    NewOps.push_back(Arg);
  }
  return DIExpression::get(Expr->getContext(), NewOps);
}

DIExpression *DIExpression::prependOpcodes(const DIExpression *Expr,
                                           SmallVectorImpl<uint64_t> &Ops,
                                           bool StackValue, bool EntryValue) {
  assert(Expr && "Can't prepend ops to this expression");

  if (EntryValue) {
    Ops.push_back(dwarf::DW_OP_LLVM_entry_value);
    // Use a block size of 1 for the target register operand.  The
    // DWARF backend currently cannot emit entry values with a block
    // size > 1.
    Ops.push_back(1);
  }

  // If there are no ops to prepend, do not even add the DW_OP_stack_value.
  if (Ops.empty())
    StackValue = false;
  for (auto Op : Expr->expr_ops()) {
    // A DW_OP_stack_value comes at the end, but before a DW_OP_LLVM_fragment.
    if (StackValue) {
      if (Op.getOp() == dwarf::DW_OP_stack_value)
        StackValue = false;
      else if (Op.getOp() == dwarf::DW_OP_LLVM_fragment) {
        Ops.push_back(dwarf::DW_OP_stack_value);
        StackValue = false;
      }
    }
    Op.appendToVector(Ops);
  }
  if (StackValue)
    Ops.push_back(dwarf::DW_OP_stack_value);
  return DIExpression::get(Expr->getContext(), Ops);
}

DIExpression *DIExpression::append(const DIExpression *Expr,
                                   ArrayRef<uint64_t> Ops) {
  assert(Expr && !Ops.empty() && "Can't append ops to this expression");

  // Copy Expr's current op list.
  SmallVector<uint64_t, 16> NewOps;
  for (auto Op : Expr->expr_ops()) {
    // Append new opcodes before DW_OP_{stack_value, LLVM_fragment}.
    if (Op.getOp() == dwarf::DW_OP_stack_value ||
        Op.getOp() == dwarf::DW_OP_LLVM_fragment) {
      NewOps.append(Ops.begin(), Ops.end());

      // Ensure that the new opcodes are only appended once.
      Ops = std::nullopt;
    }
    Op.appendToVector(NewOps);
  }
  NewOps.append(Ops.begin(), Ops.end());
  auto *result =
      DIExpression::get(Expr->getContext(), NewOps)->foldConstantMath();
  assert(result->isValid() && "concatenated expression is not valid");
  return result;
}

DIExpression *DIExpression::appendToStack(const DIExpression *Expr,
                                          ArrayRef<uint64_t> Ops) {
  assert(Expr && !Ops.empty() && "Can't append ops to this expression");
  assert(std::none_of(expr_op_iterator(Ops.begin()),
                      expr_op_iterator(Ops.end()),
                      [](auto Op) {
                        return Op.getOp() == dwarf::DW_OP_stack_value ||
                               Op.getOp() == dwarf::DW_OP_LLVM_fragment;
                      }) &&
         "Can't append this op");

  // Append a DW_OP_deref after Expr's current op list if it's non-empty and
  // has no DW_OP_stack_value.
  //
  // Match .* DW_OP_stack_value (DW_OP_LLVM_fragment A B)?.
  std::optional<FragmentInfo> FI = Expr->getFragmentInfo();
  unsigned DropUntilStackValue = FI ? 3 : 0;
  ArrayRef<uint64_t> ExprOpsBeforeFragment =
      Expr->getElements().drop_back(DropUntilStackValue);
  bool NeedsDeref = (Expr->getNumElements() > DropUntilStackValue) &&
                    (ExprOpsBeforeFragment.back() != dwarf::DW_OP_stack_value);
  bool NeedsStackValue = NeedsDeref || ExprOpsBeforeFragment.empty();

  // Append a DW_OP_deref after Expr's current op list if needed, then append
  // the new ops, and finally ensure that a single DW_OP_stack_value is present.
  SmallVector<uint64_t, 16> NewOps;
  if (NeedsDeref)
    NewOps.push_back(dwarf::DW_OP_deref);
  NewOps.append(Ops.begin(), Ops.end());
  if (NeedsStackValue)
    NewOps.push_back(dwarf::DW_OP_stack_value);
  return DIExpression::append(Expr, NewOps);
}

std::optional<DIExpression *> DIExpression::createFragmentExpression(
    const DIExpression *Expr, unsigned OffsetInBits, unsigned SizeInBits) {
  SmallVector<uint64_t, 8> Ops;
  // Track whether it's safe to split the value at the top of the DWARF stack,
  // assuming that it'll be used as an implicit location value.
  bool CanSplitValue = true;
  // Track whether we need to add a fragment expression to the end of Expr.
  bool EmitFragment = true;
  // Copy over the expression, but leave off any trailing DW_OP_LLVM_fragment.
  if (Expr) {
    for (auto Op : Expr->expr_ops()) {
      switch (Op.getOp()) {
      default:
        break;
      case dwarf::DW_OP_shr:
      case dwarf::DW_OP_shra:
      case dwarf::DW_OP_shl:
      case dwarf::DW_OP_plus:
      case dwarf::DW_OP_plus_uconst:
      case dwarf::DW_OP_minus:
        // We can't safely split arithmetic or shift operations into multiple
        // fragments because we can't express carry-over between fragments.
        //
        // FIXME: We *could* preserve the lowest fragment of a constant offset
        // operation if the offset fits into SizeInBits.
        CanSplitValue = false;
        break;
      case dwarf::DW_OP_deref:
      case dwarf::DW_OP_deref_size:
      case dwarf::DW_OP_deref_type:
      case dwarf::DW_OP_xderef:
      case dwarf::DW_OP_xderef_size:
      case dwarf::DW_OP_xderef_type:
        // Preceeding arithmetic operations have been applied to compute an
        // address. It's okay to split the value loaded from that address.
        CanSplitValue = true;
        break;
      case dwarf::DW_OP_stack_value:
        // Bail if this expression computes a value that cannot be split.
        if (!CanSplitValue)
          return std::nullopt;
        break;
      case dwarf::DW_OP_LLVM_fragment: {
        // If we've decided we don't need a fragment then give up if we see that
        // there's already a fragment expression.
        // FIXME: We could probably do better here
        if (!EmitFragment)
          return std::nullopt;
        // Make the new offset point into the existing fragment.
        uint64_t FragmentOffsetInBits = Op.getArg(0);
        uint64_t FragmentSizeInBits = Op.getArg(1);
        (void)FragmentSizeInBits;
        assert((OffsetInBits + SizeInBits <= FragmentSizeInBits) &&
               "new fragment outside of original fragment");
        OffsetInBits += FragmentOffsetInBits;
        continue;
      }
      case dwarf::DW_OP_LLVM_extract_bits_zext:
      case dwarf::DW_OP_LLVM_extract_bits_sext: {
        // If we're extracting bits from inside of the fragment that we're
        // creating then we don't have a fragment after all, and just need to
        // adjust the offset that we're extracting from.
        uint64_t ExtractOffsetInBits = Op.getArg(0);
        uint64_t ExtractSizeInBits = Op.getArg(1);
        if (ExtractOffsetInBits >= OffsetInBits &&
            ExtractOffsetInBits + ExtractSizeInBits <=
                OffsetInBits + SizeInBits) {
          Ops.push_back(Op.getOp());
          Ops.push_back(ExtractOffsetInBits - OffsetInBits);
          Ops.push_back(ExtractSizeInBits);
          EmitFragment = false;
          continue;
        }
        // If the extracted bits aren't fully contained within the fragment then
        // give up.
        // FIXME: We could probably do better here
        return std::nullopt;
      }
      }
      Op.appendToVector(Ops);
    }
  }
  assert((!Expr->isImplicit() || CanSplitValue) && "Expr can't be split");
  assert(Expr && "Unknown DIExpression");
  if (EmitFragment) {
    Ops.push_back(dwarf::DW_OP_LLVM_fragment);
    Ops.push_back(OffsetInBits);
    Ops.push_back(SizeInBits);
  }
  return DIExpression::get(Expr->getContext(), Ops);
}

/// See declaration for more info.
bool DIExpression::calculateFragmentIntersect(
    const DataLayout &DL, const Value *SliceStart, uint64_t SliceOffsetInBits,
    uint64_t SliceSizeInBits, const Value *DbgPtr, int64_t DbgPtrOffsetInBits,
    int64_t DbgExtractOffsetInBits, DIExpression::FragmentInfo VarFrag,
    std::optional<DIExpression::FragmentInfo> &Result,
    int64_t &OffsetFromLocationInBits) {

  if (VarFrag.SizeInBits == 0)
    return false; // Variable size is unknown.

  // Difference between mem slice start and the dbg location start.
  // 0   4   8   12   16 ...
  // |       |
  // dbg location start
  //         |
  //         mem slice start
  // Here MemStartRelToDbgStartInBits is 8. Note this can be negative.
  int64_t MemStartRelToDbgStartInBits;
  {
    auto MemOffsetFromDbgInBytes = SliceStart->getPointerOffsetFrom(DbgPtr, DL);
    if (!MemOffsetFromDbgInBytes)
      return false; // Can't calculate difference in addresses.
    // Difference between the pointers.
    MemStartRelToDbgStartInBits = *MemOffsetFromDbgInBytes * 8;
    // Add the difference of the offsets.
    MemStartRelToDbgStartInBits +=
        SliceOffsetInBits - (DbgPtrOffsetInBits + DbgExtractOffsetInBits);
  }

  // Out-param. Invert offset to get offset from debug location.
  OffsetFromLocationInBits = -MemStartRelToDbgStartInBits;

  // Check if the variable fragment sits outside (before) this memory slice.
  int64_t MemEndRelToDbgStart = MemStartRelToDbgStartInBits + SliceSizeInBits;
  if (MemEndRelToDbgStart < 0) {
    Result = {0, 0}; // Out-param.
    return true;
  }

  // Work towards creating SliceOfVariable which is the bits of the variable
  // that the memory region covers.
  // 0   4   8   12   16 ...
  // |       |
  // dbg location start with VarFrag offset=32
  //         |
  //         mem slice start: SliceOfVariable offset=40
  int64_t MemStartRelToVarInBits =
      MemStartRelToDbgStartInBits + VarFrag.OffsetInBits;
  int64_t MemEndRelToVarInBits = MemStartRelToVarInBits + SliceSizeInBits;
  // If the memory region starts before the debug location the fragment
  // offset would be negative, which we can't encode. Limit those to 0. This
  // is fine because those bits necessarily don't overlap with the existing
  // variable fragment.
  int64_t MemFragStart = std::max<int64_t>(0, MemStartRelToVarInBits);
  int64_t MemFragSize =
      std::max<int64_t>(0, MemEndRelToVarInBits - MemFragStart);
  DIExpression::FragmentInfo SliceOfVariable(MemFragSize, MemFragStart);

  // Intersect the memory region fragment with the variable location fragment.
  DIExpression::FragmentInfo TrimmedSliceOfVariable =
      DIExpression::FragmentInfo::intersect(SliceOfVariable, VarFrag);
  if (TrimmedSliceOfVariable == VarFrag)
    Result = std::nullopt; // Out-param.
  else
    Result = TrimmedSliceOfVariable; // Out-param.
  return true;
}

std::pair<DIExpression *, const ConstantInt *>
DIExpression::constantFold(const ConstantInt *CI) {
  // Copy the APInt so we can modify it.
  APInt NewInt = CI->getValue();
  SmallVector<uint64_t, 8> Ops;

  // Fold operators only at the beginning of the expression.
  bool First = true;
  bool Changed = false;
  for (auto Op : expr_ops()) {
    switch (Op.getOp()) {
    default:
      // We fold only the leading part of the expression; if we get to a part
      // that we're going to copy unchanged, and haven't done any folding,
      // then the entire expression is unchanged and we can return early.
      if (!Changed)
        return {this, CI};
      First = false;
      break;
    case dwarf::DW_OP_LLVM_convert:
      if (!First)
        break;
      Changed = true;
      if (Op.getArg(1) == dwarf::DW_ATE_signed)
        NewInt = NewInt.sextOrTrunc(Op.getArg(0));
      else {
        assert(Op.getArg(1) == dwarf::DW_ATE_unsigned && "Unexpected operand");
        NewInt = NewInt.zextOrTrunc(Op.getArg(0));
      }
      continue;
    }
    Op.appendToVector(Ops);
  }
  if (!Changed)
    return {this, CI};
  return {DIExpression::get(getContext(), Ops),
          ConstantInt::get(getContext(), NewInt)};
}

uint64_t DIExpression::getNumLocationOperands() const {
  uint64_t Result = 0;
  for (auto ExprOp : expr_ops())
    if (ExprOp.getOp() == dwarf::DW_OP_LLVM_arg)
      Result = std::max(Result, ExprOp.getArg(0) + 1);
  assert(hasAllLocationOps(Result) &&
         "Expression is missing one or more location operands.");
  return Result;
}

std::optional<DIExpression::SignedOrUnsignedConstant>
DIExpression::isConstant() const {

  // Recognize signed and unsigned constants.
  // An signed constants can be represented as DW_OP_consts C DW_OP_stack_value
  // (DW_OP_LLVM_fragment of Len).
  // An unsigned constant can be represented as
  // DW_OP_constu C DW_OP_stack_value (DW_OP_LLVM_fragment of Len).

  if ((getNumElements() != 2 && getNumElements() != 3 &&
       getNumElements() != 6) ||
      (getElement(0) != dwarf::DW_OP_consts &&
       getElement(0) != dwarf::DW_OP_constu))
    return std::nullopt;

  if (getNumElements() == 2 && getElement(0) == dwarf::DW_OP_consts)
    return SignedOrUnsignedConstant::SignedConstant;

  if ((getNumElements() == 3 && getElement(2) != dwarf::DW_OP_stack_value) ||
      (getNumElements() == 6 && (getElement(2) != dwarf::DW_OP_stack_value ||
                                 getElement(3) != dwarf::DW_OP_LLVM_fragment)))
    return std::nullopt;
  return getElement(0) == dwarf::DW_OP_constu
             ? SignedOrUnsignedConstant::UnsignedConstant
             : SignedOrUnsignedConstant::SignedConstant;
}

DIExpression::ExtOps DIExpression::getExtOps(unsigned FromSize, unsigned ToSize,
                                             bool Signed) {
  dwarf::TypeKind TK = Signed ? dwarf::DW_ATE_signed : dwarf::DW_ATE_unsigned;
  DIExpression::ExtOps Ops{{dwarf::DW_OP_LLVM_convert, FromSize, TK,
                            dwarf::DW_OP_LLVM_convert, ToSize, TK}};
  return Ops;
}

DIExpression *DIExpression::appendExt(const DIExpression *Expr,
                                      unsigned FromSize, unsigned ToSize,
                                      bool Signed) {
  return appendToStack(Expr, getExtOps(FromSize, ToSize, Signed));
}

DIGlobalVariableExpression *
DIGlobalVariableExpression::getImpl(LLVMContext &Context, Metadata *Variable,
                                    Metadata *Expression, StorageType Storage,
                                    bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DIGlobalVariableExpression, (Variable, Expression));
  Metadata *Ops[] = {Variable, Expression};
  DEFINE_GETIMPL_STORE_NO_CONSTRUCTOR_ARGS(DIGlobalVariableExpression, Ops);
}
DIObjCProperty::DIObjCProperty(LLVMContext &C, StorageType Storage,
                               unsigned Line, unsigned Attributes,
                               ArrayRef<Metadata *> Ops)
    : DINode(C, DIObjCPropertyKind, Storage, dwarf::DW_TAG_APPLE_property, Ops),
      Line(Line), Attributes(Attributes) {}

DIObjCProperty *DIObjCProperty::getImpl(
    LLVMContext &Context, MDString *Name, Metadata *File, unsigned Line,
    MDString *GetterName, MDString *SetterName, unsigned Attributes,
    Metadata *Type, StorageType Storage, bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  assert(isCanonical(GetterName) && "Expected canonical MDString");
  assert(isCanonical(SetterName) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIObjCProperty, (Name, File, Line, GetterName,
                                         SetterName, Attributes, Type));
  Metadata *Ops[] = {Name, File, GetterName, SetterName, Type};
  DEFINE_GETIMPL_STORE(DIObjCProperty, (Line, Attributes), Ops);
}

DIImportedEntity *DIImportedEntity::getImpl(LLVMContext &Context, unsigned Tag,
                                            Metadata *Scope, Metadata *Entity,
                                            Metadata *File, unsigned Line,
                                            MDString *Name, Metadata *Elements,
                                            StorageType Storage,
                                            bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIImportedEntity,
                        (Tag, Scope, Entity, File, Line, Name, Elements));
  Metadata *Ops[] = {Scope, Entity, Name, File, Elements};
  DEFINE_GETIMPL_STORE(DIImportedEntity, (Tag, Line), Ops);
}

DIMacro *DIMacro::getImpl(LLVMContext &Context, unsigned MIType, unsigned Line,
                          MDString *Name, MDString *Value, StorageType Storage,
                          bool ShouldCreate) {
  assert(isCanonical(Name) && "Expected canonical MDString");
  DEFINE_GETIMPL_LOOKUP(DIMacro, (MIType, Line, Name, Value));
  Metadata *Ops[] = {Name, Value};
  DEFINE_GETIMPL_STORE(DIMacro, (MIType, Line), Ops);
}

DIMacroFile *DIMacroFile::getImpl(LLVMContext &Context, unsigned MIType,
                                  unsigned Line, Metadata *File,
                                  Metadata *Elements, StorageType Storage,
                                  bool ShouldCreate) {
  DEFINE_GETIMPL_LOOKUP(DIMacroFile, (MIType, Line, File, Elements));
  Metadata *Ops[] = {File, Elements};
  DEFINE_GETIMPL_STORE(DIMacroFile, (MIType, Line), Ops);
}

DIArgList *DIArgList::get(LLVMContext &Context,
                          ArrayRef<ValueAsMetadata *> Args) {
  auto ExistingIt = Context.pImpl->DIArgLists.find_as(DIArgListKeyInfo(Args));
  if (ExistingIt != Context.pImpl->DIArgLists.end())
    return *ExistingIt;
  DIArgList *NewArgList = new DIArgList(Context, Args);
  Context.pImpl->DIArgLists.insert(NewArgList);
  return NewArgList;
}

void DIArgList::handleChangedOperand(void *Ref, Metadata *New) {
  ValueAsMetadata **OldVMPtr = static_cast<ValueAsMetadata **>(Ref);
  assert((!New || isa<ValueAsMetadata>(New)) &&
         "DIArgList must be passed a ValueAsMetadata");
  untrack();
  // We need to update the set storage once the Args are updated since they
  // form the key to the DIArgLists store.
  getContext().pImpl->DIArgLists.erase(this);
  ValueAsMetadata *NewVM = cast_or_null<ValueAsMetadata>(New);
  for (ValueAsMetadata *&VM : Args) {
    if (&VM == OldVMPtr) {
      if (NewVM)
        VM = NewVM;
      else
        VM = ValueAsMetadata::get(PoisonValue::get(VM->getValue()->getType()));
    }
  }
  // We've changed the contents of this DIArgList, and the set storage may
  // already contain a DIArgList with our new set of args; if it does, then we
  // must RAUW this with the existing DIArgList, otherwise we simply insert this
  // back into the set storage.
  DIArgList *ExistingArgList = getUniqued(getContext().pImpl->DIArgLists, this);
  if (ExistingArgList) {
    replaceAllUsesWith(ExistingArgList);
    // Clear this here so we don't try to untrack in the destructor.
    Args.clear();
    delete this;
    return;
  }
  getContext().pImpl->DIArgLists.insert(this);
  track();
}
void DIArgList::track() {
  for (ValueAsMetadata *&VAM : Args)
    if (VAM)
      MetadataTracking::track(&VAM, *VAM, *this);
}
void DIArgList::untrack() {
  for (ValueAsMetadata *&VAM : Args)
    if (VAM)
      MetadataTracking::untrack(&VAM, *VAM);
}
void DIArgList::dropAllReferences(bool Untrack) {
  if (Untrack)
    untrack();
  Args.clear();
  ReplaceableMetadataImpl::resolveAllUses(/* ResolveUsers */ false);
}
