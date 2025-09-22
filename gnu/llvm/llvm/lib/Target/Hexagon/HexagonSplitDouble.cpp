//===- HexagonSplitDouble.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#define DEBUG_TYPE "hsdr"

using namespace llvm;

namespace llvm {

  FunctionPass *createHexagonSplitDoubleRegs();
  void initializeHexagonSplitDoubleRegsPass(PassRegistry&);

} // end namespace llvm

static cl::opt<int> MaxHSDR("max-hsdr", cl::Hidden, cl::init(-1),
    cl::desc("Maximum number of split partitions"));
static cl::opt<bool> MemRefsFixed("hsdr-no-mem", cl::Hidden, cl::init(true),
    cl::desc("Do not split loads or stores"));
  static cl::opt<bool> SplitAll("hsdr-split-all", cl::Hidden, cl::init(false),
      cl::desc("Split all partitions"));

namespace {

  class HexagonSplitDoubleRegs : public MachineFunctionPass {
  public:
    static char ID;

    HexagonSplitDoubleRegs() : MachineFunctionPass(ID) {}

    StringRef getPassName() const override {
      return "Hexagon Split Double Registers";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineLoopInfoWrapperPass>();
      AU.addPreserved<MachineLoopInfoWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    static const TargetRegisterClass *const DoubleRC;

    const HexagonRegisterInfo *TRI = nullptr;
    const HexagonInstrInfo *TII = nullptr;
    const MachineLoopInfo *MLI;
    MachineRegisterInfo *MRI;

    using USet = std::set<unsigned>;
    using UUSetMap = std::map<unsigned, USet>;
    using UUPair = std::pair<unsigned, unsigned>;
    using UUPairMap = std::map<unsigned, UUPair>;
    using LoopRegMap = std::map<const MachineLoop *, USet>;

    bool isInduction(unsigned Reg, LoopRegMap &IRM) const;
    bool isVolatileInstr(const MachineInstr *MI) const;
    bool isFixedInstr(const MachineInstr *MI) const;
    void partitionRegisters(UUSetMap &P2Rs);
    int32_t profit(const MachineInstr *MI) const;
    int32_t profit(Register Reg) const;
    bool isProfitable(const USet &Part, LoopRegMap &IRM) const;

    void collectIndRegsForLoop(const MachineLoop *L, USet &Rs);
    void collectIndRegs(LoopRegMap &IRM);

    void createHalfInstr(unsigned Opc, MachineInstr *MI,
        const UUPairMap &PairMap, unsigned SubR);
    void splitMemRef(MachineInstr *MI, const UUPairMap &PairMap);
    void splitImmediate(MachineInstr *MI, const UUPairMap &PairMap);
    void splitCombine(MachineInstr *MI, const UUPairMap &PairMap);
    void splitExt(MachineInstr *MI, const UUPairMap &PairMap);
    void splitShift(MachineInstr *MI, const UUPairMap &PairMap);
    void splitAslOr(MachineInstr *MI, const UUPairMap &PairMap);
    bool splitInstr(MachineInstr *MI, const UUPairMap &PairMap);
    void replaceSubregUses(MachineInstr *MI, const UUPairMap &PairMap);
    void collapseRegPairs(MachineInstr *MI, const UUPairMap &PairMap);
    bool splitPartition(const USet &Part);

    static int Counter;

    static void dump_partition(raw_ostream&, const USet&,
       const TargetRegisterInfo&);
  };

} // end anonymous namespace

char HexagonSplitDoubleRegs::ID;
int HexagonSplitDoubleRegs::Counter = 0;
const TargetRegisterClass *const HexagonSplitDoubleRegs::DoubleRC =
    &Hexagon::DoubleRegsRegClass;

INITIALIZE_PASS(HexagonSplitDoubleRegs, "hexagon-split-double",
  "Hexagon Split Double Registers", false, false)

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void HexagonSplitDoubleRegs::dump_partition(raw_ostream &os,
      const USet &Part, const TargetRegisterInfo &TRI) {
  dbgs() << '{';
  for (auto I : Part)
    dbgs() << ' ' << printReg(I, &TRI);
  dbgs() << " }";
}
#endif

bool HexagonSplitDoubleRegs::isInduction(unsigned Reg, LoopRegMap &IRM) const {
  for (auto I : IRM) {
    const USet &Rs = I.second;
    if (Rs.find(Reg) != Rs.end())
      return true;
  }
  return false;
}

bool HexagonSplitDoubleRegs::isVolatileInstr(const MachineInstr *MI) const {
  for (auto &MO : MI->memoperands())
    if (MO->isVolatile() || MO->isAtomic())
      return true;
  return false;
}

bool HexagonSplitDoubleRegs::isFixedInstr(const MachineInstr *MI) const {
  if (MI->mayLoadOrStore())
    if (MemRefsFixed || isVolatileInstr(MI))
      return true;
  if (MI->isDebugInstr())
    return false;

  unsigned Opc = MI->getOpcode();
  switch (Opc) {
    default:
      return true;

    case TargetOpcode::PHI:
    case TargetOpcode::COPY:
      break;

    case Hexagon::L2_loadrd_io:
      // Not handling stack stores (only reg-based addresses).
      if (MI->getOperand(1).isReg())
        break;
      return true;
    case Hexagon::S2_storerd_io:
      // Not handling stack stores (only reg-based addresses).
      if (MI->getOperand(0).isReg())
        break;
      return true;
    case Hexagon::L2_loadrd_pi:
    case Hexagon::S2_storerd_pi:

    case Hexagon::A2_tfrpi:
    case Hexagon::A2_combineii:
    case Hexagon::A4_combineir:
    case Hexagon::A4_combineii:
    case Hexagon::A4_combineri:
    case Hexagon::A2_combinew:
    case Hexagon::CONST64:

    case Hexagon::A2_sxtw:

    case Hexagon::A2_andp:
    case Hexagon::A2_orp:
    case Hexagon::A2_xorp:
    case Hexagon::S2_asl_i_p_or:
    case Hexagon::S2_asl_i_p:
    case Hexagon::S2_asr_i_p:
    case Hexagon::S2_lsr_i_p:
      break;
  }

  for (auto &Op : MI->operands()) {
    if (!Op.isReg())
      continue;
    Register R = Op.getReg();
    if (!R.isVirtual())
      return true;
  }
  return false;
}

void HexagonSplitDoubleRegs::partitionRegisters(UUSetMap &P2Rs) {
  using UUMap = std::map<unsigned, unsigned>;
  using UVect = std::vector<unsigned>;

  unsigned NumRegs = MRI->getNumVirtRegs();
  BitVector DoubleRegs(NumRegs);
  for (unsigned i = 0; i < NumRegs; ++i) {
    Register R = Register::index2VirtReg(i);
    if (MRI->getRegClass(R) == DoubleRC)
      DoubleRegs.set(i);
  }

  BitVector FixedRegs(NumRegs);
  for (int x = DoubleRegs.find_first(); x >= 0; x = DoubleRegs.find_next(x)) {
    Register R = Register::index2VirtReg(x);
    MachineInstr *DefI = MRI->getVRegDef(R);
    // In some cases a register may exist, but never be defined or used.
    // It should never appear anywhere, but mark it as "fixed", just to be
    // safe.
    if (!DefI || isFixedInstr(DefI))
      FixedRegs.set(x);
  }

  UUSetMap AssocMap;
  for (int x = DoubleRegs.find_first(); x >= 0; x = DoubleRegs.find_next(x)) {
    if (FixedRegs[x])
      continue;
    Register R = Register::index2VirtReg(x);
    LLVM_DEBUG(dbgs() << printReg(R, TRI) << " ~~");
    USet &Asc = AssocMap[R];
    for (auto U = MRI->use_nodbg_begin(R), Z = MRI->use_nodbg_end();
         U != Z; ++U) {
      MachineOperand &Op = *U;
      MachineInstr *UseI = Op.getParent();
      if (isFixedInstr(UseI))
        continue;
      for (MachineOperand &MO : UseI->operands()) {
        // Skip non-registers or registers with subregisters.
        if (&MO == &Op || !MO.isReg() || MO.getSubReg())
          continue;
        Register T = MO.getReg();
        if (!T.isVirtual()) {
          FixedRegs.set(x);
          continue;
        }
        if (MRI->getRegClass(T) != DoubleRC)
          continue;
        unsigned u = Register::virtReg2Index(T);
        if (FixedRegs[u])
          continue;
        LLVM_DEBUG(dbgs() << ' ' << printReg(T, TRI));
        Asc.insert(T);
        // Make it symmetric.
        AssocMap[T].insert(R);
      }
    }
    LLVM_DEBUG(dbgs() << '\n');
  }

  UUMap R2P;
  unsigned NextP = 1;
  USet Visited;
  for (int x = DoubleRegs.find_first(); x >= 0; x = DoubleRegs.find_next(x)) {
    Register R = Register::index2VirtReg(x);
    if (Visited.count(R))
      continue;
    // Create a new partition for R.
    unsigned ThisP = FixedRegs[x] ? 0 : NextP++;
    UVect WorkQ;
    WorkQ.push_back(R);
    for (unsigned i = 0; i < WorkQ.size(); ++i) {
      unsigned T = WorkQ[i];
      if (Visited.count(T))
        continue;
      R2P[T] = ThisP;
      Visited.insert(T);
      // Add all registers associated with T.
      USet &Asc = AssocMap[T];
      append_range(WorkQ, Asc);
    }
  }

  for (auto I : R2P)
    P2Rs[I.second].insert(I.first);
}

static inline int32_t profitImm(unsigned Imm) {
  int32_t P = 0;
  if (Imm == 0 || Imm == 0xFFFFFFFF)
    P += 10;
  return P;
}

int32_t HexagonSplitDoubleRegs::profit(const MachineInstr *MI) const {
  unsigned ImmX = 0;
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
    case TargetOpcode::PHI:
      for (const auto &Op : MI->operands())
        if (!Op.getSubReg())
          return 0;
      return 10;
    case TargetOpcode::COPY:
      if (MI->getOperand(1).getSubReg() != 0)
        return 10;
      return 0;

    case Hexagon::L2_loadrd_io:
    case Hexagon::S2_storerd_io:
      return -1;
    case Hexagon::L2_loadrd_pi:
    case Hexagon::S2_storerd_pi:
      return 2;

    case Hexagon::A2_tfrpi:
    case Hexagon::CONST64: {
      uint64_t D = MI->getOperand(1).getImm();
      unsigned Lo = D & 0xFFFFFFFFULL;
      unsigned Hi = D >> 32;
      return profitImm(Lo) + profitImm(Hi);
    }
    case Hexagon::A2_combineii:
    case Hexagon::A4_combineii: {
      const MachineOperand &Op1 = MI->getOperand(1);
      const MachineOperand &Op2 = MI->getOperand(2);
      int32_t Prof1 = Op1.isImm() ? profitImm(Op1.getImm()) : 0;
      int32_t Prof2 = Op2.isImm() ? profitImm(Op2.getImm()) : 0;
      return Prof1 + Prof2;
    }
    case Hexagon::A4_combineri:
      ImmX++;
      // Fall through into A4_combineir.
      [[fallthrough]];
    case Hexagon::A4_combineir: {
      ImmX++;
      const MachineOperand &OpX = MI->getOperand(ImmX);
      if (OpX.isImm()) {
        int64_t V = OpX.getImm();
        if (V == 0 || V == -1)
          return 10;
      }
      // Fall through into A2_combinew.
      [[fallthrough]];
    }
    case Hexagon::A2_combinew:
      return 2;

    case Hexagon::A2_sxtw:
      return 3;

    case Hexagon::A2_andp:
    case Hexagon::A2_orp:
    case Hexagon::A2_xorp: {
      Register Rs = MI->getOperand(1).getReg();
      Register Rt = MI->getOperand(2).getReg();
      return profit(Rs) + profit(Rt);
    }

    case Hexagon::S2_asl_i_p_or: {
      unsigned S = MI->getOperand(3).getImm();
      if (S == 0 || S == 32)
        return 10;
      return -1;
    }
    case Hexagon::S2_asl_i_p:
    case Hexagon::S2_asr_i_p:
    case Hexagon::S2_lsr_i_p:
      unsigned S = MI->getOperand(2).getImm();
      if (S == 0 || S == 32)
        return 10;
      if (S == 16)
        return 5;
      if (S == 48)
        return 7;
      return -10;
  }

