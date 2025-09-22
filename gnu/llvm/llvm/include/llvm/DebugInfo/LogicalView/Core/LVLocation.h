//===-- LVLocation.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVOperation and LVLocation classes, which are used
// to describe variable locations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLOCATION_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLOCATION_H

#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"

namespace llvm {
namespace logicalview {

using LVLineRange = std::pair<LVLine *, LVLine *>;

// The DW_AT_data_member_location attribute is a simple member offset.
const LVSmall LVLocationMemberOffset = 0;

class LVOperation final {
  // To describe an operation:
  // OpCode
  // Operands[0]: First operand.
  // Operands[1]: Second operand.
  //   OP_bregx, OP_bit_piece, OP_[GNU_]const_type,
  //   OP_[GNU_]deref_type, OP_[GNU_]entry_value, OP_implicit_value,
  //   OP_[GNU_]implicit_pointer, OP_[GNU_]regval_type, OP_xderef_type.
  LVSmall Opcode = 0;
  SmallVector<uint64_t> Operands;

public:
  LVOperation() = delete;
  LVOperation(LVSmall Opcode, ArrayRef<LVUnsigned> Operands)
      : Opcode(Opcode), Operands(Operands) {}
  LVOperation(const LVOperation &) = delete;
  LVOperation &operator=(const LVOperation &) = delete;
  ~LVOperation() = default;

  LVSmall getOpcode() const { return Opcode; }
  std::string getOperandsDWARFInfo();
  std::string getOperandsCodeViewInfo();

  void print(raw_ostream &OS, bool Full = true) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() { print(dbgs()); }
#endif
};

class LVLocation : public LVObject {
  enum class Property {
    IsAddressRange,
    IsBaseClassOffset,
    IsBaseClassStep,
    IsClassOffset,
    IsFixedAddress,
    IsLocationSimple,
    IsGapEntry,
    IsOperation,
    IsOperationList,
    IsRegister,
    IsStackOffset,
    IsDiscardedRange,
    IsInvalidRange,
    IsInvalidLower,
    IsInvalidUpper,
    IsCallSite,
    LastEntry
  };
  // Typed bitvector with properties for this location.
  LVProperties<Property> Properties;

  // True if the location it is associated with a debug range.
  bool hasAssociatedRange() const {
    return !getIsClassOffset() && !getIsDiscardedRange();
  }

protected:
  // Line numbers associated with locations ranges.
  LVLine *LowerLine = nullptr;
  LVLine *UpperLine = nullptr;

  // Active range:
  // LowPC: an offset from an applicable base address, not a PC value.
  // HighPC: an offset from an applicable base address, or a length.
  LVAddress LowPC = 0;
  LVAddress HighPC = 0;

  void setKind();

public:
  LVLocation() : LVObject() { setIsLocation(); }
  LVLocation(const LVLocation &) = delete;
  LVLocation &operator=(const LVLocation &) = delete;
  virtual ~LVLocation() = default;

  PROPERTY(Property, IsAddressRange);
  PROPERTY(Property, IsBaseClassOffset);
  PROPERTY(Property, IsBaseClassStep);
  PROPERTY_1(Property, IsClassOffset, IsLocationSimple);
  PROPERTY_1(Property, IsFixedAddress, IsLocationSimple);
  PROPERTY(Property, IsLocationSimple);
  PROPERTY(Property, IsGapEntry);
  PROPERTY(Property, IsOperationList);
  PROPERTY(Property, IsOperation);
  PROPERTY(Property, IsRegister);
  PROPERTY_1(Property, IsStackOffset, IsLocationSimple);
  PROPERTY(Property, IsDiscardedRange);
  PROPERTY(Property, IsInvalidRange);
  PROPERTY(Property, IsInvalidLower);
  PROPERTY(Property, IsInvalidUpper);
  PROPERTY(Property, IsCallSite);

  const char *kind() const override;
  // Mark the locations that have only DW_OP_fbreg as stack offset based.
  virtual void updateKind() {}

  // Line numbers for locations.
  const LVLine *getLowerLine() const { return LowerLine; }
  void setLowerLine(LVLine *Line) { LowerLine = Line; }
  const LVLine *getUpperLine() const { return UpperLine; }
  void setUpperLine(LVLine *Line) { UpperLine = Line; }

  // Addresses for locations.
  LVAddress getLowerAddress() const override { return LowPC; }
  void setLowerAddress(LVAddress Address) override { LowPC = Address; }
  LVAddress getUpperAddress() const override { return HighPC; }
  void setUpperAddress(LVAddress Address) override { HighPC = Address; }

  std::string getIntervalInfo() const;

  bool validateRanges();

  // In order to calculate a symbol coverage (percentage), take the ranges
  // and obtain the number of units (bytes) covered by those ranges. We can't
  // use the line numbers, because they can be zero or invalid.
  // We return:
  //   false: No locations or multiple locations.
  //   true: a single location.
  static bool calculateCoverage(LVLocations *Locations, unsigned &Factor,
                                float &Percentage);

  virtual void addObject(LVAddress LowPC, LVAddress HighPC,
                         LVUnsigned SectionOffset, uint64_t LocDescOffset) {}
  virtual void addObject(LVSmall Opcode, ArrayRef<LVUnsigned> Operands) {}

  static void print(LVLocations *Locations, raw_ostream &OS, bool Full = true);
  void printInterval(raw_ostream &OS, bool Full = true) const;
  void printRaw(raw_ostream &OS, bool Full = true) const;
  virtual void printRawExtra(raw_ostream &OS, bool Full = true) const {}

  void print(raw_ostream &OS, bool Full = true) const override;
  void printExtra(raw_ostream &OS, bool Full = true) const override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const override { print(dbgs()); }
#endif
};

class LVLocationSymbol final : public LVLocation {
  // Location descriptors for the active range.
  std::unique_ptr<LVOperations> Entries;

  void updateKind() override;

public:
  LVLocationSymbol() : LVLocation() {}
  LVLocationSymbol(const LVLocationSymbol &) = delete;
  LVLocationSymbol &operator=(const LVLocationSymbol &) = delete;
  ~LVLocationSymbol() = default;

  void addObject(LVAddress LowPC, LVAddress HighPC, LVUnsigned SectionOffset,
                 uint64_t LocDescOffset) override;
  void addObject(LVSmall Opcode, ArrayRef<LVUnsigned> Operands) override;

  void printRawExtra(raw_ostream &OS, bool Full = true) const override;
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLOCATION_H
