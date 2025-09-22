//===-- SpecialCaseList.cpp - special case list for sanitizers ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a utility class for instrumentation passes (like AddressSanitizer
// or ThreadSanitizer) to avoid instrumenting some functions or global
// variables, or to instrument some functions or global variables in a specific
// way, based on a user-supplied list.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <stdio.h>
#include <string>
#include <system_error>
#include <utility>

namespace llvm {

Error SpecialCaseList::Matcher::insert(StringRef Pattern, unsigned LineNumber,
                                       bool UseGlobs) {
  if (Pattern.empty())
    return createStringError(errc::invalid_argument,
                             Twine("Supplied ") +
                                 (UseGlobs ? "glob" : "regex") + " was blank");

  if (!UseGlobs) {
    // Replace * with .*
    auto Regexp = Pattern.str();
    for (size_t pos = 0; (pos = Regexp.find('*', pos)) != std::string::npos;
         pos += strlen(".*")) {
      Regexp.replace(pos, strlen("*"), ".*");
    }

    Regexp = (Twine("^(") + StringRef(Regexp) + ")$").str();

    // Check that the regexp is valid.
    Regex CheckRE(Regexp);
    std::string REError;
    if (!CheckRE.isValid(REError))
      return createStringError(errc::invalid_argument, REError);

    RegExes.emplace_back(std::make_pair(
        std::make_unique<Regex>(std::move(CheckRE)), LineNumber));

    return Error::success();
  }

  auto [It, DidEmplace] = Globs.try_emplace(Pattern);
  if (DidEmplace) {
    // We must be sure to use the string in the map rather than the provided
    // reference which could be destroyed before match() is called
    Pattern = It->getKey();
    auto &Pair = It->getValue();
    if (auto Err = GlobPattern::create(Pattern, /*MaxSubPatterns=*/1024)
                       .moveInto(Pair.first))
      return Err;
    Pair.second = LineNumber;
  }
  return Error::success();
}

unsigned SpecialCaseList::Matcher::match(StringRef Query) const {
  for (const auto &[Pattern, Pair] : Globs)
    if (Pair.first.match(Query))
      return Pair.second;
  for (const auto &[Regex, LineNumber] : RegExes)
    if (Regex->match(Query))
      return LineNumber;
  return 0;
}

// TODO: Refactor this to return Expected<...>
std::unique_ptr<SpecialCaseList>
SpecialCaseList::create(const std::vector<std::string> &Paths,
                        llvm::vfs::FileSystem &FS, std::string &Error) {
  std::unique_ptr<SpecialCaseList> SCL(new SpecialCaseList());
  if (SCL->createInternal(Paths, FS, Error))
    return SCL;
  return nullptr;
}

std::unique_ptr<SpecialCaseList> SpecialCaseList::create(const MemoryBuffer *MB,
                                                         std::string &Error) {
  std::unique_ptr<SpecialCaseList> SCL(new SpecialCaseList());
  if (SCL->createInternal(MB, Error))
    return SCL;
  return nullptr;
}

std::unique_ptr<SpecialCaseList>
SpecialCaseList::createOrDie(const std::vector<std::string> &Paths,
                             llvm::vfs::FileSystem &FS) {
  std::string Error;
  if (auto SCL = create(Paths, FS, Error))
    return SCL;
  report_fatal_error(Twine(Error));
}

bool SpecialCaseList::createInternal(const std::vector<std::string> &Paths,
                                     vfs::FileSystem &VFS, std::string &Error) {
  for (const auto &Path : Paths) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
        VFS.getBufferForFile(Path);
    if (std::error_code EC = FileOrErr.getError()) {
      Error = (Twine("can't open file '") + Path + "': " + EC.message()).str();
      return false;
    }
    std::string ParseError;
    if (!parse(FileOrErr.get().get(), ParseError)) {
      Error = (Twine("error parsing file '") + Path + "': " + ParseError).str();
      return false;
    }
  }
  return true;
}

bool SpecialCaseList::createInternal(const MemoryBuffer *MB,
                                     std::string &Error) {
  if (!parse(MB, Error))
    return false;
  return true;
}

