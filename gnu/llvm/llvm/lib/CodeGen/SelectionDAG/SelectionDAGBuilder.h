//===- SelectionDAGBuilder.h - Selection-DAG building -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements routines for translating from LLVM IR into SelectionDAG IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SELECTIONDAG_SELECTIONDAGBUILDER_H
#define LLVM_LIB_CODEGEN_SELECTIONDAG_SELECTIONDAGBUILDER_H

#include "StatepointLowering.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/AssignmentTrackingAnalysis.h"
#include "llvm/CodeGen/CodeGenCommonISel.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/SwitchLoweringUtils.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace llvm {

class AAResults;
class AllocaInst;
class AtomicCmpXchgInst;
class AtomicRMWInst;
class AssumptionCache;
class BasicBlock;
class BranchInst;
class CallInst;
class CallBrInst;
class CatchPadInst;
class CatchReturnInst;
class CatchSwitchInst;
class CleanupPadInst;
class CleanupReturnInst;
class Constant;
class ConstrainedFPIntrinsic;
class DbgValueInst;
class DataLayout;
class DIExpression;
class DILocalVariable;
class DILocation;
class FenceInst;
class FunctionLoweringInfo;
class GCFunctionInfo;
class GCRelocateInst;
class GCResultInst;
class GCStatepointInst;
class IndirectBrInst;
class InvokeInst;
class LandingPadInst;
class LLVMContext;
class LoadInst;
class MachineBasicBlock;
class PHINode;
class ResumeInst;
class ReturnInst;
class SDDbgValue;
class SelectionDAG;
class StoreInst;
class SwiftErrorValueTracking;
class SwitchInst;
class TargetLibraryInfo;
class TargetMachine;
class Type;
class VAArgInst;
class UnreachableInst;
class Use;
class User;
class Value;

//===----------------------------------------------------------------------===//
/// SelectionDAGBuilder - This is the common target-independent lowering
/// implementation that is parameterized by a TargetLowering object.
///
class SelectionDAGBuilder {
  /// The current instruction being visited.
  const Instruction *CurInst = nullptr;

  DenseMap<const Value*, SDValue> NodeMap;

  /// Maps argument value for unused arguments. This is used
  /// to preserve debug information for incoming arguments.
  DenseMap<const Value*, SDValue> UnusedArgNodeMap;

  /// Helper type for DanglingDebugInfoMap.
  class DanglingDebugInfo {
    unsigned SDNodeOrder = 0;

  public:
    DILocalVariable *Variable;
    DIExpression *Expression;
    DebugLoc dl;
    DanglingDebugInfo() = default;
    DanglingDebugInfo(DILocalVariable *Var, DIExpression *Expr, DebugLoc DL,
                      unsigned SDNO)
        : SDNodeOrder(SDNO), Variable(Var), Expression(Expr),
          dl(std::move(DL)) {}

    DILocalVariable *getVariable() const { return Variable; }
    DIExpression *getExpression() const { return Expression; }
    DebugLoc getDebugLoc() const { return dl; }
    unsigned getSDNodeOrder() const { return SDNodeOrder; }

    /// Helper for printing DanglingDebugInfo. This hoop-jumping is to
    /// store a Value pointer, so that we can print a whole DDI as one object.
    /// Call SelectionDAGBuilder::printDDI instead of using directly.
    struct Print {
      Print(const Value *V, const DanglingDebugInfo &DDI) : V(V), DDI(DDI) {}
      const Value *V;
      const DanglingDebugInfo &DDI;
      friend raw_ostream &operator<<(raw_ostream &OS,
                                     const DanglingDebugInfo::Print &P) {
        OS << "DDI(var=" << *P.DDI.getVariable();
        if (P.V)
          OS << ", val=" << *P.V;
        else
          OS << ", val=nullptr";

        OS << ", expr=" << *P.DDI.getExpression()
           << ", order=" << P.DDI.getSDNodeOrder()
           << ", loc=" << P.DDI.getDebugLoc() << ")";
        return OS;
      }
    };
  };

  /// Returns an object that defines `raw_ostream &operator<<` for printing.
  /// Usage example:
  ////    errs() << printDDI(MyDanglingInfo) << " is dangling\n";
  DanglingDebugInfo::Print printDDI(const Value *V,
                                    const DanglingDebugInfo &DDI) {
    return DanglingDebugInfo::Print(V, DDI);
  }

