//===- UDTLayout.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/UDTLayout.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBaseClass.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTable.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>

using namespace llvm;
using namespace llvm::pdb;

static std::unique_ptr<PDBSymbol> getSymbolType(const PDBSymbol &Symbol) {
  const IPDBSession &Session = Symbol.getSession();
  const IPDBRawSymbol &RawSymbol = Symbol.getRawSymbol();
  uint32_t TypeId = RawSymbol.getTypeId();
  return Session.getSymbolById(TypeId);
}

static uint32_t getTypeLength(const PDBSymbol &Symbol) {
  auto SymbolType = getSymbolType(Symbol);
  const IPDBRawSymbol &RawType = SymbolType->getRawSymbol();

  return RawType.getLength();
}

LayoutItemBase::LayoutItemBase(const UDTLayoutBase *Parent,
                               const PDBSymbol *Symbol, const std::string &Name,
                               uint32_t OffsetInParent, uint32_t Size,
                               bool IsElided)
    : Symbol(Symbol), Parent(Parent), Name(Name),
      OffsetInParent(OffsetInParent), SizeOf(Size), LayoutSize(Size),
      IsElided(IsElided) {
  UsedBytes.resize(SizeOf, true);
}

uint32_t LayoutItemBase::deepPaddingSize() const {
  return UsedBytes.size() - UsedBytes.count();
}

uint32_t LayoutItemBase::tailPadding() const {
  int Last = UsedBytes.find_last();

  return UsedBytes.size() - (Last + 1);
}

DataMemberLayoutItem::DataMemberLayoutItem(
    const UDTLayoutBase &Parent, std::unique_ptr<PDBSymbolData> Member)
    : LayoutItemBase(&Parent, Member.get(), Member->getName(),
                     Member->getOffset(), getTypeLength(*Member), false),
      DataMember(std::move(Member)) {
  auto Type = DataMember->getType();
  if (auto UDT = unique_dyn_cast<PDBSymbolTypeUDT>(Type)) {
    UdtLayout = std::make_unique<ClassLayout>(std::move(UDT));
    UsedBytes = UdtLayout->usedBytes();
  }
}

VBPtrLayoutItem::VBPtrLayoutItem(const UDTLayoutBase &Parent,
                                 std::unique_ptr<PDBSymbolTypeBuiltin> Sym,
                                 uint32_t Offset, uint32_t Size)
    : LayoutItemBase(&Parent, Sym.get(), "<vbptr>", Offset, Size, false),
      Type(std::move(Sym)) {
}

const PDBSymbolData &DataMemberLayoutItem::getDataMember() {
  return *cast<PDBSymbolData>(Symbol);
}

bool DataMemberLayoutItem::hasUDTLayout() const { return UdtLayout != nullptr; }

const ClassLayout &DataMemberLayoutItem::getUDTLayout() const {
  return *UdtLayout;
}

VTableLayoutItem::VTableLayoutItem(const UDTLayoutBase &Parent,
                                   std::unique_ptr<PDBSymbolTypeVTable> VT)
    : LayoutItemBase(&Parent, VT.get(), "<vtbl>", 0, getTypeLength(*VT), false),
      VTable(std::move(VT)) {
  auto VTableType = cast<PDBSymbolTypePointer>(VTable->getType());
  ElementSize = VTableType->getLength();
}

UDTLayoutBase::UDTLayoutBase(const UDTLayoutBase *Parent, const PDBSymbol &Sym,
                             const std::string &Name, uint32_t OffsetInParent,
                             uint32_t Size, bool IsElided)
    : LayoutItemBase(Parent, &Sym, Name, OffsetInParent, Size, IsElided) {
  // UDT storage comes from a union of all the children's storage, so start out
  // uninitialized.
  UsedBytes.reset(0, Size);

  initializeChildren(Sym);
  if (LayoutSize < Size)
    UsedBytes.resize(LayoutSize);
}

uint32_t UDTLayoutBase::tailPadding() const {
  uint32_t Abs = LayoutItemBase::tailPadding();
  if (!LayoutItems.empty()) {
    const LayoutItemBase *Back = LayoutItems.back();
    uint32_t ChildPadding = Back->LayoutItemBase::tailPadding();
    if (Abs < ChildPadding)
      Abs = 0;
    else
      Abs -= ChildPadding;
  }
  return Abs;
}

ClassLayout::ClassLayout(const PDBSymbolTypeUDT &UDT)
    : UDTLayoutBase(nullptr, UDT, UDT.getName(), 0, UDT.getLength(), false),
      UDT(UDT) {
  ImmediateUsedBytes.resize(SizeOf, false);
  for (auto &LI : LayoutItems) {
    uint32_t Begin = LI->getOffsetInParent();
    uint32_t End = Begin + LI->getLayoutSize();
    End = std::min(SizeOf, End);
    ImmediateUsedBytes.set(Begin, End);
  }
}

ClassLayout::ClassLayout(std::unique_ptr<PDBSymbolTypeUDT> UDT)
    : ClassLayout(*UDT) {
  OwnedStorage = std::move(UDT);
}

uint32_t ClassLayout::immediatePadding() const {
  return SizeOf - ImmediateUsedBytes.count();
}

BaseClassLayout::BaseClassLayout(const UDTLayoutBase &Parent,
                                 uint32_t OffsetInParent, bool Elide,
                                 std::unique_ptr<PDBSymbolTypeBaseClass> B)
    : UDTLayoutBase(&Parent, *B, B->getName(), OffsetInParent, B->getLength(),
                    Elide),
      Base(std::move(B)) {
  if (isEmptyBase()) {
    // Special case an empty base so that it doesn't get treated as padding.
    UsedBytes.resize(1);
    UsedBytes.set(0);
  }
  IsVirtualBase = Base->isVirtualBaseClass();
}

