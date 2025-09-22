//===- SwitchLoweringUtils.h - Switch Lowering ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SWITCHLOWERINGUTILS_H
#define LLVM_CODEGEN_SWITCHLOWERINGUTILS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/BranchProbability.h"
#include <vector>

namespace llvm {

class BlockFrequencyInfo;
class ConstantInt;
class FunctionLoweringInfo;
class MachineBasicBlock;
class ProfileSummaryInfo;
class TargetLowering;
class TargetMachine;

namespace SwitchCG {

enum CaseClusterKind {
  /// A cluster of adjacent case labels with the same destination, or just one
  /// case.
  CC_Range,
  /// A cluster of cases suitable for jump table lowering.
  CC_JumpTable,
  /// A cluster of cases suitable for bit test lowering.
  CC_BitTests
};

/// A cluster of case labels.
struct CaseCluster {
  CaseClusterKind Kind;
  const ConstantInt *Low, *High;
  union {
    MachineBasicBlock *MBB;
    unsigned JTCasesIndex;
    unsigned BTCasesIndex;
  };
  BranchProbability Prob;

  static CaseCluster range(const ConstantInt *Low, const ConstantInt *High,
                           MachineBasicBlock *MBB, BranchProbability Prob) {
    CaseCluster C;
    C.Kind = CC_Range;
    C.Low = Low;
    C.High = High;
    C.MBB = MBB;
    C.Prob = Prob;
    return C;
  }

  static CaseCluster jumpTable(const ConstantInt *Low, const ConstantInt *High,
                               unsigned JTCasesIndex, BranchProbability Prob) {
    CaseCluster C;
    C.Kind = CC_JumpTable;
    C.Low = Low;
    C.High = High;
    C.JTCasesIndex = JTCasesIndex;
    C.Prob = Prob;
    return C;
  }

  static CaseCluster bitTests(const ConstantInt *Low, const ConstantInt *High,
                              unsigned BTCasesIndex, BranchProbability Prob) {
    CaseCluster C;
    C.Kind = CC_BitTests;
    C.Low = Low;
    C.High = High;
    C.BTCasesIndex = BTCasesIndex;
    C.Prob = Prob;
    return C;
  }
};

using CaseClusterVector = std::vector<CaseCluster>;
using CaseClusterIt = CaseClusterVector::iterator;

/// Sort Clusters and merge adjacent cases.
void sortAndRangeify(CaseClusterVector &Clusters);

struct CaseBits {
  uint64_t Mask = 0;
  MachineBasicBlock *BB = nullptr;
  unsigned Bits = 0;
  BranchProbability ExtraProb;

  CaseBits() = default;
  CaseBits(uint64_t mask, MachineBasicBlock *bb, unsigned bits,
           BranchProbability Prob)
      : Mask(mask), BB(bb), Bits(bits), ExtraProb(Prob) {}
};

using CaseBitsVector = std::vector<CaseBits>;

/// This structure is used to communicate between SelectionDAGBuilder and
/// SDISel for the code generation of additional basic blocks needed by
/// multi-case switch statements.
struct CaseBlock {
  // For the GISel interface.
  struct PredInfoPair {
    CmpInst::Predicate Pred;
    // Set when no comparison should be emitted.
    bool NoCmp;
  };
  union {
    // The condition code to use for the case block's setcc node.
    // Besides the integer condition codes, this can also be SETTRUE, in which
    // case no comparison gets emitted.
    ISD::CondCode CC;
    struct PredInfoPair PredInfo;
  };

  // The LHS/MHS/RHS of the comparison to emit.
  // Emit by default LHS op RHS. MHS is used for range comparisons:
  // If MHS is not null: (LHS <= MHS) and (MHS <= RHS).
  const Value *CmpLHS, *CmpMHS, *CmpRHS;

  // The block to branch to if the setcc is true/false.
  MachineBasicBlock *TrueBB, *FalseBB;

  // The block into which to emit the code for the setcc and branches.
  MachineBasicBlock *ThisBB;

  /// The debug location of the instruction this CaseBlock was
  /// produced from.
  SDLoc DL;
  DebugLoc DbgLoc;

