///===- FastISelEmitter.cpp - Generate an instruction selector ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits code for use by the "fast" instruction
// selection algorithm. See the comments at the top of
// lib/CodeGen/SelectionDAG/FastISel.cpp for background.
//
// This file scans through the target's tablegen instruction-info files
// and extracts instructions with obvious-looking patterns, and it emits
// code to look up these instructions by type and operator.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenDAGPatterns.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenRegisters.h"
#include "Common/CodeGenTarget.h"
#include "Common/InfoByHwMode.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <set>
#include <utility>
using namespace llvm;

/// InstructionMemo - This class holds additional information about an
/// instruction needed to emit code for it.
///
namespace {
struct InstructionMemo {
  std::string Name;
  const CodeGenRegisterClass *RC;
  std::string SubRegNo;
  std::vector<std::string> PhysRegs;
  std::string PredicateCheck;

  InstructionMemo(StringRef Name, const CodeGenRegisterClass *RC,
                  std::string SubRegNo, std::vector<std::string> PhysRegs,
                  std::string PredicateCheck)
      : Name(Name), RC(RC), SubRegNo(std::move(SubRegNo)),
        PhysRegs(std::move(PhysRegs)),
        PredicateCheck(std::move(PredicateCheck)) {}

  // Make sure we do not copy InstructionMemo.
  InstructionMemo(const InstructionMemo &Other) = delete;
  InstructionMemo(InstructionMemo &&Other) = default;
};
} // End anonymous namespace

/// ImmPredicateSet - This uniques predicates (represented as a string) and
/// gives them unique (small) integer ID's that start at 0.
namespace {
class ImmPredicateSet {
  DenseMap<TreePattern *, unsigned> ImmIDs;
  std::vector<TreePredicateFn> PredsByName;

public:
  unsigned getIDFor(TreePredicateFn Pred) {
    unsigned &Entry = ImmIDs[Pred.getOrigPatFragRecord()];
    if (Entry == 0) {
      PredsByName.push_back(Pred);
      Entry = PredsByName.size();
    }
    return Entry - 1;
  }

  const TreePredicateFn &getPredicate(unsigned i) {
    assert(i < PredsByName.size());
    return PredsByName[i];
  }

  typedef std::vector<TreePredicateFn>::const_iterator iterator;
  iterator begin() const { return PredsByName.begin(); }
  iterator end() const { return PredsByName.end(); }
};
} // End anonymous namespace

/// OperandsSignature - This class holds a description of a list of operand
/// types. It has utility methods for emitting text based on the operands.
///
namespace {
struct OperandsSignature {
  class OpKind {
    enum { OK_Reg, OK_FP, OK_Imm, OK_Invalid = -1 };
    char Repr;

  public:
    OpKind() : Repr(OK_Invalid) {}

    bool operator<(OpKind RHS) const { return Repr < RHS.Repr; }
    bool operator==(OpKind RHS) const { return Repr == RHS.Repr; }

    static OpKind getReg() {
      OpKind K;
      K.Repr = OK_Reg;
      return K;
    }
    static OpKind getFP() {
      OpKind K;
      K.Repr = OK_FP;
      return K;
    }
    static OpKind getImm(unsigned V) {
      assert((unsigned)OK_Imm + V < 128 &&
             "Too many integer predicates for the 'Repr' char");
      OpKind K;
      K.Repr = OK_Imm + V;
      return K;
    }

    bool isReg() const { return Repr == OK_Reg; }
    bool isFP() const { return Repr == OK_FP; }
    bool isImm() const { return Repr >= OK_Imm; }

    unsigned getImmCode() const {
      assert(isImm());
      return Repr - OK_Imm;
    }

    void printManglingSuffix(raw_ostream &OS, ImmPredicateSet &ImmPredicates,
                             bool StripImmCodes) const {
      if (isReg())
        OS << 'r';
      else if (isFP())
        OS << 'f';
      else {
        OS << 'i';
        if (!StripImmCodes)
          if (unsigned Code = getImmCode())
            OS << "_" << ImmPredicates.getPredicate(Code - 1).getFnName();
      }
    }
  };

