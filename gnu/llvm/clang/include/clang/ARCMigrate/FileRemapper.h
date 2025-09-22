//===-- FileRemapper.h - File Remapping Helper ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ARCMIGRATE_FILEREMAPPER_H
#define LLVM_CLANG_ARCMIGRATE_FILEREMAPPER_H

#include "clang/Basic/FileEntry.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <variant>

namespace llvm {
  class MemoryBuffer;
  class MemoryBufferRef;
}

namespace clang {
  class FileManager;
  class DiagnosticsEngine;
  class PreprocessorOptions;

namespace arcmt {

class FileRemapper {
  // FIXME: Reuse the same FileManager for multiple ASTContexts.
  std::unique_ptr<FileManager> FileMgr;

  using Target = std::variant<FileEntryRef, llvm::MemoryBuffer *>;
  using MappingsTy = llvm::DenseMap<FileEntryRef, Target>;
  MappingsTy FromToMappings;

  llvm::DenseMap<const FileEntry *, FileEntryRef> ToFromMappings;

public:
  FileRemapper();
  ~FileRemapper();

  bool initFromDisk(StringRef outputDir, DiagnosticsEngine &Diag,
                    bool ignoreIfFilesChanged);
  bool initFromFile(StringRef filePath, DiagnosticsEngine &Diag,
                    bool ignoreIfFilesChanged);
  bool flushToDisk(StringRef outputDir, DiagnosticsEngine &Diag);
  bool flushToFile(StringRef outputPath, DiagnosticsEngine &Diag);

  bool overwriteOriginal(DiagnosticsEngine &Diag,
                         StringRef outputDir = StringRef());

  void remap(StringRef filePath, std::unique_ptr<llvm::MemoryBuffer> memBuf);

  void applyMappings(PreprocessorOptions &PPOpts) const;

  /// Iterate through all the mappings.
  void forEachMapping(
      llvm::function_ref<void(StringRef, StringRef)> CaptureFile,
      llvm::function_ref<void(StringRef, const llvm::MemoryBufferRef &)>
          CaptureBuffer) const;

  void clear(StringRef outputDir = StringRef());

private:
  void remap(FileEntryRef file, std::unique_ptr<llvm::MemoryBuffer> memBuf);
  void remap(FileEntryRef file, FileEntryRef newfile);

  OptionalFileEntryRef getOriginalFile(StringRef filePath);
  void resetTarget(Target &targ);

  bool report(const Twine &err, DiagnosticsEngine &Diag);

  std::string getRemapInfoFile(StringRef outputDir);
};

} // end namespace arcmt

}  // end namespace clang

#endif
