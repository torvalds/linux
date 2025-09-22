//===- HeaderFile.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/InstallAPI/HeaderFile.h"
#include "llvm/TextAPI/Utils.h"

using namespace llvm;
namespace clang::installapi {

llvm::Regex HeaderFile::getFrameworkIncludeRule() {
  return llvm::Regex("/(.+)\\.framework/(.+)?Headers/(.+)");
}

std::optional<std::string> createIncludeHeaderName(const StringRef FullPath) {
  // Headers in usr(/local)*/include.
  std::string Pattern = "/include/";
  auto PathPrefix = FullPath.find(Pattern);
  if (PathPrefix != StringRef::npos) {
    PathPrefix += Pattern.size();
    return FullPath.drop_front(PathPrefix).str();
  }

  // Framework Headers.
  SmallVector<StringRef, 4> Matches;
  HeaderFile::getFrameworkIncludeRule().match(FullPath, &Matches);
  // Returned matches are always in stable order.
  if (Matches.size() != 4)
    return std::nullopt;

  return Matches[1].drop_front(Matches[1].rfind('/') + 1).str() + "/" +
         Matches[3].str();
}

bool isHeaderFile(StringRef Path) {
  return StringSwitch<bool>(sys::path::extension(Path))
      .Cases(".h", ".H", ".hh", ".hpp", ".hxx", true)
      .Default(false);
}

llvm::Expected<PathSeq> enumerateFiles(FileManager &FM, StringRef Directory) {
  PathSeq Files;
  std::error_code EC;
  auto &FS = FM.getVirtualFileSystem();
  for (llvm::vfs::recursive_directory_iterator i(FS, Directory, EC), ie;
       i != ie; i.increment(EC)) {
    if (EC)
      return errorCodeToError(EC);

    // Skip files that do not exist. This usually happens for broken symlinks.
    if (FS.status(i->path()) == std::errc::no_such_file_or_directory)
      continue;

    StringRef Path = i->path();
    if (isHeaderFile(Path))
      Files.emplace_back(Path);
  }

  return Files;
}

HeaderGlob::HeaderGlob(StringRef GlobString, Regex &&Rule, HeaderType Type)
    : GlobString(GlobString), Rule(std::move(Rule)), Type(Type) {}

bool HeaderGlob::match(const HeaderFile &Header) {
  if (Header.getType() != Type)
    return false;

  bool Match = Rule.match(Header.getPath());
  if (Match)
    FoundMatch = true;
  return Match;
}

Expected<std::unique_ptr<HeaderGlob>> HeaderGlob::create(StringRef GlobString,
                                                         HeaderType Type) {
  auto Rule = MachO::createRegexFromGlob(GlobString);
  if (!Rule)
    return Rule.takeError();

  return std::make_unique<HeaderGlob>(GlobString, std::move(*Rule), Type);
}

} // namespace clang::installapi
