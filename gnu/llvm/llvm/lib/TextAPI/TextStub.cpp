//===- TextStub.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the text stub file reader/writer.
//
//===----------------------------------------------------------------------===//

#include "TextAPIContext.h"
#include "TextStubCommon.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/PackedVersion.h"
#include "llvm/TextAPI/TextAPIReader.h"
#include "llvm/TextAPI/TextAPIWriter.h"
#include <algorithm>
#include <set>

// clang-format off
/*

 YAML Format specification.

 The TBD v1 format only support two level address libraries and is per
 definition application extension safe.

---                              # the tag !tapi-tbd-v1 is optional and
                                 # shouldn't be emitted to support older linker.
archs: [ armv7, armv7s, arm64 ]  # the list of architecture slices that are
                                 # supported by this file.
platform: ios                    # Specifies the platform (macosx, ios, etc)
install-name: /u/l/libfoo.dylib  #
current-version: 1.2.3           # Optional: defaults to 1.0
compatibility-version: 1.0       # Optional: defaults to 1.0
swift-version: 0                 # Optional: defaults to 0
objc-constraint: none            # Optional: defaults to none
exports:                         # List of export sections
...

Each export section is defined as following:

 - archs: [ arm64 ]                   # the list of architecture slices
   allowed-clients: [ client ]        # Optional: List of clients
   re-exports: [ ]                    # Optional: List of re-exports
   symbols: [ _sym ]                  # Optional: List of symbols
   objc-classes: []                   # Optional: List of Objective-C classes
   objc-ivars: []                     # Optional: List of Objective C Instance
                                      #           Variables
   weak-def-symbols: []               # Optional: List of weak defined symbols
   thread-local-symbols: []           # Optional: List of thread local symbols
*/

/*

 YAML Format specification.

--- !tapi-tbd-v2
archs: [ armv7, armv7s, arm64 ]  # the list of architecture slices that are
                                 # supported by this file.
uuids: [ armv7:... ]             # Optional: List of architecture and UUID pairs.
platform: ios                    # Specifies the platform (macosx, ios, etc)
flags: []                        # Optional:
install-name: /u/l/libfoo.dylib  #
current-version: 1.2.3           # Optional: defaults to 1.0
compatibility-version: 1.0       # Optional: defaults to 1.0
swift-version: 0                 # Optional: defaults to 0
objc-constraint: retain_release  # Optional: defaults to retain_release
parent-umbrella:                 # Optional:
exports:                         # List of export sections
...
undefineds:                      # List of undefineds sections
...

Each export section is defined as following:

- archs: [ arm64 ]                   # the list of architecture slices
  allowed-clients: [ client ]        # Optional: List of clients
  re-exports: [ ]                    # Optional: List of re-exports
  symbols: [ _sym ]                  # Optional: List of symbols
  objc-classes: []                   # Optional: List of Objective-C classes
  objc-ivars: []                     # Optional: List of Objective C Instance
                                     #           Variables
  weak-def-symbols: []               # Optional: List of weak defined symbols
  thread-local-symbols: []           # Optional: List of thread local symbols

Each undefineds section is defined as following:
- archs: [ arm64 ]     # the list of architecture slices
  symbols: [ _sym ]    # Optional: List of symbols
  objc-classes: []     # Optional: List of Objective-C classes
  objc-ivars: []       # Optional: List of Objective C Instance Variables
  weak-ref-symbols: [] # Optional: List of weak defined symbols
*/

/*

 YAML Format specification.

--- !tapi-tbd-v3
archs: [ armv7, armv7s, arm64 ]  # the list of architecture slices that are
                                 # supported by this file.
uuids: [ armv7:... ]             # Optional: List of architecture and UUID pairs.
platform: ios                    # Specifies the platform (macosx, ios, etc)
flags: []                        # Optional:
install-name: /u/l/libfoo.dylib  #
current-version: 1.2.3           # Optional: defaults to 1.0
compatibility-version: 1.0       # Optional: defaults to 1.0
swift-abi-version: 0             # Optional: defaults to 0
objc-constraint: retain_release  # Optional: defaults to retain_release
parent-umbrella:                 # Optional:
exports:                         # List of export sections
...
undefineds:                      # List of undefineds sections
...

Each export section is defined as following:

- archs: [ arm64 ]                   # the list of architecture slices
  allowed-clients: [ client ]        # Optional: List of clients
  re-exports: [ ]                    # Optional: List of re-exports
  symbols: [ _sym ]                  # Optional: List of symbols
  objc-classes: []                   # Optional: List of Objective-C classes
  objc-eh-types: []                  # Optional: List of Objective-C classes
                                     #           with EH
  objc-ivars: []                     # Optional: List of Objective C Instance
                                     #           Variables
  weak-def-symbols: []               # Optional: List of weak defined symbols
  thread-local-symbols: []           # Optional: List of thread local symbols

Each undefineds section is defined as following:
- archs: [ arm64 ]     # the list of architecture slices
  symbols: [ _sym ]    # Optional: List of symbols
  objc-classes: []     # Optional: List of Objective-C classes
  objc-eh-types: []                  # Optional: List of Objective-C classes
                                     #           with EH
  objc-ivars: []       # Optional: List of Objective C Instance Variables
  weak-ref-symbols: [] # Optional: List of weak defined symbols
*/

