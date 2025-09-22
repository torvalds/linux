//===--- SerializedDiagnostics.h - Common data for serialized diagnostics -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_SERIALIZEDDIAGNOSTICS_H
#define LLVM_CLANG_FRONTEND_SERIALIZEDDIAGNOSTICS_H

#include "llvm/Bitstream/BitCodes.h"

namespace clang {
namespace serialized_diags {

enum BlockIDs {
  /// A top-level block which represents any meta data associated
  /// with the diagostics, including versioning of the format.
  BLOCK_META = llvm::bitc::FIRST_APPLICATION_BLOCKID,

  /// The this block acts as a container for all the information
  /// for a specific diagnostic.
  BLOCK_DIAG
};

enum RecordIDs {
  RECORD_VERSION = 1,
  RECORD_DIAG,
  RECORD_SOURCE_RANGE,
  RECORD_DIAG_FLAG,
  RECORD_CATEGORY,
  RECORD_FILENAME,
  RECORD_FIXIT,
  RECORD_FIRST = RECORD_VERSION,
  RECORD_LAST = RECORD_FIXIT
};

/// A stable version of DiagnosticIDs::Level.
///
/// Do not change the order of values in this enum, and please increment the
/// serialized diagnostics version number when you add to it.
enum Level {
  Ignored = 0,
  Note,
  Warning,
  Error,
  Fatal,
  Remark
};

/// The serialized diagnostics version number.
enum { VersionNumber = 2 };

} // end serialized_diags namespace
} // end clang namespace

#endif
