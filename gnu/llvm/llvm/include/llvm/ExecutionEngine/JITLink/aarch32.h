//===------ aarch32.h - Generic JITLink arm/thumb utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing arm/thumb objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_AARCH32
#define LLVM_EXECUTIONENGINE_JITLINK_AARCH32

#include "TableManager.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace jitlink {
namespace aarch32 {

/// Check whether the given target flags are set for this Symbol.
bool hasTargetFlags(Symbol &Sym, TargetFlagsType Flags);

/// JITLink-internal AArch32 fixup kinds
enum EdgeKind_aarch32 : Edge::Kind {

  ///
  /// Relocations of class Data respect target endianness (unless otherwise
  /// specified)
  ///
  FirstDataRelocation = Edge::FirstRelocation,

  /// Relative 32-bit value relocation
  Data_Delta32 = FirstDataRelocation,

  /// Absolute 32-bit value relocation
  Data_Pointer32,

  /// Relative 31-bit value relocation that preserves the most-significant bit
  Data_PRel31,

  /// Create GOT entry and store offset
  Data_RequestGOTAndTransformToDelta32,

  LastDataRelocation = Data_RequestGOTAndTransformToDelta32,

  ///
  /// Relocations of class Arm (covers fixed-width 4-byte instruction subset)
  ///
  FirstArmRelocation,

  /// Write immediate value for unconditional PC-relative branch with link.
  /// We patch the instruction opcode to account for an instruction-set state
  /// switch: we use the bl instruction to stay in ARM and the blx instruction
  /// to switch to Thumb.
  Arm_Call = FirstArmRelocation,

  /// Write immediate value for conditional PC-relative branch without link.
  /// If the branch target is not ARM, we are forced to generate an explicit
  /// interworking stub.
  Arm_Jump24,

  /// Write immediate value to the lower halfword of the destination register
  Arm_MovwAbsNC,

  /// Write immediate value to the top halfword of the destination register
  Arm_MovtAbs,

  LastArmRelocation = Arm_MovtAbs,

  ///
  /// Relocations of class Thumb16 and Thumb32 (covers Thumb instruction subset)
  ///
  FirstThumbRelocation,

  /// Write immediate value for unconditional PC-relative branch with link.
  /// We patch the instruction opcode to account for an instruction-set state
  /// switch: we use the bl instruction to stay in Thumb and the blx instruction
  /// to switch to ARM.
  Thumb_Call = FirstThumbRelocation,

  /// Write immediate value for PC-relative branch without link. The instruction
  /// can be made conditional by an IT block. If the branch target is not ARM,
  /// we are forced to generate an explicit interworking stub.
  Thumb_Jump24,

  /// Write immediate value to the lower halfword of the destination register
  Thumb_MovwAbsNC,

  /// Write immediate value to the top halfword of the destination register
  Thumb_MovtAbs,

  /// Write PC-relative immediate value to the lower halfword of the destination
  /// register
  Thumb_MovwPrelNC,

  /// Write PC-relative immediate value to the top halfword of the destination
  /// register
  Thumb_MovtPrel,

  LastThumbRelocation = Thumb_MovtPrel,

  /// No-op relocation
  None,

