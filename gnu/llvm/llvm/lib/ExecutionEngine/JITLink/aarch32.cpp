//===--------- aarch32.cpp - Generic JITLink arm/thumb utilities ----------===//
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

#include "llvm/ExecutionEngine/JITLink/aarch32.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {
namespace aarch32 {

/// Check whether the given target flags are set for this Symbol.
bool hasTargetFlags(Symbol &Sym, TargetFlagsType Flags) {
  return static_cast<TargetFlagsType>(Sym.getTargetFlags()) & Flags;
}

/// Encode 22-bit immediate value for branch instructions without J1J2 range
/// extension (formats B T4, BL T1 and BLX T2).
///
///   00000:Imm11H:Imm11L:0 -> [ 00000:Imm11H, 00000:Imm11L ]
///                                            J1^ ^J2 will always be 1
///
HalfWords encodeImmBT4BlT1BlxT2(int64_t Value) {
  constexpr uint32_t J1J2 = 0x2800;
  uint32_t Imm11H = (Value >> 12) & 0x07ff;
  uint32_t Imm11L = (Value >> 1) & 0x07ff;
  return HalfWords{Imm11H, Imm11L | J1J2};
}

/// Decode 22-bit immediate value for branch instructions without J1J2 range
/// extension (formats B T4, BL T1 and BLX T2).
///
///   [ 00000:Imm11H, 00000:Imm11L ] -> 00000:Imm11H:Imm11L:0
///                   J1^ ^J2 will always be 1
///
int64_t decodeImmBT4BlT1BlxT2(uint32_t Hi, uint32_t Lo) {
  uint32_t Imm11H = Hi & 0x07ff;
  uint32_t Imm11L = Lo & 0x07ff;
  return SignExtend64<22>(Imm11H << 12 | Imm11L << 1);
}

/// Encode 25-bit immediate value for branch instructions with J1J2 range
/// extension (formats B T4, BL T1 and BLX T2).
///
///   S:I1:I2:Imm10:Imm11:0 -> [ 00000:S:Imm10, 00:J1:0:J2:Imm11 ]
///
HalfWords encodeImmBT4BlT1BlxT2_J1J2(int64_t Value) {
  uint32_t S = (Value >> 14) & 0x0400;
  uint32_t J1 = (((~(Value >> 10)) ^ (Value >> 11)) & 0x2000);
  uint32_t J2 = (((~(Value >> 11)) ^ (Value >> 13)) & 0x0800);
  uint32_t Imm10 = (Value >> 12) & 0x03ff;
  uint32_t Imm11 = (Value >> 1) & 0x07ff;
  return HalfWords{S | Imm10, J1 | J2 | Imm11};
}

/// Decode 25-bit immediate value for branch instructions with J1J2 range
/// extension (formats B T4, BL T1 and BLX T2).
///
///   [ 00000:S:Imm10, 00:J1:0:J2:Imm11] -> S:I1:I2:Imm10:Imm11:0
///
int64_t decodeImmBT4BlT1BlxT2_J1J2(uint32_t Hi, uint32_t Lo) {
  uint32_t S = Hi & 0x0400;
  uint32_t I1 = ~((Lo ^ (Hi << 3)) << 10) & 0x00800000;
  uint32_t I2 = ~((Lo ^ (Hi << 1)) << 11) & 0x00400000;
  uint32_t Imm10 = Hi & 0x03ff;
  uint32_t Imm11 = Lo & 0x07ff;
  return SignExtend64<25>(S << 14 | I1 | I2 | Imm10 << 12 | Imm11 << 1);
}

/// Encode 26-bit immediate value for branch instructions
/// (formats B A1, BL A1 and BLX A2).
///
///   Imm24:00 ->  00000000:Imm24
///
uint32_t encodeImmBA1BlA1BlxA2(int64_t Value) {
  return (Value >> 2) & 0x00ffffff;
}

/// Decode 26-bit immediate value for branch instructions
/// (formats B A1, BL A1 and BLX A2).
///
///   00000000:Imm24 ->  Imm24:00
///
int64_t decodeImmBA1BlA1BlxA2(int64_t Value) {
  return SignExtend64<26>((Value & 0x00ffffff) << 2);
}

/// Encode 16-bit immediate value for move instruction formats MOVT T1 and
/// MOVW T3.
///
///   Imm4:Imm1:Imm3:Imm8 -> [ 00000:i:000000:Imm4, 0:Imm3:0000:Imm8 ]
///
HalfWords encodeImmMovtT1MovwT3(uint16_t Value) {
  uint32_t Imm4 = (Value >> 12) & 0x0f;
  uint32_t Imm1 = (Value >> 11) & 0x01;
  uint32_t Imm3 = (Value >> 8) & 0x07;
  uint32_t Imm8 = Value & 0xff;
  return HalfWords{Imm1 << 10 | Imm4, Imm3 << 12 | Imm8};
}

/// Decode 16-bit immediate value from move instruction formats MOVT T1 and
/// MOVW T3.
///
///   [ 00000:i:000000:Imm4, 0:Imm3:0000:Imm8 ] -> Imm4:Imm1:Imm3:Imm8
///
uint16_t decodeImmMovtT1MovwT3(uint32_t Hi, uint32_t Lo) {
  uint32_t Imm4 = Hi & 0x0f;
  uint32_t Imm1 = (Hi >> 10) & 0x01;
  uint32_t Imm3 = (Lo >> 12) & 0x07;
  uint32_t Imm8 = Lo & 0xff;
  uint32_t Imm16 = Imm4 << 12 | Imm1 << 11 | Imm3 << 8 | Imm8;
  assert(Imm16 <= 0xffff && "Decoded value out-of-range");
  return Imm16;
}

/// Encode register ID for instruction formats MOVT T1 and MOVW T3.
///
///   Rd4 -> [0000000000000000, 0000:Rd4:00000000]
///
HalfWords encodeRegMovtT1MovwT3(int64_t Value) {
  uint32_t Rd4 = (Value & 0x0f) << 8;
  return HalfWords{0, Rd4};
}

/// Decode register ID from instruction formats MOVT T1 and MOVW T3.
///
///   [0000000000000000, 0000:Rd4:00000000] -> Rd4
///
int64_t decodeRegMovtT1MovwT3(uint32_t Hi, uint32_t Lo) {
  uint32_t Rd4 = (Lo >> 8) & 0x0f;
  return Rd4;
}

/// Encode 16-bit immediate value for move instruction formats MOVT A1 and
/// MOVW A2.
///
///   Imm4:Imm12 -> 000000000000:Imm4:0000:Imm12
///
uint32_t encodeImmMovtA1MovwA2(uint16_t Value) {
  uint32_t Imm4 = (Value >> 12) & 0x0f;
  uint32_t Imm12 = Value & 0x0fff;
  return (Imm4 << 16) | Imm12;
}

/// Decode 16-bit immediate value for move instruction formats MOVT A1 and
/// MOVW A2.
///
///   000000000000:Imm4:0000:Imm12 -> Imm4:Imm12
///
uint16_t decodeImmMovtA1MovwA2(uint64_t Value) {
  uint32_t Imm4 = (Value >> 16) & 0x0f;
  uint32_t Imm12 = Value & 0x0fff;
  return (Imm4 << 12) | Imm12;
}

/// Encode register ID for instruction formats MOVT A1 and
/// MOVW A2.
///
///   Rd4 -> 0000000000000000:Rd4:000000000000
///
uint32_t encodeRegMovtA1MovwA2(int64_t Value) {
  uint32_t Rd4 = (Value & 0x00000f) << 12;
  return Rd4;
}

/// Decode register ID for instruction formats MOVT A1 and
/// MOVW A2.
///
///   0000000000000000:Rd4:000000000000 -> Rd4
///
int64_t decodeRegMovtA1MovwA2(uint64_t Value) {
  uint32_t Rd4 = (Value >> 12) & 0x00000f;
  return Rd4;
}

namespace {

/// 32-bit Thumb instructions are stored as two little-endian halfwords.
/// An instruction at address A encodes bytes A+1, A in the first halfword (Hi),
/// followed by bytes A+3, A+2 in the second halfword (Lo).
struct WritableThumbRelocation {
  /// Create a writable reference to a Thumb32 fixup.
  WritableThumbRelocation(char *FixupPtr)
      : Hi{*reinterpret_cast<support::ulittle16_t *>(FixupPtr)},
        Lo{*reinterpret_cast<support::ulittle16_t *>(FixupPtr + 2)} {}

