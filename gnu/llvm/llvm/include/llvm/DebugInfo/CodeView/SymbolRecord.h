//===- SymbolRecord.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORD_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORD_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Endian.h"
#include <cstdint>
#include <vector>

namespace llvm {
namespace codeview {

class SymbolRecord {
protected:
  explicit SymbolRecord(SymbolRecordKind Kind) : Kind(Kind) {}

public:
  SymbolRecordKind getKind() const { return Kind; }

  SymbolRecordKind Kind;
};

// S_GPROC32, S_LPROC32, S_GPROC32_ID, S_LPROC32_ID, S_LPROC32_DPC or
// S_LPROC32_DPC_ID
class ProcSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 32;

public:
  explicit ProcSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  ProcSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  uint32_t Parent = 0;
  uint32_t End = 0;
  uint32_t Next = 0;
  uint32_t CodeSize = 0;
  uint32_t DbgStart = 0;
  uint32_t DbgEnd = 0;
  TypeIndex FunctionType;
  uint32_t CodeOffset = 0;
  uint16_t Segment = 0;
  ProcSymFlags Flags = ProcSymFlags::None;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_THUNK32
class Thunk32Sym : public SymbolRecord {
public:
  explicit Thunk32Sym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  Thunk32Sym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  uint32_t Parent = 0;
  uint32_t End = 0;
  uint32_t Next = 0;
  uint32_t Offset = 0;
  uint16_t Segment = 0;
  uint16_t Length = 0;
  ThunkOrdinal Thunk = ThunkOrdinal::Standard;
  StringRef Name;
  ArrayRef<uint8_t> VariantData;

  uint32_t RecordOffset = 0;
};

// S_TRAMPOLINE
class TrampolineSym : public SymbolRecord {
public:
  explicit TrampolineSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  TrampolineSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  TrampolineType Type;
  uint16_t Size = 0;
  uint32_t ThunkOffset = 0;
  uint32_t TargetOffset = 0;
  uint16_t ThunkSection = 0;
  uint16_t TargetSection = 0;

  uint32_t RecordOffset = 0;
};

// S_SECTION
class SectionSym : public SymbolRecord {
public:
  explicit SectionSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  SectionSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  uint16_t SectionNumber = 0;
  uint8_t Alignment = 0;
  uint32_t Rva = 0;
  uint32_t Length = 0;
  uint32_t Characteristics = 0;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_COFFGROUP
class CoffGroupSym : public SymbolRecord {
public:
  explicit CoffGroupSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  CoffGroupSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  uint32_t Size = 0;
  uint32_t Characteristics = 0;
  uint32_t Offset = 0;
  uint16_t Segment = 0;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

class ScopeEndSym : public SymbolRecord {
public:
  explicit ScopeEndSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  ScopeEndSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  uint32_t RecordOffset = 0;
};

class JumpTableSym : public SymbolRecord {
public:
  explicit JumpTableSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  JumpTableSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::JumpTableSym),
        RecordOffset(RecordOffset) {}

  uint32_t BaseOffset = 0;
  uint16_t BaseSegment = 0;

  JumpTableEntrySize SwitchType;
  uint32_t BranchOffset = 0;
  uint32_t TableOffset = 0;
  uint16_t BranchSegment = 0;
  uint16_t TableSegment = 0;

  uint32_t EntriesCount = 0;

  uint32_t RecordOffset = 0;
};

class CallerSym : public SymbolRecord {
public:
  explicit CallerSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  CallerSym(SymbolRecordKind Kind, uint32_t RecordOffset)
      : SymbolRecord(Kind), RecordOffset(RecordOffset) {}

  std::vector<TypeIndex> Indices;

