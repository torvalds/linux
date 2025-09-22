//===- HexagonSubtarget.cpp - Hexagon Subtarget Information ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Hexagon specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#include "HexagonSubtarget.h"
#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/IR/IntrinsicsHexagon.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "hexagon-subtarget"

#define GET_SUBTARGETINFO_CTOR
#define GET_SUBTARGETINFO_TARGET_DESC
#include "HexagonGenSubtargetInfo.inc"

static cl::opt<bool> EnableBSBSched("enable-bsb-sched", cl::Hidden,
                                    cl::init(true));

static cl::opt<bool> EnableTCLatencySched("enable-tc-latency-sched", cl::Hidden,
                                          cl::init(false));

static cl::opt<bool>
    EnableDotCurSched("enable-cur-sched", cl::Hidden, cl::init(true),
                      cl::desc("Enable the scheduler to generate .cur"));

static cl::opt<bool>
    DisableHexagonMISched("disable-hexagon-misched", cl::Hidden,
                          cl::desc("Disable Hexagon MI Scheduling"));

static cl::opt<bool> OverrideLongCalls(
    "hexagon-long-calls", cl::Hidden,
    cl::desc("If present, forces/disables the use of long calls"));

static cl::opt<bool>
    EnablePredicatedCalls("hexagon-pred-calls", cl::Hidden,
                          cl::desc("Consider calls to be predicable"));

static cl::opt<bool> SchedPredsCloser("sched-preds-closer", cl::Hidden,
                                      cl::init(true));

static cl::opt<bool> SchedRetvalOptimization("sched-retval-optimization",
                                             cl::Hidden, cl::init(true));

static cl::opt<bool> EnableCheckBankConflict(
    "hexagon-check-bank-conflict", cl::Hidden, cl::init(true),
    cl::desc("Enable checking for cache bank conflicts"));

HexagonSubtarget::HexagonSubtarget(const Triple &TT, StringRef CPU,
                                   StringRef FS, const TargetMachine &TM)
    : HexagonGenSubtargetInfo(TT, CPU, /*TuneCPU*/ CPU, FS),
      OptLevel(TM.getOptLevel()),
      CPUString(std::string(Hexagon_MC::selectHexagonCPU(CPU))),
      TargetTriple(TT), InstrInfo(initializeSubtargetDependencies(CPU, FS)),
      RegInfo(getHwMode()), TLInfo(TM, *this),
      InstrItins(getInstrItineraryForCPU(CPUString)) {
  Hexagon_MC::addArchSubtarget(this, FS);
  // Beware of the default constructor of InstrItineraryData: it will
  // reset all members to 0.
  assert(InstrItins.Itineraries != nullptr && "InstrItins not initialized");
}

