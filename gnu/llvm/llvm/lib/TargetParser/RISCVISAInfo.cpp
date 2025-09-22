//===-- RISCVISAInfo.cpp - RISC-V Arch String Parser ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/RISCVISAInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

using namespace llvm;

namespace {

struct RISCVSupportedExtension {
  const char *Name;
  /// Supported version.
  RISCVISAUtils::ExtensionVersion Version;

  bool operator<(const RISCVSupportedExtension &RHS) const {
    return StringRef(Name) < StringRef(RHS.Name);
  }
};

struct RISCVProfile {
  StringLiteral Name;
  StringLiteral MArch;

  bool operator<(const RISCVProfile &RHS) const {
    return StringRef(Name) < StringRef(RHS.Name);
  }
};

} // end anonymous namespace

static const char *RISCVGImplications[] = {
  "i", "m", "a", "f", "d", "zicsr", "zifencei"
};

#define GET_SUPPORTED_EXTENSIONS
#include "llvm/TargetParser/RISCVTargetParserDef.inc"

#define GET_SUPPORTED_PROFILES
#include "llvm/TargetParser/RISCVTargetParserDef.inc"

static void verifyTables() {
#ifndef NDEBUG
  static std::atomic<bool> TableChecked(false);
  if (!TableChecked.load(std::memory_order_relaxed)) {
    assert(llvm::is_sorted(SupportedExtensions) &&
           "Extensions are not sorted by name");
    assert(llvm::is_sorted(SupportedExperimentalExtensions) &&
           "Experimental extensions are not sorted by name");
    assert(llvm::is_sorted(SupportedProfiles) &&
           "Profiles are not sorted by name");
    assert(llvm::is_sorted(SupportedExperimentalProfiles) &&
           "Experimental profiles are not sorted by name");
    TableChecked.store(true, std::memory_order_relaxed);
  }
#endif
}

static void PrintExtension(StringRef Name, StringRef Version,
                           StringRef Description) {
  outs().indent(4);
  unsigned VersionWidth = Description.empty() ? 0 : 10;
  outs() << left_justify(Name, 21) << left_justify(Version, VersionWidth)
         << Description << "\n";
}

void RISCVISAInfo::printSupportedExtensions(StringMap<StringRef> &DescMap) {
  outs() << "All available -march extensions for RISC-V\n\n";
  PrintExtension("Name", "Version", (DescMap.empty() ? "" : "Description"));

  RISCVISAUtils::OrderedExtensionMap ExtMap;
  for (const auto &E : SupportedExtensions)
    ExtMap[E.Name] = {E.Version.Major, E.Version.Minor};
  for (const auto &E : ExtMap) {
    std::string Version =
        std::to_string(E.second.Major) + "." + std::to_string(E.second.Minor);
    PrintExtension(E.first, Version, DescMap[E.first]);
  }

  outs() << "\nExperimental extensions\n";
  ExtMap.clear();
  for (const auto &E : SupportedExperimentalExtensions)
    ExtMap[E.Name] = {E.Version.Major, E.Version.Minor};
  for (const auto &E : ExtMap) {
    std::string Version =
        std::to_string(E.second.Major) + "." + std::to_string(E.second.Minor);
    PrintExtension(E.first, Version, DescMap["experimental-" + E.first]);
  }

  outs() << "\nSupported Profiles\n";
  for (const auto &P : SupportedProfiles)
    outs().indent(4) << P.Name << "\n";

  outs() << "\nExperimental Profiles\n";
  for (const auto &P : SupportedExperimentalProfiles)
    outs().indent(4) << P.Name << "\n";

  outs() << "\nUse -march to specify the target's extension.\n"
            "For example, clang -march=rv32i_v1p0\n";
}

void RISCVISAInfo::printEnabledExtensions(
    bool IsRV64, std::set<StringRef> &EnabledFeatureNames,
    StringMap<StringRef> &DescMap) {
  outs() << "Extensions enabled for the given RISC-V target\n\n";
  PrintExtension("Name", "Version", (DescMap.empty() ? "" : "Description"));

  RISCVISAUtils::OrderedExtensionMap FullExtMap;
  RISCVISAUtils::OrderedExtensionMap ExtMap;
  for (const auto &E : SupportedExtensions)
    if (EnabledFeatureNames.count(E.Name) != 0) {
      FullExtMap[E.Name] = {E.Version.Major, E.Version.Minor};
      ExtMap[E.Name] = {E.Version.Major, E.Version.Minor};
    }
  for (const auto &E : ExtMap) {
    std::string Version =
        std::to_string(E.second.Major) + "." + std::to_string(E.second.Minor);
    PrintExtension(E.first, Version, DescMap[E.first]);
  }

  outs() << "\nExperimental extensions\n";
  ExtMap.clear();
  for (const auto &E : SupportedExperimentalExtensions) {
    StringRef Name(E.Name);
    if (EnabledFeatureNames.count("experimental-" + Name.str()) != 0) {
      FullExtMap[E.Name] = {E.Version.Major, E.Version.Minor};
      ExtMap[E.Name] = {E.Version.Major, E.Version.Minor};
    }
  }
  for (const auto &E : ExtMap) {
    std::string Version =
        std::to_string(E.second.Major) + "." + std::to_string(E.second.Minor);
    PrintExtension(E.first, Version, DescMap["experimental-" + E.first]);
  }

  unsigned XLen = IsRV64 ? 64 : 32;
  if (auto ISAString = RISCVISAInfo::createFromExtMap(XLen, FullExtMap))
    outs() << "\nISA String: " << ISAString.get()->toString() << "\n";
}

