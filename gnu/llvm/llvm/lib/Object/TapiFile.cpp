//===- TapiFile.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Text-based Dynamcic Library Stub format.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/TapiFile.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/Platform.h"
#include "llvm/TextAPI/Symbol.h"

using namespace llvm;
using namespace MachO;
using namespace object;

static uint32_t getFlags(const Symbol *Sym) {
  uint32_t Flags = BasicSymbolRef::SF_Global;
  if (Sym->isUndefined())
    Flags |= BasicSymbolRef::SF_Undefined;
  else
    Flags |= BasicSymbolRef::SF_Exported;

  if (Sym->isWeakDefined() || Sym->isWeakReferenced())
    Flags |= BasicSymbolRef::SF_Weak;

  return Flags;
}

static SymbolRef::Type getType(const Symbol *Sym) {
  SymbolRef::Type Type = SymbolRef::ST_Unknown;
  if (Sym->isData())
    Type = SymbolRef::ST_Data;
  else if (Sym->isText())
    Type = SymbolRef::ST_Function;

  return Type;
}

TapiFile::TapiFile(MemoryBufferRef Source, const InterfaceFile &Interface,
                   Architecture Arch)
    : SymbolicFile(ID_TapiFile, Source), Arch(Arch),
      FileKind(Interface.getFileType()) {
  for (const auto *Symbol : Interface.symbols()) {
    if (!Symbol->getArchitectures().has(Arch))
      continue;

    switch (Symbol->getKind()) {
    case EncodeKind::GlobalSymbol:
      Symbols.emplace_back(StringRef(), Symbol->getName(), getFlags(Symbol),
                           ::getType(Symbol));
      break;
    case EncodeKind::ObjectiveCClass:
      if (Interface.getPlatforms().count(PLATFORM_MACOS) && Arch == AK_i386) {
        Symbols.emplace_back(ObjC1ClassNamePrefix, Symbol->getName(),
                             getFlags(Symbol), ::getType(Symbol));
      } else {
        Symbols.emplace_back(ObjC2ClassNamePrefix, Symbol->getName(),
                             getFlags(Symbol), ::getType(Symbol));
        Symbols.emplace_back(ObjC2MetaClassNamePrefix, Symbol->getName(),
                             getFlags(Symbol), ::getType(Symbol));
      }
      break;
    case EncodeKind::ObjectiveCClassEHType:
      Symbols.emplace_back(ObjC2EHTypePrefix, Symbol->getName(),
                           getFlags(Symbol), ::getType(Symbol));
      break;
    case EncodeKind::ObjectiveCInstanceVariable:
      Symbols.emplace_back(ObjC2IVarPrefix, Symbol->getName(), getFlags(Symbol),
                           ::getType(Symbol));
      break;
    }
  }
}

TapiFile::~TapiFile() = default;

void TapiFile::moveSymbolNext(DataRefImpl &DRI) const { DRI.d.a++; }

Error TapiFile::printSymbolName(raw_ostream &OS, DataRefImpl DRI) const {
  assert(DRI.d.a < Symbols.size() && "Attempt to access symbol out of bounds");
  const Symbol &Sym = Symbols[DRI.d.a];
  OS << Sym.Prefix << Sym.Name;
  return Error::success();
}

Expected<SymbolRef::Type> TapiFile::getSymbolType(DataRefImpl DRI) const {
  assert(DRI.d.a < Symbols.size() && "Attempt to access symbol out of bounds");
  return Symbols[DRI.d.a].Type;
}

Expected<uint32_t> TapiFile::getSymbolFlags(DataRefImpl DRI) const {
  assert(DRI.d.a < Symbols.size() && "Attempt to access symbol out of bounds");
  return Symbols[DRI.d.a].Flags;
}

basic_symbol_iterator TapiFile::symbol_begin() const {
  DataRefImpl DRI;
  DRI.d.a = 0;
  return BasicSymbolRef{DRI, this};
}

basic_symbol_iterator TapiFile::symbol_end() const {
  DataRefImpl DRI;
  DRI.d.a = Symbols.size();
  return BasicSymbolRef{DRI, this};
}