  /// Helper type for DanglingDebugInfoMap.
  typedef std::vector<DanglingDebugInfo> DanglingDebugInfoVector;

  /// Keeps track of dbg_values for which we have not yet seen the referent.
  /// We defer handling these until we do see it.
  MapVector<const Value*, DanglingDebugInfoVector> DanglingDebugInfoMap;

  /// Cache the module flag for whether we should use debug-info assignment
  /// tracking.
  bool AssignmentTrackingEnabled = false;

public:
  /// Loads are not emitted to the program immediately.  We bunch them up and
  /// then emit token factor nodes when possible.  This allows us to get simple
  /// disambiguation between loads without worrying about alias analysis.
  SmallVector<SDValue, 8> PendingLoads;

  /// State used while lowering a statepoint sequence (gc_statepoint,
  /// gc_relocate, and gc_result).  See StatepointLowering.hpp/cpp for details.
  StatepointLoweringState StatepointLowering;

private:
  /// CopyToReg nodes that copy values to virtual registers for export to other
  /// blocks need to be emitted before any terminator instruction, but they have
  /// no other ordering requirements. We bunch them up and the emit a single
  /// tokenfactor for them just before terminator instructions.
  SmallVector<SDValue, 8> PendingExports;

  /// Similar to loads, nodes corresponding to constrained FP intrinsics are
  /// bunched up and emitted when necessary.  These can be moved across each
  /// other and any (normal) memory operation (load or store), but not across
  /// calls or instructions having unspecified side effects.  As a special
  /// case, constrained FP intrinsics using fpexcept.strict may not be deleted
  /// even if otherwise unused, so they need to be chained before any
  /// terminator instruction (like PendingExports).  We track the latter
  /// set of nodes in a separate list.
  SmallVector<SDValue, 8> PendingConstrainedFP;
  SmallVector<SDValue, 8> PendingConstrainedFPStrict;

  /// Update root to include all chains from the Pending list.
  SDValue updateRoot(SmallVectorImpl<SDValue> &Pending);

  /// A unique monotonically increasing number used to order the SDNodes we
  /// create.
  unsigned SDNodeOrder;

  /// Emit comparison and split W into two subtrees.
  void splitWorkItem(SwitchCG::SwitchWorkList &WorkList,
                     const SwitchCG::SwitchWorkListItem &W, Value *Cond,
                     MachineBasicBlock *SwitchMBB);

  /// Lower W.
  void lowerWorkItem(SwitchCG::SwitchWorkListItem W, Value *Cond,
                     MachineBasicBlock *SwitchMBB,
                     MachineBasicBlock *DefaultMBB);

  /// Peel the top probability case if it exceeds the threshold
  MachineBasicBlock *
  peelDominantCaseCluster(const SwitchInst &SI,
                          SwitchCG::CaseClusterVector &Clusters,
                          BranchProbability &PeeledCaseProb);

private:
  const TargetMachine &TM;

public:
  /// Lowest valid SDNodeOrder. The special case 0 is reserved for scheduling
  /// nodes without a corresponding SDNode.
  static const unsigned LowestSDNodeOrder = 1;

  SelectionDAG &DAG;
  AAResults *AA = nullptr;
  AssumptionCache *AC = nullptr;
  const TargetLibraryInfo *LibInfo = nullptr;

  class SDAGSwitchLowering : public SwitchCG::SwitchLowering {
  public:
    SDAGSwitchLowering(SelectionDAGBuilder *sdb, FunctionLoweringInfo &funcinfo)
        : SwitchCG::SwitchLowering(funcinfo), SDB(sdb) {}

    void addSuccessorWithProb(
        MachineBasicBlock *Src, MachineBasicBlock *Dst,
        BranchProbability Prob = BranchProbability::getUnknown()) override {
      SDB->addSuccessorWithProb(Src, Dst, Prob);
    }

  private:
    SelectionDAGBuilder *SDB = nullptr;
  };

  // Data related to deferred switch lowerings. Used to construct additional
  // Basic Blocks in SelectionDAGISel::FinishBasicBlock.
  std::unique_ptr<SDAGSwitchLowering> SL;

  /// A StackProtectorDescriptor structure used to communicate stack protector
  /// information in between SelectBasicBlock and FinishBasicBlock.
  StackProtectorDescriptor SPDescriptor;

  // Emit PHI-node-operand constants only once even if used by multiple
  // PHI nodes.
  DenseMap<const Constant *, unsigned> ConstantsOut;

