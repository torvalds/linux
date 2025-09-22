//===- TextStubV5.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements Text Stub JSON mappings.
//
//===----------------------------------------------------------------------===//
#include "TextStubCommon.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/JSON.h"
#include <utility>

// clang-format off
/*

JSON Format specification.

All library level keys, accept target values and are defaulted if not specified. 

{
"tapi_tbd_version": 5,                            # Required: TBD version for all documents in file
"main_library": {                                 # Required: top level library
  "target_info": [                                # Required: target information 
    {
      "target": "x86_64-macos",
      "min_deployment": "10.14"                   # Optional: minOS defaults to 0
    },
    {
      "target": "arm64-macos",
      "min_deployment": "10.14"
    },
    {
      "target": "arm64-maccatalyst",
      "min_deployment": "12.1"
    }],
  "flags":[{"attributes": ["flat_namespace"]}],     # Optional:
  "install_names":[{"name":"/S/L/F/Foo.fwk/Foo"}],  # Required: library install name 
  "current_versions":[{"version": "1.2"}],          # Optional: defaults to 1
  "compatibility_versions":[{ "version": "1.1"}],   # Optional: defaults to 1
  "rpaths": [                                       # Optional: 
    {
      "targets": ["x86_64-macos"],                  # Optional: defaults to targets in `target-info`
      "paths": ["@executable_path/.../Frameworks"]
    }],
  "parent_umbrellas": [{"umbrella": "System"}],
  "allowable_clients": [{"clients": ["ClientA"]}],
  "reexported_libraries": [{"names": ["/u/l/l/foo.dylib"]}],
  "exported_symbols": [{                            # List of export symbols section
      "targets": ["x86_64-macos", "arm64-macos"],   # Optional: defaults to targets in `target-info`
        "text": {                                   # List of Text segment symbols 
          "global": [ "_func" ],
          "weak": [],
          "thread_local": []
        },
        "data": { ... },                            # List of Data segment symbols
   }],
  "reexported_symbols": [{  ... }],                 # List of reexported symbols section
  "undefined_symbols": [{ ... }]                    # List of undefined symbols section
},
"libraries": [                                      # Optional: Array of inlined libraries
  {...}, {...}, {...}
]
}
*/
// clang-format on

using namespace llvm;
using namespace llvm::json;
using namespace llvm::MachO;

namespace {
struct JSONSymbol {
  EncodeKind Kind;
  std::string Name;
  SymbolFlags Flags;
};

using AttrToTargets = std::map<std::string, TargetList>;
using TargetsToSymbols =
    SmallVector<std::pair<TargetList, std::vector<JSONSymbol>>>;

enum TBDKey : size_t {
  TBDVersion = 0U,
  MainLibrary,
  Documents,
  TargetInfo,
  Targets,
  Target,
  Deployment,
  Flags,
  Attributes,
  InstallName,
  CurrentVersion,
  CompatibilityVersion,
  Version,
  SwiftABI,
  ABI,
  ParentUmbrella,
  Umbrella,
  AllowableClients,
  Clients,
  ReexportLibs,
  Names,
  Name,
  Exports,
  Reexports,
  Undefineds,
  Data,
  Text,
  Weak,
  ThreadLocal,
  Globals,
  ObjCClass,
  ObjCEHType,
  ObjCIvar,
  RPath,
  Paths,
};

std::array<StringRef, 64> Keys = {
    "tapi_tbd_version",
    "main_library",
    "libraries",
    "target_info",
    "targets",
    "target",
    "min_deployment",
    "flags",
    "attributes",
    "install_names",
    "current_versions",
    "compatibility_versions",
    "version",
    "swift_abi",
    "abi",
    "parent_umbrellas",
    "umbrella",
    "allowable_clients",
    "clients",
    "reexported_libraries",
    "names",
    "name",
    "exported_symbols",
    "reexported_symbols",
    "undefined_symbols",
    "data",
    "text",
    "weak",
    "thread_local",
    "global",
    "objc_class",
    "objc_eh_type",
    "objc_ivar",
    "rpaths",
    "paths",
};

static llvm::SmallString<128> getParseErrorMsg(TBDKey Key) {
  return {"invalid ", Keys[Key], " section"};
}

static llvm::SmallString<128> getSerializeErrorMsg(TBDKey Key) {
  return {"missing ", Keys[Key], " information"};
}

class JSONStubError : public llvm::ErrorInfo<llvm::json::ParseError> {
public:
  JSONStubError(Twine ErrMsg) : Message(ErrMsg.str()) {}

  void log(llvm::raw_ostream &OS) const override { OS << Message << "\n"; }
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }

private:
  std::string Message;
};

