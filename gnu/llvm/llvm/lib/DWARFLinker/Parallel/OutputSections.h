//===- OutputSections.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_OUTPUTSECTIONS_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_OUTPUTSECTIONS_H

#include "ArrayList.h"
#include "StringEntryToDwarfStringPoolEntryMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/DWARFLinker/StringPool.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cstdint>

namespace llvm {
namespace dwarf_linker {
namespace parallel {

class TypeUnit;

/// There are fields(sizes, offsets) which should be updated after
/// sections are generated. To remember offsets and related data
/// the descendants of SectionPatch structure should be used.

struct SectionPatch {
  uint64_t PatchOffset = 0;
};

/// This structure is used to update strings offsets into .debug_str.
struct DebugStrPatch : SectionPatch {
  const StringEntry *String = nullptr;
};

/// This structure is used to update strings offsets into .debug_line_str.
struct DebugLineStrPatch : SectionPatch {
  const StringEntry *String = nullptr;
};

/// This structure is used to update range list offset into
/// .debug_ranges/.debug_rnglists.
struct DebugRangePatch : SectionPatch {
  /// Indicates patch which points to immediate compile unit's attribute.
  bool IsCompileUnitRanges = false;
};

/// This structure is used to update location list offset into
/// .debug_loc/.debug_loclists.
struct DebugLocPatch : SectionPatch {
  int64_t AddrAdjustmentValue = 0;
};

/// This structure is used to update offset with start of another section.
struct SectionDescriptor;
struct DebugOffsetPatch : SectionPatch {
  DebugOffsetPatch(uint64_t PatchOffset, SectionDescriptor *SectionPtr,
                   bool AddLocalValue = false)
      : SectionPatch({PatchOffset}), SectionPtr(SectionPtr, AddLocalValue) {}

  PointerIntPair<SectionDescriptor *, 1> SectionPtr;
};

/// This structure is used to update reference to the DIE.
struct DebugDieRefPatch : SectionPatch {
  DebugDieRefPatch(uint64_t PatchOffset, CompileUnit *SrcCU, CompileUnit *RefCU,
                   uint32_t RefIdx);

  PointerIntPair<CompileUnit *, 1> RefCU;
  uint64_t RefDieIdxOrClonedOffset = 0;
};

/// This structure is used to update reference to the DIE of ULEB128 form.
struct DebugULEB128DieRefPatch : SectionPatch {
  DebugULEB128DieRefPatch(uint64_t PatchOffset, CompileUnit *SrcCU,
                          CompileUnit *RefCU, uint32_t RefIdx);

  PointerIntPair<CompileUnit *, 1> RefCU;
  uint64_t RefDieIdxOrClonedOffset = 0;
};

/// This structure is used to update reference to the type DIE.
struct DebugDieTypeRefPatch : SectionPatch {
  DebugDieTypeRefPatch(uint64_t PatchOffset, TypeEntry *RefTypeName);

  TypeEntry *RefTypeName = nullptr;
};

/// This structure is used to update reference to the type DIE.
struct DebugType2TypeDieRefPatch : SectionPatch {
  DebugType2TypeDieRefPatch(uint64_t PatchOffset, DIE *Die, TypeEntry *TypeName,
                            TypeEntry *RefTypeName);

  DIE *Die = nullptr;
  TypeEntry *TypeName = nullptr;
  TypeEntry *RefTypeName = nullptr;
};

struct DebugTypeStrPatch : SectionPatch {
  DebugTypeStrPatch(uint64_t PatchOffset, DIE *Die, TypeEntry *TypeName,
                    StringEntry *String);

  DIE *Die = nullptr;
  TypeEntry *TypeName = nullptr;
  StringEntry *String = nullptr;
};

struct DebugTypeLineStrPatch : SectionPatch {
  DebugTypeLineStrPatch(uint64_t PatchOffset, DIE *Die, TypeEntry *TypeName,
                        StringEntry *String);

  DIE *Die = nullptr;
  TypeEntry *TypeName = nullptr;
  StringEntry *String = nullptr;
};

struct DebugTypeDeclFilePatch {
  DebugTypeDeclFilePatch(DIE *Die, TypeEntry *TypeName, StringEntry *Directory,
                         StringEntry *FilePath);