  uint32_t RecordOffset = 0;
};

struct DecodedAnnotation {
  StringRef Name;
  ArrayRef<uint8_t> Bytes;
  BinaryAnnotationsOpCode OpCode = BinaryAnnotationsOpCode::Invalid;
  uint32_t U1 = 0;
  uint32_t U2 = 0;
  int32_t S1 = 0;
};

struct BinaryAnnotationIterator
    : public iterator_facade_base<BinaryAnnotationIterator,
                                  std::forward_iterator_tag,
                                  DecodedAnnotation> {
  BinaryAnnotationIterator() = default;
  BinaryAnnotationIterator(ArrayRef<uint8_t> Annotations) : Data(Annotations) {}
  BinaryAnnotationIterator(const BinaryAnnotationIterator &Other)
      : Data(Other.Data) {}

  bool operator==(BinaryAnnotationIterator Other) const {
    return Data == Other.Data;
  }

  BinaryAnnotationIterator &operator=(const BinaryAnnotationIterator Other) {
    Data = Other.Data;
    return *this;
  }

  BinaryAnnotationIterator &operator++() {
    if (!ParseCurrentAnnotation()) {
      *this = BinaryAnnotationIterator();
      return *this;
    }
    Data = Next;
    Next = ArrayRef<uint8_t>();
    Current.reset();
    return *this;
  }

  const DecodedAnnotation &operator*() {
    ParseCurrentAnnotation();
    return *Current;
  }

private:
  static uint32_t GetCompressedAnnotation(ArrayRef<uint8_t> &Annotations) {
    if (Annotations.empty())
      return -1;

    uint8_t FirstByte = Annotations.front();
    Annotations = Annotations.drop_front();

    if ((FirstByte & 0x80) == 0x00)
      return FirstByte;

    if (Annotations.empty())
      return -1;

    uint8_t SecondByte = Annotations.front();
    Annotations = Annotations.drop_front();

    if ((FirstByte & 0xC0) == 0x80)
      return ((FirstByte & 0x3F) << 8) | SecondByte;

    if (Annotations.empty())
      return -1;

    uint8_t ThirdByte = Annotations.front();
    Annotations = Annotations.drop_front();

    if (Annotations.empty())
      return -1;

    uint8_t FourthByte = Annotations.front();
    Annotations = Annotations.drop_front();

    if ((FirstByte & 0xE0) == 0xC0)
      return ((FirstByte & 0x1F) << 24) | (SecondByte << 16) |
             (ThirdByte << 8) | FourthByte;

    return -1;
  }

  static int32_t DecodeSignedOperand(uint32_t Operand) {
    if (Operand & 1)
      return -(Operand >> 1);
    return Operand >> 1;
  }

  static int32_t DecodeSignedOperand(ArrayRef<uint8_t> &Annotations) {
    return DecodeSignedOperand(GetCompressedAnnotation(Annotations));
  }

  bool ParseCurrentAnnotation() {
    if (Current)
      return true;

    Next = Data;
    uint32_t Op = GetCompressedAnnotation(Next);
    DecodedAnnotation Result;
    Result.OpCode = static_cast<BinaryAnnotationsOpCode>(Op);
    switch (Result.OpCode) {
    case BinaryAnnotationsOpCode::Invalid:
      Result.Name = "Invalid";
      Next = ArrayRef<uint8_t>();
      break;
    case BinaryAnnotationsOpCode::CodeOffset:
      Result.Name = "CodeOffset";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeOffsetBase:
      Result.Name = "ChangeCodeOffsetBase";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeOffset:
      Result.Name = "ChangeCodeOffset";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLength:
      Result.Name = "ChangeCodeLength";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeFile:
      Result.Name = "ChangeFile";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeLineEndDelta:
      Result.Name = "ChangeLineEndDelta";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeRangeKind:
      Result.Name = "ChangeRangeKind";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeColumnStart:
      Result.Name = "ChangeColumnStart";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeColumnEnd:
      Result.Name = "ChangeColumnEnd";
      Result.U1 = GetCompressedAnnotation(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeLineOffset:
      Result.Name = "ChangeLineOffset";
      Result.S1 = DecodeSignedOperand(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeColumnEndDelta:
      Result.Name = "ChangeColumnEndDelta";
      Result.S1 = DecodeSignedOperand(Next);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeOffsetAndLineOffset: {
      Result.Name = "ChangeCodeOffsetAndLineOffset";
      uint32_t Annotation = GetCompressedAnnotation(Next);
      Result.S1 = DecodeSignedOperand(Annotation >> 4);
      Result.U1 = Annotation & 0xf;
      break;
    }
    case BinaryAnnotationsOpCode::ChangeCodeLengthAndCodeOffset: {
      Result.Name = "ChangeCodeLengthAndCodeOffset";
      Result.U1 = GetCompressedAnnotation(Next);
      Result.U2 = GetCompressedAnnotation(Next);
      break;
    }
    }
    Result.Bytes = Data.take_front(Data.size() - Next.size());
    Current = Result;
    return true;
  }

  std::optional<DecodedAnnotation> Current;
  ArrayRef<uint8_t> Data;
  ArrayRef<uint8_t> Next;
};

// S_INLINESITE
class InlineSiteSym : public SymbolRecord {
public:
  explicit InlineSiteSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit InlineSiteSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::InlineSiteSym),
        RecordOffset(RecordOffset) {}

  iterator_range<BinaryAnnotationIterator> annotations() const {
    return make_range(BinaryAnnotationIterator(AnnotationData),
                      BinaryAnnotationIterator());
  }

  uint32_t Parent = 0;
  uint32_t End = 0;
  TypeIndex Inlinee;
  std::vector<uint8_t> AnnotationData;

  uint32_t RecordOffset = 0;
};

struct PublicSym32Header {
  ulittle32_t Flags;
  ulittle32_t Offset;
  ulittle16_t Segment;
  // char Name[];
};

// S_PUB32
class PublicSym32 : public SymbolRecord {
public:
  PublicSym32() : SymbolRecord(SymbolRecordKind::PublicSym32) {}
  explicit PublicSym32(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit PublicSym32(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::PublicSym32),
        RecordOffset(RecordOffset) {}

  PublicSymFlags Flags = PublicSymFlags::None;
  uint32_t Offset = 0;
  uint16_t Segment = 0;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_REGISTER
class RegisterSym : public SymbolRecord {
public:
  explicit RegisterSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit RegisterSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::RegisterSym),
        RecordOffset(RecordOffset) {}

  TypeIndex Index;
  RegisterId Register;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_PROCREF, S_LPROCREF
class ProcRefSym : public SymbolRecord {
public:
  explicit ProcRefSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit ProcRefSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ProcRefSym), RecordOffset(RecordOffset) {
  }

  uint32_t SumName = 0;
  uint32_t SymOffset = 0;
  uint16_t Module = 0;
  StringRef Name;

  uint16_t modi() const { return Module - 1; }
  uint32_t RecordOffset = 0;
};

// S_LOCAL
class LocalSym : public SymbolRecord {
public:
  explicit LocalSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit LocalSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::LocalSym), RecordOffset(RecordOffset) {}

  TypeIndex Type;
  LocalSymFlags Flags = LocalSymFlags::None;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

struct LocalVariableAddrRange {
  uint32_t OffsetStart = 0;
  uint16_t ISectStart = 0;
  uint16_t Range = 0;
};

struct LocalVariableAddrGap {
  uint16_t GapStartOffset = 0;
  uint16_t Range = 0;
};

enum : uint16_t { MaxDefRange = 0xf000 };

// S_DEFRANGE
class DefRangeSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 8;

public:
  explicit DefRangeSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit DefRangeSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeSym),
        RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  uint32_t Program = 0;
  LocalVariableAddrRange Range;
  std::vector<LocalVariableAddrGap> Gaps;

  uint32_t RecordOffset = 0;
};

// S_DEFRANGE_SUBFIELD
class DefRangeSubfieldSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 12;

public:
  explicit DefRangeSubfieldSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit DefRangeSubfieldSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeSubfieldSym),
        RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  uint32_t Program = 0;
  uint16_t OffsetInParent = 0;
  LocalVariableAddrRange Range;
  std::vector<LocalVariableAddrGap> Gaps;

