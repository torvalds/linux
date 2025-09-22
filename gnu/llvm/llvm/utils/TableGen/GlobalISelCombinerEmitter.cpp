//===- GlobalISelCombinerMatchTableEmitter.cpp - --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Generate a combiner implementation for GlobalISel from a declarative
/// syntax using GlobalISelMatchTable.
///
/// Usually, TableGen backends use "assert is an error" as a means to report
/// invalid input. They try to diagnose common case but don't try very hard and
/// crashes can be common. This backend aims to behave closer to how a language
/// compiler frontend would behave: we try extra hard to diagnose invalid inputs
/// early, and any crash should be considered a bug (= a feature or diagnostic
/// is missing).
///
/// While this can make the backend a bit more complex than it needs to be, it
/// pays off because MIR patterns can get complicated. Giving useful error
/// messages to combine writers can help boost their productivity.
///
/// As with anything, a good balance has to be found. We also don't want to
/// write hundreds of lines of code to detect edge cases. In practice, crashing
/// very occasionally, or giving poor errors in some rare instances, is fine.
///
//===----------------------------------------------------------------------===//

#include "Basic/CodeGenIntrinsics.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "Common/GlobalISel/CXXPredicates.h"
#include "Common/GlobalISel/CodeExpander.h"
#include "Common/GlobalISel/CodeExpansions.h"
#include "Common/GlobalISel/CombinerUtils.h"
#include "Common/GlobalISel/GlobalISelMatchTable.h"
#include "Common/GlobalISel/GlobalISelMatchTableExecutorEmitter.h"
#include "Common/GlobalISel/PatternParser.h"
#include "Common/GlobalISel/Patterns.h"
#include "Common/SubtargetFeatureInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cstdint>

using namespace llvm;
using namespace llvm::gi;

#define DEBUG_TYPE "gicombiner-emitter"

namespace {
cl::OptionCategory
    GICombinerEmitterCat("Options for -gen-global-isel-combiner");
cl::opt<bool> StopAfterParse(
    "gicombiner-stop-after-parse",
    cl::desc("Stop processing after parsing rules and dump state"),
    cl::cat(GICombinerEmitterCat));
cl::list<std::string>
    SelectedCombiners("combiners", cl::desc("Emit the specified combiners"),
                      cl::cat(GICombinerEmitterCat), cl::CommaSeparated);
cl::opt<bool> DebugCXXPreds(
    "gicombiner-debug-cxxpreds",
    cl::desc("Add Contextual/Debug comments to all C++ predicates"),
    cl::cat(GICombinerEmitterCat));
cl::opt<bool> DebugTypeInfer("gicombiner-debug-typeinfer",
                             cl::desc("Print type inference debug logs"),
                             cl::cat(GICombinerEmitterCat));

constexpr StringLiteral CXXCustomActionPrefix = "GICXXCustomAction_";
constexpr StringLiteral CXXPredPrefix = "GICXXPred_MI_Predicate_";
constexpr StringLiteral MatchDataClassName = "GIDefMatchData";

//===- CodeExpansions Helpers  --------------------------------------------===//

void declareInstExpansion(CodeExpansions &CE, const InstructionMatcher &IM,
                          StringRef Name) {
  CE.declare(Name, "State.MIs[" + to_string(IM.getInsnVarID()) + "]");
}

void declareInstExpansion(CodeExpansions &CE, const BuildMIAction &A,
                          StringRef Name) {
  // Note: we use redeclare here because this may overwrite a matcher inst
  // expansion.
  CE.redeclare(Name, "OutMIs[" + to_string(A.getInsnID()) + "]");
}

void declareOperandExpansion(CodeExpansions &CE, const OperandMatcher &OM,
                             StringRef Name) {
  CE.declare(Name, "State.MIs[" + to_string(OM.getInsnVarID()) +
                       "]->getOperand(" + to_string(OM.getOpIdx()) + ")");
}

void declareTempRegExpansion(CodeExpansions &CE, unsigned TempRegID,
                             StringRef Name) {
  CE.declare(Name, "State.TempRegisters[" + to_string(TempRegID) + "]");
}

//===- Misc. Helpers  -----------------------------------------------------===//

template <typename Container> auto keys(Container &&C) {
  return map_range(C, [](auto &Entry) -> auto & { return Entry.first; });
}

template <typename Container> auto values(Container &&C) {
  return map_range(C, [](auto &Entry) -> auto & { return Entry.second; });
}

std::string getIsEnabledPredicateEnumName(unsigned CombinerRuleID) {
  return "GICXXPred_Simple_IsRule" + to_string(CombinerRuleID) + "Enabled";
}

//===- MatchTable Helpers  ------------------------------------------------===//

LLTCodeGen getLLTCodeGen(const PatternType &PT) {
  return *MVTToLLT(getValueType(PT.getLLTRecord()));
}

LLTCodeGenOrTempType getLLTCodeGenOrTempType(const PatternType &PT,
                                             RuleMatcher &RM) {
  assert(!PT.isNone());

  if (PT.isLLT())
    return getLLTCodeGen(PT);

  assert(PT.isTypeOf());
  auto &OM = RM.getOperandMatcher(PT.getTypeOfOpName());
  return OM.getTempTypeIdx(RM);
}

//===- PrettyStackTrace Helpers  ------------------------------------------===//

class PrettyStackTraceParse : public PrettyStackTraceEntry {
  const Record &Def;

public:
  PrettyStackTraceParse(const Record &Def) : Def(Def) {}

  void print(raw_ostream &OS) const override {
    if (Def.isSubClassOf("GICombineRule"))
      OS << "Parsing GICombineRule '" << Def.getName() << "'";
    else if (Def.isSubClassOf(PatFrag::ClassName))
      OS << "Parsing " << PatFrag::ClassName << " '" << Def.getName() << "'";
    else
      OS << "Parsing '" << Def.getName() << "'";
    OS << '\n';
  }
};

class PrettyStackTraceEmit : public PrettyStackTraceEntry {
  const Record &Def;
  const Pattern *Pat = nullptr;

public:
  PrettyStackTraceEmit(const Record &Def, const Pattern *Pat = nullptr)
      : Def(Def), Pat(Pat) {}

  void print(raw_ostream &OS) const override {
    if (Def.isSubClassOf("GICombineRule"))
      OS << "Emitting GICombineRule '" << Def.getName() << "'";
    else if (Def.isSubClassOf(PatFrag::ClassName))
      OS << "Emitting " << PatFrag::ClassName << " '" << Def.getName() << "'";
    else
      OS << "Emitting '" << Def.getName() << "'";

    if (Pat)
      OS << " [" << Pat->getKindName() << " '" << Pat->getName() << "']";
    OS << '\n';
  }
};

//===- CombineRuleOperandTypeChecker --------------------------------------===//

/// This is a wrapper around OperandTypeChecker specialized for Combiner Rules.
/// On top of doing the same things as OperandTypeChecker, this also attempts to
/// infer as many types as possible for temporary register defs & immediates in
/// apply patterns.
///
/// The inference is trivial and leverages the MCOI OperandTypes encoded in
/// CodeGenInstructions to infer types across patterns in a CombineRule. It's
/// thus very limited and only supports CodeGenInstructions (but that's the main
/// use case so it's fine).
///
/// We only try to infer untyped operands in apply patterns when they're temp
/// reg defs, or immediates. Inference always outputs a `TypeOf<$x>` where $x is
/// a named operand from a match pattern.
class CombineRuleOperandTypeChecker : private OperandTypeChecker {
public:
  CombineRuleOperandTypeChecker(const Record &RuleDef,
                                const OperandTable &MatchOpTable)
      : OperandTypeChecker(RuleDef.getLoc()), RuleDef(RuleDef),
        MatchOpTable(MatchOpTable) {}

  /// Records and checks a 'match' pattern.
  bool processMatchPattern(InstructionPattern &P);

  /// Records and checks an 'apply' pattern.
  bool processApplyPattern(InstructionPattern &P);

  /// Propagates types, then perform type inference and do a second round of
  /// propagation in the apply patterns only if any types were inferred.
  void propagateAndInferTypes();

private:
  /// TypeEquivalenceClasses are groups of operands of an instruction that share
  /// a common type.
  ///
  /// e.g. [[a, b], [c, d]] means a and b have the same type, and c and
  /// d have the same type too. b/c and a/d don't have to have the same type,
  /// though.
  using TypeEquivalenceClasses = EquivalenceClasses<StringRef>;

  /// \returns true for `OPERAND_GENERIC_` 0 through 5.
  /// These are the MCOI types that can be registers. The other MCOI types are
  /// either immediates, or fancier operands used only post-ISel, so we don't
  /// care about them for combiners.
  static bool canMCOIOperandTypeBeARegister(StringRef MCOIType) {
    // Assume OPERAND_GENERIC_0 through 5 can be registers. The other MCOI
    // OperandTypes are either never used in gMIR, or not relevant (e.g.
    // OPERAND_GENERIC_IMM, which is definitely never a register).
    return MCOIType.drop_back(1).ends_with("OPERAND_GENERIC_");
  }

  /// Finds the "MCOI::"" operand types for each operand of \p CGP.
  ///
  /// This is a bit trickier than it looks because we need to handle variadic
  /// in/outs.
  ///
  /// e.g. for
  ///   (G_BUILD_VECTOR $vec, $x, $y) ->
  ///   [MCOI::OPERAND_GENERIC_0, MCOI::OPERAND_GENERIC_1,
  ///    MCOI::OPERAND_GENERIC_1]
  ///
  /// For unknown types (which can happen in variadics where varargs types are
  /// inconsistent), a unique name is given, e.g. "unknown_type_0".
  static std::vector<std::string>
  getMCOIOperandTypes(const CodeGenInstructionPattern &CGP);

  /// Adds the TypeEquivalenceClasses for \p P in \p OutTECs.
  void getInstEqClasses(const InstructionPattern &P,
                        TypeEquivalenceClasses &OutTECs) const;

  /// Calls `getInstEqClasses` on all patterns of the rule to produce the whole
  /// rule's TypeEquivalenceClasses.
  TypeEquivalenceClasses getRuleEqClasses() const;

  /// Tries to infer the type of the \p ImmOpIdx -th operand of \p IP using \p
  /// TECs.
  ///
  /// This is achieved by trying to find a named operand in \p IP that shares
  /// the same type as \p ImmOpIdx, and using \ref inferNamedOperandType on that
  /// operand instead.
  ///
  /// \returns the inferred type or an empty PatternType if inference didn't
  /// succeed.
  PatternType inferImmediateType(const InstructionPattern &IP,
                                 unsigned ImmOpIdx,
                                 const TypeEquivalenceClasses &TECs) const;

  /// Looks inside \p TECs to infer \p OpName's type.
  ///
  /// \returns the inferred type or an empty PatternType if inference didn't
  /// succeed.
  PatternType inferNamedOperandType(const InstructionPattern &IP,
                                    StringRef OpName,
                                    const TypeEquivalenceClasses &TECs,
                                    bool AllowSelf = false) const;

  const Record &RuleDef;
  SmallVector<InstructionPattern *, 8> MatchPats;
  SmallVector<InstructionPattern *, 8> ApplyPats;

