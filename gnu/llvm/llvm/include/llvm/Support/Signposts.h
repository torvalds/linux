//===-- llvm/Support/Signposts.h - Interval debug annotations ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Some OS's provide profilers that allow applications to provide custom
/// annotations to the profiler. For example, on Xcode 10 and later 'signposts'
/// can be emitted by the application and these will be rendered to the Points
/// of Interest track on the instruments timeline.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SIGNPOSTS_H
#define LLVM_SUPPORT_SIGNPOSTS_H

#include <memory>

namespace llvm {
class SignpostEmitterImpl;
class StringRef;

/// Manages the emission of signposts into the recording method supported by
/// the OS.
class SignpostEmitter {
  std::unique_ptr<SignpostEmitterImpl> Impl;

public:
  SignpostEmitter();
  ~SignpostEmitter();

  bool isEnabled() const;

  /// Begin a signposted interval for a given object.
  void startInterval(const void *O, StringRef Name);
  /// End a signposted interval for a given object.
  void endInterval(const void *O, StringRef Name);
};

} // end namespace llvm

#endif // LLVM_SUPPORT_SIGNPOSTS_H
