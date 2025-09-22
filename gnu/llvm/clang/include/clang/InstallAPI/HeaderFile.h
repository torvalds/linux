//===- InstallAPI/HeaderFile.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Representations of a library's headers for InstallAPI.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_HEADERFILE_H
#define LLVM_CLANG_INSTALLAPI_HEADERFILE_H

#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangStandard.h"
#include "clang/InstallAPI/MachO.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Regex.h"
#include <optional>
#include <string>

namespace clang::installapi {
enum class HeaderType {
  /// Represents declarations accessible to all clients.
  Public,
  /// Represents declarations accessible to a disclosed set of clients.
  Private,
  /// Represents declarations only accessible as implementation details to the
  /// input library.
  Project,
  /// Unset or unknown type.
  Unknown,
};

inline StringRef getName(const HeaderType T) {
  switch (T) {
  case HeaderType::Public:
    return "Public";
  case HeaderType::Private:
    return "Private";
  case HeaderType::Project:
    return "Project";
  case HeaderType::Unknown:
    return "Unknown";
  }
  llvm_unreachable("unexpected header type");
}

class HeaderFile {
  /// Full input path to header.
  std::string FullPath;
  /// Access level of header.
  HeaderType Type;
  /// Expected way header will be included by clients.
  std::string IncludeName;
  /// Supported language mode for header.
  std::optional<clang::Language> Language;
  /// Exclude header file from processing.
  bool Excluded{false};
  /// Add header file to processing.
  bool Extra{false};
  /// Specify that header file is the umbrella header for library.
  bool Umbrella{false};

public:
  HeaderFile() = delete;
  HeaderFile(StringRef FullPath, HeaderType Type,
             StringRef IncludeName = StringRef(),
             std::optional<clang::Language> Language = std::nullopt)
      : FullPath(FullPath), Type(Type), IncludeName(IncludeName),
        Language(Language) {}

  static llvm::Regex getFrameworkIncludeRule();

  HeaderType getType() const { return Type; }
  StringRef getIncludeName() const { return IncludeName; }
  StringRef getPath() const { return FullPath; }

  void setExtra(bool V = true) { Extra = V; }
  void setExcluded(bool V = true) { Excluded = V; }
  void setUmbrellaHeader(bool V = true) { Umbrella = V; }
  bool isExtra() const { return Extra; }
  bool isExcluded() const { return Excluded; }
  bool isUmbrellaHeader() const { return Umbrella; }

  bool useIncludeName() const {
    return Type != HeaderType::Project && !IncludeName.empty();
  }

  bool operator==(const HeaderFile &Other) const {
    return std::tie(Type, FullPath, IncludeName, Language, Excluded, Extra,
                    Umbrella) == std::tie(Other.Type, Other.FullPath,
                                          Other.IncludeName, Other.Language,
                                          Other.Excluded, Other.Extra,
                                          Other.Umbrella);
  }

  bool operator<(const HeaderFile &Other) const {
    /// For parsing of headers based on ordering,
    /// group by type, then whether its an umbrella.
    /// Capture 'extra' headers last.
    /// This optimizes the chance of a sucessful parse for
    /// headers that violate IWYU.
    if (isExtra() && Other.isExtra())
      return std::tie(Type, Umbrella) < std::tie(Other.Type, Other.Umbrella);

    return std::tie(Type, Umbrella, Extra, FullPath) <
           std::tie(Other.Type, Other.Umbrella, Other.Extra, Other.FullPath);
  }
};

/// Glob that represents a pattern of header files to retreive.
class HeaderGlob {
private:
  std::string GlobString;
  llvm::Regex Rule;
  HeaderType Type;
  bool FoundMatch{false};

public:
  HeaderGlob(StringRef GlobString, llvm::Regex &&, HeaderType Type);

  /// Create a header glob from string for the header access level.
  static llvm::Expected<std::unique_ptr<HeaderGlob>>
  create(StringRef GlobString, HeaderType Type);

  /// Query if provided header matches glob.
  bool match(const HeaderFile &Header);

  /// Query if a header was matched in the glob, used primarily for error
  /// reporting.
  bool didMatch() { return FoundMatch; }

  /// Provide back input glob string.
  StringRef str() { return GlobString; }
};

/// Assemble expected way header will be included by clients.
/// As in what maps inside the brackets of `#include <IncludeName.h>`
/// For example,
/// "/System/Library/Frameworks/Foo.framework/Headers/Foo.h" returns
/// "Foo/Foo.h"
///
/// \param FullPath Path to the header file which includes the library
/// structure.
std::optional<std::string> createIncludeHeaderName(const StringRef FullPath);
using HeaderSeq = std::vector<HeaderFile>;

/// Determine if Path is a header file.
/// It does not touch the file system.
///
/// \param  Path File path to file.
bool isHeaderFile(StringRef Path);

/// Given input directory, collect all header files.
///
/// \param FM FileManager for finding input files.
/// \param Directory Path to directory file.
llvm::Expected<PathSeq> enumerateFiles(clang::FileManager &FM,
                                       StringRef Directory);

} // namespace clang::installapi

#endif // LLVM_CLANG_INSTALLAPI_HEADERFILE_H
