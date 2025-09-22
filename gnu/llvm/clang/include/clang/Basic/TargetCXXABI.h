//===--- TargetCXXABI.h - C++ ABI Target Configuration ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the TargetCXXABI class, which abstracts details of the
/// C++ ABI that we're targeting.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_TARGETCXXABI_H
#define LLVM_CLANG_BASIC_TARGETCXXABI_H

#include <map>

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {

/// The basic abstraction for the target C++ ABI.
class TargetCXXABI {
public:
  /// The basic C++ ABI kind.
  enum Kind {
#define CXXABI(Name, Str) Name,
#include "TargetCXXABI.def"
  };

private:
  // Right now, this class is passed around as a cheap value type.
  // If you add more members, especially non-POD members, please
  // audit the users to pass it by reference instead.
  Kind TheKind;

  static const auto &getABIMap() {
    static llvm::StringMap<Kind> ABIMap = {
#define CXXABI(Name, Str) {Str, Name},
#include "TargetCXXABI.def"
    };
    return ABIMap;
  }

  static const auto &getSpellingMap() {
    static std::map<Kind, std::string> SpellingMap = {
#define CXXABI(Name, Str) {Name, Str},
#include "TargetCXXABI.def"
    };
    return SpellingMap;
  }

public:
  static Kind getKind(StringRef Name) { return getABIMap().lookup(Name); }
  static const auto &getSpelling(Kind ABIKind) {
    return getSpellingMap().find(ABIKind)->second;
  }
  static bool isABI(StringRef Name) { return getABIMap().contains(Name); }

  // Return true if this target should use the relative vtables C++ ABI by
  // default.
  static bool usesRelativeVTables(const llvm::Triple &T) {
    return T.isOSFuchsia();
  }

  /// A bogus initialization of the platform ABI.
  TargetCXXABI() : TheKind(GenericItanium) {}

  TargetCXXABI(Kind kind) : TheKind(kind) {}

  void set(Kind kind) {
    TheKind = kind;
  }

  Kind getKind() const { return TheKind; }

  // Check that the kind provided by the fc++-abi flag is supported on this
  // target. Users who want to experiment using different ABIs on specific
  // platforms can change this freely, but this function should be conservative
  // enough such that not all ABIs are allowed on all platforms. For example, we
  // probably don't want to allow usage of an ARM ABI on an x86 architecture.
  static bool isSupportedCXXABI(const llvm::Triple &T, Kind Kind) {
    switch (Kind) {
    case GenericARM:
      return T.isARM() || T.isAArch64();

    case iOS:
    case WatchOS:
    case AppleARM64:
      return T.isOSDarwin();

    case Fuchsia:
      return T.isOSFuchsia();

    case GenericAArch64:
      return T.isAArch64();

    case GenericMIPS:
      return T.isMIPS();

    case WebAssembly:
      return T.isWasm();

    case XL:
      return T.isOSAIX();

    case GenericItanium:
      return true;

    case Microsoft:
      return T.isKnownWindowsMSVCEnvironment();
    }
    llvm_unreachable("invalid CXXABI kind");
  }

  /// Does this ABI generally fall into the Itanium family of ABIs?
  bool isItaniumFamily() const {
    switch (getKind()) {
#define CXXABI(Name, Str)
#define ITANIUM_CXXABI(Name, Str) case Name:
#include "TargetCXXABI.def"
      return true;

    default:
      return false;
    }
    llvm_unreachable("bad ABI kind");
  }

  /// Is this ABI an MSVC-compatible ABI?
  bool isMicrosoft() const {
    switch (getKind()) {
#define CXXABI(Name, Str)
#define MICROSOFT_CXXABI(Name, Str) case Name:
#include "TargetCXXABI.def"
      return true;

    default:
      return false;
    }
    llvm_unreachable("bad ABI kind");
  }

  /// Are member functions differently aligned?
  ///
  /// Many Itanium-style C++ ABIs require member functions to be aligned, so
  /// that a pointer to such a function is guaranteed to have a zero in the
  /// least significant bit, so that pointers to member functions can use that
  /// bit to distinguish between virtual and non-virtual functions. However,
  /// some Itanium-style C++ ABIs differentiate between virtual and non-virtual
  /// functions via other means, and consequently don't require that member
  /// functions be aligned.
  bool areMemberFunctionsAligned() const {
    switch (getKind()) {
    case WebAssembly:
      // WebAssembly doesn't require any special alignment for member functions.
      return false;
    case AppleARM64:
    case Fuchsia:
    case GenericARM:
    case GenericAArch64:
    case GenericMIPS:
      // TODO: ARM-style pointers to member functions put the discriminator in
      //       the this adjustment, so they don't require functions to have any
      //       special alignment and could therefore also return false.
    case GenericItanium:
    case iOS:
    case WatchOS:
    case Microsoft:
    case XL:
      return true;
    }
    llvm_unreachable("bad ABI kind");
  }

  /// Are arguments to a call destroyed left to right in the callee?
  /// This is a fundamental language change, since it implies that objects
  /// passed by value do *not* live to the end of the full expression.
  /// Temporaries passed to a function taking a const reference live to the end
  /// of the full expression as usual.  Both the caller and the callee must
  /// have access to the destructor, while only the caller needs the
  /// destructor if this is false.
  bool areArgsDestroyedLeftToRightInCallee() const {
    return isMicrosoft();
  }

  /// Does this ABI have different entrypoints for complete-object
  /// and base-subobject constructors?
  bool hasConstructorVariants() const {
    return isItaniumFamily();
  }

  /// Does this ABI allow virtual bases to be primary base classes?
  bool hasPrimaryVBases() const {
    return isItaniumFamily();
  }

  /// Does this ABI use key functions?  If so, class data such as the
  /// vtable is emitted with strong linkage by the TU containing the key
  /// function.
  bool hasKeyFunctions() const {
    return isItaniumFamily();
  }

  /// Can an out-of-line inline function serve as a key function?
  ///
  /// This flag is only useful in ABIs where type data (for example,
  /// vtables and type_info objects) are emitted only after processing
  /// the definition of a special "key" virtual function.  (This is safe
  /// because the ODR requires that every virtual function be defined
  /// somewhere in a program.)  This usually permits such data to be
  /// emitted in only a single object file, as opposed to redundantly
  /// in every object file that requires it.
  ///
  /// One simple and common definition of "key function" is the first
  /// virtual function in the class definition which is not defined there.
  /// This rule works very well when that function has a non-inline
  /// definition in some non-header file.  Unfortunately, when that
  /// function is defined inline, this rule requires the type data
  /// to be emitted weakly, as if there were no key function.
  ///
  /// The ARM ABI observes that the ODR provides an additional guarantee:
  /// a virtual function is always ODR-used, so if it is defined inline,
  /// that definition must appear in every translation unit that defines
  /// the class.  Therefore, there is no reason to allow such functions
  /// to serve as key functions.
  ///
  /// Because this changes the rules for emitting type data,
  /// it can cause type data to be emitted with both weak and strong
  /// linkage, which is not allowed on all platforms.  Therefore,
  /// exploiting this observation requires an ABI break and cannot be
  /// done on a generic Itanium platform.
  bool canKeyFunctionBeInline() const {
    switch (getKind()) {
    case AppleARM64:
    case Fuchsia:
    case GenericARM:
    case WebAssembly:
    case WatchOS:
      return false;

    case GenericAArch64:
    case GenericItanium:
    case iOS:   // old iOS compilers did not follow this rule
    case Microsoft:
    case GenericMIPS:
    case XL:
      return true;
    }
    llvm_unreachable("bad ABI kind");
  }

  /// When is record layout allowed to allocate objects in the tail
  /// padding of a base class?
  ///
  /// This decision cannot be changed without breaking platform ABI
  /// compatibility. In ISO C++98, tail padding reuse was only permitted for
  /// non-POD base classes, but that restriction was removed retroactively by
  /// DR 43, and tail padding reuse is always permitted in all de facto C++
  /// language modes. However, many platforms use a variant of the old C++98
  /// rule for compatibility.
  enum TailPaddingUseRules {
    /// The tail-padding of a base class is always theoretically
    /// available, even if it's POD.
    AlwaysUseTailPadding,

    /// Only allocate objects in the tail padding of a base class if
    /// the base class is not POD according to the rules of C++ TR1.
    UseTailPaddingUnlessPOD03,

    /// Only allocate objects in the tail padding of a base class if
    /// the base class is not POD according to the rules of C++11.
    UseTailPaddingUnlessPOD11
  };
  TailPaddingUseRules getTailPaddingUseRules() const {
    switch (getKind()) {
    // To preserve binary compatibility, the generic Itanium ABI has
    // permanently locked the definition of POD to the rules of C++ TR1,
    // and that trickles down to derived ABIs.
    case GenericItanium:
    case GenericAArch64:
    case GenericARM:
    case iOS:
    case GenericMIPS:
    case XL:
      return UseTailPaddingUnlessPOD03;

    // AppleARM64 and WebAssembly use the C++11 POD rules.  They do not honor
    // the Itanium exception about classes with over-large bitfields.
    case AppleARM64:
    case Fuchsia:
    case WebAssembly:
    case WatchOS:
      return UseTailPaddingUnlessPOD11;

    // MSVC always allocates fields in the tail-padding of a base class
    // subobject, even if they're POD.
    case Microsoft:
      return AlwaysUseTailPadding;
    }
    llvm_unreachable("bad ABI kind");
  }

  friend bool operator==(const TargetCXXABI &left, const TargetCXXABI &right) {
    return left.getKind() == right.getKind();
  }

  friend bool operator!=(const TargetCXXABI &left, const TargetCXXABI &right) {
    return !(left == right);
  }
};

}  // end namespace clang

#endif
