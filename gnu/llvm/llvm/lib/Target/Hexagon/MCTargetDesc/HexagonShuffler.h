//===- HexagonShuffler.h - Instruction bundle shuffling ---------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONSHUFFLER_H
#define LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONSHUFFLER_H

#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace llvm {

class MCContext;
class MCInst;
class MCInstrInfo;
class MCSubtargetInfo;

// Insn resources.
class HexagonResource {
  // Mask of the slots or units that may execute the insn and
  // the weight or priority that the insn requires to be assigned a slot.
  unsigned Slots, Weight;

public:
  HexagonResource(unsigned s) { setUnits(s); }

  void setUnits(unsigned s) {
    Slots = s & ((1u << HEXAGON_PACKET_SIZE) - 1);
    setWeight(s);
  }

  void setAllUnits() {
    setUnits(((1u << HEXAGON_PACKET_SIZE) - 1));
  }
  unsigned setWeight(unsigned s);

  unsigned getUnits() const { return (Slots); }
  unsigned getWeight() const { return (Weight); }

  // Check if the resources are in ascending slot order.
  static bool lessUnits(const HexagonResource &A, const HexagonResource &B) {
    return (llvm::popcount(A.getUnits()) < llvm::popcount(B.getUnits()));
  }

  // Check if the resources are in ascending weight order.
  static bool lessWeight(const HexagonResource &A, const HexagonResource &B) {
    return (A.getWeight() < B.getWeight());
  }
};

// HVX insn resources.
class HexagonCVIResource : public HexagonResource {
public:
  using UnitsAndLanes = std::pair<unsigned, unsigned>;

private:
  // Count of adjacent slots that the insn requires to be executed.
  unsigned Lanes;
  // Flag whether the insn is a load or a store.
  bool Load, Store;
  // Flag whether the HVX resources are valid.
  bool Valid;

  void setLanes(unsigned l) { Lanes = l; }
  void setLoad(bool f = true) { Load = f; }
  void setStore(bool f = true) { Store = f; }

public:
  HexagonCVIResource(MCInstrInfo const &MCII,
                     MCSubtargetInfo const &STI,
                     unsigned s, MCInst const *id);

  bool isValid() const { return Valid; }
  unsigned getLanes() const { return Lanes; }
  bool mayLoad() const { return Load; }
  bool mayStore() const { return Store; }
};

// Handle to an insn used by the shuffling algorithm.
class HexagonInstr {
  friend class HexagonShuffler;

  MCInst const *ID;
  MCInst const *Extender;
  HexagonResource Core;
  HexagonCVIResource CVI;

public:
  HexagonInstr(MCInstrInfo const &MCII,
               MCSubtargetInfo const &STI, MCInst const *id,
               MCInst const *Extender, unsigned s)
      : ID(id), Extender(Extender), Core(s), CVI(MCII, STI, s, id){};

  MCInst const &getDesc() const { return *ID; }
  MCInst const *getExtender() const { return Extender; }

  // Check if the handles are in ascending order for shuffling purposes.
  bool operator<(const HexagonInstr &B) const {
    return (HexagonResource::lessWeight(B.Core, Core));
  }

  // Check if the handles are in ascending order by core slots.
  static bool lessCore(const HexagonInstr &A, const HexagonInstr &B) {
    return (HexagonResource::lessUnits(A.Core, B.Core));
  }

  // Check if the handles are in ascending order by HVX slots.
  static bool lessCVI(const HexagonInstr &A, const HexagonInstr &B) {
    return (HexagonResource::lessUnits(A.CVI, B.CVI));
  }
};

// Bundle shuffler.
class HexagonShuffler {
  using HexagonPacket =
      SmallVector<HexagonInstr, HEXAGON_PRESHUFFLE_PACKET_SIZE>;

