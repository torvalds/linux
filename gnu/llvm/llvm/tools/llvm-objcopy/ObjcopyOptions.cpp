//===- ObjcopyOptions.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjcopyOptions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/ObjCopy/ConfigManager.h"
#include "llvm/ObjCopy/MachO/MachOConfig.h"
#include "llvm/Object/Binary.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;
using namespace llvm::objcopy;
using namespace llvm::object;
using namespace llvm::opt;

namespace {
enum ObjcopyID {
  OBJCOPY_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(OBJCOPY_, __VA_ARGS__),
#include "ObjcopyOpts.inc"
#undef OPTION
};

namespace objcopy_opt {
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "ObjcopyOpts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info ObjcopyInfoTable[] = {
#define OPTION(...)                                                            \
  LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(OBJCOPY_, __VA_ARGS__),
#include "ObjcopyOpts.inc"
#undef OPTION
};
} // namespace objcopy_opt

class ObjcopyOptTable : public opt::GenericOptTable {
public:
  ObjcopyOptTable() : opt::GenericOptTable(objcopy_opt::ObjcopyInfoTable) {
    setGroupedShortOptions(true);
  }
};

enum InstallNameToolID {
  INSTALL_NAME_TOOL_INVALID = 0, // This is not an option ID.
#define OPTION(...)                                                            \
  LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(INSTALL_NAME_TOOL_, __VA_ARGS__),
#include "InstallNameToolOpts.inc"
#undef OPTION
};

namespace install_name_tool {

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "InstallNameToolOpts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info InstallNameToolInfoTable[] = {
#define OPTION(...)                                                            \
  LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(INSTALL_NAME_TOOL_, __VA_ARGS__),
#include "InstallNameToolOpts.inc"
#undef OPTION
};
} // namespace install_name_tool

class InstallNameToolOptTable : public opt::GenericOptTable {
public:
  InstallNameToolOptTable()
      : GenericOptTable(install_name_tool::InstallNameToolInfoTable) {}
};

enum BitcodeStripID {
  BITCODE_STRIP_INVALID = 0, // This is not an option ID.
#define OPTION(...)                                                            \
  LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(BITCODE_STRIP_, __VA_ARGS__),
#include "BitcodeStripOpts.inc"
#undef OPTION
};

namespace bitcode_strip {

#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "BitcodeStripOpts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info BitcodeStripInfoTable[] = {
#define OPTION(...)                                                            \
  LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(BITCODE_STRIP_, __VA_ARGS__),
#include "BitcodeStripOpts.inc"
#undef OPTION
};
} // namespace bitcode_strip

class BitcodeStripOptTable : public opt::GenericOptTable {
public:
  BitcodeStripOptTable()
      : opt::GenericOptTable(bitcode_strip::BitcodeStripInfoTable) {}
};

enum StripID {
  STRIP_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(STRIP_, __VA_ARGS__),
#include "StripOpts.inc"
#undef OPTION
};

namespace strip {
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "StripOpts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info StripInfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(STRIP_, __VA_ARGS__),
#include "StripOpts.inc"
#undef OPTION
};
} // namespace strip

class StripOptTable : public opt::GenericOptTable {
public:
  StripOptTable() : GenericOptTable(strip::StripInfoTable) {
    setGroupedShortOptions(true);
  }
};

} // namespace

static SectionFlag parseSectionRenameFlag(StringRef SectionName) {
  return llvm::StringSwitch<SectionFlag>(SectionName)
      .CaseLower("alloc", SectionFlag::SecAlloc)
      .CaseLower("load", SectionFlag::SecLoad)
      .CaseLower("noload", SectionFlag::SecNoload)
      .CaseLower("readonly", SectionFlag::SecReadonly)
      .CaseLower("debug", SectionFlag::SecDebug)
      .CaseLower("code", SectionFlag::SecCode)
      .CaseLower("data", SectionFlag::SecData)
      .CaseLower("rom", SectionFlag::SecRom)
      .CaseLower("merge", SectionFlag::SecMerge)
      .CaseLower("strings", SectionFlag::SecStrings)
      .CaseLower("contents", SectionFlag::SecContents)
      .CaseLower("share", SectionFlag::SecShare)
      .CaseLower("exclude", SectionFlag::SecExclude)
      .CaseLower("large", SectionFlag::SecLarge)
      .Default(SectionFlag::SecNone);
}

static Expected<SectionFlag>
parseSectionFlagSet(ArrayRef<StringRef> SectionFlags) {
  SectionFlag ParsedFlags = SectionFlag::SecNone;
  for (StringRef Flag : SectionFlags) {
    SectionFlag ParsedFlag = parseSectionRenameFlag(Flag);
    if (ParsedFlag == SectionFlag::SecNone)
      return createStringError(
          errc::invalid_argument,
          "unrecognized section flag '%s'. Flags supported for GNU "
          "compatibility: alloc, load, noload, readonly, exclude, debug, "
          "code, data, rom, share, contents, merge, strings, large",
          Flag.str().c_str());
    ParsedFlags |= ParsedFlag;
  }

  return ParsedFlags;
}

static Expected<SectionRename> parseRenameSectionValue(StringRef FlagValue) {
  if (!FlagValue.contains('='))
    return createStringError(errc::invalid_argument,
                             "bad format for --rename-section: missing '='");

  // Initial split: ".foo" = ".bar,f1,f2,..."
  auto Old2New = FlagValue.split('=');
  SectionRename SR;
  SR.OriginalName = Old2New.first;

  // Flags split: ".bar" "f1" "f2" ...
  SmallVector<StringRef, 6> NameAndFlags;
  Old2New.second.split(NameAndFlags, ',');
  SR.NewName = NameAndFlags[0];

  if (NameAndFlags.size() > 1) {
    Expected<SectionFlag> ParsedFlagSet =
        parseSectionFlagSet(ArrayRef(NameAndFlags).drop_front());
    if (!ParsedFlagSet)
      return ParsedFlagSet.takeError();
    SR.NewFlags = *ParsedFlagSet;
  }

  return SR;
}

static Expected<std::pair<StringRef, uint64_t>>
parseSetSectionAttribute(StringRef Option, StringRef FlagValue) {
  if (!FlagValue.contains('='))
    return make_error<StringError>("bad format for " + Option + ": missing '='",
                                   errc::invalid_argument);
  auto Split = StringRef(FlagValue).split('=');
  if (Split.first.empty())
    return make_error<StringError>("bad format for " + Option +
                                       ": missing section name",
                                   errc::invalid_argument);
  uint64_t Value;
  if (Split.second.getAsInteger(0, Value))
    return make_error<StringError>("invalid value for " + Option + ": '" +
                                       Split.second + "'",
                                   errc::invalid_argument);
  return std::make_pair(Split.first, Value);
}

static Expected<SectionFlagsUpdate>
parseSetSectionFlagValue(StringRef FlagValue) {
  if (!StringRef(FlagValue).contains('='))
    return createStringError(errc::invalid_argument,
                             "bad format for --set-section-flags: missing '='");

  // Initial split: ".foo" = "f1,f2,..."
  auto Section2Flags = StringRef(FlagValue).split('=');
  SectionFlagsUpdate SFU;
  SFU.Name = Section2Flags.first;

  // Flags split: "f1" "f2" ...
  SmallVector<StringRef, 6> SectionFlags;
  Section2Flags.second.split(SectionFlags, ',');
  Expected<SectionFlag> ParsedFlagSet = parseSectionFlagSet(SectionFlags);
  if (!ParsedFlagSet)
    return ParsedFlagSet.takeError();
  SFU.NewFlags = *ParsedFlagSet;

  return SFU;
}

static Expected<uint8_t> parseVisibilityType(StringRef VisType) {
  const uint8_t Invalid = 0xff;
  uint8_t type = StringSwitch<uint8_t>(VisType)
                     .Case("default", ELF::STV_DEFAULT)
                     .Case("hidden", ELF::STV_HIDDEN)
                     .Case("internal", ELF::STV_INTERNAL)
                     .Case("protected", ELF::STV_PROTECTED)
                     .Default(Invalid);
  if (type == Invalid)
    return createStringError(errc::invalid_argument,
                             "'%s' is not a valid symbol visibility",
                             VisType.str().c_str());
  return type;
}

