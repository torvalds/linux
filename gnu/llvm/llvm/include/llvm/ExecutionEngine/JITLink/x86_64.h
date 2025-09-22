//===-- x86_64.h - Generic JITLink x86-64 edge kinds, utilities -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing x86-64 objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_X86_64_H
#define LLVM_EXECUTIONENGINE_JITLINK_X86_64_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/TableManager.h"

namespace llvm {
namespace jitlink {
namespace x86_64 {

/// Represents x86-64 fixups and other x86-64-specific edge kinds.
enum EdgeKind_x86_64 : Edge::Kind {

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

  /// A signed 32-bit pointer value relocation
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : int32
  ///
  /// Errors:
  ///   - The target must reside in the signed 32-bits([-2**31, 2**32 - 1]) of
  ///   the address space, otherwise an out-of-range error will be returned.
  Pointer32Signed,

  /// A plain 16-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint16
  ///
  /// Errors:
  ///   - The target must reside in the low 16-bits of the address space,
  ///     otherwise an out-of-range error will be returned.
  ///
  Pointer16,

  /// A plain 8-bit pointer value relocation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target + Addend : uint8
  ///
  /// Errors:
  ///   - The target must reside in the low 8-bits of the address space,
  ///     otherwise an out-of-range error will be returned.
  ///
  Pointer8,

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
  ///   Fixup <- Target - Fixup + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  Delta32,

  /// An 8-bit delta.
  ///
  /// Delta from the fixup to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend : int8
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int8, otherwise
  ///     an out-of-range error will be returned.
  ///
  Delta8,

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

  /// A 64-bit GOT delta.
  ///
  /// Delta from the global offset table to the target
  ///
  /// Fixup expression:
  ///   Fixup <- Target - GOTSymbol + Addend : int64
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to a null pointer GOTSymbol, which the GOT section
  ///     symbol was not been defined.
  Delta64FromGOT,

  /// A 32-bit PC-relative branch.
  ///
  /// Represents a PC-relative call or branch to a target. This can be used to
  /// identify, record, and/or patch call sites.
  ///
  /// The fixup expression for this kind includes an implicit offset to account
  /// for the PC (unlike the Delta edges) so that a Branch32PCRel with a target
  /// T and addend zero is a call/branch to the start (offset zero) of T.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - (Fixup + 4) + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  BranchPCRel32,

  /// A 32-bit PC-relative relocation.
  ///
  /// Represents a data/control flow instruction using PC-relative addressing
  /// to a target.
  ///
  /// The fixup expression for this kind includes an implicit offset to account
  /// for the PC (unlike the Delta edges) so that a PCRel32 with a target
  /// T and addend zero is a call/branch to the start (offset zero) of T.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - (Fixup + 4) + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  PCRel32,

  /// A 32-bit PC-relative branch to a pointer jump stub.
  ///
  /// The target of this relocation should be a pointer jump stub of the form:
  ///
  /// \code{.s}
  ///   .text
  ///   jmpq *tgtptr(%rip)
  ///   ; ...
  ///
  ///   .data
  ///   tgtptr:
  ///     .quad 0
  /// \endcode
  ///
  /// This edge kind has the same fixup expression as BranchPCRel32, but further
  /// identifies the call/branch as being to a pointer jump stub. For edges of
  /// this kind the jump stub should not be bypassed (use
  /// BranchPCRel32ToPtrJumpStubBypassable for that), but the pointer location
  /// target may be recorded to allow manipulation at runtime.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend - 4 : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  BranchPCRel32ToPtrJumpStub,

  /// A relaxable version of BranchPCRel32ToPtrJumpStub.
  ///
  /// The edge kind has the same fixup expression as BranchPCRel32ToPtrJumpStub,
  /// but identifies the call/branch as being to a pointer jump stub that may be
  /// bypassed with a direct jump to the ultimate target if the ultimate target
  /// is within range of the fixup location.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - Fixup + Addend - 4: int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  BranchPCRel32ToPtrJumpStubBypassable,

  /// A GOT entry getter/constructor, transformed to Delta32 pointing at the GOT
  /// entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a Delta32 targeting
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

  /// A GOT entry getter/constructor, transformed to Delta64 pointing at the GOT
  /// entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a Delta64 targeting
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
  RequestGOTAndTransformToDelta64,

  /// A GOT entry offset within GOT getter/constructor, transformed to
  /// Delta64FromGOT
  /// pointing at the GOT entry for the original target
  ///
  /// Indicates that this edge should be transformed into a Delta64FromGOT
  /// targeting
  /// the GOT entry for the edge's current target, maintaining the same addend.
  /// A GOT entry for the target should be created if one does not already
  /// exist.
  ///
  /// Edges of this kind are usually handled by a GOT builder pass inserted by
  /// default
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase
  RequestGOTAndTransformToDelta64FromGOT,

