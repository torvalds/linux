//===--- Availability.h - Classes for availability --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This files defines some classes that implement availability checking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_AVAILABILITY_H
#define LLVM_CLANG_AST_AVAILABILITY_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"

namespace clang {

/// One specifier in an @available expression.
///
/// \code
///   @available(macos 10.10, *)
/// \endcode
///
/// Here, 'macos 10.10' and '*' both map to an instance of this type.
///
class AvailabilitySpec {
  /// Represents the version that this specifier requires. If the host OS
  /// version is greater than or equal to Version, the @available will evaluate
  /// to true.
  VersionTuple Version;

  /// Name of the platform that Version corresponds to.
  StringRef Platform;

  SourceLocation BeginLoc, EndLoc;

public:
  AvailabilitySpec(VersionTuple Version, StringRef Platform,
                   SourceLocation BeginLoc, SourceLocation EndLoc)
      : Version(Version), Platform(Platform), BeginLoc(BeginLoc),
        EndLoc(EndLoc) {}

  /// This constructor is used when representing the '*' case.
  AvailabilitySpec(SourceLocation StarLoc)
      : BeginLoc(StarLoc), EndLoc(StarLoc) {}

  VersionTuple getVersion() const { return Version; }
  StringRef getPlatform() const { return Platform; }
  SourceLocation getBeginLoc() const { return BeginLoc; }
  SourceLocation getEndLoc() const { return EndLoc; }

  /// Returns true when this represents the '*' case.
  bool isOtherPlatformSpec() const { return Version.empty(); }
};

class Decl;

/// Storage of availability attributes for a declaration.
struct AvailabilityInfo {
  /// The domain is the platform for which this availability info applies to.
  llvm::SmallString<32> Domain;
  VersionTuple Introduced;
  VersionTuple Deprecated;
  VersionTuple Obsoleted;
  bool Unavailable = false;
  bool UnconditionallyDeprecated = false;
  bool UnconditionallyUnavailable = false;

  AvailabilityInfo() = default;

  /// Determine if this AvailabilityInfo represents the default availability.
  bool isDefault() const { return *this == AvailabilityInfo(); }

  /// Check if the symbol has been obsoleted.
  bool isObsoleted() const { return !Obsoleted.empty(); }

  /// Check if the symbol is unavailable unconditionally or
  /// on the active platform and os version.
  bool isUnavailable() const {
    return Unavailable || isUnconditionallyUnavailable();
  }

  /// Check if the symbol is unconditionally deprecated.
  ///
  /// i.e. \code __attribute__((deprecated)) \endcode
  bool isUnconditionallyDeprecated() const { return UnconditionallyDeprecated; }

  /// Check if the symbol is unconditionally unavailable.
  ///
  /// i.e. \code __attribute__((unavailable)) \endcode
  bool isUnconditionallyUnavailable() const {
    return UnconditionallyUnavailable;
  }

  AvailabilityInfo(StringRef Domain, VersionTuple I, VersionTuple D,
                   VersionTuple O, bool U, bool UD, bool UU)
      : Domain(Domain), Introduced(I), Deprecated(D), Obsoleted(O),
        Unavailable(U), UnconditionallyDeprecated(UD),
        UnconditionallyUnavailable(UU) {}

  friend bool operator==(const AvailabilityInfo &Lhs,
                         const AvailabilityInfo &Rhs);

public:
  static AvailabilityInfo createFromDecl(const Decl *Decl);
};

inline bool operator==(const AvailabilityInfo &Lhs,
                       const AvailabilityInfo &Rhs) {
  return std::tie(Lhs.Introduced, Lhs.Deprecated, Lhs.Obsoleted,
                  Lhs.Unavailable, Lhs.UnconditionallyDeprecated,
                  Lhs.UnconditionallyUnavailable) ==
         std::tie(Rhs.Introduced, Rhs.Deprecated, Rhs.Obsoleted,
                  Rhs.Unavailable, Rhs.UnconditionallyDeprecated,
                  Rhs.UnconditionallyUnavailable);
}

} // end namespace clang

#endif
