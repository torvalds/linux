//===- DebugLineSectionEmitter.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_DWARFLINKER_PARALLEL_DEBUGLINESECTIONEMITTER_H
#define LLVM_LIB_DWARFLINKER_PARALLEL_DEBUGLINESECTIONEMITTER_H

#include "DWARFEmitterImpl.h"
#include "llvm/DWARFLinker/AddressesMap.h"
#include "llvm/DWARFLinker/Parallel/DWARFLinker.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"

namespace llvm {
namespace dwarf_linker {
namespace parallel {

/// This class emits specified line table into the .debug_line section.
class DebugLineSectionEmitter {
public:
  DebugLineSectionEmitter(const Triple &TheTriple, DwarfUnit &U)
      : TheTriple(TheTriple), U(U) {}

  Error emit(const DWARFDebugLine::LineTable &LineTable) {
    // FIXME: remove dependence on MCDwarfLineAddr::encode.
    // As we reuse MCDwarfLineAddr::encode, we need to create/initialize
    // some MC* classes.
    if (Error Err = init(TheTriple))
      return Err;

    // Get descriptor for output .debug_line section.
    SectionDescriptor &OutSection =
        U.getOrCreateSectionDescriptor(DebugSectionKind::DebugLine);

    // unit_length.
    OutSection.emitUnitLength(0xBADDEF);
    uint64_t OffsetAfterUnitLength = OutSection.OS.tell();

    // Emit prologue.
    emitLineTablePrologue(LineTable.Prologue, OutSection);

    // Emit rows.
    emitLineTableRows(LineTable, OutSection);
    uint64_t OffsetAfterEnd = OutSection.OS.tell();

    // Update unit length field with actual length value.
    assert(OffsetAfterUnitLength -
               OutSection.getFormParams().getDwarfOffsetByteSize() <
           OffsetAfterUnitLength);
    OutSection.apply(OffsetAfterUnitLength -
                         OutSection.getFormParams().getDwarfOffsetByteSize(),
                     dwarf::DW_FORM_sec_offset,
                     OffsetAfterEnd - OffsetAfterUnitLength);

    return Error::success();
  }

private:
  Error init(Triple TheTriple) {
    std::string ErrorStr;
    std::string TripleName;

    // Get the target.
    const Target *TheTarget =
        TargetRegistry::lookupTarget(TripleName, TheTriple, ErrorStr);
    if (!TheTarget)
      return createStringError(std::errc::invalid_argument, ErrorStr.c_str());
    TripleName = TheTriple.getTriple();

    // Create all the MC Objects.
    MRI.reset(TheTarget->createMCRegInfo(TripleName));
    if (!MRI)
      return createStringError(std::errc::invalid_argument,
                               "no register info for target %s",
                               TripleName.c_str());

    MCTargetOptions MCOptions = mc::InitMCTargetOptionsFromFlags();
    MAI.reset(TheTarget->createMCAsmInfo(*MRI, TripleName, MCOptions));
    if (!MAI)
      return createStringError(std::errc::invalid_argument,
                               "no asm info for target %s", TripleName.c_str());

    MSTI.reset(TheTarget->createMCSubtargetInfo(TripleName, "", ""));
    if (!MSTI)
      return createStringError(std::errc::invalid_argument,
                               "no subtarget info for target %s",
                               TripleName.c_str());

    MC.reset(new MCContext(TheTriple, MAI.get(), MRI.get(), MSTI.get(), nullptr,
                           nullptr, true, "__DWARF"));

    return Error::success();
  }

  void emitLineTablePrologue(const DWARFDebugLine::Prologue &P,
                             SectionDescriptor &Section) {
    // version (uhalf).
    Section.emitIntVal(P.getVersion(), 2);
    if (P.getVersion() == 5) {
      // address_size (ubyte).
      Section.emitIntVal(P.getAddressSize(), 1);

      // segment_selector_size (ubyte).
      Section.emitIntVal(P.SegSelectorSize, 1);
    }

    // header_length.
    Section.emitOffset(0xBADDEF);

    uint64_t OffsetAfterPrologueLength = Section.OS.tell();
    emitLineTableProloguePayload(P, Section);
    uint64_t OffsetAfterPrologueEnd = Section.OS.tell();

    // Update prologue length field with actual length value.
    Section.apply(OffsetAfterPrologueLength -
                      Section.getFormParams().getDwarfOffsetByteSize(),
                  dwarf::DW_FORM_sec_offset,
                  OffsetAfterPrologueEnd - OffsetAfterPrologueLength);
  }

