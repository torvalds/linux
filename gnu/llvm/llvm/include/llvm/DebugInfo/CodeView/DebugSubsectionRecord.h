//===- DebugSubsectionRecord.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONRECORD_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONRECORD_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <memory>

namespace llvm {

class BinaryStreamWriter;

namespace codeview {

class DebugSubsection;

// Corresponds to the `CV_DebugSSubsectionHeader_t` structure.
struct DebugSubsectionHeader {
  support::ulittle32_t Kind;   // codeview::DebugSubsectionKind enum
  support::ulittle32_t Length; // number of bytes occupied by this record.
};

class DebugSubsectionRecord {
public:
  DebugSubsectionRecord();
  DebugSubsectionRecord(DebugSubsectionKind Kind, BinaryStreamRef Data);

  static Error initialize(BinaryStreamRef Stream, DebugSubsectionRecord &Info);

  uint32_t getRecordLength() const;
  DebugSubsectionKind kind() const;
  BinaryStreamRef getRecordData() const;

private:
  DebugSubsectionKind Kind = DebugSubsectionKind::None;
  BinaryStreamRef Data;
};

class DebugSubsectionRecordBuilder {
public:
  DebugSubsectionRecordBuilder(std::shared_ptr<DebugSubsection> Subsection);

  /// Use this to copy existing subsections directly from source to destination.
  /// For example, line table subsections in an object file only need to be
  /// relocated before being copied into the PDB.
  DebugSubsectionRecordBuilder(const DebugSubsectionRecord &Contents);

  uint32_t calculateSerializedLength() const;
  Error commit(BinaryStreamWriter &Writer, CodeViewContainer Container) const;

private:
  /// The subsection to build. Will be null if Contents is non-empty.
  std::shared_ptr<DebugSubsection> Subsection;

  /// The bytes of the subsection. Only non-empty if Subsection is null.
  /// FIXME: Reduce the size of this.
  DebugSubsectionRecord Contents;
};

} // end namespace codeview

template <> struct VarStreamArrayExtractor<codeview::DebugSubsectionRecord> {
  Error operator()(BinaryStreamRef Stream, uint32_t &Length,
                   codeview::DebugSubsectionRecord &Info) {
    // FIXME: We need to pass the container type through to this function.  In
    // practice this isn't super important since the subsection header describes
    // its length and we can just skip it.  It's more important when writing.
    if (auto EC = codeview::DebugSubsectionRecord::initialize(Stream, Info))
      return EC;
    Length = alignTo(Info.getRecordLength(), 4);
    return Error::success();
  }
};

namespace codeview {

using DebugSubsectionArray = VarStreamArray<DebugSubsectionRecord>;

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONRECORD_H
