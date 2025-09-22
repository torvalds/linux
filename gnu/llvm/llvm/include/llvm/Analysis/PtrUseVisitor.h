//===- PtrUseVisitor.h - InstVisitors over a pointers uses ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides a collection of visitors which walk the (instruction)
/// uses of a pointer. These visitors all provide the same essential behavior
/// as an InstVisitor with similar template-based flexibility and
/// implementation strategies.
///
/// These can be used, for example, to quickly analyze the uses of an alloca,
/// global variable, or function argument.
///
/// FIXME: Provide a variant which doesn't track offsets and is cheaper.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PTRUSEVISITOR_H
#define LLVM_ANALYSIS_PTRUSEVISITOR_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IntrinsicInst.h"
#include <cassert>
#include <type_traits>

namespace llvm {
class DataLayout;
class Use;

namespace detail {

/// Implementation of non-dependent functionality for \c PtrUseVisitor.
///
/// See \c PtrUseVisitor for the public interface and detailed comments about
/// usage. This class is just a helper base class which is not templated and
/// contains all common code to be shared between different instantiations of
/// PtrUseVisitor.
class PtrUseVisitorBase {
public:
  /// This class provides information about the result of a visit.
  ///
  /// After walking all the users (recursively) of a pointer, the basic
  /// infrastructure records some commonly useful information such as escape
  /// analysis and whether the visit completed or aborted early.
  class PtrInfo {
  public:
    PtrInfo() : AbortedInfo(nullptr, false), EscapedInfo(nullptr, false) {}

    /// Reset the pointer info, clearing all state.
    void reset() {
      AbortedInfo.setPointer(nullptr);
      AbortedInfo.setInt(false);
      EscapedInfo.setPointer(nullptr);
      EscapedInfo.setInt(false);
    }

    /// Did we abort the visit early?
    bool isAborted() const { return AbortedInfo.getInt(); }

    /// Is the pointer escaped at some point?
    bool isEscaped() const { return EscapedInfo.getInt(); }

    /// Get the instruction causing the visit to abort.
    /// \returns a pointer to the instruction causing the abort if one is
    /// available; otherwise returns null.
    Instruction *getAbortingInst() const { return AbortedInfo.getPointer(); }

    /// Get the instruction causing the pointer to escape.
    /// \returns a pointer to the instruction which escapes the pointer if one
    /// is available; otherwise returns null.
    Instruction *getEscapingInst() const { return EscapedInfo.getPointer(); }

    /// Mark the visit as aborted. Intended for use in a void return.
    /// \param I The instruction which caused the visit to abort, if available.
    void setAborted(Instruction *I = nullptr) {
      AbortedInfo.setInt(true);
      AbortedInfo.setPointer(I);
    }

    /// Mark the pointer as escaped. Intended for use in a void return.
    /// \param I The instruction which escapes the pointer, if available.
    void setEscaped(Instruction *I = nullptr) {
      EscapedInfo.setInt(true);
      EscapedInfo.setPointer(I);
    }

    /// Mark the pointer as escaped, and the visit as aborted. Intended
    /// for use in a void return.
    /// \param I The instruction which both escapes the pointer and aborts the
    /// visit, if available.
    void setEscapedAndAborted(Instruction *I = nullptr) {
      setEscaped(I);
      setAborted(I);
    }

  private:
    PointerIntPair<Instruction *, 1, bool> AbortedInfo, EscapedInfo;
  };

protected:
  const DataLayout &DL;

  /// \name Visitation infrastructure
  /// @{

  /// The info collected about the pointer being visited thus far.
  PtrInfo PI;

  /// A struct of the data needed to visit a particular use.
  ///
  /// This is used to maintain a worklist fo to-visit uses. This is used to
  /// make the visit be iterative rather than recursive.
  struct UseToVisit {
    using UseAndIsOffsetKnownPair = PointerIntPair<Use *, 1, bool>;

    UseAndIsOffsetKnownPair UseAndIsOffsetKnown;
    APInt Offset;
  };

  /// The worklist of to-visit uses.
  SmallVector<UseToVisit, 8> Worklist;

  /// A set of visited uses to break cycles in unreachable code.
  SmallPtrSet<Use *, 8> VisitedUses;

  /// @}

  /// \name Per-visit state
  /// This state is reset for each instruction visited.
  /// @{

  /// The use currently being visited.
  Use *U;

  /// True if we have a known constant offset for the use currently
  /// being visited.
  bool IsOffsetKnown;

  /// The constant offset of the use if that is known.
  APInt Offset;

  /// @}

  /// Note that the constructor is protected because this class must be a base
  /// class, we can't create instances directly of this class.
  PtrUseVisitorBase(const DataLayout &DL) : DL(DL) {}

