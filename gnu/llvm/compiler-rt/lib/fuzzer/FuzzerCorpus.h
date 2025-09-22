//===- FuzzerCorpus.h - Internal header for the Fuzzer ----------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// fuzzer::InputCorpus
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_CORPUS
#define LLVM_FUZZER_CORPUS

#include "FuzzerDataFlowTrace.h"
#include "FuzzerDefs.h"
#include "FuzzerIO.h"
#include "FuzzerRandom.h"
#include "FuzzerSHA1.h"
#include "FuzzerTracePC.h"
#include <algorithm>
#include <bitset>
#include <chrono>
#include <numeric>
#include <random>
#include <unordered_set>

namespace fuzzer {

struct InputInfo {
  Unit U;  // The actual input data.
  std::chrono::microseconds TimeOfUnit;
  uint8_t Sha1[kSHA1NumBytes];  // Checksum.
  // Number of features that this input has and no smaller input has.
  size_t NumFeatures = 0;
  size_t Tmp = 0; // Used by ValidateFeatureSet.
  // Stats.
  size_t NumExecutedMutations = 0;
  size_t NumSuccessfullMutations = 0;
  bool NeverReduce = false;
  bool MayDeleteFile = false;
  bool Reduced = false;
  bool HasFocusFunction = false;
  std::vector<uint32_t> UniqFeatureSet;
  std::vector<uint8_t> DataFlowTraceForFocusFunction;
  // Power schedule.
  bool NeedsEnergyUpdate = false;
  double Energy = 0.0;
  double SumIncidence = 0.0;
  std::vector<std::pair<uint32_t, uint16_t>> FeatureFreqs;

  // Delete feature Idx and its frequency from FeatureFreqs.
  bool DeleteFeatureFreq(uint32_t Idx) {
    if (FeatureFreqs.empty())
      return false;

    // Binary search over local feature frequencies sorted by index.
    auto Lower = std::lower_bound(FeatureFreqs.begin(), FeatureFreqs.end(),
                                  std::pair<uint32_t, uint16_t>(Idx, 0));

    if (Lower != FeatureFreqs.end() && Lower->first == Idx) {
      FeatureFreqs.erase(Lower);
      return true;
    }
    return false;
  }

  // Assign more energy to a high-entropy seed, i.e., that reveals more
  // information about the globally rare features in the neighborhood of the
  // seed. Since we do not know the entropy of a seed that has never been
  // executed we assign fresh seeds maximum entropy and let II->Energy approach
  // the true entropy from above. If ScalePerExecTime is true, the computed
  // entropy is scaled based on how fast this input executes compared to the
  // average execution time of inputs. The faster an input executes, the more
  // energy gets assigned to the input.
  void UpdateEnergy(size_t GlobalNumberOfFeatures, bool ScalePerExecTime,
                    std::chrono::microseconds AverageUnitExecutionTime) {
    Energy = 0.0;
    SumIncidence = 0.0;

    // Apply add-one smoothing to locally discovered features.
    for (const auto &F : FeatureFreqs) {
      double LocalIncidence = F.second + 1;
      Energy -= LocalIncidence * log(LocalIncidence);
      SumIncidence += LocalIncidence;
    }

    // Apply add-one smoothing to locally undiscovered features.
    //   PreciseEnergy -= 0; // since log(1.0) == 0)
    SumIncidence +=
        static_cast<double>(GlobalNumberOfFeatures - FeatureFreqs.size());

    // Add a single locally abundant feature apply add-one smoothing.
    double AbdIncidence = static_cast<double>(NumExecutedMutations + 1);
    Energy -= AbdIncidence * log(AbdIncidence);
    SumIncidence += AbdIncidence;

    // Normalize.
    if (SumIncidence != 0)
      Energy = Energy / SumIncidence + log(SumIncidence);

    if (ScalePerExecTime) {
      // Scaling to favor inputs with lower execution time.
      uint32_t PerfScore = 100;
      if (TimeOfUnit.count() > AverageUnitExecutionTime.count() * 10)
        PerfScore = 10;
      else if (TimeOfUnit.count() > AverageUnitExecutionTime.count() * 4)
        PerfScore = 25;
      else if (TimeOfUnit.count() > AverageUnitExecutionTime.count() * 2)
        PerfScore = 50;
      else if (TimeOfUnit.count() * 3 > AverageUnitExecutionTime.count() * 4)
        PerfScore = 75;
      else if (TimeOfUnit.count() * 4 < AverageUnitExecutionTime.count())
        PerfScore = 300;
      else if (TimeOfUnit.count() * 3 < AverageUnitExecutionTime.count())
        PerfScore = 200;
      else if (TimeOfUnit.count() * 2 < AverageUnitExecutionTime.count())
        PerfScore = 150;

      Energy *= PerfScore;
    }
  }

