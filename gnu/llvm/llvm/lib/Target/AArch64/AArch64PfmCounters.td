//===-- AArch64PfmCounters.td - AArch64 Hardware Counters --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This describes the available hardware counters for AArch64.
//
//===----------------------------------------------------------------------===//

def CpuCyclesPfmCounter : PfmCounter<"CPU_CYCLES">;

def DefaultPfmCounters : ProcPfmCounters {
  let CycleCounter = CpuCyclesPfmCounter;
}
def : PfmCountersDefaultBinding<DefaultPfmCounters>;
