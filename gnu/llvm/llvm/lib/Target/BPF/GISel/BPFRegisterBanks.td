//===-- BPFRegisterBanks.td - Describe the BPF Banks -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Define the BPF register banks used for GlobalISel.
///
//===----------------------------------------------------------------------===//

/// General Purpose Registers
def GPRRegBank : RegisterBank<"GPRB", [GPR]>;
