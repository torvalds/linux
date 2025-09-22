//===-- WindowsResourceDumper.cpp - Windows Resource printer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Windows resource (.res) dumper for llvm-readobj.
//
//===----------------------------------------------------------------------===//

#include "WindowsResourceDumper.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ScopedPrinter.h"

namespace llvm {
namespace object {
namespace WindowsRes {

std::string stripUTF16(const ArrayRef<UTF16> &UTF16Str) {
  std::string Result;
  Result.reserve(UTF16Str.size());

  for (UTF16 Ch : UTF16Str) {
    // UTF16Str will have swapped byte order in case of big-endian machines.
    // Swap it back in such a case.
    uint16_t ChValue = support::endian::byte_swap(Ch, llvm::endianness::little);
    if (ChValue <= 0xFF)
      Result += ChValue;
    else
      Result += '?';
  }
  return Result;
}

Error Dumper::printData() {
  auto EntryPtrOrErr = WinRes->getHeadEntry();
  if (!EntryPtrOrErr)
    return EntryPtrOrErr.takeError();
  auto EntryPtr = *EntryPtrOrErr;

  bool IsEnd = false;
  while (!IsEnd) {
    printEntry(EntryPtr);

    if (auto Err = EntryPtr.moveNext(IsEnd))
      return Err;
  }
  return Error::success();
}

void Dumper::printEntry(const ResourceEntryRef &Ref) {
  if (Ref.checkTypeString()) {
    auto NarrowStr = stripUTF16(Ref.getTypeString());
    SW.printString("Resource type (string)", NarrowStr);
  } else {
    SmallString<20> IDStr;
    raw_svector_ostream OS(IDStr);
    printResourceTypeName(Ref.getTypeID(), OS);
    SW.printString("Resource type (int)", IDStr);
  }

  if (Ref.checkNameString()) {
    auto NarrowStr = stripUTF16(Ref.getNameString());
    SW.printString("Resource name (string)", NarrowStr);
  } else
    SW.printNumber("Resource name (int)", Ref.getNameID());

  SW.printNumber("Data version", Ref.getDataVersion());
  SW.printHex("Memory flags", Ref.getMemoryFlags());
  SW.printNumber("Language ID", Ref.getLanguage());
  SW.printNumber("Version (major)", Ref.getMajorVersion());
  SW.printNumber("Version (minor)", Ref.getMinorVersion());
  SW.printNumber("Characteristics", Ref.getCharacteristics());
  SW.printNumber("Data size", (uint64_t)Ref.getData().size());
  SW.printBinary("Data:", Ref.getData());
  SW.startLine() << "\n";
}

} // namespace WindowsRes
} // namespace object
} // namespace llvm
