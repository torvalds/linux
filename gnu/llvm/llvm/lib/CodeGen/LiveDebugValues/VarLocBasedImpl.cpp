//===- VarLocBasedImpl.cpp - Tracking Debug Value MIs with VarLoc class----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file VarLocBasedImpl.cpp
///
/// LiveDebugValues is an optimistic "available expressions" dataflow
/// algorithm. The set of expressions is the set of machine locations
/// (registers, spill slots, constants, and target indices) that a variable
/// fragment might be located, qualified by a DIExpression and indirect-ness
/// flag, while each variable is identified by a DebugVariable object. The
/// availability of an expression begins when a DBG_VALUE instruction specifies
/// the location of a DebugVariable, and continues until that location is
/// clobbered or re-specified by a different DBG_VALUE for the same
/// DebugVariable.
///
/// The output of LiveDebugValues is additional DBG_VALUE instructions,
/// placed to extend variable locations as far they're available. This file
/// and the VarLocBasedLDV class is an implementation that explicitly tracks
/// locations, using the VarLoc class.
///
/// The canonical "available expressions" problem doesn't have expression
/// clobbering, instead when a variable is re-assigned, any expressions using
/// that variable get invalidated. LiveDebugValues can map onto "available
/// expressions" by having every register represented by a variable, which is
/// used in an expression that becomes available at a DBG_VALUE instruction.
/// When the register is clobbered, its variable is effectively reassigned, and
/// expressions computed from it become unavailable. A similar construct is
/// needed when a DebugVariable has its location re-specified, to invalidate
/// all other locations for that DebugVariable.
///
/// Using the dataflow analysis to compute the available expressions, we create
/// a DBG_VALUE at the beginning of each block where the expression is
/// live-in. This propagates variable locations into every basic block where
/// the location can be determined, rather than only having DBG_VALUEs in blocks
/// where locations are specified due to an assignment or some optimization.
/// Movements of values between registers and spill slots are annotated with
/// DBG_VALUEs too to track variable values bewteen locations. All this allows
/// DbgEntityHistoryCalculator to focus on only the locations within individual
/// blocks, facilitating testing and improving modularity.
///
/// We follow an optimisic dataflow approach, with this lattice:
///
/// \verbatim
///                    ┬ "Unknown"
///                          |
///                          v
///                         True
///                          |
///                          v
///                      ⊥ False
/// \endverbatim With "True" signifying that the expression is available (and
/// thus a DebugVariable's location is the corresponding register), while
/// "False" signifies that the expression is unavailable. "Unknown"s never
/// survive to the end of the analysis (see below).
///
/// Formally, all DebugVariable locations that are live-out of a block are
/// initialized to \top.  A blocks live-in values take the meet of the lattice
/// value for every predecessors live-outs, except for the entry block, where
/// all live-ins are \bot. The usual dataflow propagation occurs: the transfer
/// function for a block assigns an expression for a DebugVariable to be "True"
/// if a DBG_VALUE in the block specifies it; "False" if the location is
/// clobbered; or the live-in value if it is unaffected by the block. We
/// visit each block in reverse post order until a fixedpoint is reached. The
/// solution produced is maximal.
///
/// Intuitively, we start by assuming that every expression / variable location
/// is at least "True", and then propagate "False" from the entry block and any
/// clobbers until there are no more changes to make. This gives us an accurate
/// solution because all incorrect locations will have a "False" propagated into
/// them. It also gives us a solution that copes well with loops by assuming
/// that variable locations are live-through every loop, and then removing those
/// that are not through dataflow.
///
/// Within LiveDebugValues: each variable location is represented by a
/// VarLoc object that identifies the source variable, the set of
/// machine-locations that currently describe it (a single location for
/// DBG_VALUE or multiple for DBG_VALUE_LIST), and the DBG_VALUE inst that
/// specifies the location. Each VarLoc is indexed in the (function-scope) \p
/// VarLocMap, giving each VarLoc a set of unique indexes, each of which
/// corresponds to one of the VarLoc's machine-locations and can be used to
/// lookup the VarLoc in the VarLocMap. Rather than operate directly on machine
/// locations, the dataflow analysis in this pass identifies locations by their
/// indices in the VarLocMap, meaning all the variable locations in a block can
/// be described by a sparse vector of VarLocMap indices.
///
/// All the storage for the dataflow analysis is local to the ExtendRanges
/// method and passed down to helper methods. "OutLocs" and "InLocs" record the
/// in and out lattice values for each block. "OpenRanges" maintains a list of
/// variable locations and, with the "process" method, evaluates the transfer
/// function of each block. "flushPendingLocs" installs debug value instructions
/// for each live-in location at the start of blocks, while "Transfers" records
/// transfers of values between machine-locations.
///
/// We avoid explicitly representing the "Unknown" (\top) lattice value in the
/// implementation. Instead, unvisited blocks implicitly have all lattice
/// values set as "Unknown". After being visited, there will be path back to
/// the entry block where the lattice value is "False", and as the transfer
/// function cannot make new "Unknown" locations, there are no scenarios where
/// a block can have an "Unknown" location after being visited. Similarly, we
/// don't enumerate all possible variable locations before exploring the
/// function: when a new location is discovered, all blocks previously explored
/// were implicitly "False" but unrecorded, and become explicitly "False" when
/// a new VarLoc is created with its bit not set in predecessor InLocs or
/// OutLocs.
///
//===----------------------------------------------------------------------===//

#include "LiveDebugValues.h"

#include "llvm/ADT/CoalescingBitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "livedebugvalues"

STATISTIC(NumInserted, "Number of DBG_VALUE instructions inserted");

/// If \p Op is a stack or frame register return true, otherwise return false.
/// This is used to avoid basing the debug entry values on the registers, since
/// we do not support it at the moment.
static bool isRegOtherThanSPAndFP(const MachineOperand &Op,
                                  const MachineInstr &MI,
                                  const TargetRegisterInfo *TRI) {
  if (!Op.isReg())
    return false;

  const MachineFunction *MF = MI.getParent()->getParent();
  const TargetLowering *TLI = MF->getSubtarget().getTargetLowering();
  Register SP = TLI->getStackPointerRegisterToSaveRestore();
  Register FP = TRI->getFrameRegister(*MF);
  Register Reg = Op.getReg();

  return Reg && Reg != SP && Reg != FP;
}

namespace {

// Max out the number of statically allocated elements in DefinedRegsSet, as
// this prevents fallback to std::set::count() operations.
using DefinedRegsSet = SmallSet<Register, 32>;

// The IDs in this set correspond to MachineLocs in VarLocs, as well as VarLocs
// that represent Entry Values; every VarLoc in the set will also appear
// exactly once at Location=0.
// As a result, each VarLoc may appear more than once in this "set", but each
// range corresponding to a Reg, SpillLoc, or EntryValue type will still be a
// "true" set (i.e. each VarLoc may appear only once), and the range Location=0
// is the set of all VarLocs.
using VarLocSet = CoalescingBitVector<uint64_t>;

/// A type-checked pair of {Register Location (or 0), Index}, used to index
/// into a \ref VarLocMap. This can be efficiently converted to a 64-bit int
/// for insertion into a \ref VarLocSet, and efficiently converted back. The
/// type-checker helps ensure that the conversions aren't lossy.
///
/// Why encode a location /into/ the VarLocMap index? This makes it possible
/// to find the open VarLocs killed by a register def very quickly. This is a
/// performance-critical operation for LiveDebugValues.
struct LocIndex {
  using u32_location_t = uint32_t;
  using u32_index_t = uint32_t;

  u32_location_t Location; // Physical registers live in the range [1;2^30) (see
                           // \ref MCRegister), so we have plenty of range left
                           // here to encode non-register locations.
  u32_index_t Index;

  /// The location that has an entry for every VarLoc in the map.
  static constexpr u32_location_t kUniversalLocation = 0;

  /// The first location that is reserved for VarLocs with locations of kind
  /// RegisterKind.
  static constexpr u32_location_t kFirstRegLocation = 1;

  /// The first location greater than 0 that is not reserved for VarLocs with
  /// locations of kind RegisterKind.
  static constexpr u32_location_t kFirstInvalidRegLocation = 1 << 30;

  /// A special location reserved for VarLocs with locations of kind
  /// SpillLocKind.
  static constexpr u32_location_t kSpillLocation = kFirstInvalidRegLocation;

  /// A special location reserved for VarLocs of kind EntryValueBackupKind and
  /// EntryValueCopyBackupKind.
  static constexpr u32_location_t kEntryValueBackupLocation =
      kFirstInvalidRegLocation + 1;

  /// A special location reserved for VarLocs with locations of kind
  /// WasmLocKind.
  /// TODO Placing all Wasm target index locations in this single kWasmLocation
  /// may cause slowdown in compilation time in very large functions. Consider
  /// giving a each target index/offset pair its own u32_location_t if this
  /// becomes a problem.
  static constexpr u32_location_t kWasmLocation = kFirstInvalidRegLocation + 2;

  LocIndex(u32_location_t Location, u32_index_t Index)
      : Location(Location), Index(Index) {}

  uint64_t getAsRawInteger() const {
    return (static_cast<uint64_t>(Location) << 32) | Index;
  }

  template<typename IntT> static LocIndex fromRawInteger(IntT ID) {
    static_assert(std::is_unsigned_v<IntT> && sizeof(ID) == sizeof(uint64_t),
                  "Cannot convert raw integer to LocIndex");
    return {static_cast<u32_location_t>(ID >> 32),
            static_cast<u32_index_t>(ID)};
  }

  /// Get the start of the interval reserved for VarLocs of kind RegisterKind
  /// which reside in \p Reg. The end is at rawIndexForReg(Reg+1)-1.
  static uint64_t rawIndexForReg(Register Reg) {
    return LocIndex(Reg, 0).getAsRawInteger();
  }

  /// Return a range covering all set indices in the interval reserved for
  /// \p Location in \p Set.
  static auto indexRangeForLocation(const VarLocSet &Set,
                                    u32_location_t Location) {
    uint64_t Start = LocIndex(Location, 0).getAsRawInteger();
    uint64_t End = LocIndex(Location + 1, 0).getAsRawInteger();
    return Set.half_open_range(Start, End);
  }
};

// Simple Set for storing all the VarLoc Indices at a Location bucket.
using VarLocsInRange = SmallSet<LocIndex::u32_index_t, 32>;
// Vector of all `LocIndex`s for a given VarLoc; the same Location should not
// appear in any two of these, as each VarLoc appears at most once in any
// Location bucket.
using LocIndices = SmallVector<LocIndex, 2>;

class VarLocBasedLDV : public LDVImpl {
private:
  const TargetRegisterInfo *TRI;
  const TargetInstrInfo *TII;
  const TargetFrameLowering *TFI;
  TargetPassConfig *TPC;
  BitVector CalleeSavedRegs;
  LexicalScopes LS;
  VarLocSet::Allocator Alloc;

  const MachineInstr *LastNonDbgMI;

  enum struct TransferKind { TransferCopy, TransferSpill, TransferRestore };

  using FragmentInfo = DIExpression::FragmentInfo;
  using OptFragmentInfo = std::optional<DIExpression::FragmentInfo>;

  /// A pair of debug variable and value location.
  struct VarLoc {
    // The location at which a spilled variable resides. It consists of a
    // register and an offset.
    struct SpillLoc {
      unsigned SpillBase;
      StackOffset SpillOffset;
      bool operator==(const SpillLoc &Other) const {
        return SpillBase == Other.SpillBase && SpillOffset == Other.SpillOffset;
      }
      bool operator!=(const SpillLoc &Other) const {
        return !(*this == Other);
      }
    };

    // Target indices used for wasm-specific locations.
    struct WasmLoc {
      // One of TargetIndex values defined in WebAssembly.h. We deal with
      // local-related TargetIndex in this analysis (TI_LOCAL and
      // TI_LOCAL_INDIRECT). Stack operands (TI_OPERAND_STACK) will be handled
      // separately WebAssemblyDebugFixup pass, and we don't associate debug
      // info with values in global operands (TI_GLOBAL_RELOC) at the moment.
      int Index;
      int64_t Offset;
      bool operator==(const WasmLoc &Other) const {
        return Index == Other.Index && Offset == Other.Offset;
      }
      bool operator!=(const WasmLoc &Other) const { return !(*this == Other); }
    };

    /// Identity of the variable at this location.
    const DebugVariable Var;

    /// The expression applied to this location.
    const DIExpression *Expr;

    /// DBG_VALUE to clone var/expr information from if this location
    /// is moved.
    const MachineInstr &MI;

    enum class MachineLocKind {
      InvalidKind = 0,
      RegisterKind,
      SpillLocKind,
      ImmediateKind,
      WasmLocKind
    };

    enum class EntryValueLocKind {
      NonEntryValueKind = 0,
      EntryValueKind,
      EntryValueBackupKind,
      EntryValueCopyBackupKind
    } EVKind = EntryValueLocKind::NonEntryValueKind;

