//===- MCSubtargetInfo.cpp - Subtarget Information ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <optional>

using namespace llvm;

/// Find KV in array using binary search.
template <typename T>
static const T *Find(StringRef S, ArrayRef<T> A) {
  // Binary search the array
  auto F = llvm::lower_bound(A, S);
  // If not found then return NULL
  if (F == A.end() || StringRef(F->Key) != S) return nullptr;
  // Return the found array item
  return F;
}

/// For each feature that is (transitively) implied by this feature, set it.
static
void SetImpliedBits(FeatureBitset &Bits, const FeatureBitset &Implies,
                    ArrayRef<SubtargetFeatureKV> FeatureTable) {
  // OR the Implies bits in outside the loop. This allows the Implies for CPUs
  // which might imply features not in FeatureTable to use this.
  Bits |= Implies;
  for (const SubtargetFeatureKV &FE : FeatureTable)
    if (Implies.test(FE.Value))
      SetImpliedBits(Bits, FE.Implies.getAsBitset(), FeatureTable);
}

/// For each feature that (transitively) implies this feature, clear it.
static
void ClearImpliedBits(FeatureBitset &Bits, unsigned Value,
                      ArrayRef<SubtargetFeatureKV> FeatureTable) {
  for (const SubtargetFeatureKV &FE : FeatureTable) {
    if (FE.Implies.getAsBitset().test(Value)) {
      Bits.reset(FE.Value);
      ClearImpliedBits(Bits, FE.Value, FeatureTable);
    }
  }
}

static void ApplyFeatureFlag(FeatureBitset &Bits, StringRef Feature,
                             ArrayRef<SubtargetFeatureKV> FeatureTable) {
  assert(SubtargetFeatures::hasFlag(Feature) &&
         "Feature flags should start with '+' or '-'");

  // Find feature in table.
  const SubtargetFeatureKV *FeatureEntry =
      Find(SubtargetFeatures::StripFlag(Feature), FeatureTable);
  // If there is a match
  if (FeatureEntry) {
    // Enable/disable feature in bits
    if (SubtargetFeatures::isEnabled(Feature)) {
      Bits.set(FeatureEntry->Value);

      // For each feature that this implies, set it.
      SetImpliedBits(Bits, FeatureEntry->Implies.getAsBitset(), FeatureTable);
    } else {
      Bits.reset(FeatureEntry->Value);

      // For each feature that implies this, clear it.
      ClearImpliedBits(Bits, FeatureEntry->Value, FeatureTable);
    }
  } else {
    errs() << "'" << Feature << "' is not a recognized feature for this target"
           << " (ignoring feature)\n";
  }
}

/// Return the length of the longest entry in the table.
template <typename T>
static size_t getLongestEntryLength(ArrayRef<T> Table) {
  size_t MaxLen = 0;
  for (auto &I : Table)
    MaxLen = std::max(MaxLen, std::strlen(I.Key));
  return MaxLen;
}

/// Display help for feature and mcpu choices.
static void Help(ArrayRef<SubtargetSubTypeKV> CPUTable,
                 ArrayRef<SubtargetFeatureKV> FeatTable) {
  // the static variable ensures that the help information only gets
  // printed once even though a target machine creates multiple subtargets
  static bool PrintOnce = false;
  if (PrintOnce) {
    return;
  }

  // Determine the length of the longest CPU and Feature entries.
  unsigned MaxCPULen  = getLongestEntryLength(CPUTable);
  unsigned MaxFeatLen = getLongestEntryLength(FeatTable);

  // Print the CPU table.
  errs() << "Available CPUs for this target:\n\n";
  for (auto &CPU : CPUTable)
    errs() << format("  %-*s - Select the %s processor.\n", MaxCPULen, CPU.Key,
                     CPU.Key);
  errs() << '\n';

  // Print the Feature table.
  errs() << "Available features for this target:\n\n";
  for (auto &Feature : FeatTable)
    errs() << format("  %-*s - %s.\n", MaxFeatLen, Feature.Key, Feature.Desc);
  errs() << '\n';

  errs() << "Use +feature to enable a feature, or -feature to disable it.\n"
            "For example, llc -mcpu=mycpu -mattr=+feature1,-feature2\n";

  PrintOnce = true;
}

/// Display help for mcpu choices only
static void cpuHelp(ArrayRef<SubtargetSubTypeKV> CPUTable) {
  // the static variable ensures that the help information only gets
  // printed once even though a target machine creates multiple subtargets
  static bool PrintOnce = false;
  if (PrintOnce) {
    return;
  }

  // Print the CPU table.
  errs() << "Available CPUs for this target:\n\n";
  for (auto &CPU : CPUTable)
    errs() << "\t" << CPU.Key << "\n";
  errs() << '\n';

  errs() << "Use -mcpu or -mtune to specify the target's processor.\n"
            "For example, clang --target=aarch64-unknown-linux-gnu "
            "-mcpu=cortex-a35\n";

  PrintOnce = true;
}