  const OperandTable &MatchOpTable;
};

bool CombineRuleOperandTypeChecker::processMatchPattern(InstructionPattern &P) {
  MatchPats.push_back(&P);
  return check(P, /*CheckTypeOf*/ [](const auto &) {
    // GITypeOf in 'match' is currently always rejected by the
    // CombineRuleBuilder after inference is done.
    return true;
  });
}

bool CombineRuleOperandTypeChecker::processApplyPattern(InstructionPattern &P) {
  ApplyPats.push_back(&P);
  return check(P, /*CheckTypeOf*/ [&](const PatternType &Ty) {
    // GITypeOf<"$x"> can only be used if "$x" is a matched operand.
    const auto OpName = Ty.getTypeOfOpName();
    if (MatchOpTable.lookup(OpName).Found)
      return true;

    PrintError(RuleDef.getLoc(), "'" + OpName + "' ('" + Ty.str() +
                                     "') does not refer to a matched operand!");
    return false;
  });
}

void CombineRuleOperandTypeChecker::propagateAndInferTypes() {
  /// First step here is to propagate types using the OperandTypeChecker. That
  /// way we ensure all uses of a given register have consistent types.
  propagateTypes();

  /// Build the TypeEquivalenceClasses for the whole rule.
  const TypeEquivalenceClasses TECs = getRuleEqClasses();

  /// Look at the apply patterns and find operands that need to be
  /// inferred. We then try to find an equivalence class that they're a part of
  /// and select the best operand to use for the `GITypeOf` type. We prioritize
  /// defs of matched instructions because those are guaranteed to be registers.
  bool InferredAny = false;
  for (auto *Pat : ApplyPats) {
    for (unsigned K = 0; K < Pat->operands_size(); ++K) {
      auto &Op = Pat->getOperand(K);

      // We only want to take a look at untyped defs or immediates.
      if ((!Op.isDef() && !Op.hasImmValue()) || Op.getType())
        continue;

      // Infer defs & named immediates.
      if (Op.isDef() || Op.isNamedImmediate()) {
        // Check it's not a redefinition of a matched operand.
        // In such cases, inference is not necessary because we just copy
        // operands and don't create temporary registers.
        if (MatchOpTable.lookup(Op.getOperandName()).Found)
          continue;

        // Inference is needed here, so try to do it.
        if (PatternType Ty =
                inferNamedOperandType(*Pat, Op.getOperandName(), TECs)) {
          if (DebugTypeInfer)
            errs() << "INFER: " << Op.describe() << " -> " << Ty.str() << '\n';
          Op.setType(Ty);
          InferredAny = true;
        }

        continue;
      }

      // Infer immediates
      if (Op.hasImmValue()) {
        if (PatternType Ty = inferImmediateType(*Pat, K, TECs)) {
          if (DebugTypeInfer)
            errs() << "INFER: " << Op.describe() << " -> " << Ty.str() << '\n';
          Op.setType(Ty);
          InferredAny = true;
        }
        continue;
      }
    }
  }

  // If we've inferred any types, we want to propagate them across the apply
  // patterns. Type inference only adds GITypeOf types that point to Matched
  // operands, so we definitely don't want to propagate types into the match
  // patterns as well, otherwise bad things happen.
  if (InferredAny) {
    OperandTypeChecker OTC(RuleDef.getLoc());
    for (auto *Pat : ApplyPats) {
      if (!OTC.check(*Pat, [&](const auto &) { return true; }))
        PrintFatalError(RuleDef.getLoc(),
                        "OperandTypeChecker unexpectedly failed on '" +
                            Pat->getName() + "' during Type Inference");
    }
    OTC.propagateTypes();

    if (DebugTypeInfer) {
      errs() << "Apply patterns for rule " << RuleDef.getName()
             << " after inference:\n";
      for (auto *Pat : ApplyPats) {
        errs() << "  ";
        Pat->print(errs(), /*PrintName*/ true);
        errs() << '\n';
      }
      errs() << '\n';
    }
  }
}

PatternType CombineRuleOperandTypeChecker::inferImmediateType(
    const InstructionPattern &IP, unsigned ImmOpIdx,
    const TypeEquivalenceClasses &TECs) const {
  // We can only infer CGPs (except intrinsics).
  const auto *CGP = dyn_cast<CodeGenInstructionPattern>(&IP);
  if (!CGP || CGP->isIntrinsic())
    return {};

  // For CGPs, we try to infer immediates by trying to infer another named
  // operand that shares its type.
  //
  // e.g.
  //    Pattern: G_BUILD_VECTOR $x, $y, 0
  //    MCOIs:   [MCOI::OPERAND_GENERIC_0, MCOI::OPERAND_GENERIC_1,
  //              MCOI::OPERAND_GENERIC_1]
  //    $y has the same type as 0, so we can infer $y and get the type 0 should
  //    have.

  // We infer immediates by looking for a named operand that shares the same
  // MCOI type.
  const auto MCOITypes = getMCOIOperandTypes(*CGP);
  StringRef ImmOpTy = MCOITypes[ImmOpIdx];

  for (const auto &[Idx, Ty] : enumerate(MCOITypes)) {
    if (Idx != ImmOpIdx && Ty == ImmOpTy) {
      const auto &Op = IP.getOperand(Idx);
      if (!Op.isNamedOperand())
        continue;

      // Named operand with the same name, try to infer that.
      if (PatternType InferTy = inferNamedOperandType(IP, Op.getOperandName(),
                                                      TECs, /*AllowSelf=*/true))
        return InferTy;
    }
  }

  return {};
}

PatternType CombineRuleOperandTypeChecker::inferNamedOperandType(
    const InstructionPattern &IP, StringRef OpName,
    const TypeEquivalenceClasses &TECs, bool AllowSelf) const {
  // This is the simplest possible case, we just need to find a TEC that
  // contains OpName. Look at all operands in equivalence class and try to
  // find a suitable one. If `AllowSelf` is true, the operand itself is also
  // considered suitable.

  // Check for a def of a matched pattern. This is guaranteed to always
  // be a register so we can blindly use that.
  StringRef GoodOpName;
  for (auto It = TECs.findLeader(OpName); It != TECs.member_end(); ++It) {
    if (!AllowSelf && *It == OpName)
      continue;

    const auto LookupRes = MatchOpTable.lookup(*It);
    if (LookupRes.Def) // Favor defs
      return PatternType::getTypeOf(*It);

    // Otherwise just save this in case we don't find any def.
    if (GoodOpName.empty() && LookupRes.Found)
      GoodOpName = *It;
  }

  if (!GoodOpName.empty())
    return PatternType::getTypeOf(GoodOpName);

  // No good operand found, give up.
  return {};
}

std::vector<std::string> CombineRuleOperandTypeChecker::getMCOIOperandTypes(
    const CodeGenInstructionPattern &CGP) {
  // FIXME?: Should we cache this? We call it twice when inferring immediates.

  static unsigned UnknownTypeIdx = 0;

  std::vector<std::string> OpTypes;
  auto &CGI = CGP.getInst();
  Record *VarArgsTy = CGI.TheDef->isSubClassOf("GenericInstruction")
                          ? CGI.TheDef->getValueAsOptionalDef("variadicOpsType")
                          : nullptr;
  std::string VarArgsTyName =
      VarArgsTy ? ("MCOI::" + VarArgsTy->getValueAsString("OperandType")).str()
                : ("unknown_type_" + Twine(UnknownTypeIdx++)).str();

  // First, handle defs.
  for (unsigned K = 0; K < CGI.Operands.NumDefs; ++K)
    OpTypes.push_back(CGI.Operands[K].OperandType);

  // Then, handle variadic defs if there are any.
  if (CGP.hasVariadicDefs()) {
    for (unsigned K = CGI.Operands.NumDefs; K < CGP.getNumInstDefs(); ++K)
      OpTypes.push_back(VarArgsTyName);
  }

  // If we had variadic defs, the op idx in the pattern won't match the op idx
  // in the CGI anymore.
  int CGIOpOffset = int(CGI.Operands.NumDefs) - CGP.getNumInstDefs();
  assert(CGP.hasVariadicDefs() ? (CGIOpOffset <= 0) : (CGIOpOffset == 0));

  // Handle all remaining use operands, including variadic ones.
  for (unsigned K = CGP.getNumInstDefs(); K < CGP.getNumInstOperands(); ++K) {
    unsigned CGIOpIdx = K + CGIOpOffset;
    if (CGIOpIdx >= CGI.Operands.size()) {
      assert(CGP.isVariadic());
      OpTypes.push_back(VarArgsTyName);
    } else {
      OpTypes.push_back(CGI.Operands[CGIOpIdx].OperandType);
    }
  }

  assert(OpTypes.size() == CGP.operands_size());
  return OpTypes;
}

void CombineRuleOperandTypeChecker::getInstEqClasses(
    const InstructionPattern &P, TypeEquivalenceClasses &OutTECs) const {
  // Determine the TypeEquivalenceClasses by:
  //    - Getting the MCOI Operand Types.
  //    - Creating a Map of MCOI Type -> [Operand Indexes]
  //    - Iterating over the map, filtering types we don't like, and just adding
  //      the array of Operand Indexes to \p OutTECs.

  // We can only do this on CodeGenInstructions that aren't intrinsics. Other
  // InstructionPatterns have no type inference information associated with
  // them.
  // TODO: We could try to extract some info from CodeGenIntrinsic to
  //       guide inference.

  // TODO: Could we add some inference information to builtins at least? e.g.
  // ReplaceReg should always replace with a reg of the same type, for instance.
  // Though, those patterns are often used alone so it might not be worth the
  // trouble to infer their types.
  auto *CGP = dyn_cast<CodeGenInstructionPattern>(&P);
  if (!CGP || CGP->isIntrinsic())
    return;

  const auto MCOITypes = getMCOIOperandTypes(*CGP);
  assert(MCOITypes.size() == P.operands_size());

  MapVector<StringRef, SmallVector<unsigned, 0>> TyToOpIdx;
  for (const auto &[Idx, Ty] : enumerate(MCOITypes))
    TyToOpIdx[Ty].push_back(Idx);

  if (DebugTypeInfer)
    errs() << "\tGroups for " << P.getName() << ":\t";

  for (const auto &[Ty, Idxs] : TyToOpIdx) {
    if (!canMCOIOperandTypeBeARegister(Ty))
      continue;

    if (DebugTypeInfer)
      errs() << '[';
    StringRef Sep = "";

    // We only collect named operands.
    StringRef Leader;
    for (unsigned Idx : Idxs) {
      const auto &Op = P.getOperand(Idx);
      if (!Op.isNamedOperand())
        continue;

      const auto OpName = Op.getOperandName();
      if (DebugTypeInfer) {
        errs() << Sep << OpName;
        Sep = ", ";
      }

      if (Leader.empty())
        OutTECs.insert((Leader = OpName));
      else
        OutTECs.unionSets(Leader, OpName);
    }

    if (DebugTypeInfer)
      errs() << "] ";
  }

  if (DebugTypeInfer)
    errs() << '\n';
}

CombineRuleOperandTypeChecker::TypeEquivalenceClasses
CombineRuleOperandTypeChecker::getRuleEqClasses() const {
  StringMap<unsigned> OpNameToEqClassIdx;
  TypeEquivalenceClasses TECs;

  if (DebugTypeInfer)
    errs() << "Rule Operand Type Equivalence Classes for " << RuleDef.getName()
           << ":\n";

  for (const auto *Pat : MatchPats)
    getInstEqClasses(*Pat, TECs);
  for (const auto *Pat : ApplyPats)
    getInstEqClasses(*Pat, TECs);

  if (DebugTypeInfer) {
    errs() << "Final Type Equivalence Classes: ";
    for (auto ClassIt = TECs.begin(); ClassIt != TECs.end(); ++ClassIt) {
      // only print non-empty classes.
      if (auto MembIt = TECs.member_begin(ClassIt);
          MembIt != TECs.member_end()) {
        errs() << '[';
        StringRef Sep = "";
        for (; MembIt != TECs.member_end(); ++MembIt) {
          errs() << Sep << *MembIt;
          Sep = ", ";
        }
        errs() << "] ";
      }
    }
    errs() << '\n';
  }

  return TECs;
}

//===- MatchData Handling -------------------------------------------------===//
struct MatchDataDef {
  MatchDataDef(StringRef Symbol, StringRef Type) : Symbol(Symbol), Type(Type) {}

  StringRef Symbol;
  StringRef Type;

  /// \returns the desired variable name for this MatchData.
  std::string getVarName() const {
    // Add a prefix in case the symbol name is very generic and conflicts with
    // something else.
    return "GIMatchData_" + Symbol.str();
  }
};

//===- CombineRuleBuilder -------------------------------------------------===//

/// Parses combine rule and builds a small intermediate representation to tie
/// patterns together and emit RuleMatchers to match them. This may emit more
/// than one RuleMatcher, e.g. for `wip_match_opcode`.
///
/// Memory management for `Pattern` objects is done through `std::unique_ptr`.
/// In most cases, there are two stages to a pattern's lifetime:
///   - Creation in a `parse` function
///     - The unique_ptr is stored in a variable, and may be destroyed if the
///       pattern is found to be semantically invalid.
///   - Ownership transfer into a `PatternMap`
///     - Once a pattern is moved into either the map of Match or Apply
///       patterns, it is known to be valid and it never moves back.
class CombineRuleBuilder {
public:
  using PatternMap = MapVector<StringRef, std::unique_ptr<Pattern>>;
  using PatternAlternatives = DenseMap<const Pattern *, unsigned>;

  CombineRuleBuilder(const CodeGenTarget &CGT,
                     SubtargetFeatureInfoMap &SubtargetFeatures,
                     Record &RuleDef, unsigned ID,
                     std::vector<RuleMatcher> &OutRMs)
      : Parser(CGT, RuleDef.getLoc()), CGT(CGT),
        SubtargetFeatures(SubtargetFeatures), RuleDef(RuleDef), RuleID(ID),
        OutRMs(OutRMs) {}

  /// Parses all fields in the RuleDef record.
  bool parseAll();

  /// Emits all RuleMatchers into the vector of RuleMatchers passed in the
  /// constructor.
  bool emitRuleMatchers();

  void print(raw_ostream &OS) const;
  void dump() const { print(dbgs()); }

  /// Debug-only verification of invariants.
#ifndef NDEBUG
  void verify() const;
#endif

private:
  const CodeGenInstruction &getGConstant() const {
    return CGT.getInstruction(RuleDef.getRecords().getDef("G_CONSTANT"));
  }

  void PrintError(Twine Msg) const { ::PrintError(&RuleDef, Msg); }
  void PrintWarning(Twine Msg) const { ::PrintWarning(RuleDef.getLoc(), Msg); }
  void PrintNote(Twine Msg) const { ::PrintNote(RuleDef.getLoc(), Msg); }

  void print(raw_ostream &OS, const PatternAlternatives &Alts) const;

  bool addApplyPattern(std::unique_ptr<Pattern> Pat);
  bool addMatchPattern(std::unique_ptr<Pattern> Pat);

