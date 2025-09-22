//===- Record.cpp - Record implementation ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implement the tablegen record classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/Record.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "tblgen-records"

//===----------------------------------------------------------------------===//
//    Context
//===----------------------------------------------------------------------===//

namespace llvm {
namespace detail {
/// This class represents the internal implementation of the RecordKeeper.
/// It contains all of the contextual static state of the Record classes. It is
/// kept out-of-line to simplify dependencies, and also make it easier for
/// internal classes to access the uniquer state of the keeper.
struct RecordKeeperImpl {
  RecordKeeperImpl(RecordKeeper &RK)
      : SharedBitRecTy(RK), SharedIntRecTy(RK), SharedStringRecTy(RK),
        SharedDagRecTy(RK), AnyRecord(RK, 0), TheUnsetInit(RK),
        TrueBitInit(true, &SharedBitRecTy),
        FalseBitInit(false, &SharedBitRecTy), StringInitStringPool(Allocator),
        StringInitCodePool(Allocator), AnonCounter(0), LastRecordID(0) {}

  BumpPtrAllocator Allocator;
  std::vector<BitsRecTy *> SharedBitsRecTys;
  BitRecTy SharedBitRecTy;
  IntRecTy SharedIntRecTy;
  StringRecTy SharedStringRecTy;
  DagRecTy SharedDagRecTy;

  RecordRecTy AnyRecord;
  UnsetInit TheUnsetInit;
  BitInit TrueBitInit;
  BitInit FalseBitInit;

  FoldingSet<ArgumentInit> TheArgumentInitPool;
  FoldingSet<BitsInit> TheBitsInitPool;
  std::map<int64_t, IntInit *> TheIntInitPool;
  StringMap<StringInit *, BumpPtrAllocator &> StringInitStringPool;
  StringMap<StringInit *, BumpPtrAllocator &> StringInitCodePool;
  FoldingSet<ListInit> TheListInitPool;
  FoldingSet<UnOpInit> TheUnOpInitPool;
  FoldingSet<BinOpInit> TheBinOpInitPool;
  FoldingSet<TernOpInit> TheTernOpInitPool;
  FoldingSet<FoldOpInit> TheFoldOpInitPool;
  FoldingSet<IsAOpInit> TheIsAOpInitPool;
  FoldingSet<ExistsOpInit> TheExistsOpInitPool;
  DenseMap<std::pair<RecTy *, Init *>, VarInit *> TheVarInitPool;
  DenseMap<std::pair<TypedInit *, unsigned>, VarBitInit *> TheVarBitInitPool;
  FoldingSet<VarDefInit> TheVarDefInitPool;
  DenseMap<std::pair<Init *, StringInit *>, FieldInit *> TheFieldInitPool;
  FoldingSet<CondOpInit> TheCondOpInitPool;
  FoldingSet<DagInit> TheDagInitPool;
  FoldingSet<RecordRecTy> RecordTypePool;

  unsigned AnonCounter;
  unsigned LastRecordID;
};
} // namespace detail
} // namespace llvm

//===----------------------------------------------------------------------===//
//    Type implementations
//===----------------------------------------------------------------------===//

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RecTy::dump() const { print(errs()); }
#endif

ListRecTy *RecTy::getListTy() {
  if (!ListTy)
    ListTy = new (RK.getImpl().Allocator) ListRecTy(this);
  return ListTy;
}

bool RecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  assert(RHS && "NULL pointer");
  return Kind == RHS->getRecTyKind();
}

bool RecTy::typeIsA(const RecTy *RHS) const { return this == RHS; }

BitRecTy *BitRecTy::get(RecordKeeper &RK) {
  return &RK.getImpl().SharedBitRecTy;
}

bool BitRecTy::typeIsConvertibleTo(const RecTy *RHS) const{
  if (RecTy::typeIsConvertibleTo(RHS) || RHS->getRecTyKind() == IntRecTyKind)
    return true;
  if (const BitsRecTy *BitsTy = dyn_cast<BitsRecTy>(RHS))
    return BitsTy->getNumBits() == 1;
  return false;
}

BitsRecTy *BitsRecTy::get(RecordKeeper &RK, unsigned Sz) {
  detail::RecordKeeperImpl &RKImpl = RK.getImpl();
  if (Sz >= RKImpl.SharedBitsRecTys.size())
    RKImpl.SharedBitsRecTys.resize(Sz + 1);
  BitsRecTy *&Ty = RKImpl.SharedBitsRecTys[Sz];
  if (!Ty)
    Ty = new (RKImpl.Allocator) BitsRecTy(RK, Sz);
  return Ty;
}

std::string BitsRecTy::getAsString() const {
  return "bits<" + utostr(Size) + ">";
}

bool BitsRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  if (RecTy::typeIsConvertibleTo(RHS)) //argument and the sender are same type
    return cast<BitsRecTy>(RHS)->Size == Size;
  RecTyKind kind = RHS->getRecTyKind();
  return (kind == BitRecTyKind && Size == 1) || (kind == IntRecTyKind);
}

IntRecTy *IntRecTy::get(RecordKeeper &RK) {
  return &RK.getImpl().SharedIntRecTy;
}

bool IntRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  RecTyKind kind = RHS->getRecTyKind();
  return kind==BitRecTyKind || kind==BitsRecTyKind || kind==IntRecTyKind;
}

StringRecTy *StringRecTy::get(RecordKeeper &RK) {
  return &RK.getImpl().SharedStringRecTy;
}

std::string StringRecTy::getAsString() const {
  return "string";
}

bool StringRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  RecTyKind Kind = RHS->getRecTyKind();
  return Kind == StringRecTyKind;
}

std::string ListRecTy::getAsString() const {
  return "list<" + ElementTy->getAsString() + ">";
}

bool ListRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  if (const auto *ListTy = dyn_cast<ListRecTy>(RHS))
    return ElementTy->typeIsConvertibleTo(ListTy->getElementType());
  return false;
}

bool ListRecTy::typeIsA(const RecTy *RHS) const {
  if (const ListRecTy *RHSl = dyn_cast<ListRecTy>(RHS))
    return getElementType()->typeIsA(RHSl->getElementType());
  return false;
}

DagRecTy *DagRecTy::get(RecordKeeper &RK) {
  return &RK.getImpl().SharedDagRecTy;
}

std::string DagRecTy::getAsString() const {
  return "dag";
}

static void ProfileRecordRecTy(FoldingSetNodeID &ID,
                               ArrayRef<Record *> Classes) {
  ID.AddInteger(Classes.size());
  for (Record *R : Classes)
    ID.AddPointer(R);
}

RecordRecTy *RecordRecTy::get(RecordKeeper &RK,
                              ArrayRef<Record *> UnsortedClasses) {
  detail::RecordKeeperImpl &RKImpl = RK.getImpl();
  if (UnsortedClasses.empty())
    return &RKImpl.AnyRecord;

  FoldingSet<RecordRecTy> &ThePool = RKImpl.RecordTypePool;

  SmallVector<Record *, 4> Classes(UnsortedClasses.begin(),
                                   UnsortedClasses.end());
  llvm::sort(Classes, [](Record *LHS, Record *RHS) {
    return LHS->getNameInitAsString() < RHS->getNameInitAsString();
  });

  FoldingSetNodeID ID;
  ProfileRecordRecTy(ID, Classes);

  void *IP = nullptr;
  if (RecordRecTy *Ty = ThePool.FindNodeOrInsertPos(ID, IP))
    return Ty;

#ifndef NDEBUG
  // Check for redundancy.
  for (unsigned i = 0; i < Classes.size(); ++i) {
    for (unsigned j = 0; j < Classes.size(); ++j) {
      assert(i == j || !Classes[i]->isSubClassOf(Classes[j]));
    }
    assert(&Classes[0]->getRecords() == &Classes[i]->getRecords());
  }
#endif

  void *Mem = RKImpl.Allocator.Allocate(
      totalSizeToAlloc<Record *>(Classes.size()), alignof(RecordRecTy));
  RecordRecTy *Ty = new (Mem) RecordRecTy(RK, Classes.size());
  std::uninitialized_copy(Classes.begin(), Classes.end(),
                          Ty->getTrailingObjects<Record *>());
  ThePool.InsertNode(Ty, IP);
  return Ty;
}
RecordRecTy *RecordRecTy::get(Record *Class) {
  assert(Class && "unexpected null class");
  return get(Class->getRecords(), Class);
}

void RecordRecTy::Profile(FoldingSetNodeID &ID) const {
  ProfileRecordRecTy(ID, getClasses());
}

std::string RecordRecTy::getAsString() const {
  if (NumClasses == 1)
    return getClasses()[0]->getNameInitAsString();

  std::string Str = "{";
  bool First = true;
  for (Record *R : getClasses()) {
    if (!First)
      Str += ", ";
    First = false;
    Str += R->getNameInitAsString();
  }
  Str += "}";
  return Str;
}

bool RecordRecTy::isSubClassOf(Record *Class) const {
  return llvm::any_of(getClasses(), [Class](Record *MySuperClass) {
                                      return MySuperClass == Class ||
                                             MySuperClass->isSubClassOf(Class);
                                    });
}

bool RecordRecTy::typeIsConvertibleTo(const RecTy *RHS) const {
  if (this == RHS)
    return true;

  const RecordRecTy *RTy = dyn_cast<RecordRecTy>(RHS);
  if (!RTy)
    return false;

  return llvm::all_of(RTy->getClasses(), [this](Record *TargetClass) {
                                           return isSubClassOf(TargetClass);
                                         });
}

bool RecordRecTy::typeIsA(const RecTy *RHS) const {
  return typeIsConvertibleTo(RHS);
}

static RecordRecTy *resolveRecordTypes(RecordRecTy *T1, RecordRecTy *T2) {
  SmallVector<Record *, 4> CommonSuperClasses;
  SmallVector<Record *, 4> Stack(T1->classes_begin(), T1->classes_end());

  while (!Stack.empty()) {
    Record *R = Stack.pop_back_val();

    if (T2->isSubClassOf(R)) {
      CommonSuperClasses.push_back(R);
    } else {
      R->getDirectSuperClasses(Stack);
    }
  }

  return RecordRecTy::get(T1->getRecordKeeper(), CommonSuperClasses);
}

