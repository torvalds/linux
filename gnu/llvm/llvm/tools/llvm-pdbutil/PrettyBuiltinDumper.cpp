//===- PrettyBuiltinDumper.cpp ---------------------------------- *- C++ *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PrettyBuiltinDumper.h"

#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"

using namespace llvm;
using namespace llvm::pdb;

BuiltinDumper::BuiltinDumper(LinePrinter &P)
    : PDBSymDumper(false), Printer(P) {}

void BuiltinDumper::start(const PDBSymbolTypeBuiltin &Symbol) {
  if (Symbol.isConstType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "const ";
  if (Symbol.isVolatileType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "volatile ";
  WithColor(Printer, PDB_ColorItem::Type).get() << getTypeName(Symbol);
}

StringRef BuiltinDumper::getTypeName(const PDBSymbolTypeBuiltin &Symbol) {
  PDB_BuiltinType Type = Symbol.getBuiltinType();
  switch (Type) {
  case PDB_BuiltinType::Float:
    if (Symbol.getLength() == 4)
      return "float";
    return "double";
  case PDB_BuiltinType::UInt:
    switch (Symbol.getLength()) {
    case 8:
      return "unsigned __int64";
    case 4:
      return "unsigned int";
    case 2:
      return "unsigned short";
    case 1:
      return "unsigned char";
    default:
      return "unsigned";
    }
  case PDB_BuiltinType::Int:
    switch (Symbol.getLength()) {
    case 8:
      return "__int64";
    case 4:
      return "int";
    case 2:
      return "short";
    case 1:
      return "char";
    default:
      return "int";
    }
  case PDB_BuiltinType::Char:
    return "char";
  case PDB_BuiltinType::WCharT:
    return "wchar_t";
  case PDB_BuiltinType::Void:
    return "void";
  case PDB_BuiltinType::Long:
    return "long";
  case PDB_BuiltinType::ULong:
    return "unsigned long";
  case PDB_BuiltinType::Bool:
    return "bool";
  case PDB_BuiltinType::Currency:
    return "CURRENCY";
  case PDB_BuiltinType::Date:
    return "DATE";
  case PDB_BuiltinType::Variant:
    return "VARIANT";
  case PDB_BuiltinType::Complex:
    return "complex";
  case PDB_BuiltinType::Bitfield:
    return "bitfield";
  case PDB_BuiltinType::BSTR:
    return "BSTR";
  case PDB_BuiltinType::HResult:
    return "HRESULT";
  case PDB_BuiltinType::BCD:
    return "HRESULT";
  case PDB_BuiltinType::Char16:
    return "char16_t";
  case PDB_BuiltinType::Char32:
    return "char32_t";
  case PDB_BuiltinType::Char8:
    return "char8_t";
  case PDB_BuiltinType::None:
    return "...";
  }
  llvm_unreachable("Unknown PDB_BuiltinType");
}
