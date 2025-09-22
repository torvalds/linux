//===- lib/CodeGen/CalcSpillWeights.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CALCSPILLWEIGHTS_H
#define LLVM_CODEGEN_CALCSPILLWEIGHTS_H

#include "llvm/CodeGen/SlotIndexes.h"

namespace llvm {

class LiveInterval;
class LiveIntervals;
class MachineBlockFrequencyInfo;
class MachineFunction;
class MachineLoopInfo;
class VirtRegMap;

  /// Normalize the spill weight of a live interval
  ///
  /// The spill weight of a live interval is computed as:
  ///
  ///   (sum(use freq) + sum(def freq)) / (K + size)
  ///
  /// @param UseDefFreq Expected number of executed use and def instructions
  ///                   per function call. Derived from block frequencies.
  /// @param Size       Size of live interval as returnexd by getSize()
  /// @param NumInstr   Number of instructions using this live interval
  static inline float normalizeSpillWeight(float UseDefFreq, unsigned Size,
                                           unsigned NumInstr) {
    // The constant 25 instructions is added to avoid depending too much on
    // accidental SlotIndex gaps for small intervals. The effect is that small
    // intervals have a spill weight that is mostly proportional to the number
    // of uses, while large intervals get a spill weight that is closer to a use
    // density.
    return UseDefFreq / (Size + 25*SlotIndex::InstrDist);
  }

  /// Calculate auxiliary information for a virtual register such as its
  /// spill weight and allocation hint.
  class VirtRegAuxInfo {
    MachineFunction &MF;
    LiveIntervals &LIS;
    const VirtRegMap &VRM;
    const MachineLoopInfo &Loops;
    const MachineBlockFrequencyInfo &MBFI;

    /// Returns true if Reg of live interval LI is used in instruction with many
    /// operands like STATEPOINT.
    bool isLiveAtStatepointVarArg(LiveInterval &LI);

  public:
    VirtRegAuxInfo(MachineFunction &MF, LiveIntervals &LIS,
                   const VirtRegMap &VRM, const MachineLoopInfo &Loops,
                   const MachineBlockFrequencyInfo &MBFI)
        : MF(MF), LIS(LIS), VRM(VRM), Loops(Loops), MBFI(MBFI) {}

    virtual ~VirtRegAuxInfo() = default;

    /// (re)compute li's spill weight and allocation hint.
    void calculateSpillWeightAndHint(LiveInterval &LI);

    /// Compute spill weights and allocation hints for all virtual register
    /// live intervals.
    void calculateSpillWeightsAndHints();

    /// Return the preferred allocation register for reg, given a COPY
    /// instruction.
    static Register copyHint(const MachineInstr *MI, unsigned Reg,
                             const TargetRegisterInfo &TRI,
                             const MachineRegisterInfo &MRI);

    /// Determine if all values in LI are rematerializable.
    static bool isRematerializable(const LiveInterval &LI,
                                   const LiveIntervals &LIS,
                                   const VirtRegMap &VRM,
                                   const TargetInstrInfo &TII);

  protected:
    /// Helper function for weight calculations.
    /// (Re)compute LI's spill weight and allocation hint, or, for non null
    /// start and end - compute future expected spill weight of a split
    /// artifact of LI that will span between start and end slot indexes.
    /// \param LI     The live interval for which to compute the weight.
    /// \param Start  The expected beginning of the split artifact. Instructions
    ///               before start will not affect the weight. Relevant for
    ///               weight calculation of future split artifact.
    /// \param End    The expected end of the split artifact. Instructions
    ///               after end will not affect the weight. Relevant for
    ///               weight calculation of future split artifact.
    /// \return The spill weight. Returns negative weight for unspillable LI.
    float weightCalcHelper(LiveInterval &LI, SlotIndex *Start = nullptr,
                           SlotIndex *End = nullptr);

    /// Weight normalization function.
    virtual float normalize(float UseDefFreq, unsigned Size,
                            unsigned NumInstr) {
      return normalizeSpillWeight(UseDefFreq, Size, NumInstr);
    }
  };
} // end namespace llvm

#endif // LLVM_CODEGEN_CALCSPILLWEIGHTS_H