  SmallVector<OpKind, 3> Operands;

  bool operator<(const OperandsSignature &O) const {
    return Operands < O.Operands;
  }
  bool operator==(const OperandsSignature &O) const {
    return Operands == O.Operands;
  }

  bool empty() const { return Operands.empty(); }

  bool hasAnyImmediateCodes() const {
    for (unsigned i = 0, e = Operands.size(); i != e; ++i)
      if (Operands[i].isImm() && Operands[i].getImmCode() != 0)
        return true;
    return false;
  }

  /// getWithoutImmCodes - Return a copy of this with any immediate codes forced
  /// to zero.
  OperandsSignature getWithoutImmCodes() const {
    OperandsSignature Result;
    for (unsigned i = 0, e = Operands.size(); i != e; ++i)
      if (!Operands[i].isImm())
        Result.Operands.push_back(Operands[i]);
      else
        Result.Operands.push_back(OpKind::getImm(0));
    return Result;
  }

  void emitImmediatePredicate(raw_ostream &OS, ImmPredicateSet &ImmPredicates) {
    bool EmittedAnything = false;
    for (unsigned i = 0, e = Operands.size(); i != e; ++i) {
      if (!Operands[i].isImm())
        continue;

      unsigned Code = Operands[i].getImmCode();
      if (Code == 0)
        continue;

      if (EmittedAnything)
        OS << " &&\n        ";

      TreePredicateFn PredFn = ImmPredicates.getPredicate(Code - 1);

      // Emit the type check.
      TreePattern *TP = PredFn.getOrigPatFragRecord();
      ValueTypeByHwMode VVT = TP->getTree(0)->getType(0);
      assert(VVT.isSimple() &&
             "Cannot use variable value types with fast isel");
      OS << "VT == " << getEnumName(VVT.getSimple().SimpleTy) << " && ";

      OS << PredFn.getFnName() << "(imm" << i << ')';
      EmittedAnything = true;
    }
  }

  /// initialize - Examine the given pattern and initialize the contents
  /// of the Operands array accordingly. Return true if all the operands
  /// are supported, false otherwise.
  ///
  bool initialize(TreePatternNode &InstPatNode, const CodeGenTarget &Target,
                  MVT::SimpleValueType VT, ImmPredicateSet &ImmediatePredicates,
                  const CodeGenRegisterClass *OrigDstRC) {
    if (InstPatNode.isLeaf())
      return false;

    if (InstPatNode.getOperator()->getName() == "imm") {
      Operands.push_back(OpKind::getImm(0));
      return true;
    }

    if (InstPatNode.getOperator()->getName() == "fpimm") {
      Operands.push_back(OpKind::getFP());
      return true;
    }

    const CodeGenRegisterClass *DstRC = nullptr;

    for (unsigned i = 0, e = InstPatNode.getNumChildren(); i != e; ++i) {
      TreePatternNode &Op = InstPatNode.getChild(i);

      // Handle imm operands specially.
      if (!Op.isLeaf() && Op.getOperator()->getName() == "imm") {
        unsigned PredNo = 0;
        if (!Op.getPredicateCalls().empty()) {
          TreePredicateFn PredFn = Op.getPredicateCalls()[0].Fn;
          // If there is more than one predicate weighing in on this operand
          // then we don't handle it.  This doesn't typically happen for
          // immediates anyway.
          if (Op.getPredicateCalls().size() > 1 ||
              !PredFn.isImmediatePattern() || PredFn.usesOperands())
            return false;
          // Ignore any instruction with 'FastIselShouldIgnore', these are
          // not needed and just bloat the fast instruction selector.  For
          // example, X86 doesn't need to generate code to match ADD16ri8 since
          // ADD16ri will do just fine.
          Record *Rec = PredFn.getOrigPatFragRecord()->getRecord();
          if (Rec->getValueAsBit("FastIselShouldIgnore"))
            return false;

          PredNo = ImmediatePredicates.getIDFor(PredFn) + 1;
        }

        Operands.push_back(OpKind::getImm(PredNo));
        continue;
      }

      // For now, filter out any operand with a predicate.
      // For now, filter out any operand with multiple values.
      if (!Op.getPredicateCalls().empty() || Op.getNumTypes() != 1)
        return false;

      if (!Op.isLeaf()) {
        if (Op.getOperator()->getName() == "fpimm") {
          Operands.push_back(OpKind::getFP());
          continue;
        }
        // For now, ignore other non-leaf nodes.
        return false;
      }

      assert(Op.hasConcreteType(0) && "Type infererence not done?");

      // For now, all the operands must have the same type (if they aren't
      // immediates).  Note that this causes us to reject variable sized shifts
      // on X86.
      if (Op.getSimpleType(0) != VT)
        return false;

      DefInit *OpDI = dyn_cast<DefInit>(Op.getLeafValue());
      if (!OpDI)
        return false;
      Record *OpLeafRec = OpDI->getDef();

      // For now, the only other thing we accept is register operands.
      const CodeGenRegisterClass *RC = nullptr;
      if (OpLeafRec->isSubClassOf("RegisterOperand"))
        OpLeafRec = OpLeafRec->getValueAsDef("RegClass");
      if (OpLeafRec->isSubClassOf("RegisterClass"))
        RC = &Target.getRegisterClass(OpLeafRec);
      else if (OpLeafRec->isSubClassOf("Register"))
        RC = Target.getRegBank().getRegClassForRegister(OpLeafRec);
      else if (OpLeafRec->isSubClassOf("ValueType")) {
        RC = OrigDstRC;
      } else
        return false;

      // For now, this needs to be a register class of some sort.
      if (!RC)
        return false;

      // For now, all the operands must have the same register class or be
      // a strict subclass of the destination.
      if (DstRC) {
        if (DstRC != RC && !DstRC->hasSubClass(RC))
          return false;
      } else
        DstRC = RC;
      Operands.push_back(OpKind::getReg());
    }
    return true;
  }

