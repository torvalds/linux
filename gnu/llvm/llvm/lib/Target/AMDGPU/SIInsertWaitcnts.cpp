//===- SIInsertWaitcnts.cpp - Insert Wait Instructions --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Insert wait instructions for memory reads and writes.
///
/// Memory reads and writes are issued asynchronously, so we need to insert
/// S_WAITCNT instructions when we want to access any of their results or
/// overwrite any register that's used asynchronously.
///
/// TODO: This pass currently keeps one timeline per hardware counter. A more
/// finely-grained approach that keeps one timeline per event type could
/// sometimes get away with generating weaker s_waitcnt instructions. For
/// example, when both SMEM and LDS are in flight and we need to wait for
/// the i-th-last LDS instruction, then an lgkmcnt(i) is actually sufficient,
/// but the pass will currently generate a conservative lgkmcnt(0) because
/// multiple event types are in flight.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIMachineFunctionInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/TargetParser/TargetParser.h"
using namespace llvm;

#define DEBUG_TYPE "si-insert-waitcnts"

DEBUG_COUNTER(ForceExpCounter, DEBUG_TYPE"-forceexp",
              "Force emit s_waitcnt expcnt(0) instrs");
DEBUG_COUNTER(ForceLgkmCounter, DEBUG_TYPE"-forcelgkm",
              "Force emit s_waitcnt lgkmcnt(0) instrs");
DEBUG_COUNTER(ForceVMCounter, DEBUG_TYPE"-forcevm",
              "Force emit s_waitcnt vmcnt(0) instrs");

static cl::opt<bool> ForceEmitZeroFlag(
  "amdgpu-waitcnt-forcezero",
  cl::desc("Force all waitcnt instrs to be emitted as s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0)"),
  cl::init(false), cl::Hidden);

namespace {
// Class of object that encapsulates latest instruction counter score
// associated with the operand.  Used for determining whether
// s_waitcnt instruction needs to be emitted.

enum InstCounterType {
  LOAD_CNT = 0, // VMcnt prior to gfx12.
  DS_CNT,       // LKGMcnt prior to gfx12.
  EXP_CNT,      //
  STORE_CNT,    // VScnt in gfx10/gfx11.
  NUM_NORMAL_INST_CNTS,
  SAMPLE_CNT = NUM_NORMAL_INST_CNTS, // gfx12+ only.
  BVH_CNT,                           // gfx12+ only.
  KM_CNT,                            // gfx12+ only.
  NUM_EXTENDED_INST_CNTS,
  NUM_INST_CNTS = NUM_EXTENDED_INST_CNTS
};
} // namespace

namespace llvm {
template <> struct enum_iteration_traits<InstCounterType> {
  static constexpr bool is_iterable = true;
};
} // namespace llvm

namespace {
// Return an iterator over all counters between LOAD_CNT (the first counter)
// and \c MaxCounter (exclusive, default value yields an enumeration over
// all counters).
auto inst_counter_types(InstCounterType MaxCounter = NUM_INST_CNTS) {
  return enum_seq(LOAD_CNT, MaxCounter);
}

using RegInterval = std::pair<int, int>;

struct HardwareLimits {
  unsigned LoadcntMax; // Corresponds to VMcnt prior to gfx12.
  unsigned ExpcntMax;
  unsigned DscntMax;     // Corresponds to LGKMcnt prior to gfx12.
  unsigned StorecntMax;  // Corresponds to VScnt in gfx10/gfx11.
  unsigned SamplecntMax; // gfx12+ only.
  unsigned BvhcntMax;    // gfx12+ only.
  unsigned KmcntMax;     // gfx12+ only.
};

struct RegisterEncoding {
  unsigned VGPR0;
  unsigned VGPRL;
  unsigned SGPR0;
  unsigned SGPRL;
};

enum WaitEventType {
  VMEM_ACCESS,              // vector-memory read & write
  VMEM_READ_ACCESS,         // vector-memory read
  VMEM_SAMPLER_READ_ACCESS, // vector-memory SAMPLER read (gfx12+ only)
  VMEM_BVH_READ_ACCESS,     // vector-memory BVH read (gfx12+ only)
  VMEM_WRITE_ACCESS,        // vector-memory write that is not scratch
  SCRATCH_WRITE_ACCESS,     // vector-memory write that may be scratch
  LDS_ACCESS,               // lds read & write
  GDS_ACCESS,               // gds read & write
  SQ_MESSAGE,               // send message
  SMEM_ACCESS,              // scalar-memory read & write
  EXP_GPR_LOCK,             // export holding on its data src
  GDS_GPR_LOCK,             // GDS holding on its data and addr src
  EXP_POS_ACCESS,           // write to export position
  EXP_PARAM_ACCESS,         // write to export parameter
  VMW_GPR_LOCK,             // vector-memory write holding on its data src
  EXP_LDS_ACCESS,           // read by ldsdir counting as export
  NUM_WAIT_EVENTS,
};

// The mapping is:
//  0                .. SQ_MAX_PGM_VGPRS-1               real VGPRs
//  SQ_MAX_PGM_VGPRS .. NUM_ALL_VGPRS-1                  extra VGPR-like slots
//  NUM_ALL_VGPRS    .. NUM_ALL_VGPRS+SQ_MAX_PGM_SGPRS-1 real SGPRs
// We reserve a fixed number of VGPR slots in the scoring tables for
// special tokens like SCMEM_LDS (needed for buffer load to LDS).
enum RegisterMapping {
  SQ_MAX_PGM_VGPRS = 512, // Maximum programmable VGPRs across all targets.
  AGPR_OFFSET = 256,      // Maximum programmable ArchVGPRs across all targets.
  SQ_MAX_PGM_SGPRS = 256, // Maximum programmable SGPRs across all targets.
  NUM_EXTRA_VGPRS = 9,    // Reserved slots for DS.
  // Artificial register slots to track LDS writes into specific LDS locations
  // if a location is known. When slots are exhausted or location is
  // unknown use the first slot. The first slot is also always updated in
  // addition to known location's slot to properly generate waits if dependent
  // instruction's location is unknown.
  EXTRA_VGPR_LDS = 0,
  NUM_ALL_VGPRS = SQ_MAX_PGM_VGPRS + NUM_EXTRA_VGPRS, // Where SGPR starts.
};

// Enumerate different types of result-returning VMEM operations. Although
// s_waitcnt orders them all with a single vmcnt counter, in the absence of
// s_waitcnt only instructions of the same VmemType are guaranteed to write
// their results in order -- so there is no need to insert an s_waitcnt between
// two instructions of the same type that write the same vgpr.
enum VmemType {
  // BUF instructions and MIMG instructions without a sampler.
  VMEM_NOSAMPLER,
  // MIMG instructions with a sampler.
  VMEM_SAMPLER,
  // BVH instructions
  VMEM_BVH,
  NUM_VMEM_TYPES
};

// Maps values of InstCounterType to the instruction that waits on that
// counter. Only used if GCNSubtarget::hasExtendedWaitCounts()
// returns true.
static const unsigned instrsForExtendedCounterTypes[NUM_EXTENDED_INST_CNTS] = {
    AMDGPU::S_WAIT_LOADCNT,  AMDGPU::S_WAIT_DSCNT,     AMDGPU::S_WAIT_EXPCNT,
    AMDGPU::S_WAIT_STORECNT, AMDGPU::S_WAIT_SAMPLECNT, AMDGPU::S_WAIT_BVHCNT,
    AMDGPU::S_WAIT_KMCNT};

static bool updateVMCntOnly(const MachineInstr &Inst) {
  return SIInstrInfo::isVMEM(Inst) || SIInstrInfo::isFLATGlobal(Inst) ||
         SIInstrInfo::isFLATScratch(Inst);
}

#ifndef NDEBUG
static bool isNormalMode(InstCounterType MaxCounter) {
  return MaxCounter == NUM_NORMAL_INST_CNTS;
}
#endif // NDEBUG

VmemType getVmemType(const MachineInstr &Inst) {
  assert(updateVMCntOnly(Inst));
  if (!SIInstrInfo::isMIMG(Inst) && !SIInstrInfo::isVIMAGE(Inst) &&
      !SIInstrInfo::isVSAMPLE(Inst))
    return VMEM_NOSAMPLER;
  const AMDGPU::MIMGInfo *Info = AMDGPU::getMIMGInfo(Inst.getOpcode());
  const AMDGPU::MIMGBaseOpcodeInfo *BaseInfo =
      AMDGPU::getMIMGBaseOpcodeInfo(Info->BaseOpcode);
  // We have to make an additional check for isVSAMPLE here since some
  // instructions don't have a sampler, but are still classified as sampler
  // instructions for the purposes of e.g. waitcnt.
  return BaseInfo->BVH                                         ? VMEM_BVH
         : (BaseInfo->Sampler || SIInstrInfo::isVSAMPLE(Inst)) ? VMEM_SAMPLER
                                                               : VMEM_NOSAMPLER;
}

unsigned &getCounterRef(AMDGPU::Waitcnt &Wait, InstCounterType T) {
  switch (T) {
  case LOAD_CNT:
    return Wait.LoadCnt;
  case EXP_CNT:
    return Wait.ExpCnt;
  case DS_CNT:
    return Wait.DsCnt;
  case STORE_CNT:
    return Wait.StoreCnt;
  case SAMPLE_CNT:
    return Wait.SampleCnt;
  case BVH_CNT:
    return Wait.BvhCnt;
  case KM_CNT:
    return Wait.KmCnt;
  default:
    llvm_unreachable("bad InstCounterType");
  }
}

void addWait(AMDGPU::Waitcnt &Wait, InstCounterType T, unsigned Count) {
  unsigned &WC = getCounterRef(Wait, T);
  WC = std::min(WC, Count);
}

void setNoWait(AMDGPU::Waitcnt &Wait, InstCounterType T) {
  getCounterRef(Wait, T) = ~0u;
}

unsigned getWait(AMDGPU::Waitcnt &Wait, InstCounterType T) {
  return getCounterRef(Wait, T);
}

// Mapping from event to counter according to the table masks.
InstCounterType eventCounter(const unsigned *masks, WaitEventType E) {
  for (auto T : inst_counter_types()) {
    if (masks[T] & (1 << E))
      return T;
  }
  llvm_unreachable("event type has no associated counter");
}

// This objects maintains the current score brackets of each wait counter, and
// a per-register scoreboard for each wait counter.
//
// We also maintain the latest score for every event type that can change the
// waitcnt in order to know if there are multiple types of events within
// the brackets. When multiple types of event happen in the bracket,
// wait count may get decreased out of order, therefore we need to put in
// "s_waitcnt 0" before use.
class WaitcntBrackets {
public:
  WaitcntBrackets(const GCNSubtarget *SubTarget, InstCounterType MaxCounter,
                  HardwareLimits Limits, RegisterEncoding Encoding,
                  const unsigned *WaitEventMaskForInst,
                  InstCounterType SmemAccessCounter)
      : ST(SubTarget), MaxCounter(MaxCounter), Limits(Limits),
        Encoding(Encoding), WaitEventMaskForInst(WaitEventMaskForInst),
        SmemAccessCounter(SmemAccessCounter) {}

  unsigned getWaitCountMax(InstCounterType T) const {
    switch (T) {
    case LOAD_CNT:
      return Limits.LoadcntMax;
    case DS_CNT:
      return Limits.DscntMax;
    case EXP_CNT:
      return Limits.ExpcntMax;
    case STORE_CNT:
      return Limits.StorecntMax;
    case SAMPLE_CNT:
      return Limits.SamplecntMax;
    case BVH_CNT:
      return Limits.BvhcntMax;
    case KM_CNT:
      return Limits.KmcntMax;
    default:
      break;
    }
    return 0;
  }

  unsigned getScoreLB(InstCounterType T) const {
    assert(T < NUM_INST_CNTS);
    return ScoreLBs[T];
  }

  unsigned getScoreUB(InstCounterType T) const {
    assert(T < NUM_INST_CNTS);
    return ScoreUBs[T];
  }

  unsigned getScoreRange(InstCounterType T) const {
    return getScoreUB(T) - getScoreLB(T);
  }

  unsigned getRegScore(int GprNo, InstCounterType T) const {
    if (GprNo < NUM_ALL_VGPRS) {
      return VgprScores[T][GprNo];
    }
    assert(T == SmemAccessCounter);
    return SgprScores[GprNo - NUM_ALL_VGPRS];
  }

  bool merge(const WaitcntBrackets &Other);

  RegInterval getRegInterval(const MachineInstr *MI,
                             const MachineRegisterInfo *MRI,
                             const SIRegisterInfo *TRI, unsigned OpNo) const;

  bool counterOutOfOrder(InstCounterType T) const;
  void simplifyWaitcnt(AMDGPU::Waitcnt &Wait) const;
  void simplifyWaitcnt(InstCounterType T, unsigned &Count) const;
  void determineWait(InstCounterType T, int RegNo, AMDGPU::Waitcnt &Wait) const;
  void applyWaitcnt(const AMDGPU::Waitcnt &Wait);
  void applyWaitcnt(InstCounterType T, unsigned Count);
  void updateByEvent(const SIInstrInfo *TII, const SIRegisterInfo *TRI,
                     const MachineRegisterInfo *MRI, WaitEventType E,
                     MachineInstr &MI);

  unsigned hasPendingEvent() const { return PendingEvents; }
  unsigned hasPendingEvent(WaitEventType E) const {
    return PendingEvents & (1 << E);
  }
  unsigned hasPendingEvent(InstCounterType T) const {
    unsigned HasPending = PendingEvents & WaitEventMaskForInst[T];
    assert((HasPending != 0) == (getScoreRange(T) != 0));
    return HasPending;
  }

  bool hasMixedPendingEvents(InstCounterType T) const {
    unsigned Events = hasPendingEvent(T);
    // Return true if more than one bit is set in Events.
    return Events & (Events - 1);
  }

  bool hasPendingFlat() const {
    return ((LastFlat[DS_CNT] > ScoreLBs[DS_CNT] &&
             LastFlat[DS_CNT] <= ScoreUBs[DS_CNT]) ||
            (LastFlat[LOAD_CNT] > ScoreLBs[LOAD_CNT] &&
             LastFlat[LOAD_CNT] <= ScoreUBs[LOAD_CNT]));
  }

  void setPendingFlat() {
    LastFlat[LOAD_CNT] = ScoreUBs[LOAD_CNT];
    LastFlat[DS_CNT] = ScoreUBs[DS_CNT];
  }

