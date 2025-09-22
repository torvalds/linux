//===- LoongArchLVZInstrInfo.td - LoongArch LVZ instructions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the LVZ extension instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let hasSideEffects = 1, mayLoad = 0, mayStore = 0, Predicates = [HasExtLVZ] in {

def GCSRRD : FmtCSR<0x05000000, (outs GPR:$rd), (ins uimm14:$csr_num),
                    "$rd, $csr_num">;

let Constraints = "$rd = $dst" in {
def GCSRWR : FmtCSR<0x05000020, (outs GPR:$dst),
                    (ins GPR:$rd, uimm14:$csr_num), "$rd, $csr_num">;
def GCSRXCHG : FmtCSRXCHG<0x05000000, (outs GPR:$dst),
                          (ins GPR:$rd, GPR:$rj, uimm14:$csr_num),
                          "$rd, $rj, $csr_num">;
} // Constraints = "$rd = $dst"

def GTLBFLUSH : FmtI32<0x06482401>;
def HVCL : MISC_I15<0x002b8000>;

} // hasSideEffects = 1, mayLoad = 0, mayStore = 0, Predicates = [HasExtLVZ]