  /// Adds the expansions from \see MatchDatas to \p CE.
  void declareAllMatchDatasExpansions(CodeExpansions &CE) const;

  /// Adds a matcher \p P to \p IM, expanding its code using \p CE.
  /// Note that the predicate is added on the last InstructionMatcher.
  ///
  /// \p Alts is only used if DebugCXXPreds is enabled.
  void addCXXPredicate(RuleMatcher &M, const CodeExpansions &CE,
                       const CXXPattern &P, const PatternAlternatives &Alts);

  bool hasOnlyCXXApplyPatterns() const;
  bool hasEraseRoot() const;

  // Infer machine operand types and check their consistency.
  bool typecheckPatterns();

  /// For all PatFragPatterns, add a new entry in PatternAlternatives for each
  /// PatternList it contains. This is multiplicative, so if we have 2
  /// PatFrags with 3 alternatives each, we get 2*3 permutations added to
  /// PermutationsToEmit. The "MaxPermutations" field controls how many
  /// permutations are allowed before an error is emitted and this function
  /// returns false. This is a simple safeguard to prevent combination of
  /// PatFrags from generating enormous amounts of rules.
  bool buildPermutationsToEmit();

  /// Checks additional semantics of the Patterns.
  bool checkSemantics();

  /// Creates a new RuleMatcher with some boilerplate
  /// settings/actions/predicates, and and adds it to \p OutRMs.
  /// \see addFeaturePredicates too.
  ///
  /// \param Alts Current set of alternatives, for debug comment.
  /// \param AdditionalComment Comment string to be added to the
  ///        `DebugCommentAction`.
  RuleMatcher &addRuleMatcher(const PatternAlternatives &Alts,
                              Twine AdditionalComment = "");
  bool addFeaturePredicates(RuleMatcher &M);

  bool findRoots();
  bool buildRuleOperandsTable();

  bool parseDefs(const DagInit &Def);

  bool emitMatchPattern(CodeExpansions &CE, const PatternAlternatives &Alts,
                        const InstructionPattern &IP);
  bool emitMatchPattern(CodeExpansions &CE, const PatternAlternatives &Alts,
                        const AnyOpcodePattern &AOP);

  bool emitPatFragMatchPattern(CodeExpansions &CE,
                               const PatternAlternatives &Alts, RuleMatcher &RM,
                               InstructionMatcher *IM,
                               const PatFragPattern &PFP,
                               DenseSet<const Pattern *> &SeenPats);

  bool emitApplyPatterns(CodeExpansions &CE, RuleMatcher &M);
  bool emitCXXMatchApply(CodeExpansions &CE, RuleMatcher &M,
                         ArrayRef<CXXPattern *> Matchers);

  // Recursively visits InstructionPatterns from P to build up the
  // RuleMatcher actions.
  bool emitInstructionApplyPattern(CodeExpansions &CE, RuleMatcher &M,
                                   const InstructionPattern &P,
                                   DenseSet<const Pattern *> &SeenPats,
                                   StringMap<unsigned> &OperandToTempRegID);

  bool emitCodeGenInstructionApplyImmOperand(RuleMatcher &M,
                                             BuildMIAction &DstMI,
                                             const CodeGenInstructionPattern &P,
                                             const InstructionOperand &O);

  bool emitBuiltinApplyPattern(CodeExpansions &CE, RuleMatcher &M,
                               const BuiltinPattern &P,
                               StringMap<unsigned> &OperandToTempRegID);

  // Recursively visits CodeGenInstructionPattern from P to build up the
  // RuleMatcher/InstructionMatcher. May create new InstructionMatchers as
  // needed.
  using OperandMapperFnRef =
      function_ref<InstructionOperand(const InstructionOperand &)>;
  using OperandDefLookupFn =
      function_ref<const InstructionPattern *(StringRef)>;
  bool emitCodeGenInstructionMatchPattern(
      CodeExpansions &CE, const PatternAlternatives &Alts, RuleMatcher &M,
      InstructionMatcher &IM, const CodeGenInstructionPattern &P,
      DenseSet<const Pattern *> &SeenPats, OperandDefLookupFn LookupOperandDef,
      OperandMapperFnRef OperandMapper = [](const auto &O) { return O; });

  PatternParser Parser;
  const CodeGenTarget &CGT;
  SubtargetFeatureInfoMap &SubtargetFeatures;
  Record &RuleDef;
  const unsigned RuleID;
  std::vector<RuleMatcher> &OutRMs;

  // For InstructionMatcher::addOperand
  unsigned AllocatedTemporariesBaseID = 0;

  /// The root of the pattern.
  StringRef RootName;

  /// These maps have ownership of the actual Pattern objects.
  /// They both map a Pattern's name to the Pattern instance.
  PatternMap MatchPats;
  PatternMap ApplyPats;

  /// Operand tables to tie match/apply patterns together.
  OperandTable MatchOpTable;
  OperandTable ApplyOpTable;

  /// Set by findRoots.
  Pattern *MatchRoot = nullptr;
  SmallDenseSet<InstructionPattern *, 2> ApplyRoots;

