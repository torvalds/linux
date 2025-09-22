//===- FuzzerFork.cpp - run fuzzing in separate subprocesses --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Spawn and orchestrate separate fuzzing processes.
//===----------------------------------------------------------------------===//

#include "FuzzerCommand.h"
#include "FuzzerFork.h"
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include "FuzzerMerge.h"
#include "FuzzerSHA1.h"
#include "FuzzerTracePC.h"
#include "FuzzerUtil.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>

namespace fuzzer {

struct Stats {
  size_t number_of_executed_units = 0;
  size_t peak_rss_mb = 0;
  size_t average_exec_per_sec = 0;
};

static Stats ParseFinalStatsFromLog(const std::string &LogPath) {
  std::ifstream In(LogPath);
  std::string Line;
  Stats Res;
  struct {
    const char *Name;
    size_t *Var;
  } NameVarPairs[] = {
      {"stat::number_of_executed_units:", &Res.number_of_executed_units},
      {"stat::peak_rss_mb:", &Res.peak_rss_mb},
      {"stat::average_exec_per_sec:", &Res.average_exec_per_sec},
      {nullptr, nullptr},
  };
  while (std::getline(In, Line, '\n')) {
    if (Line.find("stat::") != 0) continue;
    std::istringstream ISS(Line);
    std::string Name;
    size_t Val;
    ISS >> Name >> Val;
    for (size_t i = 0; NameVarPairs[i].Name; i++)
      if (Name == NameVarPairs[i].Name)
        *NameVarPairs[i].Var = Val;
  }
  return Res;
}

struct FuzzJob {
  // Inputs.
  Command Cmd;
  std::string CorpusDir;
  std::string FeaturesDir;
  std::string LogPath;
  std::string SeedListPath;
  std::string CFPath;
  size_t      JobId;

  int         DftTimeInSeconds = 0;

  // Fuzzing Outputs.
  int ExitCode;

  ~FuzzJob() {
    RemoveFile(CFPath);
    RemoveFile(LogPath);
    RemoveFile(SeedListPath);
    RmDirRecursive(CorpusDir);
    RmDirRecursive(FeaturesDir);
  }
};

struct GlobalEnv {
  std::vector<std::string> Args;
  std::vector<std::string> CorpusDirs;
  std::string MainCorpusDir;
  std::string TempDir;
  std::string DFTDir;
  std::string DataFlowBinary;
  std::set<uint32_t> Features, Cov;
  std::set<std::string> FilesWithDFT;
  std::vector<std::string> Files;
  std::vector<std::size_t> FilesSizes;
  Random *Rand;
  std::chrono::system_clock::time_point ProcessStartTime;
  int Verbosity = 0;
  int Group = 0;
  int NumCorpuses = 8;

  size_t NumTimeouts = 0;
  size_t NumOOMs = 0;
  size_t NumCrashes = 0;


  size_t NumRuns = 0;

  std::string StopFile() { return DirPlusFile(TempDir, "STOP"); }

