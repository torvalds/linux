//===-- Options.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Options.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Driver/Driver.h"
#include "clang/InstallAPI/DirectoryScanner.h"
#include "clang/InstallAPI/FileList.h"
#include "clang/InstallAPI/HeaderFile.h"
#include "clang/InstallAPI/InstallAPIDiagnostic.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Program.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TextAPI/DylibReader.h"
#include "llvm/TextAPI/TextAPIError.h"
#include "llvm/TextAPI/TextAPIReader.h"
#include "llvm/TextAPI/TextAPIWriter.h"

using namespace llvm;
using namespace llvm::opt;
using namespace llvm::MachO;

namespace drv = clang::driver::options;

namespace clang {
namespace installapi {

/// Create prefix string literals used in InstallAPIOpts.td.
#define PREFIX(NAME, VALUE)                                                    \
  static constexpr llvm::StringLiteral NAME##_init[] = VALUE;                  \
  static constexpr llvm::ArrayRef<llvm::StringLiteral> NAME(                   \
      NAME##_init, std::size(NAME##_init) - 1);
#include "InstallAPIOpts.inc"
#undef PREFIX

static constexpr const llvm::StringLiteral PrefixTable_init[] =
#define PREFIX_UNION(VALUES) VALUES
#include "InstallAPIOpts.inc"
#undef PREFIX_UNION
    ;
static constexpr const ArrayRef<StringLiteral>
    PrefixTable(PrefixTable_init, std::size(PrefixTable_init) - 1);

/// Create table mapping all options defined in InstallAPIOpts.td.
static constexpr OptTable::Info InfoTable[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS,         \
               VISIBILITY, PARAM, HELPTEXT, HELPTEXTSFORVARIANTS, METAVAR,     \
               VALUES)                                                         \
  {PREFIX,                                                                     \
   NAME,                                                                       \
   HELPTEXT,                                                                   \
   HELPTEXTSFORVARIANTS,                                                       \
   METAVAR,                                                                    \
   OPT_##ID,                                                                   \
   Option::KIND##Class,                                                        \
   PARAM,                                                                      \
   FLAGS,                                                                      \
   VISIBILITY,                                                                 \
   OPT_##GROUP,                                                                \
   OPT_##ALIAS,                                                                \
   ALIASARGS,                                                                  \
   VALUES},
#include "InstallAPIOpts.inc"
#undef OPTION
};

namespace {

/// \brief Create OptTable class for parsing actual command line arguments.
class DriverOptTable : public opt::PrecomputedOptTable {
public:
  DriverOptTable() : PrecomputedOptTable(InfoTable, PrefixTable) {}
};

} // end anonymous namespace.

static llvm::opt::OptTable *createDriverOptTable() {
  return new DriverOptTable();
}

/// Parse JSON input into argument list.
///
/* Expected input format.
 *  { "label" : ["-ClangArg1", "-ClangArg2"] }
 */
///
/// Input is interpreted as "-Xlabel ClangArg1 -XLabel ClangArg2".
static Expected<llvm::opt::InputArgList>
getArgListFromJSON(const StringRef Input, llvm::opt::OptTable *Table,
                   std::vector<std::string> &Storage) {
  using namespace json;
  Expected<Value> ValOrErr = json::parse(Input);
  if (!ValOrErr)
    return ValOrErr.takeError();

  const Object *Root = ValOrErr->getAsObject();
  if (!Root)
    return llvm::opt::InputArgList();

  for (const auto &KV : *Root) {
    const Array *ArgList = KV.getSecond().getAsArray();
    std::string Label = "-X" + KV.getFirst().str();
    if (!ArgList)
      return make_error<TextAPIError>(TextAPIErrorCode::InvalidInputFormat);
    for (auto Arg : *ArgList) {
      std::optional<StringRef> ArgStr = Arg.getAsString();
      if (!ArgStr)
        return make_error<TextAPIError>(TextAPIErrorCode::InvalidInputFormat);
      Storage.emplace_back(Label);
      Storage.emplace_back(*ArgStr);
    }
  }

  std::vector<const char *> CArgs(Storage.size());
  llvm::for_each(Storage,
                 [&CArgs](StringRef Str) { CArgs.emplace_back(Str.data()); });

  unsigned MissingArgIndex, MissingArgCount;
  return Table->ParseArgs(CArgs, MissingArgIndex, MissingArgCount);
}

bool Options::processDriverOptions(InputArgList &Args) {
  // Handle inputs.
  for (const StringRef Path : Args.getAllArgValues(drv::OPT_INPUT)) {
    // Assume any input that is not a directory is a filelist.
    // InstallAPI does not accept multiple directories, so retain the last one.
    if (FM->getOptionalDirectoryRef(Path))
      DriverOpts.InputDirectory = Path.str();
    else
      DriverOpts.FileLists.emplace_back(Path.str());
  }

  // Handle output.
  SmallString<PATH_MAX> OutputPath;
  if (auto *Arg = Args.getLastArg(drv::OPT_o)) {
    OutputPath = Arg->getValue();
    if (OutputPath != "-")
      FM->makeAbsolutePath(OutputPath);
    DriverOpts.OutputPath = std::string(OutputPath);
  }
  if (DriverOpts.OutputPath.empty()) {
    Diags->Report(diag::err_no_output_file);
    return false;
  }

  // Do basic error checking first for mixing -target and -arch options.
  auto *ArgArch = Args.getLastArgNoClaim(drv::OPT_arch);
  auto *ArgTarget = Args.getLastArgNoClaim(drv::OPT_target);
  auto *ArgTargetVariant =
      Args.getLastArgNoClaim(drv::OPT_darwin_target_variant);
  if (ArgArch && (ArgTarget || ArgTargetVariant)) {
    Diags->Report(clang::diag::err_drv_argument_not_allowed_with)
        << ArgArch->getAsString(Args)
        << (ArgTarget ? ArgTarget : ArgTargetVariant)->getAsString(Args);
    return false;
  }

  auto *ArgMinTargetOS = Args.getLastArgNoClaim(drv::OPT_mtargetos_EQ);
  if ((ArgTarget || ArgTargetVariant) && ArgMinTargetOS) {
    Diags->Report(clang::diag::err_drv_cannot_mix_options)
        << ArgTarget->getAsString(Args) << ArgMinTargetOS->getAsString(Args);
    return false;
  }

  // Capture target triples first.
  if (ArgTarget) {
    for (const Arg *A : Args.filtered(drv::OPT_target)) {
      A->claim();
      llvm::Triple TargetTriple(A->getValue());
      Target TAPITarget = Target(TargetTriple);
      if ((TAPITarget.Arch == AK_unknown) ||
          (TAPITarget.Platform == PLATFORM_UNKNOWN)) {
        Diags->Report(clang::diag::err_drv_unsupported_opt_for_target)
            << "installapi" << TargetTriple.str();
        return false;
      }
      DriverOpts.Targets[TAPITarget] = TargetTriple;
    }
  }

  // Capture target variants.
  DriverOpts.Zippered = ArgTargetVariant != nullptr;
  for (Arg *A : Args.filtered(drv::OPT_darwin_target_variant)) {
    A->claim();
    Triple Variant(A->getValue());
    if (Variant.getVendor() != Triple::Apple) {
      Diags->Report(diag::err_unsupported_vendor)
          << Variant.getVendorName() << A->getAsString(Args);
      return false;
    }

    switch (Variant.getOS()) {
    default:
      Diags->Report(diag::err_unsupported_os)
          << Variant.getOSName() << A->getAsString(Args);
      return false;
    case Triple::MacOSX:
    case Triple::IOS:
      break;
    }

    switch (Variant.getEnvironment()) {
    default:
      Diags->Report(diag::err_unsupported_environment)
          << Variant.getEnvironmentName() << A->getAsString(Args);
      return false;
    case Triple::UnknownEnvironment:
    case Triple::MacABI:
      break;
    }

    Target TAPIVariant(Variant);
    // See if there is a matching --target option for this --target-variant
    // option.
    auto It = find_if(DriverOpts.Targets, [&](const auto &T) {
      return (T.first.Arch == TAPIVariant.Arch) &&
             (T.first.Platform != PlatformType::PLATFORM_UNKNOWN);
    });

    if (It == DriverOpts.Targets.end()) {
      Diags->Report(diag::err_no_matching_target) << Variant.str();
      return false;
    }

    DriverOpts.Targets[TAPIVariant] = Variant;
  }

  DriverOpts.Verbose = Args.hasArgNoClaim(drv::OPT_v);

  return true;
}

bool Options::processInstallAPIXOptions(InputArgList &Args) {
  for (arg_iterator It = Args.begin(), End = Args.end(); It != End; ++It) {
    Arg *A = *It;
    if (A->getOption().matches(OPT_Xarch__)) {
      if (!processXarchOption(Args, It))
        return false;
      continue;
    } else if (A->getOption().matches(OPT_Xplatform__)) {
      if (!processXplatformOption(Args, It))
        return false;
      continue;
    } else if (A->getOption().matches(OPT_Xproject)) {
      if (!processXprojectOption(Args, It))
        return false;
      continue;
    } else if (!A->getOption().matches(OPT_X__))
      continue;

    // Handle any user defined labels.
    const StringRef Label = A->getValue(0);

    // Ban "public" and "private" labels.
    if ((Label.lower() == "public") || (Label.lower() == "private")) {
      Diags->Report(diag::err_invalid_label) << Label;
      return false;
    }

    auto NextIt = std::next(It);
    if (NextIt == End) {
      Diags->Report(clang::diag::err_drv_missing_argument)
          << A->getAsString(Args) << 1;
      return false;
    }
    Arg *NextA = *NextIt;
    switch ((ID)NextA->getOption().getID()) {
    case OPT_D:
    case OPT_U:
      break;
    default:
      Diags->Report(clang::diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args) << NextA->getAsString(Args);
      return false;
    }
    const StringRef ASpelling = NextA->getSpelling();
    const auto &AValues = NextA->getValues();
    if (AValues.empty())
      FEOpts.UniqueArgs[Label].emplace_back(ASpelling.str());
    else
      for (const StringRef Val : AValues)
        FEOpts.UniqueArgs[Label].emplace_back((ASpelling + Val).str());

    A->claim();
    NextA->claim();
  }

  return true;
}

bool Options::processXplatformOption(InputArgList &Args, arg_iterator Curr) {
  Arg *A = *Curr;

  PlatformType Platform = getPlatformFromName(A->getValue(0));
  if (Platform == PLATFORM_UNKNOWN) {
    Diags->Report(diag::err_unsupported_os)
        << getPlatformName(Platform) << A->getAsString(Args);
    return false;
  }
  auto NextIt = std::next(Curr);
  if (NextIt == Args.end()) {
    Diags->Report(diag::err_drv_missing_argument) << A->getAsString(Args) << 1;
    return false;
  }

  Arg *NextA = *NextIt;
  switch ((ID)NextA->getOption().getID()) {
  case OPT_iframework:
    FEOpts.SystemFwkPaths.emplace_back(NextA->getValue(), Platform);
    break;
  default:
    Diags->Report(diag::err_drv_invalid_argument_to_option)
        << A->getAsString(Args) << NextA->getAsString(Args);
    return false;
  }

  A->claim();
  NextA->claim();

  return true;
}

bool Options::processXprojectOption(InputArgList &Args, arg_iterator Curr) {
  Arg *A = *Curr;
  auto NextIt = std::next(Curr);
  if (NextIt == Args.end()) {
    Diags->Report(diag::err_drv_missing_argument) << A->getAsString(Args) << 1;
    return false;
  }

  Arg *NextA = *NextIt;
  switch ((ID)NextA->getOption().getID()) {
  case OPT_fobjc_arc:
  case OPT_fmodules:
  case OPT_fmodules_cache_path:
  case OPT_include_:
  case OPT_fvisibility_EQ:
    break;
  default:
    Diags->Report(diag::err_drv_argument_not_allowed_with)
        << A->getAsString(Args) << NextA->getAsString(Args);
    return false;
  }

  std::string ArgString = NextA->getSpelling().str();
  for (const StringRef Val : NextA->getValues())
    ArgString += Val.str();

  ProjectLevelArgs.push_back(ArgString);
  A->claim();
  NextA->claim();

  return true;
}

bool Options::processXarchOption(InputArgList &Args, arg_iterator Curr) {
  Arg *CurrArg = *Curr;
  Architecture Arch = getArchitectureFromName(CurrArg->getValue(0));
  if (Arch == AK_unknown) {
    Diags->Report(diag::err_drv_invalid_arch_name)
        << CurrArg->getAsString(Args);
    return false;
  }

  auto NextIt = std::next(Curr);
  if (NextIt == Args.end()) {
    Diags->Report(diag::err_drv_missing_argument)
        << CurrArg->getAsString(Args) << 1;
    return false;
  }

  // InstallAPI has a limited understanding of supported Xarch options.
  // Currently this is restricted to linker inputs.
  const Arg *NextArg = *NextIt;
  switch (NextArg->getOption().getID()) {
  case OPT_allowable_client:
  case OPT_reexport_l:
  case OPT_reexport_framework:
  case OPT_reexport_library:
  case OPT_rpath:
    break;
  default:
    Diags->Report(diag::err_drv_invalid_argument_to_option)
        << NextArg->getAsString(Args) << CurrArg->getAsString(Args);
    return false;
  }

  ArgToArchMap[NextArg] = Arch;
  CurrArg->claim();

  return true;
}

bool Options::processOptionList(InputArgList &Args,
                                llvm::opt::OptTable *Table) {
  Arg *A = Args.getLastArg(OPT_option_list);
  if (!A)
    return true;

  const StringRef Path = A->getValue(0);
  auto InputOrErr = FM->getBufferForFile(Path);
  if (auto Err = InputOrErr.getError()) {
    Diags->Report(diag::err_cannot_open_file) << Path << Err.message();
    return false;
  }
  // Backing storage referenced for argument processing.
  std::vector<std::string> Storage;
  auto ArgsOrErr =
      getArgListFromJSON((*InputOrErr)->getBuffer(), Table, Storage);

  if (auto Err = ArgsOrErr.takeError()) {
    Diags->Report(diag::err_cannot_read_input_list)
        << "option" << Path << toString(std::move(Err));
    return false;
  }
  return processInstallAPIXOptions(*ArgsOrErr);
}

bool Options::processLinkerOptions(InputArgList &Args) {
  // Handle required arguments.
  if (const Arg *A = Args.getLastArg(drv::OPT_install__name))
    LinkerOpts.InstallName = A->getValue();
  if (LinkerOpts.InstallName.empty()) {
    Diags->Report(diag::err_no_install_name);
    return false;
  }

  // Defaulted or optional arguments.
  if (auto *Arg = Args.getLastArg(drv::OPT_current__version))
    LinkerOpts.CurrentVersion.parse64(Arg->getValue());

  if (auto *Arg = Args.getLastArg(drv::OPT_compatibility__version))
    LinkerOpts.CompatVersion.parse64(Arg->getValue());

  if (auto *Arg = Args.getLastArg(drv::OPT_compatibility__version))
    LinkerOpts.CompatVersion.parse64(Arg->getValue());

  if (auto *Arg = Args.getLastArg(drv::OPT_umbrella))
    LinkerOpts.ParentUmbrella = Arg->getValue();

  LinkerOpts.IsDylib = Args.hasArg(drv::OPT_dynamiclib);

  for (auto *Arg : Args.filtered(drv::OPT_alias_list)) {
    LinkerOpts.AliasLists.emplace_back(Arg->getValue());
    Arg->claim();
  }

  LinkerOpts.AppExtensionSafe = Args.hasFlag(
      drv::OPT_fapplication_extension, drv::OPT_fno_application_extension,
      /*Default=*/LinkerOpts.AppExtensionSafe);

  if (::getenv("LD_NO_ENCRYPT") != nullptr)
    LinkerOpts.AppExtensionSafe = true;

  if (::getenv("LD_APPLICATION_EXTENSION_SAFE") != nullptr)
    LinkerOpts.AppExtensionSafe = true;

  // Capture library paths.
  PathSeq LibraryPaths;
  for (const Arg *A : Args.filtered(drv::OPT_L)) {
    LibraryPaths.emplace_back(A->getValue());
    A->claim();
  }

  if (!LibraryPaths.empty())
    LinkerOpts.LibPaths = std::move(LibraryPaths);

  return true;
}

// NOTE: Do not claim any arguments, as they will be passed along for CC1
// invocations.
bool Options::processFrontendOptions(InputArgList &Args) {
  // Capture language mode.
  if (auto *A = Args.getLastArgNoClaim(drv::OPT_x)) {
    FEOpts.LangMode = llvm::StringSwitch<clang::Language>(A->getValue())
                          .Case("c", clang::Language::C)
                          .Case("c++", clang::Language::CXX)
                          .Case("objective-c", clang::Language::ObjC)
                          .Case("objective-c++", clang::Language::ObjCXX)
                          .Default(clang::Language::Unknown);

    if (FEOpts.LangMode == clang::Language::Unknown) {
      Diags->Report(clang::diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
      return false;
    }
  }
  for (auto *A : Args.filtered(drv::OPT_ObjC, drv::OPT_ObjCXX)) {
    if (A->getOption().matches(drv::OPT_ObjC))
      FEOpts.LangMode = clang::Language::ObjC;
    else
      FEOpts.LangMode = clang::Language::ObjCXX;
  }

  // Capture Sysroot.
  if (const Arg *A = Args.getLastArgNoClaim(drv::OPT_isysroot)) {
    SmallString<PATH_MAX> Path(A->getValue());
    FM->makeAbsolutePath(Path);
    if (!FM->getOptionalDirectoryRef(Path)) {
      Diags->Report(diag::err_missing_sysroot) << Path;
      return false;
    }
    FEOpts.ISysroot = std::string(Path);
  } else if (FEOpts.ISysroot.empty()) {
    // Mirror CLANG and obtain the isysroot from the SDKROOT environment
    // variable, if it wasn't defined by the  command line.
    if (auto *Env = ::getenv("SDKROOT")) {
      if (StringRef(Env) != "/" && llvm::sys::path::is_absolute(Env) &&
          FM->getOptionalFileRef(Env))
        FEOpts.ISysroot = Env;
    }
  }

  // Capture system frameworks for all platforms.
  for (const Arg *A : Args.filtered(drv::OPT_iframework))
    FEOpts.SystemFwkPaths.emplace_back(A->getValue(),
                                       std::optional<PlatformType>{});

  // Capture framework paths.
  PathSeq FrameworkPaths;
  for (const Arg *A : Args.filtered(drv::OPT_F))
    FrameworkPaths.emplace_back(A->getValue());

  if (!FrameworkPaths.empty())
    FEOpts.FwkPaths = std::move(FrameworkPaths);

  // Add default framework/library paths.
  PathSeq DefaultLibraryPaths = {"/usr/lib", "/usr/local/lib"};
  PathSeq DefaultFrameworkPaths = {"/Library/Frameworks",
                                   "/System/Library/Frameworks"};

  for (const StringRef LibPath : DefaultLibraryPaths) {
    SmallString<PATH_MAX> Path(FEOpts.ISysroot);
    sys::path::append(Path, LibPath);
    LinkerOpts.LibPaths.emplace_back(Path.str());
  }
  for (const StringRef FwkPath : DefaultFrameworkPaths) {
    SmallString<PATH_MAX> Path(FEOpts.ISysroot);
    sys::path::append(Path, FwkPath);
    FEOpts.SystemFwkPaths.emplace_back(Path.str(),
                                       std::optional<PlatformType>{});
  }

  return true;
}

bool Options::addFilePaths(InputArgList &Args, PathSeq &Headers,
                           OptSpecifier ID) {
  for (const StringRef Path : Args.getAllArgValues(ID)) {
    if ((bool)FM->getDirectory(Path, /*CacheFailure=*/false)) {
      auto InputHeadersOrErr = enumerateFiles(*FM, Path);
      if (!InputHeadersOrErr) {
        Diags->Report(diag::err_cannot_open_file)
            << Path << toString(InputHeadersOrErr.takeError());
        return false;
      }
      // Sort headers to ensure deterministic behavior.
      sort(*InputHeadersOrErr);
      for (StringRef H : *InputHeadersOrErr)
        Headers.emplace_back(std::move(H));
    } else
      Headers.emplace_back(Path);
  }
  return true;
}

std::vector<const char *>
Options::processAndFilterOutInstallAPIOptions(ArrayRef<const char *> Args) {
  std::unique_ptr<llvm::opt::OptTable> Table;
  Table.reset(createDriverOptTable());

  unsigned MissingArgIndex, MissingArgCount;
  auto ParsedArgs = Table->ParseArgs(Args.slice(1), MissingArgIndex,
                                     MissingArgCount, Visibility());

  // Capture InstallAPI only driver options.
  if (!processInstallAPIXOptions(ParsedArgs))
    return {};

  if (!processOptionList(ParsedArgs, Table.get()))
    return {};

  DriverOpts.Demangle = ParsedArgs.hasArg(OPT_demangle);

  if (auto *A = ParsedArgs.getLastArg(OPT_filetype)) {
    DriverOpts.OutFT = TextAPIWriter::parseFileType(A->getValue());
    if (DriverOpts.OutFT == FileType::Invalid) {
      Diags->Report(clang::diag::err_drv_invalid_value)
          << A->getAsString(ParsedArgs) << A->getValue();
      return {};
    }
  }

  if (const Arg *A = ParsedArgs.getLastArg(OPT_verify_mode_EQ)) {
    DriverOpts.VerifyMode =
        StringSwitch<VerificationMode>(A->getValue())
            .Case("ErrorsOnly", VerificationMode::ErrorsOnly)
            .Case("ErrorsAndWarnings", VerificationMode::ErrorsAndWarnings)
            .Case("Pedantic", VerificationMode::Pedantic)
            .Default(VerificationMode::Invalid);

    if (DriverOpts.VerifyMode == VerificationMode::Invalid) {
      Diags->Report(clang::diag::err_drv_invalid_value)
          << A->getAsString(ParsedArgs) << A->getValue();
      return {};
    }
  }

  if (const Arg *A = ParsedArgs.getLastArg(OPT_verify_against))
    DriverOpts.DylibToVerify = A->getValue();

  if (const Arg *A = ParsedArgs.getLastArg(OPT_dsym))
    DriverOpts.DSYMPath = A->getValue();

  DriverOpts.TraceLibraryLocation = ParsedArgs.hasArg(OPT_t);

  // Linker options not handled by clang driver.
  LinkerOpts.OSLibNotForSharedCache =
      ParsedArgs.hasArg(OPT_not_for_dyld_shared_cache);

  for (const Arg *A : ParsedArgs.filtered(OPT_allowable_client)) {
    LinkerOpts.AllowableClients[A->getValue()] =
        ArgToArchMap.count(A) ? ArgToArchMap[A] : ArchitectureSet();
    A->claim();
  }

  for (const Arg *A : ParsedArgs.filtered(OPT_reexport_l)) {
    LinkerOpts.ReexportedLibraries[A->getValue()] =
        ArgToArchMap.count(A) ? ArgToArchMap[A] : ArchitectureSet();
    A->claim();
  }

  for (const Arg *A : ParsedArgs.filtered(OPT_reexport_library)) {
    LinkerOpts.ReexportedLibraryPaths[A->getValue()] =
        ArgToArchMap.count(A) ? ArgToArchMap[A] : ArchitectureSet();
    A->claim();
  }

  for (const Arg *A : ParsedArgs.filtered(OPT_reexport_framework)) {
    LinkerOpts.ReexportedFrameworks[A->getValue()] =
        ArgToArchMap.count(A) ? ArgToArchMap[A] : ArchitectureSet();
    A->claim();
  }

  for (const Arg *A : ParsedArgs.filtered(OPT_rpath)) {
    LinkerOpts.RPaths[A->getValue()] =
        ArgToArchMap.count(A) ? ArgToArchMap[A] : ArchitectureSet();
    A->claim();
  }

  // Handle exclude & extra header directories or files.
  auto handleAdditionalInputArgs = [&](PathSeq &Headers,
                                       clang::installapi::ID OptID) {
    if (ParsedArgs.hasArgNoClaim(OptID))
      Headers.clear();
    return addFilePaths(ParsedArgs, Headers, OptID);
  };

  if (!handleAdditionalInputArgs(DriverOpts.ExtraPublicHeaders,
                                 OPT_extra_public_header))
    return {};

  if (!handleAdditionalInputArgs(DriverOpts.ExtraPrivateHeaders,
                                 OPT_extra_private_header))
    return {};
  if (!handleAdditionalInputArgs(DriverOpts.ExtraProjectHeaders,
                                 OPT_extra_project_header))
    return {};

  if (!handleAdditionalInputArgs(DriverOpts.ExcludePublicHeaders,
                                 OPT_exclude_public_header))
    return {};
  if (!handleAdditionalInputArgs(DriverOpts.ExcludePrivateHeaders,
                                 OPT_exclude_private_header))
    return {};
  if (!handleAdditionalInputArgs(DriverOpts.ExcludeProjectHeaders,
                                 OPT_exclude_project_header))
    return {};

  // Handle umbrella headers.
  if (const Arg *A = ParsedArgs.getLastArg(OPT_public_umbrella_header))
    DriverOpts.PublicUmbrellaHeader = A->getValue();

  if (const Arg *A = ParsedArgs.getLastArg(OPT_private_umbrella_header))
    DriverOpts.PrivateUmbrellaHeader = A->getValue();

  if (const Arg *A = ParsedArgs.getLastArg(OPT_project_umbrella_header))
    DriverOpts.ProjectUmbrellaHeader = A->getValue();

  /// Any unclaimed arguments should be forwarded to the clang driver.
  std::vector<const char *> ClangDriverArgs(ParsedArgs.size());
  for (const Arg *A : ParsedArgs) {
    if (A->isClaimed())
      continue;
    // Forward along unclaimed but overlapping arguments to the clang driver.
    if (A->getOption().getID() > (unsigned)OPT_UNKNOWN) {
      ClangDriverArgs.push_back(A->getSpelling().data());
    } else
      llvm::copy(A->getValues(), std::back_inserter(ClangDriverArgs));
  }
  return ClangDriverArgs;
}

Options::Options(DiagnosticsEngine &Diag, FileManager *FM,
                 ArrayRef<const char *> Args, const StringRef ProgName)
    : Diags(&Diag), FM(FM) {

  // First process InstallAPI specific options.
  auto DriverArgs = processAndFilterOutInstallAPIOptions(Args);
  if (Diags->hasErrorOccurred())
    return;

  // Set up driver to parse remaining input arguments.
  clang::driver::Driver Driver(ProgName, llvm::sys::getDefaultTargetTriple(),
                               *Diags, "clang installapi tool");
  auto TargetAndMode =
      clang::driver::ToolChain::getTargetAndModeFromProgramName(ProgName);
  Driver.setTargetAndMode(TargetAndMode);
  bool HasError = false;
  llvm::opt::InputArgList ArgList =
      Driver.ParseArgStrings(DriverArgs, /*UseDriverMode=*/true, HasError);
  if (HasError)
    return;
  Driver.setCheckInputsExist(false);

  if (!processDriverOptions(ArgList))
    return;

  if (!processLinkerOptions(ArgList))
    return;

  if (!processFrontendOptions(ArgList))
    return;

  // After all InstallAPI necessary arguments have been collected. Go back and
  // assign values that were unknown before the clang driver opt table was used.
  ArchitectureSet AllArchs;
  llvm::for_each(DriverOpts.Targets,
                 [&AllArchs](const auto &T) { AllArchs.set(T.first.Arch); });
  auto assignDefaultLibAttrs = [&AllArchs](LibAttrs &Attrs) {
    for (StringMapEntry<ArchitectureSet> &Entry : Attrs)
      if (Entry.getValue().empty())
        Entry.setValue(AllArchs);
  };
  assignDefaultLibAttrs(LinkerOpts.AllowableClients);
  assignDefaultLibAttrs(LinkerOpts.ReexportedFrameworks);
  assignDefaultLibAttrs(LinkerOpts.ReexportedLibraries);
  assignDefaultLibAttrs(LinkerOpts.ReexportedLibraryPaths);
  assignDefaultLibAttrs(LinkerOpts.RPaths);

  /// Force cc1 options that should always be on.
  FrontendArgs = {"-fsyntax-only", "-Wprivate-extern"};

  /// Any unclaimed arguments should be handled by invoking the clang frontend.
  for (const Arg *A : ArgList) {
    if (A->isClaimed())
      continue;
    FrontendArgs.emplace_back(A->getSpelling());
    llvm::copy(A->getValues(), std::back_inserter(FrontendArgs));
  }
}

static Expected<std::unique_ptr<InterfaceFile>>
getInterfaceFile(const StringRef Filename) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(Filename);
  if (auto Err = BufferOrErr.getError())
    return errorCodeToError(std::move(Err));

  auto Buffer = std::move(*BufferOrErr);
  std::unique_ptr<InterfaceFile> IF;
  switch (identify_magic(Buffer->getBuffer())) {
  case file_magic::macho_dynamically_linked_shared_lib:
  case file_magic::macho_dynamically_linked_shared_lib_stub:
  case file_magic::macho_universal_binary:
    return DylibReader::get(Buffer->getMemBufferRef());
    break;
  case file_magic::tapi_file:
    return TextAPIReader::get(Buffer->getMemBufferRef());
  default:
    return make_error<TextAPIError>(TextAPIErrorCode::InvalidInputFormat,
                                    "unsupported library file format");
  }
  llvm_unreachable("unexpected failure in getInterface");
}

std::pair<LibAttrs, ReexportedInterfaces> Options::getReexportedLibraries() {
  LibAttrs Reexports;
  ReexportedInterfaces ReexportIFs;
  auto AccumulateReexports = [&](StringRef Path, const ArchitectureSet &Archs) {
    auto ReexportIFOrErr = getInterfaceFile(Path);
    if (!ReexportIFOrErr)
      return false;
    std::unique_ptr<InterfaceFile> Reexport = std::move(*ReexportIFOrErr);
    StringRef InstallName = Reexport->getInstallName();
    assert(!InstallName.empty() && "Parse error for install name");
    Reexports.insert({InstallName, Archs});
    ReexportIFs.emplace_back(std::move(*Reexport));
    return true;
  };

  PlatformSet Platforms;
  llvm::for_each(DriverOpts.Targets,
                 [&](const auto &T) { Platforms.insert(T.first.Platform); });
  // Populate search paths by looking at user paths before system ones.
  PathSeq FwkSearchPaths(FEOpts.FwkPaths.begin(), FEOpts.FwkPaths.end());
  for (const PlatformType P : Platforms) {
    PathSeq PlatformSearchPaths = getPathsForPlatform(FEOpts.SystemFwkPaths, P);
    FwkSearchPaths.insert(FwkSearchPaths.end(), PlatformSearchPaths.begin(),
                          PlatformSearchPaths.end());
    for (const StringMapEntry<ArchitectureSet> &Lib :
         LinkerOpts.ReexportedFrameworks) {
      std::string Name = (Lib.getKey() + ".framework/" + Lib.getKey()).str();
      std::string Path = findLibrary(Name, *FM, FwkSearchPaths, {}, {});
      if (Path.empty()) {
        Diags->Report(diag::err_cannot_find_reexport) << false << Lib.getKey();
        return {};
      }
      if (DriverOpts.TraceLibraryLocation)
        errs() << Path << "\n";

      AccumulateReexports(Path, Lib.getValue());
    }
    FwkSearchPaths.resize(FwkSearchPaths.size() - PlatformSearchPaths.size());
  }

  for (const StringMapEntry<ArchitectureSet> &Lib :
       LinkerOpts.ReexportedLibraries) {
    std::string Name = "lib" + Lib.getKey().str() + ".dylib";
    std::string Path = findLibrary(Name, *FM, {}, LinkerOpts.LibPaths, {});
    if (Path.empty()) {
      Diags->Report(diag::err_cannot_find_reexport) << true << Lib.getKey();
      return {};
    }
    if (DriverOpts.TraceLibraryLocation)
      errs() << Path << "\n";

    AccumulateReexports(Path, Lib.getValue());
  }

  for (const StringMapEntry<ArchitectureSet> &Lib :
       LinkerOpts.ReexportedLibraryPaths)
    AccumulateReexports(Lib.getKey(), Lib.getValue());

  return {std::move(Reexports), std::move(ReexportIFs)};
}

InstallAPIContext Options::createContext() {
  InstallAPIContext Ctx;
  Ctx.FM = FM;
  Ctx.Diags = Diags;

  // InstallAPI requires two level namespacing.
  Ctx.BA.TwoLevelNamespace = true;

  Ctx.BA.InstallName = LinkerOpts.InstallName;
  Ctx.BA.CurrentVersion = LinkerOpts.CurrentVersion;
  Ctx.BA.CompatVersion = LinkerOpts.CompatVersion;
  Ctx.BA.AppExtensionSafe = LinkerOpts.AppExtensionSafe;
  Ctx.BA.ParentUmbrella = LinkerOpts.ParentUmbrella;
  Ctx.BA.OSLibNotForSharedCache = LinkerOpts.OSLibNotForSharedCache;
  Ctx.FT = DriverOpts.OutFT;
  Ctx.OutputLoc = DriverOpts.OutputPath;
  Ctx.LangMode = FEOpts.LangMode;

  auto [Reexports, ReexportedIFs] = getReexportedLibraries();
  if (Diags->hasErrorOccurred())
    return Ctx;
  Ctx.Reexports = Reexports;

  // Collect symbols from alias lists.
  AliasMap Aliases;
  for (const StringRef ListPath : LinkerOpts.AliasLists) {
    auto Buffer = FM->getBufferForFile(ListPath);
    if (auto Err = Buffer.getError()) {
      Diags->Report(diag::err_cannot_open_file) << ListPath << Err.message();
      return Ctx;
    }
    Expected<AliasMap> Result = parseAliasList(Buffer.get());
    if (!Result) {
      Diags->Report(diag::err_cannot_read_input_list)
          << "symbol alias" << ListPath << toString(Result.takeError());
      return Ctx;
    }
    Aliases.insert(Result.get().begin(), Result.get().end());
  }

  // Attempt to find umbrella headers by capturing framework name.
  StringRef FrameworkName;
  if (!LinkerOpts.IsDylib)
    FrameworkName =
        Library::getFrameworkNameFromInstallName(LinkerOpts.InstallName);

  /// Process inputs headers.
  // 1. For headers discovered by directory scanning, sort them.
  // 2. For headers discovered by filelist, respect ordering.
  // 3. Append extra headers and mark any excluded headers.
  // 4. Finally, surface up umbrella headers to top of the list.
  if (!DriverOpts.InputDirectory.empty()) {
    DirectoryScanner Scanner(*FM, LinkerOpts.IsDylib
                                      ? ScanMode::ScanDylibs
                                      : ScanMode::ScanFrameworks);
    SmallString<PATH_MAX> NormalizedPath(DriverOpts.InputDirectory);
    FM->getVirtualFileSystem().makeAbsolute(NormalizedPath);
    sys::path::remove_dots(NormalizedPath, /*remove_dot_dot=*/true);
    if (llvm::Error Err = Scanner.scan(NormalizedPath)) {
      Diags->Report(diag::err_directory_scanning)
          << DriverOpts.InputDirectory << std::move(Err);
      return Ctx;
    }
    std::vector<Library> InputLibraries = Scanner.takeLibraries();
    if (InputLibraries.size() > 1) {
      Diags->Report(diag::err_more_than_one_library);
      return Ctx;
    }
    llvm::append_range(Ctx.InputHeaders,
                       DirectoryScanner::getHeaders(InputLibraries));
    llvm::stable_sort(Ctx.InputHeaders);
  }

  for (const StringRef ListPath : DriverOpts.FileLists) {
    auto Buffer = FM->getBufferForFile(ListPath);
    if (auto Err = Buffer.getError()) {
      Diags->Report(diag::err_cannot_open_file) << ListPath << Err.message();
      return Ctx;
    }
    if (auto Err = FileListReader::loadHeaders(std::move(Buffer.get()),
                                               Ctx.InputHeaders, FM)) {
      Diags->Report(diag::err_cannot_read_input_list)
          << "header file" << ListPath << std::move(Err);
      return Ctx;
    }
  }
  // After initial input has been processed, add any extra headers.
  auto HandleExtraHeaders = [&](PathSeq &Headers, HeaderType Type) -> bool {
    assert(Type != HeaderType::Unknown && "Missing header type.");
    for (const StringRef Path : Headers) {
      if (!FM->getOptionalFileRef(Path)) {
        Diags->Report(diag::err_no_such_header_file) << Path << (unsigned)Type;
        return false;
      }
      SmallString<PATH_MAX> FullPath(Path);
      FM->makeAbsolutePath(FullPath);

      auto IncludeName = createIncludeHeaderName(FullPath);
      Ctx.InputHeaders.emplace_back(
          FullPath, Type, IncludeName.has_value() ? *IncludeName : "");
      Ctx.InputHeaders.back().setExtra();
    }
    return true;
  };

  if (!HandleExtraHeaders(DriverOpts.ExtraPublicHeaders, HeaderType::Public) ||
      !HandleExtraHeaders(DriverOpts.ExtraPrivateHeaders,
                          HeaderType::Private) ||
      !HandleExtraHeaders(DriverOpts.ExtraProjectHeaders, HeaderType::Project))
    return Ctx;

  // After all headers have been added, consider excluded headers.
  std::vector<std::unique_ptr<HeaderGlob>> ExcludedHeaderGlobs;
  std::set<FileEntryRef> ExcludedHeaderFiles;
  auto ParseGlobs = [&](const PathSeq &Paths, HeaderType Type) {
    assert(Type != HeaderType::Unknown && "Missing header type.");
    for (const StringRef Path : Paths) {
      auto Glob = HeaderGlob::create(Path, Type);
      if (Glob)
        ExcludedHeaderGlobs.emplace_back(std::move(Glob.get()));
      else {
        consumeError(Glob.takeError());
        if (auto File = FM->getFileRef(Path))
          ExcludedHeaderFiles.emplace(*File);
        else {
          Diags->Report(diag::err_no_such_header_file)
              << Path << (unsigned)Type;
          return false;
        }
      }
    }
    return true;
  };

  if (!ParseGlobs(DriverOpts.ExcludePublicHeaders, HeaderType::Public) ||
      !ParseGlobs(DriverOpts.ExcludePrivateHeaders, HeaderType::Private) ||
      !ParseGlobs(DriverOpts.ExcludeProjectHeaders, HeaderType::Project))
    return Ctx;

  for (HeaderFile &Header : Ctx.InputHeaders) {
    for (auto &Glob : ExcludedHeaderGlobs)
      if (Glob->match(Header))
        Header.setExcluded();
  }
  if (!ExcludedHeaderFiles.empty()) {
    for (HeaderFile &Header : Ctx.InputHeaders) {
      auto FileRef = FM->getFileRef(Header.getPath());
      if (!FileRef)
        continue;
      if (ExcludedHeaderFiles.count(*FileRef))
        Header.setExcluded();
    }
  }
  // Report if glob was ignored.
  for (const auto &Glob : ExcludedHeaderGlobs)
    if (!Glob->didMatch())
      Diags->Report(diag::warn_glob_did_not_match) << Glob->str();

  // Mark any explicit or inferred umbrella headers. If one exists, move
  // that to the beginning of the input headers.
  auto MarkandMoveUmbrellaInHeaders = [&](llvm::Regex &Regex,
                                          HeaderType Type) -> bool {
    auto It = find_if(Ctx.InputHeaders, [&Regex, Type](const HeaderFile &H) {
      return (H.getType() == Type) && Regex.match(H.getPath());
    });

    if (It == Ctx.InputHeaders.end())
      return false;
    It->setUmbrellaHeader();

    // Because there can be an umbrella header per header type,
    // find the first non umbrella header to swap position with.
    auto BeginPos = find_if(Ctx.InputHeaders, [](const HeaderFile &H) {
      return !H.isUmbrellaHeader();
    });
    if (BeginPos != Ctx.InputHeaders.end() && BeginPos < It)
      std::swap(*BeginPos, *It);
    return true;
  };

  auto FindUmbrellaHeader = [&](StringRef HeaderPath, HeaderType Type) -> bool {
    assert(Type != HeaderType::Unknown && "Missing header type.");
    if (!HeaderPath.empty()) {
      auto EscapedString = Regex::escape(HeaderPath);
      Regex UmbrellaRegex(EscapedString);
      if (!MarkandMoveUmbrellaInHeaders(UmbrellaRegex, Type)) {
        Diags->Report(diag::err_no_such_umbrella_header_file)
            << HeaderPath << (unsigned)Type;
        return false;
      }
    } else if (!FrameworkName.empty() && (Type != HeaderType::Project)) {
      auto UmbrellaName = "/" + Regex::escape(FrameworkName);
      if (Type == HeaderType::Public)
        UmbrellaName += "\\.h";
      else
        UmbrellaName += "[_]?Private\\.h";
      Regex UmbrellaRegex(UmbrellaName);
      MarkandMoveUmbrellaInHeaders(UmbrellaRegex, Type);
    }
    return true;
  };
  if (!FindUmbrellaHeader(DriverOpts.PublicUmbrellaHeader,
                          HeaderType::Public) ||
      !FindUmbrellaHeader(DriverOpts.PrivateUmbrellaHeader,
                          HeaderType::Private) ||
      !FindUmbrellaHeader(DriverOpts.ProjectUmbrellaHeader,
                          HeaderType::Project))
    return Ctx;

  // Parse binary dylib and initialize verifier.
  if (DriverOpts.DylibToVerify.empty()) {
    Ctx.Verifier = std::make_unique<DylibVerifier>();
    return Ctx;
  }

  auto Buffer = FM->getBufferForFile(DriverOpts.DylibToVerify);
  if (auto Err = Buffer.getError()) {
    Diags->Report(diag::err_cannot_open_file)
        << DriverOpts.DylibToVerify << Err.message();
    return Ctx;
  }

  DylibReader::ParseOption PO;
  PO.Undefineds = false;
  Expected<Records> Slices =
      DylibReader::readFile((*Buffer)->getMemBufferRef(), PO);
  if (auto Err = Slices.takeError()) {
    Diags->Report(diag::err_cannot_open_file)
        << DriverOpts.DylibToVerify << std::move(Err);
    return Ctx;
  }

  Ctx.Verifier = std::make_unique<DylibVerifier>(
      std::move(*Slices), std::move(ReexportedIFs), std::move(Aliases), Diags,
      DriverOpts.VerifyMode, DriverOpts.Zippered, DriverOpts.Demangle,
      DriverOpts.DSYMPath);
  return Ctx;
}

void Options::addConditionalCC1Args(std::vector<std::string> &ArgStrings,
                                    const llvm::Triple &Targ,
                                    const HeaderType Type) {
  // Unique to architecture (Xarch) options hold no arguments to pass along for
  // frontend.

  // Add specific to platform arguments.
  PathSeq PlatformSearchPaths =
      getPathsForPlatform(FEOpts.SystemFwkPaths, mapToPlatformType(Targ));
  llvm::for_each(PlatformSearchPaths, [&ArgStrings](const StringRef Path) {
    ArgStrings.push_back("-iframework");
    ArgStrings.push_back(Path.str());
  });

  // Add specific to header type arguments.
  if (Type == HeaderType::Project)
    for (const StringRef A : ProjectLevelArgs)
      ArgStrings.emplace_back(A);
}

} // namespace installapi
} // namespace clang
