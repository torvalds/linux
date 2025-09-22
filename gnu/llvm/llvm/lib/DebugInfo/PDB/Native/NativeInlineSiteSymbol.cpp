//===- NativeInlineSiteSymbol.cpp - info about inline sites -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeInlineSiteSymbol.h"

#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumLineNumbers.h"
#include "llvm/DebugInfo/PDB/Native/NativeLineNumber.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeInlineSiteSymbol::NativeInlineSiteSymbol(
    NativeSession &Session, SymIndexId Id, const codeview::InlineSiteSym &Sym,
    uint64_t ParentAddr)
    : NativeRawSymbol(Session, PDB_SymType::InlineSite, Id), Sym(Sym),
      ParentAddr(ParentAddr) {}

NativeInlineSiteSymbol::~NativeInlineSiteSymbol() = default;

void NativeInlineSiteSymbol::dump(raw_ostream &OS, int Indent,
                                  PdbSymbolIdField ShowIdFields,
                                  PdbSymbolIdField RecurseIdFields) const {
  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);
  dumpSymbolField(OS, "name", getName(), Indent);
}

static std::optional<InlineeSourceLine>
findInlineeByTypeIndex(TypeIndex Id, ModuleDebugStreamRef &ModS) {
  for (const auto &SS : ModS.getSubsectionsArray()) {
    if (SS.kind() != DebugSubsectionKind::InlineeLines)
      continue;

    DebugInlineeLinesSubsectionRef InlineeLines;
    BinaryStreamReader Reader(SS.getRecordData());
    if (auto EC = InlineeLines.initialize(Reader)) {
      consumeError(std::move(EC));
      continue;
    }

    for (const InlineeSourceLine &Line : InlineeLines)
      if (Line.Header->Inlinee == Id)
        return Line;
  }
  return std::nullopt;
}

std::string NativeInlineSiteSymbol::getName() const {
  auto Tpi = Session.getPDBFile().getPDBTpiStream();
  if (!Tpi) {
    consumeError(Tpi.takeError());
    return "";
  }
  auto Ipi = Session.getPDBFile().getPDBIpiStream();
  if (!Ipi) {
    consumeError(Ipi.takeError());
    return "";
  }

  LazyRandomTypeCollection &Types = Tpi->typeCollection();
  LazyRandomTypeCollection &Ids = Ipi->typeCollection();
  CVType InlineeType = Ids.getType(Sym.Inlinee);
  std::string QualifiedName;
  if (InlineeType.kind() == LF_MFUNC_ID) {
    MemberFuncIdRecord MFRecord;
    cantFail(TypeDeserializer::deserializeAs<MemberFuncIdRecord>(InlineeType,
                                                                 MFRecord));
    TypeIndex ClassTy = MFRecord.getClassType();
    QualifiedName.append(std::string(Types.getTypeName(ClassTy)));
    QualifiedName.append("::");
  } else if (InlineeType.kind() == LF_FUNC_ID) {
    FuncIdRecord FRecord;
    cantFail(
        TypeDeserializer::deserializeAs<FuncIdRecord>(InlineeType, FRecord));
    TypeIndex ParentScope = FRecord.getParentScope();
    if (!ParentScope.isNoneType()) {
      QualifiedName.append(std::string(Ids.getTypeName(ParentScope)));
      QualifiedName.append("::");
    }
  }

  QualifiedName.append(std::string(Ids.getTypeName(Sym.Inlinee)));
  return QualifiedName;
}

