//===- HexagonShuffler.cpp - Instruction bundle shuffling -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the shuffling of insns inside a bundle according to the
// packet formation rules of the Hexagon ISA.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonShuffler.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

#define DEBUG_TYPE "hexagon-shuffle"

using namespace llvm;

namespace {

// Insn shuffling priority.
class HexagonBid {
  // The priority is directly proportional to how restricted the insn is based
  // on its flexibility to run on the available slots.  So, the fewer slots it
  // may run on, the higher its priority.
  enum { MAX = 360360 }; // LCD of 1/2, 1/3, 1/4,... 1/15.
  unsigned Bid = 0;

public:
  HexagonBid() = default;
  HexagonBid(unsigned B) { Bid = B ? MAX / llvm::popcount(B) : 0; }

  // Check if the insn priority is overflowed.
  bool isSold() const { return (Bid >= MAX); }

  HexagonBid &operator+=(const HexagonBid &B) {
    Bid += B.Bid;
    return *this;
  }
};

// Slot shuffling allocation.
class HexagonUnitAuction {
  HexagonBid Scores[HEXAGON_PACKET_SIZE];
  // Mask indicating which slot is unavailable.
  unsigned isSold : HEXAGON_PACKET_SIZE;

public:
  HexagonUnitAuction(unsigned cs = 0) : isSold(cs) {}

  // Allocate slots.
  bool bid(unsigned B) {
    // Exclude already auctioned slots from the bid.
    unsigned b = B & ~isSold;
    if (b) {
      for (unsigned i = 0; i < HEXAGON_PACKET_SIZE; ++i)
        if (b & (1 << i)) {
          // Request candidate slots.
          Scores[i] += HexagonBid(b);
          isSold |= Scores[i].isSold() << i;
        }
      return true;
    } else
      // Error if the desired slots are already full.
      return false;
  }
};

} // end anonymous namespace

unsigned HexagonResource::setWeight(unsigned s) {
  const unsigned SlotWeight = 8;
  const unsigned MaskWeight = SlotWeight - 1;
  unsigned Units = getUnits();
  unsigned Key = ((1u << s) & Units) != 0;

  // Calculate relative weight of the insn for the given slot, weighing it the
  // heavier the more restrictive the insn is and the lowest the slots that the
  // insn may be executed in.
  if (Key == 0 || Units == 0 || (SlotWeight * s >= 32))
    return Weight = 0;

  unsigned Ctpop = llvm::popcount(Units);
  unsigned Cttz = llvm::countr_zero(Units);
  Weight = (1u << (SlotWeight * s)) * ((MaskWeight - Ctpop) << Cttz);
  return Weight;
}

HexagonCVIResource::HexagonCVIResource(MCInstrInfo const &MCII,
                                       MCSubtargetInfo const &STI,
                                       unsigned s,
                                       MCInst const *id)
    : HexagonResource(s) {

  const unsigned ItinUnits = HexagonMCInstrInfo::getCVIResources(MCII, STI, *id);
  unsigned Lanes;
  const unsigned Units = HexagonConvertUnits(ItinUnits, &Lanes);

  if (Units == 0 && Lanes == 0) {
    // For core insns.
    Valid = false;
    setUnits(0);
    setLanes(0);
    setLoad(false);
    setStore(false);
  } else {
    // For an HVX insn.
    Valid = true;
    setUnits(Units);
    setLanes(Lanes);
    setLoad(HexagonMCInstrInfo::getDesc(MCII, *id).mayLoad());
    setStore(HexagonMCInstrInfo::getDesc(MCII, *id).mayStore());
  }
}

struct CVIUnits {
  unsigned Units;
  unsigned Lanes;
};
using HVXInstsT = SmallVector<struct CVIUnits, 8>;

static unsigned makeAllBits(unsigned startBit, unsigned Lanes)
{
  for (unsigned i = 1; i < Lanes; ++i)
    startBit = (startBit << 1) | startBit;
  return startBit;
}