static bool stripExperimentalPrefix(StringRef &Ext) {
  return Ext.consume_front("experimental-");
}

// This function finds the last character that doesn't belong to a version
// (e.g. zba1p0 is extension 'zba' of version '1p0'). So the function will
// consume [0-9]*p[0-9]* starting from the backward. An extension name will not
// end with a digit or the letter 'p', so this function will parse correctly.
// NOTE: This function is NOT able to take empty strings or strings that only
// have version numbers and no extension name. It assumes the extension name
// will be at least more than one character.
static size_t findLastNonVersionCharacter(StringRef Ext) {
  assert(!Ext.empty() &&
         "Already guarded by if-statement in ::parseArchString");

  int Pos = Ext.size() - 1;
  while (Pos > 0 && isDigit(Ext[Pos]))
    Pos--;
  if (Pos > 0 && Ext[Pos] == 'p' && isDigit(Ext[Pos - 1])) {
    Pos--;
    while (Pos > 0 && isDigit(Ext[Pos]))
      Pos--;
  }
  return Pos;
}

namespace {
struct LessExtName {
  bool operator()(const RISCVSupportedExtension &LHS, StringRef RHS) {
    return StringRef(LHS.Name) < RHS;
  }
  bool operator()(StringRef LHS, const RISCVSupportedExtension &RHS) {
    return LHS < StringRef(RHS.Name);
  }
};
} // namespace

static std::optional<RISCVISAUtils::ExtensionVersion>
findDefaultVersion(StringRef ExtName) {
  // Find default version of an extension.
  // TODO: We might set default version based on profile or ISA spec.
  for (auto &ExtInfo : {ArrayRef(SupportedExtensions),
                        ArrayRef(SupportedExperimentalExtensions)}) {
    auto I = llvm::lower_bound(ExtInfo, ExtName, LessExtName());

    if (I == ExtInfo.end() || I->Name != ExtName)
      continue;

    return I->Version;
  }
  return std::nullopt;
}

static StringRef getExtensionTypeDesc(StringRef Ext) {
  if (Ext.starts_with('s'))
    return "standard supervisor-level extension";
  if (Ext.starts_with('x'))
    return "non-standard user-level extension";
  if (Ext.starts_with('z'))
    return "standard user-level extension";
  return StringRef();
}

static StringRef getExtensionType(StringRef Ext) {
  if (Ext.starts_with('s'))
    return "s";
  if (Ext.starts_with('x'))
    return "x";
  if (Ext.starts_with('z'))
    return "z";
  return StringRef();
}

static std::optional<RISCVISAUtils::ExtensionVersion>
isExperimentalExtension(StringRef Ext) {
  auto I =
      llvm::lower_bound(SupportedExperimentalExtensions, Ext, LessExtName());
  if (I == std::end(SupportedExperimentalExtensions) || I->Name != Ext)
    return std::nullopt;

  return I->Version;
}

bool RISCVISAInfo::isSupportedExtensionFeature(StringRef Ext) {
  bool IsExperimental = stripExperimentalPrefix(Ext);

  ArrayRef<RISCVSupportedExtension> ExtInfo =
      IsExperimental ? ArrayRef(SupportedExperimentalExtensions)
                     : ArrayRef(SupportedExtensions);

  auto I = llvm::lower_bound(ExtInfo, Ext, LessExtName());
  return I != ExtInfo.end() && I->Name == Ext;
}

bool RISCVISAInfo::isSupportedExtension(StringRef Ext) {
  verifyTables();

  for (auto ExtInfo : {ArrayRef(SupportedExtensions),
                       ArrayRef(SupportedExperimentalExtensions)}) {
    auto I = llvm::lower_bound(ExtInfo, Ext, LessExtName());
    if (I != ExtInfo.end() && I->Name == Ext)
      return true;
  }

  return false;
}