  support::ulittle16_t &Hi; // First halfword
  support::ulittle16_t &Lo; // Second halfword
};

struct ThumbRelocation {
  /// Create a read-only reference to a Thumb32 fixup.
  ThumbRelocation(const char *FixupPtr)
      : Hi{*reinterpret_cast<const support::ulittle16_t *>(FixupPtr)},
        Lo{*reinterpret_cast<const support::ulittle16_t *>(FixupPtr + 2)} {}

  /// Create a read-only Thumb32 fixup from a writeable one.
  ThumbRelocation(WritableThumbRelocation &Writable)
      : Hi{Writable.Hi}, Lo(Writable.Lo) {}

  const support::ulittle16_t &Hi; // First halfword
  const support::ulittle16_t &Lo; // Second halfword
};

struct WritableArmRelocation {
  WritableArmRelocation(char *FixupPtr)
      : Wd{*reinterpret_cast<support::ulittle32_t *>(FixupPtr)} {}

  support::ulittle32_t &Wd;
};

struct ArmRelocation {
  ArmRelocation(const char *FixupPtr)
      : Wd{*reinterpret_cast<const support::ulittle32_t *>(FixupPtr)} {}

  ArmRelocation(WritableArmRelocation &Writable) : Wd{Writable.Wd} {}

  const support::ulittle32_t &Wd;
};

Error makeUnexpectedOpcodeError(const LinkGraph &G, const ThumbRelocation &R,
                                Edge::Kind Kind) {
  return make_error<JITLinkError>(
      formatv("Invalid opcode [ {0:x4}, {1:x4} ] for relocation: {2}",
              static_cast<uint16_t>(R.Hi), static_cast<uint16_t>(R.Lo),
              G.getEdgeKindName(Kind)));
}

Error makeUnexpectedOpcodeError(const LinkGraph &G, const ArmRelocation &R,
                                Edge::Kind Kind) {
  return make_error<JITLinkError>(
      formatv("Invalid opcode {0:x8} for relocation: {1}",
              static_cast<uint32_t>(R.Wd), G.getEdgeKindName(Kind)));
}

template <EdgeKind_aarch32 K> constexpr bool isArm() {
  return FirstArmRelocation <= K && K <= LastArmRelocation;
}
template <EdgeKind_aarch32 K> constexpr bool isThumb() {
  return FirstThumbRelocation <= K && K <= LastThumbRelocation;
}

template <EdgeKind_aarch32 K> static bool checkOpcodeArm(uint32_t Wd) {
  return (Wd & FixupInfo<K>::OpcodeMask) == FixupInfo<K>::Opcode;
}

template <EdgeKind_aarch32 K>
static bool checkOpcodeThumb(uint16_t Hi, uint16_t Lo) {
  return (Hi & FixupInfo<K>::OpcodeMask.Hi) == FixupInfo<K>::Opcode.Hi &&
         (Lo & FixupInfo<K>::OpcodeMask.Lo) == FixupInfo<K>::Opcode.Lo;
}

class FixupInfoTable {
  static constexpr size_t Items = LastRelocation + 1;

public:
  FixupInfoTable() {
    populateEntries<FirstArmRelocation, LastArmRelocation>();
    populateEntries<FirstThumbRelocation, LastThumbRelocation>();
  }