  // Return true if there might be pending writes to the specified vgpr by VMEM
  // instructions with types different from V.
  bool hasOtherPendingVmemTypes(int GprNo, VmemType V) const {
    assert(GprNo < NUM_ALL_VGPRS);
    return VgprVmemTypes[GprNo] & ~(1 << V);
  }

  void clearVgprVmemTypes(int GprNo) {
    assert(GprNo < NUM_ALL_VGPRS);
    VgprVmemTypes[GprNo] = 0;
  }

  void setStateOnFunctionEntryOrReturn() {
    setScoreUB(STORE_CNT, getScoreUB(STORE_CNT) + getWaitCountMax(STORE_CNT));
    PendingEvents |= WaitEventMaskForInst[STORE_CNT];
  }

  ArrayRef<const MachineInstr *> getLDSDMAStores() const {
    return LDSDMAStores;
  }

  void print(raw_ostream &);
  void dump() { print(dbgs()); }

private:
  struct MergeInfo {
    unsigned OldLB;
    unsigned OtherLB;
    unsigned MyShift;
    unsigned OtherShift;
  };
  static bool mergeScore(const MergeInfo &M, unsigned &Score,
                         unsigned OtherScore);

  void setScoreLB(InstCounterType T, unsigned Val) {
    assert(T < NUM_INST_CNTS);
    ScoreLBs[T] = Val;
  }

  void setScoreUB(InstCounterType T, unsigned Val) {
    assert(T < NUM_INST_CNTS);
    ScoreUBs[T] = Val;

    if (T != EXP_CNT)
      return;

    if (getScoreRange(EXP_CNT) > getWaitCountMax(EXP_CNT))
      ScoreLBs[EXP_CNT] = ScoreUBs[EXP_CNT] - getWaitCountMax(EXP_CNT);
  }

  void setRegScore(int GprNo, InstCounterType T, unsigned Val) {
    if (GprNo < NUM_ALL_VGPRS) {
      VgprUB = std::max(VgprUB, GprNo);
      VgprScores[T][GprNo] = Val;
    } else {
      assert(T == SmemAccessCounter);
      SgprUB = std::max(SgprUB, GprNo - NUM_ALL_VGPRS);
      SgprScores[GprNo - NUM_ALL_VGPRS] = Val;
    }
  }

  void setExpScore(const MachineInstr *MI, const SIInstrInfo *TII,
                   const SIRegisterInfo *TRI, const MachineRegisterInfo *MRI,
                   unsigned OpNo, unsigned Val);

  const GCNSubtarget *ST = nullptr;
  InstCounterType MaxCounter = NUM_EXTENDED_INST_CNTS;
  HardwareLimits Limits = {};
  RegisterEncoding Encoding = {};
  const unsigned *WaitEventMaskForInst;
  InstCounterType SmemAccessCounter;
  unsigned ScoreLBs[NUM_INST_CNTS] = {0};
  unsigned ScoreUBs[NUM_INST_CNTS] = {0};
  unsigned PendingEvents = 0;
  // Remember the last flat memory operation.
  unsigned LastFlat[NUM_INST_CNTS] = {0};
  // wait_cnt scores for every vgpr.
  // Keep track of the VgprUB and SgprUB to make merge at join efficient.
  int VgprUB = -1;
  int SgprUB = -1;
  unsigned VgprScores[NUM_INST_CNTS][NUM_ALL_VGPRS] = {{0}};
  // Wait cnt scores for every sgpr, only DS_CNT (corresponding to LGKMcnt
  // pre-gfx12) or KM_CNT (gfx12+ only) are relevant.
  unsigned SgprScores[SQ_MAX_PGM_SGPRS] = {0};
  // Bitmask of the VmemTypes of VMEM instructions that might have a pending
  // write to each vgpr.
  unsigned char VgprVmemTypes[NUM_ALL_VGPRS] = {0};
  // Store representative LDS DMA operations. The only useful info here is
  // alias info. One store is kept per unique AAInfo.
  SmallVector<const MachineInstr *, NUM_EXTRA_VGPRS - 1> LDSDMAStores;
};

// This abstracts the logic for generating and updating S_WAIT* instructions
// away from the analysis that determines where they are needed. This was
// done because the set of counters and instructions for waiting on them
// underwent a major shift with gfx12, sufficiently so that having this
// abstraction allows the main analysis logic to be simpler than it would
// otherwise have had to become.
class WaitcntGenerator {
protected:
  const GCNSubtarget *ST = nullptr;
  const SIInstrInfo *TII = nullptr;
  AMDGPU::IsaVersion IV;
  InstCounterType MaxCounter;
  bool OptNone;

public:
  WaitcntGenerator() = default;
  WaitcntGenerator(const MachineFunction &MF, InstCounterType MaxCounter)
      : ST(&MF.getSubtarget<GCNSubtarget>()), TII(ST->getInstrInfo()),
        IV(AMDGPU::getIsaVersion(ST->getCPU())), MaxCounter(MaxCounter),
        OptNone(MF.getFunction().hasOptNone() ||
                MF.getTarget().getOptLevel() == CodeGenOptLevel::None) {}

  // Return true if the current function should be compiled with no
  // optimization.
  bool isOptNone() const { return OptNone; }

  // Edits an existing sequence of wait count instructions according
  // to an incoming Waitcnt value, which is itself updated to reflect
  // any new wait count instructions which may need to be generated by
  // WaitcntGenerator::createNewWaitcnt(). It will return true if any edits
  // were made.
  //
  // This editing will usually be merely updated operands, but it may also
  // delete instructions if the incoming Wait value indicates they are not
  // needed. It may also remove existing instructions for which a wait
  // is needed if it can be determined that it is better to generate new
  // instructions later, as can happen on gfx12.
  virtual bool
  applyPreexistingWaitcnt(WaitcntBrackets &ScoreBrackets,
                          MachineInstr &OldWaitcntInstr, AMDGPU::Waitcnt &Wait,
                          MachineBasicBlock::instr_iterator It) const = 0;

  // Transform a soft waitcnt into a normal one.
  bool promoteSoftWaitCnt(MachineInstr *Waitcnt) const;

  // Generates new wait count instructions according to the  value of
  // Wait, returning true if any new instructions were created.
  virtual bool createNewWaitcnt(MachineBasicBlock &Block,
                                MachineBasicBlock::instr_iterator It,
                                AMDGPU::Waitcnt Wait) = 0;

  // Returns an array of bit masks which can be used to map values in
  // WaitEventType to corresponding counter values in InstCounterType.
  virtual const unsigned *getWaitEventMask() const = 0;

  // Returns a new waitcnt with all counters except VScnt set to 0. If
  // IncludeVSCnt is true, VScnt is set to 0, otherwise it is set to ~0u.
  virtual AMDGPU::Waitcnt getAllZeroWaitcnt(bool IncludeVSCnt) const = 0;

  virtual ~WaitcntGenerator() = default;

  // Create a mask value from the initializer list of wait event types.
  static constexpr unsigned
  eventMask(std::initializer_list<WaitEventType> Events) {
    unsigned Mask = 0;
    for (auto &E : Events)
      Mask |= 1 << E;

    return Mask;
  }
};

class WaitcntGeneratorPreGFX12 : public WaitcntGenerator {
public:
  WaitcntGeneratorPreGFX12() = default;
  WaitcntGeneratorPreGFX12(const MachineFunction &MF)
      : WaitcntGenerator(MF, NUM_NORMAL_INST_CNTS) {}

  bool
  applyPreexistingWaitcnt(WaitcntBrackets &ScoreBrackets,
                          MachineInstr &OldWaitcntInstr, AMDGPU::Waitcnt &Wait,
                          MachineBasicBlock::instr_iterator It) const override;

  bool createNewWaitcnt(MachineBasicBlock &Block,
                        MachineBasicBlock::instr_iterator It,
                        AMDGPU::Waitcnt Wait) override;

  const unsigned *getWaitEventMask() const override {
    assert(ST);

    static const unsigned WaitEventMaskForInstPreGFX12[NUM_INST_CNTS] = {
        eventMask({VMEM_ACCESS, VMEM_READ_ACCESS, VMEM_SAMPLER_READ_ACCESS,
                   VMEM_BVH_READ_ACCESS}),
        eventMask({SMEM_ACCESS, LDS_ACCESS, GDS_ACCESS, SQ_MESSAGE}),
        eventMask({EXP_GPR_LOCK, GDS_GPR_LOCK, VMW_GPR_LOCK, EXP_PARAM_ACCESS,
                   EXP_POS_ACCESS, EXP_LDS_ACCESS}),
        eventMask({VMEM_WRITE_ACCESS, SCRATCH_WRITE_ACCESS}),
        0,
        0,
        0};

    return WaitEventMaskForInstPreGFX12;
  }

  AMDGPU::Waitcnt getAllZeroWaitcnt(bool IncludeVSCnt) const override;
};

class WaitcntGeneratorGFX12Plus : public WaitcntGenerator {
public:
  WaitcntGeneratorGFX12Plus() = default;
  WaitcntGeneratorGFX12Plus(const MachineFunction &MF,
                            InstCounterType MaxCounter)
      : WaitcntGenerator(MF, MaxCounter) {}

  bool
  applyPreexistingWaitcnt(WaitcntBrackets &ScoreBrackets,
                          MachineInstr &OldWaitcntInstr, AMDGPU::Waitcnt &Wait,
                          MachineBasicBlock::instr_iterator It) const override;

  bool createNewWaitcnt(MachineBasicBlock &Block,
                        MachineBasicBlock::instr_iterator It,
                        AMDGPU::Waitcnt Wait) override;

  const unsigned *getWaitEventMask() const override {
    assert(ST);

    static const unsigned WaitEventMaskForInstGFX12Plus[NUM_INST_CNTS] = {
        eventMask({VMEM_ACCESS, VMEM_READ_ACCESS}),
        eventMask({LDS_ACCESS, GDS_ACCESS}),
        eventMask({EXP_GPR_LOCK, GDS_GPR_LOCK, VMW_GPR_LOCK, EXP_PARAM_ACCESS,
                   EXP_POS_ACCESS, EXP_LDS_ACCESS}),
        eventMask({VMEM_WRITE_ACCESS, SCRATCH_WRITE_ACCESS}),
        eventMask({VMEM_SAMPLER_READ_ACCESS}),
        eventMask({VMEM_BVH_READ_ACCESS}),
        eventMask({SMEM_ACCESS, SQ_MESSAGE})};

    return WaitEventMaskForInstGFX12Plus;
  }

  AMDGPU::Waitcnt getAllZeroWaitcnt(bool IncludeVSCnt) const override;
};

class SIInsertWaitcnts : public MachineFunctionPass {
private:
  const GCNSubtarget *ST = nullptr;
  const SIInstrInfo *TII = nullptr;
  const SIRegisterInfo *TRI = nullptr;
  const MachineRegisterInfo *MRI = nullptr;

  DenseMap<const Value *, MachineBasicBlock *> SLoadAddresses;
  DenseMap<MachineBasicBlock *, bool> PreheadersToFlush;
  MachineLoopInfo *MLI;
  MachinePostDominatorTree *PDT;
  AliasAnalysis *AA = nullptr;

  struct BlockInfo {
    std::unique_ptr<WaitcntBrackets> Incoming;
    bool Dirty = true;
  };

  InstCounterType SmemAccessCounter;

  MapVector<MachineBasicBlock *, BlockInfo> BlockInfos;

  // ForceEmitZeroWaitcnts: force all waitcnts insts to be s_waitcnt 0
  // because of amdgpu-waitcnt-forcezero flag
  bool ForceEmitZeroWaitcnts;
  bool ForceEmitWaitcnt[NUM_INST_CNTS];

  // In any given run of this pass, WCG will point to one of these two
  // generator objects, which must have been re-initialised before use
  // from a value made using a subtarget constructor.
  WaitcntGeneratorPreGFX12 WCGPreGFX12;
  WaitcntGeneratorGFX12Plus WCGGFX12Plus;

  WaitcntGenerator *WCG = nullptr;

  // S_ENDPGM instructions before which we should insert a DEALLOC_VGPRS
  // message.
  DenseSet<MachineInstr *> ReleaseVGPRInsts;

  InstCounterType MaxCounter = NUM_NORMAL_INST_CNTS;

public:
  static char ID;

  SIInsertWaitcnts() : MachineFunctionPass(ID) {
    (void)ForceExpCounter;
    (void)ForceLgkmCounter;
    (void)ForceVMCounter;
  }

  bool shouldFlushVmCnt(MachineLoop *ML, WaitcntBrackets &Brackets);
  bool isPreheaderToFlush(MachineBasicBlock &MBB,
                          WaitcntBrackets &ScoreBrackets);
  bool isVMEMOrFlatVMEM(const MachineInstr &MI) const;
  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SI insert wait instructions";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    AU.addUsedIfAvailable<AAResultsWrapperPass>();
    AU.addPreserved<AAResultsWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool isForceEmitWaitcnt() const {
    for (auto T : inst_counter_types())
      if (ForceEmitWaitcnt[T])
        return true;
    return false;
  }

  void setForceEmitWaitcnt() {
// For non-debug builds, ForceEmitWaitcnt has been initialized to false;
// For debug builds, get the debug counter info and adjust if need be
#ifndef NDEBUG
    if (DebugCounter::isCounterSet(ForceExpCounter) &&
        DebugCounter::shouldExecute(ForceExpCounter)) {
      ForceEmitWaitcnt[EXP_CNT] = true;
    } else {
      ForceEmitWaitcnt[EXP_CNT] = false;
    }

    if (DebugCounter::isCounterSet(ForceLgkmCounter) &&
        DebugCounter::shouldExecute(ForceLgkmCounter)) {
      ForceEmitWaitcnt[DS_CNT] = true;
      ForceEmitWaitcnt[KM_CNT] = true;
    } else {
      ForceEmitWaitcnt[DS_CNT] = false;
      ForceEmitWaitcnt[KM_CNT] = false;
    }

    if (DebugCounter::isCounterSet(ForceVMCounter) &&
        DebugCounter::shouldExecute(ForceVMCounter)) {
      ForceEmitWaitcnt[LOAD_CNT] = true;
      ForceEmitWaitcnt[SAMPLE_CNT] = true;
      ForceEmitWaitcnt[BVH_CNT] = true;
    } else {
      ForceEmitWaitcnt[LOAD_CNT] = false;
      ForceEmitWaitcnt[SAMPLE_CNT] = false;
      ForceEmitWaitcnt[BVH_CNT] = false;
    }
#endif // NDEBUG
  }

