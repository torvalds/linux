//===- DWARFContext.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===/

#ifndef LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H
#define LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Host.h"
#include <cstdint>
#include <memory>
#include <mutex>

namespace llvm {

class MemoryBuffer;
class AppleAcceleratorTable;
class DWARFCompileUnit;
class DWARFDebugAbbrev;
class DWARFDebugAranges;
class DWARFDebugFrame;
class DWARFDebugLoc;
class DWARFDebugMacro;
class DWARFDebugNames;
class DWARFGdbIndex;
class DWARFTypeUnit;
class DWARFUnitIndex;

/// DWARFContext
/// This data structure is the top level entity that deals with dwarf debug
/// information parsing. The actual data is supplied through DWARFObj.
class DWARFContext : public DIContext {
public:
  /// DWARFContextState
  /// This structure contains all member variables for DWARFContext that need
  /// to be protected in multi-threaded environments. Threading support can be
  /// enabled by setting the ThreadSafe to true when constructing a
  /// DWARFContext to allow DWARRContext to be able to be used in a
  /// multi-threaded environment, or not enabled to allow for maximum
  /// performance in single threaded environments.
  class DWARFContextState {
  protected:
    /// Helper enum to distinguish between macro[.dwo] and macinfo[.dwo]
    /// section.
    enum MacroSecType {
      MacinfoSection,
      MacinfoDwoSection,
      MacroSection,
      MacroDwoSection
    };

    DWARFContext &D;
  public:
    DWARFContextState(DWARFContext &DC) : D(DC) {}
    virtual ~DWARFContextState() = default;
    virtual DWARFUnitVector &getNormalUnits() = 0;
    virtual DWARFUnitVector &getDWOUnits(bool Lazy = false) = 0;
    virtual const DWARFDebugAbbrev *getDebugAbbrevDWO() = 0;
    virtual const DWARFUnitIndex &getCUIndex() = 0;
    virtual const DWARFUnitIndex &getTUIndex() = 0;
    virtual DWARFGdbIndex &getGdbIndex() = 0;
    virtual const DWARFDebugAbbrev *getDebugAbbrev() = 0;
    virtual const DWARFDebugLoc *getDebugLoc() = 0;
    virtual const DWARFDebugAranges *getDebugAranges() = 0;
    virtual Expected<const DWARFDebugLine::LineTable *>
        getLineTableForUnit(DWARFUnit *U,
                            function_ref<void(Error)> RecoverableErrHandler) = 0;
    virtual void clearLineTableForUnit(DWARFUnit *U) = 0;
    virtual Expected<const DWARFDebugFrame *> getDebugFrame() = 0;
    virtual Expected<const DWARFDebugFrame *> getEHFrame() = 0;
    virtual const DWARFDebugMacro *getDebugMacinfo() = 0;
    virtual const DWARFDebugMacro *getDebugMacinfoDWO() = 0;
    virtual const DWARFDebugMacro *getDebugMacro() = 0;
    virtual const DWARFDebugMacro *getDebugMacroDWO() = 0;
    virtual const DWARFDebugNames &getDebugNames() = 0;
    virtual const AppleAcceleratorTable &getAppleNames() = 0;
    virtual const AppleAcceleratorTable &getAppleTypes() = 0;
    virtual const AppleAcceleratorTable &getAppleNamespaces() = 0;
    virtual const AppleAcceleratorTable &getAppleObjC() = 0;
    virtual std::shared_ptr<DWARFContext>
        getDWOContext(StringRef AbsolutePath) = 0;
    virtual const DenseMap<uint64_t, DWARFTypeUnit *> &
    getTypeUnitMap(bool IsDWO) = 0;
    virtual bool isThreadSafe() const = 0;

    /// Parse a macro[.dwo] or macinfo[.dwo] section.
    std::unique_ptr<DWARFDebugMacro>
    parseMacroOrMacinfo(MacroSecType SectionType);

  };
  friend class DWARFContextState;

private:
  /// All important state for a DWARFContext that needs to be threadsafe needs
  /// to go into DWARFContextState.
  std::unique_ptr<DWARFContextState> State;

  /// The maximum DWARF version of all units.
  unsigned MaxVersion = 0;

  std::function<void(Error)> RecoverableErrorHandler =
      WithColor::defaultErrorHandler;
  std::function<void(Error)> WarningHandler = WithColor::defaultWarningHandler;

  /// Read compile units from the debug_info.dwo section (if necessary)
  /// and type units from the debug_types.dwo section (if necessary)
  /// and store them in DWOUnits.
  /// If \p Lazy is true, set up to parse but don't actually parse them.
  enum { EagerParse = false, LazyParse = true };
  DWARFUnitVector &getDWOUnits(bool Lazy = false);

  std::unique_ptr<const DWARFObject> DObj;