bool RISCVISAInfo::isSupportedExtension(StringRef Ext, unsigned MajorVersion,
                                        unsigned MinorVersion) {
  for (auto ExtInfo : {ArrayRef(SupportedExtensions),
                       ArrayRef(SupportedExperimentalExtensions)}) {
    auto Range =
        std::equal_range(ExtInfo.begin(), ExtInfo.end(), Ext, LessExtName());
    for (auto I = Range.first, E = Range.second; I != E; ++I)
      if (I->Version.Major == MajorVersion && I->Version.Minor == MinorVersion)
        return true;
  }

  return false;
}

bool RISCVISAInfo::hasExtension(StringRef Ext) const {
  stripExperimentalPrefix(Ext);

  if (!isSupportedExtension(Ext))
    return false;

  return Exts.count(Ext.str()) != 0;
}

std::vector<std::string> RISCVISAInfo::toFeatures(bool AddAllExtensions,
                                                  bool IgnoreUnknown) const {
  std::vector<std::string> Features;
  for (const auto &[ExtName, _] : Exts) {
    // i is a base instruction set, not an extension (see
    // https://github.com/riscv/riscv-isa-manual/blob/main/src/naming.adoc#base-integer-isa)
    // and is not recognized in clang -cc1
    if (ExtName == "i")
      continue;
    if (IgnoreUnknown && !isSupportedExtension(ExtName))
      continue;

    if (isExperimentalExtension(ExtName)) {
      Features.push_back((llvm::Twine("+experimental-") + ExtName).str());
    } else {
      Features.push_back((llvm::Twine("+") + ExtName).str());
    }
  }
  if (AddAllExtensions) {
    for (const RISCVSupportedExtension &Ext : SupportedExtensions) {
      if (Exts.count(Ext.Name))
        continue;
      Features.push_back((llvm::Twine("-") + Ext.Name).str());
    }

    for (const RISCVSupportedExtension &Ext : SupportedExperimentalExtensions) {
      if (Exts.count(Ext.Name))
        continue;
      Features.push_back((llvm::Twine("-experimental-") + Ext.Name).str());
    }
  }
  return Features;
}

static Error getError(const Twine &Message) {
  return createStringError(errc::invalid_argument, Message);
}

static Error getErrorForInvalidExt(StringRef ExtName) {
  if (ExtName.size() == 1) {
    return getError("unsupported standard user-level extension '" + ExtName +
                    "'");
  }
  return getError("unsupported " + getExtensionTypeDesc(ExtName) + " '" +
                  ExtName + "'");
}

// Extensions may have a version number, and may be separated by
// an underscore '_' e.g.: rv32i2_m2.
// Version number is divided into major and minor version numbers,
// separated by a 'p'. If the minor version is 0 then 'p0' can be
// omitted from the version string. E.g., rv32i2p0, rv32i2, rv32i2p1.
static Error getExtensionVersion(StringRef Ext, StringRef In, unsigned &Major,
                                 unsigned &Minor, unsigned &ConsumeLength,
                                 bool EnableExperimentalExtension,
                                 bool ExperimentalExtensionVersionCheck) {
  StringRef MajorStr, MinorStr;
  Major = 0;
  Minor = 0;
  ConsumeLength = 0;
  MajorStr = In.take_while(isDigit);
  In = In.substr(MajorStr.size());

  if (!MajorStr.empty() && In.consume_front("p")) {
    MinorStr = In.take_while(isDigit);
    In = In.substr(MajorStr.size() + MinorStr.size() - 1);

    // Expected 'p' to be followed by minor version number.
    if (MinorStr.empty()) {
      return getError("minor version number missing after 'p' for extension '" +
                      Ext + "'");
    }
  }

  if (!MajorStr.empty() && MajorStr.getAsInteger(10, Major))
    return getError("Failed to parse major version number for extension '" +
                    Ext + "'");

  if (!MinorStr.empty() && MinorStr.getAsInteger(10, Minor))
    return getError("Failed to parse minor version number for extension '" +
                    Ext + "'");

  ConsumeLength = MajorStr.size();

  if (!MinorStr.empty())
    ConsumeLength += MinorStr.size() + 1 /*'p'*/;

  // Expected multi-character extension with version number to have no
  // subsequent characters (i.e. must either end string or be followed by
  // an underscore).
  if (Ext.size() > 1 && In.size())
    return getError(
        "multi-character extensions must be separated by underscores");

  // If experimental extension, require use of current version number
  if (auto ExperimentalExtension = isExperimentalExtension(Ext)) {
    if (!EnableExperimentalExtension)
      return getError("requires '-menable-experimental-extensions' "
                      "for experimental extension '" +
                      Ext + "'");

    if (ExperimentalExtensionVersionCheck &&
        (MajorStr.empty() && MinorStr.empty()))
      return getError(
          "experimental extension requires explicit version number `" + Ext +
          "`");

    auto SupportedVers = *ExperimentalExtension;
    if (ExperimentalExtensionVersionCheck &&
        (Major != SupportedVers.Major || Minor != SupportedVers.Minor)) {
      std::string Error = "unsupported version number " + MajorStr.str();
      if (!MinorStr.empty())
        Error += "." + MinorStr.str();
      Error += " for experimental extension '" + Ext.str() +
               "' (this compiler supports " + utostr(SupportedVers.Major) +
               "." + utostr(SupportedVers.Minor) + ")";
      return getError(Error);
    }
    return Error::success();
  }

  // Exception rule for `g`, we don't have clear version scheme for that on
  // ISA spec.
  if (Ext == "g")
    return Error::success();

  if (MajorStr.empty() && MinorStr.empty()) {
    if (auto DefaultVersion = findDefaultVersion(Ext)) {
      Major = DefaultVersion->Major;
      Minor = DefaultVersion->Minor;
    }
    // No matter found or not, return success, assume other place will
    // verify.
    return Error::success();
  }

  if (RISCVISAInfo::isSupportedExtension(Ext, Major, Minor))
    return Error::success();

  if (!RISCVISAInfo::isSupportedExtension(Ext))
    return getErrorForInvalidExt(Ext);

  std::string Error = "unsupported version number " + MajorStr.str();
  if (!MinorStr.empty())
    Error += "." + MinorStr.str();
  Error += " for extension '" + Ext.str() + "'";
  return getError(Error);
}