  return 0;
}

int32_t HexagonSplitDoubleRegs::profit(Register Reg) const {
  assert(Reg.isVirtual());

  const MachineInstr *DefI = MRI->getVRegDef(Reg);
  switch (DefI->getOpcode()) {
    case Hexagon::A2_tfrpi:
    case Hexagon::CONST64:
    case Hexagon::A2_combineii:
    case Hexagon::A4_combineii:
    case Hexagon::A4_combineri:
    case Hexagon::A4_combineir:
    case Hexagon::A2_combinew:
      return profit(DefI);
    default:
      break;
  }
  return 0;
}

bool HexagonSplitDoubleRegs::isProfitable(const USet &Part, LoopRegMap &IRM)
      const {
  unsigned FixedNum = 0, LoopPhiNum = 0;
  int32_t TotalP = 0;

  for (unsigned DR : Part) {
    MachineInstr *DefI = MRI->getVRegDef(DR);
    int32_t P = profit(DefI);
    if (P == std::numeric_limits<int>::min())
      return false;
    TotalP += P;
    // Reduce the profitability of splitting induction registers.
    if (isInduction(DR, IRM))
      TotalP -= 30;

    for (auto U = MRI->use_nodbg_begin(DR), W = MRI->use_nodbg_end();
         U != W; ++U) {
      MachineInstr *UseI = U->getParent();
      if (isFixedInstr(UseI)) {
        FixedNum++;
        // Calculate the cost of generating REG_SEQUENCE instructions.
        for (auto &Op : UseI->operands()) {
          if (Op.isReg() && Part.count(Op.getReg()))
            if (Op.getSubReg())
              TotalP -= 2;
        }
        continue;
      }
      // If a register from this partition is used in a fixed instruction,
      // and there is also a register in this partition that is used in
      // a loop phi node, then decrease the splitting profit as this can
      // confuse the modulo scheduler.
      if (UseI->isPHI()) {
        const MachineBasicBlock *PB = UseI->getParent();
        const MachineLoop *L = MLI->getLoopFor(PB);
        if (L && L->getHeader() == PB)
          LoopPhiNum++;
      }
      // Splittable instruction.
      int32_t P = profit(UseI);
      if (P == std::numeric_limits<int>::min())
        return false;
      TotalP += P;
    }
  }

  if (FixedNum > 0 && LoopPhiNum > 0)
    TotalP -= 20*LoopPhiNum;

  LLVM_DEBUG(dbgs() << "Partition profit: " << TotalP << '\n');
  if (SplitAll)
    return true;
  return TotalP > 0;
}

