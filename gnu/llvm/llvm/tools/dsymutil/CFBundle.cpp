//===- tools/dsymutil/CFBundle.cpp - CFBundle helper ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CFBundle.h"

#ifdef __APPLE__
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <CoreFoundation/CoreFoundation.h>
#include <assert.h>
#include <glob.h>
#include <memory>
#endif

namespace llvm {
namespace dsymutil {

#ifdef __APPLE__
/// Deleter that calls CFRelease rather than deleting the pointer.
template <typename T> struct CFDeleter {
  void operator()(T *P) {
    if (P)
      ::CFRelease(P);
  }
};

/// This helper owns any CoreFoundation pointer and will call CFRelease() on
/// any valid pointer it owns unless that pointer is explicitly released using
/// the release() member function.
template <typename T>
using CFReleaser = std::unique_ptr<std::remove_pointer_t<T>,
                                   CFDeleter<std::remove_pointer_t<T>>>;

/// RAII wrapper around CFBundleRef.
class CFString : public CFReleaser<CFStringRef> {
public:
  CFString(CFStringRef CFStr = nullptr) : CFReleaser<CFStringRef>(CFStr) {}

  const char *UTF8(std::string &Str) const {
    return CFString::UTF8(get(), Str);
  }

  CFIndex GetLength() const {
    if (CFStringRef Str = get())
      return CFStringGetLength(Str);
    return 0;
  }

  static const char *UTF8(CFStringRef CFStr, std::string &Str);
};

/// Static function that puts a copy of the UTF-8 contents of CFStringRef into
/// std::string and returns the C string pointer that is contained in the
/// std::string when successful, nullptr otherwise.
///
/// This allows the std::string parameter to own the extracted string, and also
/// allows that string to be returned as a C string pointer that can be used.
const char *CFString::UTF8(CFStringRef CFStr, std::string &Str) {
  if (!CFStr)
    return nullptr;

  const CFStringEncoding Encoding = kCFStringEncodingUTF8;
  CFIndex MaxUTF8StrLength = CFStringGetLength(CFStr);
  MaxUTF8StrLength =
      CFStringGetMaximumSizeForEncoding(MaxUTF8StrLength, Encoding);
  if (MaxUTF8StrLength > 0) {
    Str.resize(MaxUTF8StrLength);
    if (!Str.empty() &&
        CFStringGetCString(CFStr, &Str[0], Str.size(), Encoding)) {
      Str.resize(strlen(Str.c_str()));
      return Str.c_str();
    }
  }

  return nullptr;
}

/// RAII wrapper around CFBundleRef.
class CFBundle : public CFReleaser<CFBundleRef> {
public:
  CFBundle(StringRef Path) : CFReleaser<CFBundleRef>() { SetFromPath(Path); }

  CFBundle(CFURLRef Url)
      : CFReleaser<CFBundleRef>(Url ? ::CFBundleCreate(nullptr, Url)
                                    : nullptr) {}

  /// Return the bundle identifier.
  CFStringRef GetIdentifier() const {
    if (CFBundleRef bundle = get())
      return ::CFBundleGetIdentifier(bundle);
    return nullptr;
  }

  /// Return value for key.
  CFTypeRef GetValueForInfoDictionaryKey(CFStringRef key) const {
    if (CFBundleRef bundle = get())
      return ::CFBundleGetValueForInfoDictionaryKey(bundle, key);
    return nullptr;
  }

private:
  /// Helper to initialize this instance with a new bundle created from the
  /// given path. This function will recursively remove components from the
  /// path in its search for the nearest Info.plist.
  void SetFromPath(StringRef Path);
};

void CFBundle::SetFromPath(StringRef Path) {
  // Start from an empty/invalid CFBundle.
  reset();

  if (Path.empty() || !sys::fs::exists(Path))
    return;

  SmallString<256> RealPath;
  sys::fs::real_path(Path, RealPath, /*expand_tilde*/ true);

  do {
    // Create a CFURL from the current path and use it to create a CFBundle.
    CFReleaser<CFURLRef> BundleURL(::CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, (const UInt8 *)RealPath.data(), RealPath.size(),
        false));
    reset(::CFBundleCreate(kCFAllocatorDefault, BundleURL.get()));

    // If we have a valid bundle and find its identifier we are done.
    if (get() != nullptr) {
      if (GetIdentifier() != nullptr)
        return;
      reset();
    }

    // Remove the last component of the path and try again until there's
    // nothing left but the root.
    sys::path::remove_filename(RealPath);
  } while (RealPath != sys::path::root_name(RealPath));
}
#endif

/// On Darwin, try and find the original executable's Info.plist to extract
/// information about the bundle. Return default values on other platforms.
CFBundleInfo getBundleInfo(StringRef ExePath) {
  CFBundleInfo BundleInfo;

#ifdef __APPLE__
  auto PrintError = [&](CFTypeID TypeID) {
    CFString TypeIDCFStr(::CFCopyTypeIDDescription(TypeID));
    std::string TypeIDStr;
    errs() << "The Info.plist key \"CFBundleShortVersionString\" is"
           << "a " << TypeIDCFStr.UTF8(TypeIDStr)
           << ", but it should be a string in: " << ExePath << ".\n";
  };

  CFBundle Bundle(ExePath);
  if (CFStringRef BundleID = Bundle.GetIdentifier()) {
    CFString::UTF8(BundleID, BundleInfo.IDStr);
    if (CFTypeRef TypeRef =
            Bundle.GetValueForInfoDictionaryKey(CFSTR("CFBundleVersion"))) {
      CFTypeID TypeID = ::CFGetTypeID(TypeRef);
      if (TypeID == ::CFStringGetTypeID())
        CFString::UTF8((CFStringRef)TypeRef, BundleInfo.VersionStr);
      else
        PrintError(TypeID);
    }
    if (CFTypeRef TypeRef = Bundle.GetValueForInfoDictionaryKey(
            CFSTR("CFBundleShortVersionString"))) {
      CFTypeID TypeID = ::CFGetTypeID(TypeRef);
      if (TypeID == ::CFStringGetTypeID())
        CFString::UTF8((CFStringRef)TypeRef, BundleInfo.ShortVersionStr);
      else
        PrintError(TypeID);
    }
  }
#endif

  return BundleInfo;
}

} // end namespace dsymutil
} // end namespace llvm