/*

 YAML Format specification.

--- !tapi-tbd
tbd-version: 4                              # The tbd version for format
targets: [ armv7-ios, x86_64-maccatalyst ]  # The list of applicable tapi supported target triples
uuids:                                      # Optional: List of target and UUID pairs.
  - target: armv7-ios
    value: ...
  - target: x86_64-maccatalyst
    value: ...
flags: []                        # Optional:
install-name: /u/l/libfoo.dylib  #
current-version: 1.2.3           # Optional: defaults to 1.0
compatibility-version: 1.0       # Optional: defaults to 1.0
swift-abi-version: 0             # Optional: defaults to 0
parent-umbrella:                 # Optional:
allowable-clients:
  - targets: [ armv7-ios ]       # Optional:
    clients: [ clientA ]
exports:                         # List of export sections
...
re-exports:                      # List of reexport sections
...
undefineds:                      # List of undefineds sections
...

Each export and reexport  section is defined as following:

- targets: [ arm64-macos ]                        # The list of target triples associated with symbols
  symbols: [ _symA ]                              # Optional: List of symbols
  objc-classes: []                                # Optional: List of Objective-C classes
  objc-eh-types: []                               # Optional: List of Objective-C classes
                                                  #           with EH
  objc-ivars: []                                  # Optional: List of Objective C Instance
                                                  #           Variables
  weak-symbols: []                                # Optional: List of weak defined symbols
  thread-local-symbols: []                        # Optional: List of thread local symbols
- targets: [ arm64-macos, x86_64-maccatalyst ]    # Optional: Targets for applicable additional symbols
  symbols: [ _symB ]                              # Optional: List of symbols

Each undefineds section is defined as following:
- targets: [ arm64-macos ]    # The list of target triples associated with symbols
  symbols: [ _symC ]          # Optional: List of symbols
  objc-classes: []            # Optional: List of Objective-C classes
  objc-eh-types: []           # Optional: List of Objective-C classes
                              #           with EH
  objc-ivars: []              # Optional: List of Objective C Instance Variables
  weak-symbols: []            # Optional: List of weak defined symbols
*/
// clang-format on

using namespace llvm;
using namespace llvm::yaml;
using namespace llvm::MachO;

namespace {
struct ExportSection {
  std::vector<Architecture> Architectures;
  std::vector<FlowStringRef> AllowableClients;
  std::vector<FlowStringRef> ReexportedLibraries;
  std::vector<FlowStringRef> Symbols;
  std::vector<FlowStringRef> Classes;
  std::vector<FlowStringRef> ClassEHs;
  std::vector<FlowStringRef> IVars;
  std::vector<FlowStringRef> WeakDefSymbols;
  std::vector<FlowStringRef> TLVSymbols;
};

struct UndefinedSection {
  std::vector<Architecture> Architectures;
  std::vector<FlowStringRef> Symbols;
  std::vector<FlowStringRef> Classes;
  std::vector<FlowStringRef> ClassEHs;
  std::vector<FlowStringRef> IVars;
  std::vector<FlowStringRef> WeakRefSymbols;
};

// Sections for direct target mapping in TBDv4
struct SymbolSection {
  TargetList Targets;
  std::vector<FlowStringRef> Symbols;
  std::vector<FlowStringRef> Classes;
  std::vector<FlowStringRef> ClassEHs;
  std::vector<FlowStringRef> Ivars;
  std::vector<FlowStringRef> WeakSymbols;
  std::vector<FlowStringRef> TlvSymbols;
};

struct MetadataSection {
  enum Option { Clients, Libraries };
  std::vector<Target> Targets;
  std::vector<FlowStringRef> Values;
};

struct UmbrellaSection {
  std::vector<Target> Targets;
  std::string Umbrella;
};

// UUID's for TBDv4 are mapped to target not arch
struct UUIDv4 {
  Target TargetID;
  std::string Value;

  UUIDv4() = default;
  UUIDv4(const Target &TargetID, const std::string &Value)
      : TargetID(TargetID), Value(Value) {}
};
} // end anonymous namespace.

LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(Architecture)
LLVM_YAML_IS_SEQUENCE_VECTOR(ExportSection)
LLVM_YAML_IS_SEQUENCE_VECTOR(UndefinedSection)
// Specific to TBDv4
LLVM_YAML_IS_SEQUENCE_VECTOR(SymbolSection)
LLVM_YAML_IS_SEQUENCE_VECTOR(MetadataSection)
LLVM_YAML_IS_SEQUENCE_VECTOR(UmbrellaSection)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(Target)
LLVM_YAML_IS_SEQUENCE_VECTOR(UUIDv4)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<ExportSection> {
  static void mapping(IO &IO, ExportSection &Section) {
    const auto *Ctx = reinterpret_cast<TextAPIContext *>(IO.getContext());
    assert((!Ctx || Ctx->FileKind != FileType::Invalid) &&
           "File type is not set in YAML context");

    IO.mapRequired("archs", Section.Architectures);
    if (Ctx->FileKind == FileType::TBD_V1)
      IO.mapOptional("allowed-clients", Section.AllowableClients);
    else
      IO.mapOptional("allowable-clients", Section.AllowableClients);
    IO.mapOptional("re-exports", Section.ReexportedLibraries);
    IO.mapOptional("symbols", Section.Symbols);
    IO.mapOptional("objc-classes", Section.Classes);
    if (Ctx->FileKind == FileType::TBD_V3)
      IO.mapOptional("objc-eh-types", Section.ClassEHs);
    IO.mapOptional("objc-ivars", Section.IVars);
    IO.mapOptional("weak-def-symbols", Section.WeakDefSymbols);
    IO.mapOptional("thread-local-symbols", Section.TLVSymbols);
  }
};

template <> struct MappingTraits<UndefinedSection> {
  static void mapping(IO &IO, UndefinedSection &Section) {
    const auto *Ctx = reinterpret_cast<TextAPIContext *>(IO.getContext());
    assert((!Ctx || Ctx->FileKind != FileType::Invalid) &&
           "File type is not set in YAML context");

    IO.mapRequired("archs", Section.Architectures);
    IO.mapOptional("symbols", Section.Symbols);
    IO.mapOptional("objc-classes", Section.Classes);
    if (Ctx->FileKind == FileType::TBD_V3)
      IO.mapOptional("objc-eh-types", Section.ClassEHs);
    IO.mapOptional("objc-ivars", Section.IVars);
    IO.mapOptional("weak-ref-symbols", Section.WeakRefSymbols);
  }
};

