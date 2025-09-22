//===-- AArch64TargetParser - Parser for AArch64 features -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise AArch64 hardware features
// such as FPU/CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_AARCH64TARGETPARSER_H
#define LLVM_TARGETPARSER_AARCH64TARGETPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Bitset.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <array>
#include <set>
#include <vector>

namespace llvm {

class Triple;

namespace AArch64 {

struct ArchInfo;
struct CpuInfo;

#include "llvm/TargetParser/AArch64CPUFeatures.inc"

static_assert(FEAT_MAX < 62,
              "Number of features in CPUFeatures are limited to 62 entries");

// Each ArchExtKind correponds directly to a possible -target-feature.
#define EMIT_ARCHEXTKIND_ENUM
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

using ExtensionBitset = Bitset<AEK_NUM_EXTENSIONS>;

// Represents an extension that can be enabled with -march=<arch>+<extension>.
// Typically these correspond to Arm Architecture extensions, unlike
// SubtargetFeature which may represent either an actual extension or some
// internal LLVM property.
struct ExtensionInfo {
  StringRef UserVisibleName;      // Human readable name used in -march, -cpu
                                  // and target func attribute, e.g. "profile".
  std::optional<StringRef> Alias; // An alias for this extension, if one exists.
  ArchExtKind ID;                 // Corresponding to the ArchExtKind, this
                                  // extensions representation in the bitfield.
  StringRef ArchFeatureName;      // The feature name defined by the
                                  // Architecture, e.g. FEAT_AdvSIMD.
  StringRef Description;          // The textual description of the extension.
  StringRef PosTargetFeature;     // -target-feature/-mattr enable string,
                                  // e.g. "+spe".
  StringRef NegTargetFeature;     // -target-feature/-mattr disable string,
                                  // e.g. "-spe".
};

#define EMIT_EXTENSIONS
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

struct FMVInfo {
  StringRef Name;     // The target_version/target_clones spelling.
  CPUFeatures Bit;    // Index of the bit in the FMV feature bitset.
  StringRef Features; // List of SubtargetFeatures to enable.
  unsigned Priority;  // FMV priority.
  FMVInfo(StringRef Name, CPUFeatures Bit, StringRef Features,
          unsigned Priority)
      : Name(Name), Bit(Bit), Features(Features), Priority(Priority){};

  SmallVector<StringRef, 8> getImpliedFeatures() {
    SmallVector<StringRef, 8> Feats;
    Features.split(Feats, ',', -1, false); // discard empty strings
    return Feats;
  }
};

const std::vector<FMVInfo> &getFMVInfo();

// Represents a dependency between two architecture extensions. Later is the
// feature which was added to the architecture after Earlier, and expands the
// functionality provided by it. If Later is enabled, then Earlier will also be
// enabled. If Earlier is disabled, then Later will also be disabled.
struct ExtensionDependency {
  ArchExtKind Earlier;
  ArchExtKind Later;
};

#define EMIT_EXTENSION_DEPENDENCIES
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

enum ArchProfile { AProfile = 'A', RProfile = 'R', InvalidProfile = '?' };

// Information about a specific architecture, e.g. V8.1-A
struct ArchInfo {
  VersionTuple Version;  // Architecture version, major + minor.
  ArchProfile Profile;   // Architecuture profile
  StringRef Name;        // Name as supplied to -march e.g. "armv8.1-a"
  StringRef ArchFeature; // Name as supplied to -target-feature, e.g. "+v8a"
  AArch64::ExtensionBitset
      DefaultExts; // bitfield of default extensions ArchExtKind

  bool operator==(const ArchInfo &Other) const {
    return this->Name == Other.Name;
  }
  bool operator!=(const ArchInfo &Other) const {
    return this->Name != Other.Name;
  }

  // Defines the following partial order, indicating when an architecture is
  // a superset of another:
  //
  //   v9.5a > v9.4a > v9.3a > v9.2a > v9.1a > v9a;
  //             v       v       v       v       v
  //           v8.9a > v8.8a > v8.7a > v8.6a > v8.5a > v8.4a > ... > v8a;
  //
  // v8r has no relation to anything. This is used to determine which
  // features to enable for a given architecture. See
  // AArch64TargetInfo::setFeatureEnabled.
  bool implies(const ArchInfo &Other) const {
    if (this->Profile != Other.Profile)
      return false; // ARMV8R
    if (this->Version.getMajor() == Other.Version.getMajor()) {
      return this->Version > Other.Version;
    }
    if (this->Version.getMajor() == 9 && Other.Version.getMajor() == 8) {
      assert(this->Version.getMinor() && Other.Version.getMinor() &&
             "AArch64::ArchInfo should have a minor version.");
      return this->Version.getMinor().value_or(0) + 5 >=
             Other.Version.getMinor().value_or(0);
    }
    return false;
  }

  // True if this architecture is a superset of Other (including being equal to
  // it).
  bool is_superset(const ArchInfo &Other) const {
    return (*this == Other) || implies(Other);
  }

