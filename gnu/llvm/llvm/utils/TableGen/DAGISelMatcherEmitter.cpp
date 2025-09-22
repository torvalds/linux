//===- DAGISelMatcherEmitter.cpp - Matcher Emitter ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to generate C++ code for a matcher.
//
//===----------------------------------------------------------------------===//

#include "Basic/SDNodeProperties.h"
#include "Common/CodeGenDAGPatterns.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenRegisters.h"
#include "Common/CodeGenTarget.h"
#include "Common/DAGISelMatcher.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

enum {
  IndexWidth = 6,
  FullIndexWidth = IndexWidth + 4,
  HistOpcWidth = 40,
};

cl::OptionCategory DAGISelCat("Options for -gen-dag-isel");

// To reduce generated source code size.
static cl::opt<bool> OmitComments("omit-comments",
                                  cl::desc("Do not generate comments"),
                                  cl::init(false), cl::cat(DAGISelCat));

static cl::opt<bool> InstrumentCoverage(
    "instrument-coverage",
    cl::desc("Generates tables to help identify patterns matched"),
    cl::init(false), cl::cat(DAGISelCat));

namespace {
class MatcherTableEmitter {
  const CodeGenDAGPatterns &CGP;

  SmallVector<unsigned, Matcher::HighestKind + 1> OpcodeCounts;

  std::vector<TreePattern *> NodePredicates;
  std::vector<TreePattern *> NodePredicatesWithOperands;

  // We de-duplicate the predicates by code string, and use this map to track
  // all the patterns with "identical" predicates.
  MapVector<std::string, TinyPtrVector<TreePattern *>, StringMap<unsigned>>
      NodePredicatesByCodeToRun;

  std::vector<std::string> PatternPredicates;

  std::vector<const ComplexPattern *> ComplexPatterns;

  DenseMap<Record *, unsigned> NodeXFormMap;
  std::vector<Record *> NodeXForms;

  std::vector<std::string> VecIncludeStrings;
  MapVector<std::string, unsigned, StringMap<unsigned>> VecPatterns;

  unsigned getPatternIdxFromTable(std::string &&P, std::string &&include_loc) {
    const auto It = VecPatterns.find(P);
    if (It == VecPatterns.end()) {
      VecPatterns.insert(std::pair(std::move(P), VecPatterns.size()));
      VecIncludeStrings.push_back(std::move(include_loc));
      return VecIncludeStrings.size() - 1;
    }
    return It->second;
  }

public:
  MatcherTableEmitter(const Matcher *TheMatcher, const CodeGenDAGPatterns &cgp)
      : CGP(cgp), OpcodeCounts(Matcher::HighestKind + 1, 0) {
    // Record the usage of ComplexPattern.
    MapVector<const ComplexPattern *, unsigned> ComplexPatternUsage;
    // Record the usage of PatternPredicate.
    MapVector<StringRef, unsigned> PatternPredicateUsage;
    // Record the usage of Predicate.
    MapVector<TreePattern *, unsigned> PredicateUsage;

    // Iterate the whole MatcherTable once and do some statistics.
    std::function<void(const Matcher *)> Statistic = [&](const Matcher *N) {
      while (N) {
        if (auto *SM = dyn_cast<ScopeMatcher>(N))
          for (unsigned I = 0; I < SM->getNumChildren(); I++)
            Statistic(SM->getChild(I));
        else if (auto *SOM = dyn_cast<SwitchOpcodeMatcher>(N))
          for (unsigned I = 0; I < SOM->getNumCases(); I++)
            Statistic(SOM->getCaseMatcher(I));
        else if (auto *STM = dyn_cast<SwitchTypeMatcher>(N))
          for (unsigned I = 0; I < STM->getNumCases(); I++)
            Statistic(STM->getCaseMatcher(I));
        else if (auto *CPM = dyn_cast<CheckComplexPatMatcher>(N))
          ++ComplexPatternUsage[&CPM->getPattern()];
        else if (auto *CPPM = dyn_cast<CheckPatternPredicateMatcher>(N))
          ++PatternPredicateUsage[CPPM->getPredicate()];
        else if (auto *PM = dyn_cast<CheckPredicateMatcher>(N))
          ++PredicateUsage[PM->getPredicate().getOrigPatFragRecord()];
        N = N->getNext();
      }
    };
    Statistic(TheMatcher);

    // Sort ComplexPatterns by usage.
    std::vector<std::pair<const ComplexPattern *, unsigned>> ComplexPatternList(
        ComplexPatternUsage.begin(), ComplexPatternUsage.end());
    stable_sort(ComplexPatternList, [](const auto &A, const auto &B) {
      return A.second > B.second;
    });
    for (const auto &ComplexPattern : ComplexPatternList)
      ComplexPatterns.push_back(ComplexPattern.first);

    // Sort PatternPredicates by usage.
    std::vector<std::pair<std::string, unsigned>> PatternPredicateList(
        PatternPredicateUsage.begin(), PatternPredicateUsage.end());
    stable_sort(PatternPredicateList, [](const auto &A, const auto &B) {
      return A.second > B.second;
    });
    for (const auto &PatternPredicate : PatternPredicateList)
      PatternPredicates.push_back(PatternPredicate.first);

    // Sort Predicates by usage.
    // Merge predicates with same code.
    for (const auto &Usage : PredicateUsage) {
      TreePattern *TP = Usage.first;
      TreePredicateFn Pred(TP);
      NodePredicatesByCodeToRun[Pred.getCodeToRunOnSDNode()].push_back(TP);
    }

    std::vector<std::pair<TreePattern *, unsigned>> PredicateList;
    // Sum the usage.
    for (auto &Predicate : NodePredicatesByCodeToRun) {
      TinyPtrVector<TreePattern *> &TPs = Predicate.second;
      stable_sort(TPs, [](const auto *A, const auto *B) {
        return A->getRecord()->getName() < B->getRecord()->getName();
      });
      unsigned Uses = 0;
      for (TreePattern *TP : TPs)
        Uses += PredicateUsage[TP];

      // We only add the first predicate here since they are with the same code.
      PredicateList.push_back({TPs[0], Uses});
    }

    stable_sort(PredicateList, [](const auto &A, const auto &B) {
      return A.second > B.second;
    });
    for (const auto &Predicate : PredicateList) {
      TreePattern *TP = Predicate.first;
      if (TreePredicateFn(TP).usesOperands())
        NodePredicatesWithOperands.push_back(TP);
      else
        NodePredicates.push_back(TP);
    }
  }

  unsigned EmitMatcherList(const Matcher *N, const unsigned Indent,
                           unsigned StartIdx, raw_ostream &OS);

  unsigned SizeMatcherList(Matcher *N, raw_ostream &OS);

  void EmitPredicateFunctions(raw_ostream &OS);

  void EmitHistogram(const Matcher *N, raw_ostream &OS);

  void EmitPatternMatchTable(raw_ostream &OS);

private:
  void EmitNodePredicatesFunction(const std::vector<TreePattern *> &Preds,
                                  StringRef Decl, raw_ostream &OS);