  // Return the appropriate VMEM_*_ACCESS type for Inst, which must be a VMEM or
  // FLAT instruction.
  WaitEventType getVmemWaitEventType(const MachineInstr &Inst) const {
    // Maps VMEM access types to their corresponding WaitEventType.
    static const WaitEventType VmemReadMapping[NUM_VMEM_TYPES] = {
        VMEM_READ_ACCESS, VMEM_SAMPLER_READ_ACCESS, VMEM_BVH_READ_ACCESS};

    assert(SIInstrInfo::isVMEM(Inst) || SIInstrInfo::isFLAT(Inst));
    // LDS DMA loads are also stores, but on the LDS side. On the VMEM side
    // these should use VM_CNT.
    if (!ST->hasVscnt() || SIInstrInfo::mayWriteLDSThroughDMA(Inst))
      return VMEM_ACCESS;
    if (Inst.mayStore() &&
        (!Inst.mayLoad() || SIInstrInfo::isAtomicNoRet(Inst))) {
      // FLAT and SCRATCH instructions may access scratch. Other VMEM
      // instructions do not.
      if (SIInstrInfo::isFLAT(Inst) && mayAccessScratchThroughFlat(Inst))
        return SCRATCH_WRITE_ACCESS;
      return VMEM_WRITE_ACCESS;
    }
    if (!ST->hasExtendedWaitCounts() || SIInstrInfo::isFLAT(Inst))
      return VMEM_READ_ACCESS;
    return VmemReadMapping[getVmemType(Inst)];
  }

  bool mayAccessVMEMThroughFlat(const MachineInstr &MI) const;
  bool mayAccessLDSThroughFlat(const MachineInstr &MI) const;
  bool mayAccessScratchThroughFlat(const MachineInstr &MI) const;
  bool generateWaitcntInstBefore(MachineInstr &MI,
                                 WaitcntBrackets &ScoreBrackets,
                                 MachineInstr *OldWaitcntInstr,
                                 bool FlushVmCnt);
  bool generateWaitcnt(AMDGPU::Waitcnt Wait,
                       MachineBasicBlock::instr_iterator It,
                       MachineBasicBlock &Block, WaitcntBrackets &ScoreBrackets,
                       MachineInstr *OldWaitcntInstr);
  void updateEventWaitcntAfter(MachineInstr &Inst,
                               WaitcntBrackets *ScoreBrackets);
  bool insertWaitcntInBlock(MachineFunction &MF, MachineBasicBlock &Block,
                            WaitcntBrackets &ScoreBrackets);
};

} // end anonymous namespace

RegInterval WaitcntBrackets::getRegInterval(const MachineInstr *MI,
                                            const MachineRegisterInfo *MRI,
                                            const SIRegisterInfo *TRI,
                                            unsigned OpNo) const {
  const MachineOperand &Op = MI->getOperand(OpNo);
  if (!TRI->isInAllocatableClass(Op.getReg()))
    return {-1, -1};

  // A use via a PW operand does not need a waitcnt.
  // A partial write is not a WAW.
  assert(!Op.getSubReg() || !Op.isUndef());

  RegInterval Result;

  unsigned Reg = TRI->getEncodingValue(AMDGPU::getMCReg(Op.getReg(), *ST)) &
                 AMDGPU::HWEncoding::REG_IDX_MASK;

  if (TRI->isVectorRegister(*MRI, Op.getReg())) {
    assert(Reg >= Encoding.VGPR0 && Reg <= Encoding.VGPRL);
    Result.first = Reg - Encoding.VGPR0;
    if (TRI->isAGPR(*MRI, Op.getReg()))
      Result.first += AGPR_OFFSET;
    assert(Result.first >= 0 && Result.first < SQ_MAX_PGM_VGPRS);
  } else if (TRI->isSGPRReg(*MRI, Op.getReg())) {
    assert(Reg >= Encoding.SGPR0 && Reg < SQ_MAX_PGM_SGPRS);
    Result.first = Reg - Encoding.SGPR0 + NUM_ALL_VGPRS;
    assert(Result.first >= NUM_ALL_VGPRS &&
           Result.first < SQ_MAX_PGM_SGPRS + NUM_ALL_VGPRS);
  }
  // TODO: Handle TTMP
  // else if (TRI->isTTMP(*MRI, Reg.getReg())) ...
  else
    return {-1, -1};

  const TargetRegisterClass *RC = TRI->getPhysRegBaseClass(Op.getReg());
  unsigned Size = TRI->getRegSizeInBits(*RC);
  Result.second = Result.first + ((Size + 16) / 32);

  return Result;
}

void WaitcntBrackets::setExpScore(const MachineInstr *MI,
                                  const SIInstrInfo *TII,
                                  const SIRegisterInfo *TRI,
                                  const MachineRegisterInfo *MRI, unsigned OpNo,
                                  unsigned Val) {
  RegInterval Interval = getRegInterval(MI, MRI, TRI, OpNo);
  assert(TRI->isVectorRegister(*MRI, MI->getOperand(OpNo).getReg()));
  for (int RegNo = Interval.first; RegNo < Interval.second; ++RegNo) {
    setRegScore(RegNo, EXP_CNT, Val);
  }
}

void WaitcntBrackets::updateByEvent(const SIInstrInfo *TII,
                                    const SIRegisterInfo *TRI,
                                    const MachineRegisterInfo *MRI,
                                    WaitEventType E, MachineInstr &Inst) {
  InstCounterType T = eventCounter(WaitEventMaskForInst, E);

  unsigned UB = getScoreUB(T);
  unsigned CurrScore = UB + 1;
  if (CurrScore == 0)
    report_fatal_error("InsertWaitcnt score wraparound");
  // PendingEvents and ScoreUB need to be update regardless if this event
  // changes the score of a register or not.
  // Examples including vm_cnt when buffer-store or lgkm_cnt when send-message.
  PendingEvents |= 1 << E;
  setScoreUB(T, CurrScore);

  if (T == EXP_CNT) {
    // Put score on the source vgprs. If this is a store, just use those
    // specific register(s).
    if (TII->isDS(Inst) && (Inst.mayStore() || Inst.mayLoad())) {
      int AddrOpIdx =
          AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::addr);
      // All GDS operations must protect their address register (same as
      // export.)
      if (AddrOpIdx != -1) {
        setExpScore(&Inst, TII, TRI, MRI, AddrOpIdx, CurrScore);
      }

      if (Inst.mayStore()) {
        if (AMDGPU::hasNamedOperand(Inst.getOpcode(), AMDGPU::OpName::data0)) {
          setExpScore(
              &Inst, TII, TRI, MRI,
              AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::data0),
              CurrScore);
        }
        if (AMDGPU::hasNamedOperand(Inst.getOpcode(), AMDGPU::OpName::data1)) {
          setExpScore(&Inst, TII, TRI, MRI,
                      AMDGPU::getNamedOperandIdx(Inst.getOpcode(),
                                                 AMDGPU::OpName::data1),
                      CurrScore);
        }
      } else if (SIInstrInfo::isAtomicRet(Inst) && !SIInstrInfo::isGWS(Inst) &&
                 Inst.getOpcode() != AMDGPU::DS_APPEND &&
                 Inst.getOpcode() != AMDGPU::DS_CONSUME &&
                 Inst.getOpcode() != AMDGPU::DS_ORDERED_COUNT) {
        for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
          const MachineOperand &Op = Inst.getOperand(I);
          if (Op.isReg() && !Op.isDef() &&
              TRI->isVectorRegister(*MRI, Op.getReg())) {
            setExpScore(&Inst, TII, TRI, MRI, I, CurrScore);
          }
        }
      }
    } else if (TII->isFLAT(Inst)) {
      if (Inst.mayStore()) {
        setExpScore(
            &Inst, TII, TRI, MRI,
            AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::data),
            CurrScore);
      } else if (SIInstrInfo::isAtomicRet(Inst)) {
        setExpScore(
            &Inst, TII, TRI, MRI,
            AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::data),
            CurrScore);
      }
    } else if (TII->isMIMG(Inst)) {
      if (Inst.mayStore()) {
        setExpScore(&Inst, TII, TRI, MRI, 0, CurrScore);
      } else if (SIInstrInfo::isAtomicRet(Inst)) {
        setExpScore(
            &Inst, TII, TRI, MRI,
            AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::data),
            CurrScore);
      }
    } else if (TII->isMTBUF(Inst)) {
      if (Inst.mayStore()) {
        setExpScore(&Inst, TII, TRI, MRI, 0, CurrScore);
      }
    } else if (TII->isMUBUF(Inst)) {
      if (Inst.mayStore()) {
        setExpScore(&Inst, TII, TRI, MRI, 0, CurrScore);
      } else if (SIInstrInfo::isAtomicRet(Inst)) {
        setExpScore(
            &Inst, TII, TRI, MRI,
            AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::data),
            CurrScore);
      }
    } else if (TII->isLDSDIR(Inst)) {
      // LDSDIR instructions attach the score to the destination.
      setExpScore(
          &Inst, TII, TRI, MRI,
          AMDGPU::getNamedOperandIdx(Inst.getOpcode(), AMDGPU::OpName::vdst),
          CurrScore);
    } else {
      if (TII->isEXP(Inst)) {
        // For export the destination registers are really temps that
        // can be used as the actual source after export patching, so
        // we need to treat them like sources and set the EXP_CNT
        // score.
        for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
          MachineOperand &DefMO = Inst.getOperand(I);
          if (DefMO.isReg() && DefMO.isDef() &&
              TRI->isVGPR(*MRI, DefMO.getReg())) {
            setRegScore(
                TRI->getEncodingValue(AMDGPU::getMCReg(DefMO.getReg(), *ST)),
                EXP_CNT, CurrScore);
          }
        }
      }
      for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
        MachineOperand &MO = Inst.getOperand(I);
        if (MO.isReg() && !MO.isDef() &&
            TRI->isVectorRegister(*MRI, MO.getReg())) {
          setExpScore(&Inst, TII, TRI, MRI, I, CurrScore);
        }
      }
    }
  } else /* LGKM_CNT || EXP_CNT || VS_CNT || NUM_INST_CNTS */ {
    // Match the score to the destination registers.
    for (unsigned I = 0, E = Inst.getNumOperands(); I != E; ++I) {
      auto &Op = Inst.getOperand(I);
      if (!Op.isReg() || !Op.isDef())
        continue;
      RegInterval Interval = getRegInterval(&Inst, MRI, TRI, I);
      if (T == LOAD_CNT || T == SAMPLE_CNT || T == BVH_CNT) {
        if (Interval.first >= NUM_ALL_VGPRS)
          continue;
        if (updateVMCntOnly(Inst)) {
          // updateVMCntOnly should only leave us with VGPRs
          // MUBUF, MTBUF, MIMG, FlatGlobal, and FlatScratch only have VGPR/AGPR
          // defs. That's required for a sane index into `VgprMemTypes` below
          assert(TRI->isVectorRegister(*MRI, Op.getReg()));
          VmemType V = getVmemType(Inst);
          for (int RegNo = Interval.first; RegNo < Interval.second; ++RegNo)
            VgprVmemTypes[RegNo] |= 1 << V;
        }
      }
      for (int RegNo = Interval.first; RegNo < Interval.second; ++RegNo) {
        setRegScore(RegNo, T, CurrScore);
      }
    }
    if (Inst.mayStore() &&
        (TII->isDS(Inst) || TII->mayWriteLDSThroughDMA(Inst))) {
      // MUBUF and FLAT LDS DMA operations need a wait on vmcnt before LDS
      // written can be accessed. A load from LDS to VMEM does not need a wait.
      unsigned Slot = 0;
      for (const auto *MemOp : Inst.memoperands()) {
        if (!MemOp->isStore() ||
            MemOp->getAddrSpace() != AMDGPUAS::LOCAL_ADDRESS)
          continue;
        // Comparing just AA info does not guarantee memoperands are equal
        // in general, but this is so for LDS DMA in practice.
        auto AAI = MemOp->getAAInfo();
        // Alias scope information gives a way to definitely identify an
        // original memory object and practically produced in the module LDS
        // lowering pass. If there is no scope available we will not be able
        // to disambiguate LDS aliasing as after the module lowering all LDS
        // is squashed into a single big object. Do not attempt to use one of
        // the limited LDSDMAStores for something we will not be able to use
        // anyway.
        if (!AAI || !AAI.Scope)
          break;
        for (unsigned I = 0, E = LDSDMAStores.size(); I != E && !Slot; ++I) {
          for (const auto *MemOp : LDSDMAStores[I]->memoperands()) {
            if (MemOp->isStore() && AAI == MemOp->getAAInfo()) {
              Slot = I + 1;
              break;
            }
          }
        }
        if (Slot || LDSDMAStores.size() == NUM_EXTRA_VGPRS - 1)
          break;
        LDSDMAStores.push_back(&Inst);
        Slot = LDSDMAStores.size();
        break;
      }
      setRegScore(SQ_MAX_PGM_VGPRS + EXTRA_VGPR_LDS + Slot, T, CurrScore);
      if (Slot)
        setRegScore(SQ_MAX_PGM_VGPRS + EXTRA_VGPR_LDS, T, CurrScore);
    }
  }
}