HexagonSubtarget &
HexagonSubtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  std::optional<Hexagon::ArchEnum> ArchVer = Hexagon::getCpu(CPUString);
  if (ArchVer)
    HexagonArchVersion = *ArchVer;
  else
    llvm_unreachable("Unrecognized Hexagon processor version");

  UseHVX128BOps = false;
  UseHVX64BOps = false;
  UseAudioOps = false;
  UseLongCalls = false;

  SubtargetFeatures Features(FS);

  // Turn on QFloat if the HVX version is v68+.
  // The function ParseSubtargetFeatures will set feature bits and initialize
  // subtarget's variables all in one, so there isn't a good way to preprocess
  // the feature string, other than by tinkering with it directly.
  auto IsQFloatFS = [](StringRef F) {
    return F == "+hvx-qfloat" || F == "-hvx-qfloat";
  };
  if (!llvm::count_if(Features.getFeatures(), IsQFloatFS)) {
    auto getHvxVersion = [&Features](StringRef FS) -> StringRef {
      for (StringRef F : llvm::reverse(Features.getFeatures())) {
        if (F.starts_with("+hvxv"))
          return F;
      }
      for (StringRef F : llvm::reverse(Features.getFeatures())) {
        if (F == "-hvx")
          return StringRef();
        if (F.starts_with("+hvx") || F == "-hvx")
          return F.take_front(4);  // Return "+hvx" or "-hvx".
      }
      return StringRef();
    };

    bool AddQFloat = false;
    StringRef HvxVer = getHvxVersion(FS);
    if (HvxVer.starts_with("+hvxv")) {
      int Ver = 0;
      if (!HvxVer.drop_front(5).consumeInteger(10, Ver) && Ver >= 68)
        AddQFloat = true;
    } else if (HvxVer == "+hvx") {
      if (hasV68Ops())
        AddQFloat = true;
    }

    if (AddQFloat)
      Features.AddFeature("+hvx-qfloat");
  }

  std::string FeatureString = Features.getString();
  ParseSubtargetFeatures(CPUString, /*TuneCPU*/ CPUString, FeatureString);

  if (useHVXV68Ops())
    UseHVXFloatingPoint = UseHVXIEEEFPOps || UseHVXQFloatOps;

  if (UseHVXQFloatOps && UseHVXIEEEFPOps && UseHVXFloatingPoint)
    LLVM_DEBUG(
        dbgs() << "Behavior is undefined for simultaneous qfloat and ieee hvx codegen...");

  if (OverrideLongCalls.getPosition())
    UseLongCalls = OverrideLongCalls;

  UseBSBScheduling = hasV60Ops() && EnableBSBSched;

  if (isTinyCore()) {
    // Tiny core has a single thread, so back-to-back scheduling is enabled by
    // default.
    if (!EnableBSBSched.getPosition())
      UseBSBScheduling = false;
  }

  FeatureBitset FeatureBits = getFeatureBits();
  if (HexagonDisableDuplex)
    setFeatureBits(FeatureBits.reset(Hexagon::FeatureDuplex));
  setFeatureBits(Hexagon_MC::completeHVXFeatures(FeatureBits));

  return *this;
}

bool HexagonSubtarget::isHVXElementType(MVT Ty, bool IncludeBool) const {
  if (!useHVXOps())
    return false;
  if (Ty.isVector())
    Ty = Ty.getVectorElementType();
  if (IncludeBool && Ty == MVT::i1)
    return true;
  ArrayRef<MVT> ElemTypes = getHVXElementTypes();
  return llvm::is_contained(ElemTypes, Ty);
}

bool HexagonSubtarget::isHVXVectorType(EVT VecTy, bool IncludeBool) const {
  if (!VecTy.isSimple())
    return false;
  if (!VecTy.isVector() || !useHVXOps() || VecTy.isScalableVector())
    return false;
  MVT ElemTy = VecTy.getSimpleVT().getVectorElementType();
  if (!IncludeBool && ElemTy == MVT::i1)
    return false;

  unsigned HwLen = getVectorLength();
  unsigned NumElems = VecTy.getVectorNumElements();
  ArrayRef<MVT> ElemTypes = getHVXElementTypes();

  if (IncludeBool && ElemTy == MVT::i1) {
    // Boolean HVX vector types are formed from regular HVX vector types
    // by replacing the element type with i1.
    for (MVT T : ElemTypes)
      if (NumElems * T.getSizeInBits() == 8 * HwLen)
        return true;
    return false;
  }

  unsigned VecWidth = VecTy.getSizeInBits();
  if (VecWidth != 8 * HwLen && VecWidth != 16 * HwLen)
    return false;
  return llvm::is_contained(ElemTypes, ElemTy);
}

