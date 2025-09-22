//===- HexagonHardwareLoops.cpp - Identify and generate hardware loops ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass identifies loops where we can generate the Hexagon hardware
// loop instruction.  The hardware loop can perform loop branches with a
// zero-cycle overhead.
//
// The pattern that defines the induction variable can changed depending on
// prior optimizations.  For example, the IndVarSimplify phase run by 'opt'
// normalizes induction variables, and the Loop Strength Reduction pass
// run by 'llc' may also make changes to the induction variable.
// The pattern detected by this phase is due to running Strength Reduction.
//
// Criteria for hardware loops:
//  - Countable loops (w/ ind. var for a trip count)
//  - Assumes loops are normalized by IndVarSimplify
//  - Try inner-most loops first
//  - No function calls in loops.
//
//===----------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "hwloops"

#ifndef NDEBUG
static cl::opt<int> HWLoopLimit("hexagon-max-hwloop", cl::Hidden, cl::init(-1));

// Option to create preheader only for a specific function.
static cl::opt<std::string> PHFn("hexagon-hwloop-phfn", cl::Hidden,
                                 cl::init(""));
#endif

// Option to create a preheader if one doesn't exist.
static cl::opt<bool> HWCreatePreheader("hexagon-hwloop-preheader",
    cl::Hidden, cl::init(true),
    cl::desc("Add a preheader to a hardware loop if one doesn't exist"));

// Turn it off by default. If a preheader block is not created here, the
// software pipeliner may be unable to find a block suitable to serve as
// a preheader. In that case SWP will not run.
static cl::opt<bool> SpecPreheader("hwloop-spec-preheader", cl::Hidden,
                                   cl::desc("Allow speculation of preheader "
                                            "instructions"));

STATISTIC(NumHWLoops, "Number of loops converted to hardware loops");

namespace llvm {

  FunctionPass *createHexagonHardwareLoops();
  void initializeHexagonHardwareLoopsPass(PassRegistry&);

} // end namespace llvm

namespace {

  class CountValue;

  struct HexagonHardwareLoops : public MachineFunctionPass {
    MachineLoopInfo            *MLI;
    MachineRegisterInfo        *MRI;
    MachineDominatorTree       *MDT;
    const HexagonInstrInfo     *TII;
    const HexagonRegisterInfo  *TRI;
#ifndef NDEBUG
    static int Counter;
#endif

  public:
    static char ID;

    HexagonHardwareLoops() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &MF) override;

    StringRef getPassName() const override { return "Hexagon Hardware Loops"; }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addRequired<MachineLoopInfoWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

  private:
    using LoopFeederMap = std::map<Register, MachineInstr *>;

    /// Kinds of comparisons in the compare instructions.
    struct Comparison {
      enum Kind {
        EQ  = 0x01,
        NE  = 0x02,
        L   = 0x04,
        G   = 0x08,
        U   = 0x40,
        LTs = L,
        LEs = L | EQ,
        GTs = G,
        GEs = G | EQ,
        LTu = L      | U,
        LEu = L | EQ | U,
        GTu = G      | U,
        GEu = G | EQ | U
      };

      static Kind getSwappedComparison(Kind Cmp) {
        assert ((!((Cmp & L) && (Cmp & G))) && "Malformed comparison operator");
        if ((Cmp & L) || (Cmp & G))
          return (Kind)(Cmp ^ (L|G));
        return Cmp;
      }

      static Kind getNegatedComparison(Kind Cmp) {
        if ((Cmp & L) || (Cmp & G))
          return (Kind)((Cmp ^ (L | G)) ^ EQ);
        if ((Cmp & NE) || (Cmp & EQ))
          return (Kind)(Cmp ^ (EQ | NE));
        return (Kind)0;
      }

      static bool isSigned(Kind Cmp) {
        return (Cmp & (L | G) && !(Cmp & U));
      }

      static bool isUnsigned(Kind Cmp) {
        return (Cmp & U);
      }
    };

    /// Find the register that contains the loop controlling
    /// induction variable.
    /// If successful, it will return true and set the \p Reg, \p IVBump
    /// and \p IVOp arguments.  Otherwise it will return false.
    /// The returned induction register is the register R that follows the
    /// following induction pattern:
    /// loop:
    ///   R = phi ..., [ R.next, LatchBlock ]
    ///   R.next = R + #bump
    ///   if (R.next < #N) goto loop
    /// IVBump is the immediate value added to R, and IVOp is the instruction
    /// "R.next = R + #bump".
    bool findInductionRegister(MachineLoop *L, Register &Reg,
                               int64_t &IVBump, MachineInstr *&IVOp) const;

    /// Return the comparison kind for the specified opcode.
    Comparison::Kind getComparisonKind(unsigned CondOpc,
                                       MachineOperand *InitialValue,
                                       const MachineOperand *Endvalue,
                                       int64_t IVBump) const;

    /// Analyze the statements in a loop to determine if the loop
    /// has a computable trip count and, if so, return a value that represents
    /// the trip count expression.
    CountValue *getLoopTripCount(MachineLoop *L,
                                 SmallVectorImpl<MachineInstr *> &OldInsts);

    /// Return the expression that represents the number of times
    /// a loop iterates.  The function takes the operands that represent the
    /// loop start value, loop end value, and induction value.  Based upon
    /// these operands, the function attempts to compute the trip count.
    /// If the trip count is not directly available (as an immediate value,
    /// or a register), the function will attempt to insert computation of it
    /// to the loop's preheader.
    CountValue *computeCount(MachineLoop *Loop, const MachineOperand *Start,
                             const MachineOperand *End, Register IVReg,
                             int64_t IVBump, Comparison::Kind Cmp) const;

    /// Return true if the instruction is not valid within a hardware
    /// loop.
    bool isInvalidLoopOperation(const MachineInstr *MI,
                                bool IsInnerHWLoop) const;

    /// Return true if the loop contains an instruction that inhibits
    /// using the hardware loop.
    bool containsInvalidInstruction(MachineLoop *L, bool IsInnerHWLoop) const;

    /// Given a loop, check if we can convert it to a hardware loop.
    /// If so, then perform the conversion and return true.
    bool convertToHardwareLoop(MachineLoop *L, bool &L0used, bool &L1used);

    /// Return true if the instruction is now dead.
    bool isDead(const MachineInstr *MI,
                SmallVectorImpl<MachineInstr *> &DeadPhis) const;

    /// Remove the instruction if it is now dead.
    void removeIfDead(MachineInstr *MI);

    /// Make sure that the "bump" instruction executes before the
    /// compare.  We need that for the IV fixup, so that the compare
    /// instruction would not use a bumped value that has not yet been
    /// defined.  If the instructions are out of order, try to reorder them.
    bool orderBumpCompare(MachineInstr *BumpI, MachineInstr *CmpI);

    /// Return true if MO and MI pair is visited only once. If visited
    /// more than once, this indicates there is recursion. In such a case,
    /// return false.
    bool isLoopFeeder(MachineLoop *L, MachineBasicBlock *A, MachineInstr *MI,
                      const MachineOperand *MO,
                      LoopFeederMap &LoopFeederPhi) const;

    /// Return true if the Phi may generate a value that may underflow,
    /// or may wrap.
    bool phiMayWrapOrUnderflow(MachineInstr *Phi, const MachineOperand *EndVal,
                               MachineBasicBlock *MBB, MachineLoop *L,
                               LoopFeederMap &LoopFeederPhi) const;

    /// Return true if the induction variable may underflow an unsigned
    /// value in the first iteration.
    bool loopCountMayWrapOrUnderFlow(const MachineOperand *InitVal,
                                     const MachineOperand *EndVal,
                                     MachineBasicBlock *MBB, MachineLoop *L,
                                     LoopFeederMap &LoopFeederPhi) const;

    /// Check if the given operand has a compile-time known constant
    /// value. Return true if yes, and false otherwise. When returning true, set
    /// Val to the corresponding constant value.
    bool checkForImmediate(const MachineOperand &MO, int64_t &Val) const;

    /// Check if the operand has a compile-time known constant value.
    bool isImmediate(const MachineOperand &MO) const {
      int64_t V;
      return checkForImmediate(MO, V);
    }

    /// Return the immediate for the specified operand.
    int64_t getImmediate(const MachineOperand &MO) const {
      int64_t V;
      if (!checkForImmediate(MO, V))
        llvm_unreachable("Invalid operand");
      return V;
    }

    /// Reset the given machine operand to now refer to a new immediate
    /// value.  Assumes that the operand was already referencing an immediate
    /// value, either directly, or via a register.
    void setImmediate(MachineOperand &MO, int64_t Val);

    /// Fix the data flow of the induction variable.
    /// The desired flow is: phi ---> bump -+-> comparison-in-latch.
    ///                                     |
    ///                                     +-> back to phi
    /// where "bump" is the increment of the induction variable:
    ///   iv = iv + #const.
    /// Due to some prior code transformations, the actual flow may look
    /// like this:
    ///   phi -+-> bump ---> back to phi
    ///        |
    ///        +-> comparison-in-latch (against upper_bound-bump),
    /// i.e. the comparison that controls the loop execution may be using
    /// the value of the induction variable from before the increment.
    ///
    /// Return true if the loop's flow is the desired one (i.e. it's
    /// either been fixed, or no fixing was necessary).
    /// Otherwise, return false.  This can happen if the induction variable
    /// couldn't be identified, or if the value in the latch's comparison
    /// cannot be adjusted to reflect the post-bump value.
    bool fixupInductionVariable(MachineLoop *L);