RecTy *llvm::resolveTypes(RecTy *T1, RecTy *T2) {
  if (T1 == T2)
    return T1;

  if (RecordRecTy *RecTy1 = dyn_cast<RecordRecTy>(T1)) {
    if (RecordRecTy *RecTy2 = dyn_cast<RecordRecTy>(T2))
      return resolveRecordTypes(RecTy1, RecTy2);
  }

  assert(T1 != nullptr && "Invalid record type");
  if (T1->typeIsConvertibleTo(T2))
    return T2;

  assert(T2 != nullptr && "Invalid record type");
  if (T2->typeIsConvertibleTo(T1))
    return T1;

  if (ListRecTy *ListTy1 = dyn_cast<ListRecTy>(T1)) {
    if (ListRecTy *ListTy2 = dyn_cast<ListRecTy>(T2)) {
      RecTy* NewType = resolveTypes(ListTy1->getElementType(),
                                    ListTy2->getElementType());
      if (NewType)
        return NewType->getListTy();
    }
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
//    Initializer implementations
//===----------------------------------------------------------------------===//

void Init::anchor() {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void Init::dump() const { return print(errs()); }
#endif

RecordKeeper &Init::getRecordKeeper() const {
  if (auto *TyInit = dyn_cast<TypedInit>(this))
    return TyInit->getType()->getRecordKeeper();
  if (auto *ArgInit = dyn_cast<ArgumentInit>(this))
    return ArgInit->getRecordKeeper();
  return cast<UnsetInit>(this)->getRecordKeeper();
}

UnsetInit *UnsetInit::get(RecordKeeper &RK) {
  return &RK.getImpl().TheUnsetInit;
}

Init *UnsetInit::getCastTo(RecTy *Ty) const {
  return const_cast<UnsetInit *>(this);
}

Init *UnsetInit::convertInitializerTo(RecTy *Ty) const {
  return const_cast<UnsetInit *>(this);
}

static void ProfileArgumentInit(FoldingSetNodeID &ID, Init *Value,
                                ArgAuxType Aux) {
  auto I = Aux.index();
  ID.AddInteger(I);
  if (I == ArgumentInit::Positional)
    ID.AddInteger(std::get<ArgumentInit::Positional>(Aux));
  if (I == ArgumentInit::Named)
    ID.AddPointer(std::get<ArgumentInit::Named>(Aux));
  ID.AddPointer(Value);
}

void ArgumentInit::Profile(FoldingSetNodeID &ID) const {
  ProfileArgumentInit(ID, Value, Aux);
}

ArgumentInit *ArgumentInit::get(Init *Value, ArgAuxType Aux) {
  FoldingSetNodeID ID;
  ProfileArgumentInit(ID, Value, Aux);

  RecordKeeper &RK = Value->getRecordKeeper();
  detail::RecordKeeperImpl &RKImpl = RK.getImpl();
  void *IP = nullptr;
  if (ArgumentInit *I = RKImpl.TheArgumentInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  ArgumentInit *I = new (RKImpl.Allocator) ArgumentInit(Value, Aux);
  RKImpl.TheArgumentInitPool.InsertNode(I, IP);
  return I;
}

Init *ArgumentInit::resolveReferences(Resolver &R) const {
  Init *NewValue = Value->resolveReferences(R);
  if (NewValue != Value)
    return cloneWithValue(NewValue);

  return const_cast<ArgumentInit *>(this);
}

BitInit *BitInit::get(RecordKeeper &RK, bool V) {
  return V ? &RK.getImpl().TrueBitInit : &RK.getImpl().FalseBitInit;
}

Init *BitInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<BitRecTy>(Ty))
    return const_cast<BitInit *>(this);

  if (isa<IntRecTy>(Ty))
    return IntInit::get(getRecordKeeper(), getValue());

  if (auto *BRT = dyn_cast<BitsRecTy>(Ty)) {
    // Can only convert single bit.
    if (BRT->getNumBits() == 1)
      return BitsInit::get(getRecordKeeper(), const_cast<BitInit *>(this));
  }

  return nullptr;
}

static void
ProfileBitsInit(FoldingSetNodeID &ID, ArrayRef<Init *> Range) {
  ID.AddInteger(Range.size());

  for (Init *I : Range)
    ID.AddPointer(I);
}

BitsInit *BitsInit::get(RecordKeeper &RK, ArrayRef<Init *> Range) {
  FoldingSetNodeID ID;
  ProfileBitsInit(ID, Range);

  detail::RecordKeeperImpl &RKImpl = RK.getImpl();
  void *IP = nullptr;
  if (BitsInit *I = RKImpl.TheBitsInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  void *Mem = RKImpl.Allocator.Allocate(totalSizeToAlloc<Init *>(Range.size()),
                                        alignof(BitsInit));
  BitsInit *I = new (Mem) BitsInit(RK, Range.size());
  std::uninitialized_copy(Range.begin(), Range.end(),
                          I->getTrailingObjects<Init *>());
  RKImpl.TheBitsInitPool.InsertNode(I, IP);
  return I;
}

void BitsInit::Profile(FoldingSetNodeID &ID) const {
  ProfileBitsInit(ID, ArrayRef(getTrailingObjects<Init *>(), NumBits));
}

Init *BitsInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<BitRecTy>(Ty)) {
    if (getNumBits() != 1) return nullptr; // Only accept if just one bit!
    return getBit(0);
  }

  if (auto *BRT = dyn_cast<BitsRecTy>(Ty)) {
    // If the number of bits is right, return it.  Otherwise we need to expand
    // or truncate.
    if (getNumBits() != BRT->getNumBits()) return nullptr;
    return const_cast<BitsInit *>(this);
  }

  if (isa<IntRecTy>(Ty)) {
    int64_t Result = 0;
    for (unsigned i = 0, e = getNumBits(); i != e; ++i)
      if (auto *Bit = dyn_cast<BitInit>(getBit(i)))
        Result |= static_cast<int64_t>(Bit->getValue()) << i;
      else
        return nullptr;
    return IntInit::get(getRecordKeeper(), Result);
  }

  return nullptr;
}

Init *
BitsInit::convertInitializerBitRange(ArrayRef<unsigned> Bits) const {
  SmallVector<Init *, 16> NewBits(Bits.size());

  for (unsigned i = 0, e = Bits.size(); i != e; ++i) {
    if (Bits[i] >= getNumBits())
      return nullptr;
    NewBits[i] = getBit(Bits[i]);
  }
  return BitsInit::get(getRecordKeeper(), NewBits);
}

bool BitsInit::isConcrete() const {
  for (unsigned i = 0, e = getNumBits(); i != e; ++i) {
    if (!getBit(i)->isConcrete())
      return false;
  }
  return true;
}

std::string BitsInit::getAsString() const {
  std::string Result = "{ ";
  for (unsigned i = 0, e = getNumBits(); i != e; ++i) {
    if (i) Result += ", ";
    if (Init *Bit = getBit(e-i-1))
      Result += Bit->getAsString();
    else
      Result += "*";
  }
  return Result + " }";
}

// resolveReferences - If there are any field references that refer to fields
// that have been filled in, we can propagate the values now.
Init *BitsInit::resolveReferences(Resolver &R) const {
  bool Changed = false;
  SmallVector<Init *, 16> NewBits(getNumBits());

  Init *CachedBitVarRef = nullptr;
  Init *CachedBitVarResolved = nullptr;

  for (unsigned i = 0, e = getNumBits(); i != e; ++i) {
    Init *CurBit = getBit(i);
    Init *NewBit = CurBit;

    if (VarBitInit *CurBitVar = dyn_cast<VarBitInit>(CurBit)) {
      if (CurBitVar->getBitVar() != CachedBitVarRef) {
        CachedBitVarRef = CurBitVar->getBitVar();
        CachedBitVarResolved = CachedBitVarRef->resolveReferences(R);
      }
      assert(CachedBitVarResolved && "Unresolved bitvar reference");
      NewBit = CachedBitVarResolved->getBit(CurBitVar->getBitNum());
    } else {
      // getBit(0) implicitly converts int and bits<1> values to bit.
      NewBit = CurBit->resolveReferences(R)->getBit(0);
    }

    if (isa<UnsetInit>(NewBit) && R.keepUnsetBits())
      NewBit = CurBit;
    NewBits[i] = NewBit;
    Changed |= CurBit != NewBit;
  }

  if (Changed)
    return BitsInit::get(getRecordKeeper(), NewBits);

  return const_cast<BitsInit *>(this);
}

IntInit *IntInit::get(RecordKeeper &RK, int64_t V) {
  IntInit *&I = RK.getImpl().TheIntInitPool[V];
  if (!I)
    I = new (RK.getImpl().Allocator) IntInit(RK, V);
  return I;
}

std::string IntInit::getAsString() const {
  return itostr(Value);
}

static bool canFitInBitfield(int64_t Value, unsigned NumBits) {
  // For example, with NumBits == 4, we permit Values from [-7 .. 15].
  return (NumBits >= sizeof(Value) * 8) ||
         (Value >> NumBits == 0) || (Value >> (NumBits-1) == -1);
}

Init *IntInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<IntRecTy>(Ty))
    return const_cast<IntInit *>(this);

  if (isa<BitRecTy>(Ty)) {
    int64_t Val = getValue();
    if (Val != 0 && Val != 1) return nullptr;  // Only accept 0 or 1 for a bit!
    return BitInit::get(getRecordKeeper(), Val != 0);
  }

  if (auto *BRT = dyn_cast<BitsRecTy>(Ty)) {
    int64_t Value = getValue();
    // Make sure this bitfield is large enough to hold the integer value.
    if (!canFitInBitfield(Value, BRT->getNumBits()))
      return nullptr;

    SmallVector<Init *, 16> NewBits(BRT->getNumBits());
    for (unsigned i = 0; i != BRT->getNumBits(); ++i)
      NewBits[i] =
          BitInit::get(getRecordKeeper(), Value & ((i < 64) ? (1LL << i) : 0));

    return BitsInit::get(getRecordKeeper(), NewBits);
  }

  return nullptr;
}

Init *
IntInit::convertInitializerBitRange(ArrayRef<unsigned> Bits) const {
  SmallVector<Init *, 16> NewBits(Bits.size());

  for (unsigned i = 0, e = Bits.size(); i != e; ++i) {
    if (Bits[i] >= 64)
      return nullptr;

    NewBits[i] =
        BitInit::get(getRecordKeeper(), Value & (INT64_C(1) << Bits[i]));
  }
  return BitsInit::get(getRecordKeeper(), NewBits);
}

AnonymousNameInit *AnonymousNameInit::get(RecordKeeper &RK, unsigned V) {
  return new (RK.getImpl().Allocator) AnonymousNameInit(RK, V);
}

StringInit *AnonymousNameInit::getNameInit() const {
  return StringInit::get(getRecordKeeper(), getAsString());
}

std::string AnonymousNameInit::getAsString() const {
  return "anonymous_" + utostr(Value);
}

Init *AnonymousNameInit::resolveReferences(Resolver &R) const {
  auto *Old = const_cast<Init *>(static_cast<const Init *>(this));
  auto *New = R.resolve(Old);
  New = New ? New : Old;
  if (R.isFinal())
    if (auto *Anonymous = dyn_cast<AnonymousNameInit>(New))
      return Anonymous->getNameInit();
  return New;
}

StringInit *StringInit::get(RecordKeeper &RK, StringRef V, StringFormat Fmt) {
  detail::RecordKeeperImpl &RKImpl = RK.getImpl();
  auto &InitMap = Fmt == SF_String ? RKImpl.StringInitStringPool
                                   : RKImpl.StringInitCodePool;
  auto &Entry = *InitMap.insert(std::make_pair(V, nullptr)).first;
  if (!Entry.second)
    Entry.second = new (RKImpl.Allocator) StringInit(RK, Entry.getKey(), Fmt);
  return Entry.second;
}

Init *StringInit::convertInitializerTo(RecTy *Ty) const {
  if (isa<StringRecTy>(Ty))
    return const_cast<StringInit *>(this);

  return nullptr;
}

static void ProfileListInit(FoldingSetNodeID &ID,
                            ArrayRef<Init *> Range,
                            RecTy *EltTy) {
  ID.AddInteger(Range.size());
  ID.AddPointer(EltTy);

  for (Init *I : Range)
    ID.AddPointer(I);
}