  unsigned SizeMatcher(Matcher *N, raw_ostream &OS);

  unsigned EmitMatcher(const Matcher *N, const unsigned Indent,
                       unsigned CurrentIdx, raw_ostream &OS);

  unsigned getNodePredicate(TreePredicateFn Pred) {
    // We use the first predicate.
    TreePattern *PredPat =
        NodePredicatesByCodeToRun[Pred.getCodeToRunOnSDNode()][0];
    return Pred.usesOperands()
               ? llvm::find(NodePredicatesWithOperands, PredPat) -
                     NodePredicatesWithOperands.begin()
               : llvm::find(NodePredicates, PredPat) - NodePredicates.begin();
  }

  unsigned getPatternPredicate(StringRef PredName) {
    return llvm::find(PatternPredicates, PredName) - PatternPredicates.begin();
  }
  unsigned getComplexPat(const ComplexPattern &P) {
    return llvm::find(ComplexPatterns, &P) - ComplexPatterns.begin();
  }

  unsigned getNodeXFormID(Record *Rec) {
    unsigned &Entry = NodeXFormMap[Rec];
    if (Entry == 0) {
      NodeXForms.push_back(Rec);
      Entry = NodeXForms.size();
    }
    return Entry - 1;
  }
};
} // end anonymous namespace.

static std::string GetPatFromTreePatternNode(const TreePatternNode &N) {
  std::string str;
  raw_string_ostream Stream(str);
  Stream << N;
  return str;
}

static unsigned GetVBRSize(unsigned Val) {
  if (Val <= 127)
    return 1;

  unsigned NumBytes = 0;
  while (Val >= 128) {
    Val >>= 7;
    ++NumBytes;
  }
  return NumBytes + 1;
}

/// EmitVBRValue - Emit the specified value as a VBR, returning the number of
/// bytes emitted.
static unsigned EmitVBRValue(uint64_t Val, raw_ostream &OS) {
  if (Val <= 127) {
    OS << Val << ", ";
    return 1;
  }

  uint64_t InVal = Val;
  unsigned NumBytes = 0;
  while (Val >= 128) {
    OS << (Val & 127) << "|128,";
    Val >>= 7;
    ++NumBytes;
  }
  OS << Val;
  if (!OmitComments)
    OS << "/*" << InVal << "*/";
  OS << ", ";
  return NumBytes + 1;
}

/// Emit the specified signed value as a VBR. To improve compression we encode
/// positive numbers shifted left by 1 and negative numbers negated and shifted
/// left by 1 with bit 0 set.
static unsigned EmitSignedVBRValue(uint64_t Val, raw_ostream &OS) {
  if ((int64_t)Val >= 0)
    Val = Val << 1;
  else
    Val = (-Val << 1) | 1;

  return EmitVBRValue(Val, OS);
}

// This is expensive and slow.
static std::string getIncludePath(const Record *R) {
  std::string str;
  raw_string_ostream Stream(str);
  auto Locs = R->getLoc();
  SMLoc L;
  if (Locs.size() > 1) {
    // Get where the pattern prototype was instantiated
    L = Locs[1];
  } else if (Locs.size() == 1) {
    L = Locs[0];
  }
  unsigned CurBuf = SrcMgr.FindBufferContainingLoc(L);
  assert(CurBuf && "Invalid or unspecified location!");

  Stream << SrcMgr.getBufferInfo(CurBuf).Buffer->getBufferIdentifier() << ":"
         << SrcMgr.FindLineNumber(L, CurBuf);
  return str;
}

/// This function traverses the matcher tree and sizes all the nodes
/// that are children of the three kinds of nodes that have them.
unsigned MatcherTableEmitter::SizeMatcherList(Matcher *N, raw_ostream &OS) {
  unsigned Size = 0;
  while (N) {
    Size += SizeMatcher(N, OS);
    N = N->getNext();
  }
  return Size;
}

/// This function sizes the children of the three kinds of nodes that
/// have them. It does so by using special cases for those three
/// nodes, but sharing the code in EmitMatcher() for the other kinds.
unsigned MatcherTableEmitter::SizeMatcher(Matcher *N, raw_ostream &OS) {
  unsigned Idx = 0;

  ++OpcodeCounts[N->getKind()];
  switch (N->getKind()) {
  // The Scope matcher has its kind, a series of child size + child,
  // and a trailing zero.
  case Matcher::Scope: {
    ScopeMatcher *SM = cast<ScopeMatcher>(N);
    assert(SM->getNext() == nullptr && "Scope matcher should not have next");
    unsigned Size = 1; // Count the kind.
    for (unsigned i = 0, e = SM->getNumChildren(); i != e; ++i) {
      const unsigned ChildSize = SizeMatcherList(SM->getChild(i), OS);
      assert(ChildSize != 0 && "Matcher cannot have child of size 0");
      SM->getChild(i)->setSize(ChildSize);
      Size += GetVBRSize(ChildSize) + ChildSize; // Count VBR and child size.
    }
    ++Size; // Count the zero sentinel.
    return Size;
  }

  // SwitchOpcode and SwitchType have their kind, a series of child size +
  // opcode/type + child, and a trailing zero.
  case Matcher::SwitchOpcode:
  case Matcher::SwitchType: {
    unsigned Size = 1; // Count the kind.
    unsigned NumCases;
    if (const SwitchOpcodeMatcher *SOM = dyn_cast<SwitchOpcodeMatcher>(N))
      NumCases = SOM->getNumCases();
    else
      NumCases = cast<SwitchTypeMatcher>(N)->getNumCases();
    for (unsigned i = 0, e = NumCases; i != e; ++i) {
      Matcher *Child;
      if (SwitchOpcodeMatcher *SOM = dyn_cast<SwitchOpcodeMatcher>(N)) {
        Child = SOM->getCaseMatcher(i);
        Size += 2; // Count the child's opcode.
      } else {
        Child = cast<SwitchTypeMatcher>(N)->getCaseMatcher(i);
        ++Size; // Count the child's type.
      }
      const unsigned ChildSize = SizeMatcherList(Child, OS);
      assert(ChildSize != 0 && "Matcher cannot have child of size 0");
      Child->setSize(ChildSize);
      Size += GetVBRSize(ChildSize) + ChildSize; // Count VBR and child size.
    }
    ++Size; // Count the zero sentinel.
    return Size;
  }

  default:
    // Employ the matcher emitter to size other matchers.
    return EmitMatcher(N, 0, Idx, OS);
  }
  llvm_unreachable("Unreachable");
}

static void BeginEmitFunction(raw_ostream &OS, StringRef RetType,
                              StringRef Decl, bool AddOverride) {
  OS << "#ifdef GET_DAGISEL_DECL\n";
  OS << RetType << ' ' << Decl;
  if (AddOverride)
    OS << " override";
  OS << ";\n"
        "#endif\n"
        "#if defined(GET_DAGISEL_BODY) || DAGISEL_INLINE\n";
  OS << RetType << " DAGISEL_CLASS_COLONCOLON " << Decl << "\n";
  if (AddOverride) {
    OS << "#if DAGISEL_INLINE\n"
          "  override\n"
          "#endif\n";
  }
}

