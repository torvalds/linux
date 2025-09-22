//===-- SPIRVRegisterBanks.td - Describe SPIR-V RegBanks ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Although RegisterBankSelection is disabled we need to distinct the banks
// as InstructionSelector RegClass checking code relies on them

def TYPERegBank : RegisterBank<"TYPEBank", [TYPE]>;
def IDRegBank : RegisterBank<"IDBank", [ID, ID64, fID, fID64, pID32, pID64, vID, vfID, vpID32, vpID64]>;
