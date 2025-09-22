//=== aarch64.h - Generic JITLink aarch64 edge kinds, utilities -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing aarch64 objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_AARCH64_H
#define LLVM_EXECUTIONENGINE_JITLINK_AARCH64_H

#include "TableManager.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/Shared/MemoryFlags.h"

namespace llvm {
namespace jitlink {
namespace aarch64 {

/// Represents aarch64 fixups and other aarch64-specific edge kinds.
enum EdgeKind_aarch64 : Edge::Kind {

  /// A plain 64-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint64
  ///
  Pointer64 = Edge::FirstRelocation,

  /// A plain 32-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint32
  ///
  /// Errors:
  ///   - The target must reside in the low 32-bits of the address space,
  ///     otherwise an out-of-range error will be returned.
  ///
  Pointer32,

  /// A 64-bit delta.
  ///
  /// Delta from the fixup to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int64
  ///
  Delta64,

  /// A 32-bit delta.
  ///
  /// Delta from the fixup to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int64
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  Delta32,

  /// A 64-bit negative delta.
  ///
  /// Delta from target back to the fixup.
  ///
  /// Fixup expression:
  ///   Fixup <- Fixup - Target + Addend : int64
  ///
  NegDelta64,

  /// A 32-bit negative delta.
  ///
  /// Delta from the target back to the fixup.
  ///
  /// Fixup expression:
  ///   Fixup <- Fixup - Target + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  NegDelta32,

  /// A 26-bit PC-relative branch.
  ///
  /// Represents a PC-relative call or branch to a target within +/-128Mb. The
  /// target must be 32-bit aligned.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int26
  ///
  /// Notes:
  ///   The '26' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF and MachO relocations.
  /// Since the low two bits must be zero (because of the 32-bit alignment of
  /// the target) the operand is effectively a signed 28-bit number.
  ///
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     32-bit aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int26 otherwise an
  ///     out-of-range error will be returned.
  Branch26PCRel,

  /// A 14-bit PC-relative test and branch.
  ///
  /// Represents a PC-relative test and branch to a target within +/-32Kb. The
  /// target must be 32-bit aligned.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int14
  ///
  /// Notes:
  ///   The '14' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF relocation.
  /// Since the low two bits must be zero (because of the 32-bit alignment of
  /// the target) the operand is effectively a signed 16-bit number.
  ///
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     32-bit aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int14 otherwise an
  ///     out-of-range error will be returned.
  TestAndBranch14PCRel,

  /// A 19-bit PC-relative conditional branch.
  ///
  /// Represents a PC-relative conditional branch to a target within +/-1Mb. The
  /// target must be 32-bit aligned.
  ///
  /// Fixup expression:
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int19
  ///
  /// Notes:
  ///   The '19' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF relocation.
  /// Since the low two bits must be zero (because of the 32-bit alignment of
  /// the target) the operand is effectively a signed 21-bit number.
  ///
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     32-bit aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int19 otherwise an
  ///     out-of-range error will be returned.
  CondBranch19PCRel,

  /// A 16-bit slice of the target address (which slice depends on the
  /// instruction at the fixup location).
  ///
  /// Used to fix up MOVK/MOVN/MOVZ instructions.
  ///
  /// Fixup expression:
  ///
  ///   Fixup <- (Target + Addend) >> Shift : uint16
  ///
  ///   where Shift is encoded in the instruction at the fixup location.
  ///
  MoveWide16,

  /// The signed 21-bit delta from the fixup to the target.
  ///
  /// Typically used to load a pointers at a PC-relative offset of +/- 1Mb. The
  /// target must be 32-bit aligned.
  ///
  /// Fixup expression:
  ///
  ///   Fixup <- (Target - Fixup + Addend) >> 2 : int19
  ///
  /// Notes:
  ///   The '19' in the name refers to the number operand bits and follows the
  /// naming convention used by the corresponding ELF relocation.
  /// Since the low two bits must be zero (because of the 32-bit alignment of
  /// the target) the operand is effectively a signed 21-bit number.
  ///
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     32-bit aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into an int19 or an
  ///     out-of-range error will be returned.
  LDRLiteral19,