static void EndEmitFunction(raw_ostream &OS) {
  OS << "#endif // GET_DAGISEL_BODY\n\n";
}

void MatcherTableEmitter::EmitPatternMatchTable(raw_ostream &OS) {

  assert(isUInt<16>(VecPatterns.size()) &&
         "Using only 16 bits to encode offset into Pattern Table");
  assert(VecPatterns.size() == VecIncludeStrings.size() &&
         "The sizes of Pattern and include vectors should be the same");

  BeginEmitFunction(OS, "StringRef", "getPatternForIndex(unsigned Index)",
                    true /*AddOverride*/);
  OS << "{\n";
  OS << "static const char *PATTERN_MATCH_TABLE[] = {\n";

  for (const auto &It : VecPatterns) {
    OS << "\"" << It.first << "\",\n";
  }

  OS << "\n};";
  OS << "\nreturn StringRef(PATTERN_MATCH_TABLE[Index]);";
  OS << "\n}\n";
  EndEmitFunction(OS);

  BeginEmitFunction(OS, "StringRef", "getIncludePathForIndex(unsigned Index)",
                    true /*AddOverride*/);
  OS << "{\n";
  OS << "static const char *INCLUDE_PATH_TABLE[] = {\n";

  for (const auto &It : VecIncludeStrings) {
    OS << "\"" << It << "\",\n";
  }

  OS << "\n};";
  OS << "\nreturn StringRef(INCLUDE_PATH_TABLE[Index]);";
  OS << "\n}\n";
  EndEmitFunction(OS);
}

