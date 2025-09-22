//===-- XcodeSDK.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_SDK_H
#define LLDB_UTILITY_SDK_H

#include "lldb/lldb-forward.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"
#include <tuple>

namespace llvm {
class Triple;
}

namespace lldb_private {

/// An abstraction for Xcode-style SDKs that works like \ref ArchSpec.
class XcodeSDK {
  std::string m_name;

public:
  /// Different types of Xcode SDKs.
  enum Type : int {
    MacOSX = 0,
    iPhoneSimulator,
    iPhoneOS,
    AppleTVSimulator,
    AppleTVOS,
    WatchSimulator,
    watchOS,
    XRSimulator,
    XROS,
    bridgeOS,
    Linux,
    unknown = -1
  };
  static constexpr int numSDKTypes = Linux + 1;

  /// A parsed SDK directory name.
  struct Info {
    Type type = unknown;
    llvm::VersionTuple version;
    bool internal = false;

    Info() = default;
    bool operator<(const Info &other) const;
    bool operator==(const Info &other) const;
  };


  /// Default constructor, constructs an empty string.
  XcodeSDK() = default;
  /// Construct an XcodeSDK object from a specification.
  XcodeSDK(Info info);
  /// Initialize an XcodeSDK object with an SDK name. The SDK name is the last
  /// directory component of a path one would pass to clang's -isysroot
  /// parameter. For example, "MacOSX.10.14.sdk".
  XcodeSDK(std::string &&name) : m_name(std::move(name)) {}
  static XcodeSDK GetAnyMacOS() { return XcodeSDK("MacOSX.sdk"); }

  /// The merge function follows a strict order to maintain monotonicity:
  /// 1. SDK with the higher SDKType wins.
  /// 2. The newer SDK wins.
  void Merge(const XcodeSDK &other);

  XcodeSDK &operator=(const XcodeSDK &other);
  XcodeSDK(const XcodeSDK&) = default;
  bool operator==(const XcodeSDK &other) const;

  /// Return parsed SDK type and version number.
  Info Parse() const;
  bool IsAppleInternalSDK() const;
  llvm::VersionTuple GetVersion() const;
  Type GetType() const;
  llvm::StringRef GetString() const;
  /// Whether this Xcode SDK supports Swift.
  bool SupportsSwift() const;

  /// Whether LLDB feels confident importing Clang modules from this SDK.
  static bool SDKSupportsModules(Type type, llvm::VersionTuple version);
  static bool SDKSupportsModules(Type desired_type, const FileSpec &sdk_path);
  /// Return the canonical SDK name, such as "macosx" for the macOS SDK.
  static std::string GetCanonicalName(Info info);
  /// Return the best-matching SDK type for a specific triple.
  static XcodeSDK::Type GetSDKTypeForTriple(const llvm::Triple &triple);

  static std::string FindXcodeContentsDirectoryInPath(llvm::StringRef path);
};

} // namespace lldb_private

#endif