  uint32_t RecordOffset = 0;
};

struct DefRangeRegisterHeader {
  ulittle16_t Register;
  ulittle16_t MayHaveNoName;
};

// S_DEFRANGE_REGISTER
class DefRangeRegisterSym : public SymbolRecord {
public:
  explicit DefRangeRegisterSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit DefRangeRegisterSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeRegisterSym),
        RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const { return RecordOffset + sizeof(DefRangeRegisterHeader); }

  DefRangeRegisterHeader Hdr;
  LocalVariableAddrRange Range;
  std::vector<LocalVariableAddrGap> Gaps;

  uint32_t RecordOffset = 0;
};

struct DefRangeSubfieldRegisterHeader {
  ulittle16_t Register;
  ulittle16_t MayHaveNoName;
  ulittle32_t OffsetInParent;
};

// S_DEFRANGE_SUBFIELD_REGISTER
class DefRangeSubfieldRegisterSym : public SymbolRecord {
public:
  explicit DefRangeSubfieldRegisterSym(SymbolRecordKind Kind)
      : SymbolRecord(Kind) {}
  explicit DefRangeSubfieldRegisterSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeSubfieldRegisterSym),
        RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const { return RecordOffset + sizeof(DefRangeSubfieldRegisterHeader); }

  DefRangeSubfieldRegisterHeader Hdr;
  LocalVariableAddrRange Range;
  std::vector<LocalVariableAddrGap> Gaps;

  uint32_t RecordOffset = 0;
};

