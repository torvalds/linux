//===--- TextAPIReader.h - Text API Reader ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_TEXTAPIREADER_H
#define LLVM_TEXTAPI_TEXTAPIREADER_H

#include "llvm/Support/Error.h"

namespace llvm {

class MemoryBufferRef;

namespace MachO {

class InterfaceFile;
enum FileType : unsigned;

class TextAPIReader {
public:
  ///  Determine whether input can be interpreted as TAPI text file.
  ///  This allows one to exit early when file is not recognized as TAPI file
  ///  as opposed to `get` which attempts to full parse and load of library
  ///  attributes.
  ///
  /// \param InputBuffer Buffer holding contents of TAPI text file.
  /// \return The file format version of TAPI text file.
  static Expected<FileType> canRead(MemoryBufferRef InputBuffer);

  /// Parse and get an InterfaceFile that represents the full
  /// library.
  ///
  /// \param InputBuffer Buffer holding contents of TAPI text file.
  static Expected<std::unique_ptr<InterfaceFile>>
  get(MemoryBufferRef InputBuffer);

  TextAPIReader() = delete;
};

} // end namespace MachO.
} // end namespace llvm.

#endif // LLVM_TEXTAPI_TEXTAPIREADER_H