  void
  emitLineTablePrologueV2IncludeAndFileTable(const DWARFDebugLine::Prologue &P,
                                             SectionDescriptor &Section) {
    // include_directories (sequence of path names).
    for (const DWARFFormValue &Include : P.IncludeDirectories) {
      std::optional<const char *> IncludeStr = dwarf::toString(Include);
      if (!IncludeStr) {
        U.warn("cann't read string from line table.");
        return;
      }

      Section.emitString(Include.getForm(), *IncludeStr);
    }
    // The last entry is followed by a single null byte.
    Section.emitIntVal(0, 1);

    // file_names (sequence of file entries).
    for (const DWARFDebugLine::FileNameEntry &File : P.FileNames) {
      std::optional<const char *> FileNameStr = dwarf::toString(File.Name);
      if (!FileNameStr) {
        U.warn("cann't read string from line table.");
        return;
      }

      // A null-terminated string containing the full or relative path name of a
      // source file.
      Section.emitString(File.Name.getForm(), *FileNameStr);

      // An unsigned LEB128 number representing the directory index of a
      // directory in the include_directories section.
      encodeULEB128(File.DirIdx, Section.OS);
      // An unsigned LEB128 number representing the (implementation-defined)
      // time of last modification for the file, or 0 if not available.
      encodeULEB128(File.ModTime, Section.OS);
      // An unsigned LEB128 number representing the length in bytes of the file,
      // or 0 if not available.
      encodeULEB128(File.Length, Section.OS);
    }
    // The last entry is followed by a single null byte.
    Section.emitIntVal(0, 1);
  }

  void
  emitLineTablePrologueV5IncludeAndFileTable(const DWARFDebugLine::Prologue &P,
                                             SectionDescriptor &Section) {
    if (P.IncludeDirectories.empty()) {
      // directory_entry_format_count(ubyte).
      Section.emitIntVal(0, 1);
    } else {
      // directory_entry_format_count(ubyte).
      Section.emitIntVal(1, 1);

      // directory_entry_format (sequence of ULEB128 pairs).
      encodeULEB128(dwarf::DW_LNCT_path, Section.OS);
      encodeULEB128(P.IncludeDirectories[0].getForm(), Section.OS);
    }

    // directories_count (ULEB128).
    encodeULEB128(P.IncludeDirectories.size(), Section.OS);
    // directories (sequence of directory names).
    for (auto Include : P.IncludeDirectories) {
      std::optional<const char *> IncludeStr = dwarf::toString(Include);
      if (!IncludeStr) {
        U.warn("cann't read string from line table.");
        return;
      }

      Section.emitString(Include.getForm(), *IncludeStr);
    }

    bool HasChecksums = P.ContentTypes.HasMD5;
    bool HasInlineSources = P.ContentTypes.HasSource;

    dwarf::Form FileNameForm = dwarf::DW_FORM_string;
    dwarf::Form LLVMSourceForm = dwarf::DW_FORM_string;

    if (P.FileNames.empty()) {
      // file_name_entry_format_count (ubyte).
      Section.emitIntVal(0, 1);
    } else {
      FileNameForm = P.FileNames[0].Name.getForm();
      LLVMSourceForm = P.FileNames[0].Source.getForm();

      // file_name_entry_format_count (ubyte).
      Section.emitIntVal(
          2 + (HasChecksums ? 1 : 0) + (HasInlineSources ? 1 : 0), 1);

      // file_name_entry_format (sequence of ULEB128 pairs).
      encodeULEB128(dwarf::DW_LNCT_path, Section.OS);
      encodeULEB128(FileNameForm, Section.OS);

      encodeULEB128(dwarf::DW_LNCT_directory_index, Section.OS);
      encodeULEB128(dwarf::DW_FORM_data1, Section.OS);

      if (HasChecksums) {
        encodeULEB128(dwarf::DW_LNCT_MD5, Section.OS);
        encodeULEB128(dwarf::DW_FORM_data16, Section.OS);
      }

      if (HasInlineSources) {
        encodeULEB128(dwarf::DW_LNCT_LLVM_source, Section.OS);
        encodeULEB128(LLVMSourceForm, Section.OS);
      }
    }

    // file_names_count (ULEB128).
    encodeULEB128(P.FileNames.size(), Section.OS);

    // file_names (sequence of file name entries).
    for (auto File : P.FileNames) {
      std::optional<const char *> FileNameStr = dwarf::toString(File.Name);
      if (!FileNameStr) {
        U.warn("cann't read string from line table.");
        return;
      }

      // A null-terminated string containing the full or relative path name of a
      // source file.
      Section.emitString(FileNameForm, *FileNameStr);
      Section.emitIntVal(File.DirIdx, 1);

      if (HasChecksums) {
        assert((File.Checksum.size() == 16) &&
               "checksum size is not equal to 16 bytes.");
        Section.emitBinaryData(
            StringRef(reinterpret_cast<const char *>(File.Checksum.data()),
                      File.Checksum.size()));
      }

      if (HasInlineSources) {
        std::optional<const char *> FileSourceStr =
            dwarf::toString(File.Source);
        if (!FileSourceStr) {
          U.warn("cann't read string from line table.");
          return;
        }

        Section.emitString(LLVMSourceForm, *FileSourceStr);
      }
    }
  }

