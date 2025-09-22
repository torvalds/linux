//===- ArchiveYAML.h - Archive YAMLIO implementation ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation of archives.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_ARCHIVEYAML_H
#define LLVM_OBJECTYAML_ARCHIVEYAML_H

#include "llvm/Support/YAMLTraits.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/ADT/MapVector.h"
#include <optional>

namespace llvm {
namespace ArchYAML {

struct Archive {
  struct Child {
    struct Field {
      Field() = default;
      Field(StringRef Default, unsigned Length)
          : DefaultValue(Default), MaxLength(Length) {}
      StringRef Value;
      StringRef DefaultValue;
      unsigned MaxLength;
    };

    Child() {
      Fields["Name"] = {"", 16};
      Fields["LastModified"] = {"0", 12};
      Fields["UID"] = {"0", 6};
      Fields["GID"] = {"0", 6};
      Fields["AccessMode"] = {"0", 8};
      Fields["Size"] = {"0", 10};
      Fields["Terminator"] = {"`\n", 2};
    }

    MapVector<StringRef, Field> Fields;

    std::optional<yaml::BinaryRef> Content;
    std::optional<llvm::yaml::Hex8> PaddingByte;
  };

  StringRef Magic;
  std::optional<std::vector<Child>> Members;
  std::optional<yaml::BinaryRef> Content;
};

} // end namespace ArchYAML
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ArchYAML::Archive::Child)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<ArchYAML::Archive> {
  static void mapping(IO &IO, ArchYAML::Archive &A);
  static std::string validate(IO &, ArchYAML::Archive &A);
};

template <> struct MappingTraits<ArchYAML::Archive::Child> {
  static void mapping(IO &IO, ArchYAML::Archive::Child &C);
  static std::string validate(IO &, ArchYAML::Archive::Child &C);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_ARCHIVEYAML_H