  struct HexagonPacketSummary {
    // Number of memory operations, loads, solo loads, stores, solo stores,
    // single stores.
    unsigned memory;
    unsigned loads;
    unsigned load0;
    unsigned stores;
    unsigned store0;
    unsigned store1;
    unsigned NonZCVIloads;
    unsigned AllCVIloads;
    unsigned CVIstores;
    // Number of duplex insns
    unsigned duplex;
    unsigned pSlot3Cnt;
    std::optional<HexagonInstr *> PrefSlot3Inst;
    unsigned memops;
    unsigned ReservedSlotMask;
    SmallVector<HexagonInstr *, HEXAGON_PRESHUFFLE_PACKET_SIZE> branchInsts;
    std::optional<SMLoc> Slot1AOKLoc;
    std::optional<SMLoc> NoSlot1StoreLoc;
  };
  // Insn handles in a bundle.
  HexagonPacket Packet;

protected:
  MCContext &Context;
  int64_t BundleFlags;
  MCInstrInfo const &MCII;
  MCSubtargetInfo const &STI;
  SMLoc Loc;
  bool ReportErrors;
  bool CheckFailure;
  std::vector<std::pair<SMLoc, std::string>> AppliedRestrictions;

  bool applySlotRestrictions(HexagonPacketSummary const &Summary,
                             const bool DoShuffle);
  void restrictSlot1AOK(HexagonPacketSummary const &Summary);
  void restrictNoSlot1Store(HexagonPacketSummary const &Summary);
  void restrictNoSlot1();
  bool restrictStoreLoadOrder(HexagonPacketSummary const &Summary);
  void restrictBranchOrder(HexagonPacketSummary const &Summary);
  void restrictPreferSlot3(HexagonPacketSummary const &Summary,
                           const bool DoShuffle);
  void permitNonSlot();

  std::optional<HexagonPacket> tryAuction(HexagonPacketSummary const &Summary);

  HexagonPacketSummary GetPacketSummary();
  bool ValidPacketMemoryOps(HexagonPacketSummary const &Summary) const;
  bool ValidResourceUsage(HexagonPacketSummary const &Summary);

public:
  using iterator = HexagonPacket::iterator;
  using const_iterator = HexagonPacket::const_iterator;
  using packet_range = iterator_range<HexagonPacket::iterator>;
  using const_packet_range = iterator_range<HexagonPacket::const_iterator>;

  HexagonShuffler(MCContext &Context, bool ReportErrors,
                  MCInstrInfo const &MCII, MCSubtargetInfo const &STI);

  // Reset to initial state.
  void reset();
  // Check if the bundle may be validly shuffled.
  bool check(const bool RequireShuffle = true);
  // Reorder the insn handles in the bundle.
  bool shuffle();

  unsigned size() const { return (Packet.size()); }

  bool isMemReorderDisabled() const {
    return (BundleFlags & HexagonMCInstrInfo::memReorderDisabledMask) != 0;
  }

  iterator begin() { return (Packet.begin()); }
  iterator end() { return (Packet.end()); }
  const_iterator cbegin() const { return (Packet.begin()); }
  const_iterator cend() const { return (Packet.end()); }
  packet_range insts(HexagonPacket &P) {
    return make_range(P.begin(), P.end());
  }
  const_packet_range insts(HexagonPacket const &P) const {
    return make_range(P.begin(), P.end());
  }
  packet_range insts() { return make_range(begin(), end()); }
  const_packet_range insts() const { return make_range(cbegin(), cend()); }

  using InstPredicate = bool (*)(MCInstrInfo const &, MCInst const &);

  bool HasInstWith(InstPredicate Pred) const {
    return llvm::any_of(insts(), [&](HexagonInstr const &I) {
      MCInst const &Inst = I.getDesc();
      return (*Pred)(MCII, Inst);
    });
  }

  // Add insn handle to the bundle .
  void append(MCInst const &ID, MCInst const *Extender, unsigned S);

  // Return the error code for the last check or shuffling of the bundle.
  void reportError(Twine const &Msg);
  void reportResourceError(HexagonPacketSummary const &Summary, StringRef Err);
  void reportResourceUsage(HexagonPacketSummary const &Summary);
};

} // end namespace llvm

#endif //  LLVM_LIB_TARGET_HEXAGON_MCTARGETDESC_HEXAGONSHUFFLER_H