static FeatureBitset getFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS,
                                 ArrayRef<SubtargetSubTypeKV> ProcDesc,
                                 ArrayRef<SubtargetFeatureKV> ProcFeatures) {
  SubtargetFeatures Features(FS);

  if (ProcDesc.empty() || ProcFeatures.empty())
    return FeatureBitset();

  assert(llvm::is_sorted(ProcDesc) && "CPU table is not sorted");
  assert(llvm::is_sorted(ProcFeatures) && "CPU features table is not sorted");
  // Resulting bits
  FeatureBitset Bits;

  // Check if help is needed
  if (CPU == "help")
    Help(ProcDesc, ProcFeatures);

  // Find CPU entry if CPU name is specified.
  else if (!CPU.empty()) {
    const SubtargetSubTypeKV *CPUEntry = Find(CPU, ProcDesc);

    // If there is a match
    if (CPUEntry) {
      // Set the features implied by this CPU feature, if any.
      SetImpliedBits(Bits, CPUEntry->Implies.getAsBitset(), ProcFeatures);
    } else {
      errs() << "'" << CPU << "' is not a recognized processor for this target"
             << " (ignoring processor)\n";
    }
  }

  if (!TuneCPU.empty()) {
    const SubtargetSubTypeKV *CPUEntry = Find(TuneCPU, ProcDesc);

    // If there is a match
    if (CPUEntry) {
      // Set the features implied by this CPU feature, if any.
      SetImpliedBits(Bits, CPUEntry->TuneImplies.getAsBitset(), ProcFeatures);
    } else if (TuneCPU != CPU) {
      errs() << "'" << TuneCPU << "' is not a recognized processor for this "
             << "target (ignoring processor)\n";
    }
  }

  // Iterate through each feature
  for (const std::string &Feature : Features.getFeatures()) {
    // Check for help
    if (Feature == "+help")
      Help(ProcDesc, ProcFeatures);
    else if (Feature == "+cpuhelp")
      cpuHelp(ProcDesc);
    else
      ApplyFeatureFlag(Bits, Feature, ProcFeatures);
  }

  return Bits;
}

void MCSubtargetInfo::InitMCProcessorInfo(StringRef CPU, StringRef TuneCPU,
                                          StringRef FS) {
  FeatureBits = getFeatures(CPU, TuneCPU, FS, ProcDesc, ProcFeatures);
  FeatureString = std::string(FS);

  if (!TuneCPU.empty())
    CPUSchedModel = &getSchedModelForCPU(TuneCPU);
  else
    CPUSchedModel = &MCSchedModel::Default;
}

void MCSubtargetInfo::setDefaultFeatures(StringRef CPU, StringRef TuneCPU,
                                         StringRef FS) {
  FeatureBits = getFeatures(CPU, TuneCPU, FS, ProcDesc, ProcFeatures);
  FeatureString = std::string(FS);
}

MCSubtargetInfo::MCSubtargetInfo(const Triple &TT, StringRef C, StringRef TC,
                                 StringRef FS, ArrayRef<SubtargetFeatureKV> PF,
                                 ArrayRef<SubtargetSubTypeKV> PD,
                                 const MCWriteProcResEntry *WPR,
                                 const MCWriteLatencyEntry *WL,
                                 const MCReadAdvanceEntry *RA,
                                 const InstrStage *IS, const unsigned *OC,
                                 const unsigned *FP)
    : TargetTriple(TT), CPU(std::string(C)), TuneCPU(std::string(TC)),
      ProcFeatures(PF), ProcDesc(PD), WriteProcResTable(WPR),
      WriteLatencyTable(WL), ReadAdvanceTable(RA), Stages(IS),
      OperandCycles(OC), ForwardingPaths(FP) {
  InitMCProcessorInfo(CPU, TuneCPU, FS);
}

FeatureBitset MCSubtargetInfo::ToggleFeature(uint64_t FB) {
  FeatureBits.flip(FB);
  return FeatureBits;
}

FeatureBitset MCSubtargetInfo::ToggleFeature(const FeatureBitset &FB) {
  FeatureBits ^= FB;
  return FeatureBits;
}

FeatureBitset MCSubtargetInfo::SetFeatureBitsTransitively(
  const FeatureBitset &FB) {
  SetImpliedBits(FeatureBits, FB, ProcFeatures);
  return FeatureBits;
}