  const FixupInfoBase *getEntry(Edge::Kind K) {
    assert(K < Data.size() && "Index out of bounds");
    return Data.at(K).get();
  }

private:
  template <EdgeKind_aarch32 K, EdgeKind_aarch32 LastK> void populateEntries() {
    assert(K < Data.size() && "Index out of range");
    assert(Data.at(K) == nullptr && "Initialized entries are immutable");
    Data[K] = initEntry<K>();
    if constexpr (K < LastK) {
      constexpr auto Next = static_cast<EdgeKind_aarch32>(K + 1);
      populateEntries<Next, LastK>();
    }
  }

  template <EdgeKind_aarch32 K>
  static std::unique_ptr<FixupInfoBase> initEntry() {
    auto Entry = std::make_unique<FixupInfo<K>>();
    static_assert(isArm<K>() != isThumb<K>(), "Classes are mutually exclusive");
    if constexpr (isArm<K>())
      Entry->checkOpcode = checkOpcodeArm<K>;
    if constexpr (isThumb<K>())
      Entry->checkOpcode = checkOpcodeThumb<K>;
    return Entry;
  }

private:
  std::array<std::unique_ptr<FixupInfoBase>, Items> Data;
};

ManagedStatic<FixupInfoTable> DynFixupInfos;

} // namespace

static Error checkOpcode(LinkGraph &G, const ArmRelocation &R,
                         Edge::Kind Kind) {
  assert(Kind >= FirstArmRelocation && Kind <= LastArmRelocation &&
         "Edge kind must be Arm relocation");
  const FixupInfoBase *Entry = DynFixupInfos->getEntry(Kind);
  const FixupInfoArm &Info = *static_cast<const FixupInfoArm *>(Entry);
  assert(Info.checkOpcode && "Opcode check is mandatory for Arm edges");
  if (!Info.checkOpcode(R.Wd))
    return makeUnexpectedOpcodeError(G, R, Kind);

  return Error::success();
}

static Error checkOpcode(LinkGraph &G, const ThumbRelocation &R,
                         Edge::Kind Kind) {
  assert(Kind >= FirstThumbRelocation && Kind <= LastThumbRelocation &&
         "Edge kind must be Thumb relocation");
  const FixupInfoBase *Entry = DynFixupInfos->getEntry(Kind);
  const FixupInfoThumb &Info = *static_cast<const FixupInfoThumb *>(Entry);
  assert(Info.checkOpcode && "Opcode check is mandatory for Thumb edges");
  if (!Info.checkOpcode(R.Hi, R.Lo))
    return makeUnexpectedOpcodeError(G, R, Kind);

  return Error::success();
}

const FixupInfoBase *FixupInfoBase::getDynFixupInfo(Edge::Kind K) {
  return DynFixupInfos->getEntry(K);
}

template <EdgeKind_aarch32 Kind>
bool checkRegister(const ThumbRelocation &R, HalfWords Reg) {
  uint16_t Hi = R.Hi & FixupInfo<Kind>::RegMask.Hi;
  uint16_t Lo = R.Lo & FixupInfo<Kind>::RegMask.Lo;
  return Hi == Reg.Hi && Lo == Reg.Lo;
}

template <EdgeKind_aarch32 Kind>
bool checkRegister(const ArmRelocation &R, uint32_t Reg) {
  uint32_t Wd = R.Wd & FixupInfo<Kind>::RegMask;
  return Wd == Reg;
}

template <EdgeKind_aarch32 Kind>
void writeRegister(WritableThumbRelocation &R, HalfWords Reg) {
  static constexpr HalfWords Mask = FixupInfo<Kind>::RegMask;
  assert((Mask.Hi & Reg.Hi) == Reg.Hi && (Mask.Lo & Reg.Lo) == Reg.Lo &&
         "Value bits exceed bit range of given mask");
  R.Hi = (R.Hi & ~Mask.Hi) | Reg.Hi;
  R.Lo = (R.Lo & ~Mask.Lo) | Reg.Lo;
}

template <EdgeKind_aarch32 Kind>
void writeRegister(WritableArmRelocation &R, uint32_t Reg) {
  static constexpr uint32_t Mask = FixupInfo<Kind>::RegMask;
  assert((Mask & Reg) == Reg && "Value bits exceed bit range of given mask");
  R.Wd = (R.Wd & ~Mask) | Reg;
}

template <EdgeKind_aarch32 Kind>
void writeImmediate(WritableThumbRelocation &R, HalfWords Imm) {
  static constexpr HalfWords Mask = FixupInfo<Kind>::ImmMask;
  assert((Mask.Hi & Imm.Hi) == Imm.Hi && (Mask.Lo & Imm.Lo) == Imm.Lo &&
         "Value bits exceed bit range of given mask");
  R.Hi = (R.Hi & ~Mask.Hi) | Imm.Hi;
  R.Lo = (R.Lo & ~Mask.Lo) | Imm.Lo;
}

template <EdgeKind_aarch32 Kind>
void writeImmediate(WritableArmRelocation &R, uint32_t Imm) {
  static constexpr uint32_t Mask = FixupInfo<Kind>::ImmMask;
  assert((Mask & Imm) == Imm && "Value bits exceed bit range of given mask");
  R.Wd = (R.Wd & ~Mask) | Imm;
}

Expected<int64_t> readAddendData(LinkGraph &G, Block &B, Edge::OffsetT Offset,
                                 Edge::Kind Kind) {
  endianness Endian = G.getEndianness();
  const char *BlockWorkingMem = B.getContent().data();
  const char *FixupPtr = BlockWorkingMem + Offset;

  switch (Kind) {
  case Data_Delta32:
  case Data_Pointer32:
  case Data_RequestGOTAndTransformToDelta32:
    return SignExtend64<32>(support::endian::read32(FixupPtr, Endian));
  case Data_PRel31:
    return SignExtend64<31>(support::endian::read32(FixupPtr, Endian));
  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " can not read implicit addend for aarch32 edge kind " +
        G.getEdgeKindName(Kind));
  }
}