bool HexagonSubtarget::isTypeForHVX(Type *VecTy, bool IncludeBool) const {
  if (!VecTy->isVectorTy() || isa<ScalableVectorType>(VecTy))
    return false;
  // Avoid types like <2 x i32*>.
  Type *ScalTy = VecTy->getScalarType();
  if (!ScalTy->isIntegerTy() &&
      !(ScalTy->isFloatingPointTy() && useHVXFloatingPoint()))
    return false;
  // The given type may be something like <17 x i32>, which is not MVT,
  // but can be represented as (non-simple) EVT.
  EVT Ty = EVT::getEVT(VecTy, /*HandleUnknown*/false);
  if (!Ty.getVectorElementType().isSimple())
    return false;

  auto isHvxTy = [this, IncludeBool](MVT SimpleTy) {
    if (isHVXVectorType(SimpleTy, IncludeBool))
      return true;
    auto Action = getTargetLowering()->getPreferredVectorAction(SimpleTy);
    return Action == TargetLoweringBase::TypeWidenVector;
  };

  // Round up EVT to have power-of-2 elements, and keep checking if it
  // qualifies for HVX, dividing it in half after each step.
  MVT ElemTy = Ty.getVectorElementType().getSimpleVT();
  unsigned VecLen = PowerOf2Ceil(Ty.getVectorNumElements());
  while (VecLen > 1) {
    MVT SimpleTy = MVT::getVectorVT(ElemTy, VecLen);
    if (SimpleTy.isValid() && isHvxTy(SimpleTy))
      return true;
    VecLen /= 2;
  }

  return false;
}

void HexagonSubtarget::UsrOverflowMutation::apply(ScheduleDAGInstrs *DAG) {
  for (SUnit &SU : DAG->SUnits) {
    if (!SU.isInstr())
      continue;
    SmallVector<SDep, 4> Erase;
    for (auto &D : SU.Preds)
      if (D.getKind() == SDep::Output && D.getReg() == Hexagon::USR_OVF)
        Erase.push_back(D);
    for (auto &E : Erase)
      SU.removePred(E);
  }
}

void HexagonSubtarget::HVXMemLatencyMutation::apply(ScheduleDAGInstrs *DAG) {
  for (SUnit &SU : DAG->SUnits) {
    // Update the latency of chain edges between v60 vector load or store
    // instructions to be 1. These instruction cannot be scheduled in the
    // same packet.
    MachineInstr &MI1 = *SU.getInstr();
    auto *QII = static_cast<const HexagonInstrInfo*>(DAG->TII);
    bool IsStoreMI1 = MI1.mayStore();
    bool IsLoadMI1 = MI1.mayLoad();
    if (!QII->isHVXVec(MI1) || !(IsStoreMI1 || IsLoadMI1))
      continue;
    for (SDep &SI : SU.Succs) {
      if (SI.getKind() != SDep::Order || SI.getLatency() != 0)
        continue;
      MachineInstr &MI2 = *SI.getSUnit()->getInstr();
      if (!QII->isHVXVec(MI2))
        continue;
      if ((IsStoreMI1 && MI2.mayStore()) || (IsLoadMI1 && MI2.mayLoad())) {
        SI.setLatency(1);
        SU.setHeightDirty();
        // Change the dependence in the opposite direction too.
        for (SDep &PI : SI.getSUnit()->Preds) {
          if (PI.getSUnit() != &SU || PI.getKind() != SDep::Order)
            continue;
          PI.setLatency(1);
          SI.getSUnit()->setDepthDirty();
        }
      }
    }
  }
}

// Check if a call and subsequent A2_tfrpi instructions should maintain
// scheduling affinity. We are looking for the TFRI to be consumed in
// the next instruction. This should help reduce the instances of
// double register pairs being allocated and scheduled before a call
// when not used until after the call. This situation is exacerbated
// by the fact that we allocate the pair from the callee saves list,
// leading to excess spills and restores.
bool HexagonSubtarget::CallMutation::shouldTFRICallBind(
      const HexagonInstrInfo &HII, const SUnit &Inst1,
      const SUnit &Inst2) const {
  if (Inst1.getInstr()->getOpcode() != Hexagon::A2_tfrpi)
    return false;

  // TypeXTYPE are 64 bit operations.
  unsigned Type = HII.getType(*Inst2.getInstr());
  return Type == HexagonII::TypeS_2op || Type == HexagonII::TypeS_3op ||
         Type == HexagonII::TypeALU64 || Type == HexagonII::TypeM;
}