    /// Given a loop, if it does not have a preheader, create one.
    /// Return the block that is the preheader.
    MachineBasicBlock *createPreheaderForLoop(MachineLoop *L);
  };

  char HexagonHardwareLoops::ID = 0;
#ifndef NDEBUG
  int HexagonHardwareLoops::Counter = 0;
#endif

  /// Abstraction for a trip count of a loop. A smaller version
  /// of the MachineOperand class without the concerns of changing the
  /// operand representation.
  class CountValue {
  public:
    enum CountValueType {
      CV_Register,
      CV_Immediate
    };

  private:
    CountValueType Kind;
    union Values {
      Values() : R{Register(), 0} {}
      Values(const Values&) = default;
      struct {
        Register Reg;
        unsigned Sub;
      } R;
      unsigned ImmVal;
    } Contents;

  public:
    explicit CountValue(CountValueType t, Register v, unsigned u = 0) {
      Kind = t;
      if (Kind == CV_Register) {
        Contents.R.Reg = v;
        Contents.R.Sub = u;
      } else {
        Contents.ImmVal = v;
      }
    }

    bool isReg() const { return Kind == CV_Register; }
    bool isImm() const { return Kind == CV_Immediate; }

    Register getReg() const {
      assert(isReg() && "Wrong CountValue accessor");
      return Contents.R.Reg;
    }

    unsigned getSubReg() const {
      assert(isReg() && "Wrong CountValue accessor");
      return Contents.R.Sub;
    }

    unsigned getImm() const {
      assert(isImm() && "Wrong CountValue accessor");
      return Contents.ImmVal;
    }

    void print(raw_ostream &OS, const TargetRegisterInfo *TRI = nullptr) const {
      if (isReg()) { OS << printReg(Contents.R.Reg, TRI, Contents.R.Sub); }
      if (isImm()) { OS << Contents.ImmVal; }
    }
  };

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(HexagonHardwareLoops, "hwloops",
                      "Hexagon Hardware Loops", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(HexagonHardwareLoops, "hwloops",
                    "Hexagon Hardware Loops", false, false)

FunctionPass *llvm::createHexagonHardwareLoops() {
  return new HexagonHardwareLoops();
}

bool HexagonHardwareLoops::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********* Hexagon Hardware Loops *********\n");
  if (skipFunction(MF.getFunction()))
    return false;

  bool Changed = false;

  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MRI = &MF.getRegInfo();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  const HexagonSubtarget &HST = MF.getSubtarget<HexagonSubtarget>();
  TII = HST.getInstrInfo();
  TRI = HST.getRegisterInfo();

  for (auto &L : *MLI)
    if (L->isOutermost()) {
      bool L0Used = false;
      bool L1Used = false;
      Changed |= convertToHardwareLoop(L, L0Used, L1Used);
    }

  return Changed;
}

bool HexagonHardwareLoops::findInductionRegister(MachineLoop *L,
                                                 Register &Reg,
                                                 int64_t &IVBump,
                                                 MachineInstr *&IVOp
                                                 ) const {
  MachineBasicBlock *Header = L->getHeader();
  MachineBasicBlock *Preheader = MLI->findLoopPreheader(L, SpecPreheader);
  MachineBasicBlock *Latch = L->getLoopLatch();
  MachineBasicBlock *ExitingBlock = L->findLoopControlBlock();
  if (!Header || !Preheader || !Latch || !ExitingBlock)
    return false;

  // This pair represents an induction register together with an immediate
  // value that will be added to it in each loop iteration.
  using RegisterBump = std::pair<Register, int64_t>;

  // Mapping:  R.next -> (R, bump), where R, R.next and bump are derived
  // from an induction operation
  //   R.next = R + bump
  // where bump is an immediate value.
  using InductionMap = std::map<Register, RegisterBump>;

  InductionMap IndMap;

  using instr_iterator = MachineBasicBlock::instr_iterator;

  for (instr_iterator I = Header->instr_begin(), E = Header->instr_end();
       I != E && I->isPHI(); ++I) {
    MachineInstr *Phi = &*I;

    // Have a PHI instruction.  Get the operand that corresponds to the
    // latch block, and see if is a result of an addition of form "reg+imm",
    // where the "reg" is defined by the PHI node we are looking at.
    for (unsigned i = 1, n = Phi->getNumOperands(); i < n; i += 2) {
      if (Phi->getOperand(i+1).getMBB() != Latch)
        continue;

      Register PhiOpReg = Phi->getOperand(i).getReg();
      MachineInstr *DI = MRI->getVRegDef(PhiOpReg);

      if (DI->getDesc().isAdd()) {
        // If the register operand to the add is the PHI we're looking at, this
        // meets the induction pattern.
        Register IndReg = DI->getOperand(1).getReg();
        MachineOperand &Opnd2 = DI->getOperand(2);
        int64_t V;
        if (MRI->getVRegDef(IndReg) == Phi && checkForImmediate(Opnd2, V)) {
          Register UpdReg = DI->getOperand(0).getReg();
          IndMap.insert(std::make_pair(UpdReg, std::make_pair(IndReg, V)));
        }
      }
    }  // for (i)
  }  // for (instr)

  SmallVector<MachineOperand,2> Cond;
  MachineBasicBlock *TB = nullptr, *FB = nullptr;
  bool NotAnalyzed = TII->analyzeBranch(*ExitingBlock, TB, FB, Cond, false);
  if (NotAnalyzed)
    return false;

  Register PredR;
  unsigned PredPos, PredRegFlags;
  if (!TII->getPredReg(Cond, PredR, PredPos, PredRegFlags))
    return false;

  MachineInstr *PredI = MRI->getVRegDef(PredR);
  if (!PredI->isCompare())
    return false;

  Register CmpReg1, CmpReg2;
  int64_t CmpImm = 0, CmpMask = 0;
  bool CmpAnalyzed =
      TII->analyzeCompare(*PredI, CmpReg1, CmpReg2, CmpMask, CmpImm);
  // Fail if the compare was not analyzed, or it's not comparing a register
  // with an immediate value.  Not checking the mask here, since we handle
  // the individual compare opcodes (including A4_cmpb*) later on.
  if (!CmpAnalyzed)
    return false;

  // Exactly one of the input registers to the comparison should be among
  // the induction registers.
  InductionMap::iterator IndMapEnd = IndMap.end();
  InductionMap::iterator F = IndMapEnd;
  if (CmpReg1 != 0) {
    InductionMap::iterator F1 = IndMap.find(CmpReg1);
    if (F1 != IndMapEnd)
      F = F1;
  }
  if (CmpReg2 != 0) {
    InductionMap::iterator F2 = IndMap.find(CmpReg2);
    if (F2 != IndMapEnd) {
      if (F != IndMapEnd)
        return false;
      F = F2;
    }
  }
  if (F == IndMapEnd)
    return false;

  Reg = F->second.first;
  IVBump = F->second.second;
  IVOp = MRI->getVRegDef(F->first);
  return true;
}

// Return the comparison kind for the specified opcode.
HexagonHardwareLoops::Comparison::Kind
HexagonHardwareLoops::getComparisonKind(unsigned CondOpc,
                                        MachineOperand *InitialValue,
                                        const MachineOperand *EndValue,
                                        int64_t IVBump) const {
  Comparison::Kind Cmp = (Comparison::Kind)0;
  switch (CondOpc) {
  case Hexagon::C2_cmpeq:
  case Hexagon::C2_cmpeqi:
  case Hexagon::C2_cmpeqp:
    Cmp = Comparison::EQ;
    break;
  case Hexagon::C4_cmpneq:
  case Hexagon::C4_cmpneqi:
    Cmp = Comparison::NE;
    break;
  case Hexagon::C2_cmplt:
    Cmp = Comparison::LTs;
    break;
  case Hexagon::C2_cmpltu:
    Cmp = Comparison::LTu;
    break;
  case Hexagon::C4_cmplte:
  case Hexagon::C4_cmpltei:
    Cmp = Comparison::LEs;
    break;
  case Hexagon::C4_cmplteu:
  case Hexagon::C4_cmplteui:
    Cmp = Comparison::LEu;
    break;
  case Hexagon::C2_cmpgt:
  case Hexagon::C2_cmpgti:
  case Hexagon::C2_cmpgtp:
    Cmp = Comparison::GTs;
    break;
  case Hexagon::C2_cmpgtu:
  case Hexagon::C2_cmpgtui:
  case Hexagon::C2_cmpgtup:
    Cmp = Comparison::GTu;
    break;
  case Hexagon::C2_cmpgei:
    Cmp = Comparison::GEs;
    break;
  case Hexagon::C2_cmpgeui:
    Cmp = Comparison::GEs;
    break;
  default:
    return (Comparison::Kind)0;
  }
  return Cmp;
}

