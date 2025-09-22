//===- DWARFLinkerTypeUnit.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFLinkerTypeUnit.h"
#include "DIEGenerator.h"
#include "DWARFEmitterImpl.h"
#include "llvm/Support/LEB128.h"

using namespace llvm;
using namespace dwarf_linker;
using namespace dwarf_linker::parallel;

TypeUnit::TypeUnit(LinkingGlobalData &GlobalData, unsigned ID,
                   std::optional<uint16_t> Language, dwarf::FormParams Format,
                   endianness Endianess)
    : DwarfUnit(GlobalData, ID, ""), Language(Language),
      AcceleratorRecords(&GlobalData.getAllocator()) {

  UnitName = "__artificial_type_unit";

  setOutputFormat(Format, Endianess);

  // Create line table prologue.
  LineTable.Prologue.FormParams = getFormParams();
  LineTable.Prologue.MinInstLength = 1;
  LineTable.Prologue.MaxOpsPerInst = 1;
  LineTable.Prologue.DefaultIsStmt = 1;
  LineTable.Prologue.LineBase = -5;
  LineTable.Prologue.LineRange = 14;
  LineTable.Prologue.OpcodeBase = 13;
  LineTable.Prologue.StandardOpcodeLengths = {0, 1, 1, 1, 1, 0,
                                              0, 0, 1, 0, 0, 1};

  getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);
}

void TypeUnit::createDIETree(BumpPtrAllocator &Allocator) {
  prepareDataForTreeCreation();

  // TaskGroup is created here as internal code has calls to
  // PerThreadBumpPtrAllocator which should be called from the task group task.
  llvm::parallel::TaskGroup TG;
  TG.spawn([&]() {
    SectionDescriptor &DebugInfoSection =
        getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);
    SectionDescriptor &DebugLineSection =
        getOrCreateSectionDescriptor(DebugSectionKind::DebugLine);

    DIEGenerator DIETreeGenerator(Allocator, *this);
    OffsetsPtrVector PatchesOffsets;

    // Create a Die for artificial compilation unit for types.
    DIE *UnitDIE = DIETreeGenerator.createDIE(dwarf::DW_TAG_compile_unit, 0);
    uint64_t OutOffset = getDebugInfoHeaderSize();
    UnitDIE->setOffset(OutOffset);

    SmallString<200> ProducerString;
    ProducerString += "llvm DWARFLinkerParallel library version ";
    DebugInfoSection.notePatchWithOffsetUpdate(
        DebugStrPatch{
            {OutOffset},
            GlobalData.getStringPool().insert(ProducerString.str()).first},
        PatchesOffsets);
    OutOffset += DIETreeGenerator
                     .addStringPlaceholderAttribute(dwarf::DW_AT_producer,
                                                    dwarf::DW_FORM_strp)
                     .second;

    if (Language) {
      OutOffset += DIETreeGenerator
                       .addScalarAttribute(dwarf::DW_AT_language,
                                           dwarf::DW_FORM_data2, *Language)
                       .second;
    }

    DebugInfoSection.notePatchWithOffsetUpdate(
        DebugStrPatch{{OutOffset},
                      GlobalData.getStringPool().insert(getUnitName()).first},
        PatchesOffsets);
    OutOffset += DIETreeGenerator
                     .addStringPlaceholderAttribute(dwarf::DW_AT_name,
                                                    dwarf::DW_FORM_strp)
                     .second;

    if (!LineTable.Prologue.FileNames.empty()) {
      DebugInfoSection.notePatchWithOffsetUpdate(
          DebugOffsetPatch{OutOffset, &DebugLineSection}, PatchesOffsets);

      OutOffset += DIETreeGenerator
                       .addScalarAttribute(dwarf::DW_AT_stmt_list,
                                           dwarf::DW_FORM_sec_offset, 0xbaddef)
                       .second;
    }

    DebugInfoSection.notePatchWithOffsetUpdate(
        DebugStrPatch{{OutOffset}, GlobalData.getStringPool().insert("").first},
        PatchesOffsets);
    OutOffset += DIETreeGenerator
                     .addStringPlaceholderAttribute(dwarf::DW_AT_comp_dir,
                                                    dwarf::DW_FORM_strp)
                     .second;

    if (!DebugStringIndexMap.empty()) {
      // Type unit is assumed to be emitted first. Thus we can use direct value
      // for DW_AT_str_offsets_base attribute(No need to fix it up with unit
      // offset value).
      OutOffset += DIETreeGenerator
                       .addScalarAttribute(dwarf::DW_AT_str_offsets_base,
                                           dwarf::DW_FORM_sec_offset,
                                           getDebugStrOffsetsHeaderSize())
                       .second;
    }

    UnitDIE->setSize(OutOffset - UnitDIE->getOffset() + 1);
    OutOffset =
        finalizeTypeEntryRec(UnitDIE->getOffset(), UnitDIE, Types.getRoot());

    // Update patch offsets.
    for (uint64_t *OffsetPtr : PatchesOffsets)
      *OffsetPtr += getULEB128Size(UnitDIE->getAbbrevNumber());

    setOutUnitDIE(UnitDIE);
  });
}

