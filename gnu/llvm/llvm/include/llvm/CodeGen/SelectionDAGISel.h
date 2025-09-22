//===-- llvm/CodeGen/SelectionDAGISel.h - Common Base Class------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SelectionDAGISel class, which is used as the common
// base class for SelectionDAG-based instruction selectors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGISEL_H
#define LLVM_CODEGEN_SELECTIONDAGISEL_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/BasicBlock.h"
#include <memory>

namespace llvm {
class AAResults;
class AssumptionCache;
class TargetInstrInfo;
class TargetMachine;
class SSPLayoutInfo;
class SelectionDAGBuilder;
class SDValue;
class MachineRegisterInfo;
class MachineFunction;
class OptimizationRemarkEmitter;
class TargetLowering;
class TargetLibraryInfo;
class TargetTransformInfo;
class FunctionLoweringInfo;
class SwiftErrorValueTracking;
class GCFunctionInfo;
class ScheduleDAGSDNodes;

/// SelectionDAGISel - This is the common base class used for SelectionDAG-based
/// pattern-matching instruction selectors.
class SelectionDAGISel {
public:
  TargetMachine &TM;
  const TargetLibraryInfo *LibInfo;
  std::unique_ptr<FunctionLoweringInfo> FuncInfo;
  SwiftErrorValueTracking *SwiftError;
  MachineFunction *MF;
  MachineRegisterInfo *RegInfo;
  SelectionDAG *CurDAG;
  std::unique_ptr<SelectionDAGBuilder> SDB;
  AAResults *AA = nullptr;
  AssumptionCache *AC = nullptr;
  GCFunctionInfo *GFI = nullptr;
  SSPLayoutInfo *SP = nullptr;
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  TargetTransformInfo *TTI = nullptr;
#endif
  CodeGenOptLevel OptLevel;
  const TargetInstrInfo *TII;
  const TargetLowering *TLI;
  bool FastISelFailed;
  SmallPtrSet<const Instruction *, 4> ElidedArgCopyInstrs;

  /// Current optimization remark emitter.
  /// Used to report things like combines and FastISel failures.
  std::unique_ptr<OptimizationRemarkEmitter> ORE;

  /// True if the function currently processing is in the function printing list
  /// (i.e. `-filter-print-funcs`).
  /// This is primarily used by ISEL_DUMP, which spans in multiple member
  /// functions. Storing the filter result here so that we only need to do the
  /// filtering once.
  bool MatchFilterFuncName = false;
  StringRef FuncName;

  explicit SelectionDAGISel(TargetMachine &tm,
                            CodeGenOptLevel OL = CodeGenOptLevel::Default);
  virtual ~SelectionDAGISel();

  const TargetLowering *getTargetLowering() const { return TLI; }

  void initializeAnalysisResults(MachineFunctionAnalysisManager &MFAM);
  void initializeAnalysisResults(MachineFunctionPass &MFP);

  virtual bool runOnMachineFunction(MachineFunction &mf);

  virtual void emitFunctionEntryCode() {}

  /// PreprocessISelDAG - This hook allows targets to hack on the graph before
  /// instruction selection starts.
  virtual void PreprocessISelDAG() {}

  /// PostprocessISelDAG() - This hook allows the target to hack on the graph
  /// right after selection.
  virtual void PostprocessISelDAG() {}

  /// Main hook for targets to transform nodes into machine nodes.
  virtual void Select(SDNode *N) = 0;

  /// SelectInlineAsmMemoryOperand - Select the specified address as a target
  /// addressing mode, according to the specified constraint.  If this does
  /// not match or is not implemented, return true.  The resultant operands
  /// (which will appear in the machine instruction) should be added to the
  /// OutOps vector.
  virtual bool
  SelectInlineAsmMemoryOperand(const SDValue &Op,
                               InlineAsm::ConstraintCode ConstraintID,
                               std::vector<SDValue> &OutOps) {
    return true;
  }

  /// IsProfitableToFold - Returns true if it's profitable to fold the specific
  /// operand node N of U during instruction selection that starts at Root.
  virtual bool IsProfitableToFold(SDValue N, SDNode *U, SDNode *Root) const;