  /// A PC-relative load of a GOT entry, relaxable if GOT entry target is
  /// in-range of the fixup
  ///
  /// TODO: Explain the optimization
  ///
  /// Fixup expression
  ///   Fixup <- Target - (Fixup + 4) + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  //
  PCRel32GOTLoadRelaxable,

  /// A PC-relative REX load of a GOT entry, relaxable if GOT entry target
  /// is in-range of the fixup.
  ///
  /// If the GOT entry target is in-range of the fixup then the load from the
  /// GOT may be replaced with a direct memory address calculation.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - (Fixup + 4) + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///
  PCRel32GOTLoadREXRelaxable,

  /// A GOT entry getter/constructor, transformed to
  /// PCRel32ToGOTLoadREXRelaxable pointing at the GOT entry for the original
  /// target.
  ///
  /// Indicates that this edge should be lowered to a PC32ToGOTLoadREXRelaxable
  /// targeting the GOT entry for the edge's current target, maintaining the
  /// same addend. A GOT entry for the target should be created if one does not
  /// already exist.
  ///
  /// Edges of this kind are usually lowered by a GOT builder pass inserted by
  /// default.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestGOTAndTransformToPCRel32GOTLoadREXRelaxable,

  /// A GOT entry getter/constructor, transformed to
  /// PCRel32ToGOTLoadRelaxable pointing at the GOT entry for the original
  /// target.
  ///
  /// Indicates that this edge should be lowered to a PC32ToGOTLoadRelaxable
  /// targeting the GOT entry for the edge's current target, maintaining the
  /// same addend. A GOT entry for the target should be created if one does not
  /// already exist.
  ///
  /// Edges of this kind are usually lowered by a GOT builder pass inserted by
  /// default.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestGOTAndTransformToPCRel32GOTLoadRelaxable,

  /// A PC-relative REX load of a Thread Local Variable Pointer (TLVP) entry,
  /// relaxable if the TLVP entry target is in-range of the fixup.
  ///
  /// If the TLVP entry target is in-range of the fixup then the load from the
  /// TLVP may be replaced with a direct memory address calculation.
  ///
  /// The target of this edge must be a thread local variable entry of the form
  ///   .quad <tlv getter thunk>
  ///   .quad <tlv key>
  ///   .quad <tlv initializer>
  ///
  /// Fixup expression:
  ///   Fixup <- Target - (Fixup + 4) + Addend : int32
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int32, otherwise
  ///     an out-of-range error will be returned.
  ///   - The target must be either external, or a TLV entry of the required
  ///     form, otherwise a malformed TLV entry error will be returned.
  ///
  PCRel32TLVPLoadREXRelaxable,

  /// TODO: Explain the generic edge kind
  RequestTLSDescInGOTAndTransformToDelta32,

