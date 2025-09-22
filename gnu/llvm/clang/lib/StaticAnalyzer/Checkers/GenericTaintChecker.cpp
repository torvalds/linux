//== GenericTaintChecker.cpp ----------------------------------- -*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker defines the attack surface for generic taint propagation.
//
// The taint information produced by it might be useful to other checkers. For
// example, checkers should report errors which involve tainted data more
// aggressively, even if the involved symbols are under constrained.
//
//===----------------------------------------------------------------------===//

#include "Yaml.h"
#include "clang/AST/Attr.h"
#include "clang/Basic/Builtins.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/YAMLTraits.h"

#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#define DEBUG_TYPE "taint-checker"

using namespace clang;
using namespace ento;
using namespace taint;

using llvm::ImmutableSet;

namespace {

class GenericTaintChecker;

/// Check for CWE-134: Uncontrolled Format String.
constexpr llvm::StringLiteral MsgUncontrolledFormatString =
    "Untrusted data is used as a format string "
    "(CWE-134: Uncontrolled Format String)";

/// Check for:
/// CERT/STR02-C. "Sanitize data passed to complex subsystems"
/// CWE-78, "Failure to Sanitize Data into an OS Command"
constexpr llvm::StringLiteral MsgSanitizeSystemArgs =
    "Untrusted data is passed to a system call "
    "(CERT/STR02-C. Sanitize data passed to complex subsystems)";

/// Check if tainted data is used as a custom sink's parameter.
constexpr llvm::StringLiteral MsgCustomSink =
    "Untrusted data is passed to a user-defined sink";

using ArgIdxTy = int;
using ArgVecTy = llvm::SmallVector<ArgIdxTy, 2>;

/// Denotes the return value.
constexpr ArgIdxTy ReturnValueIndex{-1};

static ArgIdxTy fromArgumentCount(unsigned Count) {
  assert(Count <=
             static_cast<std::size_t>(std::numeric_limits<ArgIdxTy>::max()) &&
         "ArgIdxTy is not large enough to represent the number of arguments.");
  return Count;
}

/// Check if the region the expression evaluates to is the standard input,
/// and thus, is tainted.
/// FIXME: Move this to Taint.cpp.
bool isStdin(SVal Val, const ASTContext &ACtx) {
  // FIXME: What if Val is NonParamVarRegion?

  // The region should be symbolic, we do not know it's value.
  const auto *SymReg = dyn_cast_or_null<SymbolicRegion>(Val.getAsRegion());
  if (!SymReg)
    return false;

  // Get it's symbol and find the declaration region it's pointing to.
  const auto *DeclReg =
      dyn_cast_or_null<DeclRegion>(SymReg->getSymbol()->getOriginRegion());
  if (!DeclReg)
    return false;

  // This region corresponds to a declaration, find out if it's a global/extern
  // variable named stdin with the proper type.
  if (const auto *D = dyn_cast_or_null<VarDecl>(DeclReg->getDecl())) {
    D = D->getCanonicalDecl();
    if (D->getName() == "stdin" && D->hasExternalStorage() && D->isExternC()) {
      const QualType FILETy = ACtx.getFILEType().getCanonicalType();
      const QualType Ty = D->getType().getCanonicalType();

      if (Ty->isPointerType())
        return Ty->getPointeeType() == FILETy;
    }
  }
  return false;
}

SVal getPointeeOf(ProgramStateRef State, Loc LValue) {
  const QualType ArgTy = LValue.getType(State->getStateManager().getContext());
  if (!ArgTy->isPointerType() || !ArgTy->getPointeeType()->isVoidType())
    return State->getSVal(LValue);

  // Do not dereference void pointers. Treat them as byte pointers instead.
  // FIXME: we might want to consider more than just the first byte.
  return State->getSVal(LValue, State->getStateManager().getContext().CharTy);
}

/// Given a pointer/reference argument, return the value it refers to.
std::optional<SVal> getPointeeOf(ProgramStateRef State, SVal Arg) {
  if (auto LValue = Arg.getAs<Loc>())
    return getPointeeOf(State, *LValue);
  return std::nullopt;
}

/// Given a pointer, return the SVal of its pointee or if it is tainted,
/// otherwise return the pointer's SVal if tainted.
/// Also considers stdin as a taint source.
std::optional<SVal> getTaintedPointeeOrPointer(ProgramStateRef State,
                                               SVal Arg) {
  if (auto Pointee = getPointeeOf(State, Arg))
    if (isTainted(State, *Pointee)) // FIXME: isTainted(...) ? Pointee : None;
      return Pointee;

  if (isTainted(State, Arg))
    return Arg;
  return std::nullopt;
}

bool isTaintedOrPointsToTainted(ProgramStateRef State, SVal ExprSVal) {
  return getTaintedPointeeOrPointer(State, ExprSVal).has_value();
}

/// Helps in printing taint diagnostics.
/// Marks the incoming parameters of a function interesting (to be printed)
/// when the return value, or the outgoing parameters are tainted.
const NoteTag *taintOriginTrackerTag(CheckerContext &C,
                                     std::vector<SymbolRef> TaintedSymbols,
                                     std::vector<ArgIdxTy> TaintedArgs,
                                     const LocationContext *CallLocation) {
  return C.getNoteTag([TaintedSymbols = std::move(TaintedSymbols),
                       TaintedArgs = std::move(TaintedArgs), CallLocation](
                          PathSensitiveBugReport &BR) -> std::string {
    SmallString<256> Msg;
    // We give diagnostics only for taint related reports
    if (!BR.isInteresting(CallLocation) ||
        BR.getBugType().getCategory() != categories::TaintedData) {
      return "";
    }
    if (TaintedSymbols.empty())
      return "Taint originated here";

    for (auto Sym : TaintedSymbols) {
      BR.markInteresting(Sym);
    }
    LLVM_DEBUG(for (auto Arg
                    : TaintedArgs) {
      llvm::dbgs() << "Taint Propagated from argument " << Arg + 1 << "\n";
    });
    return "";
  });
}

/// Helps in printing taint diagnostics.
/// Marks the function interesting (to be printed)
/// when the return value, or the outgoing parameters are tainted.
const NoteTag *taintPropagationExplainerTag(
    CheckerContext &C, std::vector<SymbolRef> TaintedSymbols,
    std::vector<ArgIdxTy> TaintedArgs, const LocationContext *CallLocation) {
  assert(TaintedSymbols.size() == TaintedArgs.size());
  return C.getNoteTag([TaintedSymbols = std::move(TaintedSymbols),
                       TaintedArgs = std::move(TaintedArgs), CallLocation](
                          PathSensitiveBugReport &BR) -> std::string {
    SmallString<256> Msg;
    llvm::raw_svector_ostream Out(Msg);
    // We give diagnostics only for taint related reports
    if (TaintedSymbols.empty() ||
        BR.getBugType().getCategory() != categories::TaintedData) {
      return "";
    }
    int nofTaintedArgs = 0;
    for (auto [Idx, Sym] : llvm::enumerate(TaintedSymbols)) {
      if (BR.isInteresting(Sym)) {
        BR.markInteresting(CallLocation);
        if (TaintedArgs[Idx] != ReturnValueIndex) {
          LLVM_DEBUG(llvm::dbgs() << "Taint Propagated to argument "
                                  << TaintedArgs[Idx] + 1 << "\n");
          if (nofTaintedArgs == 0)
            Out << "Taint propagated to the ";
          else
            Out << ", ";
          Out << TaintedArgs[Idx] + 1
              << llvm::getOrdinalSuffix(TaintedArgs[Idx] + 1) << " argument";
          nofTaintedArgs++;
        } else {
          LLVM_DEBUG(llvm::dbgs() << "Taint Propagated to return value.\n");
          Out << "Taint propagated to the return value";
        }
      }
    }
    return std::string(Out.str());
  });
}

/// ArgSet is used to describe arguments relevant for taint detection or
/// taint application. A discrete set of argument indexes and a variadic
/// argument list signified by a starting index are supported.
class ArgSet {
public:
  ArgSet() = default;
  ArgSet(ArgVecTy &&DiscreteArgs,
         std::optional<ArgIdxTy> VariadicIndex = std::nullopt)
      : DiscreteArgs(std::move(DiscreteArgs)),
        VariadicIndex(std::move(VariadicIndex)) {}