void TypeUnit::prepareDataForTreeCreation() {
  SectionDescriptor &DebugInfoSection =
      getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);

  // Type unit data created parallelly. So the order of data is not
  // deterministic. Order data here if we need deterministic output.

  llvm::parallel::TaskGroup TG;

  if (!GlobalData.getOptions().AllowNonDeterministicOutput) {
    TG.spawn([&]() {
      // Sort types to have a deterministic output.
      Types.sortTypes();
    });
  }

  TG.spawn([&]() {
    if (!GlobalData.getOptions().AllowNonDeterministicOutput) {
      // Sort decl type patches to have a deterministic output.
      std::function<bool(const DebugTypeDeclFilePatch &LHS,
                         const DebugTypeDeclFilePatch &RHS)>
          PatchesComparator = [&](const DebugTypeDeclFilePatch &LHS,
                                  const DebugTypeDeclFilePatch &RHS) {
            return LHS.Directory->first() < RHS.Directory->first() ||
                   (!(RHS.Directory->first() < LHS.Directory->first()) &&
                    LHS.FilePath->first() < RHS.FilePath->first());
          };
      // Sort patches to have a deterministic output.
      DebugInfoSection.ListDebugTypeDeclFilePatch.sort(PatchesComparator);
    }

    // Update DW_AT_decl_file attribute
    dwarf::Form DeclFileForm =
        getScalarFormForValue(
            DebugInfoSection.ListDebugTypeDeclFilePatch.size())
            .first;

    DebugInfoSection.ListDebugTypeDeclFilePatch.forEach(
        [&](DebugTypeDeclFilePatch &Patch) {
          TypeEntryBody *TypeEntry = Patch.TypeName->getValue().load();
          assert(TypeEntry &&
                 formatv("No data for type {0}", Patch.TypeName->getKey())
                     .str()
                     .c_str());
          if (&TypeEntry->getFinalDie() != Patch.Die)
            return;

          uint32_t FileIdx =
              addFileNameIntoLinetable(Patch.Directory, Patch.FilePath);

          unsigned DIESize = Patch.Die->getSize();
          DIEGenerator DIEGen(Patch.Die, Types.getThreadLocalAllocator(),
                              *this);

          DIESize += DIEGen
                         .addScalarAttribute(dwarf::DW_AT_decl_file,
                                             DeclFileForm, FileIdx)
                         .second;
          Patch.Die->setSize(DIESize);
        });
  });

  if (!GlobalData.getOptions().AllowNonDeterministicOutput) {
    // Sort patches to have a deterministic output.
    TG.spawn([&]() {
      forEach([&](SectionDescriptor &OutSection) {
        std::function<bool(const DebugStrPatch &LHS, const DebugStrPatch &RHS)>
            StrPatchesComparator =
                [&](const DebugStrPatch &LHS, const DebugStrPatch &RHS) {
                  return LHS.String->getKey() < RHS.String->getKey();
                };
        OutSection.ListDebugStrPatch.sort(StrPatchesComparator);

        std::function<bool(const DebugTypeStrPatch &LHS,
                           const DebugTypeStrPatch &RHS)>
            TypeStrPatchesComparator = [&](const DebugTypeStrPatch &LHS,
                                           const DebugTypeStrPatch &RHS) {
              return LHS.String->getKey() < RHS.String->getKey();
            };
        OutSection.ListDebugTypeStrPatch.sort(TypeStrPatchesComparator);
      });
    });
  }

  if (!GlobalData.getOptions().AllowNonDeterministicOutput) {
    // Sort patches to have a deterministic output.
    TG.spawn([&]() {
      forEach([&](SectionDescriptor &OutSection) {
        std::function<bool(const DebugLineStrPatch &LHS,
                           const DebugLineStrPatch &RHS)>
            LineStrPatchesComparator = [&](const DebugLineStrPatch &LHS,
                                           const DebugLineStrPatch &RHS) {
              return LHS.String->getKey() < RHS.String->getKey();
            };
        OutSection.ListDebugLineStrPatch.sort(LineStrPatchesComparator);

        std::function<bool(const DebugTypeLineStrPatch &LHS,
                           const DebugTypeLineStrPatch &RHS)>
            TypeLineStrPatchesComparator =
                [&](const DebugTypeLineStrPatch &LHS,
                    const DebugTypeLineStrPatch &RHS) {
                  return LHS.String->getKey() < RHS.String->getKey();
                };
        OutSection.ListDebugTypeLineStrPatch.sort(TypeLineStrPatchesComparator);
      });
    });
  }
}