void HexagonSubtarget::CallMutation::apply(ScheduleDAGInstrs *DAGInstrs) {
  ScheduleDAGMI *DAG = static_cast<ScheduleDAGMI*>(DAGInstrs);
  SUnit* LastSequentialCall = nullptr;
  // Map from virtual register to physical register from the copy.
  DenseMap<unsigned, unsigned> VRegHoldingReg;
  // Map from the physical register to the instruction that uses virtual
  // register. This is used to create the barrier edge.
  DenseMap<unsigned, SUnit *> LastVRegUse;
  auto &TRI = *DAG->MF.getSubtarget().getRegisterInfo();
  auto &HII = *DAG->MF.getSubtarget<HexagonSubtarget>().getInstrInfo();

  // Currently we only catch the situation when compare gets scheduled
  // before preceding call.
  for (unsigned su = 0, e = DAG->SUnits.size(); su != e; ++su) {
    // Remember the call.
    if (DAG->SUnits[su].getInstr()->isCall())
      LastSequentialCall = &DAG->SUnits[su];
    // Look for a compare that defines a predicate.
    else if (DAG->SUnits[su].getInstr()->isCompare() && LastSequentialCall)
      DAG->addEdge(&DAG->SUnits[su], SDep(LastSequentialCall, SDep::Barrier));
    // Look for call and tfri* instructions.
    else if (SchedPredsCloser && LastSequentialCall && su > 1 && su < e-1 &&
             shouldTFRICallBind(HII, DAG->SUnits[su], DAG->SUnits[su+1]))
      DAG->addEdge(&DAG->SUnits[su], SDep(&DAG->SUnits[su-1], SDep::Barrier));
    // Prevent redundant register copies due to reads and writes of physical
    // registers. The original motivation for this was the code generated
    // between two calls, which are caused both the return value and the
    // argument for the next call being in %r0.
    // Example:
    //   1: <call1>
    //   2: %vreg = COPY %r0
    //   3: <use of %vreg>
    //   4: %r0 = ...
    //   5: <call2>
    // The scheduler would often swap 3 and 4, so an additional register is
    // needed. This code inserts a Barrier dependence between 3 & 4 to prevent
    // this.
    // The code below checks for all the physical registers, not just R0/D0/V0.
    else if (SchedRetvalOptimization) {
      const MachineInstr *MI = DAG->SUnits[su].getInstr();
      if (MI->isCopy() && MI->getOperand(1).getReg().isPhysical()) {
        // %vregX = COPY %r0
        VRegHoldingReg[MI->getOperand(0).getReg()] = MI->getOperand(1).getReg();
        LastVRegUse.erase(MI->getOperand(1).getReg());
      } else {
        for (const MachineOperand &MO : MI->operands()) {
          if (!MO.isReg())
            continue;
          if (MO.isUse() && !MI->isCopy() &&
              VRegHoldingReg.count(MO.getReg())) {
            // <use of %vregX>
            LastVRegUse[VRegHoldingReg[MO.getReg()]] = &DAG->SUnits[su];
          } else if (MO.isDef() && MO.getReg().isPhysical()) {
            for (MCRegAliasIterator AI(MO.getReg(), &TRI, true); AI.isValid();
                 ++AI) {
              if (LastVRegUse.count(*AI) &&
                  LastVRegUse[*AI] != &DAG->SUnits[su])
                // %r0 = ...
                DAG->addEdge(&DAG->SUnits[su], SDep(LastVRegUse[*AI], SDep::Barrier));
              LastVRegUse.erase(*AI);
            }
          }
        }
      }
    }
  }
}