llvm::Expected<std::unique_ptr<RISCVISAInfo>>
RISCVISAInfo::createFromExtMap(unsigned XLen,
                               const RISCVISAUtils::OrderedExtensionMap &Exts) {
  assert(XLen == 32 || XLen == 64);
  std::unique_ptr<RISCVISAInfo> ISAInfo(new RISCVISAInfo(XLen));

  ISAInfo->Exts = Exts;

  return RISCVISAInfo::postProcessAndChecking(std::move(ISAInfo));
}

llvm::Expected<std::unique_ptr<RISCVISAInfo>>
RISCVISAInfo::parseFeatures(unsigned XLen,
                            const std::vector<std::string> &Features) {
  assert(XLen == 32 || XLen == 64);
  std::unique_ptr<RISCVISAInfo> ISAInfo(new RISCVISAInfo(XLen));

  for (auto &Feature : Features) {
    StringRef ExtName = Feature;
    assert(ExtName.size() > 1 && (ExtName[0] == '+' || ExtName[0] == '-'));
    bool Add = ExtName[0] == '+';
    ExtName = ExtName.drop_front(1); // Drop '+' or '-'
    bool Experimental = stripExperimentalPrefix(ExtName);
    auto ExtensionInfos = Experimental
                              ? ArrayRef(SupportedExperimentalExtensions)
                              : ArrayRef(SupportedExtensions);
    auto ExtensionInfoIterator =
        llvm::lower_bound(ExtensionInfos, ExtName, LessExtName());

    // Not all features is related to ISA extension, like `relax` or
    // `save-restore`, skip those feature.
    if (ExtensionInfoIterator == ExtensionInfos.end() ||
        ExtensionInfoIterator->Name != ExtName)
      continue;

    if (Add)
      ISAInfo->Exts[ExtName.str()] = ExtensionInfoIterator->Version;
    else
      ISAInfo->Exts.erase(ExtName.str());
  }

  return RISCVISAInfo::postProcessAndChecking(std::move(ISAInfo));
}

