//===- llvm/Remarks/RemarkStreamer.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the main interface for streaming remarks.
//
// This is used to stream any llvm::remarks::Remark to an open file taking
// advantage of all the serialization capabilities developed for remarks (e.g.
// metadata in a section, bitstream format, etc.).
//
// Typically, a specialized remark emitter should hold a reference to the main
// remark streamer set up in the LLVMContext, and should convert specialized
// diagnostics to llvm::remarks::Remark objects as they get emitted.
//
// Specialized remark emitters can be components like:
// * Remarks from LLVM (M)IR passes
// * Remarks from the frontend
// * Remarks from an intermediate IR
//
// This allows for composition between specialized remark emitters throughout
// the compilation pipeline, that end up in the same file, using the same format
// and serialization techniques.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARKSTREAMER_H
#define LLVM_REMARKS_REMARKSTREAMER_H

#include "llvm/Remarks/RemarkSerializer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Regex.h"
#include <memory>
#include <optional>

namespace llvm {

class raw_ostream;

namespace remarks {
class RemarkStreamer final {
  /// The regex used to filter remarks based on the passes that emit them.
  std::optional<Regex> PassFilter;
  /// The object used to serialize the remarks to a specific format.
  std::unique_ptr<remarks::RemarkSerializer> RemarkSerializer;
  /// The filename that the remark diagnostics are emitted to.
  const std::optional<std::string> Filename;

public:
  RemarkStreamer(std::unique_ptr<remarks::RemarkSerializer> RemarkSerializer,
                 std::optional<StringRef> Filename = std::nullopt);

  /// Return the filename that the remark diagnostics are emitted to.
  std::optional<StringRef> getFilename() const {
    return Filename ? std::optional<StringRef>(*Filename) : std::nullopt;
  }
  /// Return stream that the remark diagnostics are emitted to.
  raw_ostream &getStream() { return RemarkSerializer->OS; }
  /// Return the serializer used for this stream.
  remarks::RemarkSerializer &getSerializer() { return *RemarkSerializer; }
  /// Set a pass filter based on a regex \p Filter.
  /// Returns an error if the regex is invalid.
  Error setFilter(StringRef Filter);
  /// Check wether the string matches the filter.
  bool matchesFilter(StringRef Str);
  /// Check if the remarks also need to have associated metadata in a section.
  bool needsSection() const;
};
} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_REMARKSTREAMER_H