  /// A TLVP entry getter/constructor, transformed to
  /// Delta32ToTLVPLoadREXRelaxable.
  ///
  /// Indicates that this edge should be transformed into a
  /// Delta32ToTLVPLoadREXRelaxable targeting the TLVP entry for the edge's
  /// current target. A TLVP entry for the target should be created if one does
  /// not already exist.
  ///
  /// Fixup expression:
  ///   NONE
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to handle edges of this kind prior to the fixup
  ///     phase will result in an assert/unreachable during the fixup phase.
  ///
  RequestTLVPAndTransformToPCRel32TLVPLoadREXRelaxable,
  // First platform specific relocation.
  FirstPlatformRelocation
};

/// Returns a string name for the given x86-64 edge. For debugging purposes
/// only.
const char *getEdgeKindName(Edge::Kind K);

/// Apply fixup expression for edge to block content.
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E,
                        const Symbol *GOTSymbol) {
  using namespace support;

  char *BlockWorkingMem = B.getAlreadyMutableContent().data();
  char *FixupPtr = BlockWorkingMem + E.getOffset();
  auto FixupAddress = B.getAddress() + E.getOffset();

  switch (E.getKind()) {

  case Pointer64: {
    uint64_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    *(ulittle64_t *)FixupPtr = Value;
    break;
  }

  case Pointer32: {
    uint64_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    if (LLVM_LIKELY(isUInt<32>(Value)))
      *(ulittle32_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }
  case Pointer32Signed: {
    int64_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    if (LLVM_LIKELY(isInt<32>(Value)))
      *(little32_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case Pointer16: {
    uint64_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    if (LLVM_LIKELY(isUInt<16>(Value)))
      *(ulittle16_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case Pointer8: {
    uint64_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    if (LLVM_LIKELY(isUInt<8>(Value)))
      *(uint8_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case PCRel32:
  case BranchPCRel32:
  case BranchPCRel32ToPtrJumpStub:
  case BranchPCRel32ToPtrJumpStubBypassable:
  case PCRel32GOTLoadRelaxable:
  case PCRel32GOTLoadREXRelaxable:
  case PCRel32TLVPLoadREXRelaxable: {
    int64_t Value =
        E.getTarget().getAddress() - (FixupAddress + 4) + E.getAddend();
    if (LLVM_LIKELY(isInt<32>(Value)))
      *(little32_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case Delta64: {
    int64_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    *(little64_t *)FixupPtr = Value;
    break;
  }

  case Delta32: {
    int64_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    if (LLVM_LIKELY(isInt<32>(Value)))
      *(little32_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case Delta8: {
    int64_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    if (LLVM_LIKELY(isInt<8>(Value)))
      *FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case NegDelta64: {
    int64_t Value = FixupAddress - E.getTarget().getAddress() + E.getAddend();
    *(little64_t *)FixupPtr = Value;
    break;
  }

  case NegDelta32: {
    int64_t Value = FixupAddress - E.getTarget().getAddress() + E.getAddend();
    if (LLVM_LIKELY(isInt<32>(Value)))
      *(little32_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }
  case Delta64FromGOT: {
    assert(GOTSymbol && "No GOT section symbol");
    int64_t Value =
        E.getTarget().getAddress() - GOTSymbol->getAddress() + E.getAddend();
    *(little64_t *)FixupPtr = Value;
    break;
  }

  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " unsupported edge kind " + getEdgeKindName(E.getKind()));
  }

  return Error::success();
}

/// x86_64 pointer size.
constexpr uint64_t PointerSize = 8;

/// x86-64 null pointer content.
extern const char NullPointerContent[PointerSize];

/// x86-64 pointer jump stub content.
///
/// Contains the instruction sequence for an indirect jump via an in-memory
/// pointer:
///   jmpq *ptr(%rip)
extern const char PointerJumpStubContent[6];

/// Creates a new pointer block in the given section and returns an anonymous
/// symbol pointing to it.
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
///   alignment: 8-bit
///   alignment-offset: 0
///   address: highest allowable: (~5U)
inline Block &createPointerJumpStubBlock(LinkGraph &G, Section &StubSection,
                                         Symbol &PointerSymbol) {
  auto &B = G.createContentBlock(StubSection, PointerJumpStubContent,
                                 orc::ExecutorAddr(~uint64_t(5)), 1, 0);
  B.addEdge(BranchPCRel32, 2, PointerSymbol, 0);
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
      createPointerJumpStubBlock(G, StubSection, PointerSymbol), 0, 6, true,
      false);
}

/// Global Offset Table Builder.
class GOTTableManager : public TableManager<GOTTableManager> {
public:
  static StringRef getSectionName() { return "$__GOT"; }

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    Edge::Kind KindToSet = Edge::Invalid;
    switch (E.getKind()) {
    case x86_64::Delta64FromGOT: {
      // we need to make sure that the GOT section exists, but don't otherwise
      // need to fix up this edge
      getGOTSection(G);
      return false;
    }
    case x86_64::RequestGOTAndTransformToPCRel32GOTLoadREXRelaxable:
      KindToSet = x86_64::PCRel32GOTLoadREXRelaxable;
      break;
    case x86_64::RequestGOTAndTransformToPCRel32GOTLoadRelaxable:
      KindToSet = x86_64::PCRel32GOTLoadRelaxable;
      break;
    case x86_64::RequestGOTAndTransformToDelta64:
      KindToSet = x86_64::Delta64;
      break;
    case x86_64::RequestGOTAndTransformToDelta64FromGOT:
      KindToSet = x86_64::Delta64FromGOT;
      break;
    case x86_64::RequestGOTAndTransformToDelta32:
      KindToSet = x86_64::Delta32;
      break;
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
      GOTSection = &G.createSection(getSectionName(), orc::MemProt::Read);
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
    if (E.getKind() == x86_64::BranchPCRel32 && !E.getTarget().isDefined()) {
      DEBUG_WITH_TYPE("jitlink", {
        dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind()) << " edge at "
               << B->getFixupAddress(E) << " (" << B->getAddress() << " + "
               << formatv("{0:x}", E.getOffset()) << ")\n";
      });
      // Set the edge kind to Branch32ToPtrJumpStubBypassable to enable it to
      // be optimized when the target is in-range.
      E.setKind(x86_64::BranchPCRel32ToPtrJumpStubBypassable);
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
    if (!PLTSection)
      PLTSection = &G.createSection(getSectionName(),
                                    orc::MemProt::Read | orc::MemProt::Exec);
    return *PLTSection;
  }

  GOTTableManager &GOT;
  Section *PLTSection = nullptr;
};

/// Optimize the GOT and Stub relocations if the edge target address is in range
/// 1. PCRel32GOTLoadRelaxable. For this edge kind, if the target is in range,
/// then replace GOT load with lea
/// 2. BranchPCRel32ToPtrJumpStubRelaxable. For this edge kind, if the target is
/// in range, replace a indirect jump by plt stub with a direct jump to the
/// target
Error optimizeGOTAndStubAccesses(LinkGraph &G);

} // namespace x86_64
} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_X86_64_H
