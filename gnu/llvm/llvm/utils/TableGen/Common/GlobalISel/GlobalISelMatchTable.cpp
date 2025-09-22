//===- GlobalISelMatchTable.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GlobalISelMatchTable.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenRegisters.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"

#define DEBUG_TYPE "gi-match-table"

STATISTIC(NumPatternEmitted, "Number of patterns emitted");

namespace llvm {
namespace gi {

namespace {

Error failUnsupported(const Twine &Reason) {
  return make_error<StringError>(Reason, inconvertibleErrorCode());
}

/// Get the name of the enum value used to number the predicate function.
std::string getEnumNameForPredicate(const TreePredicateFn &Predicate) {
  if (Predicate.hasGISelPredicateCode())
    return "GICXXPred_MI_" + Predicate.getFnName();
  return "GICXXPred_" + Predicate.getImmTypeIdentifier().str() + "_" +
         Predicate.getFnName();
}

std::string getMatchOpcodeForImmPredicate(const TreePredicateFn &Predicate) {
  return "GIM_Check" + Predicate.getImmTypeIdentifier().str() + "ImmPredicate";
}

// GIMT_Encode2/4/8
constexpr StringLiteral EncodeMacroName = "GIMT_Encode";

} // namespace

//===- Helpers ------------------------------------------------------------===//

void emitEncodingMacrosDef(raw_ostream &OS) {
  OS << "#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__\n"
     << "#define " << EncodeMacroName << "2(Val)"
     << " uint8_t(Val), uint8_t((uint16_t)Val >> 8)\n"
     << "#define " << EncodeMacroName << "4(Val)"
     << " uint8_t(Val), uint8_t((uint32_t)Val >> 8), "
        "uint8_t((uint32_t)Val >> 16), uint8_t((uint32_t)Val >> 24)\n"
     << "#define " << EncodeMacroName << "8(Val)"
     << " uint8_t(Val), uint8_t((uint64_t)Val >> 8), "
        "uint8_t((uint64_t)Val >> 16), uint8_t((uint64_t)Val >> 24),  "
        "uint8_t((uint64_t)Val >> 32), uint8_t((uint64_t)Val >> 40), "
        "uint8_t((uint64_t)Val >> 48), uint8_t((uint64_t)Val >> 56)\n"
     << "#else\n"
     << "#define " << EncodeMacroName << "2(Val)"
     << " uint8_t((uint16_t)Val >> 8), uint8_t(Val)\n"
     << "#define " << EncodeMacroName << "4(Val)"
     << " uint8_t((uint32_t)Val >> 24), uint8_t((uint32_t)Val >> 16), "
        "uint8_t((uint32_t)Val >> 8), uint8_t(Val)\n"
     << "#define " << EncodeMacroName << "8(Val)"
     << " uint8_t((uint64_t)Val >> 56), uint8_t((uint64_t)Val >> 48), "
        "uint8_t((uint64_t)Val >> 40), uint8_t((uint64_t)Val >> 32),  "
        "uint8_t((uint64_t)Val >> 24), uint8_t((uint64_t)Val >> 16), "
        "uint8_t((uint64_t)Val >> 8), uint8_t(Val)\n"
     << "#endif\n";
}

void emitEncodingMacrosUndef(raw_ostream &OS) {
  OS << "#undef " << EncodeMacroName << "2\n"
     << "#undef " << EncodeMacroName << "4\n"
     << "#undef " << EncodeMacroName << "8\n";
}

std::string getNameForFeatureBitset(const std::vector<Record *> &FeatureBitset,
                                    int HwModeIdx) {
  std::string Name = "GIFBS";
  for (const auto &Feature : FeatureBitset)
    Name += ("_" + Feature->getName()).str();
  if (HwModeIdx >= 0)
    Name += ("_HwMode" + std::to_string(HwModeIdx));
  return Name;
}

template <class GroupT>
std::vector<Matcher *>
optimizeRules(ArrayRef<Matcher *> Rules,
              std::vector<std::unique_ptr<Matcher>> &MatcherStorage) {

  std::vector<Matcher *> OptRules;
  std::unique_ptr<GroupT> CurrentGroup = std::make_unique<GroupT>();
  assert(CurrentGroup->empty() && "Newly created group isn't empty!");
  unsigned NumGroups = 0;

  auto ProcessCurrentGroup = [&]() {
    if (CurrentGroup->empty())
      // An empty group is good to be reused:
      return;

    // If the group isn't large enough to provide any benefit, move all the
    // added rules out of it and make sure to re-create the group to properly
    // re-initialize it:
    if (CurrentGroup->size() < 2)
      append_range(OptRules, CurrentGroup->matchers());
    else {
      CurrentGroup->finalize();
      OptRules.push_back(CurrentGroup.get());
      MatcherStorage.emplace_back(std::move(CurrentGroup));
      ++NumGroups;
    }
    CurrentGroup = std::make_unique<GroupT>();
  };
  for (Matcher *Rule : Rules) {
    // Greedily add as many matchers as possible to the current group:
    if (CurrentGroup->addMatcher(*Rule))
      continue;

    ProcessCurrentGroup();
    assert(CurrentGroup->empty() && "A group wasn't properly re-initialized");

    // Try to add the pending matcher to a newly created empty group:
    if (!CurrentGroup->addMatcher(*Rule))
      // If we couldn't add the matcher to an empty group, that group type
      // doesn't support that kind of matchers at all, so just skip it:
      OptRules.push_back(Rule);
  }
  ProcessCurrentGroup();

  LLVM_DEBUG(dbgs() << "NumGroups: " << NumGroups << "\n");
  (void)NumGroups;
  assert(CurrentGroup->empty() && "The last group wasn't properly processed");
  return OptRules;
}

template std::vector<Matcher *> optimizeRules<GroupMatcher>(
    ArrayRef<Matcher *> Rules,
    std::vector<std::unique_ptr<Matcher>> &MatcherStorage);

template std::vector<Matcher *> optimizeRules<SwitchMatcher>(
    ArrayRef<Matcher *> Rules,
    std::vector<std::unique_ptr<Matcher>> &MatcherStorage);

static std::string getEncodedEmitStr(StringRef NamedValue, unsigned NumBytes) {
  if (NumBytes == 2 || NumBytes == 4 || NumBytes == 8)
    return (EncodeMacroName + Twine(NumBytes) + "(" + NamedValue + ")").str();
  llvm_unreachable("Unsupported number of bytes!");
}

//===- Global Data --------------------------------------------------------===//

std::set<LLTCodeGen> KnownTypes;

//===- MatchTableRecord ---------------------------------------------------===//

void MatchTableRecord::emit(raw_ostream &OS, bool LineBreakIsNextAfterThis,
                            const MatchTable &Table) const {
  bool UseLineComment =
      LineBreakIsNextAfterThis || (Flags & MTRF_LineBreakFollows);
  if (Flags & (MTRF_JumpTarget | MTRF_CommaFollows))
    UseLineComment = false;

  if (Flags & MTRF_Comment)
    OS << (UseLineComment ? "// " : "/*");

  if (NumElements > 1 && !(Flags & (MTRF_PreEncoded | MTRF_Comment)))
    OS << getEncodedEmitStr(EmitStr, NumElements);
  else
    OS << EmitStr;

  if (Flags & MTRF_Label)
    OS << ": @" << Table.getLabelIndex(LabelID);

  if ((Flags & MTRF_Comment) && !UseLineComment)
    OS << "*/";

  if (Flags & MTRF_JumpTarget) {
    if (Flags & MTRF_Comment)
      OS << " ";
    // TODO: Could encode this AOT to speed up build of generated file
    OS << getEncodedEmitStr(llvm::to_string(Table.getLabelIndex(LabelID)),
                            NumElements);
  }

  if (Flags & MTRF_CommaFollows) {
    OS << ",";
    if (!LineBreakIsNextAfterThis && !(Flags & MTRF_LineBreakFollows))
      OS << " ";
  }

  if (Flags & MTRF_LineBreakFollows)
    OS << "\n";
}

//===- MatchTable ---------------------------------------------------------===//

MatchTableRecord MatchTable::LineBreak = {
    std::nullopt, "" /* Emit String */, 0 /* Elements */,
    MatchTableRecord::MTRF_LineBreakFollows};

MatchTableRecord MatchTable::Comment(StringRef Comment) {
  return MatchTableRecord(std::nullopt, Comment, 0,
                          MatchTableRecord::MTRF_Comment);
}

MatchTableRecord MatchTable::Opcode(StringRef Opcode, int IndentAdjust) {
  unsigned ExtraFlags = 0;
  if (IndentAdjust > 0)
    ExtraFlags |= MatchTableRecord::MTRF_Indent;
  if (IndentAdjust < 0)
    ExtraFlags |= MatchTableRecord::MTRF_Outdent;

  return MatchTableRecord(std::nullopt, Opcode, 1,
                          MatchTableRecord::MTRF_CommaFollows | ExtraFlags);
}

MatchTableRecord MatchTable::NamedValue(unsigned NumBytes,
                                        StringRef NamedValue) {
  return MatchTableRecord(std::nullopt, NamedValue, NumBytes,
                          MatchTableRecord::MTRF_CommaFollows);
}

MatchTableRecord MatchTable::NamedValue(unsigned NumBytes, StringRef NamedValue,
                                        int64_t RawValue) {
  return MatchTableRecord(std::nullopt, NamedValue, NumBytes,
                          MatchTableRecord::MTRF_CommaFollows, RawValue);
}

MatchTableRecord MatchTable::NamedValue(unsigned NumBytes, StringRef Namespace,
                                        StringRef NamedValue) {
  return MatchTableRecord(std::nullopt, (Namespace + "::" + NamedValue).str(),
                          NumBytes, MatchTableRecord::MTRF_CommaFollows);
}

MatchTableRecord MatchTable::NamedValue(unsigned NumBytes, StringRef Namespace,
                                        StringRef NamedValue,
                                        int64_t RawValue) {
  return MatchTableRecord(std::nullopt, (Namespace + "::" + NamedValue).str(),
                          NumBytes, MatchTableRecord::MTRF_CommaFollows,
                          RawValue);
}

MatchTableRecord MatchTable::IntValue(unsigned NumBytes, int64_t IntValue) {
  assert(isUIntN(NumBytes * 8, IntValue) || isIntN(NumBytes * 8, IntValue));
  auto Str = llvm::to_string(IntValue);
  if (NumBytes == 1 && IntValue < 0)
    Str = "uint8_t(" + Str + ")";
  // TODO: Could optimize this directly to save the compiler some work when
  // building the file
  return MatchTableRecord(std::nullopt, Str, NumBytes,
                          MatchTableRecord::MTRF_CommaFollows);
}

MatchTableRecord MatchTable::ULEB128Value(uint64_t IntValue) {
  uint8_t Buffer[10];
  unsigned Len = encodeULEB128(IntValue, Buffer);

  // Simple case (most common)
  if (Len == 1) {
    return MatchTableRecord(std::nullopt, llvm::to_string((unsigned)Buffer[0]),
                            1, MatchTableRecord::MTRF_CommaFollows);
  }

  // Print it as, e.g. /* -123456 (*/, 0xC0, 0xBB, 0x78 /*)*/
  std::string Str;
  raw_string_ostream OS(Str);
  OS << "/* " << llvm::to_string(IntValue) << "(*/";
  for (unsigned K = 0; K < Len; ++K) {
    if (K)
      OS << ", ";
    OS << "0x" << llvm::toHex({Buffer[K]});
  }
  OS << "/*)*/";
  return MatchTableRecord(std::nullopt, Str, Len,
                          MatchTableRecord::MTRF_CommaFollows |
                              MatchTableRecord::MTRF_PreEncoded);
}

MatchTableRecord MatchTable::Label(unsigned LabelID) {
  return MatchTableRecord(LabelID, "Label " + llvm::to_string(LabelID), 0,
                          MatchTableRecord::MTRF_Label |
                              MatchTableRecord::MTRF_Comment |
                              MatchTableRecord::MTRF_LineBreakFollows);
}

MatchTableRecord MatchTable::JumpTarget(unsigned LabelID) {
  return MatchTableRecord(LabelID, "Label " + llvm::to_string(LabelID), 4,
                          MatchTableRecord::MTRF_JumpTarget |
                              MatchTableRecord::MTRF_Comment |
                              MatchTableRecord::MTRF_CommaFollows);
}

void MatchTable::emitUse(raw_ostream &OS) const { OS << "MatchTable" << ID; }

void MatchTable::emitDeclaration(raw_ostream &OS) const {
  unsigned Indentation = 4;
  OS << "  constexpr static uint8_t MatchTable" << ID << "[] = {";
  LineBreak.emit(OS, true, *this);
  OS << std::string(Indentation, ' ');

  for (auto I = Contents.begin(), E = Contents.end(); I != E; ++I) {
    bool LineBreakIsNext = false;
    const auto &NextI = std::next(I);

    if (NextI != E) {
      if (NextI->EmitStr == "" &&
          NextI->Flags == MatchTableRecord::MTRF_LineBreakFollows)
        LineBreakIsNext = true;
    }

    if (I->Flags & MatchTableRecord::MTRF_Indent)
      Indentation += 2;

    I->emit(OS, LineBreakIsNext, *this);
    if (I->Flags & MatchTableRecord::MTRF_LineBreakFollows)
      OS << std::string(Indentation, ' ');

    if (I->Flags & MatchTableRecord::MTRF_Outdent)
      Indentation -= 2;
  }
  OS << "}; // Size: " << CurrentSize << " bytes\n";
}

MatchTable MatchTable::buildTable(ArrayRef<Matcher *> Rules, bool WithCoverage,
                                  bool IsCombiner) {
  MatchTable Table(WithCoverage, IsCombiner);
  for (Matcher *Rule : Rules)
    Rule->emit(Table);

  return Table << MatchTable::Opcode("GIM_Reject") << MatchTable::LineBreak;
}

//===- LLTCodeGen ---------------------------------------------------------===//

std::string LLTCodeGen::getCxxEnumValue() const {
  std::string Str;
  raw_string_ostream OS(Str);

  emitCxxEnumValue(OS);
  return Str;
}

void LLTCodeGen::emitCxxEnumValue(raw_ostream &OS) const {
  if (Ty.isScalar()) {
    OS << "GILLT_s" << Ty.getSizeInBits();
    return;
  }
  if (Ty.isVector()) {
    OS << (Ty.isScalable() ? "GILLT_nxv" : "GILLT_v")
       << Ty.getElementCount().getKnownMinValue() << "s"
       << Ty.getScalarSizeInBits();
    return;
  }
  if (Ty.isPointer()) {
    OS << "GILLT_p" << Ty.getAddressSpace();
    if (Ty.getSizeInBits() > 0)
      OS << "s" << Ty.getSizeInBits();
    return;
  }
  llvm_unreachable("Unhandled LLT");
}

void LLTCodeGen::emitCxxConstructorCall(raw_ostream &OS) const {
  if (Ty.isScalar()) {
    OS << "LLT::scalar(" << Ty.getSizeInBits() << ")";
    return;
  }
  if (Ty.isVector()) {
    OS << "LLT::vector("
       << (Ty.isScalable() ? "ElementCount::getScalable("
                           : "ElementCount::getFixed(")
       << Ty.getElementCount().getKnownMinValue() << "), "
       << Ty.getScalarSizeInBits() << ")";
    return;
  }
  if (Ty.isPointer() && Ty.getSizeInBits() > 0) {
    OS << "LLT::pointer(" << Ty.getAddressSpace() << ", " << Ty.getSizeInBits()
       << ")";
    return;
  }
  llvm_unreachable("Unhandled LLT");
}

/// This ordering is used for std::unique() and llvm::sort(). There's no
/// particular logic behind the order but either A < B or B < A must be
/// true if A != B.
bool LLTCodeGen::operator<(const LLTCodeGen &Other) const {
  if (Ty.isValid() != Other.Ty.isValid())
    return Ty.isValid() < Other.Ty.isValid();
  if (!Ty.isValid())
    return false;

  if (Ty.isVector() != Other.Ty.isVector())
    return Ty.isVector() < Other.Ty.isVector();
  if (Ty.isScalar() != Other.Ty.isScalar())
    return Ty.isScalar() < Other.Ty.isScalar();
  if (Ty.isPointer() != Other.Ty.isPointer())
    return Ty.isPointer() < Other.Ty.isPointer();

  if (Ty.isPointer() && Ty.getAddressSpace() != Other.Ty.getAddressSpace())
    return Ty.getAddressSpace() < Other.Ty.getAddressSpace();

  if (Ty.isVector() && Ty.getElementCount() != Other.Ty.getElementCount())
    return std::tuple(Ty.isScalable(),
                      Ty.getElementCount().getKnownMinValue()) <
           std::tuple(Other.Ty.isScalable(),
                      Other.Ty.getElementCount().getKnownMinValue());

  assert((!Ty.isVector() || Ty.isScalable() == Other.Ty.isScalable()) &&
         "Unexpected mismatch of scalable property");
  return Ty.isVector()
             ? std::tuple(Ty.isScalable(),
                          Ty.getSizeInBits().getKnownMinValue()) <
                   std::tuple(Other.Ty.isScalable(),
                              Other.Ty.getSizeInBits().getKnownMinValue())
             : Ty.getSizeInBits().getFixedValue() <
                   Other.Ty.getSizeInBits().getFixedValue();
}

//===- LLTCodeGen Helpers -------------------------------------------------===//

std::optional<LLTCodeGen> MVTToLLT(MVT::SimpleValueType SVT) {
  MVT VT(SVT);

  if (VT.isVector() && !VT.getVectorElementCount().isScalar())
    return LLTCodeGen(
        LLT::vector(VT.getVectorElementCount(), VT.getScalarSizeInBits()));

  if (VT.isInteger() || VT.isFloatingPoint())
    return LLTCodeGen(LLT::scalar(VT.getSizeInBits()));

  return std::nullopt;
}

//===- Matcher ------------------------------------------------------------===//

void Matcher::optimize() {}

Matcher::~Matcher() {}

//===- GroupMatcher -------------------------------------------------------===//

bool GroupMatcher::candidateConditionMatches(
    const PredicateMatcher &Predicate) const {

  if (empty()) {
    // Sharing predicates for nested instructions is not supported yet as we
    // currently don't hoist the GIM_RecordInsn's properly, therefore we can
    // only work on the original root instruction (InsnVarID == 0):
    if (Predicate.getInsnVarID() != 0)
      return false;
    // ... otherwise an empty group can handle any predicate with no specific
    // requirements:
    return true;
  }

  const Matcher &Representative = **Matchers.begin();
  const auto &RepresentativeCondition = Representative.getFirstCondition();
  // ... if not empty, the group can only accomodate matchers with the exact
  // same first condition:
  return Predicate.isIdentical(RepresentativeCondition);
}

bool GroupMatcher::addMatcher(Matcher &Candidate) {
  if (!Candidate.hasFirstCondition())
    return false;

  const PredicateMatcher &Predicate = Candidate.getFirstCondition();
  if (!candidateConditionMatches(Predicate))
    return false;

  Matchers.push_back(&Candidate);
  return true;
}

void GroupMatcher::finalize() {
  assert(Conditions.empty() && "Already finalized?");
  if (empty())
    return;

  Matcher &FirstRule = **Matchers.begin();
  for (;;) {
    // All the checks are expected to succeed during the first iteration:
    for (const auto &Rule : Matchers)
      if (!Rule->hasFirstCondition())
        return;
    const auto &FirstCondition = FirstRule.getFirstCondition();
    for (unsigned I = 1, E = Matchers.size(); I < E; ++I)
      if (!Matchers[I]->getFirstCondition().isIdentical(FirstCondition))
        return;

    Conditions.push_back(FirstRule.popFirstCondition());
    for (unsigned I = 1, E = Matchers.size(); I < E; ++I)
      Matchers[I]->popFirstCondition();
  }
}

void GroupMatcher::emit(MatchTable &Table) {
  unsigned LabelID = ~0U;
  if (!Conditions.empty()) {
    LabelID = Table.allocateLabelID();
    Table << MatchTable::Opcode("GIM_Try", +1)
          << MatchTable::Comment("On fail goto")
          << MatchTable::JumpTarget(LabelID) << MatchTable::LineBreak;
  }
  for (auto &Condition : Conditions)
    Condition->emitPredicateOpcodes(
        Table, *static_cast<RuleMatcher *>(*Matchers.begin()));

  for (const auto &M : Matchers)
    M->emit(Table);

  // Exit the group
  if (!Conditions.empty())
    Table << MatchTable::Opcode("GIM_Reject", -1) << MatchTable::LineBreak
          << MatchTable::Label(LabelID);
}

void GroupMatcher::optimize() {
  // Make sure we only sort by a specific predicate within a range of rules that
  // all have that predicate checked against a specific value (not a wildcard):
  auto F = Matchers.begin();
  auto T = F;
  auto E = Matchers.end();
  while (T != E) {
    while (T != E) {
      auto *R = static_cast<RuleMatcher *>(*T);
      if (!R->getFirstConditionAsRootType().get().isValid())
        break;
      ++T;
    }
    std::stable_sort(F, T, [](Matcher *A, Matcher *B) {
      auto *L = static_cast<RuleMatcher *>(A);
      auto *R = static_cast<RuleMatcher *>(B);
      return L->getFirstConditionAsRootType() <
             R->getFirstConditionAsRootType();
    });
    if (T != E)
      F = ++T;
  }
  Matchers = optimizeRules<GroupMatcher>(Matchers, MatcherStorage);
  Matchers = optimizeRules<SwitchMatcher>(Matchers, MatcherStorage);
}

//===- SwitchMatcher ------------------------------------------------------===//

bool SwitchMatcher::isSupportedPredicateType(const PredicateMatcher &P) {
  return isa<InstructionOpcodeMatcher>(P) || isa<LLTOperandMatcher>(P);
}

bool SwitchMatcher::candidateConditionMatches(
    const PredicateMatcher &Predicate) const {

  if (empty()) {
    // Sharing predicates for nested instructions is not supported yet as we
    // currently don't hoist the GIM_RecordInsn's properly, therefore we can
    // only work on the original root instruction (InsnVarID == 0):
    if (Predicate.getInsnVarID() != 0)
      return false;
    // ... while an attempt to add even a root matcher to an empty SwitchMatcher
    // could fail as not all the types of conditions are supported:
    if (!isSupportedPredicateType(Predicate))
      return false;
    // ... or the condition might not have a proper implementation of
    // getValue() / isIdenticalDownToValue() yet:
    if (!Predicate.hasValue())
      return false;
    // ... otherwise an empty Switch can accomodate the condition with no
    // further requirements:
    return true;
  }

  const Matcher &CaseRepresentative = **Matchers.begin();
  const auto &RepresentativeCondition = CaseRepresentative.getFirstCondition();
  // Switch-cases must share the same kind of condition and path to the value it
  // checks:
  if (!Predicate.isIdenticalDownToValue(RepresentativeCondition))
    return false;

  const auto Value = Predicate.getValue();
  // ... but be unique with respect to the actual value they check:
  return Values.count(Value) == 0;
}

bool SwitchMatcher::addMatcher(Matcher &Candidate) {
  if (!Candidate.hasFirstCondition())
    return false;

  const PredicateMatcher &Predicate = Candidate.getFirstCondition();
  if (!candidateConditionMatches(Predicate))
    return false;
  const auto Value = Predicate.getValue();
  Values.insert(Value);

  Matchers.push_back(&Candidate);
  return true;
}

void SwitchMatcher::finalize() {
  assert(Condition == nullptr && "Already finalized");
  assert(Values.size() == Matchers.size() && "Broken SwitchMatcher");
  if (empty())
    return;

  llvm::stable_sort(Matchers, [](const Matcher *L, const Matcher *R) {
    return L->getFirstCondition().getValue() <
           R->getFirstCondition().getValue();
  });
  Condition = Matchers[0]->popFirstCondition();
  for (unsigned I = 1, E = Values.size(); I < E; ++I)
    Matchers[I]->popFirstCondition();
}

void SwitchMatcher::emitPredicateSpecificOpcodes(const PredicateMatcher &P,
                                                 MatchTable &Table) {
  assert(isSupportedPredicateType(P) && "Predicate type is not supported");

  if (const auto *Condition = dyn_cast<InstructionOpcodeMatcher>(&P)) {
    Table << MatchTable::Opcode("GIM_SwitchOpcode") << MatchTable::Comment("MI")
          << MatchTable::ULEB128Value(Condition->getInsnVarID());
    return;
  }
  if (const auto *Condition = dyn_cast<LLTOperandMatcher>(&P)) {
    Table << MatchTable::Opcode("GIM_SwitchType") << MatchTable::Comment("MI")
          << MatchTable::ULEB128Value(Condition->getInsnVarID())
          << MatchTable::Comment("Op")
          << MatchTable::ULEB128Value(Condition->getOpIdx());
    return;
  }

  llvm_unreachable("emitPredicateSpecificOpcodes is broken: can not handle a "
                   "predicate type that is claimed to be supported");
}

void SwitchMatcher::emit(MatchTable &Table) {
  assert(Values.size() == Matchers.size() && "Broken SwitchMatcher");
  if (empty())
    return;
  assert(Condition != nullptr &&
         "Broken SwitchMatcher, hasn't been finalized?");

  std::vector<unsigned> LabelIDs(Values.size());
  std::generate(LabelIDs.begin(), LabelIDs.end(),
                [&Table]() { return Table.allocateLabelID(); });
  const unsigned Default = Table.allocateLabelID();

  const int64_t LowerBound = Values.begin()->getRawValue();
  const int64_t UpperBound = Values.rbegin()->getRawValue() + 1;

  emitPredicateSpecificOpcodes(*Condition, Table);

  Table << MatchTable::Comment("[") << MatchTable::IntValue(2, LowerBound)
        << MatchTable::IntValue(2, UpperBound) << MatchTable::Comment(")")
        << MatchTable::Comment("default:") << MatchTable::JumpTarget(Default);

  int64_t J = LowerBound;
  auto VI = Values.begin();
  for (unsigned I = 0, E = Values.size(); I < E; ++I) {
    auto V = *VI++;
    while (J++ < V.getRawValue())
      Table << MatchTable::IntValue(4, 0);
    V.turnIntoComment();
    Table << MatchTable::LineBreak << V << MatchTable::JumpTarget(LabelIDs[I]);
  }
  Table << MatchTable::LineBreak;

  for (unsigned I = 0, E = Values.size(); I < E; ++I) {
    Table << MatchTable::Label(LabelIDs[I]);
    Matchers[I]->emit(Table);
    Table << MatchTable::Opcode("GIM_Reject") << MatchTable::LineBreak;
  }
  Table << MatchTable::Label(Default);
}

//===- RuleMatcher --------------------------------------------------------===//

uint64_t RuleMatcher::NextRuleID = 0;

StringRef RuleMatcher::getOpcode() const {
  return Matchers.front()->getOpcode();
}

unsigned RuleMatcher::getNumOperands() const {
  return Matchers.front()->getNumOperands();
}

LLTCodeGen RuleMatcher::getFirstConditionAsRootType() {
  InstructionMatcher &InsnMatcher = *Matchers.front();
  if (!InsnMatcher.predicates_empty())
    if (const auto *TM =
            dyn_cast<LLTOperandMatcher>(&**InsnMatcher.predicates_begin()))
      if (TM->getInsnVarID() == 0 && TM->getOpIdx() == 0)
        return TM->getTy();
  return {};
}

void RuleMatcher::optimize() {
  for (auto &Item : InsnVariableIDs) {
    InstructionMatcher &InsnMatcher = *Item.first;
    for (auto &OM : InsnMatcher.operands()) {
      // Complex Patterns are usually expensive and they relatively rarely fail
      // on their own: more often we end up throwing away all the work done by a
      // matching part of a complex pattern because some other part of the
      // enclosing pattern didn't match. All of this makes it beneficial to
      // delay complex patterns until the very end of the rule matching,
      // especially for targets having lots of complex patterns.
      for (auto &OP : OM->predicates())
        if (isa<ComplexPatternOperandMatcher>(OP))
          EpilogueMatchers.emplace_back(std::move(OP));
      OM->eraseNullPredicates();
    }
    InsnMatcher.optimize();
  }
  llvm::sort(EpilogueMatchers, [](const std::unique_ptr<PredicateMatcher> &L,
                                  const std::unique_ptr<PredicateMatcher> &R) {
    return std::tuple(L->getKind(), L->getInsnVarID(), L->getOpIdx()) <
           std::tuple(R->getKind(), R->getInsnVarID(), R->getOpIdx());
  });

  // Deduplicate EraseInst actions, and if an EraseInst erases the root, place
  // it at the end to favor generation of GIR_EraseRootFromParent_Done
  DenseSet<unsigned> AlreadySeenEraseInsts;
  auto EraseRootIt = Actions.end();
  auto It = Actions.begin();
  while (It != Actions.end()) {
    if (const auto *EI = dyn_cast<EraseInstAction>(It->get())) {
      unsigned InstID = EI->getInsnID();
      if (!AlreadySeenEraseInsts.insert(InstID).second) {
        It = Actions.erase(It);
        continue;
      }

      if (InstID == 0)
        EraseRootIt = It;
    }

    ++It;
  }

  if (EraseRootIt != Actions.end())
    Actions.splice(Actions.end(), Actions, EraseRootIt);
}

bool RuleMatcher::hasFirstCondition() const {
  if (insnmatchers_empty())
    return false;
  InstructionMatcher &Matcher = insnmatchers_front();
  if (!Matcher.predicates_empty())
    return true;
  for (auto &OM : Matcher.operands())
    for (auto &OP : OM->predicates())
      if (!isa<InstructionOperandMatcher>(OP))
        return true;
  return false;
}

const PredicateMatcher &RuleMatcher::getFirstCondition() const {
  assert(!insnmatchers_empty() &&
         "Trying to get a condition from an empty RuleMatcher");

  InstructionMatcher &Matcher = insnmatchers_front();
  if (!Matcher.predicates_empty())
    return **Matcher.predicates_begin();
  // If there is no more predicate on the instruction itself, look at its
  // operands.
  for (auto &OM : Matcher.operands())
    for (auto &OP : OM->predicates())
      if (!isa<InstructionOperandMatcher>(OP))
        return *OP;

  llvm_unreachable("Trying to get a condition from an InstructionMatcher with "
                   "no conditions");
}

std::unique_ptr<PredicateMatcher> RuleMatcher::popFirstCondition() {
  assert(!insnmatchers_empty() &&
         "Trying to pop a condition from an empty RuleMatcher");

  InstructionMatcher &Matcher = insnmatchers_front();
  if (!Matcher.predicates_empty())
    return Matcher.predicates_pop_front();
  // If there is no more predicate on the instruction itself, look at its
  // operands.
  for (auto &OM : Matcher.operands())
    for (auto &OP : OM->predicates())
      if (!isa<InstructionOperandMatcher>(OP)) {
        std::unique_ptr<PredicateMatcher> Result = std::move(OP);
        OM->eraseNullPredicates();
        return Result;
      }

  llvm_unreachable("Trying to pop a condition from an InstructionMatcher with "
                   "no conditions");
}

GISelFlags RuleMatcher::updateGISelFlag(GISelFlags CurFlags, const Record *R,
                                        StringRef FlagName,
                                        GISelFlags FlagBit) {
  // If the value of a flag is unset, ignore it.
  // If it's set, it always takes precedence over the existing value so
  // clear/set the corresponding bit.
  bool Unset = false;
  bool Value = R->getValueAsBitOrUnset("GIIgnoreCopies", Unset);
  if (!Unset)
    return Value ? (CurFlags | FlagBit) : (CurFlags & ~FlagBit);
  return CurFlags;
}

SaveAndRestore<GISelFlags> RuleMatcher::setGISelFlags(const Record *R) {
  if (!R || !R->isSubClassOf("GISelFlags"))
    return {Flags, Flags};

  assert((R->isSubClassOf("PatFrags") || R->isSubClassOf("Pattern")) &&
         "GISelFlags is only expected on Pattern/PatFrags!");

  GISelFlags NewFlags =
      updateGISelFlag(Flags, R, "GIIgnoreCopies", GISF_IgnoreCopies);
  return {Flags, NewFlags};
}

Error RuleMatcher::defineComplexSubOperand(StringRef SymbolicName,
                                           Record *ComplexPattern,
                                           unsigned RendererID,
                                           unsigned SubOperandID,
                                           StringRef ParentSymbolicName) {
  std::string ParentName(ParentSymbolicName);
  if (ComplexSubOperands.count(SymbolicName)) {
    const std::string &RecordedParentName =
        ComplexSubOperandsParentName[SymbolicName];
    if (RecordedParentName != ParentName)
      return failUnsupported("Error: Complex suboperand " + SymbolicName +
                             " referenced by different operands: " +
                             RecordedParentName + " and " + ParentName + ".");
    // Complex suboperand referenced more than once from same the operand is
    // used to generate 'same operand check'. Emitting of
    // GIR_ComplexSubOperandRenderer for them is already handled.
    return Error::success();
  }

  ComplexSubOperands[SymbolicName] =
      std::tuple(ComplexPattern, RendererID, SubOperandID);
  ComplexSubOperandsParentName[SymbolicName] = ParentName;

  return Error::success();
}

InstructionMatcher &RuleMatcher::addInstructionMatcher(StringRef SymbolicName) {
  Matchers.emplace_back(new InstructionMatcher(*this, SymbolicName));
  MutatableInsns.insert(Matchers.back().get());
  return *Matchers.back();
}

void RuleMatcher::addRequiredSimplePredicate(StringRef PredName) {
  RequiredSimplePredicates.push_back(PredName.str());
}

const std::vector<std::string> &RuleMatcher::getRequiredSimplePredicates() {
  return RequiredSimplePredicates;
}

void RuleMatcher::addRequiredFeature(Record *Feature) {
  RequiredFeatures.push_back(Feature);
}

const std::vector<Record *> &RuleMatcher::getRequiredFeatures() const {
  return RequiredFeatures;
}

unsigned RuleMatcher::implicitlyDefineInsnVar(InstructionMatcher &Matcher) {
  unsigned NewInsnVarID = NextInsnVarID++;
  InsnVariableIDs[&Matcher] = NewInsnVarID;
  return NewInsnVarID;
}

unsigned RuleMatcher::getInsnVarID(InstructionMatcher &InsnMatcher) const {
  const auto &I = InsnVariableIDs.find(&InsnMatcher);
  if (I != InsnVariableIDs.end())
    return I->second;
  llvm_unreachable("Matched Insn was not captured in a local variable");
}

void RuleMatcher::defineOperand(StringRef SymbolicName, OperandMatcher &OM) {
  if (!DefinedOperands.contains(SymbolicName)) {
    DefinedOperands[SymbolicName] = &OM;
    return;
  }

  // If the operand is already defined, then we must ensure both references in
  // the matcher have the exact same node.
  RuleMatcher &RM = OM.getInstructionMatcher().getRuleMatcher();
  OM.addPredicate<SameOperandMatcher>(
      OM.getSymbolicName(), getOperandMatcher(OM.getSymbolicName()).getOpIdx(),
      RM.getGISelFlags());
}

void RuleMatcher::definePhysRegOperand(Record *Reg, OperandMatcher &OM) {
  if (!PhysRegOperands.contains(Reg)) {
    PhysRegOperands[Reg] = &OM;
    return;
  }
}

InstructionMatcher &
RuleMatcher::getInstructionMatcher(StringRef SymbolicName) const {
  for (const auto &I : InsnVariableIDs)
    if (I.first->getSymbolicName() == SymbolicName)
      return *I.first;
  llvm_unreachable(
      ("Failed to lookup instruction " + SymbolicName).str().c_str());
}

const OperandMatcher &RuleMatcher::getPhysRegOperandMatcher(Record *Reg) const {
  const auto &I = PhysRegOperands.find(Reg);

  if (I == PhysRegOperands.end()) {
    PrintFatalError(SrcLoc, "Register " + Reg->getName() +
                                " was not declared in matcher");
  }

  return *I->second;
}

OperandMatcher &RuleMatcher::getOperandMatcher(StringRef Name) {
  const auto &I = DefinedOperands.find(Name);

  if (I == DefinedOperands.end())
    PrintFatalError(SrcLoc, "Operand " + Name + " was not declared in matcher");

  return *I->second;
}

const OperandMatcher &RuleMatcher::getOperandMatcher(StringRef Name) const {
  const auto &I = DefinedOperands.find(Name);

  if (I == DefinedOperands.end())
    PrintFatalError(SrcLoc, "Operand " + Name + " was not declared in matcher");

  return *I->second;
}

void RuleMatcher::emit(MatchTable &Table) {
  if (Matchers.empty())
    llvm_unreachable("Unexpected empty matcher!");

  // The representation supports rules that require multiple roots such as:
  //    %ptr(p0) = ...
  //    %elt0(s32) = G_LOAD %ptr
  //    %1(p0) = G_ADD %ptr, 4
  //    %elt1(s32) = G_LOAD p0 %1
  // which could be usefully folded into:
  //    %ptr(p0) = ...
  //    %elt0(s32), %elt1(s32) = TGT_LOAD_PAIR %ptr
  // on some targets but we don't need to make use of that yet.
  assert(Matchers.size() == 1 && "Cannot handle multi-root matchers yet");

  unsigned LabelID = Table.allocateLabelID();
  Table << MatchTable::Opcode("GIM_Try", +1)
        << MatchTable::Comment("On fail goto")
        << MatchTable::JumpTarget(LabelID)
        << MatchTable::Comment(("Rule ID " + Twine(RuleID) + " //").str())
        << MatchTable::LineBreak;

  if (!RequiredFeatures.empty() || HwModeIdx >= 0) {
    Table << MatchTable::Opcode("GIM_CheckFeatures")
          << MatchTable::NamedValue(
                 2, getNameForFeatureBitset(RequiredFeatures, HwModeIdx))
          << MatchTable::LineBreak;
  }

  if (!RequiredSimplePredicates.empty()) {
    for (const auto &Pred : RequiredSimplePredicates) {
      Table << MatchTable::Opcode("GIM_CheckSimplePredicate")
            << MatchTable::NamedValue(2, Pred) << MatchTable::LineBreak;
    }
  }

  Matchers.front()->emitPredicateOpcodes(Table, *this);

  // Check if it's safe to replace registers.
  for (const auto &MA : Actions)
    MA->emitAdditionalPredicates(Table, *this);

  // We must also check if it's safe to fold the matched instructions.
  if (InsnVariableIDs.size() >= 2) {

    // FIXME: Emit checks to determine it's _actually_ safe to fold and/or
    //        account for unsafe cases.
    //
    //        Example:
    //          MI1--> %0 = ...
    //                 %1 = ... %0
    //          MI0--> %2 = ... %0
    //          It's not safe to erase MI1. We currently handle this by not
    //          erasing %0 (even when it's dead).
    //
    //        Example:
    //          MI1--> %0 = load volatile @a
    //                 %1 = load volatile @a
    //          MI0--> %2 = ... %0
    //          It's not safe to sink %0's def past %1. We currently handle
    //          this by rejecting all loads.
    //
    //        Example:
    //          MI1--> %0 = load @a
    //                 %1 = store @a
    //          MI0--> %2 = ... %0
    //          It's not safe to sink %0's def past %1. We currently handle
    //          this by rejecting all loads.
    //
    //        Example:
    //                   G_CONDBR %cond, @BB1
    //                 BB0:
    //          MI1-->   %0 = load @a
    //                   G_BR @BB1
    //                 BB1:
    //          MI0-->   %2 = ... %0
    //          It's not always safe to sink %0 across control flow. In this
    //          case it may introduce a memory fault. We currentl handle
    //          this by rejecting all loads.

    Table << MatchTable::Opcode("GIM_CheckIsSafeToFold")
          << MatchTable::Comment("NumInsns")
          << MatchTable::IntValue(1, InsnVariableIDs.size() - 1)
          << MatchTable::LineBreak;
  }

  for (const auto &PM : EpilogueMatchers)
    PM->emitPredicateOpcodes(Table, *this);

  if (!CustomCXXAction.empty()) {
    /// Handle combiners relying on custom C++ code instead of actions.
    assert(Table.isCombiner() && "CustomCXXAction is only for combiners!");
    // We cannot have actions other than debug comments.
    assert(none_of(Actions, [](auto &A) {
      return A->getKind() != MatchAction::AK_DebugComment;
    }));
    for (const auto &MA : Actions)
      MA->emitActionOpcodes(Table, *this);
    Table << MatchTable::Opcode("GIR_DoneWithCustomAction", -1)
          << MatchTable::Comment("Fn")
          << MatchTable::NamedValue(2, CustomCXXAction)
          << MatchTable::LineBreak;
  } else {
    // Emit all actions except the last one, then emit coverage and emit the
    // final action.
    //
    // This is because some actions, such as GIR_EraseRootFromParent_Done, also
    // double as a GIR_Done and terminate execution of the rule.
    if (!Actions.empty()) {
      for (const auto &MA : drop_end(Actions))
        MA->emitActionOpcodes(Table, *this);
    }

    assert((Table.isWithCoverage() ? !Table.isCombiner() : true) &&
           "Combiner tables don't support coverage!");
    if (Table.isWithCoverage())
      Table << MatchTable::Opcode("GIR_Coverage")
            << MatchTable::IntValue(4, RuleID) << MatchTable::LineBreak;
    else if (!Table.isCombiner())
      Table << MatchTable::Comment(
                   ("GIR_Coverage, " + Twine(RuleID) + ",").str())
            << MatchTable::LineBreak;

    if (Actions.empty() ||
        !Actions.back()->emitActionOpcodesAndDone(Table, *this)) {
      Table << MatchTable::Opcode("GIR_Done", -1) << MatchTable::LineBreak;
    }
  }

  Table << MatchTable::Label(LabelID);
  ++NumPatternEmitted;
}

bool RuleMatcher::isHigherPriorityThan(const RuleMatcher &B) const {
  // Rules involving more match roots have higher priority.
  if (Matchers.size() > B.Matchers.size())
    return true;
  if (Matchers.size() < B.Matchers.size())
    return false;

  for (auto Matcher : zip(Matchers, B.Matchers)) {
    if (std::get<0>(Matcher)->isHigherPriorityThan(*std::get<1>(Matcher)))
      return true;
    if (std::get<1>(Matcher)->isHigherPriorityThan(*std::get<0>(Matcher)))
      return false;
  }

  return false;
}

unsigned RuleMatcher::countRendererFns() const {
  return std::accumulate(
      Matchers.begin(), Matchers.end(), 0,
      [](unsigned A, const std::unique_ptr<InstructionMatcher> &Matcher) {
        return A + Matcher->countRendererFns();
      });
}

//===- PredicateMatcher ---------------------------------------------------===//

PredicateMatcher::~PredicateMatcher() {}

//===- OperandPredicateMatcher --------------------------------------------===//

OperandPredicateMatcher::~OperandPredicateMatcher() {}

bool OperandPredicateMatcher::isHigherPriorityThan(
    const OperandPredicateMatcher &B) const {
  // Generally speaking, an instruction is more important than an Int or a
  // LiteralInt because it can cover more nodes but there's an exception to
  // this. G_CONSTANT's are less important than either of those two because they
  // are more permissive.

  const auto *AOM = dyn_cast<InstructionOperandMatcher>(this);
  const auto *BOM = dyn_cast<InstructionOperandMatcher>(&B);
  bool AIsConstantInsn = AOM && AOM->getInsnMatcher().isConstantInstruction();
  bool BIsConstantInsn = BOM && BOM->getInsnMatcher().isConstantInstruction();

  // The relative priorities between a G_CONSTANT and any other instruction
  // don't actually matter but this code is needed to ensure a strict weak
  // ordering. This is particularly important on Windows where the rules will
  // be incorrectly sorted without it.
  if (AOM && BOM)
    return !AIsConstantInsn && BIsConstantInsn;

  if (AIsConstantInsn && (B.Kind == OPM_Int || B.Kind == OPM_LiteralInt))
    return false;
  if (BIsConstantInsn && (Kind == OPM_Int || Kind == OPM_LiteralInt))
    return true;

  return Kind < B.Kind;
}

//===- SameOperandMatcher -------------------------------------------------===//

void SameOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                              RuleMatcher &Rule) const {
  const OperandMatcher &OtherOM = Rule.getOperandMatcher(MatchingName);
  unsigned OtherInsnVarID = Rule.getInsnVarID(OtherOM.getInstructionMatcher());
  assert(OtherInsnVarID == OtherOM.getInstructionMatcher().getInsnVarID());
  const bool IgnoreCopies = Flags & GISF_IgnoreCopies;
  Table << MatchTable::Opcode(IgnoreCopies
                                  ? "GIM_CheckIsSameOperandIgnoreCopies"
                                  : "GIM_CheckIsSameOperand")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("OpIdx") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment("OtherMI")
        << MatchTable::ULEB128Value(OtherInsnVarID)
        << MatchTable::Comment("OtherOpIdx")
        << MatchTable::ULEB128Value(OtherOM.getOpIdx())
        << MatchTable::LineBreak;
}

//===- LLTOperandMatcher --------------------------------------------------===//

std::map<LLTCodeGen, unsigned> LLTOperandMatcher::TypeIDValues;

MatchTableRecord LLTOperandMatcher::getValue() const {
  const auto VI = TypeIDValues.find(Ty);
  if (VI == TypeIDValues.end())
    return MatchTable::NamedValue(1, getTy().getCxxEnumValue());
  return MatchTable::NamedValue(1, getTy().getCxxEnumValue(), VI->second);
}

bool LLTOperandMatcher::hasValue() const {
  if (TypeIDValues.size() != KnownTypes.size())
    initTypeIDValuesMap();
  return TypeIDValues.count(Ty);
}

void LLTOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                             RuleMatcher &Rule) const {
  if (InsnVarID == 0) {
    Table << MatchTable::Opcode("GIM_RootCheckType");
  } else {
    Table << MatchTable::Opcode("GIM_CheckType") << MatchTable::Comment("MI")
          << MatchTable::ULEB128Value(InsnVarID);
  }
  Table << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment("Type") << getValue() << MatchTable::LineBreak;
}

