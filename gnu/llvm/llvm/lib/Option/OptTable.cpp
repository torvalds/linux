//===- OptTable.cpp - Option Table Implementation -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Option/OptTable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h" // for expandResponseFiles
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::opt;

namespace llvm {
namespace opt {

// Ordering on Info. The ordering is *almost* case-insensitive lexicographic,
// with an exception. '\0' comes at the end of the alphabet instead of the
// beginning (thus options precede any other options which prefix them).
static int StrCmpOptionNameIgnoreCase(StringRef A, StringRef B) {
  size_t MinSize = std::min(A.size(), B.size());
  if (int Res = A.substr(0, MinSize).compare_insensitive(B.substr(0, MinSize)))
    return Res;

  if (A.size() == B.size())
    return 0;

  return (A.size() == MinSize) ? 1 /* A is a prefix of B. */
                               : -1 /* B is a prefix of A */;
}

#ifndef NDEBUG
static int StrCmpOptionName(StringRef A, StringRef B) {
  if (int N = StrCmpOptionNameIgnoreCase(A, B))
    return N;
  return A.compare(B);
}

static inline bool operator<(const OptTable::Info &A, const OptTable::Info &B) {
  if (&A == &B)
    return false;

  if (int N = StrCmpOptionName(A.getName(), B.getName()))
    return N < 0;

  for (size_t I = 0, K = std::min(A.Prefixes.size(), B.Prefixes.size()); I != K;
       ++I)
    if (int N = StrCmpOptionName(A.Prefixes[I], B.Prefixes[I]))
      return N < 0;

  // Names are the same, check that classes are in order; exactly one
  // should be joined, and it should succeed the other.
  assert(((A.Kind == Option::JoinedClass) ^ (B.Kind == Option::JoinedClass)) &&
         "Unexpected classes for options with same name.");
  return B.Kind == Option::JoinedClass;
}
#endif

// Support lower_bound between info and an option name.
static inline bool operator<(const OptTable::Info &I, StringRef Name) {
  return StrCmpOptionNameIgnoreCase(I.getName(), Name) < 0;
}

} // end namespace opt
} // end namespace llvm

OptSpecifier::OptSpecifier(const Option *Opt) : ID(Opt->getID()) {}

OptTable::OptTable(ArrayRef<Info> OptionInfos, bool IgnoreCase)
    : OptionInfos(OptionInfos), IgnoreCase(IgnoreCase) {
  // Explicitly zero initialize the error to work around a bug in array
  // value-initialization on MinGW with gcc 4.3.5.

  // Find start of normal options.
  for (unsigned i = 0, e = getNumOptions(); i != e; ++i) {
    unsigned Kind = getInfo(i + 1).Kind;
    if (Kind == Option::InputClass) {
      assert(!InputOptionID && "Cannot have multiple input options!");
      InputOptionID = getInfo(i + 1).ID;
    } else if (Kind == Option::UnknownClass) {
      assert(!UnknownOptionID && "Cannot have multiple unknown options!");
      UnknownOptionID = getInfo(i + 1).ID;
    } else if (Kind != Option::GroupClass) {
      FirstSearchableIndex = i;
      break;
    }
  }
  assert(FirstSearchableIndex != 0 && "No searchable options?");

#ifndef NDEBUG
  // Check that everything after the first searchable option is a
  // regular option class.
  for (unsigned i = FirstSearchableIndex, e = getNumOptions(); i != e; ++i) {
    Option::OptionClass Kind = (Option::OptionClass) getInfo(i + 1).Kind;
    assert((Kind != Option::InputClass && Kind != Option::UnknownClass &&
            Kind != Option::GroupClass) &&
           "Special options should be defined first!");
  }

  // Check that options are in order.
  for (unsigned i = FirstSearchableIndex + 1, e = getNumOptions(); i != e; ++i){
    if (!(getInfo(i) < getInfo(i + 1))) {
      getOption(i).dump();
      getOption(i + 1).dump();
      llvm_unreachable("Options are not in order!");
    }
  }
#endif
}

