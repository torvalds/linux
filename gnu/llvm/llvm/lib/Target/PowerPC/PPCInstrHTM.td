//===-- PPCInstrHTM.td - The PowerPC Hardware Transactional Memory  -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the Hardware Transactional Memory extension to the
// PowerPC instruction set.
//
//===----------------------------------------------------------------------===//



def HasHTM : Predicate<"Subtarget->hasHTM()">;

def HTM_get_imm : SDNodeXForm<imm, [{
  return getI32Imm (N->getZExtValue(), SDLoc(N));
}]>;

let hasSideEffects = 1 in {
def TCHECK_RET : PPCCustomInserterPseudo<(outs gprc:$out), (ins), "#TCHECK_RET", []>;
def TBEGIN_RET : PPCCustomInserterPseudo<(outs gprc:$out), (ins u1imm:$R), "#TBEGIN_RET", []>;
}


let Predicates = [HasHTM] in {

let Defs = [CR0] in {
def TBEGIN : XForm_htm0 <31, 654,
                         (outs), (ins u1imm:$R), "tbegin. $R", IIC_SprMTSPR>;

def TEND : XForm_htm1 <31, 686,
                       (outs), (ins u1imm:$A), "tend. $A", IIC_SprMTSPR>;

def TABORT : XForm_base_r3xo <31, 910,
                              (outs), (ins gprc:$RA), "tabort. $RA", IIC_SprMTSPR,
                              []>, isRecordForm {
  let RST = 0;
  let RB = 0;
}

def TABORTWC : XForm_base_r3xo <31, 782,
                                (outs), (ins u5imm:$RST, gprc:$RA, gprc:$RB),
                                "tabortwc. $RST, $RA, $RB", IIC_SprMTSPR, []>,
                                isRecordForm;

def TABORTWCI : XForm_base_r3xo <31, 846,
                                 (outs), (ins u5imm:$RST, gprc:$RA, u5imm:$RB),
                                 "tabortwci. $RST, $RA, $RB", IIC_SprMTSPR, []>,
                                 isRecordForm;

def TABORTDC : XForm_base_r3xo <31, 814,
                                (outs), (ins u5imm:$RST, gprc:$RA, gprc:$RB),
                                "tabortdc. $RST, $RA, $RB", IIC_SprMTSPR, []>,
                                isRecordForm;

def TABORTDCI : XForm_base_r3xo <31, 878,
                                 (outs), (ins u5imm:$RST, gprc:$RA, u5imm:$RB),
                                 "tabortdci. $RST, $RA, $RB", IIC_SprMTSPR, []>,
                                 isRecordForm;

def TSR : XForm_htm2 <31, 750,
                      (outs), (ins u1imm:$L), "tsr. $L", IIC_SprMTSPR>,
                      isRecordForm;

def TRECLAIM : XForm_base_r3xo <31, 942,
                                (outs), (ins gprc:$RA), "treclaim. $RA",
                                IIC_SprMTSPR, []>,
                                isRecordForm {
  let RST = 0;
  let RB = 0;
}

def TRECHKPT : XForm_base_r3xo <31, 1006,
                                (outs), (ins), "trechkpt.", IIC_SprMTSPR, []>,
                                isRecordForm {
  let RST = 0;
  let RA = 0;
  let RB = 0;
}

}

def TCHECK : XForm_htm3 <31, 718,
                        (outs crrc:$BF), (ins), "tcheck $BF", IIC_SprMTSPR>;
// Builtins

// All HTM instructions, with the exception of tcheck, set CR0 with the
// value of the MSR Transaction State (TS) bits that exist before the
// instruction is executed.  For tbegin., the EQ bit in CR0 can be used
// to determine whether the transaction was successfully started (0) or
// failed (1).  We use an XORI pattern to 'flip' the bit to match the
// tbegin builtin API which defines a return value of 1 as success.

def : Pat<(int_ppc_tbegin i32:$R),
           (XORI (TBEGIN_RET(HTM_get_imm imm:$R)), 1)>;

def : Pat<(int_ppc_tend i32:$R),
          (TEND (HTM_get_imm imm:$R))>;

def : Pat<(int_ppc_tabort i32:$R),
          (TABORT $R)>;

def : Pat<(int_ppc_tabortwc i32:$TO, i32:$RA, i32:$RB),
          (TABORTWC (HTM_get_imm imm:$TO), $RA, $RB)>;

def : Pat<(int_ppc_tabortwci i32:$TO, i32:$RA, i32:$SI),
          (TABORTWCI (HTM_get_imm imm:$TO), $RA, (HTM_get_imm imm:$SI))>;

def : Pat<(int_ppc_tabortdc i32:$TO, i32:$RA, i32:$RB),
          (TABORTDC (HTM_get_imm imm:$TO), $RA, $RB)>;

def : Pat<(int_ppc_tabortdci i32:$TO, i32:$RA, i32:$SI),
          (TABORTDCI (HTM_get_imm imm:$TO), $RA, (HTM_get_imm imm:$SI))>;

def : Pat<(int_ppc_tcheck),
          (TCHECK_RET)>;

def : Pat<(int_ppc_treclaim i32:$RA),
          (TRECLAIM $RA)>;

def : Pat<(int_ppc_trechkpt),
          (TRECHKPT)>;

def : Pat<(int_ppc_tsr i32:$L),
          (TSR (HTM_get_imm imm:$L))>;

def : Pat<(int_ppc_get_texasr),
          (MFSPR8 130)>;

def : Pat<(int_ppc_get_texasru),
          (MFSPR8 131)>;

def : Pat<(int_ppc_get_tfhar),
          (MFSPR8 128)>;

def : Pat<(int_ppc_get_tfiar),
          (MFSPR8 129)>;


def : Pat<(int_ppc_set_texasr i64:$V),
          (MTSPR8 130, $V)>;

def : Pat<(int_ppc_set_texasru i64:$V),
          (MTSPR8 131, $V)>;

def : Pat<(int_ppc_set_tfhar i64:$V),
          (MTSPR8 128, $V)>;

def : Pat<(int_ppc_set_tfiar i64:$V),
          (MTSPR8 129, $V)>;


// Extended mnemonics
def : Pat<(int_ppc_tendall),
          (TEND 1)>;

def : Pat<(int_ppc_tresume),
          (TSR 1)>;

def : Pat<(int_ppc_tsuspend),
          (TSR 0)>;

def : Pat<(i64 (int_ppc_ttest)),
          (i64 (INSERT_SUBREG
                (i64 (IMPLICIT_DEF)), (TABORTWCI 0, (LI 0), 0), sub_32))>;

} // [HasHTM]

def : InstAlias<"tend.", (TEND 0)>, Requires<[HasHTM]>;
def : InstAlias<"tendall.", (TEND 1)>, Requires<[HasHTM]>;
def : InstAlias<"tsuspend.", (TSR 0)>, Requires<[HasHTM]>;
def : InstAlias<"tresume.", (TSR 1)>, Requires<[HasHTM]>;