static bool checkHVXPipes(const HVXInstsT &hvxInsts, unsigned startIdx,
                          unsigned usedUnits) {
  if (startIdx < hvxInsts.size()) {
    if (!hvxInsts[startIdx].Units)
      return checkHVXPipes(hvxInsts, startIdx + 1, usedUnits);
    for (unsigned b = 0x1; b <= 0x8; b <<= 1) {
      if ((hvxInsts[startIdx].Units & b) == 0)
        continue;
      unsigned allBits = makeAllBits(b, hvxInsts[startIdx].Lanes);
      if ((allBits & usedUnits) == 0) {
        if (checkHVXPipes(hvxInsts, startIdx + 1, usedUnits | allBits))
          return true;
      }
    }
    return false;
  }
  return true;
}

HexagonShuffler::HexagonShuffler(MCContext &Context, bool ReportErrors,
                                 MCInstrInfo const &MCII,
                                 MCSubtargetInfo const &STI)
    : Context(Context), BundleFlags(), MCII(MCII), STI(STI),
      ReportErrors(ReportErrors), CheckFailure() {
  reset();
}

void HexagonShuffler::reset() {
  Packet.clear();
  BundleFlags = 0;
  CheckFailure = false;
}

void HexagonShuffler::append(MCInst const &ID, MCInst const *Extender,
                             unsigned S) {
  HexagonInstr PI(MCII, STI, &ID, Extender, S);

  Packet.push_back(PI);
}


static const unsigned Slot0Mask = 1 << 0;
static const unsigned Slot1Mask = 1 << 1;
static const unsigned Slot3Mask = 1 << 3;
static const unsigned slotSingleLoad = Slot0Mask;
static const unsigned slotSingleStore = Slot0Mask;

void HexagonShuffler::restrictSlot1AOK(HexagonPacketSummary const &Summary) {
  if (Summary.Slot1AOKLoc)
    for (HexagonInstr &ISJ : insts()) {
      MCInst const &Inst = ISJ.getDesc();
      const unsigned Type = HexagonMCInstrInfo::getType(MCII, Inst);
      if (Type != HexagonII::TypeALU32_2op &&
          Type != HexagonII::TypeALU32_3op &&
          Type != HexagonII::TypeALU32_ADDI) {
        const unsigned Units = ISJ.Core.getUnits();

        if (Units & Slot1Mask) {
          AppliedRestrictions.push_back(std::make_pair(
              Inst.getLoc(),
              "Instruction was restricted from being in slot 1"));
          AppliedRestrictions.push_back(std::make_pair(
              *Summary.Slot1AOKLoc, "Instruction can only be combined "
                                    "with an ALU instruction in slot 1"));
          ISJ.Core.setUnits(Units & ~Slot1Mask);
        }
      }
    }
}

void HexagonShuffler::restrictNoSlot1Store(
    HexagonPacketSummary const &Summary) {
  // If this packet contains an instruction that bars slot-1 stores,
  // we should mask off slot 1 from all of the store instructions in
  // this packet.

  if (!Summary.NoSlot1StoreLoc)
    return;

  bool AppliedRestriction = false;

  for (HexagonInstr &ISJ : insts()) {
    MCInst const &Inst = ISJ.getDesc();
    if (HexagonMCInstrInfo::getDesc(MCII, Inst).mayStore()) {
      unsigned Units = ISJ.Core.getUnits();
      if (Units & Slot1Mask) {
        AppliedRestriction = true;
        AppliedRestrictions.push_back(std::make_pair(
            Inst.getLoc(), "Instruction was restricted from being in slot 1"));
        ISJ.Core.setUnits(Units & ~Slot1Mask);
      }
    }
  }

  if (AppliedRestriction)
    AppliedRestrictions.push_back(
        std::make_pair(*Summary.NoSlot1StoreLoc,
                       "Instruction does not allow a store in slot 1"));
}