  void PrintParameters(raw_ostream &OS) const {
    ListSeparator LS;
    for (unsigned i = 0, e = Operands.size(); i != e; ++i) {
      OS << LS;
      if (Operands[i].isReg()) {
        OS << "unsigned Op" << i;
      } else if (Operands[i].isImm()) {
        OS << "uint64_t imm" << i;
      } else if (Operands[i].isFP()) {
        OS << "const ConstantFP *f" << i;
      } else {
        llvm_unreachable("Unknown operand kind!");
      }
    }
  }

  void PrintArguments(raw_ostream &OS,
                      const std::vector<std::string> &PR) const {
    assert(PR.size() == Operands.size());
    ListSeparator LS;
    for (unsigned i = 0, e = Operands.size(); i != e; ++i) {
      if (PR[i] != "")
        // Implicit physical register operand.
        continue;

      OS << LS;
      if (Operands[i].isReg()) {
        OS << "Op" << i;
      } else if (Operands[i].isImm()) {
        OS << "imm" << i;
      } else if (Operands[i].isFP()) {
        OS << "f" << i;
      } else {
        llvm_unreachable("Unknown operand kind!");
      }
    }
  }

  void PrintArguments(raw_ostream &OS) const {
    ListSeparator LS;
    for (unsigned i = 0, e = Operands.size(); i != e; ++i) {
      OS << LS;
      if (Operands[i].isReg()) {
        OS << "Op" << i;
      } else if (Operands[i].isImm()) {
        OS << "imm" << i;
      } else if (Operands[i].isFP()) {
        OS << "f" << i;
      } else {
        llvm_unreachable("Unknown operand kind!");
      }
    }
  }

  void PrintManglingSuffix(raw_ostream &OS, const std::vector<std::string> &PR,
                           ImmPredicateSet &ImmPredicates,
                           bool StripImmCodes = false) const {
    for (unsigned i = 0, e = Operands.size(); i != e; ++i) {
      if (PR[i] != "")
        // Implicit physical register operand. e.g. Instruction::Mul expect to
        // select to a binary op. On x86, mul may take a single operand with
        // the other operand being implicit. We must emit something that looks
        // like a binary instruction except for the very inner fastEmitInst_*
        // call.
        continue;
      Operands[i].printManglingSuffix(OS, ImmPredicates, StripImmCodes);
    }
  }