    /// The value location. Stored separately to avoid repeatedly
    /// extracting it from MI.
    union MachineLocValue {
      uint64_t RegNo;
      SpillLoc SpillLocation;
      uint64_t Hash;
      int64_t Immediate;
      const ConstantFP *FPImm;
      const ConstantInt *CImm;
      WasmLoc WasmLocation;
      MachineLocValue() : Hash(0) {}
    };

    /// A single machine location; its Kind is either a register, spill
    /// location, or immediate value.
    /// If the VarLoc is not a NonEntryValueKind, then it will use only a
    /// single MachineLoc of RegisterKind.
    struct MachineLoc {
      MachineLocKind Kind;
      MachineLocValue Value;
      bool operator==(const MachineLoc &Other) const {
        if (Kind != Other.Kind)
          return false;
        switch (Kind) {
        case MachineLocKind::SpillLocKind:
          return Value.SpillLocation == Other.Value.SpillLocation;
        case MachineLocKind::WasmLocKind:
          return Value.WasmLocation == Other.Value.WasmLocation;
        case MachineLocKind::RegisterKind:
        case MachineLocKind::ImmediateKind:
          return Value.Hash == Other.Value.Hash;
        default:
          llvm_unreachable("Invalid kind");
        }
      }
      bool operator<(const MachineLoc &Other) const {
        switch (Kind) {
        case MachineLocKind::SpillLocKind:
          return std::make_tuple(
                     Kind, Value.SpillLocation.SpillBase,
                     Value.SpillLocation.SpillOffset.getFixed(),
                     Value.SpillLocation.SpillOffset.getScalable()) <
                 std::make_tuple(
                     Other.Kind, Other.Value.SpillLocation.SpillBase,
                     Other.Value.SpillLocation.SpillOffset.getFixed(),
                     Other.Value.SpillLocation.SpillOffset.getScalable());
        case MachineLocKind::WasmLocKind:
          return std::make_tuple(Kind, Value.WasmLocation.Index,
                                 Value.WasmLocation.Offset) <
                 std::make_tuple(Other.Kind, Other.Value.WasmLocation.Index,
                                 Other.Value.WasmLocation.Offset);
        case MachineLocKind::RegisterKind:
        case MachineLocKind::ImmediateKind:
          return std::tie(Kind, Value.Hash) <
                 std::tie(Other.Kind, Other.Value.Hash);
        default:
          llvm_unreachable("Invalid kind");
        }
      }
    };

    /// The set of machine locations used to determine the variable's value, in
    /// conjunction with Expr. Initially populated with MI's debug operands,
    /// but may be transformed independently afterwards.
    SmallVector<MachineLoc, 8> Locs;
    /// Used to map the index of each location in Locs back to the index of its
    /// original debug operand in MI. Used when multiple location operands are
    /// coalesced and the original MI's operands need to be accessed while
    /// emitting a debug value.
    SmallVector<unsigned, 8> OrigLocMap;

    VarLoc(const MachineInstr &MI)
        : Var(MI.getDebugVariable(), MI.getDebugExpression(),
              MI.getDebugLoc()->getInlinedAt()),
          Expr(MI.getDebugExpression()), MI(MI) {
      assert(MI.isDebugValue() && "not a DBG_VALUE");
      assert((MI.isDebugValueList() || MI.getNumOperands() == 4) &&
             "malformed DBG_VALUE");
      for (const MachineOperand &Op : MI.debug_operands()) {
        MachineLoc ML = GetLocForOp(Op);
        auto It = find(Locs, ML);
        if (It == Locs.end()) {
          Locs.push_back(ML);
          OrigLocMap.push_back(MI.getDebugOperandIndex(&Op));
        } else {
          // ML duplicates an element in Locs; replace references to Op
          // with references to the duplicating element.
          unsigned OpIdx = Locs.size();
          unsigned DuplicatingIdx = std::distance(Locs.begin(), It);
          Expr = DIExpression::replaceArg(Expr, OpIdx, DuplicatingIdx);
        }
      }

      // We create the debug entry values from the factory functions rather
      // than from this ctor.
      assert(EVKind != EntryValueLocKind::EntryValueKind &&
             !isEntryBackupLoc());
    }

    static MachineLoc GetLocForOp(const MachineOperand &Op) {
      MachineLocKind Kind;
      MachineLocValue Loc;
      if (Op.isReg()) {
        Kind = MachineLocKind::RegisterKind;
        Loc.RegNo = Op.getReg();
      } else if (Op.isImm()) {
        Kind = MachineLocKind::ImmediateKind;
        Loc.Immediate = Op.getImm();
      } else if (Op.isFPImm()) {
        Kind = MachineLocKind::ImmediateKind;
        Loc.FPImm = Op.getFPImm();
      } else if (Op.isCImm()) {
        Kind = MachineLocKind::ImmediateKind;
        Loc.CImm = Op.getCImm();
      } else if (Op.isTargetIndex()) {
        Kind = MachineLocKind::WasmLocKind;
        Loc.WasmLocation = {Op.getIndex(), Op.getOffset()};
      } else
        llvm_unreachable("Invalid Op kind for MachineLoc.");
      return {Kind, Loc};
    }

    /// Take the variable and machine-location in DBG_VALUE MI, and build an
    /// entry location using the given expression.
    static VarLoc CreateEntryLoc(const MachineInstr &MI,
                                 const DIExpression *EntryExpr, Register Reg) {
      VarLoc VL(MI);
      assert(VL.Locs.size() == 1 &&
             VL.Locs[0].Kind == MachineLocKind::RegisterKind);
      VL.EVKind = EntryValueLocKind::EntryValueKind;
      VL.Expr = EntryExpr;
      VL.Locs[0].Value.RegNo = Reg;
      return VL;
    }

    /// Take the variable and machine-location from the DBG_VALUE (from the
    /// function entry), and build an entry value backup location. The backup
    /// location will turn into the normal location if the backup is valid at
    /// the time of the primary location clobbering.
    static VarLoc CreateEntryBackupLoc(const MachineInstr &MI,
                                       const DIExpression *EntryExpr) {
      VarLoc VL(MI);
      assert(VL.Locs.size() == 1 &&
             VL.Locs[0].Kind == MachineLocKind::RegisterKind);
      VL.EVKind = EntryValueLocKind::EntryValueBackupKind;
      VL.Expr = EntryExpr;
      return VL;
    }

    /// Take the variable and machine-location from the DBG_VALUE (from the
    /// function entry), and build a copy of an entry value backup location by
    /// setting the register location to NewReg.
    static VarLoc CreateEntryCopyBackupLoc(const MachineInstr &MI,
                                           const DIExpression *EntryExpr,
                                           Register NewReg) {
      VarLoc VL(MI);
      assert(VL.Locs.size() == 1 &&
             VL.Locs[0].Kind == MachineLocKind::RegisterKind);
      VL.EVKind = EntryValueLocKind::EntryValueCopyBackupKind;
      VL.Expr = EntryExpr;
      VL.Locs[0].Value.RegNo = NewReg;
      return VL;
    }

    /// Copy the register location in DBG_VALUE MI, updating the register to
    /// be NewReg.
    static VarLoc CreateCopyLoc(const VarLoc &OldVL, const MachineLoc &OldML,
                                Register NewReg) {
      VarLoc VL = OldVL;
      for (MachineLoc &ML : VL.Locs)
        if (ML == OldML) {
          ML.Kind = MachineLocKind::RegisterKind;
          ML.Value.RegNo = NewReg;
          return VL;
        }
      llvm_unreachable("Should have found OldML in new VarLoc.");
    }

    /// Take the variable described by DBG_VALUE* MI, and create a VarLoc
    /// locating it in the specified spill location.
    static VarLoc CreateSpillLoc(const VarLoc &OldVL, const MachineLoc &OldML,
                                 unsigned SpillBase, StackOffset SpillOffset) {
      VarLoc VL = OldVL;
      for (MachineLoc &ML : VL.Locs)
        if (ML == OldML) {
          ML.Kind = MachineLocKind::SpillLocKind;
          ML.Value.SpillLocation = {SpillBase, SpillOffset};
          return VL;
        }
      llvm_unreachable("Should have found OldML in new VarLoc.");
    }

    /// Create a DBG_VALUE representing this VarLoc in the given function.
    /// Copies variable-specific information such as DILocalVariable and
    /// inlining information from the original DBG_VALUE instruction, which may
    /// have been several transfers ago.
    MachineInstr *BuildDbgValue(MachineFunction &MF) const {
      assert(!isEntryBackupLoc() &&
             "Tried to produce DBG_VALUE for backup VarLoc");
      const DebugLoc &DbgLoc = MI.getDebugLoc();
      bool Indirect = MI.isIndirectDebugValue();
      const auto &IID = MI.getDesc();
      const DILocalVariable *Var = MI.getDebugVariable();
      NumInserted++;

      const DIExpression *DIExpr = Expr;
      SmallVector<MachineOperand, 8> MOs;
      for (unsigned I = 0, E = Locs.size(); I < E; ++I) {
        MachineLocKind LocKind = Locs[I].Kind;
        MachineLocValue Loc = Locs[I].Value;
        const MachineOperand &Orig = MI.getDebugOperand(OrigLocMap[I]);
        switch (LocKind) {
        case MachineLocKind::RegisterKind:
          // An entry value is a register location -- but with an updated
          // expression. The register location of such DBG_VALUE is always the
          // one from the entry DBG_VALUE, it does not matter if the entry value
          // was copied in to another register due to some optimizations.
          // Non-entry value register locations are like the source
          // DBG_VALUE, but with the register number from this VarLoc.
          MOs.push_back(MachineOperand::CreateReg(
              EVKind == EntryValueLocKind::EntryValueKind ? Orig.getReg()
                                                          : Register(Loc.RegNo),
              false));
          break;
        case MachineLocKind::SpillLocKind: {
          // Spills are indirect DBG_VALUEs, with a base register and offset.
          // Use the original DBG_VALUEs expression to build the spilt location
          // on top of. FIXME: spill locations created before this pass runs
          // are not recognized, and not handled here.
          unsigned Base = Loc.SpillLocation.SpillBase;
          auto *TRI = MF.getSubtarget().getRegisterInfo();
          if (MI.isNonListDebugValue()) {
            auto Deref = Indirect ? DIExpression::DerefAfter : 0;
            DIExpr = TRI->prependOffsetExpression(
                DIExpr, DIExpression::ApplyOffset | Deref,
                Loc.SpillLocation.SpillOffset);
            Indirect = true;
          } else {
            SmallVector<uint64_t, 4> Ops;
            TRI->getOffsetOpcodes(Loc.SpillLocation.SpillOffset, Ops);
            Ops.push_back(dwarf::DW_OP_deref);
            DIExpr = DIExpression::appendOpsToArg(DIExpr, Ops, I);
          }
          MOs.push_back(MachineOperand::CreateReg(Base, false));
          break;
        }
        case MachineLocKind::ImmediateKind: {
          MOs.push_back(Orig);
          break;
        }
        case MachineLocKind::WasmLocKind: {
          MOs.push_back(Orig);
          break;
        }
        case MachineLocKind::InvalidKind:
          llvm_unreachable("Tried to produce DBG_VALUE for invalid VarLoc");
        }
      }
      return BuildMI(MF, DbgLoc, IID, Indirect, MOs, Var, DIExpr);
    }

    /// Is the Loc field a constant or constant object?
    bool isConstant(MachineLocKind Kind) const {
      return Kind == MachineLocKind::ImmediateKind;
    }

    /// Check if the Loc field is an entry backup location.
    bool isEntryBackupLoc() const {
      return EVKind == EntryValueLocKind::EntryValueBackupKind ||
             EVKind == EntryValueLocKind::EntryValueCopyBackupKind;
    }

    /// If this variable is described by register \p Reg holding the entry
    /// value, return true.
    bool isEntryValueBackupReg(Register Reg) const {
      return EVKind == EntryValueLocKind::EntryValueBackupKind && usesReg(Reg);
    }

    /// If this variable is described by register \p Reg holding a copy of the
    /// entry value, return true.
    bool isEntryValueCopyBackupReg(Register Reg) const {
      return EVKind == EntryValueLocKind::EntryValueCopyBackupKind &&
             usesReg(Reg);
    }

    /// If this variable is described in whole or part by \p Reg, return true.
    bool usesReg(Register Reg) const {
      MachineLoc RegML;
      RegML.Kind = MachineLocKind::RegisterKind;
      RegML.Value.RegNo = Reg;
      return is_contained(Locs, RegML);
    }

    /// If this variable is described in whole or part by \p Reg, return true.
    unsigned getRegIdx(Register Reg) const {
      for (unsigned Idx = 0; Idx < Locs.size(); ++Idx)
        if (Locs[Idx].Kind == MachineLocKind::RegisterKind &&
            Register{static_cast<unsigned>(Locs[Idx].Value.RegNo)} == Reg)
          return Idx;
      llvm_unreachable("Could not find given Reg in Locs");
    }