  /// IsLegalToFold - Returns true if the specific operand node N of
  /// U can be folded during instruction selection that starts at Root.
  /// FIXME: This is a static member function because the MSP430/X86
  /// targets, which uses it during isel.  This could become a proper member.
  static bool IsLegalToFold(SDValue N, SDNode *U, SDNode *Root,
                            CodeGenOptLevel OptLevel,
                            bool IgnoreChains = false);

  static void InvalidateNodeId(SDNode *N);
  static int getUninvalidatedNodeId(SDNode *N);

  static void EnforceNodeIdInvariant(SDNode *N);

  // Opcodes used by the DAG state machine:
  enum BuiltinOpcodes {
    OPC_Scope,
    OPC_RecordNode,
    OPC_RecordChild0,
    OPC_RecordChild1,
    OPC_RecordChild2,
    OPC_RecordChild3,
    OPC_RecordChild4,
    OPC_RecordChild5,
    OPC_RecordChild6,
    OPC_RecordChild7,
    OPC_RecordMemRef,
    OPC_CaptureGlueInput,
    OPC_MoveChild,
    OPC_MoveChild0,
    OPC_MoveChild1,
    OPC_MoveChild2,
    OPC_MoveChild3,
    OPC_MoveChild4,
    OPC_MoveChild5,
    OPC_MoveChild6,
    OPC_MoveChild7,
    OPC_MoveSibling,
    OPC_MoveSibling0,
    OPC_MoveSibling1,
    OPC_MoveSibling2,
    OPC_MoveSibling3,
    OPC_MoveSibling4,
    OPC_MoveSibling5,
    OPC_MoveSibling6,
    OPC_MoveSibling7,
    OPC_MoveParent,
    OPC_CheckSame,
    OPC_CheckChild0Same,
    OPC_CheckChild1Same,
    OPC_CheckChild2Same,
    OPC_CheckChild3Same,
    OPC_CheckPatternPredicate,
    OPC_CheckPatternPredicate0,
    OPC_CheckPatternPredicate1,
    OPC_CheckPatternPredicate2,
    OPC_CheckPatternPredicate3,
    OPC_CheckPatternPredicate4,
    OPC_CheckPatternPredicate5,
    OPC_CheckPatternPredicate6,
    OPC_CheckPatternPredicate7,
    OPC_CheckPatternPredicateTwoByte,
    OPC_CheckPredicate,
    OPC_CheckPredicate0,
    OPC_CheckPredicate1,
    OPC_CheckPredicate2,
    OPC_CheckPredicate3,
    OPC_CheckPredicate4,
    OPC_CheckPredicate5,
    OPC_CheckPredicate6,
    OPC_CheckPredicate7,
    OPC_CheckPredicateWithOperands,
    OPC_CheckOpcode,
    OPC_SwitchOpcode,
    OPC_CheckType,
    // Space-optimized forms that implicitly encode VT.
    OPC_CheckTypeI32,
    OPC_CheckTypeI64,
    OPC_CheckTypeRes,
    OPC_SwitchType,
    OPC_CheckChild0Type,
    OPC_CheckChild1Type,
    OPC_CheckChild2Type,
    OPC_CheckChild3Type,
    OPC_CheckChild4Type,
    OPC_CheckChild5Type,
    OPC_CheckChild6Type,
    OPC_CheckChild7Type,

    OPC_CheckChild0TypeI32,
    OPC_CheckChild1TypeI32,
    OPC_CheckChild2TypeI32,
    OPC_CheckChild3TypeI32,
    OPC_CheckChild4TypeI32,
    OPC_CheckChild5TypeI32,
    OPC_CheckChild6TypeI32,
    OPC_CheckChild7TypeI32,

    OPC_CheckChild0TypeI64,
    OPC_CheckChild1TypeI64,
    OPC_CheckChild2TypeI64,
    OPC_CheckChild3TypeI64,
    OPC_CheckChild4TypeI64,
    OPC_CheckChild5TypeI64,
    OPC_CheckChild6TypeI64,
    OPC_CheckChild7TypeI64,