template <> struct MappingTraits<SymbolSection> {
  static void mapping(IO &IO, SymbolSection &Section) {
    IO.mapRequired("targets", Section.Targets);
    IO.mapOptional("symbols", Section.Symbols);
    IO.mapOptional("objc-classes", Section.Classes);
    IO.mapOptional("objc-eh-types", Section.ClassEHs);
    IO.mapOptional("objc-ivars", Section.Ivars);
    IO.mapOptional("weak-symbols", Section.WeakSymbols);
    IO.mapOptional("thread-local-symbols", Section.TlvSymbols);
  }
};

template <> struct MappingTraits<UmbrellaSection> {
  static void mapping(IO &IO, UmbrellaSection &Section) {
    IO.mapRequired("targets", Section.Targets);
    IO.mapRequired("umbrella", Section.Umbrella);
  }
};

template <> struct MappingTraits<UUIDv4> {
  static void mapping(IO &IO, UUIDv4 &UUID) {
    IO.mapRequired("target", UUID.TargetID);
    IO.mapRequired("value", UUID.Value);
  }
};

template <>
struct MappingContextTraits<MetadataSection, MetadataSection::Option> {
  static void mapping(IO &IO, MetadataSection &Section,
                      MetadataSection::Option &OptionKind) {
    IO.mapRequired("targets", Section.Targets);
    switch (OptionKind) {
    case MetadataSection::Option::Clients:
      IO.mapRequired("clients", Section.Values);
      return;
    case MetadataSection::Option::Libraries:
      IO.mapRequired("libraries", Section.Values);
      return;
    }
    llvm_unreachable("unexpected option for metadata");
  }
};

template <> struct ScalarBitSetTraits<TBDFlags> {
  static void bitset(IO &IO, TBDFlags &Flags) {
    IO.bitSetCase(Flags, "flat_namespace", TBDFlags::FlatNamespace);
    IO.bitSetCase(Flags, "not_app_extension_safe",
                  TBDFlags::NotApplicationExtensionSafe);
    IO.bitSetCase(Flags, "installapi", TBDFlags::InstallAPI);
    IO.bitSetCase(Flags, "not_for_dyld_shared_cache",
                  TBDFlags::OSLibNotForSharedCache);
  }
};