void HexagonSubtarget::BankConflictMutation::apply(ScheduleDAGInstrs *DAG) {
  if (!EnableCheckBankConflict)
    return;

  const auto &HII = static_cast<const HexagonInstrInfo&>(*DAG->TII);

  // Create artificial edges between loads that could likely cause a bank
  // conflict. Since such loads would normally not have any dependency
  // between them, we cannot rely on existing edges.
  for (unsigned i = 0, e = DAG->SUnits.size(); i != e; ++i) {
    SUnit &S0 = DAG->SUnits[i];
    MachineInstr &L0 = *S0.getInstr();
    if (!L0.mayLoad() || L0.mayStore() ||
        HII.getAddrMode(L0) != HexagonII::BaseImmOffset)
      continue;
    int64_t Offset0;
    LocationSize Size0 = 0;
    MachineOperand *BaseOp0 = HII.getBaseAndOffset(L0, Offset0, Size0);
    // Is the access size is longer than the L1 cache line, skip the check.
    if (BaseOp0 == nullptr || !BaseOp0->isReg() || !Size0.hasValue() ||
        Size0.getValue() >= 32)
      continue;
    // Scan only up to 32 instructions ahead (to avoid n^2 complexity).
    for (unsigned j = i+1, m = std::min(i+32, e); j != m; ++j) {
      SUnit &S1 = DAG->SUnits[j];
      MachineInstr &L1 = *S1.getInstr();
      if (!L1.mayLoad() || L1.mayStore() ||
          HII.getAddrMode(L1) != HexagonII::BaseImmOffset)
        continue;
      int64_t Offset1;
      LocationSize Size1 = 0;
      MachineOperand *BaseOp1 = HII.getBaseAndOffset(L1, Offset1, Size1);
      if (BaseOp1 == nullptr || !BaseOp1->isReg() || !Size0.hasValue() ||
          Size1.getValue() >= 32 || BaseOp0->getReg() != BaseOp1->getReg())
        continue;
      // Check bits 3 and 4 of the offset: if they differ, a bank conflict
      // is unlikely.
      if (((Offset0 ^ Offset1) & 0x18) != 0)
        continue;
      // Bits 3 and 4 are the same, add an artificial edge and set extra
      // latency.
      SDep A(&S0, SDep::Artificial);
      A.setLatency(1);
      S1.addPred(A, true);
    }
  }
}

/// Enable use of alias analysis during code generation (during MI
/// scheduling, DAGCombine, etc.).
bool HexagonSubtarget::useAA() const {
  if (OptLevel != CodeGenOptLevel::None)
    return true;
  return false;
}

/// Perform target specific adjustments to the latency of a schedule
/// dependency.
void HexagonSubtarget::adjustSchedDependency(
    SUnit *Src, int SrcOpIdx, SUnit *Dst, int DstOpIdx, SDep &Dep,
    const TargetSchedModel *SchedModel) const {
  if (!Src->isInstr() || !Dst->isInstr())
    return;

  MachineInstr *SrcInst = Src->getInstr();
  MachineInstr *DstInst = Dst->getInstr();
  const HexagonInstrInfo *QII = getInstrInfo();

  // Instructions with .new operands have zero latency.
  SmallSet<SUnit *, 4> ExclSrc;
  SmallSet<SUnit *, 4> ExclDst;
  if (QII->canExecuteInBundle(*SrcInst, *DstInst) &&
      isBestZeroLatency(Src, Dst, QII, ExclSrc, ExclDst)) {
    Dep.setLatency(0);
    return;
  }

  // Set the latency for a copy to zero since we hope that is will get
  // removed.
  if (DstInst->isCopy())
    Dep.setLatency(0);

  // If it's a REG_SEQUENCE/COPY, use its destination instruction to determine
  // the correct latency.
  // If there are multiple uses of the def of COPY/REG_SEQUENCE, set the latency
  // only if the latencies on all the uses are equal, otherwise set it to
  // default.
  if ((DstInst->isRegSequence() || DstInst->isCopy())) {
    Register DReg = DstInst->getOperand(0).getReg();
    std::optional<unsigned> DLatency;
    for (const auto &DDep : Dst->Succs) {
      MachineInstr *DDst = DDep.getSUnit()->getInstr();
      int UseIdx = -1;
      for (unsigned OpNum = 0; OpNum < DDst->getNumOperands(); OpNum++) {
        const MachineOperand &MO = DDst->getOperand(OpNum);
        if (MO.isReg() && MO.getReg() && MO.isUse() && MO.getReg() == DReg) {
          UseIdx = OpNum;
          break;
        }
      }

      if (UseIdx == -1)
        continue;

      std::optional<unsigned> Latency =
          InstrInfo.getOperandLatency(&InstrItins, *SrcInst, 0, *DDst, UseIdx);

      // Set DLatency for the first time.
      if (!DLatency)
        DLatency = Latency;

      // For multiple uses, if the Latency is different across uses, reset
      // DLatency.
      if (DLatency != Latency) {
        DLatency = std::nullopt;
        break;
      }
    }
    Dep.setLatency(DLatency ? *DLatency : 0);
  }

  // Try to schedule uses near definitions to generate .cur.
  ExclSrc.clear();
  ExclDst.clear();
  if (EnableDotCurSched && QII->isToBeScheduledASAP(*SrcInst, *DstInst) &&
      isBestZeroLatency(Src, Dst, QII, ExclSrc, ExclDst)) {
    Dep.setLatency(0);
    return;
  }
  int Latency = Dep.getLatency();
  bool IsArtificial = Dep.isArtificial();
  Latency = updateLatency(*SrcInst, *DstInst, IsArtificial, Latency);
  Dep.setLatency(Latency);
}