  bool contains(ArgIdxTy ArgIdx) const {
    if (llvm::is_contained(DiscreteArgs, ArgIdx))
      return true;

    return VariadicIndex && ArgIdx >= *VariadicIndex;
  }

  bool isEmpty() const { return DiscreteArgs.empty() && !VariadicIndex; }

private:
  ArgVecTy DiscreteArgs;
  std::optional<ArgIdxTy> VariadicIndex;
};

/// A struct used to specify taint propagation rules for a function.
///
/// If any of the possible taint source arguments is tainted, all of the
/// destination arguments should also be tainted. If ReturnValueIndex is added
/// to the dst list, the return value will be tainted.
class GenericTaintRule {
  /// Arguments which are taints sinks and should be checked, and a report
  /// should be emitted if taint reaches these.
  ArgSet SinkArgs;
  /// Arguments which should be sanitized on function return.
  ArgSet FilterArgs;
  /// Arguments which can participate in taint propagation. If any of the
  /// arguments in PropSrcArgs is tainted, all arguments in  PropDstArgs should
  /// be tainted.
  ArgSet PropSrcArgs;
  ArgSet PropDstArgs;

  /// A message that explains why the call is sensitive to taint.
  std::optional<StringRef> SinkMsg;

  GenericTaintRule() = default;

  GenericTaintRule(ArgSet &&Sink, ArgSet &&Filter, ArgSet &&Src, ArgSet &&Dst,
                   std::optional<StringRef> SinkMsg = std::nullopt)
      : SinkArgs(std::move(Sink)), FilterArgs(std::move(Filter)),
        PropSrcArgs(std::move(Src)), PropDstArgs(std::move(Dst)),
        SinkMsg(SinkMsg) {}

public:
  /// Make a rule that reports a warning if taint reaches any of \p FilterArgs
  /// arguments.
  static GenericTaintRule Sink(ArgSet &&SinkArgs,
                               std::optional<StringRef> Msg = std::nullopt) {
    return {std::move(SinkArgs), {}, {}, {}, Msg};
  }

  /// Make a rule that sanitizes all FilterArgs arguments.
  static GenericTaintRule Filter(ArgSet &&FilterArgs) {
    return {{}, std::move(FilterArgs), {}, {}};
  }

  /// Make a rule that unconditionally taints all Args.
  /// If Func is provided, it must also return true for taint to propagate.
  static GenericTaintRule Source(ArgSet &&SourceArgs) {
    return {{}, {}, {}, std::move(SourceArgs)};
  }

  /// Make a rule that taints all PropDstArgs if any of PropSrcArgs is tainted.
  static GenericTaintRule Prop(ArgSet &&SrcArgs, ArgSet &&DstArgs) {
    return {{}, {}, std::move(SrcArgs), std::move(DstArgs)};
  }

  /// Process a function which could either be a taint source, a taint sink, a
  /// taint filter or a taint propagator.
  void process(const GenericTaintChecker &Checker, const CallEvent &Call,
               CheckerContext &C) const;

  /// Handles the resolution of indexes of type ArgIdxTy to Expr*-s.
  static const Expr *GetArgExpr(ArgIdxTy ArgIdx, const CallEvent &Call) {
    return ArgIdx == ReturnValueIndex ? Call.getOriginExpr()
                                      : Call.getArgExpr(ArgIdx);
  };

  /// Functions for custom taintedness propagation.
  static bool UntrustedEnv(CheckerContext &C);
};

using RuleLookupTy = CallDescriptionMap<GenericTaintRule>;

/// Used to parse the configuration file.
struct TaintConfiguration {
  using NameScopeArgs = std::tuple<std::string, std::string, ArgVecTy>;
  enum class VariadicType { None, Src, Dst };

