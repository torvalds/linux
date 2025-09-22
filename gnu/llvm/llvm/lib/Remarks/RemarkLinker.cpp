//===- RemarkLinker.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an implementation of the remark linker.
//
//===----------------------------------------------------------------------===//

#include "llvm/Remarks/RemarkLinker.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Remarks/RemarkParser.h"
#include "llvm/Remarks/RemarkSerializer.h"
#include "llvm/Support/Error.h"
#include <optional>

using namespace llvm;
using namespace llvm::remarks;

namespace llvm {
class raw_ostream;
}

static Expected<StringRef>
getRemarksSectionName(const object::ObjectFile &Obj) {
  if (Obj.isMachO())
    return StringRef("__remarks");
  // ELF -> .remarks, but there is no ELF support at this point.
  return createStringError(std::errc::illegal_byte_sequence,
                           "Unsupported file format.");
}

Expected<std::optional<StringRef>>
llvm::remarks::getRemarksSectionContents(const object::ObjectFile &Obj) {
  Expected<StringRef> SectionName = getRemarksSectionName(Obj);
  if (!SectionName)
    return SectionName.takeError();

  for (const object::SectionRef &Section : Obj.sections()) {
    Expected<StringRef> MaybeName = Section.getName();
    if (!MaybeName)
      return MaybeName.takeError();
    if (*MaybeName != *SectionName)
      continue;

    if (Expected<StringRef> Contents = Section.getContents())
      return *Contents;
    else
      return Contents.takeError();
  }
  return std::optional<StringRef>{};
}

Remark &RemarkLinker::keep(std::unique_ptr<Remark> Remark) {
  StrTab.internalize(*Remark);
  auto Inserted = Remarks.insert(std::move(Remark));
  return **Inserted.first;
}

void RemarkLinker::setExternalFilePrependPath(StringRef PrependPathIn) {
  PrependPath = std::string(PrependPathIn);
}

Error RemarkLinker::link(StringRef Buffer, std::optional<Format> RemarkFormat) {
  if (!RemarkFormat) {
    Expected<Format> ParserFormat = magicToFormat(Buffer);
    if (!ParserFormat)
      return ParserFormat.takeError();
    RemarkFormat = *ParserFormat;
  }

  Expected<std::unique_ptr<RemarkParser>> MaybeParser =
      createRemarkParserFromMeta(
          *RemarkFormat, Buffer, /*StrTab=*/std::nullopt,
          PrependPath ? std::optional<StringRef>(StringRef(*PrependPath))
                      : std::optional<StringRef>());
  if (!MaybeParser)
    return MaybeParser.takeError();

  RemarkParser &Parser = **MaybeParser;

  while (true) {
    Expected<std::unique_ptr<Remark>> Next = Parser.next();
    if (Error E = Next.takeError()) {
      if (E.isA<EndOfFileError>()) {
        consumeError(std::move(E));
        break;
      }
      return E;
    }

    assert(*Next != nullptr);

    if (shouldKeepRemark(**Next))
      keep(std::move(*Next));
  }
  return Error::success();
}

Error RemarkLinker::link(const object::ObjectFile &Obj,
                         std::optional<Format> RemarkFormat) {
  Expected<std::optional<StringRef>> SectionOrErr =
      getRemarksSectionContents(Obj);
  if (!SectionOrErr)
    return SectionOrErr.takeError();

  if (std::optional<StringRef> Section = *SectionOrErr)
    return link(*Section, RemarkFormat);
  return Error::success();
}

Error RemarkLinker::serialize(raw_ostream &OS, Format RemarksFormat) const {
  Expected<std::unique_ptr<RemarkSerializer>> MaybeSerializer =
      createRemarkSerializer(RemarksFormat, SerializerMode::Standalone, OS,
                             std::move(const_cast<StringTable &>(StrTab)));
  if (!MaybeSerializer)
    return MaybeSerializer.takeError();

  std::unique_ptr<remarks::RemarkSerializer> Serializer =
      std::move(*MaybeSerializer);

  for (const Remark &R : remarks())
    Serializer->emit(R);
  return Error::success();
}