  void PrintManglingSuffix(raw_ostream &OS, ImmPredicateSet &ImmPredicates,
                           bool StripImmCodes = false) const {
    for (unsigned i = 0, e = Operands.size(); i != e; ++i)
      Operands[i].printManglingSuffix(OS, ImmPredicates, StripImmCodes);
  }
};
} // End anonymous namespace

namespace {
class FastISelMap {
  // A multimap is needed instead of a "plain" map because the key is
  // the instruction's complexity (an int) and they are not unique.
  typedef std::multimap<int, InstructionMemo> PredMap;
  typedef std::map<MVT::SimpleValueType, PredMap> RetPredMap;
  typedef std::map<MVT::SimpleValueType, RetPredMap> TypeRetPredMap;
  typedef std::map<std::string, TypeRetPredMap> OpcodeTypeRetPredMap;
  typedef std::map<OperandsSignature, OpcodeTypeRetPredMap>
      OperandsOpcodeTypeRetPredMap;

  OperandsOpcodeTypeRetPredMap SimplePatterns;

  // This is used to check that there are no duplicate predicates
  std::set<std::tuple<OperandsSignature, std::string, MVT::SimpleValueType,
                      MVT::SimpleValueType, std::string>>
      SimplePatternsCheck;

  std::map<OperandsSignature, std::vector<OperandsSignature>>
      SignaturesWithConstantForms;

  StringRef InstNS;
  ImmPredicateSet ImmediatePredicates;

public:
  explicit FastISelMap(StringRef InstNS);

  void collectPatterns(CodeGenDAGPatterns &CGP);
  void printImmediatePredicates(raw_ostream &OS);
  void printFunctionDefinitions(raw_ostream &OS);

private:
  void emitInstructionCode(raw_ostream &OS, const OperandsSignature &Operands,
                           const PredMap &PM, const std::string &RetVTName);
};
} // End anonymous namespace

static std::string getOpcodeName(Record *Op, CodeGenDAGPatterns &CGP) {
  return std::string(CGP.getSDNodeInfo(Op).getEnumName());
}

static std::string getLegalCName(std::string OpName) {
  std::string::size_type pos = OpName.find("::");
  if (pos != std::string::npos)
    OpName.replace(pos, 2, "_");
  return OpName;
}

FastISelMap::FastISelMap(StringRef instns) : InstNS(instns) {}

static std::string PhyRegForNode(TreePatternNode &Op,
                                 const CodeGenTarget &Target) {
  std::string PhysReg;

  if (!Op.isLeaf())
    return PhysReg;

  Record *OpLeafRec = cast<DefInit>(Op.getLeafValue())->getDef();
  if (!OpLeafRec->isSubClassOf("Register"))
    return PhysReg;

  PhysReg += cast<StringInit>(OpLeafRec->getValue("Namespace")->getValue())
                 ->getValue();
  PhysReg += "::";
  PhysReg += Target.getRegBank().getReg(OpLeafRec)->getName();
  return PhysReg;
}