    OPC_CheckInteger,
    OPC_CheckChild0Integer,
    OPC_CheckChild1Integer,
    OPC_CheckChild2Integer,
    OPC_CheckChild3Integer,
    OPC_CheckChild4Integer,
    OPC_CheckCondCode,
    OPC_CheckChild2CondCode,
    OPC_CheckValueType,
    OPC_CheckComplexPat,
    OPC_CheckComplexPat0,
    OPC_CheckComplexPat1,
    OPC_CheckComplexPat2,
    OPC_CheckComplexPat3,
    OPC_CheckComplexPat4,
    OPC_CheckComplexPat5,
    OPC_CheckComplexPat6,
    OPC_CheckComplexPat7,
    OPC_CheckAndImm,
    OPC_CheckOrImm,
    OPC_CheckImmAllOnesV,
    OPC_CheckImmAllZerosV,
    OPC_CheckFoldableChainNode,

    OPC_EmitInteger,
    // Space-optimized forms that implicitly encode integer VT.
    OPC_EmitInteger8,
    OPC_EmitInteger16,
    OPC_EmitInteger32,
    OPC_EmitInteger64,
    OPC_EmitStringInteger,
    // Space-optimized forms that implicitly encode integer VT.
    OPC_EmitStringInteger32,
    OPC_EmitRegister,
    OPC_EmitRegisterI32,
    OPC_EmitRegisterI64,
    OPC_EmitRegister2,
    OPC_EmitConvertToTarget,
    OPC_EmitConvertToTarget0,
    OPC_EmitConvertToTarget1,
    OPC_EmitConvertToTarget2,
    OPC_EmitConvertToTarget3,
    OPC_EmitConvertToTarget4,
    OPC_EmitConvertToTarget5,
    OPC_EmitConvertToTarget6,
    OPC_EmitConvertToTarget7,
    OPC_EmitMergeInputChains,
    OPC_EmitMergeInputChains1_0,
    OPC_EmitMergeInputChains1_1,
    OPC_EmitMergeInputChains1_2,
    OPC_EmitCopyToReg,
    OPC_EmitCopyToReg0,
    OPC_EmitCopyToReg1,
    OPC_EmitCopyToReg2,
    OPC_EmitCopyToReg3,
    OPC_EmitCopyToReg4,
    OPC_EmitCopyToReg5,
    OPC_EmitCopyToReg6,
    OPC_EmitCopyToReg7,
    OPC_EmitCopyToRegTwoByte,
    OPC_EmitNodeXForm,
    OPC_EmitNode,
    // Space-optimized forms that implicitly encode number of result VTs.
    OPC_EmitNode0,
    OPC_EmitNode1,
    OPC_EmitNode2,
    // Space-optimized forms that implicitly encode EmitNodeInfo.
    OPC_EmitNode0None,
    OPC_EmitNode1None,
    OPC_EmitNode2None,
    OPC_EmitNode0Chain,
    OPC_EmitNode1Chain,
    OPC_EmitNode2Chain,
    OPC_MorphNodeTo,
    // Space-optimized forms that implicitly encode number of result VTs.
    OPC_MorphNodeTo0,
    OPC_MorphNodeTo1,
    OPC_MorphNodeTo2,
    // Space-optimized forms that implicitly encode EmitNodeInfo.
    OPC_MorphNodeTo0None,
    OPC_MorphNodeTo1None,
    OPC_MorphNodeTo2None,
    OPC_MorphNodeTo0Chain,
    OPC_MorphNodeTo1Chain,
    OPC_MorphNodeTo2Chain,
    OPC_MorphNodeTo0GlueInput,
    OPC_MorphNodeTo1GlueInput,
    OPC_MorphNodeTo2GlueInput,
    OPC_MorphNodeTo0GlueOutput,
    OPC_MorphNodeTo1GlueOutput,
    OPC_MorphNodeTo2GlueOutput,
    OPC_CompleteMatch,
    // Contains offset in table for pattern being selected
    OPC_Coverage
  };

  enum {
    OPFL_None       = 0,  // Node has no chain or glue input and isn't variadic.
    OPFL_Chain      = 1,     // Node has a chain input.
    OPFL_GlueInput  = 2,     // Node has a glue input.
    OPFL_GlueOutput = 4,     // Node has a glue output.
    OPFL_MemRefs    = 8,     // Node gets accumulated MemRefs.
    OPFL_Variadic0  = 1<<4,  // Node is variadic, root has 0 fixed inputs.
    OPFL_Variadic1  = 2<<4,  // Node is variadic, root has 1 fixed inputs.
    OPFL_Variadic2  = 3<<4,  // Node is variadic, root has 2 fixed inputs.
    OPFL_Variadic3  = 4<<4,  // Node is variadic, root has 3 fixed inputs.
    OPFL_Variadic4  = 5<<4,  // Node is variadic, root has 4 fixed inputs.
    OPFL_Variadic5  = 6<<4,  // Node is variadic, root has 5 fixed inputs.
    OPFL_Variadic6  = 7<<4,  // Node is variadic, root has 6 fixed inputs.