template <> struct ScalarTraits<Target> {
  static void output(const Target &Value, void *, raw_ostream &OS) {
    OS << Value.Arch << "-";
    switch (Value.Platform) {
#define PLATFORM(platform, id, name, build_name, target, tapi_target,          \
                 marketing)                                                    \
  case PLATFORM_##platform:                                                    \
    OS << #tapi_target;                                                        \
    break;
#include "llvm/BinaryFormat/MachO.def"
    }
  }

  static StringRef input(StringRef Scalar, void *, Target &Value) {
    auto Result = Target::create(Scalar);
    if (!Result) {
      consumeError(Result.takeError());
      return "unparsable target";
    }

    Value = *Result;
    if (Value.Arch == AK_unknown)
      return "unknown architecture";
    if (Value.Platform == PLATFORM_UNKNOWN)
      return "unknown platform";

    return {};
  }

  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template <> struct MappingTraits<const InterfaceFile *> {
  struct NormalizedTBD {
    explicit NormalizedTBD(IO &IO) {}
    NormalizedTBD(IO &IO, const InterfaceFile *&File) {
      Architectures = File->getArchitectures();
      Platforms = File->getPlatforms();
      InstallName = File->getInstallName();
      CurrentVersion = PackedVersion(File->getCurrentVersion());
      CompatibilityVersion = PackedVersion(File->getCompatibilityVersion());
      SwiftABIVersion = File->getSwiftABIVersion();
      ObjCConstraint = File->getObjCConstraint();

      Flags = TBDFlags::None;
      if (!File->isApplicationExtensionSafe())
        Flags |= TBDFlags::NotApplicationExtensionSafe;

      if (!File->isTwoLevelNamespace())
        Flags |= TBDFlags::FlatNamespace;

      if (!File->umbrellas().empty())
        ParentUmbrella = File->umbrellas().begin()->second;

      std::set<ArchitectureSet> ArchSet;
      for (const auto &Library : File->allowableClients())
        ArchSet.insert(Library.getArchitectures());

      for (const auto &Library : File->reexportedLibraries())
        ArchSet.insert(Library.getArchitectures());

      std::map<const Symbol *, ArchitectureSet> SymbolToArchSet;
      for (const auto *Symbol : File->symbols()) {
        auto Architectures = Symbol->getArchitectures();
        SymbolToArchSet[Symbol] = Architectures;
        ArchSet.insert(Architectures);
      }

      for (auto Architectures : ArchSet) {
        ExportSection Section;
        Section.Architectures = Architectures;

        for (const auto &Library : File->allowableClients())
          if (Library.getArchitectures() == Architectures)
            Section.AllowableClients.emplace_back(Library.getInstallName());

        for (const auto &Library : File->reexportedLibraries())
          if (Library.getArchitectures() == Architectures)
            Section.ReexportedLibraries.emplace_back(Library.getInstallName());

        for (const auto &SymArch : SymbolToArchSet) {
          if (SymArch.second != Architectures)
            continue;

          const auto *Symbol = SymArch.first;
          switch (Symbol->getKind()) {
          case EncodeKind::GlobalSymbol:
            if (Symbol->isWeakDefined())
              Section.WeakDefSymbols.emplace_back(Symbol->getName());
            else if (Symbol->isThreadLocalValue())
              Section.TLVSymbols.emplace_back(Symbol->getName());
            else
              Section.Symbols.emplace_back(Symbol->getName());
            break;
          case EncodeKind::ObjectiveCClass:
            if (File->getFileType() != FileType::TBD_V3)
              Section.Classes.emplace_back(
                  copyString("_" + Symbol->getName().str()));
            else
              Section.Classes.emplace_back(Symbol->getName());
            break;
          case EncodeKind::ObjectiveCClassEHType:
            if (File->getFileType() != FileType::TBD_V3)
              Section.Symbols.emplace_back(
                  copyString("_OBJC_EHTYPE_$_" + Symbol->getName().str()));
            else
              Section.ClassEHs.emplace_back(Symbol->getName());
            break;
          case EncodeKind::ObjectiveCInstanceVariable:
            if (File->getFileType() != FileType::TBD_V3)
              Section.IVars.emplace_back(
                  copyString("_" + Symbol->getName().str()));
            else
              Section.IVars.emplace_back(Symbol->getName());
            break;
          }
        }
        llvm::sort(Section.Symbols);
        llvm::sort(Section.Classes);
        llvm::sort(Section.ClassEHs);
        llvm::sort(Section.IVars);
        llvm::sort(Section.WeakDefSymbols);
        llvm::sort(Section.TLVSymbols);
        Exports.emplace_back(std::move(Section));
      }

      ArchSet.clear();
      SymbolToArchSet.clear();

      for (const auto *Symbol : File->undefineds()) {
        auto Architectures = Symbol->getArchitectures();
        SymbolToArchSet[Symbol] = Architectures;
        ArchSet.insert(Architectures);
      }

      for (auto Architectures : ArchSet) {
        UndefinedSection Section;
        Section.Architectures = Architectures;

        for (const auto &SymArch : SymbolToArchSet) {
          if (SymArch.second != Architectures)
            continue;

          const auto *Symbol = SymArch.first;
          switch (Symbol->getKind()) {
          case EncodeKind::GlobalSymbol:
            if (Symbol->isWeakReferenced())
              Section.WeakRefSymbols.emplace_back(Symbol->getName());
            else
              Section.Symbols.emplace_back(Symbol->getName());
            break;
          case EncodeKind::ObjectiveCClass:
            if (File->getFileType() != FileType::TBD_V3)
              Section.Classes.emplace_back(
                  copyString("_" + Symbol->getName().str()));
            else
              Section.Classes.emplace_back(Symbol->getName());
            break;
          case EncodeKind::ObjectiveCClassEHType:
            if (File->getFileType() != FileType::TBD_V3)
              Section.Symbols.emplace_back(
                  copyString("_OBJC_EHTYPE_$_" + Symbol->getName().str()));
            else
              Section.ClassEHs.emplace_back(Symbol->getName());
            break;
          case EncodeKind::ObjectiveCInstanceVariable:
            if (File->getFileType() != FileType::TBD_V3)
              Section.IVars.emplace_back(
                  copyString("_" + Symbol->getName().str()));
            else
              Section.IVars.emplace_back(Symbol->getName());
            break;
          }
        }
        llvm::sort(Section.Symbols);
        llvm::sort(Section.Classes);
        llvm::sort(Section.ClassEHs);
        llvm::sort(Section.IVars);
        llvm::sort(Section.WeakRefSymbols);
        Undefineds.emplace_back(std::move(Section));
      }
    }

    // TBD v1 - TBD v3 files only support one platform and several
    // architectures. It is possible to have more than one platform for TBD v3
    // files, but the architectures don't apply to all
    // platforms, specifically to filter out the i386 slice from
    // platform macCatalyst.
    TargetList synthesizeTargets(ArchitectureSet Architectures,
                                 const PlatformSet &Platforms) {
      TargetList Targets;

      for (auto Platform : Platforms) {
        Platform = mapToPlatformType(Platform, Architectures.hasX86());

        for (const auto &&Architecture : Architectures) {
          if ((Architecture == AK_i386) && (Platform == PLATFORM_MACCATALYST))
            continue;

          Targets.emplace_back(Architecture, Platform);
        }
      }
      return Targets;
    }

    const InterfaceFile *denormalize(IO &IO) {
      auto Ctx = reinterpret_cast<TextAPIContext *>(IO.getContext());
      assert(Ctx);

      auto *File = new InterfaceFile;
      File->setPath(Ctx->Path);
      File->setFileType(Ctx->FileKind);
      File->addTargets(synthesizeTargets(Architectures, Platforms));
      File->setInstallName(InstallName);
      File->setCurrentVersion(CurrentVersion);
      File->setCompatibilityVersion(CompatibilityVersion);
      File->setSwiftABIVersion(SwiftABIVersion);
      File->setObjCConstraint(ObjCConstraint);
      for (const auto &Target : File->targets())
        File->addParentUmbrella(Target, ParentUmbrella);

      if (Ctx->FileKind == FileType::TBD_V1) {
        File->setTwoLevelNamespace();
        File->setApplicationExtensionSafe();
      } else {
        File->setTwoLevelNamespace(!(Flags & TBDFlags::FlatNamespace));
        File->setApplicationExtensionSafe(
            !(Flags & TBDFlags::NotApplicationExtensionSafe));
      }

      // For older file formats, the segment where the symbol
      // comes from is unknown, treat all symbols as Data
      // in these cases.
      const auto Flags = SymbolFlags::Data;

      for (const auto &Section : Exports) {
        const auto Targets =
            synthesizeTargets(Section.Architectures, Platforms);

        for (const auto &Lib : Section.AllowableClients)
          for (const auto &Target : Targets)
            File->addAllowableClient(Lib, Target);

        for (const auto &Lib : Section.ReexportedLibraries)
          for (const auto &Target : Targets)
            File->addReexportedLibrary(Lib, Target);

        for (const auto &Symbol : Section.Symbols) {
          if (Ctx->FileKind != FileType::TBD_V3 &&
              Symbol.value.starts_with(ObjC2EHTypePrefix))
            File->addSymbol(EncodeKind::ObjectiveCClassEHType,
                            Symbol.value.drop_front(15), Targets, Flags);
          else
            File->addSymbol(EncodeKind::GlobalSymbol, Symbol, Targets, Flags);
        }
        for (auto &Symbol : Section.Classes) {
          auto Name = Symbol.value;
          if (Ctx->FileKind != FileType::TBD_V3)
            Name = Name.drop_front();
          File->addSymbol(EncodeKind::ObjectiveCClass, Name, Targets, Flags);
        }
        for (auto &Symbol : Section.ClassEHs)
          File->addSymbol(EncodeKind::ObjectiveCClassEHType, Symbol, Targets,
                          Flags);
        for (auto &Symbol : Section.IVars) {
          auto Name = Symbol.value;
          if (Ctx->FileKind != FileType::TBD_V3)
            Name = Name.drop_front();
          File->addSymbol(EncodeKind::ObjectiveCInstanceVariable, Name, Targets,
                          Flags);
        }
        for (auto &Symbol : Section.WeakDefSymbols)
          File->addSymbol(EncodeKind::GlobalSymbol, Symbol, Targets,
                          SymbolFlags::WeakDefined | Flags);
        for (auto &Symbol : Section.TLVSymbols)
          File->addSymbol(EncodeKind::GlobalSymbol, Symbol, Targets,
                          SymbolFlags::ThreadLocalValue | Flags);
      }

      for (const auto &Section : Undefineds) {
        const auto Targets =
            synthesizeTargets(Section.Architectures, Platforms);
        for (auto &Symbol : Section.Symbols) {
          if (Ctx->FileKind != FileType::TBD_V3 &&
              Symbol.value.starts_with(ObjC2EHTypePrefix))
            File->addSymbol(EncodeKind::ObjectiveCClassEHType,
                            Symbol.value.drop_front(15), Targets,
                            SymbolFlags::Undefined | Flags);
          else
            File->addSymbol(EncodeKind::GlobalSymbol, Symbol, Targets,
                            SymbolFlags::Undefined | Flags);
        }
        for (auto &Symbol : Section.Classes) {
          auto Name = Symbol.value;
          if (Ctx->FileKind != FileType::TBD_V3)
            Name = Name.drop_front();
          File->addSymbol(EncodeKind::ObjectiveCClass, Name, Targets,
                          SymbolFlags::Undefined | Flags);
        }
        for (auto &Symbol : Section.ClassEHs)
          File->addSymbol(EncodeKind::ObjectiveCClassEHType, Symbol, Targets,
                          SymbolFlags::Undefined | Flags);
        for (auto &Symbol : Section.IVars) {
          auto Name = Symbol.value;
          if (Ctx->FileKind != FileType::TBD_V3)
            Name = Name.drop_front();
          File->addSymbol(EncodeKind::ObjectiveCInstanceVariable, Name, Targets,
                          SymbolFlags::Undefined | Flags);
        }
        for (auto &Symbol : Section.WeakRefSymbols)
          File->addSymbol(EncodeKind::GlobalSymbol, Symbol, Targets,
                          SymbolFlags::Undefined | SymbolFlags::WeakReferenced |
                              Flags);
      }

      return File;
    }

    llvm::BumpPtrAllocator Allocator;
    StringRef copyString(StringRef String) {
      if (String.empty())
        return {};

      void *Ptr = Allocator.Allocate(String.size(), 1);
      memcpy(Ptr, String.data(), String.size());
      return StringRef(reinterpret_cast<const char *>(Ptr), String.size());
    }

    std::vector<Architecture> Architectures;
    std::vector<UUID> UUIDs;
    PlatformSet Platforms;
    StringRef InstallName;
    PackedVersion CurrentVersion;
    PackedVersion CompatibilityVersion;
    SwiftVersion SwiftABIVersion{0};
    ObjCConstraintType ObjCConstraint{ObjCConstraintType::None};
    TBDFlags Flags{TBDFlags::None};
    StringRef ParentUmbrella;
    std::vector<ExportSection> Exports;
    std::vector<UndefinedSection> Undefineds;
  };

  static void setFileTypeForInput(TextAPIContext *Ctx, IO &IO) {
    if (IO.mapTag("!tapi-tbd", false))
      Ctx->FileKind = FileType::TBD_V4;
    else if (IO.mapTag("!tapi-tbd-v3", false))
      Ctx->FileKind = FileType::TBD_V3;
    else if (IO.mapTag("!tapi-tbd-v2", false))
      Ctx->FileKind = FileType::TBD_V2;
    else if (IO.mapTag("!tapi-tbd-v1", false) ||
             IO.mapTag("tag:yaml.org,2002:map", false))
      Ctx->FileKind = FileType::TBD_V1;
    else {
      Ctx->FileKind = FileType::Invalid;
      return;
    }
  }

  static void mapping(IO &IO, const InterfaceFile *&File) {
    auto *Ctx = reinterpret_cast<TextAPIContext *>(IO.getContext());
    assert((!Ctx || !IO.outputting() ||
            (Ctx && Ctx->FileKind != FileType::Invalid)) &&
           "File type is not set in YAML context");

    if (!IO.outputting()) {
      setFileTypeForInput(Ctx, IO);
      switch (Ctx->FileKind) {
      default:
        break;
      case FileType::TBD_V4:
        mapKeysToValuesV4(IO, File);
        return;
      case FileType::Invalid:
        IO.setError("unsupported file type");
        return;
      }
    } else {
      // Set file type when writing.
      switch (Ctx->FileKind) {
      default:
        llvm_unreachable("unexpected file type");
      case FileType::TBD_V4:
        mapKeysToValuesV4(IO, File);
        return;
      case FileType::TBD_V3:
        IO.mapTag("!tapi-tbd-v3", true);
        break;
      case FileType::TBD_V2:
        IO.mapTag("!tapi-tbd-v2", true);
        break;
      case FileType::TBD_V1:
        // Don't write the tag into the .tbd file for TBD v1
        break;
      }
    }
    mapKeysToValues(Ctx->FileKind, IO, File);
  }

  using SectionList = std::vector<SymbolSection>;
  struct NormalizedTBD_V4 {
    explicit NormalizedTBD_V4(IO &IO) {}
    NormalizedTBD_V4(IO &IO, const InterfaceFile *&File) {
      auto Ctx = reinterpret_cast<TextAPIContext *>(IO.getContext());
      assert(Ctx);
      TBDVersion = Ctx->FileKind >> 4;
      Targets.insert(Targets.begin(), File->targets().begin(),
                     File->targets().end());
      InstallName = File->getInstallName();
      CurrentVersion = File->getCurrentVersion();
      CompatibilityVersion = File->getCompatibilityVersion();
      SwiftABIVersion = File->getSwiftABIVersion();

      Flags = TBDFlags::None;
      if (!File->isApplicationExtensionSafe())
        Flags |= TBDFlags::NotApplicationExtensionSafe;

      if (!File->isTwoLevelNamespace())
        Flags |= TBDFlags::FlatNamespace;

      if (File->isOSLibNotForSharedCache())
        Flags |= TBDFlags::OSLibNotForSharedCache;

      {
        std::map<std::string, TargetList> valueToTargetList;
        for (const auto &it : File->umbrellas())
          valueToTargetList[it.second].emplace_back(it.first);

        for (const auto &it : valueToTargetList) {
          UmbrellaSection CurrentSection;
          CurrentSection.Targets.insert(CurrentSection.Targets.begin(),
                                        it.second.begin(), it.second.end());
          CurrentSection.Umbrella = it.first;
          ParentUmbrellas.emplace_back(std::move(CurrentSection));
        }
      }

      assignTargetsToLibrary(File->allowableClients(), AllowableClients);
      assignTargetsToLibrary(File->reexportedLibraries(), ReexportedLibraries);

      auto handleSymbols =
          [](SectionList &CurrentSections,
             InterfaceFile::const_filtered_symbol_range Symbols) {
            std::set<TargetList> TargetSet;
            std::map<const Symbol *, TargetList> SymbolToTargetList;
            for (const auto *Symbol : Symbols) {
              TargetList Targets(Symbol->targets());
              SymbolToTargetList[Symbol] = Targets;
              TargetSet.emplace(std::move(Targets));
            }
            for (const auto &TargetIDs : TargetSet) {
              SymbolSection CurrentSection;
              CurrentSection.Targets.insert(CurrentSection.Targets.begin(),
                                            TargetIDs.begin(), TargetIDs.end());

              for (const auto &IT : SymbolToTargetList) {
                if (IT.second != TargetIDs)
                  continue;

                const auto *Symbol = IT.first;
                switch (Symbol->getKind()) {
                case EncodeKind::GlobalSymbol:
                  if (Symbol->isWeakDefined())
                    CurrentSection.WeakSymbols.emplace_back(Symbol->getName());
                  else if (Symbol->isThreadLocalValue())
                    CurrentSection.TlvSymbols.emplace_back(Symbol->getName());
                  else
                    CurrentSection.Symbols.emplace_back(Symbol->getName());
                  break;
                case EncodeKind::ObjectiveCClass:
                  CurrentSection.Classes.emplace_back(Symbol->getName());
                  break;
                case EncodeKind::ObjectiveCClassEHType:
                  CurrentSection.ClassEHs.emplace_back(Symbol->getName());
                  break;
                case EncodeKind::ObjectiveCInstanceVariable:
                  CurrentSection.Ivars.emplace_back(Symbol->getName());
                  break;
                }
              }
              sort(CurrentSection.Symbols);
              sort(CurrentSection.Classes);
              sort(CurrentSection.ClassEHs);
              sort(CurrentSection.Ivars);
              sort(CurrentSection.WeakSymbols);
              sort(CurrentSection.TlvSymbols);
              CurrentSections.emplace_back(std::move(CurrentSection));
            }
          };

      handleSymbols(Exports, File->exports());
      handleSymbols(Reexports, File->reexports());
      handleSymbols(Undefineds, File->undefineds());
    }

    const InterfaceFile *denormalize(IO &IO) {
      auto Ctx = reinterpret_cast<TextAPIContext *>(IO.getContext());
      assert(Ctx);

      auto *File = new InterfaceFile;
      File->setPath(Ctx->Path);
      File->setFileType(Ctx->FileKind);
      File->addTargets(Targets);
      File->setInstallName(InstallName);
      File->setCurrentVersion(CurrentVersion);
      File->setCompatibilityVersion(CompatibilityVersion);
      File->setSwiftABIVersion(SwiftABIVersion);
      for (const auto &CurrentSection : ParentUmbrellas)
        for (const auto &target : CurrentSection.Targets)
          File->addParentUmbrella(target, CurrentSection.Umbrella);
      File->setTwoLevelNamespace(!(Flags & TBDFlags::FlatNamespace));
      File->setApplicationExtensionSafe(
          !(Flags & TBDFlags::NotApplicationExtensionSafe));
      File->setOSLibNotForSharedCache(
          (Flags & TBDFlags::OSLibNotForSharedCache));

      for (const auto &CurrentSection : AllowableClients) {
        for (const auto &lib : CurrentSection.Values)
          for (const auto &Target : CurrentSection.Targets)
            File->addAllowableClient(lib, Target);
      }

      for (const auto &CurrentSection : ReexportedLibraries) {
        for (const auto &Lib : CurrentSection.Values)
          for (const auto &Target : CurrentSection.Targets)
            File->addReexportedLibrary(Lib, Target);
      }

      auto handleSymbols = [File](const SectionList &CurrentSections,
                                  SymbolFlags InputFlag = SymbolFlags::None) {
        // For older file formats, the segment where the symbol
        // comes from is unknown, treat all symbols as Data
        // in these cases.
        const SymbolFlags Flag = InputFlag | SymbolFlags::Data;

        for (const auto &CurrentSection : CurrentSections) {
          for (auto &sym : CurrentSection.Symbols)
            File->addSymbol(EncodeKind::GlobalSymbol, sym,
                            CurrentSection.Targets, Flag);

          for (auto &sym : CurrentSection.Classes)
            File->addSymbol(EncodeKind::ObjectiveCClass, sym,
                            CurrentSection.Targets, Flag);

          for (auto &sym : CurrentSection.ClassEHs)
            File->addSymbol(EncodeKind::ObjectiveCClassEHType, sym,
                            CurrentSection.Targets, Flag);

          for (auto &sym : CurrentSection.Ivars)
            File->addSymbol(EncodeKind::ObjectiveCInstanceVariable, sym,
                            CurrentSection.Targets, Flag);

          SymbolFlags SymFlag =
              ((Flag & SymbolFlags::Undefined) == SymbolFlags::Undefined)
                  ? SymbolFlags::WeakReferenced
                  : SymbolFlags::WeakDefined;
          for (auto &sym : CurrentSection.WeakSymbols) {
            File->addSymbol(EncodeKind::GlobalSymbol, sym,
                            CurrentSection.Targets, Flag | SymFlag);
          }

          for (auto &sym : CurrentSection.TlvSymbols)
            File->addSymbol(EncodeKind::GlobalSymbol, sym,
                            CurrentSection.Targets,
                            Flag | SymbolFlags::ThreadLocalValue);
        }
      };

      handleSymbols(Exports);
      handleSymbols(Reexports, SymbolFlags::Rexported);
      handleSymbols(Undefineds, SymbolFlags::Undefined);

      return File;
    }

    unsigned TBDVersion;
    std::vector<UUIDv4> UUIDs;
    TargetList Targets;
    StringRef InstallName;
    PackedVersion CurrentVersion;
    PackedVersion CompatibilityVersion;
    SwiftVersion SwiftABIVersion{0};
    std::vector<MetadataSection> AllowableClients;
    std::vector<MetadataSection> ReexportedLibraries;
    TBDFlags Flags{TBDFlags::None};
    std::vector<UmbrellaSection> ParentUmbrellas;
    SectionList Exports;
    SectionList Reexports;
    SectionList Undefineds;

  private:
    void assignTargetsToLibrary(const std::vector<InterfaceFileRef> &Libraries,
                                std::vector<MetadataSection> &Section) {
      std::set<TargetList> targetSet;
      std::map<const InterfaceFileRef *, TargetList> valueToTargetList;
      for (const auto &library : Libraries) {
        TargetList targets(library.targets());
        valueToTargetList[&library] = targets;
        targetSet.emplace(std::move(targets));
      }

      for (const auto &targets : targetSet) {
        MetadataSection CurrentSection;
        CurrentSection.Targets.insert(CurrentSection.Targets.begin(),
                                      targets.begin(), targets.end());

        for (const auto &it : valueToTargetList) {
          if (it.second != targets)
            continue;

          CurrentSection.Values.emplace_back(it.first->getInstallName());
        }
        llvm::sort(CurrentSection.Values);
        Section.emplace_back(std::move(CurrentSection));
      }
    }
  };

  static void mapKeysToValues(FileType FileKind, IO &IO,
                              const InterfaceFile *&File) {
    MappingNormalization<NormalizedTBD, const InterfaceFile *> Keys(IO, File);
    std::vector<UUID> EmptyUUID;
    IO.mapRequired("archs", Keys->Architectures);
    if (FileKind != FileType::TBD_V1)
      IO.mapOptional("uuids", EmptyUUID);
    IO.mapRequired("platform", Keys->Platforms);
    if (FileKind != FileType::TBD_V1)
      IO.mapOptional("flags", Keys->Flags, TBDFlags::None);
    IO.mapRequired("install-name", Keys->InstallName);
    IO.mapOptional("current-version", Keys->CurrentVersion,
                   PackedVersion(1, 0, 0));
    IO.mapOptional("compatibility-version", Keys->CompatibilityVersion,
                   PackedVersion(1, 0, 0));
    if (FileKind != FileType::TBD_V3)
      IO.mapOptional("swift-version", Keys->SwiftABIVersion, SwiftVersion(0));
    else
      IO.mapOptional("swift-abi-version", Keys->SwiftABIVersion,
                     SwiftVersion(0));
    IO.mapOptional("objc-constraint", Keys->ObjCConstraint,
                   (FileKind == FileType::TBD_V1)
                       ? ObjCConstraintType::None
                       : ObjCConstraintType::Retain_Release);
    if (FileKind != FileType::TBD_V1)
      IO.mapOptional("parent-umbrella", Keys->ParentUmbrella, StringRef());
    IO.mapOptional("exports", Keys->Exports);
    if (FileKind != FileType::TBD_V1)
      IO.mapOptional("undefineds", Keys->Undefineds);
  }

  static void mapKeysToValuesV4(IO &IO, const InterfaceFile *&File) {
    MappingNormalization<NormalizedTBD_V4, const InterfaceFile *> Keys(IO,
                                                                       File);
    std::vector<UUIDv4> EmptyUUID;
    IO.mapTag("!tapi-tbd", true);
    IO.mapRequired("tbd-version", Keys->TBDVersion);
    IO.mapRequired("targets", Keys->Targets);
    IO.mapOptional("uuids", EmptyUUID);
    IO.mapOptional("flags", Keys->Flags, TBDFlags::None);
    IO.mapRequired("install-name", Keys->InstallName);
    IO.mapOptional("current-version", Keys->CurrentVersion,
                   PackedVersion(1, 0, 0));
    IO.mapOptional("compatibility-version", Keys->CompatibilityVersion,
                   PackedVersion(1, 0, 0));
    IO.mapOptional("swift-abi-version", Keys->SwiftABIVersion, SwiftVersion(0));
    IO.mapOptional("parent-umbrella", Keys->ParentUmbrellas);
    auto OptionKind = MetadataSection::Option::Clients;
    IO.mapOptionalWithContext("allowable-clients", Keys->AllowableClients,
                              OptionKind);
    OptionKind = MetadataSection::Option::Libraries;
    IO.mapOptionalWithContext("reexported-libraries", Keys->ReexportedLibraries,
                              OptionKind);
    IO.mapOptional("exports", Keys->Exports);
    IO.mapOptional("reexports", Keys->Reexports);
    IO.mapOptional("undefineds", Keys->Undefineds);
  }
};

