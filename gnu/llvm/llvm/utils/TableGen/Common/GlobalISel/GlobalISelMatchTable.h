//===- GlobalISelMatchTable.h ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains the code related to the GlobalISel Match Table emitted by
/// GlobalISelEmitter.cpp. The generated match table is interpreted at runtime
/// by `GIMatchTableExecutorImpl.h` to match & apply ISel patterns.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_GLOBALISELMATCHTABLE_H
#define LLVM_UTILS_TABLEGEN_GLOBALISELMATCHTABLE_H

#include "Common/CodeGenDAGPatterns.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/SaveAndRestore.h"
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace llvm {

class raw_ostream;
class Record;
class SMLoc;
class CodeGenRegisterClass;

// Use a namespace to avoid conflicts because there's some fairly generic names
// in there (e.g. Matcher).
namespace gi {
class MatchTable;
class Matcher;
class OperandMatcher;
class MatchAction;
class PredicateMatcher;
class InstructionMatcher;

enum {
  GISF_IgnoreCopies = 0x1,
};

using GISelFlags = std::uint16_t;

//===- Helper functions ---------------------------------------------------===//

void emitEncodingMacrosDef(raw_ostream &OS);
void emitEncodingMacrosUndef(raw_ostream &OS);

std::string getNameForFeatureBitset(const std::vector<Record *> &FeatureBitset,
                                    int HwModeIdx);

/// Takes a sequence of \p Rules and group them based on the predicates
/// they share. \p MatcherStorage is used as a memory container
/// for the group that are created as part of this process.
///
/// What this optimization does looks like if GroupT = GroupMatcher:
/// Output without optimization:
/// \verbatim
/// # R1
///  # predicate A
///  # predicate B
///  ...
/// # R2
///  # predicate A // <-- effectively this is going to be checked twice.
///                //     Once in R1 and once in R2.
///  # predicate C
/// \endverbatim
/// Output with optimization:
/// \verbatim
/// # Group1_2
///  # predicate A // <-- Check is now shared.
///  # R1
///   # predicate B
///  # R2
///   # predicate C
/// \endverbatim
template <class GroupT>
std::vector<Matcher *>
optimizeRules(ArrayRef<Matcher *> Rules,
              std::vector<std::unique_ptr<Matcher>> &MatcherStorage);

/// A record to be stored in a MatchTable.
///
/// This class represents any and all output that may be required to emit the
/// MatchTable. Instances  are most often configured to represent an opcode or
/// value that will be emitted to the table with some formatting but it can also
/// represent commas, comments, and other formatting instructions.
struct MatchTableRecord {
  enum RecordFlagsBits {
    MTRF_None = 0x0,
    /// Causes EmitStr to be formatted as comment when emitted.
    MTRF_Comment = 0x1,
    /// Causes the record value to be followed by a comma when emitted.
    MTRF_CommaFollows = 0x2,
    /// Causes the record value to be followed by a line break when emitted.
    MTRF_LineBreakFollows = 0x4,
    /// Indicates that the record defines a label and causes an additional
    /// comment to be emitted containing the index of the label.
    MTRF_Label = 0x8,
    /// Causes the record to be emitted as the index of the label specified by
    /// LabelID along with a comment indicating where that label is.
    MTRF_JumpTarget = 0x10,
    /// Causes the formatter to add a level of indentation before emitting the
    /// record.
    MTRF_Indent = 0x20,
    /// Causes the formatter to remove a level of indentation after emitting the
    /// record.
    MTRF_Outdent = 0x40,
    /// Causes the formatter to not use encoding macros to emit this multi-byte
    /// value.
    MTRF_PreEncoded = 0x80,
  };

  /// When MTRF_Label or MTRF_JumpTarget is used, indicates a label id to
  /// reference or define.
  unsigned LabelID;
  /// The string to emit. Depending on the MTRF_* flags it may be a comment, a
  /// value, a label name.
  std::string EmitStr;

private:
  /// The number of MatchTable elements described by this record. Comments are 0
  /// while values are typically 1. Values >1 may occur when we need to emit
  /// values that exceed the size of a MatchTable element.
  unsigned NumElements;

public:
  /// A bitfield of RecordFlagsBits flags.
  unsigned Flags;

  /// The actual run-time value, if known
  int64_t RawValue;

  MatchTableRecord(std::optional<unsigned> LabelID_, StringRef EmitStr,
                   unsigned NumElements, unsigned Flags,
                   int64_t RawValue = std::numeric_limits<int64_t>::min())
      : LabelID(LabelID_.value_or(~0u)), EmitStr(EmitStr),
        NumElements(NumElements), Flags(Flags), RawValue(RawValue) {
    assert((!LabelID_ || LabelID != ~0u) &&
           "This value is reserved for non-labels");
  }
  MatchTableRecord(const MatchTableRecord &Other) = default;
  MatchTableRecord(MatchTableRecord &&Other) = default;

  /// Useful if a Match Table Record gets optimized out
  void turnIntoComment() {
    Flags |= MTRF_Comment;
    Flags &= ~MTRF_CommaFollows;
    NumElements = 0;
  }

  /// For Jump Table generation purposes
  bool operator<(const MatchTableRecord &Other) const {
    return RawValue < Other.RawValue;
  }
  int64_t getRawValue() const { return RawValue; }

  void emit(raw_ostream &OS, bool LineBreakNextAfterThis,
            const MatchTable &Table) const;
  unsigned size() const { return NumElements; }
};

/// Holds the contents of a generated MatchTable to enable formatting and the
/// necessary index tracking needed to support GIM_Try.
class MatchTable {
  /// An unique identifier for the table. The generated table will be named
  /// MatchTable${ID}.
  unsigned ID;
  /// The records that make up the table. Also includes comments describing the
  /// values being emitted and line breaks to format it.
  std::vector<MatchTableRecord> Contents;
  /// The currently defined labels.
  DenseMap<unsigned, unsigned> LabelMap;
  /// Tracks the sum of MatchTableRecord::NumElements as the table is built.
  unsigned CurrentSize = 0;
  /// A unique identifier for a MatchTable label.
  unsigned CurrentLabelID = 0;
  /// Determines if the table should be instrumented for rule coverage tracking.
  bool IsWithCoverage;
  /// Whether this table is for the GISel combiner.
  bool IsCombinerTable;

public:
  static MatchTableRecord LineBreak;
  static MatchTableRecord Comment(StringRef Comment);
  static MatchTableRecord Opcode(StringRef Opcode, int IndentAdjust = 0);
  static MatchTableRecord NamedValue(unsigned NumBytes, StringRef NamedValue);
  static MatchTableRecord NamedValue(unsigned NumBytes, StringRef NamedValue,
                                     int64_t RawValue);
  static MatchTableRecord NamedValue(unsigned NumBytes, StringRef Namespace,
                                     StringRef NamedValue);
  static MatchTableRecord NamedValue(unsigned NumBytes, StringRef Namespace,
                                     StringRef NamedValue, int64_t RawValue);
  static MatchTableRecord IntValue(unsigned NumBytes, int64_t IntValue);
  static MatchTableRecord ULEB128Value(uint64_t IntValue);
  static MatchTableRecord Label(unsigned LabelID);
  static MatchTableRecord JumpTarget(unsigned LabelID);

  static MatchTable buildTable(ArrayRef<Matcher *> Rules, bool WithCoverage,
                               bool IsCombiner = false);

  MatchTable(bool WithCoverage, bool IsCombinerTable, unsigned ID = 0)
      : ID(ID), IsWithCoverage(WithCoverage), IsCombinerTable(IsCombinerTable) {
  }

  bool isWithCoverage() const { return IsWithCoverage; }
  bool isCombiner() const { return IsCombinerTable; }

  void push_back(const MatchTableRecord &Value) {
    if (Value.Flags & MatchTableRecord::MTRF_Label)
      defineLabel(Value.LabelID);
    Contents.push_back(Value);
    CurrentSize += Value.size();
  }

  unsigned allocateLabelID() { return CurrentLabelID++; }

  void defineLabel(unsigned LabelID) {
    LabelMap.insert(std::pair(LabelID, CurrentSize));
  }

  unsigned getLabelIndex(unsigned LabelID) const {
    const auto I = LabelMap.find(LabelID);
    assert(I != LabelMap.end() && "Use of undeclared label");
    return I->second;
  }

  void emitUse(raw_ostream &OS) const;
  void emitDeclaration(raw_ostream &OS) const;
};

inline MatchTable &operator<<(MatchTable &Table,
                              const MatchTableRecord &Value) {
  Table.push_back(Value);
  return Table;
}

/// This class stands in for LLT wherever we want to tablegen-erate an
/// equivalent at compiler run-time.
class LLTCodeGen {
private:
  LLT Ty;

public:
  LLTCodeGen() = default;
  LLTCodeGen(const LLT &Ty) : Ty(Ty) {}

  std::string getCxxEnumValue() const;

  void emitCxxEnumValue(raw_ostream &OS) const;
  void emitCxxConstructorCall(raw_ostream &OS) const;

  const LLT &get() const { return Ty; }

  /// This ordering is used for std::unique() and llvm::sort(). There's no
  /// particular logic behind the order but either A < B or B < A must be
  /// true if A != B.
  bool operator<(const LLTCodeGen &Other) const;
  bool operator==(const LLTCodeGen &B) const { return Ty == B.Ty; }
};

// Track all types that are used so we can emit the corresponding enum.
extern std::set<LLTCodeGen> KnownTypes;

/// Convert an MVT to an equivalent LLT if possible, or the invalid LLT() for
/// MVTs that don't map cleanly to an LLT (e.g., iPTR, *any, ...).
std::optional<LLTCodeGen> MVTToLLT(MVT::SimpleValueType SVT);

using TempTypeIdx = int64_t;
class LLTCodeGenOrTempType {
public:
  LLTCodeGenOrTempType(const LLTCodeGen &LLT) : Data(LLT) {}
  LLTCodeGenOrTempType(TempTypeIdx TempTy) : Data(TempTy) {}

  bool isLLTCodeGen() const { return std::holds_alternative<LLTCodeGen>(Data); }
  bool isTempTypeIdx() const {
    return std::holds_alternative<TempTypeIdx>(Data);
  }

  const LLTCodeGen &getLLTCodeGen() const {
    assert(isLLTCodeGen());
    return std::get<LLTCodeGen>(Data);
  }

  TempTypeIdx getTempTypeIdx() const {
    assert(isTempTypeIdx());
    return std::get<TempTypeIdx>(Data);
  }

private:
  std::variant<LLTCodeGen, TempTypeIdx> Data;
};

inline MatchTable &operator<<(MatchTable &Table,
                              const LLTCodeGenOrTempType &Ty) {
  if (Ty.isLLTCodeGen())
    Table << MatchTable::NamedValue(1, Ty.getLLTCodeGen().getCxxEnumValue());
  else
    Table << MatchTable::IntValue(1, Ty.getTempTypeIdx());
  return Table;
}

//===- Matchers -----------------------------------------------------------===//
class Matcher {
public:
  virtual ~Matcher();
  virtual void optimize();
  virtual void emit(MatchTable &Table) = 0;

  virtual bool hasFirstCondition() const = 0;
  virtual const PredicateMatcher &getFirstCondition() const = 0;
  virtual std::unique_ptr<PredicateMatcher> popFirstCondition() = 0;
};

class GroupMatcher final : public Matcher {
  /// Conditions that form a common prefix of all the matchers contained.
  SmallVector<std::unique_ptr<PredicateMatcher>, 1> Conditions;

  /// All the nested matchers, sharing a common prefix.
  std::vector<Matcher *> Matchers;

  /// An owning collection for any auxiliary matchers created while optimizing
  /// nested matchers contained.
  std::vector<std::unique_ptr<Matcher>> MatcherStorage;

public:
  /// Add a matcher to the collection of nested matchers if it meets the
  /// requirements, and return true. If it doesn't, do nothing and return false.
  ///
  /// Expected to preserve its argument, so it could be moved out later on.
  bool addMatcher(Matcher &Candidate);

  /// Mark the matcher as fully-built and ensure any invariants expected by both
  /// optimize() and emit(...) methods. Generally, both sequences of calls
  /// are expected to lead to a sensible result:
  ///
  /// addMatcher(...)*; finalize(); optimize(); emit(...); and
  /// addMatcher(...)*; finalize(); emit(...);
  ///
  /// or generally
  ///
  /// addMatcher(...)*; finalize(); { optimize()*; emit(...); }*
  ///
  /// Multiple calls to optimize() are expected to be handled gracefully, though
  /// optimize() is not expected to be idempotent. Multiple calls to finalize()
  /// aren't generally supported. emit(...) is expected to be non-mutating and
  /// producing the exact same results upon repeated calls.
  ///
  /// addMatcher() calls after the finalize() call are not supported.
  ///
  /// finalize() and optimize() are both allowed to mutate the contained
  /// matchers, so moving them out after finalize() is not supported.
  void finalize();
  void optimize() override;
  void emit(MatchTable &Table) override;

  /// Could be used to move out the matchers added previously, unless finalize()
  /// has been already called. If any of the matchers are moved out, the group
  /// becomes safe to destroy, but not safe to re-use for anything else.
  iterator_range<std::vector<Matcher *>::iterator> matchers() {
    return make_range(Matchers.begin(), Matchers.end());
  }
  size_t size() const { return Matchers.size(); }
  bool empty() const { return Matchers.empty(); }

  std::unique_ptr<PredicateMatcher> popFirstCondition() override {
    assert(!Conditions.empty() &&
           "Trying to pop a condition from a condition-less group");
    std::unique_ptr<PredicateMatcher> P = std::move(Conditions.front());
    Conditions.erase(Conditions.begin());
    return P;
  }
  const PredicateMatcher &getFirstCondition() const override {
    assert(!Conditions.empty() &&
           "Trying to get a condition from a condition-less group");
    return *Conditions.front();
  }
  bool hasFirstCondition() const override { return !Conditions.empty(); }

private:
  /// See if a candidate matcher could be added to this group solely by
  /// analyzing its first condition.
  bool candidateConditionMatches(const PredicateMatcher &Predicate) const;
};

class SwitchMatcher : public Matcher {
  /// All the nested matchers, representing distinct switch-cases. The first
  /// conditions (as Matcher::getFirstCondition() reports) of all the nested
  /// matchers must share the same type and path to a value they check, in other
  /// words, be isIdenticalDownToValue, but have different values they check
  /// against.
  std::vector<Matcher *> Matchers;

  /// The representative condition, with a type and a path (InsnVarID and OpIdx
  /// in most cases)  shared by all the matchers contained.
  std::unique_ptr<PredicateMatcher> Condition = nullptr;

  /// Temporary set used to check that the case values don't repeat within the
  /// same switch.
  std::set<MatchTableRecord> Values;

  /// An owning collection for any auxiliary matchers created while optimizing
  /// nested matchers contained.
  std::vector<std::unique_ptr<Matcher>> MatcherStorage;

public:
  bool addMatcher(Matcher &Candidate);

  void finalize();
  void emit(MatchTable &Table) override;

  iterator_range<std::vector<Matcher *>::iterator> matchers() {
    return make_range(Matchers.begin(), Matchers.end());
  }
  size_t size() const { return Matchers.size(); }
  bool empty() const { return Matchers.empty(); }

  std::unique_ptr<PredicateMatcher> popFirstCondition() override {
    // SwitchMatcher doesn't have a common first condition for its cases, as all
    // the cases only share a kind of a value (a type and a path to it) they
    // match, but deliberately differ in the actual value they match.
    llvm_unreachable("Trying to pop a condition from a condition-less group");
  }

  const PredicateMatcher &getFirstCondition() const override {
    llvm_unreachable("Trying to pop a condition from a condition-less group");
  }

  bool hasFirstCondition() const override { return false; }

private:
  /// See if the predicate type has a Switch-implementation for it.
  static bool isSupportedPredicateType(const PredicateMatcher &Predicate);

  bool candidateConditionMatches(const PredicateMatcher &Predicate) const;

  /// emit()-helper
  static void emitPredicateSpecificOpcodes(const PredicateMatcher &P,
                                           MatchTable &Table);
};

/// Generates code to check that a match rule matches.
class RuleMatcher : public Matcher {
public:
  using ActionList = std::list<std::unique_ptr<MatchAction>>;
  using action_iterator = ActionList::iterator;

protected:
  /// A list of matchers that all need to succeed for the current rule to match.
  /// FIXME: This currently supports a single match position but could be
  /// extended to support multiple positions to support div/rem fusion or
  /// load-multiple instructions.
  using MatchersTy = std::vector<std::unique_ptr<InstructionMatcher>>;
  MatchersTy Matchers;

  /// A list of actions that need to be taken when all predicates in this rule
  /// have succeeded.
  ActionList Actions;

  /// Combiners can sometimes just run C++ code to finish matching a rule &
  /// mutate instructions instead of relying on MatchActions. Empty if unused.
  std::string CustomCXXAction;

  using DefinedInsnVariablesMap = std::map<InstructionMatcher *, unsigned>;

  /// A map of instruction matchers to the local variables
  DefinedInsnVariablesMap InsnVariableIDs;

  using MutatableInsnSet = SmallPtrSet<InstructionMatcher *, 4>;

  // The set of instruction matchers that have not yet been claimed for mutation
  // by a BuildMI.
  MutatableInsnSet MutatableInsns;

  /// A map of named operands defined by the matchers that may be referenced by
  /// the renderers.
  StringMap<OperandMatcher *> DefinedOperands;

  /// A map of anonymous physical register operands defined by the matchers that
  /// may be referenced by the renderers.
  DenseMap<Record *, OperandMatcher *> PhysRegOperands;

  /// ID for the next instruction variable defined with
  /// implicitlyDefineInsnVar()
  unsigned NextInsnVarID;

  /// ID for the next output instruction allocated with allocateOutputInsnID()
  unsigned NextOutputInsnID;

  /// ID for the next temporary register ID allocated with allocateTempRegID()
  unsigned NextTempRegID;

  /// ID for the next recorded type. Starts at -1 and counts down.
  TempTypeIdx NextTempTypeIdx = -1;

  // HwMode predicate index for this rule. -1 if no HwMode.
  int HwModeIdx = -1;

  /// Current GISelFlags
  GISelFlags Flags = 0;

  std::vector<std::string> RequiredSimplePredicates;
  std::vector<Record *> RequiredFeatures;
  std::vector<std::unique_ptr<PredicateMatcher>> EpilogueMatchers;

  DenseSet<unsigned> ErasedInsnIDs;

  ArrayRef<SMLoc> SrcLoc;

  typedef std::tuple<Record *, unsigned, unsigned>
      DefinedComplexPatternSubOperand;
  typedef StringMap<DefinedComplexPatternSubOperand>
      DefinedComplexPatternSubOperandMap;
  /// A map of Symbolic Names to ComplexPattern sub-operands.
  DefinedComplexPatternSubOperandMap ComplexSubOperands;
  /// A map used to for multiple referenced error check of ComplexSubOperand.
  /// ComplexSubOperand can't be referenced multiple from different operands,
  /// however multiple references from same operand are allowed since that is
  /// how 'same operand checks' are generated.
  StringMap<std::string> ComplexSubOperandsParentName;

  uint64_t RuleID;
  static uint64_t NextRuleID;

  GISelFlags updateGISelFlag(GISelFlags CurFlags, const Record *R,
                             StringRef FlagName, GISelFlags FlagBit);

public:
  RuleMatcher(ArrayRef<SMLoc> SrcLoc)
      : NextInsnVarID(0), NextOutputInsnID(0), NextTempRegID(0), SrcLoc(SrcLoc),
        RuleID(NextRuleID++) {}
  RuleMatcher(RuleMatcher &&Other) = default;
  RuleMatcher &operator=(RuleMatcher &&Other) = default;

  TempTypeIdx getNextTempTypeIdx() { return NextTempTypeIdx--; }

  uint64_t getRuleID() const { return RuleID; }

  InstructionMatcher &addInstructionMatcher(StringRef SymbolicName);
  void addRequiredFeature(Record *Feature);
  const std::vector<Record *> &getRequiredFeatures() const;

  void addHwModeIdx(unsigned Idx) { HwModeIdx = Idx; }
  int getHwModeIdx() const { return HwModeIdx; }

  void addRequiredSimplePredicate(StringRef PredName);
  const std::vector<std::string> &getRequiredSimplePredicates();

  /// Attempts to mark \p ID as erased (GIR_EraseFromParent called on it).
  /// If \p ID has already been erased, returns false and GIR_EraseFromParent
  /// should NOT be emitted.
  bool tryEraseInsnID(unsigned ID) { return ErasedInsnIDs.insert(ID).second; }

  void setCustomCXXAction(StringRef FnEnumName) {
    CustomCXXAction = FnEnumName.str();
  }

  // Emplaces an action of the specified Kind at the end of the action list.
  //
  // Returns a reference to the newly created action.
  //
  // Like std::vector::emplace_back(), may invalidate all iterators if the new
  // size exceeds the capacity. Otherwise, only invalidates the past-the-end
  // iterator.
  template <class Kind, class... Args> Kind &addAction(Args &&...args) {
    Actions.emplace_back(std::make_unique<Kind>(std::forward<Args>(args)...));
    return *static_cast<Kind *>(Actions.back().get());
  }

  // Emplaces an action of the specified Kind before the given insertion point.
  //
  // Returns an iterator pointing at the newly created instruction.
  //
  // Like std::vector::insert(), may invalidate all iterators if the new size
  // exceeds the capacity. Otherwise, only invalidates the iterators from the
  // insertion point onwards.
  template <class Kind, class... Args>
  action_iterator insertAction(action_iterator InsertPt, Args &&...args) {
    return Actions.emplace(InsertPt,
                           std::make_unique<Kind>(std::forward<Args>(args)...));
  }

  void setPermanentGISelFlags(GISelFlags V) { Flags = V; }

  // Update the active GISelFlags based on the GISelFlags Record R.
  // A SaveAndRestore object is returned so the old GISelFlags are restored
  // at the end of the scope.
  SaveAndRestore<GISelFlags> setGISelFlags(const Record *R);
  GISelFlags getGISelFlags() const { return Flags; }

  /// Define an instruction without emitting any code to do so.
  unsigned implicitlyDefineInsnVar(InstructionMatcher &Matcher);

  unsigned getInsnVarID(InstructionMatcher &InsnMatcher) const;
  DefinedInsnVariablesMap::const_iterator defined_insn_vars_begin() const {
    return InsnVariableIDs.begin();
  }
  DefinedInsnVariablesMap::const_iterator defined_insn_vars_end() const {
    return InsnVariableIDs.end();
  }
  iterator_range<typename DefinedInsnVariablesMap::const_iterator>
  defined_insn_vars() const {
    return make_range(defined_insn_vars_begin(), defined_insn_vars_end());
  }

  MutatableInsnSet::const_iterator mutatable_insns_begin() const {
    return MutatableInsns.begin();
  }
  MutatableInsnSet::const_iterator mutatable_insns_end() const {
    return MutatableInsns.end();
  }
  iterator_range<typename MutatableInsnSet::const_iterator>
  mutatable_insns() const {
    return make_range(mutatable_insns_begin(), mutatable_insns_end());
  }
  void reserveInsnMatcherForMutation(InstructionMatcher *InsnMatcher) {
    bool R = MutatableInsns.erase(InsnMatcher);
    assert(R && "Reserving a mutatable insn that isn't available");
    (void)R;
  }

  action_iterator actions_begin() { return Actions.begin(); }
  action_iterator actions_end() { return Actions.end(); }
  iterator_range<action_iterator> actions() {
    return make_range(actions_begin(), actions_end());
  }

  void defineOperand(StringRef SymbolicName, OperandMatcher &OM);

  void definePhysRegOperand(Record *Reg, OperandMatcher &OM);

  Error defineComplexSubOperand(StringRef SymbolicName, Record *ComplexPattern,
                                unsigned RendererID, unsigned SubOperandID,
                                StringRef ParentSymbolicName);

  std::optional<DefinedComplexPatternSubOperand>
  getComplexSubOperand(StringRef SymbolicName) const {
    const auto &I = ComplexSubOperands.find(SymbolicName);
    if (I == ComplexSubOperands.end())
      return std::nullopt;
    return I->second;
  }

  InstructionMatcher &getInstructionMatcher(StringRef SymbolicName) const;
  OperandMatcher &getOperandMatcher(StringRef Name);
  const OperandMatcher &getOperandMatcher(StringRef Name) const;
  const OperandMatcher &getPhysRegOperandMatcher(Record *) const;

  void optimize() override;
  void emit(MatchTable &Table) override;

  /// Compare the priority of this object and B.
  ///
  /// Returns true if this object is more important than B.
  bool isHigherPriorityThan(const RuleMatcher &B) const;

  /// Report the maximum number of temporary operands needed by the rule
  /// matcher.
  unsigned countRendererFns() const;

  std::unique_ptr<PredicateMatcher> popFirstCondition() override;
  const PredicateMatcher &getFirstCondition() const override;
  LLTCodeGen getFirstConditionAsRootType();
  bool hasFirstCondition() const override;
  unsigned getNumOperands() const;
  StringRef getOpcode() const;

  // FIXME: Remove this as soon as possible
  InstructionMatcher &insnmatchers_front() const { return *Matchers.front(); }

  unsigned allocateOutputInsnID() { return NextOutputInsnID++; }
  unsigned allocateTempRegID() { return NextTempRegID++; }

  iterator_range<MatchersTy::iterator> insnmatchers() {
    return make_range(Matchers.begin(), Matchers.end());
  }
  bool insnmatchers_empty() const { return Matchers.empty(); }
  void insnmatchers_pop_front() { Matchers.erase(Matchers.begin()); }
};

template <class PredicateTy> class PredicateListMatcher {
private:
  /// Template instantiations should specialize this to return a string to use
  /// for the comment emitted when there are no predicates.
  std::string getNoPredicateComment() const;

protected:
  using PredicatesTy = std::deque<std::unique_ptr<PredicateTy>>;
  PredicatesTy Predicates;

  /// Track if the list of predicates was manipulated by one of the optimization
  /// methods.
  bool Optimized = false;

public:
  typename PredicatesTy::iterator predicates_begin() {
    return Predicates.begin();
  }
  typename PredicatesTy::iterator predicates_end() { return Predicates.end(); }
  iterator_range<typename PredicatesTy::iterator> predicates() {
    return make_range(predicates_begin(), predicates_end());
  }
  typename PredicatesTy::size_type predicates_size() const {
    return Predicates.size();
  }
  bool predicates_empty() const { return Predicates.empty(); }

  template <typename Ty> bool contains() const {
    return any_of(Predicates, [&](auto &P) { return isa<Ty>(P.get()); });
  }

  std::unique_ptr<PredicateTy> predicates_pop_front() {
    std::unique_ptr<PredicateTy> Front = std::move(Predicates.front());
    Predicates.pop_front();
    Optimized = true;
    return Front;
  }

  void prependPredicate(std::unique_ptr<PredicateTy> &&Predicate) {
    Predicates.push_front(std::move(Predicate));
  }

  void eraseNullPredicates() {
    const auto NewEnd =
        std::stable_partition(Predicates.begin(), Predicates.end(),
                              std::logical_not<std::unique_ptr<PredicateTy>>());
    if (NewEnd != Predicates.begin()) {
      Predicates.erase(Predicates.begin(), NewEnd);
      Optimized = true;
    }
  }

  /// Emit MatchTable opcodes that tests whether all the predicates are met.
  template <class... Args>
  void emitPredicateListOpcodes(MatchTable &Table, Args &&...args) {
    if (Predicates.empty() && !Optimized) {
      Table << MatchTable::Comment(getNoPredicateComment())
            << MatchTable::LineBreak;
      return;
    }

    for (const auto &Predicate : predicates())
      Predicate->emitPredicateOpcodes(Table, std::forward<Args>(args)...);
  }

  /// Provide a function to avoid emitting certain predicates. This is used to
  /// defer some predicate checks until after others
  using PredicateFilterFunc = std::function<bool(const PredicateTy &)>;

  /// Emit MatchTable opcodes for predicates which satisfy \p
  /// ShouldEmitPredicate. This should be called multiple times to ensure all
  /// predicates are eventually added to the match table.
  template <class... Args>
  void emitFilteredPredicateListOpcodes(PredicateFilterFunc ShouldEmitPredicate,
                                        MatchTable &Table, Args &&...args) {
    if (Predicates.empty() && !Optimized) {
      Table << MatchTable::Comment(getNoPredicateComment())
            << MatchTable::LineBreak;
      return;
    }

    for (const auto &Predicate : predicates()) {
      if (ShouldEmitPredicate(*Predicate))
        Predicate->emitPredicateOpcodes(Table, std::forward<Args>(args)...);
    }
  }
};

class PredicateMatcher {
public:
  /// This enum is used for RTTI and also defines the priority that is given to
  /// the predicate when generating the matcher code. Kinds with higher priority
  /// must be tested first.
  ///
  /// The relative priority of OPM_LLT, OPM_RegBank, and OPM_MBB do not matter
  /// but OPM_Int must have priority over OPM_RegBank since constant integers
  /// are represented by a virtual register defined by a G_CONSTANT instruction.
  ///
  /// Note: The relative priority between IPM_ and OPM_ does not matter, they
  /// are currently not compared between each other.
  enum PredicateKind {
    IPM_Opcode,
    IPM_NumOperands,
    IPM_ImmPredicate,
    IPM_Imm,
    IPM_AtomicOrderingMMO,
    IPM_MemoryLLTSize,
    IPM_MemoryVsLLTSize,
    IPM_MemoryAddressSpace,
    IPM_MemoryAlignment,
    IPM_VectorSplatImm,
    IPM_NoUse,
    IPM_OneUse,
    IPM_GenericPredicate,
    IPM_MIFlags,
    OPM_SameOperand,
    OPM_ComplexPattern,
    OPM_IntrinsicID,
    OPM_CmpPredicate,
    OPM_Instruction,
    OPM_Int,
    OPM_LiteralInt,
    OPM_LLT,
    OPM_PointerToAny,
    OPM_RegBank,
    OPM_MBB,
    OPM_RecordNamedOperand,
    OPM_RecordRegType,
  };

protected:
  PredicateKind Kind;
  unsigned InsnVarID;
  unsigned OpIdx;

public:
  PredicateMatcher(PredicateKind Kind, unsigned InsnVarID, unsigned OpIdx = ~0)
      : Kind(Kind), InsnVarID(InsnVarID), OpIdx(OpIdx) {}
  virtual ~PredicateMatcher();

  unsigned getInsnVarID() const { return InsnVarID; }
  unsigned getOpIdx() const { return OpIdx; }

  /// Emit MatchTable opcodes that check the predicate for the given operand.
  virtual void emitPredicateOpcodes(MatchTable &Table,
                                    RuleMatcher &Rule) const = 0;

  PredicateKind getKind() const { return Kind; }

  bool dependsOnOperands() const {
    // Custom predicates really depend on the context pattern of the
    // instruction, not just the individual instruction. This therefore
    // implicitly depends on all other pattern constraints.
    return Kind == IPM_GenericPredicate;
  }

  virtual bool isIdentical(const PredicateMatcher &B) const {
    return B.getKind() == getKind() && InsnVarID == B.InsnVarID &&
           OpIdx == B.OpIdx;
  }

  virtual bool isIdenticalDownToValue(const PredicateMatcher &B) const {
    return hasValue() && PredicateMatcher::isIdentical(B);
  }

  virtual MatchTableRecord getValue() const {
    assert(hasValue() && "Can not get a value of a value-less predicate!");
    llvm_unreachable("Not implemented yet");
  }
  virtual bool hasValue() const { return false; }

  /// Report the maximum number of temporary operands needed by the predicate
  /// matcher.
  virtual unsigned countRendererFns() const { return 0; }
};

/// Generates code to check a predicate of an operand.
///
/// Typical predicates include:
/// * Operand is a particular register.
/// * Operand is assigned a particular register bank.
/// * Operand is an MBB.
class OperandPredicateMatcher : public PredicateMatcher {
public:
  OperandPredicateMatcher(PredicateKind Kind, unsigned InsnVarID,
                          unsigned OpIdx)
      : PredicateMatcher(Kind, InsnVarID, OpIdx) {}
  virtual ~OperandPredicateMatcher();

  /// Compare the priority of this object and B.
  ///
  /// Returns true if this object is more important than B.
  virtual bool isHigherPriorityThan(const OperandPredicateMatcher &B) const;
};

template <>
inline std::string
PredicateListMatcher<OperandPredicateMatcher>::getNoPredicateComment() const {
  return "No operand predicates";
}

/// Generates code to check that a register operand is defined by the same exact
/// one as another.
class SameOperandMatcher : public OperandPredicateMatcher {
  std::string MatchingName;
  unsigned OrigOpIdx;

  GISelFlags Flags;

public:
  SameOperandMatcher(unsigned InsnVarID, unsigned OpIdx, StringRef MatchingName,
                     unsigned OrigOpIdx, GISelFlags Flags)
      : OperandPredicateMatcher(OPM_SameOperand, InsnVarID, OpIdx),
        MatchingName(MatchingName), OrigOpIdx(OrigOpIdx), Flags(Flags) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_SameOperand;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           OrigOpIdx == cast<SameOperandMatcher>(&B)->OrigOpIdx &&
           MatchingName == cast<SameOperandMatcher>(&B)->MatchingName;
  }
};

/// Generates code to check that an operand is a particular LLT.
class LLTOperandMatcher : public OperandPredicateMatcher {
protected:
  LLTCodeGen Ty;

public:
  static std::map<LLTCodeGen, unsigned> TypeIDValues;

  static void initTypeIDValuesMap() {
    TypeIDValues.clear();

    unsigned ID = 0;
    for (const LLTCodeGen &LLTy : KnownTypes)
      TypeIDValues[LLTy] = ID++;
  }

  LLTOperandMatcher(unsigned InsnVarID, unsigned OpIdx, const LLTCodeGen &Ty)
      : OperandPredicateMatcher(OPM_LLT, InsnVarID, OpIdx), Ty(Ty) {
    KnownTypes.insert(Ty);
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_LLT;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           Ty == cast<LLTOperandMatcher>(&B)->Ty;
  }

  MatchTableRecord getValue() const override;
  bool hasValue() const override;

  LLTCodeGen getTy() const { return Ty; }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that an operand is a pointer to any address space.
///
/// In SelectionDAG, the types did not describe pointers or address spaces. As a
/// result, iN is used to describe a pointer of N bits to any address space and
/// PatFrag predicates are typically used to constrain the address space.
/// There's no reliable means to derive the missing type information from the
/// pattern so imported rules must test the components of a pointer separately.
///
/// If SizeInBits is zero, then the pointer size will be obtained from the
/// subtarget.
class PointerToAnyOperandMatcher : public OperandPredicateMatcher {
protected:
  unsigned SizeInBits;

public:
  PointerToAnyOperandMatcher(unsigned InsnVarID, unsigned OpIdx,
                             unsigned SizeInBits)
      : OperandPredicateMatcher(OPM_PointerToAny, InsnVarID, OpIdx),
        SizeInBits(SizeInBits) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_PointerToAny;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           SizeInBits == cast<PointerToAnyOperandMatcher>(&B)->SizeInBits;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to record named operand in RecordedOperands list at StoreIdx.
/// Predicates with 'let PredicateCodeUsesOperands = 1' get RecordedOperands as
/// an argument to predicate's c++ code once all operands have been matched.
class RecordNamedOperandMatcher : public OperandPredicateMatcher {
protected:
  unsigned StoreIdx;
  std::string Name;

public:
  RecordNamedOperandMatcher(unsigned InsnVarID, unsigned OpIdx,
                            unsigned StoreIdx, StringRef Name)
      : OperandPredicateMatcher(OPM_RecordNamedOperand, InsnVarID, OpIdx),
        StoreIdx(StoreIdx), Name(Name) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_RecordNamedOperand;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           StoreIdx == cast<RecordNamedOperandMatcher>(&B)->StoreIdx &&
           Name == cast<RecordNamedOperandMatcher>(&B)->Name;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to store a register operand's type into the set of temporary
/// LLTs.
class RecordRegisterType : public OperandPredicateMatcher {
protected:
  TempTypeIdx Idx;

public:
  RecordRegisterType(unsigned InsnVarID, unsigned OpIdx, TempTypeIdx Idx)
      : OperandPredicateMatcher(OPM_RecordRegType, InsnVarID, OpIdx), Idx(Idx) {
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_RecordRegType;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           Idx == cast<RecordRegisterType>(&B)->Idx;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that an operand is a particular target constant.
class ComplexPatternOperandMatcher : public OperandPredicateMatcher {
protected:
  const OperandMatcher &Operand;
  const Record &TheDef;

  unsigned getAllocatedTemporariesBaseID() const;

public:
  bool isIdentical(const PredicateMatcher &B) const override { return false; }

  ComplexPatternOperandMatcher(unsigned InsnVarID, unsigned OpIdx,
                               const OperandMatcher &Operand,
                               const Record &TheDef)
      : OperandPredicateMatcher(OPM_ComplexPattern, InsnVarID, OpIdx),
        Operand(Operand), TheDef(TheDef) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_ComplexPattern;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
  unsigned countRendererFns() const override { return 1; }
};

/// Generates code to check that an operand is in a particular register bank.
class RegisterBankOperandMatcher : public OperandPredicateMatcher {
protected:
  const CodeGenRegisterClass &RC;

public:
  RegisterBankOperandMatcher(unsigned InsnVarID, unsigned OpIdx,
                             const CodeGenRegisterClass &RC)
      : OperandPredicateMatcher(OPM_RegBank, InsnVarID, OpIdx), RC(RC) {}

  bool isIdentical(const PredicateMatcher &B) const override;

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_RegBank;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that an operand is a basic block.
class MBBOperandMatcher : public OperandPredicateMatcher {
public:
  MBBOperandMatcher(unsigned InsnVarID, unsigned OpIdx)
      : OperandPredicateMatcher(OPM_MBB, InsnVarID, OpIdx) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_MBB;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

class ImmOperandMatcher : public OperandPredicateMatcher {
public:
  ImmOperandMatcher(unsigned InsnVarID, unsigned OpIdx)
      : OperandPredicateMatcher(IPM_Imm, InsnVarID, OpIdx) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_Imm;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that an operand is a G_CONSTANT with a particular
/// int.
class ConstantIntOperandMatcher : public OperandPredicateMatcher {
protected:
  int64_t Value;

public:
  ConstantIntOperandMatcher(unsigned InsnVarID, unsigned OpIdx, int64_t Value)
      : OperandPredicateMatcher(OPM_Int, InsnVarID, OpIdx), Value(Value) {}

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           Value == cast<ConstantIntOperandMatcher>(&B)->Value;
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_Int;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that an operand is a raw int (where MO.isImm() or
/// MO.isCImm() is true).
class LiteralIntOperandMatcher : public OperandPredicateMatcher {
protected:
  int64_t Value;

public:
  LiteralIntOperandMatcher(unsigned InsnVarID, unsigned OpIdx, int64_t Value)
      : OperandPredicateMatcher(OPM_LiteralInt, InsnVarID, OpIdx),
        Value(Value) {}

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           Value == cast<LiteralIntOperandMatcher>(&B)->Value;
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_LiteralInt;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that an operand is an CmpInst predicate
class CmpPredicateOperandMatcher : public OperandPredicateMatcher {
protected:
  std::string PredName;

public:
  CmpPredicateOperandMatcher(unsigned InsnVarID, unsigned OpIdx, std::string P)
      : OperandPredicateMatcher(OPM_CmpPredicate, InsnVarID, OpIdx),
        PredName(std::move(P)) {}

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           PredName == cast<CmpPredicateOperandMatcher>(&B)->PredName;
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_CmpPredicate;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that an operand is an intrinsic ID.
class IntrinsicIDOperandMatcher : public OperandPredicateMatcher {
protected:
  const CodeGenIntrinsic *II;

public:
  IntrinsicIDOperandMatcher(unsigned InsnVarID, unsigned OpIdx,
                            const CodeGenIntrinsic *II)
      : OperandPredicateMatcher(OPM_IntrinsicID, InsnVarID, OpIdx), II(II) {}

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           II == cast<IntrinsicIDOperandMatcher>(&B)->II;
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_IntrinsicID;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that this operand is an immediate whose value meets
/// an immediate predicate.
class OperandImmPredicateMatcher : public OperandPredicateMatcher {
protected:
  TreePredicateFn Predicate;

public:
  OperandImmPredicateMatcher(unsigned InsnVarID, unsigned OpIdx,
                             const TreePredicateFn &Predicate)
      : OperandPredicateMatcher(IPM_ImmPredicate, InsnVarID, OpIdx),
        Predicate(Predicate) {}

  bool isIdentical(const PredicateMatcher &B) const override {
    return OperandPredicateMatcher::isIdentical(B) &&
           Predicate.getOrigPatFragRecord() ==
               cast<OperandImmPredicateMatcher>(&B)
                   ->Predicate.getOrigPatFragRecord();
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_ImmPredicate;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that a set of predicates match for a particular
/// operand.
class OperandMatcher : public PredicateListMatcher<OperandPredicateMatcher> {
protected:
  InstructionMatcher &Insn;
  unsigned OpIdx;
  std::string SymbolicName;

  /// The index of the first temporary variable allocated to this operand. The
  /// number of allocated temporaries can be found with
  /// countRendererFns().
  unsigned AllocatedTemporariesBaseID;

  TempTypeIdx TTIdx = 0;

public:
  OperandMatcher(InstructionMatcher &Insn, unsigned OpIdx,
                 const std::string &SymbolicName,
                 unsigned AllocatedTemporariesBaseID)
      : Insn(Insn), OpIdx(OpIdx), SymbolicName(SymbolicName),
        AllocatedTemporariesBaseID(AllocatedTemporariesBaseID) {}

  bool hasSymbolicName() const { return !SymbolicName.empty(); }
  StringRef getSymbolicName() const { return SymbolicName; }
  void setSymbolicName(StringRef Name) {
    assert(SymbolicName.empty() && "Operand already has a symbolic name");
    SymbolicName = std::string(Name);
  }

  /// Construct a new operand predicate and add it to the matcher.
  template <class Kind, class... Args>
  std::optional<Kind *> addPredicate(Args &&...args) {
    if (isSameAsAnotherOperand())
      return std::nullopt;
    Predicates.emplace_back(std::make_unique<Kind>(
        getInsnVarID(), getOpIdx(), std::forward<Args>(args)...));
    return static_cast<Kind *>(Predicates.back().get());
  }

  unsigned getOpIdx() const { return OpIdx; }
  unsigned getInsnVarID() const;

  /// If this OperandMatcher has not been assigned a TempTypeIdx yet, assigns it
  /// one and adds a `RecordRegisterType` predicate to this matcher. If one has
  /// already been assigned, simply returns it.
  TempTypeIdx getTempTypeIdx(RuleMatcher &Rule);

  std::string getOperandExpr(unsigned InsnVarID) const;

  InstructionMatcher &getInstructionMatcher() const { return Insn; }

  Error addTypeCheckPredicate(const TypeSetByHwMode &VTy,
                              bool OperandIsAPointer);

  /// Emit MatchTable opcodes that test whether the instruction named in
  /// InsnVarID matches all the predicates and all the operands.
  void emitPredicateOpcodes(MatchTable &Table, RuleMatcher &Rule);

  /// Compare the priority of this object and B.
  ///
  /// Returns true if this object is more important than B.
  bool isHigherPriorityThan(OperandMatcher &B);

  /// Report the maximum number of temporary operands needed by the operand
  /// matcher.
  unsigned countRendererFns();

  unsigned getAllocatedTemporariesBaseID() const {
    return AllocatedTemporariesBaseID;
  }

  bool isSameAsAnotherOperand() {
    for (const auto &Predicate : predicates())
      if (isa<SameOperandMatcher>(Predicate))
        return true;
    return false;
  }
};

/// Generates code to check a predicate on an instruction.
///
/// Typical predicates include:
/// * The opcode of the instruction is a particular value.
/// * The nsw/nuw flag is/isn't set.
class InstructionPredicateMatcher : public PredicateMatcher {
public:
  InstructionPredicateMatcher(PredicateKind Kind, unsigned InsnVarID)
      : PredicateMatcher(Kind, InsnVarID) {}
  virtual ~InstructionPredicateMatcher() {}

  /// Compare the priority of this object and B.
  ///
  /// Returns true if this object is more important than B.
  virtual bool
  isHigherPriorityThan(const InstructionPredicateMatcher &B) const {
    return Kind < B.Kind;
  };
};

template <>
inline std::string
PredicateListMatcher<PredicateMatcher>::getNoPredicateComment() const {
  return "No instruction predicates";
}

/// Generates code to check the opcode of an instruction.
class InstructionOpcodeMatcher : public InstructionPredicateMatcher {
protected:
  // Allow matching one to several, similar opcodes that share properties. This
  // is to handle patterns where one SelectionDAG operation maps to multiple
  // GlobalISel ones (e.g. G_BUILD_VECTOR and G_BUILD_VECTOR_TRUNC). The first
  // is treated as the canonical opcode.
  SmallVector<const CodeGenInstruction *, 2> Insts;

  static DenseMap<const CodeGenInstruction *, unsigned> OpcodeValues;

  MatchTableRecord getInstValue(const CodeGenInstruction *I) const;

public:
  static void initOpcodeValuesMap(const CodeGenTarget &Target);

  InstructionOpcodeMatcher(unsigned InsnVarID,
                           ArrayRef<const CodeGenInstruction *> I)
      : InstructionPredicateMatcher(IPM_Opcode, InsnVarID),
        Insts(I.begin(), I.end()) {
    assert((Insts.size() == 1 || Insts.size() == 2) &&
           "unexpected number of opcode alternatives");
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_Opcode;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return InstructionPredicateMatcher::isIdentical(B) &&
           Insts == cast<InstructionOpcodeMatcher>(&B)->Insts;
  }

  bool hasValue() const override {
    return Insts.size() == 1 && OpcodeValues.count(Insts[0]);
  }

  // TODO: This is used for the SwitchMatcher optimization. We should be able to
  // return a list of the opcodes to match.
  MatchTableRecord getValue() const override;

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;

  /// Compare the priority of this object and B.
  ///
  /// Returns true if this object is more important than B.
  bool
  isHigherPriorityThan(const InstructionPredicateMatcher &B) const override;

  bool isConstantInstruction() const;

  // The first opcode is the canonical opcode, and later are alternatives.
  StringRef getOpcode() const;
  ArrayRef<const CodeGenInstruction *> getAlternativeOpcodes() { return Insts; }
  bool isVariadicNumOperands() const;
  StringRef getOperandType(unsigned OpIdx) const;
};

class InstructionNumOperandsMatcher final : public InstructionPredicateMatcher {
  unsigned NumOperands = 0;

public:
  InstructionNumOperandsMatcher(unsigned InsnVarID, unsigned NumOperands)
      : InstructionPredicateMatcher(IPM_NumOperands, InsnVarID),
        NumOperands(NumOperands) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_NumOperands;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return InstructionPredicateMatcher::isIdentical(B) &&
           NumOperands == cast<InstructionNumOperandsMatcher>(&B)->NumOperands;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that this instruction is a constant whose value
/// meets an immediate predicate.
///
/// Immediates are slightly odd since they are typically used like an operand
/// but are represented as an operator internally. We typically write simm8:$src
/// in a tablegen pattern, but this is just syntactic sugar for
/// (imm:i32)<<P:Predicate_simm8>>:$imm which more directly describes the nodes
/// that will be matched and the predicate (which is attached to the imm
/// operator) that will be tested. In SelectionDAG this describes a
/// ConstantSDNode whose internal value will be tested using the simm8
/// predicate.
///
/// The corresponding GlobalISel representation is %1 = G_CONSTANT iN Value. In
/// this representation, the immediate could be tested with an
/// InstructionMatcher, InstructionOpcodeMatcher, OperandMatcher, and a
/// OperandPredicateMatcher-subclass to check the Value meets the predicate but
/// there are two implementation issues with producing that matcher
/// configuration from the SelectionDAG pattern:
/// * ImmLeaf is a PatFrag whose root is an InstructionMatcher. This means that
///   were we to sink the immediate predicate to the operand we would have to
///   have two partial implementations of PatFrag support, one for immediates
///   and one for non-immediates.
/// * At the point we handle the predicate, the OperandMatcher hasn't been
///   created yet. If we were to sink the predicate to the OperandMatcher we
///   would also have to complicate (or duplicate) the code that descends and
///   creates matchers for the subtree.
/// Overall, it's simpler to handle it in the place it was found.
class InstructionImmPredicateMatcher : public InstructionPredicateMatcher {
protected:
  TreePredicateFn Predicate;

public:
  InstructionImmPredicateMatcher(unsigned InsnVarID,
                                 const TreePredicateFn &Predicate)
      : InstructionPredicateMatcher(IPM_ImmPredicate, InsnVarID),
        Predicate(Predicate) {}

  bool isIdentical(const PredicateMatcher &B) const override;

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_ImmPredicate;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that a memory instruction has a atomic ordering
/// MachineMemoryOperand.
class AtomicOrderingMMOPredicateMatcher : public InstructionPredicateMatcher {
public:
  enum AOComparator {
    AO_Exactly,
    AO_OrStronger,
    AO_WeakerThan,
  };

protected:
  StringRef Order;
  AOComparator Comparator;

public:
  AtomicOrderingMMOPredicateMatcher(unsigned InsnVarID, StringRef Order,
                                    AOComparator Comparator = AO_Exactly)
      : InstructionPredicateMatcher(IPM_AtomicOrderingMMO, InsnVarID),
        Order(Order), Comparator(Comparator) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_AtomicOrderingMMO;
  }

  bool isIdentical(const PredicateMatcher &B) const override;

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that the size of an MMO is exactly N bytes.
class MemorySizePredicateMatcher : public InstructionPredicateMatcher {
protected:
  unsigned MMOIdx;
  uint64_t Size;

public:
  MemorySizePredicateMatcher(unsigned InsnVarID, unsigned MMOIdx, unsigned Size)
      : InstructionPredicateMatcher(IPM_MemoryLLTSize, InsnVarID),
        MMOIdx(MMOIdx), Size(Size) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_MemoryLLTSize;
  }
  bool isIdentical(const PredicateMatcher &B) const override {
    return InstructionPredicateMatcher::isIdentical(B) &&
           MMOIdx == cast<MemorySizePredicateMatcher>(&B)->MMOIdx &&
           Size == cast<MemorySizePredicateMatcher>(&B)->Size;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

class MemoryAddressSpacePredicateMatcher : public InstructionPredicateMatcher {
protected:
  unsigned MMOIdx;
  SmallVector<unsigned, 4> AddrSpaces;

public:
  MemoryAddressSpacePredicateMatcher(unsigned InsnVarID, unsigned MMOIdx,
                                     ArrayRef<unsigned> AddrSpaces)
      : InstructionPredicateMatcher(IPM_MemoryAddressSpace, InsnVarID),
        MMOIdx(MMOIdx), AddrSpaces(AddrSpaces.begin(), AddrSpaces.end()) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_MemoryAddressSpace;
  }

  bool isIdentical(const PredicateMatcher &B) const override;

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

class MemoryAlignmentPredicateMatcher : public InstructionPredicateMatcher {
protected:
  unsigned MMOIdx;
  int MinAlign;

public:
  MemoryAlignmentPredicateMatcher(unsigned InsnVarID, unsigned MMOIdx,
                                  int MinAlign)
      : InstructionPredicateMatcher(IPM_MemoryAlignment, InsnVarID),
        MMOIdx(MMOIdx), MinAlign(MinAlign) {
    assert(MinAlign > 0);
  }

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_MemoryAlignment;
  }

  bool isIdentical(const PredicateMatcher &B) const override;

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check that the size of an MMO is less-than, equal-to, or
/// greater than a given LLT.
class MemoryVsLLTSizePredicateMatcher : public InstructionPredicateMatcher {
public:
  enum RelationKind {
    GreaterThan,
    EqualTo,
    LessThan,
  };

protected:
  unsigned MMOIdx;
  RelationKind Relation;
  unsigned OpIdx;

public:
  MemoryVsLLTSizePredicateMatcher(unsigned InsnVarID, unsigned MMOIdx,
                                  enum RelationKind Relation, unsigned OpIdx)
      : InstructionPredicateMatcher(IPM_MemoryVsLLTSize, InsnVarID),
        MMOIdx(MMOIdx), Relation(Relation), OpIdx(OpIdx) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_MemoryVsLLTSize;
  }
  bool isIdentical(const PredicateMatcher &B) const override;

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

// Matcher for immAllOnesV/immAllZerosV
class VectorSplatImmPredicateMatcher : public InstructionPredicateMatcher {
public:
  enum SplatKind { AllZeros, AllOnes };

private:
  SplatKind Kind;

public:
  VectorSplatImmPredicateMatcher(unsigned InsnVarID, SplatKind K)
      : InstructionPredicateMatcher(IPM_VectorSplatImm, InsnVarID), Kind(K) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_VectorSplatImm;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return InstructionPredicateMatcher::isIdentical(B) &&
           Kind == static_cast<const VectorSplatImmPredicateMatcher &>(B).Kind;
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check an arbitrary C++ instruction predicate.
class GenericInstructionPredicateMatcher : public InstructionPredicateMatcher {
protected:
  std::string EnumVal;

public:
  GenericInstructionPredicateMatcher(unsigned InsnVarID,
                                     TreePredicateFn Predicate);

  GenericInstructionPredicateMatcher(unsigned InsnVarID,
                                     const std::string &EnumVal)
      : InstructionPredicateMatcher(IPM_GenericPredicate, InsnVarID),
        EnumVal(EnumVal) {}

  static bool classof(const InstructionPredicateMatcher *P) {
    return P->getKind() == IPM_GenericPredicate;
  }
  bool isIdentical(const PredicateMatcher &B) const override;
  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

class MIFlagsInstructionPredicateMatcher : public InstructionPredicateMatcher {
  SmallVector<StringRef, 2> Flags;
  bool CheckNot; // false = GIM_MIFlags, true = GIM_MIFlagsNot

public:
  MIFlagsInstructionPredicateMatcher(unsigned InsnVarID,
                                     ArrayRef<StringRef> FlagsToCheck,
                                     bool CheckNot = false)
      : InstructionPredicateMatcher(IPM_MIFlags, InsnVarID),
        Flags(FlagsToCheck), CheckNot(CheckNot) {
    sort(Flags);
  }

  static bool classof(const InstructionPredicateMatcher *P) {
    return P->getKind() == IPM_MIFlags;
  }

  bool isIdentical(const PredicateMatcher &B) const override;
  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override;
};

/// Generates code to check for the absence of use of the result.
// TODO? Generalize this to support checking for one use.
class NoUsePredicateMatcher : public InstructionPredicateMatcher {
public:
  NoUsePredicateMatcher(unsigned InsnVarID)
      : InstructionPredicateMatcher(IPM_NoUse, InsnVarID) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_NoUse;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return InstructionPredicateMatcher::isIdentical(B);
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override {
    Table << MatchTable::Opcode("GIM_CheckHasNoUse")
          << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
          << MatchTable::LineBreak;
  }
};

/// Generates code to check that the first result has only one use.
class OneUsePredicateMatcher : public InstructionPredicateMatcher {
public:
  OneUsePredicateMatcher(unsigned InsnVarID)
      : InstructionPredicateMatcher(IPM_OneUse, InsnVarID) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == IPM_OneUse;
  }

  bool isIdentical(const PredicateMatcher &B) const override {
    return InstructionPredicateMatcher::isIdentical(B);
  }

  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override {
    Table << MatchTable::Opcode("GIM_CheckHasOneUse")
          << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
          << MatchTable::LineBreak;
  }
};

/// Generates code to check that a set of predicates and operands match for a
/// particular instruction.
///
/// Typical predicates include:
/// * Has a specific opcode.
/// * Has an nsw/nuw flag or doesn't.
class InstructionMatcher final : public PredicateListMatcher<PredicateMatcher> {
protected:
  typedef std::vector<std::unique_ptr<OperandMatcher>> OperandVec;

  RuleMatcher &Rule;

  /// The operands to match. All rendered operands must be present even if the
  /// condition is always true.
  OperandVec Operands;
  bool NumOperandsCheck = true;

  std::string SymbolicName;
  unsigned InsnVarID;

  /// PhysRegInputs - List list has an entry for each explicitly specified
  /// physreg input to the pattern.  The first elt is the Register node, the
  /// second is the recorded slot number the input pattern match saved it in.
  SmallVector<std::pair<Record *, unsigned>, 2> PhysRegInputs;

public:
  InstructionMatcher(RuleMatcher &Rule, StringRef SymbolicName,
                     bool NumOpsCheck = true)
      : Rule(Rule), NumOperandsCheck(NumOpsCheck), SymbolicName(SymbolicName) {
    // We create a new instruction matcher.
    // Get a new ID for that instruction.
    InsnVarID = Rule.implicitlyDefineInsnVar(*this);
  }

  /// Construct a new instruction predicate and add it to the matcher.
  template <class Kind, class... Args>
  std::optional<Kind *> addPredicate(Args &&...args) {
    Predicates.emplace_back(
        std::make_unique<Kind>(getInsnVarID(), std::forward<Args>(args)...));
    return static_cast<Kind *>(Predicates.back().get());
  }

  RuleMatcher &getRuleMatcher() const { return Rule; }

  unsigned getInsnVarID() const { return InsnVarID; }

  /// Add an operand to the matcher.
  OperandMatcher &addOperand(unsigned OpIdx, const std::string &SymbolicName,
                             unsigned AllocatedTemporariesBaseID);
  OperandMatcher &getOperand(unsigned OpIdx);
  OperandMatcher &addPhysRegInput(Record *Reg, unsigned OpIdx,
                                  unsigned TempOpIdx);

  ArrayRef<std::pair<Record *, unsigned>> getPhysRegInputs() const {
    return PhysRegInputs;
  }

  StringRef getSymbolicName() const { return SymbolicName; }
  unsigned getNumOperands() const { return Operands.size(); }
  OperandVec::iterator operands_begin() { return Operands.begin(); }
  OperandVec::iterator operands_end() { return Operands.end(); }
  iterator_range<OperandVec::iterator> operands() {
    return make_range(operands_begin(), operands_end());
  }
  OperandVec::const_iterator operands_begin() const { return Operands.begin(); }
  OperandVec::const_iterator operands_end() const { return Operands.end(); }
  iterator_range<OperandVec::const_iterator> operands() const {
    return make_range(operands_begin(), operands_end());
  }
  bool operands_empty() const { return Operands.empty(); }

  void pop_front() { Operands.erase(Operands.begin()); }

  void optimize();

  /// Emit MatchTable opcodes that test whether the instruction named in
  /// InsnVarName matches all the predicates and all the operands.
  void emitPredicateOpcodes(MatchTable &Table, RuleMatcher &Rule);

  /// Compare the priority of this object and B.
  ///
  /// Returns true if this object is more important than B.
  bool isHigherPriorityThan(InstructionMatcher &B);

  /// Report the maximum number of temporary operands needed by the instruction
  /// matcher.
  unsigned countRendererFns();

  InstructionOpcodeMatcher &getOpcodeMatcher() {
    for (auto &P : predicates())
      if (auto *OpMatcher = dyn_cast<InstructionOpcodeMatcher>(P.get()))
        return *OpMatcher;
    llvm_unreachable("Didn't find an opcode matcher");
  }

  bool isConstantInstruction() {
    return getOpcodeMatcher().isConstantInstruction();
  }

  StringRef getOpcode() { return getOpcodeMatcher().getOpcode(); }
};

/// Generates code to check that the operand is a register defined by an
/// instruction that matches the given instruction matcher.
///
/// For example, the pattern:
///   (set $dst, (G_MUL (G_ADD $src1, $src2), $src3))
/// would use an InstructionOperandMatcher for operand 1 of the G_MUL to match
/// the:
///   (G_ADD $src1, $src2)
/// subpattern.
class InstructionOperandMatcher : public OperandPredicateMatcher {
protected:
  std::unique_ptr<InstructionMatcher> InsnMatcher;

  GISelFlags Flags;

public:
  InstructionOperandMatcher(unsigned InsnVarID, unsigned OpIdx,
                            RuleMatcher &Rule, StringRef SymbolicName,
                            bool NumOpsCheck = true)
      : OperandPredicateMatcher(OPM_Instruction, InsnVarID, OpIdx),
        InsnMatcher(new InstructionMatcher(Rule, SymbolicName, NumOpsCheck)),
        Flags(Rule.getGISelFlags()) {}

  static bool classof(const PredicateMatcher *P) {
    return P->getKind() == OPM_Instruction;
  }

  InstructionMatcher &getInsnMatcher() const { return *InsnMatcher; }

  void emitCaptureOpcodes(MatchTable &Table, RuleMatcher &Rule) const;
  void emitPredicateOpcodes(MatchTable &Table,
                            RuleMatcher &Rule) const override {
    emitCaptureOpcodes(Table, Rule);
    InsnMatcher->emitPredicateOpcodes(Table, Rule);
  }

  bool isHigherPriorityThan(const OperandPredicateMatcher &B) const override;

  /// Report the maximum number of temporary operands needed by the predicate
  /// matcher.
  unsigned countRendererFns() const override {
    return InsnMatcher->countRendererFns();
  }
};

//===- Actions ------------------------------------------------------------===//
class OperandRenderer {
public:
  enum RendererKind {
    OR_Copy,
    OR_CopyOrAddZeroReg,
    OR_CopySubReg,
    OR_CopyPhysReg,
    OR_CopyConstantAsImm,
    OR_CopyFConstantAsFPImm,
    OR_Imm,
    OR_SubRegIndex,
    OR_Register,
    OR_TempRegister,
    OR_ComplexPattern,
    OR_Intrinsic,
    OR_Custom,
    OR_CustomOperand
  };

protected:
  RendererKind Kind;

public:
  OperandRenderer(RendererKind Kind) : Kind(Kind) {}
  virtual ~OperandRenderer();

  RendererKind getKind() const { return Kind; }

  virtual void emitRenderOpcodes(MatchTable &Table,
                                 RuleMatcher &Rule) const = 0;
};

/// A CopyRenderer emits code to copy a single operand from an existing
/// instruction to the one being built.
class CopyRenderer : public OperandRenderer {
protected:
  unsigned NewInsnID;
  /// The name of the operand.
  const StringRef SymbolicName;

public:
  CopyRenderer(unsigned NewInsnID, StringRef SymbolicName)
      : OperandRenderer(OR_Copy), NewInsnID(NewInsnID),
        SymbolicName(SymbolicName) {
    assert(!SymbolicName.empty() && "Cannot copy from an unspecified source");
  }

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_Copy;
  }

  StringRef getSymbolicName() const { return SymbolicName; }

  static void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule,
                                unsigned NewInsnID, unsigned OldInsnID,
                                unsigned OpIdx, StringRef Name);

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// A CopyRenderer emits code to copy a virtual register to a specific physical
/// register.
class CopyPhysRegRenderer : public OperandRenderer {
protected:
  unsigned NewInsnID;
  Record *PhysReg;

public:
  CopyPhysRegRenderer(unsigned NewInsnID, Record *Reg)
      : OperandRenderer(OR_CopyPhysReg), NewInsnID(NewInsnID), PhysReg(Reg) {
    assert(PhysReg);
  }

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_CopyPhysReg;
  }

  Record *getPhysReg() const { return PhysReg; }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// A CopyOrAddZeroRegRenderer emits code to copy a single operand from an
/// existing instruction to the one being built. If the operand turns out to be
/// a 'G_CONSTANT 0' then it replaces the operand with a zero register.
class CopyOrAddZeroRegRenderer : public OperandRenderer {
protected:
  unsigned NewInsnID;
  /// The name of the operand.
  const StringRef SymbolicName;
  const Record *ZeroRegisterDef;

public:
  CopyOrAddZeroRegRenderer(unsigned NewInsnID, StringRef SymbolicName,
                           Record *ZeroRegisterDef)
      : OperandRenderer(OR_CopyOrAddZeroReg), NewInsnID(NewInsnID),
        SymbolicName(SymbolicName), ZeroRegisterDef(ZeroRegisterDef) {
    assert(!SymbolicName.empty() && "Cannot copy from an unspecified source");
  }

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_CopyOrAddZeroReg;
  }

  StringRef getSymbolicName() const { return SymbolicName; }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// A CopyConstantAsImmRenderer emits code to render a G_CONSTANT instruction to
/// an extended immediate operand.
class CopyConstantAsImmRenderer : public OperandRenderer {
protected:
  unsigned NewInsnID;
  /// The name of the operand.
  const std::string SymbolicName;
  bool Signed;

public:
  CopyConstantAsImmRenderer(unsigned NewInsnID, StringRef SymbolicName)
      : OperandRenderer(OR_CopyConstantAsImm), NewInsnID(NewInsnID),
        SymbolicName(SymbolicName), Signed(true) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_CopyConstantAsImm;
  }

  StringRef getSymbolicName() const { return SymbolicName; }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// A CopyFConstantAsFPImmRenderer emits code to render a G_FCONSTANT
/// instruction to an extended immediate operand.
class CopyFConstantAsFPImmRenderer : public OperandRenderer {
protected:
  unsigned NewInsnID;
  /// The name of the operand.
  const std::string SymbolicName;

public:
  CopyFConstantAsFPImmRenderer(unsigned NewInsnID, StringRef SymbolicName)
      : OperandRenderer(OR_CopyFConstantAsFPImm), NewInsnID(NewInsnID),
        SymbolicName(SymbolicName) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_CopyFConstantAsFPImm;
  }

  StringRef getSymbolicName() const { return SymbolicName; }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// A CopySubRegRenderer emits code to copy a single register operand from an
/// existing instruction to the one being built and indicate that only a
/// subregister should be copied.
class CopySubRegRenderer : public OperandRenderer {
protected:
  unsigned NewInsnID;
  /// The name of the operand.
  const StringRef SymbolicName;
  /// The subregister to extract.
  const CodeGenSubRegIndex *SubReg;

public:
  CopySubRegRenderer(unsigned NewInsnID, StringRef SymbolicName,
                     const CodeGenSubRegIndex *SubReg)
      : OperandRenderer(OR_CopySubReg), NewInsnID(NewInsnID),
        SymbolicName(SymbolicName), SubReg(SubReg) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_CopySubReg;
  }

  StringRef getSymbolicName() const { return SymbolicName; }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Adds a specific physical register to the instruction being built.
/// This is typically useful for WZR/XZR on AArch64.
class AddRegisterRenderer : public OperandRenderer {
protected:
  unsigned InsnID;
  const Record *RegisterDef;
  bool IsDef;
  const CodeGenTarget &Target;

public:
  AddRegisterRenderer(unsigned InsnID, const CodeGenTarget &Target,
                      const Record *RegisterDef, bool IsDef = false)
      : OperandRenderer(OR_Register), InsnID(InsnID), RegisterDef(RegisterDef),
        IsDef(IsDef), Target(Target) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_Register;
  }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Adds a specific temporary virtual register to the instruction being built.
/// This is used to chain instructions together when emitting multiple
/// instructions.
class TempRegRenderer : public OperandRenderer {
protected:
  unsigned InsnID;
  unsigned TempRegID;
  const CodeGenSubRegIndex *SubRegIdx;
  bool IsDef;
  bool IsDead;

public:
  TempRegRenderer(unsigned InsnID, unsigned TempRegID, bool IsDef = false,
                  const CodeGenSubRegIndex *SubReg = nullptr,
                  bool IsDead = false)
      : OperandRenderer(OR_Register), InsnID(InsnID), TempRegID(TempRegID),
        SubRegIdx(SubReg), IsDef(IsDef), IsDead(IsDead) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_TempRegister;
  }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Adds a specific immediate to the instruction being built.
/// If a LLT is passed, a ConstantInt immediate is created instead.
class ImmRenderer : public OperandRenderer {
protected:
  unsigned InsnID;
  int64_t Imm;
  std::optional<LLTCodeGenOrTempType> CImmLLT;

public:
  ImmRenderer(unsigned InsnID, int64_t Imm)
      : OperandRenderer(OR_Imm), InsnID(InsnID), Imm(Imm) {}

  ImmRenderer(unsigned InsnID, int64_t Imm, const LLTCodeGenOrTempType &CImmLLT)
      : OperandRenderer(OR_Imm), InsnID(InsnID), Imm(Imm), CImmLLT(CImmLLT) {
    if (CImmLLT.isLLTCodeGen())
      KnownTypes.insert(CImmLLT.getLLTCodeGen());
  }

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_Imm;
  }

  static void emitAddImm(MatchTable &Table, RuleMatcher &RM, unsigned InsnID,
                         int64_t Imm, StringRef ImmName = "Imm");

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Adds an enum value for a subreg index to the instruction being built.
class SubRegIndexRenderer : public OperandRenderer {
protected:
  unsigned InsnID;
  const CodeGenSubRegIndex *SubRegIdx;

public:
  SubRegIndexRenderer(unsigned InsnID, const CodeGenSubRegIndex *SRI)
      : OperandRenderer(OR_SubRegIndex), InsnID(InsnID), SubRegIdx(SRI) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_SubRegIndex;
  }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Adds operands by calling a renderer function supplied by the ComplexPattern
/// matcher function.
class RenderComplexPatternOperand : public OperandRenderer {
private:
  unsigned InsnID;
  const Record &TheDef;
  /// The name of the operand.
  const StringRef SymbolicName;
  /// The renderer number. This must be unique within a rule since it's used to
  /// identify a temporary variable to hold the renderer function.
  unsigned RendererID;
  /// When provided, this is the suboperand of the ComplexPattern operand to
  /// render. Otherwise all the suboperands will be rendered.
  std::optional<unsigned> SubOperand;
  /// The subregister to extract. Render the whole register if not specified.
  const CodeGenSubRegIndex *SubReg;

  unsigned getNumOperands() const {
    return TheDef.getValueAsDag("Operands")->getNumArgs();
  }

public:
  RenderComplexPatternOperand(unsigned InsnID, const Record &TheDef,
                              StringRef SymbolicName, unsigned RendererID,
                              std::optional<unsigned> SubOperand = std::nullopt,
                              const CodeGenSubRegIndex *SubReg = nullptr)
      : OperandRenderer(OR_ComplexPattern), InsnID(InsnID), TheDef(TheDef),
        SymbolicName(SymbolicName), RendererID(RendererID),
        SubOperand(SubOperand), SubReg(SubReg) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_ComplexPattern;
  }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Adds an intrinsic ID operand to the instruction being built.
class IntrinsicIDRenderer : public OperandRenderer {
protected:
  unsigned InsnID;
  const CodeGenIntrinsic *II;

public:
  IntrinsicIDRenderer(unsigned InsnID, const CodeGenIntrinsic *II)
      : OperandRenderer(OR_Intrinsic), InsnID(InsnID), II(II) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_Intrinsic;
  }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

class CustomRenderer : public OperandRenderer {
protected:
  unsigned InsnID;
  const Record &Renderer;
  /// The name of the operand.
  const std::string SymbolicName;

public:
  CustomRenderer(unsigned InsnID, const Record &Renderer,
                 StringRef SymbolicName)
      : OperandRenderer(OR_Custom), InsnID(InsnID), Renderer(Renderer),
        SymbolicName(SymbolicName) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_Custom;
  }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

class CustomOperandRenderer : public OperandRenderer {
protected:
  unsigned InsnID;
  const Record &Renderer;
  /// The name of the operand.
  const std::string SymbolicName;

public:
  CustomOperandRenderer(unsigned InsnID, const Record &Renderer,
                        StringRef SymbolicName)
      : OperandRenderer(OR_CustomOperand), InsnID(InsnID), Renderer(Renderer),
        SymbolicName(SymbolicName) {}

  static bool classof(const OperandRenderer *R) {
    return R->getKind() == OR_CustomOperand;
  }

  void emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// An action taken when all Matcher predicates succeeded for a parent rule.
///
/// Typical actions include:
/// * Changing the opcode of an instruction.
/// * Adding an operand to an instruction.
class MatchAction {
public:
  enum ActionKind {
    AK_DebugComment,
    AK_BuildMI,
    AK_BuildConstantMI,
    AK_EraseInst,
    AK_ReplaceReg,
    AK_ConstraintOpsToDef,
    AK_ConstraintOpsToRC,
    AK_MakeTempReg,
  };

  MatchAction(ActionKind K) : Kind(K) {}

  ActionKind getKind() const { return Kind; }

  virtual ~MatchAction() {}

  // Some actions may need to add extra predicates to ensure they can run.
  virtual void emitAdditionalPredicates(MatchTable &Table,
                                        RuleMatcher &Rule) const {}

  /// Emit the MatchTable opcodes to implement the action.
  virtual void emitActionOpcodes(MatchTable &Table,
                                 RuleMatcher &Rule) const = 0;

  /// If this opcode has an overload that can call GIR_Done directly, emit that
  /// instead of the usual opcode and return "true". Return "false" if GIR_Done
  /// still needs to be emitted.
  virtual bool emitActionOpcodesAndDone(MatchTable &Table,
                                        RuleMatcher &Rule) const {
    emitActionOpcodes(Table, Rule);
    return false;
  }

private:
  ActionKind Kind;
};

/// Generates a comment describing the matched rule being acted upon.
class DebugCommentAction : public MatchAction {
private:
  std::string S;

public:
  DebugCommentAction(StringRef S)
      : MatchAction(AK_DebugComment), S(std::string(S)) {}

  static bool classof(const MatchAction *A) {
    return A->getKind() == AK_DebugComment;
  }

  void emitActionOpcodes(MatchTable &Table, RuleMatcher &Rule) const override {
    Table << MatchTable::Comment(S) << MatchTable::LineBreak;
  }
};

/// Generates code to build an instruction or mutate an existing instruction
/// into the desired instruction when this is possible.
class BuildMIAction : public MatchAction {
private:
  unsigned InsnID;
  const CodeGenInstruction *I;
  InstructionMatcher *Matched;
  std::vector<std::unique_ptr<OperandRenderer>> OperandRenderers;
  SmallPtrSet<Record *, 4> DeadImplicitDefs;

  std::vector<const InstructionMatcher *> CopiedFlags;
  std::vector<StringRef> SetFlags;
  std::vector<StringRef> UnsetFlags;

  /// True if the instruction can be built solely by mutating the opcode.
  bool canMutate(RuleMatcher &Rule, const InstructionMatcher *Insn) const;

public:
  BuildMIAction(unsigned InsnID, const CodeGenInstruction *I)
      : MatchAction(AK_BuildMI), InsnID(InsnID), I(I), Matched(nullptr) {}

  static bool classof(const MatchAction *A) {
    return A->getKind() == AK_BuildMI;
  }

  unsigned getInsnID() const { return InsnID; }
  const CodeGenInstruction *getCGI() const { return I; }

  void addSetMIFlags(StringRef Flag) { SetFlags.push_back(Flag); }
  void addUnsetMIFlags(StringRef Flag) { UnsetFlags.push_back(Flag); }
  void addCopiedMIFlags(const InstructionMatcher &IM) {
    CopiedFlags.push_back(&IM);
  }

  void chooseInsnToMutate(RuleMatcher &Rule);

  void setDeadImplicitDef(Record *R) { DeadImplicitDefs.insert(R); }

  template <class Kind, class... Args> Kind &addRenderer(Args &&...args) {
    OperandRenderers.emplace_back(
        std::make_unique<Kind>(InsnID, std::forward<Args>(args)...));
    return *static_cast<Kind *>(OperandRenderers.back().get());
  }

  void emitActionOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Generates code to create a constant that defines a TempReg.
/// The instruction created is usually a G_CONSTANT but it could also be a
/// G_BUILD_VECTOR for vector types.
class BuildConstantAction : public MatchAction {
  unsigned TempRegID;
  int64_t Val;

public:
  BuildConstantAction(unsigned TempRegID, int64_t Val)
      : MatchAction(AK_BuildConstantMI), TempRegID(TempRegID), Val(Val) {}

  static bool classof(const MatchAction *A) {
    return A->getKind() == AK_BuildConstantMI;
  }

  void emitActionOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

class EraseInstAction : public MatchAction {
  unsigned InsnID;

public:
  EraseInstAction(unsigned InsnID)
      : MatchAction(AK_EraseInst), InsnID(InsnID) {}

  unsigned getInsnID() const { return InsnID; }

  static bool classof(const MatchAction *A) {
    return A->getKind() == AK_EraseInst;
  }

  void emitActionOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
  bool emitActionOpcodesAndDone(MatchTable &Table,
                                RuleMatcher &Rule) const override;
};

class ReplaceRegAction : public MatchAction {
  unsigned OldInsnID, OldOpIdx;
  unsigned NewInsnId = -1, NewOpIdx;
  unsigned TempRegID = -1;

public:
  ReplaceRegAction(unsigned OldInsnID, unsigned OldOpIdx, unsigned NewInsnId,
                   unsigned NewOpIdx)
      : MatchAction(AK_ReplaceReg), OldInsnID(OldInsnID), OldOpIdx(OldOpIdx),
        NewInsnId(NewInsnId), NewOpIdx(NewOpIdx) {}

  ReplaceRegAction(unsigned OldInsnID, unsigned OldOpIdx, unsigned TempRegID)
      : MatchAction(AK_ReplaceReg), OldInsnID(OldInsnID), OldOpIdx(OldOpIdx),
        TempRegID(TempRegID) {}

  static bool classof(const MatchAction *A) {
    return A->getKind() == AK_ReplaceReg;
  }

  void emitAdditionalPredicates(MatchTable &Table,
                                RuleMatcher &Rule) const override;
  void emitActionOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Generates code to constrain the operands of an output instruction to the
/// register classes specified by the definition of that instruction.
class ConstrainOperandsToDefinitionAction : public MatchAction {
  unsigned InsnID;

public:
  ConstrainOperandsToDefinitionAction(unsigned InsnID)
      : MatchAction(AK_ConstraintOpsToDef), InsnID(InsnID) {}

  static bool classof(const MatchAction *A) {
    return A->getKind() == AK_ConstraintOpsToDef;
  }

  void emitActionOpcodes(MatchTable &Table, RuleMatcher &Rule) const override {
    if (InsnID == 0) {
      Table << MatchTable::Opcode("GIR_RootConstrainSelectedInstOperands")
            << MatchTable::LineBreak;
    } else {
      Table << MatchTable::Opcode("GIR_ConstrainSelectedInstOperands")
            << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
            << MatchTable::LineBreak;
    }
  }
};

/// Generates code to constrain the specified operand of an output instruction
/// to the specified register class.
class ConstrainOperandToRegClassAction : public MatchAction {
  unsigned InsnID;
  unsigned OpIdx;
  const CodeGenRegisterClass &RC;

public:
  ConstrainOperandToRegClassAction(unsigned InsnID, unsigned OpIdx,
                                   const CodeGenRegisterClass &RC)
      : MatchAction(AK_ConstraintOpsToRC), InsnID(InsnID), OpIdx(OpIdx),
        RC(RC) {}

  static bool classof(const MatchAction *A) {
    return A->getKind() == AK_ConstraintOpsToRC;
  }

  void emitActionOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

/// Generates code to create a temporary register which can be used to chain
/// instructions together.
class MakeTempRegisterAction : public MatchAction {
private:
  LLTCodeGenOrTempType Ty;
  unsigned TempRegID;

public:
  MakeTempRegisterAction(const LLTCodeGenOrTempType &Ty, unsigned TempRegID)
      : MatchAction(AK_MakeTempReg), Ty(Ty), TempRegID(TempRegID) {
    if (Ty.isLLTCodeGen())
      KnownTypes.insert(Ty.getLLTCodeGen());
  }

  static bool classof(const MatchAction *A) {
    return A->getKind() == AK_MakeTempReg;
  }

  void emitActionOpcodes(MatchTable &Table, RuleMatcher &Rule) const override;
};

} // namespace gi
} // namespace llvm

#endif