  /// Information about the function as a whole.
  FunctionLoweringInfo &FuncInfo;

  /// Information about the swifterror values used throughout the function.
  SwiftErrorValueTracking &SwiftError;

  /// Garbage collection metadata for the function.
  GCFunctionInfo *GFI = nullptr;

  /// Map a landing pad to the call site indexes.
  DenseMap<MachineBasicBlock *, SmallVector<unsigned, 4>> LPadToCallSiteMap;

  /// This is set to true if a call in the current block has been translated as
  /// a tail call. In this case, no subsequent DAG nodes should be created.
  bool HasTailCall = false;

  LLVMContext *Context = nullptr;

  SelectionDAGBuilder(SelectionDAG &dag, FunctionLoweringInfo &funcinfo,
                      SwiftErrorValueTracking &swifterror, CodeGenOptLevel ol)
      : SDNodeOrder(LowestSDNodeOrder), TM(dag.getTarget()), DAG(dag),
        SL(std::make_unique<SDAGSwitchLowering>(this, funcinfo)),
        FuncInfo(funcinfo), SwiftError(swifterror) {}

  void init(GCFunctionInfo *gfi, AAResults *AA, AssumptionCache *AC,
            const TargetLibraryInfo *li);

  /// Clear out the current SelectionDAG and the associated state and prepare
  /// this SelectionDAGBuilder object to be used for a new block. This doesn't
  /// clear out information about additional blocks that are needed to complete
  /// switch lowering or PHI node updating; that information is cleared out as
  /// it is consumed.
  void clear();

  /// Clear the dangling debug information map. This function is separated from
  /// the clear so that debug information that is dangling in a basic block can
  /// be properly resolved in a different basic block. This allows the
  /// SelectionDAG to resolve dangling debug information attached to PHI nodes.
  void clearDanglingDebugInfo();

  /// Return the current virtual root of the Selection DAG, flushing any
  /// PendingLoad items. This must be done before emitting a store or any other
  /// memory node that may need to be ordered after any prior load instructions.
  SDValue getMemoryRoot();

  /// Similar to getMemoryRoot, but also flushes PendingConstrainedFP(Strict)
  /// items. This must be done before emitting any call other any other node
  /// that may need to be ordered after FP instructions due to other side
  /// effects.
  SDValue getRoot();

  /// Similar to getRoot, but instead of flushing all the PendingLoad items,
  /// flush all the PendingExports (and PendingConstrainedFPStrict) items.
  /// It is necessary to do this before emitting a terminator instruction.
  SDValue getControlRoot();

  SDLoc getCurSDLoc() const {
    return SDLoc(CurInst, SDNodeOrder);
  }

  DebugLoc getCurDebugLoc() const {
    return CurInst ? CurInst->getDebugLoc() : DebugLoc();
  }

  void CopyValueToVirtualRegister(const Value *V, unsigned Reg,
                                  ISD::NodeType ExtendType = ISD::ANY_EXTEND);

  void visit(const Instruction &I);
  void visitDbgInfo(const Instruction &I);

  void visit(unsigned Opcode, const User &I);

  /// If there was virtual register allocated for the value V emit CopyFromReg
  /// of the specified type Ty. Return empty SDValue() otherwise.
  SDValue getCopyFromRegs(const Value *V, Type *Ty);

  /// Register a dbg_value which relies on a Value which we have not yet seen.
  void addDanglingDebugInfo(SmallVectorImpl<Value *> &Values,
                            DILocalVariable *Var, DIExpression *Expr,
                            bool IsVariadic, DebugLoc DL, unsigned Order);

  /// If we have dangling debug info that describes \p Variable, or an
  /// overlapping part of variable considering the \p Expr, then this method
  /// will drop that debug info as it isn't valid any longer.
  void dropDanglingDebugInfo(const DILocalVariable *Variable,
                             const DIExpression *Expr);

  /// If we saw an earlier dbg_value referring to V, generate the debug data
  /// structures now that we've seen its definition.
  void resolveDanglingDebugInfo(const Value *V, SDValue Val);

  /// For the given dangling debuginfo record, perform last-ditch efforts to
  /// resolve the debuginfo to something that is represented in this DAG. If
  /// this cannot be done, produce an Undef debug value record.
  void salvageUnresolvedDbgValue(const Value *V, DanglingDebugInfo &DDI);

