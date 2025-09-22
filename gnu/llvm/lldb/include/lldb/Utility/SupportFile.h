//===-- SupportFile.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_SUPPORTFILE_H
#define LLDB_UTILITY_SUPPORTFILE_H

#include "lldb/Utility/Checksum.h"
#include "lldb/Utility/FileSpec.h"

namespace lldb_private {

/// Wraps either a FileSpec that represents a local file or a source
/// file whose contents is known (for example because it can be
/// reconstructed from debug info), but that hasn't been written to a
/// file yet. This also stores an optional checksum of the on-disk content.
class SupportFile {
public:
  SupportFile() : m_file_spec(), m_checksum() {}
  SupportFile(const FileSpec &spec) : m_file_spec(spec), m_checksum() {}
  SupportFile(const FileSpec &spec, const Checksum &checksum)
      : m_file_spec(spec), m_checksum(checksum) {}

  SupportFile(const SupportFile &other) = delete;
  SupportFile(SupportFile &&other) = default;

  virtual ~SupportFile() = default;

  enum SupportFileEquality : uint8_t {
    eEqualFileSpec = (1u << 1),
    eEqualChecksum = (1u << 2),
    eEqualChecksumIfSet = (1u << 3),
    eEqualFileSpecAndChecksum = eEqualFileSpec | eEqualChecksum,
    eEqualFileSpecAndChecksumIfSet = eEqualFileSpec | eEqualChecksumIfSet,
  };

  bool Equal(const SupportFile &other,
             SupportFileEquality equality = eEqualFileSpecAndChecksum) const {
    assert(!(equality & eEqualChecksum & eEqualChecksumIfSet) &&
           "eEqualChecksum and eEqualChecksumIfSet are mutually exclusive");

    if (equality & eEqualFileSpec) {
      if (m_file_spec != other.m_file_spec)
        return false;
    }

    if (equality & eEqualChecksum) {
      if (m_checksum != other.m_checksum)
        return false;
    }

    if (equality & eEqualChecksumIfSet) {
      if (m_checksum && other.m_checksum)
        if (m_checksum != other.m_checksum)
          return false;
    }

    return true;
  }

  /// Return the file name only. Useful for resolving breakpoints by file name.
  const FileSpec &GetSpecOnly() const { return m_file_spec; };

  /// Return the checksum or all zeros if there is none.
  const Checksum &GetChecksum() const { return m_checksum; };

  /// Materialize the file to disk and return the path to that temporary file.
  virtual const FileSpec &Materialize() { return m_file_spec; }

protected:
  FileSpec m_file_spec;
  Checksum m_checksum;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_SUPPORTFILE_H
