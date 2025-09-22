//===- Tracker.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is the component of SandboxIR that tracks all changes made to its
// state, such that we can revert the state when needed.
//
// Tracking changes
// ----------------
// The user needs to call `Tracker::save()` to enable tracking changes
// made to SandboxIR. From that point on, any change made to SandboxIR, will
// automatically create a change tracking object and register it with the
// tracker. IR-change objects are subclasses of `IRChangeBase` and get
// registered with the `Tracker::track()` function. The change objects
// are saved in the order they are registered with the tracker and are stored in
// the `Tracker::Changes` vector. All of this is done transparently to
// the user.
//
// Reverting changes
// -----------------
// Calling `Tracker::revert()` will restore the state saved when
// `Tracker::save()` was called. Internally this goes through the
// change objects in `Tracker::Changes` in reverse order, calling their
// `IRChangeBase::revert()` function one by one.
//
// Accepting changes
// -----------------
// The user needs to either revert or accept changes before the tracker object
// is destroyed. This is enforced in the tracker's destructor.
// This is the job of `Tracker::accept()`. Internally this will go
// through the change objects in `Tracker::Changes` in order, calling
// `IRChangeBase::accept()`.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_TRACKER_H
#define LLVM_SANDBOXIR_TRACKER_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/SandboxIR/Use.h"
#include "llvm/Support/Debug.h"
#include <memory>
#include <regex>

namespace llvm::sandboxir {

class BasicBlock;
class Instruction;
class Tracker;

/// The base class for IR Change classes.
class IRChangeBase {
protected:
  Tracker &Parent;

public:
  IRChangeBase(Tracker &Parent);
  /// This runs when changes get reverted.
  virtual void revert() = 0;
  /// This runs when changes get accepted.
  virtual void accept() = 0;
  virtual ~IRChangeBase() = default;
#ifndef NDEBUG
  /// \Returns the index of this change by iterating over all changes in the
  /// tracker. This is only used for debugging.
  unsigned getIdx() const;
  void dumpCommon(raw_ostream &OS) const { OS << getIdx() << ". "; }
  virtual void dump(raw_ostream &OS) const = 0;
  LLVM_DUMP_METHOD virtual void dump() const = 0;
  friend raw_ostream &operator<<(raw_ostream &OS, const IRChangeBase &C) {
    C.dump(OS);
    return OS;
  }
#endif
};

/// Tracks the change of the source Value of a sandboxir::Use.
class UseSet : public IRChangeBase {
  Use U;
  Value *OrigV = nullptr;

public:
  UseSet(const Use &U, Tracker &Tracker)
      : IRChangeBase(Tracker), U(U), OrigV(U.get()) {}
  void revert() final { U.set(OrigV); }
  void accept() final {}
#ifndef NDEBUG
  void dump(raw_ostream &OS) const final {
    dumpCommon(OS);
    OS << "UseSet";
  }
  LLVM_DUMP_METHOD void dump() const final;
#endif
};

class EraseFromParent : public IRChangeBase {
  /// Contains all the data we need to restore an "erased" (i.e., detached)
  /// instruction: the instruction itself and its operands in order.
  struct InstrAndOperands {
    /// The operands that got dropped.
    SmallVector<llvm::Value *> Operands;
    /// The instruction that got "erased".
    llvm::Instruction *LLVMI;
  };
  /// The instruction data is in reverse program order, which helps create the
  /// original program order during revert().
  SmallVector<InstrAndOperands> InstrData;
  /// This is either the next Instruction in the stream, or the parent
  /// BasicBlock if at the end of the BB.
  PointerUnion<llvm::Instruction *, llvm::BasicBlock *> NextLLVMIOrBB;
  /// We take ownership of the "erased" instruction.
  std::unique_ptr<sandboxir::Value> ErasedIPtr;

public:
  EraseFromParent(std::unique_ptr<sandboxir::Value> &&IPtr, Tracker &Tracker);
  void revert() final;
  void accept() final;
#ifndef NDEBUG
  void dump(raw_ostream &OS) const final {
    dumpCommon(OS);
    OS << "EraseFromParent";
  }
  LLVM_DUMP_METHOD void dump() const final;
  friend raw_ostream &operator<<(raw_ostream &OS, const EraseFromParent &C) {
    C.dump(OS);
    return OS;
  }
#endif
};

class RemoveFromParent : public IRChangeBase {
  /// The instruction that is about to get removed.
  Instruction *RemovedI = nullptr;
  /// This is either the next instr, or the parent BB if at the end of the BB.
  PointerUnion<Instruction *, BasicBlock *> NextInstrOrBB;

public:
  RemoveFromParent(Instruction *RemovedI, Tracker &Tracker);
  void revert() final;
  void accept() final {};
  Instruction *getInstruction() const { return RemovedI; }
#ifndef NDEBUG
  void dump(raw_ostream &OS) const final {
    dumpCommon(OS);
    OS << "RemoveFromParent";
  }
  LLVM_DUMP_METHOD void dump() const final;
#endif // NDEBUG
};

class MoveInstr : public IRChangeBase {
  /// The instruction that moved.
  Instruction *MovedI;
  /// This is either the next instruction in the block, or the parent BB if at
  /// the end of the BB.
  PointerUnion<Instruction *, BasicBlock *> NextInstrOrBB;

public:
  MoveInstr(sandboxir::Instruction *I, Tracker &Tracker);
  void revert() final;
  void accept() final {}
#ifndef NDEBUG
  void dump(raw_ostream &OS) const final {
    dumpCommon(OS);
    OS << "MoveInstr";
  }
  LLVM_DUMP_METHOD void dump() const final;
#endif // NDEBUG
};

/// The tracker collects all the change objects and implements the main API for
/// saving / reverting / accepting.
class Tracker {
public:
  enum class TrackerState {
    Disabled, ///> Tracking is disabled
    Record,   ///> Tracking changes
  };

private:
  /// The list of changes that are being tracked.
  SmallVector<std::unique_ptr<IRChangeBase>> Changes;
#ifndef NDEBUG
  friend unsigned IRChangeBase::getIdx() const; // For accessing `Changes`.
#endif
  /// The current state of the tracker.
  TrackerState State = TrackerState::Disabled;
  Context &Ctx;

public:
#ifndef NDEBUG
  /// Helps catch bugs where we are creating new change objects while in the
  /// middle of creating other change objects.
  bool InMiddleOfCreatingChange = false;
#endif // NDEBUG

  explicit Tracker(Context &Ctx) : Ctx(Ctx) {}
  ~Tracker();
  Context &getContext() const { return Ctx; }
  /// Record \p Change and take ownership. This is the main function used to
  /// track Sandbox IR changes.
  void track(std::unique_ptr<IRChangeBase> &&Change);
  /// \Returns true if the tracker is recording changes.
  bool isTracking() const { return State == TrackerState::Record; }
  /// \Returns the current state of the tracker.
  TrackerState getState() const { return State; }
  /// Turns on IR tracking.
  void save();
  /// Stops tracking and accept changes.
  void accept();
  /// Stops tracking and reverts to saved state.
  void revert();

#ifndef NDEBUG
  void dump(raw_ostream &OS) const;
  LLVM_DUMP_METHOD void dump() const;
  friend raw_ostream &operator<<(raw_ostream &OS, const Tracker &Tracker) {
    Tracker.dump(OS);
    return OS;
  }
#endif // NDEBUG
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_TRACKER_H