  DIE *Die = nullptr;
  TypeEntry *TypeName = nullptr;
  StringEntry *Directory = nullptr;
  StringEntry *FilePath = nullptr;
  uint32_t FileID = 0;
};

/// Type for section data.
using OutSectionDataTy = SmallString<0>;

/// Type for list of pointers to patches offsets.
using OffsetsPtrVector = SmallVector<uint64_t *>;

class OutputSections;

/// This structure is used to keep data of the concrete section.
/// Like data bits, list of patches, format.
struct SectionDescriptor : SectionDescriptorBase {
  friend OutputSections;

  SectionDescriptor(DebugSectionKind SectionKind, LinkingGlobalData &GlobalData,
                    dwarf::FormParams Format, llvm::endianness Endianess)
      : SectionDescriptorBase(SectionKind, Format, Endianess), OS(Contents),
        ListDebugStrPatch(&GlobalData.getAllocator()),
        ListDebugLineStrPatch(&GlobalData.getAllocator()),
        ListDebugRangePatch(&GlobalData.getAllocator()),
        ListDebugLocPatch(&GlobalData.getAllocator()),
        ListDebugDieRefPatch(&GlobalData.getAllocator()),
        ListDebugULEB128DieRefPatch(&GlobalData.getAllocator()),
        ListDebugOffsetPatch(&GlobalData.getAllocator()),
        ListDebugDieTypeRefPatch(&GlobalData.getAllocator()),
        ListDebugType2TypeDieRefPatch(&GlobalData.getAllocator()),
        ListDebugTypeStrPatch(&GlobalData.getAllocator()),
        ListDebugTypeLineStrPatch(&GlobalData.getAllocator()),
        ListDebugTypeDeclFilePatch(&GlobalData.getAllocator()),
        GlobalData(GlobalData) {}

  /// Erase whole section content(data bits, list of patches).
  void clearAllSectionData();

  /// Erase only section output data bits.
  void clearSectionContent();

  /// When objects(f.e. compile units) are glued into the single file,
  /// the debug sections corresponding to the concrete object are assigned
  /// with offsets inside the whole file. This field keeps offset
  /// to the debug section, corresponding to this object.
  uint64_t StartOffset = 0;

  /// Stream which stores data to the Contents.
  raw_svector_ostream OS;

