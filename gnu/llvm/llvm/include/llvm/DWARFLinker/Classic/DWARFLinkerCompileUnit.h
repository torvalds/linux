//===- DWARFLinkerCompileUnit.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_CLASSIC_DWARFLINKERCOMPILEUNIT_H
#define LLVM_DWARFLINKER_CLASSIC_DWARFLINKERCOMPILEUNIT_H

#include "llvm/ADT/AddressRanges.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include <optional>

namespace llvm {
namespace dwarf_linker {
namespace classic {

class DeclContext;

/// Mapped value in the address map is the offset to apply to the
/// linked address.
using RangesTy = AddressRangesMap;

// This structure keeps patch for the attribute and, optionally,
// the value of relocation which should be applied. Currently,
// only location attribute needs to have relocation: either to the
// function ranges if location attribute is of type 'loclist',
// either to the operand of DW_OP_addr/DW_OP_addrx if location attribute
// is of type 'exprloc'.
// ASSUMPTION: Location attributes of 'loclist' type containing 'exprloc'
//             with address expression operands are not supported yet.
struct PatchLocation {
  DIE::value_iterator I;
  int64_t RelocAdjustment = 0;

  PatchLocation() = default;
  PatchLocation(DIE::value_iterator I) : I(I) {}
  PatchLocation(DIE::value_iterator I, int64_t Reloc)
      : I(I), RelocAdjustment(Reloc) {}

  void set(uint64_t New) const {
    assert(I);
    const auto &Old = *I;
    assert(Old.getType() == DIEValue::isInteger);
    *I = DIEValue(Old.getAttribute(), Old.getForm(), DIEInteger(New));
  }

  uint64_t get() const {
    assert(I);
    return I->getDIEInteger().getValue();
  }
};

using RngListAttributesTy = SmallVector<PatchLocation>;
using LocListAttributesTy = SmallVector<PatchLocation>;

/// Stores all information relating to a compile unit, be it in its original
/// instance in the object file to its brand new cloned and generated DIE tree.
class CompileUnit {
public:
  /// Information gathered about a DIE in the object file.
  struct DIEInfo {
    /// Address offset to apply to the described entity.
    int64_t AddrAdjust;

    /// ODR Declaration context.
    DeclContext *Ctxt;

    /// Cloned version of that DIE.
    DIE *Clone;

    /// The index of this DIE's parent.
    uint32_t ParentIdx;

    /// Is the DIE part of the linked output?
    bool Keep : 1;

    /// Was this DIE's entity found in the map?
    bool InDebugMap : 1;

    /// Is this a pure forward declaration we can strip?
    bool Prune : 1;

    /// Does DIE transitively refer an incomplete decl?
    bool Incomplete : 1;

    /// Is DIE in the clang module scope?
    bool InModuleScope : 1;

    /// Is ODR marking done?
    bool ODRMarkingDone : 1;

    /// Is this a reference to a DIE that hasn't been cloned yet?
    bool UnclonedReference : 1;

    /// Is this a variable with a location attribute referencing address?
    bool HasLocationExpressionAddr : 1;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    LLVM_DUMP_METHOD void dump();
#endif // if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  };

  CompileUnit(DWARFUnit &OrigUnit, unsigned ID, bool CanUseODR,
              StringRef ClangModuleName)
      : OrigUnit(OrigUnit), ID(ID), ClangModuleName(ClangModuleName) {
    Info.resize(OrigUnit.getNumDIEs());

    auto CUDie = OrigUnit.getUnitDIE(false);
    if (!CUDie) {
      HasODR = false;
      return;
    }
    if (auto Lang = dwarf::toUnsigned(CUDie.find(dwarf::DW_AT_language)))
      HasODR = CanUseODR && (*Lang == dwarf::DW_LANG_C_plus_plus ||
                             *Lang == dwarf::DW_LANG_C_plus_plus_03 ||
                             *Lang == dwarf::DW_LANG_C_plus_plus_11 ||
                             *Lang == dwarf::DW_LANG_C_plus_plus_14 ||
                             *Lang == dwarf::DW_LANG_ObjC_plus_plus);
    else
      HasODR = false;
  }

  DWARFUnit &getOrigUnit() const { return OrigUnit; }