void WaitcntBrackets::print(raw_ostream &OS) {
  OS << '\n';
  for (auto T : inst_counter_types(MaxCounter)) {
    unsigned SR = getScoreRange(T);

    switch (T) {
    case LOAD_CNT:
      OS << "    " << (ST->hasExtendedWaitCounts() ? "LOAD" : "VM") << "_CNT("
         << SR << "): ";
      break;
    case DS_CNT:
      OS << "    " << (ST->hasExtendedWaitCounts() ? "DS" : "LGKM") << "_CNT("
         << SR << "): ";
      break;
    case EXP_CNT:
      OS << "    EXP_CNT(" << SR << "): ";
      break;
    case STORE_CNT:
      OS << "    " << (ST->hasExtendedWaitCounts() ? "STORE" : "VS") << "_CNT("
         << SR << "): ";
      break;
    case SAMPLE_CNT:
      OS << "    SAMPLE_CNT(" << SR << "): ";
      break;
    case BVH_CNT:
      OS << "    BVH_CNT(" << SR << "): ";
      break;
    case KM_CNT:
      OS << "    KM_CNT(" << SR << "): ";
      break;
    default:
      OS << "    UNKNOWN(" << SR << "): ";
      break;
    }

    if (SR != 0) {
      // Print vgpr scores.
      unsigned LB = getScoreLB(T);

      for (int J = 0; J <= VgprUB; J++) {
        unsigned RegScore = getRegScore(J, T);
        if (RegScore <= LB)
          continue;
        unsigned RelScore = RegScore - LB - 1;
        if (J < SQ_MAX_PGM_VGPRS + EXTRA_VGPR_LDS) {
          OS << RelScore << ":v" << J << " ";
        } else {
          OS << RelScore << ":ds ";
        }
      }
      // Also need to print sgpr scores for lgkm_cnt.
      if (T == SmemAccessCounter) {
        for (int J = 0; J <= SgprUB; J++) {
          unsigned RegScore = getRegScore(J + NUM_ALL_VGPRS, T);
          if (RegScore <= LB)
            continue;
          unsigned RelScore = RegScore - LB - 1;
          OS << RelScore << ":s" << J << " ";
        }
      }
    }
    OS << '\n';
  }
  OS << '\n';
}

/// Simplify the waitcnt, in the sense of removing redundant counts, and return
/// whether a waitcnt instruction is needed at all.
void WaitcntBrackets::simplifyWaitcnt(AMDGPU::Waitcnt &Wait) const {
  simplifyWaitcnt(LOAD_CNT, Wait.LoadCnt);
  simplifyWaitcnt(EXP_CNT, Wait.ExpCnt);
  simplifyWaitcnt(DS_CNT, Wait.DsCnt);
  simplifyWaitcnt(STORE_CNT, Wait.StoreCnt);
  simplifyWaitcnt(SAMPLE_CNT, Wait.SampleCnt);
  simplifyWaitcnt(BVH_CNT, Wait.BvhCnt);
  simplifyWaitcnt(KM_CNT, Wait.KmCnt);
}

void WaitcntBrackets::simplifyWaitcnt(InstCounterType T,
                                      unsigned &Count) const {
  // The number of outstanding events for this type, T, can be calculated
  // as (UB - LB). If the current Count is greater than or equal to the number
  // of outstanding events, then the wait for this counter is redundant.
  if (Count >= getScoreRange(T))
    Count = ~0u;
}

void WaitcntBrackets::determineWait(InstCounterType T, int RegNo,
                                    AMDGPU::Waitcnt &Wait) const {
  unsigned ScoreToWait = getRegScore(RegNo, T);

  // If the score of src_operand falls within the bracket, we need an
  // s_waitcnt instruction.
  const unsigned LB = getScoreLB(T);
  const unsigned UB = getScoreUB(T);
  if ((UB >= ScoreToWait) && (ScoreToWait > LB)) {
    if ((T == LOAD_CNT || T == DS_CNT) && hasPendingFlat() &&
        !ST->hasFlatLgkmVMemCountInOrder()) {
      // If there is a pending FLAT operation, and this is a VMem or LGKM
      // waitcnt and the target can report early completion, then we need
      // to force a waitcnt 0.
      addWait(Wait, T, 0);
    } else if (counterOutOfOrder(T)) {
      // Counter can get decremented out-of-order when there
      // are multiple types event in the bracket. Also emit an s_wait counter
      // with a conservative value of 0 for the counter.
      addWait(Wait, T, 0);
    } else {
      // If a counter has been maxed out avoid overflow by waiting for
      // MAX(CounterType) - 1 instead.
      unsigned NeededWait = std::min(UB - ScoreToWait, getWaitCountMax(T) - 1);
      addWait(Wait, T, NeededWait);
    }
  }
}

void WaitcntBrackets::applyWaitcnt(const AMDGPU::Waitcnt &Wait) {
  applyWaitcnt(LOAD_CNT, Wait.LoadCnt);
  applyWaitcnt(EXP_CNT, Wait.ExpCnt);
  applyWaitcnt(DS_CNT, Wait.DsCnt);
  applyWaitcnt(STORE_CNT, Wait.StoreCnt);
  applyWaitcnt(SAMPLE_CNT, Wait.SampleCnt);
  applyWaitcnt(BVH_CNT, Wait.BvhCnt);
  applyWaitcnt(KM_CNT, Wait.KmCnt);
}

void WaitcntBrackets::applyWaitcnt(InstCounterType T, unsigned Count) {
  const unsigned UB = getScoreUB(T);
  if (Count >= UB)
    return;
  if (Count != 0) {
    if (counterOutOfOrder(T))
      return;
    setScoreLB(T, std::max(getScoreLB(T), UB - Count));
  } else {
    setScoreLB(T, UB);
    PendingEvents &= ~WaitEventMaskForInst[T];
  }
}

// Where there are multiple types of event in the bracket of a counter,
// the decrement may go out of order.
bool WaitcntBrackets::counterOutOfOrder(InstCounterType T) const {
  // Scalar memory read always can go out of order.
  if (T == SmemAccessCounter && hasPendingEvent(SMEM_ACCESS))
    return true;
  return hasMixedPendingEvents(T);
}

INITIALIZE_PASS_BEGIN(SIInsertWaitcnts, DEBUG_TYPE, "SI Insert Waitcnts", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(SIInsertWaitcnts, DEBUG_TYPE, "SI Insert Waitcnts", false,
                    false)

char SIInsertWaitcnts::ID = 0;

char &llvm::SIInsertWaitcntsID = SIInsertWaitcnts::ID;

FunctionPass *llvm::createSIInsertWaitcntsPass() {
  return new SIInsertWaitcnts();
}

static bool updateOperandIfDifferent(MachineInstr &MI, uint16_t OpName,
                                     unsigned NewEnc) {
  int OpIdx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), OpName);
  assert(OpIdx >= 0);

  MachineOperand &MO = MI.getOperand(OpIdx);

  if (NewEnc == MO.getImm())
    return false;

  MO.setImm(NewEnc);
  return true;
}

/// Determine if \p MI is a gfx12+ single-counter S_WAIT_*CNT instruction,
/// and if so, which counter it is waiting on.
static std::optional<InstCounterType> counterTypeForInstr(unsigned Opcode) {
  switch (Opcode) {
  case AMDGPU::S_WAIT_LOADCNT:
    return LOAD_CNT;
  case AMDGPU::S_WAIT_EXPCNT:
    return EXP_CNT;
  case AMDGPU::S_WAIT_STORECNT:
    return STORE_CNT;
  case AMDGPU::S_WAIT_SAMPLECNT:
    return SAMPLE_CNT;
  case AMDGPU::S_WAIT_BVHCNT:
    return BVH_CNT;
  case AMDGPU::S_WAIT_DSCNT:
    return DS_CNT;
  case AMDGPU::S_WAIT_KMCNT:
    return KM_CNT;
  default:
    return {};
  }
}

bool WaitcntGenerator::promoteSoftWaitCnt(MachineInstr *Waitcnt) const {
  unsigned Opcode = SIInstrInfo::getNonSoftWaitcntOpcode(Waitcnt->getOpcode());
  if (Opcode == Waitcnt->getOpcode())
    return false;

  Waitcnt->setDesc(TII->get(Opcode));
  return true;
}

/// Combine consecutive S_WAITCNT and S_WAITCNT_VSCNT instructions that
/// precede \p It and follow \p OldWaitcntInstr and apply any extra waits
/// from \p Wait that were added by previous passes. Currently this pass
/// conservatively assumes that these preexisting waits are required for
/// correctness.
bool WaitcntGeneratorPreGFX12::applyPreexistingWaitcnt(
    WaitcntBrackets &ScoreBrackets, MachineInstr &OldWaitcntInstr,
    AMDGPU::Waitcnt &Wait, MachineBasicBlock::instr_iterator It) const {
  assert(ST);
  assert(isNormalMode(MaxCounter));

  bool Modified = false;
  MachineInstr *WaitcntInstr = nullptr;
  MachineInstr *WaitcntVsCntInstr = nullptr;

  for (auto &II :
       make_early_inc_range(make_range(OldWaitcntInstr.getIterator(), It))) {
    if (II.isMetaInstruction())
      continue;

    unsigned Opcode = SIInstrInfo::getNonSoftWaitcntOpcode(II.getOpcode());
    bool TrySimplify = Opcode != II.getOpcode() && !OptNone;

    // Update required wait count. If this is a soft waitcnt (= it was added
    // by an earlier pass), it may be entirely removed.
    if (Opcode == AMDGPU::S_WAITCNT) {
      unsigned IEnc = II.getOperand(0).getImm();
      AMDGPU::Waitcnt OldWait = AMDGPU::decodeWaitcnt(IV, IEnc);
      if (TrySimplify)
        ScoreBrackets.simplifyWaitcnt(OldWait);
      Wait = Wait.combined(OldWait);

      // Merge consecutive waitcnt of the same type by erasing multiples.
      if (WaitcntInstr || (!Wait.hasWaitExceptStoreCnt() && TrySimplify)) {
        II.eraseFromParent();
        Modified = true;
      } else
        WaitcntInstr = &II;
    } else {
      assert(Opcode == AMDGPU::S_WAITCNT_VSCNT);
      assert(II.getOperand(0).getReg() == AMDGPU::SGPR_NULL);

      unsigned OldVSCnt =
          TII->getNamedOperand(II, AMDGPU::OpName::simm16)->getImm();
      if (TrySimplify)
        ScoreBrackets.simplifyWaitcnt(InstCounterType::STORE_CNT, OldVSCnt);
      Wait.StoreCnt = std::min(Wait.StoreCnt, OldVSCnt);

      if (WaitcntVsCntInstr || (!Wait.hasWaitStoreCnt() && TrySimplify)) {
        II.eraseFromParent();
        Modified = true;
      } else
        WaitcntVsCntInstr = &II;
    }
  }

  if (WaitcntInstr) {
    Modified |= updateOperandIfDifferent(*WaitcntInstr, AMDGPU::OpName::simm16,
                                         AMDGPU::encodeWaitcnt(IV, Wait));
    Modified |= promoteSoftWaitCnt(WaitcntInstr);

    ScoreBrackets.applyWaitcnt(LOAD_CNT, Wait.LoadCnt);
    ScoreBrackets.applyWaitcnt(EXP_CNT, Wait.ExpCnt);
    ScoreBrackets.applyWaitcnt(DS_CNT, Wait.DsCnt);
    Wait.LoadCnt = ~0u;
    Wait.ExpCnt = ~0u;
    Wait.DsCnt = ~0u;

    LLVM_DEBUG(It == WaitcntInstr->getParent()->end()
                   ? dbgs()
                         << "applyPreexistingWaitcnt\n"
                         << "New Instr at block end: " << *WaitcntInstr << '\n'
                   : dbgs() << "applyPreexistingWaitcnt\n"
                            << "Old Instr: " << *It
                            << "New Instr: " << *WaitcntInstr << '\n');
  }

  if (WaitcntVsCntInstr) {
    Modified |= updateOperandIfDifferent(*WaitcntVsCntInstr,
                                         AMDGPU::OpName::simm16, Wait.StoreCnt);
    Modified |= promoteSoftWaitCnt(WaitcntVsCntInstr);

    ScoreBrackets.applyWaitcnt(STORE_CNT, Wait.StoreCnt);
    Wait.StoreCnt = ~0u;

    LLVM_DEBUG(It == WaitcntVsCntInstr->getParent()->end()
                   ? dbgs() << "applyPreexistingWaitcnt\n"
                            << "New Instr at block end: " << *WaitcntVsCntInstr
                            << '\n'
                   : dbgs() << "applyPreexistingWaitcnt\n"
                            << "Old Instr: " << *It
                            << "New Instr: " << *WaitcntVsCntInstr << '\n');
  }

  return Modified;
}

/// Generate S_WAITCNT and/or S_WAITCNT_VSCNT instructions for any
/// required counters in \p Wait
bool WaitcntGeneratorPreGFX12::createNewWaitcnt(
    MachineBasicBlock &Block, MachineBasicBlock::instr_iterator It,
    AMDGPU::Waitcnt Wait) {
  assert(ST);
  assert(isNormalMode(MaxCounter));

  bool Modified = false;
  const DebugLoc &DL = Block.findDebugLoc(It);

  // Waits for VMcnt, LKGMcnt and/or EXPcnt are encoded together into a
  // single instruction while VScnt has its own instruction.
  if (Wait.hasWaitExceptStoreCnt()) {
    unsigned Enc = AMDGPU::encodeWaitcnt(IV, Wait);
    [[maybe_unused]] auto SWaitInst =
        BuildMI(Block, It, DL, TII->get(AMDGPU::S_WAITCNT)).addImm(Enc);
    Modified = true;

    LLVM_DEBUG(dbgs() << "generateWaitcnt\n";
               if (It != Block.instr_end()) dbgs() << "Old Instr: " << *It;
               dbgs() << "New Instr: " << *SWaitInst << '\n');
  }

  if (Wait.hasWaitStoreCnt()) {
    assert(ST->hasVscnt());

    [[maybe_unused]] auto SWaitInst =
        BuildMI(Block, It, DL, TII->get(AMDGPU::S_WAITCNT_VSCNT))
            .addReg(AMDGPU::SGPR_NULL, RegState::Undef)
            .addImm(Wait.StoreCnt);
    Modified = true;

    LLVM_DEBUG(dbgs() << "generateWaitcnt\n";
               if (It != Block.instr_end()) dbgs() << "Old Instr: " << *It;
               dbgs() << "New Instr: " << *SWaitInst << '\n');
  }

  return Modified;
}

AMDGPU::Waitcnt
WaitcntGeneratorPreGFX12::getAllZeroWaitcnt(bool IncludeVSCnt) const {
  return AMDGPU::Waitcnt(0, 0, 0, IncludeVSCnt && ST->hasVscnt() ? 0 : ~0u);
}

AMDGPU::Waitcnt
WaitcntGeneratorGFX12Plus::getAllZeroWaitcnt(bool IncludeVSCnt) const {
  return AMDGPU::Waitcnt(0, 0, 0, IncludeVSCnt ? 0 : ~0u, 0, 0, 0);
}