template <typename JsonT, typename StubT = JsonT>
Expected<StubT> getRequiredValue(
    TBDKey Key, const Object *Obj,
    std::function<std::optional<JsonT>(const Object *, StringRef)> GetValue,
    std::function<std::optional<StubT>(JsonT)> Validate = nullptr) {
  std::optional<JsonT> Val = GetValue(Obj, Keys[Key]);
  if (!Val)
    return make_error<JSONStubError>(getParseErrorMsg(Key));

  if (Validate == nullptr)
    return static_cast<StubT>(*Val);

  std::optional<StubT> Result = Validate(*Val);
  if (!Result.has_value())
    return make_error<JSONStubError>(getParseErrorMsg(Key));
  return Result.value();
}

template <typename JsonT, typename StubT = JsonT>
Expected<StubT> getRequiredValue(
    TBDKey Key, const Object *Obj,
    std::function<std::optional<JsonT>(const Object *, StringRef)> const
        GetValue,
    StubT DefaultValue, function_ref<std::optional<StubT>(JsonT)> Validate) {
  std::optional<JsonT> Val = GetValue(Obj, Keys[Key]);
  if (!Val)
    return DefaultValue;

  std::optional<StubT> Result;
  Result = Validate(*Val);
  if (!Result.has_value())
    return make_error<JSONStubError>(getParseErrorMsg(Key));
  return Result.value();
}

Error collectFromArray(TBDKey Key, const Object *Obj,
                       function_ref<void(StringRef)> Append,
                       bool IsRequired = false) {
  const auto *Values = Obj->getArray(Keys[Key]);
  if (!Values) {
    if (IsRequired)
      return make_error<JSONStubError>(getParseErrorMsg(Key));
    return Error::success();
  }

  for (const Value &Val : *Values) {
    auto ValStr = Val.getAsString();
    if (!ValStr.has_value())
      return make_error<JSONStubError>(getParseErrorMsg(Key));
    Append(ValStr.value());
  }

  return Error::success();
}

namespace StubParser {

Expected<FileType> getVersion(const Object *File) {
  auto VersionOrErr = getRequiredValue<int64_t, FileType>(
      TBDKey::TBDVersion, File, &Object::getInteger,
      [](int64_t Val) -> std::optional<FileType> {
        unsigned Result = Val;
        if (Result != 5)
          return std::nullopt;
        return FileType::TBD_V5;
      });

  if (!VersionOrErr)
    return VersionOrErr.takeError();
  return *VersionOrErr;
}

Expected<TargetList> getTargets(const Object *Section) {
  const auto *Targets = Section->getArray(Keys[TBDKey::Targets]);
  if (!Targets)
    return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Targets));

  TargetList IFTargets;
  for (const Value &JSONTarget : *Targets) {
    auto TargetStr = JSONTarget.getAsString();
    if (!TargetStr.has_value())
      return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Target));
    auto TargetOrErr = Target::create(TargetStr.value());
    if (!TargetOrErr)
      return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Target));
    IFTargets.push_back(*TargetOrErr);
  }
  return std::move(IFTargets);
}

Expected<TargetList> getTargetsSection(const Object *Section) {
  const Array *Targets = Section->getArray(Keys[TBDKey::TargetInfo]);
  if (!Targets)
    return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Targets));

  TargetList IFTargets;
  for (const Value &JSONTarget : *Targets) {
    const auto *Obj = JSONTarget.getAsObject();
    if (!Obj)
      return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Target));
    auto TargetStr =
        getRequiredValue<StringRef>(TBDKey::Target, Obj, &Object::getString);
    if (!TargetStr)
      return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Target));
    auto TargetOrErr = Target::create(*TargetStr);
    if (!TargetOrErr)
      return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Target));

    auto VersionStr = Obj->getString(Keys[TBDKey::Deployment]);
    VersionTuple Version;
    if (VersionStr && Version.tryParse(*VersionStr))
      return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Deployment));
    TargetOrErr->MinDeployment = Version;

    // Convert to LLVM::Triple to accurately compute minOS + platform + arch
    // pairing.
    IFTargets.push_back(
        MachO::Target(Triple(getTargetTripleName(*TargetOrErr))));
  }
  return std::move(IFTargets);
}

