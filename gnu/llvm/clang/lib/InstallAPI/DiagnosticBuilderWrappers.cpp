//===- DiagnosticBuilderWrappers.cpp ----------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DiagnosticBuilderWrappers.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TextAPI/Platform.h"

using clang::DiagnosticBuilder;

namespace llvm {
namespace MachO {
const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    const Architecture &Arch) {
  DB.AddString(getArchitectureName(Arch));
  return DB;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    const ArchitectureSet &ArchSet) {
  DB.AddString(std::string(ArchSet));
  return DB;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    const PlatformType &Platform) {
  DB.AddString(getPlatformName(Platform));
  return DB;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    const PlatformVersionSet &Platforms) {
  std::string PlatformAsString;
  raw_string_ostream Stream(PlatformAsString);

  Stream << "[ ";
  llvm::interleaveComma(
      Platforms, Stream,
      [&Stream](const std::pair<PlatformType, VersionTuple> &PV) {
        Stream << getPlatformName(PV.first);
        if (!PV.second.empty())
          Stream << PV.second.getAsString();
      });
  Stream << " ]";
  DB.AddString(PlatformAsString);
  return DB;
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    const FileType &Type) {
  switch (Type) {
  case FileType::MachO_Bundle:
    DB.AddString("mach-o bundle");
    return DB;
  case FileType::MachO_DynamicLibrary:
    DB.AddString("mach-o dynamic library");
    return DB;
  case FileType::MachO_DynamicLibrary_Stub:
    DB.AddString("mach-o dynamic library stub");
    return DB;
  case FileType::TBD_V1:
    DB.AddString("tbd-v1");
    return DB;
  case FileType::TBD_V2:
    DB.AddString("tbd-v2");
    return DB;
  case FileType::TBD_V3:
    DB.AddString("tbd-v3");
    return DB;
  case FileType::TBD_V4:
    DB.AddString("tbd-v4");
    return DB;
  case FileType::TBD_V5:
    DB.AddString("tbd-v5");
    return DB;
  case FileType::Invalid:
  case FileType::All:
    break;
  }
  llvm_unreachable("Unexpected file type for diagnostics.");
}

const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                    const PackedVersion &Version) {
  std::string VersionString;
  raw_string_ostream OS(VersionString);
  OS << Version;
  DB.AddString(VersionString);
  return DB;
}

const clang::DiagnosticBuilder &
operator<<(const clang::DiagnosticBuilder &DB,
           const StringMapEntry<ArchitectureSet> &LibAttr) {
  std::string IFAsString;
  raw_string_ostream OS(IFAsString);

  OS << LibAttr.getKey() << " [ " << LibAttr.getValue() << " ]";
  DB.AddString(IFAsString);
  return DB;
}

} // namespace MachO
} // namespace llvm