/// Combine consecutive S_WAIT_*CNT instructions that precede \p It and
/// follow \p OldWaitcntInstr and apply any extra waits from \p Wait that
/// were added by previous passes. Currently this pass conservatively
/// assumes that these preexisting waits are required for correctness.
bool WaitcntGeneratorGFX12Plus::applyPreexistingWaitcnt(
    WaitcntBrackets &ScoreBrackets, MachineInstr &OldWaitcntInstr,
    AMDGPU::Waitcnt &Wait, MachineBasicBlock::instr_iterator It) const {
  assert(ST);
  assert(!isNormalMode(MaxCounter));

  bool Modified = false;
  MachineInstr *CombinedLoadDsCntInstr = nullptr;
  MachineInstr *CombinedStoreDsCntInstr = nullptr;
  MachineInstr *WaitInstrs[NUM_EXTENDED_INST_CNTS] = {};

  for (auto &II :
       make_early_inc_range(make_range(OldWaitcntInstr.getIterator(), It))) {
    if (II.isMetaInstruction())
      continue;

    MachineInstr **UpdatableInstr;

    // Update required wait count. If this is a soft waitcnt (= it was added
    // by an earlier pass), it may be entirely removed.

    unsigned Opcode = SIInstrInfo::getNonSoftWaitcntOpcode(II.getOpcode());
    bool TrySimplify = Opcode != II.getOpcode() && !OptNone;

    // Don't crash if the programmer used legacy waitcnt intrinsics, but don't
    // attempt to do more than that either.
    if (Opcode == AMDGPU::S_WAITCNT)
      continue;

    if (Opcode == AMDGPU::S_WAIT_LOADCNT_DSCNT) {
      unsigned OldEnc =
          TII->getNamedOperand(II, AMDGPU::OpName::simm16)->getImm();
      AMDGPU::Waitcnt OldWait = AMDGPU::decodeLoadcntDscnt(IV, OldEnc);
      if (TrySimplify)
        ScoreBrackets.simplifyWaitcnt(OldWait);
      Wait = Wait.combined(OldWait);
      UpdatableInstr = &CombinedLoadDsCntInstr;
    } else if (Opcode == AMDGPU::S_WAIT_STORECNT_DSCNT) {
      unsigned OldEnc =
          TII->getNamedOperand(II, AMDGPU::OpName::simm16)->getImm();
      AMDGPU::Waitcnt OldWait = AMDGPU::decodeStorecntDscnt(IV, OldEnc);
      if (TrySimplify)
        ScoreBrackets.simplifyWaitcnt(OldWait);
      Wait = Wait.combined(OldWait);
      UpdatableInstr = &CombinedStoreDsCntInstr;
    } else {
      std::optional<InstCounterType> CT = counterTypeForInstr(Opcode);
      assert(CT.has_value());
      unsigned OldCnt =
          TII->getNamedOperand(II, AMDGPU::OpName::simm16)->getImm();
      if (TrySimplify)
        ScoreBrackets.simplifyWaitcnt(CT.value(), OldCnt);
      addWait(Wait, CT.value(), OldCnt);
      UpdatableInstr = &WaitInstrs[CT.value()];
    }

    // Merge consecutive waitcnt of the same type by erasing multiples.
    if (!*UpdatableInstr) {
      *UpdatableInstr = &II;
    } else {
      II.eraseFromParent();
      Modified = true;
    }
  }

  if (CombinedLoadDsCntInstr) {
    // Only keep an S_WAIT_LOADCNT_DSCNT if both counters actually need
    // to be waited for. Otherwise, let the instruction be deleted so
    // the appropriate single counter wait instruction can be inserted
    // instead, when new S_WAIT_*CNT instructions are inserted by
    // createNewWaitcnt(). As a side effect, resetting the wait counts will
    // cause any redundant S_WAIT_LOADCNT or S_WAIT_DSCNT to be removed by
    // the loop below that deals with single counter instructions.
    if (Wait.LoadCnt != ~0u && Wait.DsCnt != ~0u) {
      unsigned NewEnc = AMDGPU::encodeLoadcntDscnt(IV, Wait);
      Modified |= updateOperandIfDifferent(*CombinedLoadDsCntInstr,
                                           AMDGPU::OpName::simm16, NewEnc);
      Modified |= promoteSoftWaitCnt(CombinedLoadDsCntInstr);
      ScoreBrackets.applyWaitcnt(LOAD_CNT, Wait.LoadCnt);
      ScoreBrackets.applyWaitcnt(DS_CNT, Wait.DsCnt);
      Wait.LoadCnt = ~0u;
      Wait.DsCnt = ~0u;

      LLVM_DEBUG(It == OldWaitcntInstr.getParent()->end()
                     ? dbgs() << "applyPreexistingWaitcnt\n"
                              << "New Instr at block end: "
                              << *CombinedLoadDsCntInstr << '\n'
                     : dbgs() << "applyPreexistingWaitcnt\n"
                              << "Old Instr: " << *It << "New Instr: "
                              << *CombinedLoadDsCntInstr << '\n');
    } else {
      CombinedLoadDsCntInstr->eraseFromParent();
      Modified = true;
    }
  }

  if (CombinedStoreDsCntInstr) {
    // Similarly for S_WAIT_STORECNT_DSCNT.
    if (Wait.StoreCnt != ~0u && Wait.DsCnt != ~0u) {
      unsigned NewEnc = AMDGPU::encodeStorecntDscnt(IV, Wait);
      Modified |= updateOperandIfDifferent(*CombinedStoreDsCntInstr,
                                           AMDGPU::OpName::simm16, NewEnc);
      Modified |= promoteSoftWaitCnt(CombinedStoreDsCntInstr);
      ScoreBrackets.applyWaitcnt(STORE_CNT, Wait.StoreCnt);
      ScoreBrackets.applyWaitcnt(DS_CNT, Wait.DsCnt);
      Wait.StoreCnt = ~0u;
      Wait.DsCnt = ~0u;

      LLVM_DEBUG(It == OldWaitcntInstr.getParent()->end()
                     ? dbgs() << "applyPreexistingWaitcnt\n"
                              << "New Instr at block end: "
                              << *CombinedStoreDsCntInstr << '\n'
                     : dbgs() << "applyPreexistingWaitcnt\n"
                              << "Old Instr: " << *It << "New Instr: "
                              << *CombinedStoreDsCntInstr << '\n');
    } else {
      CombinedStoreDsCntInstr->eraseFromParent();
      Modified = true;
    }
  }

  // Look for an opportunity to convert existing S_WAIT_LOADCNT,
  // S_WAIT_STORECNT and S_WAIT_DSCNT into new S_WAIT_LOADCNT_DSCNT
  // or S_WAIT_STORECNT_DSCNT. This is achieved by selectively removing
  // instructions so that createNewWaitcnt() will create new combined
  // instructions to replace them.

  if (Wait.DsCnt != ~0u) {
    // This is a vector of addresses in WaitInstrs pointing to instructions
    // that should be removed if they are present.
    SmallVector<MachineInstr **, 2> WaitsToErase;

    // If it's known that both DScnt and either LOADcnt or STOREcnt (but not
    // both) need to be waited for, ensure that there are no existing
    // individual wait count instructions for these.

    if (Wait.LoadCnt != ~0u) {
      WaitsToErase.push_back(&WaitInstrs[LOAD_CNT]);
      WaitsToErase.push_back(&WaitInstrs[DS_CNT]);
    } else if (Wait.StoreCnt != ~0u) {
      WaitsToErase.push_back(&WaitInstrs[STORE_CNT]);
      WaitsToErase.push_back(&WaitInstrs[DS_CNT]);
    }

    for (MachineInstr **WI : WaitsToErase) {
      if (!*WI)
        continue;

      (*WI)->eraseFromParent();
      *WI = nullptr;
      Modified = true;
    }
  }

  for (auto CT : inst_counter_types(NUM_EXTENDED_INST_CNTS)) {
    if (!WaitInstrs[CT])
      continue;

    unsigned NewCnt = getWait(Wait, CT);
    if (NewCnt != ~0u) {
      Modified |= updateOperandIfDifferent(*WaitInstrs[CT],
                                           AMDGPU::OpName::simm16, NewCnt);
      Modified |= promoteSoftWaitCnt(WaitInstrs[CT]);

      ScoreBrackets.applyWaitcnt(CT, NewCnt);
      setNoWait(Wait, CT);

      LLVM_DEBUG(It == OldWaitcntInstr.getParent()->end()
                     ? dbgs() << "applyPreexistingWaitcnt\n"
                              << "New Instr at block end: " << *WaitInstrs[CT]
                              << '\n'
                     : dbgs() << "applyPreexistingWaitcnt\n"
                              << "Old Instr: " << *It
                              << "New Instr: " << *WaitInstrs[CT] << '\n');
    } else {
      WaitInstrs[CT]->eraseFromParent();
      Modified = true;
    }
  }

  return Modified;
}

/// Generate S_WAIT_*CNT instructions for any required counters in \p Wait
bool WaitcntGeneratorGFX12Plus::createNewWaitcnt(
    MachineBasicBlock &Block, MachineBasicBlock::instr_iterator It,
    AMDGPU::Waitcnt Wait) {
  assert(ST);
  assert(!isNormalMode(MaxCounter));

  bool Modified = false;
  const DebugLoc &DL = Block.findDebugLoc(It);

  // Check for opportunities to use combined wait instructions.
  if (Wait.DsCnt != ~0u) {
    MachineInstr *SWaitInst = nullptr;

    if (Wait.LoadCnt != ~0u) {
      unsigned Enc = AMDGPU::encodeLoadcntDscnt(IV, Wait);

      SWaitInst = BuildMI(Block, It, DL, TII->get(AMDGPU::S_WAIT_LOADCNT_DSCNT))
                      .addImm(Enc);

      Wait.LoadCnt = ~0u;
      Wait.DsCnt = ~0u;
    } else if (Wait.StoreCnt != ~0u) {
      unsigned Enc = AMDGPU::encodeStorecntDscnt(IV, Wait);

      SWaitInst =
          BuildMI(Block, It, DL, TII->get(AMDGPU::S_WAIT_STORECNT_DSCNT))
              .addImm(Enc);

      Wait.StoreCnt = ~0u;
      Wait.DsCnt = ~0u;
    }

    if (SWaitInst) {
      Modified = true;

      LLVM_DEBUG(dbgs() << "generateWaitcnt\n";
                 if (It != Block.instr_end()) dbgs() << "Old Instr: " << *It;
                 dbgs() << "New Instr: " << *SWaitInst << '\n');
    }
  }

  // Generate an instruction for any remaining counter that needs
  // waiting for.

  for (auto CT : inst_counter_types(NUM_EXTENDED_INST_CNTS)) {
    unsigned Count = getWait(Wait, CT);
    if (Count == ~0u)
      continue;

    [[maybe_unused]] auto SWaitInst =
        BuildMI(Block, It, DL, TII->get(instrsForExtendedCounterTypes[CT]))
            .addImm(Count);

    Modified = true;

    LLVM_DEBUG(dbgs() << "generateWaitcnt\n";
               if (It != Block.instr_end()) dbgs() << "Old Instr: " << *It;
               dbgs() << "New Instr: " << *SWaitInst << '\n');
  }

  return Modified;
}

static bool readsVCCZ(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  return (Opc == AMDGPU::S_CBRANCH_VCCNZ || Opc == AMDGPU::S_CBRANCH_VCCZ) &&
         !MI.getOperand(1).isUndef();
}

/// \returns true if the callee inserts an s_waitcnt 0 on function entry.
static bool callWaitsOnFunctionEntry(const MachineInstr &MI) {
  // Currently all conventions wait, but this may not always be the case.
  //
  // TODO: If IPRA is enabled, and the callee is isSafeForNoCSROpt, it may make
  // senses to omit the wait and do it in the caller.
  return true;
}

/// \returns true if the callee is expected to wait for any outstanding waits
/// before returning.
static bool callWaitsOnFunctionReturn(const MachineInstr &MI) {
  return true;
}