/// EmitMatcher - Emit bytes for the specified matcher and return
/// the number of bytes emitted.
unsigned MatcherTableEmitter::EmitMatcher(const Matcher *N,
                                          const unsigned Indent,
                                          unsigned CurrentIdx,
                                          raw_ostream &OS) {
  OS.indent(Indent);

  switch (N->getKind()) {
  case Matcher::Scope: {
    const ScopeMatcher *SM = cast<ScopeMatcher>(N);
    unsigned StartIdx = CurrentIdx;

    // Emit all of the children.
    for (unsigned i = 0, e = SM->getNumChildren(); i != e; ++i) {
      if (i == 0) {
        OS << "OPC_Scope, ";
        ++CurrentIdx;
      } else {
        if (!OmitComments) {
          OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
          OS.indent(Indent) << "/*Scope*/ ";
        } else
          OS.indent(Indent);
      }

      unsigned ChildSize = SM->getChild(i)->getSize();
      unsigned VBRSize = EmitVBRValue(ChildSize, OS);
      if (!OmitComments) {
        OS << "/*->" << CurrentIdx + VBRSize + ChildSize << "*/";
        if (i == 0)
          OS << " // " << SM->getNumChildren() << " children in Scope";
      }
      OS << '\n';

      ChildSize = EmitMatcherList(SM->getChild(i), Indent + 1,
                                  CurrentIdx + VBRSize, OS);
      assert(ChildSize == SM->getChild(i)->getSize() &&
             "Emitted child size does not match calculated size");
      CurrentIdx += VBRSize + ChildSize;
    }

    // Emit a zero as a sentinel indicating end of 'Scope'.
    if (!OmitComments)
      OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
    OS.indent(Indent) << "0, ";
    if (!OmitComments)
      OS << "/*End of Scope*/";
    OS << '\n';
    return CurrentIdx - StartIdx + 1;
  }

  case Matcher::RecordNode:
    OS << "OPC_RecordNode,";
    if (!OmitComments)
      OS << " // #" << cast<RecordMatcher>(N)->getResultNo() << " = "
         << cast<RecordMatcher>(N)->getWhatFor();
    OS << '\n';
    return 1;

  case Matcher::RecordChild:
    OS << "OPC_RecordChild" << cast<RecordChildMatcher>(N)->getChildNo() << ',';
    if (!OmitComments)
      OS << " // #" << cast<RecordChildMatcher>(N)->getResultNo() << " = "
         << cast<RecordChildMatcher>(N)->getWhatFor();
    OS << '\n';
    return 1;

  case Matcher::RecordMemRef:
    OS << "OPC_RecordMemRef,\n";
    return 1;

  case Matcher::CaptureGlueInput:
    OS << "OPC_CaptureGlueInput,\n";
    return 1;

  case Matcher::MoveChild: {
    const auto *MCM = cast<MoveChildMatcher>(N);

    OS << "OPC_MoveChild";
    // Handle the specialized forms.
    if (MCM->getChildNo() >= 8)
      OS << ", ";
    OS << MCM->getChildNo() << ",\n";
    return (MCM->getChildNo() >= 8) ? 2 : 1;
  }

  case Matcher::MoveSibling: {
    const auto *MSM = cast<MoveSiblingMatcher>(N);

    OS << "OPC_MoveSibling";
    // Handle the specialized forms.
    if (MSM->getSiblingNo() >= 8)
      OS << ", ";
    OS << MSM->getSiblingNo() << ",\n";
    return (MSM->getSiblingNo() >= 8) ? 2 : 1;
  }

  case Matcher::MoveParent:
    OS << "OPC_MoveParent,\n";
    return 1;

  case Matcher::CheckSame:
    OS << "OPC_CheckSame, " << cast<CheckSameMatcher>(N)->getMatchNumber()
       << ",\n";
    return 2;

  case Matcher::CheckChildSame:
    OS << "OPC_CheckChild" << cast<CheckChildSameMatcher>(N)->getChildNo()
       << "Same, " << cast<CheckChildSameMatcher>(N)->getMatchNumber() << ",\n";
    return 2;

  case Matcher::CheckPatternPredicate: {
    StringRef Pred = cast<CheckPatternPredicateMatcher>(N)->getPredicate();
    unsigned PredNo = getPatternPredicate(Pred);
    if (PredNo > 255)
      OS << "OPC_CheckPatternPredicateTwoByte, TARGET_VAL(" << PredNo << "),";
    else if (PredNo < 8)
      OS << "OPC_CheckPatternPredicate" << PredNo << ',';
    else
      OS << "OPC_CheckPatternPredicate, " << PredNo << ',';
    if (!OmitComments)
      OS << " // " << Pred;
    OS << '\n';
    return 2 + (PredNo > 255) - (PredNo < 8);
  }
  case Matcher::CheckPredicate: {
    TreePredicateFn Pred = cast<CheckPredicateMatcher>(N)->getPredicate();
    unsigned OperandBytes = 0;
    unsigned PredNo = getNodePredicate(Pred);

    if (Pred.usesOperands()) {
      unsigned NumOps = cast<CheckPredicateMatcher>(N)->getNumOperands();
      OS << "OPC_CheckPredicateWithOperands, " << NumOps << "/*#Ops*/, ";
      for (unsigned i = 0; i < NumOps; ++i)
        OS << cast<CheckPredicateMatcher>(N)->getOperandNo(i) << ", ";
      OperandBytes = 1 + NumOps;
    } else {
      if (PredNo < 8) {
        OperandBytes = -1;
        OS << "OPC_CheckPredicate" << PredNo << ", ";
      } else
        OS << "OPC_CheckPredicate, ";
    }

    if (PredNo >= 8 || Pred.usesOperands())
      OS << PredNo << ',';
    if (!OmitComments)
      OS << " // " << Pred.getFnName();
    OS << '\n';
    return 2 + OperandBytes;
  }

  case Matcher::CheckOpcode:
    OS << "OPC_CheckOpcode, TARGET_VAL("
       << cast<CheckOpcodeMatcher>(N)->getOpcode().getEnumName() << "),\n";
    return 3;

  case Matcher::SwitchOpcode:
  case Matcher::SwitchType: {
    unsigned StartIdx = CurrentIdx;

    unsigned NumCases;
    if (const SwitchOpcodeMatcher *SOM = dyn_cast<SwitchOpcodeMatcher>(N)) {
      OS << "OPC_SwitchOpcode ";
      NumCases = SOM->getNumCases();
    } else {
      OS << "OPC_SwitchType ";
      NumCases = cast<SwitchTypeMatcher>(N)->getNumCases();
    }

    if (!OmitComments)
      OS << "/*" << NumCases << " cases */";
    OS << ", ";
    ++CurrentIdx;

    // For each case we emit the size, then the opcode, then the matcher.
    for (unsigned i = 0, e = NumCases; i != e; ++i) {
      const Matcher *Child;
      unsigned IdxSize;
      if (const SwitchOpcodeMatcher *SOM = dyn_cast<SwitchOpcodeMatcher>(N)) {
        Child = SOM->getCaseMatcher(i);
        IdxSize = 2; // size of opcode in table is 2 bytes.
      } else {
        Child = cast<SwitchTypeMatcher>(N)->getCaseMatcher(i);
        IdxSize = 1; // size of type in table is 1 byte.
      }

      if (i != 0) {
        if (!OmitComments)
          OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
        OS.indent(Indent);
        if (!OmitComments)
          OS << (isa<SwitchOpcodeMatcher>(N) ? "/*SwitchOpcode*/ "
                                             : "/*SwitchType*/ ");
      }

      unsigned ChildSize = Child->getSize();
      CurrentIdx += EmitVBRValue(ChildSize, OS) + IdxSize;
      if (const SwitchOpcodeMatcher *SOM = dyn_cast<SwitchOpcodeMatcher>(N))
        OS << "TARGET_VAL(" << SOM->getCaseOpcode(i).getEnumName() << "),";
      else
        OS << getEnumName(cast<SwitchTypeMatcher>(N)->getCaseType(i)) << ',';
      if (!OmitComments)
        OS << "// ->" << CurrentIdx + ChildSize;
      OS << '\n';

      ChildSize = EmitMatcherList(Child, Indent + 1, CurrentIdx, OS);
      assert(ChildSize == Child->getSize() &&
             "Emitted child size does not match calculated size");
      CurrentIdx += ChildSize;
    }

    // Emit the final zero to terminate the switch.
    if (!OmitComments)
      OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
    OS.indent(Indent) << "0,";
    if (!OmitComments)
      OS << (isa<SwitchOpcodeMatcher>(N) ? " // EndSwitchOpcode"
                                         : " // EndSwitchType");

    OS << '\n';
    return CurrentIdx - StartIdx + 1;
  }

  case Matcher::CheckType:
    if (cast<CheckTypeMatcher>(N)->getResNo() == 0) {
      MVT::SimpleValueType VT = cast<CheckTypeMatcher>(N)->getType();
      switch (VT) {
      case MVT::i32:
      case MVT::i64:
        OS << "OPC_CheckTypeI" << MVT(VT).getSizeInBits() << ",\n";
        return 1;
      default:
        OS << "OPC_CheckType, " << getEnumName(VT) << ",\n";
        return 2;
      }
    }
    OS << "OPC_CheckTypeRes, " << cast<CheckTypeMatcher>(N)->getResNo() << ", "
       << getEnumName(cast<CheckTypeMatcher>(N)->getType()) << ",\n";
    return 3;

  case Matcher::CheckChildType: {
    MVT::SimpleValueType VT = cast<CheckChildTypeMatcher>(N)->getType();
    switch (VT) {
    case MVT::i32:
    case MVT::i64:
      OS << "OPC_CheckChild" << cast<CheckChildTypeMatcher>(N)->getChildNo()
         << "TypeI" << MVT(VT).getSizeInBits() << ",\n";
      return 1;
    default:
      OS << "OPC_CheckChild" << cast<CheckChildTypeMatcher>(N)->getChildNo()
         << "Type, " << getEnumName(VT) << ",\n";
      return 2;
    }
  }

  case Matcher::CheckInteger: {
    OS << "OPC_CheckInteger, ";
    unsigned Bytes =
        1 + EmitSignedVBRValue(cast<CheckIntegerMatcher>(N)->getValue(), OS);
    OS << '\n';
    return Bytes;
  }
  case Matcher::CheckChildInteger: {
    OS << "OPC_CheckChild" << cast<CheckChildIntegerMatcher>(N)->getChildNo()
       << "Integer, ";
    unsigned Bytes = 1 + EmitSignedVBRValue(
                             cast<CheckChildIntegerMatcher>(N)->getValue(), OS);
    OS << '\n';
    return Bytes;
  }
  case Matcher::CheckCondCode:
    OS << "OPC_CheckCondCode, ISD::"
       << cast<CheckCondCodeMatcher>(N)->getCondCodeName() << ",\n";
    return 2;

  case Matcher::CheckChild2CondCode:
    OS << "OPC_CheckChild2CondCode, ISD::"
       << cast<CheckChild2CondCodeMatcher>(N)->getCondCodeName() << ",\n";
    return 2;

  case Matcher::CheckValueType:
    OS << "OPC_CheckValueType, "
       << getEnumName(cast<CheckValueTypeMatcher>(N)->getVT()) << ",\n";
    return 2;

  case Matcher::CheckComplexPat: {
    const CheckComplexPatMatcher *CCPM = cast<CheckComplexPatMatcher>(N);
    const ComplexPattern &Pattern = CCPM->getPattern();
    unsigned PatternNo = getComplexPat(Pattern);
    if (PatternNo < 8)
      OS << "OPC_CheckComplexPat" << PatternNo << ", /*#*/"
         << CCPM->getMatchNumber() << ',';
    else
      OS << "OPC_CheckComplexPat, /*CP*/" << PatternNo << ", /*#*/"
         << CCPM->getMatchNumber() << ',';

    if (!OmitComments) {
      OS << " // " << Pattern.getSelectFunc();
      OS << ":$" << CCPM->getName();
      for (unsigned i = 0, e = Pattern.getNumOperands(); i != e; ++i)
        OS << " #" << CCPM->getFirstResult() + i;

      if (Pattern.hasProperty(SDNPHasChain))
        OS << " + chain result";
    }
    OS << '\n';
    return PatternNo < 8 ? 2 : 3;
  }

  case Matcher::CheckAndImm: {
    OS << "OPC_CheckAndImm, ";
    unsigned Bytes =
        1 + EmitVBRValue(cast<CheckAndImmMatcher>(N)->getValue(), OS);
    OS << '\n';
    return Bytes;
  }

  case Matcher::CheckOrImm: {
    OS << "OPC_CheckOrImm, ";
    unsigned Bytes =
        1 + EmitVBRValue(cast<CheckOrImmMatcher>(N)->getValue(), OS);
    OS << '\n';
    return Bytes;
  }

  case Matcher::CheckFoldableChainNode:
    OS << "OPC_CheckFoldableChainNode,\n";
    return 1;

  case Matcher::CheckImmAllOnesV:
    OS << "OPC_CheckImmAllOnesV,\n";
    return 1;

  case Matcher::CheckImmAllZerosV:
    OS << "OPC_CheckImmAllZerosV,\n";
    return 1;

  case Matcher::EmitInteger: {
    int64_t Val = cast<EmitIntegerMatcher>(N)->getValue();
    MVT::SimpleValueType VT = cast<EmitIntegerMatcher>(N)->getVT();
    unsigned OpBytes;
    switch (VT) {
    case MVT::i8:
    case MVT::i16:
    case MVT::i32:
    case MVT::i64:
      OpBytes = 1;
      OS << "OPC_EmitInteger" << MVT(VT).getSizeInBits() << ", ";
      break;
    default:
      OpBytes = 2;
      OS << "OPC_EmitInteger, " << getEnumName(VT) << ", ";
      break;
    }
    unsigned Bytes = OpBytes + EmitSignedVBRValue(Val, OS);
    OS << '\n';
    return Bytes;
  }
  case Matcher::EmitStringInteger: {
    const std::string &Val = cast<EmitStringIntegerMatcher>(N)->getValue();
    MVT::SimpleValueType VT = cast<EmitStringIntegerMatcher>(N)->getVT();
    // These should always fit into 7 bits.
    unsigned OpBytes;
    switch (VT) {
    case MVT::i32:
      OpBytes = 1;
      OS << "OPC_EmitStringInteger" << MVT(VT).getSizeInBits() << ", ";
      break;
    default:
      OpBytes = 2;
      OS << "OPC_EmitStringInteger, " << getEnumName(VT) << ", ";
      break;
    }
    OS << Val << ",\n";
    return OpBytes + 1;
  }

  case Matcher::EmitRegister: {
    const EmitRegisterMatcher *Matcher = cast<EmitRegisterMatcher>(N);
    const CodeGenRegister *Reg = Matcher->getReg();
    MVT::SimpleValueType VT = Matcher->getVT();
    // If the enum value of the register is larger than one byte can handle,
    // use EmitRegister2.
    if (Reg && Reg->EnumValue > 255) {
      OS << "OPC_EmitRegister2, " << getEnumName(VT) << ", ";
      OS << "TARGET_VAL(" << getQualifiedName(Reg->TheDef) << "),\n";
      return 4;
    }
    unsigned OpBytes;
    switch (VT) {
    case MVT::i32:
    case MVT::i64:
      OpBytes = 1;
      OS << "OPC_EmitRegisterI" << MVT(VT).getSizeInBits() << ", ";
      break;
    default:
      OpBytes = 2;
      OS << "OPC_EmitRegister, " << getEnumName(VT) << ", ";
      break;
    }
    if (Reg) {
      OS << getQualifiedName(Reg->TheDef) << ",\n";
    } else {
      OS << "0 ";
      if (!OmitComments)
        OS << "/*zero_reg*/";
      OS << ",\n";
    }
    return OpBytes + 1;
  }

  case Matcher::EmitConvertToTarget: {
    unsigned Slot = cast<EmitConvertToTargetMatcher>(N)->getSlot();
    if (Slot < 8) {
      OS << "OPC_EmitConvertToTarget" << Slot << ",\n";
      return 1;
    }
    OS << "OPC_EmitConvertToTarget, " << Slot << ",\n";
    return 2;
  }

  case Matcher::EmitMergeInputChains: {
    const EmitMergeInputChainsMatcher *MN =
        cast<EmitMergeInputChainsMatcher>(N);

    // Handle the specialized forms OPC_EmitMergeInputChains1_0, 1_1, and 1_2.
    if (MN->getNumNodes() == 1 && MN->getNode(0) < 3) {
      OS << "OPC_EmitMergeInputChains1_" << MN->getNode(0) << ",\n";
      return 1;
    }

    OS << "OPC_EmitMergeInputChains, " << MN->getNumNodes() << ", ";
    for (unsigned i = 0, e = MN->getNumNodes(); i != e; ++i)
      OS << MN->getNode(i) << ", ";
    OS << '\n';
    return 2 + MN->getNumNodes();
  }
  case Matcher::EmitCopyToReg: {
    const auto *C2RMatcher = cast<EmitCopyToRegMatcher>(N);
    int Bytes = 3;
    const CodeGenRegister *Reg = C2RMatcher->getDestPhysReg();
    unsigned Slot = C2RMatcher->getSrcSlot();
    if (Reg->EnumValue > 255) {
      assert(isUInt<16>(Reg->EnumValue) && "not handled");
      OS << "OPC_EmitCopyToRegTwoByte, " << Slot << ", "
         << "TARGET_VAL(" << getQualifiedName(Reg->TheDef) << "),\n";
      ++Bytes;
    } else {
      if (Slot < 8) {
        OS << "OPC_EmitCopyToReg" << Slot << ", "
           << getQualifiedName(Reg->TheDef) << ",\n";
        --Bytes;
      } else
        OS << "OPC_EmitCopyToReg, " << Slot << ", "
           << getQualifiedName(Reg->TheDef) << ",\n";
    }

    return Bytes;
  }
  case Matcher::EmitNodeXForm: {
    const EmitNodeXFormMatcher *XF = cast<EmitNodeXFormMatcher>(N);
    OS << "OPC_EmitNodeXForm, " << getNodeXFormID(XF->getNodeXForm()) << ", "
       << XF->getSlot() << ',';
    if (!OmitComments)
      OS << " // " << XF->getNodeXForm()->getName();
    OS << '\n';
    return 3;
  }

  case Matcher::EmitNode:
  case Matcher::MorphNodeTo: {
    auto NumCoveredBytes = 0;
    if (InstrumentCoverage) {
      if (const MorphNodeToMatcher *SNT = dyn_cast<MorphNodeToMatcher>(N)) {
        NumCoveredBytes = 3;
        OS << "OPC_Coverage, ";
        std::string src =
            GetPatFromTreePatternNode(SNT->getPattern().getSrcPattern());
        std::string dst =
            GetPatFromTreePatternNode(SNT->getPattern().getDstPattern());
        Record *PatRecord = SNT->getPattern().getSrcRecord();
        std::string include_src = getIncludePath(PatRecord);
        unsigned Offset =
            getPatternIdxFromTable(src + " -> " + dst, std::move(include_src));
        OS << "TARGET_VAL(" << Offset << "),\n";
        OS.indent(FullIndexWidth + Indent);
      }
    }
    const EmitNodeMatcherCommon *EN = cast<EmitNodeMatcherCommon>(N);
    bool IsEmitNode = isa<EmitNodeMatcher>(EN);
    OS << (IsEmitNode ? "OPC_EmitNode" : "OPC_MorphNodeTo");
    bool CompressVTs = EN->getNumVTs() < 3;
    bool CompressNodeInfo = false;
    if (CompressVTs) {
      OS << EN->getNumVTs();
      if (!EN->hasChain() && !EN->hasInGlue() && !EN->hasOutGlue() &&
          !EN->hasMemRefs() && EN->getNumFixedArityOperands() == -1) {
        CompressNodeInfo = true;
        OS << "None";
      } else if (EN->hasChain() && !EN->hasInGlue() && !EN->hasOutGlue() &&
                 !EN->hasMemRefs() && EN->getNumFixedArityOperands() == -1) {
        CompressNodeInfo = true;
        OS << "Chain";
      } else if (!IsEmitNode && !EN->hasChain() && EN->hasInGlue() &&
                 !EN->hasOutGlue() && !EN->hasMemRefs() &&
                 EN->getNumFixedArityOperands() == -1) {
        CompressNodeInfo = true;
        OS << "GlueInput";
      } else if (!IsEmitNode && !EN->hasChain() && !EN->hasInGlue() &&
                 EN->hasOutGlue() && !EN->hasMemRefs() &&
                 EN->getNumFixedArityOperands() == -1) {
        CompressNodeInfo = true;
        OS << "GlueOutput";
      }
    }

    const CodeGenInstruction &CGI = EN->getInstruction();
    OS << ", TARGET_VAL(" << CGI.Namespace << "::" << CGI.TheDef->getName()
       << ")";

    if (!CompressNodeInfo) {
      OS << ", 0";
      if (EN->hasChain())
        OS << "|OPFL_Chain";
      if (EN->hasInGlue())
        OS << "|OPFL_GlueInput";
      if (EN->hasOutGlue())
        OS << "|OPFL_GlueOutput";
      if (EN->hasMemRefs())
        OS << "|OPFL_MemRefs";
      if (EN->getNumFixedArityOperands() != -1)
        OS << "|OPFL_Variadic" << EN->getNumFixedArityOperands();
    }
    OS << ",\n";

    OS.indent(FullIndexWidth + Indent + 4);
    if (!CompressVTs) {
      OS << EN->getNumVTs();
      if (!OmitComments)
        OS << "/*#VTs*/";
      OS << ", ";
    }
    for (unsigned i = 0, e = EN->getNumVTs(); i != e; ++i)
      OS << getEnumName(EN->getVT(i)) << ", ";

    OS << EN->getNumOperands();
    if (!OmitComments)
      OS << "/*#Ops*/";
    OS << ", ";
    unsigned NumOperandBytes = 0;
    for (unsigned i = 0, e = EN->getNumOperands(); i != e; ++i)
      NumOperandBytes += EmitVBRValue(EN->getOperand(i), OS);

    if (!OmitComments) {
      // Print the result #'s for EmitNode.
      if (const EmitNodeMatcher *E = dyn_cast<EmitNodeMatcher>(EN)) {
        if (unsigned NumResults = EN->getNumVTs()) {
          OS << " // Results =";
          unsigned First = E->getFirstResultSlot();
          for (unsigned i = 0; i != NumResults; ++i)
            OS << " #" << First + i;
        }
      }
      OS << '\n';

      if (const MorphNodeToMatcher *SNT = dyn_cast<MorphNodeToMatcher>(N)) {
        OS.indent(FullIndexWidth + Indent)
            << "// Src: " << SNT->getPattern().getSrcPattern()
            << " - Complexity = " << SNT->getPattern().getPatternComplexity(CGP)
            << '\n';
        OS.indent(FullIndexWidth + Indent)
            << "// Dst: " << SNT->getPattern().getDstPattern() << '\n';
      }
    } else
      OS << '\n';

    return 4 + !CompressVTs + !CompressNodeInfo + EN->getNumVTs() +
           NumOperandBytes + NumCoveredBytes;
  }
  case Matcher::CompleteMatch: {
    const CompleteMatchMatcher *CM = cast<CompleteMatchMatcher>(N);
    auto NumCoveredBytes = 0;
    if (InstrumentCoverage) {
      NumCoveredBytes = 3;
      OS << "OPC_Coverage, ";
      std::string src =
          GetPatFromTreePatternNode(CM->getPattern().getSrcPattern());
      std::string dst =
          GetPatFromTreePatternNode(CM->getPattern().getDstPattern());
      Record *PatRecord = CM->getPattern().getSrcRecord();
      std::string include_src = getIncludePath(PatRecord);
      unsigned Offset =
          getPatternIdxFromTable(src + " -> " + dst, std::move(include_src));
      OS << "TARGET_VAL(" << Offset << "),\n";
      OS.indent(FullIndexWidth + Indent);
    }
    OS << "OPC_CompleteMatch, " << CM->getNumResults() << ", ";
    unsigned NumResultBytes = 0;
    for (unsigned i = 0, e = CM->getNumResults(); i != e; ++i)
      NumResultBytes += EmitVBRValue(CM->getResult(i), OS);
    OS << '\n';
    if (!OmitComments) {
      OS.indent(FullIndexWidth + Indent)
          << " // Src: " << CM->getPattern().getSrcPattern()
          << " - Complexity = " << CM->getPattern().getPatternComplexity(CGP)
          << '\n';
      OS.indent(FullIndexWidth + Indent)
          << " // Dst: " << CM->getPattern().getDstPattern();
    }
    OS << '\n';
    return 2 + NumResultBytes + NumCoveredBytes;
  }
  }
  llvm_unreachable("Unreachable");
}

