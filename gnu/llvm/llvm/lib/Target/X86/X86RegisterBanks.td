//=- X86RegisterBank.td - Describe the X86 Banks -------------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

/// General Purpose Registers: RAX, RCX,...
def GPRRegBank : RegisterBank<"GPR", [GR64]>;

/// Floating Point/Vector Registers
def VECRRegBank : RegisterBank<"VECR", [VR512]>;

/// Pseudo Registers: RFP80
def PSRRegBank : RegisterBank<"PSR", [RFP32, RFP64, RFP80]>;