void OptTable::buildPrefixChars() {
  assert(PrefixChars.empty() && "rebuilding a non-empty prefix char");

  // Build prefix chars.
  for (const StringLiteral &Prefix : getPrefixesUnion()) {
    for (char C : Prefix)
      if (!is_contained(PrefixChars, C))
        PrefixChars.push_back(C);
  }
}

OptTable::~OptTable() = default;

const Option OptTable::getOption(OptSpecifier Opt) const {
  unsigned id = Opt.getID();
  if (id == 0)
    return Option(nullptr, nullptr);
  assert((unsigned) (id - 1) < getNumOptions() && "Invalid ID.");
  return Option(&getInfo(id), this);
}

static bool isInput(const ArrayRef<StringLiteral> &Prefixes, StringRef Arg) {
  if (Arg == "-")
    return true;
  for (const StringRef &Prefix : Prefixes)
    if (Arg.starts_with(Prefix))
      return false;
  return true;
}

/// \returns Matched size. 0 means no match.
static unsigned matchOption(const OptTable::Info *I, StringRef Str,
                            bool IgnoreCase) {
  for (auto Prefix : I->Prefixes) {
    if (Str.starts_with(Prefix)) {
      StringRef Rest = Str.substr(Prefix.size());
      bool Matched = IgnoreCase ? Rest.starts_with_insensitive(I->getName())
                                : Rest.starts_with(I->getName());
      if (Matched)
        return Prefix.size() + StringRef(I->getName()).size();
    }
  }
  return 0;
}

// Returns true if one of the Prefixes + In.Names matches Option
static bool optionMatches(const OptTable::Info &In, StringRef Option) {
  for (auto Prefix : In.Prefixes)
    if (Option.ends_with(In.getName()))
      if (Option.slice(0, Option.size() - In.getName().size()) == Prefix)
        return true;
  return false;
}

// This function is for flag value completion.
// Eg. When "-stdlib=" and "l" was passed to this function, it will return
// appropiriate values for stdlib, which starts with l.
std::vector<std::string>
OptTable::suggestValueCompletions(StringRef Option, StringRef Arg) const {
  // Search all options and return possible values.
  for (size_t I = FirstSearchableIndex, E = OptionInfos.size(); I < E; I++) {
    const Info &In = OptionInfos[I];
    if (!In.Values || !optionMatches(In, Option))
      continue;

    SmallVector<StringRef, 8> Candidates;
    StringRef(In.Values).split(Candidates, ",", -1, false);

    std::vector<std::string> Result;
    for (StringRef Val : Candidates)
      if (Val.starts_with(Arg) && Arg != Val)
        Result.push_back(std::string(Val));
    return Result;
  }
  return {};
}

std::vector<std::string>
OptTable::findByPrefix(StringRef Cur, Visibility VisibilityMask,
                       unsigned int DisableFlags) const {
  std::vector<std::string> Ret;
  for (size_t I = FirstSearchableIndex, E = OptionInfos.size(); I < E; I++) {
    const Info &In = OptionInfos[I];
    if (In.Prefixes.empty() || (!In.HelpText && !In.GroupID))
      continue;
    if (!(In.Visibility & VisibilityMask))
      continue;
    if (In.Flags & DisableFlags)
      continue;

    for (auto Prefix : In.Prefixes) {
      std::string S = (Prefix + In.getName() + "\t").str();
      if (In.HelpText)
        S += In.HelpText;
      if (StringRef(S).starts_with(Cur) && S != std::string(Cur) + "\t")
        Ret.push_back(S);
    }
  }
  return Ret;
}

unsigned OptTable::findNearest(StringRef Option, std::string &NearestString,
                               Visibility VisibilityMask,
                               unsigned MinimumLength,
                               unsigned MaximumDistance) const {
  return internalFindNearest(
      Option, NearestString, MinimumLength, MaximumDistance,
      [VisibilityMask](const Info &CandidateInfo) {
        return (CandidateInfo.Visibility & VisibilityMask) == 0;
      });
}