Error collectSymbolsFromSegment(const Object *Segment, TargetsToSymbols &Result,
                                SymbolFlags SectionFlag) {
  auto Err = collectFromArray(
      TBDKey::Globals, Segment, [&Result, &SectionFlag](StringRef Name) {
        JSONSymbol Sym = {EncodeKind::GlobalSymbol, Name.str(), SectionFlag};
        Result.back().second.emplace_back(Sym);
      });
  if (Err)
    return Err;

  Err = collectFromArray(
      TBDKey::ObjCClass, Segment, [&Result, &SectionFlag](StringRef Name) {
        JSONSymbol Sym = {EncodeKind::ObjectiveCClass, Name.str(), SectionFlag};
        Result.back().second.emplace_back(Sym);
      });
  if (Err)
    return Err;

  Err = collectFromArray(TBDKey::ObjCEHType, Segment,
                         [&Result, &SectionFlag](StringRef Name) {
                           JSONSymbol Sym = {EncodeKind::ObjectiveCClassEHType,
                                             Name.str(), SectionFlag};
                           Result.back().second.emplace_back(Sym);
                         });
  if (Err)
    return Err;

  Err = collectFromArray(
      TBDKey::ObjCIvar, Segment, [&Result, &SectionFlag](StringRef Name) {
        JSONSymbol Sym = {EncodeKind::ObjectiveCInstanceVariable, Name.str(),
                          SectionFlag};
        Result.back().second.emplace_back(Sym);
      });
  if (Err)
    return Err;

  SymbolFlags WeakFlag =
      SectionFlag |
      (((SectionFlag & SymbolFlags::Undefined) == SymbolFlags::Undefined)
           ? SymbolFlags::WeakReferenced
           : SymbolFlags::WeakDefined);
  Err = collectFromArray(
      TBDKey::Weak, Segment, [&Result, WeakFlag](StringRef Name) {
        JSONSymbol Sym = {EncodeKind::GlobalSymbol, Name.str(), WeakFlag};
        Result.back().second.emplace_back(Sym);
      });
  if (Err)
    return Err;

  Err = collectFromArray(
      TBDKey::ThreadLocal, Segment, [&Result, SectionFlag](StringRef Name) {
        JSONSymbol Sym = {EncodeKind::GlobalSymbol, Name.str(),
                          SymbolFlags::ThreadLocalValue | SectionFlag};
        Result.back().second.emplace_back(Sym);
      });
  if (Err)
    return Err;

  return Error::success();
}

Expected<StringRef> getNameSection(const Object *File) {
  const Array *Section = File->getArray(Keys[TBDKey::InstallName]);
  if (!Section)
    return make_error<JSONStubError>(getParseErrorMsg(TBDKey::InstallName));

  assert(!Section->empty() && "unexpected missing install name");
  // TODO: Just take first for now.
  const auto *Obj = Section->front().getAsObject();
  if (!Obj)
    return make_error<JSONStubError>(getParseErrorMsg(TBDKey::InstallName));

  return getRequiredValue<StringRef>(TBDKey::Name, Obj, &Object::getString);
}

Expected<TargetsToSymbols> getSymbolSection(const Object *File, TBDKey Key,
                                            TargetList &Targets) {

  const Array *Section = File->getArray(Keys[Key]);
  if (!Section)
    return TargetsToSymbols();

  SymbolFlags SectionFlag;
  switch (Key) {
  case TBDKey::Reexports:
    SectionFlag = SymbolFlags::Rexported;
    break;
  case TBDKey::Undefineds:
    SectionFlag = SymbolFlags::Undefined;
    break;
  default:
    SectionFlag = SymbolFlags::None;
    break;
  };

  TargetsToSymbols Result;
  TargetList MappedTargets;
  for (auto Val : *Section) {
    auto *Obj = Val.getAsObject();
    if (!Obj)
      continue;

    auto TargetsOrErr = getTargets(Obj);
    if (!TargetsOrErr) {
      MappedTargets = Targets;
      consumeError(TargetsOrErr.takeError());
    } else {
      MappedTargets = *TargetsOrErr;
    }
    Result.emplace_back(
        std::make_pair(std::move(MappedTargets), std::vector<JSONSymbol>()));

    auto *DataSection = Obj->getObject(Keys[TBDKey::Data]);
    auto *TextSection = Obj->getObject(Keys[TBDKey::Text]);
    // There should be at least one valid section.
    if (!DataSection && !TextSection)
      return make_error<JSONStubError>(getParseErrorMsg(Key));

    if (DataSection) {
      auto Err = collectSymbolsFromSegment(DataSection, Result,
                                           SectionFlag | SymbolFlags::Data);
      if (Err)
        return std::move(Err);
    }
    if (TextSection) {
      auto Err = collectSymbolsFromSegment(TextSection, Result,
                                           SectionFlag | SymbolFlags::Text);
      if (Err)
        return std::move(Err);
    }
  }

  return std::move(Result);
}

Expected<AttrToTargets> getLibSection(const Object *File, TBDKey Key,
                                      TBDKey SubKey,
                                      const TargetList &Targets) {
  auto *Section = File->getArray(Keys[Key]);
  if (!Section)
    return AttrToTargets();

  AttrToTargets Result;
  TargetList MappedTargets;
  for (auto Val : *Section) {
    auto *Obj = Val.getAsObject();
    if (!Obj)
      continue;

    auto TargetsOrErr = getTargets(Obj);
    if (!TargetsOrErr) {
      MappedTargets = Targets;
      consumeError(TargetsOrErr.takeError());
    } else {
      MappedTargets = *TargetsOrErr;
    }
    auto Err =
        collectFromArray(SubKey, Obj, [&Result, &MappedTargets](StringRef Key) {
          Result[Key.str()] = MappedTargets;
        });
    if (Err)
      return std::move(Err);
  }

  return std::move(Result);
}