    /// If this variable is described in whole or part by 1 or more registers,
    /// add each of them to \p Regs and return true.
    bool getDescribingRegs(SmallVectorImpl<uint32_t> &Regs) const {
      bool AnyRegs = false;
      for (const auto &Loc : Locs)
        if (Loc.Kind == MachineLocKind::RegisterKind) {
          Regs.push_back(Loc.Value.RegNo);
          AnyRegs = true;
        }
      return AnyRegs;
    }

    bool containsSpillLocs() const {
      return any_of(Locs, [](VarLoc::MachineLoc ML) {
        return ML.Kind == VarLoc::MachineLocKind::SpillLocKind;
      });
    }

    /// If this variable is described in whole or part by \p SpillLocation,
    /// return true.
    bool usesSpillLoc(SpillLoc SpillLocation) const {
      MachineLoc SpillML;
      SpillML.Kind = MachineLocKind::SpillLocKind;
      SpillML.Value.SpillLocation = SpillLocation;
      return is_contained(Locs, SpillML);
    }

    /// If this variable is described in whole or part by \p SpillLocation,
    /// return the index .
    unsigned getSpillLocIdx(SpillLoc SpillLocation) const {
      for (unsigned Idx = 0; Idx < Locs.size(); ++Idx)
        if (Locs[Idx].Kind == MachineLocKind::SpillLocKind &&
            Locs[Idx].Value.SpillLocation == SpillLocation)
          return Idx;
      llvm_unreachable("Could not find given SpillLoc in Locs");
    }

    bool containsWasmLocs() const {
      return any_of(Locs, [](VarLoc::MachineLoc ML) {
        return ML.Kind == VarLoc::MachineLocKind::WasmLocKind;
      });
    }

    /// If this variable is described in whole or part by \p WasmLocation,
    /// return true.
    bool usesWasmLoc(WasmLoc WasmLocation) const {
      MachineLoc WasmML;
      WasmML.Kind = MachineLocKind::WasmLocKind;
      WasmML.Value.WasmLocation = WasmLocation;
      return is_contained(Locs, WasmML);
    }

    /// Determine whether the lexical scope of this value's debug location
    /// dominates MBB.
    bool dominates(LexicalScopes &LS, MachineBasicBlock &MBB) const {
      return LS.dominates(MI.getDebugLoc().get(), &MBB);
    }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    // TRI and TII can be null.
    void dump(const TargetRegisterInfo *TRI, const TargetInstrInfo *TII,
              raw_ostream &Out = dbgs()) const {
      Out << "VarLoc(";
      for (const MachineLoc &MLoc : Locs) {
        if (Locs.begin() != &MLoc)
          Out << ", ";
        switch (MLoc.Kind) {
        case MachineLocKind::RegisterKind:
          Out << printReg(MLoc.Value.RegNo, TRI);
          break;
        case MachineLocKind::SpillLocKind:
          Out << printReg(MLoc.Value.SpillLocation.SpillBase, TRI);
          Out << "[" << MLoc.Value.SpillLocation.SpillOffset.getFixed() << " + "
              << MLoc.Value.SpillLocation.SpillOffset.getScalable()
              << "x vscale"
              << "]";
          break;
        case MachineLocKind::ImmediateKind:
          Out << MLoc.Value.Immediate;
          break;
        case MachineLocKind::WasmLocKind: {
          if (TII) {
            auto Indices = TII->getSerializableTargetIndices();
            auto Found =
                find_if(Indices, [&](const std::pair<int, const char *> &I) {
                  return I.first == MLoc.Value.WasmLocation.Index;
                });
            assert(Found != Indices.end());
            Out << Found->second;
            if (MLoc.Value.WasmLocation.Offset > 0)
              Out << " + " << MLoc.Value.WasmLocation.Offset;
          } else {
            Out << "WasmLoc";
          }
          break;
        }
        case MachineLocKind::InvalidKind:
          llvm_unreachable("Invalid VarLoc in dump method");
        }
      }

      Out << ", \"" << Var.getVariable()->getName() << "\", " << *Expr << ", ";
      if (Var.getInlinedAt())
        Out << "!" << Var.getInlinedAt()->getMetadataID() << ")\n";
      else
        Out << "(null))";

      if (isEntryBackupLoc())
        Out << " (backup loc)\n";
      else
        Out << "\n";
    }
#endif

    bool operator==(const VarLoc &Other) const {
      return std::tie(EVKind, Var, Expr, Locs) ==
             std::tie(Other.EVKind, Other.Var, Other.Expr, Other.Locs);
    }

    /// This operator guarantees that VarLocs are sorted by Variable first.
    bool operator<(const VarLoc &Other) const {
      return std::tie(Var, EVKind, Locs, Expr) <
             std::tie(Other.Var, Other.EVKind, Other.Locs, Other.Expr);
    }
  };

#ifndef NDEBUG
  using VarVec = SmallVector<VarLoc, 32>;
#endif

  /// VarLocMap is used for two things:
  /// 1) Assigning LocIndices to a VarLoc. The LocIndices can be used to
  ///    virtually insert a VarLoc into a VarLocSet.
  /// 2) Given a LocIndex, look up the unique associated VarLoc.
  class VarLocMap {
    /// Map a VarLoc to an index within the vector reserved for its location
    /// within Loc2Vars.
    std::map<VarLoc, LocIndices> Var2Indices;

    /// Map a location to a vector which holds VarLocs which live in that
    /// location.
    SmallDenseMap<LocIndex::u32_location_t, std::vector<VarLoc>> Loc2Vars;

  public:
    /// Retrieve LocIndices for \p VL.
    LocIndices insert(const VarLoc &VL) {
      LocIndices &Indices = Var2Indices[VL];
      // If Indices is not empty, VL is already in the map.
      if (!Indices.empty())
        return Indices;
      SmallVector<LocIndex::u32_location_t, 4> Locations;
      // LocIndices are determined by EVKind and MLs; each Register has a
      // unique location, while all SpillLocs use a single bucket, and any EV
      // VarLocs use only the Backup bucket or none at all (except the
      // compulsory entry at the universal location index). LocIndices will
      // always have an index at the universal location index as the last index.
      if (VL.EVKind == VarLoc::EntryValueLocKind::NonEntryValueKind) {
        VL.getDescribingRegs(Locations);
        assert(all_of(Locations,
                      [](auto RegNo) {
                        return RegNo < LocIndex::kFirstInvalidRegLocation;
                      }) &&
               "Physreg out of range?");
        if (VL.containsSpillLocs())
          Locations.push_back(LocIndex::kSpillLocation);
        if (VL.containsWasmLocs())
          Locations.push_back(LocIndex::kWasmLocation);
      } else if (VL.EVKind != VarLoc::EntryValueLocKind::EntryValueKind) {
        LocIndex::u32_location_t Loc = LocIndex::kEntryValueBackupLocation;
        Locations.push_back(Loc);
      }
      Locations.push_back(LocIndex::kUniversalLocation);
      for (LocIndex::u32_location_t Location : Locations) {
        auto &Vars = Loc2Vars[Location];
        Indices.push_back(
            {Location, static_cast<LocIndex::u32_index_t>(Vars.size())});
        Vars.push_back(VL);
      }
      return Indices;
    }

    LocIndices getAllIndices(const VarLoc &VL) const {
      auto IndIt = Var2Indices.find(VL);
      assert(IndIt != Var2Indices.end() && "VarLoc not tracked");
      return IndIt->second;
    }

    /// Retrieve the unique VarLoc associated with \p ID.
    const VarLoc &operator[](LocIndex ID) const {
      auto LocIt = Loc2Vars.find(ID.Location);
      assert(LocIt != Loc2Vars.end() && "Location not tracked");
      return LocIt->second[ID.Index];
    }
  };

  using VarLocInMBB =
      SmallDenseMap<const MachineBasicBlock *, std::unique_ptr<VarLocSet>>;
  struct TransferDebugPair {
    MachineInstr *TransferInst; ///< Instruction where this transfer occurs.
    LocIndex LocationID;        ///< Location number for the transfer dest.
  };
  using TransferMap = SmallVector<TransferDebugPair, 4>;
  // Types for recording Entry Var Locations emitted by a single MachineInstr,
  // as well as recording MachineInstr which last defined a register.
  using InstToEntryLocMap = std::multimap<const MachineInstr *, LocIndex>;
  using RegDefToInstMap = DenseMap<Register, MachineInstr *>;

  // Types for recording sets of variable fragments that overlap. For a given
  // local variable, we record all other fragments of that variable that could
  // overlap it, to reduce search time.
  using FragmentOfVar =
      std::pair<const DILocalVariable *, DIExpression::FragmentInfo>;
  using OverlapMap =
      DenseMap<FragmentOfVar, SmallVector<DIExpression::FragmentInfo, 1>>;

  // Helper while building OverlapMap, a map of all fragments seen for a given
  // DILocalVariable.
  using VarToFragments =
      DenseMap<const DILocalVariable *, SmallSet<FragmentInfo, 4>>;

  /// Collects all VarLocs from \p CollectFrom. Each unique VarLoc is added
  /// to \p Collected once, in order of insertion into \p VarLocIDs.
  static void collectAllVarLocs(SmallVectorImpl<VarLoc> &Collected,
                                const VarLocSet &CollectFrom,
                                const VarLocMap &VarLocIDs);

  /// Get the registers which are used by VarLocs of kind RegisterKind tracked
  /// by \p CollectFrom.
  void getUsedRegs(const VarLocSet &CollectFrom,
                   SmallVectorImpl<Register> &UsedRegs) const;

  /// This holds the working set of currently open ranges. For fast
  /// access, this is done both as a set of VarLocIDs, and a map of
  /// DebugVariable to recent VarLocID. Note that a DBG_VALUE ends all
  /// previous open ranges for the same variable. In addition, we keep
  /// two different maps (Vars/EntryValuesBackupVars), so erase/insert
  /// methods act differently depending on whether a VarLoc is primary
  /// location or backup one. In the case the VarLoc is backup location
  /// we will erase/insert from the EntryValuesBackupVars map, otherwise
  /// we perform the operation on the Vars.
  class OpenRangesSet {
    VarLocSet::Allocator &Alloc;
    VarLocSet VarLocs;
    // Map the DebugVariable to recent primary location ID.
    SmallDenseMap<DebugVariable, LocIndices, 8> Vars;
    // Map the DebugVariable to recent backup location ID.
    SmallDenseMap<DebugVariable, LocIndices, 8> EntryValuesBackupVars;
    OverlapMap &OverlappingFragments;

  public:
    OpenRangesSet(VarLocSet::Allocator &Alloc, OverlapMap &_OLapMap)
        : Alloc(Alloc), VarLocs(Alloc), OverlappingFragments(_OLapMap) {}

    const VarLocSet &getVarLocs() const { return VarLocs; }

    // Fetches all VarLocs in \p VarLocIDs and inserts them into \p Collected.
    // This method is needed to get every VarLoc once, as each VarLoc may have
    // multiple indices in a VarLocMap (corresponding to each applicable
    // location), but all VarLocs appear exactly once at the universal location
    // index.
    void getUniqueVarLocs(SmallVectorImpl<VarLoc> &Collected,
                          const VarLocMap &VarLocIDs) const {
      collectAllVarLocs(Collected, VarLocs, VarLocIDs);
    }

    /// Terminate all open ranges for VL.Var by removing it from the set.
    void erase(const VarLoc &VL);

    /// Terminate all open ranges listed as indices in \c KillSet with
    /// \c Location by removing them from the set.
    void erase(const VarLocsInRange &KillSet, const VarLocMap &VarLocIDs,
               LocIndex::u32_location_t Location);

    /// Insert a new range into the set.
    void insert(LocIndices VarLocIDs, const VarLoc &VL);

    /// Insert a set of ranges.
    void insertFromLocSet(const VarLocSet &ToLoad, const VarLocMap &Map);

    std::optional<LocIndices> getEntryValueBackup(DebugVariable Var);

    /// Empty the set.
    void clear() {
      VarLocs.clear();
      Vars.clear();
      EntryValuesBackupVars.clear();
    }

    /// Return whether the set is empty or not.
    bool empty() const {
      assert(Vars.empty() == EntryValuesBackupVars.empty() &&
             Vars.empty() == VarLocs.empty() &&
             "open ranges are inconsistent");
      return VarLocs.empty();
    }

    /// Get an empty range of VarLoc IDs.
    auto getEmptyVarLocRange() const {
      return iterator_range<VarLocSet::const_iterator>(getVarLocs().end(),
                                                       getVarLocs().end());
    }

    /// Get all set IDs for VarLocs with MLs of kind RegisterKind in \p Reg.
    auto getRegisterVarLocs(Register Reg) const {
      return LocIndex::indexRangeForLocation(getVarLocs(), Reg);
    }

    /// Get all set IDs for VarLocs with MLs of kind SpillLocKind.
    auto getSpillVarLocs() const {
      return LocIndex::indexRangeForLocation(getVarLocs(),
                                             LocIndex::kSpillLocation);
    }