  /// For a given list of Values, attempt to create and record a SDDbgValue in
  /// the SelectionDAG.
  bool handleDebugValue(ArrayRef<const Value *> Values, DILocalVariable *Var,
                        DIExpression *Expr, DebugLoc DbgLoc, unsigned Order,
                        bool IsVariadic);

  /// Create a record for a kill location debug intrinsic.
  void handleKillDebugValue(DILocalVariable *Var, DIExpression *Expr,
                            DebugLoc DbgLoc, unsigned Order);

  void handleDebugDeclare(Value *Address, DILocalVariable *Variable,
                          DIExpression *Expression, DebugLoc DL);

  /// Evict any dangling debug information, attempting to salvage it first.
  void resolveOrClearDbgInfo();

  SDValue getValue(const Value *V);

  SDValue getNonRegisterValue(const Value *V);
  SDValue getValueImpl(const Value *V);

  void setValue(const Value *V, SDValue NewN) {
    SDValue &N = NodeMap[V];
    assert(!N.getNode() && "Already set a value for this node!");
    N = NewN;
  }

  void setUnusedArgValue(const Value *V, SDValue NewN) {
    SDValue &N = UnusedArgNodeMap[V];
    assert(!N.getNode() && "Already set a value for this node!");
    N = NewN;
  }

  bool shouldKeepJumpConditionsTogether(
      const FunctionLoweringInfo &FuncInfo, const BranchInst &I,
      Instruction::BinaryOps Opc, const Value *Lhs, const Value *Rhs,
      TargetLoweringBase::CondMergingParams Params) const;

  void FindMergedConditions(const Value *Cond, MachineBasicBlock *TBB,
                            MachineBasicBlock *FBB, MachineBasicBlock *CurBB,
                            MachineBasicBlock *SwitchBB,
                            Instruction::BinaryOps Opc, BranchProbability TProb,
                            BranchProbability FProb, bool InvertCond);
  void EmitBranchForMergedCondition(const Value *Cond, MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    MachineBasicBlock *CurBB,
                                    MachineBasicBlock *SwitchBB,
                                    BranchProbability TProb, BranchProbability FProb,
                                    bool InvertCond);
  bool ShouldEmitAsBranches(const std::vector<SwitchCG::CaseBlock> &Cases);
  bool isExportableFromCurrentBlock(const Value *V, const BasicBlock *FromBB);
  void CopyToExportRegsIfNeeded(const Value *V);
  void ExportFromCurrentBlock(const Value *V);
  void LowerCallTo(const CallBase &CB, SDValue Callee, bool IsTailCall,
                   bool IsMustTailCall, const BasicBlock *EHPadBB = nullptr,
                   const TargetLowering::PtrAuthInfo *PAI = nullptr);

  // Lower range metadata from 0 to N to assert zext to an integer of nearest
  // floor power of two.
  SDValue lowerRangeToAssertZExt(SelectionDAG &DAG, const Instruction &I,
                                 SDValue Op);

  void populateCallLoweringInfo(TargetLowering::CallLoweringInfo &CLI,
                                const CallBase *Call, unsigned ArgIdx,
                                unsigned NumArgs, SDValue Callee,
                                Type *ReturnTy, AttributeSet RetAttrs,
                                bool IsPatchPoint);

  std::pair<SDValue, SDValue>
  lowerInvokable(TargetLowering::CallLoweringInfo &CLI,
                 const BasicBlock *EHPadBB = nullptr);

  /// When an MBB was split during scheduling, update the
  /// references that need to refer to the last resulting block.
  void UpdateSplitBlock(MachineBasicBlock *First, MachineBasicBlock *Last);

  /// Describes a gc.statepoint or a gc.statepoint like thing for the purposes
  /// of lowering into a STATEPOINT node.
  struct StatepointLoweringInfo {
    /// Bases[i] is the base pointer for Ptrs[i].  Together they denote the set
    /// of gc pointers this STATEPOINT has to relocate.
    SmallVector<const Value *, 16> Bases;
    SmallVector<const Value *, 16> Ptrs;

    /// The set of gc.relocate calls associated with this gc.statepoint.
    SmallVector<const GCRelocateInst *, 16> GCRelocates;

    /// The full list of gc arguments to the gc.statepoint being lowered.
    ArrayRef<const Use> GCArgs;

    /// The gc.statepoint instruction.
    const Instruction *StatepointInstr = nullptr;