void HexagonSubtarget::getPostRAMutations(
    std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  Mutations.push_back(std::make_unique<UsrOverflowMutation>());
  Mutations.push_back(std::make_unique<HVXMemLatencyMutation>());
  Mutations.push_back(std::make_unique<BankConflictMutation>());
}

void HexagonSubtarget::getSMSMutations(
    std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  Mutations.push_back(std::make_unique<UsrOverflowMutation>());
  Mutations.push_back(std::make_unique<HVXMemLatencyMutation>());
}

// Pin the vtable to this file.
void HexagonSubtarget::anchor() {}

bool HexagonSubtarget::enableMachineScheduler() const {
  if (DisableHexagonMISched.getNumOccurrences())
    return !DisableHexagonMISched;
  return true;
}

bool HexagonSubtarget::usePredicatedCalls() const {
  return EnablePredicatedCalls;
}

int HexagonSubtarget::updateLatency(MachineInstr &SrcInst,
                                    MachineInstr &DstInst, bool IsArtificial,
                                    int Latency) const {
  if (IsArtificial)
    return 1;
  if (!hasV60Ops())
    return Latency;

  auto &QII = static_cast<const HexagonInstrInfo &>(*getInstrInfo());
  // BSB scheduling.
  if (QII.isHVXVec(SrcInst) || useBSBScheduling())
    Latency = (Latency + 1) >> 1;
  return Latency;
}

void HexagonSubtarget::restoreLatency(SUnit *Src, SUnit *Dst) const {
  MachineInstr *SrcI = Src->getInstr();
  for (auto &I : Src->Succs) {
    if (!I.isAssignedRegDep() || I.getSUnit() != Dst)
      continue;
    Register DepR = I.getReg();
    int DefIdx = -1;
    for (unsigned OpNum = 0; OpNum < SrcI->getNumOperands(); OpNum++) {
      const MachineOperand &MO = SrcI->getOperand(OpNum);
      bool IsSameOrSubReg = false;
      if (MO.isReg()) {
        Register MOReg = MO.getReg();
        if (DepR.isVirtual()) {
          IsSameOrSubReg = (MOReg == DepR);
        } else {
          IsSameOrSubReg = getRegisterInfo()->isSubRegisterEq(DepR, MOReg);
        }
        if (MO.isDef() && IsSameOrSubReg)
          DefIdx = OpNum;
      }
    }
    assert(DefIdx >= 0 && "Def Reg not found in Src MI");
    MachineInstr *DstI = Dst->getInstr();
    SDep T = I;
    for (unsigned OpNum = 0; OpNum < DstI->getNumOperands(); OpNum++) {
      const MachineOperand &MO = DstI->getOperand(OpNum);
      if (MO.isReg() && MO.isUse() && MO.getReg() == DepR) {
        std::optional<unsigned> Latency = InstrInfo.getOperandLatency(
            &InstrItins, *SrcI, DefIdx, *DstI, OpNum);

        // For some instructions (ex: COPY), we might end up with < 0 latency
        // as they don't have any Itinerary class associated with them.
        if (!Latency)
          Latency = 0;
        bool IsArtificial = I.isArtificial();
        Latency = updateLatency(*SrcI, *DstI, IsArtificial, *Latency);
        I.setLatency(*Latency);
      }
    }

    // Update the latency of opposite edge too.
    T.setSUnit(Src);
    auto F = find(Dst->Preds, T);
    assert(F != Dst->Preds.end());
    F->setLatency(I.getLatency());
  }
}

