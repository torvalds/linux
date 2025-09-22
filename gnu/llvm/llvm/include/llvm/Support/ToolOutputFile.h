//===- ToolOutputFile.h - Output files for compiler-like tools --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ToolOutputFile class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TOOLOUTPUTFILE_H
#define LLVM_SUPPORT_TOOLOUTPUTFILE_H

#include "llvm/Support/raw_ostream.h"
#include <optional>

namespace llvm {

class CleanupInstaller {
public:
  /// The name of the file.
  std::string Filename;

  /// The flag which indicates whether we should not delete the file.
  bool Keep;

  StringRef getFilename() { return Filename; }
  explicit CleanupInstaller(StringRef Filename);
  ~CleanupInstaller();
};

/// This class contains a raw_fd_ostream and adds a few extra features commonly
/// needed for compiler-like tool output files:
///   - The file is automatically deleted if the process is killed.
///   - The file is automatically deleted when the ToolOutputFile
///     object is destroyed unless the client calls keep().
class ToolOutputFile {
  /// This class is declared before the raw_fd_ostream so that it is constructed
  /// before the raw_fd_ostream is constructed and destructed after the
  /// raw_fd_ostream is destructed. It installs cleanups in its constructor and
  /// uninstalls them in its destructor.
  CleanupInstaller Installer;

  /// Storage for the stream, if we're owning our own stream. This is
  /// intentionally declared after Installer.
  std::optional<raw_fd_ostream> OSHolder;

  /// The actual stream to use.
  raw_fd_ostream *OS;

public:
  /// This constructor's arguments are passed to raw_fd_ostream's
  /// constructor.
  ToolOutputFile(StringRef Filename, std::error_code &EC,
                 sys::fs::OpenFlags Flags);

  ToolOutputFile(StringRef Filename, int FD);

  /// Return the contained raw_fd_ostream.
  raw_fd_ostream &os() { return *OS; }

  /// Return the filename initialized with.
  StringRef getFilename() { return Installer.getFilename(); }

  /// Indicate that the tool's job wrt this output file has been successful and
  /// the file should not be deleted.
  void keep() { Installer.Keep = true; }

  const std::string &outputFilename() { return Installer.Filename; }
};

} // end llvm namespace

#endif