  // Increment the frequency of the feature Idx.
  void UpdateFeatureFrequency(uint32_t Idx) {
    NeedsEnergyUpdate = true;

    // The local feature frequencies is an ordered vector of pairs.
    // If there are no local feature frequencies, push_back preserves order.
    // Set the feature frequency for feature Idx32 to 1.
    if (FeatureFreqs.empty()) {
      FeatureFreqs.push_back(std::pair<uint32_t, uint16_t>(Idx, 1));
      return;
    }

    // Binary search over local feature frequencies sorted by index.
    auto Lower = std::lower_bound(FeatureFreqs.begin(), FeatureFreqs.end(),
                                  std::pair<uint32_t, uint16_t>(Idx, 0));

    // If feature Idx32 already exists, increment its frequency.
    // Otherwise, insert a new pair right after the next lower index.
    if (Lower != FeatureFreqs.end() && Lower->first == Idx) {
      Lower->second++;
    } else {
      FeatureFreqs.insert(Lower, std::pair<uint32_t, uint16_t>(Idx, 1));
    }
  }
};

struct EntropicOptions {
  bool Enabled;
  size_t NumberOfRarestFeatures;
  size_t FeatureFrequencyThreshold;
  bool ScalePerExecTime;
};

class InputCorpus {
  static const uint32_t kFeatureSetSize = 1 << 21;
  static const uint8_t kMaxMutationFactor = 20;
  static const size_t kSparseEnergyUpdates = 100;

  size_t NumExecutedMutations = 0;

  EntropicOptions Entropic;

public:
  InputCorpus(const std::string &OutputCorpus, EntropicOptions Entropic)
      : Entropic(Entropic), OutputCorpus(OutputCorpus) {
    memset(InputSizesPerFeature, 0, sizeof(InputSizesPerFeature));
    memset(SmallestElementPerFeature, 0, sizeof(SmallestElementPerFeature));
  }
  ~InputCorpus() {
    for (auto II : Inputs)
      delete II;
  }
  size_t size() const { return Inputs.size(); }
  size_t SizeInBytes() const {
    size_t Res = 0;
    for (auto II : Inputs)
      Res += II->U.size();
    return Res;
  }
  size_t NumActiveUnits() const {
    size_t Res = 0;
    for (auto II : Inputs)
      Res += !II->U.empty();
    return Res;
  }
  size_t MaxInputSize() const {
    size_t Res = 0;
    for (auto II : Inputs)
        Res = std::max(Res, II->U.size());
    return Res;
  }
  void IncrementNumExecutedMutations() { NumExecutedMutations++; }

  size_t NumInputsThatTouchFocusFunction() {
    return std::count_if(Inputs.begin(), Inputs.end(), [](const InputInfo *II) {
      return II->HasFocusFunction;
    });
  }

  size_t NumInputsWithDataFlowTrace() {
    return std::count_if(Inputs.begin(), Inputs.end(), [](const InputInfo *II) {
      return !II->DataFlowTraceForFocusFunction.empty();
    });
  }

