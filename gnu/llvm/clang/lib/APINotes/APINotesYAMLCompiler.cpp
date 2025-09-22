//===-- APINotesYAMLCompiler.cpp - API Notes YAML Format Reader -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The types defined locally are designed to represent the YAML state, which
// adds an additional bit of state: e.g. a tri-state boolean attribute (yes, no,
// not applied) becomes a tri-state boolean + present.  As a result, while these
// enumerations appear to be redefining constants from the attributes table
// data, they are distinct.
//

#include "clang/APINotes/APINotesYAMLCompiler.h"
#include "clang/APINotes/APINotesWriter.h"
#include "clang/APINotes/Types.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/YAMLTraits.h"
#include <optional>
#include <vector>

using namespace clang;
using namespace api_notes;

namespace {
enum class APIAvailability {
  Available = 0,
  None,
  NonSwift,
};
} // namespace

namespace llvm {
namespace yaml {
template <> struct ScalarEnumerationTraits<APIAvailability> {
  static void enumeration(IO &IO, APIAvailability &AA) {
    IO.enumCase(AA, "none", APIAvailability::None);
    IO.enumCase(AA, "nonswift", APIAvailability::NonSwift);
    IO.enumCase(AA, "available", APIAvailability::Available);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
enum class MethodKind {
  Class,
  Instance,
};
} // namespace

namespace llvm {
namespace yaml {
template <> struct ScalarEnumerationTraits<MethodKind> {
  static void enumeration(IO &IO, MethodKind &MK) {
    IO.enumCase(MK, "Class", MethodKind::Class);
    IO.enumCase(MK, "Instance", MethodKind::Instance);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Param {
  unsigned Position;
  std::optional<bool> NoEscape = false;
  std::optional<NullabilityKind> Nullability;
  std::optional<RetainCountConventionKind> RetainCountConvention;
  StringRef Type;
};

typedef std::vector<Param> ParamsSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Param)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(NullabilityKind)

namespace llvm {
namespace yaml {
template <> struct ScalarEnumerationTraits<NullabilityKind> {
  static void enumeration(IO &IO, NullabilityKind &NK) {
    IO.enumCase(NK, "Nonnull", NullabilityKind::NonNull);
    IO.enumCase(NK, "Optional", NullabilityKind::Nullable);
    IO.enumCase(NK, "Unspecified", NullabilityKind::Unspecified);
    IO.enumCase(NK, "NullableResult", NullabilityKind::NullableResult);
    // TODO: Mapping this to it's own value would allow for better cross
    // checking. Also the default should be Unknown.
    IO.enumCase(NK, "Scalar", NullabilityKind::Unspecified);

    // Aliases for compatibility with existing APINotes.
    IO.enumCase(NK, "N", NullabilityKind::NonNull);
    IO.enumCase(NK, "O", NullabilityKind::Nullable);
    IO.enumCase(NK, "U", NullabilityKind::Unspecified);
    IO.enumCase(NK, "S", NullabilityKind::Unspecified);
  }
};

template <> struct ScalarEnumerationTraits<RetainCountConventionKind> {
  static void enumeration(IO &IO, RetainCountConventionKind &RCCK) {
    IO.enumCase(RCCK, "none", RetainCountConventionKind::None);
    IO.enumCase(RCCK, "CFReturnsRetained",
                RetainCountConventionKind::CFReturnsRetained);
    IO.enumCase(RCCK, "CFReturnsNotRetained",
                RetainCountConventionKind::CFReturnsNotRetained);
    IO.enumCase(RCCK, "NSReturnsRetained",
                RetainCountConventionKind::NSReturnsRetained);
    IO.enumCase(RCCK, "NSReturnsNotRetained",
                RetainCountConventionKind::NSReturnsNotRetained);
  }
};

template <> struct MappingTraits<Param> {
  static void mapping(IO &IO, Param &P) {
    IO.mapRequired("Position", P.Position);
    IO.mapOptional("Nullability", P.Nullability, std::nullopt);
    IO.mapOptional("RetainCountConvention", P.RetainCountConvention);
    IO.mapOptional("NoEscape", P.NoEscape);
    IO.mapOptional("Type", P.Type, StringRef(""));
  }
};
} // namespace yaml
} // namespace llvm

namespace {
typedef std::vector<NullabilityKind> NullabilitySeq;

struct AvailabilityItem {
  APIAvailability Mode = APIAvailability::Available;
  StringRef Msg;
};

/// Old attribute deprecated in favor of SwiftName.
enum class FactoryAsInitKind {
  /// Infer based on name and type (the default).
  Infer,
  /// Treat as a class method.
  AsClassMethod,
  /// Treat as an initializer.
  AsInitializer,
};

struct Method {
  StringRef Selector;
  MethodKind Kind;
  ParamsSeq Params;
  NullabilitySeq Nullability;
  std::optional<NullabilityKind> NullabilityOfRet;
  std::optional<RetainCountConventionKind> RetainCountConvention;
  AvailabilityItem Availability;
  std::optional<bool> SwiftPrivate;
  StringRef SwiftName;
  FactoryAsInitKind FactoryAsInit = FactoryAsInitKind::Infer;
  bool DesignatedInit = false;
  bool Required = false;
  StringRef ResultType;
};

typedef std::vector<Method> MethodsSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Method)

namespace llvm {
namespace yaml {
template <> struct ScalarEnumerationTraits<FactoryAsInitKind> {
  static void enumeration(IO &IO, FactoryAsInitKind &FIK) {
    IO.enumCase(FIK, "A", FactoryAsInitKind::Infer);
    IO.enumCase(FIK, "C", FactoryAsInitKind::AsClassMethod);
    IO.enumCase(FIK, "I", FactoryAsInitKind::AsInitializer);
  }
};

template <> struct MappingTraits<Method> {
  static void mapping(IO &IO, Method &M) {
    IO.mapRequired("Selector", M.Selector);
    IO.mapRequired("MethodKind", M.Kind);
    IO.mapOptional("Parameters", M.Params);
    IO.mapOptional("Nullability", M.Nullability);
    IO.mapOptional("NullabilityOfRet", M.NullabilityOfRet, std::nullopt);
    IO.mapOptional("RetainCountConvention", M.RetainCountConvention);
    IO.mapOptional("Availability", M.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", M.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", M.SwiftPrivate);
    IO.mapOptional("SwiftName", M.SwiftName, StringRef(""));
    IO.mapOptional("FactoryAsInit", M.FactoryAsInit, FactoryAsInitKind::Infer);
    IO.mapOptional("DesignatedInit", M.DesignatedInit, false);
    IO.mapOptional("Required", M.Required, false);
    IO.mapOptional("ResultType", M.ResultType, StringRef(""));
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Property {
  StringRef Name;
  std::optional<MethodKind> Kind;
  std::optional<NullabilityKind> Nullability;
  AvailabilityItem Availability;
  std::optional<bool> SwiftPrivate;
  StringRef SwiftName;
  std::optional<bool> SwiftImportAsAccessors;
  StringRef Type;
};

typedef std::vector<Property> PropertiesSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Property)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<Property> {
  static void mapping(IO &IO, Property &P) {
    IO.mapRequired("Name", P.Name);
    IO.mapOptional("PropertyKind", P.Kind);
    IO.mapOptional("Nullability", P.Nullability, std::nullopt);
    IO.mapOptional("Availability", P.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", P.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", P.SwiftPrivate);
    IO.mapOptional("SwiftName", P.SwiftName, StringRef(""));
    IO.mapOptional("SwiftImportAsAccessors", P.SwiftImportAsAccessors);
    IO.mapOptional("Type", P.Type, StringRef(""));
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Class {
  StringRef Name;
  bool AuditedForNullability = false;
  AvailabilityItem Availability;
  std::optional<bool> SwiftPrivate;
  StringRef SwiftName;
  std::optional<StringRef> SwiftBridge;
  std::optional<StringRef> NSErrorDomain;
  std::optional<bool> SwiftImportAsNonGeneric;
  std::optional<bool> SwiftObjCMembers;
  MethodsSeq Methods;
  PropertiesSeq Properties;
};

typedef std::vector<Class> ClassesSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Class)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<Class> {
  static void mapping(IO &IO, Class &C) {
    IO.mapRequired("Name", C.Name);
    IO.mapOptional("AuditedForNullability", C.AuditedForNullability, false);
    IO.mapOptional("Availability", C.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", C.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", C.SwiftPrivate);
    IO.mapOptional("SwiftName", C.SwiftName, StringRef(""));
    IO.mapOptional("SwiftBridge", C.SwiftBridge);
    IO.mapOptional("NSErrorDomain", C.NSErrorDomain);
    IO.mapOptional("SwiftImportAsNonGeneric", C.SwiftImportAsNonGeneric);
    IO.mapOptional("SwiftObjCMembers", C.SwiftObjCMembers);
    IO.mapOptional("Methods", C.Methods);
    IO.mapOptional("Properties", C.Properties);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Function {
  StringRef Name;
  ParamsSeq Params;
  NullabilitySeq Nullability;
  std::optional<NullabilityKind> NullabilityOfRet;
  std::optional<api_notes::RetainCountConventionKind> RetainCountConvention;
  AvailabilityItem Availability;
  std::optional<bool> SwiftPrivate;
  StringRef SwiftName;
  StringRef Type;
  StringRef ResultType;
};

typedef std::vector<Function> FunctionsSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Function)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<Function> {
  static void mapping(IO &IO, Function &F) {
    IO.mapRequired("Name", F.Name);
    IO.mapOptional("Parameters", F.Params);
    IO.mapOptional("Nullability", F.Nullability);
    IO.mapOptional("NullabilityOfRet", F.NullabilityOfRet, std::nullopt);
    IO.mapOptional("RetainCountConvention", F.RetainCountConvention);
    IO.mapOptional("Availability", F.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", F.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", F.SwiftPrivate);
    IO.mapOptional("SwiftName", F.SwiftName, StringRef(""));
    IO.mapOptional("ResultType", F.ResultType, StringRef(""));
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct GlobalVariable {
  StringRef Name;
  std::optional<NullabilityKind> Nullability;
  AvailabilityItem Availability;
  std::optional<bool> SwiftPrivate;
  StringRef SwiftName;
  StringRef Type;
};

typedef std::vector<GlobalVariable> GlobalVariablesSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(GlobalVariable)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<GlobalVariable> {
  static void mapping(IO &IO, GlobalVariable &GV) {
    IO.mapRequired("Name", GV.Name);
    IO.mapOptional("Nullability", GV.Nullability, std::nullopt);
    IO.mapOptional("Availability", GV.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", GV.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", GV.SwiftPrivate);
    IO.mapOptional("SwiftName", GV.SwiftName, StringRef(""));
    IO.mapOptional("Type", GV.Type, StringRef(""));
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct EnumConstant {
  StringRef Name;
  AvailabilityItem Availability;
  std::optional<bool> SwiftPrivate;
  StringRef SwiftName;
};

typedef std::vector<EnumConstant> EnumConstantsSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(EnumConstant)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<EnumConstant> {
  static void mapping(IO &IO, EnumConstant &EC) {
    IO.mapRequired("Name", EC.Name);
    IO.mapOptional("Availability", EC.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", EC.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", EC.SwiftPrivate);
    IO.mapOptional("SwiftName", EC.SwiftName, StringRef(""));
  }
};
} // namespace yaml
} // namespace llvm

namespace {
/// Syntactic sugar for EnumExtensibility and FlagEnum
enum class EnumConvenienceAliasKind {
  /// EnumExtensibility: none, FlagEnum: false
  None,
  /// EnumExtensibility: open, FlagEnum: false
  CFEnum,
  /// EnumExtensibility: open, FlagEnum: true
  CFOptions,
  /// EnumExtensibility: closed, FlagEnum: false
  CFClosedEnum
};
} // namespace

namespace llvm {
namespace yaml {
template <> struct ScalarEnumerationTraits<EnumConvenienceAliasKind> {
  static void enumeration(IO &IO, EnumConvenienceAliasKind &ECAK) {
    IO.enumCase(ECAK, "none", EnumConvenienceAliasKind::None);
    IO.enumCase(ECAK, "CFEnum", EnumConvenienceAliasKind::CFEnum);
    IO.enumCase(ECAK, "NSEnum", EnumConvenienceAliasKind::CFEnum);
    IO.enumCase(ECAK, "CFOptions", EnumConvenienceAliasKind::CFOptions);
    IO.enumCase(ECAK, "NSOptions", EnumConvenienceAliasKind::CFOptions);
    IO.enumCase(ECAK, "CFClosedEnum", EnumConvenienceAliasKind::CFClosedEnum);
    IO.enumCase(ECAK, "NSClosedEnum", EnumConvenienceAliasKind::CFClosedEnum);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Tag {
  StringRef Name;
  AvailabilityItem Availability;
  StringRef SwiftName;
  std::optional<bool> SwiftPrivate;
  std::optional<StringRef> SwiftBridge;
  std::optional<StringRef> NSErrorDomain;
  std::optional<std::string> SwiftImportAs;
  std::optional<std::string> SwiftRetainOp;
  std::optional<std::string> SwiftReleaseOp;
  std::optional<EnumExtensibilityKind> EnumExtensibility;
  std::optional<bool> FlagEnum;
  std::optional<EnumConvenienceAliasKind> EnumConvenienceKind;
  std::optional<bool> SwiftCopyable;
  FunctionsSeq Methods;
};

typedef std::vector<Tag> TagsSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Tag)

namespace llvm {
namespace yaml {
template <> struct ScalarEnumerationTraits<EnumExtensibilityKind> {
  static void enumeration(IO &IO, EnumExtensibilityKind &EEK) {
    IO.enumCase(EEK, "none", EnumExtensibilityKind::None);
    IO.enumCase(EEK, "open", EnumExtensibilityKind::Open);
    IO.enumCase(EEK, "closed", EnumExtensibilityKind::Closed);
  }
};

template <> struct MappingTraits<Tag> {
  static void mapping(IO &IO, Tag &T) {
    IO.mapRequired("Name", T.Name);
    IO.mapOptional("Availability", T.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", T.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", T.SwiftPrivate);
    IO.mapOptional("SwiftName", T.SwiftName, StringRef(""));
    IO.mapOptional("SwiftBridge", T.SwiftBridge);
    IO.mapOptional("NSErrorDomain", T.NSErrorDomain);
    IO.mapOptional("SwiftImportAs", T.SwiftImportAs);
    IO.mapOptional("SwiftReleaseOp", T.SwiftReleaseOp);
    IO.mapOptional("SwiftRetainOp", T.SwiftRetainOp);
    IO.mapOptional("EnumExtensibility", T.EnumExtensibility);
    IO.mapOptional("FlagEnum", T.FlagEnum);
    IO.mapOptional("EnumKind", T.EnumConvenienceKind);
    IO.mapOptional("SwiftCopyable", T.SwiftCopyable);
    IO.mapOptional("Methods", T.Methods);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Typedef {
  StringRef Name;
  AvailabilityItem Availability;
  StringRef SwiftName;
  std::optional<bool> SwiftPrivate;
  std::optional<StringRef> SwiftBridge;
  std::optional<StringRef> NSErrorDomain;
  std::optional<SwiftNewTypeKind> SwiftType;
};

typedef std::vector<Typedef> TypedefsSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Typedef)

namespace llvm {
namespace yaml {
template <> struct ScalarEnumerationTraits<SwiftNewTypeKind> {
  static void enumeration(IO &IO, SwiftNewTypeKind &SWK) {
    IO.enumCase(SWK, "none", SwiftNewTypeKind::None);
    IO.enumCase(SWK, "struct", SwiftNewTypeKind::Struct);
    IO.enumCase(SWK, "enum", SwiftNewTypeKind::Enum);
  }
};

template <> struct MappingTraits<Typedef> {
  static void mapping(IO &IO, Typedef &T) {
    IO.mapRequired("Name", T.Name);
    IO.mapOptional("Availability", T.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", T.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", T.SwiftPrivate);
    IO.mapOptional("SwiftName", T.SwiftName, StringRef(""));
    IO.mapOptional("SwiftBridge", T.SwiftBridge);
    IO.mapOptional("NSErrorDomain", T.NSErrorDomain);
    IO.mapOptional("SwiftWrapper", T.SwiftType);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Namespace;
typedef std::vector<Namespace> NamespacesSeq;

struct TopLevelItems {
  ClassesSeq Classes;
  ClassesSeq Protocols;
  FunctionsSeq Functions;
  GlobalVariablesSeq Globals;
  EnumConstantsSeq EnumConstants;
  TagsSeq Tags;
  TypedefsSeq Typedefs;
  NamespacesSeq Namespaces;
};
} // namespace

namespace llvm {
namespace yaml {
static void mapTopLevelItems(IO &IO, TopLevelItems &TLI) {
  IO.mapOptional("Classes", TLI.Classes);
  IO.mapOptional("Protocols", TLI.Protocols);
  IO.mapOptional("Functions", TLI.Functions);
  IO.mapOptional("Globals", TLI.Globals);
  IO.mapOptional("Enumerators", TLI.EnumConstants);
  IO.mapOptional("Tags", TLI.Tags);
  IO.mapOptional("Typedefs", TLI.Typedefs);
  IO.mapOptional("Namespaces", TLI.Namespaces);
}
} // namespace yaml
} // namespace llvm

namespace {
struct Namespace {
  StringRef Name;
  AvailabilityItem Availability;
  StringRef SwiftName;
  std::optional<bool> SwiftPrivate;
  TopLevelItems Items;
};
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Namespace)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<Namespace> {
  static void mapping(IO &IO, Namespace &T) {
    IO.mapRequired("Name", T.Name);
    IO.mapOptional("Availability", T.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", T.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftPrivate", T.SwiftPrivate);
    IO.mapOptional("SwiftName", T.SwiftName, StringRef(""));
    mapTopLevelItems(IO, T.Items);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Versioned {
  VersionTuple Version;
  TopLevelItems Items;
};

typedef std::vector<Versioned> VersionedSeq;
} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(Versioned)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<Versioned> {
  static void mapping(IO &IO, Versioned &V) {
    IO.mapRequired("Version", V.Version);
    mapTopLevelItems(IO, V.Items);
  }
};
} // namespace yaml
} // namespace llvm

namespace {
struct Module {
  StringRef Name;
  AvailabilityItem Availability;
  TopLevelItems TopLevel;
  VersionedSeq SwiftVersions;

  std::optional<bool> SwiftInferImportAsMember;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() /*const*/;
#endif
};
} // namespace

namespace llvm {
namespace yaml {
template <> struct MappingTraits<Module> {
  static void mapping(IO &IO, Module &M) {
    IO.mapRequired("Name", M.Name);
    IO.mapOptional("Availability", M.Availability.Mode,
                   APIAvailability::Available);
    IO.mapOptional("AvailabilityMsg", M.Availability.Msg, StringRef(""));
    IO.mapOptional("SwiftInferImportAsMember", M.SwiftInferImportAsMember);
    mapTopLevelItems(IO, M.TopLevel);
    IO.mapOptional("SwiftVersions", M.SwiftVersions);
  }
};
} // namespace yaml
} // namespace llvm

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void Module::dump() {
  llvm::yaml::Output OS(llvm::errs());
  OS << *this;
}
#endif

namespace {
bool parseAPINotes(StringRef YI, Module &M, llvm::SourceMgr::DiagHandlerTy Diag,
                   void *DiagContext) {
  llvm::yaml::Input IS(YI, nullptr, Diag, DiagContext);
  IS >> M;
  return static_cast<bool>(IS.error());
}
} // namespace

bool clang::api_notes::parseAndDumpAPINotes(StringRef YI,
                                            llvm::raw_ostream &OS) {
  Module M;
  if (parseAPINotes(YI, M, nullptr, nullptr))
    return true;

  llvm::yaml::Output YOS(OS);
  YOS << M;

  return false;
}

namespace {
using namespace api_notes;

class YAMLConverter {
  const Module &M;
  APINotesWriter Writer;
  llvm::raw_ostream &OS;
  llvm::SourceMgr::DiagHandlerTy DiagHandler;
  void *DiagHandlerCtxt;
  bool ErrorOccured;

  /// Emit a diagnostic
  bool emitError(llvm::Twine Message) {
    DiagHandler(
        llvm::SMDiagnostic("", llvm::SourceMgr::DK_Error, Message.str()),
        DiagHandlerCtxt);
    ErrorOccured = true;
    return true;
  }

public:
  YAMLConverter(const Module &TheModule, const FileEntry *SourceFile,
                llvm::raw_ostream &OS,
                llvm::SourceMgr::DiagHandlerTy DiagHandler,
                void *DiagHandlerCtxt)
      : M(TheModule), Writer(TheModule.Name, SourceFile), OS(OS),
        DiagHandler(DiagHandler), DiagHandlerCtxt(DiagHandlerCtxt),
        ErrorOccured(false) {}

  void convertAvailability(const AvailabilityItem &Availability,
                           CommonEntityInfo &CEI, llvm::StringRef APIName) {
    // Populate the unavailability information.
    CEI.Unavailable = (Availability.Mode == APIAvailability::None);
    CEI.UnavailableInSwift = (Availability.Mode == APIAvailability::NonSwift);
    if (CEI.Unavailable || CEI.UnavailableInSwift) {
      CEI.UnavailableMsg = std::string(Availability.Msg);
    } else {
      if (!Availability.Msg.empty())
        emitError(llvm::Twine("availability message for available API '") +
                  APIName + "' will not be used");
    }
  }

  void convertParams(const ParamsSeq &Params, FunctionInfo &OutInfo) {
    for (const auto &P : Params) {
      ParamInfo PI;
      if (P.Nullability)
        PI.setNullabilityAudited(*P.Nullability);
      PI.setNoEscape(P.NoEscape);
      PI.setType(std::string(P.Type));
      PI.setRetainCountConvention(P.RetainCountConvention);
      if (OutInfo.Params.size() <= P.Position)
        OutInfo.Params.resize(P.Position + 1);
      OutInfo.Params[P.Position] |= PI;
    }
  }

  void convertNullability(const NullabilitySeq &Nullability,
                          std::optional<NullabilityKind> ReturnNullability,
                          FunctionInfo &OutInfo, llvm::StringRef APIName) {
    if (Nullability.size() > FunctionInfo::getMaxNullabilityIndex()) {
      emitError(llvm::Twine("nullability info for '") + APIName +
                "' does not fit");
      return;
    }

    bool audited = false;
    unsigned int idx = 1;
    for (const auto &N : Nullability)
      OutInfo.addTypeInfo(idx++, N);
    audited = Nullability.size() > 0 || ReturnNullability;
    if (audited)
      OutInfo.addTypeInfo(0, ReturnNullability ? *ReturnNullability
                                               : NullabilityKind::NonNull);
    if (!audited)
      return;
    OutInfo.NullabilityAudited = audited;
    OutInfo.NumAdjustedNullable = idx;
  }

  /// Convert the common parts of an entity from YAML.
  template <typename T>
  void convertCommonEntity(const T &Common, CommonEntityInfo &Info,
                           StringRef APIName) {
    convertAvailability(Common.Availability, Info, APIName);
    Info.setSwiftPrivate(Common.SwiftPrivate);
    Info.SwiftName = std::string(Common.SwiftName);
  }

  /// Convert the common parts of a type entity from YAML.
  template <typename T>
  void convertCommonType(const T &Common, CommonTypeInfo &Info,
                         StringRef APIName) {
    convertCommonEntity(Common, Info, APIName);
    if (Common.SwiftBridge)
      Info.setSwiftBridge(std::string(*Common.SwiftBridge));
    Info.setNSErrorDomain(Common.NSErrorDomain);
  }

  // Translate from Method into ObjCMethodInfo and write it out.
  void convertMethod(const Method &M, ContextID ClassID, StringRef ClassName,
                     VersionTuple SwiftVersion) {
    ObjCMethodInfo MI;
    convertCommonEntity(M, MI, M.Selector);

    // Check if the selector ends with ':' to determine if it takes arguments.
    bool takesArguments = M.Selector.ends_with(":");

    // Split the selector into pieces.
    llvm::SmallVector<StringRef, 4> Args;
    M.Selector.split(Args, ":", /*MaxSplit*/ -1, /*KeepEmpty*/ false);
    if (!takesArguments && Args.size() > 1) {
      emitError("selector '" + M.Selector + "' is missing a ':' at the end");
      return;
    }

    // Construct ObjCSelectorRef.
    api_notes::ObjCSelectorRef Selector;
    Selector.NumArgs = !takesArguments ? 0 : Args.size();
    Selector.Identifiers = Args;

    // Translate the initializer info.
    MI.DesignatedInit = M.DesignatedInit;
    MI.RequiredInit = M.Required;
    if (M.FactoryAsInit != FactoryAsInitKind::Infer)
      emitError("'FactoryAsInit' is no longer valid; use 'SwiftName' instead");

    MI.ResultType = std::string(M.ResultType);

    // Translate parameter information.
    convertParams(M.Params, MI);

    // Translate nullability info.
    convertNullability(M.Nullability, M.NullabilityOfRet, MI, M.Selector);

    MI.setRetainCountConvention(M.RetainCountConvention);

    // Write it.
    Writer.addObjCMethod(ClassID, Selector, M.Kind == MethodKind::Instance, MI,
                         SwiftVersion);
  }

  void convertContext(std::optional<ContextID> ParentContextID, const Class &C,
                      ContextKind Kind, VersionTuple SwiftVersion) {
    // Write the class.
    ContextInfo CI;
    convertCommonType(C, CI, C.Name);

    if (C.AuditedForNullability)
      CI.setDefaultNullability(NullabilityKind::NonNull);
    if (C.SwiftImportAsNonGeneric)
      CI.setSwiftImportAsNonGeneric(*C.SwiftImportAsNonGeneric);
    if (C.SwiftObjCMembers)
      CI.setSwiftObjCMembers(*C.SwiftObjCMembers);

    ContextID CtxID =
        Writer.addContext(ParentContextID, C.Name, Kind, CI, SwiftVersion);

    // Write all methods.
    llvm::StringMap<std::pair<bool, bool>> KnownMethods;
    for (const auto &method : C.Methods) {
      // Check for duplicate method definitions.
      bool IsInstanceMethod = method.Kind == MethodKind::Instance;
      bool &Known = IsInstanceMethod ? KnownMethods[method.Selector].first
                                     : KnownMethods[method.Selector].second;
      if (Known) {
        emitError(llvm::Twine("duplicate definition of method '") +
                  (IsInstanceMethod ? "-" : "+") + "[" + C.Name + " " +
                  method.Selector + "]'");
        continue;
      }
      Known = true;

      convertMethod(method, CtxID, C.Name, SwiftVersion);
    }

    // Write all properties.
    llvm::StringSet<> KnownInstanceProperties;
    llvm::StringSet<> KnownClassProperties;
    for (const auto &Property : C.Properties) {
      // Check for duplicate property definitions.
      if ((!Property.Kind || *Property.Kind == MethodKind::Instance) &&
          !KnownInstanceProperties.insert(Property.Name).second) {
        emitError(llvm::Twine("duplicate definition of instance property '") +
                  C.Name + "." + Property.Name + "'");
        continue;
      }

      if ((!Property.Kind || *Property.Kind == MethodKind::Class) &&
          !KnownClassProperties.insert(Property.Name).second) {
        emitError(llvm::Twine("duplicate definition of class property '") +
                  C.Name + "." + Property.Name + "'");
        continue;
      }

      // Translate from Property into ObjCPropertyInfo.
      ObjCPropertyInfo PI;
      convertAvailability(Property.Availability, PI, Property.Name);
      PI.setSwiftPrivate(Property.SwiftPrivate);
      PI.SwiftName = std::string(Property.SwiftName);
      if (Property.Nullability)
        PI.setNullabilityAudited(*Property.Nullability);
      if (Property.SwiftImportAsAccessors)
        PI.setSwiftImportAsAccessors(*Property.SwiftImportAsAccessors);
      PI.setType(std::string(Property.Type));

      // Add both instance and class properties with this name.
      if (Property.Kind) {
        Writer.addObjCProperty(CtxID, Property.Name,
                               *Property.Kind == MethodKind::Instance, PI,
                               SwiftVersion);
      } else {
        Writer.addObjCProperty(CtxID, Property.Name, true, PI, SwiftVersion);
        Writer.addObjCProperty(CtxID, Property.Name, false, PI, SwiftVersion);
      }
    }
  }

  void convertNamespaceContext(std::optional<ContextID> ParentContextID,
                               const Namespace &TheNamespace,
                               VersionTuple SwiftVersion) {
    // Write the namespace.
    ContextInfo CI;
    convertCommonEntity(TheNamespace, CI, TheNamespace.Name);

    ContextID CtxID =
        Writer.addContext(ParentContextID, TheNamespace.Name,
                          ContextKind::Namespace, CI, SwiftVersion);

    convertTopLevelItems(Context(CtxID, ContextKind::Namespace),
                         TheNamespace.Items, SwiftVersion);
  }

  void convertFunction(const Function &Function, FunctionInfo &FI) {
    convertAvailability(Function.Availability, FI, Function.Name);
    FI.setSwiftPrivate(Function.SwiftPrivate);
    FI.SwiftName = std::string(Function.SwiftName);
    convertParams(Function.Params, FI);
    convertNullability(Function.Nullability, Function.NullabilityOfRet, FI,
                       Function.Name);
    FI.ResultType = std::string(Function.ResultType);
    FI.setRetainCountConvention(Function.RetainCountConvention);
  }

  void convertTagContext(std::optional<Context> ParentContext, const Tag &T,
                         VersionTuple SwiftVersion) {
    TagInfo TI;
    std::optional<ContextID> ParentContextID =
        ParentContext ? std::optional<ContextID>(ParentContext->id)
                      : std::nullopt;
    convertCommonType(T, TI, T.Name);

    if ((T.SwiftRetainOp || T.SwiftReleaseOp) && !T.SwiftImportAs) {
      emitError(llvm::Twine("should declare SwiftImportAs to use "
                            "SwiftRetainOp and SwiftReleaseOp (for ") +
                T.Name + ")");
      return;
    }
    if (T.SwiftReleaseOp.has_value() != T.SwiftRetainOp.has_value()) {
      emitError(llvm::Twine("should declare both SwiftReleaseOp and "
                            "SwiftRetainOp (for ") +
                T.Name + ")");
      return;
    }

    if (T.SwiftImportAs)
      TI.SwiftImportAs = T.SwiftImportAs;
    if (T.SwiftRetainOp)
      TI.SwiftRetainOp = T.SwiftRetainOp;
    if (T.SwiftReleaseOp)
      TI.SwiftReleaseOp = T.SwiftReleaseOp;

    if (T.SwiftCopyable)
      TI.setSwiftCopyable(T.SwiftCopyable);

    if (T.EnumConvenienceKind) {
      if (T.EnumExtensibility) {
        emitError(
            llvm::Twine("cannot mix EnumKind and EnumExtensibility (for ") +
            T.Name + ")");
        return;
      }
      if (T.FlagEnum) {
        emitError(llvm::Twine("cannot mix EnumKind and FlagEnum (for ") +
                  T.Name + ")");
        return;
      }
      switch (*T.EnumConvenienceKind) {
      case EnumConvenienceAliasKind::None:
        TI.EnumExtensibility = EnumExtensibilityKind::None;
        TI.setFlagEnum(false);
        break;
      case EnumConvenienceAliasKind::CFEnum:
        TI.EnumExtensibility = EnumExtensibilityKind::Open;
        TI.setFlagEnum(false);
        break;
      case EnumConvenienceAliasKind::CFOptions:
        TI.EnumExtensibility = EnumExtensibilityKind::Open;
        TI.setFlagEnum(true);
        break;
      case EnumConvenienceAliasKind::CFClosedEnum:
        TI.EnumExtensibility = EnumExtensibilityKind::Closed;
        TI.setFlagEnum(false);
        break;
      }
    } else {
      TI.EnumExtensibility = T.EnumExtensibility;
      TI.setFlagEnum(T.FlagEnum);
    }

    Writer.addTag(ParentContext, T.Name, TI, SwiftVersion);

    ContextInfo CI;
    auto TagCtxID = Writer.addContext(ParentContextID, T.Name, ContextKind::Tag,
                                      CI, SwiftVersion);

    for (const auto &CXXMethod : T.Methods) {
      CXXMethodInfo MI;
      convertFunction(CXXMethod, MI);
      Writer.addCXXMethod(TagCtxID, CXXMethod.Name, MI, SwiftVersion);
    }
  }

  void convertTopLevelItems(std::optional<Context> Ctx,
                            const TopLevelItems &TLItems,
                            VersionTuple SwiftVersion) {
    std::optional<ContextID> CtxID =
        Ctx ? std::optional(Ctx->id) : std::nullopt;

    // Write all classes.
    llvm::StringSet<> KnownClasses;
    for (const auto &Class : TLItems.Classes) {
      // Check for duplicate class definitions.
      if (!KnownClasses.insert(Class.Name).second) {
        emitError(llvm::Twine("multiple definitions of class '") + Class.Name +
                  "'");
        continue;
      }

      convertContext(CtxID, Class, ContextKind::ObjCClass, SwiftVersion);
    }

    // Write all protocols.
    llvm::StringSet<> KnownProtocols;
    for (const auto &Protocol : TLItems.Protocols) {
      // Check for duplicate protocol definitions.
      if (!KnownProtocols.insert(Protocol.Name).second) {
        emitError(llvm::Twine("multiple definitions of protocol '") +
                  Protocol.Name + "'");
        continue;
      }

      convertContext(CtxID, Protocol, ContextKind::ObjCProtocol, SwiftVersion);
    }

    // Write all namespaces.
    llvm::StringSet<> KnownNamespaces;
    for (const auto &Namespace : TLItems.Namespaces) {
      // Check for duplicate namespace definitions.
      if (!KnownNamespaces.insert(Namespace.Name).second) {
        emitError(llvm::Twine("multiple definitions of namespace '") +
                  Namespace.Name + "'");
        continue;
      }

      convertNamespaceContext(CtxID, Namespace, SwiftVersion);
    }

    // Write all global variables.
    llvm::StringSet<> KnownGlobals;
    for (const auto &Global : TLItems.Globals) {
      // Check for duplicate global variables.
      if (!KnownGlobals.insert(Global.Name).second) {
        emitError(llvm::Twine("multiple definitions of global variable '") +
                  Global.Name + "'");
        continue;
      }

      GlobalVariableInfo GVI;
      convertAvailability(Global.Availability, GVI, Global.Name);
      GVI.setSwiftPrivate(Global.SwiftPrivate);
      GVI.SwiftName = std::string(Global.SwiftName);
      if (Global.Nullability)
        GVI.setNullabilityAudited(*Global.Nullability);
      GVI.setType(std::string(Global.Type));
      Writer.addGlobalVariable(Ctx, Global.Name, GVI, SwiftVersion);
    }

    // Write all global functions.
    llvm::StringSet<> KnownFunctions;
    for (const auto &Function : TLItems.Functions) {
      // Check for duplicate global functions.
      if (!KnownFunctions.insert(Function.Name).second) {
        emitError(llvm::Twine("multiple definitions of global function '") +
                  Function.Name + "'");
        continue;
      }

      GlobalFunctionInfo GFI;
      convertFunction(Function, GFI);
      Writer.addGlobalFunction(Ctx, Function.Name, GFI, SwiftVersion);
    }

    // Write all enumerators.
    llvm::StringSet<> KnownEnumConstants;
    for (const auto &EnumConstant : TLItems.EnumConstants) {
      // Check for duplicate enumerators
      if (!KnownEnumConstants.insert(EnumConstant.Name).second) {
        emitError(llvm::Twine("multiple definitions of enumerator '") +
                  EnumConstant.Name + "'");
        continue;
      }

      EnumConstantInfo ECI;
      convertAvailability(EnumConstant.Availability, ECI, EnumConstant.Name);
      ECI.setSwiftPrivate(EnumConstant.SwiftPrivate);
      ECI.SwiftName = std::string(EnumConstant.SwiftName);
      Writer.addEnumConstant(EnumConstant.Name, ECI, SwiftVersion);
    }

    // Write all tags.
    llvm::StringSet<> KnownTags;
    for (const auto &Tag : TLItems.Tags) {
      // Check for duplicate tag definitions.
      if (!KnownTags.insert(Tag.Name).second) {
        emitError(llvm::Twine("multiple definitions of tag '") + Tag.Name +
                  "'");
        continue;
      }

      convertTagContext(Ctx, Tag, SwiftVersion);
    }

    // Write all typedefs.
    llvm::StringSet<> KnownTypedefs;
    for (const auto &Typedef : TLItems.Typedefs) {
      // Check for duplicate typedef definitions.
      if (!KnownTypedefs.insert(Typedef.Name).second) {
        emitError(llvm::Twine("multiple definitions of typedef '") +
                  Typedef.Name + "'");
        continue;
      }

      TypedefInfo TInfo;
      convertCommonType(Typedef, TInfo, Typedef.Name);
      TInfo.SwiftWrapper = Typedef.SwiftType;

      Writer.addTypedef(Ctx, Typedef.Name, TInfo, SwiftVersion);
    }
  }

  bool convertModule() {
    // Write the top-level items.
    convertTopLevelItems(/* context */ std::nullopt, M.TopLevel,
                         VersionTuple());

    // Convert the versioned information.
    for (const auto &Versioned : M.SwiftVersions)
      convertTopLevelItems(/* context */ std::nullopt, Versioned.Items,
                           Versioned.Version);

    if (!ErrorOccured)
      Writer.writeToStream(OS);

    return ErrorOccured;
  }
};
} // namespace

static bool compile(const Module &M, const FileEntry *SourceFile,
                    llvm::raw_ostream &OS,
                    llvm::SourceMgr::DiagHandlerTy DiagHandler,
                    void *DiagHandlerCtxt) {
  YAMLConverter C(M, SourceFile, OS, DiagHandler, DiagHandlerCtxt);
  return C.convertModule();
}

/// Simple diagnostic handler that prints diagnostics to standard error.
static void printDiagnostic(const llvm::SMDiagnostic &Diag, void *Context) {
  Diag.print(nullptr, llvm::errs());
}

bool api_notes::compileAPINotes(StringRef YAMLInput,
                                const FileEntry *SourceFile,
                                llvm::raw_ostream &OS,
                                llvm::SourceMgr::DiagHandlerTy DiagHandler,
                                void *DiagHandlerCtxt) {
  Module TheModule;

  if (!DiagHandler)
    DiagHandler = &printDiagnostic;

  if (parseAPINotes(YAMLInput, TheModule, DiagHandler, DiagHandlerCtxt))
    return true;

  return compile(TheModule, SourceFile, OS, DiagHandler, DiagHandlerCtxt);
}
