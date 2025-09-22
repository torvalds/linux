//===- UDTLayout.h - UDT layout info ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_UDTLAYOUT_H
#define LLVM_DEBUGINFO_PDB_UDTLAYOUT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBaseClass.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTable.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace pdb {

class BaseClassLayout;
class ClassLayout;
class UDTLayoutBase;

class LayoutItemBase {
public:
  LayoutItemBase(const UDTLayoutBase *Parent, const PDBSymbol *Symbol,
                 const std::string &Name, uint32_t OffsetInParent,
                 uint32_t Size, bool IsElided);
  virtual ~LayoutItemBase() = default;

  uint32_t deepPaddingSize() const;
  virtual uint32_t immediatePadding() const { return 0; }
  virtual uint32_t tailPadding() const;

  const UDTLayoutBase *getParent() const { return Parent; }
  StringRef getName() const { return Name; }
  uint32_t getOffsetInParent() const { return OffsetInParent; }
  uint32_t getSize() const { return SizeOf; }
  uint32_t getLayoutSize() const { return LayoutSize; }
  const PDBSymbol *getSymbol() const { return Symbol; }
  const BitVector &usedBytes() const { return UsedBytes; }
  bool isElided() const { return IsElided; }
  virtual bool isVBPtr() const { return false; }

  uint32_t containsOffset(uint32_t Off) const {
    uint32_t Begin = getOffsetInParent();
    uint32_t End = Begin + getSize();
    return (Off >= Begin && Off < End);
  }

protected:
  const PDBSymbol *Symbol = nullptr;
  const UDTLayoutBase *Parent = nullptr;
  BitVector UsedBytes;
  std::string Name;
  uint32_t OffsetInParent = 0;
  uint32_t SizeOf = 0;
  uint32_t LayoutSize = 0;
  bool IsElided = false;
};

class VBPtrLayoutItem : public LayoutItemBase {
public:
  VBPtrLayoutItem(const UDTLayoutBase &Parent,
                  std::unique_ptr<PDBSymbolTypeBuiltin> Sym, uint32_t Offset,
                  uint32_t Size);

  bool isVBPtr() const override { return true; }

private:
  std::unique_ptr<PDBSymbolTypeBuiltin> Type;
};

class DataMemberLayoutItem : public LayoutItemBase {
public:
  DataMemberLayoutItem(const UDTLayoutBase &Parent,
                       std::unique_ptr<PDBSymbolData> DataMember);

  const PDBSymbolData &getDataMember();
  bool hasUDTLayout() const;
  const ClassLayout &getUDTLayout() const;

private:
  std::unique_ptr<PDBSymbolData> DataMember;
  std::unique_ptr<ClassLayout> UdtLayout;
};

class VTableLayoutItem : public LayoutItemBase {
public:
  VTableLayoutItem(const UDTLayoutBase &Parent,
                   std::unique_ptr<PDBSymbolTypeVTable> VTable);

  uint32_t getElementSize() const { return ElementSize; }

private:
  uint32_t ElementSize = 0;
  std::unique_ptr<PDBSymbolTypeVTable> VTable;
};

class UDTLayoutBase : public LayoutItemBase {
  template <typename T> using UniquePtrVector = std::vector<std::unique_ptr<T>>;

public:
  UDTLayoutBase(const UDTLayoutBase *Parent, const PDBSymbol &Sym,
                const std::string &Name, uint32_t OffsetInParent, uint32_t Size,
                bool IsElided);

  uint32_t tailPadding() const override;
  ArrayRef<LayoutItemBase *> layout_items() const { return LayoutItems; }
  ArrayRef<BaseClassLayout *> bases() const { return AllBases; }
  ArrayRef<BaseClassLayout *> regular_bases() const { return NonVirtualBases; }
  ArrayRef<BaseClassLayout *> virtual_bases() const { return VirtualBases; }
  uint32_t directVirtualBaseCount() const { return DirectVBaseCount; }
  ArrayRef<std::unique_ptr<PDBSymbolFunc>> funcs() const { return Funcs; }
  ArrayRef<std::unique_ptr<PDBSymbol>> other_items() const { return Other; }

protected:
  bool hasVBPtrAtOffset(uint32_t Off) const;
  void initializeChildren(const PDBSymbol &Sym);

  void addChildToLayout(std::unique_ptr<LayoutItemBase> Child);

  uint32_t DirectVBaseCount = 0;

  UniquePtrVector<PDBSymbol> Other;
  UniquePtrVector<PDBSymbolFunc> Funcs;
  UniquePtrVector<LayoutItemBase> ChildStorage;
  std::vector<LayoutItemBase *> LayoutItems;

  std::vector<BaseClassLayout *> AllBases;
  ArrayRef<BaseClassLayout *> NonVirtualBases;
  ArrayRef<BaseClassLayout *> VirtualBases;

  VTableLayoutItem *VTable = nullptr;
  VBPtrLayoutItem *VBPtr = nullptr;
};

class BaseClassLayout : public UDTLayoutBase {
public:
  BaseClassLayout(const UDTLayoutBase &Parent, uint32_t OffsetInParent,
                  bool Elide, std::unique_ptr<PDBSymbolTypeBaseClass> Base);

  const PDBSymbolTypeBaseClass &getBase() const { return *Base; }
  bool isVirtualBase() const { return IsVirtualBase; }
  bool isEmptyBase() { return SizeOf == 1 && LayoutSize == 0; }

private:
  std::unique_ptr<PDBSymbolTypeBaseClass> Base;
  bool IsVirtualBase;
};

class ClassLayout : public UDTLayoutBase {
public:
  explicit ClassLayout(const PDBSymbolTypeUDT &UDT);
  explicit ClassLayout(std::unique_ptr<PDBSymbolTypeUDT> UDT);

  ClassLayout(ClassLayout &&Other) = default;

  const PDBSymbolTypeUDT &getClass() const { return UDT; }
  uint32_t immediatePadding() const override;

private:
  BitVector ImmediateUsedBytes;
  std::unique_ptr<PDBSymbolTypeUDT> OwnedStorage;
  const PDBSymbolTypeUDT &UDT;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_UDTLAYOUT_H