ListInit *ListInit::get(ArrayRef<Init *> Range, RecTy *EltTy) {
  FoldingSetNodeID ID;
  ProfileListInit(ID, Range, EltTy);

  detail::RecordKeeperImpl &RK = EltTy->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (ListInit *I = RK.TheListInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  assert(Range.empty() || !isa<TypedInit>(Range[0]) ||
         cast<TypedInit>(Range[0])->getType()->typeIsConvertibleTo(EltTy));

  void *Mem = RK.Allocator.Allocate(totalSizeToAlloc<Init *>(Range.size()),
                                    alignof(ListInit));
  ListInit *I = new (Mem) ListInit(Range.size(), EltTy);
  std::uninitialized_copy(Range.begin(), Range.end(),
                          I->getTrailingObjects<Init *>());
  RK.TheListInitPool.InsertNode(I, IP);
  return I;
}

void ListInit::Profile(FoldingSetNodeID &ID) const {
  RecTy *EltTy = cast<ListRecTy>(getType())->getElementType();

  ProfileListInit(ID, getValues(), EltTy);
}

Init *ListInit::convertInitializerTo(RecTy *Ty) const {
  if (getType() == Ty)
    return const_cast<ListInit*>(this);

  if (auto *LRT = dyn_cast<ListRecTy>(Ty)) {
    SmallVector<Init*, 8> Elements;
    Elements.reserve(getValues().size());

    // Verify that all of the elements of the list are subclasses of the
    // appropriate class!
    bool Changed = false;
    RecTy *ElementType = LRT->getElementType();
    for (Init *I : getValues())
      if (Init *CI = I->convertInitializerTo(ElementType)) {
        Elements.push_back(CI);
        if (CI != I)
          Changed = true;
      } else
        return nullptr;

    if (!Changed)
      return const_cast<ListInit*>(this);
    return ListInit::get(Elements, ElementType);
  }

  return nullptr;
}

Record *ListInit::getElementAsRecord(unsigned i) const {
  assert(i < NumValues && "List element index out of range!");
  DefInit *DI = dyn_cast<DefInit>(getElement(i));
  if (!DI)
    PrintFatalError("Expected record in list!");
  return DI->getDef();
}

Init *ListInit::resolveReferences(Resolver &R) const {
  SmallVector<Init*, 8> Resolved;
  Resolved.reserve(size());
  bool Changed = false;

  for (Init *CurElt : getValues()) {
    Init *E = CurElt->resolveReferences(R);
    Changed |= E != CurElt;
    Resolved.push_back(E);
  }

  if (Changed)
    return ListInit::get(Resolved, getElementType());
  return const_cast<ListInit *>(this);
}

bool ListInit::isComplete() const {
  for (Init *Element : *this) {
    if (!Element->isComplete())
      return false;
  }
  return true;
}

bool ListInit::isConcrete() const {
  for (Init *Element : *this) {
    if (!Element->isConcrete())
      return false;
  }
  return true;
}

std::string ListInit::getAsString() const {
  std::string Result = "[";
  const char *sep = "";
  for (Init *Element : *this) {
    Result += sep;
    sep = ", ";
    Result += Element->getAsString();
  }
  return Result + "]";
}

Init *OpInit::getBit(unsigned Bit) const {
  if (getType() == BitRecTy::get(getRecordKeeper()))
    return const_cast<OpInit*>(this);
  return VarBitInit::get(const_cast<OpInit*>(this), Bit);
}

static void
ProfileUnOpInit(FoldingSetNodeID &ID, unsigned Opcode, Init *Op, RecTy *Type) {
  ID.AddInteger(Opcode);
  ID.AddPointer(Op);
  ID.AddPointer(Type);
}

UnOpInit *UnOpInit::get(UnaryOp Opc, Init *LHS, RecTy *Type) {
  FoldingSetNodeID ID;
  ProfileUnOpInit(ID, Opc, LHS, Type);

  detail::RecordKeeperImpl &RK = Type->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (UnOpInit *I = RK.TheUnOpInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  UnOpInit *I = new (RK.Allocator) UnOpInit(Opc, LHS, Type);
  RK.TheUnOpInitPool.InsertNode(I, IP);
  return I;
}

void UnOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileUnOpInit(ID, getOpcode(), getOperand(), getType());
}

Init *UnOpInit::Fold(Record *CurRec, bool IsFinal) const {
  RecordKeeper &RK = getRecordKeeper();
  switch (getOpcode()) {
  case REPR:
    if (LHS->isConcrete()) {
      // If it is a Record, print the full content.
      if (const auto *Def = dyn_cast<DefInit>(LHS)) {
        std::string S;
        raw_string_ostream OS(S);
        OS << *Def->getDef();
        OS.flush();
        return StringInit::get(RK, S);
      } else {
        // Otherwise, print the value of the variable.
        //
        // NOTE: we could recursively !repr the elements of a list,
        // but that could produce a lot of output when printing a
        // defset.
        return StringInit::get(RK, LHS->getAsString());
      }
    }
    break;
  case TOLOWER:
    if (StringInit *LHSs = dyn_cast<StringInit>(LHS))
      return StringInit::get(RK, LHSs->getValue().lower());
    break;
  case TOUPPER:
    if (StringInit *LHSs = dyn_cast<StringInit>(LHS))
      return StringInit::get(RK, LHSs->getValue().upper());
    break;
  case CAST:
    if (isa<StringRecTy>(getType())) {
      if (StringInit *LHSs = dyn_cast<StringInit>(LHS))
        return LHSs;

      if (DefInit *LHSd = dyn_cast<DefInit>(LHS))
        return StringInit::get(RK, LHSd->getAsString());

      if (IntInit *LHSi = dyn_cast_or_null<IntInit>(
              LHS->convertInitializerTo(IntRecTy::get(RK))))
        return StringInit::get(RK, LHSi->getAsString());

    } else if (isa<RecordRecTy>(getType())) {
      if (StringInit *Name = dyn_cast<StringInit>(LHS)) {
        Record *D = RK.getDef(Name->getValue());
        if (!D && CurRec) {
          // Self-references are allowed, but their resolution is delayed until
          // the final resolve to ensure that we get the correct type for them.
          auto *Anonymous = dyn_cast<AnonymousNameInit>(CurRec->getNameInit());
          if (Name == CurRec->getNameInit() ||
              (Anonymous && Name == Anonymous->getNameInit())) {
            if (!IsFinal)
              break;
            D = CurRec;
          }
        }

        auto PrintFatalErrorHelper = [CurRec](const Twine &T) {
          if (CurRec)
            PrintFatalError(CurRec->getLoc(), T);
          else
            PrintFatalError(T);
        };

        if (!D) {
          if (IsFinal) {
            PrintFatalErrorHelper(Twine("Undefined reference to record: '") +
                                  Name->getValue() + "'\n");
          }
          break;
        }

        DefInit *DI = DefInit::get(D);
        if (!DI->getType()->typeIsA(getType())) {
          PrintFatalErrorHelper(Twine("Expected type '") +
                                getType()->getAsString() + "', got '" +
                                DI->getType()->getAsString() + "' in: " +
                                getAsString() + "\n");
        }
        return DI;
      }
    }

    if (Init *NewInit = LHS->convertInitializerTo(getType()))
      return NewInit;
    break;

  case NOT:
    if (IntInit *LHSi = dyn_cast_or_null<IntInit>(
            LHS->convertInitializerTo(IntRecTy::get(RK))))
      return IntInit::get(RK, LHSi->getValue() ? 0 : 1);
    break;

  case HEAD:
    if (ListInit *LHSl = dyn_cast<ListInit>(LHS)) {
      assert(!LHSl->empty() && "Empty list in head");
      return LHSl->getElement(0);
    }
    break;

  case TAIL:
    if (ListInit *LHSl = dyn_cast<ListInit>(LHS)) {
      assert(!LHSl->empty() && "Empty list in tail");
      // Note the +1.  We can't just pass the result of getValues()
      // directly.
      return ListInit::get(LHSl->getValues().slice(1), LHSl->getElementType());
    }
    break;

  case SIZE:
    if (ListInit *LHSl = dyn_cast<ListInit>(LHS))
      return IntInit::get(RK, LHSl->size());
    if (DagInit *LHSd = dyn_cast<DagInit>(LHS))
      return IntInit::get(RK, LHSd->arg_size());
    if (StringInit *LHSs = dyn_cast<StringInit>(LHS))
      return IntInit::get(RK, LHSs->getValue().size());
    break;

  case EMPTY:
    if (ListInit *LHSl = dyn_cast<ListInit>(LHS))
      return IntInit::get(RK, LHSl->empty());
    if (DagInit *LHSd = dyn_cast<DagInit>(LHS))
      return IntInit::get(RK, LHSd->arg_empty());
    if (StringInit *LHSs = dyn_cast<StringInit>(LHS))
      return IntInit::get(RK, LHSs->getValue().empty());
    break;

  case GETDAGOP:
    if (DagInit *Dag = dyn_cast<DagInit>(LHS)) {
      // TI is not necessarily a def due to the late resolution in multiclasses,
      // but has to be a TypedInit.
      auto *TI = cast<TypedInit>(Dag->getOperator());
      if (!TI->getType()->typeIsA(getType())) {
        PrintFatalError(CurRec->getLoc(),
                        Twine("Expected type '") + getType()->getAsString() +
                            "', got '" + TI->getType()->getAsString() +
                            "' in: " + getAsString() + "\n");
      } else {
        return Dag->getOperator();
      }
    }
    break;

  case LOG2:
    if (IntInit *LHSi = dyn_cast_or_null<IntInit>(
            LHS->convertInitializerTo(IntRecTy::get(RK)))) {
      int64_t LHSv = LHSi->getValue();
      if (LHSv <= 0) {
        PrintFatalError(CurRec->getLoc(),
                        "Illegal operation: logtwo is undefined "
                        "on arguments less than or equal to 0");
      } else {
        uint64_t Log = Log2_64(LHSv);
        assert(Log <= INT64_MAX &&
               "Log of an int64_t must be smaller than INT64_MAX");
        return IntInit::get(RK, static_cast<int64_t>(Log));
      }
    }
    break;
  }
  return const_cast<UnOpInit *>(this);
}

Init *UnOpInit::resolveReferences(Resolver &R) const {
  Init *lhs = LHS->resolveReferences(R);

  if (LHS != lhs || (R.isFinal() && getOpcode() == CAST))
    return (UnOpInit::get(getOpcode(), lhs, getType()))
        ->Fold(R.getCurrentRecord(), R.isFinal());
  return const_cast<UnOpInit *>(this);
}

std::string UnOpInit::getAsString() const {
  std::string Result;
  switch (getOpcode()) {
  case CAST: Result = "!cast<" + getType()->getAsString() + ">"; break;
  case NOT: Result = "!not"; break;
  case HEAD: Result = "!head"; break;
  case TAIL: Result = "!tail"; break;
  case SIZE: Result = "!size"; break;
  case EMPTY: Result = "!empty"; break;
  case GETDAGOP: Result = "!getdagop"; break;
  case LOG2 : Result = "!logtwo"; break;
  case REPR:
    Result = "!repr";
    break;
  case TOLOWER:
    Result = "!tolower";
    break;
  case TOUPPER:
    Result = "!toupper";
    break;
  }
  return Result + "(" + LHS->getAsString() + ")";
}

static void
ProfileBinOpInit(FoldingSetNodeID &ID, unsigned Opcode, Init *LHS, Init *RHS,
                 RecTy *Type) {
  ID.AddInteger(Opcode);
  ID.AddPointer(LHS);
  ID.AddPointer(RHS);
  ID.AddPointer(Type);
}

BinOpInit *BinOpInit::get(BinaryOp Opc, Init *LHS, Init *RHS, RecTy *Type) {
  FoldingSetNodeID ID;
  ProfileBinOpInit(ID, Opc, LHS, RHS, Type);

  detail::RecordKeeperImpl &RK = LHS->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (BinOpInit *I = RK.TheBinOpInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  BinOpInit *I = new (RK.Allocator) BinOpInit(Opc, LHS, RHS, Type);
  RK.TheBinOpInitPool.InsertNode(I, IP);
  return I;
}

void BinOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileBinOpInit(ID, getOpcode(), getLHS(), getRHS(), getType());
}

static StringInit *ConcatStringInits(const StringInit *I0,
                                     const StringInit *I1) {
  SmallString<80> Concat(I0->getValue());
  Concat.append(I1->getValue());
  return StringInit::get(
      I0->getRecordKeeper(), Concat,
      StringInit::determineFormat(I0->getFormat(), I1->getFormat()));
}

static StringInit *interleaveStringList(const ListInit *List,
                                        const StringInit *Delim) {
  if (List->size() == 0)
    return StringInit::get(List->getRecordKeeper(), "");
  StringInit *Element = dyn_cast<StringInit>(List->getElement(0));
  if (!Element)
    return nullptr;
  SmallString<80> Result(Element->getValue());
  StringInit::StringFormat Fmt = StringInit::SF_String;

  for (unsigned I = 1, E = List->size(); I < E; ++I) {
    Result.append(Delim->getValue());
    StringInit *Element = dyn_cast<StringInit>(List->getElement(I));
    if (!Element)
      return nullptr;
    Result.append(Element->getValue());
    Fmt = StringInit::determineFormat(Fmt, Element->getFormat());
  }
  return StringInit::get(List->getRecordKeeper(), Result, Fmt);
}

static StringInit *interleaveIntList(const ListInit *List,
                                     const StringInit *Delim) {
  RecordKeeper &RK = List->getRecordKeeper();
  if (List->size() == 0)
    return StringInit::get(RK, "");
  IntInit *Element = dyn_cast_or_null<IntInit>(
      List->getElement(0)->convertInitializerTo(IntRecTy::get(RK)));
  if (!Element)
    return nullptr;
  SmallString<80> Result(Element->getAsString());

  for (unsigned I = 1, E = List->size(); I < E; ++I) {
    Result.append(Delim->getValue());
    IntInit *Element = dyn_cast_or_null<IntInit>(
        List->getElement(I)->convertInitializerTo(IntRecTy::get(RK)));
    if (!Element)
      return nullptr;
    Result.append(Element->getAsString());
  }
  return StringInit::get(RK, Result);
}

Init *BinOpInit::getStrConcat(Init *I0, Init *I1) {
  // Shortcut for the common case of concatenating two strings.
  if (const StringInit *I0s = dyn_cast<StringInit>(I0))
    if (const StringInit *I1s = dyn_cast<StringInit>(I1))
      return ConcatStringInits(I0s, I1s);
  return BinOpInit::get(BinOpInit::STRCONCAT, I0, I1,
                        StringRecTy::get(I0->getRecordKeeper()));
}

static ListInit *ConcatListInits(const ListInit *LHS,
                                 const ListInit *RHS) {
  SmallVector<Init *, 8> Args;
  llvm::append_range(Args, *LHS);
  llvm::append_range(Args, *RHS);
  return ListInit::get(Args, LHS->getElementType());
}

Init *BinOpInit::getListConcat(TypedInit *LHS, Init *RHS) {
  assert(isa<ListRecTy>(LHS->getType()) && "First arg must be a list");

  // Shortcut for the common case of concatenating two lists.
  if (const ListInit *LHSList = dyn_cast<ListInit>(LHS))
    if (const ListInit *RHSList = dyn_cast<ListInit>(RHS))
      return ConcatListInits(LHSList, RHSList);
  return BinOpInit::get(BinOpInit::LISTCONCAT, LHS, RHS, LHS->getType());
}

std::optional<bool> BinOpInit::CompareInit(unsigned Opc, Init *LHS,
                                           Init *RHS) const {
  // First see if we have two bit, bits, or int.
  IntInit *LHSi = dyn_cast_or_null<IntInit>(
      LHS->convertInitializerTo(IntRecTy::get(getRecordKeeper())));
  IntInit *RHSi = dyn_cast_or_null<IntInit>(
      RHS->convertInitializerTo(IntRecTy::get(getRecordKeeper())));

  if (LHSi && RHSi) {
    bool Result;
    switch (Opc) {
    case EQ:
      Result = LHSi->getValue() == RHSi->getValue();
      break;
    case NE:
      Result = LHSi->getValue() != RHSi->getValue();
      break;
    case LE:
      Result = LHSi->getValue() <= RHSi->getValue();
      break;
    case LT:
      Result = LHSi->getValue() < RHSi->getValue();
      break;
    case GE:
      Result = LHSi->getValue() >= RHSi->getValue();
      break;
    case GT:
      Result = LHSi->getValue() > RHSi->getValue();
      break;
    default:
      llvm_unreachable("unhandled comparison");
    }
    return Result;
  }

  // Next try strings.
  StringInit *LHSs = dyn_cast<StringInit>(LHS);
  StringInit *RHSs = dyn_cast<StringInit>(RHS);

  if (LHSs && RHSs) {
    bool Result;
    switch (Opc) {
    case EQ:
      Result = LHSs->getValue() == RHSs->getValue();
      break;
    case NE:
      Result = LHSs->getValue() != RHSs->getValue();
      break;
    case LE:
      Result = LHSs->getValue() <= RHSs->getValue();
      break;
    case LT:
      Result = LHSs->getValue() < RHSs->getValue();
      break;
    case GE:
      Result = LHSs->getValue() >= RHSs->getValue();
      break;
    case GT:
      Result = LHSs->getValue() > RHSs->getValue();
      break;
    default:
      llvm_unreachable("unhandled comparison");
    }
    return Result;
  }

  // Finally, !eq and !ne can be used with records.
  if (Opc == EQ || Opc == NE) {
    DefInit *LHSd = dyn_cast<DefInit>(LHS);
    DefInit *RHSd = dyn_cast<DefInit>(RHS);
    if (LHSd && RHSd)
      return (Opc == EQ) ? LHSd == RHSd : LHSd != RHSd;
  }

  return std::nullopt;
}

static std::optional<unsigned> getDagArgNoByKey(DagInit *Dag, Init *Key,
                                                std::string &Error) {
  // Accessor by index
  if (IntInit *Idx = dyn_cast<IntInit>(Key)) {
    int64_t Pos = Idx->getValue();
    if (Pos < 0) {
      // The index is negative.
      Error =
          (Twine("index ") + std::to_string(Pos) + Twine(" is negative")).str();
      return std::nullopt;
    }
    if (Pos >= Dag->getNumArgs()) {
      // The index is out-of-range.
      Error = (Twine("index ") + std::to_string(Pos) +
               " is out of range (dag has " +
               std::to_string(Dag->getNumArgs()) + " arguments)")
                  .str();
      return std::nullopt;
    }
    return Pos;
  }
  assert(isa<StringInit>(Key));
  // Accessor by name
  StringInit *Name = dyn_cast<StringInit>(Key);
  auto ArgNo = Dag->getArgNo(Name->getValue());
  if (!ArgNo) {
    // The key is not found.
    Error = (Twine("key '") + Name->getValue() + Twine("' is not found")).str();
    return std::nullopt;
  }
  return *ArgNo;
}

Init *BinOpInit::Fold(Record *CurRec) const {
  switch (getOpcode()) {
  case CONCAT: {
    DagInit *LHSs = dyn_cast<DagInit>(LHS);
    DagInit *RHSs = dyn_cast<DagInit>(RHS);
    if (LHSs && RHSs) {
      DefInit *LOp = dyn_cast<DefInit>(LHSs->getOperator());
      DefInit *ROp = dyn_cast<DefInit>(RHSs->getOperator());
      if ((!LOp && !isa<UnsetInit>(LHSs->getOperator())) ||
          (!ROp && !isa<UnsetInit>(RHSs->getOperator())))
        break;
      if (LOp && ROp && LOp->getDef() != ROp->getDef()) {
        PrintFatalError(Twine("Concatenated Dag operators do not match: '") +
                        LHSs->getAsString() + "' vs. '" + RHSs->getAsString() +
                        "'");
      }
      Init *Op = LOp ? LOp : ROp;
      if (!Op)
        Op = UnsetInit::get(getRecordKeeper());

      SmallVector<Init*, 8> Args;
      SmallVector<StringInit*, 8> ArgNames;
      for (unsigned i = 0, e = LHSs->getNumArgs(); i != e; ++i) {
        Args.push_back(LHSs->getArg(i));
        ArgNames.push_back(LHSs->getArgName(i));
      }
      for (unsigned i = 0, e = RHSs->getNumArgs(); i != e; ++i) {
        Args.push_back(RHSs->getArg(i));
        ArgNames.push_back(RHSs->getArgName(i));
      }
      return DagInit::get(Op, nullptr, Args, ArgNames);
    }
    break;
  }
  case LISTCONCAT: {
    ListInit *LHSs = dyn_cast<ListInit>(LHS);
    ListInit *RHSs = dyn_cast<ListInit>(RHS);
    if (LHSs && RHSs) {
      SmallVector<Init *, 8> Args;
      llvm::append_range(Args, *LHSs);
      llvm::append_range(Args, *RHSs);
      return ListInit::get(Args, LHSs->getElementType());
    }
    break;
  }
  case LISTSPLAT: {
    TypedInit *Value = dyn_cast<TypedInit>(LHS);
    IntInit *Size = dyn_cast<IntInit>(RHS);
    if (Value && Size) {
      SmallVector<Init *, 8> Args(Size->getValue(), Value);
      return ListInit::get(Args, Value->getType());
    }
    break;
  }
  case LISTREMOVE: {
    ListInit *LHSs = dyn_cast<ListInit>(LHS);
    ListInit *RHSs = dyn_cast<ListInit>(RHS);
    if (LHSs && RHSs) {
      SmallVector<Init *, 8> Args;
      for (Init *EltLHS : *LHSs) {
        bool Found = false;
        for (Init *EltRHS : *RHSs) {
          if (std::optional<bool> Result = CompareInit(EQ, EltLHS, EltRHS)) {
            if (*Result) {
              Found = true;
              break;
            }
          }
        }
        if (!Found)
          Args.push_back(EltLHS);
      }
      return ListInit::get(Args, LHSs->getElementType());
    }
    break;
  }
  case LISTELEM: {
    auto *TheList = dyn_cast<ListInit>(LHS);
    auto *Idx = dyn_cast<IntInit>(RHS);
    if (!TheList || !Idx)
      break;
    auto i = Idx->getValue();
    if (i < 0 || i >= (ssize_t)TheList->size())
      break;
    return TheList->getElement(i);
  }
  case LISTSLICE: {
    auto *TheList = dyn_cast<ListInit>(LHS);
    auto *SliceIdxs = dyn_cast<ListInit>(RHS);
    if (!TheList || !SliceIdxs)
      break;
    SmallVector<Init *, 8> Args;
    Args.reserve(SliceIdxs->size());
    for (auto *I : *SliceIdxs) {
      auto *II = dyn_cast<IntInit>(I);
      if (!II)
        goto unresolved;
      auto i = II->getValue();
      if (i < 0 || i >= (ssize_t)TheList->size())
        goto unresolved;
      Args.push_back(TheList->getElement(i));
    }
    return ListInit::get(Args, TheList->getElementType());
  }
  case RANGEC: {
    auto *LHSi = dyn_cast<IntInit>(LHS);
    auto *RHSi = dyn_cast<IntInit>(RHS);
    if (!LHSi || !RHSi)
      break;

    auto Start = LHSi->getValue();
    auto End = RHSi->getValue();
    SmallVector<Init *, 8> Args;
    if (getOpcode() == RANGEC) {
      // Closed interval
      if (Start <= End) {
        // Ascending order
        Args.reserve(End - Start + 1);
        for (auto i = Start; i <= End; ++i)
          Args.push_back(IntInit::get(getRecordKeeper(), i));
      } else {
        // Descending order
        Args.reserve(Start - End + 1);
        for (auto i = Start; i >= End; --i)
          Args.push_back(IntInit::get(getRecordKeeper(), i));
      }
    } else if (Start < End) {
      // Half-open interval (excludes `End`)
      Args.reserve(End - Start);
      for (auto i = Start; i < End; ++i)
        Args.push_back(IntInit::get(getRecordKeeper(), i));
    } else {
      // Empty set
    }
    return ListInit::get(Args, LHSi->getType());
  }
  case STRCONCAT: {
    StringInit *LHSs = dyn_cast<StringInit>(LHS);
    StringInit *RHSs = dyn_cast<StringInit>(RHS);
    if (LHSs && RHSs)
      return ConcatStringInits(LHSs, RHSs);
    break;
  }
  case INTERLEAVE: {
    ListInit *List = dyn_cast<ListInit>(LHS);
    StringInit *Delim = dyn_cast<StringInit>(RHS);
    if (List && Delim) {
      StringInit *Result;
      if (isa<StringRecTy>(List->getElementType()))
        Result = interleaveStringList(List, Delim);
      else
        Result = interleaveIntList(List, Delim);
      if (Result)
        return Result;
    }
    break;
  }
  case EQ:
  case NE:
  case LE:
  case LT:
  case GE:
  case GT: {
    if (std::optional<bool> Result = CompareInit(getOpcode(), LHS, RHS))
      return BitInit::get(getRecordKeeper(), *Result);
    break;
  }
  case GETDAGARG: {
    DagInit *Dag = dyn_cast<DagInit>(LHS);
    if (Dag && isa<IntInit, StringInit>(RHS)) {
      std::string Error;
      auto ArgNo = getDagArgNoByKey(Dag, RHS, Error);
      if (!ArgNo)
        PrintFatalError(CurRec->getLoc(), "!getdagarg " + Error);

      assert(*ArgNo < Dag->getNumArgs());

      Init *Arg = Dag->getArg(*ArgNo);
      if (auto *TI = dyn_cast<TypedInit>(Arg))
        if (!TI->getType()->typeIsConvertibleTo(getType()))
          return UnsetInit::get(Dag->getRecordKeeper());
      return Arg;
    }
    break;
  }
  case GETDAGNAME: {
    DagInit *Dag = dyn_cast<DagInit>(LHS);
    IntInit *Idx = dyn_cast<IntInit>(RHS);
    if (Dag && Idx) {
      int64_t Pos = Idx->getValue();
      if (Pos < 0 || Pos >= Dag->getNumArgs()) {
        // The index is out-of-range.
        PrintError(CurRec->getLoc(),
                   Twine("!getdagname index is out of range 0...") +
                       std::to_string(Dag->getNumArgs() - 1) + ": " +
                       std::to_string(Pos));
      }
      Init *ArgName = Dag->getArgName(Pos);
      if (!ArgName)
        return UnsetInit::get(getRecordKeeper());
      return ArgName;
    }
    break;
  }
  case SETDAGOP: {
    DagInit *Dag = dyn_cast<DagInit>(LHS);
    DefInit *Op = dyn_cast<DefInit>(RHS);
    if (Dag && Op) {
      SmallVector<Init*, 8> Args;
      SmallVector<StringInit*, 8> ArgNames;
      for (unsigned i = 0, e = Dag->getNumArgs(); i != e; ++i) {
        Args.push_back(Dag->getArg(i));
        ArgNames.push_back(Dag->getArgName(i));
      }
      return DagInit::get(Op, nullptr, Args, ArgNames);
    }
    break;
  }
  case ADD:
  case SUB:
  case MUL:
  case DIV:
  case AND:
  case OR:
  case XOR:
  case SHL:
  case SRA:
  case SRL: {
    IntInit *LHSi = dyn_cast_or_null<IntInit>(
        LHS->convertInitializerTo(IntRecTy::get(getRecordKeeper())));
    IntInit *RHSi = dyn_cast_or_null<IntInit>(
        RHS->convertInitializerTo(IntRecTy::get(getRecordKeeper())));
    if (LHSi && RHSi) {
      int64_t LHSv = LHSi->getValue(), RHSv = RHSi->getValue();
      int64_t Result;
      switch (getOpcode()) {
      default: llvm_unreachable("Bad opcode!");
      case ADD: Result = LHSv + RHSv; break;
      case SUB: Result = LHSv - RHSv; break;
      case MUL: Result = LHSv * RHSv; break;
      case DIV:
        if (RHSv == 0)
          PrintFatalError(CurRec->getLoc(),
                          "Illegal operation: division by zero");
        else if (LHSv == INT64_MIN && RHSv == -1)
          PrintFatalError(CurRec->getLoc(),
                          "Illegal operation: INT64_MIN / -1");
        else
          Result = LHSv / RHSv;
        break;
      case AND: Result = LHSv & RHSv; break;
      case OR:  Result = LHSv | RHSv; break;
      case XOR: Result = LHSv ^ RHSv; break;
      case SHL: Result = (uint64_t)LHSv << (uint64_t)RHSv; break;
      case SRA: Result = LHSv >> RHSv; break;
      case SRL: Result = (uint64_t)LHSv >> (uint64_t)RHSv; break;
      }
      return IntInit::get(getRecordKeeper(), Result);
    }
    break;
  }
  }
unresolved:
  return const_cast<BinOpInit *>(this);
}

Init *BinOpInit::resolveReferences(Resolver &R) const {
  Init *lhs = LHS->resolveReferences(R);
  Init *rhs = RHS->resolveReferences(R);

  if (LHS != lhs || RHS != rhs)
    return (BinOpInit::get(getOpcode(), lhs, rhs, getType()))
        ->Fold(R.getCurrentRecord());
  return const_cast<BinOpInit *>(this);
}

std::string BinOpInit::getAsString() const {
  std::string Result;
  switch (getOpcode()) {
  case LISTELEM:
  case LISTSLICE:
    return LHS->getAsString() + "[" + RHS->getAsString() + "]";
  case RANGEC:
    return LHS->getAsString() + "..." + RHS->getAsString();
  case CONCAT: Result = "!con"; break;
  case ADD: Result = "!add"; break;
  case SUB: Result = "!sub"; break;
  case MUL: Result = "!mul"; break;
  case DIV: Result = "!div"; break;
  case AND: Result = "!and"; break;
  case OR: Result = "!or"; break;
  case XOR: Result = "!xor"; break;
  case SHL: Result = "!shl"; break;
  case SRA: Result = "!sra"; break;
  case SRL: Result = "!srl"; break;
  case EQ: Result = "!eq"; break;
  case NE: Result = "!ne"; break;
  case LE: Result = "!le"; break;
  case LT: Result = "!lt"; break;
  case GE: Result = "!ge"; break;
  case GT: Result = "!gt"; break;
  case LISTCONCAT: Result = "!listconcat"; break;
  case LISTSPLAT: Result = "!listsplat"; break;
  case LISTREMOVE:
    Result = "!listremove";
    break;
  case STRCONCAT: Result = "!strconcat"; break;
  case INTERLEAVE: Result = "!interleave"; break;
  case SETDAGOP: Result = "!setdagop"; break;
  case GETDAGARG:
    Result = "!getdagarg<" + getType()->getAsString() + ">";
    break;
  case GETDAGNAME:
    Result = "!getdagname";
    break;
  }
  return Result + "(" + LHS->getAsString() + ", " + RHS->getAsString() + ")";
}

static void
ProfileTernOpInit(FoldingSetNodeID &ID, unsigned Opcode, Init *LHS, Init *MHS,
                  Init *RHS, RecTy *Type) {
  ID.AddInteger(Opcode);
  ID.AddPointer(LHS);
  ID.AddPointer(MHS);
  ID.AddPointer(RHS);
  ID.AddPointer(Type);
}

TernOpInit *TernOpInit::get(TernaryOp Opc, Init *LHS, Init *MHS, Init *RHS,
                            RecTy *Type) {
  FoldingSetNodeID ID;
  ProfileTernOpInit(ID, Opc, LHS, MHS, RHS, Type);

  detail::RecordKeeperImpl &RK = LHS->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (TernOpInit *I = RK.TheTernOpInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  TernOpInit *I = new (RK.Allocator) TernOpInit(Opc, LHS, MHS, RHS, Type);
  RK.TheTernOpInitPool.InsertNode(I, IP);
  return I;
}

void TernOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileTernOpInit(ID, getOpcode(), getLHS(), getMHS(), getRHS(), getType());
}

static Init *ItemApply(Init *LHS, Init *MHSe, Init *RHS, Record *CurRec) {
  MapResolver R(CurRec);
  R.set(LHS, MHSe);
  return RHS->resolveReferences(R);
}

static Init *ForeachDagApply(Init *LHS, DagInit *MHSd, Init *RHS,
                             Record *CurRec) {
  bool Change = false;
  Init *Val = ItemApply(LHS, MHSd->getOperator(), RHS, CurRec);
  if (Val != MHSd->getOperator())
    Change = true;

  SmallVector<std::pair<Init *, StringInit *>, 8> NewArgs;
  for (unsigned int i = 0; i < MHSd->getNumArgs(); ++i) {
    Init *Arg = MHSd->getArg(i);
    Init *NewArg;
    StringInit *ArgName = MHSd->getArgName(i);

    if (DagInit *Argd = dyn_cast<DagInit>(Arg))
      NewArg = ForeachDagApply(LHS, Argd, RHS, CurRec);
    else
      NewArg = ItemApply(LHS, Arg, RHS, CurRec);

    NewArgs.push_back(std::make_pair(NewArg, ArgName));
    if (Arg != NewArg)
      Change = true;
  }

  if (Change)
    return DagInit::get(Val, nullptr, NewArgs);
  return MHSd;
}

// Applies RHS to all elements of MHS, using LHS as a temp variable.
static Init *ForeachHelper(Init *LHS, Init *MHS, Init *RHS, RecTy *Type,
                           Record *CurRec) {
  if (DagInit *MHSd = dyn_cast<DagInit>(MHS))
    return ForeachDagApply(LHS, MHSd, RHS, CurRec);

  if (ListInit *MHSl = dyn_cast<ListInit>(MHS)) {
    SmallVector<Init *, 8> NewList(MHSl->begin(), MHSl->end());

    for (Init *&Item : NewList) {
      Init *NewItem = ItemApply(LHS, Item, RHS, CurRec);
      if (NewItem != Item)
        Item = NewItem;
    }
    return ListInit::get(NewList, cast<ListRecTy>(Type)->getElementType());
  }

  return nullptr;
}

// Evaluates RHS for all elements of MHS, using LHS as a temp variable.
// Creates a new list with the elements that evaluated to true.
static Init *FilterHelper(Init *LHS, Init *MHS, Init *RHS, RecTy *Type,
                          Record *CurRec) {
  if (ListInit *MHSl = dyn_cast<ListInit>(MHS)) {
    SmallVector<Init *, 8> NewList;

    for (Init *Item : MHSl->getValues()) {
      Init *Include = ItemApply(LHS, Item, RHS, CurRec);
      if (!Include)
        return nullptr;
      if (IntInit *IncludeInt =
              dyn_cast_or_null<IntInit>(Include->convertInitializerTo(
                  IntRecTy::get(LHS->getRecordKeeper())))) {
        if (IncludeInt->getValue())
          NewList.push_back(Item);
      } else {
        return nullptr;
      }
    }
    return ListInit::get(NewList, cast<ListRecTy>(Type)->getElementType());
  }

  return nullptr;
}

Init *TernOpInit::Fold(Record *CurRec) const {
  RecordKeeper &RK = getRecordKeeper();
  switch (getOpcode()) {
  case SUBST: {
    DefInit *LHSd = dyn_cast<DefInit>(LHS);
    VarInit *LHSv = dyn_cast<VarInit>(LHS);
    StringInit *LHSs = dyn_cast<StringInit>(LHS);

    DefInit *MHSd = dyn_cast<DefInit>(MHS);
    VarInit *MHSv = dyn_cast<VarInit>(MHS);
    StringInit *MHSs = dyn_cast<StringInit>(MHS);

    DefInit *RHSd = dyn_cast<DefInit>(RHS);
    VarInit *RHSv = dyn_cast<VarInit>(RHS);
    StringInit *RHSs = dyn_cast<StringInit>(RHS);

    if (LHSd && MHSd && RHSd) {
      Record *Val = RHSd->getDef();
      if (LHSd->getAsString() == RHSd->getAsString())
        Val = MHSd->getDef();
      return DefInit::get(Val);
    }
    if (LHSv && MHSv && RHSv) {
      std::string Val = std::string(RHSv->getName());
      if (LHSv->getAsString() == RHSv->getAsString())
        Val = std::string(MHSv->getName());
      return VarInit::get(Val, getType());
    }
    if (LHSs && MHSs && RHSs) {
      std::string Val = std::string(RHSs->getValue());

      std::string::size_type found;
      std::string::size_type idx = 0;
      while (true) {
        found = Val.find(std::string(LHSs->getValue()), idx);
        if (found == std::string::npos)
          break;
        Val.replace(found, LHSs->getValue().size(),
                    std::string(MHSs->getValue()));
        idx = found + MHSs->getValue().size();
      }

      return StringInit::get(RK, Val);
    }
    break;
  }

  case FOREACH: {
    if (Init *Result = ForeachHelper(LHS, MHS, RHS, getType(), CurRec))
      return Result;
    break;
  }

  case FILTER: {
    if (Init *Result = FilterHelper(LHS, MHS, RHS, getType(), CurRec))
      return Result;
    break;
  }

  case IF: {
    if (IntInit *LHSi = dyn_cast_or_null<IntInit>(
            LHS->convertInitializerTo(IntRecTy::get(RK)))) {
      if (LHSi->getValue())
        return MHS;
      return RHS;
    }
    break;
  }

  case DAG: {
    ListInit *MHSl = dyn_cast<ListInit>(MHS);
    ListInit *RHSl = dyn_cast<ListInit>(RHS);
    bool MHSok = MHSl || isa<UnsetInit>(MHS);
    bool RHSok = RHSl || isa<UnsetInit>(RHS);

    if (isa<UnsetInit>(MHS) && isa<UnsetInit>(RHS))
      break; // Typically prevented by the parser, but might happen with template args

    if (MHSok && RHSok && (!MHSl || !RHSl || MHSl->size() == RHSl->size())) {
      SmallVector<std::pair<Init *, StringInit *>, 8> Children;
      unsigned Size = MHSl ? MHSl->size() : RHSl->size();
      for (unsigned i = 0; i != Size; ++i) {
        Init *Node = MHSl ? MHSl->getElement(i) : UnsetInit::get(RK);
        Init *Name = RHSl ? RHSl->getElement(i) : UnsetInit::get(RK);
        if (!isa<StringInit>(Name) && !isa<UnsetInit>(Name))
          return const_cast<TernOpInit *>(this);
        Children.emplace_back(Node, dyn_cast<StringInit>(Name));
      }
      return DagInit::get(LHS, nullptr, Children);
    }
    break;
  }

  case RANGE: {
    auto *LHSi = dyn_cast<IntInit>(LHS);
    auto *MHSi = dyn_cast<IntInit>(MHS);
    auto *RHSi = dyn_cast<IntInit>(RHS);
    if (!LHSi || !MHSi || !RHSi)
      break;

    auto Start = LHSi->getValue();
    auto End = MHSi->getValue();
    auto Step = RHSi->getValue();
    if (Step == 0)
      PrintError(CurRec->getLoc(), "Step of !range can't be 0");

    SmallVector<Init *, 8> Args;
    if (Start < End && Step > 0) {
      Args.reserve((End - Start) / Step);
      for (auto I = Start; I < End; I += Step)
        Args.push_back(IntInit::get(getRecordKeeper(), I));
    } else if (Start > End && Step < 0) {
      Args.reserve((Start - End) / -Step);
      for (auto I = Start; I > End; I += Step)
        Args.push_back(IntInit::get(getRecordKeeper(), I));
    } else {
      // Empty set
    }
    return ListInit::get(Args, LHSi->getType());
  }

  case SUBSTR: {
    StringInit *LHSs = dyn_cast<StringInit>(LHS);
    IntInit *MHSi = dyn_cast<IntInit>(MHS);
    IntInit *RHSi = dyn_cast<IntInit>(RHS);
    if (LHSs && MHSi && RHSi) {
      int64_t StringSize = LHSs->getValue().size();
      int64_t Start = MHSi->getValue();
      int64_t Length = RHSi->getValue();
      if (Start < 0 || Start > StringSize)
        PrintError(CurRec->getLoc(),
                   Twine("!substr start position is out of range 0...") +
                       std::to_string(StringSize) + ": " +
                       std::to_string(Start));
      if (Length < 0)
        PrintError(CurRec->getLoc(), "!substr length must be nonnegative");
      return StringInit::get(RK, LHSs->getValue().substr(Start, Length),
                             LHSs->getFormat());
    }
    break;
  }

  case FIND: {
    StringInit *LHSs = dyn_cast<StringInit>(LHS);
    StringInit *MHSs = dyn_cast<StringInit>(MHS);
    IntInit *RHSi = dyn_cast<IntInit>(RHS);
    if (LHSs && MHSs && RHSi) {
      int64_t SourceSize = LHSs->getValue().size();
      int64_t Start = RHSi->getValue();
      if (Start < 0 || Start > SourceSize)
        PrintError(CurRec->getLoc(),
                   Twine("!find start position is out of range 0...") +
                       std::to_string(SourceSize) + ": " +
                       std::to_string(Start));
      auto I = LHSs->getValue().find(MHSs->getValue(), Start);
      if (I == std::string::npos)
        return IntInit::get(RK, -1);
      return IntInit::get(RK, I);
    }
    break;
  }

  case SETDAGARG: {
    DagInit *Dag = dyn_cast<DagInit>(LHS);
    if (Dag && isa<IntInit, StringInit>(MHS)) {
      std::string Error;
      auto ArgNo = getDagArgNoByKey(Dag, MHS, Error);
      if (!ArgNo)
        PrintFatalError(CurRec->getLoc(), "!setdagarg " + Error);

      assert(*ArgNo < Dag->getNumArgs());

      SmallVector<Init *, 8> Args(Dag->getArgs());
      SmallVector<StringInit *, 8> Names(Dag->getArgNames());
      Args[*ArgNo] = RHS;
      return DagInit::get(Dag->getOperator(), Dag->getName(), Args, Names);
    }
    break;
  }

  case SETDAGNAME: {
    DagInit *Dag = dyn_cast<DagInit>(LHS);
    if (Dag && isa<IntInit, StringInit>(MHS)) {
      std::string Error;
      auto ArgNo = getDagArgNoByKey(Dag, MHS, Error);
      if (!ArgNo)
        PrintFatalError(CurRec->getLoc(), "!setdagname " + Error);

      assert(*ArgNo < Dag->getNumArgs());

      SmallVector<Init *, 8> Args(Dag->getArgs());
      SmallVector<StringInit *, 8> Names(Dag->getArgNames());
      Names[*ArgNo] = dyn_cast<StringInit>(RHS);
      return DagInit::get(Dag->getOperator(), Dag->getName(), Args, Names);
    }
    break;
  }
  }

  return const_cast<TernOpInit *>(this);
}

Init *TernOpInit::resolveReferences(Resolver &R) const {
  Init *lhs = LHS->resolveReferences(R);

  if (getOpcode() == IF && lhs != LHS) {
    if (IntInit *Value = dyn_cast_or_null<IntInit>(
            lhs->convertInitializerTo(IntRecTy::get(getRecordKeeper())))) {
      // Short-circuit
      if (Value->getValue())
        return MHS->resolveReferences(R);
      return RHS->resolveReferences(R);
    }
  }

  Init *mhs = MHS->resolveReferences(R);
  Init *rhs;

  if (getOpcode() == FOREACH || getOpcode() == FILTER) {
    ShadowResolver SR(R);
    SR.addShadow(lhs);
    rhs = RHS->resolveReferences(SR);
  } else {
    rhs = RHS->resolveReferences(R);
  }

  if (LHS != lhs || MHS != mhs || RHS != rhs)
    return (TernOpInit::get(getOpcode(), lhs, mhs, rhs, getType()))
        ->Fold(R.getCurrentRecord());
  return const_cast<TernOpInit *>(this);
}

std::string TernOpInit::getAsString() const {
  std::string Result;
  bool UnquotedLHS = false;
  switch (getOpcode()) {
  case DAG: Result = "!dag"; break;
  case FILTER: Result = "!filter"; UnquotedLHS = true; break;
  case FOREACH: Result = "!foreach"; UnquotedLHS = true; break;
  case IF: Result = "!if"; break;
  case RANGE:
    Result = "!range";
    break;
  case SUBST: Result = "!subst"; break;
  case SUBSTR: Result = "!substr"; break;
  case FIND: Result = "!find"; break;
  case SETDAGARG:
    Result = "!setdagarg";
    break;
  case SETDAGNAME:
    Result = "!setdagname";
    break;
  }
  return (Result + "(" +
          (UnquotedLHS ? LHS->getAsUnquotedString() : LHS->getAsString()) +
          ", " + MHS->getAsString() + ", " + RHS->getAsString() + ")");
}

static void ProfileFoldOpInit(FoldingSetNodeID &ID, Init *Start, Init *List,
                              Init *A, Init *B, Init *Expr, RecTy *Type) {
  ID.AddPointer(Start);
  ID.AddPointer(List);
  ID.AddPointer(A);
  ID.AddPointer(B);
  ID.AddPointer(Expr);
  ID.AddPointer(Type);
}

FoldOpInit *FoldOpInit::get(Init *Start, Init *List, Init *A, Init *B,
                            Init *Expr, RecTy *Type) {
  FoldingSetNodeID ID;
  ProfileFoldOpInit(ID, Start, List, A, B, Expr, Type);

  detail::RecordKeeperImpl &RK = Start->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (FoldOpInit *I = RK.TheFoldOpInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  FoldOpInit *I = new (RK.Allocator) FoldOpInit(Start, List, A, B, Expr, Type);
  RK.TheFoldOpInitPool.InsertNode(I, IP);
  return I;
}

void FoldOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileFoldOpInit(ID, Start, List, A, B, Expr, getType());
}

Init *FoldOpInit::Fold(Record *CurRec) const {
  if (ListInit *LI = dyn_cast<ListInit>(List)) {
    Init *Accum = Start;
    for (Init *Elt : *LI) {
      MapResolver R(CurRec);
      R.set(A, Accum);
      R.set(B, Elt);
      Accum = Expr->resolveReferences(R);
    }
    return Accum;
  }
  return const_cast<FoldOpInit *>(this);
}

Init *FoldOpInit::resolveReferences(Resolver &R) const {
  Init *NewStart = Start->resolveReferences(R);
  Init *NewList = List->resolveReferences(R);
  ShadowResolver SR(R);
  SR.addShadow(A);
  SR.addShadow(B);
  Init *NewExpr = Expr->resolveReferences(SR);

  if (Start == NewStart && List == NewList && Expr == NewExpr)
    return const_cast<FoldOpInit *>(this);

  return get(NewStart, NewList, A, B, NewExpr, getType())
      ->Fold(R.getCurrentRecord());
}

Init *FoldOpInit::getBit(unsigned Bit) const {
  return VarBitInit::get(const_cast<FoldOpInit *>(this), Bit);
}

std::string FoldOpInit::getAsString() const {
  return (Twine("!foldl(") + Start->getAsString() + ", " + List->getAsString() +
          ", " + A->getAsUnquotedString() + ", " + B->getAsUnquotedString() +
          ", " + Expr->getAsString() + ")")
      .str();
}

static void ProfileIsAOpInit(FoldingSetNodeID &ID, RecTy *CheckType,
                             Init *Expr) {
  ID.AddPointer(CheckType);
  ID.AddPointer(Expr);
}

IsAOpInit *IsAOpInit::get(RecTy *CheckType, Init *Expr) {

  FoldingSetNodeID ID;
  ProfileIsAOpInit(ID, CheckType, Expr);

  detail::RecordKeeperImpl &RK = Expr->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (IsAOpInit *I = RK.TheIsAOpInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  IsAOpInit *I = new (RK.Allocator) IsAOpInit(CheckType, Expr);
  RK.TheIsAOpInitPool.InsertNode(I, IP);
  return I;
}

void IsAOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileIsAOpInit(ID, CheckType, Expr);
}

Init *IsAOpInit::Fold() const {
  if (TypedInit *TI = dyn_cast<TypedInit>(Expr)) {
    // Is the expression type known to be (a subclass of) the desired type?
    if (TI->getType()->typeIsConvertibleTo(CheckType))
      return IntInit::get(getRecordKeeper(), 1);

    if (isa<RecordRecTy>(CheckType)) {
      // If the target type is not a subclass of the expression type, or if
      // the expression has fully resolved to a record, we know that it can't
      // be of the required type.
      if (!CheckType->typeIsConvertibleTo(TI->getType()) || isa<DefInit>(Expr))
        return IntInit::get(getRecordKeeper(), 0);
    } else {
      // We treat non-record types as not castable.
      return IntInit::get(getRecordKeeper(), 0);
    }
  }
  return const_cast<IsAOpInit *>(this);
}

Init *IsAOpInit::resolveReferences(Resolver &R) const {
  Init *NewExpr = Expr->resolveReferences(R);
  if (Expr != NewExpr)
    return get(CheckType, NewExpr)->Fold();
  return const_cast<IsAOpInit *>(this);
}

Init *IsAOpInit::getBit(unsigned Bit) const {
  return VarBitInit::get(const_cast<IsAOpInit *>(this), Bit);
}

std::string IsAOpInit::getAsString() const {
  return (Twine("!isa<") + CheckType->getAsString() + ">(" +
          Expr->getAsString() + ")")
      .str();
}

static void ProfileExistsOpInit(FoldingSetNodeID &ID, RecTy *CheckType,
                                Init *Expr) {
  ID.AddPointer(CheckType);
  ID.AddPointer(Expr);
}

ExistsOpInit *ExistsOpInit::get(RecTy *CheckType, Init *Expr) {
  FoldingSetNodeID ID;
  ProfileExistsOpInit(ID, CheckType, Expr);

  detail::RecordKeeperImpl &RK = Expr->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (ExistsOpInit *I = RK.TheExistsOpInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  ExistsOpInit *I = new (RK.Allocator) ExistsOpInit(CheckType, Expr);
  RK.TheExistsOpInitPool.InsertNode(I, IP);
  return I;
}

void ExistsOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileExistsOpInit(ID, CheckType, Expr);
}

Init *ExistsOpInit::Fold(Record *CurRec, bool IsFinal) const {
  if (StringInit *Name = dyn_cast<StringInit>(Expr)) {

    // Look up all defined records to see if we can find one.
    Record *D = CheckType->getRecordKeeper().getDef(Name->getValue());
    if (D) {
      // Check if types are compatible.
      return IntInit::get(getRecordKeeper(),
                          DefInit::get(D)->getType()->typeIsA(CheckType));
    }

    if (CurRec) {
      // Self-references are allowed, but their resolution is delayed until
      // the final resolve to ensure that we get the correct type for them.
      auto *Anonymous = dyn_cast<AnonymousNameInit>(CurRec->getNameInit());
      if (Name == CurRec->getNameInit() ||
          (Anonymous && Name == Anonymous->getNameInit())) {
        if (!IsFinal)
          return const_cast<ExistsOpInit *>(this);

        // No doubt that there exists a record, so we should check if types are
        // compatible.
        return IntInit::get(getRecordKeeper(),
                            CurRec->getType()->typeIsA(CheckType));
      }
    }

    if (IsFinal)
      return IntInit::get(getRecordKeeper(), 0);
    return const_cast<ExistsOpInit *>(this);
  }
  return const_cast<ExistsOpInit *>(this);
}

Init *ExistsOpInit::resolveReferences(Resolver &R) const {
  Init *NewExpr = Expr->resolveReferences(R);
  if (Expr != NewExpr || R.isFinal())
    return get(CheckType, NewExpr)->Fold(R.getCurrentRecord(), R.isFinal());
  return const_cast<ExistsOpInit *>(this);
}

Init *ExistsOpInit::getBit(unsigned Bit) const {
  return VarBitInit::get(const_cast<ExistsOpInit *>(this), Bit);
}

std::string ExistsOpInit::getAsString() const {
  return (Twine("!exists<") + CheckType->getAsString() + ">(" +
          Expr->getAsString() + ")")
      .str();
}

RecTy *TypedInit::getFieldType(StringInit *FieldName) const {
  if (RecordRecTy *RecordType = dyn_cast<RecordRecTy>(getType())) {
    for (Record *Rec : RecordType->getClasses()) {
      if (RecordVal *Field = Rec->getValue(FieldName))
        return Field->getType();
    }
  }
  return nullptr;
}

Init *
TypedInit::convertInitializerTo(RecTy *Ty) const {
  if (getType() == Ty || getType()->typeIsA(Ty))
    return const_cast<TypedInit *>(this);

  if (isa<BitRecTy>(getType()) && isa<BitsRecTy>(Ty) &&
      cast<BitsRecTy>(Ty)->getNumBits() == 1)
    return BitsInit::get(getRecordKeeper(), {const_cast<TypedInit *>(this)});

  return nullptr;
}

Init *TypedInit::convertInitializerBitRange(ArrayRef<unsigned> Bits) const {
  BitsRecTy *T = dyn_cast<BitsRecTy>(getType());
  if (!T) return nullptr;  // Cannot subscript a non-bits variable.
  unsigned NumBits = T->getNumBits();

  SmallVector<Init *, 16> NewBits;
  NewBits.reserve(Bits.size());
  for (unsigned Bit : Bits) {
    if (Bit >= NumBits)
      return nullptr;

    NewBits.push_back(VarBitInit::get(const_cast<TypedInit *>(this), Bit));
  }
  return BitsInit::get(getRecordKeeper(), NewBits);
}

Init *TypedInit::getCastTo(RecTy *Ty) const {
  // Handle the common case quickly
  if (getType() == Ty || getType()->typeIsA(Ty))
    return const_cast<TypedInit *>(this);

  if (Init *Converted = convertInitializerTo(Ty)) {
    assert(!isa<TypedInit>(Converted) ||
           cast<TypedInit>(Converted)->getType()->typeIsA(Ty));
    return Converted;
  }

  if (!getType()->typeIsConvertibleTo(Ty))
    return nullptr;

  return UnOpInit::get(UnOpInit::CAST, const_cast<TypedInit *>(this), Ty)
      ->Fold(nullptr);
}

VarInit *VarInit::get(StringRef VN, RecTy *T) {
  Init *Value = StringInit::get(T->getRecordKeeper(), VN);
  return VarInit::get(Value, T);
}

VarInit *VarInit::get(Init *VN, RecTy *T) {
  detail::RecordKeeperImpl &RK = T->getRecordKeeper().getImpl();
  VarInit *&I = RK.TheVarInitPool[std::make_pair(T, VN)];
  if (!I)
    I = new (RK.Allocator) VarInit(VN, T);
  return I;
}

StringRef VarInit::getName() const {
  StringInit *NameString = cast<StringInit>(getNameInit());
  return NameString->getValue();
}

Init *VarInit::getBit(unsigned Bit) const {
  if (getType() == BitRecTy::get(getRecordKeeper()))
    return const_cast<VarInit*>(this);
  return VarBitInit::get(const_cast<VarInit*>(this), Bit);
}

Init *VarInit::resolveReferences(Resolver &R) const {
  if (Init *Val = R.resolve(VarName))
    return Val;
  return const_cast<VarInit *>(this);
}

VarBitInit *VarBitInit::get(TypedInit *T, unsigned B) {
  detail::RecordKeeperImpl &RK = T->getRecordKeeper().getImpl();
  VarBitInit *&I = RK.TheVarBitInitPool[std::make_pair(T, B)];
  if (!I)
    I = new (RK.Allocator) VarBitInit(T, B);
  return I;
}

std::string VarBitInit::getAsString() const {
  return TI->getAsString() + "{" + utostr(Bit) + "}";
}

Init *VarBitInit::resolveReferences(Resolver &R) const {
  Init *I = TI->resolveReferences(R);
  if (TI != I)
    return I->getBit(getBitNum());

  return const_cast<VarBitInit*>(this);
}

DefInit::DefInit(Record *D)
    : TypedInit(IK_DefInit, D->getType()), Def(D) {}

DefInit *DefInit::get(Record *R) {
  return R->getDefInit();
}

Init *DefInit::convertInitializerTo(RecTy *Ty) const {
  if (auto *RRT = dyn_cast<RecordRecTy>(Ty))
    if (getType()->typeIsConvertibleTo(RRT))
      return const_cast<DefInit *>(this);
  return nullptr;
}

RecTy *DefInit::getFieldType(StringInit *FieldName) const {
  if (const RecordVal *RV = Def->getValue(FieldName))
    return RV->getType();
  return nullptr;
}

std::string DefInit::getAsString() const { return std::string(Def->getName()); }

static void ProfileVarDefInit(FoldingSetNodeID &ID, Record *Class,
                              ArrayRef<ArgumentInit *> Args) {
  ID.AddInteger(Args.size());
  ID.AddPointer(Class);

  for (Init *I : Args)
    ID.AddPointer(I);
}

VarDefInit::VarDefInit(Record *Class, unsigned N)
    : TypedInit(IK_VarDefInit, RecordRecTy::get(Class)), Class(Class),
      NumArgs(N) {}

VarDefInit *VarDefInit::get(Record *Class, ArrayRef<ArgumentInit *> Args) {
  FoldingSetNodeID ID;
  ProfileVarDefInit(ID, Class, Args);

  detail::RecordKeeperImpl &RK = Class->getRecords().getImpl();
  void *IP = nullptr;
  if (VarDefInit *I = RK.TheVarDefInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  void *Mem = RK.Allocator.Allocate(
      totalSizeToAlloc<ArgumentInit *>(Args.size()), alignof(VarDefInit));
  VarDefInit *I = new (Mem) VarDefInit(Class, Args.size());
  std::uninitialized_copy(Args.begin(), Args.end(),
                          I->getTrailingObjects<ArgumentInit *>());
  RK.TheVarDefInitPool.InsertNode(I, IP);
  return I;
}

void VarDefInit::Profile(FoldingSetNodeID &ID) const {
  ProfileVarDefInit(ID, Class, args());
}

DefInit *VarDefInit::instantiate() {
  if (!Def) {
    RecordKeeper &Records = Class->getRecords();
    auto NewRecOwner =
        std::make_unique<Record>(Records.getNewAnonymousName(), Class->getLoc(),
                                 Records, Record::RK_AnonymousDef);
    Record *NewRec = NewRecOwner.get();

    // Copy values from class to instance
    for (const RecordVal &Val : Class->getValues())
      NewRec->addValue(Val);

    // Copy assertions from class to instance.
    NewRec->appendAssertions(Class);

    // Copy dumps from class to instance.
    NewRec->appendDumps(Class);

    // Substitute and resolve template arguments
    ArrayRef<Init *> TArgs = Class->getTemplateArgs();
    MapResolver R(NewRec);

    for (Init *Arg : TArgs) {
      R.set(Arg, NewRec->getValue(Arg)->getValue());
      NewRec->removeValue(Arg);
    }

    for (auto *Arg : args()) {
      if (Arg->isPositional())
        R.set(TArgs[Arg->getIndex()], Arg->getValue());
      if (Arg->isNamed())
        R.set(Arg->getName(), Arg->getValue());
    }

    NewRec->resolveReferences(R);

    // Add superclasses.
    ArrayRef<std::pair<Record *, SMRange>> SCs = Class->getSuperClasses();
    for (const auto &SCPair : SCs)
      NewRec->addSuperClass(SCPair.first, SCPair.second);

    NewRec->addSuperClass(Class,
                          SMRange(Class->getLoc().back(),
                                  Class->getLoc().back()));

    // Resolve internal references and store in record keeper
    NewRec->resolveReferences();
    Records.addDef(std::move(NewRecOwner));

    // Check the assertions.
    NewRec->checkRecordAssertions();

    // Check the assertions.
    NewRec->emitRecordDumps();

    Def = DefInit::get(NewRec);
  }

  return Def;
}

Init *VarDefInit::resolveReferences(Resolver &R) const {
  TrackUnresolvedResolver UR(&R);
  bool Changed = false;
  SmallVector<ArgumentInit *, 8> NewArgs;
  NewArgs.reserve(args_size());

  for (ArgumentInit *Arg : args()) {
    auto *NewArg = cast<ArgumentInit>(Arg->resolveReferences(UR));
    NewArgs.push_back(NewArg);
    Changed |= NewArg != Arg;
  }

  if (Changed) {
    auto New = VarDefInit::get(Class, NewArgs);
    if (!UR.foundUnresolved())
      return New->instantiate();
    return New;
  }
  return const_cast<VarDefInit *>(this);
}

Init *VarDefInit::Fold() const {
  if (Def)
    return Def;

  TrackUnresolvedResolver R;
  for (Init *Arg : args())
    Arg->resolveReferences(R);

  if (!R.foundUnresolved())
    return const_cast<VarDefInit *>(this)->instantiate();
  return const_cast<VarDefInit *>(this);
}

std::string VarDefInit::getAsString() const {
  std::string Result = Class->getNameInitAsString() + "<";
  const char *sep = "";
  for (Init *Arg : args()) {
    Result += sep;
    sep = ", ";
    Result += Arg->getAsString();
  }
  return Result + ">";
}

FieldInit *FieldInit::get(Init *R, StringInit *FN) {
  detail::RecordKeeperImpl &RK = R->getRecordKeeper().getImpl();
  FieldInit *&I = RK.TheFieldInitPool[std::make_pair(R, FN)];
  if (!I)
    I = new (RK.Allocator) FieldInit(R, FN);
  return I;
}

Init *FieldInit::getBit(unsigned Bit) const {
  if (getType() == BitRecTy::get(getRecordKeeper()))
    return const_cast<FieldInit*>(this);
  return VarBitInit::get(const_cast<FieldInit*>(this), Bit);
}

Init *FieldInit::resolveReferences(Resolver &R) const {
  Init *NewRec = Rec->resolveReferences(R);
  if (NewRec != Rec)
    return FieldInit::get(NewRec, FieldName)->Fold(R.getCurrentRecord());
  return const_cast<FieldInit *>(this);
}

Init *FieldInit::Fold(Record *CurRec) const {
  if (DefInit *DI = dyn_cast<DefInit>(Rec)) {
    Record *Def = DI->getDef();
    if (Def == CurRec)
      PrintFatalError(CurRec->getLoc(),
                      Twine("Attempting to access field '") +
                      FieldName->getAsUnquotedString() + "' of '" +
                      Rec->getAsString() + "' is a forbidden self-reference");
    Init *FieldVal = Def->getValue(FieldName)->getValue();
    if (FieldVal->isConcrete())
      return FieldVal;
  }
  return const_cast<FieldInit *>(this);
}

bool FieldInit::isConcrete() const {
  if (DefInit *DI = dyn_cast<DefInit>(Rec)) {
    Init *FieldVal = DI->getDef()->getValue(FieldName)->getValue();
    return FieldVal->isConcrete();
  }
  return false;
}

static void ProfileCondOpInit(FoldingSetNodeID &ID,
                             ArrayRef<Init *> CondRange,
                             ArrayRef<Init *> ValRange,
                             const RecTy *ValType) {
  assert(CondRange.size() == ValRange.size() &&
         "Number of conditions and values must match!");
  ID.AddPointer(ValType);
  ArrayRef<Init *>::iterator Case = CondRange.begin();
  ArrayRef<Init *>::iterator Val = ValRange.begin();

  while (Case != CondRange.end()) {
    ID.AddPointer(*Case++);
    ID.AddPointer(*Val++);
  }
}

void CondOpInit::Profile(FoldingSetNodeID &ID) const {
  ProfileCondOpInit(ID, ArrayRef(getTrailingObjects<Init *>(), NumConds),
                    ArrayRef(getTrailingObjects<Init *>() + NumConds, NumConds),
                    ValType);
}

CondOpInit *CondOpInit::get(ArrayRef<Init *> CondRange,
                            ArrayRef<Init *> ValRange, RecTy *Ty) {
  assert(CondRange.size() == ValRange.size() &&
         "Number of conditions and values must match!");

  FoldingSetNodeID ID;
  ProfileCondOpInit(ID, CondRange, ValRange, Ty);

  detail::RecordKeeperImpl &RK = Ty->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (CondOpInit *I = RK.TheCondOpInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  void *Mem = RK.Allocator.Allocate(
      totalSizeToAlloc<Init *>(2 * CondRange.size()), alignof(BitsInit));
  CondOpInit *I = new(Mem) CondOpInit(CondRange.size(), Ty);

  std::uninitialized_copy(CondRange.begin(), CondRange.end(),
                          I->getTrailingObjects<Init *>());
  std::uninitialized_copy(ValRange.begin(), ValRange.end(),
                          I->getTrailingObjects<Init *>()+CondRange.size());
  RK.TheCondOpInitPool.InsertNode(I, IP);
  return I;
}

Init *CondOpInit::resolveReferences(Resolver &R) const {
  SmallVector<Init*, 4> NewConds;
  bool Changed = false;
  for (const Init *Case : getConds()) {
    Init *NewCase = Case->resolveReferences(R);
    NewConds.push_back(NewCase);
    Changed |= NewCase != Case;
  }

  SmallVector<Init*, 4> NewVals;
  for (const Init *Val : getVals()) {
    Init *NewVal = Val->resolveReferences(R);
    NewVals.push_back(NewVal);
    Changed |= NewVal != Val;
  }

  if (Changed)
    return (CondOpInit::get(NewConds, NewVals,
            getValType()))->Fold(R.getCurrentRecord());

  return const_cast<CondOpInit *>(this);
}

Init *CondOpInit::Fold(Record *CurRec) const {
  RecordKeeper &RK = getRecordKeeper();
  for ( unsigned i = 0; i < NumConds; ++i) {
    Init *Cond = getCond(i);
    Init *Val = getVal(i);

    if (IntInit *CondI = dyn_cast_or_null<IntInit>(
            Cond->convertInitializerTo(IntRecTy::get(RK)))) {
      if (CondI->getValue())
        return Val->convertInitializerTo(getValType());
    } else {
      return const_cast<CondOpInit *>(this);
    }
  }

  PrintFatalError(CurRec->getLoc(),
                  CurRec->getNameInitAsString() +
                  " does not have any true condition in:" +
                  this->getAsString());
  return nullptr;
}

bool CondOpInit::isConcrete() const {
  for (const Init *Case : getConds())
    if (!Case->isConcrete())
      return false;

  for (const Init *Val : getVals())
    if (!Val->isConcrete())
      return false;

  return true;
}

bool CondOpInit::isComplete() const {
  for (const Init *Case : getConds())
    if (!Case->isComplete())
      return false;

  for (const Init *Val : getVals())
    if (!Val->isConcrete())
      return false;

  return true;
}

std::string CondOpInit::getAsString() const {
  std::string Result = "!cond(";
  for (unsigned i = 0; i < getNumConds(); i++) {
    Result += getCond(i)->getAsString() + ": ";
    Result += getVal(i)->getAsString();
    if (i != getNumConds()-1)
      Result += ", ";
  }
  return Result + ")";
}

Init *CondOpInit::getBit(unsigned Bit) const {
  return VarBitInit::get(const_cast<CondOpInit *>(this), Bit);
}

static void ProfileDagInit(FoldingSetNodeID &ID, Init *V, StringInit *VN,
                           ArrayRef<Init *> ArgRange,
                           ArrayRef<StringInit *> NameRange) {
  ID.AddPointer(V);
  ID.AddPointer(VN);

  ArrayRef<Init *>::iterator Arg = ArgRange.begin();
  ArrayRef<StringInit *>::iterator Name = NameRange.begin();
  while (Arg != ArgRange.end()) {
    assert(Name != NameRange.end() && "Arg name underflow!");
    ID.AddPointer(*Arg++);
    ID.AddPointer(*Name++);
  }
  assert(Name == NameRange.end() && "Arg name overflow!");
}

DagInit *DagInit::get(Init *V, StringInit *VN, ArrayRef<Init *> ArgRange,
                      ArrayRef<StringInit *> NameRange) {
  assert(ArgRange.size() == NameRange.size());
  FoldingSetNodeID ID;
  ProfileDagInit(ID, V, VN, ArgRange, NameRange);

  detail::RecordKeeperImpl &RK = V->getRecordKeeper().getImpl();
  void *IP = nullptr;
  if (DagInit *I = RK.TheDagInitPool.FindNodeOrInsertPos(ID, IP))
    return I;

  void *Mem = RK.Allocator.Allocate(
      totalSizeToAlloc<Init *, StringInit *>(ArgRange.size(), NameRange.size()),
      alignof(BitsInit));
  DagInit *I = new (Mem) DagInit(V, VN, ArgRange.size(), NameRange.size());
  std::uninitialized_copy(ArgRange.begin(), ArgRange.end(),
                          I->getTrailingObjects<Init *>());
  std::uninitialized_copy(NameRange.begin(), NameRange.end(),
                          I->getTrailingObjects<StringInit *>());
  RK.TheDagInitPool.InsertNode(I, IP);
  return I;
}

DagInit *
DagInit::get(Init *V, StringInit *VN,
             ArrayRef<std::pair<Init*, StringInit*>> args) {
  SmallVector<Init *, 8> Args;
  SmallVector<StringInit *, 8> Names;

  for (const auto &Arg : args) {
    Args.push_back(Arg.first);
    Names.push_back(Arg.second);
  }

  return DagInit::get(V, VN, Args, Names);
}

void DagInit::Profile(FoldingSetNodeID &ID) const {
  ProfileDagInit(ID, Val, ValName,
                 ArrayRef(getTrailingObjects<Init *>(), NumArgs),
                 ArrayRef(getTrailingObjects<StringInit *>(), NumArgNames));
}

Record *DagInit::getOperatorAsDef(ArrayRef<SMLoc> Loc) const {
  if (DefInit *DefI = dyn_cast<DefInit>(Val))
    return DefI->getDef();
  PrintFatalError(Loc, "Expected record as operator");
  return nullptr;
}

std::optional<unsigned> DagInit::getArgNo(StringRef Name) const {
  for (unsigned i = 0, e = getNumArgs(); i < e; ++i) {
    StringInit *ArgName = getArgName(i);
    if (ArgName && ArgName->getValue() == Name)
      return i;
  }
  return std::nullopt;
}

Init *DagInit::resolveReferences(Resolver &R) const {
  SmallVector<Init*, 8> NewArgs;
  NewArgs.reserve(arg_size());
  bool ArgsChanged = false;
  for (const Init *Arg : getArgs()) {
    Init *NewArg = Arg->resolveReferences(R);
    NewArgs.push_back(NewArg);
    ArgsChanged |= NewArg != Arg;
  }

  Init *Op = Val->resolveReferences(R);
  if (Op != Val || ArgsChanged)
    return DagInit::get(Op, ValName, NewArgs, getArgNames());

  return const_cast<DagInit *>(this);
}

bool DagInit::isConcrete() const {
  if (!Val->isConcrete())
    return false;
  for (const Init *Elt : getArgs()) {
    if (!Elt->isConcrete())
      return false;
  }
  return true;
}

std::string DagInit::getAsString() const {
  std::string Result = "(" + Val->getAsString();
  if (ValName)
    Result += ":" + ValName->getAsUnquotedString();
  if (!arg_empty()) {
    Result += " " + getArg(0)->getAsString();
    if (getArgName(0)) Result += ":$" + getArgName(0)->getAsUnquotedString();
    for (unsigned i = 1, e = getNumArgs(); i != e; ++i) {
      Result += ", " + getArg(i)->getAsString();
      if (getArgName(i)) Result += ":$" + getArgName(i)->getAsUnquotedString();
    }
  }
  return Result + ")";
}

//===----------------------------------------------------------------------===//
//    Other implementations
//===----------------------------------------------------------------------===//

RecordVal::RecordVal(Init *N, RecTy *T, FieldKind K)
    : Name(N), TyAndKind(T, K) {
  setValue(UnsetInit::get(N->getRecordKeeper()));
  assert(Value && "Cannot create unset value for current type!");
}

// This constructor accepts the same arguments as the above, but also
// a source location.
RecordVal::RecordVal(Init *N, SMLoc Loc, RecTy *T, FieldKind K)
    : Name(N), Loc(Loc), TyAndKind(T, K) {
  setValue(UnsetInit::get(N->getRecordKeeper()));
  assert(Value && "Cannot create unset value for current type!");
}

StringRef RecordVal::getName() const {
  return cast<StringInit>(getNameInit())->getValue();
}

std::string RecordVal::getPrintType() const {
  if (getType() == StringRecTy::get(getRecordKeeper())) {
    if (auto *StrInit = dyn_cast<StringInit>(Value)) {
      if (StrInit->hasCodeFormat())
        return "code";
      else
        return "string";
    } else {
      return "string";
    }
  } else {
    return TyAndKind.getPointer()->getAsString();
  }
}

bool RecordVal::setValue(Init *V) {
  if (V) {
    Value = V->getCastTo(getType());
    if (Value) {
      assert(!isa<TypedInit>(Value) ||
             cast<TypedInit>(Value)->getType()->typeIsA(getType()));
      if (BitsRecTy *BTy = dyn_cast<BitsRecTy>(getType())) {
        if (!isa<BitsInit>(Value)) {
          SmallVector<Init *, 64> Bits;
          Bits.reserve(BTy->getNumBits());
          for (unsigned I = 0, E = BTy->getNumBits(); I < E; ++I)
            Bits.push_back(Value->getBit(I));
          Value = BitsInit::get(V->getRecordKeeper(), Bits);
        }
      }
    }
    return Value == nullptr;
  }
  Value = nullptr;
  return false;
}

// This version of setValue takes a source location and resets the
// location in the RecordVal.
bool RecordVal::setValue(Init *V, SMLoc NewLoc) {
  Loc = NewLoc;
  if (V) {
    Value = V->getCastTo(getType());
    if (Value) {
      assert(!isa<TypedInit>(Value) ||
             cast<TypedInit>(Value)->getType()->typeIsA(getType()));
      if (BitsRecTy *BTy = dyn_cast<BitsRecTy>(getType())) {
        if (!isa<BitsInit>(Value)) {
          SmallVector<Init *, 64> Bits;
          Bits.reserve(BTy->getNumBits());
          for (unsigned I = 0, E = BTy->getNumBits(); I < E; ++I)
            Bits.push_back(Value->getBit(I));
          Value = BitsInit::get(getRecordKeeper(), Bits);
        }
      }
    }
    return Value == nullptr;
  }
  Value = nullptr;
  return false;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
#include "llvm/TableGen/Record.h"
LLVM_DUMP_METHOD void RecordVal::dump() const { errs() << *this; }
#endif

void RecordVal::print(raw_ostream &OS, bool PrintSem) const {
  if (isNonconcreteOK()) OS << "field ";
  OS << getPrintType() << " " << getNameInitAsString();

  if (getValue())
    OS << " = " << *getValue();

  if (PrintSem) OS << ";\n";
}

void Record::updateClassLoc(SMLoc Loc) {
  assert(Locs.size() == 1);
  ForwardDeclarationLocs.push_back(Locs.front());

  Locs.clear();
  Locs.push_back(Loc);
}

void Record::checkName() {
  // Ensure the record name has string type.
  const TypedInit *TypedName = cast<const TypedInit>(Name);
  if (!isa<StringRecTy>(TypedName->getType()))
    PrintFatalError(getLoc(), Twine("Record name '") + Name->getAsString() +
                                  "' is not a string!");
}

RecordRecTy *Record::getType() {
  SmallVector<Record *, 4> DirectSCs;
  getDirectSuperClasses(DirectSCs);
  return RecordRecTy::get(TrackedRecords, DirectSCs);
}

DefInit *Record::getDefInit() {
  if (!CorrespondingDefInit) {
    CorrespondingDefInit =
        new (TrackedRecords.getImpl().Allocator) DefInit(this);
  }
  return CorrespondingDefInit;
}

unsigned Record::getNewUID(RecordKeeper &RK) {
  return RK.getImpl().LastRecordID++;
}

void Record::setName(Init *NewName) {
  Name = NewName;
  checkName();
  // DO NOT resolve record values to the name at this point because
  // there might be default values for arguments of this def.  Those
  // arguments might not have been resolved yet so we don't want to
  // prematurely assume values for those arguments were not passed to
  // this def.
  //
  // Nonetheless, it may be that some of this Record's values
  // reference the record name.  Indeed, the reason for having the
  // record name be an Init is to provide this flexibility.  The extra
  // resolve steps after completely instantiating defs takes care of
  // this.  See TGParser::ParseDef and TGParser::ParseDefm.
}

// NOTE for the next two functions:
// Superclasses are in post-order, so the final one is a direct
// superclass. All of its transitive superclases immediately precede it,
// so we can step through the direct superclasses in reverse order.

bool Record::hasDirectSuperClass(const Record *Superclass) const {
  ArrayRef<std::pair<Record *, SMRange>> SCs = getSuperClasses();

  for (int I = SCs.size() - 1; I >= 0; --I) {
    const Record *SC = SCs[I].first;
    if (SC == Superclass)
      return true;
    I -= SC->getSuperClasses().size();
  }

  return false;
}

void Record::getDirectSuperClasses(SmallVectorImpl<Record *> &Classes) const {
  ArrayRef<std::pair<Record *, SMRange>> SCs = getSuperClasses();

  while (!SCs.empty()) {
    Record *SC = SCs.back().first;
    SCs = SCs.drop_back(1 + SC->getSuperClasses().size());
    Classes.push_back(SC);
  }
}

void Record::resolveReferences(Resolver &R, const RecordVal *SkipVal) {
  Init *OldName = getNameInit();
  Init *NewName = Name->resolveReferences(R);
  if (NewName != OldName) {
    // Re-register with RecordKeeper.
    setName(NewName);
  }

  // Resolve the field values.
  for (RecordVal &Value : Values) {
    if (SkipVal == &Value) // Skip resolve the same field as the given one
      continue;
    if (Init *V = Value.getValue()) {
      Init *VR = V->resolveReferences(R);
      if (Value.setValue(VR)) {
        std::string Type;
        if (TypedInit *VRT = dyn_cast<TypedInit>(VR))
          Type =
              (Twine("of type '") + VRT->getType()->getAsString() + "' ").str();
        PrintFatalError(
            getLoc(),
            Twine("Invalid value ") + Type + "found when setting field '" +
                Value.getNameInitAsString() + "' of type '" +
                Value.getType()->getAsString() +
                "' after resolving references: " + VR->getAsUnquotedString() +
                "\n");
      }
    }
  }

  // Resolve the assertion expressions.
  for (auto &Assertion : Assertions) {
    Init *Value = Assertion.Condition->resolveReferences(R);
    Assertion.Condition = Value;
    Value = Assertion.Message->resolveReferences(R);
    Assertion.Message = Value;
  }
  // Resolve the dump expressions.
  for (auto &Dump : Dumps) {
    Init *Value = Dump.Message->resolveReferences(R);
    Dump.Message = Value;
  }
}

void Record::resolveReferences(Init *NewName) {
  RecordResolver R(*this);
  R.setName(NewName);
  R.setFinal(true);
  resolveReferences(R);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void Record::dump() const { errs() << *this; }
#endif

raw_ostream &llvm::operator<<(raw_ostream &OS, const Record &R) {
  OS << R.getNameInitAsString();

  ArrayRef<Init *> TArgs = R.getTemplateArgs();
  if (!TArgs.empty()) {
    OS << "<";
    bool NeedComma = false;
    for (const Init *TA : TArgs) {
      if (NeedComma) OS << ", ";
      NeedComma = true;
      const RecordVal *RV = R.getValue(TA);
      assert(RV && "Template argument record not found??");
      RV->print(OS, false);
    }
    OS << ">";
  }

  OS << " {";
  ArrayRef<std::pair<Record *, SMRange>> SC = R.getSuperClasses();
  if (!SC.empty()) {
    OS << "\t//";
    for (const auto &SuperPair : SC)
      OS << " " << SuperPair.first->getNameInitAsString();
  }
  OS << "\n";

  for (const RecordVal &Val : R.getValues())
    if (Val.isNonconcreteOK() && !R.isTemplateArg(Val.getNameInit()))
      OS << Val;
  for (const RecordVal &Val : R.getValues())
    if (!Val.isNonconcreteOK() && !R.isTemplateArg(Val.getNameInit()))
      OS << Val;

  return OS << "}\n";
}

SMLoc Record::getFieldLoc(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R)
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");
  return R->getLoc();
}

Init *Record::getValueInit(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");
  return R->getValue();
}

StringRef Record::getValueAsString(StringRef FieldName) const {
  std::optional<StringRef> S = getValueAsOptionalString(FieldName);
  if (!S)
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");
  return *S;
}

std::optional<StringRef>
Record::getValueAsOptionalString(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    return std::nullopt;
  if (isa<UnsetInit>(R->getValue()))
    return std::nullopt;

  if (StringInit *SI = dyn_cast<StringInit>(R->getValue()))
    return SI->getValue();

  PrintFatalError(getLoc(),
                  "Record `" + getName() + "', ` field `" + FieldName +
                      "' exists but does not have a string initializer!");
}

BitsInit *Record::getValueAsBitsInit(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (BitsInit *BI = dyn_cast<BitsInit>(R->getValue()))
    return BI;
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" + FieldName +
                                "' exists but does not have a bits value");
}

ListInit *Record::getValueAsListInit(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (ListInit *LI = dyn_cast<ListInit>(R->getValue()))
    return LI;
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" + FieldName +
                                "' exists but does not have a list value");
}

std::vector<Record*>
Record::getValueAsListOfDefs(StringRef FieldName) const {
  ListInit *List = getValueAsListInit(FieldName);
  std::vector<Record*> Defs;
  for (Init *I : List->getValues()) {
    if (DefInit *DI = dyn_cast<DefInit>(I))
      Defs.push_back(DI->getDef());
    else
      PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
        FieldName + "' list is not entirely DefInit!");
  }
  return Defs;
}

int64_t Record::getValueAsInt(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (IntInit *II = dyn_cast<IntInit>(R->getValue()))
    return II->getValue();
  PrintFatalError(getLoc(), Twine("Record `") + getName() + "', field `" +
                                FieldName +
                                "' exists but does not have an int value: " +
                                R->getValue()->getAsString());
}

std::vector<int64_t>
Record::getValueAsListOfInts(StringRef FieldName) const {
  ListInit *List = getValueAsListInit(FieldName);
  std::vector<int64_t> Ints;
  for (Init *I : List->getValues()) {
    if (IntInit *II = dyn_cast<IntInit>(I))
      Ints.push_back(II->getValue());
    else
      PrintFatalError(getLoc(),
                      Twine("Record `") + getName() + "', field `" + FieldName +
                          "' exists but does not have a list of ints value: " +
                          I->getAsString());
  }
  return Ints;
}

std::vector<StringRef>
Record::getValueAsListOfStrings(StringRef FieldName) const {
  ListInit *List = getValueAsListInit(FieldName);
  std::vector<StringRef> Strings;
  for (Init *I : List->getValues()) {
    if (StringInit *SI = dyn_cast<StringInit>(I))
      Strings.push_back(SI->getValue());
    else
      PrintFatalError(getLoc(),
                      Twine("Record `") + getName() + "', field `" + FieldName +
                          "' exists but does not have a list of strings value: " +
                          I->getAsString());
  }
  return Strings;
}

Record *Record::getValueAsDef(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (DefInit *DI = dyn_cast<DefInit>(R->getValue()))
    return DI->getDef();
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a def initializer!");
}

Record *Record::getValueAsOptionalDef(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (DefInit *DI = dyn_cast<DefInit>(R->getValue()))
    return DI->getDef();
  if (isa<UnsetInit>(R->getValue()))
    return nullptr;
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have either a def initializer or '?'!");
}


bool Record::getValueAsBit(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (BitInit *BI = dyn_cast<BitInit>(R->getValue()))
    return BI->getValue();
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a bit initializer!");
}

bool Record::getValueAsBitOrUnset(StringRef FieldName, bool &Unset) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName.str() + "'!\n");

  if (isa<UnsetInit>(R->getValue())) {
    Unset = true;
    return false;
  }
  Unset = false;
  if (BitInit *BI = dyn_cast<BitInit>(R->getValue()))
    return BI->getValue();
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a bit initializer!");
}

