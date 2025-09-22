//===-- llvm-c++filt.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Demangle/StringViewExtras.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdlib>
#include <iostream>

using namespace llvm;

namespace {
enum ID {
  OPT_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr llvm::StringLiteral NAME##_init[] = VALUE;                  \
  static constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                   \
      NAME##_init, std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

using namespace llvm::opt;
static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class CxxfiltOptTable : public opt::GenericOptTable {
public:
  CxxfiltOptTable() : opt::GenericOptTable(InfoTable) {
    setGroupedShortOptions(true);
  }
};
} // namespace

static bool ParseParams;
static bool StripUnderscore;
static bool Types;

static StringRef ToolName;

static void error(const Twine &Message) {
  WithColor::error(errs(), ToolName) << Message << '\n';
  exit(1);
}

static std::string demangle(const std::string &Mangled) {
  using llvm::itanium_demangle::starts_with;
  std::string_view DecoratedStr = Mangled;
  bool CanHaveLeadingDot = true;
  if (StripUnderscore && DecoratedStr[0] == '_') {
    DecoratedStr.remove_prefix(1);
    CanHaveLeadingDot = false;
  }

  std::string Result;
  if (nonMicrosoftDemangle(DecoratedStr, Result, CanHaveLeadingDot,
                           ParseParams))
    return Result;

  std::string Prefix;
  char *Undecorated = nullptr;

  if (Types)
    Undecorated = itaniumDemangle(DecoratedStr, ParseParams);

  if (!Undecorated && starts_with(DecoratedStr, "__imp_")) {
    Prefix = "import thunk for ";
    Undecorated = itaniumDemangle(DecoratedStr.substr(6), ParseParams);
  }

  Result = Undecorated ? Prefix + Undecorated : Mangled;
  free(Undecorated);
  return Result;
}

// Split 'Source' on any character that fails to pass 'IsLegalChar'.  The
// returned vector consists of pairs where 'first' is the delimited word, and
// 'second' are the delimiters following that word.
static void SplitStringDelims(
    StringRef Source,
    SmallVectorImpl<std::pair<StringRef, StringRef>> &OutFragments,
    function_ref<bool(char)> IsLegalChar) {
  // The beginning of the input string.
  const auto Head = Source.begin();

  // Obtain any leading delimiters.
  auto Start = std::find_if(Head, Source.end(), IsLegalChar);
  if (Start != Head)
    OutFragments.push_back({"", Source.slice(0, Start - Head)});

  // Capture each word and the delimiters following that word.
  while (Start != Source.end()) {
    Start = std::find_if(Start, Source.end(), IsLegalChar);
    auto End = std::find_if_not(Start, Source.end(), IsLegalChar);
    auto DEnd = std::find_if(End, Source.end(), IsLegalChar);
    OutFragments.push_back({Source.slice(Start - Head, End - Head),
                            Source.slice(End - Head, DEnd - Head)});
    Start = DEnd;
  }
}

// This returns true if 'C' is a character that can show up in an
// Itanium-mangled string.
static bool IsLegalItaniumChar(char C) {
  // Itanium CXX ABI [External Names]p5.1.1:
  // '$' and '.' in mangled names are reserved for private implementations.
  return isAlnum(C) || C == '.' || C == '$' || C == '_';
}

// If 'Split' is true, then 'Mangled' is broken into individual words and each
// word is demangled.  Otherwise, the entire string is treated as a single
// mangled item.  The result is output to 'OS'.
static void demangleLine(llvm::raw_ostream &OS, StringRef Mangled, bool Split) {
  std::string Result;
  if (Split) {
    SmallVector<std::pair<StringRef, StringRef>, 16> Words;
    SplitStringDelims(Mangled, Words, IsLegalItaniumChar);
    for (const auto &Word : Words)
      Result += ::demangle(std::string(Word.first)) + Word.second.str();
  } else
    Result = ::demangle(std::string(Mangled));
  OS << Result << '\n';
  OS.flush();
}

int llvm_cxxfilt_main(int argc, char **argv, const llvm::ToolContext &) {
  BumpPtrAllocator A;
  StringSaver Saver(A);
  CxxfiltOptTable Tbl;
  ToolName = argv[0];
  opt::InputArgList Args = Tbl.parseArgs(argc, argv, OPT_UNKNOWN, Saver,
                                         [&](StringRef Msg) { error(Msg); });
  if (Args.hasArg(OPT_help)) {
    Tbl.printHelp(outs(),
                  (Twine(ToolName) + " [options] <mangled>").str().c_str(),
                  "LLVM symbol undecoration tool");
    // TODO Replace this with OptTable API once it adds extrahelp support.
    outs() << "\nPass @FILE as argument to read options from FILE.\n";
    return 0;
  }
  if (Args.hasArg(OPT_version)) {
    outs() << ToolName << '\n';
    cl::PrintVersionMessage();
    return 0;
  }

  // The default value depends on the default triple. Mach-O has symbols
  // prefixed with "_", so strip by default.
  if (opt::Arg *A =
          Args.getLastArg(OPT_strip_underscore, OPT_no_strip_underscore))
    StripUnderscore = A->getOption().matches(OPT_strip_underscore);
  else
    StripUnderscore = Triple(sys::getProcessTriple()).isOSBinFormatMachO();

  ParseParams = !Args.hasArg(OPT_no_params);

  Types = Args.hasArg(OPT_types);

  std::vector<std::string> Decorated = Args.getAllArgValues(OPT_INPUT);
  if (Decorated.empty())
    for (std::string Mangled; std::getline(std::cin, Mangled);)
      demangleLine(llvm::outs(), Mangled, true);
  else
    for (const auto &Symbol : Decorated)
      demangleLine(llvm::outs(), Symbol, false);

  return EXIT_SUCCESS;
}