bool HexagonShuffler::applySlotRestrictions(HexagonPacketSummary const &Summary,
                                            const bool DoShuffle) {
  // These restrictions can modify the slot masks in the instructions
  // in the Packet member.  They should run unconditionally and their
  // order does not matter.
  restrictSlot1AOK(Summary);
  restrictNoSlot1Store(Summary);

  permitNonSlot();

  // These restrictions can modify the slot masks in the instructions
  // in the Packet member, but they can also detect constraint failures
  // which are fatal.
  if (!CheckFailure)
    restrictStoreLoadOrder(Summary);
  if (!CheckFailure)
    restrictBranchOrder(Summary);
  if (!CheckFailure)
    restrictPreferSlot3(Summary, DoShuffle);
  return !CheckFailure;
}

void HexagonShuffler::restrictBranchOrder(HexagonPacketSummary const &Summary) {
  // preserve branch order
  const bool HasMultipleBranches = Summary.branchInsts.size() > 1;
  if (!HasMultipleBranches)
    return;

  if (Summary.branchInsts.size() > 2) {
    reportError(Twine("too many branches in packet"));
    return;
  }

  const static std::pair<unsigned, unsigned> jumpSlots[] = {
      {8, 4}, {8, 2}, {8, 1}, {4, 2}, {4, 1}, {2, 1}};
  // try all possible choices
  for (std::pair<unsigned, unsigned> jumpSlot : jumpSlots) {
    // validate first jump with this slot rule
    if (!(jumpSlot.first & Summary.branchInsts[0]->Core.getUnits()))
      continue;

    // validate second jump with this slot rule
    if (!(jumpSlot.second & Summary.branchInsts[1]->Core.getUnits()))
      continue;

    // both valid for this configuration, set new slot rules
    const HexagonPacket PacketSave = Packet;
    Summary.branchInsts[0]->Core.setUnits(jumpSlot.first);
    Summary.branchInsts[1]->Core.setUnits(jumpSlot.second);

    const bool HasShuffledPacket = tryAuction(Summary).has_value();
    if (HasShuffledPacket)
      return;

    // if yes, great, if not then restore original slot mask
    // restore original values
    Packet = PacketSave;
  }

  reportResourceError(Summary, "out of slots");
}

void HexagonShuffler::permitNonSlot() {
  for (HexagonInstr &ISJ : insts()) {
    const bool RequiresSlot = HexagonMCInstrInfo::requiresSlot(STI, *ISJ.ID);
    if (!RequiresSlot)
      ISJ.Core.setAllUnits();
  }
}

bool HexagonShuffler::ValidResourceUsage(HexagonPacketSummary const &Summary) {
  std::optional<HexagonPacket> ShuffledPacket = tryAuction(Summary);

  if (!ShuffledPacket) {
    reportResourceError(Summary, "slot error");
    return false;
  }

  // Verify the CVI slot subscriptions.
  llvm::stable_sort(*ShuffledPacket, HexagonInstr::lessCVI);
  // create vector of hvx instructions to check
  HVXInstsT hvxInsts;
  hvxInsts.clear();
  for (const auto &I : *ShuffledPacket) {
    struct CVIUnits inst;
    inst.Units = I.CVI.getUnits();
    inst.Lanes = I.CVI.getLanes();
    if (inst.Units == 0)
      continue; // not an hvx inst or an hvx inst that doesn't uses any pipes
    hvxInsts.push_back(inst);
  }

  // if there are any hvx instructions in this packet, check pipe usage
  if (hvxInsts.size() > 0) {
    unsigned startIdx, usedUnits;
    startIdx = usedUnits = 0x0;
    if (!checkHVXPipes(hvxInsts, startIdx, usedUnits)) {
      // too many pipes used to be valid
      reportError(Twine("invalid instruction packet: slot error"));
      return false;
    }
  }

  Packet = *ShuffledPacket;

  return true;
}

