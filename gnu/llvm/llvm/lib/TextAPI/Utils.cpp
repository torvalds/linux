//===- Utils.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements utility functions for TextAPI Darwin operations.
//
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/Utils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TextAPI/TextAPIError.h"

using namespace llvm;
using namespace llvm::MachO;

void llvm::MachO::replace_extension(SmallVectorImpl<char> &Path,
                                    const Twine &Extension) {
  StringRef P(Path.begin(), Path.size());
  auto ParentPath = sys::path::parent_path(P);
  auto Filename = sys::path::filename(P);

  if (!ParentPath.ends_with(Filename.str() + ".framework")) {
    sys::path::replace_extension(Path, Extension);
    return;
  }
  // Framework dylibs do not have a file extension, in those cases the new
  // extension is appended. e.g. given Path: "Foo.framework/Foo" and Extension:
  // "tbd", the result is "Foo.framework/Foo.tbd".
  SmallString<8> Storage;
  StringRef Ext = Extension.toStringRef(Storage);

  // Append '.' if needed.
  if (!Ext.empty() && Ext[0] != '.')
    Path.push_back('.');

  // Append extension.
  Path.append(Ext.begin(), Ext.end());
}

std::error_code llvm::MachO::shouldSkipSymLink(const Twine &Path,
                                               bool &Result) {
  Result = false;
  SmallString<PATH_MAX> Storage;
  auto P = Path.toNullTerminatedStringRef(Storage);
  sys::fs::file_status Stat1;
  auto EC = sys::fs::status(P.data(), Stat1);
  if (EC == std::errc::too_many_symbolic_link_levels) {
    Result = true;
    return {};
  }

  if (EC)
    return EC;

  StringRef Parent = sys::path::parent_path(P);
  while (!Parent.empty()) {
    sys::fs::file_status Stat2;
    if (auto ec = sys::fs::status(Parent, Stat2))
      return ec;

    if (sys::fs::equivalent(Stat1, Stat2)) {
      Result = true;
      return {};
    }

    Parent = sys::path::parent_path(Parent);
  }
  return {};
}

std::error_code
llvm::MachO::make_relative(StringRef From, StringRef To,
                           SmallVectorImpl<char> &RelativePath) {
  SmallString<PATH_MAX> Src = From;
  SmallString<PATH_MAX> Dst = To;
  if (auto EC = sys::fs::make_absolute(Src))
    return EC;

  if (auto EC = sys::fs::make_absolute(Dst))
    return EC;

  SmallString<PATH_MAX> Result;
  Src = sys::path::parent_path(From);
  auto IT1 = sys::path::begin(Src), IT2 = sys::path::begin(Dst),
       IE1 = sys::path::end(Src), IE2 = sys::path::end(Dst);
  // Ignore the common part.
  for (; IT1 != IE1 && IT2 != IE2; ++IT1, ++IT2) {
    if (*IT1 != *IT2)
      break;
  }

  for (; IT1 != IE1; ++IT1)
    sys::path::append(Result, "../");

  for (; IT2 != IE2; ++IT2)
    sys::path::append(Result, *IT2);

  if (Result.empty())
    Result = ".";

  RelativePath.swap(Result);

  return {};
}

bool llvm::MachO::isPrivateLibrary(StringRef Path, bool IsSymLink) {
  // Remove the iOSSupport and DriverKit prefix to identify public locations.
  Path.consume_front(MACCATALYST_PREFIX_PATH);
  Path.consume_front(DRIVERKIT_PREFIX_PATH);
  // Also /Library/Apple prefix for ROSP.
  Path.consume_front("/Library/Apple");

  if (Path.starts_with("/usr/local/lib"))
    return true;

  if (Path.starts_with("/System/Library/PrivateFrameworks"))
    return true;

  // Everything in /usr/lib/swift (including sub-directories) are considered
  // public.
  if (Path.consume_front("/usr/lib/swift/"))
    return false;

  // Only libraries directly in /usr/lib are public. All other libraries in
  // sub-directories are private.
  if (Path.consume_front("/usr/lib/"))
    return Path.contains('/');

  // "/System/Library/Frameworks/" is a public location.
  if (Path.starts_with("/System/Library/Frameworks/")) {
    StringRef Name, Rest;
    std::tie(Name, Rest) =
        Path.drop_front(sizeof("/System/Library/Frameworks")).split('.');

    // Allow symlinks to top-level frameworks.
    if (IsSymLink && Rest == "framework")
      return false;

    // Only top level framework are public.
    // /System/Library/Frameworks/Foo.framework/Foo ==> true
    // /System/Library/Frameworks/Foo.framework/Versions/A/Foo ==> true
    // /System/Library/Frameworks/Foo.framework/Resources/libBar.dylib ==> false
    // /System/Library/Frameworks/Foo.framework/Frameworks/Bar.framework/Bar
    // ==> false
    // /System/Library/Frameworks/Foo.framework/Frameworks/Xfoo.framework/XFoo
    // ==> false
    return !(Rest.starts_with("framework/") &&
             (Rest.ends_with(Name) || Rest.ends_with((Name + ".tbd").str()) ||
              (IsSymLink && Rest.ends_with("Current"))));
  }
  return false;
}