  /// The signed 21-bit delta from the fixup to the target.
  ///
  /// Fixup expression:
  ///
  ///   Fixup <- Target - Fixup + Addend : int21
  ///
  /// Notes:
  ///   For ADR fixups.
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int21 otherwise an
  ///     out-of-range error will be returned.
  ADRLiteral21,

  /// The signed 21-bit delta from the fixup page to the page containing the
  /// target.
  ///
  /// Fixup expression:
  ///
  ///   Fixup <- (((Target + Addend) & ~0xfff) - (Fixup & ~0xfff)) >> 12 : int21
  ///
  /// Notes:
  ///   For ADRP fixups.
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int21 otherwise an
  ///     out-of-range error will be returned.
  Page21,

  /// The 12-bit (potentially shifted) offset of the target within its page.
  ///
  /// Typically used to fix up LDR immediates.
  ///
  /// Fixup expression:
  ///
  ///   Fixup <- ((Target + Addend) >> Shift) & 0xfff : uint12
  ///
  ///   where Shift is encoded in the size field of the instruction.
  ///
  /// Errors:
  ///   - The result of the unshifted part of the fixup expression must be
  ///     aligned otherwise an alignment error will be returned.
  ///   - The result of the fixup expression must fit into a uint12 otherwise an
  ///     out-of-range error will be returned.
  PageOffset12,

  /// A GOT entry getter/constructor, transformed to Page21 pointing at the GOT
  /// entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a Page21 targeting
  /// the GOT entry for the edge's current target, maintaining the same addend.
  /// A GOT entry for the target should be created if one does not already
  /// exist.
  ///
  /// Edges of this kind are usually handled by a GOT builder pass inserted by
  /// default.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestGOTAndTransformToPage21,

  /// A GOT entry getter/constructor, transformed to Pageoffset12 pointing at
  /// the GOT entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a PageOffset12
  /// targeting the GOT entry for the edge's current target, maintaining the
  /// same addend. A GOT entry for the target should be created if one does not
  /// already exist.
  ///
  /// Edges of this kind are usually handled by a GOT builder pass inserted by
  /// default.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestGOTAndTransformToPageOffset12,

  /// A GOT entry getter/constructor, transformed to Delta32 pointing at the GOT
  /// entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a Delta32/ targeting
  /// the GOT entry for the edge's current target, maintaining the same addend.
  /// A GOT entry for the target should be created if one does not already
  /// exist.
  ///
  /// Edges of this kind are usually handled by a GOT builder pass inserted by
  /// default.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestGOTAndTransformToDelta32,

  /// A TLVP entry getter/constructor, transformed to Page21.
  ///
  /// Indicates that this edge should be transformed into a Page21 targeting the
  /// TLVP entry for the edge's current target. A TLVP entry for the target
  /// should be created if one does not already exist.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestTLVPAndTransformToPage21,

  /// A TLVP entry getter/constructor, transformed to PageOffset12.
  ///
  /// Indicates that this edge should be transformed into a PageOffset12
  /// targeting the TLVP entry for the edge's current target. A TLVP entry for
  /// the target should be created if one does not already exist.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestTLVPAndTransformToPageOffset12,

  /// A TLSDesc entry getter/constructor, transformed to Page21.
  ///
  /// Indicates that this edge should be transformed into a Page21 targeting the
  /// TLSDesc entry for the edge's current target. A TLSDesc entry for the
  /// target should be created if one does not already exist.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestTLSDescEntryAndTransformToPage21,

  /// A TLSDesc entry getter/constructor, transformed to PageOffset12.
  ///
  /// Indicates that this edge should be transformed into a PageOffset12
  /// targeting the TLSDesc entry for the edge's current target. A TLSDesc entry
  /// for the target should be created if one does not already exist.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestTLSDescEntryAndTransformToPageOffset12,
};

/// Returns a string name for the given aarch64 edge. For debugging purposes
/// only
const char *getEdgeKindName(Edge::Kind K);

// Returns whether the Instr is LD/ST (imm12)
inline bool isLoadStoreImm12(uint32_t Instr) {
  constexpr uint32_t LoadStoreImm12Mask = 0x3b000000;
  return (Instr & LoadStoreImm12Mask) == 0x39000000;
}

inline bool isTestAndBranchImm14(uint32_t Instr) {
  constexpr uint32_t TestAndBranchImm14Mask = 0x7e000000;
  return (Instr & TestAndBranchImm14Mask) == 0x36000000;
}

inline bool isCondBranchImm19(uint32_t Instr) {
  constexpr uint32_t CondBranchImm19Mask = 0xfe000000;
  return (Instr & CondBranchImm19Mask) == 0x54000000;
}

inline bool isCompAndBranchImm19(uint32_t Instr) {
  constexpr uint32_t CompAndBranchImm19Mask = 0x7e000000;
  return (Instr & CompAndBranchImm19Mask) == 0x34000000;
}

inline bool isADR(uint32_t Instr) {
  constexpr uint32_t ADRMask = 0x9f000000;
  return (Instr & ADRMask) == 0x10000000;
}

inline bool isLDRLiteral(uint32_t Instr) {
  constexpr uint32_t LDRLitMask = 0x3b000000;
  return (Instr & LDRLitMask) == 0x18000000;
}

// Returns the amount the address operand of LD/ST (imm12)
// should be shifted right by.
//
// The shift value varies by the data size of LD/ST instruction.
// For instance, LDH instructoin needs the address to be shifted
// right by 1.
inline unsigned getPageOffset12Shift(uint32_t Instr) {
  constexpr uint32_t Vec128Mask = 0x04800000;

  if (isLoadStoreImm12(Instr)) {
    uint32_t ImplicitShift = Instr >> 30;
    if (ImplicitShift == 0)
      if ((Instr & Vec128Mask) == Vec128Mask)
        ImplicitShift = 4;

    return ImplicitShift;
  }

  return 0;
}

// Returns whether the Instr is MOVK/MOVZ (imm16) with a zero immediate field
inline bool isMoveWideImm16(uint32_t Instr) {
  constexpr uint32_t MoveWideImm16Mask = 0x5f9fffe0;
  return (Instr & MoveWideImm16Mask) == 0x52800000;
}

// Returns the amount the address operand of MOVK/MOVZ (imm16)
// should be shifted right by.
//
// The shift value is specfied in the assembly as LSL #<shift>.
inline unsigned getMoveWide16Shift(uint32_t Instr) {
  if (isMoveWideImm16(Instr)) {
    uint32_t ImplicitShift = (Instr >> 21) & 0b11;
    return ImplicitShift << 4;
  }

  return 0;
}

/// Apply fixup expression for edge to block content.
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E) {
  using namespace support;

  char *BlockWorkingMem = B.getAlreadyMutableContent().data();
  char *FixupPtr = BlockWorkingMem + E.getOffset();
  orc::ExecutorAddr FixupAddress = B.getAddress() + E.getOffset();

  switch (E.getKind()) {
  case Pointer64: {
    uint64_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    *(ulittle64_t *)FixupPtr = Value;
    break;
  }
  case Pointer32: {
    uint64_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    if (Value > std::numeric_limits<uint32_t>::max())
      return makeTargetOutOfRangeError(G, B, E);
    *(ulittle32_t *)FixupPtr = Value;
    break;
  }
  case Delta32:
  case Delta64:
  case NegDelta32:
  case NegDelta64: {
    int64_t Value;
    if (E.getKind() == Delta32 || E.getKind() == Delta64)
      Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    else
      Value = FixupAddress - E.getTarget().getAddress() + E.getAddend();

    if (E.getKind() == Delta32 || E.getKind() == NegDelta32) {
      if (Value < std::numeric_limits<int32_t>::min() ||
          Value > std::numeric_limits<int32_t>::max())
        return makeTargetOutOfRangeError(G, B, E);
      *(little32_t *)FixupPtr = Value;
    } else
      *(little64_t *)FixupPtr = Value;
    break;
  }
  case Branch26PCRel: {
    assert((FixupAddress.getValue() & 0x3) == 0 &&
           "Branch-inst is not 32-bit aligned");

    int64_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();

    if (static_cast<uint64_t>(Value) & 0x3)
      return make_error<JITLinkError>("BranchPCRel26 target is not 32-bit "
                                      "aligned");

    if (Value < -(1 << 27) || Value > ((1 << 27) - 1))
      return makeTargetOutOfRangeError(G, B, E);

    uint32_t RawInstr = *(little32_t *)FixupPtr;
    assert((RawInstr & 0x7fffffff) == 0x14000000 &&
           "RawInstr isn't a B or BR immediate instruction");
    uint32_t Imm = (static_cast<uint32_t>(Value) & ((1 << 28) - 1)) >> 2;
    uint32_t FixedInstr = RawInstr | Imm;
    *(little32_t *)FixupPtr = FixedInstr;
    break;
  }
  case MoveWide16: {
    uint64_t TargetOffset =
        (E.getTarget().getAddress() + E.getAddend()).getValue();

    uint32_t RawInstr = *(ulittle32_t *)FixupPtr;
    assert(isMoveWideImm16(RawInstr) &&
           "RawInstr isn't a MOVK/MOVZ instruction");

    unsigned ImmShift = getMoveWide16Shift(RawInstr);
    uint32_t Imm = (TargetOffset >> ImmShift) & 0xffff;
    uint32_t FixedInstr = RawInstr | (Imm << 5);
    *(ulittle32_t *)FixupPtr = FixedInstr;
    break;
  }
  case LDRLiteral19: {
    assert((FixupAddress.getValue() & 0x3) == 0 && "LDR is not 32-bit aligned");
    uint32_t RawInstr = *(ulittle32_t *)FixupPtr;
    assert(isLDRLiteral(RawInstr) && "RawInstr is not an LDR Literal");
    int64_t Delta = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
    if (Delta & 0x3)
      return make_error<JITLinkError>("LDR literal target is not 32-bit "
                                      "aligned");
    if (!isInt<21>(Delta))
      return makeTargetOutOfRangeError(G, B, E);
    uint32_t EncodedImm = ((static_cast<uint32_t>(Delta) >> 2) & 0x7ffff) << 5;
    uint32_t FixedInstr = RawInstr | EncodedImm;
    *(ulittle32_t *)FixupPtr = FixedInstr;
    break;
  }
  case ADRLiteral21: {
    assert((FixupAddress.getValue() & 0x3) == 0 && "ADR is not 32-bit aligned");
    uint32_t RawInstr = *(ulittle32_t *)FixupPtr;
    assert(isADR(RawInstr) && "RawInstr is not an ADR");
    int64_t Delta = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
    if (!isInt<21>(Delta))
      return makeTargetOutOfRangeError(G, B, E);
    auto UDelta = static_cast<uint32_t>(Delta);
    uint32_t EncodedImmHi = ((UDelta >> 2) & 0x7ffff) << 5;
    uint32_t EncodedImmLo = (UDelta & 0x3) << 29;
    uint32_t FixedInstr = RawInstr | EncodedImmHi | EncodedImmLo;
    *(ulittle32_t *)FixupPtr = FixedInstr;
    break;
  }
  case TestAndBranch14PCRel: {
    assert((FixupAddress.getValue() & 0x3) == 0 &&
           "Test and branch is not 32-bit aligned");
    uint32_t RawInstr = *(ulittle32_t *)FixupPtr;
    assert(isTestAndBranchImm14(RawInstr) &&
           "RawInstr is not a test and branch");
    int64_t Delta = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
    if (Delta & 0x3)
      return make_error<JITLinkError>(
          "Test and branch literal target is not 32-bit aligned");
    if (!isInt<16>(Delta))
      return makeTargetOutOfRangeError(G, B, E);
    uint32_t EncodedImm = ((static_cast<uint32_t>(Delta) >> 2) & 0x3fff) << 5;
    uint32_t FixedInstr = RawInstr | EncodedImm;
    *(ulittle32_t *)FixupPtr = FixedInstr;
    break;
  }
  case CondBranch19PCRel: {
    assert((FixupAddress.getValue() & 0x3) == 0 &&
           "Conditional branch is not 32-bit aligned");
    uint32_t RawInstr = *(ulittle32_t *)FixupPtr;
    assert((isCondBranchImm19(RawInstr) || isCompAndBranchImm19(RawInstr)) &&
           "RawInstr is not a conditional branch");
    int64_t Delta = E.getTarget().getAddress() + E.getAddend() - FixupAddress;
    if (Delta & 0x3)
      return make_error<JITLinkError>(
          "Conditional branch literal target is not 32-bit "
          "aligned");
    if (!isInt<21>(Delta))
      return makeTargetOutOfRangeError(G, B, E);
    uint32_t EncodedImm = ((static_cast<uint32_t>(Delta) >> 2) & 0x7ffff) << 5;
    uint32_t FixedInstr = RawInstr | EncodedImm;
    *(ulittle32_t *)FixupPtr = FixedInstr;
    break;
  }
  case Page21: {
    uint64_t TargetPage =
        (E.getTarget().getAddress().getValue() + E.getAddend()) &
        ~static_cast<uint64_t>(4096 - 1);
    uint64_t PCPage =
        FixupAddress.getValue() & ~static_cast<uint64_t>(4096 - 1);

    int64_t PageDelta = TargetPage - PCPage;
    if (!isInt<33>(PageDelta))
      return makeTargetOutOfRangeError(G, B, E);

    uint32_t RawInstr = *(ulittle32_t *)FixupPtr;
    assert((RawInstr & 0xffffffe0) == 0x90000000 &&
           "RawInstr isn't an ADRP instruction");
    uint32_t ImmLo = (static_cast<uint64_t>(PageDelta) >> 12) & 0x3;
    uint32_t ImmHi = (static_cast<uint64_t>(PageDelta) >> 14) & 0x7ffff;
    uint32_t FixedInstr = RawInstr | (ImmLo << 29) | (ImmHi << 5);
    *(ulittle32_t *)FixupPtr = FixedInstr;
    break;
  }
  case PageOffset12: {
    uint64_t TargetOffset =
        (E.getTarget().getAddress() + E.getAddend()).getValue() & 0xfff;

    uint32_t RawInstr = *(ulittle32_t *)FixupPtr;
    unsigned ImmShift = getPageOffset12Shift(RawInstr);

    if (TargetOffset & ((1 << ImmShift) - 1))
      return make_error<JITLinkError>("PAGEOFF12 target is not aligned");

    uint32_t EncodedImm = (TargetOffset >> ImmShift) << 10;
    uint32_t FixedInstr = RawInstr | EncodedImm;
    *(ulittle32_t *)FixupPtr = FixedInstr;
    break;
  }
  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " unsupported edge kind " + getEdgeKindName(E.getKind()));
  }