/// Change the latency between the two SUnits.
void HexagonSubtarget::changeLatency(SUnit *Src, SUnit *Dst, unsigned Lat)
      const {
  for (auto &I : Src->Succs) {
    if (!I.isAssignedRegDep() || I.getSUnit() != Dst)
      continue;
    SDep T = I;
    I.setLatency(Lat);

    // Update the latency of opposite edge too.
    T.setSUnit(Src);
    auto F = find(Dst->Preds, T);
    assert(F != Dst->Preds.end());
    F->setLatency(Lat);
  }
}

/// If the SUnit has a zero latency edge, return the other SUnit.
static SUnit *getZeroLatency(SUnit *N, SmallVector<SDep, 4> &Deps) {
  for (auto &I : Deps)
    if (I.isAssignedRegDep() && I.getLatency() == 0 &&
        !I.getSUnit()->getInstr()->isPseudo())
      return I.getSUnit();
  return nullptr;
}

// Return true if these are the best two instructions to schedule
// together with a zero latency. Only one dependence should have a zero
// latency. If there are multiple choices, choose the best, and change
// the others, if needed.
bool HexagonSubtarget::isBestZeroLatency(SUnit *Src, SUnit *Dst,
      const HexagonInstrInfo *TII, SmallSet<SUnit*, 4> &ExclSrc,
      SmallSet<SUnit*, 4> &ExclDst) const {
  MachineInstr &SrcInst = *Src->getInstr();
  MachineInstr &DstInst = *Dst->getInstr();

  // Ignore Boundary SU nodes as these have null instructions.
  if (Dst->isBoundaryNode())
    return false;

  if (SrcInst.isPHI() || DstInst.isPHI())
    return false;

  if (!TII->isToBeScheduledASAP(SrcInst, DstInst) &&
      !TII->canExecuteInBundle(SrcInst, DstInst))
    return false;

  // The architecture doesn't allow three dependent instructions in the same
  // packet. So, if the destination has a zero latency successor, then it's
  // not a candidate for a zero latency predecessor.
  if (getZeroLatency(Dst, Dst->Succs) != nullptr)
    return false;

  // Check if the Dst instruction is the best candidate first.
  SUnit *Best = nullptr;
  SUnit *DstBest = nullptr;
  SUnit *SrcBest = getZeroLatency(Dst, Dst->Preds);
  if (SrcBest == nullptr || Src->NodeNum >= SrcBest->NodeNum) {
    // Check that Src doesn't have a better candidate.
    DstBest = getZeroLatency(Src, Src->Succs);
    if (DstBest == nullptr || Dst->NodeNum <= DstBest->NodeNum)
      Best = Dst;
  }
  if (Best != Dst)
    return false;

  // The caller frequently adds the same dependence twice. If so, then
  // return true for this case too.
  if ((Src == SrcBest && Dst == DstBest ) ||
      (SrcBest == nullptr && Dst == DstBest) ||
      (Src == SrcBest && Dst == nullptr))
    return true;

  // Reassign the latency for the previous bests, which requires setting
  // the dependence edge in both directions.
  if (SrcBest != nullptr) {
    if (!hasV60Ops())
      changeLatency(SrcBest, Dst, 1);
    else
      restoreLatency(SrcBest, Dst);
  }
  if (DstBest != nullptr) {
    if (!hasV60Ops())
      changeLatency(Src, DstBest, 1);
    else
      restoreLatency(Src, DstBest);
  }

  // Attempt to find another opprotunity for zero latency in a different
  // dependence.
  if (SrcBest && DstBest)
    // If there is an edge from SrcBest to DstBst, then try to change that
    // to 0 now.
    changeLatency(SrcBest, DstBest, 0);
  else if (DstBest) {
    // Check if the previous best destination instruction has a new zero
    // latency dependence opportunity.
    ExclSrc.insert(Src);
    for (auto &I : DstBest->Preds)
      if (ExclSrc.count(I.getSUnit()) == 0 &&
          isBestZeroLatency(I.getSUnit(), DstBest, TII, ExclSrc, ExclDst))
        changeLatency(I.getSUnit(), DstBest, 0);
  } else if (SrcBest) {
    // Check if previous best source instruction has a new zero latency
    // dependence opportunity.
    ExclDst.insert(Dst);
    for (auto &I : SrcBest->Succs)
      if (ExclDst.count(I.getSUnit()) == 0 &&
          isBestZeroLatency(SrcBest, I.getSUnit(), TII, ExclSrc, ExclDst))
        changeLatency(SrcBest, I.getSUnit(), 0);
  }

  return true;
}