struct DefRangeFramePointerRelHeader {
  little32_t Offset;
};

// S_DEFRANGE_FRAMEPOINTER_REL
class DefRangeFramePointerRelSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 8;

public:
  explicit DefRangeFramePointerRelSym(SymbolRecordKind Kind)
      : SymbolRecord(Kind) {}
  explicit DefRangeFramePointerRelSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeFramePointerRelSym),
        RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  DefRangeFramePointerRelHeader Hdr;
  LocalVariableAddrRange Range;
  std::vector<LocalVariableAddrGap> Gaps;

  uint32_t RecordOffset = 0;
};

struct DefRangeRegisterRelHeader {
  ulittle16_t Register;
  ulittle16_t Flags;
  little32_t BasePointerOffset;
};

// S_DEFRANGE_REGISTER_REL
class DefRangeRegisterRelSym : public SymbolRecord {
public:
  explicit DefRangeRegisterRelSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit DefRangeRegisterRelSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeRegisterRelSym),
        RecordOffset(RecordOffset) {}

  // The flags implement this notional bitfield:
  //   uint16_t IsSubfield : 1;
  //   uint16_t Padding : 3;
  //   uint16_t OffsetInParent : 12;
  enum : uint16_t {
    IsSubfieldFlag = 1,
    OffsetInParentShift = 4,
  };

  bool hasSpilledUDTMember() const { return Hdr.Flags & IsSubfieldFlag; }
  uint16_t offsetInParent() const { return Hdr.Flags >> OffsetInParentShift; }

  uint32_t getRelocationOffset() const { return RecordOffset + sizeof(DefRangeRegisterRelHeader); }

  DefRangeRegisterRelHeader Hdr;
  LocalVariableAddrRange Range;
  std::vector<LocalVariableAddrGap> Gaps;

  uint32_t RecordOffset = 0;
};

// S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE
class DefRangeFramePointerRelFullScopeSym : public SymbolRecord {
public:
  explicit DefRangeFramePointerRelFullScopeSym(SymbolRecordKind Kind)
      : SymbolRecord(Kind) {}
  explicit DefRangeFramePointerRelFullScopeSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DefRangeFramePointerRelFullScopeSym),
        RecordOffset(RecordOffset) {}

  int32_t Offset = 0;

  uint32_t RecordOffset = 0;
};

// S_BLOCK32
class BlockSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 16;

public:
  explicit BlockSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit BlockSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::BlockSym), RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  uint32_t Parent = 0;
  uint32_t End = 0;
  uint32_t CodeSize = 0;
  uint32_t CodeOffset = 0;
  uint16_t Segment = 0;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_LABEL32
class LabelSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 4;

public:
  explicit LabelSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit LabelSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::LabelSym), RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  uint32_t CodeOffset = 0;
  uint16_t Segment = 0;
  ProcSymFlags Flags = ProcSymFlags::None;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_OBJNAME
class ObjNameSym : public SymbolRecord {
public:
  explicit ObjNameSym() : SymbolRecord(SymbolRecordKind::ObjNameSym) {}
  explicit ObjNameSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit ObjNameSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ObjNameSym), RecordOffset(RecordOffset) {
  }

  uint32_t Signature = 0;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_ENVBLOCK
class EnvBlockSym : public SymbolRecord {
public:
  explicit EnvBlockSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit EnvBlockSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::EnvBlockSym),
        RecordOffset(RecordOffset) {}

  std::vector<StringRef> Fields;

  uint32_t RecordOffset = 0;
};