DagInit *Record::getValueAsDag(StringRef FieldName) const {
  const RecordVal *R = getValue(FieldName);
  if (!R || !R->getValue())
    PrintFatalError(getLoc(), "Record `" + getName() +
      "' does not have a field named `" + FieldName + "'!\n");

  if (DagInit *DI = dyn_cast<DagInit>(R->getValue()))
    return DI;
  PrintFatalError(getLoc(), "Record `" + getName() + "', field `" +
    FieldName + "' does not have a dag initializer!");
}

// Check all record assertions: For each one, resolve the condition
// and message, then call CheckAssert().
// Note: The condition and message are probably already resolved,
//       but resolving again allows calls before records are resolved.
void Record::checkRecordAssertions() {
  RecordResolver R(*this);
  R.setFinal(true);

  for (const auto &Assertion : getAssertions()) {
    Init *Condition = Assertion.Condition->resolveReferences(R);
    Init *Message = Assertion.Message->resolveReferences(R);
    CheckAssert(Assertion.Loc, Condition, Message);
  }
}

void Record::emitRecordDumps() {
  RecordResolver R(*this);
  R.setFinal(true);

  for (const auto &Dump : getDumps()) {
    Init *Message = Dump.Message->resolveReferences(R);
    dumpMessage(Dump.Loc, Message);
  }
}