namespace {
struct TargetInfo {
  FileFormat Format;
  MachineInfo Machine;
};
} // namespace

// FIXME: consolidate with the bfd parsing used by lld.
static const StringMap<MachineInfo> TargetMap{
    // Name, {EMachine, 64bit, LittleEndian}
    // x86
    {"elf32-i386", {ELF::EM_386, false, true}},
    {"elf32-x86-64", {ELF::EM_X86_64, false, true}},
    {"elf64-x86-64", {ELF::EM_X86_64, true, true}},
    // Intel MCU
    {"elf32-iamcu", {ELF::EM_IAMCU, false, true}},
    // ARM
    {"elf32-littlearm", {ELF::EM_ARM, false, true}},
    // ARM AArch64
    {"elf64-aarch64", {ELF::EM_AARCH64, true, true}},
    {"elf64-littleaarch64", {ELF::EM_AARCH64, true, true}},
    // RISC-V
    {"elf32-littleriscv", {ELF::EM_RISCV, false, true}},
    {"elf64-littleriscv", {ELF::EM_RISCV, true, true}},
    // PowerPC
    {"elf32-powerpc", {ELF::EM_PPC, false, false}},
    {"elf32-powerpcle", {ELF::EM_PPC, false, true}},
    {"elf64-powerpc", {ELF::EM_PPC64, true, false}},
    {"elf64-powerpcle", {ELF::EM_PPC64, true, true}},
    // MIPS
    {"elf32-bigmips", {ELF::EM_MIPS, false, false}},
    {"elf32-ntradbigmips", {ELF::EM_MIPS, false, false}},
    {"elf32-ntradlittlemips", {ELF::EM_MIPS, false, true}},
    {"elf32-tradbigmips", {ELF::EM_MIPS, false, false}},
    {"elf32-tradlittlemips", {ELF::EM_MIPS, false, true}},
    {"elf64-tradbigmips", {ELF::EM_MIPS, true, false}},
    {"elf64-tradlittlemips", {ELF::EM_MIPS, true, true}},
    // SPARC
    {"elf32-sparc", {ELF::EM_SPARC, false, false}},
    {"elf32-sparcel", {ELF::EM_SPARC, false, true}},
    // Hexagon
    {"elf32-hexagon", {ELF::EM_HEXAGON, false, true}},
    // LoongArch
    {"elf32-loongarch", {ELF::EM_LOONGARCH, false, true}},
    {"elf64-loongarch", {ELF::EM_LOONGARCH, true, true}},
    // SystemZ
    {"elf64-s390", {ELF::EM_S390, true, false}},
};

static Expected<TargetInfo>
getOutputTargetInfoByTargetName(StringRef TargetName) {
  StringRef OriginalTargetName = TargetName;
  bool IsFreeBSD = TargetName.consume_back("-freebsd");
  auto Iter = TargetMap.find(TargetName);
  if (Iter == std::end(TargetMap))
    return createStringError(errc::invalid_argument,
                             "invalid output format: '%s'",
                             OriginalTargetName.str().c_str());
  MachineInfo MI = Iter->getValue();
  if (IsFreeBSD)
    MI.OSABI = ELF::ELFOSABI_FREEBSD;

  FileFormat Format;
  if (TargetName.starts_with("elf"))
    Format = FileFormat::ELF;
  else
    // This should never happen because `TargetName` is valid (it certainly
    // exists in the TargetMap).
    llvm_unreachable("unknown target prefix");

  return {TargetInfo{Format, MI}};
}

static Error addSymbolsFromFile(NameMatcher &Symbols, BumpPtrAllocator &Alloc,
                                StringRef Filename, MatchStyle MS,
                                function_ref<Error(Error)> ErrorCallback) {
  StringSaver Saver(Alloc);
  SmallVector<StringRef, 16> Lines;
  auto BufOrErr = MemoryBuffer::getFile(Filename);
  if (!BufOrErr)
    return createFileError(Filename, BufOrErr.getError());

  BufOrErr.get()->getBuffer().split(Lines, '\n');
  for (StringRef Line : Lines) {
    // Ignore everything after '#', trim whitespace, and only add the symbol if
    // it's not empty.
    auto TrimmedLine = Line.split('#').first.trim();
    if (!TrimmedLine.empty())
      if (Error E = Symbols.addMatcher(NameOrPattern::create(
              Saver.save(TrimmedLine), MS, ErrorCallback)))
        return E;
  }

  return Error::success();
}

static Error addSymbolsToRenameFromFile(StringMap<StringRef> &SymbolsToRename,
                                        BumpPtrAllocator &Alloc,
                                        StringRef Filename) {
  StringSaver Saver(Alloc);
  SmallVector<StringRef, 16> Lines;
  auto BufOrErr = MemoryBuffer::getFile(Filename);
  if (!BufOrErr)
    return createFileError(Filename, BufOrErr.getError());

  BufOrErr.get()->getBuffer().split(Lines, '\n');
  size_t NumLines = Lines.size();
  for (size_t LineNo = 0; LineNo < NumLines; ++LineNo) {
    StringRef TrimmedLine = Lines[LineNo].split('#').first.trim();
    if (TrimmedLine.empty())
      continue;

    std::pair<StringRef, StringRef> Pair = Saver.save(TrimmedLine).split(' ');
    StringRef NewName = Pair.second.trim();
    if (NewName.empty())
      return createStringError(errc::invalid_argument,
                               "%s:%zu: missing new symbol name",
                               Filename.str().c_str(), LineNo + 1);
    SymbolsToRename.insert({Pair.first, NewName});
  }
  return Error::success();
}

template <class T> static ErrorOr<T> getAsInteger(StringRef Val) {
  T Result;
  if (Val.getAsInteger(0, Result))
    return errc::invalid_argument;
  return Result;
}

namespace {

enum class ToolType { Objcopy, Strip, InstallNameTool, BitcodeStrip };

} // anonymous namespace

static void printHelp(const opt::OptTable &OptTable, raw_ostream &OS,
                      ToolType Tool) {
  StringRef HelpText, ToolName;
  switch (Tool) {
  case ToolType::Objcopy:
    ToolName = "llvm-objcopy";
    HelpText = " [options] input [output]";
    break;
  case ToolType::Strip:
    ToolName = "llvm-strip";
    HelpText = " [options] inputs...";
    break;
  case ToolType::InstallNameTool:
    ToolName = "llvm-install-name-tool";
    HelpText = " [options] input";
    break;
  case ToolType::BitcodeStrip:
    ToolName = "llvm-bitcode-strip";
    HelpText = " [options] input";
    break;
  }
  OptTable.printHelp(OS, (ToolName + HelpText).str().c_str(),
                     (ToolName + " tool").str().c_str());
  // TODO: Replace this with libOption call once it adds extrahelp support.
  // The CommandLine library has a cl::extrahelp class to support this,
  // but libOption does not have that yet.
  OS << "\nPass @FILE as argument to read options from FILE.\n";
}

