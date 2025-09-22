//===-- CSKYInstrAlias.td - Target Description for CSKY ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the CSKY instructions alias.
//
//===----------------------------------------------------------------------===//

def : InstAlias<"nop", (MOV16 R0, R0)>;
def : InstAlias<"nop", (MOV32 R0, R0)>, Requires<[iHasE2]>;

def : InstAlias<"bgeni16 $dst, $imm", (BGENI GPR:$dst, uimm5:$imm)>;
def : InstAlias<"bgeni32 $dst, $imm", (BGENI GPR:$dst, uimm5:$imm)>;

def : InstAlias<"bsr $dst", (BSR32 call_symbol:$dst)>;

def : InstAlias<"grs\t$rz, $offset", (GRS32 GPR:$rz, bare_symbol:$offset)>;

def : InstAlias<"jbsr\t$src1", (JBSR32 call_symbol:$src1)>;

def : InstAlias<"jbr $dst", (JBR16 br_symbol_16bit:$dst)>;
def : InstAlias<"jbt $dst", (JBT16 C, br_symbol_16bit:$dst)>;
def : InstAlias<"jbf $dst", (JBF16 C, br_symbol_16bit:$dst)>;

def : InstAlias<"lrw $rz, $src", (PseudoLRW16 mGPR:$rz, bare_symbol:$src)>;
def : InstAlias<"lrw $rz, $src", (LRW16 mGPR:$rz, constpool_symbol_16bit:$src)>;
def : InstAlias<"lrw $rz, $src", (PseudoLRW32 GPR:$rz, bare_symbol:$src)>;
def : InstAlias<"lrw $rz, $src", (LRW32 GPR:$rz, constpool_symbol:$src)>;

def : InstAlias<"jsri $dst", (PseudoJSRI32 call_symbol:$dst)>;
def : InstAlias<"jsri $dst", (JSRI32 constpool_symbol:$dst)>;

def : InstAlias<"jmpi $dst", (PseudoJMPI32 br_symbol:$dst)>;
def : InstAlias<"jmpi $dst", (JMPI32 constpool_symbol:$dst)>;