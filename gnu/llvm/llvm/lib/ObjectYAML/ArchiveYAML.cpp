//===- ArchiveYAML.cpp - ELF YAMLIO implementation -------------------- ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of archives.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/ArchiveYAML.h"

namespace llvm {

namespace yaml {

void MappingTraits<ArchYAML::Archive>::mapping(IO &IO, ArchYAML::Archive &A) {
  assert(!IO.getContext() && "The IO context is initialized already");
  IO.setContext(&A);
  IO.mapTag("!Arch", true);
  IO.mapOptional("Magic", A.Magic, "!<arch>\n");
  IO.mapOptional("Members", A.Members);
  IO.mapOptional("Content", A.Content);
  IO.setContext(nullptr);
}

std::string MappingTraits<ArchYAML::Archive>::validate(IO &,
                                                       ArchYAML::Archive &A) {
  if (A.Members && A.Content)
    return "\"Content\" and \"Members\" cannot be used together";
  return "";
}

void MappingTraits<ArchYAML::Archive::Child>::mapping(
    IO &IO, ArchYAML::Archive::Child &E) {
  assert(IO.getContext() && "The IO context is not initialized");
  for (auto &P : E.Fields)
    IO.mapOptional(P.first.data(), P.second.Value, P.second.DefaultValue);
  IO.mapOptional("Content", E.Content);
  IO.mapOptional("PaddingByte", E.PaddingByte);
}

std::string
MappingTraits<ArchYAML::Archive::Child>::validate(IO &,
                                                  ArchYAML::Archive::Child &C) {
  for (auto &P : C.Fields)
    if (P.second.Value.size() > P.second.MaxLength)
      return ("the maximum length of \"" + P.first + "\" field is " +
              Twine(P.second.MaxLength))
          .str();
  return "";
}

} // end namespace yaml

} // end namespace llvm