  SmallVector<MatchDataDef, 2> MatchDatas;
  SmallVector<PatternAlternatives, 1> PermutationsToEmit;
};

bool CombineRuleBuilder::parseAll() {
  auto StackTrace = PrettyStackTraceParse(RuleDef);

  if (!parseDefs(*RuleDef.getValueAsDag("Defs")))
    return false;

  if (!Parser.parsePatternList(
          *RuleDef.getValueAsDag("Match"),
          [this](auto Pat) { return addMatchPattern(std::move(Pat)); }, "match",
          (RuleDef.getName() + "_match").str()))
    return false;

  if (!Parser.parsePatternList(
          *RuleDef.getValueAsDag("Apply"),
          [this](auto Pat) { return addApplyPattern(std::move(Pat)); }, "apply",
          (RuleDef.getName() + "_apply").str()))
    return false;

  if (!buildRuleOperandsTable() || !typecheckPatterns() || !findRoots() ||
      !checkSemantics() || !buildPermutationsToEmit())
    return false;
  LLVM_DEBUG(verify());
  return true;
}

bool CombineRuleBuilder::emitRuleMatchers() {
  auto StackTrace = PrettyStackTraceEmit(RuleDef);

  assert(MatchRoot);
  CodeExpansions CE;

  assert(!PermutationsToEmit.empty());
  for (const auto &Alts : PermutationsToEmit) {
    switch (MatchRoot->getKind()) {
    case Pattern::K_AnyOpcode: {
      if (!emitMatchPattern(CE, Alts, *cast<AnyOpcodePattern>(MatchRoot)))
        return false;
      break;
    }
    case Pattern::K_PatFrag:
    case Pattern::K_Builtin:
    case Pattern::K_CodeGenInstruction:
      if (!emitMatchPattern(CE, Alts, *cast<InstructionPattern>(MatchRoot)))
        return false;
      break;
    case Pattern::K_CXX:
      PrintError("C++ code cannot be the root of a rule!");
      return false;
    default:
      llvm_unreachable("unknown pattern kind!");
    }
  }

  return true;
}

void CombineRuleBuilder::print(raw_ostream &OS) const {
  OS << "(CombineRule name:" << RuleDef.getName() << " id:" << RuleID
     << " root:" << RootName << '\n';

  if (!MatchDatas.empty()) {
    OS << "  (MatchDatas\n";
    for (const auto &MD : MatchDatas) {
      OS << "    (MatchDataDef symbol:" << MD.Symbol << " type:" << MD.Type
         << ")\n";
    }
    OS << "  )\n";
  }

  const auto &SeenPFs = Parser.getSeenPatFrags();
  if (!SeenPFs.empty()) {
    OS << "  (PatFrags\n";
    for (const auto *PF : Parser.getSeenPatFrags()) {
      PF->print(OS, /*Indent=*/"    ");
      OS << '\n';
    }
    OS << "  )\n";
  }

  const auto DumpPats = [&](StringRef Name, const PatternMap &Pats) {
    OS << "  (" << Name << " ";
    if (Pats.empty()) {
      OS << "<empty>)\n";
      return;
    }

    OS << '\n';
    for (const auto &[Name, Pat] : Pats) {
      OS << "    ";
      if (Pat.get() == MatchRoot)
        OS << "<match_root>";
      if (isa<InstructionPattern>(Pat.get()) &&
          ApplyRoots.contains(cast<InstructionPattern>(Pat.get())))
        OS << "<apply_root>";
      OS << Name << ":";
      Pat->print(OS, /*PrintName=*/false);
      OS << '\n';
    }
    OS << "  )\n";
  };

  DumpPats("MatchPats", MatchPats);
  DumpPats("ApplyPats", ApplyPats);

  MatchOpTable.print(OS, "MatchPats", /*Indent*/ "  ");
  ApplyOpTable.print(OS, "ApplyPats", /*Indent*/ "  ");

  if (PermutationsToEmit.size() > 1) {
    OS << "  (PermutationsToEmit\n";
    for (const auto &Perm : PermutationsToEmit) {
      OS << "    ";
      print(OS, Perm);
      OS << ",\n";
    }
    OS << "  )\n";
  }

  OS << ")\n";
}

#ifndef NDEBUG
void CombineRuleBuilder::verify() const {
  const auto VerifyPats = [&](const PatternMap &Pats) {
    for (const auto &[Name, Pat] : Pats) {
      if (!Pat)
        PrintFatalError("null pattern in pattern map!");

      if (Name != Pat->getName()) {
        Pat->dump();
        PrintFatalError("Pattern name mismatch! Map name: " + Name +
                        ", Pat name: " + Pat->getName());
      }

      // Sanity check: the map should point to the same data as the Pattern.
      // Both strings are allocated in the pool using insertStrRef.
      if (Name.data() != Pat->getName().data()) {
        dbgs() << "Map StringRef: '" << Name << "' @ "
               << (const void *)Name.data() << '\n';
        dbgs() << "Pat String: '" << Pat->getName() << "' @ "
               << (const void *)Pat->getName().data() << '\n';
        PrintFatalError("StringRef stored in the PatternMap is not referencing "
                        "the same string as its Pattern!");
      }
    }
  };

  VerifyPats(MatchPats);
  VerifyPats(ApplyPats);

  // Check there are no wip_match_opcode patterns in the "apply" patterns.
  if (any_of(ApplyPats,
             [&](auto &E) { return isa<AnyOpcodePattern>(E.second.get()); })) {
    dump();
    PrintFatalError(
        "illegal wip_match_opcode pattern in the 'apply' patterns!");
  }

  // Check there are no nullptrs in ApplyRoots.
  if (ApplyRoots.contains(nullptr)) {
    PrintFatalError(
        "CombineRuleBuilder's ApplyRoots set contains a null pointer!");
  }
}
#endif

void CombineRuleBuilder::print(raw_ostream &OS,
                               const PatternAlternatives &Alts) const {
  SmallVector<std::string, 1> Strings(
      map_range(Alts, [](const auto &PatAndPerm) {
        return PatAndPerm.first->getName().str() + "[" +
               to_string(PatAndPerm.second) + "]";
      }));
  // Sort so output is deterministic for tests. Otherwise it's sorted by pointer
  // values.
  sort(Strings);
  OS << "[" << join(Strings, ", ") << "]";
}

bool CombineRuleBuilder::addApplyPattern(std::unique_ptr<Pattern> Pat) {
  StringRef Name = Pat->getName();
  if (ApplyPats.contains(Name)) {
    PrintError("'" + Name + "' apply pattern defined more than once!");
    return false;
  }

  if (isa<AnyOpcodePattern>(Pat.get())) {
    PrintError("'" + Name +
               "': wip_match_opcode is not supported in apply patterns");
    return false;
  }

  if (isa<PatFragPattern>(Pat.get())) {
    PrintError("'" + Name + "': using " + PatFrag::ClassName +
               " is not supported in apply patterns");
    return false;
  }

  if (auto *CXXPat = dyn_cast<CXXPattern>(Pat.get()))
    CXXPat->setIsApply();

  ApplyPats[Name] = std::move(Pat);
  return true;
}

bool CombineRuleBuilder::addMatchPattern(std::unique_ptr<Pattern> Pat) {
  StringRef Name = Pat->getName();
  if (MatchPats.contains(Name)) {
    PrintError("'" + Name + "' match pattern defined more than once!");
    return false;
  }

  // For now, none of the builtins can appear in 'match'.
  if (const auto *BP = dyn_cast<BuiltinPattern>(Pat.get())) {
    PrintError("'" + BP->getInstName() +
               "' cannot be used in a 'match' pattern");
    return false;
  }

  MatchPats[Name] = std::move(Pat);
  return true;
}

void CombineRuleBuilder::declareAllMatchDatasExpansions(
    CodeExpansions &CE) const {
  for (const auto &MD : MatchDatas)
    CE.declare(MD.Symbol, MD.getVarName());
}

void CombineRuleBuilder::addCXXPredicate(RuleMatcher &M,
                                         const CodeExpansions &CE,
                                         const CXXPattern &P,
                                         const PatternAlternatives &Alts) {
  // FIXME: Hack so C++ code is executed last. May not work for more complex
  // patterns.
  auto &IM = *std::prev(M.insnmatchers().end());
  auto Loc = RuleDef.getLoc();
  const auto AddComment = [&](raw_ostream &OS) {
    OS << "// Pattern Alternatives: ";
    print(OS, Alts);
    OS << '\n';
  };
  const auto &ExpandedCode =
      DebugCXXPreds ? P.expandCode(CE, Loc, AddComment) : P.expandCode(CE, Loc);
  IM->addPredicate<GenericInstructionPredicateMatcher>(
      ExpandedCode.getEnumNameWithPrefix(CXXPredPrefix));
}

bool CombineRuleBuilder::hasOnlyCXXApplyPatterns() const {
  return all_of(ApplyPats, [&](auto &Entry) {
    return isa<CXXPattern>(Entry.second.get());
  });
}

bool CombineRuleBuilder::hasEraseRoot() const {
  return any_of(ApplyPats, [&](auto &Entry) {
    if (const auto *BP = dyn_cast<BuiltinPattern>(Entry.second.get()))
      return BP->getBuiltinKind() == BI_EraseRoot;
    return false;
  });
}

bool CombineRuleBuilder::typecheckPatterns() {
  CombineRuleOperandTypeChecker OTC(RuleDef, MatchOpTable);

  for (auto &Pat : values(MatchPats)) {
    if (auto *IP = dyn_cast<InstructionPattern>(Pat.get())) {
      if (!OTC.processMatchPattern(*IP))
        return false;
    }
  }

  for (auto &Pat : values(ApplyPats)) {
    if (auto *IP = dyn_cast<InstructionPattern>(Pat.get())) {
      if (!OTC.processApplyPattern(*IP))
        return false;
    }
  }

  OTC.propagateAndInferTypes();

  // Always check this after in case inference adds some special types to the
  // match patterns.
  for (auto &Pat : values(MatchPats)) {
    if (auto *IP = dyn_cast<InstructionPattern>(Pat.get())) {
      if (IP->diagnoseAllSpecialTypes(
              RuleDef.getLoc(), PatternType::SpecialTyClassName +
                                    " is not supported in 'match' patterns")) {
        return false;
      }
    }
  }
  return true;
}

bool CombineRuleBuilder::buildPermutationsToEmit() {
  PermutationsToEmit.clear();

  // Start with one empty set of alternatives.
  PermutationsToEmit.emplace_back();
  for (const auto &Pat : values(MatchPats)) {
    unsigned NumAlts = 0;
    // Note: technically, AnyOpcodePattern also needs permutations, but:
    //    - We only allow a single one of them in the root.
    //    - They cannot be mixed with any other pattern other than C++ code.
    // So we don't really need to take them into account here. We could, but
    // that pattern is a hack anyway and the less it's involved, the better.
    if (const auto *PFP = dyn_cast<PatFragPattern>(Pat.get()))
      NumAlts = PFP->getPatFrag().num_alternatives();
    else
      continue;

    // For each pattern that needs permutations, multiply the current set of
    // alternatives.
    auto CurPerms = PermutationsToEmit;
    PermutationsToEmit.clear();

    for (const auto &Perm : CurPerms) {
      assert(!Perm.count(Pat.get()) && "Pattern already emitted?");
      for (unsigned K = 0; K < NumAlts; ++K) {
        PatternAlternatives NewPerm = Perm;
        NewPerm[Pat.get()] = K;
        PermutationsToEmit.emplace_back(std::move(NewPerm));
      }
    }
  }

  if (int64_t MaxPerms = RuleDef.getValueAsInt("MaxPermutations");
      MaxPerms > 0) {
    if ((int64_t)PermutationsToEmit.size() > MaxPerms) {
      PrintError("cannot emit rule '" + RuleDef.getName() + "'; " +
                 Twine(PermutationsToEmit.size()) +
                 " permutations would be emitted, but the max is " +
                 Twine(MaxPerms));
      return false;
    }
  }

  // Ensure we always have a single empty entry, it simplifies the emission
  // logic so it doesn't need to handle the case where there are no perms.
  if (PermutationsToEmit.empty()) {
    PermutationsToEmit.emplace_back();
    return true;
  }

  return true;
}

bool CombineRuleBuilder::checkSemantics() {
  assert(MatchRoot && "Cannot call this before findRoots()");

  bool UsesWipMatchOpcode = false;
  for (const auto &Match : MatchPats) {
    const auto *Pat = Match.second.get();

    if (const auto *CXXPat = dyn_cast<CXXPattern>(Pat)) {
      if (!CXXPat->getRawCode().contains("return "))
        PrintWarning("'match' C++ code does not seem to return!");
      continue;
    }

    // MIFlags in match cannot use the following syntax: (MIFlags $mi)
    if (const auto *CGP = dyn_cast<CodeGenInstructionPattern>(Pat)) {
      if (auto *FI = CGP->getMIFlagsInfo()) {
        if (!FI->copy_flags().empty()) {
          PrintError(
              "'match' patterns cannot refer to flags from other instructions");
          PrintNote("MIFlags in '" + CGP->getName() +
                    "' refer to: " + join(FI->copy_flags(), ", "));
          return false;
        }
      }
    }

    const auto *AOP = dyn_cast<AnyOpcodePattern>(Pat);
    if (!AOP)
      continue;

    if (UsesWipMatchOpcode) {
      PrintError("wip_opcode_match can only be present once");
      return false;
    }

    UsesWipMatchOpcode = true;
  }

  std::optional<bool> IsUsingCXXPatterns;
  for (const auto &Apply : ApplyPats) {
    Pattern *Pat = Apply.second.get();
    if (IsUsingCXXPatterns) {
      if (*IsUsingCXXPatterns != isa<CXXPattern>(Pat)) {
        PrintError("'apply' patterns cannot mix C++ code with other types of "
                   "patterns");
        return false;
      }
    } else
      IsUsingCXXPatterns = isa<CXXPattern>(Pat);

    assert(Pat);
    const auto *IP = dyn_cast<InstructionPattern>(Pat);
    if (!IP)
      continue;

    if (UsesWipMatchOpcode) {
      PrintError("cannot use wip_match_opcode in combination with apply "
                 "instruction patterns!");
      return false;
    }

    // Check that the insts mentioned in copy_flags exist.
    if (const auto *CGP = dyn_cast<CodeGenInstructionPattern>(IP)) {
      if (auto *FI = CGP->getMIFlagsInfo()) {
        for (auto InstName : FI->copy_flags()) {
          auto It = MatchPats.find(InstName);
          if (It == MatchPats.end()) {
            PrintError("unknown instruction '$" + InstName +
                       "' referenced in MIFlags of '" + CGP->getName() + "'");
            return false;
          }

          if (!isa<CodeGenInstructionPattern>(It->second.get())) {
            PrintError(
                "'$" + InstName +
                "' does not refer to a CodeGenInstruction in MIFlags of '" +
                CGP->getName() + "'");
            return false;
          }
        }
      }
    }

    const auto *BIP = dyn_cast<BuiltinPattern>(IP);
    if (!BIP)
      continue;
    StringRef Name = BIP->getInstName();

    // (GIEraseInst) has to be the only apply pattern, or it can not be used at
    // all. The root cannot have any defs either.
    switch (BIP->getBuiltinKind()) {
    case BI_EraseRoot: {
      if (ApplyPats.size() > 1) {
        PrintError(Name + " must be the only 'apply' pattern");
        return false;
      }

      const auto *IRoot = dyn_cast<CodeGenInstructionPattern>(MatchRoot);
      if (!IRoot) {
        PrintError(Name + " can only be used if the root is a "
                          "CodeGenInstruction or Intrinsic");
        return false;
      }

      if (IRoot->getNumInstDefs() != 0) {
        PrintError(Name + " can only be used if on roots that do "
                          "not have any output operand");
        PrintNote("'" + IRoot->getInstName() + "' has " +
                  Twine(IRoot->getNumInstDefs()) + " output operands");
        return false;
      }
      break;
    }
    case BI_ReplaceReg: {
      // (GIReplaceReg can only be used on the root instruction)
      // TODO: When we allow rewriting non-root instructions, also allow this.
      StringRef OldRegName = BIP->getOperand(0).getOperandName();
      auto *Def = MatchOpTable.getDef(OldRegName);
      if (!Def) {
        PrintError(Name + " cannot find a matched pattern that defines '" +
                   OldRegName + "'");
        return false;
      }
      if (MatchOpTable.getDef(OldRegName) != MatchRoot) {
        PrintError(Name + " cannot replace '" + OldRegName +
                   "': this builtin can only replace a register defined by the "
                   "match root");
        return false;
      }
      break;
    }
    }
  }

  if (!hasOnlyCXXApplyPatterns() && !MatchDatas.empty()) {
    PrintError(MatchDataClassName +
               " can only be used if 'apply' in entirely written in C++");
    return false;
  }

  return true;
}

RuleMatcher &CombineRuleBuilder::addRuleMatcher(const PatternAlternatives &Alts,
                                                Twine AdditionalComment) {
  auto &RM = OutRMs.emplace_back(RuleDef.getLoc());
  addFeaturePredicates(RM);
  RM.setPermanentGISelFlags(GISF_IgnoreCopies);
  RM.addRequiredSimplePredicate(getIsEnabledPredicateEnumName(RuleID));

  std::string Comment;
  raw_string_ostream CommentOS(Comment);
  CommentOS << "Combiner Rule #" << RuleID << ": " << RuleDef.getName();
  if (!Alts.empty()) {
    CommentOS << " @ ";
    print(CommentOS, Alts);
  }
  if (!AdditionalComment.isTriviallyEmpty())
    CommentOS << "; " << AdditionalComment;
  RM.addAction<DebugCommentAction>(Comment);
  return RM;
}

bool CombineRuleBuilder::addFeaturePredicates(RuleMatcher &M) {
  if (!RuleDef.getValue("Predicates"))
    return true;

  ListInit *Preds = RuleDef.getValueAsListInit("Predicates");
  for (Init *PI : Preds->getValues()) {
    DefInit *Pred = dyn_cast<DefInit>(PI);
    if (!Pred)
      continue;

    Record *Def = Pred->getDef();
    if (!Def->isSubClassOf("Predicate")) {
      ::PrintError(Def, "Unknown 'Predicate' Type");
      return false;
    }

    if (Def->getValueAsString("CondString").empty())
      continue;

    if (SubtargetFeatures.count(Def) == 0) {
      SubtargetFeatures.emplace(
          Def, SubtargetFeatureInfo(Def, SubtargetFeatures.size()));
    }

    M.addRequiredFeature(Def);
  }

  return true;
}

bool CombineRuleBuilder::findRoots() {
  const auto Finish = [&]() {
    assert(MatchRoot);

    if (hasOnlyCXXApplyPatterns() || hasEraseRoot())
      return true;

    auto *IPRoot = dyn_cast<InstructionPattern>(MatchRoot);
    if (!IPRoot)
      return true;

    if (IPRoot->getNumInstDefs() == 0) {
      // No defs to work with -> find the root using the pattern name.
      auto It = ApplyPats.find(RootName);
      if (It == ApplyPats.end()) {
        PrintError("Cannot find root '" + RootName + "' in apply patterns!");
        return false;
      }

      auto *ApplyRoot = dyn_cast<InstructionPattern>(It->second.get());
      if (!ApplyRoot) {
        PrintError("apply pattern root '" + RootName +
                   "' must be an instruction pattern");
        return false;
      }

      ApplyRoots.insert(ApplyRoot);
      return true;
    }

    // Collect all redefinitions of the MatchRoot's defs and put them in
    // ApplyRoots.
    const auto DefsNeeded = IPRoot->getApplyDefsNeeded();
    for (auto &Op : DefsNeeded) {
      assert(Op.isDef() && Op.isNamedOperand());
      StringRef Name = Op.getOperandName();

      auto *ApplyRedef = ApplyOpTable.getDef(Name);
      if (!ApplyRedef) {
        PrintError("'" + Name + "' must be redefined in the 'apply' pattern");
        return false;
      }

      ApplyRoots.insert((InstructionPattern *)ApplyRedef);
    }

    if (auto It = ApplyPats.find(RootName); It != ApplyPats.end()) {
      if (find(ApplyRoots, It->second.get()) == ApplyRoots.end()) {
        PrintError("apply pattern '" + RootName +
                   "' is supposed to be a root but it does not redefine any of "
                   "the defs of the match root");
        return false;
      }
    }

    return true;
  };

  // Look by pattern name, e.g.
  //    (G_FNEG $x, $y):$root
  if (auto MatchPatIt = MatchPats.find(RootName);
      MatchPatIt != MatchPats.end()) {
    MatchRoot = MatchPatIt->second.get();
    return Finish();
  }

  // Look by def:
  //    (G_FNEG $root, $y)
  auto LookupRes = MatchOpTable.lookup(RootName);
  if (!LookupRes.Found) {
    PrintError("Cannot find root '" + RootName + "' in match patterns!");
    return false;
  }

  MatchRoot = LookupRes.Def;
  if (!MatchRoot) {
    PrintError("Cannot use live-in operand '" + RootName +
               "' as match pattern root!");
    return false;
  }

  return Finish();
}

bool CombineRuleBuilder::buildRuleOperandsTable() {
  const auto DiagnoseRedefMatch = [&](StringRef OpName) {
    PrintError("Operand '" + OpName +
               "' is defined multiple times in the 'match' patterns");
  };

  const auto DiagnoseRedefApply = [&](StringRef OpName) {
    PrintError("Operand '" + OpName +
               "' is defined multiple times in the 'apply' patterns");
  };

  for (auto &Pat : values(MatchPats)) {
    auto *IP = dyn_cast<InstructionPattern>(Pat.get());
    if (IP && !MatchOpTable.addPattern(IP, DiagnoseRedefMatch))
      return false;
  }

  for (auto &Pat : values(ApplyPats)) {
    auto *IP = dyn_cast<InstructionPattern>(Pat.get());
    if (IP && !ApplyOpTable.addPattern(IP, DiagnoseRedefApply))
      return false;
  }

  return true;
}

bool CombineRuleBuilder::parseDefs(const DagInit &Def) {
  if (Def.getOperatorAsDef(RuleDef.getLoc())->getName() != "defs") {
    PrintError("Expected defs operator");
    return false;
  }

  SmallVector<StringRef> Roots;
  for (unsigned I = 0, E = Def.getNumArgs(); I < E; ++I) {
    if (isSpecificDef(*Def.getArg(I), "root")) {
      Roots.emplace_back(Def.getArgNameStr(I));
      continue;
    }

    // Subclasses of GIDefMatchData should declare that this rule needs to pass
    // data from the match stage to the apply stage, and ensure that the
    // generated matcher has a suitable variable for it to do so.
    if (Record *MatchDataRec =
            getDefOfSubClass(*Def.getArg(I), MatchDataClassName)) {
      MatchDatas.emplace_back(Def.getArgNameStr(I),
                              MatchDataRec->getValueAsString("Type"));
      continue;
    }

    // Otherwise emit an appropriate error message.
    if (getDefOfSubClass(*Def.getArg(I), "GIDefKind"))
      PrintError("This GIDefKind not implemented in tablegen");
    else if (getDefOfSubClass(*Def.getArg(I), "GIDefKindWithArgs"))
      PrintError("This GIDefKindWithArgs not implemented in tablegen");
    else
      PrintError("Expected a subclass of GIDefKind or a sub-dag whose "
                 "operator is of type GIDefKindWithArgs");
    return false;
  }

  if (Roots.size() != 1) {
    PrintError("Combine rules must have exactly one root");
    return false;
  }

  RootName = Roots.front();
  return true;
}

bool CombineRuleBuilder::emitMatchPattern(CodeExpansions &CE,
                                          const PatternAlternatives &Alts,
                                          const InstructionPattern &IP) {
  auto StackTrace = PrettyStackTraceEmit(RuleDef, &IP);

  auto &M = addRuleMatcher(Alts);
  InstructionMatcher &IM = M.addInstructionMatcher(IP.getName());
  declareInstExpansion(CE, IM, IP.getName());

  DenseSet<const Pattern *> SeenPats;

  const auto FindOperandDef = [&](StringRef Op) -> InstructionPattern * {
    return MatchOpTable.getDef(Op);
  };

  if (const auto *CGP = dyn_cast<CodeGenInstructionPattern>(&IP)) {
    if (!emitCodeGenInstructionMatchPattern(CE, Alts, M, IM, *CGP, SeenPats,
                                            FindOperandDef))
      return false;
  } else if (const auto *PFP = dyn_cast<PatFragPattern>(&IP)) {
    if (!PFP->getPatFrag().canBeMatchRoot()) {
      PrintError("cannot use '" + PFP->getInstName() + " as match root");
      return false;
    }

    if (!emitPatFragMatchPattern(CE, Alts, M, &IM, *PFP, SeenPats))
      return false;
  } else if (isa<BuiltinPattern>(&IP)) {
    llvm_unreachable("No match builtins known!");
  } else
    llvm_unreachable("Unknown kind of InstructionPattern!");

  // Emit remaining patterns
  const bool IsUsingCustomCXXAction = hasOnlyCXXApplyPatterns();
  SmallVector<CXXPattern *, 2> CXXMatchers;
  for (auto &Pat : values(MatchPats)) {
    if (SeenPats.contains(Pat.get()))
      continue;

    switch (Pat->getKind()) {
    case Pattern::K_AnyOpcode:
      PrintError("wip_match_opcode can not be used with instruction patterns!");
      return false;
    case Pattern::K_PatFrag: {
      if (!emitPatFragMatchPattern(CE, Alts, M, /*IM*/ nullptr,
                                   *cast<PatFragPattern>(Pat.get()), SeenPats))
        return false;
      continue;
    }
    case Pattern::K_Builtin:
      PrintError("No known match builtins");
      return false;
    case Pattern::K_CodeGenInstruction:
      cast<InstructionPattern>(Pat.get())->reportUnreachable(RuleDef.getLoc());
      return false;
    case Pattern::K_CXX: {
      // Delay emission for top-level C++ matchers (which can use MatchDatas).
      if (IsUsingCustomCXXAction)
        CXXMatchers.push_back(cast<CXXPattern>(Pat.get()));
      else
        addCXXPredicate(M, CE, *cast<CXXPattern>(Pat.get()), Alts);
      continue;
    }
    default:
      llvm_unreachable("unknown pattern kind!");
    }
  }

  return IsUsingCustomCXXAction ? emitCXXMatchApply(CE, M, CXXMatchers)
                                : emitApplyPatterns(CE, M);
}

bool CombineRuleBuilder::emitMatchPattern(CodeExpansions &CE,
                                          const PatternAlternatives &Alts,
                                          const AnyOpcodePattern &AOP) {
  auto StackTrace = PrettyStackTraceEmit(RuleDef, &AOP);

  const bool IsUsingCustomCXXAction = hasOnlyCXXApplyPatterns();
  for (const CodeGenInstruction *CGI : AOP.insts()) {
    auto &M = addRuleMatcher(Alts, "wip_match_opcode '" +
                                       CGI->TheDef->getName() + "'");

    InstructionMatcher &IM = M.addInstructionMatcher(AOP.getName());
    declareInstExpansion(CE, IM, AOP.getName());
    // declareInstExpansion needs to be identical, otherwise we need to create a
    // CodeExpansions object here instead.
    assert(IM.getInsnVarID() == 0);

    IM.addPredicate<InstructionOpcodeMatcher>(CGI);

    // Emit remaining patterns.
    SmallVector<CXXPattern *, 2> CXXMatchers;
    for (auto &Pat : values(MatchPats)) {
      if (Pat.get() == &AOP)
        continue;

      switch (Pat->getKind()) {
      case Pattern::K_AnyOpcode:
        PrintError("wip_match_opcode can only be present once!");
        return false;
      case Pattern::K_PatFrag: {
        DenseSet<const Pattern *> SeenPats;
        if (!emitPatFragMatchPattern(CE, Alts, M, /*IM*/ nullptr,
                                     *cast<PatFragPattern>(Pat.get()),
                                     SeenPats))
          return false;
        continue;
      }
      case Pattern::K_Builtin:
        PrintError("No known match builtins");
        return false;
      case Pattern::K_CodeGenInstruction:
        cast<InstructionPattern>(Pat.get())->reportUnreachable(
            RuleDef.getLoc());
        return false;
      case Pattern::K_CXX: {
        // Delay emission for top-level C++ matchers (which can use MatchDatas).
        if (IsUsingCustomCXXAction)
          CXXMatchers.push_back(cast<CXXPattern>(Pat.get()));
        else
          addCXXPredicate(M, CE, *cast<CXXPattern>(Pat.get()), Alts);
        break;
      }
      default:
        llvm_unreachable("unknown pattern kind!");
      }
    }

    const bool Res = IsUsingCustomCXXAction
                         ? emitCXXMatchApply(CE, M, CXXMatchers)
                         : emitApplyPatterns(CE, M);
    if (!Res)
      return false;
  }

  return true;
}

bool CombineRuleBuilder::emitPatFragMatchPattern(
    CodeExpansions &CE, const PatternAlternatives &Alts, RuleMatcher &RM,
    InstructionMatcher *IM, const PatFragPattern &PFP,
    DenseSet<const Pattern *> &SeenPats) {
  auto StackTrace = PrettyStackTraceEmit(RuleDef, &PFP);

  if (SeenPats.contains(&PFP))
    return true;
  SeenPats.insert(&PFP);

  const auto &PF = PFP.getPatFrag();

  if (!IM) {
    // When we don't have an IM, this means this PatFrag isn't reachable from
    // the root. This is only acceptable if it doesn't define anything (e.g. a
    // pure C++ PatFrag).
    if (PF.num_out_params() != 0) {
      PFP.reportUnreachable(RuleDef.getLoc());
      return false;
    }
  } else {
    // When an IM is provided, this is reachable from the root, and we're
    // expecting to have output operands.
    // TODO: If we want to allow for multiple roots we'll need a map of IMs
    // then, and emission becomes a bit more complicated.
    assert(PF.num_roots() == 1);
  }

  CodeExpansions PatFragCEs;
  if (!PFP.mapInputCodeExpansions(CE, PatFragCEs, RuleDef.getLoc()))
    return false;

  // List of {ParamName, ArgName}.
  // When all patterns have been emitted, find expansions in PatFragCEs named
  // ArgName and add their expansion to CE using ParamName as the key.
  SmallVector<std::pair<std::string, std::string>, 4> CEsToImport;

  // Map parameter names to the actual argument.
  const auto OperandMapper =
      [&](const InstructionOperand &O) -> InstructionOperand {
    if (!O.isNamedOperand())
      return O;

    StringRef ParamName = O.getOperandName();

    // Not sure what to do with those tbh. They should probably never be here.
    assert(!O.isNamedImmediate() && "TODO: handle named imms");
    unsigned PIdx = PF.getParamIdx(ParamName);

    // Map parameters to the argument values.
    if (PIdx == (unsigned)-1) {
      // This is a temp of the PatFragPattern, prefix the name to avoid
      // conflicts.
      return O.withNewName(
          insertStrRef((PFP.getName() + "." + ParamName).str()));
    }

    // The operand will be added to PatFragCEs's code expansions using the
    // parameter's name. If it's bound to some operand during emission of the
    // patterns, we'll want to add it to CE.
    auto ArgOp = PFP.getOperand(PIdx);
    if (ArgOp.isNamedOperand())
      CEsToImport.emplace_back(ArgOp.getOperandName().str(), ParamName);

    if (ArgOp.getType() && O.getType() && ArgOp.getType() != O.getType()) {
      StringRef PFName = PF.getName();
      PrintWarning("impossible type constraints: operand " + Twine(PIdx) +
                   " of '" + PFP.getName() + "' has type '" +
                   ArgOp.getType().str() + "', but '" + PFName +
                   "' constrains it to '" + O.getType().str() + "'");
      if (ArgOp.isNamedOperand())
        PrintNote("operand " + Twine(PIdx) + " of '" + PFP.getName() +
                  "' is '" + ArgOp.getOperandName() + "'");
      if (O.isNamedOperand())
        PrintNote("argument " + Twine(PIdx) + " of '" + PFName + "' is '" +
                  ParamName + "'");
    }

    return ArgOp;
  };

  // PatFragPatterns are only made of InstructionPatterns or CXXPatterns.
  // Emit instructions from the root.
  const auto &FragAlt = PF.getAlternative(Alts.lookup(&PFP));
  const auto &FragAltOT = FragAlt.OpTable;
  const auto LookupOperandDef =
      [&](StringRef Op) -> const InstructionPattern * {
    return FragAltOT.getDef(Op);
  };

  DenseSet<const Pattern *> PatFragSeenPats;
  for (const auto &[Idx, InOp] : enumerate(PF.out_params())) {
    if (InOp.Kind != PatFrag::PK_Root)
      continue;

    StringRef ParamName = InOp.Name;
    const auto *Def = FragAltOT.getDef(ParamName);
    assert(Def && "PatFrag::checkSemantics should have emitted an error if "
                  "an out operand isn't defined!");
    assert(isa<CodeGenInstructionPattern>(Def) &&
           "Nested PatFrags not supported yet");

    if (!emitCodeGenInstructionMatchPattern(
            PatFragCEs, Alts, RM, *IM, *cast<CodeGenInstructionPattern>(Def),
            PatFragSeenPats, LookupOperandDef, OperandMapper))
      return false;
  }

  // Emit leftovers.
  for (const auto &Pat : FragAlt.Pats) {
    if (PatFragSeenPats.contains(Pat.get()))
      continue;

    if (const auto *CXXPat = dyn_cast<CXXPattern>(Pat.get())) {
      addCXXPredicate(RM, PatFragCEs, *CXXPat, Alts);
      continue;
    }

    if (const auto *IP = dyn_cast<InstructionPattern>(Pat.get())) {
      IP->reportUnreachable(PF.getLoc());
      return false;
    }

    llvm_unreachable("Unexpected pattern kind in PatFrag");
  }

  for (const auto &[ParamName, ArgName] : CEsToImport) {
    // Note: we're find if ParamName already exists. It just means it's been
    // bound before, so we prefer to keep the first binding.
    CE.declare(ParamName, PatFragCEs.lookup(ArgName));
  }

  return true;
}

bool CombineRuleBuilder::emitApplyPatterns(CodeExpansions &CE, RuleMatcher &M) {
  assert(MatchDatas.empty());

  DenseSet<const Pattern *> SeenPats;
  StringMap<unsigned> OperandToTempRegID;

  for (auto *ApplyRoot : ApplyRoots) {
    assert(isa<InstructionPattern>(ApplyRoot) &&
           "Root can only be a InstructionPattern!");
    if (!emitInstructionApplyPattern(CE, M,
                                     cast<InstructionPattern>(*ApplyRoot),
                                     SeenPats, OperandToTempRegID))
      return false;
  }

  for (auto &Pat : values(ApplyPats)) {
    if (SeenPats.contains(Pat.get()))
      continue;

    switch (Pat->getKind()) {
    case Pattern::K_AnyOpcode:
      llvm_unreachable("Unexpected pattern in apply!");
    case Pattern::K_PatFrag:
      // TODO: We could support pure C++ PatFrags as a temporary thing.
      llvm_unreachable("Unexpected pattern in apply!");
    case Pattern::K_Builtin:
      if (!emitInstructionApplyPattern(CE, M, cast<BuiltinPattern>(*Pat),
                                       SeenPats, OperandToTempRegID))
        return false;
      break;
    case Pattern::K_CodeGenInstruction:
      cast<CodeGenInstructionPattern>(*Pat).reportUnreachable(RuleDef.getLoc());
      return false;
    case Pattern::K_CXX: {
      llvm_unreachable(
          "CXX Pattern Emission should have been handled earlier!");
    }
    default:
      llvm_unreachable("unknown pattern kind!");
    }
  }

  // Erase the root.
  unsigned RootInsnID =
      M.getInsnVarID(M.getInstructionMatcher(MatchRoot->getName()));
  M.addAction<EraseInstAction>(RootInsnID);

  return true;
}

bool CombineRuleBuilder::emitCXXMatchApply(CodeExpansions &CE, RuleMatcher &M,
                                           ArrayRef<CXXPattern *> Matchers) {
  assert(hasOnlyCXXApplyPatterns());
  declareAllMatchDatasExpansions(CE);

  std::string CodeStr;
  raw_string_ostream OS(CodeStr);

  for (auto &MD : MatchDatas)
    OS << MD.Type << " " << MD.getVarName() << ";\n";

  if (!Matchers.empty()) {
    OS << "// Match Patterns\n";
    for (auto *M : Matchers) {
      OS << "if(![&](){";
      CodeExpander Expander(M->getRawCode(), CE, RuleDef.getLoc(),
                            /*ShowExpansions=*/false);
      Expander.emit(OS);
      OS << "}()) {\n"
         << "  return false;\n}\n";
    }
  }

  OS << "// Apply Patterns\n";
  ListSeparator LS("\n");
  for (auto &Pat : ApplyPats) {
    auto *CXXPat = cast<CXXPattern>(Pat.second.get());
    CodeExpander Expander(CXXPat->getRawCode(), CE, RuleDef.getLoc(),
                          /*ShowExpansions=*/ false);
    OS << LS;
    Expander.emit(OS);
  }

  const auto &Code = CXXPredicateCode::getCustomActionCode(CodeStr);
  M.setCustomCXXAction(Code.getEnumNameWithPrefix(CXXCustomActionPrefix));
  return true;
}

bool CombineRuleBuilder::emitInstructionApplyPattern(
    CodeExpansions &CE, RuleMatcher &M, const InstructionPattern &P,
    DenseSet<const Pattern *> &SeenPats,
    StringMap<unsigned> &OperandToTempRegID) {
  auto StackTrace = PrettyStackTraceEmit(RuleDef, &P);

  if (SeenPats.contains(&P))
    return true;

  SeenPats.insert(&P);

  // First, render the uses.
  for (auto &Op : P.named_operands()) {
    if (Op.isDef())
      continue;

    StringRef OpName = Op.getOperandName();
    if (const auto *DefPat = ApplyOpTable.getDef(OpName)) {
      if (!emitInstructionApplyPattern(CE, M, *DefPat, SeenPats,
                                       OperandToTempRegID))
        return false;
    } else {
      // If we have no def, check this exists in the MatchRoot.
      if (!Op.isNamedImmediate() && !MatchOpTable.lookup(OpName).Found) {
        PrintError("invalid output operand '" + OpName +
                   "': operand is not a live-in of the match pattern, and it "
                   "has no definition");
        return false;
      }
    }
  }

  if (const auto *BP = dyn_cast<BuiltinPattern>(&P))
    return emitBuiltinApplyPattern(CE, M, *BP, OperandToTempRegID);

  if (isa<PatFragPattern>(&P))
    llvm_unreachable("PatFragPatterns is not supported in 'apply'!");

  auto &CGIP = cast<CodeGenInstructionPattern>(P);

  // Now render this inst.
  auto &DstMI =
      M.addAction<BuildMIAction>(M.allocateOutputInsnID(), &CGIP.getInst());

  bool HasEmittedIntrinsicID = false;
  const auto EmitIntrinsicID = [&]() {
    assert(CGIP.isIntrinsic());
    DstMI.addRenderer<IntrinsicIDRenderer>(CGIP.getIntrinsic());
    HasEmittedIntrinsicID = true;
  };

  for (auto &Op : P.operands()) {
    // Emit the intrinsic ID after the last def.
    if (CGIP.isIntrinsic() && !Op.isDef() && !HasEmittedIntrinsicID)
      EmitIntrinsicID();

    if (Op.isNamedImmediate()) {
      PrintError("invalid output operand '" + Op.getOperandName() +
                 "': output immediates cannot be named");
      PrintNote("while emitting pattern '" + P.getName() + "' (" +
                P.getInstName() + ")");
      return false;
    }

    if (Op.hasImmValue()) {
      if (!emitCodeGenInstructionApplyImmOperand(M, DstMI, CGIP, Op))
        return false;
      continue;
    }

    StringRef OpName = Op.getOperandName();

    // Uses of operand.
    if (!Op.isDef()) {
      if (auto It = OperandToTempRegID.find(OpName);
          It != OperandToTempRegID.end()) {
        assert(!MatchOpTable.lookup(OpName).Found &&
               "Temp reg is also from match pattern?");
        DstMI.addRenderer<TempRegRenderer>(It->second);
      } else {
        // This should be a match live in or a redef of a matched instr.
        // If it's a use of a temporary register, then we messed up somewhere -
        // the previous condition should have passed.
        assert(MatchOpTable.lookup(OpName).Found &&
               !ApplyOpTable.getDef(OpName) && "Temp reg not emitted yet!");
        DstMI.addRenderer<CopyRenderer>(OpName);
      }
      continue;
    }

    // Determine what we're dealing with. Are we replace a matched instruction?
    // Creating a new one?
    auto OpLookupRes = MatchOpTable.lookup(OpName);
    if (OpLookupRes.Found) {
      if (OpLookupRes.isLiveIn()) {
        // live-in of the match pattern.
        PrintError("Cannot define live-in operand '" + OpName +
                   "' in the 'apply' pattern");
        return false;
      }
      assert(OpLookupRes.Def);

      // TODO: Handle this. We need to mutate the instr, or delete the old
      // one.
      //       Likewise, we also need to ensure we redef everything, if the
      //       instr has more than one def, we need to redef all or nothing.
      if (OpLookupRes.Def != MatchRoot) {
        PrintError("redefining an instruction other than the root is not "
                   "supported (operand '" +
                   OpName + "')");
        return false;
      }
      // redef of a match
      DstMI.addRenderer<CopyRenderer>(OpName);
      continue;
    }

    // Define a new register unique to the apply patterns (AKA a "temp"
    // register).
    unsigned TempRegID;
    if (auto It = OperandToTempRegID.find(OpName);
        It != OperandToTempRegID.end()) {
      TempRegID = It->second;
    } else {
      // This is a brand new register.
      TempRegID = M.allocateTempRegID();
      OperandToTempRegID[OpName] = TempRegID;
      const auto Ty = Op.getType();
      if (!Ty) {
        PrintError("def of a new register '" + OpName +
                   "' in the apply patterns must have a type");
        return false;
      }

      declareTempRegExpansion(CE, TempRegID, OpName);
      // Always insert the action at the beginning, otherwise we may end up
      // using the temp reg before it's available.
      M.insertAction<MakeTempRegisterAction>(
          M.actions_begin(), getLLTCodeGenOrTempType(Ty, M), TempRegID);
    }

    DstMI.addRenderer<TempRegRenderer>(TempRegID, /*IsDef=*/true);
  }

  // Some intrinsics have no in operands, ensure the ID is still emitted in such
  // cases.
  if (CGIP.isIntrinsic() && !HasEmittedIntrinsicID)
    EmitIntrinsicID();

  // Render MIFlags
  if (const auto *FI = CGIP.getMIFlagsInfo()) {
    for (StringRef InstName : FI->copy_flags())
      DstMI.addCopiedMIFlags(M.getInstructionMatcher(InstName));
    for (StringRef F : FI->set_flags())
      DstMI.addSetMIFlags(F);
    for (StringRef F : FI->unset_flags())
      DstMI.addUnsetMIFlags(F);
  }

  // Don't allow mutating opcodes for GISel combiners. We want a more precise
  // handling of MIFlags so we require them to be explicitly preserved.
  //
  // TODO: We don't mutate very often, if at all in combiners, but it'd be nice
  // to re-enable this. We'd then need to always clear MIFlags when mutating
  // opcodes, and never mutate an inst that we copy flags from.
  // DstMI.chooseInsnToMutate(M);
  declareInstExpansion(CE, DstMI, P.getName());

  return true;
}

bool CombineRuleBuilder::emitCodeGenInstructionApplyImmOperand(
    RuleMatcher &M, BuildMIAction &DstMI, const CodeGenInstructionPattern &P,
    const InstructionOperand &O) {
  // If we have a type, we implicitly emit a G_CONSTANT, except for G_CONSTANT
  // itself where we emit a CImm.
  //
  // No type means we emit a simple imm.
  // G_CONSTANT is a special case and needs a CImm though so this is likely a
  // mistake.
  const bool isGConstant = P.is("G_CONSTANT");
  const auto Ty = O.getType();
  if (!Ty) {
    if (isGConstant) {
      PrintError("'G_CONSTANT' immediate must be typed!");
      PrintNote("while emitting pattern '" + P.getName() + "' (" +
                P.getInstName() + ")");
      return false;
    }

    DstMI.addRenderer<ImmRenderer>(O.getImmValue());
    return true;
  }

  auto ImmTy = getLLTCodeGenOrTempType(Ty, M);

  if (isGConstant) {
    DstMI.addRenderer<ImmRenderer>(O.getImmValue(), ImmTy);
    return true;
  }

  unsigned TempRegID = M.allocateTempRegID();
  // Ensure MakeTempReg & the BuildConstantAction occur at the beginning.
  auto InsertIt = M.insertAction<MakeTempRegisterAction>(M.actions_begin(),
                                                         ImmTy, TempRegID);
  M.insertAction<BuildConstantAction>(++InsertIt, TempRegID, O.getImmValue());
  DstMI.addRenderer<TempRegRenderer>(TempRegID);
  return true;
}

bool CombineRuleBuilder::emitBuiltinApplyPattern(
    CodeExpansions &CE, RuleMatcher &M, const BuiltinPattern &P,
    StringMap<unsigned> &OperandToTempRegID) {
  const auto Error = [&](Twine Reason) {
    PrintError("cannot emit '" + P.getInstName() + "' builtin: " + Reason);
    return false;
  };

  switch (P.getBuiltinKind()) {
  case BI_EraseRoot: {
    // Root is always inst 0.
    M.addAction<EraseInstAction>(/*InsnID*/ 0);
    return true;
  }
  case BI_ReplaceReg: {
    StringRef Old = P.getOperand(0).getOperandName();
    StringRef New = P.getOperand(1).getOperandName();

    if (!ApplyOpTable.lookup(New).Found && !MatchOpTable.lookup(New).Found)
      return Error("unknown operand '" + Old + "'");

    auto &OldOM = M.getOperandMatcher(Old);
    if (auto It = OperandToTempRegID.find(New);
        It != OperandToTempRegID.end()) {
      // Replace with temp reg.
      M.addAction<ReplaceRegAction>(OldOM.getInsnVarID(), OldOM.getOpIdx(),
                                    It->second);
    } else {
      // Replace with matched reg.
      auto &NewOM = M.getOperandMatcher(New);
      M.addAction<ReplaceRegAction>(OldOM.getInsnVarID(), OldOM.getOpIdx(),
                                    NewOM.getInsnVarID(), NewOM.getOpIdx());
    }
    // checkSemantics should have ensured that we can only rewrite the root.
    // Ensure we're deleting it.
    assert(MatchOpTable.getDef(Old) == MatchRoot);
    return true;
  }
  }

  llvm_unreachable("Unknown BuiltinKind!");
}

bool isLiteralImm(const InstructionPattern &P, unsigned OpIdx) {
  if (const auto *CGP = dyn_cast<CodeGenInstructionPattern>(&P)) {
    StringRef InstName = CGP->getInst().TheDef->getName();
    return (InstName == "G_CONSTANT" || InstName == "G_FCONSTANT") &&
           OpIdx == 1;
  }

  llvm_unreachable("TODO");
}

bool CombineRuleBuilder::emitCodeGenInstructionMatchPattern(
    CodeExpansions &CE, const PatternAlternatives &Alts, RuleMatcher &M,
    InstructionMatcher &IM, const CodeGenInstructionPattern &P,
    DenseSet<const Pattern *> &SeenPats, OperandDefLookupFn LookupOperandDef,
    OperandMapperFnRef OperandMapper) {
  auto StackTrace = PrettyStackTraceEmit(RuleDef, &P);

  if (SeenPats.contains(&P))
    return true;

  SeenPats.insert(&P);

  IM.addPredicate<InstructionOpcodeMatcher>(&P.getInst());
  declareInstExpansion(CE, IM, P.getName());

  // If this is an intrinsic, check the intrinsic ID.
  if (P.isIntrinsic()) {
    // The IntrinsicID's operand is the first operand after the defs.
    OperandMatcher &OM = IM.addOperand(P.getNumInstDefs(), "$intrinsic_id",
                                       AllocatedTemporariesBaseID++);
    OM.addPredicate<IntrinsicIDOperandMatcher>(P.getIntrinsic());
  }

  // Check flags if needed.
  if (const auto *FI = P.getMIFlagsInfo()) {
    assert(FI->copy_flags().empty());

    if (const auto &SetF = FI->set_flags(); !SetF.empty())
      IM.addPredicate<MIFlagsInstructionPredicateMatcher>(SetF.getArrayRef());
    if (const auto &UnsetF = FI->unset_flags(); !UnsetF.empty())
      IM.addPredicate<MIFlagsInstructionPredicateMatcher>(UnsetF.getArrayRef(),
                                                          /*CheckNot=*/true);
  }

  for (auto [Idx, OriginalO] : enumerate(P.operands())) {
    // Remap the operand. This is used when emitting InstructionPatterns inside
    // PatFrags, so it can remap them to the arguments passed to the pattern.
    //
    // We use the remapped operand to emit immediates, and for the symbolic
    // operand names (in IM.addOperand). CodeExpansions and OperandTable lookups
    // still use the original name.
    //
    // The "def" flag on the remapped operand is always ignored.
    auto RemappedO = OperandMapper(OriginalO);
    assert(RemappedO.isNamedOperand() == OriginalO.isNamedOperand() &&
           "Cannot remap an unnamed operand to a named one!");

    const auto OpName =
        RemappedO.isNamedOperand() ? RemappedO.getOperandName().str() : "";

    // For intrinsics, the first use operand is the intrinsic id, so the true
    // operand index is shifted by 1.
    //
    // From now on:
    //    Idx = index in the pattern operand list.
    //    RealIdx = expected index in the MachineInstr.
    const unsigned RealIdx =
        (P.isIntrinsic() && !OriginalO.isDef()) ? (Idx + 1) : Idx;
    OperandMatcher &OM =
        IM.addOperand(RealIdx, OpName, AllocatedTemporariesBaseID++);
    if (!OpName.empty())
      declareOperandExpansion(CE, OM, OriginalO.getOperandName());

    // Handle immediates.
    if (RemappedO.hasImmValue()) {
      if (isLiteralImm(P, Idx))
        OM.addPredicate<LiteralIntOperandMatcher>(RemappedO.getImmValue());
      else
        OM.addPredicate<ConstantIntOperandMatcher>(RemappedO.getImmValue());
    }

    // Handle typed operands, but only bother to check if it hasn't been done
    // before.
    //
    // getOperandMatcher will always return the first OM to have been created
    // for that Operand. "OM" here is always a new OperandMatcher.
    //
    // Always emit a check for unnamed operands.
    if (OpName.empty() ||
        !M.getOperandMatcher(OpName).contains<LLTOperandMatcher>()) {
      if (const auto Ty = RemappedO.getType()) {
        // TODO: We could support GITypeOf here on the condition that the
        // OperandMatcher exists already. Though it's clunky to make this work
        // and isn't all that useful so it's just rejected in typecheckPatterns
        // at this time.
        assert(Ty.isLLT() && "Only LLTs are supported in match patterns!");
        OM.addPredicate<LLTOperandMatcher>(getLLTCodeGen(Ty));
      }
    }

    // Stop here if the operand is a def, or if it had no name.
    if (OriginalO.isDef() || !OriginalO.isNamedOperand())
      continue;

    const auto *DefPat = LookupOperandDef(OriginalO.getOperandName());
    if (!DefPat)
      continue;

    if (OriginalO.hasImmValue()) {
      assert(!OpName.empty());
      // This is a named immediate that also has a def, that's not okay.
      // e.g.
      //    (G_SEXT $y, (i32 0))
      //    (COPY $x, 42:$y)
      PrintError("'" + OpName +
                 "' is a named immediate, it cannot be defined by another "
                 "instruction");
      PrintNote("'" + OpName + "' is defined by '" + DefPat->getName() + "'");
      return false;
    }

    // From here we know that the operand defines an instruction, and we need to
    // emit it.
    auto InstOpM =
        OM.addPredicate<InstructionOperandMatcher>(M, DefPat->getName());
    if (!InstOpM) {
      // TODO: copy-pasted from GlobalISelEmitter.cpp. Is it still relevant
      // here?
      PrintError("Nested instruction '" + DefPat->getName() +
                 "' cannot be the same as another operand '" +
                 OriginalO.getOperandName() + "'");
      return false;
    }

    auto &IM = (*InstOpM)->getInsnMatcher();
    if (const auto *CGIDef = dyn_cast<CodeGenInstructionPattern>(DefPat)) {
      if (!emitCodeGenInstructionMatchPattern(CE, Alts, M, IM, *CGIDef,
                                              SeenPats, LookupOperandDef,
                                              OperandMapper))
        return false;
      continue;
    }

    if (const auto *PFPDef = dyn_cast<PatFragPattern>(DefPat)) {
      if (!emitPatFragMatchPattern(CE, Alts, M, &IM, *PFPDef, SeenPats))
        return false;
      continue;
    }

    llvm_unreachable("unknown type of InstructionPattern");
  }

  return true;
}

//===- GICombinerEmitter --------------------------------------------------===//

/// Main implementation class. This emits the tablegenerated output.
///
/// It collects rules, uses `CombineRuleBuilder` to parse them and accumulate
/// RuleMatchers, then takes all the necessary state/data from the various
/// static storage pools and wires them together to emit the match table &
/// associated function/data structures.
class GICombinerEmitter final : public GlobalISelMatchTableExecutorEmitter {
  RecordKeeper &Records;
  StringRef Name;
  const CodeGenTarget &Target;
  Record *Combiner;
  unsigned NextRuleID = 0;