/// Analyze the statements in a loop to determine if the loop has
/// a computable trip count and, if so, return a value that represents
/// the trip count expression.
///
/// This function iterates over the phi nodes in the loop to check for
/// induction variable patterns that are used in the calculation for
/// the number of time the loop is executed.
CountValue *HexagonHardwareLoops::getLoopTripCount(MachineLoop *L,
    SmallVectorImpl<MachineInstr *> &OldInsts) {
  MachineBasicBlock *TopMBB = L->getTopBlock();
  MachineBasicBlock::pred_iterator PI = TopMBB->pred_begin();
  assert(PI != TopMBB->pred_end() &&
         "Loop must have more than one incoming edge!");
  MachineBasicBlock *Backedge = *PI++;
  if (PI == TopMBB->pred_end())  // dead loop?
    return nullptr;
  MachineBasicBlock *Incoming = *PI++;
  if (PI != TopMBB->pred_end())  // multiple backedges?
    return nullptr;

  // Make sure there is one incoming and one backedge and determine which
  // is which.
  if (L->contains(Incoming)) {
    if (L->contains(Backedge))
      return nullptr;
    std::swap(Incoming, Backedge);
  } else if (!L->contains(Backedge))
    return nullptr;

  // Look for the cmp instruction to determine if we can get a useful trip
  // count.  The trip count can be either a register or an immediate.  The
  // location of the value depends upon the type (reg or imm).
  MachineBasicBlock *ExitingBlock = L->findLoopControlBlock();
  if (!ExitingBlock)
    return nullptr;

  Register IVReg = 0;
  int64_t IVBump = 0;
  MachineInstr *IVOp;
  bool FoundIV = findInductionRegister(L, IVReg, IVBump, IVOp);
  if (!FoundIV)
    return nullptr;

  MachineBasicBlock *Preheader = MLI->findLoopPreheader(L, SpecPreheader);

  MachineOperand *InitialValue = nullptr;
  MachineInstr *IV_Phi = MRI->getVRegDef(IVReg);
  MachineBasicBlock *Latch = L->getLoopLatch();
  for (unsigned i = 1, n = IV_Phi->getNumOperands(); i < n; i += 2) {
    MachineBasicBlock *MBB = IV_Phi->getOperand(i+1).getMBB();
    if (MBB == Preheader)
      InitialValue = &IV_Phi->getOperand(i);
    else if (MBB == Latch)
      IVReg = IV_Phi->getOperand(i).getReg();  // Want IV reg after bump.
  }
  if (!InitialValue)
    return nullptr;

  SmallVector<MachineOperand,2> Cond;
  MachineBasicBlock *TB = nullptr, *FB = nullptr;
  bool NotAnalyzed = TII->analyzeBranch(*ExitingBlock, TB, FB, Cond, false);
  if (NotAnalyzed)
    return nullptr;

  MachineBasicBlock *Header = L->getHeader();
  // TB must be non-null.  If FB is also non-null, one of them must be
  // the header.  Otherwise, branch to TB could be exiting the loop, and
  // the fall through can go to the header.
  assert (TB && "Exit block without a branch?");
  if (ExitingBlock != Latch && (TB == Latch || FB == Latch)) {
    MachineBasicBlock *LTB = nullptr, *LFB = nullptr;
    SmallVector<MachineOperand,2> LCond;
    bool NotAnalyzed = TII->analyzeBranch(*Latch, LTB, LFB, LCond, false);
    if (NotAnalyzed)
      return nullptr;
    if (TB == Latch)
      TB = (LTB == Header) ? LTB : LFB;
    else
      FB = (LTB == Header) ? LTB: LFB;
  }
  assert ((!FB || TB == Header || FB == Header) && "Branches not to header?");
  if (!TB || (FB && TB != Header && FB != Header))
    return nullptr;

  // Branches of form "if (!P) ..." cause HexagonInstrInfo::analyzeBranch
  // to put imm(0), followed by P in the vector Cond.
  // If TB is not the header, it means that the "not-taken" path must lead
  // to the header.
  bool Negated = TII->predOpcodeHasNot(Cond) ^ (TB != Header);
  Register PredReg;
  unsigned PredPos, PredRegFlags;
  if (!TII->getPredReg(Cond, PredReg, PredPos, PredRegFlags))
    return nullptr;
  MachineInstr *CondI = MRI->getVRegDef(PredReg);
  unsigned CondOpc = CondI->getOpcode();

  Register CmpReg1, CmpReg2;
  int64_t Mask = 0, ImmValue = 0;
  bool AnalyzedCmp =
      TII->analyzeCompare(*CondI, CmpReg1, CmpReg2, Mask, ImmValue);
  if (!AnalyzedCmp)
    return nullptr;

  // The comparison operator type determines how we compute the loop
  // trip count.
  OldInsts.push_back(CondI);
  OldInsts.push_back(IVOp);

  // Sadly, the following code gets information based on the position
  // of the operands in the compare instruction.  This has to be done
  // this way, because the comparisons check for a specific relationship
  // between the operands (e.g. is-less-than), rather than to find out
  // what relationship the operands are in (as on PPC).
  Comparison::Kind Cmp;
  bool isSwapped = false;
  const MachineOperand &Op1 = CondI->getOperand(1);
  const MachineOperand &Op2 = CondI->getOperand(2);
  const MachineOperand *EndValue = nullptr;

  if (Op1.isReg()) {
    if (Op2.isImm() || Op1.getReg() == IVReg)
      EndValue = &Op2;
    else {
      EndValue = &Op1;
      isSwapped = true;
    }
  }

  if (!EndValue)
    return nullptr;

  Cmp = getComparisonKind(CondOpc, InitialValue, EndValue, IVBump);
  if (!Cmp)
    return nullptr;
  if (Negated)
    Cmp = Comparison::getNegatedComparison(Cmp);
  if (isSwapped)
    Cmp = Comparison::getSwappedComparison(Cmp);

  if (InitialValue->isReg()) {
    Register R = InitialValue->getReg();
    MachineBasicBlock *DefBB = MRI->getVRegDef(R)->getParent();
    if (!MDT->properlyDominates(DefBB, Header)) {
      int64_t V;
      if (!checkForImmediate(*InitialValue, V))
        return nullptr;
    }
    OldInsts.push_back(MRI->getVRegDef(R));
  }
  if (EndValue->isReg()) {
    Register R = EndValue->getReg();
    MachineBasicBlock *DefBB = MRI->getVRegDef(R)->getParent();
    if (!MDT->properlyDominates(DefBB, Header)) {
      int64_t V;
      if (!checkForImmediate(*EndValue, V))
        return nullptr;
    }
    OldInsts.push_back(MRI->getVRegDef(R));
  }

  return computeCount(L, InitialValue, EndValue, IVReg, IVBump, Cmp);
}