void HexagonSplitDoubleRegs::collectIndRegsForLoop(const MachineLoop *L,
      USet &Rs) {
  const MachineBasicBlock *HB = L->getHeader();
  const MachineBasicBlock *LB = L->getLoopLatch();
  if (!HB || !LB)
    return;

  // Examine the latch branch. Expect it to be a conditional branch to
  // the header (either "br-cond header" or "br-cond exit; br header").
  MachineBasicBlock *TB = nullptr, *FB = nullptr;
  MachineBasicBlock *TmpLB = const_cast<MachineBasicBlock*>(LB);
  SmallVector<MachineOperand,2> Cond;
  bool BadLB = TII->analyzeBranch(*TmpLB, TB, FB, Cond, false);
  // Only analyzable conditional branches. HII::analyzeBranch will put
  // the branch opcode as the first element of Cond, and the predicate
  // operand as the second.
  if (BadLB || Cond.size() != 2)
    return;
  // Only simple jump-conditional (with or without negation).
  if (!TII->PredOpcodeHasJMP_c(Cond[0].getImm()))
    return;
  // Must go to the header.
  if (TB != HB && FB != HB)
    return;
  assert(Cond[1].isReg() && "Unexpected Cond vector from analyzeBranch");
  // Expect a predicate register.
  Register PR = Cond[1].getReg();
  assert(MRI->getRegClass(PR) == &Hexagon::PredRegsRegClass);

  // Get the registers on which the loop controlling compare instruction
  // depends.
  Register CmpR1, CmpR2;
  const MachineInstr *CmpI = MRI->getVRegDef(PR);
  while (CmpI->getOpcode() == Hexagon::C2_not)
    CmpI = MRI->getVRegDef(CmpI->getOperand(1).getReg());

  int64_t Mask = 0, Val = 0;
  bool OkCI = TII->analyzeCompare(*CmpI, CmpR1, CmpR2, Mask, Val);
  if (!OkCI)
    return;
  // Eliminate non-double input registers.
  if (CmpR1 && MRI->getRegClass(CmpR1) != DoubleRC)
    CmpR1 = 0;
  if (CmpR2 && MRI->getRegClass(CmpR2) != DoubleRC)
    CmpR2 = 0;
  if (!CmpR1 && !CmpR2)
    return;

  // Now examine the top of the loop: the phi nodes that could poten-
  // tially define loop induction registers. The registers defined by
  // such a phi node would be used in a 64-bit add, which then would
  // be used in the loop compare instruction.

  // Get the set of all double registers defined by phi nodes in the
  // loop header.
  using UVect = std::vector<unsigned>;

  UVect DP;
  for (auto &MI : *HB) {
    if (!MI.isPHI())
      break;
    const MachineOperand &MD = MI.getOperand(0);
    Register R = MD.getReg();
    if (MRI->getRegClass(R) == DoubleRC)
      DP.push_back(R);
  }
  if (DP.empty())
    return;

  auto NoIndOp = [this, CmpR1, CmpR2] (unsigned R) -> bool {
    for (auto I = MRI->use_nodbg_begin(R), E = MRI->use_nodbg_end();
         I != E; ++I) {
      const MachineInstr *UseI = I->getParent();
      if (UseI->getOpcode() != Hexagon::A2_addp)
        continue;
      // Get the output from the add. If it is one of the inputs to the
      // loop-controlling compare instruction, then R is likely an induc-
      // tion register.
      Register T = UseI->getOperand(0).getReg();
      if (T == CmpR1 || T == CmpR2)
        return false;
    }
    return true;
  };
  UVect::iterator End = llvm::remove_if(DP, NoIndOp);
  Rs.insert(DP.begin(), End);
  Rs.insert(CmpR1);
  Rs.insert(CmpR2);

  LLVM_DEBUG({
    dbgs() << "For loop at " << printMBBReference(*HB) << " ind regs: ";
    dump_partition(dbgs(), Rs, *TRI);
    dbgs() << '\n';
  });
}