  // List all combine rules (ID, name) imported.
  // Note that the combiner rule ID is different from the RuleMatcher ID. The
  // latter is internal to the MatchTable, the former is the canonical ID of the
  // combine rule used to disable/enable it.
  std::vector<std::pair<unsigned, std::string>> AllCombineRules;

  // Keep track of all rules we've seen so far to ensure we don't process
  // the same rule twice.
  StringSet<> RulesSeen;

  MatchTable buildMatchTable(MutableArrayRef<RuleMatcher> Rules);

  void emitRuleConfigImpl(raw_ostream &OS);

  void emitAdditionalImpl(raw_ostream &OS) override;

  void emitMIPredicateFns(raw_ostream &OS) override;
  void emitI64ImmPredicateFns(raw_ostream &OS) override;
  void emitAPFloatImmPredicateFns(raw_ostream &OS) override;
  void emitAPIntImmPredicateFns(raw_ostream &OS) override;
  void emitTestSimplePredicate(raw_ostream &OS) override;
  void emitRunCustomAction(raw_ostream &OS) override;

  const CodeGenTarget &getTarget() const override { return Target; }
  StringRef getClassName() const override {
    return Combiner->getValueAsString("Classname");
  }

  StringRef getCombineAllMethodName() const {
    return Combiner->getValueAsString("CombineAllMethodName");
  }