static Expected<NewSymbolInfo> parseNewSymbolInfo(StringRef FlagValue) {
  // Parse value given with --add-symbol option and create the
  // new symbol if possible. The value format for --add-symbol is:
  //
  // <name>=[<section>:]<value>[,<flags>]
  //
  // where:
  // <name> - symbol name, can be empty string
  // <section> - optional section name. If not given ABS symbol is created
  // <value> - symbol value, can be decimal or hexadecimal number prefixed
  //           with 0x.
  // <flags> - optional flags affecting symbol type, binding or visibility.
  NewSymbolInfo SI;
  StringRef Value;
  std::tie(SI.SymbolName, Value) = FlagValue.split('=');
  if (Value.empty())
    return createStringError(
        errc::invalid_argument,
        "bad format for --add-symbol, missing '=' after '%s'",
        SI.SymbolName.str().c_str());

  if (Value.contains(':')) {
    std::tie(SI.SectionName, Value) = Value.split(':');
    if (SI.SectionName.empty() || Value.empty())
      return createStringError(
          errc::invalid_argument,
          "bad format for --add-symbol, missing section name or symbol value");
  }

  SmallVector<StringRef, 6> Flags;
  Value.split(Flags, ',');
  if (Flags[0].getAsInteger(0, SI.Value))
    return createStringError(errc::invalid_argument, "bad symbol value: '%s'",
                             Flags[0].str().c_str());

  using Functor = std::function<void()>;
  SmallVector<StringRef, 6> UnsupportedFlags;
  for (size_t I = 1, NumFlags = Flags.size(); I < NumFlags; ++I)
    static_cast<Functor>(
        StringSwitch<Functor>(Flags[I])
            .CaseLower("global",
                       [&] { SI.Flags.push_back(SymbolFlag::Global); })
            .CaseLower("local", [&] { SI.Flags.push_back(SymbolFlag::Local); })
            .CaseLower("weak", [&] { SI.Flags.push_back(SymbolFlag::Weak); })
            .CaseLower("default",
                       [&] { SI.Flags.push_back(SymbolFlag::Default); })
            .CaseLower("hidden",
                       [&] { SI.Flags.push_back(SymbolFlag::Hidden); })
            .CaseLower("protected",
                       [&] { SI.Flags.push_back(SymbolFlag::Protected); })
            .CaseLower("file", [&] { SI.Flags.push_back(SymbolFlag::File); })
            .CaseLower("section",
                       [&] { SI.Flags.push_back(SymbolFlag::Section); })
            .CaseLower("object",
                       [&] { SI.Flags.push_back(SymbolFlag::Object); })
            .CaseLower("function",
                       [&] { SI.Flags.push_back(SymbolFlag::Function); })
            .CaseLower(
                "indirect-function",
                [&] { SI.Flags.push_back(SymbolFlag::IndirectFunction); })
            .CaseLower("debug", [&] { SI.Flags.push_back(SymbolFlag::Debug); })
            .CaseLower("constructor",
                       [&] { SI.Flags.push_back(SymbolFlag::Constructor); })
            .CaseLower("warning",
                       [&] { SI.Flags.push_back(SymbolFlag::Warning); })
            .CaseLower("indirect",
                       [&] { SI.Flags.push_back(SymbolFlag::Indirect); })
            .CaseLower("synthetic",
                       [&] { SI.Flags.push_back(SymbolFlag::Synthetic); })
            .CaseLower("unique-object",
                       [&] { SI.Flags.push_back(SymbolFlag::UniqueObject); })
            .StartsWithLower("before=",
                             [&] {
                               StringRef SymNamePart =
                                   Flags[I].split('=').second;

                               if (!SymNamePart.empty())
                                 SI.BeforeSyms.push_back(SymNamePart);
                             })
            .Default([&] { UnsupportedFlags.push_back(Flags[I]); }))();
  if (!UnsupportedFlags.empty())
    return createStringError(errc::invalid_argument,
                             "unsupported flag%s for --add-symbol: '%s'",
                             UnsupportedFlags.size() > 1 ? "s" : "",
                             join(UnsupportedFlags, "', '").c_str());

  return SI;
}

// Parse input option \p ArgValue and load section data. This function
// extracts section name and name of the file keeping section data from
// ArgValue, loads data from the file, and stores section name and data
// into the vector of new sections \p NewSections.
static Error loadNewSectionData(StringRef ArgValue, StringRef OptionName,
                                SmallVector<NewSectionInfo, 0> &NewSections) {
  if (!ArgValue.contains('='))
    return createStringError(errc::invalid_argument,
                             "bad format for " + OptionName + ": missing '='");

  std::pair<StringRef, StringRef> SecPair = ArgValue.split("=");
  if (SecPair.second.empty())
    return createStringError(errc::invalid_argument, "bad format for " +
                                                         OptionName +
                                                         ": missing file name");

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(SecPair.second);
  if (!BufOrErr)
    return createFileError(SecPair.second,
                           errorCodeToError(BufOrErr.getError()));

  NewSections.push_back({SecPair.first, std::move(*BufOrErr)});
  return Error::success();
}

static Expected<int64_t> parseChangeSectionLMA(StringRef ArgValue,
                                               StringRef OptionName) {
  StringRef StringValue;
  if (ArgValue.starts_with("*+")) {
    StringValue = ArgValue.slice(2, StringRef::npos);
  } else if (ArgValue.starts_with("*-")) {
    StringValue = ArgValue.slice(1, StringRef::npos);
  } else if (ArgValue.contains("=")) {
    return createStringError(errc::invalid_argument,
                             "bad format for " + OptionName +
                                 ": changing LMA to a specific value is not "
                                 "supported. Use *+val or *-val instead");
  } else if (ArgValue.contains("+") || ArgValue.contains("-")) {
    return createStringError(errc::invalid_argument,
                             "bad format for " + OptionName +
                                 ": changing a specific section LMA is not "
                                 "supported. Use *+val or *-val instead");
  }
  if (StringValue.empty())
    return createStringError(errc::invalid_argument,
                             "bad format for " + OptionName +
                                 ": missing LMA offset");

  auto LMAValue = getAsInteger<int64_t>(StringValue);
  if (!LMAValue)
    return createStringError(LMAValue.getError(),
                             "bad format for " + OptionName + ": value after " +
                                 ArgValue.slice(0, 2) + " is " + StringValue +
                                 " when it should be an integer");
  return *LMAValue;
}

