//===- FuzzerMerge.cpp - merging corpora ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Merging corpora.
//===----------------------------------------------------------------------===//

#include "FuzzerCommand.h"
#include "FuzzerMerge.h"
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include "FuzzerTracePC.h"
#include "FuzzerUtil.h"

#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_set>

namespace fuzzer {

bool Merger::Parse(const std::string &Str, bool ParseCoverage) {
  std::istringstream SS(Str);
  return Parse(SS, ParseCoverage);
}

void Merger::ParseOrExit(std::istream &IS, bool ParseCoverage) {
  if (!Parse(IS, ParseCoverage)) {
    Printf("MERGE: failed to parse the control file (unexpected error)\n");
    exit(1);
  }
}

// The control file example:
//
// 3 # The number of inputs
// 1 # The number of inputs in the first corpus, <= the previous number
// file0
// file1
// file2  # One file name per line.
// STARTED 0 123  # FileID, file size
// FT 0 1 4 6 8  # FileID COV1 COV2 ...
// COV 0 7 8 9 # FileID COV1 COV1
// STARTED 1 456  # If FT is missing, the input crashed while processing.
// STARTED 2 567
// FT 2 8 9
// COV 2 11 12
bool Merger::Parse(std::istream &IS, bool ParseCoverage) {
  LastFailure.clear();
  std::string Line;

  // Parse NumFiles.
  if (!std::getline(IS, Line, '\n')) return false;
  std::istringstream L1(Line);
  size_t NumFiles = 0;
  L1 >> NumFiles;
  if (NumFiles == 0 || NumFiles > 10000000) return false;

  // Parse NumFilesInFirstCorpus.
  if (!std::getline(IS, Line, '\n')) return false;
  std::istringstream L2(Line);
  NumFilesInFirstCorpus = NumFiles + 1;
  L2 >> NumFilesInFirstCorpus;
  if (NumFilesInFirstCorpus > NumFiles) return false;

  // Parse file names.
  Files.resize(NumFiles);
  for (size_t i = 0; i < NumFiles; i++)
    if (!std::getline(IS, Files[i].Name, '\n'))
      return false;

  // Parse STARTED, FT, and COV lines.
  size_t ExpectedStartMarker = 0;
  const size_t kInvalidStartMarker = -1;
  size_t LastSeenStartMarker = kInvalidStartMarker;
  bool HaveFtMarker = true;
  std::vector<uint32_t> TmpFeatures;
  std::set<uint32_t> PCs;
  while (std::getline(IS, Line, '\n')) {
    std::istringstream ISS1(Line);
    std::string Marker;
    uint32_t N;
    if (!(ISS1 >> Marker) || !(ISS1 >> N))
      return false;
    if (Marker == "STARTED") {
      // STARTED FILE_ID FILE_SIZE
      if (ExpectedStartMarker != N)
        return false;
      ISS1 >> Files[ExpectedStartMarker].Size;
      LastSeenStartMarker = ExpectedStartMarker;
      assert(ExpectedStartMarker < Files.size());
      ExpectedStartMarker++;
      HaveFtMarker = false;
    } else if (Marker == "FT") {
      // FT FILE_ID COV1 COV2 COV3 ...
      size_t CurrentFileIdx = N;
      if (CurrentFileIdx != LastSeenStartMarker)
        return false;
      HaveFtMarker = true;
      if (ParseCoverage) {
        TmpFeatures.clear();  // use a vector from outer scope to avoid resizes.
        while (ISS1 >> N)
          TmpFeatures.push_back(N);
        std::sort(TmpFeatures.begin(), TmpFeatures.end());
        Files[CurrentFileIdx].Features = TmpFeatures;
      }
    } else if (Marker == "COV") {
      size_t CurrentFileIdx = N;
      if (CurrentFileIdx != LastSeenStartMarker)
        return false;
      if (ParseCoverage)
        while (ISS1 >> N)
          if (PCs.insert(N).second)
            Files[CurrentFileIdx].Cov.push_back(N);
    } else {
      return false;
    }
  }
  if (!HaveFtMarker && LastSeenStartMarker != kInvalidStartMarker)
    LastFailure = Files[LastSeenStartMarker].Name;

  FirstNotProcessedFile = ExpectedStartMarker;
  return true;
}

size_t Merger::ApproximateMemoryConsumption() const  {
  size_t Res = 0;
  for (const auto &F: Files)
    Res += sizeof(F) + F.Features.size() * sizeof(F.Features[0]);
  return Res;
}

// Decides which files need to be merged (add those to NewFiles).
// Returns the number of new features added.
size_t Merger::Merge(const std::set<uint32_t> &InitialFeatures,
                     std::set<uint32_t> *NewFeatures,
                     const std::set<uint32_t> &InitialCov,
                     std::set<uint32_t> *NewCov,
                     std::vector<std::string> *NewFiles) {
  NewFiles->clear();
  NewFeatures->clear();
  NewCov->clear();
  assert(NumFilesInFirstCorpus <= Files.size());
  std::set<uint32_t> AllFeatures = InitialFeatures;

  // What features are in the initial corpus?
  for (size_t i = 0; i < NumFilesInFirstCorpus; i++) {
    auto &Cur = Files[i].Features;
    AllFeatures.insert(Cur.begin(), Cur.end());
  }
  // Remove all features that we already know from all other inputs.
  for (size_t i = NumFilesInFirstCorpus; i < Files.size(); i++) {
    auto &Cur = Files[i].Features;
    std::vector<uint32_t> Tmp;
    std::set_difference(Cur.begin(), Cur.end(), AllFeatures.begin(),
                        AllFeatures.end(), std::inserter(Tmp, Tmp.begin()));
    Cur.swap(Tmp);
  }

  // Sort. Give preference to
  //   * smaller files
  //   * files with more features.
  std::sort(Files.begin() + NumFilesInFirstCorpus, Files.end(),
            [&](const MergeFileInfo &a, const MergeFileInfo &b) -> bool {
              if (a.Size != b.Size)
                return a.Size < b.Size;
              return a.Features.size() > b.Features.size();
            });

  // One greedy pass: add the file's features to AllFeatures.
  // If new features were added, add this file to NewFiles.
  for (size_t i = NumFilesInFirstCorpus; i < Files.size(); i++) {
    auto &Cur = Files[i].Features;
    // Printf("%s -> sz %zd ft %zd\n", Files[i].Name.c_str(),
    //       Files[i].Size, Cur.size());
    bool FoundNewFeatures = false;
    for (auto Fe: Cur) {
      if (AllFeatures.insert(Fe).second) {
        FoundNewFeatures = true;
        NewFeatures->insert(Fe);
      }
    }
    if (FoundNewFeatures)
      NewFiles->push_back(Files[i].Name);
    for (auto Cov : Files[i].Cov)
      if (InitialCov.find(Cov) == InitialCov.end())
        NewCov->insert(Cov);
  }
  return NewFeatures->size();
}

std::set<uint32_t> Merger::AllFeatures() const {
  std::set<uint32_t> S;
  for (auto &File : Files)
    S.insert(File.Features.begin(), File.Features.end());
  return S;
}

// Inner process. May crash if the target crashes.
void Fuzzer::CrashResistantMergeInternalStep(const std::string &CFPath,
                                             bool IsSetCoverMerge) {
  Printf("MERGE-INNER: using the control file '%s'\n", CFPath.c_str());
  Merger M;
  std::ifstream IF(CFPath);
  M.ParseOrExit(IF, false);
  IF.close();
  if (!M.LastFailure.empty())
    Printf("MERGE-INNER: '%s' caused a failure at the previous merge step\n",
           M.LastFailure.c_str());

  Printf("MERGE-INNER: %zd total files;"
         " %zd processed earlier; will process %zd files now\n",
         M.Files.size(), M.FirstNotProcessedFile,
         M.Files.size() - M.FirstNotProcessedFile);

  std::ofstream OF(CFPath, std::ofstream::out | std::ofstream::app);
  std::set<size_t> AllFeatures;
  auto PrintStatsWrapper = [this, &AllFeatures](const char* Where) {
    this->PrintStats(Where, "\n", 0, AllFeatures.size());
  };
  std::set<const TracePC::PCTableEntry *> AllPCs;
  for (size_t i = M.FirstNotProcessedFile; i < M.Files.size(); i++) {
    Fuzzer::MaybeExitGracefully();
    auto U = FileToVector(M.Files[i].Name);
    if (U.size() > MaxInputLen) {
      U.resize(MaxInputLen);
      U.shrink_to_fit();
    }

    // Write the pre-run marker.
    OF << "STARTED " << i << " " << U.size() << "\n";
    OF.flush();  // Flush is important since Command::Execute may crash.
    // Run.
    TPC.ResetMaps();
    ExecuteCallback(U.data(), U.size());
    // Collect coverage. We are iterating over the files in this order:
    // * First, files in the initial corpus ordered by size, smallest first.
    // * Then, all other files, smallest first.
    std::set<size_t> Features;
    if (IsSetCoverMerge)
      TPC.CollectFeatures([&](size_t Feature) { Features.insert(Feature); });
    else
      TPC.CollectFeatures([&](size_t Feature) {
        if (AllFeatures.insert(Feature).second)
          Features.insert(Feature);
      });
    TPC.UpdateObservedPCs();
    // Show stats.
    if (!(TotalNumberOfRuns & (TotalNumberOfRuns - 1)))
      PrintStatsWrapper("pulse ");
    if (TotalNumberOfRuns == M.NumFilesInFirstCorpus)
      PrintStatsWrapper("LOADED");
    // Write the post-run marker and the coverage.
    OF << "FT " << i;
    for (size_t F : Features)
      OF << " " << F;
    OF << "\n";
    OF << "COV " << i;
    TPC.ForEachObservedPC([&](const TracePC::PCTableEntry *TE) {
      if (AllPCs.insert(TE).second)
        OF << " " << TPC.PCTableEntryIdx(TE);
    });
    OF << "\n";
    OF.flush();
  }
  PrintStatsWrapper("DONE  ");
}

// Merges all corpora into the first corpus. A file is added into
// the first corpus only if it adds new features. Unlike `Merger::Merge`,
// this implementation calculates an approximation of the minimum set
// of corpora files, that cover all known features (set cover problem).
// Generally, this means that files with more features are preferred for
// merge into the first corpus. When two files have the same number of
// features, the smaller one is preferred.
size_t Merger::SetCoverMerge(const std::set<uint32_t> &InitialFeatures,
                             std::set<uint32_t> *NewFeatures,
                             const std::set<uint32_t> &InitialCov,
                             std::set<uint32_t> *NewCov,
                             std::vector<std::string> *NewFiles) {
  assert(NumFilesInFirstCorpus <= Files.size());
  NewFiles->clear();
  NewFeatures->clear();
  NewCov->clear();
  std::set<uint32_t> AllFeatures;
  // 1 << 21 - 1 is the maximum feature index.
  // See 'kFeatureSetSize' in 'FuzzerCorpus.h'.
  const uint32_t kFeatureSetSize = 1 << 21;
  std::vector<bool> Covered(kFeatureSetSize, false);
  size_t NumCovered = 0;

  std::set<uint32_t> ExistingFeatures = InitialFeatures;
  for (size_t i = 0; i < NumFilesInFirstCorpus; ++i)
    ExistingFeatures.insert(Files[i].Features.begin(), Files[i].Features.end());

  // Mark the existing features as covered.
  for (const auto &F : ExistingFeatures) {
    if (!Covered[F % kFeatureSetSize]) {
      ++NumCovered;
      Covered[F % kFeatureSetSize] = true;
    }
    // Calculate an underestimation of the set of covered features
    // since the `Covered` bitvector is smaller than the feature range.
    AllFeatures.insert(F % kFeatureSetSize);
  }

  std::set<size_t> RemainingFiles;
  for (size_t i = NumFilesInFirstCorpus; i < Files.size(); ++i) {
    // Construct an incremental sequence which represent the
    // indices to all files (excluding those in the initial corpus).
    // RemainingFiles = range(NumFilesInFirstCorpus..Files.size()).
    RemainingFiles.insert(i);
    // Insert this file's unique features to all features.
    for (const auto &F : Files[i].Features)
      AllFeatures.insert(F % kFeatureSetSize);
  }

  // Integrate files into Covered until set is complete.
  while (NumCovered != AllFeatures.size()) {
    // Index to file with largest number of unique features.
    size_t MaxFeaturesIndex = NumFilesInFirstCorpus;
    // Indices to remove from RemainingFiles.
    std::set<size_t> RemoveIndices;
    // Running max unique feature count.
    // Updated upon finding a file with more features.
    size_t MaxNumFeatures = 0;

    // Iterate over all files not yet integrated into Covered,
    // to find the file which has the largest number of
    // features that are not already in Covered.
    for (const auto &i : RemainingFiles) {
      const auto &File = Files[i];
      size_t CurrentUnique = 0;
      // Count number of features in this file
      // which are not yet in Covered.
      for (const auto &F : File.Features)
        if (!Covered[F % kFeatureSetSize])
          ++CurrentUnique;

      if (CurrentUnique == 0) {
        // All features in this file are already in Covered: skip next time.
        RemoveIndices.insert(i);
      } else if (CurrentUnique > MaxNumFeatures ||
                 (CurrentUnique == MaxNumFeatures &&
                  File.Size < Files[MaxFeaturesIndex].Size)) {
        // Update the max features file based on unique features
        // Break ties by selecting smaller files.
        MaxNumFeatures = CurrentUnique;
        MaxFeaturesIndex = i;
      }
    }
    // Must be a valid index/
    assert(MaxFeaturesIndex < Files.size());
    // Remove any feature-less files found.
    for (const auto &i : RemoveIndices)
      RemainingFiles.erase(i);
    if (MaxNumFeatures == 0) {
      // Did not find a file that adds unique features.
      // This means that we should have no remaining files.
      assert(RemainingFiles.size() == 0);
      assert(NumCovered == AllFeatures.size());
      break;
    }

    // MaxFeaturesIndex must be an element of Remaining.
    assert(RemainingFiles.find(MaxFeaturesIndex) != RemainingFiles.end());
    // Remove the file with the most features from Remaining.
    RemainingFiles.erase(MaxFeaturesIndex);
    const auto &MaxFeatureFile = Files[MaxFeaturesIndex];
    // Add the features of the max feature file to Covered.
    for (const auto &F : MaxFeatureFile.Features) {
      if (!Covered[F % kFeatureSetSize]) {
        ++NumCovered;
        Covered[F % kFeatureSetSize] = true;
        NewFeatures->insert(F);
      }
    }
    // Add the index to this file to the result.
    NewFiles->push_back(MaxFeatureFile.Name);
    // Update NewCov with the additional coverage
    // that MaxFeatureFile provides.
    for (const auto &C : MaxFeatureFile.Cov)
      if (InitialCov.find(C) == InitialCov.end())
        NewCov->insert(C);
  }

  return NewFeatures->size();
}

static size_t
WriteNewControlFile(const std::string &CFPath,
                    const std::vector<SizedFile> &OldCorpus,
                    const std::vector<SizedFile> &NewCorpus,
                    const std::vector<MergeFileInfo> &KnownFiles) {
  std::unordered_set<std::string> FilesToSkip;
  for (auto &SF: KnownFiles)
    FilesToSkip.insert(SF.Name);

  std::vector<std::string> FilesToUse;
  auto MaybeUseFile = [=, &FilesToUse](std::string Name) {
    if (FilesToSkip.find(Name) == FilesToSkip.end())
      FilesToUse.push_back(Name);
  };
  for (auto &SF: OldCorpus)
    MaybeUseFile(SF.File);
  auto FilesToUseFromOldCorpus = FilesToUse.size();
  for (auto &SF: NewCorpus)
    MaybeUseFile(SF.File);

  RemoveFile(CFPath);
  std::ofstream ControlFile(CFPath);
  ControlFile << FilesToUse.size() << "\n";
  ControlFile << FilesToUseFromOldCorpus << "\n";
  for (auto &FN: FilesToUse)
    ControlFile << FN << "\n";

  if (!ControlFile) {
    Printf("MERGE-OUTER: failed to write to the control file: %s\n",
           CFPath.c_str());
    exit(1);
  }

  return FilesToUse.size();
}

// Outer process. Does not call the target code and thus should not fail.
void CrashResistantMerge(const std::vector<std::string> &Args,
                         const std::vector<SizedFile> &OldCorpus,
                         const std::vector<SizedFile> &NewCorpus,
                         std::vector<std::string> *NewFiles,
                         const std::set<uint32_t> &InitialFeatures,
                         std::set<uint32_t> *NewFeatures,
                         const std::set<uint32_t> &InitialCov,
                         std::set<uint32_t> *NewCov, const std::string &CFPath,
                         bool V, /*Verbose*/
                         bool IsSetCoverMerge) {
  if (NewCorpus.empty() && OldCorpus.empty()) return;  // Nothing to merge.
  size_t NumAttempts = 0;
  std::vector<MergeFileInfo> KnownFiles;
  if (FileSize(CFPath)) {
    VPrintf(V, "MERGE-OUTER: non-empty control file provided: '%s'\n",
           CFPath.c_str());
    Merger M;
    std::ifstream IF(CFPath);
    if (M.Parse(IF, /*ParseCoverage=*/true)) {
      VPrintf(V, "MERGE-OUTER: control file ok, %zd files total,"
             " first not processed file %zd\n",
             M.Files.size(), M.FirstNotProcessedFile);
      if (!M.LastFailure.empty())
        VPrintf(V, "MERGE-OUTER: '%s' will be skipped as unlucky "
               "(merge has stumbled on it the last time)\n",
               M.LastFailure.c_str());
      if (M.FirstNotProcessedFile >= M.Files.size()) {
        // Merge has already been completed with the given merge control file.
        if (M.Files.size() == OldCorpus.size() + NewCorpus.size()) {
          VPrintf(
              V,
              "MERGE-OUTER: nothing to do, merge has been completed before\n");
          exit(0);
        }

        // Number of input files likely changed, start merge from scratch, but
        // reuse coverage information from the given merge control file.
        VPrintf(
            V,
            "MERGE-OUTER: starting merge from scratch, but reusing coverage "
            "information from the given control file\n");
        KnownFiles = M.Files;
      } else {
        // There is a merge in progress, continue.
        NumAttempts = M.Files.size() - M.FirstNotProcessedFile;
      }
    } else {
      VPrintf(V, "MERGE-OUTER: bad control file, will overwrite it\n");
    }
  }

  if (!NumAttempts) {
    // The supplied control file is empty or bad, create a fresh one.
    VPrintf(V, "MERGE-OUTER: "
            "%zd files, %zd in the initial corpus, %zd processed earlier\n",
            OldCorpus.size() + NewCorpus.size(), OldCorpus.size(),
            KnownFiles.size());
    NumAttempts = WriteNewControlFile(CFPath, OldCorpus, NewCorpus, KnownFiles);
  }

  // Execute the inner process until it passes.
  // Every inner process should execute at least one input.
  Command BaseCmd(Args);
  BaseCmd.removeFlag("merge");
  BaseCmd.removeFlag("set_cover_merge");
  BaseCmd.removeFlag("fork");
  BaseCmd.removeFlag("collect_data_flow");
  for (size_t Attempt = 1; Attempt <= NumAttempts; Attempt++) {
    Fuzzer::MaybeExitGracefully();
    VPrintf(V, "MERGE-OUTER: attempt %zd\n", Attempt);
    Command Cmd(BaseCmd);
    Cmd.addFlag("merge_control_file", CFPath);
    // If we are going to use the set cover implementation for
    // minimization add the merge_inner=2 internal flag.
    Cmd.addFlag("merge_inner", IsSetCoverMerge ? "2" : "1");
    if (!V) {
      Cmd.setOutputFile(getDevNull());
      Cmd.combineOutAndErr();
    }
    auto ExitCode = ExecuteCommand(Cmd);
    if (!ExitCode) {
      VPrintf(V, "MERGE-OUTER: successful in %zd attempt(s)\n", Attempt);
      break;
    }
  }
  // Read the control file and do the merge.
  Merger M;
  std::ifstream IF(CFPath);
  IF.seekg(0, IF.end);
  VPrintf(V, "MERGE-OUTER: the control file has %zd bytes\n",
          (size_t)IF.tellg());
  IF.seekg(0, IF.beg);
  M.ParseOrExit(IF, true);
  IF.close();
  VPrintf(V,
          "MERGE-OUTER: consumed %zdMb (%zdMb rss) to parse the control file\n",
          M.ApproximateMemoryConsumption() >> 20, GetPeakRSSMb());

  M.Files.insert(M.Files.end(), KnownFiles.begin(), KnownFiles.end());
  if (IsSetCoverMerge)
    M.SetCoverMerge(InitialFeatures, NewFeatures, InitialCov, NewCov, NewFiles);
  else
    M.Merge(InitialFeatures, NewFeatures, InitialCov, NewCov, NewFiles);
  VPrintf(V, "MERGE-OUTER: %zd new files with %zd new features added; "
          "%zd new coverage edges\n",
         NewFiles->size(), NewFeatures->size(), NewCov->size());
}

} // namespace fuzzer