void HexagonSplitDoubleRegs::collectIndRegs(LoopRegMap &IRM) {
  using LoopVector = std::vector<MachineLoop *>;

  LoopVector WorkQ;

  append_range(WorkQ, *MLI);
  for (unsigned i = 0; i < WorkQ.size(); ++i)
    append_range(WorkQ, *WorkQ[i]);

  USet Rs;
  for (MachineLoop *L : WorkQ) {
    Rs.clear();
    collectIndRegsForLoop(L, Rs);
    if (!Rs.empty())
      IRM.insert(std::make_pair(L, Rs));
  }
}

void HexagonSplitDoubleRegs::createHalfInstr(unsigned Opc, MachineInstr *MI,
      const UUPairMap &PairMap, unsigned SubR) {
  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  MachineInstr *NewI = BuildMI(B, MI, DL, TII->get(Opc));

  for (auto &Op : MI->operands()) {
    if (!Op.isReg()) {
      NewI->addOperand(Op);
      continue;
    }
    // For register operands, set the subregister.
    Register R = Op.getReg();
    unsigned SR = Op.getSubReg();
    bool isVirtReg = R.isVirtual();
    bool isKill = Op.isKill();
    if (isVirtReg && MRI->getRegClass(R) == DoubleRC) {
      isKill = false;
      UUPairMap::const_iterator F = PairMap.find(R);
      if (F == PairMap.end()) {
        SR = SubR;
      } else {
        const UUPair &P = F->second;
        R = (SubR == Hexagon::isub_lo) ? P.first : P.second;
        SR = 0;
      }
    }
    auto CO = MachineOperand::CreateReg(R, Op.isDef(), Op.isImplicit(), isKill,
          Op.isDead(), Op.isUndef(), Op.isEarlyClobber(), SR, Op.isDebug(),
          Op.isInternalRead());
    NewI->addOperand(CO);
  }
}

void HexagonSplitDoubleRegs::splitMemRef(MachineInstr *MI,
      const UUPairMap &PairMap) {
  bool Load = MI->mayLoad();
  unsigned OrigOpc = MI->getOpcode();
  bool PostInc = (OrigOpc == Hexagon::L2_loadrd_pi ||
                  OrigOpc == Hexagon::S2_storerd_pi);
  MachineInstr *LowI, *HighI;
  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  // Index of the base-address-register operand.
  unsigned AdrX = PostInc ? (Load ? 2 : 1)
                          : (Load ? 1 : 0);
  MachineOperand &AdrOp = MI->getOperand(AdrX);
  unsigned RSA = getRegState(AdrOp);
  MachineOperand &ValOp = Load ? MI->getOperand(0)
                               : (PostInc ? MI->getOperand(3)
                                          : MI->getOperand(2));
  UUPairMap::const_iterator F = PairMap.find(ValOp.getReg());
  assert(F != PairMap.end());

  if (Load) {
    const UUPair &P = F->second;
    int64_t Off = PostInc ? 0 : MI->getOperand(2).getImm();
    LowI = BuildMI(B, MI, DL, TII->get(Hexagon::L2_loadri_io), P.first)
             .addReg(AdrOp.getReg(), RSA & ~RegState::Kill, AdrOp.getSubReg())
             .addImm(Off);
    HighI = BuildMI(B, MI, DL, TII->get(Hexagon::L2_loadri_io), P.second)
              .addReg(AdrOp.getReg(), RSA & ~RegState::Kill, AdrOp.getSubReg())
              .addImm(Off+4);
  } else {
    const UUPair &P = F->second;
    int64_t Off = PostInc ? 0 : MI->getOperand(1).getImm();
    LowI = BuildMI(B, MI, DL, TII->get(Hexagon::S2_storeri_io))
             .addReg(AdrOp.getReg(), RSA & ~RegState::Kill, AdrOp.getSubReg())
             .addImm(Off)
             .addReg(P.first);
    HighI = BuildMI(B, MI, DL, TII->get(Hexagon::S2_storeri_io))
              .addReg(AdrOp.getReg(), RSA & ~RegState::Kill, AdrOp.getSubReg())
              .addImm(Off+4)
              .addReg(P.second);
  }

  if (PostInc) {
    // Create the increment of the address register.
    int64_t Inc = Load ? MI->getOperand(3).getImm()
                       : MI->getOperand(2).getImm();
    MachineOperand &UpdOp = Load ? MI->getOperand(1) : MI->getOperand(0);
    const TargetRegisterClass *RC = MRI->getRegClass(UpdOp.getReg());
    Register NewR = MRI->createVirtualRegister(RC);
    assert(!UpdOp.getSubReg() && "Def operand with subreg");
    BuildMI(B, MI, DL, TII->get(Hexagon::A2_addi), NewR)
      .addReg(AdrOp.getReg(), RSA)
      .addImm(Inc);
    MRI->replaceRegWith(UpdOp.getReg(), NewR);
    // The original instruction will be deleted later.
  }

  // Generate a new pair of memory-operands.
  MachineFunction &MF = *B.getParent();
  for (auto &MO : MI->memoperands()) {
    const MachinePointerInfo &Ptr = MO->getPointerInfo();
    MachineMemOperand::Flags F = MO->getFlags();
    Align A = MO->getAlign();

    auto *Tmp1 = MF.getMachineMemOperand(Ptr, F, 4 /*size*/, A);
    LowI->addMemOperand(MF, Tmp1);
    auto *Tmp2 =
        MF.getMachineMemOperand(Ptr, F, 4 /*size*/, std::min(A, Align(4)));
    HighI->addMemOperand(MF, Tmp2);
  }
}