Expected<AttrToTargets> getUmbrellaSection(const Object *File,
                                           const TargetList &Targets) {
  const auto *Umbrella = File->getArray(Keys[TBDKey::ParentUmbrella]);
  if (!Umbrella)
    return AttrToTargets();

  AttrToTargets Result;
  TargetList MappedTargets;
  for (auto Val : *Umbrella) {
    auto *Obj = Val.getAsObject();
    if (!Obj)
      return make_error<JSONStubError>(
          getParseErrorMsg(TBDKey::ParentUmbrella));

    // Get Targets section.
    auto TargetsOrErr = getTargets(Obj);
    if (!TargetsOrErr) {
      MappedTargets = Targets;
      consumeError(TargetsOrErr.takeError());
    } else {
      MappedTargets = *TargetsOrErr;
    }

    auto UmbrellaOrErr =
        getRequiredValue<StringRef>(TBDKey::Umbrella, Obj, &Object::getString);
    if (!UmbrellaOrErr)
      return UmbrellaOrErr.takeError();
    Result[UmbrellaOrErr->str()] = Targets;
  }
  return std::move(Result);
}

Expected<uint8_t> getSwiftVersion(const Object *File) {
  const Array *Versions = File->getArray(Keys[TBDKey::SwiftABI]);
  if (!Versions)
    return 0;

  for (const auto &Val : *Versions) {
    const auto *Obj = Val.getAsObject();
    if (!Obj)
      return make_error<JSONStubError>(getParseErrorMsg(TBDKey::SwiftABI));

    // TODO: Take first for now.
    return getRequiredValue<int64_t, uint8_t>(TBDKey::ABI, Obj,
                                              &Object::getInteger);
  }

  return 0;
}

Expected<PackedVersion> getPackedVersion(const Object *File, TBDKey Key) {
  const Array *Versions = File->getArray(Keys[Key]);
  if (!Versions)
    return PackedVersion(1, 0, 0);

  for (const auto &Val : *Versions) {
    const auto *Obj = Val.getAsObject();
    if (!Obj)
      return make_error<JSONStubError>(getParseErrorMsg(Key));

    auto ValidatePV = [](StringRef Version) -> std::optional<PackedVersion> {
      PackedVersion PV;
      auto [success, truncated] = PV.parse64(Version);
      if (!success || truncated)
        return std::nullopt;
      return PV;
    };
    // TODO: Take first for now.
    return getRequiredValue<StringRef, PackedVersion>(
        TBDKey::Version, Obj, &Object::getString, PackedVersion(1, 0, 0),
        ValidatePV);
  }

  return PackedVersion(1, 0, 0);
}

Expected<TBDFlags> getFlags(const Object *File) {
  TBDFlags Flags = TBDFlags::None;
  const Array *Section = File->getArray(Keys[TBDKey::Flags]);
  if (!Section || Section->empty())
    return Flags;

  for (auto &Val : *Section) {
    // FIXME: Flags currently apply to all target triples.
    const auto *Obj = Val.getAsObject();
    if (!Obj)
      return make_error<JSONStubError>(getParseErrorMsg(TBDKey::Flags));

    auto FlagsOrErr =
        collectFromArray(TBDKey::Attributes, Obj, [&Flags](StringRef Flag) {
          TBDFlags TBDFlag =
              StringSwitch<TBDFlags>(Flag)
                  .Case("flat_namespace", TBDFlags::FlatNamespace)
                  .Case("not_app_extension_safe",
                        TBDFlags::NotApplicationExtensionSafe)
                  .Case("sim_support", TBDFlags::SimulatorSupport)
                  .Case("not_for_dyld_shared_cache",
                        TBDFlags::OSLibNotForSharedCache)
                  .Default(TBDFlags::None);
          Flags |= TBDFlag;
        });

    if (FlagsOrErr)
      return std::move(FlagsOrErr);

    return Flags;
  }

  return Flags;
}