llvm::Expected<std::unique_ptr<RISCVISAInfo>>
RISCVISAInfo::parseNormalizedArchString(StringRef Arch) {
  // RISC-V ISA strings must be [a-z0-9_]
  if (!llvm::all_of(
          Arch, [](char C) { return isDigit(C) || isLower(C) || C == '_'; }))
    return getError("string may only contain [a-z0-9_]");

  // Must start with a valid base ISA name.
  unsigned XLen = 0;
  if (Arch.consume_front("rv32"))
    XLen = 32;
  else if (Arch.consume_front("rv64"))
    XLen = 64;

  if (XLen == 0 || Arch.empty() || (Arch[0] != 'i' && Arch[0] != 'e'))
    return getError("arch string must begin with valid base ISA");

  std::unique_ptr<RISCVISAInfo> ISAInfo(new RISCVISAInfo(XLen));

  // Each extension is of the form ${name}${major_version}p${minor_version}
  // and separated by _. Split by _ and then extract the name and version
  // information for each extension.
  while (!Arch.empty()) {
    if (Arch[0] == '_') {
      if (Arch.size() == 1 || Arch[1] == '_')
        return getError("extension name missing after separator '_'");
      Arch = Arch.drop_front();
    }

    size_t Idx = Arch.find('_');
    StringRef Ext = Arch.slice(0, Idx);
    Arch = Arch.slice(Idx, StringRef::npos);

    StringRef Prefix, MinorVersionStr;
    std::tie(Prefix, MinorVersionStr) = Ext.rsplit('p');
    if (MinorVersionStr.empty())
      return getError("extension lacks version in expected format");
    unsigned MajorVersion, MinorVersion;
    if (MinorVersionStr.getAsInteger(10, MinorVersion))
      return getError("failed to parse minor version number");

    // Split Prefix into the extension name and the major version number
    // (the trailing digits of Prefix).
    size_t VersionStart = Prefix.size();
    while (VersionStart != 0) {
      if (!isDigit(Prefix[VersionStart - 1]))
        break;
      --VersionStart;
    }
    if (VersionStart == Prefix.size())
      return getError("extension lacks version in expected format");

    if (VersionStart == 0)
      return getError("missing extension name");

    StringRef ExtName = Prefix.slice(0, VersionStart);
    StringRef MajorVersionStr = Prefix.slice(VersionStart, StringRef::npos);
    if (MajorVersionStr.getAsInteger(10, MajorVersion))
      return getError("failed to parse major version number");

    if ((ExtName[0] == 'z' || ExtName[0] == 's' || ExtName[0] == 'x') &&
        (ExtName.size() == 1 || isDigit(ExtName[1])))
      return getError("'" + Twine(ExtName[0]) +
                      "' must be followed by a letter");

    if (!ISAInfo->Exts
             .emplace(
                 ExtName.str(),
                 RISCVISAUtils::ExtensionVersion{MajorVersion, MinorVersion})
             .second)
      return getError("duplicate extension '" + ExtName + "'");
  }
  ISAInfo->updateImpliedLengths();
  return std::move(ISAInfo);
}

