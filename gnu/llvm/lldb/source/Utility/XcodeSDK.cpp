//===-- XcodeSDK.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/XcodeSDK.h"
#include "lldb/Utility/FileSpec.h"

#include "lldb/lldb-types.h"

#include "llvm/TargetParser/Triple.h"

#include <string>

using namespace lldb;
using namespace lldb_private;

static llvm::StringRef GetName(XcodeSDK::Type type) {
  switch (type) {
  case XcodeSDK::MacOSX:
    return "MacOSX";
  case XcodeSDK::iPhoneSimulator:
    return "iPhoneSimulator";
  case XcodeSDK::iPhoneOS:
    return "iPhoneOS";
  case XcodeSDK::AppleTVSimulator:
    return "AppleTVSimulator";
  case XcodeSDK::AppleTVOS:
    return "AppleTVOS";
  case XcodeSDK::WatchSimulator:
    return "WatchSimulator";
  case XcodeSDK::watchOS:
    return "WatchOS";
  case XcodeSDK::XRSimulator:
    return "XRSimulator";
  case XcodeSDK::XROS:
    return "XROS";
  case XcodeSDK::bridgeOS:
    return "bridgeOS";
  case XcodeSDK::Linux:
    return "Linux";
  case XcodeSDK::unknown:
    return {};
  }
  llvm_unreachable("Unhandled sdk type!");
}

XcodeSDK::XcodeSDK(XcodeSDK::Info info) : m_name(GetName(info.type).str()) {
  if (!m_name.empty()) {
    if (!info.version.empty())
      m_name += info.version.getAsString();
    if (info.internal)
      m_name += ".Internal";
    m_name += ".sdk";
  }
}

XcodeSDK &XcodeSDK::operator=(const XcodeSDK &other) = default;

bool XcodeSDK::operator==(const XcodeSDK &other) const {
  return m_name == other.m_name;
}

static XcodeSDK::Type ParseSDKName(llvm::StringRef &name) {
  if (name.consume_front("MacOSX"))
    return XcodeSDK::MacOSX;
  if (name.consume_front("iPhoneSimulator"))
    return XcodeSDK::iPhoneSimulator;
  if (name.consume_front("iPhoneOS"))
    return XcodeSDK::iPhoneOS;
  if (name.consume_front("AppleTVSimulator"))
    return XcodeSDK::AppleTVSimulator;
  if (name.consume_front("AppleTVOS"))
    return XcodeSDK::AppleTVOS;
  if (name.consume_front("WatchSimulator"))
    return XcodeSDK::WatchSimulator;
  if (name.consume_front("WatchOS"))
    return XcodeSDK::watchOS;
  if (name.consume_front("XRSimulator"))
    return XcodeSDK::XRSimulator;
  if (name.consume_front("XROS"))
    return XcodeSDK::XROS;
  if (name.consume_front("bridgeOS"))
    return XcodeSDK::bridgeOS;
  if (name.consume_front("Linux"))
    return XcodeSDK::Linux;
  static_assert(XcodeSDK::Linux == XcodeSDK::numSDKTypes - 1,
                "New SDK type was added, update this list!");
  return XcodeSDK::unknown;
}

static llvm::VersionTuple ParseSDKVersion(llvm::StringRef &name) {
  unsigned i = 0;
  while (i < name.size() && name[i] >= '0' && name[i] <= '9')
    ++i;
  if (i == name.size() || name[i++] != '.')
    return {};
  while (i < name.size() && name[i] >= '0' && name[i] <= '9')
    ++i;
  if (i == name.size() || name[i++] != '.')
    return {};

  llvm::VersionTuple version;
  version.tryParse(name.slice(0, i - 1));
  name = name.drop_front(i);
  return version;
}

static bool ParseAppleInternalSDK(llvm::StringRef &name) {
  return name.consume_front("Internal.") || name.consume_front(".Internal.");
}

XcodeSDK::Info XcodeSDK::Parse() const {
  XcodeSDK::Info info;
  llvm::StringRef input(m_name);
  info.type = ParseSDKName(input);
  info.version = ParseSDKVersion(input);
  info.internal = ParseAppleInternalSDK(input);
  return info;
}

bool XcodeSDK::IsAppleInternalSDK() const {
  llvm::StringRef input(m_name);
  ParseSDKName(input);
  ParseSDKVersion(input);
  return ParseAppleInternalSDK(input);
}

llvm::VersionTuple XcodeSDK::GetVersion() const {
  llvm::StringRef input(m_name);
  ParseSDKName(input);
  return ParseSDKVersion(input);
}

XcodeSDK::Type XcodeSDK::GetType() const {
  llvm::StringRef input(m_name);
  return ParseSDKName(input);
}

llvm::StringRef XcodeSDK::GetString() const { return m_name; }

bool XcodeSDK::Info::operator<(const Info &other) const {
  return std::tie(type, version, internal) <
         std::tie(other.type, other.version, other.internal);
}

bool XcodeSDK::Info::operator==(const Info &other) const {
  return std::tie(type, version, internal) ==
         std::tie(other.type, other.version, other.internal);
}