/// Helper function that returns the expression that represents the
/// number of times a loop iterates.  The function takes the operands that
/// represent the loop start value, loop end value, and induction value.
/// Based upon these operands, the function attempts to compute the trip count.
CountValue *HexagonHardwareLoops::computeCount(MachineLoop *Loop,
                                               const MachineOperand *Start,
                                               const MachineOperand *End,
                                               Register IVReg,
                                               int64_t IVBump,
                                               Comparison::Kind Cmp) const {
  // Cannot handle comparison EQ, i.e. while (A == B).
  if (Cmp == Comparison::EQ)
    return nullptr;

  // Check if either the start or end values are an assignment of an immediate.
  // If so, use the immediate value rather than the register.
  if (Start->isReg()) {
    const MachineInstr *StartValInstr = MRI->getVRegDef(Start->getReg());
    if (StartValInstr && (StartValInstr->getOpcode() == Hexagon::A2_tfrsi ||
                          StartValInstr->getOpcode() == Hexagon::A2_tfrpi))
      Start = &StartValInstr->getOperand(1);
  }
  if (End->isReg()) {
    const MachineInstr *EndValInstr = MRI->getVRegDef(End->getReg());
    if (EndValInstr && (EndValInstr->getOpcode() == Hexagon::A2_tfrsi ||
                        EndValInstr->getOpcode() == Hexagon::A2_tfrpi))
      End = &EndValInstr->getOperand(1);
  }

  if (!Start->isReg() && !Start->isImm())
    return nullptr;
  if (!End->isReg() && !End->isImm())
    return nullptr;

  bool CmpLess =     Cmp & Comparison::L;
  bool CmpGreater =  Cmp & Comparison::G;
  bool CmpHasEqual = Cmp & Comparison::EQ;

  // Avoid certain wrap-arounds.  This doesn't detect all wrap-arounds.
  if (CmpLess && IVBump < 0)
    // Loop going while iv is "less" with the iv value going down.  Must wrap.
    return nullptr;

  if (CmpGreater && IVBump > 0)
    // Loop going while iv is "greater" with the iv value going up.  Must wrap.
    return nullptr;

  // Phis that may feed into the loop.
  LoopFeederMap LoopFeederPhi;

  // Check if the initial value may be zero and can be decremented in the first
  // iteration. If the value is zero, the endloop instruction will not decrement
  // the loop counter, so we shouldn't generate a hardware loop in this case.
  if (loopCountMayWrapOrUnderFlow(Start, End, Loop->getLoopPreheader(), Loop,
                                  LoopFeederPhi))
      return nullptr;

  if (Start->isImm() && End->isImm()) {
    // Both, start and end are immediates.
    int64_t StartV = Start->getImm();
    int64_t EndV = End->getImm();
    int64_t Dist = EndV - StartV;
    if (Dist == 0)
      return nullptr;

    bool Exact = (Dist % IVBump) == 0;

    if (Cmp == Comparison::NE) {
      if (!Exact)
        return nullptr;
      if ((Dist < 0) ^ (IVBump < 0))
        return nullptr;
    }

    // For comparisons that include the final value (i.e. include equality
    // with the final value), we need to increase the distance by 1.
    if (CmpHasEqual)
      Dist = Dist > 0 ? Dist+1 : Dist-1;

    // For the loop to iterate, CmpLess should imply Dist > 0.  Similarly,
    // CmpGreater should imply Dist < 0.  These conditions could actually
    // fail, for example, in unreachable code (which may still appear to be
    // reachable in the CFG).
    if ((CmpLess && Dist < 0) || (CmpGreater && Dist > 0))
      return nullptr;

    // "Normalized" distance, i.e. with the bump set to +-1.
    int64_t Dist1 = (IVBump > 0) ? (Dist +  (IVBump - 1)) / IVBump
                                 : (-Dist + (-IVBump - 1)) / (-IVBump);
    assert (Dist1 > 0 && "Fishy thing.  Both operands have the same sign.");

    uint64_t Count = Dist1;

    if (Count > 0xFFFFFFFFULL)
      return nullptr;

    return new CountValue(CountValue::CV_Immediate, Count);
  }

  // A general case: Start and End are some values, but the actual
  // iteration count may not be available.  If it is not, insert
  // a computation of it into the preheader.

  // If the induction variable bump is not a power of 2, quit.
  // Othwerise we'd need a general integer division.
  if (!isPowerOf2_64(std::abs(IVBump)))
    return nullptr;

  MachineBasicBlock *PH = MLI->findLoopPreheader(Loop, SpecPreheader);
  assert (PH && "Should have a preheader by now");
  MachineBasicBlock::iterator InsertPos = PH->getFirstTerminator();
  DebugLoc DL;
  if (InsertPos != PH->end())
    DL = InsertPos->getDebugLoc();

  // If Start is an immediate and End is a register, the trip count
  // will be "reg - imm".  Hexagon's "subtract immediate" instruction
  // is actually "reg + -imm".

  // If the loop IV is going downwards, i.e. if the bump is negative,
  // then the iteration count (computed as End-Start) will need to be
  // negated.  To avoid the negation, just swap Start and End.
  if (IVBump < 0) {
    std::swap(Start, End);
    IVBump = -IVBump;
  }
  // Cmp may now have a wrong direction, e.g.  LEs may now be GEs.
  // Signedness, and "including equality" are preserved.

  bool RegToImm = Start->isReg() && End->isImm(); // for (reg..imm)
  bool RegToReg = Start->isReg() && End->isReg(); // for (reg..reg)

  int64_t StartV = 0, EndV = 0;
  if (Start->isImm())
    StartV = Start->getImm();
  if (End->isImm())
    EndV = End->getImm();

  int64_t AdjV = 0;
  // To compute the iteration count, we would need this computation:
  //   Count = (End - Start + (IVBump-1)) / IVBump
  // or, when CmpHasEqual:
  //   Count = (End - Start + (IVBump-1)+1) / IVBump
  // The "IVBump-1" part is the adjustment (AdjV).  We can avoid
  // generating an instruction specifically to add it if we can adjust
  // the immediate values for Start or End.

  if (CmpHasEqual) {
    // Need to add 1 to the total iteration count.
    if (Start->isImm())
      StartV--;
    else if (End->isImm())
      EndV++;
    else
      AdjV += 1;
  }

  if (Cmp != Comparison::NE) {
    if (Start->isImm())
      StartV -= (IVBump-1);
    else if (End->isImm())
      EndV += (IVBump-1);
    else
      AdjV += (IVBump-1);
  }

  Register R = 0;
  unsigned SR = 0;
  if (Start->isReg()) {
    R = Start->getReg();
    SR = Start->getSubReg();
  } else {
    R = End->getReg();
    SR = End->getSubReg();
  }
  const TargetRegisterClass *RC = MRI->getRegClass(R);
  // Hardware loops cannot handle 64-bit registers.  If it's a double
  // register, it has to have a subregister.
  if (!SR && RC == &Hexagon::DoubleRegsRegClass)
    return nullptr;
  const TargetRegisterClass *IntRC = &Hexagon::IntRegsRegClass;

  // Compute DistR (register with the distance between Start and End).
  Register DistR;
  unsigned DistSR;

  // Avoid special case, where the start value is an imm(0).
  if (Start->isImm() && StartV == 0) {
    DistR = End->getReg();
    DistSR = End->getSubReg();
  } else {
    const MCInstrDesc &SubD = RegToReg ? TII->get(Hexagon::A2_sub) :
                              (RegToImm ? TII->get(Hexagon::A2_subri) :
                                          TII->get(Hexagon::A2_addi));
    if (RegToReg || RegToImm) {
      Register SubR = MRI->createVirtualRegister(IntRC);
      MachineInstrBuilder SubIB =
        BuildMI(*PH, InsertPos, DL, SubD, SubR);

      if (RegToReg)
        SubIB.addReg(End->getReg(), 0, End->getSubReg())
          .addReg(Start->getReg(), 0, Start->getSubReg());
      else
        SubIB.addImm(EndV)
          .addReg(Start->getReg(), 0, Start->getSubReg());
      DistR = SubR;
    } else {
      // If the loop has been unrolled, we should use the original loop count
      // instead of recalculating the value. This will avoid additional
      // 'Add' instruction.
      const MachineInstr *EndValInstr = MRI->getVRegDef(End->getReg());
      if (EndValInstr->getOpcode() == Hexagon::A2_addi &&
          EndValInstr->getOperand(1).getSubReg() == 0 &&
          EndValInstr->getOperand(2).getImm() == StartV) {
        DistR = EndValInstr->getOperand(1).getReg();
      } else {
        Register SubR = MRI->createVirtualRegister(IntRC);
        MachineInstrBuilder SubIB =
          BuildMI(*PH, InsertPos, DL, SubD, SubR);
        SubIB.addReg(End->getReg(), 0, End->getSubReg())
             .addImm(-StartV);
        DistR = SubR;
      }
    }
    DistSR = 0;
  }

  // From DistR, compute AdjR (register with the adjusted distance).
  Register AdjR;
  unsigned AdjSR;

  if (AdjV == 0) {
    AdjR = DistR;
    AdjSR = DistSR;
  } else {
    // Generate CountR = ADD DistR, AdjVal
    Register AddR = MRI->createVirtualRegister(IntRC);
    MCInstrDesc const &AddD = TII->get(Hexagon::A2_addi);
    BuildMI(*PH, InsertPos, DL, AddD, AddR)
      .addReg(DistR, 0, DistSR)
      .addImm(AdjV);

    AdjR = AddR;
    AdjSR = 0;
  }

  // From AdjR, compute CountR (register with the final count).
  Register CountR;
  unsigned CountSR;

  if (IVBump == 1) {
    CountR = AdjR;
    CountSR = AdjSR;
  } else {
    // The IV bump is a power of two. Log_2(IV bump) is the shift amount.
    unsigned Shift = Log2_32(IVBump);

    // Generate NormR = LSR DistR, Shift.
    Register LsrR = MRI->createVirtualRegister(IntRC);
    const MCInstrDesc &LsrD = TII->get(Hexagon::S2_lsr_i_r);
    BuildMI(*PH, InsertPos, DL, LsrD, LsrR)
      .addReg(AdjR, 0, AdjSR)
      .addImm(Shift);

    CountR = LsrR;
    CountSR = 0;
  }

  return new CountValue(CountValue::CV_Register, CountR, CountSR);
}

/// Return true if the operation is invalid within hardware loop.
bool HexagonHardwareLoops::isInvalidLoopOperation(const MachineInstr *MI,
                                                  bool IsInnerHWLoop) const {
  // Call is not allowed because the callee may use a hardware loop except for
  // the case when the call never returns.
  if (MI->getDesc().isCall())
    return !TII->doesNotReturn(*MI);

  // Check if the instruction defines a hardware loop register.
  using namespace Hexagon;

  static const Register Regs01[] = { LC0, SA0, LC1, SA1 };
  static const Register Regs1[]  = { LC1, SA1 };
  auto CheckRegs = IsInnerHWLoop ? ArrayRef(Regs01) : ArrayRef(Regs1);
  for (Register R : CheckRegs)
    if (MI->modifiesRegister(R, TRI))
      return true;

  return false;
}

/// Return true if the loop contains an instruction that inhibits
/// the use of the hardware loop instruction.
bool HexagonHardwareLoops::containsInvalidInstruction(MachineLoop *L,
    bool IsInnerHWLoop) const {
  LLVM_DEBUG(dbgs() << "\nhw_loop head, "
                    << printMBBReference(**L->block_begin()));
  for (MachineBasicBlock *MBB : L->getBlocks()) {
    for (const MachineInstr &MI : *MBB) {
      if (isInvalidLoopOperation(&MI, IsInnerHWLoop)) {
        LLVM_DEBUG(dbgs() << "\nCannot convert to hw_loop due to:";
                   MI.dump(););
        return true;
      }
    }
  }
  return false;
}

