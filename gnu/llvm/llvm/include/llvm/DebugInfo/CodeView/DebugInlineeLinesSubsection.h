//===- DebugInlineeLinesSubsection.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGINLINEELINESSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGINLINEELINESSUBSECTION_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {

namespace codeview {

class DebugChecksumsSubsection;

enum class InlineeLinesSignature : uint32_t {
  Normal,    // CV_INLINEE_SOURCE_LINE_SIGNATURE
  ExtraFiles // CV_INLINEE_SOURCE_LINE_SIGNATURE_EX
};

struct InlineeSourceLineHeader {
  TypeIndex Inlinee;                  // ID of the function that was inlined.
  support::ulittle32_t FileID;        // Offset into FileChecksums subsection.
  support::ulittle32_t SourceLineNum; // First line of inlined code.
                                      // If extra files present:
                                      //   ulittle32_t ExtraFileCount;
                                      //   ulittle32_t Files[];
};

struct InlineeSourceLine {
  const InlineeSourceLineHeader *Header;
  FixedStreamArray<support::ulittle32_t> ExtraFiles;
};

} // end namespace codeview

template <> struct VarStreamArrayExtractor<codeview::InlineeSourceLine> {
  Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                   codeview::InlineeSourceLine &Item);

  bool HasExtraFiles = false;
};

namespace codeview {

class DebugInlineeLinesSubsectionRef final : public DebugSubsectionRef {
  using LinesArray = VarStreamArray<InlineeSourceLine>;
  using Iterator = LinesArray::Iterator;

public:
  DebugInlineeLinesSubsectionRef();

  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::InlineeLines;
  }

  Error initialize(BinaryStreamReader Reader);
  Error initialize(BinaryStreamRef Section) {
    return initialize(BinaryStreamReader(Section));
  }

  bool valid() const { return Lines.valid(); }
  bool hasExtraFiles() const;

  Iterator begin() const { return Lines.begin(); }
  Iterator end() const { return Lines.end(); }

private:
  InlineeLinesSignature Signature;
  LinesArray Lines;
};

class DebugInlineeLinesSubsection final : public DebugSubsection {
public:
  struct Entry {
    std::vector<support::ulittle32_t> ExtraFiles;
    InlineeSourceLineHeader Header;
  };

  DebugInlineeLinesSubsection(DebugChecksumsSubsection &Checksums,
                              bool HasExtraFiles = false);

  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::InlineeLines;
  }

  Error commit(BinaryStreamWriter &Writer) const override;
  uint32_t calculateSerializedSize() const override;

  void addInlineSite(TypeIndex FuncId, StringRef FileName, uint32_t SourceLine);
  void addExtraFile(StringRef FileName);

  bool hasExtraFiles() const { return HasExtraFiles; }
  void setHasExtraFiles(bool Has) { HasExtraFiles = Has; }

  std::vector<Entry>::const_iterator begin() const { return Entries.begin(); }
  std::vector<Entry>::const_iterator end() const { return Entries.end(); }

private:
  DebugChecksumsSubsection &Checksums;
  bool HasExtraFiles = false;
  uint32_t ExtraFileCount = 0;
  std::vector<Entry> Entries;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGINLINEELINESSUBSECTION_H
