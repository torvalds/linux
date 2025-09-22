//===-- X86InstrSGX.td - SGX Instruction Set Extension -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the instructions that make up the Intel SGX instruction
// set.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SGX instructions

let SchedRW = [WriteSystem], Predicates = [HasSGX] in {
// ENCLS - Execute an Enclave System Function of Specified Leaf Number
def ENCLS : I<0x01, MRM_CF, (outs), (ins),
             "encls", []>, TB;

// ENCLU - Execute an Enclave User Function of Specified Leaf Number
def ENCLU : I<0x01, MRM_D7, (outs), (ins),
             "enclu", []>, TB;

// ENCLV - Execute an Enclave VMM Function of Specified Leaf Number
def ENCLV : I<0x01, MRM_C0, (outs), (ins),
             "enclv", []>, TB;
} // SchedRW
