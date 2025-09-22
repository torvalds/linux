//===- DAGISelMatcherGen.cpp - Matcher generator --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Basic/SDNodeProperties.h"
#include "Common/CodeGenDAGPatterns.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenRegisters.h"
#include "Common/CodeGenTarget.h"
#include "Common/DAGISelMatcher.h"
#include "Common/InfoByHwMode.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <utility>
using namespace llvm;

/// getRegisterValueType - Look up and return the ValueType of the specified
/// register. If the register is a member of multiple register classes, they
/// must all have the same type.
static MVT::SimpleValueType getRegisterValueType(Record *R,
                                                 const CodeGenTarget &T) {
  bool FoundRC = false;
  MVT::SimpleValueType VT = MVT::Other;
  const CodeGenRegister *Reg = T.getRegBank().getReg(R);

  for (const auto &RC : T.getRegBank().getRegClasses()) {
    if (!RC.contains(Reg))
      continue;

    if (!FoundRC) {
      FoundRC = true;
      const ValueTypeByHwMode &VVT = RC.getValueTypeNum(0);
      assert(VVT.isSimple());
      VT = VVT.getSimple().SimpleTy;
      continue;
    }

#ifndef NDEBUG
    // If this occurs in multiple register classes, they all have to agree.
    const ValueTypeByHwMode &VVT = RC.getValueTypeNum(0);
    assert(VVT.isSimple() && VVT.getSimple().SimpleTy == VT &&
           "ValueType mismatch between register classes for this register");
#endif
  }
  return VT;
}

namespace {
class MatcherGen {
  const PatternToMatch &Pattern;
  const CodeGenDAGPatterns &CGP;

  /// PatWithNoTypes - This is a clone of Pattern.getSrcPattern() that starts
  /// out with all of the types removed.  This allows us to insert type checks
  /// as we scan the tree.
  TreePatternNodePtr PatWithNoTypes;

  /// VariableMap - A map from variable names ('$dst') to the recorded operand
  /// number that they were captured as.  These are biased by 1 to make
  /// insertion easier.
  StringMap<unsigned> VariableMap;

  /// This maintains the recorded operand number that OPC_CheckComplexPattern
  /// drops each sub-operand into. We don't want to insert these into
  /// VariableMap because that leads to identity checking if they are
  /// encountered multiple times. Biased by 1 like VariableMap for
  /// consistency.
  StringMap<unsigned> NamedComplexPatternOperands;

  /// NextRecordedOperandNo - As we emit opcodes to record matched values in
  /// the RecordedNodes array, this keeps track of which slot will be next to
  /// record into.
  unsigned NextRecordedOperandNo;

  /// MatchedChainNodes - This maintains the position in the recorded nodes
  /// array of all of the recorded input nodes that have chains.
  SmallVector<unsigned, 2> MatchedChainNodes;

  /// MatchedComplexPatterns - This maintains a list of all of the
  /// ComplexPatterns that we need to check. The second element of each pair
  /// is the recorded operand number of the input node.
  SmallVector<std::pair<const TreePatternNode *, unsigned>, 2>
      MatchedComplexPatterns;

  /// PhysRegInputs - List list has an entry for each explicitly specified
  /// physreg input to the pattern.  The first elt is the Register node, the
  /// second is the recorded slot number the input pattern match saved it in.
  SmallVector<std::pair<Record *, unsigned>, 2> PhysRegInputs;

  /// Matcher - This is the top level of the generated matcher, the result.
  Matcher *TheMatcher;

  /// CurPredicate - As we emit matcher nodes, this points to the latest check
  /// which should have future checks stuck into its Next position.
  Matcher *CurPredicate;

public:
  MatcherGen(const PatternToMatch &pattern, const CodeGenDAGPatterns &cgp);

  bool EmitMatcherCode(unsigned Variant);
  void EmitResultCode();

  Matcher *GetMatcher() const { return TheMatcher; }

private:
  void AddMatcher(Matcher *NewNode);
  void InferPossibleTypes();

  // Matcher Generation.
  void EmitMatchCode(const TreePatternNode &N, TreePatternNode &NodeNoTypes);
  void EmitLeafMatchCode(const TreePatternNode &N);
  void EmitOperatorMatchCode(const TreePatternNode &N,
                             TreePatternNode &NodeNoTypes);

  /// If this is the first time a node with unique identifier Name has been
  /// seen, record it. Otherwise, emit a check to make sure this is the same
  /// node. Returns true if this is the first encounter.
  bool recordUniqueNode(ArrayRef<std::string> Names);

  // Result Code Generation.
  unsigned getNamedArgumentSlot(StringRef Name) {
    unsigned VarMapEntry = VariableMap[Name];
    assert(VarMapEntry != 0 &&
           "Variable referenced but not defined and not caught earlier!");
    return VarMapEntry - 1;
  }

  void EmitResultOperand(const TreePatternNode &N,
                         SmallVectorImpl<unsigned> &ResultOps);
  void EmitResultOfNamedOperand(const TreePatternNode &N,
                                SmallVectorImpl<unsigned> &ResultOps);
  void EmitResultLeafAsOperand(const TreePatternNode &N,
                               SmallVectorImpl<unsigned> &ResultOps);
  void EmitResultInstructionAsOperand(const TreePatternNode &N,
                                      SmallVectorImpl<unsigned> &ResultOps);
  void EmitResultSDNodeXFormAsOperand(const TreePatternNode &N,
                                      SmallVectorImpl<unsigned> &ResultOps);
};

} // end anonymous namespace