    /// The list of gc transition arguments present in the gc.statepoint being
    /// lowered.
    ArrayRef<const Use> GCTransitionArgs;

    /// The ID that the resulting STATEPOINT instruction has to report.
    uint64_t ID = -1;

    /// Information regarding the underlying call instruction.
    TargetLowering::CallLoweringInfo CLI;

    /// The deoptimization state associated with this gc.statepoint call, if
    /// any.
    ArrayRef<const Use> DeoptState;

    /// Flags associated with the meta arguments being lowered.
    uint64_t StatepointFlags = -1;

    /// The number of patchable bytes the call needs to get lowered into.
    unsigned NumPatchBytes = -1;

    /// The exception handling unwind destination, in case this represents an
    /// invoke of gc.statepoint.
    const BasicBlock *EHPadBB = nullptr;

    explicit StatepointLoweringInfo(SelectionDAG &DAG) : CLI(DAG) {}
  };

  /// Lower \p SLI into a STATEPOINT instruction.
  SDValue LowerAsSTATEPOINT(StatepointLoweringInfo &SI);

  // This function is responsible for the whole statepoint lowering process.
  // It uniformly handles invoke and call statepoints.
  void LowerStatepoint(const GCStatepointInst &I,
                       const BasicBlock *EHPadBB = nullptr);

  void LowerCallSiteWithDeoptBundle(const CallBase *Call, SDValue Callee,
                                    const BasicBlock *EHPadBB);

  void LowerDeoptimizeCall(const CallInst *CI);
  void LowerDeoptimizingReturn();

  void LowerCallSiteWithDeoptBundleImpl(const CallBase *Call, SDValue Callee,
                                        const BasicBlock *EHPadBB,
                                        bool VarArgDisallowed,
                                        bool ForceVoidReturnTy);

  void LowerCallSiteWithPtrAuthBundle(const CallBase &CB,
                                      const BasicBlock *EHPadBB);

  /// Returns the type of FrameIndex and TargetFrameIndex nodes.
  MVT getFrameIndexTy() {
    return DAG.getTargetLoweringInfo().getFrameIndexTy(DAG.getDataLayout());
  }

private:
  // Terminator instructions.
  void visitRet(const ReturnInst &I);
  void visitBr(const BranchInst &I);
  void visitSwitch(const SwitchInst &I);
  void visitIndirectBr(const IndirectBrInst &I);
  void visitUnreachable(const UnreachableInst &I);
  void visitCleanupRet(const CleanupReturnInst &I);
  void visitCatchSwitch(const CatchSwitchInst &I);
  void visitCatchRet(const CatchReturnInst &I);
  void visitCatchPad(const CatchPadInst &I);
  void visitCleanupPad(const CleanupPadInst &CPI);

  BranchProbability getEdgeProbability(const MachineBasicBlock *Src,
                                       const MachineBasicBlock *Dst) const;
  void addSuccessorWithProb(
      MachineBasicBlock *Src, MachineBasicBlock *Dst,
      BranchProbability Prob = BranchProbability::getUnknown());

public:
  void visitSwitchCase(SwitchCG::CaseBlock &CB, MachineBasicBlock *SwitchBB);
  void visitSPDescriptorParent(StackProtectorDescriptor &SPD,
                               MachineBasicBlock *ParentBB);
  void visitSPDescriptorFailure(StackProtectorDescriptor &SPD);
  void visitBitTestHeader(SwitchCG::BitTestBlock &B,
                          MachineBasicBlock *SwitchBB);
  void visitBitTestCase(SwitchCG::BitTestBlock &BB, MachineBasicBlock *NextMBB,
                        BranchProbability BranchProbToNext, unsigned Reg,
                        SwitchCG::BitTestCase &B, MachineBasicBlock *SwitchBB);
  void visitJumpTable(SwitchCG::JumpTable &JT);
  void visitJumpTableHeader(SwitchCG::JumpTable &JT,
                            SwitchCG::JumpTableHeader &JTH,
                            MachineBasicBlock *SwitchBB);

private:
  // These all get lowered before this pass.
  void visitInvoke(const InvokeInst &I);
  void visitCallBr(const CallBrInst &I);
  void visitCallBrLandingPad(const CallInst &I);
  void visitResume(const ResumeInst &I);

  void visitUnary(const User &I, unsigned Opcode);
  void visitFNeg(const User &I) { visitUnary(I, ISD::FNEG); }