template <>
struct DocumentListTraits<std::vector<const MachO::InterfaceFile *>> {
  static size_t size(IO &IO, std::vector<const MachO::InterfaceFile *> &Seq) {
    return Seq.size();
  }
  static const InterfaceFile *&
  element(IO &IO, std::vector<const InterfaceFile *> &Seq, size_t Index) {
    if (Index >= Seq.size())
      Seq.resize(Index + 1);
    return Seq[Index];
  }
};

} // end namespace yaml.
} // namespace llvm

static void DiagHandler(const SMDiagnostic &Diag, void *Context) {
  auto *File = static_cast<TextAPIContext *>(Context);
  SmallString<1024> Message;
  raw_svector_ostream S(Message);

  SMDiagnostic NewDiag(*Diag.getSourceMgr(), Diag.getLoc(), File->Path,
                       Diag.getLineNo(), Diag.getColumnNo(), Diag.getKind(),
                       Diag.getMessage(), Diag.getLineContents(),
                       Diag.getRanges(), Diag.getFixIts());

  NewDiag.print(nullptr, S);
  File->ErrorMessage = ("malformed file\n" + Message).str();
}

Expected<FileType> TextAPIReader::canRead(MemoryBufferRef InputBuffer) {
  auto TAPIFile = InputBuffer.getBuffer().trim();
  if (TAPIFile.starts_with("{") && TAPIFile.ends_with("}"))
    return FileType::TBD_V5;

  if (!TAPIFile.ends_with("..."))
    return createStringError(std::errc::not_supported, "unsupported file type");

  if (TAPIFile.starts_with("--- !tapi-tbd"))
    return FileType::TBD_V4;

  if (TAPIFile.starts_with("--- !tapi-tbd-v3"))
    return FileType::TBD_V3;

  if (TAPIFile.starts_with("--- !tapi-tbd-v2"))
    return FileType::TBD_V2;

  if (TAPIFile.starts_with("--- !tapi-tbd-v1") ||
      TAPIFile.starts_with("---\narchs:"))
    return FileType::TBD_V1;

  return createStringError(std::errc::not_supported, "unsupported file type");
}

