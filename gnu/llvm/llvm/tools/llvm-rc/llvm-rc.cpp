//===-- llvm-rc.cpp - Compile .rc scripts into .res -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Compile .rc scripts into .res files. This is intended to be a
// platform-independent port of Microsoft's rc.exe tool.
//
//===----------------------------------------------------------------------===//

#include "ResourceFileWriter.h"
#include "ResourceScriptCppFilter.h"
#include "ResourceScriptParser.h"
#include "ResourceScriptStmt.h"
#include "ResourceScriptToken.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Object/WindowsResource.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <system_error>

using namespace llvm;
using namespace llvm::rc;
using namespace llvm::opt;

namespace {

// Input options tables.

enum ID {
  OPT_INVALID = 0, // This is not a correct option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

namespace rc_opt {
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "Opts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};
} // namespace rc_opt

class RcOptTable : public opt::GenericOptTable {
public:
  RcOptTable() : GenericOptTable(rc_opt::InfoTable, /* IgnoreCase = */ true) {}
};

enum Windres_ID {
  WINDRES_INVALID = 0, // This is not a correct option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(WINDRES_, __VA_ARGS__),
#include "WindresOpts.inc"
#undef OPTION
};

namespace windres_opt {
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr StringLiteral NAME##_init[] = VALUE;                        \
  static constexpr ArrayRef<StringLiteral> NAME(NAME##_init,                   \
                                                std::size(NAME##_init) - 1);
#include "WindresOpts.inc"
#undef PREFIX

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...)                                                            \
  LLVM_CONSTRUCT_OPT_INFO_WITH_ID_PREFIX(WINDRES_, __VA_ARGS__),
#include "WindresOpts.inc"
#undef OPTION
};
} // namespace windres_opt

class WindresOptTable : public opt::GenericOptTable {
public:
  WindresOptTable()
      : GenericOptTable(windres_opt::InfoTable, /* IgnoreCase = */ false) {}
};

static ExitOnError ExitOnErr;
static FileRemover TempPreprocFile;
static FileRemover TempResFile;

[[noreturn]] static void fatalError(const Twine &Message) {
  errs() << Message << "\n";
  exit(1);
}

std::string createTempFile(const Twine &Prefix, StringRef Suffix) {
  std::error_code EC;
  SmallString<128> FileName;
  if ((EC = sys::fs::createTemporaryFile(Prefix, Suffix, FileName)))
    fatalError("Unable to create temp file: " + EC.message());
  return static_cast<std::string>(FileName);
}

ErrorOr<std::string> findClang(const char *Argv0, StringRef Triple) {
  // This just needs to be some symbol in the binary.
  void *P = (void*) (intptr_t) findClang;
  std::string MainExecPath = llvm::sys::fs::getMainExecutable(Argv0, P);
  if (MainExecPath.empty())
    MainExecPath = Argv0;

  ErrorOr<std::string> Path = std::error_code();
  std::string TargetClang = (Triple + "-clang").str();
  std::string VersionedClang = ("clang-" + Twine(LLVM_VERSION_MAJOR)).str();
  for (const auto *Name :
       {TargetClang.c_str(), VersionedClang.c_str(), "clang", "clang-cl"}) {
    for (const StringRef Parent :
         {llvm::sys::path::parent_path(MainExecPath),
          llvm::sys::path::parent_path(Argv0)}) {
      // Look for various versions of "clang" first in the MainExecPath parent
      // directory and then in the argv[0] parent directory.
      // On Windows (but not Unix) argv[0] is overwritten with the eqiuvalent
      // of MainExecPath by InitLLVM.
      Path = sys::findProgramByName(Name, Parent);
      if (Path)
        return Path;
    }
  }

  // If no parent directory known, or not found there, look everywhere in PATH
  for (const auto *Name : {"clang", "clang-cl"}) {
    Path = sys::findProgramByName(Name);
    if (Path)
      return Path;
  }
  return Path;
}

bool isUsableArch(Triple::ArchType Arch) {
  switch (Arch) {
  case Triple::x86:
  case Triple::x86_64:
  case Triple::arm:
  case Triple::thumb:
  case Triple::aarch64:
    // These work properly with the clang driver, setting the expected
    // defines such as _WIN32 etc.
    return true;
  default:
    // Other archs aren't set up for use with windows as target OS, (clang
    // doesn't define e.g. _WIN32 etc), so with them we need to set a
    // different default arch.
    return false;
  }
}

Triple::ArchType getDefaultFallbackArch() {
  return Triple::x86_64;
}

std::string getClangClTriple() {
  Triple T(sys::getDefaultTargetTriple());
  if (!isUsableArch(T.getArch()))
    T.setArch(getDefaultFallbackArch());
  T.setOS(Triple::Win32);
  T.setVendor(Triple::PC);
  T.setEnvironment(Triple::MSVC);
  T.setObjectFormat(Triple::COFF);
  return T.str();
}

std::string getMingwTriple() {
  Triple T(sys::getDefaultTargetTriple());
  if (!isUsableArch(T.getArch()))
    T.setArch(getDefaultFallbackArch());
  if (T.isWindowsGNUEnvironment())
    return T.str();
  // Write out the literal form of the vendor/env here, instead of
  // constructing them with enum values (which end up with them in
  // normalized form). The literal form of the triple can matter for
  // finding include files.
  return (Twine(T.getArchName()) + "-w64-mingw32").str();
}

enum Format { Rc, Res, Coff, Unknown };

struct RcOptions {
  bool Preprocess = true;
  bool PrintCmdAndExit = false;
  std::string Triple;
  std::optional<std::string> Preprocessor;
  std::vector<std::string> PreprocessArgs;