unsigned OptTable::findNearest(StringRef Option, std::string &NearestString,
                               unsigned FlagsToInclude, unsigned FlagsToExclude,
                               unsigned MinimumLength,
                               unsigned MaximumDistance) const {
  return internalFindNearest(
      Option, NearestString, MinimumLength, MaximumDistance,
      [FlagsToInclude, FlagsToExclude](const Info &CandidateInfo) {
        if (FlagsToInclude && !(CandidateInfo.Flags & FlagsToInclude))
          return true;
        if (CandidateInfo.Flags & FlagsToExclude)
          return true;
        return false;
      });
}

unsigned OptTable::internalFindNearest(
    StringRef Option, std::string &NearestString, unsigned MinimumLength,
    unsigned MaximumDistance,
    std::function<bool(const Info &)> ExcludeOption) const {
  assert(!Option.empty());

  // Consider each [option prefix + option name] pair as a candidate, finding
  // the closest match.
  unsigned BestDistance =
      MaximumDistance == UINT_MAX ? UINT_MAX : MaximumDistance + 1;
  SmallString<16> Candidate;
  SmallString<16> NormalizedName;

  for (const Info &CandidateInfo :
       ArrayRef<Info>(OptionInfos).drop_front(FirstSearchableIndex)) {
    StringRef CandidateName = CandidateInfo.getName();

    // We can eliminate some option prefix/name pairs as candidates right away:
    // * Ignore option candidates with empty names, such as "--", or names
    //   that do not meet the minimum length.
    if (CandidateName.size() < MinimumLength)
      continue;

    // Ignore options that are excluded via masks
    if (ExcludeOption(CandidateInfo))
      continue;

    // * Ignore positional argument option candidates (which do not
    //   have prefixes).
    if (CandidateInfo.Prefixes.empty())
      continue;

    // Now check if the candidate ends with a character commonly used when
    // delimiting an option from its value, such as '=' or ':'. If it does,
    // attempt to split the given option based on that delimiter.
    char Last = CandidateName.back();
    bool CandidateHasDelimiter = Last == '=' || Last == ':';
    StringRef RHS;
    if (CandidateHasDelimiter) {
      std::tie(NormalizedName, RHS) = Option.split(Last);
      if (Option.find(Last) == NormalizedName.size())
        NormalizedName += Last;
    } else
      NormalizedName = Option;

    // Consider each possible prefix for each candidate to find the most
    // appropriate one. For example, if a user asks for "--helm", suggest
    // "--help" over "-help".
    for (auto CandidatePrefix : CandidateInfo.Prefixes) {
      // If Candidate and NormalizedName have more than 'BestDistance'
      // characters of difference, no need to compute the edit distance, it's
      // going to be greater than BestDistance. Don't bother computing Candidate
      // at all.
      size_t CandidateSize = CandidatePrefix.size() + CandidateName.size(),
             NormalizedSize = NormalizedName.size();
      size_t AbsDiff = CandidateSize > NormalizedSize
                           ? CandidateSize - NormalizedSize
                           : NormalizedSize - CandidateSize;
      if (AbsDiff > BestDistance) {
        continue;
      }
      Candidate = CandidatePrefix;
      Candidate += CandidateName;
      unsigned Distance = StringRef(Candidate).edit_distance(
          NormalizedName, /*AllowReplacements=*/true,
          /*MaxEditDistance=*/BestDistance);
      if (RHS.empty() && CandidateHasDelimiter) {
        // The Candidate ends with a = or : delimiter, but the option passed in
        // didn't contain the delimiter (or doesn't have anything after it).
        // In that case, penalize the correction: `-nodefaultlibs` is more
        // likely to be a spello for `-nodefaultlib` than `-nodefaultlib:` even
        // though both have an unmodified editing distance of 1, since the
        // latter would need an argument.
        ++Distance;
      }
      if (Distance < BestDistance) {
        BestDistance = Distance;
        NearestString = (Candidate + RHS).str();
      }
    }
  }
  return BestDistance;
}