    /// Get all set IDs for VarLocs of EVKind EntryValueBackupKind or
    /// EntryValueCopyBackupKind.
    auto getEntryValueBackupVarLocs() const {
      return LocIndex::indexRangeForLocation(
          getVarLocs(), LocIndex::kEntryValueBackupLocation);
    }

    /// Get all set IDs for VarLocs with MLs of kind WasmLocKind.
    auto getWasmVarLocs() const {
      return LocIndex::indexRangeForLocation(getVarLocs(),
                                             LocIndex::kWasmLocation);
    }
  };

  /// Collect all VarLoc IDs from \p CollectFrom for VarLocs with MLs of kind
  /// RegisterKind which are located in any reg in \p Regs. The IDs for each
  /// VarLoc correspond to entries in the universal location bucket, which every
  /// VarLoc has exactly 1 entry for. Insert collected IDs into \p Collected.
  static void collectIDsForRegs(VarLocsInRange &Collected,
                                const DefinedRegsSet &Regs,
                                const VarLocSet &CollectFrom,
                                const VarLocMap &VarLocIDs);

  VarLocSet &getVarLocsInMBB(const MachineBasicBlock *MBB, VarLocInMBB &Locs) {
    std::unique_ptr<VarLocSet> &VLS = Locs[MBB];
    if (!VLS)
      VLS = std::make_unique<VarLocSet>(Alloc);
    return *VLS;
  }

  const VarLocSet &getVarLocsInMBB(const MachineBasicBlock *MBB,
                                   const VarLocInMBB &Locs) const {
    auto It = Locs.find(MBB);
    assert(It != Locs.end() && "MBB not in map");
    return *It->second;
  }

  /// Tests whether this instruction is a spill to a stack location.
  bool isSpillInstruction(const MachineInstr &MI, MachineFunction *MF);

  /// Decide if @MI is a spill instruction and return true if it is. We use 2
  /// criteria to make this decision:
  /// - Is this instruction a store to a spill slot?
  /// - Is there a register operand that is both used and killed?
  /// TODO: Store optimization can fold spills into other stores (including
  /// other spills). We do not handle this yet (more than one memory operand).
  bool isLocationSpill(const MachineInstr &MI, MachineFunction *MF,
                       Register &Reg);

  /// Returns true if the given machine instruction is a debug value which we
  /// can emit entry values for.
  ///
  /// Currently, we generate debug entry values only for parameters that are
  /// unmodified throughout the function and located in a register.
  bool isEntryValueCandidate(const MachineInstr &MI,
                             const DefinedRegsSet &Regs) const;

  /// If a given instruction is identified as a spill, return the spill location
  /// and set \p Reg to the spilled register.
  std::optional<VarLoc::SpillLoc> isRestoreInstruction(const MachineInstr &MI,
                                                       MachineFunction *MF,
                                                       Register &Reg);
  /// Given a spill instruction, extract the register and offset used to
  /// address the spill location in a target independent way.
  VarLoc::SpillLoc extractSpillBaseRegAndOffset(const MachineInstr &MI);
  void insertTransferDebugPair(MachineInstr &MI, OpenRangesSet &OpenRanges,
                               TransferMap &Transfers, VarLocMap &VarLocIDs,
                               LocIndex OldVarID, TransferKind Kind,
                               const VarLoc::MachineLoc &OldLoc,
                               Register NewReg = Register());

  void transferDebugValue(const MachineInstr &MI, OpenRangesSet &OpenRanges,
                          VarLocMap &VarLocIDs,
                          InstToEntryLocMap &EntryValTransfers,
                          RegDefToInstMap &RegSetInstrs);
  void transferSpillOrRestoreInst(MachineInstr &MI, OpenRangesSet &OpenRanges,
                                  VarLocMap &VarLocIDs, TransferMap &Transfers);
  void cleanupEntryValueTransfers(const MachineInstr *MI,
                                  OpenRangesSet &OpenRanges,
                                  VarLocMap &VarLocIDs, const VarLoc &EntryVL,
                                  InstToEntryLocMap &EntryValTransfers);
  void removeEntryValue(const MachineInstr &MI, OpenRangesSet &OpenRanges,
                        VarLocMap &VarLocIDs, const VarLoc &EntryVL,
                        InstToEntryLocMap &EntryValTransfers,
                        RegDefToInstMap &RegSetInstrs);
  void emitEntryValues(MachineInstr &MI, OpenRangesSet &OpenRanges,
                       VarLocMap &VarLocIDs,
                       InstToEntryLocMap &EntryValTransfers,
                       VarLocsInRange &KillSet);
  void recordEntryValue(const MachineInstr &MI,
                        const DefinedRegsSet &DefinedRegs,
                        OpenRangesSet &OpenRanges, VarLocMap &VarLocIDs);
  void transferRegisterCopy(MachineInstr &MI, OpenRangesSet &OpenRanges,
                            VarLocMap &VarLocIDs, TransferMap &Transfers);
  void transferRegisterDef(MachineInstr &MI, OpenRangesSet &OpenRanges,
                           VarLocMap &VarLocIDs,
                           InstToEntryLocMap &EntryValTransfers,
                           RegDefToInstMap &RegSetInstrs);
  void transferWasmDef(MachineInstr &MI, OpenRangesSet &OpenRanges,
                       VarLocMap &VarLocIDs);
  bool transferTerminator(MachineBasicBlock *MBB, OpenRangesSet &OpenRanges,
                          VarLocInMBB &OutLocs, const VarLocMap &VarLocIDs);

  void process(MachineInstr &MI, OpenRangesSet &OpenRanges,
               VarLocMap &VarLocIDs, TransferMap &Transfers,
               InstToEntryLocMap &EntryValTransfers,
               RegDefToInstMap &RegSetInstrs);

  void accumulateFragmentMap(MachineInstr &MI, VarToFragments &SeenFragments,
                             OverlapMap &OLapMap);

  bool join(MachineBasicBlock &MBB, VarLocInMBB &OutLocs, VarLocInMBB &InLocs,
            const VarLocMap &VarLocIDs,
            SmallPtrSet<const MachineBasicBlock *, 16> &Visited,
            SmallPtrSetImpl<const MachineBasicBlock *> &ArtificialBlocks);

  /// Create DBG_VALUE insts for inlocs that have been propagated but
  /// had their instruction creation deferred.
  void flushPendingLocs(VarLocInMBB &PendingInLocs, VarLocMap &VarLocIDs);

  bool ExtendRanges(MachineFunction &MF, MachineDominatorTree *DomTree,
                    TargetPassConfig *TPC, unsigned InputBBLimit,
                    unsigned InputDbgValLimit) override;

public:
  /// Default construct and initialize the pass.
  VarLocBasedLDV();

  ~VarLocBasedLDV();