//===- PointerToAnyOperandMatcher -----------------------------------------===//

void PointerToAnyOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                      RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckPointerToAny")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment("SizeInBits")
        << MatchTable::ULEB128Value(SizeInBits) << MatchTable::LineBreak;
}

//===- RecordNamedOperandMatcher ------------------------------------------===//

void RecordNamedOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                     RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_RecordNamedOperand")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment("StoreIdx") << MatchTable::ULEB128Value(StoreIdx)
        << MatchTable::Comment("Name : " + Name) << MatchTable::LineBreak;
}

//===- RecordRegisterType ------------------------------------------===//

void RecordRegisterType::emitPredicateOpcodes(MatchTable &Table,
                                              RuleMatcher &Rule) const {
  assert(Idx < 0 && "Temp types always have negative indexes!");
  Table << MatchTable::Opcode("GIM_RecordRegType") << MatchTable::Comment("MI")
        << MatchTable::ULEB128Value(InsnVarID) << MatchTable::Comment("Op")
        << MatchTable::ULEB128Value(OpIdx) << MatchTable::Comment("TempTypeIdx")
        << MatchTable::IntValue(1, Idx) << MatchTable::LineBreak;
}

//===- ComplexPatternOperandMatcher ---------------------------------------===//

void ComplexPatternOperandMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  unsigned ID = getAllocatedTemporariesBaseID();
  Table << MatchTable::Opcode("GIM_CheckComplexPattern")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment("Renderer") << MatchTable::IntValue(2, ID)
        << MatchTable::NamedValue(2, ("GICP_" + TheDef.getName()).str())
        << MatchTable::LineBreak;
}