// Parse a single argument, return the new argument, and update Index. If
// GroupedShortOptions is true, -a matches "-abc" and the argument in Args will
// be updated to "-bc". This overload does not support VisibilityMask or case
// insensitive options.
std::unique_ptr<Arg> OptTable::parseOneArgGrouped(InputArgList &Args,
                                                  unsigned &Index) const {
  // Anything that doesn't start with PrefixesUnion is an input, as is '-'
  // itself.
  const char *CStr = Args.getArgString(Index);
  StringRef Str(CStr);
  if (isInput(getPrefixesUnion(), Str))
    return std::make_unique<Arg>(getOption(InputOptionID), Str, Index++, CStr);

  const Info *End = OptionInfos.data() + OptionInfos.size();
  StringRef Name = Str.ltrim(PrefixChars);
  const Info *Start =
      std::lower_bound(OptionInfos.data() + FirstSearchableIndex, End, Name);
  const Info *Fallback = nullptr;
  unsigned Prev = Index;

  // Search for the option which matches Str.
  for (; Start != End; ++Start) {
    unsigned ArgSize = matchOption(Start, Str, IgnoreCase);
    if (!ArgSize)
      continue;

    Option Opt(Start, this);
    if (std::unique_ptr<Arg> A =
            Opt.accept(Args, StringRef(Args.getArgString(Index), ArgSize),
                       /*GroupedShortOption=*/false, Index))
      return A;

    // If Opt is a Flag of length 2 (e.g. "-a"), we know it is a prefix of
    // the current argument (e.g. "-abc"). Match it as a fallback if no longer
    // option (e.g. "-ab") exists.
    if (ArgSize == 2 && Opt.getKind() == Option::FlagClass)
      Fallback = Start;

    // Otherwise, see if the argument is missing.
    if (Prev != Index)
      return nullptr;
  }
  if (Fallback) {
    Option Opt(Fallback, this);
    // Check that the last option isn't a flag wrongly given an argument.
    if (Str[2] == '=')
      return std::make_unique<Arg>(getOption(UnknownOptionID), Str, Index++,
                                   CStr);

    if (std::unique_ptr<Arg> A = Opt.accept(
            Args, Str.substr(0, 2), /*GroupedShortOption=*/true, Index)) {
      Args.replaceArgString(Index, Twine('-') + Str.substr(2));
      return A;
    }
  }

  // In the case of an incorrect short option extract the character and move to
  // the next one.
  if (Str[1] != '-') {
    CStr = Args.MakeArgString(Str.substr(0, 2));
    Args.replaceArgString(Index, Twine('-') + Str.substr(2));
    return std::make_unique<Arg>(getOption(UnknownOptionID), CStr, Index, CStr);
  }

  return std::make_unique<Arg>(getOption(UnknownOptionID), Str, Index++, CStr);
}

std::unique_ptr<Arg> OptTable::ParseOneArg(const ArgList &Args, unsigned &Index,
                                           Visibility VisibilityMask) const {
  return internalParseOneArg(Args, Index, [VisibilityMask](const Option &Opt) {
    return !Opt.hasVisibilityFlag(VisibilityMask);
  });
}

std::unique_ptr<Arg> OptTable::ParseOneArg(const ArgList &Args, unsigned &Index,
                                           unsigned FlagsToInclude,
                                           unsigned FlagsToExclude) const {
  return internalParseOneArg(
      Args, Index, [FlagsToInclude, FlagsToExclude](const Option &Opt) {
        if (FlagsToInclude && !Opt.hasFlag(FlagsToInclude))
          return true;
        if (Opt.hasFlag(FlagsToExclude))
          return true;
        return false;
      });
}