/// Returns true if the instruction is dead.  This was essentially
/// copied from DeadMachineInstructionElim::isDead, but with special cases
/// for inline asm, physical registers and instructions with side effects
/// removed.
bool HexagonHardwareLoops::isDead(const MachineInstr *MI,
                              SmallVectorImpl<MachineInstr *> &DeadPhis) const {
  // Examine each operand.
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;

    Register Reg = MO.getReg();
    if (MRI->use_nodbg_empty(Reg))
      continue;

    using use_nodbg_iterator = MachineRegisterInfo::use_nodbg_iterator;

    // This instruction has users, but if the only user is the phi node for the
    // parent block, and the only use of that phi node is this instruction, then
    // this instruction is dead: both it (and the phi node) can be removed.
    use_nodbg_iterator I = MRI->use_nodbg_begin(Reg);
    use_nodbg_iterator End = MRI->use_nodbg_end();
    if (std::next(I) != End || !I->getParent()->isPHI())
      return false;

    MachineInstr *OnePhi = I->getParent();
    for (const MachineOperand &OPO : OnePhi->operands()) {
      if (!OPO.isReg() || !OPO.isDef())
        continue;

      Register OPReg = OPO.getReg();
      use_nodbg_iterator nextJ;
      for (use_nodbg_iterator J = MRI->use_nodbg_begin(OPReg);
           J != End; J = nextJ) {
        nextJ = std::next(J);
        MachineOperand &Use = *J;
        MachineInstr *UseMI = Use.getParent();

        // If the phi node has a user that is not MI, bail.
        if (MI != UseMI)
          return false;
      }
    }
    DeadPhis.push_back(OnePhi);
  }

  // If there are no defs with uses, the instruction is dead.
  return true;
}

void HexagonHardwareLoops::removeIfDead(MachineInstr *MI) {
  // This procedure was essentially copied from DeadMachineInstructionElim.

  SmallVector<MachineInstr*, 1> DeadPhis;
  if (isDead(MI, DeadPhis)) {
    LLVM_DEBUG(dbgs() << "HW looping will remove: " << *MI);

    // It is possible that some DBG_VALUE instructions refer to this
    // instruction.  Examine each def operand for such references;
    // if found, mark the DBG_VALUE as undef (but don't delete it).
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef())
        continue;
      Register Reg = MO.getReg();
      // We use make_early_inc_range here because setReg below invalidates the
      // iterator.
      for (MachineOperand &MO :
           llvm::make_early_inc_range(MRI->use_operands(Reg))) {
        MachineInstr *UseMI = MO.getParent();
        if (UseMI == MI)
          continue;
        if (MO.isDebug())
          MO.setReg(0U);
      }
    }

    MI->eraseFromParent();
    for (unsigned i = 0; i < DeadPhis.size(); ++i)
      DeadPhis[i]->eraseFromParent();
  }
}

/// Check if the loop is a candidate for converting to a hardware
/// loop.  If so, then perform the transformation.
///
/// This function works on innermost loops first.  A loop can be converted
/// if it is a counting loop; either a register value or an immediate.
///
/// The code makes several assumptions about the representation of the loop
/// in llvm.
bool HexagonHardwareLoops::convertToHardwareLoop(MachineLoop *L,
                                                 bool &RecL0used,
                                                 bool &RecL1used) {
  // This is just to confirm basic correctness.
  assert(L->getHeader() && "Loop without a header?");

  bool Changed = false;
  bool L0Used = false;
  bool L1Used = false;

  // Process nested loops first.
  for (MachineLoop *I : *L) {
    Changed |= convertToHardwareLoop(I, RecL0used, RecL1used);
    L0Used |= RecL0used;
    L1Used |= RecL1used;
  }

  // If a nested loop has been converted, then we can't convert this loop.
  if (Changed && L0Used && L1Used)
    return Changed;

  unsigned LOOP_i;
  unsigned LOOP_r;
  unsigned ENDLOOP;

  // Flag used to track loopN instruction:
  // 1 - Hardware loop is being generated for the inner most loop.
  // 0 - Hardware loop is being generated for the outer loop.
  unsigned IsInnerHWLoop = 1;

  if (L0Used) {
    LOOP_i = Hexagon::J2_loop1i;
    LOOP_r = Hexagon::J2_loop1r;
    ENDLOOP = Hexagon::ENDLOOP1;
    IsInnerHWLoop = 0;
  } else {
    LOOP_i = Hexagon::J2_loop0i;
    LOOP_r = Hexagon::J2_loop0r;
    ENDLOOP = Hexagon::ENDLOOP0;
  }

#ifndef NDEBUG
  // Stop trying after reaching the limit (if any).
  int Limit = HWLoopLimit;
  if (Limit >= 0) {
    if (Counter >= HWLoopLimit)
      return false;
    Counter++;
  }
#endif

  // Does the loop contain any invalid instructions?
  if (containsInvalidInstruction(L, IsInnerHWLoop))
    return false;

  MachineBasicBlock *LastMBB = L->findLoopControlBlock();
  // Don't generate hw loop if the loop has more than one exit.
  if (!LastMBB)
    return false;

  MachineBasicBlock::iterator LastI = LastMBB->getFirstTerminator();
  if (LastI == LastMBB->end())
    return false;

  // Is the induction variable bump feeding the latch condition?
  if (!fixupInductionVariable(L))
    return false;

  // Ensure the loop has a preheader: the loop instruction will be
  // placed there.
  MachineBasicBlock *Preheader = MLI->findLoopPreheader(L, SpecPreheader);
  if (!Preheader) {
    Preheader = createPreheaderForLoop(L);
    if (!Preheader)
      return false;
  }

  MachineBasicBlock::iterator InsertPos = Preheader->getFirstTerminator();

  SmallVector<MachineInstr*, 2> OldInsts;
  // Are we able to determine the trip count for the loop?
  CountValue *TripCount = getLoopTripCount(L, OldInsts);
  if (!TripCount)
    return false;

  // Is the trip count available in the preheader?
  if (TripCount->isReg()) {
    // There will be a use of the register inserted into the preheader,
    // so make sure that the register is actually defined at that point.
    MachineInstr *TCDef = MRI->getVRegDef(TripCount->getReg());
    MachineBasicBlock *BBDef = TCDef->getParent();
    if (!MDT->dominates(BBDef, Preheader))
      return false;
  }

  // Determine the loop start.
  MachineBasicBlock *TopBlock = L->getTopBlock();
  MachineBasicBlock *ExitingBlock = L->findLoopControlBlock();
  MachineBasicBlock *LoopStart = nullptr;
  if (ExitingBlock !=  L->getLoopLatch()) {
    MachineBasicBlock *TB = nullptr, *FB = nullptr;
    SmallVector<MachineOperand, 2> Cond;

    if (TII->analyzeBranch(*ExitingBlock, TB, FB, Cond, false))
      return false;

    if (L->contains(TB))
      LoopStart = TB;
    else if (L->contains(FB))
      LoopStart = FB;
    else
      return false;
  }
  else
    LoopStart = TopBlock;

  // Convert the loop to a hardware loop.
  LLVM_DEBUG(dbgs() << "Change to hardware loop at "; L->dump());
  DebugLoc DL;
  if (InsertPos != Preheader->end())
    DL = InsertPos->getDebugLoc();

  if (TripCount->isReg()) {
    // Create a copy of the loop count register.
    Register CountReg = MRI->createVirtualRegister(&Hexagon::IntRegsRegClass);
    BuildMI(*Preheader, InsertPos, DL, TII->get(TargetOpcode::COPY), CountReg)
      .addReg(TripCount->getReg(), 0, TripCount->getSubReg());
    // Add the Loop instruction to the beginning of the loop.
    BuildMI(*Preheader, InsertPos, DL, TII->get(LOOP_r)).addMBB(LoopStart)
      .addReg(CountReg);
  } else {
    assert(TripCount->isImm() && "Expecting immediate value for trip count");
    // Add the Loop immediate instruction to the beginning of the loop,
    // if the immediate fits in the instructions.  Otherwise, we need to
    // create a new virtual register.
    int64_t CountImm = TripCount->getImm();
    if (!TII->isValidOffset(LOOP_i, CountImm, TRI)) {
      Register CountReg = MRI->createVirtualRegister(&Hexagon::IntRegsRegClass);
      BuildMI(*Preheader, InsertPos, DL, TII->get(Hexagon::A2_tfrsi), CountReg)
        .addImm(CountImm);
      BuildMI(*Preheader, InsertPos, DL, TII->get(LOOP_r))
        .addMBB(LoopStart).addReg(CountReg);
    } else
      BuildMI(*Preheader, InsertPos, DL, TII->get(LOOP_i))
        .addMBB(LoopStart).addImm(CountImm);
  }

  // Make sure the loop start always has a reference in the CFG.
  LoopStart->setMachineBlockAddressTaken();

  // Replace the loop branch with an endloop instruction.
  DebugLoc LastIDL = LastI->getDebugLoc();
  BuildMI(*LastMBB, LastI, LastIDL, TII->get(ENDLOOP)).addMBB(LoopStart);

  // The loop ends with either:
  //  - a conditional branch followed by an unconditional branch, or
  //  - a conditional branch to the loop start.
  if (LastI->getOpcode() == Hexagon::J2_jumpt ||
      LastI->getOpcode() == Hexagon::J2_jumpf) {
    // Delete one and change/add an uncond. branch to out of the loop.
    MachineBasicBlock *BranchTarget = LastI->getOperand(1).getMBB();
    LastI = LastMBB->erase(LastI);
    if (!L->contains(BranchTarget)) {
      if (LastI != LastMBB->end())
        LastI = LastMBB->erase(LastI);
      SmallVector<MachineOperand, 0> Cond;
      TII->insertBranch(*LastMBB, BranchTarget, nullptr, Cond, LastIDL);
    }
  } else {
    // Conditional branch to loop start; just delete it.
    LastMBB->erase(LastI);
  }
  delete TripCount;

  // The induction operation and the comparison may now be
  // unneeded. If these are unneeded, then remove them.
  for (unsigned i = 0; i < OldInsts.size(); ++i)
    removeIfDead(OldInsts[i]);

  ++NumHWLoops;

  // Set RecL1used and RecL0used only after hardware loop has been
  // successfully generated. Doing it earlier can cause wrong loop instruction
  // to be used.
  if (L0Used) // Loop0 was already used. So, the correct loop must be loop1.
    RecL1used = true;
  else
    RecL0used = true;

  return true;
}