// Report a warning if the record has unused template arguments.
void Record::checkUnusedTemplateArgs() {
  for (const Init *TA : getTemplateArgs()) {
    const RecordVal *Arg = getValue(TA);
    if (!Arg->isUsed())
      PrintWarning(Arg->getLoc(),
                   "unused template argument: " + Twine(Arg->getName()));
  }
}

RecordKeeper::RecordKeeper()
    : Impl(std::make_unique<detail::RecordKeeperImpl>(*this)) {}
RecordKeeper::~RecordKeeper() = default;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RecordKeeper::dump() const { errs() << *this; }
#endif

raw_ostream &llvm::operator<<(raw_ostream &OS, const RecordKeeper &RK) {
  OS << "------------- Classes -----------------\n";
  for (const auto &C : RK.getClasses())
    OS << "class " << *C.second;

  OS << "------------- Defs -----------------\n";
  for (const auto &D : RK.getDefs())
    OS << "def " << *D.second;
  return OS;
}

/// GetNewAnonymousName - Generate a unique anonymous name that can be used as
/// an identifier.
Init *RecordKeeper::getNewAnonymousName() {
  return AnonymousNameInit::get(*this, getImpl().AnonCounter++);
}

// These functions implement the phase timing facility. Starting a timer
// when one is already running stops the running one.

