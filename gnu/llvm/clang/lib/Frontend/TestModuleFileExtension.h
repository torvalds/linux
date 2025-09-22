//===-- TestModuleFileExtension.h - Module Extension Tester -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_FRONTEND_TESTMODULEFILEEXTENSION_H
#define LLVM_CLANG_FRONTEND_TESTMODULEFILEEXTENSION_H

#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include <string>

namespace clang {

/// A module file extension used for testing purposes.
class TestModuleFileExtension
    : public llvm::RTTIExtends<TestModuleFileExtension, ModuleFileExtension> {
  std::string BlockName;
  unsigned MajorVersion;
  unsigned MinorVersion;
  bool Hashed;
  std::string UserInfo;

  class Writer : public ModuleFileExtensionWriter {
  public:
    Writer(ModuleFileExtension *Ext) : ModuleFileExtensionWriter(Ext) { }
    ~Writer() override;

    void writeExtensionContents(Sema &SemaRef,
                                llvm::BitstreamWriter &Stream) override;
  };

  class Reader : public ModuleFileExtensionReader {
    llvm::BitstreamCursor Stream;

  public:
    ~Reader() override;

    Reader(ModuleFileExtension *Ext, const llvm::BitstreamCursor &InStream);
  };

public:
  static char ID;

  TestModuleFileExtension(StringRef BlockName, unsigned MajorVersion,
                          unsigned MinorVersion, bool Hashed,
                          StringRef UserInfo)
      : BlockName(BlockName), MajorVersion(MajorVersion),
        MinorVersion(MinorVersion), Hashed(Hashed), UserInfo(UserInfo) {}
  ~TestModuleFileExtension() override;

  ModuleFileExtensionMetadata getExtensionMetadata() const override;

  void hashExtension(ExtensionHashBuilder &HBuilder) const override;

  std::unique_ptr<ModuleFileExtensionWriter>
  createExtensionWriter(ASTWriter &Writer) override;

  std::unique_ptr<ModuleFileExtensionReader>
  createExtensionReader(const ModuleFileExtensionMetadata &Metadata,
                        ASTReader &Reader, serialization::ModuleFile &Mod,
                        const llvm::BitstreamCursor &Stream) override;

  std::string str() const;
};

} // end namespace clang

#endif // LLVM_CLANG_FRONTEND_TESTMODULEFILEEXTENSION_H