void HexagonSplitDoubleRegs::splitImmediate(MachineInstr *MI,
      const UUPairMap &PairMap) {
  MachineOperand &Op0 = MI->getOperand(0);
  MachineOperand &Op1 = MI->getOperand(1);
  assert(Op0.isReg() && Op1.isImm());
  uint64_t V = Op1.getImm();

  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  UUPairMap::const_iterator F = PairMap.find(Op0.getReg());
  assert(F != PairMap.end());
  const UUPair &P = F->second;

  // The operand to A2_tfrsi can only have 32 significant bits. Immediate
  // values in MachineOperand are stored as 64-bit integers, and so the
  // value -1 may be represented either as 64-bit -1, or 4294967295. Both
  // will have the 32 higher bits truncated in the end, but -1 will remain
  // as -1, while the latter may appear to be a large unsigned value
  // requiring a constant extender. The casting to int32_t will select the
  // former representation. (The same reasoning applies to all 32-bit
  // values.)
  BuildMI(B, MI, DL, TII->get(Hexagon::A2_tfrsi), P.first)
    .addImm(int32_t(V & 0xFFFFFFFFULL));
  BuildMI(B, MI, DL, TII->get(Hexagon::A2_tfrsi), P.second)
    .addImm(int32_t(V >> 32));
}

void HexagonSplitDoubleRegs::splitCombine(MachineInstr *MI,
      const UUPairMap &PairMap) {
  MachineOperand &Op0 = MI->getOperand(0);
  MachineOperand &Op1 = MI->getOperand(1);
  MachineOperand &Op2 = MI->getOperand(2);
  assert(Op0.isReg());

  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  UUPairMap::const_iterator F = PairMap.find(Op0.getReg());
  assert(F != PairMap.end());
  const UUPair &P = F->second;

  if (!Op1.isReg()) {
    BuildMI(B, MI, DL, TII->get(Hexagon::A2_tfrsi), P.second)
      .add(Op1);
  } else {
    BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), P.second)
      .addReg(Op1.getReg(), getRegState(Op1), Op1.getSubReg());
  }

  if (!Op2.isReg()) {
    BuildMI(B, MI, DL, TII->get(Hexagon::A2_tfrsi), P.first)
      .add(Op2);
  } else {
    BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), P.first)
      .addReg(Op2.getReg(), getRegState(Op2), Op2.getSubReg());
  }
}

void HexagonSplitDoubleRegs::splitExt(MachineInstr *MI,
      const UUPairMap &PairMap) {
  MachineOperand &Op0 = MI->getOperand(0);
  MachineOperand &Op1 = MI->getOperand(1);
  assert(Op0.isReg() && Op1.isReg());

  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  UUPairMap::const_iterator F = PairMap.find(Op0.getReg());
  assert(F != PairMap.end());
  const UUPair &P = F->second;
  unsigned RS = getRegState(Op1);

  BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), P.first)
    .addReg(Op1.getReg(), RS & ~RegState::Kill, Op1.getSubReg());
  BuildMI(B, MI, DL, TII->get(Hexagon::S2_asr_i_r), P.second)
    .addReg(Op1.getReg(), RS, Op1.getSubReg())
    .addImm(31);
}