llvm::Expected<std::unique_ptr<RISCVISAInfo>>
RISCVISAInfo::parseArchString(StringRef Arch, bool EnableExperimentalExtension,
                              bool ExperimentalExtensionVersionCheck) {
  // RISC-V ISA strings must be [a-z0-9_]
  if (!llvm::all_of(
          Arch, [](char C) { return isDigit(C) || isLower(C) || C == '_'; }))
    return getError("string may only contain [a-z0-9_]");

  // ISA string must begin with rv32, rv64, or a profile.
  unsigned XLen = 0;
  if (Arch.consume_front("rv32")) {
    XLen = 32;
  } else if (Arch.consume_front("rv64")) {
    XLen = 64;
  } else {
    // Try parsing as a profile.
    auto ProfileCmp = [](StringRef Arch, const RISCVProfile &Profile) {
      return Arch < Profile.Name;
    };
    auto I = llvm::upper_bound(SupportedProfiles, Arch, ProfileCmp);
    bool FoundProfile = I != std::begin(SupportedProfiles) &&
                        Arch.starts_with(std::prev(I)->Name);
    if (!FoundProfile) {
      I = llvm::upper_bound(SupportedExperimentalProfiles, Arch, ProfileCmp);
      FoundProfile = (I != std::begin(SupportedExperimentalProfiles) &&
                      Arch.starts_with(std::prev(I)->Name));
      if (FoundProfile && !EnableExperimentalExtension) {
        return getError("requires '-menable-experimental-extensions' "
                        "for profile '" +
                        std::prev(I)->Name + "'");
      }
    }
    if (FoundProfile) {
      --I;
      std::string NewArch = I->MArch.str();
      StringRef ArchWithoutProfile = Arch.drop_front(I->Name.size());
      if (!ArchWithoutProfile.empty()) {
        if (ArchWithoutProfile.front() != '_')
          return getError("additional extensions must be after separator '_'");
        NewArch += ArchWithoutProfile.str();
      }
      return parseArchString(NewArch, EnableExperimentalExtension,
                             ExperimentalExtensionVersionCheck);
    }
  }

  if (XLen == 0 || Arch.empty())
    return getError(
        "string must begin with rv32{i,e,g}, rv64{i,e,g}, or a supported "
        "profile name");

  std::unique_ptr<RISCVISAInfo> ISAInfo(new RISCVISAInfo(XLen));

  // The canonical order specified in ISA manual.
  // Ref: Table 22.1 in RISC-V User-Level ISA V2.2
  char Baseline = Arch.front();
  // Skip the baseline.
  Arch = Arch.drop_front();

  unsigned Major, Minor, ConsumeLength;

  // First letter should be 'e', 'i' or 'g'.
  switch (Baseline) {
  default:
    return getError("first letter after \'rv" + Twine(XLen) +
                    "\' should be 'e', 'i' or 'g'");
  case 'e':
  case 'i':
    // Baseline is `i` or `e`
    if (auto E = getExtensionVersion(
            StringRef(&Baseline, 1), Arch, Major, Minor, ConsumeLength,
            EnableExperimentalExtension, ExperimentalExtensionVersionCheck))
      return std::move(E);

    ISAInfo->Exts[std::string(1, Baseline)] = {Major, Minor};
    break;
  case 'g':
    // g expands to extensions in RISCVGImplications.
    if (!Arch.empty() && isDigit(Arch.front()))
      return getError("version not supported for 'g'");

    // Versions for g are disallowed, and this was checked for previously.
    ConsumeLength = 0;

    // No matter which version is given to `g`, we always set imafd to default
    // version since the we don't have clear version scheme for that on
    // ISA spec.
    for (const char *Ext : RISCVGImplications) {
      auto Version = findDefaultVersion(Ext);
      assert(Version && "Default extension version not found?");
      // Postpone AddExtension until end of this function
      ISAInfo->Exts[std::string(Ext)] = {Version->Major, Version->Minor};
    }
    break;
  }

  // Consume the base ISA version number and any '_' between rvxxx and the
  // first extension
  Arch = Arch.drop_front(ConsumeLength);

  while (!Arch.empty()) {
    if (Arch.front() == '_') {
      if (Arch.size() == 1 || Arch[1] == '_')
        return getError("extension name missing after separator '_'");
      Arch = Arch.drop_front();
    }

    size_t Idx = Arch.find('_');
    StringRef Ext = Arch.slice(0, Idx);
    Arch = Arch.slice(Idx, StringRef::npos);

    do {
      StringRef Name, Vers, Desc;
      if (RISCVISAUtils::AllStdExts.contains(Ext.front())) {
        Name = Ext.take_front(1);
        Ext = Ext.drop_front();
        Vers = Ext;
        Desc = "standard user-level extension";
      } else if (Ext.front() == 'z' || Ext.front() == 's' ||
                 Ext.front() == 'x') {
        // Handle other types of extensions other than the standard
        // general purpose and standard user-level extensions.
        // Parse the ISA string containing non-standard user-level
        // extensions, standard supervisor-level extensions and
        // non-standard supervisor-level extensions.
        // These extensions start with 'z', 's', 'x' prefixes, might have a
        // version number (major, minor) and are separated by a single
        // underscore '_'. We do not enforce a canonical order for them.
        StringRef Type = getExtensionType(Ext);
        Desc = getExtensionTypeDesc(Ext);
        auto Pos = findLastNonVersionCharacter(Ext) + 1;
        Name = Ext.substr(0, Pos);
        Vers = Ext.substr(Pos);
        Ext = StringRef();

        assert(!Type.empty() && "Empty type?");
        if (Name.size() == Type.size())
          return getError(Desc + " name missing after '" + Type + "'");
      } else {
        return getError("invalid standard user-level extension '" +
                        Twine(Ext.front()) + "'");
      }

      unsigned Major, Minor, ConsumeLength;
      if (auto E = getExtensionVersion(Name, Vers, Major, Minor, ConsumeLength,
                                       EnableExperimentalExtension,
                                       ExperimentalExtensionVersionCheck))
        return E;

      if (Name.size() == 1)
        Ext = Ext.substr(ConsumeLength);

      if (!RISCVISAInfo::isSupportedExtension(Name))
        return getErrorForInvalidExt(Name);

      // Insert and error for duplicates.
      if (!ISAInfo->Exts
               .emplace(Name.str(),
                        RISCVISAUtils::ExtensionVersion{Major, Minor})
               .second)
        return getError("duplicated " + Desc + " '" + Name + "'");

    } while (!Ext.empty());
  }

  return RISCVISAInfo::postProcessAndChecking(std::move(ISAInfo));
}