MatcherGen::MatcherGen(const PatternToMatch &pattern,
                       const CodeGenDAGPatterns &cgp)
    : Pattern(pattern), CGP(cgp), NextRecordedOperandNo(0), TheMatcher(nullptr),
      CurPredicate(nullptr) {
  // We need to produce the matcher tree for the patterns source pattern.  To
  // do this we need to match the structure as well as the types.  To do the
  // type matching, we want to figure out the fewest number of type checks we
  // need to emit.  For example, if there is only one integer type supported
  // by a target, there should be no type comparisons at all for integer
  // patterns!
  //
  // To figure out the fewest number of type checks needed, clone the pattern,
  // remove the types, then perform type inference on the pattern as a whole.
  // If there are unresolved types, emit an explicit check for those types,
  // apply the type to the tree, then rerun type inference.  Iterate until all
  // types are resolved.
  //
  PatWithNoTypes = Pattern.getSrcPattern().clone();
  PatWithNoTypes->RemoveAllTypes();

  // If there are types that are manifestly known, infer them.
  InferPossibleTypes();
}

/// InferPossibleTypes - As we emit the pattern, we end up generating type
/// checks and applying them to the 'PatWithNoTypes' tree.  As we do this, we
/// want to propagate implied types as far throughout the tree as possible so
/// that we avoid doing redundant type checks.  This does the type propagation.
void MatcherGen::InferPossibleTypes() {
  // TP - Get *SOME* tree pattern, we don't care which.  It is only used for
  // diagnostics, which we know are impossible at this point.
  TreePattern &TP = *CGP.pf_begin()->second;

  bool MadeChange = true;
  while (MadeChange)
    MadeChange = PatWithNoTypes->ApplyTypeConstraints(
        TP, true /*Ignore reg constraints*/);
}

/// AddMatcher - Add a matcher node to the current graph we're building.
void MatcherGen::AddMatcher(Matcher *NewNode) {
  if (CurPredicate)
    CurPredicate->setNext(NewNode);
  else
    TheMatcher = NewNode;
  CurPredicate = NewNode;
}

//===----------------------------------------------------------------------===//
// Pattern Match Generation
//===----------------------------------------------------------------------===//

/// EmitLeafMatchCode - Generate matching code for leaf nodes.
void MatcherGen::EmitLeafMatchCode(const TreePatternNode &N) {
  assert(N.isLeaf() && "Not a leaf?");

  // Direct match against an integer constant.
  if (IntInit *II = dyn_cast<IntInit>(N.getLeafValue())) {
    // If this is the root of the dag we're matching, we emit a redundant opcode
    // check to ensure that this gets folded into the normal top-level
    // OpcodeSwitch.
    if (&N == &Pattern.getSrcPattern()) {
      const SDNodeInfo &NI = CGP.getSDNodeInfo(CGP.getSDNodeNamed("imm"));
      AddMatcher(new CheckOpcodeMatcher(NI));
    }

    return AddMatcher(new CheckIntegerMatcher(II->getValue()));
  }

  // An UnsetInit represents a named node without any constraints.
  if (isa<UnsetInit>(N.getLeafValue())) {
    assert(N.hasName() && "Unnamed ? leaf");
    return;
  }

  DefInit *DI = dyn_cast<DefInit>(N.getLeafValue());
  if (!DI) {
    errs() << "Unknown leaf kind: " << N << "\n";
    abort();
  }

  Record *LeafRec = DI->getDef();

  // A ValueType leaf node can represent a register when named, or itself when
  // unnamed.
  if (LeafRec->isSubClassOf("ValueType")) {
    // A named ValueType leaf always matches: (add i32:$a, i32:$b).
    if (N.hasName())
      return;
    // An unnamed ValueType as in (sext_inreg GPR:$foo, i8).
    return AddMatcher(new CheckValueTypeMatcher(llvm::getValueType(LeafRec)));
  }

  if ( // Handle register references.  Nothing to do here, they always match.
      LeafRec->isSubClassOf("RegisterClass") ||
      LeafRec->isSubClassOf("RegisterOperand") ||
      LeafRec->isSubClassOf("PointerLikeRegClass") ||
      LeafRec->isSubClassOf("SubRegIndex") ||
      // Place holder for SRCVALUE nodes. Nothing to do here.
      LeafRec->getName() == "srcvalue")
    return;

  // If we have a physreg reference like (mul gpr:$src, EAX) then we need to
  // record the register
  if (LeafRec->isSubClassOf("Register")) {
    AddMatcher(new RecordMatcher("physreg input " + LeafRec->getName().str(),
                                 NextRecordedOperandNo));
    PhysRegInputs.push_back(std::pair(LeafRec, NextRecordedOperandNo++));
    return;
  }

  if (LeafRec->isSubClassOf("CondCode"))
    return AddMatcher(new CheckCondCodeMatcher(LeafRec->getName()));

  if (LeafRec->isSubClassOf("ComplexPattern")) {
    // We can't model ComplexPattern uses that don't have their name taken yet.
    // The OPC_CheckComplexPattern operation implicitly records the results.
    if (N.getName().empty()) {
      std::string S;
      raw_string_ostream OS(S);
      OS << "We expect complex pattern uses to have names: " << N;
      PrintFatalError(S);
    }

    // Remember this ComplexPattern so that we can emit it after all the other
    // structural matches are done.
    unsigned InputOperand = VariableMap[N.getName()] - 1;
    MatchedComplexPatterns.push_back(std::pair(&N, InputOperand));
    return;
  }

  if (LeafRec->getName() == "immAllOnesV" ||
      LeafRec->getName() == "immAllZerosV") {
    // If this is the root of the dag we're matching, we emit a redundant opcode
    // check to ensure that this gets folded into the normal top-level
    // OpcodeSwitch.
    if (&N == &Pattern.getSrcPattern()) {
      MVT VT = N.getSimpleType(0);
      StringRef Name = VT.isScalableVector() ? "splat_vector" : "build_vector";
      const SDNodeInfo &NI = CGP.getSDNodeInfo(CGP.getSDNodeNamed(Name));
      AddMatcher(new CheckOpcodeMatcher(NI));
    }
    if (LeafRec->getName() == "immAllOnesV")
      AddMatcher(new CheckImmAllOnesVMatcher());
    else
      AddMatcher(new CheckImmAllZerosVMatcher());
    return;
  }

  errs() << "Unknown leaf kind: " << N << "\n";
  abort();
}

