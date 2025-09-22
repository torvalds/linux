//===- llvm/Object/BuildID.cpp - Build ID ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines a library for handling Build IDs and using them to find
/// debug info.
///
//===----------------------------------------------------------------------===//

#include "llvm/Object/BuildID.h"

#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace llvm::object;

namespace {

template <typename ELFT> BuildIDRef getBuildID(const ELFFile<ELFT> &Obj) {
  auto PhdrsOrErr = Obj.program_headers();
  if (!PhdrsOrErr) {
    consumeError(PhdrsOrErr.takeError());
    return {};
  }
  for (const auto &P : *PhdrsOrErr) {
    if (P.p_type != ELF::PT_NOTE)
      continue;
    Error Err = Error::success();
    for (auto N : Obj.notes(P, Err))
      if (N.getType() == ELF::NT_GNU_BUILD_ID &&
          N.getName() == ELF::ELF_NOTE_GNU)
        return N.getDesc(P.p_align);
    consumeError(std::move(Err));
  }
  return {};
}

} // namespace

BuildID llvm::object::parseBuildID(StringRef Str) {
  std::string Bytes;
  if (!tryGetFromHex(Str, Bytes))
    return {};
  ArrayRef<uint8_t> BuildID(reinterpret_cast<const uint8_t *>(Bytes.data()),
                            Bytes.size());
  return SmallVector<uint8_t>(BuildID.begin(), BuildID.end());
}

BuildIDRef llvm::object::getBuildID(const ObjectFile *Obj) {
  if (auto *O = dyn_cast<ELFObjectFile<ELF32LE>>(Obj))
    return ::getBuildID(O->getELFFile());
  if (auto *O = dyn_cast<ELFObjectFile<ELF32BE>>(Obj))
    return ::getBuildID(O->getELFFile());
  if (auto *O = dyn_cast<ELFObjectFile<ELF64LE>>(Obj))
    return ::getBuildID(O->getELFFile());
  if (auto *O = dyn_cast<ELFObjectFile<ELF64BE>>(Obj))
    return ::getBuildID(O->getELFFile());
  return std::nullopt;
}

std::optional<std::string> BuildIDFetcher::fetch(BuildIDRef BuildID) const {
  auto GetDebugPath = [&](StringRef Directory) {
    SmallString<128> Path{Directory};
    sys::path::append(Path, ".build-id",
                      llvm::toHex(BuildID[0], /*LowerCase=*/true),
                      llvm::toHex(BuildID.slice(1), /*LowerCase=*/true));
    Path += ".debug";
    return Path;
  };
  if (DebugFileDirectories.empty()) {
    SmallString<128> Path = GetDebugPath(
#if defined(__NetBSD__)
        // Try /usr/libdata/debug/.build-id/../...
        "/usr/libdata/debug"
#else
        // Try /usr/lib/debug/.build-id/../...
        "/usr/lib/debug"
#endif
    );
    if (llvm::sys::fs::exists(Path))
      return std::string(Path);
  } else {
    for (const auto &Directory : DebugFileDirectories) {
      // Try <debug-file-directory>/.build-id/../...
      SmallString<128> Path = GetDebugPath(Directory);
      if (llvm::sys::fs::exists(Path))
        return std::string(Path);
    }
  }
  return std::nullopt;
}