  // Return ArchFeature without the leading "+".
  StringRef getSubArch() const { return ArchFeature.substr(1); }

  // Search for ArchInfo by SubArch name
  static std::optional<ArchInfo> findBySubArch(StringRef SubArch);
};

#define EMIT_ARCHITECTURES
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

// Details of a specific CPU.
struct CpuInfo {
  StringRef Name; // Name, as written for -mcpu.
  const ArchInfo &Arch;
  AArch64::ExtensionBitset
      DefaultExtensions; // Default extensions for this CPU.

  AArch64::ExtensionBitset getImpliedExtensions() const {
    return DefaultExtensions;
  }
};

#define EMIT_CPU_INFO
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

struct ExtensionSet {
  // Set of extensions which are currently enabled.
  ExtensionBitset Enabled;
  // Set of extensions which have been enabled or disabled at any point. Used
  // to avoid cluttering the cc1 command-line with lots of unneeded features.
  ExtensionBitset Touched;
  // Base architecture version, which we need to know because some feature
  // dependencies change depending on this.
  const ArchInfo *BaseArch;

  ExtensionSet() : Enabled(), Touched(), BaseArch(nullptr) {}

  // Enable the given architecture extension, and any other extensions it
  // depends on. Does not change the base architecture, or follow dependencies
  // between features which are only related by required arcitecture versions.
  void enable(ArchExtKind E);

  // Disable the given architecture extension, and any other extensions which
  // depend on it. Does not change the base architecture, or follow
  // dependencies between features which are only related by required
  // arcitecture versions.
  void disable(ArchExtKind E);

  // Add default extensions for the given CPU. Records the base architecture,
  // to later resolve dependencies which depend on it.
  void addCPUDefaults(const CpuInfo &CPU);

  // Add default extensions for the given architecture version. Records the
  // base architecture, to later resolve dependencies which depend on it.
  void addArchDefaults(const ArchInfo &Arch);

  // Add or remove a feature based on a modifier string. The string must be of
  // the form "<name>" to enable a feature or "no<name>" to disable it. This
  // will also enable or disable any features as required by the dependencies
  // between them.
  bool parseModifier(StringRef Modifier, const bool AllowNoDashForm = false);

  // Constructs a new ExtensionSet by toggling the corresponding bits for every
  // feature in the \p Features list without expanding their dependencies. Used
  // for reconstructing an ExtensionSet from the output of toLLVMFeatures().
  // Features that are not recognized are pushed back to \p NonExtensions.
  void reconstructFromParsedFeatures(const std::vector<std::string> &Features,
                                     std::vector<std::string> &NonExtensions);

  // Convert the set of enabled extension to an LLVM feature list, appending
  // them to Features.
  template <typename T> void toLLVMFeatureList(std::vector<T> &Features) const {
    if (BaseArch && !BaseArch->ArchFeature.empty())
      Features.emplace_back(T(BaseArch->ArchFeature));

    for (const auto &E : Extensions) {
      if (E.PosTargetFeature.empty() || !Touched.test(E.ID))
        continue;
      if (Enabled.test(E.ID))
        Features.emplace_back(T(E.PosTargetFeature));
      else
        Features.emplace_back(T(E.NegTargetFeature));
    }
  }

  void dump() const;
};

// Name alias.
struct Alias {
  StringRef AltName;
  StringRef Name;
};

#define EMIT_CPU_ALIAS
#include "llvm/TargetParser/AArch64TargetParserDef.inc"

const ExtensionInfo &getExtensionByID(ArchExtKind(ExtID));

bool getExtensionFeatures(
    const AArch64::ExtensionBitset &Extensions,
    std::vector<StringRef> &Features);

StringRef getArchExtFeature(StringRef ArchExt);
StringRef resolveCPUAlias(StringRef CPU);

// Information by Name
const ArchInfo *getArchForCpu(StringRef CPU);

// Parser
const ArchInfo *parseArch(StringRef Arch);

// Return the extension which has the given -target-feature name.
std::optional<ExtensionInfo> targetFeatureToExtension(StringRef TargetFeature);

// Parse a name as defined by the Extension class in tablegen.
std::optional<ExtensionInfo> parseArchExtension(StringRef Extension);

// Parse a name as defined by the FMVInfo class in tablegen.
std::optional<FMVInfo> parseFMVExtension(StringRef Extension);

// Given the name of a CPU or alias, return the correponding CpuInfo.
std::optional<CpuInfo> parseCpu(StringRef Name);
// Used by target parser tests
void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values);

bool isX18ReservedByDefault(const Triple &TT);

// For given feature names, return a bitmask corresponding to the entries of
// AArch64::CPUFeatures. The values in CPUFeatures are not bitmasks
// themselves, they are sequential (0, 1, 2, 3, ...).
uint64_t getCpuSupportsMask(ArrayRef<StringRef> FeatureStrs);

void PrintSupportedExtensions();

void printEnabledExtensions(const std::set<StringRef> &EnabledFeatureNames);

} // namespace AArch64
} // namespace llvm

#endif
