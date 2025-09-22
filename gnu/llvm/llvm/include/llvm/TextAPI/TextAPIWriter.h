//===--- TextAPIWriter.h - Text API Writer ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_TEXTAPIWRITER_H
#define LLVM_TEXTAPI_TEXTAPIWRITER_H

#include "llvm/ADT/StringSwitch.h"
#include "llvm/TextAPI/InterfaceFile.h"

namespace llvm {

class Error;
class raw_ostream;

namespace MachO {

class TextAPIWriter {
public:
  TextAPIWriter() = delete;

  /// Write TAPI text file contents into stream.
  ///
  /// \param OS Stream to write to.
  /// \param File Library attributes to write as text file.
  /// \param FileKind File format to write text file as. If not specified, it
  /// will read from File.
  /// \param Compact Whether to limit whitespace in text file.
  static Error writeToStream(raw_ostream &OS, const InterfaceFile &File,
                             const FileType FileKind = FileType::Invalid,
                             bool Compact = false);

  /// Get TAPI FileType from the input string.
  ///
  /// \param FT String of input to map to FileType.
  static FileType parseFileType(const StringRef FT) {
    return StringSwitch<FileType>(FT)
        .Case("tbd-v1", FileType::TBD_V1)
        .Case("tbd-v2", FileType::TBD_V2)
        .Case("tbd-v3", FileType::TBD_V3)
        .Case("tbd-v4", FileType::TBD_V4)
        .Case("tbd-v5", FileType::TBD_V5)
        .Default(FileType::Invalid);
  }
};

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_TEXTAPIWRITER_H