  return Error::success();
}

/// aarch64 pointer size.
constexpr uint64_t PointerSize = 8;

/// AArch64 null pointer content.
extern const char NullPointerContent[PointerSize];

/// AArch64 pointer jump stub content.
///
/// Contains the instruction sequence for an indirect jump via an in-memory
/// pointer:
///   ADRP x16, ptr@page21
///   LDR  x16, [x16, ptr@pageoff12]
///   BR   x16
extern const char PointerJumpStubContent[12];

/// Creates a new pointer block in the given section and returns an
/// Anonymous symbol pointing to it.
///
/// If InitialTarget is given then an Pointer64 relocation will be added to the
/// block pointing at InitialTarget.
///
/// The pointer block will have the following default values:
///   alignment: 64-bit
///   alignment-offset: 0
///   address: highest allowable (~7U)
inline Symbol &createAnonymousPointer(LinkGraph &G, Section &PointerSection,
                                      Symbol *InitialTarget = nullptr,
                                      uint64_t InitialAddend = 0) {
  auto &B = G.createContentBlock(PointerSection, NullPointerContent,
                                 orc::ExecutorAddr(~uint64_t(7)), 8, 0);
  if (InitialTarget)
    B.addEdge(Pointer64, 0, *InitialTarget, InitialAddend);
  return G.addAnonymousSymbol(B, 0, 8, false, false);
}

/// Create a jump stub block that jumps via the pointer at the given symbol.
///
/// The stub block will have the following default values:
///   alignment: 32-bit
///   alignment-offset: 0
///   address: highest allowable: (~11U)
inline Block &createPointerJumpStubBlock(LinkGraph &G, Section &StubSection,
                                         Symbol &PointerSymbol) {
  auto &B = G.createContentBlock(StubSection, PointerJumpStubContent,
                                 orc::ExecutorAddr(~uint64_t(11)), 4, 0);
  B.addEdge(Page21, 0, PointerSymbol, 0);
  B.addEdge(PageOffset12, 4, PointerSymbol, 0);
  return B;
}

/// Create a jump stub that jumps via the pointer at the given symbol and
/// an anonymous symbol pointing to it. Return the anonymous symbol.
///
/// The stub block will be created by createPointerJumpStubBlock.
inline Symbol &createAnonymousPointerJumpStub(LinkGraph &G,
                                              Section &StubSection,
                                              Symbol &PointerSymbol) {
  return G.addAnonymousSymbol(
      createPointerJumpStubBlock(G, StubSection, PointerSymbol), 0,
      sizeof(PointerJumpStubContent), true, false);
}

/// Global Offset Table Builder.
class GOTTableManager : public TableManager<GOTTableManager> {
public:
  static StringRef getSectionName() { return "$__GOT"; }

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    Edge::Kind KindToSet = Edge::Invalid;
    const char *BlockWorkingMem = B->getContent().data();
    const char *FixupPtr = BlockWorkingMem + E.getOffset();

