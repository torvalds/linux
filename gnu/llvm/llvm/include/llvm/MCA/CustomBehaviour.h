//===---------------------- CustomBehaviour.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the base class CustomBehaviour which can be inherited from
/// by specific targets (ex. llvm/tools/llvm-mca/lib/X86CustomBehaviour.h).
/// CustomBehaviour is designed to enforce custom behaviour and dependencies
/// within the llvm-mca pipeline simulation that llvm-mca isn't already capable
/// of extracting from the Scheduling Models.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_CUSTOMBEHAVIOUR_H
#define LLVM_MCA_CUSTOMBEHAVIOUR_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/MCA/View.h"

namespace llvm {
namespace mca {

/// Class which can be overriden by targets to modify the
/// mca::Instruction objects before the pipeline starts.
/// A common usage of this class is to add immediate operands to certain
/// instructions or to remove Defs/Uses from an instruction where the
/// schedulinng model is incorrect.
class InstrPostProcess {
protected:
  const MCSubtargetInfo &STI;
  const MCInstrInfo &MCII;

public:
  InstrPostProcess(const MCSubtargetInfo &STI, const MCInstrInfo &MCII)
      : STI(STI), MCII(MCII) {}

  virtual ~InstrPostProcess() = default;

  /// This method can be overriden by targets to modify the mca::Instruction
  /// object after it has been lowered from the MCInst.
  /// This is generally a less disruptive alternative to modifying the
  /// scheduling model.
  virtual void postProcessInstruction(std::unique_ptr<Instruction> &Inst,
                                      const MCInst &MCI) {}

  // The resetState() method gets invoked at the beginning of each code region
  // so that targets that override this function can clear any state that they
  // have left from the previous code region.
  virtual void resetState() {}
};

/// Class which can be overriden by targets to enforce instruction
/// dependencies and behaviours that aren't expressed well enough
/// within the scheduling model for mca to automatically simulate
/// them properly.
/// If you implement this class for your target, make sure to also implement
/// a target specific InstrPostProcess class as well.
class CustomBehaviour {
protected:
  const MCSubtargetInfo &STI;
  const mca::SourceMgr &SrcMgr;
  const MCInstrInfo &MCII;

public:
  CustomBehaviour(const MCSubtargetInfo &STI, const mca::SourceMgr &SrcMgr,
                  const MCInstrInfo &MCII)
      : STI(STI), SrcMgr(SrcMgr), MCII(MCII) {}

  virtual ~CustomBehaviour();

  /// Before the llvm-mca pipeline dispatches an instruction, it first checks
  /// for any register or resource dependencies / hazards. If it doesn't find
  /// any, this method will be invoked to determine if there are any custom
  /// hazards that the instruction needs to wait for.
  /// The return value of this method is the number of cycles that the
  /// instruction needs to wait for.
  /// It's safe to underestimate the number of cycles to wait for since these
  /// checks will be invoked again before the intruction gets dispatched.
  /// However, it's not safe (accurate) to overestimate the number of cycles
  /// to wait for since the instruction will wait for AT LEAST that number of
  /// cycles before attempting to be dispatched again.
  virtual unsigned checkCustomHazard(ArrayRef<InstRef> IssuedInst,
                                     const InstRef &IR);

  // Functions that target CBs can override to return a list of
  // target specific Views that need to live within /lib/Target/ so that
  // they can benefit from the target CB or from backend functionality that is
  // not already exposed through MC-layer classes. Keep in mind that how this
  // function is used is that the function is called within llvm-mca.cpp and
  // then each unique_ptr<View> is passed into the PipelinePrinter::addView()
  // function. This function will then std::move the View into its own vector of
  // Views. So any CB that overrides this function needs to make sure that they
  // are not relying on the current address or reference of the View
  // unique_ptrs. If you do need the CB and View to be able to communicate with
  // each other, consider giving the View a reference or pointer to the CB when
  // the View is constructed. Then the View can query the CB for information
  // when it needs it.
  /// Return a vector of Views that will be added before all other Views.
  virtual std::vector<std::unique_ptr<View>>
  getStartViews(llvm::MCInstPrinter &IP, llvm::ArrayRef<llvm::MCInst> Insts);
  /// Return a vector of Views that will be added after the InstructionInfoView.
  virtual std::vector<std::unique_ptr<View>>
  getPostInstrInfoViews(llvm::MCInstPrinter &IP,
                        llvm::ArrayRef<llvm::MCInst> Insts);
  /// Return a vector of Views that will be added after all other Views.
  virtual std::vector<std::unique_ptr<View>>
  getEndViews(llvm::MCInstPrinter &IP, llvm::ArrayRef<llvm::MCInst> Insts);
};

class Instrument {
  /// The description of Instrument kind
  const StringRef Desc;

  /// The instrumentation data
  const StringRef Data;

public:
  Instrument(StringRef Desc, StringRef Data) : Desc(Desc), Data(Data) {}

  Instrument() : Instrument("", "") {}

  virtual ~Instrument() = default;

  StringRef getDesc() const { return Desc; }
  StringRef getData() const { return Data; }
};

using UniqueInstrument = std::unique_ptr<Instrument>;

/// This class allows targets to optionally customize the logic that resolves
/// scheduling class IDs. Targets can use information encoded in Instrument
/// objects to make more informed scheduling decisions.
class InstrumentManager {
protected:
  const MCSubtargetInfo &STI;
  const MCInstrInfo &MCII;

public:
  InstrumentManager(const MCSubtargetInfo &STI, const MCInstrInfo &MCII)
      : STI(STI), MCII(MCII) {}

  virtual ~InstrumentManager() = default;

  /// Returns true if llvm-mca should ignore instruments.
  virtual bool shouldIgnoreInstruments() const { return true; }

  // Returns true if this supports processing Instrument with
  // Instrument.Desc equal to Type
  virtual bool supportsInstrumentType(StringRef Type) const { return false; }

  /// Allocate an Instrument, and return a unique pointer to it. This function
  /// may be useful to create instruments coming from comments in the assembly.
  /// See createInstruments to create Instruments from MCInst
  virtual UniqueInstrument createInstrument(StringRef Desc, StringRef Data);

  /// Return a list of unique pointers to Instruments, where each Instrument
  /// is allocated by this function. See createInstrument to create Instrument
  /// from a description and data.
  virtual SmallVector<UniqueInstrument> createInstruments(const MCInst &Inst);

  /// Given an MCInst and a vector of Instrument, a target can
  /// return a SchedClassID. This can be used by a subtarget to return a
  /// PseudoInstruction SchedClassID instead of the one that belongs to the
  /// BaseInstruction This can be useful when a BaseInstruction does not convey
  /// the correct scheduling information without additional data. By default,
  /// it returns the SchedClassID that belongs to MCI.
  virtual unsigned getSchedClassID(const MCInstrInfo &MCII, const MCInst &MCI,
                                   const SmallVector<Instrument *> &IVec) const;
};

} // namespace mca
} // namespace llvm

#endif /* LLVM_MCA_CUSTOMBEHAVIOUR_H */