void MatcherGen::EmitOperatorMatchCode(const TreePatternNode &N,
                                       TreePatternNode &NodeNoTypes) {
  assert(!N.isLeaf() && "Not an operator?");

  if (N.getOperator()->isSubClassOf("ComplexPattern")) {
    // The "name" of a non-leaf complex pattern (MY_PAT $op1, $op2) is
    // "MY_PAT:op1:op2". We should already have validated that the uses are
    // consistent.
    std::string PatternName = std::string(N.getOperator()->getName());
    for (unsigned i = 0; i < N.getNumChildren(); ++i) {
      PatternName += ":";
      PatternName += N.getChild(i).getName();
    }

    if (recordUniqueNode(PatternName)) {
      auto NodeAndOpNum = std::pair(&N, NextRecordedOperandNo - 1);
      MatchedComplexPatterns.push_back(NodeAndOpNum);
    }

    return;
  }

  const SDNodeInfo &CInfo = CGP.getSDNodeInfo(N.getOperator());

  // If this is an 'and R, 1234' where the operation is AND/OR and the RHS is
  // a constant without a predicate fn that has more than one bit set, handle
  // this as a special case.  This is usually for targets that have special
  // handling of certain large constants (e.g. alpha with it's 8/16/32-bit
  // handling stuff).  Using these instructions is often far more efficient
  // than materializing the constant.  Unfortunately, both the instcombiner
  // and the dag combiner can often infer that bits are dead, and thus drop
  // them from the mask in the dag.  For example, it might turn 'AND X, 255'
  // into 'AND X, 254' if it knows the low bit is set.  Emit code that checks
  // to handle this.
  if ((N.getOperator()->getName() == "and" ||
       N.getOperator()->getName() == "or") &&
      N.getChild(1).isLeaf() && N.getChild(1).getPredicateCalls().empty() &&
      N.getPredicateCalls().empty()) {
    if (IntInit *II = dyn_cast<IntInit>(N.getChild(1).getLeafValue())) {
      if (!llvm::has_single_bit<uint32_t>(
              II->getValue())) { // Don't bother with single bits.
        // If this is at the root of the pattern, we emit a redundant
        // CheckOpcode so that the following checks get factored properly under
        // a single opcode check.
        if (&N == &Pattern.getSrcPattern())
          AddMatcher(new CheckOpcodeMatcher(CInfo));

        // Emit the CheckAndImm/CheckOrImm node.
        if (N.getOperator()->getName() == "and")
          AddMatcher(new CheckAndImmMatcher(II->getValue()));
        else
          AddMatcher(new CheckOrImmMatcher(II->getValue()));

        // Match the LHS of the AND as appropriate.
        AddMatcher(new MoveChildMatcher(0));
        EmitMatchCode(N.getChild(0), NodeNoTypes.getChild(0));
        AddMatcher(new MoveParentMatcher());
        return;
      }
    }
  }

  // Check that the current opcode lines up.
  AddMatcher(new CheckOpcodeMatcher(CInfo));

  // If this node has memory references (i.e. is a load or store), tell the
  // interpreter to capture them in the memref array.
  if (N.NodeHasProperty(SDNPMemOperand, CGP))
    AddMatcher(new RecordMemRefMatcher());

  // If this node has a chain, then the chain is operand #0 is the SDNode, and
  // the child numbers of the node are all offset by one.
  unsigned OpNo = 0;
  if (N.NodeHasProperty(SDNPHasChain, CGP)) {
    // Record the node and remember it in our chained nodes list.
    AddMatcher(new RecordMatcher("'" + N.getOperator()->getName().str() +
                                     "' chained node",
                                 NextRecordedOperandNo));
    // Remember all of the input chains our pattern will match.
    MatchedChainNodes.push_back(NextRecordedOperandNo++);

    // Don't look at the input chain when matching the tree pattern to the
    // SDNode.
    OpNo = 1;

    // If this node is not the root and the subtree underneath it produces a
    // chain, then the result of matching the node is also produce a chain.
    // Beyond that, this means that we're also folding (at least) the root node
    // into the node that produce the chain (for example, matching
    // "(add reg, (load ptr))" as a add_with_memory on X86).  This is
    // problematic, if the 'reg' node also uses the load (say, its chain).
    // Graphically:
    //
    //         [LD]
    //         ^  ^
    //         |  \                              DAG's like cheese.
    //        /    |
    //       /    [YY]
    //       |     ^
    //      [XX]--/
    //
    // It would be invalid to fold XX and LD.  In this case, folding the two
    // nodes together would induce a cycle in the DAG, making it a 'cyclic DAG'
    // To prevent this, we emit a dynamic check for legality before allowing
    // this to be folded.
    //
    const TreePatternNode &Root = Pattern.getSrcPattern();
    if (&N != &Root) { // Not the root of the pattern.
      // If there is a node between the root and this node, then we definitely
      // need to emit the check.
      bool NeedCheck = !Root.hasChild(&N);

      // If it *is* an immediate child of the root, we can still need a check if
      // the root SDNode has multiple inputs.  For us, this means that it is an
      // intrinsic, has multiple operands, or has other inputs like chain or
      // glue).
      if (!NeedCheck) {
        const SDNodeInfo &PInfo = CGP.getSDNodeInfo(Root.getOperator());
        NeedCheck =
            Root.getOperator() == CGP.get_intrinsic_void_sdnode() ||
            Root.getOperator() == CGP.get_intrinsic_w_chain_sdnode() ||
            Root.getOperator() == CGP.get_intrinsic_wo_chain_sdnode() ||
            PInfo.getNumOperands() > 1 || PInfo.hasProperty(SDNPHasChain) ||
            PInfo.hasProperty(SDNPInGlue) || PInfo.hasProperty(SDNPOptInGlue);
      }

      if (NeedCheck)
        AddMatcher(new CheckFoldableChainNodeMatcher());
    }
  }

  // If this node has an output glue and isn't the root, remember it.
  if (N.NodeHasProperty(SDNPOutGlue, CGP) && &N != &Pattern.getSrcPattern()) {
    // TODO: This redundantly records nodes with both glues and chains.

    // Record the node and remember it in our chained nodes list.
    AddMatcher(new RecordMatcher("'" + N.getOperator()->getName().str() +
                                     "' glue output node",
                                 NextRecordedOperandNo));
  }

  // If this node is known to have an input glue or if it *might* have an input
  // glue, capture it as the glue input of the pattern.
  if (N.NodeHasProperty(SDNPOptInGlue, CGP) ||
      N.NodeHasProperty(SDNPInGlue, CGP))
    AddMatcher(new CaptureGlueInputMatcher());

  for (unsigned i = 0, e = N.getNumChildren(); i != e; ++i, ++OpNo) {
    // Get the code suitable for matching this child.  Move to the child, check
    // it then move back to the parent.
    AddMatcher(new MoveChildMatcher(OpNo));
    EmitMatchCode(N.getChild(i), NodeNoTypes.getChild(i));
    AddMatcher(new MoveParentMatcher());
  }
}