std::unique_ptr<Arg> OptTable::internalParseOneArg(
    const ArgList &Args, unsigned &Index,
    std::function<bool(const Option &)> ExcludeOption) const {
  unsigned Prev = Index;
  StringRef Str = Args.getArgString(Index);

  // Anything that doesn't start with PrefixesUnion is an input, as is '-'
  // itself.
  if (isInput(getPrefixesUnion(), Str))
    return std::make_unique<Arg>(getOption(InputOptionID), Str, Index++,
                                 Str.data());

  const Info *Start = OptionInfos.data() + FirstSearchableIndex;
  const Info *End = OptionInfos.data() + OptionInfos.size();
  StringRef Name = Str.ltrim(PrefixChars);

  // Search for the first next option which could be a prefix.
  Start = std::lower_bound(Start, End, Name);

  // Options are stored in sorted order, with '\0' at the end of the
  // alphabet. Since the only options which can accept a string must
  // prefix it, we iteratively search for the next option which could
  // be a prefix.
  //
  // FIXME: This is searching much more than necessary, but I am
  // blanking on the simplest way to make it fast. We can solve this
  // problem when we move to TableGen.
  for (; Start != End; ++Start) {
    unsigned ArgSize = 0;
    // Scan for first option which is a proper prefix.
    for (; Start != End; ++Start)
      if ((ArgSize = matchOption(Start, Str, IgnoreCase)))
        break;
    if (Start == End)
      break;

    Option Opt(Start, this);

    if (ExcludeOption(Opt))
      continue;

    // See if this option matches.
    if (std::unique_ptr<Arg> A =
            Opt.accept(Args, StringRef(Args.getArgString(Index), ArgSize),
                       /*GroupedShortOption=*/false, Index))
      return A;

    // Otherwise, see if this argument was missing values.
    if (Prev != Index)
      return nullptr;
  }

  // If we failed to find an option and this arg started with /, then it's
  // probably an input path.
  if (Str[0] == '/')
    return std::make_unique<Arg>(getOption(InputOptionID), Str, Index++,
                                 Str.data());

  return std::make_unique<Arg>(getOption(UnknownOptionID), Str, Index++,
                               Str.data());
}

InputArgList OptTable::ParseArgs(ArrayRef<const char *> Args,
                                 unsigned &MissingArgIndex,
                                 unsigned &MissingArgCount,
                                 Visibility VisibilityMask) const {
  return internalParseArgs(
      Args, MissingArgIndex, MissingArgCount,
      [VisibilityMask](const Option &Opt) {
        return !Opt.hasVisibilityFlag(VisibilityMask);
      });
}

InputArgList OptTable::ParseArgs(ArrayRef<const char *> Args,
                                 unsigned &MissingArgIndex,
                                 unsigned &MissingArgCount,
                                 unsigned FlagsToInclude,
                                 unsigned FlagsToExclude) const {
  return internalParseArgs(
      Args, MissingArgIndex, MissingArgCount,
      [FlagsToInclude, FlagsToExclude](const Option &Opt) {
        if (FlagsToInclude && !Opt.hasFlag(FlagsToInclude))
          return true;
        if (Opt.hasFlag(FlagsToExclude))
          return true;
        return false;
      });
}

InputArgList OptTable::internalParseArgs(
    ArrayRef<const char *> ArgArr, unsigned &MissingArgIndex,
    unsigned &MissingArgCount,
    std::function<bool(const Option &)> ExcludeOption) const {
  InputArgList Args(ArgArr.begin(), ArgArr.end());

  // FIXME: Handle '@' args (or at least error on them).

  MissingArgIndex = MissingArgCount = 0;
  unsigned Index = 0, End = ArgArr.size();
  while (Index < End) {
    // Ingore nullptrs, they are response file's EOL markers
    if (Args.getArgString(Index) == nullptr) {
      ++Index;
      continue;
    }
    // Ignore empty arguments (other things may still take them as arguments).
    StringRef Str = Args.getArgString(Index);
    if (Str == "") {
      ++Index;
      continue;
    }

    // In DashDashParsing mode, the first "--" stops option scanning and treats
    // all subsequent arguments as positional.
    if (DashDashParsing && Str == "--") {
      while (++Index < End) {
        Args.append(new Arg(getOption(InputOptionID), Str, Index,
                            Args.getArgString(Index)));
      }
      break;
    }

    unsigned Prev = Index;
    std::unique_ptr<Arg> A = GroupedShortOptions
                 ? parseOneArgGrouped(Args, Index)
                 : internalParseOneArg(Args, Index, ExcludeOption);
    assert((Index > Prev || GroupedShortOptions) &&
           "Parser failed to consume argument.");

    // Check for missing argument error.
    if (!A) {
      assert(Index >= End && "Unexpected parser error.");
      assert(Index - Prev - 1 && "No missing arguments!");
      MissingArgIndex = Prev;
      MissingArgCount = Index - Prev - 1;
      break;
    }

    Args.append(A.release());
  }

  return Args;
}