  LastRelocation = None,
};

/// Flags enum for AArch32-specific symbol properties
enum TargetFlags_aarch32 : TargetFlagsType {
  ThumbSymbol = 1 << 0,
};

/// Human-readable name for a given CPU architecture kind
const char *getCPUArchName(ARMBuildAttrs::CPUArch K);

/// Get a human-readable name for the given AArch32 edge kind.
const char *getEdgeKindName(Edge::Kind K);

/// AArch32 uses stubs for a number of purposes, like branch range extension
/// or interworking between Arm and Thumb instruction subsets.
///
/// Stub implementations vary depending on CPU architecture (v4, v6, v7),
/// instruction subset and branch type (absolute/PC-relative).
///
/// For each kind of stub, the StubsFlavor defines one concrete form that is
/// used throughout the LinkGraph.
///
/// Stubs are often called "veneers" in the official docs and online.
///
enum class StubsFlavor {
  Undefined = 0,
  pre_v7,
  v7,
};

/// JITLink sub-arch configuration for Arm CPU models
struct ArmConfig {
  bool J1J2BranchEncoding = false;
  StubsFlavor Stubs = StubsFlavor::Undefined;
  // In the long term, we might want a linker switch like --target1-rel
  bool Target1Rel = false;
};

/// Obtain the sub-arch configuration for a given Arm CPU model.
inline ArmConfig getArmConfigForCPUArch(ARMBuildAttrs::CPUArch CPUArch) {
  ArmConfig ArmCfg;
  if (CPUArch == ARMBuildAttrs::v7 || CPUArch >= ARMBuildAttrs::v7E_M) {
    ArmCfg.J1J2BranchEncoding = true;
    ArmCfg.Stubs = StubsFlavor::v7;
  } else {
    ArmCfg.J1J2BranchEncoding = false;
    ArmCfg.Stubs = StubsFlavor::pre_v7;
  }
  return ArmCfg;
}

/// Immutable pair of halfwords, Hi and Lo, with overflow check
struct HalfWords {
  constexpr HalfWords() : Hi(0), Lo(0) {}
  constexpr HalfWords(uint32_t Hi, uint32_t Lo) : Hi(Hi), Lo(Lo) {
    assert(isUInt<16>(Hi) && "Overflow in first half-word");
    assert(isUInt<16>(Lo) && "Overflow in second half-word");
  }
  const uint16_t Hi; // First halfword
  const uint16_t Lo; // Second halfword
};

/// FixupInfo base class is required for dynamic lookups.
struct FixupInfoBase {
  static const FixupInfoBase *getDynFixupInfo(Edge::Kind K);
  virtual ~FixupInfoBase() {}
};

/// FixupInfo checks for Arm edge kinds work on 32-bit words
struct FixupInfoArm : public FixupInfoBase {
  bool (*checkOpcode)(uint32_t Wd) = nullptr;
};

/// FixupInfo check for Thumb32 edge kinds work on a pair of 16-bit halfwords
struct FixupInfoThumb : public FixupInfoBase {
  bool (*checkOpcode)(uint16_t Hi, uint16_t Lo) = nullptr;
};

/// Collection of named constants per fixup kind
///
/// Mandatory entries:
///   Opcode      - Values of the op-code bits in the instruction, with
///                 unaffected bits nulled
///   OpcodeMask  - Mask with all bits set that encode the op-code
///
/// Other common entries:
///   ImmMask     - Mask with all bits set that encode the immediate value
///   RegMask     - Mask with all bits set that encode the register
///
/// Specializations can add further custom fields without restrictions.
///
template <EdgeKind_aarch32 Kind> struct FixupInfo {};

struct FixupInfoArmBranch : public FixupInfoArm {
  static constexpr uint32_t Opcode = 0x0a000000;
  static constexpr uint32_t ImmMask = 0x00ffffff;
};

template <> struct FixupInfo<Arm_Jump24> : public FixupInfoArmBranch {
  static constexpr uint32_t OpcodeMask = 0x0f000000;
};

template <> struct FixupInfo<Arm_Call> : public FixupInfoArmBranch {
  static constexpr uint32_t OpcodeMask = 0x0e000000;
  static constexpr uint32_t CondMask = 0xe0000000; // excluding BLX bit
  static constexpr uint32_t Unconditional = 0xe0000000;
  static constexpr uint32_t BitH = 0x01000000;
  static constexpr uint32_t BitBlx = 0x10000000;
};

struct FixupInfoArmMov : public FixupInfoArm {
  static constexpr uint32_t OpcodeMask = 0x0ff00000;
  static constexpr uint32_t ImmMask = 0x000f0fff;
  static constexpr uint32_t RegMask = 0x0000f000;
};

template <> struct FixupInfo<Arm_MovtAbs> : public FixupInfoArmMov {
  static constexpr uint32_t Opcode = 0x03400000;
};

template <> struct FixupInfo<Arm_MovwAbsNC> : public FixupInfoArmMov {
  static constexpr uint32_t Opcode = 0x03000000;
};

template <> struct FixupInfo<Thumb_Jump24> : public FixupInfoThumb {
  static constexpr HalfWords Opcode{0xf000, 0x9000};
  static constexpr HalfWords OpcodeMask{0xf800, 0x9000};
  static constexpr HalfWords ImmMask{0x07ff, 0x2fff};
};

template <> struct FixupInfo<Thumb_Call> : public FixupInfoThumb {
  static constexpr HalfWords Opcode{0xf000, 0xc000};
  static constexpr HalfWords OpcodeMask{0xf800, 0xc000};
  static constexpr HalfWords ImmMask{0x07ff, 0x2fff};
  static constexpr uint16_t LoBitH = 0x0001;
  static constexpr uint16_t LoBitNoBlx = 0x1000;
};

struct FixupInfoThumbMov : public FixupInfoThumb {
  static constexpr HalfWords OpcodeMask{0xfbf0, 0x8000};
  static constexpr HalfWords ImmMask{0x040f, 0x70ff};
  static constexpr HalfWords RegMask{0x0000, 0x0f00};
};

template <> struct FixupInfo<Thumb_MovtAbs> : public FixupInfoThumbMov {
  static constexpr HalfWords Opcode{0xf2c0, 0x0000};
};

template <> struct FixupInfo<Thumb_MovtPrel> : public FixupInfoThumbMov {
  static constexpr HalfWords Opcode{0xf2c0, 0x0000};
};

template <> struct FixupInfo<Thumb_MovwAbsNC> : public FixupInfoThumbMov {
  static constexpr HalfWords Opcode{0xf240, 0x0000};
};

template <> struct FixupInfo<Thumb_MovwPrelNC> : public FixupInfoThumbMov {
  static constexpr HalfWords Opcode{0xf240, 0x0000};
};

/// Helper function to read the initial addend for Data-class relocations.
Expected<int64_t> readAddendData(LinkGraph &G, Block &B, Edge::OffsetT Offset,
                                 Edge::Kind Kind);

/// Helper function to read the initial addend for Arm-class relocations.
Expected<int64_t> readAddendArm(LinkGraph &G, Block &B, Edge::OffsetT Offset,
                                Edge::Kind Kind);

/// Helper function to read the initial addend for Thumb-class relocations.
Expected<int64_t> readAddendThumb(LinkGraph &G, Block &B, Edge::OffsetT Offset,
                                  Edge::Kind Kind, const ArmConfig &ArmCfg);

/// Read the initial addend for a REL-type relocation. It's the value encoded
/// in the immediate field of the fixup location by the compiler.
inline Expected<int64_t> readAddend(LinkGraph &G, Block &B,
                                    Edge::OffsetT Offset, Edge::Kind Kind,
                                    const ArmConfig &ArmCfg) {
  if (Kind <= LastDataRelocation)
    return readAddendData(G, B, Offset, Kind);

  if (Kind <= LastArmRelocation)
    return readAddendArm(G, B, Offset, Kind);

  if (Kind <= LastThumbRelocation)
    return readAddendThumb(G, B, Offset, Kind, ArmCfg);

  assert(Kind == None && "Not associated with a relocation class");
  return 0;
}

/// Helper function to apply the fixup for Data-class relocations.
Error applyFixupData(LinkGraph &G, Block &B, const Edge &E);

/// Helper function to apply the fixup for Arm-class relocations.
Error applyFixupArm(LinkGraph &G, Block &B, const Edge &E);

/// Helper function to apply the fixup for Thumb-class relocations.
Error applyFixupThumb(LinkGraph &G, Block &B, const Edge &E,
                      const ArmConfig &ArmCfg);

/// Apply fixup expression for edge to block content.
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E,
                        const ArmConfig &ArmCfg) {
  Edge::Kind Kind = E.getKind();

  if (Kind <= LastDataRelocation)
    return applyFixupData(G, B, E);

  if (Kind <= LastArmRelocation)
    return applyFixupArm(G, B, E);

  if (Kind <= LastThumbRelocation)
    return applyFixupThumb(G, B, E, ArmCfg);

  assert(Kind == None && "Not associated with a relocation class");
  return Error::success();
}

