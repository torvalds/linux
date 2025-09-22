//===- FuzzerMerge.h - merging corpa ----------------------------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Merging Corpora.
//
// The task:
//   Take the existing corpus (possibly empty) and merge new inputs into
//   it so that only inputs with new coverage ('features') are added.
//   The process should tolerate the crashes, OOMs, leaks, etc.
//
// Algorithm:
//   The outer process collects the set of files and writes their names
//   into a temporary "control" file, then repeatedly launches the inner
//   process until all inputs are processed.
//   The outer process does not actually execute the target code.
//
//   The inner process reads the control file and sees a) list of all the inputs
//   and b) the last processed input. Then it starts processing the inputs one
//   by one. Before processing every input it writes one line to control file:
//   STARTED INPUT_ID INPUT_SIZE
//   After processing an input it writes the following lines:
//   FT INPUT_ID Feature1 Feature2 Feature3 ...
//   COV INPUT_ID Coverage1 Coverage2 Coverage3 ...
//   If a crash happens while processing an input the last line in the control
//   file will be "STARTED INPUT_ID" and so the next process will know
//   where to resume.
//
//   Once all inputs are processed by the inner process(es) the outer process
//   reads the control files and does the merge based entirely on the contents
//   of control file.
//   It uses a single pass greedy algorithm choosing first the smallest inputs
//   within the same size the inputs that have more new features.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_MERGE_H
#define LLVM_FUZZER_MERGE_H

#include "FuzzerDefs.h"
#include "FuzzerIO.h"

#include <istream>
#include <ostream>
#include <set>
#include <vector>

namespace fuzzer {

struct MergeFileInfo {
  std::string Name;
  size_t Size = 0;
  std::vector<uint32_t> Features, Cov;
};

struct Merger {
  std::vector<MergeFileInfo> Files;
  size_t NumFilesInFirstCorpus = 0;
  size_t FirstNotProcessedFile = 0;
  std::string LastFailure;

  bool Parse(std::istream &IS, bool ParseCoverage);
  bool Parse(const std::string &Str, bool ParseCoverage);
  void ParseOrExit(std::istream &IS, bool ParseCoverage);
  size_t Merge(const std::set<uint32_t> &InitialFeatures,
               std::set<uint32_t> *NewFeatures,
               const std::set<uint32_t> &InitialCov, std::set<uint32_t> *NewCov,
               std::vector<std::string> *NewFiles);
  size_t SetCoverMerge(const std::set<uint32_t> &InitialFeatures,
                       std::set<uint32_t> *NewFeatures,
                       const std::set<uint32_t> &InitialCov,
                       std::set<uint32_t> *NewCov,
                       std::vector<std::string> *NewFiles);
  size_t ApproximateMemoryConsumption() const;
  std::set<uint32_t> AllFeatures() const;
};

void CrashResistantMerge(const std::vector<std::string> &Args,
                         const std::vector<SizedFile> &OldCorpus,
                         const std::vector<SizedFile> &NewCorpus,
                         std::vector<std::string> *NewFiles,
                         const std::set<uint32_t> &InitialFeatures,
                         std::set<uint32_t> *NewFeatures,
                         const std::set<uint32_t> &InitialCov,
                         std::set<uint32_t> *NewCov, const std::string &CFPath,
                         bool Verbose, bool IsSetCoverMerge);

}  // namespace fuzzer

#endif  // LLVM_FUZZER_MERGE_H