  struct Common {
    std::string Name;
    std::string Scope;
  };

  struct Sink : Common {
    ArgVecTy SinkArgs;
  };

  struct Filter : Common {
    ArgVecTy FilterArgs;
  };

  struct Propagation : Common {
    ArgVecTy SrcArgs;
    ArgVecTy DstArgs;
    VariadicType VarType;
    ArgIdxTy VarIndex;
  };

  std::vector<Propagation> Propagations;
  std::vector<Filter> Filters;
  std::vector<Sink> Sinks;

  TaintConfiguration() = default;
  TaintConfiguration(const TaintConfiguration &) = default;
  TaintConfiguration(TaintConfiguration &&) = default;
  TaintConfiguration &operator=(const TaintConfiguration &) = default;
  TaintConfiguration &operator=(TaintConfiguration &&) = default;
};

struct GenericTaintRuleParser {
  GenericTaintRuleParser(CheckerManager &Mgr) : Mgr(Mgr) {}
  /// Container type used to gather call identification objects grouped into
  /// pairs with their corresponding taint rules. It is temporary as it is used
  /// to finally initialize RuleLookupTy, which is considered to be immutable.
  using RulesContTy = std::vector<std::pair<CallDescription, GenericTaintRule>>;
  RulesContTy parseConfiguration(const std::string &Option,
                                 TaintConfiguration &&Config) const;

private:
  using NamePartsTy = llvm::SmallVector<StringRef, 2>;

  /// Validate part of the configuration, which contains a list of argument
  /// indexes.
  void validateArgVector(const std::string &Option, const ArgVecTy &Args) const;

  template <typename Config> static NamePartsTy parseNameParts(const Config &C);

  // Takes the config and creates a CallDescription for it and associates a Rule
  // with that.
  template <typename Config>
  static void consumeRulesFromConfig(const Config &C, GenericTaintRule &&Rule,
                                     RulesContTy &Rules);

  void parseConfig(const std::string &Option, TaintConfiguration::Sink &&P,
                   RulesContTy &Rules) const;
  void parseConfig(const std::string &Option, TaintConfiguration::Filter &&P,
                   RulesContTy &Rules) const;
  void parseConfig(const std::string &Option,
                   TaintConfiguration::Propagation &&P,
                   RulesContTy &Rules) const;

  CheckerManager &Mgr;
};

class GenericTaintChecker : public Checker<check::PreCall, check::PostCall> {
public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;

  void printState(raw_ostream &Out, ProgramStateRef State, const char *NL,
                  const char *Sep) const override;

  /// Generate a report if the expression is tainted or points to tainted data.
  bool generateReportIfTainted(const Expr *E, StringRef Msg,
                               CheckerContext &C) const;

  bool isTaintReporterCheckerEnabled = false;
  std::optional<BugType> BT;

private:
  bool checkUncontrolledFormatString(const CallEvent &Call,
                                     CheckerContext &C) const;

  void taintUnsafeSocketProtocol(const CallEvent &Call,
                                 CheckerContext &C) const;

  /// The taint rules are initalized with the help of a CheckerContext to
  /// access user-provided configuration.
  void initTaintRules(CheckerContext &C) const;

  // TODO: The two separate `CallDescriptionMap`s were introduced when
  // `CallDescription` was unable to restrict matches to the global namespace
  // only. This limitation no longer exists, so the following two maps should
  // be unified.
  mutable std::optional<RuleLookupTy> StaticTaintRules;
  mutable std::optional<RuleLookupTy> DynamicTaintRules;
};
} // end of anonymous namespace

/// YAML serialization mapping.
LLVM_YAML_IS_SEQUENCE_VECTOR(TaintConfiguration::Sink)
LLVM_YAML_IS_SEQUENCE_VECTOR(TaintConfiguration::Filter)
LLVM_YAML_IS_SEQUENCE_VECTOR(TaintConfiguration::Propagation)

namespace llvm {
namespace yaml {
template <> struct MappingTraits<TaintConfiguration> {
  static void mapping(IO &IO, TaintConfiguration &Config) {
    IO.mapOptional("Propagations", Config.Propagations);
    IO.mapOptional("Filters", Config.Filters);
    IO.mapOptional("Sinks", Config.Sinks);
  }
};

template <> struct MappingTraits<TaintConfiguration::Sink> {
  static void mapping(IO &IO, TaintConfiguration::Sink &Sink) {
    IO.mapRequired("Name", Sink.Name);
    IO.mapOptional("Scope", Sink.Scope);
    IO.mapRequired("Args", Sink.SinkArgs);
  }
};

template <> struct MappingTraits<TaintConfiguration::Filter> {
  static void mapping(IO &IO, TaintConfiguration::Filter &Filter) {
    IO.mapRequired("Name", Filter.Name);
    IO.mapOptional("Scope", Filter.Scope);
    IO.mapRequired("Args", Filter.FilterArgs);
  }
};

template <> struct MappingTraits<TaintConfiguration::Propagation> {
  static void mapping(IO &IO, TaintConfiguration::Propagation &Propagation) {
    IO.mapRequired("Name", Propagation.Name);
    IO.mapOptional("Scope", Propagation.Scope);
    IO.mapOptional("SrcArgs", Propagation.SrcArgs);
    IO.mapOptional("DstArgs", Propagation.DstArgs);
    IO.mapOptional("VariadicType", Propagation.VarType);
    IO.mapOptional("VariadicIndex", Propagation.VarIndex);
  }
};

template <> struct ScalarEnumerationTraits<TaintConfiguration::VariadicType> {
  static void enumeration(IO &IO, TaintConfiguration::VariadicType &Value) {
    IO.enumCase(Value, "None", TaintConfiguration::VariadicType::None);
    IO.enumCase(Value, "Src", TaintConfiguration::VariadicType::Src);
    IO.enumCase(Value, "Dst", TaintConfiguration::VariadicType::Dst);
  }
};
} // namespace yaml
} // namespace llvm