  size_t secondsSinceProcessStartUp() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now() - ProcessStartTime)
        .count();
  }

  FuzzJob *CreateNewJob(size_t JobId) {
    Command Cmd(Args);
    Cmd.removeFlag("fork");
    Cmd.removeFlag("runs");
    Cmd.removeFlag("collect_data_flow");
    for (auto &C : CorpusDirs) // Remove all corpora from the args.
      Cmd.removeArgument(C);
    Cmd.addFlag("reload", "0");  // working in an isolated dir, no reload.
    Cmd.addFlag("print_final_stats", "1");
    Cmd.addFlag("print_funcs", "0");  // no need to spend time symbolizing.
    Cmd.addFlag("max_total_time", std::to_string(std::min((size_t)300, JobId)));
    Cmd.addFlag("stop_file", StopFile());
    if (!DataFlowBinary.empty()) {
      Cmd.addFlag("data_flow_trace", DFTDir);
      if (!Cmd.hasFlag("focus_function"))
        Cmd.addFlag("focus_function", "auto");
    }
    auto Job = new FuzzJob;
    std::string Seeds;
    if (size_t CorpusSubsetSize =
            std::min(Files.size(), (size_t)sqrt(Files.size() + 2))) {
      auto Time1 = std::chrono::system_clock::now();
      if (Group) { // whether to group the corpus.
        size_t AverageCorpusSize = Files.size() / NumCorpuses + 1;
        size_t StartIndex = ((JobId - 1) % NumCorpuses) * AverageCorpusSize;
        for (size_t i = 0; i < CorpusSubsetSize; i++) {
          size_t RandNum = (*Rand)(AverageCorpusSize);
          size_t Index = RandNum + StartIndex;
          Index = Index < Files.size() ? Index
                                       : Rand->SkewTowardsLast(Files.size());
          auto &SF = Files[Index];
          Seeds += (Seeds.empty() ? "" : ",") + SF;
          CollectDFT(SF);
        }
      } else {
        for (size_t i = 0; i < CorpusSubsetSize; i++) {
          auto &SF = Files[Rand->SkewTowardsLast(Files.size())];
          Seeds += (Seeds.empty() ? "" : ",") + SF;
          CollectDFT(SF);
        }
      }
      auto Time2 = std::chrono::system_clock::now();
      auto DftTimeInSeconds = duration_cast<seconds>(Time2 - Time1).count();
      assert(DftTimeInSeconds < std::numeric_limits<int>::max());
      Job->DftTimeInSeconds = static_cast<int>(DftTimeInSeconds);
    }
    if (!Seeds.empty()) {
      Job->SeedListPath =
          DirPlusFile(TempDir, std::to_string(JobId) + ".seeds");
      WriteToFile(Seeds, Job->SeedListPath);
      Cmd.addFlag("seed_inputs", "@" + Job->SeedListPath);
    }
    Job->LogPath = DirPlusFile(TempDir, std::to_string(JobId) + ".log");
    Job->CorpusDir = DirPlusFile(TempDir, "C" + std::to_string(JobId));
    Job->FeaturesDir = DirPlusFile(TempDir, "F" + std::to_string(JobId));
    Job->CFPath = DirPlusFile(TempDir, std::to_string(JobId) + ".merge");
    Job->JobId = JobId;


    Cmd.addArgument(Job->CorpusDir);
    Cmd.addFlag("features_dir", Job->FeaturesDir);

    for (auto &D : {Job->CorpusDir, Job->FeaturesDir}) {
      RmDirRecursive(D);
      MkDir(D);
    }

    Cmd.setOutputFile(Job->LogPath);
    Cmd.combineOutAndErr();

    Job->Cmd = Cmd;

    if (Verbosity >= 2)
      Printf("Job %zd/%p Created: %s\n", JobId, Job,
             Job->Cmd.toString().c_str());
    // Start from very short runs and gradually increase them.
    return Job;
  }

  void RunOneMergeJob(FuzzJob *Job) {
    auto Stats = ParseFinalStatsFromLog(Job->LogPath);
    NumRuns += Stats.number_of_executed_units;

    std::vector<SizedFile> TempFiles, MergeCandidates;
    // Read all newly created inputs and their feature sets.
    // Choose only those inputs that have new features.
    GetSizedFilesFromDir(Job->CorpusDir, &TempFiles);
    std::sort(TempFiles.begin(), TempFiles.end());
    for (auto &F : TempFiles) {
      auto FeatureFile = F.File;
      FeatureFile.replace(0, Job->CorpusDir.size(), Job->FeaturesDir);
      auto FeatureBytes = FileToVector(FeatureFile, 0, false);
      assert((FeatureBytes.size() % sizeof(uint32_t)) == 0);
      std::vector<uint32_t> NewFeatures(FeatureBytes.size() / sizeof(uint32_t));
      memcpy(NewFeatures.data(), FeatureBytes.data(), FeatureBytes.size());
      for (auto Ft : NewFeatures) {
        if (!Features.count(Ft)) {
          MergeCandidates.push_back(F);
          break;
        }
      }
    }
    // if (!FilesToAdd.empty() || Job->ExitCode != 0)
    Printf("#%zd: cov: %zd ft: %zd corp: %zd exec/s: %zd "
           "oom/timeout/crash: %zd/%zd/%zd time: %zds job: %zd dft_time: %d\n",
           NumRuns, Cov.size(), Features.size(), Files.size(),
           Stats.average_exec_per_sec, NumOOMs, NumTimeouts, NumCrashes,
           secondsSinceProcessStartUp(), Job->JobId, Job->DftTimeInSeconds);

    if (MergeCandidates.empty()) return;

    std::vector<std::string> FilesToAdd;
    std::set<uint32_t> NewFeatures, NewCov;
    bool IsSetCoverMerge =
        !Job->Cmd.getFlagValue("set_cover_merge").compare("1");
    CrashResistantMerge(Args, {}, MergeCandidates, &FilesToAdd, Features,
                        &NewFeatures, Cov, &NewCov, Job->CFPath, false,
                        IsSetCoverMerge);
    for (auto &Path : FilesToAdd) {
      auto U = FileToVector(Path);
      auto NewPath = DirPlusFile(MainCorpusDir, Hash(U));
      WriteToFile(U, NewPath);
      if (Group) { // Insert the queue according to the size of the seed.
        size_t UnitSize = U.size();
        auto Idx =
            std::upper_bound(FilesSizes.begin(), FilesSizes.end(), UnitSize) -
            FilesSizes.begin();
        FilesSizes.insert(FilesSizes.begin() + Idx, UnitSize);
        Files.insert(Files.begin() + Idx, NewPath);
      } else {
        Files.push_back(NewPath);
      }
    }
    Features.insert(NewFeatures.begin(), NewFeatures.end());
    Cov.insert(NewCov.begin(), NewCov.end());
    for (auto Idx : NewCov)
      if (auto *TE = TPC.PCTableEntryByIdx(Idx))
        if (TPC.PcIsFuncEntry(TE))
          PrintPC("  NEW_FUNC: %p %F %L\n", "",
                  TPC.GetNextInstructionPc(TE->PC));
  }

  void CollectDFT(const std::string &InputPath) {
    if (DataFlowBinary.empty()) return;
    if (!FilesWithDFT.insert(InputPath).second) return;
    Command Cmd(Args);
    Cmd.removeFlag("fork");
    Cmd.removeFlag("runs");
    Cmd.addFlag("data_flow_trace", DFTDir);
    Cmd.addArgument(InputPath);
    for (auto &C : CorpusDirs) // Remove all corpora from the args.
      Cmd.removeArgument(C);
    Cmd.setOutputFile(DirPlusFile(TempDir, "dft.log"));
    Cmd.combineOutAndErr();
    // Printf("CollectDFT: %s\n", Cmd.toString().c_str());
    ExecuteCommand(Cmd);
  }

};

