//===- TapiUniversal.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Text-based Dynamic Library Stub format.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/TapiUniversal.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/TapiFile.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/TextAPIReader.h"

using namespace llvm;
using namespace MachO;
using namespace object;

TapiUniversal::TapiUniversal(MemoryBufferRef Source, Error &Err)
    : Binary(ID_TapiUniversal, Source) {
  Expected<std::unique_ptr<InterfaceFile>> Result = TextAPIReader::get(Source);
  ErrorAsOutParameter ErrAsOuParam(&Err);
  if (!Result) {
    Err = Result.takeError();
    return;
  }
  ParsedFile = std::move(Result.get());

  auto FlattenObjectInfo = [this](const auto &File) {
    StringRef Name = File->getInstallName();
    for (const Architecture Arch : File->getArchitectures())
      Libraries.emplace_back(Library({Name, Arch}));
  };

  FlattenObjectInfo(ParsedFile);
  // Get inlined documents from tapi file.
  for (const std::shared_ptr<InterfaceFile> &File : ParsedFile->documents())
    FlattenObjectInfo(File);
}

TapiUniversal::~TapiUniversal() = default;

Expected<std::unique_ptr<TapiFile>>
TapiUniversal::ObjectForArch::getAsObjectFile() const {
  return std::make_unique<TapiFile>(Parent->getMemoryBufferRef(),
                                    *Parent->ParsedFile,
                                    Parent->Libraries[Index].Arch);
}

Expected<std::unique_ptr<TapiUniversal>>
TapiUniversal::create(MemoryBufferRef Source) {
  Error Err = Error::success();
  std::unique_ptr<TapiUniversal> Ret(new TapiUniversal(Source, Err));
  if (Err)
    return std::move(Err);
  return std::move(Ret);
}
