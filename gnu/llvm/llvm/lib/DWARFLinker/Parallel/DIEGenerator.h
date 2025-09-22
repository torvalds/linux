//===- DIEGenerator.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_DIEGENERATOR_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_DIEGENERATOR_H

#include "DWARFLinkerGlobalData.h"
#include "DWARFLinkerUnit.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/Support/LEB128.h"

namespace llvm {
namespace dwarf_linker {
namespace parallel {

/// This class is a helper to create output DIE tree.
class DIEGenerator {
public:
  DIEGenerator(BumpPtrAllocator &Allocator, DwarfUnit &CU)
      : Allocator(Allocator), CU(CU) {}

  DIEGenerator(DIE *OutputDIE, BumpPtrAllocator &Allocator, DwarfUnit &CU)
      : Allocator(Allocator), CU(CU), OutputDIE(OutputDIE) {}

  /// Creates a DIE of specified tag \p DieTag and \p OutOffset.
  DIE *createDIE(dwarf::Tag DieTag, uint32_t OutOffset) {
    OutputDIE = DIE::get(Allocator, DieTag);

    OutputDIE->setOffset(OutOffset);

    return OutputDIE;
  }

  DIE *getDIE() { return OutputDIE; }

  /// Adds a specified \p Child to the current DIE.
  void addChild(DIE *Child) {
    assert(Child != nullptr);
    assert(OutputDIE != nullptr);

    OutputDIE->addChild(Child);
  }

  /// Adds specified scalar attribute to the current DIE.
  std::pair<DIEValue &, size_t> addScalarAttribute(dwarf::Attribute Attr,
                                                   dwarf::Form AttrForm,
                                                   uint64_t Value) {
    return addAttribute(Attr, AttrForm, DIEInteger(Value));
  }

  /// Adds specified location attribute to the current DIE.
  std::pair<DIEValue &, size_t> addLocationAttribute(dwarf::Attribute Attr,
                                                     dwarf::Form AttrForm,
                                                     ArrayRef<uint8_t> Bytes) {
    DIELoc *Loc = new (Allocator) DIELoc;
    for (auto Byte : Bytes)
      static_cast<DIEValueList *>(Loc)->addValue(
          Allocator, static_cast<dwarf::Attribute>(0), dwarf::DW_FORM_data1,
          DIEInteger(Byte));
    Loc->setSize(Bytes.size());

    return addAttribute(Attr, AttrForm, Loc);
  }

  /// Adds specified block or exprloc attribute to the current DIE.
  std::pair<DIEValue &, size_t> addBlockAttribute(dwarf::Attribute Attr,
                                                  dwarf::Form AttrForm,
                                                  ArrayRef<uint8_t> Bytes) {
    // The expression location data might be updated and exceed the original
    // size. Check whether the new data fits into the original form.
    assert((AttrForm == dwarf::DW_FORM_block) ||
           (AttrForm == dwarf::DW_FORM_exprloc) ||
           (AttrForm == dwarf::DW_FORM_block1 && Bytes.size() <= UINT8_MAX) ||
           (AttrForm == dwarf::DW_FORM_block2 && Bytes.size() <= UINT16_MAX) ||
           (AttrForm == dwarf::DW_FORM_block4 && Bytes.size() <= UINT32_MAX));

    DIEBlock *Block = new (Allocator) DIEBlock;
    for (auto Byte : Bytes)
      static_cast<DIEValueList *>(Block)->addValue(
          Allocator, static_cast<dwarf::Attribute>(0), dwarf::DW_FORM_data1,
          DIEInteger(Byte));
    Block->setSize(Bytes.size());

    return addAttribute(Attr, AttrForm, Block);
  }

  /// Adds specified location list attribute to the current DIE.
  std::pair<DIEValue &, size_t> addLocListAttribute(dwarf::Attribute Attr,
                                                    dwarf::Form AttrForm,
                                                    uint64_t Value) {
    return addAttribute(Attr, AttrForm, DIELocList(Value));
  }

  /// Adds indexed string attribute.
  std::pair<DIEValue &, size_t> addIndexedStringAttribute(dwarf::Attribute Attr,
                                                          dwarf::Form AttrForm,
                                                          uint64_t Idx) {
    assert(AttrForm == dwarf::DW_FORM_strx);
    return addAttribute(Attr, AttrForm, DIEInteger(Idx));
  }

  /// Adds string attribute with dummy offset to the current DIE.
  std::pair<DIEValue &, size_t>
  addStringPlaceholderAttribute(dwarf::Attribute Attr, dwarf::Form AttrForm) {
    assert(AttrForm == dwarf::DW_FORM_strp ||
           AttrForm == dwarf::DW_FORM_line_strp);
    return addAttribute(Attr, AttrForm, DIEInteger(0xBADDEF));
  }

  /// Adds inplace string attribute to the current DIE.
  std::pair<DIEValue &, size_t> addInplaceString(dwarf::Attribute Attr,
                                                 StringRef String) {
    DIEBlock *Block = new (Allocator) DIEBlock;
    for (auto Byte : String.bytes())
      static_cast<DIEValueList *>(Block)->addValue(
          Allocator, static_cast<dwarf::Attribute>(0), dwarf::DW_FORM_data1,
          DIEInteger(Byte));

    static_cast<DIEValueList *>(Block)->addValue(
        Allocator, static_cast<dwarf::Attribute>(0), dwarf::DW_FORM_data1,
        DIEInteger(0));
    Block->setSize(String.size() + 1);

    DIEValue &ValueRef =
        *OutputDIE->addValue(Allocator, Attr, dwarf::DW_FORM_string, Block);
    return std::pair<DIEValue &, size_t>(ValueRef, String.size() + 1);
  }

  /// Creates appreviations for the current DIE. Returns value of
  /// abbreviation number. Updates offsets with the size of abbreviation
  /// number.
  size_t finalizeAbbreviations(bool CHILDREN_yes,
                               OffsetsPtrVector *OffsetsList) {
    // Create abbreviations for output DIE.
    DIEAbbrev NewAbbrev = OutputDIE->generateAbbrev();
    if (CHILDREN_yes)
      NewAbbrev.setChildrenFlag(dwarf::DW_CHILDREN_yes);

    CU.assignAbbrev(NewAbbrev);
    OutputDIE->setAbbrevNumber(NewAbbrev.getNumber());

    size_t AbbrevNumberSize = getULEB128Size(OutputDIE->getAbbrevNumber());

    // Add size of abbreviation number to the offsets.
    if (OffsetsList != nullptr) {
      for (uint64_t *OffsetPtr : *OffsetsList)
        *OffsetPtr += AbbrevNumberSize;
    }

    return AbbrevNumberSize;
  }

protected:
  template <typename T>
  std::pair<DIEValue &, size_t> addAttribute(dwarf::Attribute Attr,
                                             dwarf::Form AttrForm, T &&Value) {
    DIEValue &ValueRef =
        *OutputDIE->addValue(Allocator, Attr, AttrForm, std::forward<T>(Value));
    unsigned ValueSize = ValueRef.sizeOf(CU.getFormParams());
    return std::pair<DIEValue &, size_t>(ValueRef, ValueSize);
  }

  // Allocator for output DIEs and values.
  BumpPtrAllocator &Allocator;

  // Unit for the output DIE.
  DwarfUnit &CU;

  // OutputDIE.
  DIE *OutputDIE = nullptr;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_DIEGENERATOR_H