bool MatcherGen::recordUniqueNode(ArrayRef<std::string> Names) {
  unsigned Entry = 0;
  for (const std::string &Name : Names) {
    unsigned &VarMapEntry = VariableMap[Name];
    if (!Entry)
      Entry = VarMapEntry;
    assert(Entry == VarMapEntry);
  }

  bool NewRecord = false;
  if (Entry == 0) {
    // If it is a named node, we must emit a 'Record' opcode.
    std::string WhatFor;
    for (const std::string &Name : Names) {
      if (!WhatFor.empty())
        WhatFor += ',';
      WhatFor += "$" + Name;
    }
    AddMatcher(new RecordMatcher(WhatFor, NextRecordedOperandNo));
    Entry = ++NextRecordedOperandNo;
    NewRecord = true;
  } else {
    // If we get here, this is a second reference to a specific name.  Since
    // we already have checked that the first reference is valid, we don't
    // have to recursively match it, just check that it's the same as the
    // previously named thing.
    AddMatcher(new CheckSameMatcher(Entry - 1));
  }

  for (const std::string &Name : Names)
    VariableMap[Name] = Entry;

  return NewRecord;
}

void MatcherGen::EmitMatchCode(const TreePatternNode &N,
                               TreePatternNode &NodeNoTypes) {
  // If N and NodeNoTypes don't agree on a type, then this is a case where we
  // need to do a type check.  Emit the check, apply the type to NodeNoTypes and
  // reinfer any correlated types.
  SmallVector<unsigned, 2> ResultsToTypeCheck;

  for (unsigned i = 0, e = NodeNoTypes.getNumTypes(); i != e; ++i) {
    if (NodeNoTypes.getExtType(i) == N.getExtType(i))
      continue;
    NodeNoTypes.setType(i, N.getExtType(i));
    InferPossibleTypes();
    ResultsToTypeCheck.push_back(i);
  }

  // If this node has a name associated with it, capture it in VariableMap. If
  // we already saw this in the pattern, emit code to verify dagness.
  SmallVector<std::string, 4> Names;
  if (!N.getName().empty())
    Names.push_back(N.getName());

  for (const ScopedName &Name : N.getNamesAsPredicateArg()) {
    Names.push_back(
        ("pred:" + Twine(Name.getScope()) + ":" + Name.getIdentifier()).str());
  }

  if (!Names.empty()) {
    if (!recordUniqueNode(Names))
      return;
  }

  if (N.isLeaf())
    EmitLeafMatchCode(N);
  else
    EmitOperatorMatchCode(N, NodeNoTypes);

  // If there are node predicates for this node, generate their checks.
  for (unsigned i = 0, e = N.getPredicateCalls().size(); i != e; ++i) {
    const TreePredicateCall &Pred = N.getPredicateCalls()[i];
    SmallVector<unsigned, 4> Operands;
    if (Pred.Fn.usesOperands()) {
      TreePattern *TP = Pred.Fn.getOrigPatFragRecord();
      for (unsigned i = 0; i < TP->getNumArgs(); ++i) {
        std::string Name =
            ("pred:" + Twine(Pred.Scope) + ":" + TP->getArgName(i)).str();
        Operands.push_back(getNamedArgumentSlot(Name));
      }
    }
    AddMatcher(new CheckPredicateMatcher(Pred.Fn, Operands));
  }

  for (unsigned i = 0, e = ResultsToTypeCheck.size(); i != e; ++i)
    AddMatcher(new CheckTypeMatcher(N.getSimpleType(ResultsToTypeCheck[i]),
                                    ResultsToTypeCheck[i]));
}

