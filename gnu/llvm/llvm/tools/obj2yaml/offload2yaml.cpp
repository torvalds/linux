//===------ offload2yaml.cpp - obj2yaml conversion tool ---*- C++ -------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "obj2yaml.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/ObjectYAML/OffloadYAML.h"
#include "llvm/Support/StringSaver.h"

using namespace llvm;

namespace {

void populateYAML(OffloadYAML::Binary &YAMLBinary, object::OffloadBinary &OB,
                  UniqueStringSaver Saver) {
  YAMLBinary.Members.emplace_back();
  auto &Member = YAMLBinary.Members.back();
  Member.ImageKind = OB.getImageKind();
  Member.OffloadKind = OB.getOffloadKind();
  Member.Flags = OB.getFlags();
  if (!OB.strings().empty()) {
    Member.StringEntries = std::vector<OffloadYAML::Binary::StringEntry>();
    for (const auto &Entry : OB.strings())
      Member.StringEntries->emplace_back(OffloadYAML::Binary::StringEntry(
          {Saver.save(Entry.first), Saver.save(Entry.second)}));
  }

  if (!OB.getImage().empty())
    Member.Content = arrayRefFromStringRef(OB.getImage());
}

Expected<OffloadYAML::Binary *> dump(MemoryBufferRef Source,
                                     UniqueStringSaver Saver) {
  Expected<std::unique_ptr<object::OffloadBinary>> OB =
      object::OffloadBinary::create(Source);
  if (!OB)
    return OB.takeError();

  std::unique_ptr<OffloadYAML::Binary> YAMLBinary =
      std::make_unique<OffloadYAML::Binary>();

  YAMLBinary->Members = std::vector<OffloadYAML::Binary::Member>();

  uint64_t Offset = 0;
  while (Offset < (*OB)->getMemoryBufferRef().getBufferSize()) {
    MemoryBufferRef Buffer = MemoryBufferRef(
        (*OB)->getData().drop_front(Offset), (*OB)->getFileName());
    auto BinaryOrErr = object::OffloadBinary::create(Buffer);
    if (!BinaryOrErr)
      return BinaryOrErr.takeError();

    object::OffloadBinary &Binary = **BinaryOrErr;

    populateYAML(*YAMLBinary, Binary, Saver);

    Offset += Binary.getSize();
  }

  return YAMLBinary.release();
}

} // namespace

Error offload2yaml(raw_ostream &Out, MemoryBufferRef Source) {
  BumpPtrAllocator Alloc;
  UniqueStringSaver Saver(Alloc);

  Expected<OffloadYAML::Binary *> YAMLOrErr = dump(Source, Saver);
  if (!YAMLOrErr)
    return YAMLOrErr.takeError();

  std::unique_ptr<OffloadYAML::Binary> YAML(YAMLOrErr.get());
  yaml::Output Yout(Out);
  Yout << *YAML;

  return Error::success();
}
