//===-- ObjDumper.cpp - Base dumper class -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements ObjDumper.
///
//===----------------------------------------------------------------------===//

#include "ObjDumper.h"
#include "llvm-readobj.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Decompressor.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/SystemZ/zOSSupport.h"
#include "llvm/Support/raw_ostream.h"
#include <map>

namespace llvm {

static inline Error createError(const Twine &Msg) {
  return createStringError(object::object_error::parse_failed, Msg);
}

ObjDumper::ObjDumper(ScopedPrinter &Writer, StringRef ObjName) : W(Writer) {
  // Dumper reports all non-critical errors as warnings.
  // It does not print the same warning more than once.
  WarningHandler = [=](const Twine &Msg) {
    if (Warnings.insert(Msg.str()).second)
      reportWarning(createError(Msg), ObjName);
    return Error::success();
  };
}

ObjDumper::~ObjDumper() {}

void ObjDumper::reportUniqueWarning(Error Err) const {
  reportUniqueWarning(toString(std::move(Err)));
}

void ObjDumper::reportUniqueWarning(const Twine &Msg) const {
  cantFail(WarningHandler(Msg),
           "WarningHandler should always return ErrorSuccess");
}

static void printAsPrintable(raw_ostream &W, const uint8_t *Start, size_t Len) {
  for (size_t i = 0; i < Len; i++)
    W << (isPrint(Start[i]) ? static_cast<char>(Start[i]) : '.');
}

void ObjDumper::printAsStringList(StringRef StringContent,
                                  size_t StringDataOffset) {
  size_t StrSize = StringContent.size();
  if (StrSize == 0)
    return;
  if (StrSize < StringDataOffset) {
    reportUniqueWarning("offset (0x" + Twine::utohexstr(StringDataOffset) +
                        ") is past the end of the contents (size 0x" +
                        Twine::utohexstr(StrSize) + ")");
    return;
  }

  const uint8_t *StrContent = StringContent.bytes_begin();
  // Some formats contain additional metadata at the start which should not be
  // interpreted as strings. Skip these bytes, but account for them in the
  // string offsets.
  const uint8_t *CurrentWord = StrContent + StringDataOffset;
  const uint8_t *StrEnd = StringContent.bytes_end();

  while (CurrentWord <= StrEnd) {
    size_t WordSize = strnlen(reinterpret_cast<const char *>(CurrentWord),
                              StrEnd - CurrentWord);
    if (!WordSize) {
      CurrentWord++;
      continue;
    }
    W.startLine() << format("[%6tx] ", CurrentWord - StrContent);
    printAsPrintable(W.startLine(), CurrentWord, WordSize);
    W.startLine() << '\n';
    CurrentWord += WordSize + 1;
  }
}

void ObjDumper::printFileSummary(StringRef FileStr, object::ObjectFile &Obj,
                                 ArrayRef<std::string> InputFilenames,
                                 const object::Archive *A) {
  W.startLine() << "\n";
  W.printString("File", FileStr);
  W.printString("Format", Obj.getFileFormatName());
  W.printString("Arch", Triple::getArchTypeName(Obj.getArch()));
  W.printString("AddressSize",
                std::string(formatv("{0}bit", 8 * Obj.getBytesInAddress())));
  this->printLoadName();
}

static std::vector<object::SectionRef>
getSectionRefsByNameOrIndex(const object::ObjectFile &Obj,
                            ArrayRef<std::string> Sections) {
  std::vector<object::SectionRef> Ret;
  std::map<std::string, bool> SecNames;
  std::map<unsigned, bool> SecIndices;
  unsigned SecIndex;
  for (StringRef Section : Sections) {
    if (!Section.getAsInteger(0, SecIndex))
      SecIndices.emplace(SecIndex, false);
    else
      SecNames.emplace(std::string(Section), false);
  }

  SecIndex = Obj.isELF() ? 0 : 1;
  for (object::SectionRef SecRef : Obj.sections()) {
    StringRef SecName = unwrapOrError(Obj.getFileName(), SecRef.getName());
    auto NameIt = SecNames.find(std::string(SecName));
    if (NameIt != SecNames.end())
      NameIt->second = true;
    auto IndexIt = SecIndices.find(SecIndex);
    if (IndexIt != SecIndices.end())
      IndexIt->second = true;
    if (NameIt != SecNames.end() || IndexIt != SecIndices.end())
      Ret.push_back(SecRef);
    SecIndex++;
  }

  for (const std::pair<const std::string, bool> &S : SecNames)
    if (!S.second)
      reportWarning(
          createError(formatv("could not find section '{0}'", S.first).str()),
          Obj.getFileName());

  for (std::pair<unsigned, bool> S : SecIndices)
    if (!S.second)
      reportWarning(
          createError(formatv("could not find section {0}", S.first).str()),
          Obj.getFileName());

  return Ret;
}

static void maybeDecompress(const object::ObjectFile &Obj,
                            StringRef SectionName, StringRef &SectionContent,
                            SmallString<0> &Out) {
  Expected<object::Decompressor> Decompressor = object::Decompressor::create(
      SectionName, SectionContent, Obj.isLittleEndian(), Obj.is64Bit());
  if (!Decompressor)
    reportWarning(Decompressor.takeError(), Obj.getFileName());
  else if (auto Err = Decompressor->resizeAndDecompress(Out))
    reportWarning(std::move(Err), Obj.getFileName());
  else
    SectionContent = Out;
}

void ObjDumper::printSectionsAsString(const object::ObjectFile &Obj,
                                      ArrayRef<std::string> Sections,
                                      bool Decompress) {
  SmallString<0> Out;
  for (object::SectionRef Section :
       getSectionRefsByNameOrIndex(Obj, Sections)) {
    StringRef SectionName = unwrapOrError(Obj.getFileName(), Section.getName());
    W.startLine() << "\nString dump of section '" << SectionName << "':\n";

    StringRef SectionContent =
        unwrapOrError(Obj.getFileName(), Section.getContents());
    if (Decompress && Section.isCompressed())
      maybeDecompress(Obj, SectionName, SectionContent, Out);
    printAsStringList(SectionContent);
  }
}

void ObjDumper::printSectionsAsHex(const object::ObjectFile &Obj,
                                   ArrayRef<std::string> Sections,
                                   bool Decompress) {
  SmallString<0> Out;
  for (object::SectionRef Section :
       getSectionRefsByNameOrIndex(Obj, Sections)) {
    StringRef SectionName = unwrapOrError(Obj.getFileName(), Section.getName());
    W.startLine() << "\nHex dump of section '" << SectionName << "':\n";

    StringRef SectionContent =
        unwrapOrError(Obj.getFileName(), Section.getContents());
    if (Decompress && Section.isCompressed())
      maybeDecompress(Obj, SectionName, SectionContent, Out);
    const uint8_t *SecContent = SectionContent.bytes_begin();
    const uint8_t *SecEnd = SecContent + SectionContent.size();

    for (const uint8_t *SecPtr = SecContent; SecPtr < SecEnd; SecPtr += 16) {
      const uint8_t *TmpSecPtr = SecPtr;
      uint8_t i;
      uint8_t k;

      W.startLine() << format_hex(Section.getAddress() + (SecPtr - SecContent),
                                  10);
      W.startLine() << ' ';
      for (i = 0; TmpSecPtr < SecEnd && i < 4; ++i) {
        for (k = 0; TmpSecPtr < SecEnd && k < 4; k++, TmpSecPtr++) {
          uint8_t Val = *(reinterpret_cast<const uint8_t *>(TmpSecPtr));
          W.startLine() << format_hex_no_prefix(Val, 2);
        }
        W.startLine() << ' ';
      }

      // We need to print the correct amount of spaces to match the format.
      // We are adding the (4 - i) last rows that are 8 characters each.
      // Then, the (4 - i) spaces that are in between the rows.
      // Least, if we cut in a middle of a row, we add the remaining characters,
      // which is (8 - (k * 2)).
      if (i < 4)
        W.startLine() << format("%*c", (4 - i) * 8 + (4 - i), ' ');
      if (k < 4)
        W.startLine() << format("%*c", 8 - k * 2, ' ');

      TmpSecPtr = SecPtr;
      for (i = 0; TmpSecPtr + i < SecEnd && i < 16; ++i)
        W.startLine() << (isPrint(TmpSecPtr[i])
                              ? static_cast<char>(TmpSecPtr[i])
                              : '.');

      W.startLine() << '\n';
    }
  }
}

} // namespace llvm