    switch (E.getKind()) {
    case aarch64::RequestGOTAndTransformToPage21:
    case aarch64::RequestTLVPAndTransformToPage21: {
      KindToSet = aarch64::Page21;
      break;
    }
    case aarch64::RequestGOTAndTransformToPageOffset12:
    case aarch64::RequestTLVPAndTransformToPageOffset12: {
      KindToSet = aarch64::PageOffset12;
      uint32_t RawInstr = *(const support::ulittle32_t *)FixupPtr;
      (void)RawInstr;
      assert(E.getAddend() == 0 &&
             "GOTPageOffset12/TLVPageOffset12 with non-zero addend");
      assert((RawInstr & 0xfffffc00) == 0xf9400000 &&
             "RawInstr isn't a 64-bit LDR immediate");
      break;
    }
    case aarch64::RequestGOTAndTransformToDelta32: {
      KindToSet = aarch64::Delta32;
      break;
    }
    default:
      return false;
    }
    assert(KindToSet != Edge::Invalid &&
           "Fell through switch, but no new kind to set");
    DEBUG_WITH_TYPE("jitlink", {
      dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind()) << " edge at "
             << B->getFixupAddress(E) << " (" << B->getAddress() << " + "
             << formatv("{0:x}", E.getOffset()) << ")\n";
    });
    E.setKind(KindToSet);
    E.setTarget(getEntryForTarget(G, E.getTarget()));
    return true;
  }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointer(G, getGOTSection(G), &Target);
  }