bool HexagonHardwareLoops::orderBumpCompare(MachineInstr *BumpI,
                                            MachineInstr *CmpI) {
  assert (BumpI != CmpI && "Bump and compare in the same instruction?");

  MachineBasicBlock *BB = BumpI->getParent();
  if (CmpI->getParent() != BB)
    return false;

  using instr_iterator = MachineBasicBlock::instr_iterator;

  // Check if things are in order to begin with.
  for (instr_iterator I(BumpI), E = BB->instr_end(); I != E; ++I)
    if (&*I == CmpI)
      return true;

  // Out of order.
  Register PredR = CmpI->getOperand(0).getReg();
  bool FoundBump = false;
  instr_iterator CmpIt = CmpI->getIterator(), NextIt = std::next(CmpIt);
  for (instr_iterator I = NextIt, E = BB->instr_end(); I != E; ++I) {
    MachineInstr *In = &*I;
    for (unsigned i = 0, n = In->getNumOperands(); i < n; ++i) {
      MachineOperand &MO = In->getOperand(i);
      if (MO.isReg() && MO.isUse()) {
        if (MO.getReg() == PredR)  // Found an intervening use of PredR.
          return false;
      }
    }

    if (In == BumpI) {
      BB->splice(++BumpI->getIterator(), BB, CmpI->getIterator());
      FoundBump = true;
      break;
    }
  }
  assert (FoundBump && "Cannot determine instruction order");
  return FoundBump;
}

/// This function is required to break recursion. Visiting phis in a loop may
/// result in recursion during compilation. We break the recursion by making
/// sure that we visit a MachineOperand and its definition in a
/// MachineInstruction only once. If we attempt to visit more than once, then
/// there is recursion, and will return false.
bool HexagonHardwareLoops::isLoopFeeder(MachineLoop *L, MachineBasicBlock *A,
                                        MachineInstr *MI,
                                        const MachineOperand *MO,
                                        LoopFeederMap &LoopFeederPhi) const {
  if (LoopFeederPhi.find(MO->getReg()) == LoopFeederPhi.end()) {
    LLVM_DEBUG(dbgs() << "\nhw_loop head, "
                      << printMBBReference(**L->block_begin()));
    // Ignore all BBs that form Loop.
    if (llvm::is_contained(L->getBlocks(), A))
      return false;
    MachineInstr *Def = MRI->getVRegDef(MO->getReg());
    LoopFeederPhi.insert(std::make_pair(MO->getReg(), Def));
    return true;
  } else
    // Already visited node.
    return false;
}

/// Return true if a Phi may generate a value that can underflow.
/// This function calls loopCountMayWrapOrUnderFlow for each Phi operand.
bool HexagonHardwareLoops::phiMayWrapOrUnderflow(
    MachineInstr *Phi, const MachineOperand *EndVal, MachineBasicBlock *MBB,
    MachineLoop *L, LoopFeederMap &LoopFeederPhi) const {
  assert(Phi->isPHI() && "Expecting a Phi.");
  // Walk through each Phi, and its used operands. Make sure that
  // if there is recursion in Phi, we won't generate hardware loops.
  for (int i = 1, n = Phi->getNumOperands(); i < n; i += 2)
    if (isLoopFeeder(L, MBB, Phi, &(Phi->getOperand(i)), LoopFeederPhi))
      if (loopCountMayWrapOrUnderFlow(&(Phi->getOperand(i)), EndVal,
                                      Phi->getParent(), L, LoopFeederPhi))
        return true;
  return false;
}

/// Return true if the induction variable can underflow in the first iteration.
/// An example, is an initial unsigned value that is 0 and is decrement in the
/// first itertion of a do-while loop.  In this case, we cannot generate a
/// hardware loop because the endloop instruction does not decrement the loop
/// counter if it is <= 1. We only need to perform this analysis if the
/// initial value is a register.
///
/// This function assumes the initial value may underfow unless proven
/// otherwise. If the type is signed, then we don't care because signed
/// underflow is undefined. We attempt to prove the initial value is not
/// zero by perfoming a crude analysis of the loop counter. This function
/// checks if the initial value is used in any comparison prior to the loop
/// and, if so, assumes the comparison is a range check. This is inexact,
/// but will catch the simple cases.
bool HexagonHardwareLoops::loopCountMayWrapOrUnderFlow(
    const MachineOperand *InitVal, const MachineOperand *EndVal,
    MachineBasicBlock *MBB, MachineLoop *L,
    LoopFeederMap &LoopFeederPhi) const {
  // Only check register values since they are unknown.
  if (!InitVal->isReg())
    return false;

  if (!EndVal->isImm())
    return false;

  // A register value that is assigned an immediate is a known value, and it
  // won't underflow in the first iteration.
  int64_t Imm;
  if (checkForImmediate(*InitVal, Imm))
    return (EndVal->getImm() == Imm);

  Register Reg = InitVal->getReg();

  // We don't know the value of a physical register.
  if (!Reg.isVirtual())
    return true;

  MachineInstr *Def = MRI->getVRegDef(Reg);
  if (!Def)
    return true;

  // If the initial value is a Phi or copy and the operands may not underflow,
  // then the definition cannot be underflow either.
  if (Def->isPHI() && !phiMayWrapOrUnderflow(Def, EndVal, Def->getParent(),
                                             L, LoopFeederPhi))
    return false;
  if (Def->isCopy() && !loopCountMayWrapOrUnderFlow(&(Def->getOperand(1)),
                                                    EndVal, Def->getParent(),
                                                    L, LoopFeederPhi))
    return false;

  // Iterate over the uses of the initial value. If the initial value is used
  // in a compare, then we assume this is a range check that ensures the loop
  // doesn't underflow. This is not an exact test and should be improved.
  for (MachineRegisterInfo::use_instr_nodbg_iterator I = MRI->use_instr_nodbg_begin(Reg),
         E = MRI->use_instr_nodbg_end(); I != E; ++I) {
    MachineInstr *MI = &*I;
    Register CmpReg1, CmpReg2;
    int64_t CmpMask = 0, CmpValue = 0;

    if (!TII->analyzeCompare(*MI, CmpReg1, CmpReg2, CmpMask, CmpValue))
      continue;

    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    SmallVector<MachineOperand, 2> Cond;
    if (TII->analyzeBranch(*MI->getParent(), TBB, FBB, Cond, false))
      continue;

    Comparison::Kind Cmp =
        getComparisonKind(MI->getOpcode(), nullptr, nullptr, 0);
    if (Cmp == 0)
      continue;
    if (TII->predOpcodeHasNot(Cond) ^ (TBB != MBB))
      Cmp = Comparison::getNegatedComparison(Cmp);
    if (CmpReg2 != 0 && CmpReg2 == Reg)
      Cmp = Comparison::getSwappedComparison(Cmp);

    // Signed underflow is undefined.
    if (Comparison::isSigned(Cmp))
      return false;

    // Check if there is a comparison of the initial value. If the initial value
    // is greater than or not equal to another value, then assume this is a
    // range check.
    if ((Cmp & Comparison::G) || Cmp == Comparison::NE)
      return false;
  }

  // OK - this is a hack that needs to be improved. We really need to analyze
  // the instructions performed on the initial value. This works on the simplest
  // cases only.
  if (!Def->isCopy() && !Def->isPHI())
    return false;

  return true;
}