/// A set which is used to pass information from call pre-visit instruction
/// to the call post-visit. The values are signed integers, which are either
/// ReturnValueIndex, or indexes of the pointer/reference argument, which
/// points to data, which should be tainted on return.
REGISTER_MAP_WITH_PROGRAMSTATE(TaintArgsOnPostVisit, const LocationContext *,
                               ImmutableSet<ArgIdxTy>)
REGISTER_SET_FACTORY_WITH_PROGRAMSTATE(ArgIdxFactory, ArgIdxTy)

void GenericTaintRuleParser::validateArgVector(const std::string &Option,
                                               const ArgVecTy &Args) const {
  for (ArgIdxTy Arg : Args) {
    if (Arg < ReturnValueIndex) {
      Mgr.reportInvalidCheckerOptionValue(
          Mgr.getChecker<GenericTaintChecker>(), Option,
          "an argument number for propagation rules greater or equal to -1");
    }
  }
}

template <typename Config>
GenericTaintRuleParser::NamePartsTy
GenericTaintRuleParser::parseNameParts(const Config &C) {
  NamePartsTy NameParts;
  if (!C.Scope.empty()) {
    // If the Scope argument contains multiple "::" parts, those are considered
    // namespace identifiers.
    StringRef{C.Scope}.split(NameParts, "::", /*MaxSplit*/ -1,
                             /*KeepEmpty*/ false);
  }
  NameParts.emplace_back(C.Name);
  return NameParts;
}

template <typename Config>
void GenericTaintRuleParser::consumeRulesFromConfig(const Config &C,
                                                    GenericTaintRule &&Rule,
                                                    RulesContTy &Rules) {
  NamePartsTy NameParts = parseNameParts(C);
  Rules.emplace_back(CallDescription(CDM::Unspecified, NameParts),
                     std::move(Rule));
}

void GenericTaintRuleParser::parseConfig(const std::string &Option,
                                         TaintConfiguration::Sink &&S,
                                         RulesContTy &Rules) const {
  validateArgVector(Option, S.SinkArgs);
  consumeRulesFromConfig(S, GenericTaintRule::Sink(std::move(S.SinkArgs)),
                         Rules);
}

void GenericTaintRuleParser::parseConfig(const std::string &Option,
                                         TaintConfiguration::Filter &&S,
                                         RulesContTy &Rules) const {
  validateArgVector(Option, S.FilterArgs);
  consumeRulesFromConfig(S, GenericTaintRule::Filter(std::move(S.FilterArgs)),
                         Rules);
}

void GenericTaintRuleParser::parseConfig(const std::string &Option,
                                         TaintConfiguration::Propagation &&P,
                                         RulesContTy &Rules) const {
  validateArgVector(Option, P.SrcArgs);
  validateArgVector(Option, P.DstArgs);
  bool IsSrcVariadic = P.VarType == TaintConfiguration::VariadicType::Src;
  bool IsDstVariadic = P.VarType == TaintConfiguration::VariadicType::Dst;
  std::optional<ArgIdxTy> JustVarIndex = P.VarIndex;

  ArgSet SrcDesc(std::move(P.SrcArgs),
                 IsSrcVariadic ? JustVarIndex : std::nullopt);
  ArgSet DstDesc(std::move(P.DstArgs),
                 IsDstVariadic ? JustVarIndex : std::nullopt);

  consumeRulesFromConfig(
      P, GenericTaintRule::Prop(std::move(SrcDesc), std::move(DstDesc)), Rules);
}

GenericTaintRuleParser::RulesContTy
GenericTaintRuleParser::parseConfiguration(const std::string &Option,
                                           TaintConfiguration &&Config) const {

  RulesContTy Rules;

  for (auto &F : Config.Filters)
    parseConfig(Option, std::move(F), Rules);

  for (auto &S : Config.Sinks)
    parseConfig(Option, std::move(S), Rules);

  for (auto &P : Config.Propagations)
    parseConfig(Option, std::move(P), Rules);

  return Rules;
}