Expected<std::unique_ptr<InterfaceFile>>
TextAPIReader::get(MemoryBufferRef InputBuffer) {
  TextAPIContext Ctx;
  Ctx.Path = std::string(InputBuffer.getBufferIdentifier());
  if (auto FTOrErr = canRead(InputBuffer))
    Ctx.FileKind = *FTOrErr;
  else
    return FTOrErr.takeError();

  // Handle JSON Format.
  if (Ctx.FileKind >= FileType::TBD_V5) {
    auto FileOrErr = getInterfaceFileFromJSON(InputBuffer.getBuffer());
    if (!FileOrErr)
      return FileOrErr.takeError();

    (*FileOrErr)->setPath(Ctx.Path);
    return std::move(*FileOrErr);
  }
  yaml::Input YAMLIn(InputBuffer.getBuffer(), &Ctx, DiagHandler, &Ctx);

  // Fill vector with interface file objects created by parsing the YAML file.
  std::vector<const InterfaceFile *> Files;
  YAMLIn >> Files;

  // YAMLIn dynamically allocates for Interface file and in case of error,
  // memory leak will occur unless wrapped around unique_ptr
  auto File = std::unique_ptr<InterfaceFile>(
      const_cast<InterfaceFile *>(Files.front()));

  for (const InterfaceFile *FI : llvm::drop_begin(Files))
    File->addDocument(
        std::shared_ptr<InterfaceFile>(const_cast<InterfaceFile *>(FI)));

  if (YAMLIn.error())
    return make_error<StringError>(Ctx.ErrorMessage, YAMLIn.error());

  return std::move(File);
}

Error TextAPIWriter::writeToStream(raw_ostream &OS, const InterfaceFile &File,
                                   const FileType FileKind, bool Compact) {
  TextAPIContext Ctx;
  Ctx.Path = std::string(File.getPath());

  // Prefer parameter for format if passed, otherwise fallback to the File
  // FileType.
  Ctx.FileKind =
      (FileKind == FileType::Invalid) ? File.getFileType() : FileKind;

  // Write out in JSON format.
  if (Ctx.FileKind >= FileType::TBD_V5) {
    return serializeInterfaceFileToJSON(OS, File, Ctx.FileKind, Compact);
  }

  llvm::yaml::Output YAMLOut(OS, &Ctx, /*WrapColumn=*/80);

  std::vector<const InterfaceFile *> Files;
  Files.emplace_back(&File);

  for (const auto &Document : File.documents())
    Files.emplace_back(Document.get());

  // Stream out yaml.
  YAMLOut << Files;

  return Error::success();
}