bool HexagonShuffler::restrictStoreLoadOrder(
    HexagonPacketSummary const &Summary) {
  // Modify packet accordingly.
  // TODO: need to reserve slots #0 and #1 for duplex insns.
  static const unsigned slotFirstLoadStore = Slot1Mask;
  static const unsigned slotLastLoadStore = Slot0Mask;
  unsigned slotLoadStore = slotFirstLoadStore;

  for (iterator ISJ = begin(); ISJ != end(); ++ISJ) {
    MCInst const &ID = ISJ->getDesc();

    if (!ISJ->Core.getUnits())
      // Error if insn may not be executed in any slot.
      return false;

    // A single load must use slot #0.
    if (HexagonMCInstrInfo::getDesc(MCII, ID).mayLoad()) {
      if (Summary.loads == 1 && Summary.loads == Summary.memory &&
          Summary.memops == 0)
        // Pin the load to slot #0.
        switch (ID.getOpcode()) {
        case Hexagon::V6_vgathermw:
        case Hexagon::V6_vgathermh:
        case Hexagon::V6_vgathermhw:
        case Hexagon::V6_vgathermwq:
        case Hexagon::V6_vgathermhq:
        case Hexagon::V6_vgathermhwq:
          // Slot1 only loads
          break;
        default:
          ISJ->Core.setUnits(ISJ->Core.getUnits() & slotSingleLoad);
          break;
        }
      else if (Summary.loads >= 1 && isMemReorderDisabled()) { // }:mem_noshuf
        // Loads must keep the original order ONLY if
        // isMemReorderDisabled() == true
        if (slotLoadStore < slotLastLoadStore) {
          // Error if no more slots available for loads.
          reportError("invalid instruction packet: too many loads");
          return false;
        }
        // Pin the load to the highest slot available to it.
        ISJ->Core.setUnits(ISJ->Core.getUnits() & slotLoadStore);
        // Update the next highest slot available to loads.
        slotLoadStore >>= 1;
      }
    }

    // A single store must use slot #0.
    if (HexagonMCInstrInfo::getDesc(MCII, ID).mayStore()) {
      if (!Summary.store0) {
        const bool PacketHasNoOnlySlot0 =
            llvm::none_of(insts(), [&](HexagonInstr const &I) {
              return I.Core.getUnits() == Slot0Mask &&
                     I.ID->getOpcode() != ID.getOpcode();
            });
        const bool SafeToMoveToSlot0 =
            (Summary.loads == 0) ||
            (!isMemReorderDisabled() && PacketHasNoOnlySlot0);

        if (Summary.stores == 1 && SafeToMoveToSlot0)
          // Pin the store to slot #0 only if isMemReorderDisabled() == false
          ISJ->Core.setUnits(ISJ->Core.getUnits() & slotSingleStore);
        else if (Summary.stores >= 1) {
          if (slotLoadStore < slotLastLoadStore) {
            // Error if no more slots available for stores.
            reportError("invalid instruction packet: too many stores");
            return false;
          }
          // Pin the store to the highest slot available to it.
          ISJ->Core.setUnits(ISJ->Core.getUnits() & slotLoadStore);
          // Update the next highest slot available to stores.
          slotLoadStore >>= 1;
        }
      }
      if (Summary.store1 && Summary.stores > 1) {
        // Error if a single store with another store.
        reportError("invalid instruction packet: too many stores");
        return false;
      }
    }
  }

  return true;
}

static std::string SlotMaskToText(unsigned SlotMask) {
    SmallVector<std::string, HEXAGON_PRESHUFFLE_PACKET_SIZE> Slots;
    for (unsigned SlotNum = 0; SlotNum < HEXAGON_PACKET_SIZE; SlotNum++)
        if ((SlotMask & (1 << SlotNum)) != 0)
            Slots.push_back(utostr(SlotNum));

    return llvm::join(Slots, StringRef(", "));
}