InputArgList OptTable::parseArgs(int Argc, char *const *Argv,
                                 OptSpecifier Unknown, StringSaver &Saver,
                                 std::function<void(StringRef)> ErrorFn) const {
  SmallVector<const char *, 0> NewArgv;
  // The environment variable specifies initial options which can be overridden
  // by commnad line options.
  cl::expandResponseFiles(Argc, Argv, EnvVar, Saver, NewArgv);

  unsigned MAI, MAC;
  opt::InputArgList Args = ParseArgs(ArrayRef(NewArgv), MAI, MAC);
  if (MAC)
    ErrorFn((Twine(Args.getArgString(MAI)) + ": missing argument").str());

  // For each unknwon option, call ErrorFn with a formatted error message. The
  // message includes a suggested alternative option spelling if available.
  std::string Nearest;
  for (const opt::Arg *A : Args.filtered(Unknown)) {
    std::string Spelling = A->getAsString(Args);
    if (findNearest(Spelling, Nearest) > 1)
      ErrorFn("unknown argument '" + Spelling + "'");
    else
      ErrorFn("unknown argument '" + Spelling + "', did you mean '" + Nearest +
              "'?");
  }
  return Args;
}

static std::string getOptionHelpName(const OptTable &Opts, OptSpecifier Id) {
  const Option O = Opts.getOption(Id);
  std::string Name = O.getPrefixedName().str();

  // Add metavar, if used.
  switch (O.getKind()) {
  case Option::GroupClass: case Option::InputClass: case Option::UnknownClass:
    llvm_unreachable("Invalid option with help text.");

  case Option::MultiArgClass:
    if (const char *MetaVarName = Opts.getOptionMetaVar(Id)) {
      // For MultiArgs, metavar is full list of all argument names.
      Name += ' ';
      Name += MetaVarName;
    }
    else {
      // For MultiArgs<N>, if metavar not supplied, print <value> N times.
      for (unsigned i=0, e=O.getNumArgs(); i< e; ++i) {
        Name += " <value>";
      }
    }
    break;

  case Option::FlagClass:
    break;

  case Option::ValuesClass:
    break;

  case Option::SeparateClass: case Option::JoinedOrSeparateClass:
  case Option::RemainingArgsClass: case Option::RemainingArgsJoinedClass:
    Name += ' ';
    [[fallthrough]];
  case Option::JoinedClass: case Option::CommaJoinedClass:
  case Option::JoinedAndSeparateClass:
    if (const char *MetaVarName = Opts.getOptionMetaVar(Id))
      Name += MetaVarName;
    else
      Name += "<value>";
    break;
  }

  return Name;
}

namespace {
struct OptionInfo {
  std::string Name;
  StringRef HelpText;
};
} // namespace

static void PrintHelpOptionList(raw_ostream &OS, StringRef Title,
                                std::vector<OptionInfo> &OptionHelp) {
  OS << Title << ":\n";

  // Find the maximum option length.
  unsigned OptionFieldWidth = 0;
  for (const OptionInfo &Opt : OptionHelp) {
    // Limit the amount of padding we are willing to give up for alignment.
    unsigned Length = Opt.Name.size();
    if (Length <= 23)
      OptionFieldWidth = std::max(OptionFieldWidth, Length);
  }

  const unsigned InitialPad = 2;
  for (const OptionInfo &Opt : OptionHelp) {
    const std::string &Option = Opt.Name;
    int Pad = OptionFieldWidth + InitialPad;
    int FirstLinePad = OptionFieldWidth - int(Option.size());
    OS.indent(InitialPad) << Option;

    // Break on long option names.
    if (FirstLinePad < 0) {
      OS << "\n";
      FirstLinePad = OptionFieldWidth + InitialPad;
      Pad = FirstLinePad;
    }

    SmallVector<StringRef> Lines;
    Opt.HelpText.split(Lines, '\n');
    assert(Lines.size() && "Expected at least the first line in the help text");
    auto *LinesIt = Lines.begin();
    OS.indent(FirstLinePad + 1) << *LinesIt << '\n';
    while (Lines.end() != ++LinesIt)
      OS.indent(Pad + 1) << *LinesIt << '\n';
  }
}