  // Branch weights.
  BranchProbability TrueProb, FalseProb;

  // Constructor for SelectionDAG.
  CaseBlock(ISD::CondCode cc, const Value *cmplhs, const Value *cmprhs,
            const Value *cmpmiddle, MachineBasicBlock *truebb,
            MachineBasicBlock *falsebb, MachineBasicBlock *me, SDLoc dl,
            BranchProbability trueprob = BranchProbability::getUnknown(),
            BranchProbability falseprob = BranchProbability::getUnknown())
      : CC(cc), CmpLHS(cmplhs), CmpMHS(cmpmiddle), CmpRHS(cmprhs),
        TrueBB(truebb), FalseBB(falsebb), ThisBB(me), DL(dl),
        TrueProb(trueprob), FalseProb(falseprob) {}

  // Constructor for GISel.
  CaseBlock(CmpInst::Predicate pred, bool nocmp, const Value *cmplhs,
            const Value *cmprhs, const Value *cmpmiddle,
            MachineBasicBlock *truebb, MachineBasicBlock *falsebb,
            MachineBasicBlock *me, DebugLoc dl,
            BranchProbability trueprob = BranchProbability::getUnknown(),
            BranchProbability falseprob = BranchProbability::getUnknown())
      : PredInfo({pred, nocmp}), CmpLHS(cmplhs), CmpMHS(cmpmiddle),
        CmpRHS(cmprhs), TrueBB(truebb), FalseBB(falsebb), ThisBB(me),
        DbgLoc(dl), TrueProb(trueprob), FalseProb(falseprob) {}
};

struct JumpTable {
  /// The virtual register containing the index of the jump table entry
  /// to jump to.
  unsigned Reg;
  /// The JumpTableIndex for this jump table in the function.
  unsigned JTI;
  /// The MBB into which to emit the code for the indirect jump.
  MachineBasicBlock *MBB;
  /// The MBB of the default bb, which is a successor of the range
  /// check MBB.  This is when updating PHI nodes in successors.
  MachineBasicBlock *Default;

  /// The debug location of the instruction this JumpTable was produced from.
  std::optional<SDLoc> SL; // For SelectionDAG

  JumpTable(unsigned R, unsigned J, MachineBasicBlock *M, MachineBasicBlock *D,
            std::optional<SDLoc> SL)
      : Reg(R), JTI(J), MBB(M), Default(D), SL(SL) {}
};
struct JumpTableHeader {
  APInt First;
  APInt Last;
  const Value *SValue;
  MachineBasicBlock *HeaderBB;
  bool Emitted;
  bool FallthroughUnreachable = false;

  JumpTableHeader(APInt F, APInt L, const Value *SV, MachineBasicBlock *H,
                  bool E = false)
      : First(std::move(F)), Last(std::move(L)), SValue(SV), HeaderBB(H),
        Emitted(E) {}
};
using JumpTableBlock = std::pair<JumpTableHeader, JumpTable>;

struct BitTestCase {
  uint64_t Mask;
  MachineBasicBlock *ThisBB;
  MachineBasicBlock *TargetBB;
  BranchProbability ExtraProb;

  BitTestCase(uint64_t M, MachineBasicBlock *T, MachineBasicBlock *Tr,
              BranchProbability Prob)
      : Mask(M), ThisBB(T), TargetBB(Tr), ExtraProb(Prob) {}
};

using BitTestInfo = SmallVector<BitTestCase, 3>;

struct BitTestBlock {
  APInt First;
  APInt Range;
  const Value *SValue;
  unsigned Reg;
  MVT RegVT;
  bool Emitted;
  bool ContiguousRange;
  MachineBasicBlock *Parent;
  MachineBasicBlock *Default;
  BitTestInfo Cases;
  BranchProbability Prob;
  BranchProbability DefaultProb;
  bool FallthroughUnreachable = false;