  unsigned getUniqueID() const { return ID; }

  void createOutputDIE() { NewUnit.emplace(OrigUnit.getUnitDIE().getTag()); }

  DIE *getOutputUnitDIE() const {
    if (NewUnit)
      return &const_cast<BasicDIEUnit &>(*NewUnit).getUnitDie();
    return nullptr;
  }

  dwarf::Tag getTag() const { return OrigUnit.getUnitDIE().getTag(); }

  bool hasODR() const { return HasODR; }
  bool isClangModule() const { return !ClangModuleName.empty(); }
  uint16_t getLanguage();
  /// Return the DW_AT_LLVM_sysroot of the compile unit or an empty StringRef.
  StringRef getSysRoot();

  const std::string &getClangModuleName() const { return ClangModuleName; }

  DIEInfo &getInfo(unsigned Idx) { return Info[Idx]; }
  const DIEInfo &getInfo(unsigned Idx) const { return Info[Idx]; }

  DIEInfo &getInfo(const DWARFDie &Die) {
    unsigned Idx = getOrigUnit().getDIEIndex(Die);
    return Info[Idx];
  }

  uint64_t getStartOffset() const { return StartOffset; }
  uint64_t getNextUnitOffset() const { return NextUnitOffset; }
  void setStartOffset(uint64_t DebugInfoSize) { StartOffset = DebugInfoSize; }

  std::optional<uint64_t> getLowPc() const { return LowPc; }
  uint64_t getHighPc() const { return HighPc; }
  bool hasLabelAt(uint64_t Addr) const { return Labels.count(Addr); }

  const RangesTy &getFunctionRanges() const { return Ranges; }

  const RngListAttributesTy &getRangesAttributes() { return RangeAttributes; }

  std::optional<PatchLocation> getUnitRangesAttribute() const {
    return UnitRangeAttribute;
  }

  const LocListAttributesTy &getLocationAttributes() const {
    return LocationAttributes;
  }

  /// Mark every DIE in this unit as kept. This function also
  /// marks variables as InDebugMap so that they appear in the
  /// reconstructed accelerator tables.
  void markEverythingAsKept();

  /// Compute the end offset for this unit. Must be called after the CU's DIEs
  /// have been cloned.  \returns the next unit offset (which is also the
  /// current debug_info section size).
  uint64_t computeNextUnitOffset(uint16_t DwarfVersion);

  /// Keep track of a forward reference to DIE \p Die in \p RefUnit by \p
  /// Attr. The attribute should be fixed up later to point to the absolute
  /// offset of \p Die in the debug_info section or to the canonical offset of
  /// \p Ctxt if it is non-null.
  void noteForwardReference(DIE *Die, const CompileUnit *RefUnit,
                            DeclContext *Ctxt, PatchLocation Attr);

  /// Apply all fixups recorded by noteForwardReference().
  void fixupForwardReferences();

  /// Add the low_pc of a label that is relocated by applying
  /// offset \p PCOffset.
  void addLabelLowPc(uint64_t LabelLowPc, int64_t PcOffset);

  /// Add a function range [\p LowPC, \p HighPC) that is relocated by applying
  /// offset \p PCOffset.
  void addFunctionRange(uint64_t LowPC, uint64_t HighPC, int64_t PCOffset);

  /// Keep track of a DW_AT_range attribute that we will need to patch up later.
  void noteRangeAttribute(const DIE &Die, PatchLocation Attr);

  /// Keep track of a location attribute pointing to a location list in the
  /// debug_loc section.
  void noteLocationAttribute(PatchLocation Attr);

  /// Add a name accelerator entry for \a Die with \a Name.
  void addNamespaceAccelerator(const DIE *Die, DwarfStringPoolEntryRef Name);

  /// Add a name accelerator entry for \a Die with \a Name.
  void addNameAccelerator(const DIE *Die, DwarfStringPoolEntryRef Name,
                          bool SkipPubnamesSection = false);

  /// Add various accelerator entries for \p Die with \p Name which is stored
  /// in the string table at \p Offset. \p Name must be an Objective-C
  /// selector.
  void addObjCAccelerator(const DIE *Die, DwarfStringPoolEntryRef Name,
                          bool SkipPubnamesSection = false);