void XcodeSDK::Merge(const XcodeSDK &other) {
  // The "bigger" SDK always wins.
  auto l = Parse();
  auto r = other.Parse();
  if (l < r)
    *this = other;
  else {
    // The Internal flag always wins.
    if (llvm::StringRef(m_name).ends_with(".sdk"))
      if (!l.internal && r.internal)
        m_name =
            m_name.substr(0, m_name.size() - 3) + std::string("Internal.sdk");
  }
}

std::string XcodeSDK::GetCanonicalName(XcodeSDK::Info info) {
  std::string name;
  switch (info.type) {
  case MacOSX:
    name = "macosx";
    break;
  case iPhoneSimulator:
    name = "iphonesimulator";
    break;
  case iPhoneOS:
    name = "iphoneos";
    break;
  case AppleTVSimulator:
    name = "appletvsimulator";
    break;
  case AppleTVOS:
    name = "appletvos";
    break;
  case WatchSimulator:
    name = "watchsimulator";
    break;
  case watchOS:
    name = "watchos";
    break;
  case XRSimulator:
    name = "xrsimulator";
    break;
  case XROS:
    name = "xros";
    break;
  case bridgeOS:
    name = "bridgeos";
    break;
  case Linux:
    name = "linux";
    break;
  case unknown:
    return {};
  }
  if (!info.version.empty())
    name += info.version.getAsString();
  if (info.internal)
    name += ".internal";
  return name;
}

bool XcodeSDK::SDKSupportsModules(XcodeSDK::Type sdk_type,
                                  llvm::VersionTuple version) {
  switch (sdk_type) {
  case Type::MacOSX:
    return version >= llvm::VersionTuple(10, 10);
  case Type::iPhoneOS:
  case Type::iPhoneSimulator:
  case Type::AppleTVOS:
  case Type::AppleTVSimulator:
    return version >= llvm::VersionTuple(8);
  case Type::watchOS:
  case Type::WatchSimulator:
    return version >= llvm::VersionTuple(6);
  case Type::XROS:
  case Type::XRSimulator:
    return true;
  default:
    return false;
  }

  return false;
}

bool XcodeSDK::SupportsSwift() const {
  XcodeSDK::Info info = Parse();
  switch (info.type) {
  case Type::MacOSX:
    return info.version.empty() || info.version >= llvm::VersionTuple(10, 10);
  case Type::iPhoneOS:
  case Type::iPhoneSimulator:
    return info.version.empty() || info.version >= llvm::VersionTuple(8);
  case Type::AppleTVSimulator:
  case Type::AppleTVOS:
    return info.version.empty() || info.version >= llvm::VersionTuple(9);
  case Type::WatchSimulator:
  case Type::watchOS:
    return info.version.empty() || info.version >= llvm::VersionTuple(2);
  case Type::XROS:
  case Type::XRSimulator:
  case Type::Linux:
    return true;
  default:
    return false;
  }
}

bool XcodeSDK::SDKSupportsModules(XcodeSDK::Type desired_type,
                                  const FileSpec &sdk_path) {
  ConstString last_path_component = sdk_path.GetFilename();

  if (!last_path_component)
    return false;

  XcodeSDK sdk(last_path_component.GetStringRef().str());
  if (sdk.GetType() != desired_type)
    return false;
  return SDKSupportsModules(sdk.GetType(), sdk.GetVersion());
}

XcodeSDK::Type XcodeSDK::GetSDKTypeForTriple(const llvm::Triple &triple) {
  using namespace llvm;
  switch (triple.getOS()) {
  case Triple::MacOSX:
  case Triple::Darwin:
    return XcodeSDK::MacOSX;
  case Triple::IOS:
    switch (triple.getEnvironment()) {
    case Triple::MacABI:
      return XcodeSDK::MacOSX;
    case Triple::Simulator:
      return XcodeSDK::iPhoneSimulator;
    default:
      return XcodeSDK::iPhoneOS;
    }
  case Triple::TvOS:
    if (triple.getEnvironment() == Triple::Simulator)
      return XcodeSDK::AppleTVSimulator;
    return XcodeSDK::AppleTVOS;
  case Triple::WatchOS:
    if (triple.getEnvironment() == Triple::Simulator)
      return XcodeSDK::WatchSimulator;
    return XcodeSDK::watchOS;
  case Triple::XROS:
    if (triple.getEnvironment() == Triple::Simulator)
      return XcodeSDK::XRSimulator;
    return XcodeSDK::XROS;
  case Triple::Linux:
    return XcodeSDK::Linux;
  default:
    return XcodeSDK::unknown;
  }
}

std::string XcodeSDK::FindXcodeContentsDirectoryInPath(llvm::StringRef path) {
  auto begin = llvm::sys::path::begin(path);
  auto end = llvm::sys::path::end(path);

  // Iterate over the path components until we find something that ends with
  // .app. If the next component is Contents then we've found the Contents
  // directory.
  for (auto it = begin; it != end; ++it) {
    if (it->ends_with(".app")) {
      auto next = it;
      if (++next != end && *next == "Contents") {
        llvm::SmallString<128> buffer;
        llvm::sys::path::append(buffer, begin, ++next,
                                llvm::sys::path::Style::posix);
        return buffer.str().str();
      }
    }
  }

  return {};
}