  /// Section patches.
#define ADD_PATCHES_LIST(T)                                                    \
  T &notePatch(const T &Patch) { return List##T.add(Patch); }                  \
  ArrayList<T> List##T;

  ADD_PATCHES_LIST(DebugStrPatch)
  ADD_PATCHES_LIST(DebugLineStrPatch)
  ADD_PATCHES_LIST(DebugRangePatch)
  ADD_PATCHES_LIST(DebugLocPatch)
  ADD_PATCHES_LIST(DebugDieRefPatch)
  ADD_PATCHES_LIST(DebugULEB128DieRefPatch)
  ADD_PATCHES_LIST(DebugOffsetPatch)
  ADD_PATCHES_LIST(DebugDieTypeRefPatch)
  ADD_PATCHES_LIST(DebugType2TypeDieRefPatch)
  ADD_PATCHES_LIST(DebugTypeStrPatch)
  ADD_PATCHES_LIST(DebugTypeLineStrPatch)
  ADD_PATCHES_LIST(DebugTypeDeclFilePatch)

  /// While creating patches, offsets to attributes may be partially
  /// unknown(because size of abbreviation number is unknown). In such case we
  /// remember patch itself and pointer to patch application offset to add size
  /// of abbreviation number later.
  template <typename T>
  void notePatchWithOffsetUpdate(const T &Patch,
                                 OffsetsPtrVector &PatchesOffsetsList) {
    PatchesOffsetsList.emplace_back(&notePatch(Patch).PatchOffset);
  }

  /// Some sections are emitted using AsmPrinter. In that case "Contents"
  /// member of SectionDescriptor contains elf file. This method searches
  /// for section data inside elf file and remember offset to it.
  void setSizesForSectionCreatedByAsmPrinter();

  /// Returns section content.
  StringRef getContents() override {
    if (SectionOffsetInsideAsmPrinterOutputStart == 0)
      return Contents;

    return Contents.slice(SectionOffsetInsideAsmPrinterOutputStart,
                          SectionOffsetInsideAsmPrinterOutputEnd);
  }

  /// Emit unit length into the current section contents.
  void emitUnitLength(uint64_t Length) {
    maybeEmitDwarf64Mark();
    emitIntVal(Length, getFormParams().getDwarfOffsetByteSize());
  }

  /// Emit DWARF64 mark into the current section contents.
  void maybeEmitDwarf64Mark() {
    if (getFormParams().Format != dwarf::DWARF64)
      return;
    emitIntVal(dwarf::DW_LENGTH_DWARF64, 4);
  }

  /// Emit specified offset value into the current section contents.
  void emitOffset(uint64_t Val) {
    emitIntVal(Val, getFormParams().getDwarfOffsetByteSize());
  }

  /// Emit specified integer value into the current section contents.
  void emitIntVal(uint64_t Val, unsigned Size);

  void emitString(dwarf::Form StringForm, const char *StringVal);

  void emitBinaryData(llvm::StringRef Data);

  /// Emit specified inplace string value into the current section contents.
  void emitInplaceString(StringRef String) {
    OS << String;
    emitIntVal(0, 1);
  }

  /// Emit string placeholder into the current section contents.
  void emitStringPlaceholder() {
    // emit bad offset which should be updated later.
    emitOffset(0xBADDEF);
  }

  /// Write specified \p Value of \p AttrForm to the \p PatchOffset.
  void apply(uint64_t PatchOffset, dwarf::Form AttrForm, uint64_t Val);

  /// Returns integer value of \p Size located by specified \p PatchOffset.
  uint64_t getIntVal(uint64_t PatchOffset, unsigned Size);

protected:
  /// Writes integer value \p Val of \p Size by specified \p PatchOffset.
  void applyIntVal(uint64_t PatchOffset, uint64_t Val, unsigned Size);

  /// Writes integer value \p Val of ULEB128 format by specified \p PatchOffset.
  void applyULEB128(uint64_t PatchOffset, uint64_t Val);

  /// Writes integer value \p Val of SLEB128 format by specified \p PatchOffset.
  void applySLEB128(uint64_t PatchOffset, uint64_t Val);

  /// Sets output format.
  void setOutputFormat(dwarf::FormParams Format, llvm::endianness Endianess) {
    this->Format = Format;
    this->Endianess = Endianess;
  }

  LinkingGlobalData &GlobalData;

  /// Section data bits.
  OutSectionDataTy Contents;

  /// Some sections are generated using AsmPrinter. The real section data
  /// located inside elf file in that case. Following fields points to the
  /// real section content inside elf file.
  size_t SectionOffsetInsideAsmPrinterOutputStart = 0;
  size_t SectionOffsetInsideAsmPrinterOutputEnd = 0;
};

/// This class keeps contents and offsets to the debug sections. Any objects
/// which is supposed to be emitted into the debug sections should use this
/// class to track debug sections offsets and keep sections data.
class OutputSections {
public:
  OutputSections(LinkingGlobalData &GlobalData) : GlobalData(GlobalData) {}

  /// Sets output format for all keeping sections.
  void setOutputFormat(dwarf::FormParams Format, llvm::endianness Endianness) {
    this->Format = Format;
    this->Endianness = Endianness;
  }

  /// Returns descriptor for the specified section of \p SectionKind.
  /// The descriptor should already be created. The llvm_unreachable
  /// would be raised if it is not.
  const SectionDescriptor &
  getSectionDescriptor(DebugSectionKind SectionKind) const {
    SectionsSetTy::const_iterator It = SectionDescriptors.find(SectionKind);

    if (It == SectionDescriptors.end())
      llvm_unreachable(
          formatv("Section {0} does not exist", getSectionName(SectionKind))
              .str()
              .c_str());

    return *It->second;
  }

  /// Returns descriptor for the specified section of \p SectionKind.
  /// The descriptor should already be created. The llvm_unreachable
  /// would be raised if it is not.
  SectionDescriptor &getSectionDescriptor(DebugSectionKind SectionKind) {
    SectionsSetTy::iterator It = SectionDescriptors.find(SectionKind);

    if (It == SectionDescriptors.end())
      llvm_unreachable(
          formatv("Section {0} does not exist", getSectionName(SectionKind))
              .str()
              .c_str());

    assert(It->second.get() != nullptr);

    return *It->second;
  }

