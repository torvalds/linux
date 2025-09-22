//===----- llvm/CodeGen/GlobalISel/GISelChangeObserver.h --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This contains common code to allow clients to notify changes to machine
/// instr.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_GISELCHANGEOBSERVER_H
#define LLVM_CODEGEN_GLOBALISEL_GISELCHANGEOBSERVER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {
class MachineInstr;
class MachineRegisterInfo;

/// Abstract class that contains various methods for clients to notify about
/// changes. This should be the preferred way for APIs to notify changes.
/// Typically calling erasingInstr/createdInstr multiple times should not affect
/// the result. The observer would likely need to check if it was already
/// notified earlier (consider using GISelWorkList).
class GISelChangeObserver {
  SmallPtrSet<MachineInstr *, 4> ChangingAllUsesOfReg;

public:
  virtual ~GISelChangeObserver() = default;

  /// An instruction is about to be erased.
  virtual void erasingInstr(MachineInstr &MI) = 0;

  /// An instruction has been created and inserted into the function.
  /// Note that the instruction might not be a fully fledged instruction at this
  /// point and won't be if the MachineFunction::Delegate is calling it. This is
  /// because the delegate only sees the construction of the MachineInstr before
  /// operands have been added.
  virtual void createdInstr(MachineInstr &MI) = 0;

  /// This instruction is about to be mutated in some way.
  virtual void changingInstr(MachineInstr &MI) = 0;

  /// This instruction was mutated in some way.
  virtual void changedInstr(MachineInstr &MI) = 0;

  /// All the instructions using the given register are being changed.
  /// For convenience, finishedChangingAllUsesOfReg() will report the completion
  /// of the changes. The use list may change between this call and
  /// finishedChangingAllUsesOfReg().
  void changingAllUsesOfReg(const MachineRegisterInfo &MRI, Register Reg);
  /// All instructions reported as changing by changingAllUsesOfReg() have
  /// finished being changed.
  void finishedChangingAllUsesOfReg();

};

/// Simple wrapper observer that takes several observers, and calls
/// each one for each event. If there are multiple observers (say CSE,
/// Legalizer, Combiner), it's sufficient to register this to the machine
/// function as the delegate.
class GISelObserverWrapper : public MachineFunction::Delegate,
                             public GISelChangeObserver {
  SmallVector<GISelChangeObserver *, 4> Observers;

public:
  GISelObserverWrapper() = default;
  GISelObserverWrapper(ArrayRef<GISelChangeObserver *> Obs)
      : Observers(Obs.begin(), Obs.end()) {}
  // Adds an observer.
  void addObserver(GISelChangeObserver *O) { Observers.push_back(O); }
  // Removes an observer from the list and does nothing if observer is not
  // present.
  void removeObserver(GISelChangeObserver *O) {
    auto It = llvm::find(Observers, O);
    if (It != Observers.end())
      Observers.erase(It);
  }
  // API for Observer.
  void erasingInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->erasingInstr(MI);
  }
  void createdInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->createdInstr(MI);
  }
  void changingInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->changingInstr(MI);
  }
  void changedInstr(MachineInstr &MI) override {
    for (auto &O : Observers)
      O->changedInstr(MI);
  }
  // API for MachineFunction::Delegate
  void MF_HandleInsertion(MachineInstr &MI) override { createdInstr(MI); }
  void MF_HandleRemoval(MachineInstr &MI) override { erasingInstr(MI); }
};

/// A simple RAII based Delegate installer.
/// Use this in a scope to install a delegate to the MachineFunction and reset
/// it at the end of the scope.
class RAIIDelegateInstaller {
  MachineFunction &MF;
  MachineFunction::Delegate *Delegate;

public:
  RAIIDelegateInstaller(MachineFunction &MF, MachineFunction::Delegate *Del);
  ~RAIIDelegateInstaller();
};

/// A simple RAII based Observer installer.
/// Use this in a scope to install the Observer to the MachineFunction and reset
/// it at the end of the scope.
class RAIIMFObserverInstaller {
  MachineFunction &MF;

public:
  RAIIMFObserverInstaller(MachineFunction &MF, GISelChangeObserver &Observer);
  ~RAIIMFObserverInstaller();
};

/// Class to install both of the above.
class RAIIMFObsDelInstaller {
  RAIIDelegateInstaller DelI;
  RAIIMFObserverInstaller ObsI;

public:
  RAIIMFObsDelInstaller(MachineFunction &MF, GISelObserverWrapper &Wrapper)
      : DelI(MF, &Wrapper), ObsI(MF, Wrapper) {}
  ~RAIIMFObsDelInstaller() = default;
};

} // namespace llvm
#endif
