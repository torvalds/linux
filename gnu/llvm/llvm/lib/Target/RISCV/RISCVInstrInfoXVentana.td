//===-- RISCVInstrInfoXVentana.td --------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the vendor extensions defined by Ventana Micro Systems.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// XVentanaCondOps
//===----------------------------------------------------------------------===//

let Predicates = [IsRV64, HasVendorXVentanaCondOps], hasSideEffects = 0,
    mayLoad = 0, mayStore = 0, isCodeGenOnly = 0, DecoderNamespace = "XVentana" in
class VTMaskedMove<bits<3> funct3, string opcodestr>
    : RVInstR<0b0000000, funct3, OPC_CUSTOM_3, (outs GPR:$rd),
              (ins GPR:$rs1, GPR:$rs2), opcodestr,
              "$rd, $rs1, $rs2"> {
}

def VT_MASKC : VTMaskedMove<0b110, "vt.maskc">,
           Sched<[WriteIALU, ReadIALU, ReadIALU]>;

def VT_MASKCN : VTMaskedMove<0b111, "vt.maskcn">,
           Sched<[WriteIALU, ReadIALU, ReadIALU]>;

let Predicates = [IsRV64, HasVendorXVentanaCondOps] in {
def : Pat<(i64 (riscv_czero_eqz GPR:$rs1, GPR:$rc)),
          (VT_MASKC GPR:$rs1, GPR:$rc)>;
def : Pat<(i64 (riscv_czero_nez GPR:$rs1, GPR:$rc)),
          (VT_MASKCN GPR:$rs1, GPR:$rc)>;

def : Pat<(i64 (riscv_czero_eqz GPR:$rs1, (riscv_setne (i64 GPR:$rc)))),
          (VT_MASKC GPR:$rs1, GPR:$rc)>;
def : Pat<(i64 (riscv_czero_eqz GPR:$rs1, (riscv_seteq (i64 GPR:$rc)))),
          (VT_MASKCN GPR:$rs1, GPR:$rc)>;
def : Pat<(i64 (riscv_czero_nez GPR:$rs1, (riscv_setne (i64 GPR:$rc)))),
          (VT_MASKCN GPR:$rs1, GPR:$rc)>;
def : Pat<(i64 (riscv_czero_nez GPR:$rs1, (riscv_seteq (i64 GPR:$rc)))),
          (VT_MASKC GPR:$rs1, GPR:$rc)>;
} // Predicates = [IsRV64, HasVendorXVentanaCondOps]