  /// Returns descriptor for the specified section of \p SectionKind.
  /// Returns std::nullopt if section descriptor is not created yet.
  std::optional<const SectionDescriptor *>
  tryGetSectionDescriptor(DebugSectionKind SectionKind) const {
    SectionsSetTy::const_iterator It = SectionDescriptors.find(SectionKind);

    if (It == SectionDescriptors.end())
      return std::nullopt;

    return It->second.get();
  }

  /// Returns descriptor for the specified section of \p SectionKind.
  /// Returns std::nullopt if section descriptor is not created yet.
  std::optional<SectionDescriptor *>
  tryGetSectionDescriptor(DebugSectionKind SectionKind) {
    SectionsSetTy::iterator It = SectionDescriptors.find(SectionKind);

    if (It == SectionDescriptors.end())
      return std::nullopt;

    return It->second.get();
  }

  /// Returns descriptor for the specified section of \p SectionKind.
  /// If descriptor does not exist then creates it.
  SectionDescriptor &
  getOrCreateSectionDescriptor(DebugSectionKind SectionKind) {
    SectionsSetTy::iterator It = SectionDescriptors.find(SectionKind);

    if (It == SectionDescriptors.end()) {
      SectionDescriptor *Section =
          new SectionDescriptor(SectionKind, GlobalData, Format, Endianness);
      auto Result = SectionDescriptors.try_emplace(SectionKind, Section);
      assert(Result.second);

      It = Result.first;
    }

    return *It->second;
  }

  /// Erases data of all sections.
  void eraseSections() {
    for (auto &Section : SectionDescriptors)
      Section.second->clearAllSectionData();
  }

  /// Enumerate all sections and call \p Handler for each.
  void forEach(function_ref<void(SectionDescriptor &)> Handler) {
    for (auto &Section : SectionDescriptors) {
      assert(Section.second.get() != nullptr);
      Handler(*(Section.second));
    }
  }

  /// Enumerate all sections and call \p Handler for each.
  void forEach(
      function_ref<void(std::shared_ptr<SectionDescriptor> Section)> Handler) {
    for (auto &Section : SectionDescriptors)
      Handler(Section.second);
  }

  /// Enumerate all sections, for each section set current offset
  /// (kept by \p SectionSizesAccumulator), update current offset with section
  /// length.
  void assignSectionsOffsetAndAccumulateSize(
      std::array<uint64_t, SectionKindsNum> &SectionSizesAccumulator) {
    for (auto &Section : SectionDescriptors) {
      Section.second->StartOffset =
          SectionSizesAccumulator[static_cast<uint8_t>(
              Section.second->getKind())];
      SectionSizesAccumulator[static_cast<uint8_t>(
          Section.second->getKind())] += Section.second->getContents().size();
    }
  }

  /// Enumerate all sections, for each section apply all section patches.
  void applyPatches(SectionDescriptor &Section,
                    StringEntryToDwarfStringPoolEntryMap &DebugStrStrings,
                    StringEntryToDwarfStringPoolEntryMap &DebugLineStrStrings,
                    TypeUnit *TypeUnitPtr);

  /// Endiannes for the sections.
  llvm::endianness getEndianness() const { return Endianness; }

  /// Return DWARF version.
  uint16_t getVersion() const { return Format.Version; }

  /// Return size of header of debug_info table.
  uint16_t getDebugInfoHeaderSize() const {
    return Format.Version >= 5 ? 12 : 11;
  }

  /// Return size of header of debug_ table.
  uint16_t getDebugAddrHeaderSize() const {
    assert(Format.Version >= 5);
    return Format.Format == dwarf::DwarfFormat::DWARF32 ? 8 : 16;
  }

  /// Return size of header of debug_str_offsets table.
  uint16_t getDebugStrOffsetsHeaderSize() const {
    assert(Format.Version >= 5);
    return Format.Format == dwarf::DwarfFormat::DWARF32 ? 8 : 16;
  }

  /// Return size of address.
  const dwarf::FormParams &getFormParams() const { return Format; }

protected:
  LinkingGlobalData &GlobalData;

  /// Format for sections.
  dwarf::FormParams Format = {4, 4, dwarf::DWARF32};

  /// Endiannes for sections.
  llvm::endianness Endianness = llvm::endianness::native;

  /// All keeping sections.
  using SectionsSetTy =
      std::map<DebugSectionKind, std::shared_ptr<SectionDescriptor>>;
  SectionsSetTy SectionDescriptors;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_OUTPUTSECTIONS_H
