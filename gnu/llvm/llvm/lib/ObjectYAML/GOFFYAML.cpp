//===-- GOFFYAML.cpp - GOFF YAMLIO implementation ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of GOFF.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/GOFFYAML.h"
#include "llvm/BinaryFormat/GOFF.h"
#include <string.h>

namespace llvm {
namespace GOFFYAML {

Object::Object() {}

} // namespace GOFFYAML

namespace yaml {

void MappingTraits<GOFFYAML::FileHeader>::mapping(
    IO &IO, GOFFYAML::FileHeader &FileHdr) {
  IO.mapOptional("TargetEnvironment", FileHdr.TargetEnvironment, 0);
  IO.mapOptional("TargetOperatingSystem", FileHdr.TargetOperatingSystem, 0);
  IO.mapOptional("CCSID", FileHdr.CCSID, 0);
  IO.mapOptional("CharacterSetName", FileHdr.CharacterSetName, "");
  IO.mapOptional("LanguageProductIdentifier", FileHdr.LanguageProductIdentifier,
                 "");
  IO.mapOptional("ArchitectureLevel", FileHdr.ArchitectureLevel, 1);
  IO.mapOptional("InternalCCSID", FileHdr.InternalCCSID);
  IO.mapOptional("TargetSoftwareEnvironment",
                 FileHdr.TargetSoftwareEnvironment);
}

void MappingTraits<GOFFYAML::Object>::mapping(IO &IO, GOFFYAML::Object &Obj) {
  IO.mapTag("!GOFF", true);
  IO.mapRequired("FileHeader", Obj.Header);
}

} // namespace yaml
} // namespace llvm