HexagonShuffler::HexagonPacketSummary HexagonShuffler::GetPacketSummary() {
  HexagonPacketSummary Summary = HexagonPacketSummary();

  // Collect information from the insns in the packet.
  for (iterator ISJ = begin(); ISJ != end(); ++ISJ) {
    MCInst const &ID = ISJ->getDesc();

    if (HexagonMCInstrInfo::isRestrictSlot1AOK(MCII, ID))
      Summary.Slot1AOKLoc = ID.getLoc();
    if (HexagonMCInstrInfo::isRestrictNoSlot1Store(MCII, ID))
      Summary.NoSlot1StoreLoc = ID.getLoc();

    if (HexagonMCInstrInfo::prefersSlot3(MCII, ID)) {
      ++Summary.pSlot3Cnt;
      Summary.PrefSlot3Inst = ISJ;
    }
    const unsigned ReservedSlots =
        HexagonMCInstrInfo::getOtherReservedSlots(MCII, STI, ID);
    Summary.ReservedSlotMask |= ReservedSlots;
    if (ReservedSlots != 0)
      AppliedRestrictions.push_back(std::make_pair(ID.getLoc(),
                  (Twine("Instruction has reserved slots: ") +
                   SlotMaskToText(ReservedSlots)).str()));

    switch (HexagonMCInstrInfo::getType(MCII, ID)) {
    case HexagonII::TypeS_2op:
    case HexagonII::TypeS_3op:
    case HexagonII::TypeALU64:
      break;
    case HexagonII::TypeJ:
      if (HexagonMCInstrInfo::IsABranchingInst(MCII, STI, *ISJ->ID))
        Summary.branchInsts.push_back(ISJ);
      break;
    case HexagonII::TypeCVI_VM_VP_LDU:
    case HexagonII::TypeCVI_VM_LD:
    case HexagonII::TypeCVI_VM_TMP_LD:
    case HexagonII::TypeCVI_GATHER:
    case HexagonII::TypeCVI_GATHER_DV:
    case HexagonII::TypeCVI_GATHER_RST:
      ++Summary.NonZCVIloads;
      [[fallthrough]];
    case HexagonII::TypeCVI_ZW:
      ++Summary.AllCVIloads;
      [[fallthrough]];
    case HexagonII::TypeLD:
      ++Summary.loads;
      ++Summary.memory;
      if (ISJ->Core.getUnits() == slotSingleLoad ||
          HexagonMCInstrInfo::getType(MCII, ID) == HexagonII::TypeCVI_VM_VP_LDU)
        ++Summary.load0;
      if (HexagonMCInstrInfo::getDesc(MCII, ID).isReturn())
        Summary.branchInsts.push_back(ISJ);
      break;
    case HexagonII::TypeCVI_VM_STU:
    case HexagonII::TypeCVI_VM_ST:
    case HexagonII::TypeCVI_VM_NEW_ST:
    case HexagonII::TypeCVI_SCATTER:
    case HexagonII::TypeCVI_SCATTER_DV:
    case HexagonII::TypeCVI_SCATTER_RST:
    case HexagonII::TypeCVI_SCATTER_NEW_RST:
    case HexagonII::TypeCVI_SCATTER_NEW_ST:
      ++Summary.CVIstores;
      [[fallthrough]];
    case HexagonII::TypeST:
      ++Summary.stores;
      ++Summary.memory;
      if (ISJ->Core.getUnits() == slotSingleStore ||
          HexagonMCInstrInfo::getType(MCII, ID) == HexagonII::TypeCVI_VM_STU)
        ++Summary.store0;
      break;
    case HexagonII::TypeV4LDST:
      ++Summary.loads;
      ++Summary.stores;
      ++Summary.store1;
      ++Summary.memops;
      ++Summary.memory;
      break;
    case HexagonII::TypeNCJ:
      ++Summary.memory; // NV insns are memory-like.
      Summary.branchInsts.push_back(ISJ);
      break;
    case HexagonII::TypeV2LDST:
      if (HexagonMCInstrInfo::getDesc(MCII, ID).mayLoad()) {
        ++Summary.loads;
        ++Summary.memory;
        if (ISJ->Core.getUnits() == slotSingleLoad ||
            HexagonMCInstrInfo::getType(MCII, ID) ==
                HexagonII::TypeCVI_VM_VP_LDU)
          ++Summary.load0;
      } else {
        assert(HexagonMCInstrInfo::getDesc(MCII, ID).mayStore());
        ++Summary.memory;
        ++Summary.stores;
      }
      break;
    case HexagonII::TypeCR:
    // Legacy conditional branch predicated on a register.
    case HexagonII::TypeCJ:
      if (HexagonMCInstrInfo::getDesc(MCII, ID).isBranch())
        Summary.branchInsts.push_back(ISJ);
      break;
    case HexagonII::TypeDUPLEX: {
      ++Summary.duplex;
      MCInst const &Inst0 = *ID.getOperand(0).getInst();
      MCInst const &Inst1 = *ID.getOperand(1).getInst();
      if (HexagonMCInstrInfo::getDesc(MCII, Inst0).isBranch())
        Summary.branchInsts.push_back(ISJ);
      if (HexagonMCInstrInfo::getDesc(MCII, Inst1).isBranch())
        Summary.branchInsts.push_back(ISJ);
      if (HexagonMCInstrInfo::getDesc(MCII, Inst0).isReturn())
        Summary.branchInsts.push_back(ISJ);
      if (HexagonMCInstrInfo::getDesc(MCII, Inst1).isReturn())
        Summary.branchInsts.push_back(ISJ);
      break;
    }
    }
  }
  return Summary;
}

