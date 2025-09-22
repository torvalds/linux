//===-- YAMLRemarkSerializer.h - YAML Remark serialization ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an interface for serializing remarks to YAML.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_YAMLREMARKSERIALIZER_H
#define LLVM_REMARKS_YAMLREMARKSERIALIZER_H

#include "llvm/Remarks/RemarkSerializer.h"
#include "llvm/Support/YAMLTraits.h"
#include <optional>

namespace llvm {
namespace remarks {

/// Serialize the remarks to YAML. One remark entry looks like this:
/// --- !<TYPE>
/// Pass:            <PASSNAME>
/// Name:            <REMARKNAME>
/// DebugLoc:        { File: <SOURCEFILENAME>, Line: <SOURCELINE>,
///                    Column: <SOURCECOLUMN> }
/// Function:        <FUNCTIONNAME>
/// Args:
///   - <KEY>: <VALUE>
///     DebugLoc:        { File: <FILE>, Line: <LINE>, Column: <COL> }
/// ...
struct YAMLRemarkSerializer : public RemarkSerializer {
  /// The YAML streamer.
  yaml::Output YAMLOutput;

  YAMLRemarkSerializer(raw_ostream &OS, SerializerMode Mode,
                       std::optional<StringTable> StrTab = std::nullopt);

  void emit(const Remark &Remark) override;
  std::unique_ptr<MetaSerializer> metaSerializer(
      raw_ostream &OS,
      std::optional<StringRef> ExternalFilename = std::nullopt) override;

  static bool classof(const RemarkSerializer *S) {
    return S->SerializerFormat == Format::YAML;
  }

protected:
  YAMLRemarkSerializer(Format SerializerFormat, raw_ostream &OS,
                       SerializerMode Mode,
                       std::optional<StringTable> StrTab = std::nullopt);
};

struct YAMLMetaSerializer : public MetaSerializer {
  std::optional<StringRef> ExternalFilename;

  YAMLMetaSerializer(raw_ostream &OS, std::optional<StringRef> ExternalFilename)
      : MetaSerializer(OS), ExternalFilename(ExternalFilename) {}

  void emit() override;
};

/// Serialize the remarks to YAML using a string table. An remark entry looks
/// like the regular YAML remark but instead of string entries it's using
/// numbers that map to an index in the string table.
struct YAMLStrTabRemarkSerializer : public YAMLRemarkSerializer {
  /// Wether we already emitted the metadata in standalone mode.
  /// This should be set to true after the first invocation of `emit`.
  bool DidEmitMeta = false;

  YAMLStrTabRemarkSerializer(raw_ostream &OS, SerializerMode Mode)
      : YAMLRemarkSerializer(Format::YAMLStrTab, OS, Mode) {
    // We always need a string table for this type of serializer.
    StrTab.emplace();
  }
  YAMLStrTabRemarkSerializer(raw_ostream &OS, SerializerMode Mode,
                             StringTable StrTab)
      : YAMLRemarkSerializer(Format::YAMLStrTab, OS, Mode, std::move(StrTab)) {}

  /// Override to emit the metadata if necessary.
  void emit(const Remark &Remark) override;

  std::unique_ptr<MetaSerializer> metaSerializer(
      raw_ostream &OS,
      std::optional<StringRef> ExternalFilename = std::nullopt) override;

  static bool classof(const RemarkSerializer *S) {
    return S->SerializerFormat == Format::YAMLStrTab;
  }
};

struct YAMLStrTabMetaSerializer : public YAMLMetaSerializer {
  /// The string table is part of the metadata.
  const StringTable &StrTab;

  YAMLStrTabMetaSerializer(raw_ostream &OS,
                           std::optional<StringRef> ExternalFilename,
                           const StringTable &StrTab)
      : YAMLMetaSerializer(OS, ExternalFilename), StrTab(StrTab) {}

  void emit() override;
};

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_YAMLREMARKSERIALIZER_H
