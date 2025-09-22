//===- TargetPfmCounters.td - Target Pfm Counters -*- tablegen ----------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the target-independent interfaces for performance counters.
//
//===----------------------------------------------------------------------===//

// Definition of a hardware counters from libpfm identifiers.
class PfmCounter<string counter> {
  // The name of the counter that measures events.
  // The name can be "some_counter + some_other_counter", in which case the
  // measured value is the sum of events on these counters.
  string Counter = counter;
}

// Issue counters can be tied to a ProcResource
class PfmIssueCounter<string resource_name, string counter>
    : PfmCounter<counter> {
  // The name of the ProcResource on which uops are issued. This is used by
  // llvm-exegesis to compare measurements with values in the SchedModels.
  // If the CPU has a sched model, this should correspond to the name of a
  // ProcResource.
  string ResourceName = resource_name;
}

// Definition of a validation event. A validation event represents a specific
// event that can be measured using performance counters that is interesting
// in regard to the snippet state.
class ValidationEvent <int event_number> {
  int EventNumber = event_number;
}

// TableGen names for events defined in `llvm::exegesis::ValidationEvent`.
def InstructionRetired  : ValidationEvent<0>;
def L1DCacheLoadMiss : ValidationEvent<1>;
def L1DCacheStoreMiss : ValidationEvent<2>;
def L1ICacheLoadMiss : ValidationEvent<3>;
def DataTLBLoadMiss : ValidationEvent<4>;
def DataTLBStoreMiss : ValidationEvent<5>;
def InstructionTLBLoadMiss : ValidationEvent<6>;
def BranchPredictionMiss : ValidationEvent<7>;


// PfmValidationCounter provides a mapping between the events that are
// are interesting in regards to the snippet execution environment and
// a concrete performance counter name that can be looked up in libpfm.
class PfmValidationCounter<ValidationEvent event_type, string counter>
    : PfmCounter<counter> {
  // The name of the event that the validation counter detects.
  ValidationEvent EventType = event_type;
}

def NoPfmCounter : PfmCounter <""> {}

// Set of PfmCounters for measuring sched model characteristics.
class ProcPfmCounters {
  // Processors can define how to measure cycles by defining a CycleCounter.
  PfmCounter CycleCounter = NoPfmCounter;
  // Processors can define how to measure uops by defining a UopsCounter.
  PfmCounter UopsCounter = NoPfmCounter;
  // Processors can define how to measure issued uops by defining IssueCounters.
  list<PfmIssueCounter> IssueCounters = [];
  // Processor can list mappings between validation events and real counters
  // to measure the specified events.
  list<PfmValidationCounter> ValidationCounters = [];
}

// A binding of a set of counters to a CPU.
class PfmCountersBinding<string cpu_name, ProcPfmCounters counters> {
  string CpuName = cpu_name;
  ProcPfmCounters Counters = counters;
}

// Declares the default binding for unbound CPUs for the target.
class PfmCountersDefaultBinding<ProcPfmCounters counters>
    : PfmCountersBinding<"", counters> {}
