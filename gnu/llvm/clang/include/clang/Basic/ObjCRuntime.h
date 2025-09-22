//===- ObjCRuntime.h - Objective-C Runtime Configuration --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines types useful for describing an Objective-C runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_OBJCRUNTIME_H
#define LLVM_CLANG_BASIC_OBJCRUNTIME_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/HashBuilder.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"
#include <string>

namespace clang {

/// The basic abstraction for the target Objective-C runtime.
class ObjCRuntime {
public:
  /// The basic Objective-C runtimes that we know about.
  enum Kind {
    /// 'macosx' is the Apple-provided NeXT-derived runtime on Mac OS
    /// X platforms that use the non-fragile ABI; the version is a
    /// release of that OS.
    MacOSX,

    /// 'macosx-fragile' is the Apple-provided NeXT-derived runtime on
    /// Mac OS X platforms that use the fragile ABI; the version is a
    /// release of that OS.
    FragileMacOSX,

    /// 'ios' is the Apple-provided NeXT-derived runtime on iOS or the iOS
    /// simulator;  it is always non-fragile.  The version is a release
    /// version of iOS.
    iOS,

    /// 'watchos' is a variant of iOS for Apple's watchOS. The version
    /// is a release version of watchOS.
    WatchOS,

    /// 'gcc' is the Objective-C runtime shipped with GCC, implementing a
    /// fragile Objective-C ABI
    GCC,

    /// 'gnustep' is the modern non-fragile GNUstep runtime.
    GNUstep,

    /// 'objfw' is the Objective-C runtime included in ObjFW
    ObjFW
  };

private:
  Kind TheKind = MacOSX;
  VersionTuple Version;

public:
  /// A bogus initialization of the runtime.
  ObjCRuntime() = default;
  ObjCRuntime(Kind kind, const VersionTuple &version)
      : TheKind(kind), Version(version) {}

  void set(Kind kind, VersionTuple version) {
    TheKind = kind;
    Version = version;
  }

  Kind getKind() const { return TheKind; }
  const VersionTuple &getVersion() const { return Version; }

  /// Does this runtime follow the set of implied behaviors for a
  /// "non-fragile" ABI?
  bool isNonFragile() const {
    switch (getKind()) {
    case FragileMacOSX: return false;
    case GCC: return false;
    case MacOSX: return true;
    case GNUstep: return true;
    case ObjFW: return true;
    case iOS: return true;
    case WatchOS: return true;
    }
    llvm_unreachable("bad kind");
  }

  /// The inverse of isNonFragile():  does this runtime follow the set of
  /// implied behaviors for a "fragile" ABI?
  bool isFragile() const { return !isNonFragile(); }

  /// The default dispatch mechanism to use for the specified architecture
  bool isLegacyDispatchDefaultForArch(llvm::Triple::ArchType Arch) {
    // The GNUstep runtime uses a newer dispatch method by default from
    // version 1.6 onwards
    if (getKind() == GNUstep) {
      switch (Arch) {
      case llvm::Triple::arm:
      case llvm::Triple::x86:
      case llvm::Triple::x86_64:
        return !(getVersion() >= VersionTuple(1, 6));
      case llvm::Triple::aarch64:
      case llvm::Triple::mips64:
        return !(getVersion() >= VersionTuple(1, 9));
      case llvm::Triple::riscv64:
        return !(getVersion() >= VersionTuple(2, 2));
      default:
        return true;
      }
    } else if ((getKind() == MacOSX) && isNonFragile() &&
               (getVersion() >= VersionTuple(10, 0)) &&
               (getVersion() < VersionTuple(10, 6)))
      return Arch != llvm::Triple::x86_64;
    // Except for deployment target of 10.5 or less,
    // Mac runtimes use legacy dispatch everywhere now.
    return true;
  }

  /// Is this runtime basically of the GNU family of runtimes?
  bool isGNUFamily() const {
    switch (getKind()) {
    case FragileMacOSX:
    case MacOSX:
    case iOS:
    case WatchOS:
      return false;
    case GCC:
    case GNUstep:
    case ObjFW:
      return true;
    }
    llvm_unreachable("bad kind");
  }