bool HexagonHardwareLoops::checkForImmediate(const MachineOperand &MO,
                                             int64_t &Val) const {
  if (MO.isImm()) {
    Val = MO.getImm();
    return true;
  }
  if (!MO.isReg())
    return false;

  // MO is a register. Check whether it is defined as an immediate value,
  // and if so, get the value of it in TV. That value will then need to be
  // processed to handle potential subregisters in MO.
  int64_t TV;

  Register R = MO.getReg();
  if (!R.isVirtual())
    return false;
  MachineInstr *DI = MRI->getVRegDef(R);
  unsigned DOpc = DI->getOpcode();
  switch (DOpc) {
    case TargetOpcode::COPY:
    case Hexagon::A2_tfrsi:
    case Hexagon::A2_tfrpi:
    case Hexagon::CONST32:
    case Hexagon::CONST64:
      // Call recursively to avoid an extra check whether operand(1) is
      // indeed an immediate (it could be a global address, for example),
      // plus we can handle COPY at the same time.
      if (!checkForImmediate(DI->getOperand(1), TV))
        return false;
      break;
    case Hexagon::A2_combineii:
    case Hexagon::A4_combineir:
    case Hexagon::A4_combineii:
    case Hexagon::A4_combineri:
    case Hexagon::A2_combinew: {
      const MachineOperand &S1 = DI->getOperand(1);
      const MachineOperand &S2 = DI->getOperand(2);
      int64_t V1, V2;
      if (!checkForImmediate(S1, V1) || !checkForImmediate(S2, V2))
        return false;
      TV = V2 | (static_cast<uint64_t>(V1) << 32);
      break;
    }
    case TargetOpcode::REG_SEQUENCE: {
      const MachineOperand &S1 = DI->getOperand(1);
      const MachineOperand &S3 = DI->getOperand(3);
      int64_t V1, V3;
      if (!checkForImmediate(S1, V1) || !checkForImmediate(S3, V3))
        return false;
      unsigned Sub2 = DI->getOperand(2).getImm();
      unsigned Sub4 = DI->getOperand(4).getImm();
      if (Sub2 == Hexagon::isub_lo && Sub4 == Hexagon::isub_hi)
        TV = V1 | (V3 << 32);
      else if (Sub2 == Hexagon::isub_hi && Sub4 == Hexagon::isub_lo)
        TV = V3 | (V1 << 32);
      else
        llvm_unreachable("Unexpected form of REG_SEQUENCE");
      break;
    }

    default:
      return false;
  }

  // By now, we should have successfully obtained the immediate value defining
  // the register referenced in MO. Handle a potential use of a subregister.
  switch (MO.getSubReg()) {
    case Hexagon::isub_lo:
      Val = TV & 0xFFFFFFFFULL;
      break;
    case Hexagon::isub_hi:
      Val = (TV >> 32) & 0xFFFFFFFFULL;
      break;
    default:
      Val = TV;
      break;
  }
  return true;
}

void HexagonHardwareLoops::setImmediate(MachineOperand &MO, int64_t Val) {
  if (MO.isImm()) {
    MO.setImm(Val);
    return;
  }

  assert(MO.isReg());
  Register R = MO.getReg();
  MachineInstr *DI = MRI->getVRegDef(R);

  const TargetRegisterClass *RC = MRI->getRegClass(R);
  Register NewR = MRI->createVirtualRegister(RC);
  MachineBasicBlock &B = *DI->getParent();
  DebugLoc DL = DI->getDebugLoc();
  BuildMI(B, DI, DL, TII->get(DI->getOpcode()), NewR).addImm(Val);
  MO.setReg(NewR);
}

bool HexagonHardwareLoops::fixupInductionVariable(MachineLoop *L) {
  MachineBasicBlock *Header = L->getHeader();
  MachineBasicBlock *Latch = L->getLoopLatch();
  MachineBasicBlock *ExitingBlock = L->findLoopControlBlock();

  if (!(Header && Latch && ExitingBlock))
    return false;

  // These data structures follow the same concept as the corresponding
  // ones in findInductionRegister (where some comments are).
  using RegisterBump = std::pair<Register, int64_t>;
  using RegisterInduction = std::pair<Register, RegisterBump>;
  using RegisterInductionSet = std::set<RegisterInduction>;

  // Register candidates for induction variables, with their associated bumps.
  RegisterInductionSet IndRegs;

  // Look for induction patterns:
  //   %1 = PHI ..., [ latch, %2 ]
  //   %2 = ADD %1, imm
  using instr_iterator = MachineBasicBlock::instr_iterator;

  for (instr_iterator I = Header->instr_begin(), E = Header->instr_end();
       I != E && I->isPHI(); ++I) {
    MachineInstr *Phi = &*I;

    // Have a PHI instruction.
    for (unsigned i = 1, n = Phi->getNumOperands(); i < n; i += 2) {
      if (Phi->getOperand(i+1).getMBB() != Latch)
        continue;

      Register PhiReg = Phi->getOperand(i).getReg();
      MachineInstr *DI = MRI->getVRegDef(PhiReg);

      if (DI->getDesc().isAdd()) {
        // If the register operand to the add/sub is the PHI we are looking
        // at, this meets the induction pattern.
        Register IndReg = DI->getOperand(1).getReg();
        MachineOperand &Opnd2 = DI->getOperand(2);
        int64_t V;
        if (MRI->getVRegDef(IndReg) == Phi && checkForImmediate(Opnd2, V)) {
          Register UpdReg = DI->getOperand(0).getReg();
          IndRegs.insert(std::make_pair(UpdReg, std::make_pair(IndReg, V)));
        }
      }
    }  // for (i)
  }  // for (instr)

  if (IndRegs.empty())
    return false;

  MachineBasicBlock *TB = nullptr, *FB = nullptr;
  SmallVector<MachineOperand,2> Cond;
  // analyzeBranch returns true if it fails to analyze branch.
  bool NotAnalyzed = TII->analyzeBranch(*ExitingBlock, TB, FB, Cond, false);
  if (NotAnalyzed || Cond.empty())
    return false;

  if (ExitingBlock != Latch && (TB == Latch || FB == Latch)) {
    MachineBasicBlock *LTB = nullptr, *LFB = nullptr;
    SmallVector<MachineOperand,2> LCond;
    bool NotAnalyzed = TII->analyzeBranch(*Latch, LTB, LFB, LCond, false);
    if (NotAnalyzed)
      return false;

    // Since latch is not the exiting block, the latch branch should be an
    // unconditional branch to the loop header.
    if (TB == Latch)
      TB = (LTB == Header) ? LTB : LFB;
    else
      FB = (LTB == Header) ? LTB : LFB;
  }
  if (TB != Header) {
    if (FB != Header) {
      // The latch/exit block does not go back to the header.
      return false;
    }
    // FB is the header (i.e., uncond. jump to branch header)
    // In this case, the LoopBody -> TB should not be a back edge otherwise
    // it could result in an infinite loop after conversion to hw_loop.
    // This case can happen when the Latch has two jumps like this:
    // Jmp_c OuterLoopHeader <-- TB
    // Jmp   InnerLoopHeader <-- FB
    if (MDT->dominates(TB, FB))
      return false;
  }

  // Expecting a predicate register as a condition.  It won't be a hardware
  // predicate register at this point yet, just a vreg.
  // HexagonInstrInfo::analyzeBranch for negated branches inserts imm(0)
  // into Cond, followed by the predicate register.  For non-negated branches
  // it's just the register.
  unsigned CSz = Cond.size();
  if (CSz != 1 && CSz != 2)
    return false;

  if (!Cond[CSz-1].isReg())
    return false;

  Register P = Cond[CSz - 1].getReg();
  MachineInstr *PredDef = MRI->getVRegDef(P);

  if (!PredDef->isCompare())
    return false;

  SmallSet<Register,2> CmpRegs;
  MachineOperand *CmpImmOp = nullptr;

  // Go over all operands to the compare and look for immediate and register
  // operands.  Assume that if the compare has a single register use and a
  // single immediate operand, then the register is being compared with the
  // immediate value.
  for (MachineOperand &MO : PredDef->operands()) {
    if (MO.isReg()) {
      // Skip all implicit references.  In one case there was:
      //   %140 = FCMPUGT32_rr %138, %139, implicit %usr
      if (MO.isImplicit())
        continue;
      if (MO.isUse()) {
        if (!isImmediate(MO)) {
          CmpRegs.insert(MO.getReg());
          continue;
        }
        // Consider the register to be the "immediate" operand.
        if (CmpImmOp)
          return false;
        CmpImmOp = &MO;
      }
    } else if (MO.isImm()) {
      if (CmpImmOp)    // A second immediate argument?  Confusing.  Bail out.
        return false;
      CmpImmOp = &MO;
    }
  }

  if (CmpRegs.empty())
    return false;

  // Check if the compared register follows the order we want.  Fix if needed.
  for (RegisterInductionSet::iterator I = IndRegs.begin(), E = IndRegs.end();
       I != E; ++I) {
    // This is a success.  If the register used in the comparison is one that
    // we have identified as a bumped (updated) induction register, there is
    // nothing to do.
    if (CmpRegs.count(I->first))
      return true;

    // Otherwise, if the register being compared comes out of a PHI node,
    // and has been recognized as following the induction pattern, and is
    // compared against an immediate, we can fix it.
    const RegisterBump &RB = I->second;
    if (CmpRegs.count(RB.first)) {
      if (!CmpImmOp) {
        // If both operands to the compare instruction are registers, see if
        // it can be changed to use induction register as one of the operands.
        MachineInstr *IndI = nullptr;
        MachineInstr *nonIndI = nullptr;
        MachineOperand *IndMO = nullptr;
        MachineOperand *nonIndMO = nullptr;

        for (unsigned i = 1, n = PredDef->getNumOperands(); i < n; ++i) {
          MachineOperand &MO = PredDef->getOperand(i);
          if (MO.isReg() && MO.getReg() == RB.first) {
            LLVM_DEBUG(dbgs() << "\n DefMI(" << i
                              << ") = " << *(MRI->getVRegDef(I->first)));
            if (IndI)
              return false;

            IndI = MRI->getVRegDef(I->first);
            IndMO = &MO;
          } else if (MO.isReg()) {
            LLVM_DEBUG(dbgs() << "\n DefMI(" << i
                              << ") = " << *(MRI->getVRegDef(MO.getReg())));
            if (nonIndI)
              return false;

            nonIndI = MRI->getVRegDef(MO.getReg());
            nonIndMO = &MO;
          }
        }
        if (IndI && nonIndI &&
            nonIndI->getOpcode() == Hexagon::A2_addi &&
            nonIndI->getOperand(2).isImm() &&
            nonIndI->getOperand(2).getImm() == - RB.second) {
          bool Order = orderBumpCompare(IndI, PredDef);
          if (Order) {
            IndMO->setReg(I->first);
            nonIndMO->setReg(nonIndI->getOperand(1).getReg());
            return true;
          }
        }
        return false;
      }

      // It is not valid to do this transformation on an unsigned comparison
      // because it may underflow.
      Comparison::Kind Cmp =
          getComparisonKind(PredDef->getOpcode(), nullptr, nullptr, 0);
      if (!Cmp || Comparison::isUnsigned(Cmp))
        return false;

      // If the register is being compared against an immediate, try changing
      // the compare instruction to use induction register and adjust the
      // immediate operand.
      int64_t CmpImm = getImmediate(*CmpImmOp);
      int64_t V = RB.second;
      // Handle Overflow (64-bit).
      if (((V > 0) && (CmpImm > INT64_MAX - V)) ||
          ((V < 0) && (CmpImm < INT64_MIN - V)))
        return false;
      CmpImm += V;
      // Most comparisons of register against an immediate value allow
      // the immediate to be constant-extended. There are some exceptions
      // though. Make sure the new combination will work.
      if (CmpImmOp->isImm() && !TII->isExtendable(*PredDef) &&
          !TII->isValidOffset(PredDef->getOpcode(), CmpImm, TRI, false))
        return false;

      // Make sure that the compare happens after the bump.  Otherwise,
      // after the fixup, the compare would use a yet-undefined register.
      MachineInstr *BumpI = MRI->getVRegDef(I->first);
      bool Order = orderBumpCompare(BumpI, PredDef);
      if (!Order)
        return false;

      // Finally, fix the compare instruction.
      setImmediate(*CmpImmOp, CmpImm);
      for (MachineOperand &MO : PredDef->operands()) {
        if (MO.isReg() && MO.getReg() == RB.first) {
          MO.setReg(I->first);
          return true;
        }
      }
    }
  }

  return false;
}

