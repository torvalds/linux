//===- IFSStub.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/

#include "llvm/InterfaceStub/IFSStub.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace llvm::ifs;

IFSStub::IFSStub(IFSStub const &Stub) {
  IfsVersion = Stub.IfsVersion;
  Target = Stub.Target;
  SoName = Stub.SoName;
  NeededLibs = Stub.NeededLibs;
  Symbols = Stub.Symbols;
}

IFSStub::IFSStub(IFSStub &&Stub) {
  IfsVersion = std::move(Stub.IfsVersion);
  Target = std::move(Stub.Target);
  SoName = std::move(Stub.SoName);
  NeededLibs = std::move(Stub.NeededLibs);
  Symbols = std::move(Stub.Symbols);
}

IFSStubTriple::IFSStubTriple(IFSStubTriple const &Stub) : IFSStub() {
  IfsVersion = Stub.IfsVersion;
  Target = Stub.Target;
  SoName = Stub.SoName;
  NeededLibs = Stub.NeededLibs;
  Symbols = Stub.Symbols;
}

IFSStubTriple::IFSStubTriple(IFSStub const &Stub) {
  IfsVersion = Stub.IfsVersion;
  Target = Stub.Target;
  SoName = Stub.SoName;
  NeededLibs = Stub.NeededLibs;
  Symbols = Stub.Symbols;
}

IFSStubTriple::IFSStubTriple(IFSStubTriple &&Stub) {
  IfsVersion = std::move(Stub.IfsVersion);
  Target = std::move(Stub.Target);
  SoName = std::move(Stub.SoName);
  NeededLibs = std::move(Stub.NeededLibs);
  Symbols = std::move(Stub.Symbols);
}

bool IFSTarget::empty() {
  return !Triple && !ObjectFormat && !Arch && !ArchString && !Endianness &&
         !BitWidth;
}

uint8_t ifs::convertIFSBitWidthToELF(IFSBitWidthType BitWidth) {
  switch (BitWidth) {
  case IFSBitWidthType::IFS32:
    return ELF::ELFCLASS32;
  case IFSBitWidthType::IFS64:
    return ELF::ELFCLASS64;
  default:
    llvm_unreachable("unknown bitwidth");
  }
}

uint8_t ifs::convertIFSEndiannessToELF(IFSEndiannessType Endianness) {
  switch (Endianness) {
  case IFSEndiannessType::Little:
    return ELF::ELFDATA2LSB;
  case IFSEndiannessType::Big:
    return ELF::ELFDATA2MSB;
  default:
    llvm_unreachable("unknown endianness");
  }
}

uint8_t ifs::convertIFSSymbolTypeToELF(IFSSymbolType SymbolType) {
  switch (SymbolType) {
  case IFSSymbolType::Object:
    return ELF::STT_OBJECT;
  case IFSSymbolType::Func:
    return ELF::STT_FUNC;
  case IFSSymbolType::TLS:
    return ELF::STT_TLS;
  case IFSSymbolType::NoType:
    return ELF::STT_NOTYPE;
  default:
    llvm_unreachable("unknown symbol type");
  }
}

IFSBitWidthType ifs::convertELFBitWidthToIFS(uint8_t BitWidth) {
  switch (BitWidth) {
  case ELF::ELFCLASS32:
    return IFSBitWidthType::IFS32;
  case ELF::ELFCLASS64:
    return IFSBitWidthType::IFS64;
  default:
    return IFSBitWidthType::Unknown;
  }
}

IFSEndiannessType ifs::convertELFEndiannessToIFS(uint8_t Endianness) {
  switch (Endianness) {
  case ELF::ELFDATA2LSB:
    return IFSEndiannessType::Little;
  case ELF::ELFDATA2MSB:
    return IFSEndiannessType::Big;
  default:
    return IFSEndiannessType::Unknown;
  }
}

IFSSymbolType ifs::convertELFSymbolTypeToIFS(uint8_t SymbolType) {
  SymbolType = SymbolType & 0xf;
  switch (SymbolType) {
  case ELF::STT_OBJECT:
    return IFSSymbolType::Object;
  case ELF::STT_FUNC:
    return IFSSymbolType::Func;
  case ELF::STT_TLS:
    return IFSSymbolType::TLS;
  case ELF::STT_NOTYPE:
    return IFSSymbolType::NoType;
  default:
    return IFSSymbolType::Unknown;
  }
}