  /// Is this runtime basically of the NeXT family of runtimes?
  bool isNeXTFamily() const {
    // For now, this is just the inverse of isGNUFamily(), but that's
    // not inherently true.
    return !isGNUFamily();
  }

  /// Does this runtime allow ARC at all?
  bool allowsARC() const {
    switch (getKind()) {
    case FragileMacOSX:
      // No stub library for the fragile runtime.
      return getVersion() >= VersionTuple(10, 7);
    case MacOSX: return true;
    case iOS: return true;
    case WatchOS: return true;
    case GCC: return false;
    case GNUstep: return true;
    case ObjFW: return true;
    }
    llvm_unreachable("bad kind");
  }

  /// Does this runtime natively provide the ARC entrypoints?
  ///
  /// ARC cannot be directly supported on a platform that does not provide
  /// these entrypoints, although it may be supportable via a stub
  /// library.
  bool hasNativeARC() const {
    switch (getKind()) {
    case FragileMacOSX: return getVersion() >= VersionTuple(10, 7);
    case MacOSX: return getVersion() >= VersionTuple(10, 7);
    case iOS: return getVersion() >= VersionTuple(5);
    case WatchOS: return true;

    case GCC: return false;
    case GNUstep: return getVersion() >= VersionTuple(1, 6);
    case ObjFW: return true;
    }
    llvm_unreachable("bad kind");
  }

  /// Does this runtime provide ARC entrypoints that are likely to be faster
  /// than an ordinary message send of the appropriate selector?
  ///
  /// The ARC entrypoints are guaranteed to be equivalent to just sending the
  /// corresponding message.  If the entrypoint is implemented naively as just a
  /// message send, using it is a trade-off: it sacrifices a few cycles of
  /// overhead to save a small amount of code.  However, it's possible for
  /// runtimes to detect and special-case classes that use "standard"
  /// retain/release behavior; if that's dynamically a large proportion of all
  /// retained objects, using the entrypoint will also be faster than using a
  /// message send.
  ///
  /// When this method returns true, Clang will turn non-super message sends of
  /// certain selectors into calls to the correspond entrypoint:
  ///   retain => objc_retain
  ///   release => objc_release
  ///   autorelease => objc_autorelease
  bool shouldUseARCFunctionsForRetainRelease() const {
    switch (getKind()) {
    case FragileMacOSX:
      return false;
    case MacOSX:
      return getVersion() >= VersionTuple(10, 10);
    case iOS:
      return getVersion() >= VersionTuple(8);
    case WatchOS:
      return true;
    case GCC:
      return false;
    case GNUstep:
      // This could be enabled for all versions, except for the fact that the
      // implementation of `objc_retain` and friends prior to 2.2 call [object
      // retain] in their fall-back paths, which leads to infinite recursion if
      // the runtime is built with this enabled.  Since distributions typically
      // build all Objective-C things with the same compiler version and flags,
      // it's better to be conservative here.
      return (getVersion() >= VersionTuple(2, 2));
    case ObjFW:
      return false;
    }
    llvm_unreachable("bad kind");
  }

  /// Does this runtime provide entrypoints that are likely to be faster
  /// than an ordinary message send of the "alloc" selector?
  ///
  /// The "alloc" entrypoint is guaranteed to be equivalent to just sending the
  /// corresponding message.  If the entrypoint is implemented naively as just a
  /// message send, using it is a trade-off: it sacrifices a few cycles of
  /// overhead to save a small amount of code.  However, it's possible for
  /// runtimes to detect and special-case classes that use "standard"
  /// alloc behavior; if that's dynamically a large proportion of all
  /// objects, using the entrypoint will also be faster than using a message
  /// send.
  ///
  /// When this method returns true, Clang will turn non-super message sends of
  /// certain selectors into calls to the corresponding entrypoint:
  ///   alloc => objc_alloc
  ///   allocWithZone:nil => objc_allocWithZone
  bool shouldUseRuntimeFunctionsForAlloc() const {
    switch (getKind()) {
    case FragileMacOSX:
      return false;
    case MacOSX:
      return getVersion() >= VersionTuple(10, 10);
    case iOS:
      return getVersion() >= VersionTuple(8);
    case WatchOS:
      return true;

    case GCC:
      return false;
    case GNUstep:
      return getVersion() >= VersionTuple(2, 2);
    case ObjFW:
      return false;
    }
    llvm_unreachable("bad kind");
  }

