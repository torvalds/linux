//===- ArchiveWriter.h - ar archive file format writer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the writeArchive function for writing an archive file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ARCHIVEWRITER_H
#define LLVM_OBJECT_ARCHIVEWRITER_H

#include "llvm/Object/Archive.h"

namespace llvm {

struct NewArchiveMember {
  std::unique_ptr<MemoryBuffer> Buf;
  StringRef MemberName;
  sys::TimePoint<std::chrono::seconds> ModTime;
  unsigned UID = 0, GID = 0, Perms = 0644;

  NewArchiveMember() = default;
  NewArchiveMember(MemoryBufferRef BufRef);

  // Detect the archive format from the object or bitcode file. This helps
  // assume the archive format when creating or editing archives in the case
  // one isn't explicitly set.
  object::Archive::Kind detectKindFromObject() const;

  static Expected<NewArchiveMember>
  getOldMember(const object::Archive::Child &OldMember, bool Deterministic);

  static Expected<NewArchiveMember> getFile(StringRef FileName,
                                            bool Deterministic);
};

Expected<std::string> computeArchiveRelativePath(StringRef From, StringRef To);

enum class SymtabWritingMode {
  NoSymtab,     // Do not write symbol table.
  NormalSymtab, // Write symbol table. For the Big Archive format, write both
                // 32-bit and 64-bit symbol tables.
  BigArchive32, // Only write the 32-bit symbol table.
  BigArchive64  // Only write the 64-bit symbol table.
};

void warnToStderr(Error Err);

// Write an archive directly to an output stream.
Error writeArchiveToStream(raw_ostream &Out,
                           ArrayRef<NewArchiveMember> NewMembers,
                           SymtabWritingMode WriteSymtab,
                           object::Archive::Kind Kind, bool Deterministic,
                           bool Thin, std::optional<bool> IsEC = std::nullopt,
                           function_ref<void(Error)> Warn = warnToStderr);

Error writeArchive(StringRef ArcName, ArrayRef<NewArchiveMember> NewMembers,
                   SymtabWritingMode WriteSymtab, object::Archive::Kind Kind,
                   bool Deterministic, bool Thin,
                   std::unique_ptr<MemoryBuffer> OldArchiveBuf = nullptr,
                   std::optional<bool> IsEC = std::nullopt,
                   function_ref<void(Error)> Warn = warnToStderr);

// writeArchiveToBuffer is similar to writeArchive but returns the Archive in a
// buffer instead of writing it out to a file.
Expected<std::unique_ptr<MemoryBuffer>>
writeArchiveToBuffer(ArrayRef<NewArchiveMember> NewMembers,
                     SymtabWritingMode WriteSymtab, object::Archive::Kind Kind,
                     bool Deterministic, bool Thin,
                     function_ref<void(Error)> Warn = warnToStderr);
}

#endif