void FastISelMap::collectPatterns(CodeGenDAGPatterns &CGP) {
  const CodeGenTarget &Target = CGP.getTargetInfo();

  // Scan through all the patterns and record the simple ones.
  for (CodeGenDAGPatterns::ptm_iterator I = CGP.ptm_begin(), E = CGP.ptm_end();
       I != E; ++I) {
    const PatternToMatch &Pattern = *I;

    // For now, just look at Instructions, so that we don't have to worry
    // about emitting multiple instructions for a pattern.
    TreePatternNode &Dst = Pattern.getDstPattern();
    if (Dst.isLeaf())
      continue;
    Record *Op = Dst.getOperator();
    if (!Op->isSubClassOf("Instruction"))
      continue;
    CodeGenInstruction &II = CGP.getTargetInfo().getInstruction(Op);
    if (II.Operands.empty())
      continue;

    // Allow instructions to be marked as unavailable for FastISel for
    // certain cases, i.e. an ISA has two 'and' instruction which differ
    // by what registers they can use but are otherwise identical for
    // codegen purposes.
    if (II.FastISelShouldIgnore)
      continue;

    // For now, ignore multi-instruction patterns.
    bool MultiInsts = false;
    for (unsigned i = 0, e = Dst.getNumChildren(); i != e; ++i) {
      TreePatternNode &ChildOp = Dst.getChild(i);
      if (ChildOp.isLeaf())
        continue;
      if (ChildOp.getOperator()->isSubClassOf("Instruction")) {
        MultiInsts = true;
        break;
      }
    }
    if (MultiInsts)
      continue;

    // For now, ignore instructions where the first operand is not an
    // output register.
    const CodeGenRegisterClass *DstRC = nullptr;
    std::string SubRegNo;
    if (Op->getName() != "EXTRACT_SUBREG") {
      Record *Op0Rec = II.Operands[0].Rec;
      if (Op0Rec->isSubClassOf("RegisterOperand"))
        Op0Rec = Op0Rec->getValueAsDef("RegClass");
      if (!Op0Rec->isSubClassOf("RegisterClass"))
        continue;
      DstRC = &Target.getRegisterClass(Op0Rec);
      if (!DstRC)
        continue;
    } else {
      // If this isn't a leaf, then continue since the register classes are
      // a bit too complicated for now.
      if (!Dst.getChild(1).isLeaf())
        continue;

      DefInit *SR = dyn_cast<DefInit>(Dst.getChild(1).getLeafValue());
      if (SR)
        SubRegNo = getQualifiedName(SR->getDef());
      else
        SubRegNo = Dst.getChild(1).getLeafValue()->getAsString();
    }

    // Inspect the pattern.
    TreePatternNode &InstPatNode = Pattern.getSrcPattern();
    if (InstPatNode.isLeaf())
      continue;

    // Ignore multiple result nodes for now.
    if (InstPatNode.getNumTypes() > 1)
      continue;

    Record *InstPatOp = InstPatNode.getOperator();
    std::string OpcodeName = getOpcodeName(InstPatOp, CGP);
    MVT::SimpleValueType RetVT = MVT::isVoid;
    if (InstPatNode.getNumTypes())
      RetVT = InstPatNode.getSimpleType(0);
    MVT::SimpleValueType VT = RetVT;
    if (InstPatNode.getNumChildren()) {
      assert(InstPatNode.getChild(0).getNumTypes() == 1);
      VT = InstPatNode.getChild(0).getSimpleType(0);
    }

    // For now, filter out any instructions with predicates.
    if (!InstPatNode.getPredicateCalls().empty())
      continue;

    // Check all the operands.
    OperandsSignature Operands;
    if (!Operands.initialize(InstPatNode, Target, VT, ImmediatePredicates,
                             DstRC))
      continue;

    std::vector<std::string> PhysRegInputs;
    if (InstPatNode.getOperator()->getName() == "imm" ||
        InstPatNode.getOperator()->getName() == "fpimm")
      PhysRegInputs.push_back("");
    else {
      // Compute the PhysRegs used by the given pattern, and check that
      // the mapping from the src to dst patterns is simple.
      bool FoundNonSimplePattern = false;
      unsigned DstIndex = 0;
      for (unsigned i = 0, e = InstPatNode.getNumChildren(); i != e; ++i) {
        std::string PhysReg = PhyRegForNode(InstPatNode.getChild(i), Target);
        if (PhysReg.empty()) {
          if (DstIndex >= Dst.getNumChildren() ||
              Dst.getChild(DstIndex).getName() !=
                  InstPatNode.getChild(i).getName()) {
            FoundNonSimplePattern = true;
            break;
          }
          ++DstIndex;
        }

        PhysRegInputs.push_back(PhysReg);
      }

      if (Op->getName() != "EXTRACT_SUBREG" && DstIndex < Dst.getNumChildren())
        FoundNonSimplePattern = true;

      if (FoundNonSimplePattern)
        continue;
    }

    // Check if the operands match one of the patterns handled by FastISel.
    std::string ManglingSuffix;
    raw_string_ostream SuffixOS(ManglingSuffix);
    Operands.PrintManglingSuffix(SuffixOS, ImmediatePredicates, true);
    if (!StringSwitch<bool>(ManglingSuffix)
             .Cases("", "r", "rr", "ri", "i", "f", true)
             .Default(false))
      continue;

    // Get the predicate that guards this pattern.
    std::string PredicateCheck = Pattern.getPredicateCheck();

    // Ok, we found a pattern that we can handle. Remember it.
    InstructionMemo Memo(Pattern.getDstPattern().getOperator()->getName(),
                         DstRC, SubRegNo, PhysRegInputs, PredicateCheck);

    int complexity = Pattern.getPatternComplexity(CGP);

    auto inserted_simple_pattern = SimplePatternsCheck.insert(
        std::tuple(Operands, OpcodeName, VT, RetVT, PredicateCheck));
    if (!inserted_simple_pattern.second) {
      PrintFatalError(Pattern.getSrcRecord()->getLoc(),
                      "Duplicate predicate in FastISel table!");
    }

    // Note: Instructions with the same complexity will appear in the order
    // that they are encountered.
    SimplePatterns[Operands][OpcodeName][VT][RetVT].emplace(complexity,
                                                            std::move(Memo));

    // If any of the operands were immediates with predicates on them, strip
    // them down to a signature that doesn't have predicates so that we can
    // associate them with the stripped predicate version.
    if (Operands.hasAnyImmediateCodes()) {
      SignaturesWithConstantForms[Operands.getWithoutImmCodes()].push_back(
          Operands);
    }
  }
}

