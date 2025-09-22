//===-- TestModuleFileExtension.cpp - Module Extension Tester -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "TestModuleFileExtension.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
using namespace clang;
using namespace clang::serialization;

char TestModuleFileExtension::ID = 0;

TestModuleFileExtension::Writer::~Writer() { }

void TestModuleFileExtension::Writer::writeExtensionContents(
       Sema &SemaRef,
       llvm::BitstreamWriter &Stream) {
  using namespace llvm;

  // Write an abbreviation for this record.
  auto Abv = std::make_shared<llvm::BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(FIRST_EXTENSION_RECORD_ID));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # of characters
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));   // message
  auto Abbrev = Stream.EmitAbbrev(std::move(Abv));

  // Write a message into the extension block.
  SmallString<64> Message;
  {
    auto Ext = static_cast<TestModuleFileExtension *>(getExtension());
    raw_svector_ostream OS(Message);
    OS << "Hello from " << Ext->BlockName << " v" << Ext->MajorVersion << "."
       << Ext->MinorVersion;
  }
  uint64_t Record[] = {FIRST_EXTENSION_RECORD_ID, Message.size()};
  Stream.EmitRecordWithBlob(Abbrev, Record, Message);
}

TestModuleFileExtension::Reader::Reader(ModuleFileExtension *Ext,
                                        const llvm::BitstreamCursor &InStream)
  : ModuleFileExtensionReader(Ext), Stream(InStream)
{
  // Read the extension block.
  SmallVector<uint64_t, 4> Record;
  while (true) {
    llvm::Expected<llvm::BitstreamEntry> MaybeEntry =
        Stream.advanceSkippingSubblocks();
    if (!MaybeEntry)
      (void)MaybeEntry.takeError();
    llvm::BitstreamEntry Entry = MaybeEntry.get();

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock:
    case llvm::BitstreamEntry::EndBlock:
    case llvm::BitstreamEntry::Error:
      return;

    case llvm::BitstreamEntry::Record:
      break;
    }

    Record.clear();
    StringRef Blob;
    Expected<unsigned> MaybeRecCode =
        Stream.readRecord(Entry.ID, Record, &Blob);
    if (!MaybeRecCode)
      fprintf(stderr, "Failed reading rec code: %s\n",
              toString(MaybeRecCode.takeError()).c_str());
    switch (MaybeRecCode.get()) {
    case FIRST_EXTENSION_RECORD_ID: {
      StringRef Message = Blob.substr(0, Record[0]);
      fprintf(stderr, "Read extension block message: %s\n",
              Message.str().c_str());
      break;
    }
    }
  }
}

TestModuleFileExtension::Reader::~Reader() { }

TestModuleFileExtension::~TestModuleFileExtension() { }

ModuleFileExtensionMetadata
TestModuleFileExtension::getExtensionMetadata() const {
  return { BlockName, MajorVersion, MinorVersion, UserInfo };
}

void TestModuleFileExtension::hashExtension(
    ExtensionHashBuilder &HBuilder) const {
  if (Hashed) {
    HBuilder.add(BlockName);
    HBuilder.add(MajorVersion);
    HBuilder.add(MinorVersion);
    HBuilder.add(UserInfo);
  }
}

std::unique_ptr<ModuleFileExtensionWriter>
TestModuleFileExtension::createExtensionWriter(ASTWriter &) {
  return std::unique_ptr<ModuleFileExtensionWriter>(new Writer(this));
}

std::unique_ptr<ModuleFileExtensionReader>
TestModuleFileExtension::createExtensionReader(
  const ModuleFileExtensionMetadata &Metadata,
  ASTReader &Reader, serialization::ModuleFile &Mod,
  const llvm::BitstreamCursor &Stream)
{
  assert(Metadata.BlockName == BlockName && "Wrong block name");
  if (std::make_pair(Metadata.MajorVersion, Metadata.MinorVersion) !=
        std::make_pair(MajorVersion, MinorVersion)) {
    Reader.getDiags().Report(Mod.ImportLoc,
                             diag::err_test_module_file_extension_version)
      << BlockName << Metadata.MajorVersion << Metadata.MinorVersion
      << MajorVersion << MinorVersion;
    return nullptr;
  }

  return std::unique_ptr<ModuleFileExtensionReader>(
                                                    new TestModuleFileExtension::Reader(this, Stream));
}

std::string TestModuleFileExtension::str() const {
  std::string Buffer;
  llvm::raw_string_ostream OS(Buffer);
  OS << BlockName << ":" << MajorVersion << ":" << MinorVersion << ":" << Hashed
     << ":" << UserInfo;
  return Buffer;
}