unsigned ComplexPatternOperandMatcher::getAllocatedTemporariesBaseID() const {
  return Operand.getAllocatedTemporariesBaseID();
}

//===- RegisterBankOperandMatcher -----------------------------------------===//

bool RegisterBankOperandMatcher::isIdentical(const PredicateMatcher &B) const {
  return OperandPredicateMatcher::isIdentical(B) &&
         RC.getDef() == cast<RegisterBankOperandMatcher>(&B)->RC.getDef();
}

void RegisterBankOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                      RuleMatcher &Rule) const {
  if (InsnVarID == 0) {
    Table << MatchTable::Opcode("GIM_RootCheckRegBankForClass");
  } else {
    Table << MatchTable::Opcode("GIM_CheckRegBankForClass")
          << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID);
  }

  Table << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment("RC")
        << MatchTable::NamedValue(2, RC.getQualifiedIdName())
        << MatchTable::LineBreak;
}

//===- MBBOperandMatcher --------------------------------------------------===//

void MBBOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                             RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckIsMBB") << MatchTable::Comment("MI")
        << MatchTable::ULEB128Value(InsnVarID) << MatchTable::Comment("Op")
        << MatchTable::ULEB128Value(OpIdx) << MatchTable::LineBreak;
}

//===- ImmOperandMatcher --------------------------------------------------===//

void ImmOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                             RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckIsImm") << MatchTable::Comment("MI")
        << MatchTable::ULEB128Value(InsnVarID) << MatchTable::Comment("Op")
        << MatchTable::ULEB128Value(OpIdx) << MatchTable::LineBreak;
}

//===- ConstantIntOperandMatcher ------------------------------------------===//

void ConstantIntOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                     RuleMatcher &Rule) const {
  const bool IsInt8 = isInt<8>(Value);
  Table << MatchTable::Opcode(IsInt8 ? "GIM_CheckConstantInt8"
                                     : "GIM_CheckConstantInt")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::IntValue(IsInt8 ? 1 : 8, Value) << MatchTable::LineBreak;
}

//===- LiteralIntOperandMatcher -------------------------------------------===//

void LiteralIntOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                    RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckLiteralInt")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::IntValue(8, Value) << MatchTable::LineBreak;
}

//===- CmpPredicateOperandMatcher -----------------------------------------===//

void CmpPredicateOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                      RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckCmpPredicate")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment("Predicate")
        << MatchTable::NamedValue(2, "CmpInst", PredName)
        << MatchTable::LineBreak;
}

//===- IntrinsicIDOperandMatcher ------------------------------------------===//

void IntrinsicIDOperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                     RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckIntrinsicID")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::NamedValue(2, "Intrinsic::" + II->EnumName)
        << MatchTable::LineBreak;
}

