//===- Utils.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_UTILS_H
#define LLVM_DWARFLINKER_UTILS_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

namespace llvm {
namespace dwarf_linker {

/// This function calls \p Iteration() until it returns false.
/// If number of iterations exceeds \p MaxCounter then an Error is returned.
/// This function should be used for loops which assumed to have number of
/// iterations significantly smaller than \p MaxCounter to avoid infinite
/// looping in error cases.
inline Error finiteLoop(function_ref<Expected<bool>()> Iteration,
                        size_t MaxCounter = 100000) {
  size_t iterationsCounter = 0;
  while (iterationsCounter++ < MaxCounter) {
    Expected<bool> IterationResultOrError = Iteration();
    if (!IterationResultOrError)
      return IterationResultOrError.takeError();
    if (!IterationResultOrError.get())
      return Error::success();
  }
  return createStringError(std::errc::invalid_argument, "Infinite recursion");
}

/// Make a best effort to guess the
/// Xcode.app/Contents/Developer path from an SDK path.
inline StringRef guessDeveloperDir(StringRef SysRoot) {
  // Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  auto it = sys::path::rbegin(SysRoot);
  auto end = sys::path::rend(SysRoot);
  if (it == end || !it->ends_with(".sdk"))
    return {};
  ++it;
  // Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs
  if (it == end || *it != "SDKs")
    return {};
  auto developerEnd = it;
  ++it;
  while (it != end) {
    // Contents/Developer/Platforms/MacOSX.platform/Developer
    if (*it != "Developer")
      return {};
    ++it;
    if (it == end)
      return {};
    if (*it == "Contents")
      return StringRef(SysRoot.data(),
                       developerEnd - sys::path::rend(SysRoot) - 1);
    // Contents/Developer/Platforms/MacOSX.platform
    if (!it->ends_with(".platform"))
      return {};
    ++it;
    // Contents/Developer/Platforms
    if (it == end || *it != "Platforms")
      return {};
    developerEnd = it;
    ++it;
  }
  return {};
}

/// Make a best effort to determine whether Path is inside a toolchain.
inline bool isInToolchainDir(StringRef Path) {
  // Library/Developer/Toolchains/swift-DEVELOPMENT-SNAPSHOT-2024-05-15-a.xctoolchain/usr/lib/swift/macosx/_StringProcessing.swiftmodule/arm64-apple-macos.private.swiftinterface
  for (auto it = sys::path::rbegin(Path), end = sys::path::rend(Path);
       it != end; ++it) {
    if (it->ends_with(".xctoolchain")) {
      ++it;
      if (it == end)
        return false;
      if (*it != "Toolchains")
        return false;
      ++it;
      if (it == end)
        return false;
      if (*it != "Developer")
        return false;
      return true;
    }
  }
  return false;
}

inline bool isPathAbsoluteOnWindowsOrPosix(const Twine &Path) {
  // Debug info can contain paths from any OS, not necessarily
  // an OS we're currently running on. Moreover different compilation units can
  // be compiled on different operating systems and linked together later.
  return sys::path::is_absolute(Path, sys::path::Style::posix) ||
         sys::path::is_absolute(Path, sys::path::Style::windows);
}

} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_UTILS_H