uint64_t TypeUnit::finalizeTypeEntryRec(uint64_t OutOffset, DIE *OutDIE,
                                        TypeEntry *Entry) {
  bool HasChildren = !Entry->getValue().load()->Children.empty();
  DIEGenerator DIEGen(OutDIE, Types.getThreadLocalAllocator(), *this);
  OutOffset += DIEGen.finalizeAbbreviations(HasChildren, nullptr);
  OutOffset += OutDIE->getSize() - 1;

  if (HasChildren) {
    Entry->getValue().load()->Children.forEach([&](TypeEntry *ChildEntry) {
      DIE *ChildDIE = &ChildEntry->getValue().load()->getFinalDie();
      DIEGen.addChild(ChildDIE);

      ChildDIE->setOffset(OutOffset);

      OutOffset = finalizeTypeEntryRec(OutOffset, ChildDIE, ChildEntry);
    });

    // End of children marker.
    OutOffset += sizeof(int8_t);
  }

  OutDIE->setSize(OutOffset - OutDIE->getOffset());
  return OutOffset;
}

uint32_t TypeUnit::addFileNameIntoLinetable(StringEntry *Dir,
                                            StringEntry *FileName) {
  uint32_t DirIdx = 0;

  if (Dir->first() == "") {
    DirIdx = 0;
  } else {
    DirectoriesMapTy::iterator DirEntry = DirectoriesMap.find(Dir);
    if (DirEntry == DirectoriesMap.end()) {
      // We currently do not support more than UINT32_MAX directories.
      assert(LineTable.Prologue.IncludeDirectories.size() < UINT32_MAX);
      DirIdx = LineTable.Prologue.IncludeDirectories.size();
      DirectoriesMap.insert({Dir, DirIdx});
      LineTable.Prologue.IncludeDirectories.push_back(
          DWARFFormValue::createFromPValue(dwarf::DW_FORM_string,
                                           Dir->getKeyData()));
    } else {
      DirIdx = DirEntry->second;
    }

    if (getVersion() < 5)
      DirIdx++;
  }

  uint32_t FileIdx = 0;
  FilenamesMapTy::iterator FileEntry = FileNamesMap.find({FileName, DirIdx});
  if (FileEntry == FileNamesMap.end()) {
    // We currently do not support more than UINT32_MAX files.
    assert(LineTable.Prologue.FileNames.size() < UINT32_MAX);
    FileIdx = LineTable.Prologue.FileNames.size();
    FileNamesMap.insert({{FileName, DirIdx}, FileIdx});
    LineTable.Prologue.FileNames.push_back(DWARFDebugLine::FileNameEntry());
    LineTable.Prologue.FileNames.back().Name = DWARFFormValue::createFromPValue(
        dwarf::DW_FORM_string, FileName->getKeyData());
    LineTable.Prologue.FileNames.back().DirIdx = DirIdx;
  } else {
    FileIdx = FileEntry->second;
  }

  return getVersion() < 5 ? FileIdx + 1 : FileIdx;
}