//===- OperandImmPredicateMatcher -----------------------------------------===//

void OperandImmPredicateMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                      RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckImmOperandPredicate")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("MO") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment("Predicate")
        << MatchTable::NamedValue(2, getEnumNameForPredicate(Predicate))
        << MatchTable::LineBreak;
}

//===- OperandMatcher -----------------------------------------------------===//

std::string OperandMatcher::getOperandExpr(unsigned InsnVarID) const {
  return "State.MIs[" + llvm::to_string(InsnVarID) + "]->getOperand(" +
         llvm::to_string(OpIdx) + ")";
}

unsigned OperandMatcher::getInsnVarID() const { return Insn.getInsnVarID(); }

TempTypeIdx OperandMatcher::getTempTypeIdx(RuleMatcher &Rule) {
  if (TTIdx >= 0) {
    // Temp type index not assigned yet, so assign one and add the necessary
    // predicate.
    TTIdx = Rule.getNextTempTypeIdx();
    assert(TTIdx < 0);
    addPredicate<RecordRegisterType>(TTIdx);
    return TTIdx;
  }
  return TTIdx;
}

void OperandMatcher::emitPredicateOpcodes(MatchTable &Table,
                                          RuleMatcher &Rule) {
  if (!Optimized) {
    std::string Comment;
    raw_string_ostream CommentOS(Comment);
    CommentOS << "MIs[" << getInsnVarID() << "] ";
    if (SymbolicName.empty())
      CommentOS << "Operand " << OpIdx;
    else
      CommentOS << SymbolicName;
    Table << MatchTable::Comment(Comment) << MatchTable::LineBreak;
  }

  emitPredicateListOpcodes(Table, Rule);
}

bool OperandMatcher::isHigherPriorityThan(OperandMatcher &B) {
  // Operand matchers involving more predicates have higher priority.
  if (predicates_size() > B.predicates_size())
    return true;
  if (predicates_size() < B.predicates_size())
    return false;

  // This assumes that predicates are added in a consistent order.
  for (auto &&Predicate : zip(predicates(), B.predicates())) {
    if (std::get<0>(Predicate)->isHigherPriorityThan(*std::get<1>(Predicate)))
      return true;
    if (std::get<1>(Predicate)->isHigherPriorityThan(*std::get<0>(Predicate)))
      return false;
  }

  return false;
}

unsigned OperandMatcher::countRendererFns() {
  return std::accumulate(
      predicates().begin(), predicates().end(), 0,
      [](unsigned A,
         const std::unique_ptr<OperandPredicateMatcher> &Predicate) {
        return A + Predicate->countRendererFns();
      });
}

Error OperandMatcher::addTypeCheckPredicate(const TypeSetByHwMode &VTy,
                                            bool OperandIsAPointer) {
  if (!VTy.isMachineValueType())
    return failUnsupported("unsupported typeset");

  if (VTy.getMachineValueType() == MVT::iPTR && OperandIsAPointer) {
    addPredicate<PointerToAnyOperandMatcher>(0);
    return Error::success();
  }

  auto OpTyOrNone = MVTToLLT(VTy.getMachineValueType().SimpleTy);
  if (!OpTyOrNone)
    return failUnsupported("unsupported type");

  if (OperandIsAPointer)
    addPredicate<PointerToAnyOperandMatcher>(OpTyOrNone->get().getSizeInBits());
  else if (VTy.isPointer())
    addPredicate<LLTOperandMatcher>(
        LLT::pointer(VTy.getPtrAddrSpace(), OpTyOrNone->get().getSizeInBits()));
  else
    addPredicate<LLTOperandMatcher>(*OpTyOrNone);
  return Error::success();
}

//===- InstructionOpcodeMatcher -------------------------------------------===//

DenseMap<const CodeGenInstruction *, unsigned>
    InstructionOpcodeMatcher::OpcodeValues;

MatchTableRecord
InstructionOpcodeMatcher::getInstValue(const CodeGenInstruction *I) const {
  const auto VI = OpcodeValues.find(I);
  if (VI != OpcodeValues.end())
    return MatchTable::NamedValue(2, I->Namespace, I->TheDef->getName(),
                                  VI->second);
  return MatchTable::NamedValue(2, I->Namespace, I->TheDef->getName());
}

void InstructionOpcodeMatcher::initOpcodeValuesMap(
    const CodeGenTarget &Target) {
  OpcodeValues.clear();

  for (const CodeGenInstruction *I : Target.getInstructionsByEnumValue())
    OpcodeValues[I] = Target.getInstrIntValue(I->TheDef);
}

MatchTableRecord InstructionOpcodeMatcher::getValue() const {
  assert(Insts.size() == 1);

  const CodeGenInstruction *I = Insts[0];
  const auto VI = OpcodeValues.find(I);
  if (VI != OpcodeValues.end())
    return MatchTable::NamedValue(2, I->Namespace, I->TheDef->getName(),
                                  VI->second);
  return MatchTable::NamedValue(2, I->Namespace, I->TheDef->getName());
}

void InstructionOpcodeMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                    RuleMatcher &Rule) const {
  StringRef CheckType =
      Insts.size() == 1 ? "GIM_CheckOpcode" : "GIM_CheckOpcodeIsEither";
  Table << MatchTable::Opcode(CheckType) << MatchTable::Comment("MI")
        << MatchTable::ULEB128Value(InsnVarID);

  for (const CodeGenInstruction *I : Insts)
    Table << getInstValue(I);
  Table << MatchTable::LineBreak;
}

bool InstructionOpcodeMatcher::isHigherPriorityThan(
    const InstructionPredicateMatcher &B) const {
  if (InstructionPredicateMatcher::isHigherPriorityThan(B))
    return true;
  if (B.InstructionPredicateMatcher::isHigherPriorityThan(*this))
    return false;

  // Prioritize opcodes for cosmetic reasons in the generated source. Although
  // this is cosmetic at the moment, we may want to drive a similar ordering
  // using instruction frequency information to improve compile time.
  if (const InstructionOpcodeMatcher *BO =
          dyn_cast<InstructionOpcodeMatcher>(&B))
    return Insts[0]->TheDef->getName() < BO->Insts[0]->TheDef->getName();

  return false;
}

bool InstructionOpcodeMatcher::isConstantInstruction() const {
  return Insts.size() == 1 && Insts[0]->TheDef->getName() == "G_CONSTANT";
}

StringRef InstructionOpcodeMatcher::getOpcode() const {
  return Insts[0]->TheDef->getName();
}

bool InstructionOpcodeMatcher::isVariadicNumOperands() const {
  // If one is variadic, they all should be.
  return Insts[0]->Operands.isVariadic;
}

StringRef InstructionOpcodeMatcher::getOperandType(unsigned OpIdx) const {
  // Types expected to be uniform for all alternatives.
  return Insts[0]->Operands[OpIdx].OperandType;
}

//===- InstructionNumOperandsMatcher --------------------------------------===//

void InstructionNumOperandsMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckNumOperands")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Expected")
        << MatchTable::ULEB128Value(NumOperands) << MatchTable::LineBreak;
}

//===- InstructionImmPredicateMatcher -------------------------------------===//

bool InstructionImmPredicateMatcher::isIdentical(
    const PredicateMatcher &B) const {
  return InstructionPredicateMatcher::isIdentical(B) &&
         Predicate.getOrigPatFragRecord() ==
             cast<InstructionImmPredicateMatcher>(&B)
                 ->Predicate.getOrigPatFragRecord();
}

void InstructionImmPredicateMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  Table << MatchTable::Opcode(getMatchOpcodeForImmPredicate(Predicate))
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("Predicate")
        << MatchTable::NamedValue(2, getEnumNameForPredicate(Predicate))
        << MatchTable::LineBreak;
}

//===- AtomicOrderingMMOPredicateMatcher ----------------------------------===//

bool AtomicOrderingMMOPredicateMatcher::isIdentical(
    const PredicateMatcher &B) const {
  if (!InstructionPredicateMatcher::isIdentical(B))
    return false;
  const auto &R = *cast<AtomicOrderingMMOPredicateMatcher>(&B);
  return Order == R.Order && Comparator == R.Comparator;
}

void AtomicOrderingMMOPredicateMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  StringRef Opcode = "GIM_CheckAtomicOrdering";

  if (Comparator == AO_OrStronger)
    Opcode = "GIM_CheckAtomicOrderingOrStrongerThan";
  if (Comparator == AO_WeakerThan)
    Opcode = "GIM_CheckAtomicOrderingWeakerThan";

  Table << MatchTable::Opcode(Opcode) << MatchTable::Comment("MI")
        << MatchTable::ULEB128Value(InsnVarID) << MatchTable::Comment("Order")
        << MatchTable::NamedValue(1,
                                  ("(uint8_t)AtomicOrdering::" + Order).str())
        << MatchTable::LineBreak;
}

//===- MemorySizePredicateMatcher -----------------------------------------===//

void MemorySizePredicateMatcher::emitPredicateOpcodes(MatchTable &Table,
                                                      RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckMemorySizeEqualTo")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("MMO") << MatchTable::ULEB128Value(MMOIdx)
        << MatchTable::Comment("Size") << MatchTable::IntValue(4, Size)
        << MatchTable::LineBreak;
}

//===- MemoryAddressSpacePredicateMatcher ---------------------------------===//

bool MemoryAddressSpacePredicateMatcher::isIdentical(
    const PredicateMatcher &B) const {
  if (!InstructionPredicateMatcher::isIdentical(B))
    return false;
  auto *Other = cast<MemoryAddressSpacePredicateMatcher>(&B);
  return MMOIdx == Other->MMOIdx && AddrSpaces == Other->AddrSpaces;
}

void MemoryAddressSpacePredicateMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  assert(AddrSpaces.size() < 256);
  Table << MatchTable::Opcode("GIM_CheckMemoryAddressSpace")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("MMO")
        << MatchTable::ULEB128Value(MMOIdx)
        // Encode number of address spaces to expect.
        << MatchTable::Comment("NumAddrSpace")
        << MatchTable::IntValue(1, AddrSpaces.size());
  for (unsigned AS : AddrSpaces)
    Table << MatchTable::Comment("AddrSpace") << MatchTable::ULEB128Value(AS);

  Table << MatchTable::LineBreak;
}

//===- MemoryAlignmentPredicateMatcher ------------------------------------===//

bool MemoryAlignmentPredicateMatcher::isIdentical(
    const PredicateMatcher &B) const {
  if (!InstructionPredicateMatcher::isIdentical(B))
    return false;
  auto *Other = cast<MemoryAlignmentPredicateMatcher>(&B);
  return MMOIdx == Other->MMOIdx && MinAlign == Other->MinAlign;
}

void MemoryAlignmentPredicateMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  // TODO: we could support more, just need to emit the right opcode or switch
  // to log alignment.
  assert(MinAlign < 256);
  Table << MatchTable::Opcode("GIM_CheckMemoryAlignment")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("MMO") << MatchTable::ULEB128Value(MMOIdx)
        << MatchTable::Comment("MinAlign") << MatchTable::IntValue(1, MinAlign)
        << MatchTable::LineBreak;
}

//===- MemoryVsLLTSizePredicateMatcher ------------------------------------===//

bool MemoryVsLLTSizePredicateMatcher::isIdentical(
    const PredicateMatcher &B) const {
  return InstructionPredicateMatcher::isIdentical(B) &&
         MMOIdx == cast<MemoryVsLLTSizePredicateMatcher>(&B)->MMOIdx &&
         Relation == cast<MemoryVsLLTSizePredicateMatcher>(&B)->Relation &&
         OpIdx == cast<MemoryVsLLTSizePredicateMatcher>(&B)->OpIdx;
}

void MemoryVsLLTSizePredicateMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  Table << MatchTable::Opcode(
               Relation == EqualTo       ? "GIM_CheckMemorySizeEqualToLLT"
               : Relation == GreaterThan ? "GIM_CheckMemorySizeGreaterThanLLT"
                                         : "GIM_CheckMemorySizeLessThanLLT")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("MMO") << MatchTable::ULEB128Value(MMOIdx)
        << MatchTable::Comment("OpIdx") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::LineBreak;
}

//===- VectorSplatImmPredicateMatcher -------------------------------------===//

void VectorSplatImmPredicateMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  if (Kind == AllOnes)
    Table << MatchTable::Opcode("GIM_CheckIsBuildVectorAllOnes");
  else
    Table << MatchTable::Opcode("GIM_CheckIsBuildVectorAllZeros");

  Table << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID);
  Table << MatchTable::LineBreak;
}

//===- GenericInstructionPredicateMatcher ---------------------------------===//

GenericInstructionPredicateMatcher::GenericInstructionPredicateMatcher(
    unsigned InsnVarID, TreePredicateFn Predicate)
    : GenericInstructionPredicateMatcher(InsnVarID,
                                         getEnumNameForPredicate(Predicate)) {}

bool GenericInstructionPredicateMatcher::isIdentical(
    const PredicateMatcher &B) const {
  return InstructionPredicateMatcher::isIdentical(B) &&
         EnumVal ==
             static_cast<const GenericInstructionPredicateMatcher &>(B).EnumVal;
}
void GenericInstructionPredicateMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIM_CheckCxxInsnPredicate")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::Comment("FnId") << MatchTable::NamedValue(2, EnumVal)
        << MatchTable::LineBreak;
}

//===- MIFlagsInstructionPredicateMatcher ---------------------------------===//

bool MIFlagsInstructionPredicateMatcher::isIdentical(
    const PredicateMatcher &B) const {
  if (!InstructionPredicateMatcher::isIdentical(B))
    return false;
  const auto &Other =
      static_cast<const MIFlagsInstructionPredicateMatcher &>(B);
  return Flags == Other.Flags && CheckNot == Other.CheckNot;
}

void MIFlagsInstructionPredicateMatcher::emitPredicateOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  Table << MatchTable::Opcode(CheckNot ? "GIM_MIFlagsNot" : "GIM_MIFlags")
        << MatchTable::Comment("MI") << MatchTable::ULEB128Value(InsnVarID)
        << MatchTable::NamedValue(4, join(Flags, " | "))
        << MatchTable::LineBreak;
}

//===- InstructionMatcher -------------------------------------------------===//

OperandMatcher &
InstructionMatcher::addOperand(unsigned OpIdx, const std::string &SymbolicName,
                               unsigned AllocatedTemporariesBaseID) {
  Operands.emplace_back(new OperandMatcher(*this, OpIdx, SymbolicName,
                                           AllocatedTemporariesBaseID));
  if (!SymbolicName.empty())
    Rule.defineOperand(SymbolicName, *Operands.back());

  return *Operands.back();
}

OperandMatcher &InstructionMatcher::getOperand(unsigned OpIdx) {
  auto I = llvm::find_if(Operands,
                         [&OpIdx](const std::unique_ptr<OperandMatcher> &X) {
                           return X->getOpIdx() == OpIdx;
                         });
  if (I != Operands.end())
    return **I;
  llvm_unreachable("Failed to lookup operand");
}

OperandMatcher &InstructionMatcher::addPhysRegInput(Record *Reg, unsigned OpIdx,
                                                    unsigned TempOpIdx) {
  assert(SymbolicName.empty());
  OperandMatcher *OM = new OperandMatcher(*this, OpIdx, "", TempOpIdx);
  Operands.emplace_back(OM);
  Rule.definePhysRegOperand(Reg, *OM);
  PhysRegInputs.emplace_back(Reg, OpIdx);
  return *OM;
}

void InstructionMatcher::emitPredicateOpcodes(MatchTable &Table,
                                              RuleMatcher &Rule) {
  if (NumOperandsCheck)
    InstructionNumOperandsMatcher(InsnVarID, getNumOperands())
        .emitPredicateOpcodes(Table, Rule);

  // First emit all instruction level predicates need to be verified before we
  // can verify operands.
  emitFilteredPredicateListOpcodes(
      [](const PredicateMatcher &P) { return !P.dependsOnOperands(); }, Table,
      Rule);

  // Emit all operand constraints.
  for (const auto &Operand : Operands)
    Operand->emitPredicateOpcodes(Table, Rule);

  // All of the tablegen defined predicates should now be matched. Now emit
  // any custom predicates that rely on all generated checks.
  emitFilteredPredicateListOpcodes(
      [](const PredicateMatcher &P) { return P.dependsOnOperands(); }, Table,
      Rule);
}

bool InstructionMatcher::isHigherPriorityThan(InstructionMatcher &B) {
  // Instruction matchers involving more operands have higher priority.
  if (Operands.size() > B.Operands.size())
    return true;
  if (Operands.size() < B.Operands.size())
    return false;

  for (auto &&P : zip(predicates(), B.predicates())) {
    auto L = static_cast<InstructionPredicateMatcher *>(std::get<0>(P).get());
    auto R = static_cast<InstructionPredicateMatcher *>(std::get<1>(P).get());
    if (L->isHigherPriorityThan(*R))
      return true;
    if (R->isHigherPriorityThan(*L))
      return false;
  }

  for (auto Operand : zip(Operands, B.Operands)) {
    if (std::get<0>(Operand)->isHigherPriorityThan(*std::get<1>(Operand)))
      return true;
    if (std::get<1>(Operand)->isHigherPriorityThan(*std::get<0>(Operand)))
      return false;
  }

  return false;
}

unsigned InstructionMatcher::countRendererFns() {
  return std::accumulate(
             predicates().begin(), predicates().end(), 0,
             [](unsigned A,
                const std::unique_ptr<PredicateMatcher> &Predicate) {
               return A + Predicate->countRendererFns();
             }) +
         std::accumulate(
             Operands.begin(), Operands.end(), 0,
             [](unsigned A, const std::unique_ptr<OperandMatcher> &Operand) {
               return A + Operand->countRendererFns();
             });
}

void InstructionMatcher::optimize() {
  SmallVector<std::unique_ptr<PredicateMatcher>, 8> Stash;
  const auto &OpcMatcher = getOpcodeMatcher();

  Stash.push_back(predicates_pop_front());
  if (Stash.back().get() == &OpcMatcher) {
    if (NumOperandsCheck && OpcMatcher.isVariadicNumOperands() &&
        getNumOperands() != 0)
      Stash.emplace_back(
          new InstructionNumOperandsMatcher(InsnVarID, getNumOperands()));
    NumOperandsCheck = false;

    for (auto &OM : Operands)
      for (auto &OP : OM->predicates())
        if (isa<IntrinsicIDOperandMatcher>(OP)) {
          Stash.push_back(std::move(OP));
          OM->eraseNullPredicates();
          break;
        }
  }

  if (InsnVarID > 0) {
    assert(!Operands.empty() && "Nested instruction is expected to def a vreg");
    for (auto &OP : Operands[0]->predicates())
      OP.reset();
    Operands[0]->eraseNullPredicates();
  }
  for (auto &OM : Operands) {
    for (auto &OP : OM->predicates())
      if (isa<LLTOperandMatcher>(OP))
        Stash.push_back(std::move(OP));
    OM->eraseNullPredicates();
  }
  while (!Stash.empty())
    prependPredicate(Stash.pop_back_val());
}

//===- InstructionOperandMatcher ------------------------------------------===//

void InstructionOperandMatcher::emitCaptureOpcodes(MatchTable &Table,
                                                   RuleMatcher &Rule) const {
  const unsigned NewInsnVarID = InsnMatcher->getInsnVarID();
  const bool IgnoreCopies = Flags & GISF_IgnoreCopies;
  Table << MatchTable::Opcode(IgnoreCopies ? "GIM_RecordInsnIgnoreCopies"
                                           : "GIM_RecordInsn")
        << MatchTable::Comment("DefineMI")
        << MatchTable::ULEB128Value(NewInsnVarID) << MatchTable::Comment("MI")
        << MatchTable::ULEB128Value(getInsnVarID())
        << MatchTable::Comment("OpIdx") << MatchTable::ULEB128Value(getOpIdx())
        << MatchTable::Comment("MIs[" + llvm::to_string(NewInsnVarID) + "]")
        << MatchTable::LineBreak;
}