// parseObjcopyOptions returns the config and sets the input arguments. If a
// help flag is set then parseObjcopyOptions will print the help messege and
// exit.
Expected<DriverConfig>
objcopy::parseObjcopyOptions(ArrayRef<const char *> RawArgsArr,
                             function_ref<Error(Error)> ErrorCallback) {
  DriverConfig DC;
  ObjcopyOptTable T;

  const char *const *DashDash =
      llvm::find_if(RawArgsArr, [](StringRef Str) { return Str == "--"; });
  ArrayRef<const char *> ArgsArr = ArrayRef(RawArgsArr.begin(), DashDash);
  if (DashDash != RawArgsArr.end())
    DashDash = std::next(DashDash);

  unsigned MissingArgumentIndex, MissingArgumentCount;
  llvm::opt::InputArgList InputArgs =
      T.ParseArgs(ArgsArr, MissingArgumentIndex, MissingArgumentCount);

  if (MissingArgumentCount)
    return createStringError(
        errc::invalid_argument,
        "argument to '%s' is missing (expected %d value(s))",
        InputArgs.getArgString(MissingArgumentIndex), MissingArgumentCount);

  if (InputArgs.size() == 0 && DashDash == RawArgsArr.end()) {
    printHelp(T, errs(), ToolType::Objcopy);
    exit(1);
  }

  if (InputArgs.hasArg(OBJCOPY_help)) {
    printHelp(T, outs(), ToolType::Objcopy);
    exit(0);
  }

  if (InputArgs.hasArg(OBJCOPY_version)) {
    outs() << "llvm-objcopy, compatible with GNU objcopy\n";
    cl::PrintVersionMessage();
    exit(0);
  }

  SmallVector<const char *, 2> Positional;

  for (auto *Arg : InputArgs.filtered(OBJCOPY_UNKNOWN))
    return createStringError(errc::invalid_argument, "unknown argument '%s'",
                             Arg->getAsString(InputArgs).c_str());

  for (auto *Arg : InputArgs.filtered(OBJCOPY_INPUT))
    Positional.push_back(Arg->getValue());
  std::copy(DashDash, RawArgsArr.end(), std::back_inserter(Positional));

  if (Positional.empty())
    return createStringError(errc::invalid_argument, "no input file specified");

  if (Positional.size() > 2)
    return createStringError(errc::invalid_argument,
                             "too many positional arguments");

  ConfigManager ConfigMgr;
  CommonConfig &Config = ConfigMgr.Common;
  COFFConfig &COFFConfig = ConfigMgr.COFF;
  ELFConfig &ELFConfig = ConfigMgr.ELF;
  MachOConfig &MachOConfig = ConfigMgr.MachO;
  Config.InputFilename = Positional[0];
  Config.OutputFilename = Positional[Positional.size() == 1 ? 0 : 1];
  if (InputArgs.hasArg(OBJCOPY_target) &&
      (InputArgs.hasArg(OBJCOPY_input_target) ||
       InputArgs.hasArg(OBJCOPY_output_target)))
    return createStringError(
        errc::invalid_argument,
        "--target cannot be used with --input-target or --output-target");

  if (InputArgs.hasArg(OBJCOPY_regex) && InputArgs.hasArg(OBJCOPY_wildcard))
    return createStringError(errc::invalid_argument,
                             "--regex and --wildcard are incompatible");

  MatchStyle SectionMatchStyle = InputArgs.hasArg(OBJCOPY_regex)
                                     ? MatchStyle::Regex
                                     : MatchStyle::Wildcard;
  MatchStyle SymbolMatchStyle
      = InputArgs.hasArg(OBJCOPY_regex)    ? MatchStyle::Regex
      : InputArgs.hasArg(OBJCOPY_wildcard) ? MatchStyle::Wildcard
                                           : MatchStyle::Literal;
  StringRef InputFormat, OutputFormat;
  if (InputArgs.hasArg(OBJCOPY_target)) {
    InputFormat = InputArgs.getLastArgValue(OBJCOPY_target);
    OutputFormat = InputArgs.getLastArgValue(OBJCOPY_target);
  } else {
    InputFormat = InputArgs.getLastArgValue(OBJCOPY_input_target);
    OutputFormat = InputArgs.getLastArgValue(OBJCOPY_output_target);
  }

  // FIXME:  Currently, we ignore the target for non-binary/ihex formats
  // explicitly specified by -I option (e.g. -Ielf32-x86-64) and guess the
  // format by llvm::object::createBinary regardless of the option value.
  Config.InputFormat = StringSwitch<FileFormat>(InputFormat)
                           .Case("binary", FileFormat::Binary)
                           .Case("ihex", FileFormat::IHex)
                           .Default(FileFormat::Unspecified);

  if (InputArgs.hasArg(OBJCOPY_new_symbol_visibility)) {
    const uint8_t Invalid = 0xff;
    StringRef VisibilityStr =
        InputArgs.getLastArgValue(OBJCOPY_new_symbol_visibility);

    ELFConfig.NewSymbolVisibility = StringSwitch<uint8_t>(VisibilityStr)
                                        .Case("default", ELF::STV_DEFAULT)
                                        .Case("hidden", ELF::STV_HIDDEN)
                                        .Case("internal", ELF::STV_INTERNAL)
                                        .Case("protected", ELF::STV_PROTECTED)
                                        .Default(Invalid);

    if (ELFConfig.NewSymbolVisibility == Invalid)
      return createStringError(errc::invalid_argument,
                               "'%s' is not a valid symbol visibility",
                               VisibilityStr.str().c_str());
  }

  for (const auto *Arg : InputArgs.filtered(OBJCOPY_subsystem)) {
    StringRef Subsystem, Version;
    std::tie(Subsystem, Version) = StringRef(Arg->getValue()).split(':');
    COFFConfig.Subsystem =
        StringSwitch<unsigned>(Subsystem.lower())
            .Case("boot_application",
                  COFF::IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION)
            .Case("console", COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI)
            .Cases("efi_application", "efi-app",
                   COFF::IMAGE_SUBSYSTEM_EFI_APPLICATION)
            .Cases("efi_boot_service_driver", "efi-bsd",
                   COFF::IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER)
            .Case("efi_rom", COFF::IMAGE_SUBSYSTEM_EFI_ROM)
            .Cases("efi_runtime_driver", "efi-rtd",
                   COFF::IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER)
            .Case("native", COFF::IMAGE_SUBSYSTEM_NATIVE)
            .Case("posix", COFF::IMAGE_SUBSYSTEM_POSIX_CUI)
            .Case("windows", COFF::IMAGE_SUBSYSTEM_WINDOWS_GUI)
            .Default(COFF::IMAGE_SUBSYSTEM_UNKNOWN);
    if (*COFFConfig.Subsystem == COFF::IMAGE_SUBSYSTEM_UNKNOWN)
      return createStringError(errc::invalid_argument,
                               "'%s' is not a valid subsystem",
                               Subsystem.str().c_str());
    if (!Version.empty()) {
      StringRef Major, Minor;
      std::tie(Major, Minor) = Version.split('.');
      unsigned Number;
      if (Major.getAsInteger(10, Number))
        return createStringError(errc::invalid_argument,
                                 "'%s' is not a valid subsystem major version",
                                 Major.str().c_str());
      COFFConfig.MajorSubsystemVersion = Number;
      Number = 0;
      if (!Minor.empty() && Minor.getAsInteger(10, Number))
        return createStringError(errc::invalid_argument,
                                 "'%s' is not a valid subsystem minor version",
                                 Minor.str().c_str());
      COFFConfig.MinorSubsystemVersion = Number;
    }
  }

  Config.OutputFormat = StringSwitch<FileFormat>(OutputFormat)
                            .Case("binary", FileFormat::Binary)
                            .Case("ihex", FileFormat::IHex)
                            .Case("srec", FileFormat::SREC)
                            .Default(FileFormat::Unspecified);
  if (Config.OutputFormat == FileFormat::Unspecified) {
    if (OutputFormat.empty()) {
      Config.OutputFormat = Config.InputFormat;
    } else {
      Expected<TargetInfo> Target =
          getOutputTargetInfoByTargetName(OutputFormat);
      if (!Target)
        return Target.takeError();
      Config.OutputFormat = Target->Format;
      Config.OutputArch = Target->Machine;
    }
  }

  if (const auto *A = InputArgs.getLastArg(OBJCOPY_compress_debug_sections)) {
    Config.CompressionType = StringSwitch<DebugCompressionType>(A->getValue())
                                 .Case("zlib", DebugCompressionType::Zlib)
                                 .Case("zstd", DebugCompressionType::Zstd)
                                 .Default(DebugCompressionType::None);
    if (Config.CompressionType == DebugCompressionType::None) {
      return createStringError(
          errc::invalid_argument,
          "invalid or unsupported --compress-debug-sections format: %s",
          A->getValue());
    }
    if (const char *Reason = compression::getReasonIfUnsupported(
            compression::formatFor(Config.CompressionType)))
      return createStringError(errc::invalid_argument, Reason);
  }

  for (const auto *A : InputArgs.filtered(OBJCOPY_compress_sections)) {
    SmallVector<StringRef, 0> Fields;
    StringRef(A->getValue()).split(Fields, '=');
    if (Fields.size() != 2 || Fields[1].empty()) {
      return createStringError(
          errc::invalid_argument,
          A->getSpelling() +
              ": parse error, not 'section-glob=[none|zlib|zstd]'");
    }

    auto Type = StringSwitch<DebugCompressionType>(Fields[1])
                    .Case("zlib", DebugCompressionType::Zlib)
                    .Case("zstd", DebugCompressionType::Zstd)
                    .Default(DebugCompressionType::None);
    if (Type == DebugCompressionType::None && Fields[1] != "none") {
      return createStringError(
          errc::invalid_argument,
          "invalid or unsupported --compress-sections format: %s",
          A->getValue());
    }

    auto &P = Config.compressSections.emplace_back();
    P.second = Type;
    auto Matcher =
        NameOrPattern::create(Fields[0], SectionMatchStyle, ErrorCallback);
    // =none allows overriding a previous =zlib or =zstd. Reject negative
    // patterns, which would be confusing.
    if (Matcher && !Matcher->isPositiveMatch()) {
      return createStringError(
          errc::invalid_argument,
          "--compress-sections: negative pattern is unsupported");
    }
    if (Error E = P.first.addMatcher(std::move(Matcher)))
      return std::move(E);
  }

  Config.AddGnuDebugLink = InputArgs.getLastArgValue(OBJCOPY_add_gnu_debuglink);
  // The gnu_debuglink's target is expected to not change or else its CRC would
  // become invalidated and get rejected. We can avoid recalculating the
  // checksum for every target file inside an archive by precomputing the CRC
  // here. This prevents a significant amount of I/O.
  if (!Config.AddGnuDebugLink.empty()) {
    auto DebugOrErr = MemoryBuffer::getFile(Config.AddGnuDebugLink);
    if (!DebugOrErr)
      return createFileError(Config.AddGnuDebugLink, DebugOrErr.getError());
    auto Debug = std::move(*DebugOrErr);
    Config.GnuDebugLinkCRC32 =
        llvm::crc32(arrayRefFromStringRef(Debug->getBuffer()));
  }
  Config.SplitDWO = InputArgs.getLastArgValue(OBJCOPY_split_dwo);

  Config.SymbolsPrefix = InputArgs.getLastArgValue(OBJCOPY_prefix_symbols);
  Config.SymbolsPrefixRemove =
      InputArgs.getLastArgValue(OBJCOPY_remove_symbol_prefix);

  Config.AllocSectionsPrefix =
      InputArgs.getLastArgValue(OBJCOPY_prefix_alloc_sections);
  if (auto Arg = InputArgs.getLastArg(OBJCOPY_extract_partition))
    Config.ExtractPartition = Arg->getValue();

  if (const auto *A = InputArgs.getLastArg(OBJCOPY_gap_fill)) {
    if (Config.OutputFormat != FileFormat::Binary)
      return createStringError(
          errc::invalid_argument,
          "'--gap-fill' is only supported for binary output");
    ErrorOr<uint64_t> Val = getAsInteger<uint64_t>(A->getValue());
    if (!Val)
      return createStringError(Val.getError(), "--gap-fill: bad number: %s",
                               A->getValue());
    uint8_t ByteVal = Val.get();
    if (ByteVal != Val.get())
      return createStringError(std::errc::value_too_large,
                               "gap-fill value %s is out of range (0 to 0xff)",
                               A->getValue());
    Config.GapFill = ByteVal;
  }

  if (const auto *A = InputArgs.getLastArg(OBJCOPY_pad_to)) {
    if (Config.OutputFormat != FileFormat::Binary)
      return createStringError(
          errc::invalid_argument,
          "'--pad-to' is only supported for binary output");
    ErrorOr<uint64_t> Addr = getAsInteger<uint64_t>(A->getValue());
    if (!Addr)
      return createStringError(Addr.getError(), "--pad-to: bad number: %s",
                               A->getValue());
    Config.PadTo = *Addr;
  }

  if (const auto *Arg = InputArgs.getLastArg(OBJCOPY_change_section_lma)) {
    Expected<int64_t> LMAValue =
        parseChangeSectionLMA(Arg->getValue(), Arg->getSpelling());
    if (!LMAValue)
      return LMAValue.takeError();
    Config.ChangeSectionLMAValAll = *LMAValue;
  }

  for (auto *Arg : InputArgs.filtered(OBJCOPY_redefine_symbol)) {
    if (!StringRef(Arg->getValue()).contains('='))
      return createStringError(errc::invalid_argument,
                               "bad format for --redefine-sym");
    auto Old2New = StringRef(Arg->getValue()).split('=');
    if (!Config.SymbolsToRename.insert(Old2New).second)
      return createStringError(errc::invalid_argument,
                               "multiple redefinition of symbol '%s'",
                               Old2New.first.str().c_str());
  }

  for (auto *Arg : InputArgs.filtered(OBJCOPY_redefine_symbols))
    if (Error E = addSymbolsToRenameFromFile(Config.SymbolsToRename, DC.Alloc,
                                             Arg->getValue()))
      return std::move(E);

  for (auto *Arg : InputArgs.filtered(OBJCOPY_rename_section)) {
    Expected<SectionRename> SR =
        parseRenameSectionValue(StringRef(Arg->getValue()));
    if (!SR)
      return SR.takeError();
    if (!Config.SectionsToRename.try_emplace(SR->OriginalName, *SR).second)
      return createStringError(errc::invalid_argument,
                               "multiple renames of section '%s'",
                               SR->OriginalName.str().c_str());
  }
  for (auto *Arg : InputArgs.filtered(OBJCOPY_set_section_alignment)) {
    Expected<std::pair<StringRef, uint64_t>> NameAndAlign =
        parseSetSectionAttribute("--set-section-alignment", Arg->getValue());
    if (!NameAndAlign)
      return NameAndAlign.takeError();
    Config.SetSectionAlignment[NameAndAlign->first] = NameAndAlign->second;
  }
  for (auto *Arg : InputArgs.filtered(OBJCOPY_set_section_flags)) {
    Expected<SectionFlagsUpdate> SFU =
        parseSetSectionFlagValue(Arg->getValue());
    if (!SFU)
      return SFU.takeError();
    if (!Config.SetSectionFlags.try_emplace(SFU->Name, *SFU).second)
      return createStringError(
          errc::invalid_argument,
          "--set-section-flags set multiple times for section '%s'",
          SFU->Name.str().c_str());
  }
  for (auto *Arg : InputArgs.filtered(OBJCOPY_set_section_type)) {
    Expected<std::pair<StringRef, uint64_t>> NameAndType =
        parseSetSectionAttribute("--set-section-type", Arg->getValue());
    if (!NameAndType)
      return NameAndType.takeError();
    Config.SetSectionType[NameAndType->first] = NameAndType->second;
  }
  // Prohibit combinations of --set-section-{flags,type} when the section name
  // is used as the destination of a --rename-section.
  for (const auto &E : Config.SectionsToRename) {
    const SectionRename &SR = E.second;
    auto Err = [&](const char *Option) {
      return createStringError(
          errc::invalid_argument,
          "--set-section-%s=%s conflicts with --rename-section=%s=%s", Option,
          SR.NewName.str().c_str(), SR.OriginalName.str().c_str(),
          SR.NewName.str().c_str());
    };
    if (Config.SetSectionFlags.count(SR.NewName))
      return Err("flags");
    if (Config.SetSectionType.count(SR.NewName))
      return Err("type");
  }

  for (auto *Arg : InputArgs.filtered(OBJCOPY_remove_section))
    if (Error E = Config.ToRemove.addMatcher(NameOrPattern::create(
            Arg->getValue(), SectionMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_keep_section))
    if (Error E = Config.KeepSection.addMatcher(NameOrPattern::create(
            Arg->getValue(), SectionMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_only_section))
    if (Error E = Config.OnlySection.addMatcher(NameOrPattern::create(
            Arg->getValue(), SectionMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_add_section)) {
    if (Error Err = loadNewSectionData(Arg->getValue(), "--add-section",
                                       Config.AddSection))
      return std::move(Err);
  }
  for (auto *Arg : InputArgs.filtered(OBJCOPY_update_section)) {
    if (Error Err = loadNewSectionData(Arg->getValue(), "--update-section",
                                       Config.UpdateSection))
      return std::move(Err);
  }
  for (auto *Arg : InputArgs.filtered(OBJCOPY_dump_section)) {
    StringRef Value(Arg->getValue());
    if (Value.split('=').second.empty())
      return createStringError(
          errc::invalid_argument,
          "bad format for --dump-section, expected section=file");
    Config.DumpSection.push_back(Value);
  }
  Config.StripAll = InputArgs.hasArg(OBJCOPY_strip_all);
  Config.StripAllGNU = InputArgs.hasArg(OBJCOPY_strip_all_gnu);
  Config.StripDebug = InputArgs.hasArg(OBJCOPY_strip_debug);
  Config.StripDWO = InputArgs.hasArg(OBJCOPY_strip_dwo);
  Config.StripSections = InputArgs.hasArg(OBJCOPY_strip_sections);
  Config.StripNonAlloc = InputArgs.hasArg(OBJCOPY_strip_non_alloc);
  Config.StripUnneeded = InputArgs.hasArg(OBJCOPY_strip_unneeded);
  Config.ExtractDWO = InputArgs.hasArg(OBJCOPY_extract_dwo);
  Config.ExtractMainPartition =
      InputArgs.hasArg(OBJCOPY_extract_main_partition);
  ELFConfig.LocalizeHidden = InputArgs.hasArg(OBJCOPY_localize_hidden);
  Config.Weaken = InputArgs.hasArg(OBJCOPY_weaken);
  if (auto *Arg =
          InputArgs.getLastArg(OBJCOPY_discard_all, OBJCOPY_discard_locals)) {
    Config.DiscardMode = Arg->getOption().matches(OBJCOPY_discard_all)
                             ? DiscardType::All
                             : DiscardType::Locals;
  }

  ELFConfig.VerifyNoteSections = InputArgs.hasFlag(
      OBJCOPY_verify_note_sections, OBJCOPY_no_verify_note_sections, true);

  Config.OnlyKeepDebug = InputArgs.hasArg(OBJCOPY_only_keep_debug);
  ELFConfig.KeepFileSymbols = InputArgs.hasArg(OBJCOPY_keep_file_symbols);
  MachOConfig.KeepUndefined = InputArgs.hasArg(OBJCOPY_keep_undefined);
  Config.DecompressDebugSections =
      InputArgs.hasArg(OBJCOPY_decompress_debug_sections);
  if (Config.DiscardMode == DiscardType::All) {
    Config.StripDebug = true;
    ELFConfig.KeepFileSymbols = true;
  }
  for (auto *Arg : InputArgs.filtered(OBJCOPY_localize_symbol))
    if (Error E = Config.SymbolsToLocalize.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_localize_symbols))
    if (Error E = addSymbolsFromFile(Config.SymbolsToLocalize, DC.Alloc,
                                     Arg->getValue(), SymbolMatchStyle,
                                     ErrorCallback))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_keep_global_symbol))
    if (Error E = Config.SymbolsToKeepGlobal.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_keep_global_symbols))
    if (Error E = addSymbolsFromFile(Config.SymbolsToKeepGlobal, DC.Alloc,
                                     Arg->getValue(), SymbolMatchStyle,
                                     ErrorCallback))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_globalize_symbol))
    if (Error E = Config.SymbolsToGlobalize.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_globalize_symbols))
    if (Error E = addSymbolsFromFile(Config.SymbolsToGlobalize, DC.Alloc,
                                     Arg->getValue(), SymbolMatchStyle,
                                     ErrorCallback))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_weaken_symbol))
    if (Error E = Config.SymbolsToWeaken.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_weaken_symbols))
    if (Error E = addSymbolsFromFile(Config.SymbolsToWeaken, DC.Alloc,
                                     Arg->getValue(), SymbolMatchStyle,
                                     ErrorCallback))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_strip_symbol))
    if (Error E = Config.SymbolsToRemove.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_strip_symbols))
    if (Error E = addSymbolsFromFile(Config.SymbolsToRemove, DC.Alloc,
                                     Arg->getValue(), SymbolMatchStyle,
                                     ErrorCallback))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_strip_unneeded_symbol))
    if (Error E =
            Config.UnneededSymbolsToRemove.addMatcher(NameOrPattern::create(
                Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_strip_unneeded_symbols))
    if (Error E = addSymbolsFromFile(Config.UnneededSymbolsToRemove, DC.Alloc,
                                     Arg->getValue(), SymbolMatchStyle,
                                     ErrorCallback))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_keep_symbol))
    if (Error E = Config.SymbolsToKeep.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_keep_symbols))
    if (Error E =
            addSymbolsFromFile(Config.SymbolsToKeep, DC.Alloc, Arg->getValue(),
                               SymbolMatchStyle, ErrorCallback))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_skip_symbol))
    if (Error E = Config.SymbolsToSkip.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_skip_symbols))
    if (Error E =
            addSymbolsFromFile(Config.SymbolsToSkip, DC.Alloc, Arg->getValue(),
                               SymbolMatchStyle, ErrorCallback))
      return std::move(E);
  for (auto *Arg : InputArgs.filtered(OBJCOPY_add_symbol)) {
    Expected<NewSymbolInfo> SymInfo = parseNewSymbolInfo(Arg->getValue());
    if (!SymInfo)
      return SymInfo.takeError();

    Config.SymbolsToAdd.push_back(*SymInfo);
  }
  for (auto *Arg : InputArgs.filtered(OBJCOPY_set_symbol_visibility)) {
    if (!StringRef(Arg->getValue()).contains('='))
      return createStringError(errc::invalid_argument,
                               "bad format for --set-symbol-visibility");
    auto [Sym, Visibility] = StringRef(Arg->getValue()).split('=');
    Expected<uint8_t> Type = parseVisibilityType(Visibility);
    if (!Type)
      return Type.takeError();
    ELFConfig.SymbolsToSetVisibility.emplace_back(NameMatcher(), *Type);
    if (Error E = ELFConfig.SymbolsToSetVisibility.back().first.addMatcher(
            NameOrPattern::create(Sym, SymbolMatchStyle, ErrorCallback)))
      return std::move(E);
  }
  for (auto *Arg : InputArgs.filtered(OBJCOPY_set_symbols_visibility)) {
    if (!StringRef(Arg->getValue()).contains('='))
      return createStringError(errc::invalid_argument,
                               "bad format for --set-symbols-visibility");
    auto [File, Visibility] = StringRef(Arg->getValue()).split('=');
    Expected<uint8_t> Type = parseVisibilityType(Visibility);
    if (!Type)
      return Type.takeError();
    ELFConfig.SymbolsToSetVisibility.emplace_back(NameMatcher(), *Type);
    if (Error E =
            addSymbolsFromFile(ELFConfig.SymbolsToSetVisibility.back().first,
                               DC.Alloc, File, SymbolMatchStyle, ErrorCallback))
      return std::move(E);
  }

  ELFConfig.AllowBrokenLinks = InputArgs.hasArg(OBJCOPY_allow_broken_links);

  Config.DeterministicArchives = InputArgs.hasFlag(
      OBJCOPY_enable_deterministic_archives,
      OBJCOPY_disable_deterministic_archives, /*default=*/true);

  Config.PreserveDates = InputArgs.hasArg(OBJCOPY_preserve_dates);

  if (Config.PreserveDates &&
      (Config.OutputFilename == "-" || Config.InputFilename == "-"))
    return createStringError(errc::invalid_argument,
                             "--preserve-dates requires a file");

  for (auto *Arg : InputArgs)
    if (Arg->getOption().matches(OBJCOPY_set_start)) {
      auto EAddr = getAsInteger<uint64_t>(Arg->getValue());
      if (!EAddr)
        return createStringError(
            EAddr.getError(), "bad entry point address: '%s'", Arg->getValue());

      ELFConfig.EntryExpr = [EAddr](uint64_t) { return *EAddr; };
    } else if (Arg->getOption().matches(OBJCOPY_change_start)) {
      auto EIncr = getAsInteger<int64_t>(Arg->getValue());
      if (!EIncr)
        return createStringError(EIncr.getError(),
                                 "bad entry point increment: '%s'",
                                 Arg->getValue());
      auto Expr = ELFConfig.EntryExpr ? std::move(ELFConfig.EntryExpr)
                                      : [](uint64_t A) { return A; };
      ELFConfig.EntryExpr = [Expr, EIncr](uint64_t EAddr) {
        return Expr(EAddr) + *EIncr;
      };
    }

  if (Config.DecompressDebugSections &&
      Config.CompressionType != DebugCompressionType::None) {
    return createStringError(
        errc::invalid_argument,
        "cannot specify both --compress-debug-sections and "
        "--decompress-debug-sections");
  }

  if (Config.ExtractPartition && Config.ExtractMainPartition)
    return createStringError(errc::invalid_argument,
                             "cannot specify --extract-partition together with "
                             "--extract-main-partition");

  DC.CopyConfigs.push_back(std::move(ConfigMgr));
  return std::move(DC);
}