///  Generate s_waitcnt instruction to be placed before cur_Inst.
///  Instructions of a given type are returned in order,
///  but instructions of different types can complete out of order.
///  We rely on this in-order completion
///  and simply assign a score to the memory access instructions.
///  We keep track of the active "score bracket" to determine
///  if an access of a memory read requires an s_waitcnt
///  and if so what the value of each counter is.
///  The "score bracket" is bound by the lower bound and upper bound
///  scores (*_score_LB and *_score_ub respectively).
///  If FlushVmCnt is true, that means that we want to generate a s_waitcnt to
///  flush the vmcnt counter here.
bool SIInsertWaitcnts::generateWaitcntInstBefore(MachineInstr &MI,
                                                 WaitcntBrackets &ScoreBrackets,
                                                 MachineInstr *OldWaitcntInstr,
                                                 bool FlushVmCnt) {
  setForceEmitWaitcnt();

  if (MI.isMetaInstruction())
    return false;

  AMDGPU::Waitcnt Wait;

  // FIXME: This should have already been handled by the memory legalizer.
  // Removing this currently doesn't affect any lit tests, but we need to
  // verify that nothing was relying on this. The number of buffer invalidates
  // being handled here should not be expanded.
  if (MI.getOpcode() == AMDGPU::BUFFER_WBINVL1 ||
      MI.getOpcode() == AMDGPU::BUFFER_WBINVL1_SC ||
      MI.getOpcode() == AMDGPU::BUFFER_WBINVL1_VOL ||
      MI.getOpcode() == AMDGPU::BUFFER_GL0_INV ||
      MI.getOpcode() == AMDGPU::BUFFER_GL1_INV) {
    Wait.LoadCnt = 0;
  }

  // All waits must be resolved at call return.
  // NOTE: this could be improved with knowledge of all call sites or
  //   with knowledge of the called routines.
  if (MI.getOpcode() == AMDGPU::SI_RETURN_TO_EPILOG ||
      MI.getOpcode() == AMDGPU::SI_RETURN ||
      MI.getOpcode() == AMDGPU::S_SETPC_B64_return ||
      (MI.isReturn() && MI.isCall() && !callWaitsOnFunctionEntry(MI))) {
    Wait = Wait.combined(WCG->getAllZeroWaitcnt(/*IncludeVSCnt=*/false));
  }
  // Identify S_ENDPGM instructions which may have to wait for outstanding VMEM
  // stores. In this case it can be useful to send a message to explicitly
  // release all VGPRs before the stores have completed, but it is only safe to
  // do this if:
  // * there are no outstanding scratch stores
  // * we are not in Dynamic VGPR mode
  else if (MI.getOpcode() == AMDGPU::S_ENDPGM ||
           MI.getOpcode() == AMDGPU::S_ENDPGM_SAVED) {
    if (ST->getGeneration() >= AMDGPUSubtarget::GFX11 && !WCG->isOptNone() &&
        ScoreBrackets.getScoreRange(STORE_CNT) != 0 &&
        !ScoreBrackets.hasPendingEvent(SCRATCH_WRITE_ACCESS))
      ReleaseVGPRInsts.insert(&MI);
  }
  // Resolve vm waits before gs-done.
  else if ((MI.getOpcode() == AMDGPU::S_SENDMSG ||
            MI.getOpcode() == AMDGPU::S_SENDMSGHALT) &&
           ST->hasLegacyGeometry() &&
           ((MI.getOperand(0).getImm() & AMDGPU::SendMsg::ID_MASK_PreGFX11_) ==
            AMDGPU::SendMsg::ID_GS_DONE_PreGFX11)) {
    Wait.LoadCnt = 0;
  }

  // Export & GDS instructions do not read the EXEC mask until after the export
  // is granted (which can occur well after the instruction is issued).
  // The shader program must flush all EXP operations on the export-count
  // before overwriting the EXEC mask.
  else {
    if (MI.modifiesRegister(AMDGPU::EXEC, TRI)) {
      // Export and GDS are tracked individually, either may trigger a waitcnt
      // for EXEC.
      if (ScoreBrackets.hasPendingEvent(EXP_GPR_LOCK) ||
          ScoreBrackets.hasPendingEvent(EXP_PARAM_ACCESS) ||
          ScoreBrackets.hasPendingEvent(EXP_POS_ACCESS) ||
          ScoreBrackets.hasPendingEvent(GDS_GPR_LOCK)) {
        Wait.ExpCnt = 0;
      }
    }

    if (MI.isCall() && callWaitsOnFunctionEntry(MI)) {
      // The function is going to insert a wait on everything in its prolog.
      // This still needs to be careful if the call target is a load (e.g. a GOT
      // load). We also need to check WAW dependency with saved PC.
      Wait = AMDGPU::Waitcnt();

      int CallAddrOpIdx =
          AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::src0);

      if (MI.getOperand(CallAddrOpIdx).isReg()) {
        RegInterval CallAddrOpInterval =
            ScoreBrackets.getRegInterval(&MI, MRI, TRI, CallAddrOpIdx);

        for (int RegNo = CallAddrOpInterval.first;
             RegNo < CallAddrOpInterval.second; ++RegNo)
          ScoreBrackets.determineWait(SmemAccessCounter, RegNo, Wait);

        int RtnAddrOpIdx =
          AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::dst);
        if (RtnAddrOpIdx != -1) {
          RegInterval RtnAddrOpInterval =
              ScoreBrackets.getRegInterval(&MI, MRI, TRI, RtnAddrOpIdx);

          for (int RegNo = RtnAddrOpInterval.first;
               RegNo < RtnAddrOpInterval.second; ++RegNo)
            ScoreBrackets.determineWait(SmemAccessCounter, RegNo, Wait);
        }
      }
    } else {
      // FIXME: Should not be relying on memoperands.
      // Look at the source operands of every instruction to see if
      // any of them results from a previous memory operation that affects
      // its current usage. If so, an s_waitcnt instruction needs to be
      // emitted.
      // If the source operand was defined by a load, add the s_waitcnt
      // instruction.
      //
      // Two cases are handled for destination operands:
      // 1) If the destination operand was defined by a load, add the s_waitcnt
      // instruction to guarantee the right WAW order.
      // 2) If a destination operand that was used by a recent export/store ins,
      // add s_waitcnt on exp_cnt to guarantee the WAR order.

      for (const MachineMemOperand *Memop : MI.memoperands()) {
        const Value *Ptr = Memop->getValue();
        if (Memop->isStore() && SLoadAddresses.count(Ptr)) {
          addWait(Wait, SmemAccessCounter, 0);
          if (PDT->dominates(MI.getParent(), SLoadAddresses.find(Ptr)->second))
            SLoadAddresses.erase(Ptr);
        }
        unsigned AS = Memop->getAddrSpace();
        if (AS != AMDGPUAS::LOCAL_ADDRESS && AS != AMDGPUAS::FLAT_ADDRESS)
          continue;
        // No need to wait before load from VMEM to LDS.
        if (TII->mayWriteLDSThroughDMA(MI))
          continue;

        // LOAD_CNT is only relevant to vgpr or LDS.
        unsigned RegNo = SQ_MAX_PGM_VGPRS + EXTRA_VGPR_LDS;
        bool FoundAliasingStore = false;
        // Only objects with alias scope info were added to LDSDMAScopes array.
        // In the absense of the scope info we will not be able to disambiguate
        // aliasing here. There is no need to try searching for a corresponding
        // store slot. This is conservatively correct because in that case we
        // will produce a wait using the first (general) LDS DMA wait slot which
        // will wait on all of them anyway.
        if (Ptr && Memop->getAAInfo() && Memop->getAAInfo().Scope) {
          const auto &LDSDMAStores = ScoreBrackets.getLDSDMAStores();
          for (unsigned I = 0, E = LDSDMAStores.size(); I != E; ++I) {
            if (MI.mayAlias(AA, *LDSDMAStores[I], true)) {
              FoundAliasingStore = true;
              ScoreBrackets.determineWait(LOAD_CNT, RegNo + I + 1, Wait);
            }
          }
        }
        if (!FoundAliasingStore)
          ScoreBrackets.determineWait(LOAD_CNT, RegNo, Wait);
        if (Memop->isStore()) {
          ScoreBrackets.determineWait(EXP_CNT, RegNo, Wait);
        }
      }

      // Loop over use and def operands.
      for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
        MachineOperand &Op = MI.getOperand(I);
        if (!Op.isReg())
          continue;

        // If the instruction does not read tied source, skip the operand.
        if (Op.isTied() && Op.isUse() && TII->doesNotReadTiedSource(MI))
          continue;

        RegInterval Interval = ScoreBrackets.getRegInterval(&MI, MRI, TRI, I);

        const bool IsVGPR = TRI->isVectorRegister(*MRI, Op.getReg());
        for (int RegNo = Interval.first; RegNo < Interval.second; ++RegNo) {
          if (IsVGPR) {
            // RAW always needs an s_waitcnt. WAW needs an s_waitcnt unless the
            // previous write and this write are the same type of VMEM
            // instruction, in which case they are (in some architectures)
            // guaranteed to write their results in order anyway.
            if (Op.isUse() || !updateVMCntOnly(MI) ||
                ScoreBrackets.hasOtherPendingVmemTypes(RegNo,
                                                       getVmemType(MI)) ||
                !ST->hasVmemWriteVgprInOrder()) {
              ScoreBrackets.determineWait(LOAD_CNT, RegNo, Wait);
              ScoreBrackets.determineWait(SAMPLE_CNT, RegNo, Wait);
              ScoreBrackets.determineWait(BVH_CNT, RegNo, Wait);
              ScoreBrackets.clearVgprVmemTypes(RegNo);
            }
            if (Op.isDef() || ScoreBrackets.hasPendingEvent(EXP_LDS_ACCESS)) {
              ScoreBrackets.determineWait(EXP_CNT, RegNo, Wait);
            }
            ScoreBrackets.determineWait(DS_CNT, RegNo, Wait);
          } else {
            ScoreBrackets.determineWait(SmemAccessCounter, RegNo, Wait);
          }
        }
      }
    }
  }

  // The subtarget may have an implicit S_WAITCNT 0 before barriers. If it does
  // not, we need to ensure the subtarget is capable of backing off barrier
  // instructions in case there are any outstanding memory operations that may
  // cause an exception. Otherwise, insert an explicit S_WAITCNT 0 here.
  if (TII->isBarrierStart(MI.getOpcode()) &&
      !ST->hasAutoWaitcntBeforeBarrier() && !ST->supportsBackOffBarrier()) {
    Wait = Wait.combined(WCG->getAllZeroWaitcnt(/*IncludeVSCnt=*/true));
  }

  // TODO: Remove this work-around, enable the assert for Bug 457939
  //       after fixing the scheduler. Also, the Shader Compiler code is
  //       independent of target.
  if (readsVCCZ(MI) && ST->hasReadVCCZBug()) {
    if (ScoreBrackets.hasPendingEvent(SMEM_ACCESS)) {
      Wait.DsCnt = 0;
    }
  }

  // Verify that the wait is actually needed.
  ScoreBrackets.simplifyWaitcnt(Wait);

  if (ForceEmitZeroWaitcnts)
    Wait = WCG->getAllZeroWaitcnt(/*IncludeVSCnt=*/false);

  if (ForceEmitWaitcnt[LOAD_CNT])
    Wait.LoadCnt = 0;
  if (ForceEmitWaitcnt[EXP_CNT])
    Wait.ExpCnt = 0;
  if (ForceEmitWaitcnt[DS_CNT])
    Wait.DsCnt = 0;
  if (ForceEmitWaitcnt[SAMPLE_CNT])
    Wait.SampleCnt = 0;
  if (ForceEmitWaitcnt[BVH_CNT])
    Wait.BvhCnt = 0;
  if (ForceEmitWaitcnt[KM_CNT])
    Wait.KmCnt = 0;

  if (FlushVmCnt) {
    if (ScoreBrackets.hasPendingEvent(LOAD_CNT))
      Wait.LoadCnt = 0;
    if (ScoreBrackets.hasPendingEvent(SAMPLE_CNT))
      Wait.SampleCnt = 0;
    if (ScoreBrackets.hasPendingEvent(BVH_CNT))
      Wait.BvhCnt = 0;
  }

  return generateWaitcnt(Wait, MI.getIterator(), *MI.getParent(), ScoreBrackets,
                         OldWaitcntInstr);
}

bool SIInsertWaitcnts::generateWaitcnt(AMDGPU::Waitcnt Wait,
                                       MachineBasicBlock::instr_iterator It,
                                       MachineBasicBlock &Block,
                                       WaitcntBrackets &ScoreBrackets,
                                       MachineInstr *OldWaitcntInstr) {
  bool Modified = false;

  if (OldWaitcntInstr)
    // Try to merge the required wait with preexisting waitcnt instructions.
    // Also erase redundant waitcnt.
    Modified =
        WCG->applyPreexistingWaitcnt(ScoreBrackets, *OldWaitcntInstr, Wait, It);

  // Any counts that could have been applied to any existing waitcnt
  // instructions will have been done so, now deal with any remaining.
  ScoreBrackets.applyWaitcnt(Wait);

  // ExpCnt can be merged into VINTERP.
  if (Wait.ExpCnt != ~0u && It != Block.instr_end() &&
      SIInstrInfo::isVINTERP(*It)) {
    MachineOperand *WaitExp =
        TII->getNamedOperand(*It, AMDGPU::OpName::waitexp);
    if (Wait.ExpCnt < WaitExp->getImm()) {
      WaitExp->setImm(Wait.ExpCnt);
      Modified = true;
    }
    Wait.ExpCnt = ~0u;

    LLVM_DEBUG(dbgs() << "generateWaitcnt\n"
                      << "Update Instr: " << *It);
  }

  if (WCG->createNewWaitcnt(Block, It, Wait))
    Modified = true;

  return Modified;
}

// This is a flat memory operation. Check to see if it has memory tokens other
// than LDS. Other address spaces supported by flat memory operations involve
// global memory.
bool SIInsertWaitcnts::mayAccessVMEMThroughFlat(const MachineInstr &MI) const {
  assert(TII->isFLAT(MI));

  // All flat instructions use the VMEM counter.
  assert(TII->usesVM_CNT(MI));

  // If there are no memory operands then conservatively assume the flat
  // operation may access VMEM.
  if (MI.memoperands_empty())
    return true;

  // See if any memory operand specifies an address space that involves VMEM.
  // Flat operations only supported FLAT, LOCAL (LDS), or address spaces
  // involving VMEM such as GLOBAL, CONSTANT, PRIVATE (SCRATCH), etc. The REGION
  // (GDS) address space is not supported by flat operations. Therefore, simply
  // return true unless only the LDS address space is found.
  for (const MachineMemOperand *Memop : MI.memoperands()) {
    unsigned AS = Memop->getAddrSpace();
    assert(AS != AMDGPUAS::REGION_ADDRESS);
    if (AS != AMDGPUAS::LOCAL_ADDRESS)
      return true;
  }

  return false;
}

// This is a flat memory operation. Check to see if it has memory tokens for
// either LDS or FLAT.
bool SIInsertWaitcnts::mayAccessLDSThroughFlat(const MachineInstr &MI) const {
  assert(TII->isFLAT(MI));

  // Flat instruction such as SCRATCH and GLOBAL do not use the lgkm counter.
  if (!TII->usesLGKM_CNT(MI))
    return false;

  // If in tgsplit mode then there can be no use of LDS.
  if (ST->isTgSplitEnabled())
    return false;

  // If there are no memory operands then conservatively assume the flat
  // operation may access LDS.
  if (MI.memoperands_empty())
    return true;

  // See if any memory operand specifies an address space that involves LDS.
  for (const MachineMemOperand *Memop : MI.memoperands()) {
    unsigned AS = Memop->getAddrSpace();
    if (AS == AMDGPUAS::LOCAL_ADDRESS || AS == AMDGPUAS::FLAT_ADDRESS)
      return true;
  }

  return false;
}

// This is a flat memory operation. Check to see if it has memory tokens for
// either scratch or FLAT.
bool SIInsertWaitcnts::mayAccessScratchThroughFlat(
    const MachineInstr &MI) const {
  assert(TII->isFLAT(MI));

  // SCRATCH instructions always access scratch.
  if (TII->isFLATScratch(MI))
    return true;

  // GLOBAL instructions never access scratch.
  if (TII->isFLATGlobal(MI))
    return false;

  // If there are no memory operands then conservatively assume the flat
  // operation may access scratch.
  if (MI.memoperands_empty())
    return true;

  // See if any memory operand specifies an address space that involves scratch.
  return any_of(MI.memoperands(), [](const MachineMemOperand *Memop) {
    unsigned AS = Memop->getAddrSpace();
    return AS == AMDGPUAS::PRIVATE_ADDRESS || AS == AMDGPUAS::FLAT_ADDRESS;
  });
}

