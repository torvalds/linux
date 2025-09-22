//=- AArch64RegisterBank.td - Describe the AArch64 Banks -----*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

/// General Purpose Registers: W, X.
def GPRRegBank : RegisterBank<"GPR", [XSeqPairsClass]>;

/// Floating Point, Vector, Scalable Vector Registers: B, H, S, D, Q, Z.
def FPRRegBank : RegisterBank<"FPR", [QQQQ, ZPR]>;

/// Conditional register: NZCV.
def CCRegBank : RegisterBank<"CC", [CCR]>;