void RecordKeeper::startTimer(StringRef Name) {
  if (TimingGroup) {
    if (LastTimer && LastTimer->isRunning()) {
      LastTimer->stopTimer();
      if (BackendTimer) {
        LastTimer->clear();
        BackendTimer = false;
      }
    }

    LastTimer = new Timer("", Name, *TimingGroup);
    LastTimer->startTimer();
  }
}

void RecordKeeper::stopTimer() {
  if (TimingGroup) {
    assert(LastTimer && "No phase timer was started");
    LastTimer->stopTimer();
  }
}

void RecordKeeper::startBackendTimer(StringRef Name) {
  if (TimingGroup) {
    startTimer(Name);
    BackendTimer = true;
  }
}

void RecordKeeper::stopBackendTimer() {
  if (TimingGroup) {
    if (BackendTimer) {
      stopTimer();
      BackendTimer = false;
    }
  }
}

std::vector<Record *>
RecordKeeper::getAllDerivedDefinitions(StringRef ClassName) const {
  // We cache the record vectors for single classes. Many backends request
  // the same vectors multiple times.
  auto Pair = ClassRecordsMap.try_emplace(ClassName);
  if (Pair.second)
    Pair.first->second = getAllDerivedDefinitions(ArrayRef(ClassName));

  return Pair.first->second;
}