  /// Print to ostream with a message.
  void printVarLocInMBB(const MachineFunction &MF, const VarLocInMBB &V,
                        const VarLocMap &VarLocIDs, const char *msg,
                        raw_ostream &Out) const;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//            Implementation
//===----------------------------------------------------------------------===//

VarLocBasedLDV::VarLocBasedLDV() = default;

VarLocBasedLDV::~VarLocBasedLDV() = default;

/// Erase a variable from the set of open ranges, and additionally erase any
/// fragments that may overlap it. If the VarLoc is a backup location, erase
/// the variable from the EntryValuesBackupVars set, indicating we should stop
/// tracking its backup entry location. Otherwise, if the VarLoc is primary
/// location, erase the variable from the Vars set.
void VarLocBasedLDV::OpenRangesSet::erase(const VarLoc &VL) {
  // Erasure helper.
  auto DoErase = [&VL, this](DebugVariable VarToErase) {
    auto *EraseFrom = VL.isEntryBackupLoc() ? &EntryValuesBackupVars : &Vars;
    auto It = EraseFrom->find(VarToErase);
    if (It != EraseFrom->end()) {
      LocIndices IDs = It->second;
      for (LocIndex ID : IDs)
        VarLocs.reset(ID.getAsRawInteger());
      EraseFrom->erase(It);
    }
  };

  DebugVariable Var = VL.Var;

  // Erase the variable/fragment that ends here.
  DoErase(Var);

  // Extract the fragment. Interpret an empty fragment as one that covers all
  // possible bits.
  FragmentInfo ThisFragment = Var.getFragmentOrDefault();

  // There may be fragments that overlap the designated fragment. Look them up
  // in the pre-computed overlap map, and erase them too.
  auto MapIt = OverlappingFragments.find({Var.getVariable(), ThisFragment});
  if (MapIt != OverlappingFragments.end()) {
    for (auto Fragment : MapIt->second) {
      VarLocBasedLDV::OptFragmentInfo FragmentHolder;
      if (!DebugVariable::isDefaultFragment(Fragment))
        FragmentHolder = VarLocBasedLDV::OptFragmentInfo(Fragment);
      DoErase({Var.getVariable(), FragmentHolder, Var.getInlinedAt()});
    }
  }
}

void VarLocBasedLDV::OpenRangesSet::erase(const VarLocsInRange &KillSet,
                                          const VarLocMap &VarLocIDs,
                                          LocIndex::u32_location_t Location) {
  VarLocSet RemoveSet(Alloc);
  for (LocIndex::u32_index_t ID : KillSet) {
    const VarLoc &VL = VarLocIDs[LocIndex(Location, ID)];
    auto *EraseFrom = VL.isEntryBackupLoc() ? &EntryValuesBackupVars : &Vars;
    EraseFrom->erase(VL.Var);
    LocIndices VLI = VarLocIDs.getAllIndices(VL);
    for (LocIndex ID : VLI)
      RemoveSet.set(ID.getAsRawInteger());
  }
  VarLocs.intersectWithComplement(RemoveSet);
}

void VarLocBasedLDV::OpenRangesSet::insertFromLocSet(const VarLocSet &ToLoad,
                                                     const VarLocMap &Map) {
  VarLocsInRange UniqueVarLocIDs;
  DefinedRegsSet Regs;
  Regs.insert(LocIndex::kUniversalLocation);
  collectIDsForRegs(UniqueVarLocIDs, Regs, ToLoad, Map);
  for (uint64_t ID : UniqueVarLocIDs) {
    LocIndex Idx = LocIndex::fromRawInteger(ID);
    const VarLoc &VarL = Map[Idx];
    const LocIndices Indices = Map.getAllIndices(VarL);
    insert(Indices, VarL);
  }
}

void VarLocBasedLDV::OpenRangesSet::insert(LocIndices VarLocIDs,
                                           const VarLoc &VL) {
  auto *InsertInto = VL.isEntryBackupLoc() ? &EntryValuesBackupVars : &Vars;
  for (LocIndex ID : VarLocIDs)
    VarLocs.set(ID.getAsRawInteger());
  InsertInto->insert({VL.Var, VarLocIDs});
}

/// Return the Loc ID of an entry value backup location, if it exists for the
/// variable.
std::optional<LocIndices>
VarLocBasedLDV::OpenRangesSet::getEntryValueBackup(DebugVariable Var) {
  auto It = EntryValuesBackupVars.find(Var);
  if (It != EntryValuesBackupVars.end())
    return It->second;

  return std::nullopt;
}

void VarLocBasedLDV::collectIDsForRegs(VarLocsInRange &Collected,
                                       const DefinedRegsSet &Regs,
                                       const VarLocSet &CollectFrom,
                                       const VarLocMap &VarLocIDs) {
  assert(!Regs.empty() && "Nothing to collect");
  SmallVector<Register, 32> SortedRegs;
  append_range(SortedRegs, Regs);
  array_pod_sort(SortedRegs.begin(), SortedRegs.end());
  auto It = CollectFrom.find(LocIndex::rawIndexForReg(SortedRegs.front()));
  auto End = CollectFrom.end();
  for (Register Reg : SortedRegs) {
    // The half-open interval [FirstIndexForReg, FirstInvalidIndex) contains
    // all possible VarLoc IDs for VarLocs with MLs of kind RegisterKind which
    // live in Reg.
    uint64_t FirstIndexForReg = LocIndex::rawIndexForReg(Reg);
    uint64_t FirstInvalidIndex = LocIndex::rawIndexForReg(Reg + 1);
    It.advanceToLowerBound(FirstIndexForReg);

    // Iterate through that half-open interval and collect all the set IDs.
    for (; It != End && *It < FirstInvalidIndex; ++It) {
      LocIndex ItIdx = LocIndex::fromRawInteger(*It);
      const VarLoc &VL = VarLocIDs[ItIdx];
      LocIndices LI = VarLocIDs.getAllIndices(VL);
      // For now, the back index is always the universal location index.
      assert(LI.back().Location == LocIndex::kUniversalLocation &&
             "Unexpected order of LocIndices for VarLoc; was it inserted into "
             "the VarLocMap correctly?");
      Collected.insert(LI.back().Index);
    }

    if (It == End)
      return;
  }
}

void VarLocBasedLDV::getUsedRegs(const VarLocSet &CollectFrom,
                                 SmallVectorImpl<Register> &UsedRegs) const {
  // All register-based VarLocs are assigned indices greater than or equal to
  // FirstRegIndex.
  uint64_t FirstRegIndex =
      LocIndex::rawIndexForReg(LocIndex::kFirstRegLocation);
  uint64_t FirstInvalidIndex =
      LocIndex::rawIndexForReg(LocIndex::kFirstInvalidRegLocation);
  for (auto It = CollectFrom.find(FirstRegIndex),
            End = CollectFrom.find(FirstInvalidIndex);
       It != End;) {
    // We found a VarLoc ID for a VarLoc that lives in a register. Figure out
    // which register and add it to UsedRegs.
    uint32_t FoundReg = LocIndex::fromRawInteger(*It).Location;
    assert((UsedRegs.empty() || FoundReg != UsedRegs.back()) &&
           "Duplicate used reg");
    UsedRegs.push_back(FoundReg);

    // Skip to the next /set/ register. Note that this finds a lower bound, so
    // even if there aren't any VarLocs living in `FoundReg+1`, we're still
    // guaranteed to move on to the next register (or to end()).
    uint64_t NextRegIndex = LocIndex::rawIndexForReg(FoundReg + 1);
    It.advanceToLowerBound(NextRegIndex);
  }
}

//===----------------------------------------------------------------------===//
//            Debug Range Extension Implementation
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
void VarLocBasedLDV::printVarLocInMBB(const MachineFunction &MF,
                                       const VarLocInMBB &V,
                                       const VarLocMap &VarLocIDs,
                                       const char *msg,
                                       raw_ostream &Out) const {
  Out << '\n' << msg << '\n';
  for (const MachineBasicBlock &BB : MF) {
    if (!V.count(&BB))
      continue;
    const VarLocSet &L = getVarLocsInMBB(&BB, V);
    if (L.empty())
      continue;
    SmallVector<VarLoc, 32> VarLocs;
    collectAllVarLocs(VarLocs, L, VarLocIDs);
    Out << "MBB: " << BB.getNumber() << ":\n";
    for (const VarLoc &VL : VarLocs) {
      Out << " Var: " << VL.Var.getVariable()->getName();
      Out << " MI: ";
      VL.dump(TRI, TII, Out);
    }
  }
  Out << "\n";
}
#endif

VarLocBasedLDV::VarLoc::SpillLoc
VarLocBasedLDV::extractSpillBaseRegAndOffset(const MachineInstr &MI) {
  assert(MI.hasOneMemOperand() &&
         "Spill instruction does not have exactly one memory operand?");
  auto MMOI = MI.memoperands_begin();
  const PseudoSourceValue *PVal = (*MMOI)->getPseudoValue();
  assert(PVal->kind() == PseudoSourceValue::FixedStack &&
         "Inconsistent memory operand in spill instruction");
  int FI = cast<FixedStackPseudoSourceValue>(PVal)->getFrameIndex();
  const MachineBasicBlock *MBB = MI.getParent();
  Register Reg;
  StackOffset Offset = TFI->getFrameIndexReference(*MBB->getParent(), FI, Reg);
  return {Reg, Offset};
}

/// Do cleanup of \p EntryValTransfers created by \p TRInst, by removing the
/// Transfer, which uses the to-be-deleted \p EntryVL.
void VarLocBasedLDV::cleanupEntryValueTransfers(
    const MachineInstr *TRInst, OpenRangesSet &OpenRanges, VarLocMap &VarLocIDs,
    const VarLoc &EntryVL, InstToEntryLocMap &EntryValTransfers) {
  if (EntryValTransfers.empty() || TRInst == nullptr)
    return;

  auto TransRange = EntryValTransfers.equal_range(TRInst);
  for (auto &TDPair : llvm::make_range(TransRange.first, TransRange.second)) {
    const VarLoc &EmittedEV = VarLocIDs[TDPair.second];
    if (std::tie(EntryVL.Var, EntryVL.Locs[0].Value.RegNo, EntryVL.Expr) ==
        std::tie(EmittedEV.Var, EmittedEV.Locs[0].Value.RegNo,
                 EmittedEV.Expr)) {
      OpenRanges.erase(EmittedEV);
      EntryValTransfers.erase(TRInst);
      break;
    }
  }
}

/// Try to salvage the debug entry value if we encounter a new debug value
/// describing the same parameter, otherwise stop tracking the value. Return
/// true if we should stop tracking the entry value and do the cleanup of
/// emitted Entry Value Transfers, otherwise return false.
void VarLocBasedLDV::removeEntryValue(const MachineInstr &MI,
                                      OpenRangesSet &OpenRanges,
                                      VarLocMap &VarLocIDs,
                                      const VarLoc &EntryVL,
                                      InstToEntryLocMap &EntryValTransfers,
                                      RegDefToInstMap &RegSetInstrs) {
  // Skip the DBG_VALUE which is the debug entry value itself.
  if (&MI == &EntryVL.MI)
    return;

  // If the parameter's location is not register location, we can not track
  // the entry value any more. It doesn't have the TransferInst which defines
  // register, so no Entry Value Transfers have been emitted already.
  if (!MI.getDebugOperand(0).isReg())
    return;

  // Try to get non-debug instruction responsible for the DBG_VALUE.
  const MachineInstr *TransferInst = nullptr;
  Register Reg = MI.getDebugOperand(0).getReg();
  if (Reg.isValid() && RegSetInstrs.contains(Reg))
    TransferInst = RegSetInstrs.find(Reg)->second;

  // Case of the parameter's DBG_VALUE at the start of entry MBB.
  if (!TransferInst && !LastNonDbgMI && MI.getParent()->isEntryBlock())
    return;

  // If the debug expression from the DBG_VALUE is not empty, we can assume the
  // parameter's value has changed indicating that we should stop tracking its
  // entry value as well.
  if (MI.getDebugExpression()->getNumElements() == 0 && TransferInst) {
    // If the DBG_VALUE comes from a copy instruction that copies the entry
    // value, it means the parameter's value has not changed and we should be
    // able to use its entry value.
    // TODO: Try to keep tracking of an entry value if we encounter a propagated
    // DBG_VALUE describing the copy of the entry value. (Propagated entry value
    // does not indicate the parameter modification.)
    auto DestSrc = TII->isCopyLikeInstr(*TransferInst);
    if (DestSrc) {
      const MachineOperand *SrcRegOp, *DestRegOp;
      SrcRegOp = DestSrc->Source;
      DestRegOp = DestSrc->Destination;
      if (Reg == DestRegOp->getReg()) {
        for (uint64_t ID : OpenRanges.getEntryValueBackupVarLocs()) {
          const VarLoc &VL = VarLocIDs[LocIndex::fromRawInteger(ID)];
          if (VL.isEntryValueCopyBackupReg(Reg) &&
              // Entry Values should not be variadic.
              VL.MI.getDebugOperand(0).getReg() == SrcRegOp->getReg())
            return;
        }
      }
    }
  }

  LLVM_DEBUG(dbgs() << "Deleting a DBG entry value because of: ";
             MI.print(dbgs(), /*IsStandalone*/ false,
                      /*SkipOpers*/ false, /*SkipDebugLoc*/ false,
                      /*AddNewLine*/ true, TII));
  cleanupEntryValueTransfers(TransferInst, OpenRanges, VarLocIDs, EntryVL,
                             EntryValTransfers);
  OpenRanges.erase(EntryVL);
}

/// End all previous ranges related to @MI and start a new range from @MI
/// if it is a DBG_VALUE instr.
void VarLocBasedLDV::transferDebugValue(const MachineInstr &MI,
                                        OpenRangesSet &OpenRanges,
                                        VarLocMap &VarLocIDs,
                                        InstToEntryLocMap &EntryValTransfers,
                                        RegDefToInstMap &RegSetInstrs) {
  if (!MI.isDebugValue())
    return;
  const DILocalVariable *Var = MI.getDebugVariable();
  const DIExpression *Expr = MI.getDebugExpression();
  const DILocation *DebugLoc = MI.getDebugLoc();
  const DILocation *InlinedAt = DebugLoc->getInlinedAt();
  assert(Var->isValidLocationForIntrinsic(DebugLoc) &&
         "Expected inlined-at fields to agree");

  DebugVariable V(Var, Expr, InlinedAt);

  // Check if this DBG_VALUE indicates a parameter's value changing.
  // If that is the case, we should stop tracking its entry value.
  auto EntryValBackupID = OpenRanges.getEntryValueBackup(V);
  if (Var->isParameter() && EntryValBackupID) {
    const VarLoc &EntryVL = VarLocIDs[EntryValBackupID->back()];
    removeEntryValue(MI, OpenRanges, VarLocIDs, EntryVL, EntryValTransfers,
                     RegSetInstrs);
  }

  if (all_of(MI.debug_operands(), [](const MachineOperand &MO) {
        return (MO.isReg() && MO.getReg()) || MO.isImm() || MO.isFPImm() ||
               MO.isCImm() || MO.isTargetIndex();
      })) {
    // Use normal VarLoc constructor for registers and immediates.
    VarLoc VL(MI);
    // End all previous ranges of VL.Var.
    OpenRanges.erase(VL);

    LocIndices IDs = VarLocIDs.insert(VL);
    // Add the VarLoc to OpenRanges from this DBG_VALUE.
    OpenRanges.insert(IDs, VL);
  } else if (MI.memoperands().size() > 0) {
    llvm_unreachable("DBG_VALUE with mem operand encountered after regalloc?");
  } else {
    // This must be an undefined location. If it has an open range, erase it.
    assert(MI.isUndefDebugValue() &&
           "Unexpected non-undef DBG_VALUE encountered");
    VarLoc VL(MI);
    OpenRanges.erase(VL);
  }
}

// This should be removed later, doesn't fit the new design.
void VarLocBasedLDV::collectAllVarLocs(SmallVectorImpl<VarLoc> &Collected,
                                       const VarLocSet &CollectFrom,
                                       const VarLocMap &VarLocIDs) {
  // The half-open interval [FirstIndexForReg, FirstInvalidIndex) contains all
  // possible VarLoc IDs for VarLocs with MLs of kind RegisterKind which live
  // in Reg.
  uint64_t FirstIndex = LocIndex::rawIndexForReg(LocIndex::kUniversalLocation);
  uint64_t FirstInvalidIndex =
      LocIndex::rawIndexForReg(LocIndex::kUniversalLocation + 1);
  // Iterate through that half-open interval and collect all the set IDs.
  for (auto It = CollectFrom.find(FirstIndex), End = CollectFrom.end();
       It != End && *It < FirstInvalidIndex; ++It) {
    LocIndex RegIdx = LocIndex::fromRawInteger(*It);
    Collected.push_back(VarLocIDs[RegIdx]);
  }
}

/// Turn the entry value backup locations into primary locations.
void VarLocBasedLDV::emitEntryValues(MachineInstr &MI,
                                     OpenRangesSet &OpenRanges,
                                     VarLocMap &VarLocIDs,
                                     InstToEntryLocMap &EntryValTransfers,
                                     VarLocsInRange &KillSet) {
  // Do not insert entry value locations after a terminator.
  if (MI.isTerminator())
    return;

  for (uint32_t ID : KillSet) {
    // The KillSet IDs are indices for the universal location bucket.
    LocIndex Idx = LocIndex(LocIndex::kUniversalLocation, ID);
    const VarLoc &VL = VarLocIDs[Idx];
    if (!VL.Var.getVariable()->isParameter())
      continue;

    auto DebugVar = VL.Var;
    std::optional<LocIndices> EntryValBackupIDs =
        OpenRanges.getEntryValueBackup(DebugVar);

    // If the parameter has the entry value backup, it means we should
    // be able to use its entry value.
    if (!EntryValBackupIDs)
      continue;

    const VarLoc &EntryVL = VarLocIDs[EntryValBackupIDs->back()];
    VarLoc EntryLoc = VarLoc::CreateEntryLoc(EntryVL.MI, EntryVL.Expr,
                                             EntryVL.Locs[0].Value.RegNo);
    LocIndices EntryValueIDs = VarLocIDs.insert(EntryLoc);
    assert(EntryValueIDs.size() == 1 &&
           "EntryValue loc should not be variadic");
    EntryValTransfers.insert({&MI, EntryValueIDs.back()});
    OpenRanges.insert(EntryValueIDs, EntryLoc);
  }
}

/// Create new TransferDebugPair and insert it in \p Transfers. The VarLoc
/// with \p OldVarID should be deleted form \p OpenRanges and replaced with
/// new VarLoc. If \p NewReg is different than default zero value then the
/// new location will be register location created by the copy like instruction,
/// otherwise it is variable's location on the stack.
void VarLocBasedLDV::insertTransferDebugPair(
    MachineInstr &MI, OpenRangesSet &OpenRanges, TransferMap &Transfers,
    VarLocMap &VarLocIDs, LocIndex OldVarID, TransferKind Kind,
    const VarLoc::MachineLoc &OldLoc, Register NewReg) {
  const VarLoc &OldVarLoc = VarLocIDs[OldVarID];

  auto ProcessVarLoc = [&MI, &OpenRanges, &Transfers, &VarLocIDs](VarLoc &VL) {
    LocIndices LocIds = VarLocIDs.insert(VL);

    // Close this variable's previous location range.
    OpenRanges.erase(VL);

    // Record the new location as an open range, and a postponed transfer
    // inserting a DBG_VALUE for this location.
    OpenRanges.insert(LocIds, VL);
    assert(!MI.isTerminator() && "Cannot insert DBG_VALUE after terminator");
    TransferDebugPair MIP = {&MI, LocIds.back()};
    Transfers.push_back(MIP);
  };

  // End all previous ranges of VL.Var.
  OpenRanges.erase(VarLocIDs[OldVarID]);
  switch (Kind) {
  case TransferKind::TransferCopy: {
    assert(NewReg &&
           "No register supplied when handling a copy of a debug value");
    // Create a DBG_VALUE instruction to describe the Var in its new
    // register location.
    VarLoc VL = VarLoc::CreateCopyLoc(OldVarLoc, OldLoc, NewReg);
    ProcessVarLoc(VL);
    LLVM_DEBUG({
      dbgs() << "Creating VarLoc for register copy:";
      VL.dump(TRI, TII);
    });
    return;
  }
  case TransferKind::TransferSpill: {
    // Create a DBG_VALUE instruction to describe the Var in its spilled
    // location.
    VarLoc::SpillLoc SpillLocation = extractSpillBaseRegAndOffset(MI);
    VarLoc VL = VarLoc::CreateSpillLoc(
        OldVarLoc, OldLoc, SpillLocation.SpillBase, SpillLocation.SpillOffset);
    ProcessVarLoc(VL);
    LLVM_DEBUG({
      dbgs() << "Creating VarLoc for spill:";
      VL.dump(TRI, TII);
    });
    return;
  }
  case TransferKind::TransferRestore: {
    assert(NewReg &&
           "No register supplied when handling a restore of a debug value");
    // DebugInstr refers to the pre-spill location, therefore we can reuse
    // its expression.
    VarLoc VL = VarLoc::CreateCopyLoc(OldVarLoc, OldLoc, NewReg);
    ProcessVarLoc(VL);
    LLVM_DEBUG({
      dbgs() << "Creating VarLoc for restore:";
      VL.dump(TRI, TII);
    });
    return;
  }
  }
  llvm_unreachable("Invalid transfer kind");
}

/// A definition of a register may mark the end of a range.
void VarLocBasedLDV::transferRegisterDef(MachineInstr &MI,
                                         OpenRangesSet &OpenRanges,
                                         VarLocMap &VarLocIDs,
                                         InstToEntryLocMap &EntryValTransfers,
                                         RegDefToInstMap &RegSetInstrs) {

  // Meta Instructions do not affect the debug liveness of any register they
  // define.
  if (MI.isMetaInstruction())
    return;

  MachineFunction *MF = MI.getMF();
  const TargetLowering *TLI = MF->getSubtarget().getTargetLowering();
  Register SP = TLI->getStackPointerRegisterToSaveRestore();

  // Find the regs killed by MI, and find regmasks of preserved regs.
  DefinedRegsSet DeadRegs;
  SmallVector<const uint32_t *, 4> RegMasks;
  for (const MachineOperand &MO : MI.operands()) {
    // Determine whether the operand is a register def.
    if (MO.isReg() && MO.isDef() && MO.getReg() && MO.getReg().isPhysical() &&
        !(MI.isCall() && MO.getReg() == SP)) {
      // Remove ranges of all aliased registers.
      for (MCRegAliasIterator RAI(MO.getReg(), TRI, true); RAI.isValid(); ++RAI)
        // FIXME: Can we break out of this loop early if no insertion occurs?
        DeadRegs.insert(*RAI);
      RegSetInstrs.erase(MO.getReg());
      RegSetInstrs.insert({MO.getReg(), &MI});
    } else if (MO.isRegMask()) {
      RegMasks.push_back(MO.getRegMask());
    }
  }

  // Erase VarLocs which reside in one of the dead registers. For performance
  // reasons, it's critical to not iterate over the full set of open VarLocs.
  // Iterate over the set of dying/used regs instead.
  if (!RegMasks.empty()) {
    SmallVector<Register, 32> UsedRegs;
    getUsedRegs(OpenRanges.getVarLocs(), UsedRegs);
    for (Register Reg : UsedRegs) {
      // Remove ranges of all clobbered registers. Register masks don't usually
      // list SP as preserved. Assume that call instructions never clobber SP,
      // because some backends (e.g., AArch64) never list SP in the regmask.
      // While the debug info may be off for an instruction or two around
      // callee-cleanup calls, transferring the DEBUG_VALUE across the call is
      // still a better user experience.
      if (Reg == SP)
        continue;
      bool AnyRegMaskKillsReg =
          any_of(RegMasks, [Reg](const uint32_t *RegMask) {
            return MachineOperand::clobbersPhysReg(RegMask, Reg);
          });
      if (AnyRegMaskKillsReg)
        DeadRegs.insert(Reg);
      if (AnyRegMaskKillsReg) {
        RegSetInstrs.erase(Reg);
        RegSetInstrs.insert({Reg, &MI});
      }
    }
  }

  if (DeadRegs.empty())
    return;

  VarLocsInRange KillSet;
  collectIDsForRegs(KillSet, DeadRegs, OpenRanges.getVarLocs(), VarLocIDs);
  OpenRanges.erase(KillSet, VarLocIDs, LocIndex::kUniversalLocation);

  if (TPC) {
    auto &TM = TPC->getTM<TargetMachine>();
    if (TM.Options.ShouldEmitDebugEntryValues())
      emitEntryValues(MI, OpenRanges, VarLocIDs, EntryValTransfers, KillSet);
  }
}

void VarLocBasedLDV::transferWasmDef(MachineInstr &MI,
                                     OpenRangesSet &OpenRanges,
                                     VarLocMap &VarLocIDs) {
  // If this is not a Wasm local.set or local.tee, which sets local values,
  // return.
  int Index;
  int64_t Offset;
  if (!TII->isExplicitTargetIndexDef(MI, Index, Offset))
    return;

  // Find the target indices killed by MI, and delete those variable locations
  // from the open range.
  VarLocsInRange KillSet;
  VarLoc::WasmLoc Loc{Index, Offset};
  for (uint64_t ID : OpenRanges.getWasmVarLocs()) {
    LocIndex Idx = LocIndex::fromRawInteger(ID);
    const VarLoc &VL = VarLocIDs[Idx];
    assert(VL.containsWasmLocs() && "Broken VarLocSet?");
    if (VL.usesWasmLoc(Loc))
      KillSet.insert(ID);
  }
  OpenRanges.erase(KillSet, VarLocIDs, LocIndex::kWasmLocation);
}

bool VarLocBasedLDV::isSpillInstruction(const MachineInstr &MI,
                                         MachineFunction *MF) {
  // TODO: Handle multiple stores folded into one.
  if (!MI.hasOneMemOperand())
    return false;

  if (!MI.getSpillSize(TII) && !MI.getFoldedSpillSize(TII))
    return false; // This is not a spill instruction, since no valid size was
                  // returned from either function.

  return true;
}

bool VarLocBasedLDV::isLocationSpill(const MachineInstr &MI,
                                      MachineFunction *MF, Register &Reg) {
  if (!isSpillInstruction(MI, MF))
    return false;

  auto isKilledReg = [&](const MachineOperand MO, Register &Reg) {
    if (!MO.isReg() || !MO.isUse()) {
      Reg = 0;
      return false;
    }
    Reg = MO.getReg();
    return MO.isKill();
  };

  for (const MachineOperand &MO : MI.operands()) {
    // In a spill instruction generated by the InlineSpiller the spilled
    // register has its kill flag set.
    if (isKilledReg(MO, Reg))
      return true;
    if (Reg != 0) {
      // Check whether next instruction kills the spilled register.
      // FIXME: Current solution does not cover search for killed register in
      // bundles and instructions further down the chain.
      auto NextI = std::next(MI.getIterator());
      // Skip next instruction that points to basic block end iterator.
      if (MI.getParent()->end() == NextI)
        continue;
      Register RegNext;
      for (const MachineOperand &MONext : NextI->operands()) {
        // Return true if we came across the register from the
        // previous spill instruction that is killed in NextI.
        if (isKilledReg(MONext, RegNext) && RegNext == Reg)
          return true;
      }
    }
  }
  // Return false if we didn't find spilled register.
  return false;
}

std::optional<VarLocBasedLDV::VarLoc::SpillLoc>
VarLocBasedLDV::isRestoreInstruction(const MachineInstr &MI,
                                     MachineFunction *MF, Register &Reg) {
  if (!MI.hasOneMemOperand())
    return std::nullopt;

  // FIXME: Handle folded restore instructions with more than one memory
  // operand.
  if (MI.getRestoreSize(TII)) {
    Reg = MI.getOperand(0).getReg();
    return extractSpillBaseRegAndOffset(MI);
  }
  return std::nullopt;
}

/// A spilled register may indicate that we have to end the current range of
/// a variable and create a new one for the spill location.
/// A restored register may indicate the reverse situation.
/// We don't want to insert any instructions in process(), so we just create
/// the DBG_VALUE without inserting it and keep track of it in \p Transfers.
/// It will be inserted into the BB when we're done iterating over the
/// instructions.
void VarLocBasedLDV::transferSpillOrRestoreInst(MachineInstr &MI,
                                                 OpenRangesSet &OpenRanges,
                                                 VarLocMap &VarLocIDs,
                                                 TransferMap &Transfers) {
  MachineFunction *MF = MI.getMF();
  TransferKind TKind;
  Register Reg;
  std::optional<VarLoc::SpillLoc> Loc;

  LLVM_DEBUG(dbgs() << "Examining instruction: "; MI.dump(););

  // First, if there are any DBG_VALUEs pointing at a spill slot that is
  // written to, then close the variable location. The value in memory
  // will have changed.
  VarLocsInRange KillSet;
  if (isSpillInstruction(MI, MF)) {
    Loc = extractSpillBaseRegAndOffset(MI);
    for (uint64_t ID : OpenRanges.getSpillVarLocs()) {
      LocIndex Idx = LocIndex::fromRawInteger(ID);
      const VarLoc &VL = VarLocIDs[Idx];
      assert(VL.containsSpillLocs() && "Broken VarLocSet?");
      if (VL.usesSpillLoc(*Loc)) {
        // This location is overwritten by the current instruction -- terminate
        // the open range, and insert an explicit DBG_VALUE $noreg.
        //
        // Doing this at a later stage would require re-interpreting all
        // DBG_VALUes and DIExpressions to identify whether they point at
        // memory, and then analysing all memory writes to see if they
        // overwrite that memory, which is expensive.
        //
        // At this stage, we already know which DBG_VALUEs are for spills and
        // where they are located; it's best to fix handle overwrites now.
        KillSet.insert(ID);
        unsigned SpillLocIdx = VL.getSpillLocIdx(*Loc);
        VarLoc::MachineLoc OldLoc = VL.Locs[SpillLocIdx];
        VarLoc UndefVL = VarLoc::CreateCopyLoc(VL, OldLoc, 0);
        LocIndices UndefLocIDs = VarLocIDs.insert(UndefVL);
        Transfers.push_back({&MI, UndefLocIDs.back()});
      }
    }
    OpenRanges.erase(KillSet, VarLocIDs, LocIndex::kSpillLocation);
  }

  // Try to recognise spill and restore instructions that may create a new
  // variable location.
  if (isLocationSpill(MI, MF, Reg)) {
    TKind = TransferKind::TransferSpill;
    LLVM_DEBUG(dbgs() << "Recognized as spill: "; MI.dump(););
    LLVM_DEBUG(dbgs() << "Register: " << Reg << " " << printReg(Reg, TRI)
                      << "\n");
  } else {
    if (!(Loc = isRestoreInstruction(MI, MF, Reg)))
      return;
    TKind = TransferKind::TransferRestore;
    LLVM_DEBUG(dbgs() << "Recognized as restore: "; MI.dump(););
    LLVM_DEBUG(dbgs() << "Register: " << Reg << " " << printReg(Reg, TRI)
                      << "\n");
  }
  // Check if the register or spill location is the location of a debug value.
  auto TransferCandidates = OpenRanges.getEmptyVarLocRange();
  if (TKind == TransferKind::TransferSpill)
    TransferCandidates = OpenRanges.getRegisterVarLocs(Reg);
  else if (TKind == TransferKind::TransferRestore)
    TransferCandidates = OpenRanges.getSpillVarLocs();
  for (uint64_t ID : TransferCandidates) {
    LocIndex Idx = LocIndex::fromRawInteger(ID);
    const VarLoc &VL = VarLocIDs[Idx];
    unsigned LocIdx;
    if (TKind == TransferKind::TransferSpill) {
      assert(VL.usesReg(Reg) && "Broken VarLocSet?");
      LLVM_DEBUG(dbgs() << "Spilling Register " << printReg(Reg, TRI) << '('
                        << VL.Var.getVariable()->getName() << ")\n");
      LocIdx = VL.getRegIdx(Reg);
    } else {
      assert(TKind == TransferKind::TransferRestore && VL.containsSpillLocs() &&
             "Broken VarLocSet?");
      if (!VL.usesSpillLoc(*Loc))
        // The spill location is not the location of a debug value.
        continue;
      LLVM_DEBUG(dbgs() << "Restoring Register " << printReg(Reg, TRI) << '('
                        << VL.Var.getVariable()->getName() << ")\n");
      LocIdx = VL.getSpillLocIdx(*Loc);
    }
    VarLoc::MachineLoc MLoc = VL.Locs[LocIdx];
    insertTransferDebugPair(MI, OpenRanges, Transfers, VarLocIDs, Idx, TKind,
                            MLoc, Reg);
    // FIXME: A comment should explain why it's correct to return early here,
    // if that is in fact correct.
    return;
  }
}

/// If \p MI is a register copy instruction, that copies a previously tracked
/// value from one register to another register that is callee saved, we
/// create new DBG_VALUE instruction  described with copy destination register.
void VarLocBasedLDV::transferRegisterCopy(MachineInstr &MI,
                                           OpenRangesSet &OpenRanges,
                                           VarLocMap &VarLocIDs,
                                           TransferMap &Transfers) {
  auto DestSrc = TII->isCopyLikeInstr(MI);
  if (!DestSrc)
    return;

  const MachineOperand *DestRegOp = DestSrc->Destination;
  const MachineOperand *SrcRegOp = DestSrc->Source;

  if (!DestRegOp->isDef())
    return;

  auto isCalleeSavedReg = [&](Register Reg) {
    for (MCRegAliasIterator RAI(Reg, TRI, true); RAI.isValid(); ++RAI)
      if (CalleeSavedRegs.test(*RAI))
        return true;
    return false;
  };

  Register SrcReg = SrcRegOp->getReg();
  Register DestReg = DestRegOp->getReg();

  // We want to recognize instructions where destination register is callee
  // saved register. If register that could be clobbered by the call is
  // included, there would be a great chance that it is going to be clobbered
  // soon. It is more likely that previous register location, which is callee
  // saved, is going to stay unclobbered longer, even if it is killed.
  if (!isCalleeSavedReg(DestReg))
    return;

  // Remember an entry value movement. If we encounter a new debug value of
  // a parameter describing only a moving of the value around, rather then
  // modifying it, we are still able to use the entry value if needed.
  if (isRegOtherThanSPAndFP(*DestRegOp, MI, TRI)) {
    for (uint64_t ID : OpenRanges.getEntryValueBackupVarLocs()) {
      LocIndex Idx = LocIndex::fromRawInteger(ID);
      const VarLoc &VL = VarLocIDs[Idx];
      if (VL.isEntryValueBackupReg(SrcReg)) {
        LLVM_DEBUG(dbgs() << "Copy of the entry value: "; MI.dump(););
        VarLoc EntryValLocCopyBackup =
            VarLoc::CreateEntryCopyBackupLoc(VL.MI, VL.Expr, DestReg);
        // Stop tracking the original entry value.
        OpenRanges.erase(VL);

        // Start tracking the entry value copy.
        LocIndices EntryValCopyLocIDs = VarLocIDs.insert(EntryValLocCopyBackup);
        OpenRanges.insert(EntryValCopyLocIDs, EntryValLocCopyBackup);
        break;
      }
    }
  }

  if (!SrcRegOp->isKill())
    return;

  for (uint64_t ID : OpenRanges.getRegisterVarLocs(SrcReg)) {
    LocIndex Idx = LocIndex::fromRawInteger(ID);
    assert(VarLocIDs[Idx].usesReg(SrcReg) && "Broken VarLocSet?");
    VarLoc::MachineLocValue Loc;
    Loc.RegNo = SrcReg;
    VarLoc::MachineLoc MLoc{VarLoc::MachineLocKind::RegisterKind, Loc};
    insertTransferDebugPair(MI, OpenRanges, Transfers, VarLocIDs, Idx,
                            TransferKind::TransferCopy, MLoc, DestReg);
    // FIXME: A comment should explain why it's correct to return early here,
    // if that is in fact correct.
    return;
  }
}

/// Terminate all open ranges at the end of the current basic block.
bool VarLocBasedLDV::transferTerminator(MachineBasicBlock *CurMBB,
                                         OpenRangesSet &OpenRanges,
                                         VarLocInMBB &OutLocs,
                                         const VarLocMap &VarLocIDs) {
  bool Changed = false;
  LLVM_DEBUG({
    VarVec VarLocs;
    OpenRanges.getUniqueVarLocs(VarLocs, VarLocIDs);
    for (VarLoc &VL : VarLocs) {
      // Copy OpenRanges to OutLocs, if not already present.
      dbgs() << "Add to OutLocs in MBB #" << CurMBB->getNumber() << ":  ";
      VL.dump(TRI, TII);
    }
  });
  VarLocSet &VLS = getVarLocsInMBB(CurMBB, OutLocs);
  Changed = VLS != OpenRanges.getVarLocs();
  // New OutLocs set may be different due to spill, restore or register
  // copy instruction processing.
  if (Changed)
    VLS = OpenRanges.getVarLocs();
  OpenRanges.clear();
  return Changed;
}

/// Accumulate a mapping between each DILocalVariable fragment and other
/// fragments of that DILocalVariable which overlap. This reduces work during
/// the data-flow stage from "Find any overlapping fragments" to "Check if the
/// known-to-overlap fragments are present".
/// \param MI A previously unprocessed DEBUG_VALUE instruction to analyze for
///           fragment usage.
/// \param SeenFragments Map from DILocalVariable to all fragments of that
///           Variable which are known to exist.
/// \param OverlappingFragments The overlap map being constructed, from one
///           Var/Fragment pair to a vector of fragments known to overlap.
void VarLocBasedLDV::accumulateFragmentMap(MachineInstr &MI,
                                            VarToFragments &SeenFragments,
                                            OverlapMap &OverlappingFragments) {
  DebugVariable MIVar(MI.getDebugVariable(), MI.getDebugExpression(),
                      MI.getDebugLoc()->getInlinedAt());
  FragmentInfo ThisFragment = MIVar.getFragmentOrDefault();

  // If this is the first sighting of this variable, then we are guaranteed
  // there are currently no overlapping fragments either. Initialize the set
  // of seen fragments, record no overlaps for the current one, and return.
  auto SeenIt = SeenFragments.find(MIVar.getVariable());
  if (SeenIt == SeenFragments.end()) {
    SmallSet<FragmentInfo, 4> OneFragment;
    OneFragment.insert(ThisFragment);
    SeenFragments.insert({MIVar.getVariable(), OneFragment});

    OverlappingFragments.insert({{MIVar.getVariable(), ThisFragment}, {}});
    return;
  }

  // If this particular Variable/Fragment pair already exists in the overlap
  // map, it has already been accounted for.
  auto IsInOLapMap =
      OverlappingFragments.insert({{MIVar.getVariable(), ThisFragment}, {}});
  if (!IsInOLapMap.second)
    return;

  auto &ThisFragmentsOverlaps = IsInOLapMap.first->second;
  auto &AllSeenFragments = SeenIt->second;

  // Otherwise, examine all other seen fragments for this variable, with "this"
  // fragment being a previously unseen fragment. Record any pair of
  // overlapping fragments.
  for (const auto &ASeenFragment : AllSeenFragments) {
    // Does this previously seen fragment overlap?
    if (DIExpression::fragmentsOverlap(ThisFragment, ASeenFragment)) {
      // Yes: Mark the current fragment as being overlapped.
      ThisFragmentsOverlaps.push_back(ASeenFragment);
      // Mark the previously seen fragment as being overlapped by the current
      // one.
      auto ASeenFragmentsOverlaps =
          OverlappingFragments.find({MIVar.getVariable(), ASeenFragment});
      assert(ASeenFragmentsOverlaps != OverlappingFragments.end() &&
             "Previously seen var fragment has no vector of overlaps");
      ASeenFragmentsOverlaps->second.push_back(ThisFragment);
    }
  }

  AllSeenFragments.insert(ThisFragment);
}

/// This routine creates OpenRanges.
void VarLocBasedLDV::process(MachineInstr &MI, OpenRangesSet &OpenRanges,
                             VarLocMap &VarLocIDs, TransferMap &Transfers,
                             InstToEntryLocMap &EntryValTransfers,
                             RegDefToInstMap &RegSetInstrs) {
  if (!MI.isDebugInstr())
    LastNonDbgMI = &MI;
  transferDebugValue(MI, OpenRanges, VarLocIDs, EntryValTransfers,
                     RegSetInstrs);
  transferRegisterDef(MI, OpenRanges, VarLocIDs, EntryValTransfers,
                      RegSetInstrs);
  transferWasmDef(MI, OpenRanges, VarLocIDs);
  transferRegisterCopy(MI, OpenRanges, VarLocIDs, Transfers);
  transferSpillOrRestoreInst(MI, OpenRanges, VarLocIDs, Transfers);
}

/// This routine joins the analysis results of all incoming edges in @MBB by
/// inserting a new DBG_VALUE instruction at the start of the @MBB - if the same
/// source variable in all the predecessors of @MBB reside in the same location.
bool VarLocBasedLDV::join(
    MachineBasicBlock &MBB, VarLocInMBB &OutLocs, VarLocInMBB &InLocs,
    const VarLocMap &VarLocIDs,
    SmallPtrSet<const MachineBasicBlock *, 16> &Visited,
    SmallPtrSetImpl<const MachineBasicBlock *> &ArtificialBlocks) {
  LLVM_DEBUG(dbgs() << "join MBB: " << MBB.getNumber() << "\n");

  VarLocSet InLocsT(Alloc); // Temporary incoming locations.

  // For all predecessors of this MBB, find the set of VarLocs that
  // can be joined.
  int NumVisited = 0;
  for (auto *p : MBB.predecessors()) {
    // Ignore backedges if we have not visited the predecessor yet. As the
    // predecessor hasn't yet had locations propagated into it, most locations
    // will not yet be valid, so treat them as all being uninitialized and
    // potentially valid. If a location guessed to be correct here is
    // invalidated later, we will remove it when we revisit this block.
    if (!Visited.count(p)) {
      LLVM_DEBUG(dbgs() << "  ignoring unvisited pred MBB: " << p->getNumber()
                        << "\n");
      continue;
    }
    auto OL = OutLocs.find(p);
    // Join is null in case of empty OutLocs from any of the pred.
    if (OL == OutLocs.end())
      return false;

    // Just copy over the Out locs to incoming locs for the first visited
    // predecessor, and for all other predecessors join the Out locs.
    VarLocSet &OutLocVLS = *OL->second;
    if (!NumVisited)
      InLocsT = OutLocVLS;
    else
      InLocsT &= OutLocVLS;

    LLVM_DEBUG({
      if (!InLocsT.empty()) {
        VarVec VarLocs;
        collectAllVarLocs(VarLocs, InLocsT, VarLocIDs);
        for (const VarLoc &VL : VarLocs)
          dbgs() << "  gathered candidate incoming var: "
                 << VL.Var.getVariable()->getName() << "\n";
      }
    });

    NumVisited++;
  }

  // Filter out DBG_VALUES that are out of scope.
  VarLocSet KillSet(Alloc);
  bool IsArtificial = ArtificialBlocks.count(&MBB);
  if (!IsArtificial) {
    for (uint64_t ID : InLocsT) {
      LocIndex Idx = LocIndex::fromRawInteger(ID);
      if (!VarLocIDs[Idx].dominates(LS, MBB)) {
        KillSet.set(ID);
        LLVM_DEBUG({
          auto Name = VarLocIDs[Idx].Var.getVariable()->getName();
          dbgs() << "  killing " << Name << ", it doesn't dominate MBB\n";
        });
      }
    }
  }
  InLocsT.intersectWithComplement(KillSet);

  // As we are processing blocks in reverse post-order we
  // should have processed at least one predecessor, unless it
  // is the entry block which has no predecessor.
  assert((NumVisited || MBB.pred_empty()) &&
         "Should have processed at least one predecessor");

  VarLocSet &ILS = getVarLocsInMBB(&MBB, InLocs);
  bool Changed = false;
  if (ILS != InLocsT) {
    ILS = InLocsT;
    Changed = true;
  }

  return Changed;
}

void VarLocBasedLDV::flushPendingLocs(VarLocInMBB &PendingInLocs,
                                       VarLocMap &VarLocIDs) {
  // PendingInLocs records all locations propagated into blocks, which have
  // not had DBG_VALUE insts created. Go through and create those insts now.
  for (auto &Iter : PendingInLocs) {
    // Map is keyed on a constant pointer, unwrap it so we can insert insts.
    auto &MBB = const_cast<MachineBasicBlock &>(*Iter.first);
    VarLocSet &Pending = *Iter.second;

    SmallVector<VarLoc, 32> VarLocs;
    collectAllVarLocs(VarLocs, Pending, VarLocIDs);

    for (VarLoc DiffIt : VarLocs) {
      // The ID location is live-in to MBB -- work out what kind of machine
      // location it is and create a DBG_VALUE.
      if (DiffIt.isEntryBackupLoc())
        continue;
      MachineInstr *MI = DiffIt.BuildDbgValue(*MBB.getParent());
      MBB.insert(MBB.instr_begin(), MI);

      (void)MI;
      LLVM_DEBUG(dbgs() << "Inserted: "; MI->dump(););
    }
  }
}

bool VarLocBasedLDV::isEntryValueCandidate(
    const MachineInstr &MI, const DefinedRegsSet &DefinedRegs) const {
  assert(MI.isDebugValue() && "This must be DBG_VALUE.");

  // TODO: Add support for local variables that are expressed in terms of
  // parameters entry values.
  // TODO: Add support for modified arguments that can be expressed
  // by using its entry value.
  auto *DIVar = MI.getDebugVariable();
  if (!DIVar->isParameter())
    return false;

  // Do not consider parameters that belong to an inlined function.
  if (MI.getDebugLoc()->getInlinedAt())
    return false;

  // Only consider parameters that are described using registers. Parameters
  // that are passed on the stack are not yet supported, so ignore debug
  // values that are described by the frame or stack pointer.
  if (!isRegOtherThanSPAndFP(MI.getDebugOperand(0), MI, TRI))
    return false;

  // If a parameter's value has been propagated from the caller, then the
  // parameter's DBG_VALUE may be described using a register defined by some
  // instruction in the entry block, in which case we shouldn't create an
  // entry value.
  if (DefinedRegs.count(MI.getDebugOperand(0).getReg()))
    return false;

  // TODO: Add support for parameters that have a pre-existing debug expressions
  // (e.g. fragments).
  // A simple deref expression is equivalent to an indirect debug value.
  const DIExpression *Expr = MI.getDebugExpression();
  if (Expr->getNumElements() > 0 && !Expr->isDeref())
    return false;

  return true;
}

/// Collect all register defines (including aliases) for the given instruction.
static void collectRegDefs(const MachineInstr &MI, DefinedRegsSet &Regs,
                           const TargetRegisterInfo *TRI) {
  for (const MachineOperand &MO : MI.all_defs()) {
    if (MO.getReg() && MO.getReg().isPhysical()) {
      Regs.insert(MO.getReg());
      for (MCRegAliasIterator AI(MO.getReg(), TRI, true); AI.isValid(); ++AI)
        Regs.insert(*AI);
    }
  }
}

/// This routine records the entry values of function parameters. The values
/// could be used as backup values. If we loose the track of some unmodified
/// parameters, the backup values will be used as a primary locations.
void VarLocBasedLDV::recordEntryValue(const MachineInstr &MI,
                                       const DefinedRegsSet &DefinedRegs,
                                       OpenRangesSet &OpenRanges,
                                       VarLocMap &VarLocIDs) {
  if (TPC) {
    auto &TM = TPC->getTM<TargetMachine>();
    if (!TM.Options.ShouldEmitDebugEntryValues())
      return;
  }

  DebugVariable V(MI.getDebugVariable(), MI.getDebugExpression(),
                  MI.getDebugLoc()->getInlinedAt());

  if (!isEntryValueCandidate(MI, DefinedRegs) ||
      OpenRanges.getEntryValueBackup(V))
    return;

  LLVM_DEBUG(dbgs() << "Creating the backup entry location: "; MI.dump(););

  // Create the entry value and use it as a backup location until it is
  // valid. It is valid until a parameter is not changed.
  DIExpression *NewExpr =
      DIExpression::prepend(MI.getDebugExpression(), DIExpression::EntryValue);
  VarLoc EntryValLocAsBackup = VarLoc::CreateEntryBackupLoc(MI, NewExpr);
  LocIndices EntryValLocIDs = VarLocIDs.insert(EntryValLocAsBackup);
  OpenRanges.insert(EntryValLocIDs, EntryValLocAsBackup);
}

/// Calculate the liveness information for the given machine function and
/// extend ranges across basic blocks.
bool VarLocBasedLDV::ExtendRanges(MachineFunction &MF,
                                  MachineDominatorTree *DomTree,
                                  TargetPassConfig *TPC, unsigned InputBBLimit,
                                  unsigned InputDbgValLimit) {
  (void)DomTree;
  LLVM_DEBUG(dbgs() << "\nDebug Range Extension: " << MF.getName() << "\n");

  if (!MF.getFunction().getSubprogram())
    // VarLocBaseLDV will already have removed all DBG_VALUEs.
    return false;

  // Skip functions from NoDebug compilation units.
  if (MF.getFunction().getSubprogram()->getUnit()->getEmissionKind() ==
      DICompileUnit::NoDebug)
    return false;

  TRI = MF.getSubtarget().getRegisterInfo();
  TII = MF.getSubtarget().getInstrInfo();
  TFI = MF.getSubtarget().getFrameLowering();
  TFI->getCalleeSaves(MF, CalleeSavedRegs);
  this->TPC = TPC;
  LS.initialize(MF);

  bool Changed = false;
  bool OLChanged = false;
  bool MBBJoined = false;

  VarLocMap VarLocIDs;         // Map VarLoc<>unique ID for use in bitvectors.
  OverlapMap OverlapFragments; // Map of overlapping variable fragments.
  OpenRangesSet OpenRanges(Alloc, OverlapFragments);
                              // Ranges that are open until end of bb.
  VarLocInMBB OutLocs;        // Ranges that exist beyond bb.
  VarLocInMBB InLocs;         // Ranges that are incoming after joining.
  TransferMap Transfers;      // DBG_VALUEs associated with transfers (such as
                              // spills, copies and restores).
  // Map responsible MI to attached Transfer emitted from Backup Entry Value.
  InstToEntryLocMap EntryValTransfers;
  // Map a Register to the last MI which clobbered it.
  RegDefToInstMap RegSetInstrs;

  VarToFragments SeenFragments;

  // Blocks which are artificial, i.e. blocks which exclusively contain
  // instructions without locations, or with line 0 locations.
  SmallPtrSet<const MachineBasicBlock *, 16> ArtificialBlocks;

  DenseMap<unsigned int, MachineBasicBlock *> OrderToBB;
  DenseMap<MachineBasicBlock *, unsigned int> BBToOrder;
  std::priority_queue<unsigned int, std::vector<unsigned int>,
                      std::greater<unsigned int>>
      Worklist;
  std::priority_queue<unsigned int, std::vector<unsigned int>,
                      std::greater<unsigned int>>
      Pending;

  // Set of register defines that are seen when traversing the entry block
  // looking for debug entry value candidates.
  DefinedRegsSet DefinedRegs;

  // Only in the case of entry MBB collect DBG_VALUEs representing
  // function parameters in order to generate debug entry values for them.
  MachineBasicBlock &First_MBB = *(MF.begin());
  for (auto &MI : First_MBB) {
    collectRegDefs(MI, DefinedRegs, TRI);
    if (MI.isDebugValue())
      recordEntryValue(MI, DefinedRegs, OpenRanges, VarLocIDs);
  }

  // Initialize per-block structures and scan for fragment overlaps.
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      if (MI.isDebugValue())
        accumulateFragmentMap(MI, SeenFragments, OverlapFragments);

  auto hasNonArtificialLocation = [](const MachineInstr &MI) -> bool {
    if (const DebugLoc &DL = MI.getDebugLoc())
      return DL.getLine() != 0;
    return false;
  };
  for (auto &MBB : MF)
    if (none_of(MBB.instrs(), hasNonArtificialLocation))
      ArtificialBlocks.insert(&MBB);

  LLVM_DEBUG(printVarLocInMBB(MF, OutLocs, VarLocIDs,
                              "OutLocs after initialization", dbgs()));

  ReversePostOrderTraversal<MachineFunction *> RPOT(&MF);
  unsigned int RPONumber = 0;
  for (MachineBasicBlock *MBB : RPOT) {
    OrderToBB[RPONumber] = MBB;
    BBToOrder[MBB] = RPONumber;
    Worklist.push(RPONumber);
    ++RPONumber;
  }

  if (RPONumber > InputBBLimit) {
    unsigned NumInputDbgValues = 0;
    for (auto &MBB : MF)
      for (auto &MI : MBB)
        if (MI.isDebugValue())
          ++NumInputDbgValues;
    if (NumInputDbgValues > InputDbgValLimit) {
      LLVM_DEBUG(dbgs() << "Disabling VarLocBasedLDV: " << MF.getName()
                        << " has " << RPONumber << " basic blocks and "
                        << NumInputDbgValues
                        << " input DBG_VALUEs, exceeding limits.\n");
      return false;
    }
  }

  // This is a standard "union of predecessor outs" dataflow problem.
  // To solve it, we perform join() and process() using the two worklist method
  // until the ranges converge.
  // Ranges have converged when both worklists are empty.
  SmallPtrSet<const MachineBasicBlock *, 16> Visited;
  while (!Worklist.empty() || !Pending.empty()) {
    // We track what is on the pending worklist to avoid inserting the same
    // thing twice.  We could avoid this with a custom priority queue, but this
    // is probably not worth it.
    SmallPtrSet<MachineBasicBlock *, 16> OnPending;
    LLVM_DEBUG(dbgs() << "Processing Worklist\n");
    while (!Worklist.empty()) {
      MachineBasicBlock *MBB = OrderToBB[Worklist.top()];
      Worklist.pop();
      MBBJoined = join(*MBB, OutLocs, InLocs, VarLocIDs, Visited,
                       ArtificialBlocks);
      MBBJoined |= Visited.insert(MBB).second;
      if (MBBJoined) {
        MBBJoined = false;
        Changed = true;
        // Now that we have started to extend ranges across BBs we need to
        // examine spill, copy and restore instructions to see whether they
        // operate with registers that correspond to user variables.
        // First load any pending inlocs.
        OpenRanges.insertFromLocSet(getVarLocsInMBB(MBB, InLocs), VarLocIDs);
        LastNonDbgMI = nullptr;
        RegSetInstrs.clear();
        for (auto &MI : *MBB)
          process(MI, OpenRanges, VarLocIDs, Transfers, EntryValTransfers,
                  RegSetInstrs);
        OLChanged |= transferTerminator(MBB, OpenRanges, OutLocs, VarLocIDs);

        LLVM_DEBUG(printVarLocInMBB(MF, OutLocs, VarLocIDs,
                                    "OutLocs after propagating", dbgs()));
        LLVM_DEBUG(printVarLocInMBB(MF, InLocs, VarLocIDs,
                                    "InLocs after propagating", dbgs()));

        if (OLChanged) {
          OLChanged = false;
          for (auto *s : MBB->successors())
            if (OnPending.insert(s).second) {
              Pending.push(BBToOrder[s]);
            }
        }
      }
    }
    Worklist.swap(Pending);
    // At this point, pending must be empty, since it was just the empty
    // worklist
    assert(Pending.empty() && "Pending should be empty");
  }

  // Add any DBG_VALUE instructions created by location transfers.
  for (auto &TR : Transfers) {
    assert(!TR.TransferInst->isTerminator() &&
           "Cannot insert DBG_VALUE after terminator");
    MachineBasicBlock *MBB = TR.TransferInst->getParent();
    const VarLoc &VL = VarLocIDs[TR.LocationID];
    MachineInstr *MI = VL.BuildDbgValue(MF);
    MBB->insertAfterBundle(TR.TransferInst->getIterator(), MI);
  }
  Transfers.clear();

  // Add DBG_VALUEs created using Backup Entry Value location.
  for (auto &TR : EntryValTransfers) {
    MachineInstr *TRInst = const_cast<MachineInstr *>(TR.first);
    assert(!TRInst->isTerminator() &&
           "Cannot insert DBG_VALUE after terminator");
    MachineBasicBlock *MBB = TRInst->getParent();
    const VarLoc &VL = VarLocIDs[TR.second];
    MachineInstr *MI = VL.BuildDbgValue(MF);
    MBB->insertAfterBundle(TRInst->getIterator(), MI);
  }
  EntryValTransfers.clear();

  // Deferred inlocs will not have had any DBG_VALUE insts created; do
  // that now.
  flushPendingLocs(InLocs, VarLocIDs);

  LLVM_DEBUG(printVarLocInMBB(MF, OutLocs, VarLocIDs, "Final OutLocs", dbgs()));
  LLVM_DEBUG(printVarLocInMBB(MF, InLocs, VarLocIDs, "Final InLocs", dbgs()));
  return Changed;
}

LDVImpl *
llvm::makeVarLocBasedLiveDebugValues()
{
  return new VarLocBasedLDV();
}