Error RISCVISAInfo::checkDependency() {
  bool HasE = Exts.count("e") != 0;
  bool HasI = Exts.count("i") != 0;
  bool HasC = Exts.count("c") != 0;
  bool HasF = Exts.count("f") != 0;
  bool HasD = Exts.count("d") != 0;
  bool HasZfinx = Exts.count("zfinx") != 0;
  bool HasVector = Exts.count("zve32x") != 0;
  bool HasZvl = MinVLen != 0;
  bool HasZcmt = Exts.count("zcmt") != 0;

  if (HasI && HasE)
    return getError("'I' and 'E' extensions are incompatible");

  if (HasF && HasZfinx)
    return getError("'f' and 'zfinx' extensions are incompatible");

  if (HasZvl && !HasVector)
    return getError(
        "'zvl*b' requires 'v' or 'zve*' extension to also be specified");

  if (Exts.count("zvbb") && !HasVector)
    return getError(
        "'zvbb' requires 'v' or 'zve*' extension to also be specified");

  if (Exts.count("zvbc") && !Exts.count("zve64x"))
    return getError(
        "'zvbc' requires 'v' or 'zve64*' extension to also be specified");

  if ((Exts.count("zvkb") || Exts.count("zvkg") || Exts.count("zvkned") ||
       Exts.count("zvknha") || Exts.count("zvksed") || Exts.count("zvksh")) &&
      !HasVector)
    return getError(
        "'zvk*' requires 'v' or 'zve*' extension to also be specified");

  if (Exts.count("zvknhb") && !Exts.count("zve64x"))
    return getError(
        "'zvknhb' requires 'v' or 'zve64*' extension to also be specified");

  if ((HasZcmt || Exts.count("zcmp")) && HasD && (HasC || Exts.count("zcd")))
    return getError(Twine("'") + (HasZcmt ? "zcmt" : "zcmp") +
                    "' extension is incompatible with '" +
                    (HasC ? "c" : "zcd") +
                    "' extension when 'd' extension is enabled");

  if (XLen != 32 && Exts.count("zcf"))
    return getError("'zcf' is only supported for 'rv32'");

  if (Exts.count("zacas") && !(Exts.count("a") || Exts.count("zaamo")))
    return getError(
        "'zacas' requires 'a' or 'zaamo' extension to also be specified");

  if (Exts.count("zabha") && !(Exts.count("a") || Exts.count("zaamo")))
    return getError(
        "'zabha' requires 'a' or 'zaamo' extension to also be specified");

  if (Exts.count("xwchc") != 0) {
    if (XLen != 32)
      return getError("'Xwchc' is only supported for 'rv32'");

    if (HasD)
      return getError("'D' and 'Xwchc' extensions are incompatible");

    if (Exts.count("zcb") != 0)
      return getError("'Xwchc' and 'Zcb' extensions are incompatible");
  }

  return Error::success();
}

struct ImpliedExtsEntry {
  StringLiteral Name;
  const char *ImpliedExt;

  bool operator<(const ImpliedExtsEntry &Other) const {
    return Name < Other.Name;
  }
};

static bool operator<(const ImpliedExtsEntry &LHS, StringRef RHS) {
  return LHS.Name < RHS;
}

static bool operator<(StringRef LHS, const ImpliedExtsEntry &RHS) {
  return LHS < RHS.Name;
}

#define GET_IMPLIED_EXTENSIONS
#include "llvm/TargetParser/RISCVTargetParserDef.inc"

void RISCVISAInfo::updateImplication() {
  bool HasE = Exts.count("e") != 0;
  bool HasI = Exts.count("i") != 0;

  // If not in e extension and i extension does not exist, i extension is
  // implied
  if (!HasE && !HasI) {
    auto Version = findDefaultVersion("i");
    Exts["i"] = *Version;
  }

  if (HasE && HasI)
    Exts.erase("i");

  assert(llvm::is_sorted(ImpliedExts) && "Table not sorted by Name");

  // This loop may execute over 1 iteration since implication can be layered
  // Exits loop if no more implication is applied
  SmallVector<StringRef, 16> WorkList;
  for (auto const &Ext : Exts)
    WorkList.push_back(Ext.first);

  while (!WorkList.empty()) {
    StringRef ExtName = WorkList.pop_back_val();
    auto Range = std::equal_range(std::begin(ImpliedExts),
                                  std::end(ImpliedExts), ExtName);
    std::for_each(Range.first, Range.second,
                  [&](const ImpliedExtsEntry &Implied) {
                    const char *ImpliedExt = Implied.ImpliedExt;
                    if (Exts.count(ImpliedExt))
                      return;
                    auto Version = findDefaultVersion(ImpliedExt);
                    Exts[ImpliedExt] = *Version;
                    WorkList.push_back(ImpliedExt);
                  });
  }

  // Add Zcf if Zce and F are enabled on RV32.
  if (XLen == 32 && Exts.count("zce") && Exts.count("f") &&
      !Exts.count("zcf")) {
    auto Version = findDefaultVersion("zcf");
    Exts["zcf"] = *Version;
  }
}

static constexpr StringLiteral CombineIntoExts[] = {
    {"zk"},    {"zkn"},  {"zks"},   {"zvkn"},  {"zvknc"},
    {"zvkng"}, {"zvks"}, {"zvksc"}, {"zvksg"},
};