void UDTLayoutBase::initializeChildren(const PDBSymbol &Sym) {
  // Handled bases first, followed by VTables, followed by data members,
  // followed by functions, followed by other.  This ordering is necessary
  // so that bases and vtables get initialized before any functions which
  // may override them.
  UniquePtrVector<PDBSymbolTypeBaseClass> Bases;
  UniquePtrVector<PDBSymbolTypeVTable> VTables;
  UniquePtrVector<PDBSymbolData> Members;
  UniquePtrVector<PDBSymbolTypeBaseClass> VirtualBaseSyms;

  auto Children = Sym.findAllChildren();
  while (auto Child = Children->getNext()) {
    if (auto Base = unique_dyn_cast<PDBSymbolTypeBaseClass>(Child)) {
      if (Base->isVirtualBaseClass())
        VirtualBaseSyms.push_back(std::move(Base));
      else
        Bases.push_back(std::move(Base));
    }
    else if (auto Data = unique_dyn_cast<PDBSymbolData>(Child)) {
      if (Data->getDataKind() == PDB_DataKind::Member)
        Members.push_back(std::move(Data));
      else
        Other.push_back(std::move(Data));
    } else if (auto VT = unique_dyn_cast<PDBSymbolTypeVTable>(Child))
      VTables.push_back(std::move(VT));
    else if (auto Func = unique_dyn_cast<PDBSymbolFunc>(Child))
      Funcs.push_back(std::move(Func));
    else {
      Other.push_back(std::move(Child));
    }
  }

  // We don't want to have any re-allocations in the list of bases, so make
  // sure to reserve enough space so that our ArrayRefs don't get invalidated.
  AllBases.reserve(Bases.size() + VirtualBaseSyms.size());

  // Only add non-virtual bases to the class first.  Only at the end of the
  // class, after all non-virtual bases and data members have been added do we
  // add virtual bases.  This way the offsets are correctly aligned when we go
  // to lay out virtual bases.
  for (auto &Base : Bases) {
    uint32_t Offset = Base->getOffset();
    // Non-virtual bases never get elided.
    auto BL = std::make_unique<BaseClassLayout>(*this, Offset, false,
                                                 std::move(Base));

    AllBases.push_back(BL.get());
    addChildToLayout(std::move(BL));
  }
  NonVirtualBases = AllBases;

  assert(VTables.size() <= 1);
  if (!VTables.empty()) {
    auto VTLayout =
        std::make_unique<VTableLayoutItem>(*this, std::move(VTables[0]));

    VTable = VTLayout.get();

    addChildToLayout(std::move(VTLayout));
  }

  for (auto &Data : Members) {
    auto DM = std::make_unique<DataMemberLayoutItem>(*this, std::move(Data));

    addChildToLayout(std::move(DM));
  }

  // Make sure add virtual bases before adding functions, since functions may be
  // overrides of virtual functions declared in a virtual base, so the VTables
  // and virtual intros need to be correctly initialized.
  for (auto &VB : VirtualBaseSyms) {
    int VBPO = VB->getVirtualBasePointerOffset();
    if (!hasVBPtrAtOffset(VBPO)) {
      if (auto VBP = VB->getRawSymbol().getVirtualBaseTableType()) {
        auto VBPL = std::make_unique<VBPtrLayoutItem>(*this, std::move(VBP),
                                                       VBPO, VBP->getLength());
        VBPtr = VBPL.get();
        addChildToLayout(std::move(VBPL));
      }
    }

    // Virtual bases always go at the end.  So just look for the last place we
    // ended when writing something, and put our virtual base there.
    // Note that virtual bases get elided unless this is a top-most derived
    // class.
    uint32_t Offset = UsedBytes.find_last() + 1;
    bool Elide = (Parent != nullptr);
    auto BL =
        std::make_unique<BaseClassLayout>(*this, Offset, Elide, std::move(VB));
    AllBases.push_back(BL.get());

    // Only lay this virtual base out directly inside of *this* class if this
    // is a top-most derived class.  Keep track of it regardless, but only
    // physically lay it out if it's a topmost derived class.
    addChildToLayout(std::move(BL));
  }
  VirtualBases = ArrayRef(AllBases).drop_front(NonVirtualBases.size());

  if (Parent != nullptr)
    LayoutSize = UsedBytes.find_last() + 1;
}

bool UDTLayoutBase::hasVBPtrAtOffset(uint32_t Off) const {
  if (VBPtr && VBPtr->getOffsetInParent() == Off)
    return true;
  for (BaseClassLayout *BL : AllBases) {
    if (BL->hasVBPtrAtOffset(Off - BL->getOffsetInParent()))
      return true;
  }
  return false;
}

void UDTLayoutBase::addChildToLayout(std::unique_ptr<LayoutItemBase> Child) {
  uint32_t Begin = Child->getOffsetInParent();

  if (!Child->isElided()) {
    BitVector ChildBytes = Child->usedBytes();

    // Suppose the child occupies 4 bytes starting at offset 12 in a 32 byte
    // class.  When we call ChildBytes.resize(32), the Child's storage will
    // still begin at offset 0, so we need to shift it left by offset bytes
    // to get it into the right position.
    ChildBytes.resize(UsedBytes.size());
    ChildBytes <<= Child->getOffsetInParent();
    UsedBytes |= ChildBytes;

    if (ChildBytes.count() > 0) {
      auto Loc = llvm::upper_bound(
          LayoutItems, Begin, [](uint32_t Off, const LayoutItemBase *Item) {
            return (Off < Item->getOffsetInParent());
          });

      LayoutItems.insert(Loc, Child.get());
    }
  }

  ChildStorage.push_back(std::move(Child));
}