  BitTestBlock(APInt F, APInt R, const Value *SV, unsigned Rg, MVT RgVT, bool E,
               bool CR, MachineBasicBlock *P, MachineBasicBlock *D,
               BitTestInfo C, BranchProbability Pr)
      : First(std::move(F)), Range(std::move(R)), SValue(SV), Reg(Rg),
        RegVT(RgVT), Emitted(E), ContiguousRange(CR), Parent(P), Default(D),
        Cases(std::move(C)), Prob(Pr) {}
};

/// Return the range of values within a range.
uint64_t getJumpTableRange(const CaseClusterVector &Clusters, unsigned First,
                           unsigned Last);

/// Return the number of cases within a range.
uint64_t getJumpTableNumCases(const SmallVectorImpl<unsigned> &TotalCases,
                              unsigned First, unsigned Last);

struct SwitchWorkListItem {
  MachineBasicBlock *MBB = nullptr;
  CaseClusterIt FirstCluster;
  CaseClusterIt LastCluster;
  const ConstantInt *GE = nullptr;
  const ConstantInt *LT = nullptr;
  BranchProbability DefaultProb;
};
using SwitchWorkList = SmallVector<SwitchWorkListItem, 4>;

class SwitchLowering {
public:
  SwitchLowering(FunctionLoweringInfo &funcinfo) : FuncInfo(funcinfo) {}

  void init(const TargetLowering &tli, const TargetMachine &tm,
            const DataLayout &dl) {
    TLI = &tli;
    TM = &tm;
    DL = &dl;
  }

  /// Vector of CaseBlock structures used to communicate SwitchInst code
  /// generation information.
  std::vector<CaseBlock> SwitchCases;

  /// Vector of JumpTable structures used to communicate SwitchInst code
  /// generation information.
  std::vector<JumpTableBlock> JTCases;

  /// Vector of BitTestBlock structures used to communicate SwitchInst code
  /// generation information.
  std::vector<BitTestBlock> BitTestCases;

  void findJumpTables(CaseClusterVector &Clusters, const SwitchInst *SI,
                      std::optional<SDLoc> SL, MachineBasicBlock *DefaultMBB,
                      ProfileSummaryInfo *PSI, BlockFrequencyInfo *BFI);

  bool buildJumpTable(const CaseClusterVector &Clusters, unsigned First,
                      unsigned Last, const SwitchInst *SI,
                      const std::optional<SDLoc> &SL,
                      MachineBasicBlock *DefaultMBB, CaseCluster &JTCluster);

  void findBitTestClusters(CaseClusterVector &Clusters, const SwitchInst *SI);

  /// Build a bit test cluster from Clusters[First..Last]. Returns false if it
  /// decides it's not a good idea.
  bool buildBitTests(CaseClusterVector &Clusters, unsigned First, unsigned Last,
                     const SwitchInst *SI, CaseCluster &BTCluster);

  virtual void addSuccessorWithProb(
      MachineBasicBlock *Src, MachineBasicBlock *Dst,
      BranchProbability Prob = BranchProbability::getUnknown()) = 0;

  /// Determine the rank by weight of CC in [First,Last]. If CC has more weight
  /// than each cluster in the range, its rank is 0.
  unsigned caseClusterRank(const CaseCluster &CC, CaseClusterIt First,
                           CaseClusterIt Last);

  struct SplitWorkItemInfo {
    CaseClusterIt LastLeft;
    CaseClusterIt FirstRight;
    BranchProbability LeftProb;
    BranchProbability RightProb;
  };
  /// Compute information to balance the tree based on branch probabilities to
  /// create a near-optimal (in terms of search time given key frequency) binary
  /// search tree. See e.g. Kurt Mehlhorn "Nearly Optimal Binary Search Trees"
  /// (1975).
  SplitWorkItemInfo computeSplitWorkItemInfo(const SwitchWorkListItem &W);
  virtual ~SwitchLowering() = default;

private:
  const TargetLowering *TLI = nullptr;
  const TargetMachine *TM = nullptr;
  const DataLayout *DL = nullptr;
  FunctionLoweringInfo &FuncInfo;
};

} // namespace SwitchCG
} // namespace llvm

#endif // LLVM_CODEGEN_SWITCHLOWERINGUTILS_H