using IFPtr = std::unique_ptr<InterfaceFile>;
Expected<IFPtr> parseToInterfaceFile(const Object *File) {
  auto TargetsOrErr = getTargetsSection(File);
  if (!TargetsOrErr)
    return TargetsOrErr.takeError();
  TargetList Targets = *TargetsOrErr;

  auto NameOrErr = getNameSection(File);
  if (!NameOrErr)
    return NameOrErr.takeError();
  StringRef Name = *NameOrErr;

  auto CurrVersionOrErr = getPackedVersion(File, TBDKey::CurrentVersion);
  if (!CurrVersionOrErr)
    return CurrVersionOrErr.takeError();
  PackedVersion CurrVersion = *CurrVersionOrErr;

  auto CompVersionOrErr = getPackedVersion(File, TBDKey::CompatibilityVersion);
  if (!CompVersionOrErr)
    return CompVersionOrErr.takeError();
  PackedVersion CompVersion = *CompVersionOrErr;

  auto SwiftABIOrErr = getSwiftVersion(File);
  if (!SwiftABIOrErr)
    return SwiftABIOrErr.takeError();
  uint8_t SwiftABI = *SwiftABIOrErr;

  auto FlagsOrErr = getFlags(File);
  if (!FlagsOrErr)
    return FlagsOrErr.takeError();
  TBDFlags Flags = *FlagsOrErr;

  auto UmbrellasOrErr = getUmbrellaSection(File, Targets);
  if (!UmbrellasOrErr)
    return UmbrellasOrErr.takeError();
  AttrToTargets Umbrellas = *UmbrellasOrErr;

  auto ClientsOrErr =
      getLibSection(File, TBDKey::AllowableClients, TBDKey::Clients, Targets);
  if (!ClientsOrErr)
    return ClientsOrErr.takeError();
  AttrToTargets Clients = *ClientsOrErr;

  auto RLOrErr =
      getLibSection(File, TBDKey::ReexportLibs, TBDKey::Names, Targets);
  if (!RLOrErr)
    return RLOrErr.takeError();
  AttrToTargets ReexportLibs = std::move(*RLOrErr);

  auto RPathsOrErr = getLibSection(File, TBDKey::RPath, TBDKey::Paths, Targets);
  if (!RPathsOrErr)
    return RPathsOrErr.takeError();
  AttrToTargets RPaths = std::move(*RPathsOrErr);

  auto ExportsOrErr = getSymbolSection(File, TBDKey::Exports, Targets);
  if (!ExportsOrErr)
    return ExportsOrErr.takeError();
  TargetsToSymbols Exports = std::move(*ExportsOrErr);

  auto ReexportsOrErr = getSymbolSection(File, TBDKey::Reexports, Targets);
  if (!ReexportsOrErr)
    return ReexportsOrErr.takeError();
  TargetsToSymbols Reexports = std::move(*ReexportsOrErr);

  auto UndefinedsOrErr = getSymbolSection(File, TBDKey::Undefineds, Targets);
  if (!UndefinedsOrErr)
    return UndefinedsOrErr.takeError();
  TargetsToSymbols Undefineds = std::move(*UndefinedsOrErr);

  IFPtr F(new InterfaceFile);
  F->setInstallName(Name);
  F->setCurrentVersion(CurrVersion);
  F->setCompatibilityVersion(CompVersion);
  F->setSwiftABIVersion(SwiftABI);
  F->setTwoLevelNamespace(!(Flags & TBDFlags::FlatNamespace));
  F->setApplicationExtensionSafe(
      !(Flags & TBDFlags::NotApplicationExtensionSafe));
  F->setSimulatorSupport((Flags & TBDFlags::SimulatorSupport));
  F->setOSLibNotForSharedCache((Flags & TBDFlags::OSLibNotForSharedCache));
  for (auto &T : Targets)
    F->addTarget(T);
  for (auto &[Lib, Targets] : Clients)
    for (auto Target : Targets)
      F->addAllowableClient(Lib, Target);
  for (auto &[Lib, Targets] : ReexportLibs)
    for (auto Target : Targets)
      F->addReexportedLibrary(Lib, Target);
  for (auto &[Lib, Targets] : Umbrellas)
    for (auto Target : Targets)
      F->addParentUmbrella(Target, Lib);
  for (auto &[Path, Targets] : RPaths)
    for (auto Target : Targets)
      F->addRPath(Path, Target);
  for (auto &[Targets, Symbols] : Exports)
    for (auto &Sym : Symbols)
      F->addSymbol(Sym.Kind, Sym.Name, Targets, Sym.Flags);
  for (auto &[Targets, Symbols] : Reexports)
    for (auto &Sym : Symbols)
      F->addSymbol(Sym.Kind, Sym.Name, Targets, Sym.Flags);
  for (auto &[Targets, Symbols] : Undefineds)
    for (auto &Sym : Symbols)
      F->addSymbol(Sym.Kind, Sym.Name, Targets, Sym.Flags);

  return std::move(F);
}