  void visitBinary(const User &I, unsigned Opcode);
  void visitShift(const User &I, unsigned Opcode);
  void visitAdd(const User &I)  { visitBinary(I, ISD::ADD); }
  void visitFAdd(const User &I) { visitBinary(I, ISD::FADD); }
  void visitSub(const User &I)  { visitBinary(I, ISD::SUB); }
  void visitFSub(const User &I) { visitBinary(I, ISD::FSUB); }
  void visitMul(const User &I)  { visitBinary(I, ISD::MUL); }
  void visitFMul(const User &I) { visitBinary(I, ISD::FMUL); }
  void visitURem(const User &I) { visitBinary(I, ISD::UREM); }
  void visitSRem(const User &I) { visitBinary(I, ISD::SREM); }
  void visitFRem(const User &I) { visitBinary(I, ISD::FREM); }
  void visitUDiv(const User &I) { visitBinary(I, ISD::UDIV); }
  void visitSDiv(const User &I);
  void visitFDiv(const User &I) { visitBinary(I, ISD::FDIV); }
  void visitAnd (const User &I) { visitBinary(I, ISD::AND); }
  void visitOr  (const User &I) { visitBinary(I, ISD::OR); }
  void visitXor (const User &I) { visitBinary(I, ISD::XOR); }
  void visitShl (const User &I) { visitShift(I, ISD::SHL); }
  void visitLShr(const User &I) { visitShift(I, ISD::SRL); }
  void visitAShr(const User &I) { visitShift(I, ISD::SRA); }
  void visitICmp(const ICmpInst &I);
  void visitFCmp(const FCmpInst &I);
  // Visit the conversion instructions
  void visitTrunc(const User &I);
  void visitZExt(const User &I);
  void visitSExt(const User &I);
  void visitFPTrunc(const User &I);
  void visitFPExt(const User &I);
  void visitFPToUI(const User &I);
  void visitFPToSI(const User &I);
  void visitUIToFP(const User &I);
  void visitSIToFP(const User &I);
  void visitPtrToInt(const User &I);
  void visitIntToPtr(const User &I);
  void visitBitCast(const User &I);
  void visitAddrSpaceCast(const User &I);

  void visitExtractElement(const User &I);
  void visitInsertElement(const User &I);
  void visitShuffleVector(const User &I);

  void visitExtractValue(const ExtractValueInst &I);
  void visitInsertValue(const InsertValueInst &I);
  void visitLandingPad(const LandingPadInst &LP);

  void visitGetElementPtr(const User &I);
  void visitSelect(const User &I);

  void visitAlloca(const AllocaInst &I);
  void visitLoad(const LoadInst &I);
  void visitStore(const StoreInst &I);
  void visitMaskedLoad(const CallInst &I, bool IsExpanding = false);
  void visitMaskedStore(const CallInst &I, bool IsCompressing = false);
  void visitMaskedGather(const CallInst &I);
  void visitMaskedScatter(const CallInst &I);
  void visitAtomicCmpXchg(const AtomicCmpXchgInst &I);
  void visitAtomicRMW(const AtomicRMWInst &I);
  void visitFence(const FenceInst &I);
  void visitPHI(const PHINode &I);
  void visitCall(const CallInst &I);
  bool visitMemCmpBCmpCall(const CallInst &I);
  bool visitMemPCpyCall(const CallInst &I);
  bool visitMemChrCall(const CallInst &I);
  bool visitStrCpyCall(const CallInst &I, bool isStpcpy);
  bool visitStrCmpCall(const CallInst &I);
  bool visitStrLenCall(const CallInst &I);
  bool visitStrNLenCall(const CallInst &I);
  bool visitUnaryFloatCall(const CallInst &I, unsigned Opcode);
  bool visitBinaryFloatCall(const CallInst &I, unsigned Opcode);
  void visitAtomicLoad(const LoadInst &I);
  void visitAtomicStore(const StoreInst &I);
  void visitLoadFromSwiftError(const LoadInst &I);
  void visitStoreToSwiftError(const StoreInst &I);
  void visitFreeze(const FreezeInst &I);

  void visitInlineAsm(const CallBase &Call,
                      const BasicBlock *EHPadBB = nullptr);