Expected<int64_t> readAddendArm(LinkGraph &G, Block &B, Edge::OffsetT Offset,
                                Edge::Kind Kind) {
  ArmRelocation R(B.getContent().data() + Offset);
  if (Error Err = checkOpcode(G, R, Kind))
    return std::move(Err);

  switch (Kind) {
  case Arm_Call:
  case Arm_Jump24:
    return decodeImmBA1BlA1BlxA2(R.Wd);

  case Arm_MovtAbs:
  case Arm_MovwAbsNC:
    return decodeImmMovtA1MovwA2(R.Wd);

  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " can not read implicit addend for aarch32 edge kind " +
        G.getEdgeKindName(Kind));
  }
}

Expected<int64_t> readAddendThumb(LinkGraph &G, Block &B, Edge::OffsetT Offset,
                                  Edge::Kind Kind, const ArmConfig &ArmCfg) {
  ThumbRelocation R(B.getContent().data() + Offset);
  if (Error Err = checkOpcode(G, R, Kind))
    return std::move(Err);

  switch (Kind) {
  case Thumb_Call:
  case Thumb_Jump24:
    return LLVM_LIKELY(ArmCfg.J1J2BranchEncoding)
               ? decodeImmBT4BlT1BlxT2_J1J2(R.Hi, R.Lo)
               : decodeImmBT4BlT1BlxT2(R.Hi, R.Lo);

  case Thumb_MovwAbsNC:
  case Thumb_MovwPrelNC:
    // Initial addend is interpreted as a signed value
    return SignExtend64<16>(decodeImmMovtT1MovwT3(R.Hi, R.Lo));

  case Thumb_MovtAbs:
  case Thumb_MovtPrel:
    // Initial addend is interpreted as a signed value
    return SignExtend64<16>(decodeImmMovtT1MovwT3(R.Hi, R.Lo));

  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " can not read implicit addend for aarch32 edge kind " +
        G.getEdgeKindName(Kind));
  }
}