  bool empty() const { return Inputs.empty(); }
  const Unit &operator[] (size_t Idx) const { return Inputs[Idx]->U; }
  InputInfo *AddToCorpus(const Unit &U, size_t NumFeatures, bool MayDeleteFile,
                         bool HasFocusFunction, bool NeverReduce,
                         std::chrono::microseconds TimeOfUnit,
                         const std::vector<uint32_t> &FeatureSet,
                         const DataFlowTrace &DFT, const InputInfo *BaseII) {
    assert(!U.empty());
    if (FeatureDebug)
      Printf("ADD_TO_CORPUS %zd NF %zd\n", Inputs.size(), NumFeatures);
    // Inputs.size() is cast to uint32_t below.
    assert(Inputs.size() < std::numeric_limits<uint32_t>::max());
    Inputs.push_back(new InputInfo());
    InputInfo &II = *Inputs.back();
    II.U = U;
    II.NumFeatures = NumFeatures;
    II.NeverReduce = NeverReduce;
    II.TimeOfUnit = TimeOfUnit;
    II.MayDeleteFile = MayDeleteFile;
    II.UniqFeatureSet = FeatureSet;
    II.HasFocusFunction = HasFocusFunction;
    // Assign maximal energy to the new seed.
    II.Energy = RareFeatures.empty() ? 1.0 : log(RareFeatures.size());
    II.SumIncidence = static_cast<double>(RareFeatures.size());
    II.NeedsEnergyUpdate = false;
    std::sort(II.UniqFeatureSet.begin(), II.UniqFeatureSet.end());
    ComputeSHA1(U.data(), U.size(), II.Sha1);
    auto Sha1Str = Sha1ToString(II.Sha1);
    Hashes.insert(Sha1Str);
    if (HasFocusFunction)
      if (auto V = DFT.Get(Sha1Str))
        II.DataFlowTraceForFocusFunction = *V;
    // This is a gross heuristic.
    // Ideally, when we add an element to a corpus we need to know its DFT.
    // But if we don't, we'll use the DFT of its base input.
    if (II.DataFlowTraceForFocusFunction.empty() && BaseII)
      II.DataFlowTraceForFocusFunction = BaseII->DataFlowTraceForFocusFunction;
    DistributionNeedsUpdate = true;
    PrintCorpus();
    // ValidateFeatureSet();
    return &II;
  }

  // Debug-only
  void PrintUnit(const Unit &U) {
    if (!FeatureDebug) return;
    for (uint8_t C : U) {
      if (C != 'F' && C != 'U' && C != 'Z')
        C = '.';
      Printf("%c", C);
    }
  }

  // Debug-only
  void PrintFeatureSet(const std::vector<uint32_t> &FeatureSet) {
    if (!FeatureDebug) return;
    Printf("{");
    for (uint32_t Feature: FeatureSet)
      Printf("%u,", Feature);
    Printf("}");
  }

  // Debug-only
  void PrintCorpus() {
    if (!FeatureDebug) return;
    Printf("======= CORPUS:\n");
    int i = 0;
    for (auto II : Inputs) {
      if (std::find(II->U.begin(), II->U.end(), 'F') != II->U.end()) {
        Printf("[%2d] ", i);
        Printf("%s sz=%zd ", Sha1ToString(II->Sha1).c_str(), II->U.size());
        PrintUnit(II->U);
        Printf(" ");
        PrintFeatureSet(II->UniqFeatureSet);
        Printf("\n");
      }
      i++;
    }
  }

  void Replace(InputInfo *II, const Unit &U,
               std::chrono::microseconds TimeOfUnit) {
    assert(II->U.size() > U.size());
    Hashes.erase(Sha1ToString(II->Sha1));
    DeleteFile(*II);
    ComputeSHA1(U.data(), U.size(), II->Sha1);
    Hashes.insert(Sha1ToString(II->Sha1));
    II->U = U;
    II->Reduced = true;
    II->TimeOfUnit = TimeOfUnit;
    DistributionNeedsUpdate = true;
  }