bool HexagonShuffler::ValidPacketMemoryOps(
    HexagonPacketSummary const &Summary) const {
  // Check if the packet is legal.
  const unsigned ZCVIloads = Summary.AllCVIloads - Summary.NonZCVIloads;
  const bool ValidHVXMem =
      Summary.NonZCVIloads <= 1 && ZCVIloads <= 1 && Summary.CVIstores <= 1;
  const bool InvalidPacket =
      ((Summary.load0 > 1 || Summary.store0 > 1 || !ValidHVXMem) ||
       (Summary.duplex > 1 || (Summary.duplex && Summary.memory)));

  return !InvalidPacket;
}

void HexagonShuffler::restrictPreferSlot3(HexagonPacketSummary const &Summary,
                                          const bool DoShuffle) {
  // flag if an instruction requires to be in slot 3
  const bool HasOnlySlot3 = llvm::any_of(insts(), [&](HexagonInstr const &I) {
    return (I.Core.getUnits() == Slot3Mask);
  });
  const bool NeedsPrefSlot3Shuffle = Summary.branchInsts.size() <= 1 &&
                                     !HasOnlySlot3 && Summary.pSlot3Cnt == 1 &&
                                     Summary.PrefSlot3Inst && DoShuffle;

  if (!NeedsPrefSlot3Shuffle)
    return;

  HexagonInstr *PrefSlot3Inst = *Summary.PrefSlot3Inst;
  // save off slot mask of instruction marked with A_PREFER_SLOT3
  // and then pin it to slot #3
  const unsigned saveUnits = PrefSlot3Inst->Core.getUnits();
  PrefSlot3Inst->Core.setUnits(saveUnits & Slot3Mask);
  const bool HasShuffledPacket = tryAuction(Summary).has_value();
  if (HasShuffledPacket)
    return;

  PrefSlot3Inst->Core.setUnits(saveUnits);
}

/// Check that the packet is legal and enforce relative insn order.
bool HexagonShuffler::check(const bool RequireShuffle) {
  const HexagonPacketSummary Summary = GetPacketSummary();
  if (!applySlotRestrictions(Summary, RequireShuffle))
    return false;

  if (!ValidPacketMemoryOps(Summary)) {
    reportError("invalid instruction packet");
    return false;
  }

  if (RequireShuffle)
    ValidResourceUsage(Summary);

  return !CheckFailure;
}