  /// Enqueue the users of this instruction in the visit worklist.
  ///
  /// This will visit the users with the same offset of the current visit
  /// (including an unknown offset if that is the current state).
  void enqueueUsers(Instruction &I);

  /// Walk the operands of a GEP and adjust the offset as appropriate.
  ///
  /// This routine does the heavy lifting of the pointer walk by computing
  /// offsets and looking through GEPs.
  bool adjustOffsetForGEP(GetElementPtrInst &GEPI);
};

} // end namespace detail

/// A base class for visitors over the uses of a pointer value.
///
/// Once constructed, a user can call \c visit on a pointer value, and this
/// will walk its uses and visit each instruction using an InstVisitor. It also
/// provides visit methods which will recurse through any pointer-to-pointer
/// transformations such as GEPs and bitcasts.
///
/// During the visit, the current Use* being visited is available to the
/// subclass, as well as the current offset from the original base pointer if
/// known.
///
/// The recursive visit of uses is accomplished with a worklist, so the only
/// ordering guarantee is that an instruction is visited before any uses of it
/// are visited. Note that this does *not* mean before any of its users are
/// visited! This is because users can be visited multiple times due to
/// multiple, different uses of pointers derived from the same base.
///
/// A particular Use will only be visited once, but a User may be visited
/// multiple times, once per Use. This visits may notably have different
/// offsets.
///
/// All visit methods on the underlying InstVisitor return a boolean. This
/// return short-circuits the visit, stopping it immediately.
///
/// FIXME: Generalize this for all values rather than just instructions.
template <typename DerivedT>
class PtrUseVisitor : protected InstVisitor<DerivedT>,
                      public detail::PtrUseVisitorBase {
  friend class InstVisitor<DerivedT>;

  using Base = InstVisitor<DerivedT>;

public:
  PtrUseVisitor(const DataLayout &DL) : PtrUseVisitorBase(DL) {
    static_assert(std::is_base_of<PtrUseVisitor, DerivedT>::value,
                  "Must pass the derived type to this template!");
  }

  /// Recursively visit the uses of the given pointer.
  /// \returns An info struct about the pointer. See \c PtrInfo for details.
  PtrInfo visitPtr(Instruction &I) {
    // This must be a pointer type. Get an integer type suitable to hold
    // offsets on this pointer.
    // FIXME: Support a vector of pointers.
    assert(I.getType()->isPointerTy());
    IntegerType *IntIdxTy = cast<IntegerType>(DL.getIndexType(I.getType()));
    IsOffsetKnown = true;
    Offset = APInt(IntIdxTy->getBitWidth(), 0);
    PI.reset();

    // Enqueue the uses of this pointer.
    enqueueUsers(I);

    // Visit all the uses off the worklist until it is empty.
    while (!Worklist.empty()) {
      UseToVisit ToVisit = Worklist.pop_back_val();
      U = ToVisit.UseAndIsOffsetKnown.getPointer();
      IsOffsetKnown = ToVisit.UseAndIsOffsetKnown.getInt();
      if (IsOffsetKnown)
        Offset = std::move(ToVisit.Offset);

      Instruction *I = cast<Instruction>(U->getUser());
      static_cast<DerivedT*>(this)->visit(I);
      if (PI.isAborted())
        break;
    }
    return PI;
  }

protected:
  void visitStoreInst(StoreInst &SI) {
    if (SI.getValueOperand() == U->get())
      PI.setEscaped(&SI);
  }

  void visitBitCastInst(BitCastInst &BC) {
    enqueueUsers(BC);
  }

  void visitAddrSpaceCastInst(AddrSpaceCastInst &ASC) {
    enqueueUsers(ASC);
  }

  void visitPtrToIntInst(PtrToIntInst &I) {
    PI.setEscaped(&I);
  }

  void visitGetElementPtrInst(GetElementPtrInst &GEPI) {
    if (GEPI.use_empty())
      return;

    // If we can't walk the GEP, clear the offset.
    if (!adjustOffsetForGEP(GEPI)) {
      IsOffsetKnown = false;
      Offset = APInt();
    }

    // Enqueue the users now that the offset has been adjusted.
    enqueueUsers(GEPI);
  }

  // No-op intrinsics which we know don't escape the pointer to logic in
  // some other function.
  void visitDbgInfoIntrinsic(DbgInfoIntrinsic &I) {}
  void visitMemIntrinsic(MemIntrinsic &I) {}
  void visitIntrinsicInst(IntrinsicInst &II) {
    switch (II.getIntrinsicID()) {
    default:
      return Base::visitIntrinsicInst(II);

    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
      return; // No-op intrinsics.
    }
  }

  // Generically, arguments to calls and invokes escape the pointer to some
  // other function. Mark that.
  void visitCallBase(CallBase &CB) {
    PI.setEscaped(&CB);
    Base::visitCallBase(CB);
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_PTRUSEVISITOR_H