void NativeInlineSiteSymbol::getLineOffset(uint32_t OffsetInFunc,
                                           uint32_t &LineOffset,
                                           uint32_t &FileOffset) const {
  LineOffset = 0;
  FileOffset = 0;
  uint32_t CodeOffset = 0;
  std::optional<uint32_t> CodeOffsetBase;
  std::optional<uint32_t> CodeOffsetEnd;
  std::optional<int32_t> CurLineOffset;
  std::optional<int32_t> NextLineOffset;
  std::optional<uint32_t> NextFileOffset;
  auto UpdateCodeOffset = [&](uint32_t Delta) {
    if (!CodeOffsetBase)
      CodeOffsetBase = CodeOffset;
    else if (!CodeOffsetEnd)
      CodeOffsetEnd = *CodeOffsetBase + Delta;
  };
  auto UpdateLineOffset = [&](int32_t Delta) {
    LineOffset += Delta;
    if (!CodeOffsetBase || !CurLineOffset)
      CurLineOffset = LineOffset;
    else
      NextLineOffset = LineOffset;
  };
  auto UpdateFileOffset = [&](uint32_t Offset) {
    if (!CodeOffsetBase)
      FileOffset = Offset;
    else
      NextFileOffset = Offset;
  };
  auto ValidateAndReset = [&]() {
    // Current range is finished. Check if OffsetInFunc is in the range.
    if (CodeOffsetBase && CodeOffsetEnd && CurLineOffset) {
      if (CodeOffsetBase <= OffsetInFunc && OffsetInFunc < CodeOffsetEnd) {
        LineOffset = *CurLineOffset;
        return true;
      }
      // Set base, end, file offset and line offset for next range.
      if (NextFileOffset)
        FileOffset = *NextFileOffset;
      if (NextLineOffset) {
        CurLineOffset = NextLineOffset;
        NextLineOffset = std::nullopt;
      }
      CodeOffsetBase = CodeOffsetEnd;
      CodeOffsetEnd = NextFileOffset = std::nullopt;
    }
    return false;
  };
  for (const auto &Annot : Sym.annotations()) {
    switch (Annot.OpCode) {
    case BinaryAnnotationsOpCode::CodeOffset:
    case BinaryAnnotationsOpCode::ChangeCodeOffset:
    case BinaryAnnotationsOpCode::ChangeCodeOffsetBase:
      CodeOffset += Annot.U1;
      UpdateCodeOffset(Annot.U1);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLength:
      UpdateCodeOffset(Annot.U1);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLengthAndCodeOffset:
      CodeOffset += Annot.U2;
      UpdateCodeOffset(Annot.U2);
      UpdateCodeOffset(Annot.U1);
      break;
    case BinaryAnnotationsOpCode::ChangeLineOffset:
      UpdateLineOffset(Annot.S1);
      break;
    case BinaryAnnotationsOpCode::ChangeCodeOffsetAndLineOffset:
      CodeOffset += Annot.U1;
      UpdateCodeOffset(Annot.U1);
      UpdateLineOffset(Annot.S1);
      break;
    case BinaryAnnotationsOpCode::ChangeFile:
      UpdateFileOffset(Annot.U1);
      break;
    default:
      break;
    }

    if (ValidateAndReset())
      return;
  }
}

std::unique_ptr<IPDBEnumLineNumbers>
NativeInlineSiteSymbol::findInlineeLinesByVA(uint64_t VA,
                                             uint32_t Length) const {
  uint16_t Modi;
  if (!Session.moduleIndexForVA(VA, Modi))
    return nullptr;

  Expected<ModuleDebugStreamRef> ModS = Session.getModuleDebugStream(Modi);
  if (!ModS) {
    consumeError(ModS.takeError());
    return nullptr;
  }

  Expected<DebugChecksumsSubsectionRef> Checksums =
      ModS->findChecksumsSubsection();
  if (!Checksums) {
    consumeError(Checksums.takeError());
    return nullptr;
  }

  // Get the line number offset and source file offset.
  uint32_t SrcLineOffset;
  uint32_t SrcFileOffset;
  getLineOffset(VA - ParentAddr, SrcLineOffset, SrcFileOffset);

  // Get line info from inlinee line table.
  std::optional<InlineeSourceLine> Inlinee =
      findInlineeByTypeIndex(Sym.Inlinee, ModS.get());

  if (!Inlinee)
    return nullptr;

  uint32_t SrcLine = Inlinee->Header->SourceLineNum + SrcLineOffset;
  uint32_t SrcCol = 0; // Inline sites don't seem to have column info.
  uint32_t FileChecksumOffset =
      (SrcFileOffset == 0) ? Inlinee->Header->FileID : SrcFileOffset;

  auto ChecksumIter = Checksums->getArray().at(FileChecksumOffset);
  uint32_t SrcFileId =
      Session.getSymbolCache().getOrCreateSourceFile(*ChecksumIter);

  uint32_t LineSect, LineOff;
  Session.addressForVA(VA, LineSect, LineOff);
  NativeLineNumber LineNum(Session, SrcLine, SrcCol, LineSect, LineOff, Length,
                           SrcFileId, Modi);
  auto SrcFile = Session.getSymbolCache().getSourceFileById(SrcFileId);
  std::vector<NativeLineNumber> Lines{LineNum};

  return std::make_unique<NativeEnumLineNumbers>(std::move(Lines));
}