std::vector<Record *> RecordKeeper::getAllDerivedDefinitions(
    ArrayRef<StringRef> ClassNames) const {
  SmallVector<Record *, 2> ClassRecs;
  std::vector<Record *> Defs;

  assert(ClassNames.size() > 0 && "At least one class must be passed.");
  for (const auto &ClassName : ClassNames) {
    Record *Class = getClass(ClassName);
    if (!Class)
      PrintFatalError("The class '" + ClassName + "' is not defined\n");
    ClassRecs.push_back(Class);
  }

  for (const auto &OneDef : getDefs()) {
    if (all_of(ClassRecs, [&OneDef](const Record *Class) {
                            return OneDef.second->isSubClassOf(Class);
                          }))
      Defs.push_back(OneDef.second.get());
  }

  llvm::sort(Defs, LessRecord());

  return Defs;
}

std::vector<Record *>
RecordKeeper::getAllDerivedDefinitionsIfDefined(StringRef ClassName) const {
  return getClass(ClassName) ? getAllDerivedDefinitions(ClassName)
                             : std::vector<Record *>();
}

Init *MapResolver::resolve(Init *VarName) {
  auto It = Map.find(VarName);
  if (It == Map.end())
    return nullptr;

  Init *I = It->second.V;

  if (!It->second.Resolved && Map.size() > 1) {
    // Resolve mutual references among the mapped variables, but prevent
    // infinite recursion.
    Map.erase(It);
    I = I->resolveReferences(*this);
    Map[VarName] = {I, true};
  }

  return I;
}