Expected<std::vector<IFPtr>> getInlinedLibs(const Object *File) {
  std::vector<IFPtr> IFs;
  const Array *Files = File->getArray(Keys[TBDKey::Documents]);
  if (!Files)
    return std::move(IFs);

  for (auto Lib : *Files) {
    auto IFOrErr = parseToInterfaceFile(Lib.getAsObject());
    if (!IFOrErr)
      return IFOrErr.takeError();
    auto IF = std::move(*IFOrErr);
    IFs.emplace_back(std::move(IF));
  }
  return std::move(IFs);
}

} // namespace StubParser
} // namespace

Expected<std::unique_ptr<InterfaceFile>>
MachO::getInterfaceFileFromJSON(StringRef JSON) {
  auto ValOrErr = parse(JSON);
  if (!ValOrErr)
    return ValOrErr.takeError();

  auto *Root = ValOrErr->getAsObject();
  auto VersionOrErr = StubParser::getVersion(Root);
  if (!VersionOrErr)
    return VersionOrErr.takeError();
  FileType Version = *VersionOrErr;

  Object *MainLib = Root->getObject(Keys[TBDKey::MainLibrary]);
  auto IFOrErr = StubParser::parseToInterfaceFile(MainLib);
  if (!IFOrErr)
    return IFOrErr.takeError();
  (*IFOrErr)->setFileType(Version);
  std::unique_ptr<InterfaceFile> IF(std::move(*IFOrErr));

  auto IFsOrErr = StubParser::getInlinedLibs(Root);
  if (!IFsOrErr)
    return IFsOrErr.takeError();
  for (auto &File : *IFsOrErr) {
    File->setFileType(Version);
    IF->addDocument(std::shared_ptr<InterfaceFile>(std::move(File)));
  }
  return std::move(IF);
}

namespace {

template <typename ContainerT = Array>
bool insertNonEmptyValues(Object &Obj, TBDKey Key, ContainerT &&Contents) {
  if (Contents.empty())
    return false;
  Obj[Keys[Key]] = std::move(Contents);
  return true;
}

std::string getFormattedStr(const MachO::Target &Targ) {
  std::string PlatformStr = Targ.Platform == PLATFORM_MACCATALYST
                                ? "maccatalyst"
                                : getOSAndEnvironmentName(Targ.Platform);
  return (getArchitectureName(Targ.Arch) + "-" + PlatformStr).str();
}

template <typename AggregateT>
std::vector<std::string> serializeTargets(const AggregateT Targets,
                                          const TargetList &ActiveTargets) {
  std::vector<std::string> TargetsStr;
  if (Targets.size() == ActiveTargets.size())
    return TargetsStr;

  for (const MachO::Target &Target : Targets)
    TargetsStr.emplace_back(getFormattedStr(Target));

  return TargetsStr;
}

Array serializeTargetInfo(const TargetList &ActiveTargets) {
  Array Targets;
  for (const auto Targ : ActiveTargets) {
    Object TargetInfo;
    if (!Targ.MinDeployment.empty())
      TargetInfo[Keys[TBDKey::Deployment]] = Targ.MinDeployment.getAsString();
    TargetInfo[Keys[TBDKey::Target]] = getFormattedStr(Targ);
    Targets.emplace_back(std::move(TargetInfo));
  }
  return Targets;
}

template <typename ValueT, typename EntryT = ValueT>
Array serializeScalar(TBDKey Key, ValueT Value, ValueT Default = ValueT()) {
  if (Value == Default)
    return {};
  Array Container;
  Object ScalarObj({Object::KV({Keys[Key], EntryT(Value)})});

  Container.emplace_back(std::move(ScalarObj));
  return Container;
}

using TargetsToValuesMap =
    std::map<std::vector<std::string>, std::vector<std::string>>;

template <typename AggregateT = TargetsToValuesMap>
Array serializeAttrToTargets(AggregateT &Entries, TBDKey Key) {
  Array Container;
  for (const auto &[Targets, Values] : Entries) {
    Object Obj;
    insertNonEmptyValues(Obj, TBDKey::Targets, std::move(Targets));
    Obj[Keys[Key]] = Values;
    Container.emplace_back(std::move(Obj));
  }
  return Container;
}

template <typename ValueT = std::string,
          typename AggregateT = std::vector<std::pair<MachO::Target, ValueT>>>
Array serializeField(TBDKey Key, const AggregateT &Values,
                     const TargetList &ActiveTargets, bool IsArray = true) {
  std::map<ValueT, std::set<MachO::Target>> Entries;
  for (const auto &[Target, Val] : Values)
    Entries[Val].insert(Target);

  if (!IsArray) {
    std::map<std::vector<std::string>, std::string> FinalEntries;
    for (const auto &[Val, Targets] : Entries)
      FinalEntries[serializeTargets(Targets, ActiveTargets)] = Val;
    return serializeAttrToTargets(FinalEntries, Key);
  }

  TargetsToValuesMap FinalEntries;
  for (const auto &[Val, Targets] : Entries)
    FinalEntries[serializeTargets(Targets, ActiveTargets)].emplace_back(Val);
  return serializeAttrToTargets(FinalEntries, Key);
}

Array serializeField(TBDKey Key, const std::vector<InterfaceFileRef> &Values,
                     const TargetList &ActiveTargets) {
  TargetsToValuesMap FinalEntries;
  for (const auto &Ref : Values) {
    TargetList Targets{Ref.targets().begin(), Ref.targets().end()};
    FinalEntries[serializeTargets(Targets, ActiveTargets)].emplace_back(
        Ref.getInstallName());
  }
  return serializeAttrToTargets(FinalEntries, Key);
}

struct SymbolFields {
  struct SymbolTypes {
    std::vector<StringRef> Weaks;
    std::vector<StringRef> Globals;
    std::vector<StringRef> TLV;
    std::vector<StringRef> ObjCClasses;
    std::vector<StringRef> IVars;
    std::vector<StringRef> EHTypes;