  /// Does this runtime provide the objc_alloc_init entrypoint? This can apply
  /// the same optimization as objc_alloc, but also sends an -init message,
  /// reducing code size on the caller.
  bool shouldUseRuntimeFunctionForCombinedAllocInit() const {
    switch (getKind()) {
    case MacOSX:
      return getVersion() >= VersionTuple(10, 14, 4);
    case iOS:
      return getVersion() >= VersionTuple(12, 2);
    case WatchOS:
      return getVersion() >= VersionTuple(5, 2);
    case GNUstep:
      return getVersion() >= VersionTuple(2, 2);
    default:
      return false;
    }
  }

  /// Does this runtime supports optimized setter entrypoints?
  bool hasOptimizedSetter() const {
    switch (getKind()) {
      case MacOSX:
        return getVersion() >= VersionTuple(10, 8);
      case iOS:
        return (getVersion() >= VersionTuple(6));
      case WatchOS:
        return true;
      case GNUstep:
        return getVersion() >= VersionTuple(1, 7);
      default:
        return false;
    }
  }

  /// Does this runtime allow the use of __weak?
  bool allowsWeak() const {
    return hasNativeWeak();
  }

  /// Does this runtime natively provide ARC-compliant 'weak'
  /// entrypoints?
  bool hasNativeWeak() const {
    // Right now, this is always equivalent to whether the runtime
    // natively supports ARC decision.
    return hasNativeARC();
  }

  /// Does this runtime directly support the subscripting methods?
  ///
  /// This is really a property of the library, not the runtime.
  bool hasSubscripting() const {
    switch (getKind()) {
    case FragileMacOSX: return false;
    case MacOSX: return getVersion() >= VersionTuple(10, 11);
    case iOS: return getVersion() >= VersionTuple(9);
    case WatchOS: return true;

    // This is really a lie, because some implementations and versions
    // of the runtime do not support ARC.  Probably -fgnu-runtime
    // should imply a "maximal" runtime or something?
    case GCC: return true;
    case GNUstep: return true;
    case ObjFW: return true;
    }
    llvm_unreachable("bad kind");
  }

  /// Does this runtime allow sizeof or alignof on object types?
  bool allowsSizeofAlignof() const {
    return isFragile();
  }

  /// Does this runtime allow pointer arithmetic on objects?
  ///
  /// This covers +, -, ++, --, and (if isSubscriptPointerArithmetic()
  /// yields true) [].
  bool allowsPointerArithmetic() const {
    switch (getKind()) {
    case FragileMacOSX:
    case GCC:
      return true;
    case MacOSX:
    case iOS:
    case WatchOS:
    case GNUstep:
    case ObjFW:
      return false;
    }
    llvm_unreachable("bad kind");
  }

  /// Is subscripting pointer arithmetic?
  bool isSubscriptPointerArithmetic() const {
    return allowsPointerArithmetic();
  }

  /// Does this runtime provide an objc_terminate function?
  ///
  /// This is used in handlers for exceptions during the unwind process;
  /// without it, abort() must be used in pure ObjC files.
  bool hasTerminate() const {
    switch (getKind()) {
    case FragileMacOSX: return getVersion() >= VersionTuple(10, 8);
    case MacOSX: return getVersion() >= VersionTuple(10, 8);
    case iOS: return getVersion() >= VersionTuple(5);
    case WatchOS: return true;
    case GCC: return false;
    case GNUstep: return false;
    case ObjFW: return false;
    }
    llvm_unreachable("bad kind");
  }