std::optional<HexagonShuffler::HexagonPacket>
HexagonShuffler::tryAuction(HexagonPacketSummary const &Summary) {
  HexagonPacket PacketResult = Packet;
  HexagonUnitAuction AuctionCore(Summary.ReservedSlotMask);
  llvm::stable_sort(PacketResult, HexagonInstr::lessCore);

  const bool ValidSlots =
      llvm::all_of(insts(PacketResult), [&AuctionCore](HexagonInstr const &I) {
        return AuctionCore.bid(I.Core.getUnits());
      });

  LLVM_DEBUG(
    dbgs() << "Shuffle attempt: " << (ValidSlots ? "passed" : "failed")
           << "\n";
    for (HexagonInstr const &ISJ : insts(PacketResult))
      dbgs() << "\t" << HexagonMCInstrInfo::getName(MCII, *ISJ.ID) << ": "
             << llvm::format_hex(ISJ.Core.getUnits(), 4, true) << "\n";
  );

  std::optional<HexagonPacket> Res;
  if (ValidSlots)
    Res = PacketResult;

  return Res;
}

bool HexagonShuffler::shuffle() {
  if (size() > HEXAGON_PACKET_SIZE) {
    // Ignore a packet with more than what a packet can hold
    // or with compound or duplex insns for now.
    reportError("invalid instruction packet");
    return false;
  }

  // Check and prepare packet.
  bool Ok = check();
  if (size() > 1 && Ok)
    // Reorder the handles for each slot.
    for (unsigned nSlot = 0, emptySlots = 0; nSlot < HEXAGON_PACKET_SIZE;
         ++nSlot) {
      iterator ISJ, ISK;
      unsigned slotSkip, slotWeight;

      // Prioritize the handles considering their restrictions.
      for (ISJ = ISK = Packet.begin(), slotSkip = slotWeight = 0;
           ISK != Packet.end(); ++ISK, ++slotSkip)
        if (slotSkip < nSlot - emptySlots)
          // Note which handle to begin at.
          ++ISJ;
        else
          // Calculate the weight of the slot.
          slotWeight += ISK->Core.setWeight(HEXAGON_PACKET_SIZE - nSlot - 1);

      if (slotWeight)
        // Sort the packet, favoring source order,
        // beginning after the previous slot.
        std::stable_sort(ISJ, Packet.end());
      else
        // Skip unused slot.
        ++emptySlots;
    }

  LLVM_DEBUG(
    for (HexagonInstr const &ISJ : insts()) {
      dbgs().write_hex(ISJ.Core.getUnits());
      if (ISJ.CVI.isValid()) {
        dbgs() << '/';
        dbgs().write_hex(ISJ.CVI.getUnits()) << '|';
        dbgs() << ISJ.CVI.getLanes();
      }
      dbgs() << ':'
             << HexagonMCInstrInfo::getDesc(MCII, ISJ.getDesc()).getOpcode()
             << '\n';
    } dbgs() << '\n';
  );

  return Ok;
}

void HexagonShuffler::reportResourceError(HexagonPacketSummary const &Summary, StringRef Err) {
  if (ReportErrors)
    reportResourceUsage(Summary);
  reportError(Twine("invalid instruction packet: ") + Err);
}


void HexagonShuffler::reportResourceUsage(HexagonPacketSummary const &Summary) {
  auto SM = Context.getSourceManager();
  if (SM) {
    for (HexagonInstr const &I : insts()) {
      const unsigned Units = I.Core.getUnits();

      if (HexagonMCInstrInfo::requiresSlot(STI, *I.ID)) {
        const std::string UnitsText = Units ? SlotMaskToText(Units) : "<None>";
        SM->PrintMessage(I.ID->getLoc(), SourceMgr::DK_Note,
                Twine("Instruction can utilize slots: ") +
                UnitsText);
      }
      else if (!HexagonMCInstrInfo::isImmext(*I.ID))
        SM->PrintMessage(I.ID->getLoc(), SourceMgr::DK_Note,
                       "Instruction does not require a slot");
    }
  }
}

void HexagonShuffler::reportError(Twine const &Msg) {
  CheckFailure = true;
  if (ReportErrors) {
    for (auto const &I : AppliedRestrictions) {
      auto SM = Context.getSourceManager();
      if (SM)
        SM->PrintMessage(I.first, SourceMgr::DK_Note, I.second);
    }
    Context.reportError(Loc, Msg);
  }
}
