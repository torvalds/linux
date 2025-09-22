//=- CodeViewYAMLDebugSections.h - CodeView YAMLIO debug sections -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of CodeView
// Debug Info.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_CODEVIEWYAMLDEBUGSECTIONS_H
#define LLVM_OBJECTYAML_CODEVIEWYAMLDEBUGSECTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {

namespace codeview {

class StringsAndChecksums;
class StringsAndChecksumsRef;

} // end namespace codeview

namespace CodeViewYAML {

namespace detail {

struct YAMLSubsectionBase;

} // end namespace detail

struct YAMLFrameData {
  uint32_t RvaStart;
  uint32_t CodeSize;
  uint32_t LocalSize;
  uint32_t ParamsSize;
  uint32_t MaxStackSize;
  StringRef FrameFunc;
  uint32_t PrologSize;
  uint32_t SavedRegsSize;
  uint32_t Flags;
};

struct YAMLCrossModuleImport {
  StringRef ModuleName;
  std::vector<uint32_t> ImportIds;
};

struct SourceLineEntry {
  uint32_t Offset;
  uint32_t LineStart;
  uint32_t EndDelta;
  bool IsStatement;
};

struct SourceColumnEntry {
  uint16_t StartColumn;
  uint16_t EndColumn;
};

struct SourceLineBlock {
  StringRef FileName;
  std::vector<SourceLineEntry> Lines;
  std::vector<SourceColumnEntry> Columns;
};

struct HexFormattedString {
  std::vector<uint8_t> Bytes;
};

struct SourceFileChecksumEntry {
  StringRef FileName;
  codeview::FileChecksumKind Kind;
  HexFormattedString ChecksumBytes;
};

struct SourceLineInfo {
  uint32_t RelocOffset;
  uint32_t RelocSegment;
  codeview::LineFlags Flags;
  uint32_t CodeSize;
  std::vector<SourceLineBlock> Blocks;
};

struct InlineeSite {
  uint32_t Inlinee;
  StringRef FileName;
  uint32_t SourceLineNum;
  std::vector<StringRef> ExtraFiles;
};

struct InlineeInfo {
  bool HasExtraFiles;
  std::vector<InlineeSite> Sites;
};

struct YAMLDebugSubsection {
  static Expected<YAMLDebugSubsection>
  fromCodeViewSubection(const codeview::StringsAndChecksumsRef &SC,
                        const codeview::DebugSubsectionRecord &SS);

  std::shared_ptr<detail::YAMLSubsectionBase> Subsection;
};

Expected<std::vector<std::shared_ptr<codeview::DebugSubsection>>>
toCodeViewSubsectionList(BumpPtrAllocator &Allocator,
                         ArrayRef<YAMLDebugSubsection> Subsections,
                         const codeview::StringsAndChecksums &SC);

std::vector<YAMLDebugSubsection>
fromDebugS(ArrayRef<uint8_t> Data, const codeview::StringsAndChecksumsRef &SC);

void initializeStringsAndChecksums(ArrayRef<YAMLDebugSubsection> Sections,
                                   codeview::StringsAndChecksums &SC);

} // end namespace CodeViewYAML

} // end namespace llvm

LLVM_YAML_DECLARE_MAPPING_TRAITS(CodeViewYAML::YAMLDebugSubsection)

LLVM_YAML_IS_SEQUENCE_VECTOR(CodeViewYAML::YAMLDebugSubsection)

#endif // LLVM_OBJECTYAML_CODEVIEWYAMLDEBUGSECTIONS_H