Expected<SpecialCaseList::Section *>
SpecialCaseList::addSection(StringRef SectionStr, unsigned LineNo,
                            bool UseGlobs) {
  auto [It, DidEmplace] = Sections.try_emplace(SectionStr);
  auto &Section = It->getValue();
  if (DidEmplace)
    if (auto Err = Section.SectionMatcher->insert(SectionStr, LineNo, UseGlobs))
      return createStringError(errc::invalid_argument,
                               "malformed section at line " + Twine(LineNo) +
                                   ": '" + SectionStr +
                                   "': " + toString(std::move(Err)));
  return &Section;
}

bool SpecialCaseList::parse(const MemoryBuffer *MB, std::string &Error) {
  Section *CurrentSection;
  if (auto Err = addSection("*", 1).moveInto(CurrentSection)) {
    Error = toString(std::move(Err));
    return false;
  }

  // In https://reviews.llvm.org/D154014 we added glob support and planned to
  // remove regex support in patterns. We temporarily support the original
  // behavior using regexes if "#!special-case-list-v1" is the first line of the
  // file. For more details, see
  // https://discourse.llvm.org/t/use-glob-instead-of-regex-for-specialcaselists/71666
  bool UseGlobs = !MB->getBuffer().starts_with("#!special-case-list-v1\n");

  for (line_iterator LineIt(*MB, /*SkipBlanks=*/true, /*CommentMarker=*/'#');
       !LineIt.is_at_eof(); LineIt++) {
    unsigned LineNo = LineIt.line_number();
    StringRef Line = LineIt->trim();
    if (Line.empty())
      continue;

    // Save section names
    if (Line.starts_with("[")) {
      if (!Line.ends_with("]")) {
        Error =
            ("malformed section header on line " + Twine(LineNo) + ": " + Line)
                .str();
        return false;
      }

      if (auto Err = addSection(Line.drop_front().drop_back(), LineNo, UseGlobs)
                         .moveInto(CurrentSection)) {
        Error = toString(std::move(Err));
        return false;
      }
      continue;
    }

    // Get our prefix and unparsed glob.
    auto [Prefix, Postfix] = Line.split(":");
    if (Postfix.empty()) {
      // Missing ':' in the line.
      Error = ("malformed line " + Twine(LineNo) + ": '" + Line + "'").str();
      return false;
    }

    auto [Pattern, Category] = Postfix.split("=");
    auto &Entry = CurrentSection->Entries[Prefix][Category];
    if (auto Err = Entry.insert(Pattern, LineNo, UseGlobs)) {
      Error =
          (Twine("malformed ") + (UseGlobs ? "glob" : "regex") + " in line " +
           Twine(LineNo) + ": '" + Pattern + "': " + toString(std::move(Err)))
              .str();
      return false;
    }
  }
  return true;
}

SpecialCaseList::~SpecialCaseList() = default;

bool SpecialCaseList::inSection(StringRef Section, StringRef Prefix,
                                StringRef Query, StringRef Category) const {
  return inSectionBlame(Section, Prefix, Query, Category);
}

unsigned SpecialCaseList::inSectionBlame(StringRef Section, StringRef Prefix,
                                         StringRef Query,
                                         StringRef Category) const {
  for (const auto &It : Sections) {
    const auto &S = It.getValue();
    if (S.SectionMatcher->match(Section)) {
      unsigned Blame = inSectionBlame(S.Entries, Prefix, Query, Category);
      if (Blame)
        return Blame;
    }
  }
  return 0;
}

unsigned SpecialCaseList::inSectionBlame(const SectionEntries &Entries,
                                         StringRef Prefix, StringRef Query,
                                         StringRef Category) const {
  SectionEntries::const_iterator I = Entries.find(Prefix);
  if (I == Entries.end()) return 0;
  StringMap<Matcher>::const_iterator II = I->second.find(Category);
  if (II == I->second.end()) return 0;

  return II->getValue().match(Query);
}

} // namespace llvm