/// This function traverses the matcher tree and emits all the nodes.
/// The nodes have already been sized.
unsigned MatcherTableEmitter::EmitMatcherList(const Matcher *N,
                                              const unsigned Indent,
                                              unsigned CurrentIdx,
                                              raw_ostream &OS) {
  unsigned Size = 0;
  while (N) {
    if (!OmitComments)
      OS << "/*" << format_decimal(CurrentIdx, IndexWidth) << "*/";
    unsigned MatcherSize = EmitMatcher(N, Indent, CurrentIdx, OS);
    Size += MatcherSize;
    CurrentIdx += MatcherSize;

    // If there are other nodes in this list, iterate to them, otherwise we're
    // done.
    N = N->getNext();
  }
  return Size;
}

void MatcherTableEmitter::EmitNodePredicatesFunction(
    const std::vector<TreePattern *> &Preds, StringRef Decl, raw_ostream &OS) {
  if (Preds.empty())
    return;

  BeginEmitFunction(OS, "bool", Decl, true /*AddOverride*/);
  OS << "{\n";
  OS << "  switch (PredNo) {\n";
  OS << "  default: llvm_unreachable(\"Invalid predicate in table?\");\n";
  for (unsigned i = 0, e = Preds.size(); i != e; ++i) {
    // Emit the predicate code corresponding to this pattern.
    TreePredicateFn PredFn(Preds[i]);
    assert(!PredFn.isAlwaysTrue() && "No code in this predicate");
    std::string PredFnCodeStr = PredFn.getCodeToRunOnSDNode();

    OS << "  case " << i << ": {\n";
    for (auto *SimilarPred : NodePredicatesByCodeToRun[PredFnCodeStr])
      OS << "    // " << TreePredicateFn(SimilarPred).getFnName() << '\n';
    OS << PredFnCodeStr << "\n  }\n";
  }
  OS << "  }\n";
  OS << "}\n";
  EndEmitFunction(OS);
}