void GenericTaintChecker::initTaintRules(CheckerContext &C) const {
  // Check for exact name match for functions without builtin substitutes.
  // Use qualified name, because these are C functions without namespace.

  if (StaticTaintRules || DynamicTaintRules)
    return;

  using RulesConstructionTy =
      std::vector<std::pair<CallDescription, GenericTaintRule>>;
  using TR = GenericTaintRule;

  RulesConstructionTy GlobalCRules{
      // Sources
      {{CDM::CLibrary, {"fdopen"}}, TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"fopen"}}, TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"freopen"}}, TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"getch"}}, TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"getchar"}}, TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"getchar_unlocked"}}, TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"gets"}}, TR::Source({{0, ReturnValueIndex}})},
      {{CDM::CLibrary, {"gets_s"}}, TR::Source({{0, ReturnValueIndex}})},
      {{CDM::CLibrary, {"scanf"}}, TR::Source({{}, 1})},
      {{CDM::CLibrary, {"scanf_s"}}, TR::Source({{}, 1})},
      {{CDM::CLibrary, {"wgetch"}}, TR::Source({{ReturnValueIndex}})},
      // Sometimes the line between taint sources and propagators is blurry.
      // _IO_getc is choosen to be a source, but could also be a propagator.
      // This way it is simpler, as modeling it as a propagator would require
      // to model the possible sources of _IO_FILE * values, which the _IO_getc
      // function takes as parameters.
      {{CDM::CLibrary, {"_IO_getc"}}, TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"getcwd"}}, TR::Source({{0, ReturnValueIndex}})},
      {{CDM::CLibrary, {"getwd"}}, TR::Source({{0, ReturnValueIndex}})},
      {{CDM::CLibrary, {"readlink"}}, TR::Source({{1, ReturnValueIndex}})},
      {{CDM::CLibrary, {"readlinkat"}}, TR::Source({{2, ReturnValueIndex}})},
      {{CDM::CLibrary, {"get_current_dir_name"}},
       TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"gethostname"}}, TR::Source({{0}})},
      {{CDM::CLibrary, {"getnameinfo"}}, TR::Source({{2, 4}})},
      {{CDM::CLibrary, {"getseuserbyname"}}, TR::Source({{1, 2}})},
      {{CDM::CLibrary, {"getgroups"}}, TR::Source({{1, ReturnValueIndex}})},
      {{CDM::CLibrary, {"getlogin"}}, TR::Source({{ReturnValueIndex}})},
      {{CDM::CLibrary, {"getlogin_r"}}, TR::Source({{0}})},

      // Props
      {{CDM::CLibrary, {"accept"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"atoi"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"atol"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"atoll"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"fgetc"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"fgetln"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"fgets"}},
       TR::Prop({{2}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"fgetws"}},
       TR::Prop({{2}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibrary, {"fscanf"}}, TR::Prop({{0}}, {{}, 2})},
      {{CDM::CLibrary, {"fscanf_s"}}, TR::Prop({{0}}, {{}, 2})},
      {{CDM::CLibrary, {"sscanf"}}, TR::Prop({{0}}, {{}, 2})},
      {{CDM::CLibrary, {"sscanf_s"}}, TR::Prop({{0}}, {{}, 2})},

      {{CDM::CLibrary, {"getc"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"getc_unlocked"}},
       TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"getdelim"}}, TR::Prop({{3}}, {{0}})},
      // TODO: this intends to match the C function `getline()`, but the call
      // description also matches the C++ function `std::getline()`; it should
      // be ruled out by some additional logic.
      {{CDM::CLibrary, {"getline"}}, TR::Prop({{2}}, {{0}})},
      {{CDM::CLibrary, {"getw"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"pread"}},
       TR::Prop({{0, 1, 2, 3}}, {{1, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"read"}},
       TR::Prop({{0, 2}}, {{1, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"fread"}},
       TR::Prop({{3}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"recv"}},
       TR::Prop({{0}}, {{1, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"recvfrom"}},
       TR::Prop({{0}}, {{1, ReturnValueIndex}})},

      {{CDM::CLibrary, {"ttyname"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"ttyname_r"}},
       TR::Prop({{0}}, {{1, ReturnValueIndex}})},

      {{CDM::CLibrary, {"basename"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"dirname"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"fnmatch"}}, TR::Prop({{1}}, {{ReturnValueIndex}})},

      {{CDM::CLibrary, {"mbtowc"}}, TR::Prop({{1}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibrary, {"wctomb"}}, TR::Prop({{1}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibrary, {"wcwidth"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},

      {{CDM::CLibrary, {"memcmp"}},
       TR::Prop({{0, 1, 2}}, {{ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"memcpy"}},
       TR::Prop({{1, 2}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"memmove"}},
       TR::Prop({{1, 2}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"bcopy"}}, TR::Prop({{0, 2}}, {{1}})},

      // Note: "memmem" and its variants search for a byte sequence ("needle")
      // in a larger area ("haystack"). Currently we only propagate taint from
      // the haystack to the result, but in theory tampering with the needle
      // could also produce incorrect results.
      {{CDM::CLibrary, {"memmem"}}, TR::Prop({{0, 1}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strstr"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strcasestr"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},

      // Analogously, the following functions search for a byte within a buffer
      // and we only propagate taint from the buffer to the result.
      {{CDM::CLibraryMaybeHardened, {"memchr"}},
       TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"memrchr"}},
       TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"rawmemchr"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"strchr"}},
       TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"strrchr"}},
       TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"strchrnul"}},
       TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"index"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"rindex"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},

      // FIXME: In case of arrays, only the first element of the array gets
      // tainted.
      {{CDM::CLibrary, {"qsort"}}, TR::Prop({{0}}, {{0}})},
      {{CDM::CLibrary, {"qsort_r"}}, TR::Prop({{0}}, {{0}})},

      {{CDM::CLibrary, {"strcmp"}}, TR::Prop({{0, 1}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strcasecmp"}},
       TR::Prop({{0, 1}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strncmp"}},
       TR::Prop({{0, 1, 2}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strncasecmp"}},
       TR::Prop({{0, 1, 2}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strspn"}}, TR::Prop({{0, 1}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strcspn"}}, TR::Prop({{0, 1}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strpbrk"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},

      {{CDM::CLibrary, {"strndup"}}, TR::Prop({{0, 1}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strndupa"}}, TR::Prop({{0, 1}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strdup"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"strdupa"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"wcsdup"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},

      // strlen, wcslen, strnlen and alike intentionally don't propagate taint.
      // See the details here: https://github.com/llvm/llvm-project/pull/66086

      {{CDM::CLibrary, {"strtol"}}, TR::Prop({{0}}, {{1, ReturnValueIndex}})},
      {{CDM::CLibrary, {"strtoll"}}, TR::Prop({{0}}, {{1, ReturnValueIndex}})},
      {{CDM::CLibrary, {"strtoul"}}, TR::Prop({{0}}, {{1, ReturnValueIndex}})},
      {{CDM::CLibrary, {"strtoull"}}, TR::Prop({{0}}, {{1, ReturnValueIndex}})},

      {{CDM::CLibrary, {"tolower"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"toupper"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},

      {{CDM::CLibrary, {"isalnum"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isalpha"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isascii"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isblank"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"iscntrl"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isdigit"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isgraph"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"islower"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isprint"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"ispunct"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isspace"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isupper"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},
      {{CDM::CLibrary, {"isxdigit"}}, TR::Prop({{0}}, {{ReturnValueIndex}})},

      {{CDM::CLibraryMaybeHardened, {"strcpy"}},
       TR::Prop({{1}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"stpcpy"}},
       TR::Prop({{1}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"strcat"}},
       TR::Prop({{0, 1}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"wcsncat"}},
       TR::Prop({{0, 1}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"strncpy"}},
       TR::Prop({{1, 2}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"strncat"}},
       TR::Prop({{0, 1, 2}}, {{0, ReturnValueIndex}})},
      {{CDM::CLibraryMaybeHardened, {"strlcpy"}}, TR::Prop({{1, 2}}, {{0}})},
      {{CDM::CLibraryMaybeHardened, {"strlcat"}}, TR::Prop({{0, 1, 2}}, {{0}})},

      // Usually the matching mode `CDM::CLibraryMaybeHardened` is sufficient
      // for unified handling of a function `FOO()` and its hardened variant
      // `__FOO_chk()`, but in the "sprintf" family the extra parameters of the
      // hardened variants are inserted into the middle of the parameter list,
      // so that would not work in their case.
      // int snprintf(char * str, size_t maxlen, const char * format, ...);
      {{CDM::CLibrary, {"snprintf"}},
       TR::Prop({{1, 2}, 3}, {{0, ReturnValueIndex}})},
      // int sprintf(char * str, const char * format, ...);
      {{CDM::CLibrary, {"sprintf"}},
       TR::Prop({{1}, 2}, {{0, ReturnValueIndex}})},
      // int __snprintf_chk(char * str, size_t maxlen, int flag, size_t strlen,
      //                    const char * format, ...);
      {{CDM::CLibrary, {"__snprintf_chk"}},
       TR::Prop({{1, 4}, 5}, {{0, ReturnValueIndex}})},
      // int __sprintf_chk(char * str, int flag, size_t strlen, const char *
      //                   format, ...);
      {{CDM::CLibrary, {"__sprintf_chk"}},
       TR::Prop({{3}, 4}, {{0, ReturnValueIndex}})},

      // Sinks
      {{CDM::CLibrary, {"system"}}, TR::Sink({{0}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"popen"}}, TR::Sink({{0}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"execl"}}, TR::Sink({{}, {0}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"execle"}}, TR::Sink({{}, {0}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"execlp"}}, TR::Sink({{}, {0}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"execv"}}, TR::Sink({{0, 1}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"execve"}},
       TR::Sink({{0, 1, 2}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"fexecve"}},
       TR::Sink({{0, 1, 2}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"execvp"}}, TR::Sink({{0, 1}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"execvpe"}},
       TR::Sink({{0, 1, 2}}, MsgSanitizeSystemArgs)},
      {{CDM::CLibrary, {"dlopen"}}, TR::Sink({{0}}, MsgSanitizeSystemArgs)},

      // malloc, calloc, alloca, realloc, memccpy
      // are intentionally not marked as taint sinks because unconditional
      // reporting for these functions generates many false positives.
      // These taint sinks should be implemented in other checkers with more
      // sophisticated sanitation heuristics.

      {{CDM::CLibrary, {"setproctitle"}},
       TR::Sink({{0}, 1}, MsgUncontrolledFormatString)},
      {{CDM::CLibrary, {"setproctitle_fast"}},
       TR::Sink({{0}, 1}, MsgUncontrolledFormatString)}};

  if (TR::UntrustedEnv(C)) {
    // void setproctitle_init(int argc, char *argv[], char *envp[])
    // TODO: replace `MsgCustomSink` with a message that fits this situation.
    GlobalCRules.push_back({{CDM::CLibrary, {"setproctitle_init"}},
                            TR::Sink({{1, 2}}, MsgCustomSink)});

    // `getenv` returns taint only in untrusted environments.
    GlobalCRules.push_back(
        {{CDM::CLibrary, {"getenv"}}, TR::Source({{ReturnValueIndex}})});
  }

  StaticTaintRules.emplace(std::make_move_iterator(GlobalCRules.begin()),
                           std::make_move_iterator(GlobalCRules.end()));

  // User-provided taint configuration.
  CheckerManager *Mgr = C.getAnalysisManager().getCheckerManager();
  assert(Mgr);
  GenericTaintRuleParser ConfigParser{*Mgr};
  std::string Option{"Config"};
  StringRef ConfigFile =
      Mgr->getAnalyzerOptions().getCheckerStringOption(this, Option);
  std::optional<TaintConfiguration> Config =
      getConfiguration<TaintConfiguration>(*Mgr, this, Option, ConfigFile);
  if (!Config) {
    // We don't have external taint config, no parsing required.
    DynamicTaintRules = RuleLookupTy{};
    return;
  }

  GenericTaintRuleParser::RulesContTy Rules{
      ConfigParser.parseConfiguration(Option, std::move(*Config))};

  DynamicTaintRules.emplace(std::make_move_iterator(Rules.begin()),
                            std::make_move_iterator(Rules.end()));
}

void GenericTaintChecker::checkPreCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  initTaintRules(C);

  // FIXME: this should be much simpler.
  if (const auto *Rule =
          Call.isGlobalCFunction() ? StaticTaintRules->lookup(Call) : nullptr)
    Rule->process(*this, Call, C);
  else if (const auto *Rule = DynamicTaintRules->lookup(Call))
    Rule->process(*this, Call, C);

  // FIXME: These edge cases are to be eliminated from here eventually.
  //
  // Additional check that is not supported by CallDescription.
  // TODO: Make CallDescription be able to match attributes such as printf-like
  // arguments.
  checkUncontrolledFormatString(Call, C);

  // TODO: Modeling sockets should be done in a specific checker.
  // Socket is a source, which taints the return value.
  taintUnsafeSocketProtocol(Call, C);
}

void GenericTaintChecker::checkPostCall(const CallEvent &Call,
                                        CheckerContext &C) const {
  // Set the marked values as tainted. The return value only accessible from
  // checkPostStmt.
  ProgramStateRef State = C.getState();
  const StackFrameContext *CurrentFrame = C.getStackFrame();

  // Depending on what was tainted at pre-visit, we determined a set of
  // arguments which should be tainted after the function returns. These are
  // stored in the state as TaintArgsOnPostVisit set.
  TaintArgsOnPostVisitTy TaintArgsMap = State->get<TaintArgsOnPostVisit>();

  const ImmutableSet<ArgIdxTy> *TaintArgs = TaintArgsMap.lookup(CurrentFrame);
  if (!TaintArgs)
    return;
  assert(!TaintArgs->isEmpty());

  LLVM_DEBUG(for (ArgIdxTy I
                  : *TaintArgs) {
    llvm::dbgs() << "PostCall<";
    Call.dump(llvm::dbgs());
    llvm::dbgs() << "> actually wants to taint arg index: " << I << '\n';
  });

  const NoteTag *InjectionTag = nullptr;
  std::vector<SymbolRef> TaintedSymbols;
  std::vector<ArgIdxTy> TaintedIndexes;
  for (ArgIdxTy ArgNum : *TaintArgs) {
    // Special handling for the tainted return value.
    if (ArgNum == ReturnValueIndex) {
      State = addTaint(State, Call.getReturnValue());
      std::vector<SymbolRef> TaintedSyms =
          getTaintedSymbols(State, Call.getReturnValue());
      if (!TaintedSyms.empty()) {
        TaintedSymbols.push_back(TaintedSyms[0]);
        TaintedIndexes.push_back(ArgNum);
      }
      continue;
    }
    // The arguments are pointer arguments. The data they are pointing at is
    // tainted after the call.
    if (auto V = getPointeeOf(State, Call.getArgSVal(ArgNum))) {
      State = addTaint(State, *V);
      std::vector<SymbolRef> TaintedSyms = getTaintedSymbols(State, *V);
      if (!TaintedSyms.empty()) {
        TaintedSymbols.push_back(TaintedSyms[0]);
        TaintedIndexes.push_back(ArgNum);
      }
    }
  }
  // Create a NoteTag callback, which prints to the user where the taintedness
  // was propagated to.
  InjectionTag = taintPropagationExplainerTag(C, TaintedSymbols, TaintedIndexes,
                                              Call.getCalleeStackFrame(0));
  // Clear up the taint info from the state.
  State = State->remove<TaintArgsOnPostVisit>(CurrentFrame);
  C.addTransition(State, InjectionTag);
}

void GenericTaintChecker::printState(raw_ostream &Out, ProgramStateRef State,
                                     const char *NL, const char *Sep) const {
  printTaint(State, Out, NL, Sep);
}

void GenericTaintRule::process(const GenericTaintChecker &Checker,
                               const CallEvent &Call, CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const ArgIdxTy CallNumArgs = fromArgumentCount(Call.getNumArgs());

  /// Iterate every call argument, and get their corresponding Expr and SVal.
  const auto ForEachCallArg = [&C, &Call, CallNumArgs](auto &&Fun) {
    for (ArgIdxTy I = ReturnValueIndex; I < CallNumArgs; ++I) {
      const Expr *E = GetArgExpr(I, Call);
      Fun(I, E, C.getSVal(E));
    }
  };

  /// Check for taint sinks.
  ForEachCallArg([this, &Checker, &C, &State](ArgIdxTy I, const Expr *E, SVal) {
    // Add taintedness to stdin parameters
    if (isStdin(C.getSVal(E), C.getASTContext())) {
      State = addTaint(State, C.getSVal(E));
    }
    if (SinkArgs.contains(I) && isTaintedOrPointsToTainted(State, C.getSVal(E)))
      Checker.generateReportIfTainted(E, SinkMsg.value_or(MsgCustomSink), C);
  });

  /// Check for taint filters.
  ForEachCallArg([this, &State](ArgIdxTy I, const Expr *E, SVal S) {
    if (FilterArgs.contains(I)) {
      State = removeTaint(State, S);
      if (auto P = getPointeeOf(State, S))
        State = removeTaint(State, *P);
    }
  });

  /// Check for taint propagation sources.
  /// A rule will make the destination variables tainted if PropSrcArgs
  /// is empty (taints the destination
  /// arguments unconditionally), or if any of its signified
  /// args are tainted in context of the current CallEvent.
  bool IsMatching = PropSrcArgs.isEmpty();
  std::vector<SymbolRef> TaintedSymbols;
  std::vector<ArgIdxTy> TaintedIndexes;
  ForEachCallArg([this, &C, &IsMatching, &State, &TaintedSymbols,
                  &TaintedIndexes](ArgIdxTy I, const Expr *E, SVal) {
    std::optional<SVal> TaintedSVal =
        getTaintedPointeeOrPointer(State, C.getSVal(E));
    IsMatching =
        IsMatching || (PropSrcArgs.contains(I) && TaintedSVal.has_value());

    // We track back tainted arguments except for stdin
    if (TaintedSVal && !isStdin(*TaintedSVal, C.getASTContext())) {
      std::vector<SymbolRef> TaintedArgSyms =
          getTaintedSymbols(State, *TaintedSVal);
      if (!TaintedArgSyms.empty()) {
        llvm::append_range(TaintedSymbols, TaintedArgSyms);
        TaintedIndexes.push_back(I);
      }
    }
  });

  // Early return for propagation rules which dont match.
  // Matching propagations, Sinks and Filters will pass this point.
  if (!IsMatching)
    return;

  const auto WouldEscape = [](SVal V, QualType Ty) -> bool {
    if (!isa<Loc>(V))
      return false;

    const bool IsNonConstRef = Ty->isReferenceType() && !Ty.isConstQualified();
    const bool IsNonConstPtr =
        Ty->isPointerType() && !Ty->getPointeeType().isConstQualified();

    return IsNonConstRef || IsNonConstPtr;
  };

  /// Propagate taint where it is necessary.
  auto &F = State->getStateManager().get_context<ArgIdxFactory>();
  ImmutableSet<ArgIdxTy> Result = F.getEmptySet();
  ForEachCallArg(
      [&](ArgIdxTy I, const Expr *E, SVal V) {
        if (PropDstArgs.contains(I)) {
          LLVM_DEBUG(llvm::dbgs() << "PreCall<"; Call.dump(llvm::dbgs());
                     llvm::dbgs()
                     << "> prepares tainting arg index: " << I << '\n';);
          Result = F.add(Result, I);
        }

        // Taint property gets lost if the variable is passed as a
        // non-const pointer or reference to a function which is
        // not inlined. For matching rules we want to preserve the taintedness.
        // TODO: We should traverse all reachable memory regions via the
        // escaping parameter. Instead of doing that we simply mark only the
        // referred memory region as tainted.
        if (WouldEscape(V, E->getType()) && getTaintedPointeeOrPointer(State, V)) {
          LLVM_DEBUG(if (!Result.contains(I)) {
            llvm::dbgs() << "PreCall<";
            Call.dump(llvm::dbgs());
            llvm::dbgs() << "> prepares tainting arg index: " << I << '\n';
          });
          Result = F.add(Result, I);
        }
      });

  if (!Result.isEmpty())
    State = State->set<TaintArgsOnPostVisit>(C.getStackFrame(), Result);
  const NoteTag *InjectionTag = taintOriginTrackerTag(
      C, std::move(TaintedSymbols), std::move(TaintedIndexes),
      Call.getCalleeStackFrame(0));
  C.addTransition(State, InjectionTag);
}

bool GenericTaintRule::UntrustedEnv(CheckerContext &C) {
  return !C.getAnalysisManager()
              .getAnalyzerOptions()
              .ShouldAssumeControlledEnvironment;
}

bool GenericTaintChecker::generateReportIfTainted(const Expr *E, StringRef Msg,
                                                  CheckerContext &C) const {
  assert(E);
  if (!isTaintReporterCheckerEnabled)
    return false;
  std::optional<SVal> TaintedSVal =
      getTaintedPointeeOrPointer(C.getState(), C.getSVal(E));

  if (!TaintedSVal)
    return false;

  // Generate diagnostic.
  assert(BT);
  static CheckerProgramPointTag Tag(BT->getCheckerName(), Msg);
  if (ExplodedNode *N = C.generateNonFatalErrorNode(C.getState(), &Tag)) {
    auto report = std::make_unique<PathSensitiveBugReport>(*BT, Msg, N);
    report->addRange(E->getSourceRange());
    for (auto TaintedSym : getTaintedSymbols(C.getState(), *TaintedSVal)) {
      report->markInteresting(TaintedSym);
    }
    C.emitReport(std::move(report));
    return true;
  }
  return false;
}

/// TODO: remove checking for printf format attributes and socket whitelisting
/// from GenericTaintChecker, and that means the following functions:
/// getPrintfFormatArgumentNum,
/// GenericTaintChecker::checkUncontrolledFormatString,
/// GenericTaintChecker::taintUnsafeSocketProtocol

static bool getPrintfFormatArgumentNum(const CallEvent &Call,
                                       const CheckerContext &C,
                                       ArgIdxTy &ArgNum) {
  // Find if the function contains a format string argument.
  // Handles: fprintf, printf, sprintf, snprintf, vfprintf, vprintf, vsprintf,
  // vsnprintf, syslog, custom annotated functions.
  const Decl *CallDecl = Call.getDecl();
  if (!CallDecl)
    return false;
  const FunctionDecl *FDecl = CallDecl->getAsFunction();
  if (!FDecl)
    return false;

  const ArgIdxTy CallNumArgs = fromArgumentCount(Call.getNumArgs());

  for (const auto *Format : FDecl->specific_attrs<FormatAttr>()) {
    ArgNum = Format->getFormatIdx() - 1;
    if ((Format->getType()->getName() == "printf") && CallNumArgs > ArgNum)
      return true;
  }

  return false;
}

bool GenericTaintChecker::checkUncontrolledFormatString(
    const CallEvent &Call, CheckerContext &C) const {
  // Check if the function contains a format string argument.
  ArgIdxTy ArgNum = 0;
  if (!getPrintfFormatArgumentNum(Call, C, ArgNum))
    return false;

  // If either the format string content or the pointer itself are tainted,
  // warn.
  return generateReportIfTainted(Call.getArgExpr(ArgNum),
                                 MsgUncontrolledFormatString, C);
}

void GenericTaintChecker::taintUnsafeSocketProtocol(const CallEvent &Call,
                                                    CheckerContext &C) const {
  if (Call.getNumArgs() < 1)
    return;
  const IdentifierInfo *ID = Call.getCalleeIdentifier();
  if (!ID)
    return;
  if (ID->getName() != "socket")
    return;

  SourceLocation DomLoc = Call.getArgExpr(0)->getExprLoc();
  StringRef DomName = C.getMacroNameOrSpelling(DomLoc);
  // Allow internal communication protocols.
  bool SafeProtocol = DomName == "AF_SYSTEM" || DomName == "AF_LOCAL" ||
                      DomName == "AF_UNIX" || DomName == "AF_RESERVED_36";
  if (SafeProtocol)
    return;

  ProgramStateRef State = C.getState();
  auto &F = State->getStateManager().get_context<ArgIdxFactory>();
  ImmutableSet<ArgIdxTy> Result = F.add(F.getEmptySet(), ReturnValueIndex);
  State = State->set<TaintArgsOnPostVisit>(C.getStackFrame(), Result);
  C.addTransition(State);
}

/// Checker registration
void ento::registerTaintPropagationChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<GenericTaintChecker>();
}

bool ento::shouldRegisterTaintPropagationChecker(const CheckerManager &mgr) {
  return true;
}

void ento::registerGenericTaintChecker(CheckerManager &Mgr) {
  GenericTaintChecker *checker = Mgr.getChecker<GenericTaintChecker>();
  checker->isTaintReporterCheckerEnabled = true;
  checker->BT.emplace(Mgr.getCurrentCheckerName(), "Use of Untrusted Data",
                      categories::TaintedData);
}

bool ento::shouldRegisterGenericTaintChecker(const CheckerManager &mgr) {
  return true;
}
