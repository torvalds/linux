//===- llvm/MC/MCSubtargetInfo.h - Subtarget Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the subtarget options of a Target machine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSUBTARGETINFO_H
#define LLVM_MC_MCSUBTARGETINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {

class MCInst;

//===----------------------------------------------------------------------===//

/// Used to provide key value pairs for feature and CPU bit flags.
struct SubtargetFeatureKV {
  const char *Key;                      ///< K-V key string
  const char *Desc;                     ///< Help descriptor
  unsigned Value;                       ///< K-V integer value
  FeatureBitArray Implies;              ///< K-V bit mask

  /// Compare routine for std::lower_bound
  bool operator<(StringRef S) const {
    return StringRef(Key) < S;
  }

  /// Compare routine for std::is_sorted.
  bool operator<(const SubtargetFeatureKV &Other) const {
    return StringRef(Key) < StringRef(Other.Key);
  }
};

//===----------------------------------------------------------------------===//

/// Used to provide key value pairs for feature and CPU bit flags.
struct SubtargetSubTypeKV {
  const char *Key;                      ///< K-V key string
  FeatureBitArray Implies;              ///< K-V bit mask
  FeatureBitArray TuneImplies;          ///< K-V bit mask
  const MCSchedModel *SchedModel;

  /// Compare routine for std::lower_bound
  bool operator<(StringRef S) const {
    return StringRef(Key) < S;
  }

  /// Compare routine for std::is_sorted.
  bool operator<(const SubtargetSubTypeKV &Other) const {
    return StringRef(Key) < StringRef(Other.Key);
  }
};

//===----------------------------------------------------------------------===//
///
/// Generic base class for all target subtargets.
///
class MCSubtargetInfo {
  Triple TargetTriple;
  std::string CPU; // CPU being targeted.
  std::string TuneCPU; // CPU being tuned for.
  ArrayRef<SubtargetFeatureKV> ProcFeatures;  // Processor feature list
  ArrayRef<SubtargetSubTypeKV> ProcDesc;  // Processor descriptions

  // Scheduler machine model
  const MCWriteProcResEntry *WriteProcResTable;
  const MCWriteLatencyEntry *WriteLatencyTable;
  const MCReadAdvanceEntry *ReadAdvanceTable;
  const MCSchedModel *CPUSchedModel;

  const InstrStage *Stages;            // Instruction itinerary stages
  const unsigned *OperandCycles;       // Itinerary operand cycles
  const unsigned *ForwardingPaths;
  FeatureBitset FeatureBits;           // Feature bits for current CPU + FS
  std::string FeatureString;           // Feature string

public:
  MCSubtargetInfo(const MCSubtargetInfo &) = default;
  MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                  StringRef FS, ArrayRef<SubtargetFeatureKV> PF,
                  ArrayRef<SubtargetSubTypeKV> PD,
                  const MCWriteProcResEntry *WPR, const MCWriteLatencyEntry *WL,
                  const MCReadAdvanceEntry *RA, const InstrStage *IS,
                  const unsigned *OC, const unsigned *FP);
  MCSubtargetInfo() = delete;
  MCSubtargetInfo &operator=(const MCSubtargetInfo &) = delete;
  MCSubtargetInfo &operator=(MCSubtargetInfo &&) = delete;
  virtual ~MCSubtargetInfo() = default;

  const Triple &getTargetTriple() const { return TargetTriple; }
  StringRef getCPU() const { return CPU; }
  StringRef getTuneCPU() const { return TuneCPU; }

  const FeatureBitset& getFeatureBits() const { return FeatureBits; }
  void setFeatureBits(const FeatureBitset &FeatureBits_) {
    FeatureBits = FeatureBits_;
  }

  StringRef getFeatureString() const { return FeatureString; }

  bool hasFeature(unsigned Feature) const {
    return FeatureBits[Feature];
  }

protected:
  /// Initialize the scheduling model and feature bits.
  ///
  /// FIXME: Find a way to stick this in the constructor, since it should only
  /// be called during initialization.
  void InitMCProcessorInfo(StringRef CPU, StringRef TuneCPU, StringRef FS);