void MatcherTableEmitter::EmitPredicateFunctions(raw_ostream &OS) {
  // Emit pattern predicates.
  if (!PatternPredicates.empty()) {
    BeginEmitFunction(OS, "bool",
                      "CheckPatternPredicate(unsigned PredNo) const",
                      true /*AddOverride*/);
    OS << "{\n";
    OS << "  switch (PredNo) {\n";
    OS << "  default: llvm_unreachable(\"Invalid predicate in table?\");\n";
    for (unsigned i = 0, e = PatternPredicates.size(); i != e; ++i)
      OS << "  case " << i << ": return " << PatternPredicates[i] << ";\n";
    OS << "  }\n";
    OS << "}\n";
    EndEmitFunction(OS);
  }

  // Emit Node predicates.
  EmitNodePredicatesFunction(
      NodePredicates, "CheckNodePredicate(SDNode *Node, unsigned PredNo) const",
      OS);
  EmitNodePredicatesFunction(
      NodePredicatesWithOperands,
      "CheckNodePredicateWithOperands(SDNode *Node, unsigned PredNo, "
      "const SmallVectorImpl<SDValue> &Operands) const",
      OS);

  // Emit CompletePattern matchers.
  // FIXME: This should be const.
  if (!ComplexPatterns.empty()) {
    BeginEmitFunction(
        OS, "bool",
        "CheckComplexPattern(SDNode *Root, SDNode *Parent,\n"
        "      SDValue N, unsigned PatternNo,\n"
        "      SmallVectorImpl<std::pair<SDValue, SDNode *>> &Result)",
        true /*AddOverride*/);
    OS << "{\n";
    OS << "  unsigned NextRes = Result.size();\n";
    OS << "  switch (PatternNo) {\n";
    OS << "  default: llvm_unreachable(\"Invalid pattern # in table?\");\n";
    for (unsigned i = 0, e = ComplexPatterns.size(); i != e; ++i) {
      const ComplexPattern &P = *ComplexPatterns[i];
      unsigned NumOps = P.getNumOperands();

      if (P.hasProperty(SDNPHasChain))
        ++NumOps; // Get the chained node too.

      OS << "  case " << i << ":\n";
      if (InstrumentCoverage)
        OS << "  {\n";
      OS << "    Result.resize(NextRes+" << NumOps << ");\n";
      if (InstrumentCoverage)
        OS << "    bool Succeeded = " << P.getSelectFunc();
      else
        OS << "  return " << P.getSelectFunc();

      OS << "(";
      // If the complex pattern wants the root of the match, pass it in as the
      // first argument.
      if (P.hasProperty(SDNPWantRoot))
        OS << "Root, ";

      // If the complex pattern wants the parent of the operand being matched,
      // pass it in as the next argument.
      if (P.hasProperty(SDNPWantParent))
        OS << "Parent, ";

      OS << "N";
      for (unsigned i = 0; i != NumOps; ++i)
        OS << ", Result[NextRes+" << i << "].first";
      OS << ");\n";
      if (InstrumentCoverage) {
        OS << "    if (Succeeded)\n";
        OS << "       dbgs() << \"\\nCOMPLEX_PATTERN: " << P.getSelectFunc()
           << "\\n\" ;\n";
        OS << "    return Succeeded;\n";
        OS << "    }\n";
      }
    }
    OS << "  }\n";
    OS << "}\n";
    EndEmitFunction(OS);
  }

  // Emit SDNodeXForm handlers.
  // FIXME: This should be const.
  if (!NodeXForms.empty()) {
    BeginEmitFunction(OS, "SDValue",
                      "RunSDNodeXForm(SDValue V, unsigned XFormNo)",
                      true /*AddOverride*/);
    OS << "{\n";
    OS << "  switch (XFormNo) {\n";
    OS << "  default: llvm_unreachable(\"Invalid xform # in table?\");\n";

    // FIXME: The node xform could take SDValue's instead of SDNode*'s.
    for (unsigned i = 0, e = NodeXForms.size(); i != e; ++i) {
      const CodeGenDAGPatterns::NodeXForm &Entry =
          CGP.getSDNodeTransform(NodeXForms[i]);

      Record *SDNode = Entry.first;
      const std::string &Code = Entry.second;

      OS << "  case " << i << ": {  ";
      if (!OmitComments)
        OS << "// " << NodeXForms[i]->getName();
      OS << '\n';

      std::string ClassName =
          std::string(CGP.getSDNodeInfo(SDNode).getSDClassName());
      if (ClassName == "SDNode")
        OS << "    SDNode *N = V.getNode();\n";
      else
        OS << "    " << ClassName << " *N = cast<" << ClassName
           << ">(V.getNode());\n";
      OS << Code << "\n  }\n";
    }
    OS << "  }\n";
    OS << "}\n";
    EndEmitFunction(OS);
  }
}