static const char *getOptionHelpGroup(const OptTable &Opts, OptSpecifier Id) {
  unsigned GroupID = Opts.getOptionGroupID(Id);

  // If not in a group, return the default help group.
  if (!GroupID)
    return "OPTIONS";

  // Abuse the help text of the option groups to store the "help group"
  // name.
  //
  // FIXME: Split out option groups.
  if (const char *GroupHelp = Opts.getOptionHelpText(GroupID))
    return GroupHelp;

  // Otherwise keep looking.
  return getOptionHelpGroup(Opts, GroupID);
}

void OptTable::printHelp(raw_ostream &OS, const char *Usage, const char *Title,
                         bool ShowHidden, bool ShowAllAliases,
                         Visibility VisibilityMask) const {
  return internalPrintHelp(
      OS, Usage, Title, ShowHidden, ShowAllAliases,
      [VisibilityMask](const Info &CandidateInfo) -> bool {
        return (CandidateInfo.Visibility & VisibilityMask) == 0;
      },
      VisibilityMask);
}

void OptTable::printHelp(raw_ostream &OS, const char *Usage, const char *Title,
                         unsigned FlagsToInclude, unsigned FlagsToExclude,
                         bool ShowAllAliases) const {
  bool ShowHidden = !(FlagsToExclude & HelpHidden);
  FlagsToExclude &= ~HelpHidden;
  return internalPrintHelp(
      OS, Usage, Title, ShowHidden, ShowAllAliases,
      [FlagsToInclude, FlagsToExclude](const Info &CandidateInfo) {
        if (FlagsToInclude && !(CandidateInfo.Flags & FlagsToInclude))
          return true;
        if (CandidateInfo.Flags & FlagsToExclude)
          return true;
        return false;
      },
      Visibility(0));
}

void OptTable::internalPrintHelp(
    raw_ostream &OS, const char *Usage, const char *Title, bool ShowHidden,
    bool ShowAllAliases, std::function<bool(const Info &)> ExcludeOption,
    Visibility VisibilityMask) const {
  OS << "OVERVIEW: " << Title << "\n\n";
  OS << "USAGE: " << Usage << "\n\n";

  // Render help text into a map of group-name to a list of (option, help)
  // pairs.
  std::map<std::string, std::vector<OptionInfo>> GroupedOptionHelp;

  for (unsigned Id = 1, e = getNumOptions() + 1; Id != e; ++Id) {
    // FIXME: Split out option groups.
    if (getOptionKind(Id) == Option::GroupClass)
      continue;

    const Info &CandidateInfo = getInfo(Id);
    if (!ShowHidden && (CandidateInfo.Flags & opt::HelpHidden))
      continue;

    if (ExcludeOption(CandidateInfo))
      continue;

    // If an alias doesn't have a help text, show a help text for the aliased
    // option instead.
    const char *HelpText = getOptionHelpText(Id, VisibilityMask);
    if (!HelpText && ShowAllAliases) {
      const Option Alias = getOption(Id).getAlias();
      if (Alias.isValid())
        HelpText = getOptionHelpText(Alias.getID(), VisibilityMask);
    }

    if (HelpText && (strlen(HelpText) != 0)) {
      const char *HelpGroup = getOptionHelpGroup(*this, Id);
      const std::string &OptName = getOptionHelpName(*this, Id);
      GroupedOptionHelp[HelpGroup].push_back({OptName, HelpText});
    }
  }

  for (auto& OptionGroup : GroupedOptionHelp) {
    if (OptionGroup.first != GroupedOptionHelp.begin()->first)
      OS << "\n";
    PrintHelpOptionList(OS, OptionGroup.first, OptionGroup.second);
  }

  OS.flush();
}

GenericOptTable::GenericOptTable(ArrayRef<Info> OptionInfos, bool IgnoreCase)
    : OptTable(OptionInfos, IgnoreCase) {

  std::set<StringLiteral> TmpPrefixesUnion;
  for (auto const &Info : OptionInfos.drop_front(FirstSearchableIndex))
    TmpPrefixesUnion.insert(Info.Prefixes.begin(), Info.Prefixes.end());
  PrefixesUnionBuffer.append(TmpPrefixesUnion.begin(), TmpPrefixesUnion.end());
  buildPrefixChars();
}