// S_EXPORT
class ExportSym : public SymbolRecord {
public:
  explicit ExportSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit ExportSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ExportSym), RecordOffset(RecordOffset) {}

  uint16_t Ordinal = 0;
  ExportFlags Flags = ExportFlags::None;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_FILESTATIC
class FileStaticSym : public SymbolRecord {
public:
  explicit FileStaticSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit FileStaticSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::FileStaticSym),
        RecordOffset(RecordOffset) {}

  TypeIndex Index;
  uint32_t ModFilenameOffset = 0;
  LocalSymFlags Flags = LocalSymFlags::None;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_COMPILE2
class Compile2Sym : public SymbolRecord {
public:
  explicit Compile2Sym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit Compile2Sym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::Compile2Sym),
        RecordOffset(RecordOffset) {}

  CompileSym2Flags Flags = CompileSym2Flags::None;
  CPUType Machine;
  uint16_t VersionFrontendMajor = 0;
  uint16_t VersionFrontendMinor = 0;
  uint16_t VersionFrontendBuild = 0;
  uint16_t VersionBackendMajor = 0;
  uint16_t VersionBackendMinor = 0;
  uint16_t VersionBackendBuild = 0;
  StringRef Version;
  std::vector<StringRef> ExtraStrings;

  uint8_t getLanguage() const { return static_cast<uint32_t>(Flags) & 0xFF; }
  uint32_t getFlags() const { return static_cast<uint32_t>(Flags) & ~0xFF; }

  uint32_t RecordOffset = 0;
};

// S_COMPILE3
class Compile3Sym : public SymbolRecord {
public:
  Compile3Sym() : SymbolRecord(SymbolRecordKind::Compile3Sym) {}
  explicit Compile3Sym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit Compile3Sym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::Compile3Sym),
        RecordOffset(RecordOffset) {}

  CompileSym3Flags Flags = CompileSym3Flags::None;
  CPUType Machine;
  uint16_t VersionFrontendMajor = 0;
  uint16_t VersionFrontendMinor = 0;
  uint16_t VersionFrontendBuild = 0;
  uint16_t VersionFrontendQFE = 0;
  uint16_t VersionBackendMajor = 0;
  uint16_t VersionBackendMinor = 0;
  uint16_t VersionBackendBuild = 0;
  uint16_t VersionBackendQFE = 0;
  StringRef Version;

  void setLanguage(SourceLanguage Lang) {
    Flags = CompileSym3Flags((uint32_t(Flags) & 0xFFFFFF00) | uint32_t(Lang));
  }

  SourceLanguage getLanguage() const {
    return static_cast<SourceLanguage>(static_cast<uint32_t>(Flags) & 0xFF);
  }
  CompileSym3Flags getFlags() const {
    return static_cast<CompileSym3Flags>(static_cast<uint32_t>(Flags) & ~0xFF);
  }

  bool hasOptimizations() const {
    return CompileSym3Flags::None !=
           (getFlags() & (CompileSym3Flags::PGO | CompileSym3Flags::LTCG));
  }

  uint32_t RecordOffset = 0;
};

// S_FRAMEPROC
class FrameProcSym : public SymbolRecord {
public:
  explicit FrameProcSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit FrameProcSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::FrameProcSym),
        RecordOffset(RecordOffset) {}

  uint32_t TotalFrameBytes = 0;
  uint32_t PaddingFrameBytes = 0;
  uint32_t OffsetToPadding = 0;
  uint32_t BytesOfCalleeSavedRegisters = 0;
  uint32_t OffsetOfExceptionHandler = 0;
  uint16_t SectionIdOfExceptionHandler = 0;
  FrameProcedureOptions Flags = FrameProcedureOptions::None;

  /// Extract the register this frame uses to refer to local variables.
  RegisterId getLocalFramePtrReg(CPUType CPU) const {
    return decodeFramePtrReg(
        EncodedFramePtrReg((uint32_t(Flags) >> 14U) & 0x3U), CPU);
  }

  /// Extract the register this frame uses to refer to parameters.
  RegisterId getParamFramePtrReg(CPUType CPU) const {
    return decodeFramePtrReg(
        EncodedFramePtrReg((uint32_t(Flags) >> 16U) & 0x3U), CPU);
  }

  uint32_t RecordOffset = 0;

