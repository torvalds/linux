//===- AddressesMap.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_ADDRESSESMAP_H
#define LLVM_DWARFLINKER_ADDRESSESMAP_H

#include "llvm/ADT/AddressRanges.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include <cstdint>

namespace llvm {
namespace dwarf_linker {

/// Mapped value in the address map is the offset to apply to the
/// linked address.
using RangesTy = AddressRangesMap;

/// AddressesMap represents information about valid addresses used
/// by debug information. Valid addresses are those which points to
/// live code sections. i.e. relocations for these addresses point
/// into sections which would be/are placed into resulting binary.
class AddressesMap {
public:
  virtual ~AddressesMap() = default;

  /// Checks that there are valid relocations in the .debug_info
  /// section.
  virtual bool hasValidRelocs() = 0;

  /// Checks that the specified DWARF expression operand \p Op references live
  /// code section and returns the relocation adjustment value (to get the
  /// linked address this value might be added to the source expression operand
  /// address). Print debug output if \p Verbose is true.
  /// \returns relocation adjustment value or std::nullopt if there is no
  /// corresponding live address.
  virtual std::optional<int64_t> getExprOpAddressRelocAdjustment(
      DWARFUnit &U, const DWARFExpression::Operation &Op, uint64_t StartOffset,
      uint64_t EndOffset, bool Verbose) = 0;

  /// Checks that the specified subprogram \p DIE references the live code
  /// section and returns the relocation adjustment value (to get the linked
  /// address this value might be added to the source subprogram address).
  /// Allowed kinds of input DIE: DW_TAG_subprogram, DW_TAG_label.
  /// Print debug output if \p Verbose is true.
  /// \returns relocation adjustment value or std::nullopt if there is no
  /// corresponding live address.
  virtual std::optional<int64_t>
  getSubprogramRelocAdjustment(const DWARFDie &DIE, bool Verbose) = 0;

  // Returns the library install name associated to the AddessesMap.
  virtual std::optional<StringRef> getLibraryInstallName() = 0;

  /// Apply the valid relocations to the buffer \p Data, taking into
  /// account that Data is at \p BaseOffset in the .debug_info section.
  ///
  /// \returns true whether any reloc has been applied.
  virtual bool applyValidRelocs(MutableArrayRef<char> Data, uint64_t BaseOffset,
                                bool IsLittleEndian) = 0;

  /// Check if the linker needs to gather and save relocation info.
  virtual bool needToSaveValidRelocs() = 0;

  /// Update and save relocation values to be serialized
  virtual void updateAndSaveValidRelocs(bool IsDWARF5,
                                        uint64_t OriginalUnitOffset,
                                        int64_t LinkedOffset,
                                        uint64_t StartOffset,
                                        uint64_t EndOffset) = 0;

  /// Update the valid relocations that used OriginalUnitOffset as the compile
  /// unit offset, and update their values to reflect OutputUnitOffset.
  virtual void updateRelocationsWithUnitOffset(uint64_t OriginalUnitOffset,
                                               uint64_t OutputUnitOffset) = 0;

  /// Erases all data.
  virtual void clear() = 0;

  /// This function checks whether variable has DWARF expression containing
  /// operation referencing live address(f.e. DW_OP_addr, DW_OP_addrx...).
  /// \returns first is true if the expression has an operation referencing an
  /// address.
  ///          second is the relocation adjustment value if the live address is
  ///          referenced.
  std::pair<bool, std::optional<int64_t>>
  getVariableRelocAdjustment(const DWARFDie &DIE, bool Verbose) {
    assert((DIE.getTag() == dwarf::DW_TAG_variable ||
            DIE.getTag() == dwarf::DW_TAG_constant) &&
           "Wrong type of input die");

    const auto *Abbrev = DIE.getAbbreviationDeclarationPtr();

    // Check if DIE has DW_AT_location attribute.
    DWARFUnit *U = DIE.getDwarfUnit();
    std::optional<uint32_t> LocationIdx =
        Abbrev->findAttributeIndex(dwarf::DW_AT_location);
    if (!LocationIdx)
      return std::make_pair(false, std::nullopt);

    // Get offset to the DW_AT_location attribute.
    uint64_t AttrOffset =
        Abbrev->getAttributeOffsetFromIndex(*LocationIdx, DIE.getOffset(), *U);

    // Get value of the DW_AT_location attribute.
    std::optional<DWARFFormValue> LocationValue =
        Abbrev->getAttributeValueFromOffset(*LocationIdx, AttrOffset, *U);
    if (!LocationValue)
      return std::make_pair(false, std::nullopt);

    // Check that DW_AT_location attribute is of 'exprloc' class.
    // Handling value of location expressions for attributes of 'loclist'
    // class is not implemented yet.
    std::optional<ArrayRef<uint8_t>> Expr = LocationValue->getAsBlock();
    if (!Expr)
      return std::make_pair(false, std::nullopt);

    // Parse 'exprloc' expression.
    DataExtractor Data(toStringRef(*Expr), U->getContext().isLittleEndian(),
                       U->getAddressByteSize());
    DWARFExpression Expression(Data, U->getAddressByteSize(),
                               U->getFormParams().Format);

    bool HasLocationAddress = false;
    uint64_t CurExprOffset = 0;
    for (DWARFExpression::iterator It = Expression.begin();
         It != Expression.end(); ++It) {
      DWARFExpression::iterator NextIt = It;
      ++NextIt;

      const DWARFExpression::Operation &Op = *It;
      switch (Op.getCode()) {
      case dwarf::DW_OP_const2u:
      case dwarf::DW_OP_const4u:
      case dwarf::DW_OP_const8u:
      case dwarf::DW_OP_const2s:
      case dwarf::DW_OP_const4s:
      case dwarf::DW_OP_const8s:
        if (NextIt == Expression.end() || !isTlsAddressCode(NextIt->getCode()))
          break;
        [[fallthrough]];
      case dwarf::DW_OP_addr: {
        HasLocationAddress = true;
        // Check relocation for the address.
        if (std::optional<int64_t> RelocAdjustment =
                getExprOpAddressRelocAdjustment(
                    *U, Op, AttrOffset + CurExprOffset,
                    AttrOffset + Op.getEndOffset(), Verbose))
          return std::make_pair(HasLocationAddress, *RelocAdjustment);
      } break;
      case dwarf::DW_OP_constx:
      case dwarf::DW_OP_addrx: {
        HasLocationAddress = true;
        if (std::optional<uint64_t> AddressOffset =
                DIE.getDwarfUnit()->getIndexedAddressOffset(
                    Op.getRawOperand(0))) {
          // Check relocation for the address.
          if (std::optional<int64_t> RelocAdjustment =
                  getExprOpAddressRelocAdjustment(
                      *U, Op, *AddressOffset,
                      *AddressOffset + DIE.getDwarfUnit()->getAddressByteSize(),
                      Verbose))
            return std::make_pair(HasLocationAddress, *RelocAdjustment);
        }
      } break;
      default: {
        // Nothing to do.
      } break;
      }
      CurExprOffset = Op.getEndOffset();
    }

    return std::make_pair(HasLocationAddress, std::nullopt);
  }

protected:
  inline bool isTlsAddressCode(uint8_t DW_OP_Code) {
    return DW_OP_Code == dwarf::DW_OP_form_tls_address ||
           DW_OP_Code == dwarf::DW_OP_GNU_push_tls_address;
  }
};

} // namespace dwarf_linker
} // end namespace llvm

#endif // LLVM_DWARFLINKER_ADDRESSESMAP_H