void HexagonSplitDoubleRegs::splitShift(MachineInstr *MI,
      const UUPairMap &PairMap) {
  using namespace Hexagon;

  MachineOperand &Op0 = MI->getOperand(0);
  MachineOperand &Op1 = MI->getOperand(1);
  MachineOperand &Op2 = MI->getOperand(2);
  assert(Op0.isReg() && Op1.isReg() && Op2.isImm());
  int64_t Sh64 = Op2.getImm();
  assert(Sh64 >= 0 && Sh64 < 64);
  unsigned S = Sh64;

  UUPairMap::const_iterator F = PairMap.find(Op0.getReg());
  assert(F != PairMap.end());
  const UUPair &P = F->second;
  Register LoR = P.first;
  Register HiR = P.second;

  unsigned Opc = MI->getOpcode();
  bool Right = (Opc == S2_lsr_i_p || Opc == S2_asr_i_p);
  bool Left = !Right;
  bool Signed = (Opc == S2_asr_i_p);

  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned RS = getRegState(Op1);
  unsigned ShiftOpc = Left ? S2_asl_i_r
                           : (Signed ? S2_asr_i_r : S2_lsr_i_r);
  unsigned LoSR = isub_lo;
  unsigned HiSR = isub_hi;

  if (S == 0) {
    // No shift, subregister copy.
    BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), LoR)
      .addReg(Op1.getReg(), RS & ~RegState::Kill, LoSR);
    BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), HiR)
      .addReg(Op1.getReg(), RS, HiSR);
  } else if (S < 32) {
    const TargetRegisterClass *IntRC = &IntRegsRegClass;
    Register TmpR = MRI->createVirtualRegister(IntRC);
    // Expansion:
    // Shift left:    DR = shl R, #s
    //   LoR  = shl R.lo, #s
    //   TmpR = extractu R.lo, #s, #32-s
    //   HiR  = or (TmpR, asl(R.hi, #s))
    // Shift right:   DR = shr R, #s
    //   HiR  = shr R.hi, #s
    //   TmpR = shr R.lo, #s
    //   LoR  = insert TmpR, R.hi, #s, #32-s

    // Shift left:
    //   LoR  = shl R.lo, #s
    // Shift right:
    //   TmpR = shr R.lo, #s

    // Make a special case for A2_aslh and A2_asrh (they are predicable as
    // opposed to S2_asl_i_r/S2_asr_i_r).
    if (S == 16 && Left)
      BuildMI(B, MI, DL, TII->get(A2_aslh), LoR)
        .addReg(Op1.getReg(), RS & ~RegState::Kill, LoSR);
    else if (S == 16 && Signed)
      BuildMI(B, MI, DL, TII->get(A2_asrh), TmpR)
        .addReg(Op1.getReg(), RS & ~RegState::Kill, LoSR);
    else
      BuildMI(B, MI, DL, TII->get(ShiftOpc), (Left ? LoR : TmpR))
        .addReg(Op1.getReg(), RS & ~RegState::Kill, LoSR)
        .addImm(S);

    if (Left) {
      // TmpR = extractu R.lo, #s, #32-s
      BuildMI(B, MI, DL, TII->get(S2_extractu), TmpR)
        .addReg(Op1.getReg(), RS & ~RegState::Kill, LoSR)
        .addImm(S)
        .addImm(32-S);
      // HiR  = or (TmpR, asl(R.hi, #s))
      BuildMI(B, MI, DL, TII->get(S2_asl_i_r_or), HiR)
        .addReg(TmpR)
        .addReg(Op1.getReg(), RS, HiSR)
        .addImm(S);
    } else {
      // HiR  = shr R.hi, #s
      BuildMI(B, MI, DL, TII->get(ShiftOpc), HiR)
        .addReg(Op1.getReg(), RS & ~RegState::Kill, HiSR)
        .addImm(S);
      // LoR  = insert TmpR, R.hi, #s, #32-s
      BuildMI(B, MI, DL, TII->get(S2_insert), LoR)
        .addReg(TmpR)
        .addReg(Op1.getReg(), RS, HiSR)
        .addImm(S)
        .addImm(32-S);
    }
  } else if (S == 32) {
    BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), (Left ? HiR : LoR))
      .addReg(Op1.getReg(), RS & ~RegState::Kill, (Left ? LoSR : HiSR));
    if (!Signed)
      BuildMI(B, MI, DL, TII->get(A2_tfrsi), (Left ? LoR : HiR))
        .addImm(0);
    else  // Must be right shift.
      BuildMI(B, MI, DL, TII->get(S2_asr_i_r), HiR)
        .addReg(Op1.getReg(), RS, HiSR)
        .addImm(31);
  } else if (S < 64) {
    S -= 32;
    if (S == 16 && Left)
      BuildMI(B, MI, DL, TII->get(A2_aslh), HiR)
        .addReg(Op1.getReg(), RS & ~RegState::Kill, LoSR);
    else if (S == 16 && Signed)
      BuildMI(B, MI, DL, TII->get(A2_asrh), LoR)
        .addReg(Op1.getReg(), RS & ~RegState::Kill, HiSR);
    else
      BuildMI(B, MI, DL, TII->get(ShiftOpc), (Left ? HiR : LoR))
        .addReg(Op1.getReg(), RS & ~RegState::Kill, (Left ? LoSR : HiSR))
        .addImm(S);

    if (Signed)
      BuildMI(B, MI, DL, TII->get(S2_asr_i_r), HiR)
        .addReg(Op1.getReg(), RS, HiSR)
        .addImm(31);
    else
      BuildMI(B, MI, DL, TII->get(A2_tfrsi), (Left ? LoR : HiR))
        .addImm(0);
  }
}