  bool HasUnit(const Unit &U) { return Hashes.count(Hash(U)); }
  bool HasUnit(const std::string &H) { return Hashes.count(H); }
  InputInfo &ChooseUnitToMutate(Random &Rand) {
    InputInfo &II = *Inputs[ChooseUnitIdxToMutate(Rand)];
    assert(!II.U.empty());
    return II;
  }

  InputInfo &ChooseUnitToCrossOverWith(Random &Rand, bool UniformDist) {
    if (!UniformDist) {
      return ChooseUnitToMutate(Rand);
    }
    InputInfo &II = *Inputs[Rand(Inputs.size())];
    assert(!II.U.empty());
    return II;
  }

  // Returns an index of random unit from the corpus to mutate.
  size_t ChooseUnitIdxToMutate(Random &Rand) {
    UpdateCorpusDistribution(Rand);
    size_t Idx = static_cast<size_t>(CorpusDistribution(Rand));
    assert(Idx < Inputs.size());
    return Idx;
  }

  void PrintStats() {
    for (size_t i = 0; i < Inputs.size(); i++) {
      const auto &II = *Inputs[i];
      Printf("  [% 3zd %s] sz: % 5zd runs: % 5zd succ: % 5zd focus: %d\n", i,
             Sha1ToString(II.Sha1).c_str(), II.U.size(),
             II.NumExecutedMutations, II.NumSuccessfullMutations,
             II.HasFocusFunction);
    }
  }

  void PrintFeatureSet() {
    for (size_t i = 0; i < kFeatureSetSize; i++) {
      if(size_t Sz = GetFeature(i))
        Printf("[%zd: id %zd sz%zd] ", i, SmallestElementPerFeature[i], Sz);
    }
    Printf("\n\t");
    for (size_t i = 0; i < Inputs.size(); i++)
      if (size_t N = Inputs[i]->NumFeatures)
        Printf(" %zd=>%zd ", i, N);
    Printf("\n");
  }

  void DeleteFile(const InputInfo &II) {
    if (!OutputCorpus.empty() && II.MayDeleteFile)
      RemoveFile(DirPlusFile(OutputCorpus, Sha1ToString(II.Sha1)));
  }

  void DeleteInput(size_t Idx) {
    InputInfo &II = *Inputs[Idx];
    DeleteFile(II);
    Unit().swap(II.U);
    II.Energy = 0.0;
    II.NeedsEnergyUpdate = false;
    DistributionNeedsUpdate = true;
    if (FeatureDebug)
      Printf("EVICTED %zd\n", Idx);
  }