// parseInstallNameToolOptions returns the config and sets the input arguments.
// If a help flag is set then parseInstallNameToolOptions will print the help
// messege and exit.
Expected<DriverConfig>
objcopy::parseInstallNameToolOptions(ArrayRef<const char *> ArgsArr) {
  DriverConfig DC;
  ConfigManager ConfigMgr;
  CommonConfig &Config = ConfigMgr.Common;
  MachOConfig &MachOConfig = ConfigMgr.MachO;
  InstallNameToolOptTable T;
  unsigned MissingArgumentIndex, MissingArgumentCount;
  llvm::opt::InputArgList InputArgs =
      T.ParseArgs(ArgsArr, MissingArgumentIndex, MissingArgumentCount);

  if (MissingArgumentCount)
    return createStringError(
        errc::invalid_argument,
        "missing argument to " +
            StringRef(InputArgs.getArgString(MissingArgumentIndex)) +
            " option");

  if (InputArgs.size() == 0) {
    printHelp(T, errs(), ToolType::InstallNameTool);
    exit(1);
  }

  if (InputArgs.hasArg(INSTALL_NAME_TOOL_help)) {
    printHelp(T, outs(), ToolType::InstallNameTool);
    exit(0);
  }

  if (InputArgs.hasArg(INSTALL_NAME_TOOL_version)) {
    outs() << "llvm-install-name-tool, compatible with cctools "
              "install_name_tool\n";
    cl::PrintVersionMessage();
    exit(0);
  }

  for (auto *Arg : InputArgs.filtered(INSTALL_NAME_TOOL_add_rpath))
    MachOConfig.RPathToAdd.push_back(Arg->getValue());

  for (auto *Arg : InputArgs.filtered(INSTALL_NAME_TOOL_prepend_rpath))
    MachOConfig.RPathToPrepend.push_back(Arg->getValue());

  for (auto *Arg : InputArgs.filtered(INSTALL_NAME_TOOL_delete_rpath)) {
    StringRef RPath = Arg->getValue();

    // Cannot add and delete the same rpath at the same time.
    if (is_contained(MachOConfig.RPathToAdd, RPath))
      return createStringError(
          errc::invalid_argument,
          "cannot specify both -add_rpath '%s' and -delete_rpath '%s'",
          RPath.str().c_str(), RPath.str().c_str());
    if (is_contained(MachOConfig.RPathToPrepend, RPath))
      return createStringError(
          errc::invalid_argument,
          "cannot specify both -prepend_rpath '%s' and -delete_rpath '%s'",
          RPath.str().c_str(), RPath.str().c_str());

    MachOConfig.RPathsToRemove.insert(RPath);
  }

  for (auto *Arg : InputArgs.filtered(INSTALL_NAME_TOOL_rpath)) {
    StringRef Old = Arg->getValue(0);
    StringRef New = Arg->getValue(1);

    auto Match = [=](StringRef RPath) { return RPath == Old || RPath == New; };

    // Cannot specify duplicate -rpath entries
    auto It1 = find_if(
        MachOConfig.RPathsToUpdate,
        [&Match](const DenseMap<StringRef, StringRef>::value_type &OldNew) {
          return Match(OldNew.getFirst()) || Match(OldNew.getSecond());
        });
    if (It1 != MachOConfig.RPathsToUpdate.end())
      return createStringError(errc::invalid_argument,
                               "cannot specify both -rpath '" +
                                   It1->getFirst() + "' '" + It1->getSecond() +
                                   "' and -rpath '" + Old + "' '" + New + "'");

    // Cannot specify the same rpath under both -delete_rpath and -rpath
    auto It2 = find_if(MachOConfig.RPathsToRemove, Match);
    if (It2 != MachOConfig.RPathsToRemove.end())
      return createStringError(errc::invalid_argument,
                               "cannot specify both -delete_rpath '" + *It2 +
                                   "' and -rpath '" + Old + "' '" + New + "'");

    // Cannot specify the same rpath under both -add_rpath and -rpath
    auto It3 = find_if(MachOConfig.RPathToAdd, Match);
    if (It3 != MachOConfig.RPathToAdd.end())
      return createStringError(errc::invalid_argument,
                               "cannot specify both -add_rpath '" + *It3 +
                                   "' and -rpath '" + Old + "' '" + New + "'");

    // Cannot specify the same rpath under both -prepend_rpath and -rpath.
    auto It4 = find_if(MachOConfig.RPathToPrepend, Match);
    if (It4 != MachOConfig.RPathToPrepend.end())
      return createStringError(errc::invalid_argument,
                               "cannot specify both -prepend_rpath '" + *It4 +
                                   "' and -rpath '" + Old + "' '" + New + "'");

    MachOConfig.RPathsToUpdate.insert({Old, New});
  }

  if (auto *Arg = InputArgs.getLastArg(INSTALL_NAME_TOOL_id)) {
    MachOConfig.SharedLibId = Arg->getValue();
    if (MachOConfig.SharedLibId->empty())
      return createStringError(errc::invalid_argument,
                               "cannot specify an empty id");
  }

  for (auto *Arg : InputArgs.filtered(INSTALL_NAME_TOOL_change))
    MachOConfig.InstallNamesToUpdate.insert(
        {Arg->getValue(0), Arg->getValue(1)});

  MachOConfig.RemoveAllRpaths =
      InputArgs.hasArg(INSTALL_NAME_TOOL_delete_all_rpaths);

  SmallVector<StringRef, 2> Positional;
  for (auto *Arg : InputArgs.filtered(INSTALL_NAME_TOOL_UNKNOWN))
    return createStringError(errc::invalid_argument, "unknown argument '%s'",
                             Arg->getAsString(InputArgs).c_str());
  for (auto *Arg : InputArgs.filtered(INSTALL_NAME_TOOL_INPUT))
    Positional.push_back(Arg->getValue());
  if (Positional.empty())
    return createStringError(errc::invalid_argument, "no input file specified");
  if (Positional.size() > 1)
    return createStringError(
        errc::invalid_argument,
        "llvm-install-name-tool expects a single input file");
  Config.InputFilename = Positional[0];
  Config.OutputFilename = Positional[0];

  Expected<OwningBinary<Binary>> BinaryOrErr =
      createBinary(Config.InputFilename);
  if (!BinaryOrErr)
    return createFileError(Config.InputFilename, BinaryOrErr.takeError());
  auto *Binary = (*BinaryOrErr).getBinary();
  if (!Binary->isMachO() && !Binary->isMachOUniversalBinary())
    return createStringError(errc::invalid_argument,
                             "input file: %s is not a Mach-O file",
                             Config.InputFilename.str().c_str());

  DC.CopyConfigs.push_back(std::move(ConfigMgr));
  return std::move(DC);
}

