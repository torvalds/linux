//===-- COFFImportDumper.cpp - COFF import library dumper -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the COFF import library dumper for llvm-readobj.
///
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Support/ScopedPrinter.h"

using namespace llvm::object;

namespace llvm {

void dumpCOFFImportFile(const COFFImportFile *File, ScopedPrinter &Writer) {
  Writer.startLine() << '\n';
  Writer.printString("File", File->getFileName());
  Writer.printString("Format", File->getFileFormatName());

  const coff_import_header *H = File->getCOFFImportHeader();
  switch (H->getType()) {
  case COFF::IMPORT_CODE:  Writer.printString("Type", "code"); break;
  case COFF::IMPORT_DATA:  Writer.printString("Type", "data"); break;
  case COFF::IMPORT_CONST: Writer.printString("Type", "const"); break;
  }

  switch (H->getNameType()) {
  case COFF::IMPORT_ORDINAL:
    Writer.printString("Name type", "ordinal");
    break;
  case COFF::IMPORT_NAME:
    Writer.printString("Name type", "name");
    break;
  case COFF::IMPORT_NAME_NOPREFIX:
    Writer.printString("Name type", "noprefix");
    break;
  case COFF::IMPORT_NAME_UNDECORATE:
    Writer.printString("Name type", "undecorate");
    break;
  case COFF::IMPORT_NAME_EXPORTAS:
    Writer.printString("Name type", "export as");
    break;
  }

  if (H->getNameType() != COFF::IMPORT_ORDINAL)
    Writer.printString("Export name", File->getExportName());

  for (const object::BasicSymbolRef &Sym : File->symbols()) {
    raw_ostream &OS = Writer.startLine();
    OS << "Symbol: ";
    cantFail(Sym.printName(OS));
    OS << "\n";
  }
}

} // namespace llvm
