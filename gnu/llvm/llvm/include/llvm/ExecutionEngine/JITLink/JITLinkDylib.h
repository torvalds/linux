//===-- JITLinkDylib.h - JITLink Dylib type ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the JITLinkDylib API.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_JITLINKDYLIB_H
#define LLVM_EXECUTIONENGINE_JITLINK_JITLINKDYLIB_H

#include <string>

namespace llvm {
namespace jitlink {

class JITLinkDylib {
public:
  JITLinkDylib(std::string Name) : Name(std::move(Name)) {}

  /// Get the name for this JITLinkDylib.
  const std::string &getName() const { return Name; }

private:
  std::string Name;
};

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_JITLINKDYLIB_H