  bool visitEntryValueDbgValue(ArrayRef<const Value *> Values,
                               DILocalVariable *Variable, DIExpression *Expr,
                               DebugLoc DbgLoc);
  void visitIntrinsicCall(const CallInst &I, unsigned Intrinsic);
  void visitTargetIntrinsic(const CallInst &I, unsigned Intrinsic);
  void visitConstrainedFPIntrinsic(const ConstrainedFPIntrinsic &FPI);
  void visitConvergenceControl(const CallInst &I, unsigned Intrinsic);
  void visitVectorHistogram(const CallInst &I, unsigned IntrinsicID);
  void visitVPLoad(const VPIntrinsic &VPIntrin, EVT VT,
                   const SmallVectorImpl<SDValue> &OpValues);
  void visitVPStore(const VPIntrinsic &VPIntrin,
                    const SmallVectorImpl<SDValue> &OpValues);
  void visitVPGather(const VPIntrinsic &VPIntrin, EVT VT,
                     const SmallVectorImpl<SDValue> &OpValues);
  void visitVPScatter(const VPIntrinsic &VPIntrin,
                      const SmallVectorImpl<SDValue> &OpValues);
  void visitVPStridedLoad(const VPIntrinsic &VPIntrin, EVT VT,
                          const SmallVectorImpl<SDValue> &OpValues);
  void visitVPStridedStore(const VPIntrinsic &VPIntrin,
                           const SmallVectorImpl<SDValue> &OpValues);
  void visitVPCmp(const VPCmpIntrinsic &VPIntrin);
  void visitVectorPredicationIntrinsic(const VPIntrinsic &VPIntrin);

  void visitVAStart(const CallInst &I);
  void visitVAArg(const VAArgInst &I);
  void visitVAEnd(const CallInst &I);
  void visitVACopy(const CallInst &I);
  void visitStackmap(const CallInst &I);
  void visitPatchpoint(const CallBase &CB, const BasicBlock *EHPadBB = nullptr);

  // These two are implemented in StatepointLowering.cpp
  void visitGCRelocate(const GCRelocateInst &Relocate);
  void visitGCResult(const GCResultInst &I);

  void visitVectorReduce(const CallInst &I, unsigned Intrinsic);
  void visitVectorReverse(const CallInst &I);
  void visitVectorSplice(const CallInst &I);
  void visitVectorInterleave(const CallInst &I);
  void visitVectorDeinterleave(const CallInst &I);
  void visitStepVector(const CallInst &I);

  void visitUserOp1(const Instruction &I) {
    llvm_unreachable("UserOp1 should not exist at instruction selection time!");
  }
  void visitUserOp2(const Instruction &I) {
    llvm_unreachable("UserOp2 should not exist at instruction selection time!");
  }

  void processIntegerCallValue(const Instruction &I,
                               SDValue Value, bool IsSigned);

  void HandlePHINodesInSuccessorBlocks(const BasicBlock *LLVMBB);

  void emitInlineAsmError(const CallBase &Call, const Twine &Message);

  /// An enum that states to emit func argument dbg value the kind of intrinsic
  /// it originally had. This controls the internal behavior of
  /// EmitFuncArgumentDbgValue.
  enum class FuncArgumentDbgValueKind {
    Value,   // This was originally a llvm.dbg.value.
    Declare, // This was originally a llvm.dbg.declare.
  };

  /// If V is an function argument then create corresponding DBG_VALUE machine
  /// instruction for it now. At the end of instruction selection, they will be
  /// inserted to the entry BB.
  bool EmitFuncArgumentDbgValue(const Value *V, DILocalVariable *Variable,
                                DIExpression *Expr, DILocation *DL,
                                FuncArgumentDbgValueKind Kind,
                                const SDValue &N);

  /// Return the next block after MBB, or nullptr if there is none.
  MachineBasicBlock *NextBlock(MachineBasicBlock *MBB);

  /// Update the DAG and DAG builder with the relevant information after
  /// a new root node has been created which could be a tail call.
  void updateDAGForMaybeTailCall(SDValue MaybeTC);

  /// Return the appropriate SDDbgValue based on N.
  SDDbgValue *getDbgValue(SDValue N, DILocalVariable *Variable,
                          DIExpression *Expr, const DebugLoc &dl,
                          unsigned DbgSDNodeOrder);

  /// Lowers CallInst to an external symbol.
  void lowerCallToExternalSymbol(const CallInst &I, const char *FunctionName);