void FastISelMap::printImmediatePredicates(raw_ostream &OS) {
  if (ImmediatePredicates.begin() == ImmediatePredicates.end())
    return;

  OS << "\n// FastEmit Immediate Predicate functions.\n";
  for (auto ImmediatePredicate : ImmediatePredicates) {
    OS << "static bool " << ImmediatePredicate.getFnName()
       << "(int64_t Imm) {\n";
    OS << ImmediatePredicate.getImmediatePredicateCode() << "\n}\n";
  }

  OS << "\n\n";
}

void FastISelMap::emitInstructionCode(raw_ostream &OS,
                                      const OperandsSignature &Operands,
                                      const PredMap &PM,
                                      const std::string &RetVTName) {
  // Emit code for each possible instruction. There may be
  // multiple if there are subtarget concerns.  A reverse iterator
  // is used to produce the ones with highest complexity first.

  bool OneHadNoPredicate = false;
  for (PredMap::const_reverse_iterator PI = PM.rbegin(), PE = PM.rend();
       PI != PE; ++PI) {
    const InstructionMemo &Memo = PI->second;
    std::string PredicateCheck = Memo.PredicateCheck;

    if (PredicateCheck.empty()) {
      assert(!OneHadNoPredicate &&
             "Multiple instructions match and more than one had "
             "no predicate!");
      OneHadNoPredicate = true;
    } else {
      if (OneHadNoPredicate) {
        PrintFatalError("Multiple instructions match and one with no "
                        "predicate came before one with a predicate!  "
                        "name:" +
                        Memo.Name + "  predicate: " + PredicateCheck);
      }
      OS << "  if (" + PredicateCheck + ") {\n";
      OS << "  ";
    }

    for (unsigned i = 0; i < Memo.PhysRegs.size(); ++i) {
      if (Memo.PhysRegs[i] != "")
        OS << "  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, MIMD, "
           << "TII.get(TargetOpcode::COPY), " << Memo.PhysRegs[i]
           << ").addReg(Op" << i << ");\n";
    }

    OS << "  return fastEmitInst_";
    if (Memo.SubRegNo.empty()) {
      Operands.PrintManglingSuffix(OS, Memo.PhysRegs, ImmediatePredicates,
                                   true);
      OS << "(" << InstNS << "::" << Memo.Name << ", ";
      OS << "&" << InstNS << "::" << Memo.RC->getName() << "RegClass";
      if (!Operands.empty())
        OS << ", ";
      Operands.PrintArguments(OS, Memo.PhysRegs);
      OS << ");\n";
    } else {
      OS << "extractsubreg(" << RetVTName << ", Op0, " << Memo.SubRegNo
         << ");\n";
    }

    if (!PredicateCheck.empty()) {
      OS << "  }\n";
    }
  }
  // Return 0 if all of the possibilities had predicates but none
  // were satisfied.
  if (!OneHadNoPredicate)
    OS << "  return 0;\n";
  OS << "}\n";
  OS << "\n";
}