unsigned HexagonSubtarget::getL1CacheLineSize() const {
  return 32;
}

unsigned HexagonSubtarget::getL1PrefetchDistance() const {
  return 32;
}

bool HexagonSubtarget::enableSubRegLiveness() const { return true; }

Intrinsic::ID HexagonSubtarget::getIntrinsicId(unsigned Opc) const {
  struct Scalar {
    unsigned Opcode;
    Intrinsic::ID IntId;
  };
  struct Hvx {
    unsigned Opcode;
    Intrinsic::ID Int64Id, Int128Id;
  };

  static Scalar ScalarInts[] = {
#define GET_SCALAR_INTRINSICS
#include "HexagonDepInstrIntrinsics.inc"
#undef GET_SCALAR_INTRINSICS
  };

  static Hvx HvxInts[] = {
#define GET_HVX_INTRINSICS
#include "HexagonDepInstrIntrinsics.inc"
#undef GET_HVX_INTRINSICS
  };

  const auto CmpOpcode = [](auto A, auto B) { return A.Opcode < B.Opcode; };
  [[maybe_unused]] static bool SortedScalar =
      (llvm::sort(ScalarInts, CmpOpcode), true);
  [[maybe_unused]] static bool SortedHvx =
      (llvm::sort(HvxInts, CmpOpcode), true);

  auto [BS, ES] = std::make_pair(std::begin(ScalarInts), std::end(ScalarInts));
  auto [BH, EH] = std::make_pair(std::begin(HvxInts), std::end(HvxInts));

  auto FoundScalar = std::lower_bound(BS, ES, Scalar{Opc, 0}, CmpOpcode);
  if (FoundScalar != ES && FoundScalar->Opcode == Opc)
    return FoundScalar->IntId;

  auto FoundHvx = std::lower_bound(BH, EH, Hvx{Opc, 0, 0}, CmpOpcode);
  if (FoundHvx != EH && FoundHvx->Opcode == Opc) {
    unsigned HwLen = getVectorLength();
    if (HwLen == 64)
      return FoundHvx->Int64Id;
    if (HwLen == 128)
      return FoundHvx->Int128Id;
  }

  std::string error = "Invalid opcode (" + std::to_string(Opc) + ")";
  llvm_unreachable(error.c_str());
  return 0;
}