  // When set parses debug_info.dwo/debug_abbrev.dwo manually and populates CU
  // Index, and TU Index for DWARF5.
  bool ParseCUTUIndexManually = false;

public:
  DWARFContext(std::unique_ptr<const DWARFObject> DObj,
               std::string DWPName = "",
               std::function<void(Error)> RecoverableErrorHandler =
                   WithColor::defaultErrorHandler,
               std::function<void(Error)> WarningHandler =
                   WithColor::defaultWarningHandler,
               bool ThreadSafe = false);
  ~DWARFContext() override;

  DWARFContext(DWARFContext &) = delete;
  DWARFContext &operator=(DWARFContext &) = delete;

  const DWARFObject &getDWARFObj() const { return *DObj; }

  static bool classof(const DIContext *DICtx) {
    return DICtx->getKind() == CK_DWARF;
  }

  /// Dump a textual representation to \p OS. If any \p DumpOffsets are present,
  /// dump only the record at the specified offset.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts,
            std::array<std::optional<uint64_t>, DIDT_ID_Count> DumpOffsets);

  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) override {
    std::array<std::optional<uint64_t>, DIDT_ID_Count> DumpOffsets;
    dump(OS, DumpOpts, DumpOffsets);
  }

  bool verify(raw_ostream &OS, DIDumpOptions DumpOpts = {}) override;

  using unit_iterator_range = DWARFUnitVector::iterator_range;
  using compile_unit_range = DWARFUnitVector::compile_unit_range;

  /// Get units from .debug_info in this context.
  unit_iterator_range info_section_units() {
    DWARFUnitVector &NormalUnits = State->getNormalUnits();
    return unit_iterator_range(NormalUnits.begin(),
                               NormalUnits.begin() +
                                   NormalUnits.getNumInfoUnits());
  }

  const DWARFUnitVector &getNormalUnitsVector() {
    return State->getNormalUnits();
  }

  /// Get units from .debug_types in this context.
  unit_iterator_range types_section_units() {
    DWARFUnitVector &NormalUnits = State->getNormalUnits();
    return unit_iterator_range(
        NormalUnits.begin() + NormalUnits.getNumInfoUnits(), NormalUnits.end());
  }

  /// Get compile units in this context.
  compile_unit_range compile_units() {
    return make_filter_range(info_section_units(), isCompileUnit);
  }

  // If you want type_units(), it'll need to be a concat iterator of a filter of
  // TUs in info_section + all the (all type) units in types_section

  /// Get all normal compile/type units in this context.
  unit_iterator_range normal_units() {
    DWARFUnitVector &NormalUnits = State->getNormalUnits();
    return unit_iterator_range(NormalUnits.begin(), NormalUnits.end());
  }

  /// Get units from .debug_info..dwo in the DWO context.
  unit_iterator_range dwo_info_section_units() {
    DWARFUnitVector &DWOUnits = State->getDWOUnits();
    return unit_iterator_range(DWOUnits.begin(),
                               DWOUnits.begin() + DWOUnits.getNumInfoUnits());
  }

  const DWARFUnitVector &getDWOUnitsVector() {
    return State->getDWOUnits();
  }

  /// Get units from .debug_types.dwo in the DWO context.
  unit_iterator_range dwo_types_section_units() {
    DWARFUnitVector &DWOUnits = State->getDWOUnits();
    return unit_iterator_range(DWOUnits.begin() + DWOUnits.getNumInfoUnits(),
                               DWOUnits.end());
  }

  /// Get compile units in the DWO context.
  compile_unit_range dwo_compile_units() {
    return make_filter_range(dwo_info_section_units(), isCompileUnit);
  }

  // If you want dwo_type_units(), it'll need to be a concat iterator of a
  // filter of TUs in dwo_info_section + all the (all type) units in
  // dwo_types_section.

  /// Get all units in the DWO context.
  unit_iterator_range dwo_units() {
    DWARFUnitVector &DWOUnits = State->getDWOUnits();
    return unit_iterator_range(DWOUnits.begin(), DWOUnits.end());
  }

  /// Get the number of compile units in this context.
  unsigned getNumCompileUnits() {
    return State->getNormalUnits().getNumInfoUnits();
  }

  /// Get the number of type units in this context.
  unsigned getNumTypeUnits() {
    return State->getNormalUnits().getNumTypesUnits();
  }

  /// Get the number of compile units in the DWO context.
  unsigned getNumDWOCompileUnits() {
    return State->getDWOUnits().getNumInfoUnits();
  }

  /// Get the number of type units in the DWO context.
  unsigned getNumDWOTypeUnits() {
    return State->getDWOUnits().getNumTypesUnits();
  }

  /// Get the unit at the specified index.
  DWARFUnit *getUnitAtIndex(unsigned index) {
    return State->getNormalUnits()[index].get();
  }

  /// Get the unit at the specified index for the DWO units.
  DWARFUnit *getDWOUnitAtIndex(unsigned index) {
    return State->getDWOUnits()[index].get();
  }

  DWARFCompileUnit *getDWOCompileUnitForHash(uint64_t Hash);
  DWARFTypeUnit *getTypeUnitForHash(uint16_t Version, uint64_t Hash, bool IsDWO);

  /// Return the compile unit that includes an offset (relative to .debug_info).
  DWARFCompileUnit *getCompileUnitForOffset(uint64_t Offset);

  /// Get a DIE given an exact offset.
  DWARFDie getDIEForOffset(uint64_t Offset);

  unsigned getMaxVersion() {
    // Ensure info units have been parsed to discover MaxVersion
    info_section_units();
    return MaxVersion;
  }

  unsigned getMaxDWOVersion() {
    // Ensure DWO info units have been parsed to discover MaxVersion
    dwo_info_section_units();
    return MaxVersion;
  }

  void setMaxVersionIfGreater(unsigned Version) {
    if (Version > MaxVersion)
      MaxVersion = Version;
  }

  const DWARFUnitIndex &getCUIndex();
  DWARFGdbIndex &getGdbIndex();
  const DWARFUnitIndex &getTUIndex();

  /// Get a pointer to the parsed DebugAbbrev object.
  const DWARFDebugAbbrev *getDebugAbbrev();

  /// Get a pointer to the parsed DebugLoc object.
  const DWARFDebugLoc *getDebugLoc();

  /// Get a pointer to the parsed dwo abbreviations object.
  const DWARFDebugAbbrev *getDebugAbbrevDWO();

  /// Get a pointer to the parsed DebugAranges object.
  const DWARFDebugAranges *getDebugAranges();

  /// Get a pointer to the parsed frame information object.
  Expected<const DWARFDebugFrame *> getDebugFrame();

  /// Get a pointer to the parsed eh frame information object.
  Expected<const DWARFDebugFrame *> getEHFrame();

  /// Get a pointer to the parsed DebugMacinfo information object.
  const DWARFDebugMacro *getDebugMacinfo();

  /// Get a pointer to the parsed DebugMacinfoDWO information object.
  const DWARFDebugMacro *getDebugMacinfoDWO();

  /// Get a pointer to the parsed DebugMacro information object.
  const DWARFDebugMacro *getDebugMacro();

  /// Get a pointer to the parsed DebugMacroDWO information object.
  const DWARFDebugMacro *getDebugMacroDWO();

  /// Get a reference to the parsed accelerator table object.
  const DWARFDebugNames &getDebugNames();

  /// Get a reference to the parsed accelerator table object.
  const AppleAcceleratorTable &getAppleNames();

  /// Get a reference to the parsed accelerator table object.
  const AppleAcceleratorTable &getAppleTypes();

  /// Get a reference to the parsed accelerator table object.
  const AppleAcceleratorTable &getAppleNamespaces();

  /// Get a reference to the parsed accelerator table object.
  const AppleAcceleratorTable &getAppleObjC();

  /// Get a pointer to a parsed line table corresponding to a compile unit.
  /// Report any parsing issues as warnings on stderr.
  const DWARFDebugLine::LineTable *getLineTableForUnit(DWARFUnit *U);

  /// Get a pointer to a parsed line table corresponding to a compile unit.
  /// Report any recoverable parsing problems using the handler.
  Expected<const DWARFDebugLine::LineTable *>
  getLineTableForUnit(DWARFUnit *U,
                      function_ref<void(Error)> RecoverableErrorHandler);

  // Clear the line table object corresponding to a compile unit for memory
  // management purpose. When it's referred to again, it'll be re-populated.
  void clearLineTableForUnit(DWARFUnit *U);

  DataExtractor getStringExtractor() const {
    return DataExtractor(DObj->getStrSection(), false, 0);
  }
  DataExtractor getStringDWOExtractor() const {
    return DataExtractor(DObj->getStrDWOSection(), false, 0);
  }
  DataExtractor getLineStringExtractor() const {
    return DataExtractor(DObj->getLineStrSection(), false, 0);
  }

  /// Wraps the returned DIEs for a given address.
  struct DIEsForAddress {
    DWARFCompileUnit *CompileUnit = nullptr;
    DWARFDie FunctionDIE;
    DWARFDie BlockDIE;
    explicit operator bool() const { return CompileUnit != nullptr; }
  };

  /// Get the compilation unit, the function DIE and lexical block DIE for the
  /// given address where applicable.
  /// TODO: change input parameter from "uint64_t Address"
  ///       into "SectionedAddress Address"
  /// \param[in] CheckDWO If this is false then only search for address matches
  ///            in the current context's DIEs. If this is true, then each
  ///            DWARFUnit that has a DWO file will have the debug info in the
  ///            DWO file searched as well. This allows for lookups to succeed
  ///            by searching the split DWARF debug info when using the main
  ///            executable's debug info.
  DIEsForAddress getDIEsForAddress(uint64_t Address, bool CheckDWO = false);

  DILineInfo getLineInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  DILineInfo
  getLineInfoForDataAddress(object::SectionedAddress Address) override;
  DILineInfoTable getLineInfoForAddressRange(
      object::SectionedAddress Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  DIInliningInfo getInliningInfoForAddress(
      object::SectionedAddress Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  std::vector<DILocal>
  getLocalsForAddress(object::SectionedAddress Address) override;

  bool isLittleEndian() const { return DObj->isLittleEndian(); }
  static unsigned getMaxSupportedVersion() { return 5; }
  static bool isSupportedVersion(unsigned version) {
    return version >= 2 && version <= getMaxSupportedVersion();
  }

  static SmallVector<uint8_t, 3> getSupportedAddressSizes() {
    return {2, 4, 8};
  }
  static bool isAddressSizeSupported(unsigned AddressSize) {
    return llvm::is_contained(getSupportedAddressSizes(), AddressSize);
  }
  template <typename... Ts>
  static Error checkAddressSizeSupported(unsigned AddressSize,
                                         std::error_code EC, char const *Fmt,
                                         const Ts &...Vals) {
    if (isAddressSizeSupported(AddressSize))
      return Error::success();
    std::string Buffer;
    raw_string_ostream Stream(Buffer);
    Stream << format(Fmt, Vals...)
           << " has unsupported address size: " << AddressSize
           << " (supported are ";
    ListSeparator LS;
    for (unsigned Size : DWARFContext::getSupportedAddressSizes())
      Stream << LS << Size;
    Stream << ')';
    return make_error<StringError>(Buffer, EC);
  }

  std::shared_ptr<DWARFContext> getDWOContext(StringRef AbsolutePath);

  function_ref<void(Error)> getRecoverableErrorHandler() {
    return RecoverableErrorHandler;
  }

  function_ref<void(Error)> getWarningHandler() { return WarningHandler; }

  enum class ProcessDebugRelocations { Process, Ignore };

  static std::unique_ptr<DWARFContext>
  create(const object::ObjectFile &Obj,
         ProcessDebugRelocations RelocAction = ProcessDebugRelocations::Process,
         const LoadedObjectInfo *L = nullptr, std::string DWPName = "",
         std::function<void(Error)> RecoverableErrorHandler =
             WithColor::defaultErrorHandler,
         std::function<void(Error)> WarningHandler =
             WithColor::defaultWarningHandler,
         bool ThreadSafe = false);

  static std::unique_ptr<DWARFContext>
  create(const StringMap<std::unique_ptr<MemoryBuffer>> &Sections,
         uint8_t AddrSize, bool isLittleEndian = sys::IsLittleEndianHost,
         std::function<void(Error)> RecoverableErrorHandler =
             WithColor::defaultErrorHandler,
         std::function<void(Error)> WarningHandler =
             WithColor::defaultWarningHandler,
         bool ThreadSafe = false);

  /// Get address size from CUs.
  /// TODO: refactor compile_units() to make this const.
  uint8_t getCUAddrSize();

  Triple::ArchType getArch() const {
    return getDWARFObj().getFile()->getArch();
  }

  /// Return the compile unit which contains instruction with provided
  /// address.
  /// TODO: change input parameter from "uint64_t Address"
  ///       into "SectionedAddress Address"
  DWARFCompileUnit *getCompileUnitForCodeAddress(uint64_t Address);

  /// Return the compile unit which contains data with the provided address.
  /// Note: This is more expensive than `getCompileUnitForAddress`, as if
  /// `Address` isn't found in the CU ranges (which is cheap), then it falls
  /// back to an expensive O(n) walk of all CU's looking for data that spans the
  /// address.
  /// TODO: change input parameter from "uint64_t Address" into
  ///       "SectionedAddress Address"
  DWARFCompileUnit *getCompileUnitForDataAddress(uint64_t Address);

  /// Returns whether CU/TU should be populated manually. TU Index populated
  /// manually only for DWARF5.
  bool getParseCUTUIndexManually() const { return ParseCUTUIndexManually; }

  /// Sets whether CU/TU should be populated manually. TU Index populated
  /// manually only for DWARF5.
  void setParseCUTUIndexManually(bool PCUTU) { ParseCUTUIndexManually = PCUTU; }

private:
  void addLocalsForDie(DWARFCompileUnit *CU, DWARFDie Subprogram, DWARFDie Die,
                       std::vector<DILocal> &Result);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H
