//===-- M68kSchedule.td - M68k Scheduling Definitions ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains M68k scheduler definitions.
///
//===----------------------------------------------------------------------===//

/// This is a very general M68k Scheduling Model and best suited for the very
/// first M68000 CPU, other model must override these characteristics
class M68kSchedModel : SchedMachineModel {
  let LoadLatency = 4;  // Word (Rn)
  let HighLatency = 16; // Long ABS
  let PostRAScheduler = 0;
  let CompleteModel = 0;
}

def GenericM68kModel : M68kSchedModel;