Expected<DriverConfig>
objcopy::parseBitcodeStripOptions(ArrayRef<const char *> ArgsArr,
                                  function_ref<Error(Error)> ErrorCallback) {
  DriverConfig DC;
  ConfigManager ConfigMgr;
  CommonConfig &Config = ConfigMgr.Common;
  MachOConfig &MachOConfig = ConfigMgr.MachO;
  BitcodeStripOptTable T;
  unsigned MissingArgumentIndex, MissingArgumentCount;
  opt::InputArgList InputArgs =
      T.ParseArgs(ArgsArr, MissingArgumentIndex, MissingArgumentCount);

  if (InputArgs.size() == 0) {
    printHelp(T, errs(), ToolType::BitcodeStrip);
    exit(1);
  }

  if (InputArgs.hasArg(BITCODE_STRIP_help)) {
    printHelp(T, outs(), ToolType::BitcodeStrip);
    exit(0);
  }

  if (InputArgs.hasArg(BITCODE_STRIP_version)) {
    outs() << "llvm-bitcode-strip, compatible with cctools "
              "bitcode_strip\n";
    cl::PrintVersionMessage();
    exit(0);
  }

  for (auto *Arg : InputArgs.filtered(BITCODE_STRIP_UNKNOWN))
    return createStringError(errc::invalid_argument, "unknown argument '%s'",
                             Arg->getAsString(InputArgs).c_str());

  SmallVector<StringRef, 2> Positional;
  for (auto *Arg : InputArgs.filtered(BITCODE_STRIP_INPUT))
    Positional.push_back(Arg->getValue());
  if (Positional.size() > 1)
    return createStringError(errc::invalid_argument,
                             "llvm-bitcode-strip expects a single input file");
  assert(!Positional.empty());
  Config.InputFilename = Positional[0];

  if (!InputArgs.hasArg(BITCODE_STRIP_output)) {
    return createStringError(errc::invalid_argument,
                             "-o is a required argument");
  }
  Config.OutputFilename = InputArgs.getLastArgValue(BITCODE_STRIP_output);

  if (!InputArgs.hasArg(BITCODE_STRIP_remove))
    return createStringError(errc::invalid_argument, "no action specified");

  // We only support -r for now, which removes all bitcode sections and
  // the __LLVM segment if it's now empty.
  cantFail(Config.ToRemove.addMatcher(NameOrPattern::create(
      "__LLVM,__asm", MatchStyle::Literal, ErrorCallback)));
  cantFail(Config.ToRemove.addMatcher(NameOrPattern::create(
      "__LLVM,__bitcode", MatchStyle::Literal, ErrorCallback)));
  cantFail(Config.ToRemove.addMatcher(NameOrPattern::create(
      "__LLVM,__bundle", MatchStyle::Literal, ErrorCallback)));
  cantFail(Config.ToRemove.addMatcher(NameOrPattern::create(
      "__LLVM,__cmdline", MatchStyle::Literal, ErrorCallback)));
  cantFail(Config.ToRemove.addMatcher(NameOrPattern::create(
      "__LLVM,__swift_cmdline", MatchStyle::Literal, ErrorCallback)));
  MachOConfig.EmptySegmentsToRemove.insert("__LLVM");

  DC.CopyConfigs.push_back(std::move(ConfigMgr));
  return std::move(DC);
}