void HexagonSplitDoubleRegs::splitAslOr(MachineInstr *MI,
      const UUPairMap &PairMap) {
  using namespace Hexagon;

  MachineOperand &Op0 = MI->getOperand(0);
  MachineOperand &Op1 = MI->getOperand(1);
  MachineOperand &Op2 = MI->getOperand(2);
  MachineOperand &Op3 = MI->getOperand(3);
  assert(Op0.isReg() && Op1.isReg() && Op2.isReg() && Op3.isImm());
  int64_t Sh64 = Op3.getImm();
  assert(Sh64 >= 0 && Sh64 < 64);
  unsigned S = Sh64;

  UUPairMap::const_iterator F = PairMap.find(Op0.getReg());
  assert(F != PairMap.end());
  const UUPair &P = F->second;
  unsigned LoR = P.first;
  unsigned HiR = P.second;

  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();
  unsigned RS1 = getRegState(Op1);
  unsigned RS2 = getRegState(Op2);
  const TargetRegisterClass *IntRC = &IntRegsRegClass;

  unsigned LoSR = isub_lo;
  unsigned HiSR = isub_hi;

  // Op0 = S2_asl_i_p_or Op1, Op2, Op3
  // means:  Op0 = or (Op1, asl(Op2, Op3))

  // Expansion of
  //   DR = or (R1, asl(R2, #s))
  //
  //   LoR  = or (R1.lo, asl(R2.lo, #s))
  //   Tmp1 = extractu R2.lo, #s, #32-s
  //   Tmp2 = or R1.hi, Tmp1
  //   HiR  = or (Tmp2, asl(R2.hi, #s))

  if (S == 0) {
    // DR  = or (R1, asl(R2, #0))
    //    -> or (R1, R2)
    // i.e. LoR = or R1.lo, R2.lo
    //      HiR = or R1.hi, R2.hi
    BuildMI(B, MI, DL, TII->get(A2_or), LoR)
      .addReg(Op1.getReg(), RS1 & ~RegState::Kill, LoSR)
      .addReg(Op2.getReg(), RS2 & ~RegState::Kill, LoSR);
    BuildMI(B, MI, DL, TII->get(A2_or), HiR)
      .addReg(Op1.getReg(), RS1, HiSR)
      .addReg(Op2.getReg(), RS2, HiSR);
  } else if (S < 32) {
    BuildMI(B, MI, DL, TII->get(S2_asl_i_r_or), LoR)
      .addReg(Op1.getReg(), RS1 & ~RegState::Kill, LoSR)
      .addReg(Op2.getReg(), RS2 & ~RegState::Kill, LoSR)
      .addImm(S);
    Register TmpR1 = MRI->createVirtualRegister(IntRC);
    BuildMI(B, MI, DL, TII->get(S2_extractu), TmpR1)
      .addReg(Op2.getReg(), RS2 & ~RegState::Kill, LoSR)
      .addImm(S)
      .addImm(32-S);
    Register TmpR2 = MRI->createVirtualRegister(IntRC);
    BuildMI(B, MI, DL, TII->get(A2_or), TmpR2)
      .addReg(Op1.getReg(), RS1, HiSR)
      .addReg(TmpR1);
    BuildMI(B, MI, DL, TII->get(S2_asl_i_r_or), HiR)
      .addReg(TmpR2)
      .addReg(Op2.getReg(), RS2, HiSR)
      .addImm(S);
  } else if (S == 32) {
    // DR  = or (R1, asl(R2, #32))
    //    -> or R1, R2.lo
    // LoR = R1.lo
    // HiR = or R1.hi, R2.lo
    BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), LoR)
      .addReg(Op1.getReg(), RS1 & ~RegState::Kill, LoSR);
    BuildMI(B, MI, DL, TII->get(A2_or), HiR)
      .addReg(Op1.getReg(), RS1, HiSR)
      .addReg(Op2.getReg(), RS2, LoSR);
  } else if (S < 64) {
    // DR  = or (R1, asl(R2, #s))
    //
    // LoR = R1:lo
    // HiR = or (R1:hi, asl(R2:lo, #s-32))
    S -= 32;
    BuildMI(B, MI, DL, TII->get(TargetOpcode::COPY), LoR)
      .addReg(Op1.getReg(), RS1 & ~RegState::Kill, LoSR);
    BuildMI(B, MI, DL, TII->get(S2_asl_i_r_or), HiR)
      .addReg(Op1.getReg(), RS1, HiSR)
      .addReg(Op2.getReg(), RS2, LoSR)
      .addImm(S);
  }
}

bool HexagonSplitDoubleRegs::splitInstr(MachineInstr *MI,
      const UUPairMap &PairMap) {
  using namespace Hexagon;

  LLVM_DEBUG(dbgs() << "Splitting: " << *MI);
  bool Split = false;
  unsigned Opc = MI->getOpcode();

  switch (Opc) {
    case TargetOpcode::PHI:
    case TargetOpcode::COPY: {
      Register DstR = MI->getOperand(0).getReg();
      if (MRI->getRegClass(DstR) == DoubleRC) {
        createHalfInstr(Opc, MI, PairMap, isub_lo);
        createHalfInstr(Opc, MI, PairMap, isub_hi);
        Split = true;
      }
      break;
    }
    case A2_andp:
      createHalfInstr(A2_and, MI, PairMap, isub_lo);
      createHalfInstr(A2_and, MI, PairMap, isub_hi);
      Split = true;
      break;
    case A2_orp:
      createHalfInstr(A2_or, MI, PairMap, isub_lo);
      createHalfInstr(A2_or, MI, PairMap, isub_hi);
      Split = true;
      break;
    case A2_xorp:
      createHalfInstr(A2_xor, MI, PairMap, isub_lo);
      createHalfInstr(A2_xor, MI, PairMap, isub_hi);
      Split = true;
      break;

    case L2_loadrd_io:
    case L2_loadrd_pi:
    case S2_storerd_io:
    case S2_storerd_pi:
      splitMemRef(MI, PairMap);
      Split = true;
      break;

    case A2_tfrpi:
    case CONST64:
      splitImmediate(MI, PairMap);
      Split = true;
      break;

    case A2_combineii:
    case A4_combineir:
    case A4_combineii:
    case A4_combineri:
    case A2_combinew:
      splitCombine(MI, PairMap);
      Split = true;
      break;

    case A2_sxtw:
      splitExt(MI, PairMap);
      Split = true;
      break;

    case S2_asl_i_p:
    case S2_asr_i_p:
    case S2_lsr_i_p:
      splitShift(MI, PairMap);
      Split = true;
      break;

    case S2_asl_i_p_or:
      splitAslOr(MI, PairMap);
      Split = true;
      break;

    default:
      llvm_unreachable("Instruction not splitable");
      return false;
  }

  return Split;
}