public:
  /// Set the features to the default for the given CPU and TuneCPU, with ano
  /// appended feature string.
  void setDefaultFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  /// Toggle a feature and return the re-computed feature bits.
  /// This version does not change the implied bits.
  FeatureBitset ToggleFeature(uint64_t FB);

  /// Toggle a feature and return the re-computed feature bits.
  /// This version does not change the implied bits.
  FeatureBitset ToggleFeature(const FeatureBitset& FB);

  /// Toggle a set of features and return the re-computed feature bits.
  /// This version will also change all implied bits.
  FeatureBitset ToggleFeature(StringRef FS);

  /// Apply a feature flag and return the re-computed feature bits, including
  /// all feature bits implied by the flag.
  FeatureBitset ApplyFeatureFlag(StringRef FS);

  /// Set/clear additional feature bits, including all other bits they imply.
  FeatureBitset SetFeatureBitsTransitively(const FeatureBitset& FB);
  FeatureBitset ClearFeatureBitsTransitively(const FeatureBitset &FB);

  /// Check whether the subtarget features are enabled/disabled as per
  /// the provided string, ignoring all other features.
  bool checkFeatures(StringRef FS) const;

  /// Get the machine model of a CPU.
  const MCSchedModel &getSchedModelForCPU(StringRef CPU) const;

  /// Get the machine model for this subtarget's CPU.
  const MCSchedModel &getSchedModel() const { return *CPUSchedModel; }

  /// Return an iterator at the first process resource consumed by the given
  /// scheduling class.
  const MCWriteProcResEntry *getWriteProcResBegin(
    const MCSchedClassDesc *SC) const {
    return &WriteProcResTable[SC->WriteProcResIdx];
  }
  const MCWriteProcResEntry *getWriteProcResEnd(
    const MCSchedClassDesc *SC) const {
    return getWriteProcResBegin(SC) + SC->NumWriteProcResEntries;
  }

  const MCWriteLatencyEntry *getWriteLatencyEntry(const MCSchedClassDesc *SC,
                                                  unsigned DefIdx) const {
    assert(DefIdx < SC->NumWriteLatencyEntries &&
           "MachineModel does not specify a WriteResource for DefIdx");

    return &WriteLatencyTable[SC->WriteLatencyIdx + DefIdx];
  }

  int getReadAdvanceCycles(const MCSchedClassDesc *SC, unsigned UseIdx,
                           unsigned WriteResID) const {
    // TODO: The number of read advance entries in a class can be significant
    // (~50). Consider compressing the WriteID into a dense ID of those that are
    // used by ReadAdvance and representing them as a bitset.
    for (const MCReadAdvanceEntry *I = &ReadAdvanceTable[SC->ReadAdvanceIdx],
           *E = I + SC->NumReadAdvanceEntries; I != E; ++I) {
      if (I->UseIdx < UseIdx)
        continue;
      if (I->UseIdx > UseIdx)
        break;
      // Find the first WriteResIdx match, which has the highest cycle count.
      if (!I->WriteResourceID || I->WriteResourceID == WriteResID) {
        return I->Cycles;
      }
    }
    return 0;
  }

  /// Return the set of ReadAdvance entries declared by the scheduling class
  /// descriptor in input.
  ArrayRef<MCReadAdvanceEntry>
  getReadAdvanceEntries(const MCSchedClassDesc &SC) const {
    if (!SC.NumReadAdvanceEntries)
      return ArrayRef<MCReadAdvanceEntry>();
    return ArrayRef<MCReadAdvanceEntry>(&ReadAdvanceTable[SC.ReadAdvanceIdx],
                                        SC.NumReadAdvanceEntries);
  }

  /// Get scheduling itinerary of a CPU.
  InstrItineraryData getInstrItineraryForCPU(StringRef CPU) const;

  /// Initialize an InstrItineraryData instance.
  void initInstrItins(InstrItineraryData &InstrItins) const;

  /// Resolve a variant scheduling class for the given MCInst and CPU.
  virtual unsigned resolveVariantSchedClass(unsigned SchedClass,
                                            const MCInst *MI,
                                            const MCInstrInfo *MCII,
                                            unsigned CPUID) const {
    return 0;
  }

  /// Check whether the CPU string is valid.
  virtual bool isCPUStringValid(StringRef CPU) const {
    auto Found = llvm::lower_bound(ProcDesc, CPU);
    return Found != ProcDesc.end() && StringRef(Found->Key) == CPU;
  }

  /// Return processor descriptions.
  ArrayRef<SubtargetSubTypeKV> getAllProcessorDescriptions() const {
    return ProcDesc;
  }

  /// Return processor features.
  ArrayRef<SubtargetFeatureKV> getAllProcessorFeatures() const {
    return ProcFeatures;
  }

  /// Return the list of processor features currently enabled.
  std::vector<SubtargetFeatureKV> getEnabledProcessorFeatures() const;

  /// HwMode IDs are stored and accessed in a bit set format, enabling
  /// users to efficiently retrieve specific IDs, such as the RegInfo
  /// HwMode ID, from the set as required. Using this approach, various
  /// types of HwMode IDs can be added to a subtarget to manage different
  /// attributes within that subtarget, significantly enhancing the
  /// scalability and usability of HwMode. Moreover, to ensure compatibility,
  /// this method also supports controlling multiple attributes with a single
  /// HwMode ID, just as was done previously.
  enum HwModeType {
    HwMode_Default,   // Return the smallest HwMode ID of current subtarget.
    HwMode_ValueType, // Return the HwMode ID that controls the ValueType.
    HwMode_RegInfo,   // Return the HwMode ID that controls the RegSizeInfo and
                      // SubRegRange.
    HwMode_EncodingInfo // Return the HwMode ID that controls the EncodingInfo.
  };

  /// Return a bit set containing all HwMode IDs of the current subtarget.
  virtual unsigned getHwModeSet() const { return 0; }

  /// HwMode ID corresponding to the 'type' parameter is retrieved from the
  /// HwMode bit set of the current subtarget. It’s important to note that if
  /// the current subtarget possesses two HwMode IDs and both control a single
  /// attribute (such as RegInfo), this interface will result in an error.
  virtual unsigned getHwMode(enum HwModeType type = HwMode_Default) const {
    return 0;
  }

  /// Return the cache size in bytes for the given level of cache.
  /// Level is zero-based, so a value of zero means the first level of
  /// cache.
  ///
  virtual std::optional<unsigned> getCacheSize(unsigned Level) const;

  /// Return the cache associatvity for the given level of cache.
  /// Level is zero-based, so a value of zero means the first level of
  /// cache.
  ///
  virtual std::optional<unsigned> getCacheAssociativity(unsigned Level) const;

  /// Return the target cache line size in bytes at a given level.
  ///
  virtual std::optional<unsigned> getCacheLineSize(unsigned Level) const;

  /// Return the target cache line size in bytes.  By default, return
  /// the line size for the bottom-most level of cache.  This provides
  /// a more convenient interface for the common case where all cache
  /// levels have the same line size.  Return zero if there is no
  /// cache model.
  ///
  virtual unsigned getCacheLineSize() const {
    std::optional<unsigned> Size = getCacheLineSize(0);
    if (Size)
      return *Size;

    return 0;
  }

  /// Return the preferred prefetch distance in terms of instructions.
  ///
  virtual unsigned getPrefetchDistance() const;

  /// Return the maximum prefetch distance in terms of loop
  /// iterations.
  ///
  virtual unsigned getMaxPrefetchIterationsAhead() const;

  /// \return True if prefetching should also be done for writes.
  ///
  virtual bool enableWritePrefetching() const;

  /// Return the minimum stride necessary to trigger software
  /// prefetching.
  ///
  virtual unsigned getMinPrefetchStride(unsigned NumMemAccesses,
                                        unsigned NumStridedMemAccesses,
                                        unsigned NumPrefetches,
                                        bool HasCall) const;

  /// \return if target want to issue a prefetch in address space \p AS.
  virtual bool shouldPrefetchAddressSpace(unsigned AS) const;
};

} // end namespace llvm

#endif // LLVM_MC_MCSUBTARGETINFO_H