  void AddRareFeature(uint32_t Idx) {
    // Maintain *at least* TopXRarestFeatures many rare features
    // and all features with a frequency below ConsideredRare.
    // Remove all other features.
    while (RareFeatures.size() > Entropic.NumberOfRarestFeatures &&
           FreqOfMostAbundantRareFeature > Entropic.FeatureFrequencyThreshold) {

      // Find most and second most abbundant feature.
      uint32_t MostAbundantRareFeatureIndices[2] = {RareFeatures[0],
                                                    RareFeatures[0]};
      size_t Delete = 0;
      for (size_t i = 0; i < RareFeatures.size(); i++) {
        uint32_t Idx2 = RareFeatures[i];
        if (GlobalFeatureFreqs[Idx2] >=
            GlobalFeatureFreqs[MostAbundantRareFeatureIndices[0]]) {
          MostAbundantRareFeatureIndices[1] = MostAbundantRareFeatureIndices[0];
          MostAbundantRareFeatureIndices[0] = Idx2;
          Delete = i;
        }
      }

      // Remove most abundant rare feature.
      IsRareFeature[Delete] = false;
      RareFeatures[Delete] = RareFeatures.back();
      RareFeatures.pop_back();

      for (auto II : Inputs) {
        if (II->DeleteFeatureFreq(MostAbundantRareFeatureIndices[0]))
          II->NeedsEnergyUpdate = true;
      }

      // Set 2nd most abundant as the new most abundant feature count.
      FreqOfMostAbundantRareFeature =
          GlobalFeatureFreqs[MostAbundantRareFeatureIndices[1]];
    }

    // Add rare feature, handle collisions, and update energy.
    RareFeatures.push_back(Idx);
    IsRareFeature[Idx] = true;
    GlobalFeatureFreqs[Idx] = 0;
    for (auto II : Inputs) {
      II->DeleteFeatureFreq(Idx);

      // Apply add-one smoothing to this locally undiscovered feature.
      // Zero energy seeds will never be fuzzed and remain zero energy.
      if (II->Energy > 0.0) {
        II->SumIncidence += 1;
        II->Energy += log(II->SumIncidence) / II->SumIncidence;
      }
    }

    DistributionNeedsUpdate = true;
  }

  bool AddFeature(size_t Idx, uint32_t NewSize, bool Shrink) {
    assert(NewSize);
    Idx = Idx % kFeatureSetSize;
    uint32_t OldSize = GetFeature(Idx);
    if (OldSize == 0 || (Shrink && OldSize > NewSize)) {
      if (OldSize > 0) {
        size_t OldIdx = SmallestElementPerFeature[Idx];
        InputInfo &II = *Inputs[OldIdx];
        assert(II.NumFeatures > 0);
        II.NumFeatures--;
        if (II.NumFeatures == 0)
          DeleteInput(OldIdx);
      } else {
        NumAddedFeatures++;
        if (Entropic.Enabled)
          AddRareFeature((uint32_t)Idx);
      }
      NumUpdatedFeatures++;
      if (FeatureDebug)
        Printf("ADD FEATURE %zd sz %d\n", Idx, NewSize);
      // Inputs.size() is guaranteed to be less than UINT32_MAX by AddToCorpus.
      SmallestElementPerFeature[Idx] = static_cast<uint32_t>(Inputs.size());
      InputSizesPerFeature[Idx] = NewSize;
      return true;
    }
    return false;
  }

  // Increment frequency of feature Idx globally and locally.
  void UpdateFeatureFrequency(InputInfo *II, size_t Idx) {
    uint32_t Idx32 = Idx % kFeatureSetSize;

    // Saturated increment.
    if (GlobalFeatureFreqs[Idx32] == 0xFFFF)
      return;
    uint16_t Freq = GlobalFeatureFreqs[Idx32]++;

    // Skip if abundant.
    if (Freq > FreqOfMostAbundantRareFeature || !IsRareFeature[Idx32])
      return;

    // Update global frequencies.
    if (Freq == FreqOfMostAbundantRareFeature)
      FreqOfMostAbundantRareFeature++;

    // Update local frequencies.
    if (II)
      II->UpdateFeatureFrequency(Idx32);
  }

  size_t NumFeatures() const { return NumAddedFeatures; }
  size_t NumFeatureUpdates() const { return NumUpdatedFeatures; }

private:

  static const bool FeatureDebug = false;

  uint32_t GetFeature(size_t Idx) const { return InputSizesPerFeature[Idx]; }

  void ValidateFeatureSet() {
    if (FeatureDebug)
      PrintFeatureSet();
    for (size_t Idx = 0; Idx < kFeatureSetSize; Idx++)
      if (GetFeature(Idx))
        Inputs[SmallestElementPerFeature[Idx]]->Tmp++;
    for (auto II: Inputs) {
      if (II->Tmp != II->NumFeatures)
        Printf("ZZZ %zd %zd\n", II->Tmp, II->NumFeatures);
      assert(II->Tmp == II->NumFeatures);
      II->Tmp = 0;
    }
  }