  std::string getRuleConfigClassName() const {
    return getClassName().str() + "RuleConfig";
  }

  void gatherRules(std::vector<RuleMatcher> &Rules,
                   const std::vector<Record *> &&RulesAndGroups);

public:
  explicit GICombinerEmitter(RecordKeeper &RK, const CodeGenTarget &Target,
                             StringRef Name, Record *Combiner);
  ~GICombinerEmitter() {}

  void run(raw_ostream &OS);
};

void GICombinerEmitter::emitRuleConfigImpl(raw_ostream &OS) {
  OS << "struct " << getRuleConfigClassName() << " {\n"
     << "  SparseBitVector<> DisabledRules;\n\n"
     << "  bool isRuleEnabled(unsigned RuleID) const;\n"
     << "  bool parseCommandLineOption();\n"
     << "  bool setRuleEnabled(StringRef RuleIdentifier);\n"
     << "  bool setRuleDisabled(StringRef RuleIdentifier);\n"
     << "};\n\n";

  std::vector<std::pair<std::string, std::string>> Cases;
  Cases.reserve(AllCombineRules.size());

  for (const auto &[ID, Name] : AllCombineRules)
    Cases.emplace_back(Name, "return " + to_string(ID) + ";\n");

  OS << "static std::optional<uint64_t> getRuleIdxForIdentifier(StringRef "
        "RuleIdentifier) {\n"
     << "  uint64_t I;\n"
     << "  // getAtInteger(...) returns false on success\n"
     << "  bool Parsed = !RuleIdentifier.getAsInteger(0, I);\n"
     << "  if (Parsed)\n"
     << "    return I;\n\n"
     << "#ifndef NDEBUG\n";
  StringMatcher Matcher("RuleIdentifier", Cases, OS);
  Matcher.Emit();
  OS << "#endif // ifndef NDEBUG\n\n"
     << "  return std::nullopt;\n"
     << "}\n";

  OS << "static std::optional<std::pair<uint64_t, uint64_t>> "
        "getRuleRangeForIdentifier(StringRef RuleIdentifier) {\n"
     << "  std::pair<StringRef, StringRef> RangePair = "
        "RuleIdentifier.split('-');\n"
     << "  if (!RangePair.second.empty()) {\n"
     << "    const auto First = "
        "getRuleIdxForIdentifier(RangePair.first);\n"
     << "    const auto Last = "
        "getRuleIdxForIdentifier(RangePair.second);\n"
     << "    if (!First || !Last)\n"
     << "      return std::nullopt;\n"
     << "    if (First >= Last)\n"
     << "      report_fatal_error(\"Beginning of range should be before "
        "end of range\");\n"
     << "    return {{*First, *Last + 1}};\n"
     << "  }\n"
     << "  if (RangePair.first == \"*\") {\n"
     << "    return {{0, " << AllCombineRules.size() << "}};\n"
     << "  }\n"
     << "  const auto I = getRuleIdxForIdentifier(RangePair.first);\n"
     << "  if (!I)\n"
     << "    return std::nullopt;\n"
     << "  return {{*I, *I + 1}};\n"
     << "}\n\n";

  for (bool Enabled : {true, false}) {
    OS << "bool " << getRuleConfigClassName() << "::setRule"
       << (Enabled ? "Enabled" : "Disabled") << "(StringRef RuleIdentifier) {\n"
       << "  auto MaybeRange = getRuleRangeForIdentifier(RuleIdentifier);\n"
       << "  if (!MaybeRange)\n"
       << "    return false;\n"
       << "  for (auto I = MaybeRange->first; I < MaybeRange->second; ++I)\n"
       << "    DisabledRules." << (Enabled ? "reset" : "set") << "(I);\n"
       << "  return true;\n"
       << "}\n\n";
  }

  OS << "static std::vector<std::string> " << Name << "Option;\n"
     << "static cl::list<std::string> " << Name << "DisableOption(\n"
     << "    \"" << Name.lower() << "-disable-rule\",\n"
     << "    cl::desc(\"Disable one or more combiner rules temporarily in "
     << "the " << Name << " pass\"),\n"
     << "    cl::CommaSeparated,\n"
     << "    cl::Hidden,\n"
     << "    cl::cat(GICombinerOptionCategory),\n"
     << "    cl::callback([](const std::string &Str) {\n"
     << "      " << Name << "Option.push_back(Str);\n"
     << "    }));\n"
     << "static cl::list<std::string> " << Name << "OnlyEnableOption(\n"
     << "    \"" << Name.lower() << "-only-enable-rule\",\n"
     << "    cl::desc(\"Disable all rules in the " << Name
     << " pass then re-enable the specified ones\"),\n"
     << "    cl::Hidden,\n"
     << "    cl::cat(GICombinerOptionCategory),\n"
     << "    cl::callback([](const std::string &CommaSeparatedArg) {\n"
     << "      StringRef Str = CommaSeparatedArg;\n"
     << "      " << Name << "Option.push_back(\"*\");\n"
     << "      do {\n"
     << "        auto X = Str.split(\",\");\n"
     << "        " << Name << "Option.push_back((\"!\" + X.first).str());\n"
     << "        Str = X.second;\n"
     << "      } while (!Str.empty());\n"
     << "    }));\n"
     << "\n\n"
     << "bool " << getRuleConfigClassName()
     << "::isRuleEnabled(unsigned RuleID) const {\n"
     << "    return  !DisabledRules.test(RuleID);\n"
     << "}\n"
     << "bool " << getRuleConfigClassName() << "::parseCommandLineOption() {\n"
     << "  for (StringRef Identifier : " << Name << "Option) {\n"
     << "    bool Enabled = Identifier.consume_front(\"!\");\n"
     << "    if (Enabled && !setRuleEnabled(Identifier))\n"
     << "      return false;\n"
     << "    if (!Enabled && !setRuleDisabled(Identifier))\n"
     << "      return false;\n"
     << "  }\n"
     << "  return true;\n"
     << "}\n\n";
}

void GICombinerEmitter::emitAdditionalImpl(raw_ostream &OS) {
  OS << "bool " << getClassName() << "::" << getCombineAllMethodName()
     << "(MachineInstr &I) const {\n"
     << "  const TargetSubtargetInfo &ST = MF.getSubtarget();\n"
     << "  const PredicateBitset AvailableFeatures = "
        "getAvailableFeatures();\n"
     << "  B.setInstrAndDebugLoc(I);\n"
     << "  State.MIs.clear();\n"
     << "  State.MIs.push_back(&I);\n"
     << "  if (executeMatchTable(*this, State, ExecInfo, B"
     << ", getMatchTable(), *ST.getInstrInfo(), MRI, "
        "*MRI.getTargetRegisterInfo(), *ST.getRegBankInfo(), AvailableFeatures"
     << ", /*CoverageInfo*/ nullptr)) {\n"
     << "    return true;\n"
     << "  }\n\n"
     << "  return false;\n"
     << "}\n\n";
}

void GICombinerEmitter::emitMIPredicateFns(raw_ostream &OS) {
  auto MatchCode = CXXPredicateCode::getAllMatchCode();
  emitMIPredicateFnsImpl<const CXXPredicateCode *>(
      OS, "", ArrayRef<const CXXPredicateCode *>(MatchCode),
      [](const CXXPredicateCode *C) -> StringRef { return C->BaseEnumName; },
      [](const CXXPredicateCode *C) -> StringRef { return C->Code; });
}

void GICombinerEmitter::emitI64ImmPredicateFns(raw_ostream &OS) {
  // Unused, but still needs to be called.
  emitImmPredicateFnsImpl<unsigned>(
      OS, "I64", "int64_t", {}, [](unsigned) { return ""; },
      [](unsigned) { return ""; });
}

void GICombinerEmitter::emitAPFloatImmPredicateFns(raw_ostream &OS) {
  // Unused, but still needs to be called.
  emitImmPredicateFnsImpl<unsigned>(
      OS, "APFloat", "const APFloat &", {}, [](unsigned) { return ""; },
      [](unsigned) { return ""; });
}

void GICombinerEmitter::emitAPIntImmPredicateFns(raw_ostream &OS) {
  // Unused, but still needs to be called.
  emitImmPredicateFnsImpl<unsigned>(
      OS, "APInt", "const APInt &", {}, [](unsigned) { return ""; },
      [](unsigned) { return ""; });
}

void GICombinerEmitter::emitTestSimplePredicate(raw_ostream &OS) {
  if (!AllCombineRules.empty()) {
    OS << "enum {\n";
    std::string EnumeratorSeparator = " = GICXXPred_Invalid + 1,\n";
    // To avoid emitting a switch, we expect that all those rules are in order.
    // That way we can just get the RuleID from the enum by subtracting
    // (GICXXPred_Invalid + 1).
    unsigned ExpectedID = 0;
    (void)ExpectedID;
    for (const auto &ID : keys(AllCombineRules)) {
      assert(ExpectedID++ == ID && "combine rules are not ordered!");
      OS << "  " << getIsEnabledPredicateEnumName(ID) << EnumeratorSeparator;
      EnumeratorSeparator = ",\n";
    }
    OS << "};\n\n";
  }

  OS << "bool " << getClassName()
     << "::testSimplePredicate(unsigned Predicate) const {\n"
     << "    return RuleConfig.isRuleEnabled(Predicate - "
        "GICXXPred_Invalid - "
        "1);\n"
     << "}\n";
}

void GICombinerEmitter::emitRunCustomAction(raw_ostream &OS) {
  const auto CustomActionsCode = CXXPredicateCode::getAllCustomActionsCode();

  if (!CustomActionsCode.empty()) {
    OS << "enum {\n";
    std::string EnumeratorSeparator = " = GICXXCustomAction_Invalid + 1,\n";
    for (const auto &CA : CustomActionsCode) {
      OS << "  " << CA->getEnumNameWithPrefix(CXXCustomActionPrefix)
         << EnumeratorSeparator;
      EnumeratorSeparator = ",\n";
    }
    OS << "};\n";
  }

  OS << "bool " << getClassName()
     << "::runCustomAction(unsigned ApplyID, const MatcherState &State, "
        "NewMIVector &OutMIs) const "
        "{\n  Helper.getBuilder().setInstrAndDebugLoc(*State.MIs[0]);\n";
  if (!CustomActionsCode.empty()) {
    OS << "  switch(ApplyID) {\n";
    for (const auto &CA : CustomActionsCode) {
      OS << "  case " << CA->getEnumNameWithPrefix(CXXCustomActionPrefix)
         << ":{\n"
         << "    " << join(split(CA->Code, '\n'), "\n    ") << '\n'
         << "    return true;\n";
      OS << "  }\n";
    }
    OS << "  }\n";
  }
  OS << "  llvm_unreachable(\"Unknown Apply Action\");\n"
     << "}\n";
}

GICombinerEmitter::GICombinerEmitter(RecordKeeper &RK,
                                     const CodeGenTarget &Target,
                                     StringRef Name, Record *Combiner)
    : Records(RK), Name(Name), Target(Target), Combiner(Combiner) {}

MatchTable
GICombinerEmitter::buildMatchTable(MutableArrayRef<RuleMatcher> Rules) {
  std::vector<Matcher *> InputRules;
  for (Matcher &Rule : Rules)
    InputRules.push_back(&Rule);

  unsigned CurrentOrdering = 0;
  StringMap<unsigned> OpcodeOrder;
  for (RuleMatcher &Rule : Rules) {
    const StringRef Opcode = Rule.getOpcode();
    assert(!Opcode.empty() && "Didn't expect an undefined opcode");
    if (OpcodeOrder.count(Opcode) == 0)
      OpcodeOrder[Opcode] = CurrentOrdering++;
  }

  llvm::stable_sort(InputRules, [&OpcodeOrder](const Matcher *A,
                                               const Matcher *B) {
    auto *L = static_cast<const RuleMatcher *>(A);
    auto *R = static_cast<const RuleMatcher *>(B);
    return std::make_tuple(OpcodeOrder[L->getOpcode()], L->getNumOperands()) <
           std::make_tuple(OpcodeOrder[R->getOpcode()], R->getNumOperands());
  });

  for (Matcher *Rule : InputRules)
    Rule->optimize();

  std::vector<std::unique_ptr<Matcher>> MatcherStorage;
  std::vector<Matcher *> OptRules =
      optimizeRules<GroupMatcher>(InputRules, MatcherStorage);

  for (Matcher *Rule : OptRules)
    Rule->optimize();

  OptRules = optimizeRules<SwitchMatcher>(OptRules, MatcherStorage);

  return MatchTable::buildTable(OptRules, /*WithCoverage*/ false,
                                /*IsCombiner*/ true);
}

/// Recurse into GICombineGroup's and flatten the ruleset into a simple list.
void GICombinerEmitter::gatherRules(
    std::vector<RuleMatcher> &ActiveRules,
    const std::vector<Record *> &&RulesAndGroups) {
  for (Record *Rec : RulesAndGroups) {
    if (!Rec->isValueUnset("Rules")) {
      gatherRules(ActiveRules, Rec->getValueAsListOfDefs("Rules"));
      continue;
    }

    StringRef RuleName = Rec->getName();
    if (!RulesSeen.insert(RuleName).second) {
      PrintWarning(Rec->getLoc(),
                   "skipping rule '" + Rec->getName() +
                       "' because it has already been processed");
      continue;
    }

    AllCombineRules.emplace_back(NextRuleID, Rec->getName().str());
    CombineRuleBuilder CRB(Target, SubtargetFeatures, *Rec, NextRuleID++,
                           ActiveRules);

    if (!CRB.parseAll()) {
      assert(ErrorsPrinted && "Parsing failed without errors!");
      continue;
    }

    if (StopAfterParse) {
      CRB.print(outs());
      continue;
    }

    if (!CRB.emitRuleMatchers()) {
      assert(ErrorsPrinted && "Emission failed without errors!");
      continue;
    }
  }
}

void GICombinerEmitter::run(raw_ostream &OS) {
  InstructionOpcodeMatcher::initOpcodeValuesMap(Target);
  LLTOperandMatcher::initTypeIDValuesMap();

  Records.startTimer("Gather rules");
  std::vector<RuleMatcher> Rules;
  gatherRules(Rules, Combiner->getValueAsListOfDefs("Rules"));
  if (ErrorsPrinted)
    PrintFatalError(Combiner->getLoc(), "Failed to parse one or more rules");

  if (StopAfterParse)
    return;

  Records.startTimer("Creating Match Table");
  unsigned MaxTemporaries = 0;
  for (const auto &Rule : Rules)
    MaxTemporaries = std::max(MaxTemporaries, Rule.countRendererFns());

  llvm::stable_sort(Rules, [&](const RuleMatcher &A, const RuleMatcher &B) {
    if (A.isHigherPriorityThan(B)) {
      assert(!B.isHigherPriorityThan(A) && "Cannot be more important "
                                           "and less important at "
                                           "the same time");
      return true;
    }
    return false;
  });

  const MatchTable Table = buildMatchTable(Rules);

  Records.startTimer("Emit combiner");

  emitSourceFileHeader(getClassName().str() + " Combiner Match Table", OS);

  // Unused
  std::vector<StringRef> CustomRendererFns;
  // Unused
  std::vector<Record *> ComplexPredicates;

  SmallVector<LLTCodeGen, 16> TypeObjects;
  append_range(TypeObjects, KnownTypes);
  llvm::sort(TypeObjects);

  // Hack: Avoid empty declarator.
  if (TypeObjects.empty())
    TypeObjects.push_back(LLT::scalar(1));

  // GET_GICOMBINER_DEPS, which pulls in extra dependencies.
  OS << "#ifdef GET_GICOMBINER_DEPS\n"
     << "#include \"llvm/ADT/SparseBitVector.h\"\n"
     << "namespace llvm {\n"
     << "extern cl::OptionCategory GICombinerOptionCategory;\n"
     << "} // end namespace llvm\n"
     << "#endif // ifdef GET_GICOMBINER_DEPS\n\n";

  // GET_GICOMBINER_TYPES, which needs to be included before the declaration of
  // the class.
  OS << "#ifdef GET_GICOMBINER_TYPES\n";
  emitRuleConfigImpl(OS);
  OS << "#endif // ifdef GET_GICOMBINER_TYPES\n\n";
  emitPredicateBitset(OS, "GET_GICOMBINER_TYPES");

  // GET_GICOMBINER_CLASS_MEMBERS, which need to be included inside the class.
  emitPredicatesDecl(OS, "GET_GICOMBINER_CLASS_MEMBERS");
  emitTemporariesDecl(OS, "GET_GICOMBINER_CLASS_MEMBERS");

  // GET_GICOMBINER_IMPL, which needs to be included outside the class.
  emitExecutorImpl(OS, Table, TypeObjects, Rules, ComplexPredicates,
                   CustomRendererFns, "GET_GICOMBINER_IMPL");

  // GET_GICOMBINER_CONSTRUCTOR_INITS, which are in the constructor's
  // initializer list.
  emitPredicatesInit(OS, "GET_GICOMBINER_CONSTRUCTOR_INITS");
  emitTemporariesInit(OS, MaxTemporaries, "GET_GICOMBINER_CONSTRUCTOR_INITS");
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//

static void EmitGICombiner(RecordKeeper &RK, raw_ostream &OS) {
  EnablePrettyStackTrace();
  CodeGenTarget Target(RK);

  if (SelectedCombiners.empty())
    PrintFatalError("No combiners selected with -combiners");
  for (const auto &Combiner : SelectedCombiners) {
    Record *CombinerDef = RK.getDef(Combiner);
    if (!CombinerDef)
      PrintFatalError("Could not find " + Combiner);
    GICombinerEmitter(RK, Target, Combiner, CombinerDef).run(OS);
  }
}

static TableGen::Emitter::Opt X("gen-global-isel-combiner", EmitGICombiner,
                                "Generate GlobalISel Combiner");