  SDValue lowerStartEH(SDValue Chain, const BasicBlock *EHPadBB,
                       MCSymbol *&BeginLabel);
  SDValue lowerEndEH(SDValue Chain, const InvokeInst *II,
                     const BasicBlock *EHPadBB, MCSymbol *BeginLabel);
};

/// This struct represents the registers (physical or virtual)
/// that a particular set of values is assigned, and the type information about
/// the value. The most common situation is to represent one value at a time,
/// but struct or array values are handled element-wise as multiple values.  The
/// splitting of aggregates is performed recursively, so that we never have
/// aggregate-typed registers. The values at this point do not necessarily have
/// legal types, so each value may require one or more registers of some legal
/// type.
///
struct RegsForValue {
  /// The value types of the values, which may not be legal, and
  /// may need be promoted or synthesized from one or more registers.
  SmallVector<EVT, 4> ValueVTs;

  /// The value types of the registers. This is the same size as ValueVTs and it
  /// records, for each value, what the type of the assigned register or
  /// registers are. (Individual values are never synthesized from more than one
  /// type of register.)
  ///
  /// With virtual registers, the contents of RegVTs is redundant with TLI's
  /// getRegisterType member function, however when with physical registers
  /// it is necessary to have a separate record of the types.
  SmallVector<MVT, 4> RegVTs;

  /// This list holds the registers assigned to the values.
  /// Each legal or promoted value requires one register, and each
  /// expanded value requires multiple registers.
  SmallVector<unsigned, 4> Regs;

  /// This list holds the number of registers for each value.
  SmallVector<unsigned, 4> RegCount;

  /// Records if this value needs to be treated in an ABI dependant manner,
  /// different to normal type legalization.
  std::optional<CallingConv::ID> CallConv;

  RegsForValue() = default;
  RegsForValue(const SmallVector<unsigned, 4> &regs, MVT regvt, EVT valuevt,
               std::optional<CallingConv::ID> CC = std::nullopt);
  RegsForValue(LLVMContext &Context, const TargetLowering &TLI,
               const DataLayout &DL, unsigned Reg, Type *Ty,
               std::optional<CallingConv::ID> CC);

  bool isABIMangled() const { return CallConv.has_value(); }

  /// Add the specified values to this one.
  void append(const RegsForValue &RHS) {
    ValueVTs.append(RHS.ValueVTs.begin(), RHS.ValueVTs.end());
    RegVTs.append(RHS.RegVTs.begin(), RHS.RegVTs.end());
    Regs.append(RHS.Regs.begin(), RHS.Regs.end());
    RegCount.push_back(RHS.Regs.size());
  }

  /// Emit a series of CopyFromReg nodes that copies from this value and returns
  /// the result as a ValueVTs value. This uses Chain/Flag as the input and
  /// updates them for the output Chain/Flag. If the Flag pointer is NULL, no
  /// flag is used.
  SDValue getCopyFromRegs(SelectionDAG &DAG, FunctionLoweringInfo &FuncInfo,
                          const SDLoc &dl, SDValue &Chain, SDValue *Glue,
                          const Value *V = nullptr) const;

  /// Emit a series of CopyToReg nodes that copies the specified value into the
  /// registers specified by this object. This uses Chain/Flag as the input and
  /// updates them for the output Chain/Flag. If the Flag pointer is nullptr, no
  /// flag is used. If V is not nullptr, then it is used in printing better
  /// diagnostic messages on error.
  void getCopyToRegs(SDValue Val, SelectionDAG &DAG, const SDLoc &dl,
                     SDValue &Chain, SDValue *Glue, const Value *V = nullptr,
                     ISD::NodeType PreferredExtendType = ISD::ANY_EXTEND) const;

  /// Add this value to the specified inlineasm node operand list. This adds the
  /// code marker, matching input operand index (if applicable), and includes
  /// the number of values added into it.
  void AddInlineAsmOperands(InlineAsm::Kind Code, bool HasMatching,
                            unsigned MatchingIdx, const SDLoc &dl,
                            SelectionDAG &DAG, std::vector<SDValue> &Ops) const;

  /// Check if the total RegCount is greater than one.
  bool occupiesMultipleRegs() const {
    return std::accumulate(RegCount.begin(), RegCount.end(), 0) > 1;
  }

  /// Return a list of registers and their sizes.
  SmallVector<std::pair<unsigned, TypeSize>, 4> getRegsAndSizes() const;
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_SELECTIONDAG_SELECTIONDAGBUILDER_H