// parseStripOptions returns the config and sets the input arguments. If a
// help flag is set then parseStripOptions will print the help messege and
// exit.
Expected<DriverConfig>
objcopy::parseStripOptions(ArrayRef<const char *> RawArgsArr,
                           function_ref<Error(Error)> ErrorCallback) {
  const char *const *DashDash =
      llvm::find_if(RawArgsArr, [](StringRef Str) { return Str == "--"; });
  ArrayRef<const char *> ArgsArr = ArrayRef(RawArgsArr.begin(), DashDash);
  if (DashDash != RawArgsArr.end())
    DashDash = std::next(DashDash);

  StripOptTable T;
  unsigned MissingArgumentIndex, MissingArgumentCount;
  llvm::opt::InputArgList InputArgs =
      T.ParseArgs(ArgsArr, MissingArgumentIndex, MissingArgumentCount);

  if (InputArgs.size() == 0 && DashDash == RawArgsArr.end()) {
    printHelp(T, errs(), ToolType::Strip);
    exit(1);
  }

  if (InputArgs.hasArg(STRIP_help)) {
    printHelp(T, outs(), ToolType::Strip);
    exit(0);
  }

  if (InputArgs.hasArg(STRIP_version)) {
    outs() << "llvm-strip, compatible with GNU strip\n";
    cl::PrintVersionMessage();
    exit(0);
  }

  SmallVector<StringRef, 2> Positional;
  for (auto *Arg : InputArgs.filtered(STRIP_UNKNOWN))
    return createStringError(errc::invalid_argument, "unknown argument '%s'",
                             Arg->getAsString(InputArgs).c_str());
  for (auto *Arg : InputArgs.filtered(STRIP_INPUT))
    Positional.push_back(Arg->getValue());
  std::copy(DashDash, RawArgsArr.end(), std::back_inserter(Positional));

  if (Positional.empty())
    return createStringError(errc::invalid_argument, "no input file specified");

  if (Positional.size() > 1 && InputArgs.hasArg(STRIP_output))
    return createStringError(
        errc::invalid_argument,
        "multiple input files cannot be used in combination with -o");

  ConfigManager ConfigMgr;
  CommonConfig &Config = ConfigMgr.Common;
  ELFConfig &ELFConfig = ConfigMgr.ELF;
  MachOConfig &MachOConfig = ConfigMgr.MachO;

  if (InputArgs.hasArg(STRIP_regex) && InputArgs.hasArg(STRIP_wildcard))
    return createStringError(errc::invalid_argument,
                             "--regex and --wildcard are incompatible");
  MatchStyle SectionMatchStyle =
      InputArgs.hasArg(STRIP_regex) ? MatchStyle::Regex : MatchStyle::Wildcard;
  MatchStyle SymbolMatchStyle
      = InputArgs.hasArg(STRIP_regex)    ? MatchStyle::Regex
      : InputArgs.hasArg(STRIP_wildcard) ? MatchStyle::Wildcard
                                         : MatchStyle::Literal;
  ELFConfig.AllowBrokenLinks = InputArgs.hasArg(STRIP_allow_broken_links);
  Config.StripDebug = InputArgs.hasArg(STRIP_strip_debug);

  if (auto *Arg = InputArgs.getLastArg(STRIP_discard_all, STRIP_discard_locals))
    Config.DiscardMode = Arg->getOption().matches(STRIP_discard_all)
                             ? DiscardType::All
                             : DiscardType::Locals;
  Config.StripSections = InputArgs.hasArg(STRIP_strip_sections);
  Config.StripUnneeded = InputArgs.hasArg(STRIP_strip_unneeded);
  if (auto Arg = InputArgs.getLastArg(STRIP_strip_all, STRIP_no_strip_all))
    Config.StripAll = Arg->getOption().getID() == STRIP_strip_all;
  Config.StripAllGNU = InputArgs.hasArg(STRIP_strip_all_gnu);
  MachOConfig.StripSwiftSymbols = InputArgs.hasArg(STRIP_strip_swift_symbols);
  Config.OnlyKeepDebug = InputArgs.hasArg(STRIP_only_keep_debug);
  ELFConfig.KeepFileSymbols = InputArgs.hasArg(STRIP_keep_file_symbols);
  MachOConfig.KeepUndefined = InputArgs.hasArg(STRIP_keep_undefined);

  for (auto *Arg : InputArgs.filtered(STRIP_keep_section))
    if (Error E = Config.KeepSection.addMatcher(NameOrPattern::create(
            Arg->getValue(), SectionMatchStyle, ErrorCallback)))
      return std::move(E);

  for (auto *Arg : InputArgs.filtered(STRIP_remove_section))
    if (Error E = Config.ToRemove.addMatcher(NameOrPattern::create(
            Arg->getValue(), SectionMatchStyle, ErrorCallback)))
      return std::move(E);

  for (auto *Arg : InputArgs.filtered(STRIP_strip_symbol))
    if (Error E = Config.SymbolsToRemove.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);

  for (auto *Arg : InputArgs.filtered(STRIP_keep_symbol))
    if (Error E = Config.SymbolsToKeep.addMatcher(NameOrPattern::create(
            Arg->getValue(), SymbolMatchStyle, ErrorCallback)))
      return std::move(E);

  if (!InputArgs.hasArg(STRIP_no_strip_all) && !Config.StripDebug &&
      !Config.OnlyKeepDebug && !Config.StripUnneeded &&
      Config.DiscardMode == DiscardType::None && !Config.StripAllGNU &&
      Config.SymbolsToRemove.empty())
    Config.StripAll = true;

  if (Config.DiscardMode == DiscardType::All) {
    Config.StripDebug = true;
    ELFConfig.KeepFileSymbols = true;
  }

  Config.DeterministicArchives =
      InputArgs.hasFlag(STRIP_enable_deterministic_archives,
                        STRIP_disable_deterministic_archives, /*default=*/true);

  Config.PreserveDates = InputArgs.hasArg(STRIP_preserve_dates);
  Config.InputFormat = FileFormat::Unspecified;
  Config.OutputFormat = FileFormat::Unspecified;

  DriverConfig DC;
  if (Positional.size() == 1) {
    Config.InputFilename = Positional[0];
    Config.OutputFilename =
        InputArgs.getLastArgValue(STRIP_output, Positional[0]);
    DC.CopyConfigs.push_back(std::move(ConfigMgr));
  } else {
    StringMap<unsigned> InputFiles;
    for (StringRef Filename : Positional) {
      if (InputFiles[Filename]++ == 1) {
        if (Filename == "-")
          return createStringError(
              errc::invalid_argument,
              "cannot specify '-' as an input file more than once");
        if (Error E = ErrorCallback(createStringError(
                errc::invalid_argument, "'%s' was already specified",
                Filename.str().c_str())))
          return std::move(E);
      }
      Config.InputFilename = Filename;
      Config.OutputFilename = Filename;
      DC.CopyConfigs.push_back(ConfigMgr);
    }
  }

  if (Config.PreserveDates && (is_contained(Positional, "-") ||
                               InputArgs.getLastArgValue(STRIP_output) == "-"))
    return createStringError(errc::invalid_argument,
                             "--preserve-dates requires a file");

  return std::move(DC);
}