  // Updates the probability distribution for the units in the corpus.
  // Must be called whenever the corpus or unit weights are changed.
  //
  // Hypothesis: inputs that maximize information about globally rare features
  // are interesting.
  void UpdateCorpusDistribution(Random &Rand) {
    // Skip update if no seeds or rare features were added/deleted.
    // Sparse updates for local change of feature frequencies,
    // i.e., randomly do not skip.
    if (!DistributionNeedsUpdate &&
        (!Entropic.Enabled || Rand(kSparseEnergyUpdates)))
      return;

    DistributionNeedsUpdate = false;

    size_t N = Inputs.size();
    assert(N);
    Intervals.resize(N + 1);
    Weights.resize(N);
    std::iota(Intervals.begin(), Intervals.end(), 0);

    std::chrono::microseconds AverageUnitExecutionTime(0);
    for (auto II : Inputs) {
      AverageUnitExecutionTime += II->TimeOfUnit;
    }
    AverageUnitExecutionTime /= N;

    bool VanillaSchedule = true;
    if (Entropic.Enabled) {
      for (auto II : Inputs) {
        if (II->NeedsEnergyUpdate && II->Energy != 0.0) {
          II->NeedsEnergyUpdate = false;
          II->UpdateEnergy(RareFeatures.size(), Entropic.ScalePerExecTime,
                           AverageUnitExecutionTime);
        }
      }

      for (size_t i = 0; i < N; i++) {

        if (Inputs[i]->NumFeatures == 0) {
          // If the seed doesn't represent any features, assign zero energy.
          Weights[i] = 0.;
        } else if (Inputs[i]->NumExecutedMutations / kMaxMutationFactor >
                   NumExecutedMutations / Inputs.size()) {
          // If the seed was fuzzed a lot more than average, assign zero energy.
          Weights[i] = 0.;
        } else {
          // Otherwise, simply assign the computed energy.
          Weights[i] = Inputs[i]->Energy;
        }

        // If energy for all seeds is zero, fall back to vanilla schedule.
        if (Weights[i] > 0.0)
          VanillaSchedule = false;
      }
    }

    if (VanillaSchedule) {
      for (size_t i = 0; i < N; i++)
        Weights[i] =
            Inputs[i]->NumFeatures
                ? static_cast<double>((i + 1) *
                                      (Inputs[i]->HasFocusFunction ? 1000 : 1))
                : 0.;
    }

    if (FeatureDebug) {
      for (size_t i = 0; i < N; i++)
        Printf("%zd ", Inputs[i]->NumFeatures);
      Printf("SCORE\n");
      for (size_t i = 0; i < N; i++)
        Printf("%f ", Weights[i]);
      Printf("Weights\n");
    }
    CorpusDistribution = std::piecewise_constant_distribution<double>(
        Intervals.begin(), Intervals.end(), Weights.begin());
  }
  std::piecewise_constant_distribution<double> CorpusDistribution;

  std::vector<double> Intervals;
  std::vector<double> Weights;

  std::unordered_set<std::string> Hashes;
  std::vector<InputInfo *> Inputs;

  size_t NumAddedFeatures = 0;
  size_t NumUpdatedFeatures = 0;
  uint32_t InputSizesPerFeature[kFeatureSetSize];
  uint32_t SmallestElementPerFeature[kFeatureSetSize];

  bool DistributionNeedsUpdate = true;
  uint16_t FreqOfMostAbundantRareFeature = 0;
  uint16_t GlobalFeatureFreqs[kFeatureSetSize] = {};
  std::vector<uint32_t> RareFeatures;
  std::bitset<kFeatureSetSize> IsRareFeature;

  std::string OutputCorpus;
};

}  // namespace fuzzer

#endif  // LLVM_FUZZER_CORPUS