std::pair<dwarf::Form, uint8_t>
TypeUnit::getScalarFormForValue(uint64_t Value) const {
  if (Value > 0xFFFFFFFF)
    return std::make_pair(dwarf::DW_FORM_data8, 8);

  if (Value > 0xFFFF)
    return std::make_pair(dwarf::DW_FORM_data4, 4);

  if (Value > 0xFF)
    return std::make_pair(dwarf::DW_FORM_data2, 2);

  return std::make_pair(dwarf::DW_FORM_data1, 1);
}

uint8_t TypeUnit::getSizeByAttrForm(dwarf::Form Form) const {
  if (Form == dwarf::DW_FORM_data1)
    return 1;

  if (Form == dwarf::DW_FORM_data2)
    return 2;

  if (Form == dwarf::DW_FORM_data4)
    return 4;

  if (Form == dwarf::DW_FORM_data8)
    return 8;

  if (Form == dwarf::DW_FORM_data16)
    return 16;

  llvm_unreachable("Unsupported Attr Form");
}

Error TypeUnit::finishCloningAndEmit(const Triple &TargetTriple) {
  BumpPtrAllocator Allocator;
  createDIETree(Allocator);

  if (getOutUnitDIE() == nullptr)
    return Error::success();

  // Create sections ahead so that they should not be created asynchronously
  // later.
  getOrCreateSectionDescriptor(DebugSectionKind::DebugInfo);
  getOrCreateSectionDescriptor(DebugSectionKind::DebugLine);
  getOrCreateSectionDescriptor(DebugSectionKind::DebugStrOffsets);
  getOrCreateSectionDescriptor(DebugSectionKind::DebugAbbrev);
  if (llvm::is_contained(GlobalData.getOptions().AccelTables,
                         DWARFLinker::AccelTableKind::Pub)) {
    getOrCreateSectionDescriptor(DebugSectionKind::DebugPubNames);
    getOrCreateSectionDescriptor(DebugSectionKind::DebugPubTypes);
  }

  SmallVector<std::function<Error(void)>> Tasks;

  // Add task for emitting .debug_line section.
  if (!LineTable.Prologue.FileNames.empty()) {
    Tasks.push_back(
        [&]() -> Error { return emitDebugLine(TargetTriple, LineTable); });
  }

  // Add task for emitting .debug_info section.
  Tasks.push_back([&]() -> Error { return emitDebugInfo(TargetTriple); });

  // Add task for emitting Pub accelerator sections.
  if (llvm::is_contained(GlobalData.getOptions().AccelTables,
                         DWARFLinker::AccelTableKind::Pub)) {
    Tasks.push_back([&]() -> Error {
      emitPubAccelerators();
      return Error::success();
    });
  }

  // Add task for emitting .debug_str_offsets section.
  Tasks.push_back([&]() -> Error { return emitDebugStringOffsetSection(); });

  // Add task for emitting .debug_abbr section.
  Tasks.push_back([&]() -> Error { return emitAbbreviations(); });

  if (auto Err = parallelForEachError(
          Tasks, [&](std::function<Error(void)> F) { return F(); }))
    return Err;

  return Error::success();
}