struct JobQueue {
  std::queue<FuzzJob *> Qu;
  std::mutex Mu;
  std::condition_variable Cv;

  void Push(FuzzJob *Job) {
    {
      std::lock_guard<std::mutex> Lock(Mu);
      Qu.push(Job);
    }
    Cv.notify_one();
  }
  FuzzJob *Pop() {
    std::unique_lock<std::mutex> Lk(Mu);
    // std::lock_guard<std::mutex> Lock(Mu);
    Cv.wait(Lk, [&]{return !Qu.empty();});
    assert(!Qu.empty());
    auto Job = Qu.front();
    Qu.pop();
    return Job;
  }
};

void WorkerThread(JobQueue *FuzzQ, JobQueue *MergeQ) {
  while (auto Job = FuzzQ->Pop()) {
    // Printf("WorkerThread: job %p\n", Job);
    Job->ExitCode = ExecuteCommand(Job->Cmd);
    MergeQ->Push(Job);
  }
}

// This is just a skeleton of an experimental -fork=1 feature.
void FuzzWithFork(Random &Rand, const FuzzingOptions &Options,
                  const std::vector<std::string> &Args,
                  const std::vector<std::string> &CorpusDirs, int NumJobs) {
  Printf("INFO: -fork=%d: fuzzing in separate process(s)\n", NumJobs);

  GlobalEnv Env;
  Env.Args = Args;
  Env.CorpusDirs = CorpusDirs;
  Env.Rand = &Rand;
  Env.Verbosity = Options.Verbosity;
  Env.ProcessStartTime = std::chrono::system_clock::now();
  Env.DataFlowBinary = Options.CollectDataFlow;
  Env.Group = Options.ForkCorpusGroups;

  std::vector<SizedFile> SeedFiles;
  for (auto &Dir : CorpusDirs)
    GetSizedFilesFromDir(Dir, &SeedFiles);
  std::sort(SeedFiles.begin(), SeedFiles.end());
  Env.TempDir = TempPath("FuzzWithFork", ".dir");
  Env.DFTDir = DirPlusFile(Env.TempDir, "DFT");
  RmDirRecursive(Env.TempDir);  // in case there is a leftover from old runs.
  MkDir(Env.TempDir);
  MkDir(Env.DFTDir);


  if (CorpusDirs.empty())
    MkDir(Env.MainCorpusDir = DirPlusFile(Env.TempDir, "C"));
  else
    Env.MainCorpusDir = CorpusDirs[0];

  if (Options.KeepSeed) {
    for (auto &File : SeedFiles)
      Env.Files.push_back(File.File);
  } else {
    auto CFPath = DirPlusFile(Env.TempDir, "merge.txt");
    std::set<uint32_t> NewFeatures, NewCov;
    CrashResistantMerge(Env.Args, {}, SeedFiles, &Env.Files, Env.Features,
                        &NewFeatures, Env.Cov, &NewCov, CFPath,
                        /*Verbose=*/false, /*IsSetCoverMerge=*/false);
    Env.Features.insert(NewFeatures.begin(), NewFeatures.end());
    Env.Cov.insert(NewCov.begin(), NewCov.end());
    RemoveFile(CFPath);
  }

  if (Env.Group) {
    for (auto &path : Env.Files)
      Env.FilesSizes.push_back(FileSize(path));
  }

  Printf("INFO: -fork=%d: %zd seed inputs, starting to fuzz in %s\n", NumJobs,
         Env.Files.size(), Env.TempDir.c_str());

  int ExitCode = 0;

  JobQueue FuzzQ, MergeQ;

  auto StopJobs = [&]() {
    for (int i = 0; i < NumJobs; i++)
      FuzzQ.Push(nullptr);
    MergeQ.Push(nullptr);
    WriteToFile(Unit({1}), Env.StopFile());
  };

  size_t MergeCycle = 20;
  size_t JobExecuted = 0;
  size_t JobId = 1;
  std::vector<std::thread> Threads;
  for (int t = 0; t < NumJobs; t++) {
    Threads.push_back(std::thread(WorkerThread, &FuzzQ, &MergeQ));
    FuzzQ.Push(Env.CreateNewJob(JobId++));
  }

  while (true) {
    std::unique_ptr<FuzzJob> Job(MergeQ.Pop());
    if (!Job)
      break;
    ExitCode = Job->ExitCode;
    if (ExitCode == Options.InterruptExitCode) {
      Printf("==%lu== libFuzzer: a child was interrupted; exiting\n", GetPid());
      StopJobs();
      break;
    }
    Fuzzer::MaybeExitGracefully();

    Env.RunOneMergeJob(Job.get());

    // merge the corpus .
    JobExecuted++;
    if (Env.Group && JobExecuted >= MergeCycle) {
      std::vector<SizedFile> CurrentSeedFiles;
      for (auto &Dir : CorpusDirs)
        GetSizedFilesFromDir(Dir, &CurrentSeedFiles);
      std::sort(CurrentSeedFiles.begin(), CurrentSeedFiles.end());

      auto CFPath = DirPlusFile(Env.TempDir, "merge.txt");
      std::set<uint32_t> TmpNewFeatures, TmpNewCov;
      std::set<uint32_t> TmpFeatures, TmpCov;
      Env.Files.clear();
      Env.FilesSizes.clear();
      CrashResistantMerge(Env.Args, {}, CurrentSeedFiles, &Env.Files,
                          TmpFeatures, &TmpNewFeatures, TmpCov, &TmpNewCov,
                          CFPath, /*Verbose=*/false, /*IsSetCoverMerge=*/false);
      for (auto &path : Env.Files)
        Env.FilesSizes.push_back(FileSize(path));
      RemoveFile(CFPath);
      JobExecuted = 0;
      MergeCycle += 5;
    }

    // Since the number of corpus seeds will gradually increase, in order to
    // control the number in each group to be about three times the number of
    // seeds selected each time, the number of groups is dynamically adjusted.
    if (Env.Files.size() < 2000)
      Env.NumCorpuses = 12;
    else if (Env.Files.size() < 6000)
      Env.NumCorpuses = 20;
    else if (Env.Files.size() < 12000)
      Env.NumCorpuses = 32;
    else if (Env.Files.size() < 16000)
      Env.NumCorpuses = 40;
    else if (Env.Files.size() < 24000)
      Env.NumCorpuses = 60;
    else
      Env.NumCorpuses = 80;

    // Continue if our crash is one of the ignored ones.
    if (Options.IgnoreTimeouts && ExitCode == Options.TimeoutExitCode)
      Env.NumTimeouts++;
    else if (Options.IgnoreOOMs && ExitCode == Options.OOMExitCode)
      Env.NumOOMs++;
    else if (ExitCode != 0) {
      Env.NumCrashes++;
      if (Options.IgnoreCrashes) {
        std::ifstream In(Job->LogPath);
        std::string Line;
        while (std::getline(In, Line, '\n'))
          if (Line.find("ERROR:") != Line.npos ||
              Line.find("runtime error:") != Line.npos)
            Printf("%s\n", Line.c_str());
      } else {
        // And exit if we don't ignore this crash.
        Printf("INFO: log from the inner process:\n%s",
               FileToString(Job->LogPath).c_str());
        StopJobs();
        break;
      }
    }

    // Stop if we are over the time budget.
    // This is not precise, since other threads are still running
    // and we will wait while joining them.
    // We also don't stop instantly: other jobs need to finish.
    if (Options.MaxTotalTimeSec > 0 &&
        Env.secondsSinceProcessStartUp() >= (size_t)Options.MaxTotalTimeSec) {
      Printf("INFO: fuzzed for %zd seconds, wrapping up soon\n",
             Env.secondsSinceProcessStartUp());
      StopJobs();
      break;
    }
    if (Env.NumRuns >= Options.MaxNumberOfRuns) {
      Printf("INFO: fuzzed for %zd iterations, wrapping up soon\n",
             Env.NumRuns);
      StopJobs();
      break;
    }

    FuzzQ.Push(Env.CreateNewJob(JobId++));
  }

  for (auto &T : Threads)
    T.join();

  // The workers have terminated. Don't try to remove the directory before they
  // terminate to avoid a race condition preventing cleanup on Windows.
  RmDirRecursive(Env.TempDir);

  // Use the exit code from the last child process.
  Printf("INFO: exiting: %d time: %zds\n", ExitCode,
         Env.secondsSinceProcessStartUp());
  exit(ExitCode);
}

} // namespace fuzzer