static bool isCacheInvOrWBInst(MachineInstr &Inst) {
  auto Opc = Inst.getOpcode();
  return Opc == AMDGPU::GLOBAL_INV || Opc == AMDGPU::GLOBAL_WB ||
         Opc == AMDGPU::GLOBAL_WBINV;
}

void SIInsertWaitcnts::updateEventWaitcntAfter(MachineInstr &Inst,
                                               WaitcntBrackets *ScoreBrackets) {
  // Now look at the instruction opcode. If it is a memory access
  // instruction, update the upper-bound of the appropriate counter's
  // bracket and the destination operand scores.
  // TODO: Use the (TSFlags & SIInstrFlags::DS_CNT) property everywhere.

  if (TII->isDS(Inst) && TII->usesLGKM_CNT(Inst)) {
    if (TII->isAlwaysGDS(Inst.getOpcode()) ||
        TII->hasModifiersSet(Inst, AMDGPU::OpName::gds)) {
      ScoreBrackets->updateByEvent(TII, TRI, MRI, GDS_ACCESS, Inst);
      ScoreBrackets->updateByEvent(TII, TRI, MRI, GDS_GPR_LOCK, Inst);
    } else {
      ScoreBrackets->updateByEvent(TII, TRI, MRI, LDS_ACCESS, Inst);
    }
  } else if (TII->isFLAT(Inst)) {
    // TODO: Track this properly.
    if (isCacheInvOrWBInst(Inst))
      return;

    assert(Inst.mayLoadOrStore());

    int FlatASCount = 0;

    if (mayAccessVMEMThroughFlat(Inst)) {
      ++FlatASCount;
      ScoreBrackets->updateByEvent(TII, TRI, MRI, getVmemWaitEventType(Inst),
                                   Inst);
    }

    if (mayAccessLDSThroughFlat(Inst)) {
      ++FlatASCount;
      ScoreBrackets->updateByEvent(TII, TRI, MRI, LDS_ACCESS, Inst);
    }

    // A Flat memory operation must access at least one address space.
    assert(FlatASCount);

    // This is a flat memory operation that access both VMEM and LDS, so note it
    // - it will require that both the VM and LGKM be flushed to zero if it is
    // pending when a VM or LGKM dependency occurs.
    if (FlatASCount > 1)
      ScoreBrackets->setPendingFlat();
  } else if (SIInstrInfo::isVMEM(Inst) &&
             !llvm::AMDGPU::getMUBUFIsBufferInv(Inst.getOpcode())) {
    ScoreBrackets->updateByEvent(TII, TRI, MRI, getVmemWaitEventType(Inst),
                                 Inst);

    if (ST->vmemWriteNeedsExpWaitcnt() &&
        (Inst.mayStore() || SIInstrInfo::isAtomicRet(Inst))) {
      ScoreBrackets->updateByEvent(TII, TRI, MRI, VMW_GPR_LOCK, Inst);
    }
  } else if (TII->isSMRD(Inst)) {
    ScoreBrackets->updateByEvent(TII, TRI, MRI, SMEM_ACCESS, Inst);
  } else if (Inst.isCall()) {
    if (callWaitsOnFunctionReturn(Inst)) {
      // Act as a wait on everything
      ScoreBrackets->applyWaitcnt(
          WCG->getAllZeroWaitcnt(/*IncludeVSCnt=*/false));
      ScoreBrackets->setStateOnFunctionEntryOrReturn();
    } else {
      // May need to way wait for anything.
      ScoreBrackets->applyWaitcnt(AMDGPU::Waitcnt());
    }
  } else if (SIInstrInfo::isLDSDIR(Inst)) {
    ScoreBrackets->updateByEvent(TII, TRI, MRI, EXP_LDS_ACCESS, Inst);
  } else if (TII->isVINTERP(Inst)) {
    int64_t Imm = TII->getNamedOperand(Inst, AMDGPU::OpName::waitexp)->getImm();
    ScoreBrackets->applyWaitcnt(EXP_CNT, Imm);
  } else if (SIInstrInfo::isEXP(Inst)) {
    unsigned Imm = TII->getNamedOperand(Inst, AMDGPU::OpName::tgt)->getImm();
    if (Imm >= AMDGPU::Exp::ET_PARAM0 && Imm <= AMDGPU::Exp::ET_PARAM31)
      ScoreBrackets->updateByEvent(TII, TRI, MRI, EXP_PARAM_ACCESS, Inst);
    else if (Imm >= AMDGPU::Exp::ET_POS0 && Imm <= AMDGPU::Exp::ET_POS_LAST)
      ScoreBrackets->updateByEvent(TII, TRI, MRI, EXP_POS_ACCESS, Inst);
    else
      ScoreBrackets->updateByEvent(TII, TRI, MRI, EXP_GPR_LOCK, Inst);
  } else {
    switch (Inst.getOpcode()) {
    case AMDGPU::S_SENDMSG:
    case AMDGPU::S_SENDMSG_RTN_B32:
    case AMDGPU::S_SENDMSG_RTN_B64:
    case AMDGPU::S_SENDMSGHALT:
      ScoreBrackets->updateByEvent(TII, TRI, MRI, SQ_MESSAGE, Inst);
      break;
    case AMDGPU::S_MEMTIME:
    case AMDGPU::S_MEMREALTIME:
    case AMDGPU::S_BARRIER_SIGNAL_ISFIRST_M0:
    case AMDGPU::S_BARRIER_SIGNAL_ISFIRST_IMM:
    case AMDGPU::S_BARRIER_LEAVE:
    case AMDGPU::S_GET_BARRIER_STATE_M0:
    case AMDGPU::S_GET_BARRIER_STATE_IMM:
      ScoreBrackets->updateByEvent(TII, TRI, MRI, SMEM_ACCESS, Inst);
      break;
    }
  }
}

bool WaitcntBrackets::mergeScore(const MergeInfo &M, unsigned &Score,
                                 unsigned OtherScore) {
  unsigned MyShifted = Score <= M.OldLB ? 0 : Score + M.MyShift;
  unsigned OtherShifted =
      OtherScore <= M.OtherLB ? 0 : OtherScore + M.OtherShift;
  Score = std::max(MyShifted, OtherShifted);
  return OtherShifted > MyShifted;
}

/// Merge the pending events and associater score brackets of \p Other into
/// this brackets status.
///
/// Returns whether the merge resulted in a change that requires tighter waits
/// (i.e. the merged brackets strictly dominate the original brackets).
bool WaitcntBrackets::merge(const WaitcntBrackets &Other) {
  bool StrictDom = false;

  VgprUB = std::max(VgprUB, Other.VgprUB);
  SgprUB = std::max(SgprUB, Other.SgprUB);

  for (auto T : inst_counter_types(MaxCounter)) {
    // Merge event flags for this counter
    const unsigned OldEvents = PendingEvents & WaitEventMaskForInst[T];
    const unsigned OtherEvents = Other.PendingEvents & WaitEventMaskForInst[T];
    if (OtherEvents & ~OldEvents)
      StrictDom = true;
    PendingEvents |= OtherEvents;

    // Merge scores for this counter
    const unsigned MyPending = ScoreUBs[T] - ScoreLBs[T];
    const unsigned OtherPending = Other.ScoreUBs[T] - Other.ScoreLBs[T];
    const unsigned NewUB = ScoreLBs[T] + std::max(MyPending, OtherPending);
    if (NewUB < ScoreLBs[T])
      report_fatal_error("waitcnt score overflow");

    MergeInfo M;
    M.OldLB = ScoreLBs[T];
    M.OtherLB = Other.ScoreLBs[T];
    M.MyShift = NewUB - ScoreUBs[T];
    M.OtherShift = NewUB - Other.ScoreUBs[T];

    ScoreUBs[T] = NewUB;

    StrictDom |= mergeScore(M, LastFlat[T], Other.LastFlat[T]);

    for (int J = 0; J <= VgprUB; J++)
      StrictDom |= mergeScore(M, VgprScores[T][J], Other.VgprScores[T][J]);

    if (T == SmemAccessCounter) {
      for (int J = 0; J <= SgprUB; J++)
        StrictDom |= mergeScore(M, SgprScores[J], Other.SgprScores[J]);
    }
  }

  for (int J = 0; J <= VgprUB; J++) {
    unsigned char NewVmemTypes = VgprVmemTypes[J] | Other.VgprVmemTypes[J];
    StrictDom |= NewVmemTypes != VgprVmemTypes[J];
    VgprVmemTypes[J] = NewVmemTypes;
  }

  return StrictDom;
}

static bool isWaitInstr(MachineInstr &Inst) {
  unsigned Opcode = SIInstrInfo::getNonSoftWaitcntOpcode(Inst.getOpcode());
  return Opcode == AMDGPU::S_WAITCNT ||
         (Opcode == AMDGPU::S_WAITCNT_VSCNT && Inst.getOperand(0).isReg() &&
          Inst.getOperand(0).getReg() == AMDGPU::SGPR_NULL) ||
         Opcode == AMDGPU::S_WAIT_LOADCNT_DSCNT ||
         Opcode == AMDGPU::S_WAIT_STORECNT_DSCNT ||
         counterTypeForInstr(Opcode).has_value();
}

// Generate s_waitcnt instructions where needed.
bool SIInsertWaitcnts::insertWaitcntInBlock(MachineFunction &MF,
                                            MachineBasicBlock &Block,
                                            WaitcntBrackets &ScoreBrackets) {
  bool Modified = false;

  LLVM_DEBUG({
    dbgs() << "*** Block" << Block.getNumber() << " ***";
    ScoreBrackets.dump();
  });

  // Track the correctness of vccz through this basic block. There are two
  // reasons why it might be incorrect; see ST->hasReadVCCZBug() and
  // ST->partialVCCWritesUpdateVCCZ().
  bool VCCZCorrect = true;
  if (ST->hasReadVCCZBug()) {
    // vccz could be incorrect at a basic block boundary if a predecessor wrote
    // to vcc and then issued an smem load.
    VCCZCorrect = false;
  } else if (!ST->partialVCCWritesUpdateVCCZ()) {
    // vccz could be incorrect at a basic block boundary if a predecessor wrote
    // to vcc_lo or vcc_hi.
    VCCZCorrect = false;
  }

  // Walk over the instructions.
  MachineInstr *OldWaitcntInstr = nullptr;

  for (MachineBasicBlock::instr_iterator Iter = Block.instr_begin(),
                                         E = Block.instr_end();
       Iter != E;) {
    MachineInstr &Inst = *Iter;

    // Track pre-existing waitcnts that were added in earlier iterations or by
    // the memory legalizer.
    if (isWaitInstr(Inst)) {
      if (!OldWaitcntInstr)
        OldWaitcntInstr = &Inst;
      ++Iter;
      continue;
    }

    bool FlushVmCnt = Block.getFirstTerminator() == Inst &&
                      isPreheaderToFlush(Block, ScoreBrackets);

    // Generate an s_waitcnt instruction to be placed before Inst, if needed.
    Modified |= generateWaitcntInstBefore(Inst, ScoreBrackets, OldWaitcntInstr,
                                          FlushVmCnt);
    OldWaitcntInstr = nullptr;

    // Restore vccz if it's not known to be correct already.
    bool RestoreVCCZ = !VCCZCorrect && readsVCCZ(Inst);

    // Don't examine operands unless we need to track vccz correctness.
    if (ST->hasReadVCCZBug() || !ST->partialVCCWritesUpdateVCCZ()) {
      if (Inst.definesRegister(AMDGPU::VCC_LO, /*TRI=*/nullptr) ||
          Inst.definesRegister(AMDGPU::VCC_HI, /*TRI=*/nullptr)) {
        // Up to gfx9, writes to vcc_lo and vcc_hi don't update vccz.
        if (!ST->partialVCCWritesUpdateVCCZ())
          VCCZCorrect = false;
      } else if (Inst.definesRegister(AMDGPU::VCC, /*TRI=*/nullptr)) {
        // There is a hardware bug on CI/SI where SMRD instruction may corrupt
        // vccz bit, so when we detect that an instruction may read from a
        // corrupt vccz bit, we need to:
        // 1. Insert s_waitcnt lgkm(0) to wait for all outstanding SMRD
        //    operations to complete.
        // 2. Restore the correct value of vccz by writing the current value
        //    of vcc back to vcc.
        if (ST->hasReadVCCZBug() &&
            ScoreBrackets.hasPendingEvent(SMEM_ACCESS)) {
          // Writes to vcc while there's an outstanding smem read may get
          // clobbered as soon as any read completes.
          VCCZCorrect = false;
        } else {
          // Writes to vcc will fix any incorrect value in vccz.
          VCCZCorrect = true;
        }
      }
    }

    if (TII->isSMRD(Inst)) {
      for (const MachineMemOperand *Memop : Inst.memoperands()) {
        // No need to handle invariant loads when avoiding WAR conflicts, as
        // there cannot be a vector store to the same memory location.
        if (!Memop->isInvariant()) {
          const Value *Ptr = Memop->getValue();
          SLoadAddresses.insert(std::pair(Ptr, Inst.getParent()));
        }
      }
      if (ST->hasReadVCCZBug()) {
        // This smem read could complete and clobber vccz at any time.
        VCCZCorrect = false;
      }
    }

    updateEventWaitcntAfter(Inst, &ScoreBrackets);

    if (ST->isPreciseMemoryEnabled() && Inst.mayLoadOrStore()) {
      AMDGPU::Waitcnt Wait = WCG->getAllZeroWaitcnt(
          Inst.mayStore() && !SIInstrInfo::isAtomicRet(Inst));
      ScoreBrackets.simplifyWaitcnt(Wait);
      Modified |= generateWaitcnt(Wait, std::next(Inst.getIterator()), Block,
                                  ScoreBrackets, /*OldWaitcntInstr=*/nullptr);
    }

    LLVM_DEBUG({
      Inst.print(dbgs());
      ScoreBrackets.dump();
    });

    // TODO: Remove this work-around after fixing the scheduler and enable the
    // assert above.
    if (RestoreVCCZ) {
      // Restore the vccz bit.  Any time a value is written to vcc, the vcc
      // bit is updated, so we can restore the bit by reading the value of
      // vcc and then writing it back to the register.
      BuildMI(Block, Inst, Inst.getDebugLoc(),
              TII->get(ST->isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64),
              TRI->getVCC())
          .addReg(TRI->getVCC());
      VCCZCorrect = true;
      Modified = true;
    }

    ++Iter;
  }

  // Flush the LOADcnt, SAMPLEcnt and BVHcnt counters at the end of the block if
  // needed.
  AMDGPU::Waitcnt Wait;
  if (Block.getFirstTerminator() == Block.end() &&
      isPreheaderToFlush(Block, ScoreBrackets)) {
    if (ScoreBrackets.hasPendingEvent(LOAD_CNT))
      Wait.LoadCnt = 0;
    if (ScoreBrackets.hasPendingEvent(SAMPLE_CNT))
      Wait.SampleCnt = 0;
    if (ScoreBrackets.hasPendingEvent(BVH_CNT))
      Wait.BvhCnt = 0;
  }

  // Combine or remove any redundant waitcnts at the end of the block.
  Modified |= generateWaitcnt(Wait, Block.instr_end(), Block, ScoreBrackets,
                              OldWaitcntInstr);

  return Modified;
}