  void emitLineTableProloguePayload(const DWARFDebugLine::Prologue &P,
                                    SectionDescriptor &Section) {
    // minimum_instruction_length (ubyte).
    Section.emitIntVal(P.MinInstLength, 1);
    if (P.FormParams.Version >= 4) {
      // maximum_operations_per_instruction (ubyte).
      Section.emitIntVal(P.MaxOpsPerInst, 1);
    }
    // default_is_stmt (ubyte).
    Section.emitIntVal(P.DefaultIsStmt, 1);
    // line_base (sbyte).
    Section.emitIntVal(P.LineBase, 1);
    // line_range (ubyte).
    Section.emitIntVal(P.LineRange, 1);
    // opcode_base (ubyte).
    Section.emitIntVal(P.OpcodeBase, 1);

    // standard_opcode_lengths (array of ubyte).
    for (auto Length : P.StandardOpcodeLengths)
      Section.emitIntVal(Length, 1);

    if (P.FormParams.Version < 5)
      emitLineTablePrologueV2IncludeAndFileTable(P, Section);
    else
      emitLineTablePrologueV5IncludeAndFileTable(P, Section);
  }

  void emitLineTableRows(const DWARFDebugLine::LineTable &LineTable,
                         SectionDescriptor &Section) {

    MCDwarfLineTableParams Params;
    Params.DWARF2LineOpcodeBase = LineTable.Prologue.OpcodeBase;
    Params.DWARF2LineBase = LineTable.Prologue.LineBase;
    Params.DWARF2LineRange = LineTable.Prologue.LineRange;

    SmallString<128> EncodingBuffer;

    if (LineTable.Rows.empty()) {
      // We only have the dummy entry, dsymutil emits an entry with a 0
      // address in that case.
      MCDwarfLineAddr::encode(*MC, Params, std::numeric_limits<int64_t>::max(),
                              0, EncodingBuffer);
      Section.OS.write(EncodingBuffer.c_str(), EncodingBuffer.size());
      return;
    }

    // Line table state machine fields
    unsigned FileNum = 1;
    unsigned LastLine = 1;
    unsigned Column = 0;
    unsigned Discriminator = 0;
    unsigned IsStatement = 1;
    unsigned Isa = 0;
    uint64_t Address = -1ULL;

    unsigned RowsSinceLastSequence = 0;

    for (const DWARFDebugLine::Row &Row : LineTable.Rows) {
      int64_t AddressDelta;
      if (Address == -1ULL) {
        Section.emitIntVal(dwarf::DW_LNS_extended_op, 1);
        encodeULEB128(Section.getFormParams().AddrSize + 1, Section.OS);
        Section.emitIntVal(dwarf::DW_LNE_set_address, 1);
        Section.emitIntVal(Row.Address.Address,
                           Section.getFormParams().AddrSize);
        AddressDelta = 0;
      } else {
        AddressDelta =
            (Row.Address.Address - Address) / LineTable.Prologue.MinInstLength;
      }

      // FIXME: code copied and transformed from
      // MCDwarf.cpp::EmitDwarfLineTable. We should find a way to share this
      // code, but the current compatibility requirement with classic dsymutil
      // makes it hard. Revisit that once this requirement is dropped.

      if (FileNum != Row.File) {
        FileNum = Row.File;
        Section.emitIntVal(dwarf::DW_LNS_set_file, 1);
        encodeULEB128(FileNum, Section.OS);
      }
      if (Column != Row.Column) {
        Column = Row.Column;
        Section.emitIntVal(dwarf::DW_LNS_set_column, 1);
        encodeULEB128(Column, Section.OS);
      }
      if (Discriminator != Row.Discriminator && MC->getDwarfVersion() >= 4) {
        Discriminator = Row.Discriminator;
        unsigned Size = getULEB128Size(Discriminator);
        Section.emitIntVal(dwarf::DW_LNS_extended_op, 1);
        encodeULEB128(Size + 1, Section.OS);
        Section.emitIntVal(dwarf::DW_LNE_set_discriminator, 1);
        encodeULEB128(Discriminator, Section.OS);
      }
      Discriminator = 0;

      if (Isa != Row.Isa) {
        Isa = Row.Isa;
        Section.emitIntVal(dwarf::DW_LNS_set_isa, 1);
        encodeULEB128(Isa, Section.OS);
      }
      if (IsStatement != Row.IsStmt) {
        IsStatement = Row.IsStmt;
        Section.emitIntVal(dwarf::DW_LNS_negate_stmt, 1);
      }
      if (Row.BasicBlock)
        Section.emitIntVal(dwarf::DW_LNS_set_basic_block, 1);

      if (Row.PrologueEnd)
        Section.emitIntVal(dwarf::DW_LNS_set_prologue_end, 1);

      if (Row.EpilogueBegin)
        Section.emitIntVal(dwarf::DW_LNS_set_epilogue_begin, 1);

      int64_t LineDelta = int64_t(Row.Line) - LastLine;
      if (!Row.EndSequence) {
        MCDwarfLineAddr::encode(*MC, Params, LineDelta, AddressDelta,
                                EncodingBuffer);
        Section.OS.write(EncodingBuffer.c_str(), EncodingBuffer.size());
        EncodingBuffer.resize(0);
        Address = Row.Address.Address;
        LastLine = Row.Line;
        RowsSinceLastSequence++;
      } else {
        if (LineDelta) {
          Section.emitIntVal(dwarf::DW_LNS_advance_line, 1);
          encodeSLEB128(LineDelta, Section.OS);
        }
        if (AddressDelta) {
          Section.emitIntVal(dwarf::DW_LNS_advance_pc, 1);
          encodeULEB128(AddressDelta, Section.OS);
        }
        MCDwarfLineAddr::encode(*MC, Params,
                                std::numeric_limits<int64_t>::max(), 0,
                                EncodingBuffer);
        Section.OS.write(EncodingBuffer.c_str(), EncodingBuffer.size());
        EncodingBuffer.resize(0);
        Address = -1ULL;
        LastLine = FileNum = IsStatement = 1;
        RowsSinceLastSequence = Column = Discriminator = Isa = 0;
      }
    }

    if (RowsSinceLastSequence) {
      MCDwarfLineAddr::encode(*MC, Params, std::numeric_limits<int64_t>::max(),
                              0, EncodingBuffer);
      Section.OS.write(EncodingBuffer.c_str(), EncodingBuffer.size());
      EncodingBuffer.resize(0);
    }
  }

  Triple TheTriple;
  DwarfUnit &U;

  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCContext> MC;
  std::unique_ptr<MCSubtargetInfo> MSTI;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_LIB_DWARFLINKER_PARALLEL_DEBUGLINESECTIONEMITTER_H
