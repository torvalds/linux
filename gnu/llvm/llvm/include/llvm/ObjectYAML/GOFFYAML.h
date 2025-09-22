//===- GOFFYAML.h - GOFF YAMLIO implementation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares classes for handling the YAML representation of GOFF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_GOFFYAML_H
#define LLVM_OBJECTYAML_GOFFYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/ObjectYAML/YAML.h"
#include <cstdint>
#include <vector>

namespace llvm {

// The structure of the yaml files is not an exact 1:1 match to GOFF. In order
// to use yaml::IO, we use these structures which are closer to the source.
namespace GOFFYAML {

struct FileHeader {
  uint32_t TargetEnvironment = 0;
  uint32_t TargetOperatingSystem = 0;
  uint16_t CCSID = 0;
  StringRef CharacterSetName;
  StringRef LanguageProductIdentifier;
  uint32_t ArchitectureLevel = 0;
  std::optional<uint16_t> InternalCCSID;
  std::optional<uint8_t> TargetSoftwareEnvironment;
};

struct Object {
  FileHeader Header;
  Object();
};
} // end namespace GOFFYAML
} // end namespace llvm

LLVM_YAML_DECLARE_MAPPING_TRAITS(GOFFYAML::FileHeader)
LLVM_YAML_DECLARE_MAPPING_TRAITS(GOFFYAML::Object)

#endif // LLVM_OBJECTYAML_GOFFYAML_H