private:
  Section &getGOTSection(LinkGraph &G) {
    if (!GOTSection)
      GOTSection = &G.createSection(getSectionName(),
                                    orc::MemProt::Read | orc::MemProt::Exec);
    return *GOTSection;
  }

  Section *GOTSection = nullptr;
};

/// Procedure Linkage Table Builder.
class PLTTableManager : public TableManager<PLTTableManager> {
public:
  PLTTableManager(GOTTableManager &GOT) : GOT(GOT) {}

  static StringRef getSectionName() { return "$__STUBS"; }

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    if (E.getKind() == aarch64::Branch26PCRel && !E.getTarget().isDefined()) {
      DEBUG_WITH_TYPE("jitlink", {
        dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind()) << " edge at "
               << B->getFixupAddress(E) << " (" << B->getAddress() << " + "
               << formatv("{0:x}", E.getOffset()) << ")\n";
      });
      E.setTarget(getEntryForTarget(G, E.getTarget()));
      return true;
    }
    return false;
  }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    return createAnonymousPointerJumpStub(G, getStubsSection(G),
                                          GOT.getEntryForTarget(G, Target));
  }

public:
  Section &getStubsSection(LinkGraph &G) {
    if (!StubsSection)
      StubsSection = &G.createSection(getSectionName(),
                                      orc::MemProt::Read | orc::MemProt::Exec);
    return *StubsSection;
  }

  GOTTableManager &GOT;
  Section *StubsSection = nullptr;
};

} // namespace aarch64
} // namespace jitlink
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_AARCH64_H