Init *RecordResolver::resolve(Init *VarName) {
  Init *Val = Cache.lookup(VarName);
  if (Val)
    return Val;

  if (llvm::is_contained(Stack, VarName))
    return nullptr; // prevent infinite recursion

  if (RecordVal *RV = getCurrentRecord()->getValue(VarName)) {
    if (!isa<UnsetInit>(RV->getValue())) {
      Val = RV->getValue();
      Stack.push_back(VarName);
      Val = Val->resolveReferences(*this);
      Stack.pop_back();
    }
  } else if (Name && VarName == getCurrentRecord()->getNameInit()) {
    Stack.push_back(VarName);
    Val = Name->resolveReferences(*this);
    Stack.pop_back();
  }

  Cache[VarName] = Val;
  return Val;
}

Init *TrackUnresolvedResolver::resolve(Init *VarName) {
  Init *I = nullptr;

  if (R) {
    I = R->resolve(VarName);
    if (I && !FoundUnresolved) {
      // Do not recurse into the resolved initializer, as that would change
      // the behavior of the resolver we're delegating, but do check to see
      // if there are unresolved variables remaining.
      TrackUnresolvedResolver Sub;
      I->resolveReferences(Sub);
      FoundUnresolved |= Sub.FoundUnresolved;
    }
  }

  if (!I)
    FoundUnresolved = true;
  return I;
}

Init *HasReferenceResolver::resolve(Init *VarName)
{
  if (VarName == VarNameToTrack)
    Found = true;
  return nullptr;
}