Error applyFixupData(LinkGraph &G, Block &B, const Edge &E) {
  using namespace support;

  char *BlockWorkingMem = B.getAlreadyMutableContent().data();
  char *FixupPtr = BlockWorkingMem + E.getOffset();

  Edge::Kind Kind = E.getKind();
  uint64_t FixupAddress = (B.getAddress() + E.getOffset()).getValue();
  int64_t Addend = E.getAddend();
  Symbol &TargetSymbol = E.getTarget();
  uint64_t TargetAddress = TargetSymbol.getAddress().getValue();

  // Data relocations have alignment 1, size 4 (except R_ARM_ABS8 and
  // R_ARM_ABS16) and write the full 32-bit result (except R_ARM_PREL31).
  switch (Kind) {
  case Data_Delta32: {
    int64_t Value = TargetAddress - FixupAddress + Addend;
    if (!isInt<32>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    if (LLVM_LIKELY(G.getEndianness() == endianness::little))
      endian::write32le(FixupPtr, Value);
    else
      endian::write32be(FixupPtr, Value);
    return Error::success();
  }
  case Data_Pointer32: {
    int64_t Value = TargetAddress + Addend;
    if (!isUInt<32>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    if (LLVM_LIKELY(G.getEndianness() == endianness::little))
      endian::write32le(FixupPtr, Value);
    else
      endian::write32be(FixupPtr, Value);
    return Error::success();
  }
  case Data_PRel31: {
    int64_t Value = TargetAddress - FixupAddress + Addend;
    if (!isInt<31>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    if (LLVM_LIKELY(G.getEndianness() == endianness::little)) {
      uint32_t MSB = endian::read32le(FixupPtr) & 0x80000000;
      endian::write32le(FixupPtr, MSB | (Value & ~0x80000000));
    } else {
      uint32_t MSB = endian::read32be(FixupPtr) & 0x80000000;
      endian::write32be(FixupPtr, MSB | (Value & ~0x80000000));
    }
    return Error::success();
  }
  case Data_RequestGOTAndTransformToDelta32:
    llvm_unreachable("Should be transformed");
  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " encountered unfixable aarch32 edge kind " +
        G.getEdgeKindName(E.getKind()));
  }
}

Error applyFixupArm(LinkGraph &G, Block &B, const Edge &E) {
  WritableArmRelocation R(B.getAlreadyMutableContent().data() + E.getOffset());
  Edge::Kind Kind = E.getKind();
  if (Error Err = checkOpcode(G, R, Kind))
    return Err;

  uint64_t FixupAddress = (B.getAddress() + E.getOffset()).getValue();
  int64_t Addend = E.getAddend();
  Symbol &TargetSymbol = E.getTarget();
  uint64_t TargetAddress = TargetSymbol.getAddress().getValue();

  switch (Kind) {
  case Arm_Jump24: {
    if (hasTargetFlags(TargetSymbol, ThumbSymbol))
      return make_error<JITLinkError>("Branch relocation needs interworking "
                                      "stub when bridging to Thumb: " +
                                      StringRef(G.getEdgeKindName(Kind)));

    int64_t Value = TargetAddress - FixupAddress + Addend;

    if (!isInt<26>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    writeImmediate<Arm_Jump24>(R, encodeImmBA1BlA1BlxA2(Value));

    return Error::success();
  }
  case Arm_Call: {
    if ((R.Wd & FixupInfo<Arm_Call>::CondMask) !=
        FixupInfo<Arm_Call>::Unconditional)
      return make_error<JITLinkError>("Relocation expects an unconditional "
                                      "BL/BLX branch instruction: " +
                                      StringRef(G.getEdgeKindName(Kind)));

    int64_t Value = TargetAddress - FixupAddress + Addend;

    // The call instruction itself is Arm. The call destination can either be
    // Thumb or Arm. We use BL to stay in Arm and BLX to change to Thumb.
    bool TargetIsThumb = hasTargetFlags(TargetSymbol, ThumbSymbol);
    bool InstrIsBlx = (~R.Wd & FixupInfo<Arm_Call>::BitBlx) == 0;
    if (TargetIsThumb != InstrIsBlx) {
      if (LLVM_LIKELY(TargetIsThumb)) {
        // Change opcode BL -> BLX
        R.Wd = R.Wd | FixupInfo<Arm_Call>::BitBlx;
        R.Wd = R.Wd & ~FixupInfo<Arm_Call>::BitH;
      } else {
        // Change opcode BLX -> BL
        R.Wd = R.Wd & ~FixupInfo<Arm_Call>::BitBlx;
      }
    }

    if (!isInt<26>(Value))
      return makeTargetOutOfRangeError(G, B, E);
    writeImmediate<Arm_Call>(R, encodeImmBA1BlA1BlxA2(Value));

    return Error::success();
  }
  case Arm_MovwAbsNC: {
    uint16_t Value = (TargetAddress + Addend) & 0xffff;
    writeImmediate<Arm_MovwAbsNC>(R, encodeImmMovtA1MovwA2(Value));
    return Error::success();
  }
  case Arm_MovtAbs: {
    uint16_t Value = ((TargetAddress + Addend) >> 16) & 0xffff;
    writeImmediate<Arm_MovtAbs>(R, encodeImmMovtA1MovwA2(Value));
    return Error::success();
  }
  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " encountered unfixable aarch32 edge kind " +
        G.getEdgeKindName(E.getKind()));
  }
}

Error applyFixupThumb(LinkGraph &G, Block &B, const Edge &E,
                      const ArmConfig &ArmCfg) {
  WritableThumbRelocation R(B.getAlreadyMutableContent().data() +
                            E.getOffset());
  Edge::Kind Kind = E.getKind();
  if (Error Err = checkOpcode(G, R, Kind))
    return Err;

  uint64_t FixupAddress = (B.getAddress() + E.getOffset()).getValue();
  int64_t Addend = E.getAddend();
  Symbol &TargetSymbol = E.getTarget();
  uint64_t TargetAddress = TargetSymbol.getAddress().getValue();

  switch (Kind) {
  case Thumb_Jump24: {
    if (!hasTargetFlags(TargetSymbol, ThumbSymbol))
      return make_error<JITLinkError>("Branch relocation needs interworking "
                                      "stub when bridging to ARM: " +
                                      StringRef(G.getEdgeKindName(Kind)));

    int64_t Value = TargetAddress - FixupAddress + Addend;
    if (LLVM_LIKELY(ArmCfg.J1J2BranchEncoding)) {
      if (!isInt<25>(Value))
        return makeTargetOutOfRangeError(G, B, E);
      writeImmediate<Thumb_Jump24>(R, encodeImmBT4BlT1BlxT2_J1J2(Value));
    } else {
      if (!isInt<22>(Value))
        return makeTargetOutOfRangeError(G, B, E);
      writeImmediate<Thumb_Jump24>(R, encodeImmBT4BlT1BlxT2(Value));
    }

    return Error::success();
  }

  case Thumb_Call: {
    int64_t Value = TargetAddress - FixupAddress + Addend;

    // The call instruction itself is Thumb. The call destination can either be
    // Thumb or Arm. We use BL to stay in Thumb and BLX to change to Arm.
    bool TargetIsArm = !hasTargetFlags(TargetSymbol, ThumbSymbol);
    bool InstrIsBlx = (R.Lo & FixupInfo<Thumb_Call>::LoBitNoBlx) == 0;
    if (TargetIsArm != InstrIsBlx) {
      if (LLVM_LIKELY(TargetIsArm)) {
        // Change opcode BL -> BLX and fix range value: account for 4-byte
        // aligned destination while instruction may only be 2-byte aligned
        R.Lo = R.Lo & ~FixupInfo<Thumb_Call>::LoBitNoBlx;
        R.Lo = R.Lo & ~FixupInfo<Thumb_Call>::LoBitH;
        Value = alignTo(Value, 4);
      } else {
        // Change opcode BLX -> BL
        R.Lo = R.Lo & ~FixupInfo<Thumb_Call>::LoBitNoBlx;
      }
    }

    if (LLVM_LIKELY(ArmCfg.J1J2BranchEncoding)) {
      if (!isInt<25>(Value))
        return makeTargetOutOfRangeError(G, B, E);
      writeImmediate<Thumb_Call>(R, encodeImmBT4BlT1BlxT2_J1J2(Value));
    } else {
      if (!isInt<22>(Value))
        return makeTargetOutOfRangeError(G, B, E);
      writeImmediate<Thumb_Call>(R, encodeImmBT4BlT1BlxT2(Value));
    }

    assert(((R.Lo & FixupInfo<Thumb_Call>::LoBitNoBlx) ||
            (R.Lo & FixupInfo<Thumb_Call>::LoBitH) == 0) &&
           "Opcode BLX implies H bit is clear (avoid UB in BLX T2)");
    return Error::success();
  }

  case Thumb_MovwAbsNC: {
    uint16_t Value = (TargetAddress + Addend) & 0xffff;
    writeImmediate<Thumb_MovwAbsNC>(R, encodeImmMovtT1MovwT3(Value));
    return Error::success();
  }
  case Thumb_MovtAbs: {
    uint16_t Value = ((TargetAddress + Addend) >> 16) & 0xffff;
    writeImmediate<Thumb_MovtAbs>(R, encodeImmMovtT1MovwT3(Value));
    return Error::success();
  }
  case Thumb_MovwPrelNC: {
    uint16_t Value = ((TargetAddress + Addend - FixupAddress) & 0xffff);
    writeImmediate<Thumb_MovwPrelNC>(R, encodeImmMovtT1MovwT3(Value));
    return Error::success();
  }
  case Thumb_MovtPrel: {
    uint16_t Value = (((TargetAddress + Addend - FixupAddress) >> 16) & 0xffff);
    writeImmediate<Thumb_MovtPrel>(R, encodeImmMovtT1MovwT3(Value));
    return Error::success();
  }

  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " encountered unfixable aarch32 edge kind " +
        G.getEdgeKindName(E.getKind()));
  }
}

const uint8_t GOTEntryInit[] = {
    0x00,
    0x00,
    0x00,
    0x00,
};

/// Create a new node in the link-graph for the given pointer value.
template <size_t Size>
static Block &allocPointer(LinkGraph &G, Section &S,
                           const uint8_t (&Content)[Size]) {
  static_assert(Size == 4, "Pointers are 32-bit");
  constexpr uint64_t Alignment = 4;
  ArrayRef<char> Init(reinterpret_cast<const char *>(Content), Size);
  return G.createContentBlock(S, Init, orc::ExecutorAddr(), Alignment, 0);
}

Symbol &GOTBuilder::createEntry(LinkGraph &G, Symbol &Target) {
  if (!GOTSection)
    GOTSection = &G.createSection(getSectionName(), orc::MemProt::Read);
  Block &B = allocPointer(G, *GOTSection, GOTEntryInit);
  constexpr int64_t GOTEntryAddend = 0;
  B.addEdge(Data_Pointer32, 0, Target, GOTEntryAddend);
  return G.addAnonymousSymbol(B, 0, B.getSize(), false, false);
}

bool GOTBuilder::visitEdge(LinkGraph &G, Block *B, Edge &E) {
  Edge::Kind KindToSet = Edge::Invalid;
  switch (E.getKind()) {
  case aarch32::Data_RequestGOTAndTransformToDelta32: {
    KindToSet = aarch32::Data_Delta32;
    break;
  }
  default:
    return false;
  }
  LLVM_DEBUG(dbgs() << "  Transforming " << G.getEdgeKindName(E.getKind())
                    << " edge at " << B->getFixupAddress(E) << " ("
                    << B->getAddress() << " + "
                    << formatv("{0:x}", E.getOffset()) << ") into "
                    << G.getEdgeKindName(KindToSet) << "\n");
  E.setKind(KindToSet);
  E.setTarget(getEntryForTarget(G, E.getTarget()));
  return true;
}

const uint8_t ArmThumbv5LdrPc[] = {
    0x78, 0x47,             // bx pc
    0xfd, 0xe7,             // b #-6 ; Arm recommended sequence to follow bx pc
    0x04, 0xf0, 0x1f, 0xe5, // ldr pc, [pc,#-4] ; L1
    0x00, 0x00, 0x00, 0x00, // L1: .word S
};

const uint8_t Armv7ABS[] = {
    0x00, 0xc0, 0x00, 0xe3, // movw r12, #0x0000     ; lower 16-bit
    0x00, 0xc0, 0x40, 0xe3, // movt r12, #0x0000     ; upper 16-bit
    0x1c, 0xff, 0x2f, 0xe1  // bx   r12
};

const uint8_t Thumbv7ABS[] = {
    0x40, 0xf2, 0x00, 0x0c, // movw r12, #0x0000    ; lower 16-bit
    0xc0, 0xf2, 0x00, 0x0c, // movt r12, #0x0000    ; upper 16-bit
    0x60, 0x47              // bx   r12
};

/// Create a new node in the link-graph for the given stub template.
template <size_t Size>
static Block &allocStub(LinkGraph &G, Section &S, const uint8_t (&Code)[Size]) {
  constexpr uint64_t Alignment = 4;
  ArrayRef<char> Template(reinterpret_cast<const char *>(Code), Size);
  return G.createContentBlock(S, Template, orc::ExecutorAddr(), Alignment, 0);
}

static Block &createStubPrev7(LinkGraph &G, Section &S, Symbol &Target) {
  Block &B = allocStub(G, S, ArmThumbv5LdrPc);
  B.addEdge(Data_Pointer32, 8, Target, 0);
  return B;
}

static Block &createStubThumbv7(LinkGraph &G, Section &S, Symbol &Target) {
  Block &B = allocStub(G, S, Thumbv7ABS);
  B.addEdge(Thumb_MovwAbsNC, 0, Target, 0);
  B.addEdge(Thumb_MovtAbs, 4, Target, 0);

  [[maybe_unused]] const char *StubPtr = B.getContent().data();
  [[maybe_unused]] HalfWords Reg12 = encodeRegMovtT1MovwT3(12);
  assert(checkRegister<Thumb_MovwAbsNC>(StubPtr, Reg12) &&
         checkRegister<Thumb_MovtAbs>(StubPtr + 4, Reg12) &&
         "Linker generated stubs may only corrupt register r12 (IP)");
  return B;
}

static Block &createStubArmv7(LinkGraph &G, Section &S, Symbol &Target) {
  Block &B = allocStub(G, S, Armv7ABS);
  B.addEdge(Arm_MovwAbsNC, 0, Target, 0);
  B.addEdge(Arm_MovtAbs, 4, Target, 0);

  [[maybe_unused]] const char *StubPtr = B.getContent().data();
  [[maybe_unused]] uint32_t Reg12 = encodeRegMovtA1MovwA2(12);
  assert(checkRegister<Arm_MovwAbsNC>(StubPtr, Reg12) &&
         checkRegister<Arm_MovtAbs>(StubPtr + 4, Reg12) &&
         "Linker generated stubs may only corrupt register r12 (IP)");
  return B;
}

static bool needsStub(const Edge &E) {
  Symbol &Target = E.getTarget();

  // Create stubs for external branch targets.
  if (!Target.isDefined()) {
    switch (E.getKind()) {
    case Arm_Call:
    case Arm_Jump24:
    case Thumb_Call:
    case Thumb_Jump24:
      return true;
    default:
      return false;
    }
  }

  // For local targets, create interworking stubs if we switch Arm/Thumb with an
  // instruction that cannot switch the instruction set state natively.
  bool TargetIsThumb = Target.getTargetFlags() & ThumbSymbol;
  switch (E.getKind()) {
  case Arm_Jump24:
    return TargetIsThumb; // Branch to Thumb needs interworking stub
  case Thumb_Jump24:
    return !TargetIsThumb; // Branch to Arm needs interworking stub
  default:
    break;
  }

  return false;
}

// The ArmThumbv5LdrPc stub has 2 entrypoints: Thumb at offset 0 is taken only
// for Thumb B instructions. Thumb BL is rewritten to BLX and takes the Arm
// entrypoint at offset 4. Arm branches always use that one.
Symbol *StubsManager_prev7::getOrCreateSlotEntrypoint(LinkGraph &G,
                                                      StubMapEntry &Slot,
                                                      bool Thumb) {
  constexpr orc::ExecutorAddrDiff ThumbEntrypointOffset = 0;
  constexpr orc::ExecutorAddrDiff ArmEntrypointOffset = 4;
  if (Thumb && !Slot.ThumbEntry) {
    Slot.ThumbEntry =
        &G.addAnonymousSymbol(*Slot.B, ThumbEntrypointOffset, 4, true, false);
    Slot.ThumbEntry->setTargetFlags(ThumbSymbol);
  }
  if (!Thumb && !Slot.ArmEntry)
    Slot.ArmEntry =
        &G.addAnonymousSymbol(*Slot.B, ArmEntrypointOffset, 8, true, false);
  return Thumb ? Slot.ThumbEntry : Slot.ArmEntry;
}

bool StubsManager_prev7::visitEdge(LinkGraph &G, Block *B, Edge &E) {
  if (!needsStub(E))
    return false;

  Symbol &Target = E.getTarget();
  assert(Target.hasName() && "Edge cannot point to anonymous target");
  auto [Slot, NewStub] = getStubMapSlot(Target.getName());

  if (NewStub) {
    if (!StubsSection)
      StubsSection = &G.createSection(getSectionName(),
                                      orc::MemProt::Read | orc::MemProt::Exec);
    LLVM_DEBUG({
      dbgs() << "    Created stub entry for " << Target.getName() << " in "
             << StubsSection->getName() << "\n";
    });
    Slot->B = &createStubPrev7(G, *StubsSection, Target);
  }

  // The ArmThumbv5LdrPc stub has 2 entrypoints: Thumb at offset 0 is taken only
  // for Thumb B instructions. Thumb BL is rewritten to BLX and takes the Arm
  // entrypoint at offset 4. Arm branches always use that one.
  bool UseThumb = E.getKind() == Thumb_Jump24;
  Symbol *StubEntrypoint = getOrCreateSlotEntrypoint(G, *Slot, UseThumb);

  LLVM_DEBUG({
    dbgs() << "    Using " << (UseThumb ? "Thumb" : "Arm") << " entrypoint "
           << *StubEntrypoint << " in "
           << StubEntrypoint->getBlock().getSection().getName() << "\n";
  });

  E.setTarget(*StubEntrypoint);
  return true;
}

bool StubsManager_v7::visitEdge(LinkGraph &G, Block *B, Edge &E) {
  if (!needsStub(E))
    return false;

  // Stub Arm/Thumb follows instruction set state at relocation site.
  // TODO: We may reduce them at relaxation time and reuse freed slots.
  bool MakeThumb = (E.getKind() > LastArmRelocation);
  LLVM_DEBUG(dbgs() << "  Preparing " << (MakeThumb ? "Thumb" : "Arm")
                    << " stub for " << G.getEdgeKindName(E.getKind())
                    << " edge at " << B->getFixupAddress(E) << " ("
                    << B->getAddress() << " + "
                    << formatv("{0:x}", E.getOffset()) << ")\n");

  Symbol &Target = E.getTarget();
  assert(Target.hasName() && "Edge cannot point to anonymous target");
  Symbol *&StubSymbol = getStubSymbolSlot(Target.getName(), MakeThumb);

  if (!StubSymbol) {
    if (!StubsSection)
      StubsSection = &G.createSection(getSectionName(),
                                      orc::MemProt::Read | orc::MemProt::Exec);
    Block &B = MakeThumb ? createStubThumbv7(G, *StubsSection, Target)
                         : createStubArmv7(G, *StubsSection, Target);
    StubSymbol = &G.addAnonymousSymbol(B, 0, B.getSize(), true, false);
    if (MakeThumb)
      StubSymbol->setTargetFlags(ThumbSymbol);

    LLVM_DEBUG({
      dbgs() << "    Created " << (MakeThumb ? "Thumb" : "Arm") << " entry for "
             << Target.getName() << " in " << StubsSection->getName() << ": "
             << *StubSymbol << "\n";
    });
  }

  assert(MakeThumb == (StubSymbol->getTargetFlags() & ThumbSymbol) &&
         "Instruction set states of stub and relocation site should be equal");
  LLVM_DEBUG({
    dbgs() << "    Using " << (MakeThumb ? "Thumb" : "Arm") << " entry "
           << *StubSymbol << " in "
           << StubSymbol->getBlock().getSection().getName() << "\n";
  });

  E.setTarget(*StubSymbol);
  return true;
}

const char *getEdgeKindName(Edge::Kind K) {
#define KIND_NAME_CASE(K)                                                      \
  case K:                                                                      \
    return #K;

  switch (K) {
    KIND_NAME_CASE(Data_Delta32)
    KIND_NAME_CASE(Data_Pointer32)
    KIND_NAME_CASE(Data_PRel31)
    KIND_NAME_CASE(Data_RequestGOTAndTransformToDelta32)
    KIND_NAME_CASE(Arm_Call)
    KIND_NAME_CASE(Arm_Jump24)
    KIND_NAME_CASE(Arm_MovwAbsNC)
    KIND_NAME_CASE(Arm_MovtAbs)
    KIND_NAME_CASE(Thumb_Call)
    KIND_NAME_CASE(Thumb_Jump24)
    KIND_NAME_CASE(Thumb_MovwAbsNC)
    KIND_NAME_CASE(Thumb_MovtAbs)
    KIND_NAME_CASE(Thumb_MovwPrelNC)
    KIND_NAME_CASE(Thumb_MovtPrel)
    KIND_NAME_CASE(None)
  default:
    return getGenericEdgeKindName(K);
  }
#undef KIND_NAME_CASE
}

const char *getCPUArchName(ARMBuildAttrs::CPUArch K) {
#define CPUARCH_NAME_CASE(K)                                                   \
  case K:                                                                      \
    return #K;

  using namespace ARMBuildAttrs;
  switch (K) {
    CPUARCH_NAME_CASE(Pre_v4)
    CPUARCH_NAME_CASE(v4)
    CPUARCH_NAME_CASE(v4T)
    CPUARCH_NAME_CASE(v5T)
    CPUARCH_NAME_CASE(v5TE)
    CPUARCH_NAME_CASE(v5TEJ)
    CPUARCH_NAME_CASE(v6)
    CPUARCH_NAME_CASE(v6KZ)
    CPUARCH_NAME_CASE(v6T2)
    CPUARCH_NAME_CASE(v6K)
    CPUARCH_NAME_CASE(v7)
    CPUARCH_NAME_CASE(v6_M)
    CPUARCH_NAME_CASE(v6S_M)
    CPUARCH_NAME_CASE(v7E_M)
    CPUARCH_NAME_CASE(v8_A)
    CPUARCH_NAME_CASE(v8_R)
    CPUARCH_NAME_CASE(v8_M_Base)
    CPUARCH_NAME_CASE(v8_M_Main)
    CPUARCH_NAME_CASE(v8_1_M_Main)
    CPUARCH_NAME_CASE(v9_A)
  }
  llvm_unreachable("Missing CPUArch in switch?");
#undef CPUARCH_NAME_CASE
}

} // namespace aarch32
} // namespace jitlink
} // namespace llvm