    OPFL_VariadicInfo = OPFL_Variadic6
  };

  /// getNumFixedFromVariadicInfo - Transform an EmitNode flags word into the
  /// number of fixed arity values that should be skipped when copying from the
  /// root.
  static inline int getNumFixedFromVariadicInfo(unsigned Flags) {
    return ((Flags&OPFL_VariadicInfo) >> 4)-1;
  }


protected:
  /// DAGSize - Size of DAG being instruction selected.
  ///
  unsigned DAGSize = 0;

  /// ReplaceUses - replace all uses of the old node F with the use
  /// of the new node T.
  void ReplaceUses(SDValue F, SDValue T) {
    CurDAG->ReplaceAllUsesOfValueWith(F, T);
    EnforceNodeIdInvariant(T.getNode());
  }

  /// ReplaceUses - replace all uses of the old nodes F with the use
  /// of the new nodes T.
  void ReplaceUses(const SDValue *F, const SDValue *T, unsigned Num) {
    CurDAG->ReplaceAllUsesOfValuesWith(F, T, Num);
    for (unsigned i = 0; i < Num; ++i)
      EnforceNodeIdInvariant(T[i].getNode());
  }

  /// ReplaceUses - replace all uses of the old node F with the use
  /// of the new node T.
  void ReplaceUses(SDNode *F, SDNode *T) {
    CurDAG->ReplaceAllUsesWith(F, T);
    EnforceNodeIdInvariant(T);
  }

  /// Replace all uses of \c F with \c T, then remove \c F from the DAG.
  void ReplaceNode(SDNode *F, SDNode *T) {
    CurDAG->ReplaceAllUsesWith(F, T);
    EnforceNodeIdInvariant(T);
    CurDAG->RemoveDeadNode(F);
  }

  /// SelectInlineAsmMemoryOperands - Calls to this are automatically generated
  /// by tblgen.  Others should not call it.
  void SelectInlineAsmMemoryOperands(std::vector<SDValue> &Ops,
                                     const SDLoc &DL);