bool InstructionOperandMatcher::isHigherPriorityThan(
    const OperandPredicateMatcher &B) const {
  if (OperandPredicateMatcher::isHigherPriorityThan(B))
    return true;
  if (B.OperandPredicateMatcher::isHigherPriorityThan(*this))
    return false;

  if (const InstructionOperandMatcher *BP =
          dyn_cast<InstructionOperandMatcher>(&B))
    if (InsnMatcher->isHigherPriorityThan(*BP->InsnMatcher))
      return true;
  return false;
}

//===- OperandRenderer ----------------------------------------------------===//

OperandRenderer::~OperandRenderer() {}

//===- CopyRenderer -------------------------------------------------------===//

void CopyRenderer::emitRenderOpcodes(MatchTable &Table, RuleMatcher &Rule,
                                     unsigned NewInsnID, unsigned OldInsnID,
                                     unsigned OpIdx, StringRef Name) {
  if (NewInsnID == 0 && OldInsnID == 0) {
    Table << MatchTable::Opcode("GIR_RootToRootCopy");
  } else {
    Table << MatchTable::Opcode("GIR_Copy") << MatchTable::Comment("NewInsnID")
          << MatchTable::ULEB128Value(NewInsnID)
          << MatchTable::Comment("OldInsnID")
          << MatchTable::ULEB128Value(OldInsnID);
  }

  Table << MatchTable::Comment("OpIdx") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::Comment(Name) << MatchTable::LineBreak;
}

void CopyRenderer::emitRenderOpcodes(MatchTable &Table,
                                     RuleMatcher &Rule) const {
  const OperandMatcher &Operand = Rule.getOperandMatcher(SymbolicName);
  unsigned OldInsnVarID = Rule.getInsnVarID(Operand.getInstructionMatcher());
  emitRenderOpcodes(Table, Rule, NewInsnID, OldInsnVarID, Operand.getOpIdx(),
                    SymbolicName);
}

//===- CopyPhysRegRenderer ------------------------------------------------===//

void CopyPhysRegRenderer::emitRenderOpcodes(MatchTable &Table,
                                            RuleMatcher &Rule) const {
  const OperandMatcher &Operand = Rule.getPhysRegOperandMatcher(PhysReg);
  unsigned OldInsnVarID = Rule.getInsnVarID(Operand.getInstructionMatcher());
  CopyRenderer::emitRenderOpcodes(Table, Rule, NewInsnID, OldInsnVarID,
                                  Operand.getOpIdx(), PhysReg->getName());
}

//===- CopyOrAddZeroRegRenderer -------------------------------------------===//

void CopyOrAddZeroRegRenderer::emitRenderOpcodes(MatchTable &Table,
                                                 RuleMatcher &Rule) const {
  const OperandMatcher &Operand = Rule.getOperandMatcher(SymbolicName);
  unsigned OldInsnVarID = Rule.getInsnVarID(Operand.getInstructionMatcher());
  Table << MatchTable::Opcode("GIR_CopyOrAddZeroReg")
        << MatchTable::Comment("NewInsnID")
        << MatchTable::ULEB128Value(NewInsnID)
        << MatchTable::Comment("OldInsnID")
        << MatchTable::ULEB128Value(OldInsnVarID)
        << MatchTable::Comment("OpIdx")
        << MatchTable::ULEB128Value(Operand.getOpIdx())
        << MatchTable::NamedValue(
               2,
               (ZeroRegisterDef->getValue("Namespace")
                    ? ZeroRegisterDef->getValueAsString("Namespace")
                    : ""),
               ZeroRegisterDef->getName())
        << MatchTable::Comment(SymbolicName) << MatchTable::LineBreak;
}

//===- CopyConstantAsImmRenderer ------------------------------------------===//

void CopyConstantAsImmRenderer::emitRenderOpcodes(MatchTable &Table,
                                                  RuleMatcher &Rule) const {
  InstructionMatcher &InsnMatcher = Rule.getInstructionMatcher(SymbolicName);
  unsigned OldInsnVarID = Rule.getInsnVarID(InsnMatcher);
  Table << MatchTable::Opcode(Signed ? "GIR_CopyConstantAsSImm"
                                     : "GIR_CopyConstantAsUImm")
        << MatchTable::Comment("NewInsnID")
        << MatchTable::ULEB128Value(NewInsnID)
        << MatchTable::Comment("OldInsnID")
        << MatchTable::ULEB128Value(OldInsnVarID)
        << MatchTable::Comment(SymbolicName) << MatchTable::LineBreak;
}

//===- CopyFConstantAsFPImmRenderer ---------------------------------------===//

void CopyFConstantAsFPImmRenderer::emitRenderOpcodes(MatchTable &Table,
                                                     RuleMatcher &Rule) const {
  InstructionMatcher &InsnMatcher = Rule.getInstructionMatcher(SymbolicName);
  unsigned OldInsnVarID = Rule.getInsnVarID(InsnMatcher);
  Table << MatchTable::Opcode("GIR_CopyFConstantAsFPImm")
        << MatchTable::Comment("NewInsnID")
        << MatchTable::ULEB128Value(NewInsnID)
        << MatchTable::Comment("OldInsnID")
        << MatchTable::ULEB128Value(OldInsnVarID)
        << MatchTable::Comment(SymbolicName) << MatchTable::LineBreak;
}

//===- CopySubRegRenderer -------------------------------------------------===//

void CopySubRegRenderer::emitRenderOpcodes(MatchTable &Table,
                                           RuleMatcher &Rule) const {
  const OperandMatcher &Operand = Rule.getOperandMatcher(SymbolicName);
  unsigned OldInsnVarID = Rule.getInsnVarID(Operand.getInstructionMatcher());
  Table << MatchTable::Opcode("GIR_CopySubReg")
        << MatchTable::Comment("NewInsnID")
        << MatchTable::ULEB128Value(NewInsnID)
        << MatchTable::Comment("OldInsnID")
        << MatchTable::ULEB128Value(OldInsnVarID)
        << MatchTable::Comment("OpIdx")
        << MatchTable::ULEB128Value(Operand.getOpIdx())
        << MatchTable::Comment("SubRegIdx")
        << MatchTable::IntValue(2, SubReg->EnumValue)
        << MatchTable::Comment(SymbolicName) << MatchTable::LineBreak;
}

//===- AddRegisterRenderer ------------------------------------------------===//

void AddRegisterRenderer::emitRenderOpcodes(MatchTable &Table,
                                            RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIR_AddRegister")
        << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID);
  if (RegisterDef->getName() != "zero_reg") {
    Table << MatchTable::NamedValue(
        2,
        (RegisterDef->getValue("Namespace")
             ? RegisterDef->getValueAsString("Namespace")
             : ""),
        RegisterDef->getName());
  } else {
    Table << MatchTable::NamedValue(2, Target.getRegNamespace(), "NoRegister");
  }
  Table << MatchTable::Comment("AddRegisterRegFlags");

  // TODO: This is encoded as a 64-bit element, but only 16 or 32-bits are
  // really needed for a physical register reference. We can pack the
  // register and flags in a single field.
  if (IsDef)
    Table << MatchTable::NamedValue(2, "RegState::Define");
  else
    Table << MatchTable::IntValue(2, 0);
  Table << MatchTable::LineBreak;
}

//===- TempRegRenderer ----------------------------------------------------===//

void TempRegRenderer::emitRenderOpcodes(MatchTable &Table,
                                        RuleMatcher &Rule) const {
  const bool NeedsFlags = (SubRegIdx || IsDef);
  if (SubRegIdx) {
    assert(!IsDef);
    Table << MatchTable::Opcode("GIR_AddTempSubRegister");
  } else
    Table << MatchTable::Opcode(NeedsFlags ? "GIR_AddTempRegister"
                                           : "GIR_AddSimpleTempRegister");

  Table << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
        << MatchTable::Comment("TempRegID")
        << MatchTable::ULEB128Value(TempRegID);

  if (!NeedsFlags) {
    Table << MatchTable::LineBreak;
    return;
  }

  Table << MatchTable::Comment("TempRegFlags");
  if (IsDef) {
    SmallString<32> RegFlags;
    RegFlags += "RegState::Define";
    if (IsDead)
      RegFlags += "|RegState::Dead";
    Table << MatchTable::NamedValue(2, RegFlags);
  } else
    Table << MatchTable::IntValue(2, 0);

  if (SubRegIdx)
    Table << MatchTable::NamedValue(2, SubRegIdx->getQualifiedName());
  Table << MatchTable::LineBreak;
}

//===- ImmRenderer --------------------------------------------------------===//

void ImmRenderer::emitAddImm(MatchTable &Table, RuleMatcher &RM,
                             unsigned InsnID, int64_t Imm, StringRef ImmName) {
  const bool IsInt8 = isInt<8>(Imm);

  Table << MatchTable::Opcode(IsInt8 ? "GIR_AddImm8" : "GIR_AddImm")
        << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
        << MatchTable::Comment(ImmName)
        << MatchTable::IntValue(IsInt8 ? 1 : 8, Imm) << MatchTable::LineBreak;
}

void ImmRenderer::emitRenderOpcodes(MatchTable &Table,
                                    RuleMatcher &Rule) const {
  if (CImmLLT) {
    assert(Table.isCombiner() &&
           "ConstantInt immediate are only for combiners!");
    Table << MatchTable::Opcode("GIR_AddCImm") << MatchTable::Comment("InsnID")
          << MatchTable::ULEB128Value(InsnID) << MatchTable::Comment("Type")
          << *CImmLLT << MatchTable::Comment("Imm")
          << MatchTable::IntValue(8, Imm) << MatchTable::LineBreak;
  } else
    emitAddImm(Table, Rule, InsnID, Imm);
}

//===- SubRegIndexRenderer ------------------------------------------------===//

void SubRegIndexRenderer::emitRenderOpcodes(MatchTable &Table,
                                            RuleMatcher &Rule) const {
  ImmRenderer::emitAddImm(Table, Rule, InsnID, SubRegIdx->EnumValue,
                          "SubRegIndex");
}

//===- RenderComplexPatternOperand ----------------------------------------===//

void RenderComplexPatternOperand::emitRenderOpcodes(MatchTable &Table,
                                                    RuleMatcher &Rule) const {
  Table << MatchTable::Opcode(
               SubOperand ? (SubReg ? "GIR_ComplexSubOperandSubRegRenderer"
                                    : "GIR_ComplexSubOperandRenderer")
                          : "GIR_ComplexRenderer")
        << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
        << MatchTable::Comment("RendererID")
        << MatchTable::IntValue(2, RendererID);
  if (SubOperand)
    Table << MatchTable::Comment("SubOperand")
          << MatchTable::ULEB128Value(*SubOperand);
  if (SubReg)
    Table << MatchTable::Comment("SubRegIdx")
          << MatchTable::IntValue(2, SubReg->EnumValue);
  Table << MatchTable::Comment(SymbolicName) << MatchTable::LineBreak;
}

//===- IntrinsicIDRenderer ------------------------------------------------===//

void IntrinsicIDRenderer::emitRenderOpcodes(MatchTable &Table,
                                            RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIR_AddIntrinsicID") << MatchTable::Comment("MI")
        << MatchTable::ULEB128Value(InsnID)
        << MatchTable::NamedValue(2, "Intrinsic::" + II->EnumName)
        << MatchTable::LineBreak;
}

//===- CustomRenderer -----------------------------------------------------===//