  /// Add a type accelerator entry for \p Die with \p Name which is stored in
  /// the string table at \p Offset.
  void addTypeAccelerator(const DIE *Die, DwarfStringPoolEntryRef Name,
                          bool ObjcClassImplementation,
                          uint32_t QualifiedNameHash);

  struct AccelInfo {
    /// Name of the entry.
    DwarfStringPoolEntryRef Name;

    /// DIE this entry describes.
    const DIE *Die;

    /// Hash of the fully qualified name.
    uint32_t QualifiedNameHash;

    /// Emit this entry only in the apple_* sections.
    bool SkipPubSection;

    /// Is this an ObjC class implementation?
    bool ObjcClassImplementation;

    AccelInfo(DwarfStringPoolEntryRef Name, const DIE *Die,
              bool SkipPubSection = false)
        : Name(Name), Die(Die), SkipPubSection(SkipPubSection) {}

    AccelInfo(DwarfStringPoolEntryRef Name, const DIE *Die,
              uint32_t QualifiedNameHash, bool ObjCClassIsImplementation)
        : Name(Name), Die(Die), QualifiedNameHash(QualifiedNameHash),
          SkipPubSection(false),
          ObjcClassImplementation(ObjCClassIsImplementation) {}
  };

  const std::vector<AccelInfo> &getPubnames() const { return Pubnames; }
  const std::vector<AccelInfo> &getPubtypes() const { return Pubtypes; }
  const std::vector<AccelInfo> &getNamespaces() const { return Namespaces; }
  const std::vector<AccelInfo> &getObjC() const { return ObjC; }

  MCSymbol *getLabelBegin() { return LabelBegin; }
  void setLabelBegin(MCSymbol *S) { LabelBegin = S; }

private:
  DWARFUnit &OrigUnit;
  unsigned ID;
  std::vector<DIEInfo> Info; ///< DIE info indexed by DIE index.
  std::optional<BasicDIEUnit> NewUnit;
  MCSymbol *LabelBegin = nullptr;

  uint64_t StartOffset;
  uint64_t NextUnitOffset;

  std::optional<uint64_t> LowPc;
  uint64_t HighPc = 0;

  /// A list of attributes to fixup with the absolute offset of
  /// a DIE in the debug_info section.
  ///
  /// The offsets for the attributes in this array couldn't be set while
  /// cloning because for cross-cu forward references the target DIE's offset
  /// isn't known you emit the reference attribute.
  std::vector<
      std::tuple<DIE *, const CompileUnit *, DeclContext *, PatchLocation>>
      ForwardDIEReferences;

  /// The ranges in that map are the PC ranges for functions in this unit,
  /// associated with the PC offset to apply to the addresses to get
  /// the linked address.
  RangesTy Ranges;

  /// The DW_AT_low_pc of each DW_TAG_label.
  SmallDenseMap<uint64_t, uint64_t, 1> Labels;

  /// 'rnglist'(DW_AT_ranges, DW_AT_start_scope) attributes to patch after
  /// we have gathered all the unit's function addresses.
  /// @{
  RngListAttributesTy RangeAttributes;
  std::optional<PatchLocation> UnitRangeAttribute;
  /// @}

  /// Location attributes that need to be transferred from the
  /// original debug_loc section to the linked one. They are stored
  /// along with the PC offset that is to be applied to their
  /// function's address or to be applied to address operands of
  /// location expression.
  LocListAttributesTy LocationAttributes;

  /// Accelerator entries for the unit, both for the pub*
  /// sections and the apple* ones.
  /// @{
  std::vector<AccelInfo> Pubnames;
  std::vector<AccelInfo> Pubtypes;
  std::vector<AccelInfo> Namespaces;
  std::vector<AccelInfo> ObjC;
  /// @}

  /// Is this unit subject to the ODR rule?
  bool HasODR;

  /// The DW_AT_language of this unit.
  uint16_t Language = 0;

  /// The DW_AT_LLVM_sysroot of this unit.
  std::string SysRoot;

  /// If this is a Clang module, this holds the module's name.
  std::string ClangModuleName;
};

} // end of namespace classic
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_CLASSIC_DWARFLINKERCOMPILEUNIT_H