void HexagonSplitDoubleRegs::replaceSubregUses(MachineInstr *MI,
      const UUPairMap &PairMap) {
  for (auto &Op : MI->operands()) {
    if (!Op.isReg() || !Op.isUse() || !Op.getSubReg())
      continue;
    Register R = Op.getReg();
    UUPairMap::const_iterator F = PairMap.find(R);
    if (F == PairMap.end())
      continue;
    const UUPair &P = F->second;
    switch (Op.getSubReg()) {
      case Hexagon::isub_lo:
        Op.setReg(P.first);
        break;
      case Hexagon::isub_hi:
        Op.setReg(P.second);
        break;
    }
    Op.setSubReg(0);
  }
}

void HexagonSplitDoubleRegs::collapseRegPairs(MachineInstr *MI,
      const UUPairMap &PairMap) {
  MachineBasicBlock &B = *MI->getParent();
  DebugLoc DL = MI->getDebugLoc();

  for (auto &Op : MI->operands()) {
    if (!Op.isReg() || !Op.isUse())
      continue;
    Register R = Op.getReg();
    if (!R.isVirtual())
      continue;
    if (MRI->getRegClass(R) != DoubleRC || Op.getSubReg())
      continue;
    UUPairMap::const_iterator F = PairMap.find(R);
    if (F == PairMap.end())
      continue;
    const UUPair &Pr = F->second;
    Register NewDR = MRI->createVirtualRegister(DoubleRC);
    BuildMI(B, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), NewDR)
      .addReg(Pr.first)
      .addImm(Hexagon::isub_lo)
      .addReg(Pr.second)
      .addImm(Hexagon::isub_hi);
    Op.setReg(NewDR);
  }
}

bool HexagonSplitDoubleRegs::splitPartition(const USet &Part) {
  using MISet = std::set<MachineInstr *>;

  const TargetRegisterClass *IntRC = &Hexagon::IntRegsRegClass;
  bool Changed = false;

  LLVM_DEBUG(dbgs() << "Splitting partition: ";
             dump_partition(dbgs(), Part, *TRI); dbgs() << '\n');

  UUPairMap PairMap;

  MISet SplitIns;
  for (unsigned DR : Part) {
    MachineInstr *DefI = MRI->getVRegDef(DR);
    SplitIns.insert(DefI);

    // Collect all instructions, including fixed ones.  We won't split them,
    // but we need to visit them again to insert the REG_SEQUENCE instructions.
    for (auto U = MRI->use_nodbg_begin(DR), W = MRI->use_nodbg_end();
         U != W; ++U)
      SplitIns.insert(U->getParent());

    Register LoR = MRI->createVirtualRegister(IntRC);
    Register HiR = MRI->createVirtualRegister(IntRC);
    LLVM_DEBUG(dbgs() << "Created mapping: " << printReg(DR, TRI) << " -> "
                      << printReg(HiR, TRI) << ':' << printReg(LoR, TRI)
                      << '\n');
    PairMap.insert(std::make_pair(DR, UUPair(LoR, HiR)));
  }

  MISet Erase;
  for (auto *MI : SplitIns) {
    if (isFixedInstr(MI)) {
      collapseRegPairs(MI, PairMap);
    } else {
      bool Done = splitInstr(MI, PairMap);
      if (Done)
        Erase.insert(MI);
      Changed |= Done;
    }
  }

  for (unsigned DR : Part) {
    // Before erasing "double" instructions, revisit all uses of the double
    // registers in this partition, and replace all uses of them with subre-
    // gisters, with the corresponding single registers.
    MISet Uses;
    for (auto U = MRI->use_nodbg_begin(DR), W = MRI->use_nodbg_end();
         U != W; ++U)
      Uses.insert(U->getParent());
    for (auto *M : Uses)
      replaceSubregUses(M, PairMap);
  }

  for (auto *MI : Erase) {
    MachineBasicBlock *B = MI->getParent();
    B->erase(MI);
  }

  return Changed;
}

bool HexagonSplitDoubleRegs::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "Splitting double registers in function: "
                    << MF.getName() << '\n');

  auto &ST = MF.getSubtarget<HexagonSubtarget>();
  TRI = ST.getRegisterInfo();
  TII = ST.getInstrInfo();
  MRI = &MF.getRegInfo();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();

  UUSetMap P2Rs;
  LoopRegMap IRM;

  collectIndRegs(IRM);
  partitionRegisters(P2Rs);

  LLVM_DEBUG({
    dbgs() << "Register partitioning: (partition #0 is fixed)\n";
    for (UUSetMap::iterator I = P2Rs.begin(), E = P2Rs.end(); I != E; ++I) {
      dbgs() << '#' << I->first << " -> ";
      dump_partition(dbgs(), I->second, *TRI);
      dbgs() << '\n';
    }
  });

  bool Changed = false;
  int Limit = MaxHSDR;

  for (UUSetMap::iterator I = P2Rs.begin(), E = P2Rs.end(); I != E; ++I) {
    if (I->first == 0)
      continue;
    if (Limit >= 0 && Counter >= Limit)
      break;
    USet &Part = I->second;
    LLVM_DEBUG(dbgs() << "Calculating profit for partition #" << I->first
                      << '\n');
    if (!isProfitable(Part, IRM))
      continue;
    Counter++;
    Changed |= splitPartition(Part);
  }

  return Changed;
}

FunctionPass *llvm::createHexagonSplitDoubleRegs() {
  return new HexagonSplitDoubleRegs();
}
