//===-- SBReproducer.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBREPRODUCER_H
#define LLDB_API_SBREPRODUCER_H

#include "lldb/API/SBDefines.h"

namespace lldb_private {
namespace repro {
struct ReplayOptions;
}
} // namespace lldb_private

namespace lldb {

#ifndef SWIG
class LLDB_API SBReplayOptions {
public:
  SBReplayOptions();
  SBReplayOptions(const SBReplayOptions &rhs);
  ~SBReplayOptions();

  SBReplayOptions &operator=(const SBReplayOptions &rhs);

  void SetVerify(bool verify);
  bool GetVerify() const;

  void SetCheckVersion(bool check);
  bool GetCheckVersion() const;
};
#endif

/// The SBReproducer class is special because it bootstraps the capture and
/// replay of SB API calls. As a result we cannot rely on any other SB objects
/// in the interface or implementation of this class.
class LLDB_API SBReproducer {
public:
#ifndef SWIG
  static const char *Capture();
#endif
  static const char *Capture(const char *path);
#ifndef SWIG
  static const char *Replay(const char *path);
  static const char *Replay(const char *path, bool skip_version_check);
  static const char *Replay(const char *path, const SBReplayOptions &options);
#endif
  static const char *PassiveReplay(const char *path);
#ifndef SWIG
  static const char *Finalize(const char *path);
  static const char *GetPath();
#endif
  static bool SetAutoGenerate(bool b);
#ifndef SWIG
  static bool Generate();
#endif

  /// The working directory is set to the current working directory when the
  /// reproducers are initialized. This method allows setting a different
  /// working directory. This is used by the API test suite  which temporarily
  /// changes the directory to where the test lives. This is a NO-OP in every
  /// mode but capture.
  static void SetWorkingDirectory(const char *path);
};

} // namespace lldb

#endif