static StringLiteral RegexMetachars = "()^$|+.[]\\{}";

llvm::Expected<Regex> llvm::MachO::createRegexFromGlob(StringRef Glob) {
  SmallString<128> RegexString("^");
  unsigned NumWildcards = 0;
  for (unsigned i = 0; i < Glob.size(); ++i) {
    char C = Glob[i];
    switch (C) {
    case '?':
      RegexString += '.';
      break;
    case '*': {
      const char *PrevChar = i > 0 ? Glob.data() + i - 1 : nullptr;
      NumWildcards = 1;
      ++i;
      while (i < Glob.size() && Glob[i] == '*') {
        ++NumWildcards;
        ++i;
      }
      const char *NextChar = i < Glob.size() ? Glob.data() + i : nullptr;

      if ((NumWildcards > 1) && (PrevChar == nullptr || *PrevChar == '/') &&
          (NextChar == nullptr || *NextChar == '/')) {
        RegexString += "(([^/]*(/|$))*)";
      } else
        RegexString += "([^/]*)";
      break;
    }
    default:
      if (RegexMetachars.contains(C))
        RegexString.push_back('\\');
      RegexString.push_back(C);
    }
  }
  RegexString.push_back('$');
  if (NumWildcards == 0)
    return make_error<StringError>("not a glob", inconvertibleErrorCode());

  llvm::Regex Rule = Regex(RegexString);
  std::string Error;
  if (!Rule.isValid(Error))
    return make_error<StringError>(Error, inconvertibleErrorCode());

  return std::move(Rule);
}

Expected<AliasMap>
llvm::MachO::parseAliasList(std::unique_ptr<llvm::MemoryBuffer> &Buffer) {
  SmallVector<StringRef, 16> Lines;
  AliasMap Aliases;
  Buffer->getBuffer().split(Lines, "\n", /*MaxSplit=*/-1,
                            /*KeepEmpty=*/false);
  for (const StringRef Line : Lines) {
    StringRef L = Line.trim();
    if (L.empty())
      continue;
    // Skip comments.
    if (L.starts_with("#"))
      continue;
    StringRef Symbol, Remain, Alias;
    // Base symbol is separated by whitespace.
    std::tie(Symbol, Remain) = getToken(L);
    // The Alias symbol ends before a comment or EOL.
    std::tie(Alias, Remain) = getToken(Remain, "#");
    Alias = Alias.trim();
    if (Alias.empty())
      return make_error<TextAPIError>(
          TextAPIError(TextAPIErrorCode::InvalidInputFormat,
                       ("missing alias for: " + Symbol).str()));
    SimpleSymbol AliasSym = parseSymbol(Alias);
    SimpleSymbol BaseSym = parseSymbol(Symbol);
    Aliases[{AliasSym.Name.str(), AliasSym.Kind}] = {BaseSym.Name.str(),
                                                     BaseSym.Kind};
  }

  return Aliases;
}

PathSeq llvm::MachO::getPathsForPlatform(const PathToPlatformSeq &Paths,
                                         PlatformType Platform) {
  PathSeq Result;
  for (const auto &[Path, CurrP] : Paths) {
    if (!CurrP.has_value() || CurrP.value() == Platform)
      Result.push_back(Path);
  }
  return Result;
}