/// createPreheaderForLoop - Create a preheader for a given loop.
MachineBasicBlock *HexagonHardwareLoops::createPreheaderForLoop(
      MachineLoop *L) {
  if (MachineBasicBlock *TmpPH = MLI->findLoopPreheader(L, SpecPreheader))
    return TmpPH;
  if (!HWCreatePreheader)
    return nullptr;

  MachineBasicBlock *Header = L->getHeader();
  MachineBasicBlock *Latch = L->getLoopLatch();
  MachineBasicBlock *ExitingBlock = L->findLoopControlBlock();
  MachineFunction *MF = Header->getParent();
  DebugLoc DL;

#ifndef NDEBUG
  if ((!PHFn.empty()) && (PHFn != MF->getName()))
    return nullptr;
#endif

  if (!Latch || !ExitingBlock || Header->hasAddressTaken())
    return nullptr;

  using instr_iterator = MachineBasicBlock::instr_iterator;

  // Verify that all existing predecessors have analyzable branches
  // (or no branches at all).
  using MBBVector = std::vector<MachineBasicBlock *>;

  MBBVector Preds(Header->pred_begin(), Header->pred_end());
  SmallVector<MachineOperand,2> Tmp1;
  MachineBasicBlock *TB = nullptr, *FB = nullptr;

  if (TII->analyzeBranch(*ExitingBlock, TB, FB, Tmp1, false))
    return nullptr;

  for (MachineBasicBlock *PB : Preds) {
    bool NotAnalyzed = TII->analyzeBranch(*PB, TB, FB, Tmp1, false);
    if (NotAnalyzed)
      return nullptr;
  }

  MachineBasicBlock *NewPH = MF->CreateMachineBasicBlock();
  MF->insert(Header->getIterator(), NewPH);

  if (Header->pred_size() > 2) {
    // Ensure that the header has only two predecessors: the preheader and
    // the loop latch.  Any additional predecessors of the header should
    // join at the newly created preheader. Inspect all PHI nodes from the
    // header and create appropriate corresponding PHI nodes in the preheader.

    for (instr_iterator I = Header->instr_begin(), E = Header->instr_end();
         I != E && I->isPHI(); ++I) {
      MachineInstr *PN = &*I;

      const MCInstrDesc &PD = TII->get(TargetOpcode::PHI);
      MachineInstr *NewPN = MF->CreateMachineInstr(PD, DL);
      NewPH->insert(NewPH->end(), NewPN);

      Register PR = PN->getOperand(0).getReg();
      const TargetRegisterClass *RC = MRI->getRegClass(PR);
      Register NewPR = MRI->createVirtualRegister(RC);
      NewPN->addOperand(MachineOperand::CreateReg(NewPR, true));

      // Copy all non-latch operands of a header's PHI node to the newly
      // created PHI node in the preheader.
      for (unsigned i = 1, n = PN->getNumOperands(); i < n; i += 2) {
        Register PredR = PN->getOperand(i).getReg();
        unsigned PredRSub = PN->getOperand(i).getSubReg();
        MachineBasicBlock *PredB = PN->getOperand(i+1).getMBB();
        if (PredB == Latch)
          continue;

        MachineOperand MO = MachineOperand::CreateReg(PredR, false);
        MO.setSubReg(PredRSub);
        NewPN->addOperand(MO);
        NewPN->addOperand(MachineOperand::CreateMBB(PredB));
      }

      // Remove copied operands from the old PHI node and add the value
      // coming from the preheader's PHI.
      for (int i = PN->getNumOperands()-2; i > 0; i -= 2) {
        MachineBasicBlock *PredB = PN->getOperand(i+1).getMBB();
        if (PredB != Latch) {
          PN->removeOperand(i+1);
          PN->removeOperand(i);
        }
      }
      PN->addOperand(MachineOperand::CreateReg(NewPR, false));
      PN->addOperand(MachineOperand::CreateMBB(NewPH));
    }
  } else {
    assert(Header->pred_size() == 2);

    // The header has only two predecessors, but the non-latch predecessor
    // is not a preheader (e.g. it has other successors, etc.)
    // In such a case we don't need any extra PHI nodes in the new preheader,
    // all we need is to adjust existing PHIs in the header to now refer to
    // the new preheader.
    for (instr_iterator I = Header->instr_begin(), E = Header->instr_end();
         I != E && I->isPHI(); ++I) {
      MachineInstr *PN = &*I;
      for (unsigned i = 1, n = PN->getNumOperands(); i < n; i += 2) {
        MachineOperand &MO = PN->getOperand(i+1);
        if (MO.getMBB() != Latch)
          MO.setMBB(NewPH);
      }
    }
  }

  // "Reroute" the CFG edges to link in the new preheader.
  // If any of the predecessors falls through to the header, insert a branch
  // to the new preheader in that place.
  SmallVector<MachineOperand,1> Tmp2;
  SmallVector<MachineOperand,1> EmptyCond;

  TB = FB = nullptr;

  for (MachineBasicBlock *PB : Preds) {
    if (PB != Latch) {
      Tmp2.clear();
      bool NotAnalyzed = TII->analyzeBranch(*PB, TB, FB, Tmp2, false);
      (void)NotAnalyzed; // suppress compiler warning
      assert (!NotAnalyzed && "Should be analyzable!");
      if (TB != Header && (Tmp2.empty() || FB != Header))
        TII->insertBranch(*PB, NewPH, nullptr, EmptyCond, DL);
      PB->ReplaceUsesOfBlockWith(Header, NewPH);
    }
  }

  // It can happen that the latch block will fall through into the header.
  // Insert an unconditional branch to the header.
  TB = FB = nullptr;
  bool LatchNotAnalyzed = TII->analyzeBranch(*Latch, TB, FB, Tmp2, false);
  (void)LatchNotAnalyzed; // suppress compiler warning
  assert (!LatchNotAnalyzed && "Should be analyzable!");
  if (!TB && !FB)
    TII->insertBranch(*Latch, Header, nullptr, EmptyCond, DL);

  // Finally, the branch from the preheader to the header.
  TII->insertBranch(*NewPH, Header, nullptr, EmptyCond, DL);
  NewPH->addSuccessor(Header);

  MachineLoop *ParentLoop = L->getParentLoop();
  if (ParentLoop)
    ParentLoop->addBasicBlockToLoop(NewPH, *MLI);

  // Update the dominator information with the new preheader.
  if (MDT) {
    if (MachineDomTreeNode *HN = MDT->getNode(Header)) {
      if (MachineDomTreeNode *DHN = HN->getIDom()) {
        MDT->addNewBlock(NewPH, DHN->getBlock());
        MDT->changeImmediateDominator(Header, NewPH);
      }
    }
  }

  return NewPH;
}
