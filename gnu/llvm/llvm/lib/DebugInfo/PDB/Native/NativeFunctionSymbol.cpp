//===- NativeFunctionSymbol.cpp - info about function symbols----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeFunctionSymbol.h"

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumSymbols.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeFunctionSymbol::NativeFunctionSymbol(NativeSession &Session,
                                           SymIndexId Id,
                                           const codeview::ProcSym &Sym,
                                           uint32_t Offset)
    : NativeRawSymbol(Session, PDB_SymType::Function, Id), Sym(Sym),
      RecordOffset(Offset) {}

NativeFunctionSymbol::~NativeFunctionSymbol() = default;

void NativeFunctionSymbol::dump(raw_ostream &OS, int Indent,
                                PdbSymbolIdField ShowIdFields,
                                PdbSymbolIdField RecurseIdFields) const {
  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);
  dumpSymbolField(OS, "name", getName(), Indent);
  dumpSymbolField(OS, "length", getLength(), Indent);
  dumpSymbolField(OS, "offset", getAddressOffset(), Indent);
  dumpSymbolField(OS, "section", getAddressSection(), Indent);
}

uint32_t NativeFunctionSymbol::getAddressOffset() const {
  return Sym.CodeOffset;
}

uint32_t NativeFunctionSymbol::getAddressSection() const { return Sym.Segment; }
std::string NativeFunctionSymbol::getName() const {
  return std::string(Sym.Name);
}

uint64_t NativeFunctionSymbol::getLength() const { return Sym.CodeSize; }

uint32_t NativeFunctionSymbol::getRelativeVirtualAddress() const {
  return Session.getRVAFromSectOffset(Sym.Segment, Sym.CodeOffset);
}

uint64_t NativeFunctionSymbol::getVirtualAddress() const {
  return Session.getVAFromSectOffset(Sym.Segment, Sym.CodeOffset);
}

static bool inlineSiteContainsAddress(InlineSiteSym &IS,
                                      uint32_t OffsetInFunc) {
  // Returns true if inline site contains the offset.
  bool Found = false;
  uint32_t CodeOffset = 0;
  for (auto &Annot : IS.annotations()) {
    switch (Annot.OpCode) {
    case BinaryAnnotationsOpCode::CodeOffset:
    case BinaryAnnotationsOpCode::ChangeCodeOffset:
    case BinaryAnnotationsOpCode::ChangeCodeOffsetAndLineOffset:
      CodeOffset += Annot.U1;
      if (OffsetInFunc >= CodeOffset)
        Found = true;
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLength:
      CodeOffset += Annot.U1;
      if (Found && OffsetInFunc < CodeOffset)
        return true;
      Found = false;
      break;
    case BinaryAnnotationsOpCode::ChangeCodeLengthAndCodeOffset:
      CodeOffset += Annot.U2;
      if (OffsetInFunc >= CodeOffset && OffsetInFunc < CodeOffset + Annot.U1)
        return true;
      Found = false;
      break;
    default:
      break;
    }
  }
  return false;
}

std::unique_ptr<IPDBEnumSymbols>
NativeFunctionSymbol::findInlineFramesByVA(uint64_t VA) const {
  uint16_t Modi;
  if (!Session.moduleIndexForVA(VA, Modi))
    return nullptr;

  Expected<ModuleDebugStreamRef> ModS = Session.getModuleDebugStream(Modi);
  if (!ModS) {
    consumeError(ModS.takeError());
    return nullptr;
  }
  CVSymbolArray Syms = ModS->getSymbolArray();

  // Search for inline sites. There should be one matching top level inline
  // site. Then search in its nested inline sites.
  std::vector<SymIndexId> Frames;
  uint32_t CodeOffset = VA - getVirtualAddress();
  auto Start = Syms.at(RecordOffset);
  auto End = Syms.at(Sym.End);
  while (Start != End) {
    bool Found = false;
    // Find matching inline site within Start and End.
    for (; Start != End; ++Start) {
      if (Start->kind() != S_INLINESITE)
        continue;

      InlineSiteSym IS =
          cantFail(SymbolDeserializer::deserializeAs<InlineSiteSym>(*Start));
      if (inlineSiteContainsAddress(IS, CodeOffset)) {
        // Insert frames in reverse order.
        SymIndexId Id = Session.getSymbolCache().getOrCreateInlineSymbol(
            IS, getVirtualAddress(), Modi, Start.offset());
        Frames.insert(Frames.begin(), Id);

        // Update offsets to search within this inline site.
        ++Start;
        End = Syms.at(IS.End);
        Found = true;
        break;
      }

      Start = Syms.at(IS.End);
      if (Start == End)
        break;
    }

    if (!Found)
      break;
  }

  return std::make_unique<NativeEnumSymbols>(Session, std::move(Frames));
}
