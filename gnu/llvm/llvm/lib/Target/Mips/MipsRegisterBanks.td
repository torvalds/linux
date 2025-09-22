//===- MipsRegisterBank.td ---------------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

def GPRBRegBank : RegisterBank<"GPRB", [GPR32]>;

def FPRBRegBank : RegisterBank<"FPRB", [FGR64, AFGR64, MSA128D]>;