void CustomRenderer::emitRenderOpcodes(MatchTable &Table,
                                       RuleMatcher &Rule) const {
  InstructionMatcher &InsnMatcher = Rule.getInstructionMatcher(SymbolicName);
  unsigned OldInsnVarID = Rule.getInsnVarID(InsnMatcher);
  Table << MatchTable::Opcode("GIR_CustomRenderer")
        << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
        << MatchTable::Comment("OldInsnID")
        << MatchTable::ULEB128Value(OldInsnVarID)
        << MatchTable::Comment("Renderer")
        << MatchTable::NamedValue(
               2, "GICR_" + Renderer.getValueAsString("RendererFn").str())
        << MatchTable::Comment(SymbolicName) << MatchTable::LineBreak;
}

//===- CustomOperandRenderer ----------------------------------------------===//

void CustomOperandRenderer::emitRenderOpcodes(MatchTable &Table,
                                              RuleMatcher &Rule) const {
  const OperandMatcher &OpdMatcher = Rule.getOperandMatcher(SymbolicName);
  Table << MatchTable::Opcode("GIR_CustomOperandRenderer")
        << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
        << MatchTable::Comment("OldInsnID")
        << MatchTable::ULEB128Value(OpdMatcher.getInsnVarID())
        << MatchTable::Comment("OpIdx")
        << MatchTable::ULEB128Value(OpdMatcher.getOpIdx())
        << MatchTable::Comment("OperandRenderer")
        << MatchTable::NamedValue(
               2, "GICR_" + Renderer.getValueAsString("RendererFn").str())
        << MatchTable::Comment(SymbolicName) << MatchTable::LineBreak;
}

//===- BuildMIAction ------------------------------------------------------===//

bool BuildMIAction::canMutate(RuleMatcher &Rule,
                              const InstructionMatcher *Insn) const {
  if (!Insn)
    return false;

  if (OperandRenderers.size() != Insn->getNumOperands())
    return false;

  for (const auto &Renderer : enumerate(OperandRenderers)) {
    if (const auto *Copy = dyn_cast<CopyRenderer>(&*Renderer.value())) {
      const OperandMatcher &OM =
          Rule.getOperandMatcher(Copy->getSymbolicName());
      if (Insn != &OM.getInstructionMatcher() ||
          OM.getOpIdx() != Renderer.index())
        return false;
    } else
      return false;
  }

  return true;
}

void BuildMIAction::chooseInsnToMutate(RuleMatcher &Rule) {
  for (auto *MutateCandidate : Rule.mutatable_insns()) {
    if (canMutate(Rule, MutateCandidate)) {
      // Take the first one we're offered that we're able to mutate.
      Rule.reserveInsnMatcherForMutation(MutateCandidate);
      Matched = MutateCandidate;
      return;
    }
  }
}

void BuildMIAction::emitActionOpcodes(MatchTable &Table,
                                      RuleMatcher &Rule) const {
  const auto AddMIFlags = [&]() {
    for (const InstructionMatcher *IM : CopiedFlags) {
      Table << MatchTable::Opcode("GIR_CopyMIFlags")
            << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
            << MatchTable::Comment("OldInsnID")
            << MatchTable::ULEB128Value(IM->getInsnVarID())
            << MatchTable::LineBreak;
    }

    if (!SetFlags.empty()) {
      Table << MatchTable::Opcode("GIR_SetMIFlags")
            << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
            << MatchTable::NamedValue(4, join(SetFlags, " | "))
            << MatchTable::LineBreak;
    }

    if (!UnsetFlags.empty()) {
      Table << MatchTable::Opcode("GIR_UnsetMIFlags")
            << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
            << MatchTable::NamedValue(4, join(UnsetFlags, " | "))
            << MatchTable::LineBreak;
    }
  };

  if (Matched) {
    assert(canMutate(Rule, Matched) &&
           "Arranged to mutate an insn that isn't mutatable");

    unsigned RecycleInsnID = Rule.getInsnVarID(*Matched);
    Table << MatchTable::Opcode("GIR_MutateOpcode")
          << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
          << MatchTable::Comment("RecycleInsnID")
          << MatchTable::ULEB128Value(RecycleInsnID)
          << MatchTable::Comment("Opcode")
          << MatchTable::NamedValue(2, I->Namespace, I->TheDef->getName())
          << MatchTable::LineBreak;

    if (!I->ImplicitDefs.empty() || !I->ImplicitUses.empty()) {
      for (auto *Def : I->ImplicitDefs) {
        auto Namespace = Def->getValue("Namespace")
                             ? Def->getValueAsString("Namespace")
                             : "";
        const bool IsDead = DeadImplicitDefs.contains(Def);
        Table << MatchTable::Opcode("GIR_AddImplicitDef")
              << MatchTable::Comment("InsnID")
              << MatchTable::ULEB128Value(InsnID)
              << MatchTable::NamedValue(2, Namespace, Def->getName())
              << (IsDead ? MatchTable::NamedValue(2, "RegState", "Dead")
                         : MatchTable::IntValue(2, 0))
              << MatchTable::LineBreak;
      }
      for (auto *Use : I->ImplicitUses) {
        auto Namespace = Use->getValue("Namespace")
                             ? Use->getValueAsString("Namespace")
                             : "";
        Table << MatchTable::Opcode("GIR_AddImplicitUse")
              << MatchTable::Comment("InsnID")
              << MatchTable::ULEB128Value(InsnID)
              << MatchTable::NamedValue(2, Namespace, Use->getName())
              << MatchTable::LineBreak;
      }
    }

    AddMIFlags();

    // Mark the mutated instruction as erased.
    Rule.tryEraseInsnID(RecycleInsnID);
    return;
  }

  // TODO: Simple permutation looks like it could be almost as common as
  //       mutation due to commutative operations.

  if (InsnID == 0) {
    Table << MatchTable::Opcode("GIR_BuildRootMI");
  } else {
    Table << MatchTable::Opcode("GIR_BuildMI") << MatchTable::Comment("InsnID")
          << MatchTable::ULEB128Value(InsnID);
  }

  Table << MatchTable::Comment("Opcode")
        << MatchTable::NamedValue(2, I->Namespace, I->TheDef->getName())
        << MatchTable::LineBreak;

  for (const auto &Renderer : OperandRenderers)
    Renderer->emitRenderOpcodes(Table, Rule);

  for (auto [OpIdx, Def] : enumerate(I->ImplicitDefs)) {
    auto Namespace =
        Def->getValue("Namespace") ? Def->getValueAsString("Namespace") : "";
    if (DeadImplicitDefs.contains(Def)) {
      Table
          << MatchTable::Opcode("GIR_SetImplicitDefDead")
          << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
          << MatchTable::Comment(
                 ("OpIdx for " + Namespace + "::" + Def->getName() + "").str())
          << MatchTable::ULEB128Value(OpIdx) << MatchTable::LineBreak;
    }
  }

  if (I->mayLoad || I->mayStore) {
    // Emit the ID's for all the instructions that are matched by this rule.
    // TODO: Limit this to matched instructions that mayLoad/mayStore or have
    //       some other means of having a memoperand. Also limit this to
    //       emitted instructions that expect to have a memoperand too. For
    //       example, (G_SEXT (G_LOAD x)) that results in separate load and
    //       sign-extend instructions shouldn't put the memoperand on the
    //       sign-extend since it has no effect there.

    std::vector<unsigned> MergeInsnIDs;
    for (const auto &IDMatcherPair : Rule.defined_insn_vars())
      MergeInsnIDs.push_back(IDMatcherPair.second);
    llvm::sort(MergeInsnIDs);

    Table << MatchTable::Opcode("GIR_MergeMemOperands")
          << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
          << MatchTable::Comment("NumInsns")
          << MatchTable::IntValue(1, MergeInsnIDs.size())
          << MatchTable::Comment("MergeInsnID's");
    for (const auto &MergeInsnID : MergeInsnIDs)
      Table << MatchTable::ULEB128Value(MergeInsnID);
    Table << MatchTable::LineBreak;
  }

  AddMIFlags();
}

//===- BuildConstantAction ------------------------------------------------===//

void BuildConstantAction::emitActionOpcodes(MatchTable &Table,
                                            RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIR_BuildConstant")
        << MatchTable::Comment("TempRegID")
        << MatchTable::ULEB128Value(TempRegID) << MatchTable::Comment("Val")
        << MatchTable::IntValue(8, Val) << MatchTable::LineBreak;
}

//===- EraseInstAction ----------------------------------------------------===//

void EraseInstAction::emitActionOpcodes(MatchTable &Table,
                                        RuleMatcher &Rule) const {
  // Avoid erasing the same inst twice.
  if (!Rule.tryEraseInsnID(InsnID))
    return;

  Table << MatchTable::Opcode("GIR_EraseFromParent")
        << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
        << MatchTable::LineBreak;
}

bool EraseInstAction::emitActionOpcodesAndDone(MatchTable &Table,
                                               RuleMatcher &Rule) const {
  if (InsnID != 0) {
    emitActionOpcodes(Table, Rule);
    return false;
  }

  if (!Rule.tryEraseInsnID(0))
    return false;

  Table << MatchTable::Opcode("GIR_EraseRootFromParent_Done", -1)
        << MatchTable::LineBreak;
  return true;
}

//===- ReplaceRegAction ---------------------------------------------------===//

void ReplaceRegAction::emitAdditionalPredicates(MatchTable &Table,
                                                RuleMatcher &Rule) const {
  if (TempRegID != (unsigned)-1)
    return;

  Table << MatchTable::Opcode("GIM_CheckCanReplaceReg")
        << MatchTable::Comment("OldInsnID")
        << MatchTable::ULEB128Value(OldInsnID)
        << MatchTable::Comment("OldOpIdx") << MatchTable::ULEB128Value(OldOpIdx)
        << MatchTable::Comment("NewInsnId")
        << MatchTable::ULEB128Value(NewInsnId)
        << MatchTable::Comment("NewOpIdx") << MatchTable::ULEB128Value(NewOpIdx)
        << MatchTable::LineBreak;
}

void ReplaceRegAction::emitActionOpcodes(MatchTable &Table,
                                         RuleMatcher &Rule) const {
  if (TempRegID != (unsigned)-1) {
    Table << MatchTable::Opcode("GIR_ReplaceRegWithTempReg")
          << MatchTable::Comment("OldInsnID")
          << MatchTable::ULEB128Value(OldInsnID)
          << MatchTable::Comment("OldOpIdx")
          << MatchTable::ULEB128Value(OldOpIdx)
          << MatchTable::Comment("TempRegID")
          << MatchTable::ULEB128Value(TempRegID) << MatchTable::LineBreak;
  } else {
    Table << MatchTable::Opcode("GIR_ReplaceReg")
          << MatchTable::Comment("OldInsnID")
          << MatchTable::ULEB128Value(OldInsnID)
          << MatchTable::Comment("OldOpIdx")
          << MatchTable::ULEB128Value(OldOpIdx)
          << MatchTable::Comment("NewInsnId")
          << MatchTable::ULEB128Value(NewInsnId)
          << MatchTable::Comment("NewOpIdx")
          << MatchTable::ULEB128Value(NewOpIdx) << MatchTable::LineBreak;
  }
}

//===- ConstrainOperandToRegClassAction -----------------------------------===//

void ConstrainOperandToRegClassAction::emitActionOpcodes(
    MatchTable &Table, RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIR_ConstrainOperandRC")
        << MatchTable::Comment("InsnID") << MatchTable::ULEB128Value(InsnID)
        << MatchTable::Comment("Op") << MatchTable::ULEB128Value(OpIdx)
        << MatchTable::NamedValue(2, RC.getQualifiedIdName())
        << MatchTable::LineBreak;
}

//===- MakeTempRegisterAction ---------------------------------------------===//

void MakeTempRegisterAction::emitActionOpcodes(MatchTable &Table,
                                               RuleMatcher &Rule) const {
  Table << MatchTable::Opcode("GIR_MakeTempReg")
        << MatchTable::Comment("TempRegID")
        << MatchTable::ULEB128Value(TempRegID) << MatchTable::Comment("TypeID")
        << Ty << MatchTable::LineBreak;
}

} // namespace gi
} // namespace llvm