/// Populate a Global Offset Table from edges that request it.
class GOTBuilder : public TableManager<GOTBuilder> {
public:
  static StringRef getSectionName() { return "$__GOT"; }

  bool visitEdge(LinkGraph &G, Block *B, Edge &E);
  Symbol &createEntry(LinkGraph &G, Symbol &Target);

private:
  Section *GOTSection = nullptr;
};

/// Stubs builder emits non-position-independent Arm stubs for pre-v7 CPUs.
/// These architectures have no MovT/MovW instructions and don't support Thumb2.
/// BL is the only Thumb instruction that can generate stubs and they can always
/// be transformed into BLX.
class StubsManager_prev7 {
public:
  StubsManager_prev7() = default;

  /// Name of the object file section that will contain all our stubs.
  static StringRef getSectionName() {
    return "__llvm_jitlink_aarch32_STUBS_prev7";
  }

  /// Implements link-graph traversal via visitExistingEdges()
  bool visitEdge(LinkGraph &G, Block *B, Edge &E);

private:
  // Each stub uses a single block that can have 2 entryponts, one for Arm and
  // one for Thumb
  struct StubMapEntry {
    Block *B = nullptr;
    Symbol *ArmEntry = nullptr;
    Symbol *ThumbEntry = nullptr;
  };

  std::pair<StubMapEntry *, bool> getStubMapSlot(StringRef Name) {
    auto &&[Stubs, NewStub] = StubMap.try_emplace(Name);
    return std::make_pair(&Stubs->second, NewStub);
  }

  Symbol *getOrCreateSlotEntrypoint(LinkGraph &G, StubMapEntry &Slot,
                                    bool Thumb);

  DenseMap<StringRef, StubMapEntry> StubMap;
  Section *StubsSection = nullptr;
};

/// Stubs builder for v7 emits non-position-independent Arm and Thumb stubs.
class StubsManager_v7 {
public:
  StubsManager_v7() = default;

  /// Name of the object file section that will contain all our stubs.
  static StringRef getSectionName() {
    return "__llvm_jitlink_aarch32_STUBS_v7";
  }

  /// Implements link-graph traversal via visitExistingEdges().
  bool visitEdge(LinkGraph &G, Block *B, Edge &E);

private:
  // Two slots per external: Arm and Thumb
  using StubMapEntry = std::tuple<Symbol *, Symbol *>;

  Symbol *&getStubSymbolSlot(StringRef Name, bool Thumb) {
    StubMapEntry &Stubs = StubMap.try_emplace(Name).first->second;
    if (Thumb)
      return std::get<1>(Stubs);
    return std::get<0>(Stubs);
  }

  DenseMap<StringRef, StubMapEntry> StubMap;
  Section *StubsSection = nullptr;
};

} // namespace aarch32
} // namespace jitlink
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_AARCH32