  std::string InputFile;
  Format InputFormat = Rc;
  std::string OutputFile;
  Format OutputFormat = Res;

  bool IsWindres = false;
  bool BeVerbose = false;
  WriterParams Params;
  bool AppendNull = false;
  bool IsDryRun = false;
  // Set the default language; choose en-US arbitrarily.
  unsigned LangId = (/*PrimaryLangId*/ 0x09) | (/*SubLangId*/ 0x01 << 10);
};

void preprocess(StringRef Src, StringRef Dst, const RcOptions &Opts,
                const char *Argv0) {
  std::string Clang;
  if (Opts.PrintCmdAndExit || Opts.Preprocessor) {
    Clang = "clang";
  } else {
    ErrorOr<std::string> ClangOrErr = findClang(Argv0, Opts.Triple);
    if (ClangOrErr) {
      Clang = *ClangOrErr;
    } else {
      errs() << "llvm-rc: Unable to find clang for preprocessing."
             << "\n";
      StringRef OptionName =
          Opts.IsWindres ? "--no-preprocess" : "-no-preprocess";
      errs() << "Pass " << OptionName << " to disable preprocessing.\n";
      fatalError("llvm-rc: Unable to preprocess.");
    }
  }

  SmallVector<StringRef, 8> Args = {
      Clang, "--driver-mode=gcc", "-target", Opts.Triple, "-E",
      "-xc", "-DRC_INVOKED"};
  std::string PreprocessorExecutable;
  if (Opts.Preprocessor) {
    Args.clear();
    Args.push_back(*Opts.Preprocessor);
    if (!sys::fs::can_execute(Args[0])) {
      if (auto P = sys::findProgramByName(Args[0])) {
        PreprocessorExecutable = *P;
        Args[0] = PreprocessorExecutable;
      }
    }
  }
  for (const auto &S : Opts.PreprocessArgs)
    Args.push_back(S);
  Args.push_back(Src);
  Args.push_back("-o");
  Args.push_back(Dst);
  if (Opts.PrintCmdAndExit || Opts.BeVerbose) {
    for (const auto &A : Args) {
      outs() << " ";
      sys::printArg(outs(), A, Opts.PrintCmdAndExit);
    }
    outs() << "\n";
    if (Opts.PrintCmdAndExit)
      exit(0);
  }
  // The llvm Support classes don't handle reading from stdout of a child
  // process; otherwise we could avoid using a temp file.
  std::string ErrMsg;
  int Res =
      sys::ExecuteAndWait(Args[0], Args, /*Env=*/std::nullopt, /*Redirects=*/{},
                          /*SecondsToWait=*/0, /*MemoryLimit=*/0, &ErrMsg);
  if (Res) {
    if (!ErrMsg.empty())
      fatalError("llvm-rc: Preprocessing failed: " + ErrMsg);
    else
      fatalError("llvm-rc: Preprocessing failed.");
  }
}

static std::pair<bool, std::string> isWindres(llvm::StringRef Argv0) {
  StringRef ProgName = llvm::sys::path::stem(Argv0);
  // x86_64-w64-mingw32-windres -> x86_64-w64-mingw32, windres
  // llvm-rc -> "", llvm-rc
  // aarch64-w64-mingw32-llvm-windres-10.exe -> aarch64-w64-mingw32, llvm-windres
  ProgName = ProgName.rtrim("0123456789.-");
  if (!ProgName.consume_back_insensitive("windres"))
    return std::make_pair<bool, std::string>(false, "");
  ProgName.consume_back_insensitive("llvm-");
  ProgName.consume_back_insensitive("-");
  return std::make_pair<bool, std::string>(true, ProgName.str());
}

Format parseFormat(StringRef S) {
  Format F = StringSwitch<Format>(S.lower())
                 .Case("rc", Rc)
                 .Case("res", Res)
                 .Case("coff", Coff)
                 .Default(Unknown);
  if (F == Unknown)
    fatalError("Unable to parse '" + Twine(S) + "' as a format");
  return F;
}

void deduceFormat(Format &Dest, StringRef File) {
  Format F = StringSwitch<Format>(sys::path::extension(File.lower()))
                 .Case(".rc", Rc)
                 .Case(".res", Res)
                 .Case(".o", Coff)
                 .Case(".obj", Coff)
                 .Default(Unknown);
  if (F != Unknown)
    Dest = F;
}

std::string unescape(StringRef S) {
  std::string Out;
  Out.reserve(S.size());
  for (int I = 0, E = S.size(); I < E; I++) {
    if (S[I] == '\\') {
      if (I + 1 < E)
        Out.push_back(S[++I]);
      else
        fatalError("Unterminated escape");
      continue;
    } else if (S[I] == '"') {
      // This eats an individual unescaped quote, like a shell would do.
      continue;
    }
    Out.push_back(S[I]);
  }
  return Out;
}

RcOptions parseWindresOptions(ArrayRef<const char *> ArgsArr,
                              ArrayRef<const char *> InputArgsArray,
                              std::string Prefix) {
  WindresOptTable T;
  RcOptions Opts;
  unsigned MAI, MAC;
  opt::InputArgList InputArgs = T.ParseArgs(ArgsArr, MAI, MAC);

  Opts.IsWindres = true;

  // The tool prints nothing when invoked with no command-line arguments.
  if (InputArgs.hasArg(WINDRES_help)) {
    T.printHelp(outs(), "windres [options] file...",
                "LLVM windres (GNU windres compatible)", false, true);
    exit(0);
  }

  if (InputArgs.hasArg(WINDRES_version)) {
    outs() << "llvm-windres, compatible with GNU windres\n";
    cl::PrintVersionMessage();
    exit(0);
  }

  std::vector<std::string> FileArgs = InputArgs.getAllArgValues(WINDRES_INPUT);
  FileArgs.insert(FileArgs.end(), InputArgsArray.begin(), InputArgsArray.end());

  if (InputArgs.hasArg(WINDRES_input)) {
    Opts.InputFile = InputArgs.getLastArgValue(WINDRES_input).str();
  } else if (!FileArgs.empty()) {
    Opts.InputFile = FileArgs.front();
    FileArgs.erase(FileArgs.begin());
  } else {
    // TODO: GNU windres takes input on stdin in this case.
    fatalError("Missing input file");
  }

  if (InputArgs.hasArg(WINDRES_output)) {
    Opts.OutputFile = InputArgs.getLastArgValue(WINDRES_output).str();
  } else if (!FileArgs.empty()) {
    Opts.OutputFile = FileArgs.front();
    FileArgs.erase(FileArgs.begin());
  } else {
    // TODO: GNU windres writes output in rc form to stdout in this case.
    fatalError("Missing output file");
  }

  if (InputArgs.hasArg(WINDRES_input_format)) {
    Opts.InputFormat =
        parseFormat(InputArgs.getLastArgValue(WINDRES_input_format));
  } else {
    deduceFormat(Opts.InputFormat, Opts.InputFile);
  }
  if (Opts.InputFormat == Coff)
    fatalError("Unsupported input format");

  if (InputArgs.hasArg(WINDRES_output_format)) {
    Opts.OutputFormat =
        parseFormat(InputArgs.getLastArgValue(WINDRES_output_format));
  } else {
    // The default in windres differs from the default in RcOptions
    Opts.OutputFormat = Coff;
    deduceFormat(Opts.OutputFormat, Opts.OutputFile);
  }
  if (Opts.OutputFormat == Rc)
    fatalError("Unsupported output format");
  if (Opts.InputFormat == Opts.OutputFormat) {
    outs() << "Nothing to do.\n";
    exit(0);
  }

  Opts.PrintCmdAndExit = InputArgs.hasArg(WINDRES__HASH_HASH_HASH);
  Opts.Preprocess = !InputArgs.hasArg(WINDRES_no_preprocess);
  Triple TT(Prefix);
  if (InputArgs.hasArg(WINDRES_target)) {
    StringRef Value = InputArgs.getLastArgValue(WINDRES_target);
    if (Value == "pe-i386")
      Opts.Triple = "i686-w64-mingw32";
    else if (Value == "pe-x86-64")
      Opts.Triple = "x86_64-w64-mingw32";
    else
      // Implicit extension; if the --target value isn't one of the known
      // BFD targets, allow setting the full triple string via this instead.
      Opts.Triple = Value.str();
  } else if (TT.getArch() != Triple::UnknownArch)
    Opts.Triple = Prefix;
  else
    Opts.Triple = getMingwTriple();

  for (const auto *Arg :
       InputArgs.filtered(WINDRES_include_dir, WINDRES_define, WINDRES_undef,
                          WINDRES_preprocessor_arg)) {
    // GNU windres passes the arguments almost as-is on to popen() (it only
    // backslash escapes spaces in the arguments), where a shell would
    // unescape backslash escapes for quotes and similar. This means that
    // when calling GNU windres, callers need to double escape chars like
    // quotes, e.g. as -DSTRING=\\\"1.2.3\\\".
    //
    // Exactly how the arguments are interpreted depends on the platform
    // though - but the cases where this matters (where callers would have
    // done this double escaping) probably is confined to cases like these
    // quoted string defines, and those happen to work the same across unix
    // and windows.
    //
    // If GNU windres is executed with --use-temp-file, it doesn't use
    // popen() to invoke the preprocessor, but uses another function which
    // actually preserves tricky characters better. To mimic this behaviour,
    // don't unescape arguments here.
    std::string Value = Arg->getValue();
    if (!InputArgs.hasArg(WINDRES_use_temp_file))
      Value = unescape(Value);
    switch (Arg->getOption().getID()) {
    case WINDRES_include_dir:
      // Technically, these are handled the same way as e.g. defines, but
      // the way we consistently unescape the unix way breaks windows paths
      // with single backslashes. Alternatively, our unescape function would
      // need to mimic the platform specific command line parsing/unescaping
      // logic.
      Opts.Params.Include.push_back(Arg->getValue());
      Opts.PreprocessArgs.push_back("-I");
      Opts.PreprocessArgs.push_back(Arg->getValue());
      break;
    case WINDRES_define:
      Opts.PreprocessArgs.push_back("-D");
      Opts.PreprocessArgs.push_back(Value);
      break;
    case WINDRES_undef:
      Opts.PreprocessArgs.push_back("-U");
      Opts.PreprocessArgs.push_back(Value);
      break;
    case WINDRES_preprocessor_arg:
      Opts.PreprocessArgs.push_back(Value);
      break;
    }
  }
  if (InputArgs.hasArg(WINDRES_preprocessor))
    Opts.Preprocessor = InputArgs.getLastArgValue(WINDRES_preprocessor);

  Opts.Params.CodePage = CpWin1252; // Different default
  if (InputArgs.hasArg(WINDRES_codepage)) {
    if (InputArgs.getLastArgValue(WINDRES_codepage)
            .getAsInteger(0, Opts.Params.CodePage))
      fatalError("Invalid code page: " +
                 InputArgs.getLastArgValue(WINDRES_codepage));
  }
  if (InputArgs.hasArg(WINDRES_language)) {
    StringRef Val = InputArgs.getLastArgValue(WINDRES_language);
    Val.consume_front_insensitive("0x");
    if (Val.getAsInteger(16, Opts.LangId))
      fatalError("Invalid language id: " +
                 InputArgs.getLastArgValue(WINDRES_language));
  }

  Opts.BeVerbose = InputArgs.hasArg(WINDRES_verbose);

  return Opts;
}

RcOptions parseRcOptions(ArrayRef<const char *> ArgsArr,
                         ArrayRef<const char *> InputArgsArray) {
  RcOptTable T;
  RcOptions Opts;
  unsigned MAI, MAC;
  opt::InputArgList InputArgs = T.ParseArgs(ArgsArr, MAI, MAC);

  // The tool prints nothing when invoked with no command-line arguments.
  if (InputArgs.hasArg(OPT_help)) {
    T.printHelp(outs(), "llvm-rc [options] file...", "LLVM Resource Converter",
                false);
    exit(0);
  }

  std::vector<std::string> InArgsInfo = InputArgs.getAllArgValues(OPT_INPUT);
  InArgsInfo.insert(InArgsInfo.end(), InputArgsArray.begin(),
                    InputArgsArray.end());
  if (InArgsInfo.size() != 1) {
    fatalError("Exactly one input file should be provided.");
  }

  Opts.PrintCmdAndExit = InputArgs.hasArg(OPT__HASH_HASH_HASH);
  Opts.Triple = getClangClTriple();
  for (const auto *Arg :
       InputArgs.filtered(OPT_includepath, OPT_define, OPT_undef)) {
    switch (Arg->getOption().getID()) {
    case OPT_includepath:
      Opts.PreprocessArgs.push_back("-I");
      break;
    case OPT_define:
      Opts.PreprocessArgs.push_back("-D");
      break;
    case OPT_undef:
      Opts.PreprocessArgs.push_back("-U");
      break;
    }
    Opts.PreprocessArgs.push_back(Arg->getValue());
  }

  Opts.InputFile = InArgsInfo[0];
  Opts.BeVerbose = InputArgs.hasArg(OPT_verbose);
  Opts.Preprocess = !InputArgs.hasArg(OPT_no_preprocess);
  Opts.Params.Include = InputArgs.getAllArgValues(OPT_includepath);
  Opts.Params.NoInclude = InputArgs.hasArg(OPT_noinclude);
  if (Opts.Params.NoInclude) {
    // Clear the INLCUDE variable for the external preprocessor
#ifdef _WIN32
    ::_putenv("INCLUDE=");
#else
    ::unsetenv("INCLUDE");
#endif
  }
  if (InputArgs.hasArg(OPT_codepage)) {
    if (InputArgs.getLastArgValue(OPT_codepage)
            .getAsInteger(10, Opts.Params.CodePage))
      fatalError("Invalid code page: " +
                 InputArgs.getLastArgValue(OPT_codepage));
  }
  Opts.IsDryRun = InputArgs.hasArg(OPT_dry_run);
  auto OutArgsInfo = InputArgs.getAllArgValues(OPT_fileout);
  if (OutArgsInfo.empty()) {
    SmallString<128> OutputFile(Opts.InputFile);
    llvm::sys::fs::make_absolute(OutputFile);
    llvm::sys::path::replace_extension(OutputFile, "res");
    OutArgsInfo.push_back(std::string(OutputFile));
  }
  if (!Opts.IsDryRun) {
    if (OutArgsInfo.size() != 1)
      fatalError(
          "No more than one output file should be provided (using /FO flag).");
    Opts.OutputFile = OutArgsInfo[0];
  }
  Opts.AppendNull = InputArgs.hasArg(OPT_add_null);
  if (InputArgs.hasArg(OPT_lang_id)) {
    StringRef Val = InputArgs.getLastArgValue(OPT_lang_id);
    Val.consume_front_insensitive("0x");
    if (Val.getAsInteger(16, Opts.LangId))
      fatalError("Invalid language id: " +
                 InputArgs.getLastArgValue(OPT_lang_id));
  }
  return Opts;
}

RcOptions getOptions(const char *Argv0, ArrayRef<const char *> ArgsArr,
                     ArrayRef<const char *> InputArgs) {
  std::string Prefix;
  bool IsWindres;
  std::tie(IsWindres, Prefix) = isWindres(Argv0);
  if (IsWindres)
    return parseWindresOptions(ArgsArr, InputArgs, Prefix);
  else
    return parseRcOptions(ArgsArr, InputArgs);
}

void doRc(std::string Src, std::string Dest, RcOptions &Opts,
          const char *Argv0) {
  std::string PreprocessedFile = Src;
  if (Opts.Preprocess) {
    std::string OutFile = createTempFile("preproc", "rc");
    TempPreprocFile.setFile(OutFile);
    preprocess(Src, OutFile, Opts, Argv0);
    PreprocessedFile = OutFile;
  }

  // Read and tokenize the input file.
  ErrorOr<std::unique_ptr<MemoryBuffer>> File =
      MemoryBuffer::getFile(PreprocessedFile);
  if (!File) {
    fatalError("Error opening file '" + Twine(PreprocessedFile) +
               "': " + File.getError().message());
  }

  std::unique_ptr<MemoryBuffer> FileContents = std::move(*File);
  StringRef Contents = FileContents->getBuffer();

  std::string FilteredContents = filterCppOutput(Contents);
  std::vector<RCToken> Tokens = ExitOnErr(tokenizeRC(FilteredContents));

  if (Opts.BeVerbose) {
    const Twine TokenNames[] = {
#define TOKEN(Name) #Name,
#define SHORT_TOKEN(Name, Ch) #Name,
#include "ResourceScriptTokenList.def"
    };

    for (const RCToken &Token : Tokens) {
      outs() << TokenNames[static_cast<int>(Token.kind())] << ": "
             << Token.value();
      if (Token.kind() == RCToken::Kind::Int)
        outs() << "; int value = " << Token.intValue();

      outs() << "\n";
    }
  }

  WriterParams &Params = Opts.Params;
  SmallString<128> InputFile(Src);
  llvm::sys::fs::make_absolute(InputFile);
  Params.InputFilePath = InputFile;

  switch (Params.CodePage) {
  case CpAcp:
  case CpWin1252:
  case CpUtf8:
    break;
  default:
    fatalError("Unsupported code page, only 0, 1252 and 65001 are supported!");
  }

  std::unique_ptr<ResourceFileWriter> Visitor;

  if (!Opts.IsDryRun) {
    std::error_code EC;
    auto FOut = std::make_unique<raw_fd_ostream>(
        Dest, EC, sys::fs::FA_Read | sys::fs::FA_Write);
    if (EC)
      fatalError("Error opening output file '" + Dest + "': " + EC.message());
    Visitor = std::make_unique<ResourceFileWriter>(Params, std::move(FOut));
    Visitor->AppendNull = Opts.AppendNull;

    ExitOnErr(NullResource().visit(Visitor.get()));

    unsigned PrimaryLangId = Opts.LangId & 0x3ff;
    unsigned SubLangId = Opts.LangId >> 10;
    ExitOnErr(LanguageResource(PrimaryLangId, SubLangId).visit(Visitor.get()));
  }

  rc::RCParser Parser{std::move(Tokens)};
  while (!Parser.isEof()) {
    auto Resource = ExitOnErr(Parser.parseSingleResource());
    if (Opts.BeVerbose)
      Resource->log(outs());
    if (!Opts.IsDryRun)
      ExitOnErr(Resource->visit(Visitor.get()));
  }

  // STRINGTABLE resources come at the very end.
  if (!Opts.IsDryRun)
    ExitOnErr(Visitor->dumpAllStringTables());
}

void doCvtres(std::string Src, std::string Dest, std::string TargetTriple) {
  object::WindowsResourceParser Parser;

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(Src);
  if (!BufferOrErr)
    fatalError("Error opening file '" + Twine(Src) +
               "': " + BufferOrErr.getError().message());
  std::unique_ptr<MemoryBuffer> &Buffer = BufferOrErr.get();
  std::unique_ptr<object::WindowsResource> Binary =
      ExitOnErr(object::WindowsResource::createWindowsResource(
          Buffer->getMemBufferRef()));

  std::vector<std::string> Duplicates;
  ExitOnErr(Parser.parse(Binary.get(), Duplicates));
  for (const auto &DupeDiag : Duplicates)
    fatalError("Duplicate resources: " + DupeDiag);

  Triple T(TargetTriple);
  COFF::MachineTypes MachineType;
  switch (T.getArch()) {
  case Triple::x86:
    MachineType = COFF::IMAGE_FILE_MACHINE_I386;
    break;
  case Triple::x86_64:
    MachineType = COFF::IMAGE_FILE_MACHINE_AMD64;
    break;
  case Triple::arm:
  case Triple::thumb:
    MachineType = COFF::IMAGE_FILE_MACHINE_ARMNT;
    break;
  case Triple::aarch64:
    if (T.isWindowsArm64EC())
      MachineType = COFF::IMAGE_FILE_MACHINE_ARM64EC;
    else
      MachineType = COFF::IMAGE_FILE_MACHINE_ARM64;
    break;
  default:
    fatalError("Unsupported architecture in target '" + Twine(TargetTriple) +
               "'");
  }

  std::unique_ptr<MemoryBuffer> OutputBuffer =
      ExitOnErr(object::writeWindowsResourceCOFF(MachineType, Parser,
                                                 /*DateTimeStamp*/ 0));
  std::unique_ptr<FileOutputBuffer> FileBuffer =
      ExitOnErr(FileOutputBuffer::create(Dest, OutputBuffer->getBufferSize()));
  std::copy(OutputBuffer->getBufferStart(), OutputBuffer->getBufferEnd(),
            FileBuffer->getBufferStart());
  ExitOnErr(FileBuffer->commit());
}

} // anonymous namespace

int llvm_rc_main(int Argc, char **Argv, const llvm::ToolContext &) {
  ExitOnErr.setBanner("llvm-rc: ");

  char **DashDash = std::find_if(Argv + 1, Argv + Argc,
                                 [](StringRef Str) { return Str == "--"; });
  ArrayRef<const char *> ArgsArr = ArrayRef(Argv + 1, DashDash);
  ArrayRef<const char *> FileArgsArr;
  if (DashDash != Argv + Argc)
    FileArgsArr = ArrayRef(DashDash + 1, Argv + Argc);

  RcOptions Opts = getOptions(Argv[0], ArgsArr, FileArgsArr);

  std::string ResFile = Opts.OutputFile;
  if (Opts.InputFormat == Rc) {
    if (Opts.OutputFormat == Coff) {
      ResFile = createTempFile("rc", "res");
      TempResFile.setFile(ResFile);
    }
    doRc(Opts.InputFile, ResFile, Opts, Argv[0]);
  } else {
    ResFile = Opts.InputFile;
  }
  if (Opts.OutputFormat == Coff) {
    doCvtres(ResFile, Opts.OutputFile, Opts.Triple);
  }

  return 0;
}
