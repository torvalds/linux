//=== i386.h - Generic JITLink i386 edge kinds, utilities -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing i386 objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_I386_H
#define LLVM_EXECUTIONENGINE_JITLINK_I386_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/TableManager.h"

namespace llvm::jitlink::i386 {
/// Represets i386 fixups
enum EdgeKind_i386 : Edge::Kind {

  /// None
  None = Edge::FirstRelocation,

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

  /// A 16-bit PC-relative relocation.
  ///
  /// Represents a data/control flow instruction using PC-relative addressing
  /// to a target.
  ///
  /// The fixup expression for this kind includes an implicit offset to account
  /// for the PC (unlike the Delta edges) so that a PCRel16 with a target
  /// T and addend zero is a call/branch to the start (offset zero) of T.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - (Fixup + 4) + Addend : int16
  ///
  /// Errors:
  ///   - The result of the fixup expression must fit into an int16, otherwise
  ///     an out-of-range error will be returned.
  ///
  PCRel16,

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
  Delta32,

  /// A 32-bit GOT delta.
  ///
  /// Delta from the global offset table to the target.
  ///
  /// Fixup expression:
  ///   Fixup <- Target - GOTSymbol + Addend : int32
  ///
  /// Errors:
  ///   - *ASSERTION* Failure to a null pointer GOTSymbol, which the GOT section
  ///     symbol was not been defined.
  Delta32FromGOT,

  /// A GOT entry offset within GOT getter/constructor, transformed to
  /// Delta32FromGOT pointing at the GOT entry for the original target.
  ///
  /// Indicates that this edge should be transformed into a Delta32FromGOT
  /// targeting the GOT entry for the edge's current target, maintaining the
  /// same addend.
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
  RequestGOTAndTransformToDelta32FromGOT,

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

  /// A 32-bit PC-relative branch to a pointer jump stub.
  ///
  /// The target of this relocation should be a pointer jump stub of the form:
  ///
  /// \code{.s}
  ///   .text
  ///   jmp *tgtptr
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
};

/// Returns a string name for the given i386 edge. For debugging purposes
/// only
const char *getEdgeKindName(Edge::Kind K);

/// Apply fixup expression for edge to block content.
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E,
                        const Symbol *GOTSymbol) {
  using namespace i386;
  using namespace llvm::support;

  char *BlockWorkingMem = B.getAlreadyMutableContent().data();
  char *FixupPtr = BlockWorkingMem + E.getOffset();
  auto FixupAddress = B.getAddress() + E.getOffset();

  switch (E.getKind()) {
  case i386::None: {
    break;
  }

  case i386::Pointer32: {
    uint32_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    *(ulittle32_t *)FixupPtr = Value;
    break;
  }

  case i386::PCRel32: {
    int32_t Value =
        E.getTarget().getAddress() - (FixupAddress + 4) + E.getAddend();
    *(little32_t *)FixupPtr = Value;
    break;
  }

  case i386::Pointer16: {
    uint32_t Value = E.getTarget().getAddress().getValue() + E.getAddend();
    if (LLVM_LIKELY(isUInt<16>(Value)))
      *(ulittle16_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case i386::PCRel16: {
    int32_t Value =
        E.getTarget().getAddress() - (FixupAddress + 4) + E.getAddend();
    if (LLVM_LIKELY(isInt<16>(Value)))
      *(little16_t *)FixupPtr = Value;
    else
      return makeTargetOutOfRangeError(G, B, E);
    break;
  }

  case i386::Delta32: {
    int32_t Value = E.getTarget().getAddress() - FixupAddress + E.getAddend();
    *(little32_t *)FixupPtr = Value;
    break;
  }

  case i386::Delta32FromGOT: {
    assert(GOTSymbol && "No GOT section symbol");
    int32_t Value =
        E.getTarget().getAddress() - GOTSymbol->getAddress() + E.getAddend();
    *(little32_t *)FixupPtr = Value;
    break;
  }

  case i386::BranchPCRel32:
  case i386::BranchPCRel32ToPtrJumpStub:
  case i386::BranchPCRel32ToPtrJumpStubBypassable: {
    int32_t Value =
        E.getTarget().getAddress() - (FixupAddress + 4) + E.getAddend();
    *(little32_t *)FixupPtr = Value;
    break;
  }

  default:
    return make_error<JITLinkError>(
        "In graph " + G.getName() + ", section " + B.getSection().getName() +
        " unsupported edge kind " + getEdgeKindName(E.getKind()));
  }

  return Error::success();
}

/// i386 pointer size.
constexpr uint32_t PointerSize = 4;

/// i386 null pointer content.
extern const char NullPointerContent[PointerSize];

/// i386 pointer jump stub content.
///
/// Contains the instruction sequence for an indirect jump via an in-memory
/// pointer:
///   jmpq *ptr
extern const char PointerJumpStubContent[6];

/// Creates a new pointer block in the given section and returns an anonymous
/// symbol pointing to it.
///
/// If InitialTarget is given then an Pointer32 relocation will be added to the
/// block pointing at InitialTarget.
///
/// The pointer block will have the following default values:
///   alignment: 32-bit
///   alignment-offset: 0
///   address: highest allowable (~7U)
inline Symbol &createAnonymousPointer(LinkGraph &G, Section &PointerSection,
                                      Symbol *InitialTarget = nullptr,
                                      uint64_t InitialAddend = 0) {
  auto &B = G.createContentBlock(PointerSection, NullPointerContent,
                                 orc::ExecutorAddr(), 8, 0);
  if (InitialTarget)
    B.addEdge(Pointer32, 0, *InitialTarget, InitialAddend);
  return G.addAnonymousSymbol(B, 0, PointerSize, false, false);
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
                                 orc::ExecutorAddr(), 8, 0);
  B.addEdge(Pointer32,
            // Offset is 2 because the the first 2 bytes of the
            // jump stub block are {0xff, 0x25} -- an indirect absolute
            // jump.
            2, PointerSymbol, 0);
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
    case i386::Delta32FromGOT: {
      // we need to make sure that the GOT section exists, but don't otherwise
      // need to fix up this edge
      getGOTSection(G);
      return false;
    }
    case i386::RequestGOTAndTransformToDelta32FromGOT:
      KindToSet = i386::Delta32FromGOT;
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
    if (E.getKind() == i386::BranchPCRel32 && !E.getTarget().isDefined()) {
      DEBUG_WITH_TYPE("jitlink", {
        dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind()) << " edge at "
               << B->getFixupAddress(E) << " (" << B->getAddress() << " + "
               << formatv("{0:x}", E.getOffset()) << ")\n";
      });
      // Set the edge kind to Branch32ToPtrJumpStubBypassable to enable it to
      // be optimized when the target is in-range.
      E.setKind(i386::BranchPCRel32ToPtrJumpStubBypassable);
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
/// then replace GOT load with lea. (THIS IS UNIMPLEMENTED RIGHT NOW!)
/// 2. BranchPCRel32ToPtrJumpStubRelaxable. For this edge kind, if the target is
/// in range, replace a indirect jump by plt stub with a direct jump to the
/// target
Error optimizeGOTAndStubAccesses(LinkGraph &G);

} // namespace llvm::jitlink::i386

#endif // LLVM_EXECUTIONENGINE_JITLINK_I386_H
