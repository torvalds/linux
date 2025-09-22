//===-- PPCRegisterBanks.td - Describe the PPC Banks -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Define the PPC register banks used for GlobalISel.
///
//===----------------------------------------------------------------------===//

/// General Purpose Registers
def GPRRegBank : RegisterBank<"GPR", [G8RC, G8RC_NOX0]>;
/// Floating point Registers
def FPRRegBank : RegisterBank<"FPR", [VSSRC]>;
/// Vector Registers
def VECRegBank : RegisterBank<"VEC", [VSRC]>;
/// Condition Registers
def CRRegBank : RegisterBank<"CR", [CRRC]>;