// Return true if the given machine basic block is a preheader of a loop in
// which we want to flush the vmcnt counter, and false otherwise.
bool SIInsertWaitcnts::isPreheaderToFlush(MachineBasicBlock &MBB,
                                          WaitcntBrackets &ScoreBrackets) {
  auto [Iterator, IsInserted] = PreheadersToFlush.try_emplace(&MBB, false);
  if (!IsInserted)
    return Iterator->second;

  MachineBasicBlock *Succ = MBB.getSingleSuccessor();
  if (!Succ)
    return false;

  MachineLoop *Loop = MLI->getLoopFor(Succ);
  if (!Loop)
    return false;

  if (Loop->getLoopPreheader() == &MBB &&
      shouldFlushVmCnt(Loop, ScoreBrackets)) {
    Iterator->second = true;
    return true;
  }

  return false;
}

bool SIInsertWaitcnts::isVMEMOrFlatVMEM(const MachineInstr &MI) const {
  return SIInstrInfo::isVMEM(MI) ||
         (SIInstrInfo::isFLAT(MI) && mayAccessVMEMThroughFlat(MI));
}

// Return true if it is better to flush the vmcnt counter in the preheader of
// the given loop. We currently decide to flush in two situations:
// 1. The loop contains vmem store(s), no vmem load and at least one use of a
//    vgpr containing a value that is loaded outside of the loop. (Only on
//    targets with no vscnt counter).
// 2. The loop contains vmem load(s), but the loaded values are not used in the
//    loop, and at least one use of a vgpr containing a value that is loaded
//    outside of the loop.
bool SIInsertWaitcnts::shouldFlushVmCnt(MachineLoop *ML,
                                        WaitcntBrackets &Brackets) {
  bool HasVMemLoad = false;
  bool HasVMemStore = false;
  bool UsesVgprLoadedOutside = false;
  DenseSet<Register> VgprUse;
  DenseSet<Register> VgprDef;

  for (MachineBasicBlock *MBB : ML->blocks()) {
    for (MachineInstr &MI : *MBB) {
      if (isVMEMOrFlatVMEM(MI)) {
        if (MI.mayLoad())
          HasVMemLoad = true;
        if (MI.mayStore())
          HasVMemStore = true;
      }
      for (unsigned I = 0; I < MI.getNumOperands(); I++) {
        MachineOperand &Op = MI.getOperand(I);
        if (!Op.isReg() || !TRI->isVectorRegister(*MRI, Op.getReg()))
          continue;
        RegInterval Interval = Brackets.getRegInterval(&MI, MRI, TRI, I);
        // Vgpr use
        if (Op.isUse()) {
          for (int RegNo = Interval.first; RegNo < Interval.second; ++RegNo) {
            // If we find a register that is loaded inside the loop, 1. and 2.
            // are invalidated and we can exit.
            if (VgprDef.contains(RegNo))
              return false;
            VgprUse.insert(RegNo);
            // If at least one of Op's registers is in the score brackets, the
            // value is likely loaded outside of the loop.
            if (Brackets.getRegScore(RegNo, LOAD_CNT) >
                    Brackets.getScoreLB(LOAD_CNT) ||
                Brackets.getRegScore(RegNo, SAMPLE_CNT) >
                    Brackets.getScoreLB(SAMPLE_CNT) ||
                Brackets.getRegScore(RegNo, BVH_CNT) >
                    Brackets.getScoreLB(BVH_CNT)) {
              UsesVgprLoadedOutside = true;
              break;
            }
          }
        }
        // VMem load vgpr def
        else if (isVMEMOrFlatVMEM(MI) && MI.mayLoad() && Op.isDef())
          for (int RegNo = Interval.first; RegNo < Interval.second; ++RegNo) {
            // If we find a register that is loaded inside the loop, 1. and 2.
            // are invalidated and we can exit.
            if (VgprUse.contains(RegNo))
              return false;
            VgprDef.insert(RegNo);
          }
      }
    }
  }
  if (!ST->hasVscnt() && HasVMemStore && !HasVMemLoad && UsesVgprLoadedOutside)
    return true;
  return HasVMemLoad && UsesVgprLoadedOutside && ST->hasVmemWriteVgprInOrder();
}

bool SIInsertWaitcnts::runOnMachineFunction(MachineFunction &MF) {
  ST = &MF.getSubtarget<GCNSubtarget>();
  TII = ST->getInstrInfo();
  TRI = &TII->getRegisterInfo();
  MRI = &MF.getRegInfo();
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  PDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
  if (auto AAR = getAnalysisIfAvailable<AAResultsWrapperPass>())
    AA = &AAR->getAAResults();

  AMDGPU::IsaVersion IV = AMDGPU::getIsaVersion(ST->getCPU());

  if (ST->hasExtendedWaitCounts()) {
    MaxCounter = NUM_EXTENDED_INST_CNTS;
    WCGGFX12Plus = WaitcntGeneratorGFX12Plus(MF, MaxCounter);
    WCG = &WCGGFX12Plus;
  } else {
    MaxCounter = NUM_NORMAL_INST_CNTS;
    WCGPreGFX12 = WaitcntGeneratorPreGFX12(MF);
    WCG = &WCGPreGFX12;
  }

  ForceEmitZeroWaitcnts = ForceEmitZeroFlag;
  for (auto T : inst_counter_types())
    ForceEmitWaitcnt[T] = false;

  const unsigned *WaitEventMaskForInst = WCG->getWaitEventMask();

  SmemAccessCounter = eventCounter(WaitEventMaskForInst, SMEM_ACCESS);

  HardwareLimits Limits = {};
  if (ST->hasExtendedWaitCounts()) {
    Limits.LoadcntMax = AMDGPU::getLoadcntBitMask(IV);
    Limits.DscntMax = AMDGPU::getDscntBitMask(IV);
  } else {
    Limits.LoadcntMax = AMDGPU::getVmcntBitMask(IV);
    Limits.DscntMax = AMDGPU::getLgkmcntBitMask(IV);
  }
  Limits.ExpcntMax = AMDGPU::getExpcntBitMask(IV);
  Limits.StorecntMax = AMDGPU::getStorecntBitMask(IV);
  Limits.SamplecntMax = AMDGPU::getSamplecntBitMask(IV);
  Limits.BvhcntMax = AMDGPU::getBvhcntBitMask(IV);
  Limits.KmcntMax = AMDGPU::getKmcntBitMask(IV);

  unsigned NumVGPRsMax = ST->getAddressableNumVGPRs();
  unsigned NumSGPRsMax = ST->getAddressableNumSGPRs();
  assert(NumVGPRsMax <= SQ_MAX_PGM_VGPRS);
  assert(NumSGPRsMax <= SQ_MAX_PGM_SGPRS);

  RegisterEncoding Encoding = {};
  Encoding.VGPR0 =
      TRI->getEncodingValue(AMDGPU::VGPR0) & AMDGPU::HWEncoding::REG_IDX_MASK;
  Encoding.VGPRL = Encoding.VGPR0 + NumVGPRsMax - 1;
  Encoding.SGPR0 =
      TRI->getEncodingValue(AMDGPU::SGPR0) & AMDGPU::HWEncoding::REG_IDX_MASK;
  Encoding.SGPRL = Encoding.SGPR0 + NumSGPRsMax - 1;

  BlockInfos.clear();
  bool Modified = false;

  MachineBasicBlock &EntryBB = MF.front();
  MachineBasicBlock::iterator I = EntryBB.begin();

  if (!MFI->isEntryFunction()) {
    // Wait for any outstanding memory operations that the input registers may
    // depend on. We can't track them and it's better to do the wait after the
    // costly call sequence.

    // TODO: Could insert earlier and schedule more liberally with operations
    // that only use caller preserved registers.
    for (MachineBasicBlock::iterator E = EntryBB.end();
         I != E && (I->isPHI() || I->isMetaInstruction()); ++I)
      ;

    if (ST->hasExtendedWaitCounts()) {
      BuildMI(EntryBB, I, DebugLoc(), TII->get(AMDGPU::S_WAIT_LOADCNT_DSCNT))
          .addImm(0);
      for (auto CT : inst_counter_types(NUM_EXTENDED_INST_CNTS)) {
        if (CT == LOAD_CNT || CT == DS_CNT || CT == STORE_CNT)
          continue;

        BuildMI(EntryBB, I, DebugLoc(),
                TII->get(instrsForExtendedCounterTypes[CT]))
            .addImm(0);
      }
    } else {
      BuildMI(EntryBB, I, DebugLoc(), TII->get(AMDGPU::S_WAITCNT)).addImm(0);
    }

    auto NonKernelInitialState = std::make_unique<WaitcntBrackets>(
        ST, MaxCounter, Limits, Encoding, WaitEventMaskForInst,
        SmemAccessCounter);
    NonKernelInitialState->setStateOnFunctionEntryOrReturn();
    BlockInfos[&EntryBB].Incoming = std::move(NonKernelInitialState);

    Modified = true;
  }

  // Keep iterating over the blocks in reverse post order, inserting and
  // updating s_waitcnt where needed, until a fix point is reached.
  for (auto *MBB : ReversePostOrderTraversal<MachineFunction *>(&MF))
    BlockInfos.insert({MBB, BlockInfo()});

  std::unique_ptr<WaitcntBrackets> Brackets;
  bool Repeat;
  do {
    Repeat = false;

    for (auto BII = BlockInfos.begin(), BIE = BlockInfos.end(); BII != BIE;
         ++BII) {
      MachineBasicBlock *MBB = BII->first;
      BlockInfo &BI = BII->second;
      if (!BI.Dirty)
        continue;

      if (BI.Incoming) {
        if (!Brackets)
          Brackets = std::make_unique<WaitcntBrackets>(*BI.Incoming);
        else
          *Brackets = *BI.Incoming;
      } else {
        if (!Brackets)
          Brackets = std::make_unique<WaitcntBrackets>(
              ST, MaxCounter, Limits, Encoding, WaitEventMaskForInst,
              SmemAccessCounter);
        else
          *Brackets = WaitcntBrackets(ST, MaxCounter, Limits, Encoding,
                                      WaitEventMaskForInst, SmemAccessCounter);
      }

      Modified |= insertWaitcntInBlock(MF, *MBB, *Brackets);
      BI.Dirty = false;

      if (Brackets->hasPendingEvent()) {
        BlockInfo *MoveBracketsToSucc = nullptr;
        for (MachineBasicBlock *Succ : MBB->successors()) {
          auto SuccBII = BlockInfos.find(Succ);
          BlockInfo &SuccBI = SuccBII->second;
          if (!SuccBI.Incoming) {
            SuccBI.Dirty = true;
            if (SuccBII <= BII)
              Repeat = true;
            if (!MoveBracketsToSucc) {
              MoveBracketsToSucc = &SuccBI;
            } else {
              SuccBI.Incoming = std::make_unique<WaitcntBrackets>(*Brackets);
            }
          } else if (SuccBI.Incoming->merge(*Brackets)) {
            SuccBI.Dirty = true;
            if (SuccBII <= BII)
              Repeat = true;
          }
        }
        if (MoveBracketsToSucc)
          MoveBracketsToSucc->Incoming = std::move(Brackets);
      }
    }
  } while (Repeat);

  if (ST->hasScalarStores()) {
    SmallVector<MachineBasicBlock *, 4> EndPgmBlocks;
    bool HaveScalarStores = false;

    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        if (!HaveScalarStores && TII->isScalarStore(MI))
          HaveScalarStores = true;

        if (MI.getOpcode() == AMDGPU::S_ENDPGM ||
            MI.getOpcode() == AMDGPU::SI_RETURN_TO_EPILOG)
          EndPgmBlocks.push_back(&MBB);
      }
    }

    if (HaveScalarStores) {
      // If scalar writes are used, the cache must be flushed or else the next
      // wave to reuse the same scratch memory can be clobbered.
      //
      // Insert s_dcache_wb at wave termination points if there were any scalar
      // stores, and only if the cache hasn't already been flushed. This could
      // be improved by looking across blocks for flushes in postdominating
      // blocks from the stores but an explicitly requested flush is probably
      // very rare.
      for (MachineBasicBlock *MBB : EndPgmBlocks) {
        bool SeenDCacheWB = false;

        for (MachineBasicBlock::iterator I = MBB->begin(), E = MBB->end();
             I != E; ++I) {
          if (I->getOpcode() == AMDGPU::S_DCACHE_WB)
            SeenDCacheWB = true;
          else if (TII->isScalarStore(*I))
            SeenDCacheWB = false;

          // FIXME: It would be better to insert this before a waitcnt if any.
          if ((I->getOpcode() == AMDGPU::S_ENDPGM ||
               I->getOpcode() == AMDGPU::SI_RETURN_TO_EPILOG) &&
              !SeenDCacheWB) {
            Modified = true;
            BuildMI(*MBB, I, I->getDebugLoc(), TII->get(AMDGPU::S_DCACHE_WB));
          }
        }
      }
    }
  }

  // Insert DEALLOC_VGPR messages before previously identified S_ENDPGM
  // instructions.
  for (MachineInstr *MI : ReleaseVGPRInsts) {
    if (ST->requiresNopBeforeDeallocVGPRs()) {
      BuildMI(*MI->getParent(), MI, MI->getDebugLoc(), TII->get(AMDGPU::S_NOP))
          .addImm(0);
    }
    BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
            TII->get(AMDGPU::S_SENDMSG))
        .addImm(AMDGPU::SendMsg::ID_DEALLOC_VGPRS_GFX11Plus);
    Modified = true;
  }
  ReleaseVGPRInsts.clear();

  return Modified;
}
