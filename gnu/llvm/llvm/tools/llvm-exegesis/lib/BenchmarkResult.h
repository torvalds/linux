//===-- BenchmarkResult.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines classes to represent measurements and serialize/deserialize them to
//  Yaml.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_BENCHMARKRESULT_H
#define LLVM_TOOLS_LLVM_EXEGESIS_BENCHMARKRESULT_H

#include "LlvmState.h"
#include "RegisterValue.h"
#include "ValidationEvent.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/Support/YAMLTraits.h"
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvm {
class Error;

namespace exegesis {

enum class BenchmarkPhaseSelectorE {
  PrepareSnippet,
  PrepareAndAssembleSnippet,
  AssembleMeasuredCode,
  Measure,
};

enum class BenchmarkFilter { All, RegOnly, WithMem };

struct MemoryValue {
  // The arbitrary bit width constant that defines the value.
  APInt Value;
  // The size of the value in bytes.
  size_t SizeBytes;
  // The index of the memory value.
  size_t Index;
};

struct MemoryMapping {
  // The address to place the mapping at.
  intptr_t Address;
  // The name of the value that should be mapped.
  std::string MemoryValueName;
};

struct BenchmarkKey {
  // The LLVM opcode name.
  std::vector<MCInst> Instructions;
  // The initial values of the registers.
  std::vector<RegisterValue> RegisterInitialValues;
  // The memory values that can be mapped into the execution context of the
  // snippet.
  std::unordered_map<std::string, MemoryValue> MemoryValues;
  // The memory mappings that the snippet can access.
  std::vector<MemoryMapping> MemoryMappings;
  // An opaque configuration, that can be used to separate several benchmarks of
  // the same instruction under different configurations.
  std::string Config;
  // The address that the snippet should be loaded in at if the execution mode
  // being used supports it.
  intptr_t SnippetAddress = 0;
  // The register that should be used to hold the loop counter.
  unsigned LoopRegister;
};

struct BenchmarkMeasure {
  // A helper to create an unscaled BenchmarkMeasure.
  static BenchmarkMeasure
  Create(std::string Key, double Value,
         std::map<ValidationEvent, int64_t> ValCounters) {
    return {Key, Value, Value, Value, ValCounters};
  }
  std::string Key;
  // This is the per-instruction value, i.e. measured quantity scaled per
  // instruction.
  double PerInstructionValue;
  // This is the per-snippet value, i.e. measured quantity for one repetition of
  // the whole snippet.
  double PerSnippetValue;
  // This is the raw value collected from the full execution.
  double RawValue;
  // These are the validation counter values.
  std::map<ValidationEvent, int64_t> ValidationCounters;
};

// The result of an instruction benchmark.
struct Benchmark {
  BenchmarkKey Key;
  enum ModeE { Unknown, Latency, Uops, InverseThroughput };
  ModeE Mode;
  std::string CpuName;
  std::string LLVMTriple;
  // Which instruction is being benchmarked here?
  const MCInst &keyInstruction() const { return Key.Instructions[0]; }
  // The number of instructions inside the repeated snippet. For example, if a
  // snippet of 3 instructions is repeated 4 times, this is 12.
  unsigned MinInstructions = 0;
  enum RepetitionModeE {
    Duplicate,
    Loop,
    AggregateMin,
    MiddleHalfDuplicate,
    MiddleHalfLoop
  };
  // Note that measurements are per instruction.
  std::vector<BenchmarkMeasure> Measurements;
  std::string Error;
  std::string Info;
  std::vector<uint8_t> AssembledSnippet;
  // How to aggregate measurements.
  enum ResultAggregationModeE { Min, Max, Mean, MinVariance };

  Benchmark() = default;
  Benchmark(Benchmark &&) = default;

  Benchmark(const Benchmark &) = delete;
  Benchmark &operator=(const Benchmark &) = delete;
  Benchmark &operator=(Benchmark &&) = delete;

  // Read functions.
  static Expected<Benchmark> readYaml(const LLVMState &State,
                                                 MemoryBufferRef Buffer);

  static Expected<std::vector<Benchmark>>
  readYamls(const LLVMState &State, MemoryBufferRef Buffer);

  // Given a set of serialized instruction benchmarks, returns the set of
  // triples and CPUs that appear in the list of benchmarks.
  struct TripleAndCpu {
    std::string LLVMTriple;
    std::string CpuName;
    bool operator<(const TripleAndCpu &O) const {
      return std::tie(LLVMTriple, CpuName) < std::tie(O.LLVMTriple, O.CpuName);
    }
  };
  static Expected<std::set<TripleAndCpu>>
  readTriplesAndCpusFromYamls(MemoryBufferRef Buffer);

  class Error readYamlFrom(const LLVMState &State, StringRef InputContent);

  // Write functions, non-const because of YAML traits.
  // NOTE: we intentionally do *NOT* have a variant of this function taking
  //       filename, because it's behaviour is bugprone with regards to
  //       accidentally using it more than once and overriding previous YAML.
  class Error writeYamlTo(const LLVMState &State, raw_ostream &S);
};

bool operator==(const BenchmarkMeasure &A, const BenchmarkMeasure &B);

//------------------------------------------------------------------------------
// Utilities to work with Benchmark measures.

// A class that measures stats over benchmark measures.
class PerInstructionStats {
public:
  void push(const BenchmarkMeasure &BM);

  double avg() const {
    assert(NumValues);
    return SumValues / NumValues;
  }
  double min() const { return MinValue; }
  double max() const { return MaxValue; }

  const std::string &key() const { return Key; }

private:
  std::string Key;
  double SumValues = 0.0;
  int NumValues = 0;
  double MaxValue = std::numeric_limits<double>::min();
  double MinValue = std::numeric_limits<double>::max();
};

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_BENCHMARKRESULT_H