    bool empty() const {
      return Weaks.empty() && Globals.empty() && TLV.empty() &&
             ObjCClasses.empty() && IVars.empty() && EHTypes.empty();
    }
  };
  SymbolTypes Data;
  SymbolTypes Text;
};

Array serializeSymbols(InterfaceFile::const_filtered_symbol_range Symbols,
                       const TargetList &ActiveTargets) {
  auto AssignForSymbolType = [](SymbolFields::SymbolTypes &Assignment,
                                const Symbol *Sym) {
    switch (Sym->getKind()) {
    case EncodeKind::ObjectiveCClass:
      Assignment.ObjCClasses.emplace_back(Sym->getName());
      return;
    case EncodeKind::ObjectiveCClassEHType:
      Assignment.EHTypes.emplace_back(Sym->getName());
      return;
    case EncodeKind::ObjectiveCInstanceVariable:
      Assignment.IVars.emplace_back(Sym->getName());
      return;
    case EncodeKind::GlobalSymbol: {
      if (Sym->isWeakReferenced() || Sym->isWeakDefined())
        Assignment.Weaks.emplace_back(Sym->getName());
      else if (Sym->isThreadLocalValue())
        Assignment.TLV.emplace_back(Sym->getName());
      else
        Assignment.Globals.emplace_back(Sym->getName());
      return;
    }
    }
  };

  std::map<std::vector<std::string>, SymbolFields> Entries;
  for (const auto *Sym : Symbols) {
    std::set<MachO::Target> Targets{Sym->targets().begin(),
                                    Sym->targets().end()};
    auto JSONTargets = serializeTargets(Targets, ActiveTargets);
    if (Sym->isData())
      AssignForSymbolType(Entries[std::move(JSONTargets)].Data, Sym);
    else if (Sym->isText())
      AssignForSymbolType(Entries[std::move(JSONTargets)].Text, Sym);
    else
      llvm_unreachable("unexpected symbol type");
  }

  auto InsertSymbolsToJSON = [](Object &SymSection, TBDKey SegmentKey,
                                SymbolFields::SymbolTypes &SymField) {
    if (SymField.empty())
      return;
    Object Segment;
    insertNonEmptyValues(Segment, TBDKey::Globals, std::move(SymField.Globals));
    insertNonEmptyValues(Segment, TBDKey::ThreadLocal, std::move(SymField.TLV));
    insertNonEmptyValues(Segment, TBDKey::Weak, std::move(SymField.Weaks));
    insertNonEmptyValues(Segment, TBDKey::ObjCClass,
                         std::move(SymField.ObjCClasses));
    insertNonEmptyValues(Segment, TBDKey::ObjCEHType,
                         std::move(SymField.EHTypes));
    insertNonEmptyValues(Segment, TBDKey::ObjCIvar, std::move(SymField.IVars));
    insertNonEmptyValues(SymSection, SegmentKey, std::move(Segment));
  };

  Array SymbolSection;
  for (auto &[Targets, Fields] : Entries) {
    Object AllSyms;
    insertNonEmptyValues(AllSyms, TBDKey::Targets, std::move(Targets));
    InsertSymbolsToJSON(AllSyms, TBDKey::Data, Fields.Data);
    InsertSymbolsToJSON(AllSyms, TBDKey::Text, Fields.Text);
    SymbolSection.emplace_back(std::move(AllSyms));
  }

  return SymbolSection;
}

Array serializeFlags(const InterfaceFile *File) {
  // TODO: Give all Targets the same flags for now.
  Array Flags;
  if (!File->isTwoLevelNamespace())
    Flags.emplace_back("flat_namespace");
  if (!File->isApplicationExtensionSafe())
    Flags.emplace_back("not_app_extension_safe");
  if (File->hasSimulatorSupport())
    Flags.emplace_back("sim_support");
  if (File->isOSLibNotForSharedCache())
    Flags.emplace_back("not_for_dyld_shared_cache");
  return serializeScalar(TBDKey::Attributes, std::move(Flags));
}

Expected<Object> serializeIF(const InterfaceFile *File) {
  Object Library;

  // Handle required keys.
  TargetList ActiveTargets{File->targets().begin(), File->targets().end()};
  if (!insertNonEmptyValues(Library, TBDKey::TargetInfo,
                            serializeTargetInfo(ActiveTargets)))
    return make_error<JSONStubError>(getSerializeErrorMsg(TBDKey::TargetInfo));

  Array Name = serializeScalar<StringRef>(TBDKey::Name, File->getInstallName());
  if (!insertNonEmptyValues(Library, TBDKey::InstallName, std::move(Name)))
    return make_error<JSONStubError>(getSerializeErrorMsg(TBDKey::InstallName));

  // Handle optional keys.
  Array Flags = serializeFlags(File);
  insertNonEmptyValues(Library, TBDKey::Flags, std::move(Flags));

  Array CurrentV = serializeScalar<PackedVersion, std::string>(
      TBDKey::Version, File->getCurrentVersion(), PackedVersion(1, 0, 0));
  insertNonEmptyValues(Library, TBDKey::CurrentVersion, std::move(CurrentV));

  Array CompatV = serializeScalar<PackedVersion, std::string>(
      TBDKey::Version, File->getCompatibilityVersion(), PackedVersion(1, 0, 0));
  insertNonEmptyValues(Library, TBDKey::CompatibilityVersion,
                       std::move(CompatV));

  Array SwiftABI = serializeScalar<uint8_t, int64_t>(
      TBDKey::ABI, File->getSwiftABIVersion(), 0u);
  insertNonEmptyValues(Library, TBDKey::SwiftABI, std::move(SwiftABI));

  Array RPaths = serializeField(TBDKey::Paths, File->rpaths(), ActiveTargets);
  insertNonEmptyValues(Library, TBDKey::RPath, std::move(RPaths));

  Array Umbrellas = serializeField(TBDKey::Umbrella, File->umbrellas(),
                                   ActiveTargets, /*IsArray=*/false);
  insertNonEmptyValues(Library, TBDKey::ParentUmbrella, std::move(Umbrellas));

  Array Clients =
      serializeField(TBDKey::Clients, File->allowableClients(), ActiveTargets);
  insertNonEmptyValues(Library, TBDKey::AllowableClients, std::move(Clients));

  Array ReexportLibs =
      serializeField(TBDKey::Names, File->reexportedLibraries(), ActiveTargets);
  insertNonEmptyValues(Library, TBDKey::ReexportLibs, std::move(ReexportLibs));

  // Handle symbols.
  Array Exports = serializeSymbols(File->exports(), ActiveTargets);
  insertNonEmptyValues(Library, TBDKey::Exports, std::move(Exports));

  Array Reexports = serializeSymbols(File->reexports(), ActiveTargets);
  insertNonEmptyValues(Library, TBDKey::Reexports, std::move(Reexports));

  if (!File->isTwoLevelNamespace()) {
    Array Undefineds = serializeSymbols(File->undefineds(), ActiveTargets);
    insertNonEmptyValues(Library, TBDKey::Undefineds, std::move(Undefineds));
  }

  return std::move(Library);
}

Expected<Object> getJSON(const InterfaceFile *File, const FileType FileKind) {
  assert(FileKind == FileType::TBD_V5 && "unexpected json file format version");
  Object Root;

  auto MainLibOrErr = serializeIF(File);
  if (!MainLibOrErr)
    return MainLibOrErr;
  Root[Keys[TBDKey::MainLibrary]] = std::move(*MainLibOrErr);
  Array Documents;
  for (const auto &Doc : File->documents()) {
    auto LibOrErr = serializeIF(Doc.get());
    if (!LibOrErr)
      return LibOrErr;
    Documents.emplace_back(std::move(*LibOrErr));
  }

  Root[Keys[TBDKey::TBDVersion]] = 5;
  insertNonEmptyValues(Root, TBDKey::Documents, std::move(Documents));
  return std::move(Root);
}

} // namespace

Error MachO::serializeInterfaceFileToJSON(raw_ostream &OS,
                                          const InterfaceFile &File,
                                          const FileType FileKind,
                                          bool Compact) {
  auto TextFile = getJSON(&File, FileKind);
  if (!TextFile)
    return TextFile.takeError();
  if (Compact)
    OS << formatv("{0}", Value(std::move(*TextFile))) << "\n";
  else
    OS << formatv("{0:2}", Value(std::move(*TextFile))) << "\n";
  return Error::success();
}