void RISCVISAInfo::updateCombination() {
  bool MadeChange = false;
  do {
    MadeChange = false;
    for (StringRef CombineExt : CombineIntoExts) {
      if (Exts.count(CombineExt.str()))
        continue;

      // Look up the extension in the ImpliesExt table to find everything it
      // depends on.
      auto Range = std::equal_range(std::begin(ImpliedExts),
                                    std::end(ImpliedExts), CombineExt);
      bool HasAllRequiredFeatures = std::all_of(
          Range.first, Range.second, [&](const ImpliedExtsEntry &Implied) {
            return Exts.count(Implied.ImpliedExt);
          });
      if (HasAllRequiredFeatures) {
        auto Version = findDefaultVersion(CombineExt);
        Exts[CombineExt.str()] = *Version;
        MadeChange = true;
      }
    }
  } while (MadeChange);
}

void RISCVISAInfo::updateImpliedLengths() {
  assert(FLen == 0 && MaxELenFp == 0 && MaxELen == 0 && MinVLen == 0 &&
         "Expected lengths to be initialied to zero");

  // TODO: Handle q extension.
  if (Exts.count("d"))
    FLen = 64;
  else if (Exts.count("f"))
    FLen = 32;

  if (Exts.count("v")) {
    MaxELenFp = std::max(MaxELenFp, 64u);
    MaxELen = std::max(MaxELen, 64u);
  }

  for (auto const &Ext : Exts) {
    StringRef ExtName = Ext.first;
    // Infer MaxELen and MaxELenFp from Zve(32/64)(x/f/d)
    if (ExtName.consume_front("zve")) {
      unsigned ZveELen;
      if (ExtName.consumeInteger(10, ZveELen))
        continue;

      if (ExtName == "f")
        MaxELenFp = std::max(MaxELenFp, 32u);
      else if (ExtName == "d")
        MaxELenFp = std::max(MaxELenFp, 64u);
      else if (ExtName != "x")
        continue;

      MaxELen = std::max(MaxELen, ZveELen);
      continue;
    }

    // Infer MinVLen from zvl*b.
    if (ExtName.consume_front("zvl")) {
      unsigned ZvlLen;
      if (ExtName.consumeInteger(10, ZvlLen))
        continue;

      if (ExtName != "b")
        continue;

      MinVLen = std::max(MinVLen, ZvlLen);
      continue;
    }
  }
}

std::string RISCVISAInfo::toString() const {
  std::string Buffer;
  raw_string_ostream Arch(Buffer);

  Arch << "rv" << XLen;

  ListSeparator LS("_");
  for (auto const &Ext : Exts) {
    StringRef ExtName = Ext.first;
    auto ExtInfo = Ext.second;
    Arch << LS << ExtName;
    Arch << ExtInfo.Major << "p" << ExtInfo.Minor;
  }

  return Arch.str();
}

llvm::Expected<std::unique_ptr<RISCVISAInfo>>
RISCVISAInfo::postProcessAndChecking(std::unique_ptr<RISCVISAInfo> &&ISAInfo) {
  ISAInfo->updateImplication();
  ISAInfo->updateCombination();
  ISAInfo->updateImpliedLengths();

  if (Error Result = ISAInfo->checkDependency())
    return std::move(Result);
  return std::move(ISAInfo);
}

StringRef RISCVISAInfo::computeDefaultABI() const {
  if (XLen == 32) {
    if (Exts.count("e"))
      return "ilp32e";
    if (Exts.count("d"))
      return "ilp32d";
    if (Exts.count("f"))
      return "ilp32f";
    return "ilp32";
  } else if (XLen == 64) {
    if (Exts.count("e"))
      return "lp64e";
    if (Exts.count("d"))
      return "lp64d";
    if (Exts.count("f"))
      return "lp64f";
    return "lp64";
  }
  llvm_unreachable("Invalid XLEN");
}

bool RISCVISAInfo::isSupportedExtensionWithVersion(StringRef Ext) {
  if (Ext.empty())
    return false;

  auto Pos = findLastNonVersionCharacter(Ext) + 1;
  StringRef Name = Ext.substr(0, Pos);
  StringRef Vers = Ext.substr(Pos);
  if (Vers.empty())
    return false;

  unsigned Major, Minor, ConsumeLength;
  if (auto E = getExtensionVersion(Name, Vers, Major, Minor, ConsumeLength,
                                   true, true)) {
    consumeError(std::move(E));
    return false;
  }

  return true;
}

std::string RISCVISAInfo::getTargetFeatureForExtension(StringRef Ext) {
  if (Ext.empty())
    return std::string();

  auto Pos = findLastNonVersionCharacter(Ext) + 1;
  StringRef Name = Ext.substr(0, Pos);

  if (Pos != Ext.size() && !isSupportedExtensionWithVersion(Ext))
    return std::string();

  if (!isSupportedExtension(Name))
    return std::string();

  return isExperimentalExtension(Name) ? "experimental-" + Name.str()
                                       : Name.str();
}