static StringRef getOpcodeString(Matcher::KindTy Kind) {
  switch (Kind) {
  case Matcher::Scope:
    return "OPC_Scope";
  case Matcher::RecordNode:
    return "OPC_RecordNode";
  case Matcher::RecordChild:
    return "OPC_RecordChild";
  case Matcher::RecordMemRef:
    return "OPC_RecordMemRef";
  case Matcher::CaptureGlueInput:
    return "OPC_CaptureGlueInput";
  case Matcher::MoveChild:
    return "OPC_MoveChild";
  case Matcher::MoveSibling:
    return "OPC_MoveSibling";
  case Matcher::MoveParent:
    return "OPC_MoveParent";
  case Matcher::CheckSame:
    return "OPC_CheckSame";
  case Matcher::CheckChildSame:
    return "OPC_CheckChildSame";
  case Matcher::CheckPatternPredicate:
    return "OPC_CheckPatternPredicate";
  case Matcher::CheckPredicate:
    return "OPC_CheckPredicate";
  case Matcher::CheckOpcode:
    return "OPC_CheckOpcode";
  case Matcher::SwitchOpcode:
    return "OPC_SwitchOpcode";
  case Matcher::CheckType:
    return "OPC_CheckType";
  case Matcher::SwitchType:
    return "OPC_SwitchType";
  case Matcher::CheckChildType:
    return "OPC_CheckChildType";
  case Matcher::CheckInteger:
    return "OPC_CheckInteger";
  case Matcher::CheckChildInteger:
    return "OPC_CheckChildInteger";
  case Matcher::CheckCondCode:
    return "OPC_CheckCondCode";
  case Matcher::CheckChild2CondCode:
    return "OPC_CheckChild2CondCode";
  case Matcher::CheckValueType:
    return "OPC_CheckValueType";
  case Matcher::CheckComplexPat:
    return "OPC_CheckComplexPat";
  case Matcher::CheckAndImm:
    return "OPC_CheckAndImm";
  case Matcher::CheckOrImm:
    return "OPC_CheckOrImm";
  case Matcher::CheckFoldableChainNode:
    return "OPC_CheckFoldableChainNode";
  case Matcher::CheckImmAllOnesV:
    return "OPC_CheckImmAllOnesV";
  case Matcher::CheckImmAllZerosV:
    return "OPC_CheckImmAllZerosV";
  case Matcher::EmitInteger:
    return "OPC_EmitInteger";
  case Matcher::EmitStringInteger:
    return "OPC_EmitStringInteger";
  case Matcher::EmitRegister:
    return "OPC_EmitRegister";
  case Matcher::EmitConvertToTarget:
    return "OPC_EmitConvertToTarget";
  case Matcher::EmitMergeInputChains:
    return "OPC_EmitMergeInputChains";
  case Matcher::EmitCopyToReg:
    return "OPC_EmitCopyToReg";
  case Matcher::EmitNode:
    return "OPC_EmitNode";
  case Matcher::MorphNodeTo:
    return "OPC_MorphNodeTo";
  case Matcher::EmitNodeXForm:
    return "OPC_EmitNodeXForm";
  case Matcher::CompleteMatch:
    return "OPC_CompleteMatch";
  }

  llvm_unreachable("Unhandled opcode?");
}