FeatureBitset MCSubtargetInfo::ClearFeatureBitsTransitively(
  const FeatureBitset &FB) {
  for (unsigned I = 0, E = FB.size(); I < E; I++) {
    if (FB[I]) {
      FeatureBits.reset(I);
      ClearImpliedBits(FeatureBits, I, ProcFeatures);
    }
  }
  return FeatureBits;
}

FeatureBitset MCSubtargetInfo::ToggleFeature(StringRef Feature) {
  // Find feature in table.
  const SubtargetFeatureKV *FeatureEntry =
      Find(SubtargetFeatures::StripFlag(Feature), ProcFeatures);
  // If there is a match
  if (FeatureEntry) {
    if (FeatureBits.test(FeatureEntry->Value)) {
      FeatureBits.reset(FeatureEntry->Value);
      // For each feature that implies this, clear it.
      ClearImpliedBits(FeatureBits, FeatureEntry->Value, ProcFeatures);
    } else {
      FeatureBits.set(FeatureEntry->Value);

      // For each feature that this implies, set it.
      SetImpliedBits(FeatureBits, FeatureEntry->Implies.getAsBitset(),
                     ProcFeatures);
    }
  } else {
    errs() << "'" << Feature << "' is not a recognized feature for this target"
           << " (ignoring feature)\n";
  }

  return FeatureBits;
}

FeatureBitset MCSubtargetInfo::ApplyFeatureFlag(StringRef FS) {
  ::ApplyFeatureFlag(FeatureBits, FS, ProcFeatures);
  return FeatureBits;
}

bool MCSubtargetInfo::checkFeatures(StringRef FS) const {
  SubtargetFeatures T(FS);
  FeatureBitset Set, All;
  for (std::string F : T.getFeatures()) {
    ::ApplyFeatureFlag(Set, F, ProcFeatures);
    if (F[0] == '-')
      F[0] = '+';
    ::ApplyFeatureFlag(All, F, ProcFeatures);
  }
  return (FeatureBits & All) == Set;
}

const MCSchedModel &MCSubtargetInfo::getSchedModelForCPU(StringRef CPU) const {
  assert(llvm::is_sorted(ProcDesc) &&
         "Processor machine model table is not sorted");

  // Find entry
  const SubtargetSubTypeKV *CPUEntry = Find(CPU, ProcDesc);

  if (!CPUEntry) {
    if (CPU != "help") // Don't error if the user asked for help.
      errs() << "'" << CPU
             << "' is not a recognized processor for this target"
             << " (ignoring processor)\n";
    return MCSchedModel::Default;
  }
  assert(CPUEntry->SchedModel && "Missing processor SchedModel value");
  return *CPUEntry->SchedModel;
}

InstrItineraryData
MCSubtargetInfo::getInstrItineraryForCPU(StringRef CPU) const {
  const MCSchedModel &SchedModel = getSchedModelForCPU(CPU);
  return InstrItineraryData(SchedModel, Stages, OperandCycles, ForwardingPaths);
}

void MCSubtargetInfo::initInstrItins(InstrItineraryData &InstrItins) const {
  InstrItins = InstrItineraryData(getSchedModel(), Stages, OperandCycles,
                                  ForwardingPaths);
}

std::vector<SubtargetFeatureKV>
MCSubtargetInfo::getEnabledProcessorFeatures() const {
  std::vector<SubtargetFeatureKV> EnabledFeatures;
  auto IsEnabled = [&](const SubtargetFeatureKV &FeatureKV) {
    return FeatureBits.test(FeatureKV.Value);
  };
  llvm::copy_if(ProcFeatures, std::back_inserter(EnabledFeatures), IsEnabled);
  return EnabledFeatures;
}

std::optional<unsigned> MCSubtargetInfo::getCacheSize(unsigned Level) const {
  return std::nullopt;
}

std::optional<unsigned>
MCSubtargetInfo::getCacheAssociativity(unsigned Level) const {
  return std::nullopt;
}

std::optional<unsigned>
MCSubtargetInfo::getCacheLineSize(unsigned Level) const {
  return std::nullopt;
}

unsigned MCSubtargetInfo::getPrefetchDistance() const {
  return 0;
}

unsigned MCSubtargetInfo::getMaxPrefetchIterationsAhead() const {
  return UINT_MAX;
}

bool MCSubtargetInfo::enableWritePrefetching() const {
  return false;
}

unsigned MCSubtargetInfo::getMinPrefetchStride(unsigned NumMemAccesses,
                                               unsigned NumStridedMemAccesses,
                                               unsigned NumPrefetches,
                                               bool HasCall) const {
  return 1;
}

bool MCSubtargetInfo::shouldPrefetchAddressSpace(unsigned AS) const {
  return !AS;
}