void FastISelMap::printFunctionDefinitions(raw_ostream &OS) {
  // Now emit code for all the patterns that we collected.
  for (const auto &SimplePattern : SimplePatterns) {
    const OperandsSignature &Operands = SimplePattern.first;
    const OpcodeTypeRetPredMap &OTM = SimplePattern.second;

    for (const auto &I : OTM) {
      const std::string &Opcode = I.first;
      const TypeRetPredMap &TM = I.second;

      OS << "// FastEmit functions for " << Opcode << ".\n";
      OS << "\n";

      // Emit one function for each opcode,type pair.
      for (const auto &TI : TM) {
        MVT::SimpleValueType VT = TI.first;
        const RetPredMap &RM = TI.second;
        if (RM.size() != 1) {
          for (const auto &RI : RM) {
            MVT::SimpleValueType RetVT = RI.first;
            const PredMap &PM = RI.second;

            OS << "unsigned fastEmit_" << getLegalCName(Opcode) << "_"
               << getLegalCName(std::string(getName(VT))) << "_"
               << getLegalCName(std::string(getName(RetVT))) << "_";
            Operands.PrintManglingSuffix(OS, ImmediatePredicates);
            OS << "(";
            Operands.PrintParameters(OS);
            OS << ") {\n";

            emitInstructionCode(OS, Operands, PM, std::string(getName(RetVT)));
          }

          // Emit one function for the type that demultiplexes on return type.
          OS << "unsigned fastEmit_" << getLegalCName(Opcode) << "_"
             << getLegalCName(std::string(getName(VT))) << "_";
          Operands.PrintManglingSuffix(OS, ImmediatePredicates);
          OS << "(MVT RetVT";
          if (!Operands.empty())
            OS << ", ";
          Operands.PrintParameters(OS);
          OS << ") {\nswitch (RetVT.SimpleTy) {\n";
          for (const auto &RI : RM) {
            MVT::SimpleValueType RetVT = RI.first;
            OS << "  case " << getName(RetVT) << ": return fastEmit_"
               << getLegalCName(Opcode) << "_"
               << getLegalCName(std::string(getName(VT))) << "_"
               << getLegalCName(std::string(getName(RetVT))) << "_";
            Operands.PrintManglingSuffix(OS, ImmediatePredicates);
            OS << "(";
            Operands.PrintArguments(OS);
            OS << ");\n";
          }
          OS << "  default: return 0;\n}\n}\n\n";

        } else {
          // Non-variadic return type.
          OS << "unsigned fastEmit_" << getLegalCName(Opcode) << "_"
             << getLegalCName(std::string(getName(VT))) << "_";
          Operands.PrintManglingSuffix(OS, ImmediatePredicates);
          OS << "(MVT RetVT";
          if (!Operands.empty())
            OS << ", ";
          Operands.PrintParameters(OS);
          OS << ") {\n";

          OS << "  if (RetVT.SimpleTy != " << getName(RM.begin()->first)
             << ")\n    return 0;\n";

          const PredMap &PM = RM.begin()->second;

          emitInstructionCode(OS, Operands, PM, "RetVT");
        }
      }

      // Emit one function for the opcode that demultiplexes based on the type.
      OS << "unsigned fastEmit_" << getLegalCName(Opcode) << "_";
      Operands.PrintManglingSuffix(OS, ImmediatePredicates);
      OS << "(MVT VT, MVT RetVT";
      if (!Operands.empty())
        OS << ", ";
      Operands.PrintParameters(OS);
      OS << ") {\n";
      OS << "  switch (VT.SimpleTy) {\n";
      for (const auto &TI : TM) {
        MVT::SimpleValueType VT = TI.first;
        std::string TypeName = std::string(getName(VT));
        OS << "  case " << TypeName << ": return fastEmit_"
           << getLegalCName(Opcode) << "_" << getLegalCName(TypeName) << "_";
        Operands.PrintManglingSuffix(OS, ImmediatePredicates);
        OS << "(RetVT";
        if (!Operands.empty())
          OS << ", ";
        Operands.PrintArguments(OS);
        OS << ");\n";
      }
      OS << "  default: return 0;\n";
      OS << "  }\n";
      OS << "}\n";
      OS << "\n";
    }

    OS << "// Top-level FastEmit function.\n";
    OS << "\n";

    // Emit one function for the operand signature that demultiplexes based
    // on opcode and type.
    OS << "unsigned fastEmit_";
    Operands.PrintManglingSuffix(OS, ImmediatePredicates);
    OS << "(MVT VT, MVT RetVT, unsigned Opcode";
    if (!Operands.empty())
      OS << ", ";
    Operands.PrintParameters(OS);
    OS << ") ";
    if (!Operands.hasAnyImmediateCodes())
      OS << "override ";
    OS << "{\n";

    // If there are any forms of this signature available that operate on
    // constrained forms of the immediate (e.g., 32-bit sext immediate in a
    // 64-bit operand), check them first.

    std::map<OperandsSignature, std::vector<OperandsSignature>>::iterator MI =
        SignaturesWithConstantForms.find(Operands);
    if (MI != SignaturesWithConstantForms.end()) {
      // Unique any duplicates out of the list.
      llvm::sort(MI->second);
      MI->second.erase(llvm::unique(MI->second), MI->second.end());

      // Check each in order it was seen.  It would be nice to have a good
      // relative ordering between them, but we're not going for optimality
      // here.
      for (unsigned i = 0, e = MI->second.size(); i != e; ++i) {
        OS << "  if (";
        MI->second[i].emitImmediatePredicate(OS, ImmediatePredicates);
        OS << ")\n    if (unsigned Reg = fastEmit_";
        MI->second[i].PrintManglingSuffix(OS, ImmediatePredicates);
        OS << "(VT, RetVT, Opcode";
        if (!MI->second[i].empty())
          OS << ", ";
        MI->second[i].PrintArguments(OS);
        OS << "))\n      return Reg;\n\n";
      }

      // Done with this, remove it.
      SignaturesWithConstantForms.erase(MI);
    }

    OS << "  switch (Opcode) {\n";
    for (const auto &I : OTM) {
      const std::string &Opcode = I.first;

      OS << "  case " << Opcode << ": return fastEmit_" << getLegalCName(Opcode)
         << "_";
      Operands.PrintManglingSuffix(OS, ImmediatePredicates);
      OS << "(VT, RetVT";
      if (!Operands.empty())
        OS << ", ";
      Operands.PrintArguments(OS);
      OS << ");\n";
    }
    OS << "  default: return 0;\n";
    OS << "  }\n";
    OS << "}\n";
    OS << "\n";
  }

  // TODO: SignaturesWithConstantForms should be empty here.
}

static void EmitFastISel(RecordKeeper &RK, raw_ostream &OS) {
  CodeGenDAGPatterns CGP(RK);
  const CodeGenTarget &Target = CGP.getTargetInfo();
  emitSourceFileHeader("\"Fast\" Instruction Selector for the " +
                           Target.getName().str() + " target",
                       OS);

  // Determine the target's namespace name.
  StringRef InstNS = Target.getInstNamespace();
  assert(!InstNS.empty() && "Can't determine target-specific namespace!");

  FastISelMap F(InstNS);
  F.collectPatterns(CGP);
  F.printImmediatePredicates(OS);
  F.printFunctionDefinitions(OS);
}

static TableGen::Emitter::Opt X("gen-fast-isel", EmitFastISel,
                                "Generate a \"fast\" instruction selector");