/// EmitMatcherCode - Generate the code that matches the predicate of this
/// pattern for the specified Variant.  If the variant is invalid this returns
/// true and does not generate code, if it is valid, it returns false.
bool MatcherGen::EmitMatcherCode(unsigned Variant) {
  // If the root of the pattern is a ComplexPattern and if it is specified to
  // match some number of root opcodes, these are considered to be our variants.
  // Depending on which variant we're generating code for, emit the root opcode
  // check.
  if (const ComplexPattern *CP =
          Pattern.getSrcPattern().getComplexPatternInfo(CGP)) {
    const std::vector<Record *> &OpNodes = CP->getRootNodes();
    assert(!OpNodes.empty() &&
           "Complex Pattern must specify what it can match");
    if (Variant >= OpNodes.size())
      return true;

    AddMatcher(new CheckOpcodeMatcher(CGP.getSDNodeInfo(OpNodes[Variant])));
  } else {
    if (Variant != 0)
      return true;
  }

  // Emit the matcher for the pattern structure and types.
  EmitMatchCode(Pattern.getSrcPattern(), *PatWithNoTypes);

  // If the pattern has a predicate on it (e.g. only enabled when a subtarget
  // feature is around, do the check).
  std::string PredicateCheck = Pattern.getPredicateCheck();
  if (!PredicateCheck.empty())
    AddMatcher(new CheckPatternPredicateMatcher(PredicateCheck));

  // Now that we've completed the structural type match, emit any ComplexPattern
  // checks (e.g. addrmode matches).  We emit this after the structural match
  // because they are generally more expensive to evaluate and more difficult to
  // factor.
  for (unsigned i = 0, e = MatchedComplexPatterns.size(); i != e; ++i) {
    auto &N = *MatchedComplexPatterns[i].first;

    // Remember where the results of this match get stuck.
    if (N.isLeaf()) {
      NamedComplexPatternOperands[N.getName()] = NextRecordedOperandNo + 1;
    } else {
      unsigned CurOp = NextRecordedOperandNo;
      for (unsigned i = 0; i < N.getNumChildren(); ++i) {
        NamedComplexPatternOperands[N.getChild(i).getName()] = CurOp + 1;
        CurOp += N.getChild(i).getNumMIResults(CGP);
      }
    }

    // Get the slot we recorded the value in from the name on the node.
    unsigned RecNodeEntry = MatchedComplexPatterns[i].second;

    const ComplexPattern *CP = N.getComplexPatternInfo(CGP);
    assert(CP && "Not a valid ComplexPattern!");

    // Emit a CheckComplexPat operation, which does the match (aborting if it
    // fails) and pushes the matched operands onto the recorded nodes list.
    AddMatcher(new CheckComplexPatMatcher(*CP, RecNodeEntry, N.getName(),
                                          NextRecordedOperandNo));

    // Record the right number of operands.
    NextRecordedOperandNo += CP->getNumOperands();
    if (CP->hasProperty(SDNPHasChain)) {
      // If the complex pattern has a chain, then we need to keep track of the
      // fact that we just recorded a chain input.  The chain input will be
      // matched as the last operand of the predicate if it was successful.
      ++NextRecordedOperandNo; // Chained node operand.

      // It is the last operand recorded.
      assert(NextRecordedOperandNo > 1 &&
             "Should have recorded input/result chains at least!");
      MatchedChainNodes.push_back(NextRecordedOperandNo - 1);
    }

    // TODO: Complex patterns can't have output glues, if they did, we'd want
    // to record them.
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Node Result Generation
//===----------------------------------------------------------------------===//

void MatcherGen::EmitResultOfNamedOperand(
    const TreePatternNode &N, SmallVectorImpl<unsigned> &ResultOps) {
  assert(!N.getName().empty() && "Operand not named!");

  if (unsigned SlotNo = NamedComplexPatternOperands[N.getName()]) {
    // Complex operands have already been completely selected, just find the
    // right slot ant add the arguments directly.
    for (unsigned i = 0; i < N.getNumMIResults(CGP); ++i)
      ResultOps.push_back(SlotNo - 1 + i);

    return;
  }

  unsigned SlotNo = getNamedArgumentSlot(N.getName());

  // If this is an 'imm' or 'fpimm' node, make sure to convert it to the target
  // version of the immediate so that it doesn't get selected due to some other
  // node use.
  if (!N.isLeaf()) {
    StringRef OperatorName = N.getOperator()->getName();
    if (OperatorName == "imm" || OperatorName == "fpimm") {
      AddMatcher(new EmitConvertToTargetMatcher(SlotNo));
      ResultOps.push_back(NextRecordedOperandNo++);
      return;
    }
  }

  for (unsigned i = 0; i < N.getNumMIResults(CGP); ++i)
    ResultOps.push_back(SlotNo + i);
}

void MatcherGen::EmitResultLeafAsOperand(const TreePatternNode &N,
                                         SmallVectorImpl<unsigned> &ResultOps) {
  assert(N.isLeaf() && "Must be a leaf");

  if (IntInit *II = dyn_cast<IntInit>(N.getLeafValue())) {
    AddMatcher(new EmitIntegerMatcher(II->getValue(), N.getSimpleType(0)));
    ResultOps.push_back(NextRecordedOperandNo++);
    return;
  }

  // If this is an explicit register reference, handle it.
  if (DefInit *DI = dyn_cast<DefInit>(N.getLeafValue())) {
    Record *Def = DI->getDef();
    if (Def->isSubClassOf("Register")) {
      const CodeGenRegister *Reg = CGP.getTargetInfo().getRegBank().getReg(Def);
      AddMatcher(new EmitRegisterMatcher(Reg, N.getSimpleType(0)));
      ResultOps.push_back(NextRecordedOperandNo++);
      return;
    }

    if (Def->getName() == "zero_reg") {
      AddMatcher(new EmitRegisterMatcher(nullptr, N.getSimpleType(0)));
      ResultOps.push_back(NextRecordedOperandNo++);
      return;
    }

    if (Def->getName() == "undef_tied_input") {
      MVT::SimpleValueType ResultVT = N.getSimpleType(0);
      auto IDOperandNo = NextRecordedOperandNo++;
      Record *ImpDef = Def->getRecords().getDef("IMPLICIT_DEF");
      CodeGenInstruction &II = CGP.getTargetInfo().getInstruction(ImpDef);
      AddMatcher(new EmitNodeMatcher(II, ResultVT, std::nullopt, false, false,
                                     false, false, -1, IDOperandNo));
      ResultOps.push_back(IDOperandNo);
      return;
    }

    // Handle a reference to a register class. This is used
    // in COPY_TO_SUBREG instructions.
    if (Def->isSubClassOf("RegisterOperand"))
      Def = Def->getValueAsDef("RegClass");
    if (Def->isSubClassOf("RegisterClass")) {
      // If the register class has an enum integer value greater than 127, the
      // encoding overflows the limit of 7 bits, which precludes the use of
      // StringIntegerMatcher. In this case, fallback to using IntegerMatcher.
      const CodeGenRegisterClass &RC =
          CGP.getTargetInfo().getRegisterClass(Def);
      if (RC.EnumValue <= 127) {
        std::string Value = RC.getQualifiedIdName();
        AddMatcher(new EmitStringIntegerMatcher(Value, MVT::i32));
        ResultOps.push_back(NextRecordedOperandNo++);
      } else {
        AddMatcher(new EmitIntegerMatcher(RC.EnumValue, MVT::i32));
        ResultOps.push_back(NextRecordedOperandNo++);
      }
      return;
    }

    // Handle a subregister index. This is used for INSERT_SUBREG etc.
    if (Def->isSubClassOf("SubRegIndex")) {
      const CodeGenRegBank &RB = CGP.getTargetInfo().getRegBank();
      // If we have more than 127 subreg indices the encoding can overflow
      // 7 bit and we cannot use StringInteger.
      if (RB.getSubRegIndices().size() > 127) {
        const CodeGenSubRegIndex *I = RB.findSubRegIdx(Def);
        assert(I && "Cannot find subreg index by name!");
        if (I->EnumValue > 127) {
          AddMatcher(new EmitIntegerMatcher(I->EnumValue, MVT::i32));
          ResultOps.push_back(NextRecordedOperandNo++);
          return;
        }
      }
      std::string Value = getQualifiedName(Def);
      AddMatcher(new EmitStringIntegerMatcher(Value, MVT::i32));
      ResultOps.push_back(NextRecordedOperandNo++);
      return;
    }
  }

  errs() << "unhandled leaf node:\n";
  N.dump();
}

static bool mayInstNodeLoadOrStore(const TreePatternNode &N,
                                   const CodeGenDAGPatterns &CGP) {
  Record *Op = N.getOperator();
  const CodeGenTarget &CGT = CGP.getTargetInfo();
  CodeGenInstruction &II = CGT.getInstruction(Op);
  return II.mayLoad || II.mayStore;
}

static unsigned numNodesThatMayLoadOrStore(const TreePatternNode &N,
                                           const CodeGenDAGPatterns &CGP) {
  if (N.isLeaf())
    return 0;

  Record *OpRec = N.getOperator();
  if (!OpRec->isSubClassOf("Instruction"))
    return 0;

  unsigned Count = 0;
  if (mayInstNodeLoadOrStore(N, CGP))
    ++Count;

  for (unsigned i = 0, e = N.getNumChildren(); i != e; ++i)
    Count += numNodesThatMayLoadOrStore(N.getChild(i), CGP);

  return Count;
}

void MatcherGen::EmitResultInstructionAsOperand(
    const TreePatternNode &N, SmallVectorImpl<unsigned> &OutputOps) {
  Record *Op = N.getOperator();
  const CodeGenTarget &CGT = CGP.getTargetInfo();
  CodeGenInstruction &II = CGT.getInstruction(Op);
  const DAGInstruction &Inst = CGP.getInstruction(Op);

  bool isRoot = &N == &Pattern.getDstPattern();

  // TreeHasOutGlue - True if this tree has glue.
  bool TreeHasInGlue = false, TreeHasOutGlue = false;
  if (isRoot) {
    const TreePatternNode &SrcPat = Pattern.getSrcPattern();
    TreeHasInGlue = SrcPat.TreeHasProperty(SDNPOptInGlue, CGP) ||
                    SrcPat.TreeHasProperty(SDNPInGlue, CGP);

    // FIXME2: this is checking the entire pattern, not just the node in
    // question, doing this just for the root seems like a total hack.
    TreeHasOutGlue = SrcPat.TreeHasProperty(SDNPOutGlue, CGP);
  }

  // NumResults - This is the number of results produced by the instruction in
  // the "outs" list.
  unsigned NumResults = Inst.getNumResults();

  // Number of operands we know the output instruction must have. If it is
  // variadic, we could have more operands.
  unsigned NumFixedOperands = II.Operands.size();

  SmallVector<unsigned, 8> InstOps;

  // Loop over all of the fixed operands of the instruction pattern, emitting
  // code to fill them all in. The node 'N' usually has number children equal to
  // the number of input operands of the instruction.  However, in cases where
  // there are predicate operands for an instruction, we need to fill in the
  // 'execute always' values. Match up the node operands to the instruction
  // operands to do this.
  unsigned ChildNo = 0;

  // Similarly to the code in TreePatternNode::ApplyTypeConstraints, count the
  // number of operands at the end of the list which have default values.
  // Those can come from the pattern if it provides enough arguments, or be
  // filled in with the default if the pattern hasn't provided them. But any
  // operand with a default value _before_ the last mandatory one will be
  // filled in with their defaults unconditionally.
  unsigned NonOverridableOperands = NumFixedOperands;
  while (NonOverridableOperands > NumResults &&
         CGP.operandHasDefault(II.Operands[NonOverridableOperands - 1].Rec))
    --NonOverridableOperands;

  for (unsigned InstOpNo = NumResults, e = NumFixedOperands; InstOpNo != e;
       ++InstOpNo) {
    // Determine what to emit for this operand.
    Record *OperandNode = II.Operands[InstOpNo].Rec;
    if (CGP.operandHasDefault(OperandNode) &&
        (InstOpNo < NonOverridableOperands || ChildNo >= N.getNumChildren())) {
      // This is a predicate or optional def operand which the pattern has not
      // overridden, or which we aren't letting it override; emit the 'default
      // ops' operands.
      const DAGDefaultOperand &DefaultOp = CGP.getDefaultOperand(OperandNode);
      for (unsigned i = 0, e = DefaultOp.DefaultOps.size(); i != e; ++i)
        EmitResultOperand(*DefaultOp.DefaultOps[i], InstOps);
      continue;
    }

    // Otherwise this is a normal operand or a predicate operand without
    // 'execute always'; emit it.

    // For operands with multiple sub-operands we may need to emit
    // multiple child patterns to cover them all.  However, ComplexPattern
    // children may themselves emit multiple MI operands.
    unsigned NumSubOps = 1;
    if (OperandNode->isSubClassOf("Operand")) {
      DagInit *MIOpInfo = OperandNode->getValueAsDag("MIOperandInfo");
      if (unsigned NumArgs = MIOpInfo->getNumArgs())
        NumSubOps = NumArgs;
    }

    unsigned FinalNumOps = InstOps.size() + NumSubOps;
    while (InstOps.size() < FinalNumOps) {
      const TreePatternNode &Child = N.getChild(ChildNo);
      unsigned BeforeAddingNumOps = InstOps.size();
      EmitResultOperand(Child, InstOps);
      assert(InstOps.size() > BeforeAddingNumOps && "Didn't add any operands");

      // If the operand is an instruction and it produced multiple results, just
      // take the first one.
      if (!Child.isLeaf() && Child.getOperator()->isSubClassOf("Instruction"))
        InstOps.resize(BeforeAddingNumOps + 1);

      ++ChildNo;
    }
  }

  // If this is a variadic output instruction (i.e. REG_SEQUENCE), we can't
  // expand suboperands, use default operands, or other features determined from
  // the CodeGenInstruction after the fixed operands, which were handled
  // above. Emit the remaining instructions implicitly added by the use for
  // variable_ops.
  if (II.Operands.isVariadic) {
    for (unsigned I = ChildNo, E = N.getNumChildren(); I < E; ++I)
      EmitResultOperand(N.getChild(I), InstOps);
  }

  // If this node has input glue or explicitly specified input physregs, we
  // need to add chained and glued copyfromreg nodes and materialize the glue
  // input.
  if (isRoot && !PhysRegInputs.empty()) {
    // Emit all of the CopyToReg nodes for the input physical registers.  These
    // occur in patterns like (mul:i8 AL:i8, GR8:i8:$src).
    for (unsigned i = 0, e = PhysRegInputs.size(); i != e; ++i) {
      const CodeGenRegister *Reg =
          CGP.getTargetInfo().getRegBank().getReg(PhysRegInputs[i].first);
      AddMatcher(new EmitCopyToRegMatcher(PhysRegInputs[i].second, Reg));
    }

    // Even if the node has no other glue inputs, the resultant node must be
    // glued to the CopyFromReg nodes we just generated.
    TreeHasInGlue = true;
  }

  // Result order: node results, chain, glue

  // Determine the result types.
  SmallVector<MVT::SimpleValueType, 4> ResultVTs;
  for (unsigned i = 0, e = N.getNumTypes(); i != e; ++i)
    ResultVTs.push_back(N.getSimpleType(i));

  // If this is the root instruction of a pattern that has physical registers in
  // its result pattern, add output VTs for them.  For example, X86 has:
  //   (set AL, (mul ...))
  // This also handles implicit results like:
  //   (implicit EFLAGS)
  if (isRoot && !Pattern.getDstRegs().empty()) {
    // If the root came from an implicit def in the instruction handling stuff,
    // don't re-add it.
    Record *HandledReg = nullptr;
    if (II.HasOneImplicitDefWithKnownVT(CGT) != MVT::Other)
      HandledReg = II.ImplicitDefs[0];

    for (Record *Reg : Pattern.getDstRegs()) {
      if (!Reg->isSubClassOf("Register") || Reg == HandledReg)
        continue;
      ResultVTs.push_back(getRegisterValueType(Reg, CGT));
    }
  }

  // If this is the root of the pattern and the pattern we're matching includes
  // a node that is variadic, mark the generated node as variadic so that it
  // gets the excess operands from the input DAG.
  int NumFixedArityOperands = -1;
  if (isRoot && Pattern.getSrcPattern().NodeHasProperty(SDNPVariadic, CGP))
    NumFixedArityOperands = Pattern.getSrcPattern().getNumChildren();

  // If this is the root node and multiple matched nodes in the input pattern
  // have MemRefs in them, have the interpreter collect them and plop them onto
  // this node. If there is just one node with MemRefs, leave them on that node
  // even if it is not the root.
  //
  // FIXME3: This is actively incorrect for result patterns with multiple
  // memory-referencing instructions.
  bool PatternHasMemOperands =
      Pattern.getSrcPattern().TreeHasProperty(SDNPMemOperand, CGP);

  bool NodeHasMemRefs = false;
  if (PatternHasMemOperands) {
    unsigned NumNodesThatLoadOrStore =
        numNodesThatMayLoadOrStore(Pattern.getDstPattern(), CGP);
    bool NodeIsUniqueLoadOrStore =
        mayInstNodeLoadOrStore(N, CGP) && NumNodesThatLoadOrStore == 1;
    NodeHasMemRefs =
        NodeIsUniqueLoadOrStore || (isRoot && (mayInstNodeLoadOrStore(N, CGP) ||
                                               NumNodesThatLoadOrStore != 1));
  }

  // Determine whether we need to attach a chain to this node.
  bool NodeHasChain = false;
  if (Pattern.getSrcPattern().TreeHasProperty(SDNPHasChain, CGP)) {
    // For some instructions, we were able to infer from the pattern whether
    // they should have a chain.  Otherwise, attach the chain to the root.
    //
    // FIXME2: This is extremely dubious for several reasons, not the least of
    // which it gives special status to instructions with patterns that Pat<>
    // nodes can't duplicate.
    if (II.hasChain_Inferred)
      NodeHasChain = II.hasChain;
    else
      NodeHasChain = isRoot;
    // Instructions which load and store from memory should have a chain,
    // regardless of whether they happen to have a pattern saying so.
    if (II.hasCtrlDep || II.mayLoad || II.mayStore || II.canFoldAsLoad ||
        II.hasSideEffects)
      NodeHasChain = true;
  }

  assert((!ResultVTs.empty() || TreeHasOutGlue || NodeHasChain) &&
         "Node has no result");

  AddMatcher(new EmitNodeMatcher(II, ResultVTs, InstOps, NodeHasChain,
                                 TreeHasInGlue, TreeHasOutGlue, NodeHasMemRefs,
                                 NumFixedArityOperands, NextRecordedOperandNo));

  // The non-chain and non-glue results of the newly emitted node get recorded.
  for (unsigned i = 0, e = ResultVTs.size(); i != e; ++i) {
    if (ResultVTs[i] == MVT::Other || ResultVTs[i] == MVT::Glue)
      break;
    OutputOps.push_back(NextRecordedOperandNo++);
  }
}

void MatcherGen::EmitResultSDNodeXFormAsOperand(
    const TreePatternNode &N, SmallVectorImpl<unsigned> &ResultOps) {
  assert(N.getOperator()->isSubClassOf("SDNodeXForm") && "Not SDNodeXForm?");

  // Emit the operand.
  SmallVector<unsigned, 8> InputOps;

  // FIXME2: Could easily generalize this to support multiple inputs and outputs
  // to the SDNodeXForm.  For now we just support one input and one output like
  // the old instruction selector.
  assert(N.getNumChildren() == 1);
  EmitResultOperand(N.getChild(0), InputOps);

  // The input currently must have produced exactly one result.
  assert(InputOps.size() == 1 && "Unexpected input to SDNodeXForm");

  AddMatcher(new EmitNodeXFormMatcher(InputOps[0], N.getOperator()));
  ResultOps.push_back(NextRecordedOperandNo++);
}

void MatcherGen::EmitResultOperand(const TreePatternNode &N,
                                   SmallVectorImpl<unsigned> &ResultOps) {
  // This is something selected from the pattern we matched.
  if (!N.getName().empty())
    return EmitResultOfNamedOperand(N, ResultOps);

  if (N.isLeaf())
    return EmitResultLeafAsOperand(N, ResultOps);

  Record *OpRec = N.getOperator();
  if (OpRec->isSubClassOf("Instruction"))
    return EmitResultInstructionAsOperand(N, ResultOps);
  if (OpRec->isSubClassOf("SDNodeXForm"))
    return EmitResultSDNodeXFormAsOperand(N, ResultOps);
  errs() << "Unknown result node to emit code for: " << N << '\n';
  PrintFatalError("Unknown node in result pattern!");
}

void MatcherGen::EmitResultCode() {
  // Patterns that match nodes with (potentially multiple) chain inputs have to
  // merge them together into a token factor.  This informs the generated code
  // what all the chained nodes are.
  if (!MatchedChainNodes.empty())
    AddMatcher(new EmitMergeInputChainsMatcher(MatchedChainNodes));

  // Codegen the root of the result pattern, capturing the resulting values.
  SmallVector<unsigned, 8> Ops;
  EmitResultOperand(Pattern.getDstPattern(), Ops);

  // At this point, we have however many values the result pattern produces.
  // However, the input pattern might not need all of these.  If there are
  // excess values at the end (such as implicit defs of condition codes etc)
  // just lop them off.  This doesn't need to worry about glue or chains, just
  // explicit results.
  //
  unsigned NumSrcResults = Pattern.getSrcPattern().getNumTypes();

  // If the pattern also has (implicit) results, count them as well.
  if (!Pattern.getDstRegs().empty()) {
    // If the root came from an implicit def in the instruction handling stuff,
    // don't re-add it.
    Record *HandledReg = nullptr;
    const TreePatternNode &DstPat = Pattern.getDstPattern();
    if (!DstPat.isLeaf() && DstPat.getOperator()->isSubClassOf("Instruction")) {
      const CodeGenTarget &CGT = CGP.getTargetInfo();
      CodeGenInstruction &II = CGT.getInstruction(DstPat.getOperator());

      if (II.HasOneImplicitDefWithKnownVT(CGT) != MVT::Other)
        HandledReg = II.ImplicitDefs[0];
    }

    for (Record *Reg : Pattern.getDstRegs()) {
      if (!Reg->isSubClassOf("Register") || Reg == HandledReg)
        continue;
      ++NumSrcResults;
    }
  }

  SmallVector<unsigned, 8> Results(Ops);

  // Apply result permutation.
  for (unsigned ResNo = 0; ResNo < Pattern.getDstPattern().getNumResults();
       ++ResNo) {
    Results[ResNo] = Ops[Pattern.getDstPattern().getResultIndex(ResNo)];
  }

  Results.resize(NumSrcResults);
  AddMatcher(new CompleteMatchMatcher(Results, Pattern));
}

/// ConvertPatternToMatcher - Create the matcher for the specified pattern with
/// the specified variant.  If the variant number is invalid, this returns null.
Matcher *llvm::ConvertPatternToMatcher(const PatternToMatch &Pattern,
                                       unsigned Variant,
                                       const CodeGenDAGPatterns &CGP) {
  MatcherGen Gen(Pattern, CGP);

  // Generate the code for the matcher.
  if (Gen.EmitMatcherCode(Variant))
    return nullptr;

  // FIXME2: Kill extra MoveParent commands at the end of the matcher sequence.
  // FIXME2: Split result code out to another table, and make the matcher end
  // with an "Emit <index>" command.  This allows result generation stuff to be
  // shared and factored?

  // If the match succeeds, then we generate Pattern.
  Gen.EmitResultCode();

  // Unconditional match.
  return Gen.GetMatcher();
}