private:
};

// S_CALLSITEINFO
class CallSiteInfoSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 4;

public:
  explicit CallSiteInfoSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit CallSiteInfoSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::CallSiteInfoSym) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  uint32_t CodeOffset = 0;
  uint16_t Segment = 0;
  TypeIndex Type;

  uint32_t RecordOffset = 0;
};

// S_HEAPALLOCSITE
class HeapAllocationSiteSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 4;

public:
  explicit HeapAllocationSiteSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit HeapAllocationSiteSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::HeapAllocationSiteSym),
        RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  uint32_t CodeOffset = 0;
  uint16_t Segment = 0;
  uint16_t CallInstructionSize = 0;
  TypeIndex Type;

  uint32_t RecordOffset = 0;
};

// S_FRAMECOOKIE
class FrameCookieSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 4;

public:
  explicit FrameCookieSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit FrameCookieSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::FrameCookieSym) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  uint32_t CodeOffset = 0;
  uint16_t Register = 0;
  FrameCookieKind CookieKind;
  uint8_t Flags = 0;

  uint32_t RecordOffset = 0;
};

// S_UDT, S_COBOLUDT
class UDTSym : public SymbolRecord {
public:
  explicit UDTSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit UDTSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::UDTSym) {}

  TypeIndex Type;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_BUILDINFO
class BuildInfoSym : public SymbolRecord {
public:
  explicit BuildInfoSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit BuildInfoSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::BuildInfoSym),
        RecordOffset(RecordOffset) {}

  TypeIndex BuildId;

  uint32_t RecordOffset = 0;
};

// S_BPREL32
class BPRelativeSym : public SymbolRecord {
public:
  explicit BPRelativeSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit BPRelativeSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::BPRelativeSym),
        RecordOffset(RecordOffset) {}

  int32_t Offset = 0;
  TypeIndex Type;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_REGREL32
class RegRelativeSym : public SymbolRecord {
public:
  explicit RegRelativeSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit RegRelativeSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::RegRelativeSym),
        RecordOffset(RecordOffset) {}

  uint32_t Offset = 0;
  TypeIndex Type;
  RegisterId Register;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_CONSTANT, S_MANCONSTANT
class ConstantSym : public SymbolRecord {
public:
  explicit ConstantSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit ConstantSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ConstantSym),
        RecordOffset(RecordOffset) {}

  TypeIndex Type;
  APSInt Value;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_LDATA32, S_GDATA32, S_LMANDATA, S_GMANDATA
class DataSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 8;

public:
  explicit DataSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit DataSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::DataSym), RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  TypeIndex Type;
  uint32_t DataOffset = 0;
  uint16_t Segment = 0;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_LTHREAD32, S_GTHREAD32
class ThreadLocalDataSym : public SymbolRecord {
  static constexpr uint32_t RelocationOffset = 8;

public:
  explicit ThreadLocalDataSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit ThreadLocalDataSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::ThreadLocalDataSym),
        RecordOffset(RecordOffset) {}

  uint32_t getRelocationOffset() const {
    return RecordOffset + RelocationOffset;
  }

  TypeIndex Type;
  uint32_t DataOffset = 0;
  uint16_t Segment = 0;
  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_UNAMESPACE
class UsingNamespaceSym : public SymbolRecord {
public:
  explicit UsingNamespaceSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit UsingNamespaceSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::UsingNamespaceSym),
        RecordOffset(RecordOffset) {}

  StringRef Name;

  uint32_t RecordOffset = 0;
};

// S_ANNOTATION
class AnnotationSym : public SymbolRecord {
public:
  explicit AnnotationSym(SymbolRecordKind Kind) : SymbolRecord(Kind) {}
  explicit AnnotationSym(uint32_t RecordOffset)
      : SymbolRecord(SymbolRecordKind::AnnotationSym),
        RecordOffset(RecordOffset) {}

  uint32_t CodeOffset = 0;
  uint16_t Segment = 0;
  std::vector<StringRef> Strings;

  uint32_t RecordOffset = 0;
};

Expected<CVSymbol> readSymbolFromStream(BinaryStreamRef Stream,
                                        uint32_t Offset);

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLRECORD_H