  /// getPatternForIndex - Patterns selected by tablegen during ISEL
  virtual StringRef getPatternForIndex(unsigned index) {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// getIncludePathForIndex - get the td source location of pattern instantiation
  virtual StringRef getIncludePathForIndex(unsigned index) {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  bool shouldOptForSize(const MachineFunction *MF) const {
    return CurDAG->shouldOptForSize();
  }

public:
  // Calls to these predicates are generated by tblgen.
  bool CheckAndMask(SDValue LHS, ConstantSDNode *RHS,
                    int64_t DesiredMaskS) const;
  bool CheckOrMask(SDValue LHS, ConstantSDNode *RHS,
                    int64_t DesiredMaskS) const;


  /// CheckPatternPredicate - This function is generated by tblgen in the
  /// target.  It runs the specified pattern predicate and returns true if it
  /// succeeds or false if it fails.  The number is a private implementation
  /// detail to the code tblgen produces.
  virtual bool CheckPatternPredicate(unsigned PredNo) const {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// CheckNodePredicate - This function is generated by tblgen in the target.
  /// It runs node predicate number PredNo and returns true if it succeeds or
  /// false if it fails.  The number is a private implementation
  /// detail to the code tblgen produces.
  virtual bool CheckNodePredicate(SDNode *N, unsigned PredNo) const {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// CheckNodePredicateWithOperands - This function is generated by tblgen in
  /// the target.
  /// It runs node predicate number PredNo and returns true if it succeeds or
  /// false if it fails.  The number is a private implementation detail to the
  /// code tblgen produces.
  virtual bool CheckNodePredicateWithOperands(
      SDNode *N, unsigned PredNo,
      const SmallVectorImpl<SDValue> &Operands) const {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  virtual bool CheckComplexPattern(SDNode *Root, SDNode *Parent, SDValue N,
                                   unsigned PatternNo,
                        SmallVectorImpl<std::pair<SDValue, SDNode*> > &Result) {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  virtual SDValue RunSDNodeXForm(SDValue V, unsigned XFormNo) {
    llvm_unreachable("Tblgen should generate this!");
  }

  void SelectCodeCommon(SDNode *NodeToMatch, const unsigned char *MatcherTable,
                        unsigned TableSize);

  /// Return true if complex patterns for this target can mutate the
  /// DAG.
  virtual bool ComplexPatternFuncMutatesDAG() const {
    return false;
  }

  /// Return whether the node may raise an FP exception.
  bool mayRaiseFPException(SDNode *Node) const;

  bool isOrEquivalentToAdd(const SDNode *N) const;

private:

  // Calls to these functions are generated by tblgen.
  void Select_INLINEASM(SDNode *N);
  void Select_READ_REGISTER(SDNode *Op);
  void Select_WRITE_REGISTER(SDNode *Op);
  void Select_UNDEF(SDNode *N);
  void CannotYetSelect(SDNode *N);

  void Select_FREEZE(SDNode *N);
  void Select_ARITH_FENCE(SDNode *N);
  void Select_MEMBARRIER(SDNode *N);

  void Select_CONVERGENCECTRL_ANCHOR(SDNode *N);
  void Select_CONVERGENCECTRL_ENTRY(SDNode *N);
  void Select_CONVERGENCECTRL_LOOP(SDNode *N);

  void pushStackMapLiveVariable(SmallVectorImpl<SDValue> &Ops, SDValue Operand,
                                SDLoc DL);
  void Select_STACKMAP(SDNode *N);
  void Select_PATCHPOINT(SDNode *N);

  void Select_JUMP_TABLE_DEBUG_INFO(SDNode *N);

private:
  void DoInstructionSelection();
  SDNode *MorphNode(SDNode *Node, unsigned TargetOpc, SDVTList VTList,
                    ArrayRef<SDValue> Ops, unsigned EmitNodeInfo);

  /// Prepares the landing pad to take incoming values or do other EH
  /// personality specific tasks. Returns true if the block should be
  /// instruction selected, false if no code should be emitted for it.
  bool PrepareEHLandingPad();

  // Mark and Report IPToState for each Block under AsynchEH
  void reportIPToStateForBlocks(MachineFunction *Fn);

  /// Perform instruction selection on all basic blocks in the function.
  void SelectAllBasicBlocks(const Function &Fn);

  /// Perform instruction selection on a single basic block, for
  /// instructions between \p Begin and \p End.  \p HadTailCall will be set
  /// to true if a call in the block was translated as a tail call.
  void SelectBasicBlock(BasicBlock::const_iterator Begin,
                        BasicBlock::const_iterator End,
                        bool &HadTailCall);
  void FinishBasicBlock();

  void CodeGenAndEmitDAG();

  /// Generate instructions for lowering the incoming arguments of the
  /// given function.
  void LowerArguments(const Function &F);

  void ComputeLiveOutVRegInfo();

  /// Create the scheduler. If a specific scheduler was specified
  /// via the SchedulerRegistry, use it, otherwise select the
  /// one preferred by the target.
  ///
  ScheduleDAGSDNodes *CreateScheduler();

  /// OpcodeOffset - This is a cache used to dispatch efficiently into isel
  /// state machines that start with a OPC_SwitchOpcode node.
  std::vector<unsigned> OpcodeOffset;

  void UpdateChains(SDNode *NodeToMatch, SDValue InputChain,
                    SmallVectorImpl<SDNode *> &ChainNodesMatched,
                    bool isMorphNodeTo);
};

class SelectionDAGISelLegacy : public MachineFunctionPass {
  std::unique_ptr<SelectionDAGISel> Selector;

public:
  SelectionDAGISelLegacy(char &ID, std::unique_ptr<SelectionDAGISel> S);

  ~SelectionDAGISelLegacy() override = default;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction &MF) override;
};

class SelectionDAGISelPass : public PassInfoMixin<SelectionDAGISelPass> {
  std::unique_ptr<SelectionDAGISel> Selector;

protected:
  SelectionDAGISelPass(std::unique_ptr<SelectionDAGISel> Selector)
      : Selector(std::move(Selector)) {}

public:
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};
}

#endif /* LLVM_CODEGEN_SELECTIONDAGISEL_H */