void MatcherTableEmitter::EmitHistogram(const Matcher *M, raw_ostream &OS) {
  if (OmitComments)
    return;

  OS << "  // Opcode Histogram:\n";
  for (unsigned i = 0, e = OpcodeCounts.size(); i != e; ++i) {
    OS << "  // #"
       << left_justify(getOpcodeString((Matcher::KindTy)i), HistOpcWidth)
       << " = " << OpcodeCounts[i] << '\n';
  }
  OS << '\n';
}

void llvm::EmitMatcherTable(Matcher *TheMatcher, const CodeGenDAGPatterns &CGP,
                            raw_ostream &OS) {
  OS << "#if defined(GET_DAGISEL_DECL) && defined(GET_DAGISEL_BODY)\n";
  OS << "#error GET_DAGISEL_DECL and GET_DAGISEL_BODY cannot be both defined, ";
  OS << "undef both for inline definitions\n";
  OS << "#endif\n\n";

  // Emit a check for omitted class name.
  OS << "#ifdef GET_DAGISEL_BODY\n";
  OS << "#define LOCAL_DAGISEL_STRINGIZE(X) LOCAL_DAGISEL_STRINGIZE_(X)\n";
  OS << "#define LOCAL_DAGISEL_STRINGIZE_(X) #X\n";
  OS << "static_assert(sizeof(LOCAL_DAGISEL_STRINGIZE(GET_DAGISEL_BODY)) > 1,"
        "\n";
  OS << "   \"GET_DAGISEL_BODY is empty: it should be defined with the class "
        "name\");\n";
  OS << "#undef LOCAL_DAGISEL_STRINGIZE_\n";
  OS << "#undef LOCAL_DAGISEL_STRINGIZE\n";
  OS << "#endif\n\n";

  OS << "#if !defined(GET_DAGISEL_DECL) && !defined(GET_DAGISEL_BODY)\n";
  OS << "#define DAGISEL_INLINE 1\n";
  OS << "#else\n";
  OS << "#define DAGISEL_INLINE 0\n";
  OS << "#endif\n\n";

  OS << "#if !DAGISEL_INLINE\n";
  OS << "#define DAGISEL_CLASS_COLONCOLON GET_DAGISEL_BODY ::\n";
  OS << "#else\n";
  OS << "#define DAGISEL_CLASS_COLONCOLON\n";
  OS << "#endif\n\n";

  BeginEmitFunction(OS, "void", "SelectCode(SDNode *N)", false /*AddOverride*/);
  MatcherTableEmitter MatcherEmitter(TheMatcher, CGP);

  // First we size all the children of the three kinds of matchers that have
  // them. This is done by sharing the code in EmitMatcher(). but we don't
  // want to emit anything, so we turn off comments and use a null stream.
  bool SaveOmitComments = OmitComments;
  OmitComments = true;
  raw_null_ostream NullOS;
  unsigned TotalSize = MatcherEmitter.SizeMatcherList(TheMatcher, NullOS);
  OmitComments = SaveOmitComments;

  // Now that the matchers are sized, we can emit the code for them to the
  // final stream.
  OS << "{\n";
  OS << "  // Some target values are emitted as 2 bytes, TARGET_VAL handles\n";
  OS << "  // this.\n";
  OS << "  #define TARGET_VAL(X) X & 255, unsigned(X) >> 8\n";
  OS << "  static const unsigned char MatcherTable[] = {\n";
  TotalSize = MatcherEmitter.EmitMatcherList(TheMatcher, 1, 0, OS);
  OS << "    0\n  }; // Total Array size is " << (TotalSize + 1)
     << " bytes\n\n";

  MatcherEmitter.EmitHistogram(TheMatcher, OS);

  OS << "  #undef TARGET_VAL\n";
  OS << "  SelectCodeCommon(N, MatcherTable, sizeof(MatcherTable));\n";
  OS << "}\n";
  EndEmitFunction(OS);

  // Next up, emit the function for node and pattern predicates:
  MatcherEmitter.EmitPredicateFunctions(OS);

  if (InstrumentCoverage)
    MatcherEmitter.EmitPatternMatchTable(OS);

  // Clean up the preprocessor macros.
  OS << "\n";
  OS << "#ifdef DAGISEL_INLINE\n";
  OS << "#undef DAGISEL_INLINE\n";
  OS << "#endif\n";
  OS << "#ifdef DAGISEL_CLASS_COLONCOLON\n";
  OS << "#undef DAGISEL_CLASS_COLONCOLON\n";
  OS << "#endif\n";
  OS << "#ifdef GET_DAGISEL_DECL\n";
  OS << "#undef GET_DAGISEL_DECL\n";
  OS << "#endif\n";
  OS << "#ifdef GET_DAGISEL_BODY\n";
  OS << "#undef GET_DAGISEL_BODY\n";
  OS << "#endif\n";
}