  /// Does this runtime support weakly importing classes?
  bool hasWeakClassImport() const {
    switch (getKind()) {
    case MacOSX: return true;
    case iOS: return true;
    case WatchOS: return true;
    case FragileMacOSX: return false;
    case GCC: return true;
    case GNUstep: return true;
    case ObjFW: return true;
    }
    llvm_unreachable("bad kind");
  }

  /// Does this runtime use zero-cost exceptions?
  bool hasUnwindExceptions() const {
    switch (getKind()) {
    case MacOSX: return true;
    case iOS: return true;
    case WatchOS: return true;
    case FragileMacOSX: return false;
    case GCC: return true;
    case GNUstep: return true;
    case ObjFW: return true;
    }
    llvm_unreachable("bad kind");
  }

  bool hasAtomicCopyHelper() const {
    switch (getKind()) {
    case FragileMacOSX:
    case MacOSX:
    case iOS:
    case WatchOS:
      return true;
    case GNUstep:
      return getVersion() >= VersionTuple(1, 7);
    default: return false;
    }
  }

  /// Is objc_unsafeClaimAutoreleasedReturnValue available?
  bool hasARCUnsafeClaimAutoreleasedReturnValue() const {
    switch (getKind()) {
    case MacOSX:
    case FragileMacOSX:
      return getVersion() >= VersionTuple(10, 11);
    case iOS:
      return getVersion() >= VersionTuple(9);
    case WatchOS:
      return getVersion() >= VersionTuple(2);
    case GNUstep:
      return false;
    default:
      return false;
    }
  }

  /// Are the empty collection symbols available?
  bool hasEmptyCollections() const {
    switch (getKind()) {
    default:
      return false;
    case MacOSX:
      return getVersion() >= VersionTuple(10, 11);
    case iOS:
      return getVersion() >= VersionTuple(9);
    case WatchOS:
      return getVersion() >= VersionTuple(2);
    }
  }

  /// Returns true if this Objective-C runtime supports Objective-C class
  /// stubs.
  bool allowsClassStubs() const {
    switch (getKind()) {
    case FragileMacOSX:
    case GCC:
    case GNUstep:
    case ObjFW:
      return false;
    case MacOSX:
    case iOS:
    case WatchOS:
      return true;
    }
    llvm_unreachable("bad kind");
  }

  /// Does this runtime supports direct dispatch
  bool allowsDirectDispatch() const {
    switch (getKind()) {
    case FragileMacOSX: return false;
    case MacOSX: return true;
    case iOS: return true;
    case WatchOS: return true;
    case GCC: return false;
    case GNUstep:
      return (getVersion() >= VersionTuple(2, 2));
    case ObjFW: return false;
    }
    llvm_unreachable("bad kind");
  }

  /// Try to parse an Objective-C runtime specification from the given
  /// string.
  ///
  /// \return true on error.
  bool tryParse(StringRef input);

  std::string getAsString() const;

  friend bool operator==(const ObjCRuntime &left, const ObjCRuntime &right) {
    return left.getKind() == right.getKind() &&
           left.getVersion() == right.getVersion();
  }

  friend bool operator!=(const ObjCRuntime &left, const ObjCRuntime &right) {
    return !(left == right);
  }

  friend llvm::hash_code hash_value(const ObjCRuntime &OCR) {
    return llvm::hash_combine(OCR.getKind(), OCR.getVersion());
  }

  template <typename HasherT, llvm::endianness Endianness>
  friend void addHash(llvm::HashBuilder<HasherT, Endianness> &HBuilder,
                      const ObjCRuntime &OCR) {
    HBuilder.add(OCR.getKind(), OCR.getVersion());
  }
};

raw_ostream &operator<<(raw_ostream &out, const ObjCRuntime &value);

} // namespace clang

#endif // LLVM_CLANG_BASIC_OBJCRUNTIME_H
