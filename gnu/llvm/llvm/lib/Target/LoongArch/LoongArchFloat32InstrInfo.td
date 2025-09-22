// LoongArchFloat32InstrInfo.td - Single-Precision Float instr --*- tablegen -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the baisc single-precision floating-point instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// LoongArch specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDT_LoongArchMOVGR2FR_W_LA64
    : SDTypeProfile<1, 1, [SDTCisVT<0, f32>, SDTCisVT<1, i64>]>;
def SDT_LoongArchMOVFR2GR_S_LA64
    : SDTypeProfile<1, 1, [SDTCisVT<0, i64>, SDTCisVT<1, f32>]>;
def SDT_LoongArchFTINT : SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisFP<1>]>;

def loongarch_movgr2fr_w_la64
    : SDNode<"LoongArchISD::MOVGR2FR_W_LA64", SDT_LoongArchMOVGR2FR_W_LA64>;
def loongarch_movfr2gr_s_la64
    : SDNode<"LoongArchISD::MOVFR2GR_S_LA64", SDT_LoongArchMOVFR2GR_S_LA64>;
def loongarch_ftint : SDNode<"LoongArchISD::FTINT", SDT_LoongArchFTINT>;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let Predicates = [HasBasicF] in {

// Arithmetic Operation Instructions
def FADD_S : FP_ALU_3R<0x01008000>;
def FSUB_S : FP_ALU_3R<0x01028000>;
def FMUL_S : FP_ALU_3R<0x01048000>;
def FDIV_S : FP_ALU_3R<0x01068000>;
def FMADD_S  : FP_ALU_4R<0x08100000>;
def FMSUB_S  : FP_ALU_4R<0x08500000>;
def FNMADD_S : FP_ALU_4R<0x08900000>;
def FNMSUB_S : FP_ALU_4R<0x08d00000>;
def FMAX_S  : FP_ALU_3R<0x01088000>;
def FMIN_S  : FP_ALU_3R<0x010a8000>;
def FMAXA_S : FP_ALU_3R<0x010c8000>;
def FMINA_S : FP_ALU_3R<0x010e8000>;
def FABS_S   : FP_ALU_2R<0x01140400>;
def FNEG_S   : FP_ALU_2R<0x01141400>;
def FSQRT_S  : FP_ALU_2R<0x01144400>;
def FRECIP_S : FP_ALU_2R<0x01145400>;
def FRSQRT_S : FP_ALU_2R<0x01146400>;
def FRECIPE_S : FP_ALU_2R<0x01147400>;
def FRSQRTE_S : FP_ALU_2R<0x01148400>;
def FSCALEB_S : FP_ALU_3R<0x01108000>;
def FLOGB_S   : FP_ALU_2R<0x01142400>;
def FCOPYSIGN_S : FP_ALU_3R<0x01128000>;
def FCLASS_S  : FP_ALU_2R<0x01143400>;


// Comparison Instructions
def FCMP_CAF_S  : FP_CMP<0x0c100000>;
def FCMP_CUN_S  : FP_CMP<0x0c140000>;
def FCMP_CEQ_S  : FP_CMP<0x0c120000>;
def FCMP_CUEQ_S : FP_CMP<0x0c160000>;
def FCMP_CLT_S  : FP_CMP<0x0c110000>;
def FCMP_CULT_S : FP_CMP<0x0c150000>;
def FCMP_CLE_S  : FP_CMP<0x0c130000>;
def FCMP_CULE_S : FP_CMP<0x0c170000>;
def FCMP_CNE_S  : FP_CMP<0x0c180000>;
def FCMP_COR_S  : FP_CMP<0x0c1a0000>;
def FCMP_CUNE_S : FP_CMP<0x0c1c0000>;
def FCMP_SAF_S  : FP_CMP<0x0c108000>;
def FCMP_SUN_S  : FP_CMP<0x0c148000>;
def FCMP_SEQ_S  : FP_CMP<0x0c128000>;
def FCMP_SUEQ_S : FP_CMP<0x0c168000>;
def FCMP_SLT_S  : FP_CMP<0x0c118000>;
def FCMP_SULT_S : FP_CMP<0x0c158000>;
def FCMP_SLE_S  : FP_CMP<0x0c138000>;
def FCMP_SULE_S : FP_CMP<0x0c178000>;
def FCMP_SNE_S  : FP_CMP<0x0c188000>;
def FCMP_SOR_S  : FP_CMP<0x0c1a8000>;
def FCMP_SUNE_S : FP_CMP<0x0c1c8000>;

// Conversion Instructions
def FFINT_S_W    : FP_CONV<0x011d1000>;
def FTINT_W_S    : FP_CONV<0x011b0400>;
def FTINTRM_W_S  : FP_CONV<0x011a0400>;
def FTINTRP_W_S  : FP_CONV<0x011a4400>;
def FTINTRZ_W_S  : FP_CONV<0x011a8400>;
def FTINTRNE_W_S : FP_CONV<0x011ac400>;
def FRINT_S      : FP_CONV<0x011e4400>;

// Move Instructions
def FSEL_xS    : FP_SEL<0x0d000000>;
def FMOV_S     : FP_MOV<0x01149400>;
def MOVGR2FR_W : FP_MOV<0x0114a400, FPR32, GPR>;
def MOVFR2GR_S : FP_MOV<0x0114b400, GPR, FPR32>;
let hasSideEffects = 1 in {
def MOVGR2FCSR : FP_MOV<0x0114c000, FCSR, GPR>;
def MOVFCSR2GR : FP_MOV<0x0114c800, GPR, FCSR>;
} // hasSideEffects = 1
def MOVFR2CF_xS : FP_MOV<0x0114d000, CFR, FPR32>;
def MOVCF2FR_xS : FP_MOV<0x0114d400, FPR32, CFR>;
def MOVGR2CF    : FP_MOV<0x0114d800, CFR, GPR>;
def MOVCF2GR    : FP_MOV<0x0114dc00, GPR, CFR>;

// Branch Instructions
def BCEQZ : FP_BRANCH<0x48000000>;
def BCNEZ : FP_BRANCH<0x48000100>;

// Common Memory Access Instructions
def FLD_S : FP_LOAD_2RI12<0x2b000000>;
def FST_S : FP_STORE_2RI12<0x2b400000>;
def FLDX_S : FP_LOAD_3R<0x38300000>;
def FSTX_S : FP_STORE_3R<0x38380000>;

// Bound Check Memory Access Instructions
def FLDGT_S : FP_LOAD_3R<0x38740000>;
def FLDLE_S : FP_LOAD_3R<0x38750000>;
def FSTGT_S : FP_STORE_3R<0x38760000>;
def FSTLE_S : FP_STORE_3R<0x38770000>;

// Pseudo instructions for spill/reload CFRs.
let hasSideEffects = 0, mayLoad = 0, mayStore = 1 in
def PseudoST_CFR : Pseudo<(outs),
                          (ins CFR:$ccd, GPR:$rj, grlenimm:$imm)>;
let hasSideEffects = 0, mayLoad = 1, mayStore = 0 in
def PseudoLD_CFR : Pseudo<(outs CFR:$ccd),
                          (ins GPR:$rj, grlenimm:$imm)>;

// SET_CFR_{FALSE,TRUE}
// These instructions are defined in order to avoid expensive check error if
// regular instruction patterns are used.
// fcmp.caf.s $dst, $fa0, $fa0
def SET_CFR_FALSE : SET_CFR<0x0c100000, "fcmp.caf.s">;
// fcmp.cueq.s $dst, $fa0, $fa0
def SET_CFR_TRUE  : SET_CFR<0x0c160000, "fcmp.cueq.s">;

// Pseudo instruction for copying CFRs.
def PseudoCopyCFR : Pseudo<(outs CFR:$dst), (ins CFR:$src)> {
  let mayLoad = 0;
  let mayStore = 0;
  let hasSideEffects = 0;
  let Size = 12;
}

} // Predicates = [HasBasicF]

//===----------------------------------------------------------------------===//
// Pseudo-instructions and codegen patterns
//===----------------------------------------------------------------------===//

/// Generic pattern classes

class PatFpr<SDPatternOperator OpNode, LAInst Inst, RegisterClass RegTy>
    : Pat<(OpNode RegTy:$fj), (Inst $fj)>;
class PatFprFpr<SDPatternOperator OpNode, LAInst Inst, RegisterClass RegTy>
    : Pat<(OpNode RegTy:$fj, RegTy:$fk), (Inst $fj, $fk)>;

let Predicates = [HasBasicF] in {

/// Float arithmetic operations

def : PatFprFpr<fadd, FADD_S, FPR32>;
def : PatFprFpr<fsub, FSUB_S, FPR32>;
def : PatFprFpr<fmul, FMUL_S, FPR32>;
def : PatFprFpr<fdiv, FDIV_S, FPR32>;
def : PatFprFpr<fcopysign, FCOPYSIGN_S, FPR32>;
def : PatFprFpr<fmaxnum_ieee, FMAX_S, FPR32>;
def : PatFprFpr<fminnum_ieee, FMIN_S, FPR32>;
def : PatFpr<fneg, FNEG_S, FPR32>;
def : PatFpr<fabs, FABS_S, FPR32>;
def : PatFpr<fsqrt, FSQRT_S, FPR32>;
def : Pat<(fdiv fpimm1, (fsqrt FPR32:$fj)), (FRSQRT_S FPR32:$fj)>;
def : Pat<(fcanonicalize FPR32:$fj), (FMAX_S $fj, $fj)>;
def : Pat<(is_fpclass FPR32:$fj, (i32 timm:$mask)),
          (SLTU R0, (ANDI (MOVFR2GR_S (FCLASS_S FPR32:$fj)),
                          (to_fclass_mask timm:$mask)))>;

/// Setcc

// Match non-signaling comparison

class PatFPSetcc<CondCode cc, LAInst CmpInst, RegisterClass RegTy>
    : Pat<(any_fsetcc RegTy:$fj, RegTy:$fk, cc),
          (CmpInst RegTy:$fj, RegTy:$fk)>;
// SETOGT/SETOGE/SETUGT/SETUGE/SETGE/SETNE/SETGT will expand into
// SETOLT/SETOLE/SETULT/SETULE/SETLE/SETEQ/SETLT.
def : PatFPSetcc<SETOEQ, FCMP_CEQ_S,  FPR32>;
def : PatFPSetcc<SETEQ,  FCMP_CEQ_S,  FPR32>;
def : PatFPSetcc<SETOLT, FCMP_CLT_S,  FPR32>;
def : PatFPSetcc<SETOLE, FCMP_CLE_S,  FPR32>;
def : PatFPSetcc<SETLE,  FCMP_CLE_S,  FPR32>;
def : PatFPSetcc<SETONE, FCMP_CNE_S,  FPR32>;
def : PatFPSetcc<SETO,   FCMP_COR_S,  FPR32>;
def : PatFPSetcc<SETUEQ, FCMP_CUEQ_S, FPR32>;
def : PatFPSetcc<SETULT, FCMP_CULT_S, FPR32>;
def : PatFPSetcc<SETULE, FCMP_CULE_S, FPR32>;
def : PatFPSetcc<SETUNE, FCMP_CUNE_S, FPR32>;
def : PatFPSetcc<SETUO,  FCMP_CUN_S,  FPR32>;
def : PatFPSetcc<SETLT,  FCMP_CLT_S,  FPR32>;

multiclass PatFPBrcond<CondCode cc, LAInst CmpInst, RegisterClass RegTy> {
  def : Pat<(brcond (xor (GRLenVT (setcc RegTy:$fj, RegTy:$fk, cc)), -1),
                     bb:$imm21),
            (BCEQZ (CmpInst RegTy:$fj, RegTy:$fk), bb:$imm21)>;
  def : Pat<(brcond (GRLenVT (setcc RegTy:$fj, RegTy:$fk, cc)), bb:$imm21),
            (BCNEZ (CmpInst RegTy:$fj, RegTy:$fk), bb:$imm21)>;
}

defm : PatFPBrcond<SETOEQ, FCMP_CEQ_S, FPR32>;
defm : PatFPBrcond<SETOLT, FCMP_CLT_S, FPR32>;
defm : PatFPBrcond<SETOLE, FCMP_CLE_S, FPR32>;
defm : PatFPBrcond<SETONE, FCMP_CNE_S, FPR32>;
defm : PatFPBrcond<SETO,   FCMP_COR_S, FPR32>;
defm : PatFPBrcond<SETUEQ, FCMP_CUEQ_S, FPR32>;
defm : PatFPBrcond<SETULT, FCMP_CULT_S, FPR32>;
defm : PatFPBrcond<SETULE, FCMP_CULE_S, FPR32>;
defm : PatFPBrcond<SETUNE, FCMP_CUNE_S, FPR32>;
defm : PatFPBrcond<SETUO,  FCMP_CUN_S, FPR32>;
defm : PatFPBrcond<SETLT,  FCMP_CLT_S, FPR32>;

// Match signaling comparison

class PatStrictFsetccs<CondCode cc, LAInst CmpInst, RegisterClass RegTy>
    : Pat<(strict_fsetccs RegTy:$fj, RegTy:$fk, cc),
          (CmpInst RegTy:$fj, RegTy:$fk)>;
def : PatStrictFsetccs<SETOEQ, FCMP_SEQ_S,  FPR32>;
def : PatStrictFsetccs<SETOLT, FCMP_SLT_S,  FPR32>;
def : PatStrictFsetccs<SETOLE, FCMP_SLE_S,  FPR32>;
def : PatStrictFsetccs<SETONE, FCMP_SNE_S,  FPR32>;
def : PatStrictFsetccs<SETO,   FCMP_SOR_S,  FPR32>;
def : PatStrictFsetccs<SETUEQ, FCMP_SUEQ_S, FPR32>;
def : PatStrictFsetccs<SETULT, FCMP_SULT_S, FPR32>;
def : PatStrictFsetccs<SETULE, FCMP_SULE_S, FPR32>;
def : PatStrictFsetccs<SETUNE, FCMP_SUNE_S, FPR32>;
def : PatStrictFsetccs<SETUO,  FCMP_SUN_S,  FPR32>;
def : PatStrictFsetccs<SETLT,  FCMP_SLT_S,  FPR32>;

/// Select

def : Pat<(select CFR:$cc, FPR32:$fk, FPR32:$fj),
          (FSEL_xS FPR32:$fj, FPR32:$fk, CFR:$cc)>;

/// Selectcc

class PatFPSelectcc<CondCode cc, LAInst CmpInst, LAInst SelInst,
                    RegisterClass RegTy>
    : Pat<(select (GRLenVT (setcc RegTy:$a, RegTy:$b, cc)), RegTy:$t, RegTy:$f),
          (SelInst RegTy:$f, RegTy:$t, (CmpInst RegTy:$a, RegTy:$b))>;
def : PatFPSelectcc<SETOEQ, FCMP_CEQ_S,  FSEL_xS, FPR32>;
def : PatFPSelectcc<SETOLT, FCMP_CLT_S,  FSEL_xS, FPR32>;
def : PatFPSelectcc<SETOLE, FCMP_CLE_S,  FSEL_xS, FPR32>;
def : PatFPSelectcc<SETONE, FCMP_CNE_S,  FSEL_xS, FPR32>;
def : PatFPSelectcc<SETO,   FCMP_COR_S,  FSEL_xS, FPR32>;
def : PatFPSelectcc<SETUEQ, FCMP_CUEQ_S, FSEL_xS, FPR32>;
def : PatFPSelectcc<SETULT, FCMP_CULT_S, FSEL_xS, FPR32>;
def : PatFPSelectcc<SETULE, FCMP_CULE_S, FSEL_xS, FPR32>;
def : PatFPSelectcc<SETUNE, FCMP_CUNE_S, FSEL_xS, FPR32>;
def : PatFPSelectcc<SETUO,  FCMP_CUN_S,  FSEL_xS, FPR32>;

/// Loads

defm : LdPat<load, FLD_S, f32>;
def : RegRegLdPat<load, FLDX_S, f32>;

/// Stores

defm : StPat<store, FST_S, FPR32, f32>;
def : RegRegStPat<store, FSTX_S, FPR32, f32>;

/// Floating point constants

def : Pat<(f32 fpimm0), (MOVGR2FR_W R0)>;
def : Pat<(f32 fpimm0neg), (FNEG_S (MOVGR2FR_W R0))>;
def : Pat<(f32 fpimm1), (FFINT_S_W (MOVGR2FR_W (ADDI_W R0, 1)))>;

// FP Conversion
def : Pat<(loongarch_ftint FPR32:$src), (FTINTRZ_W_S FPR32:$src)>;

// FP reciprocal operation
def : Pat<(fdiv fpimm1, FPR32:$src), (FRECIP_S $src)>;

let Predicates = [HasFrecipe] in {
// FP approximate reciprocal operation
def : Pat<(int_loongarch_frecipe_s FPR32:$src), (FRECIPE_S FPR32:$src)>;
def : Pat<(int_loongarch_frsqrte_s FPR32:$src), (FRSQRTE_S FPR32:$src)>;
}

// fmadd.s: fj * fk + fa
def : Pat<(fma FPR32:$fj, FPR32:$fk, FPR32:$fa), (FMADD_S $fj, $fk, $fa)>;

// fmsub.s: fj * fk - fa
def : Pat<(fma FPR32:$fj, FPR32:$fk, (fneg FPR32:$fa)),
          (FMSUB_S FPR32:$fj, FPR32:$fk, FPR32:$fa)>;

// fnmadd.s: -(fj * fk + fa)
def : Pat<(fneg (fma FPR32:$fj, FPR32:$fk, FPR32:$fa)),
          (FNMADD_S FPR32:$fj, FPR32:$fk, FPR32:$fa)>;

// fnmadd.s: -fj * fk - fa (the nsz flag on the FMA)
def : Pat<(fma_nsz (fneg FPR32:$fj), FPR32:$fk, (fneg FPR32:$fa)),
          (FNMADD_S FPR32:$fj, FPR32:$fk, FPR32:$fa)>;

// fnmsub.s: -(fj * fk - fa)
def : Pat<(fneg (fma FPR32:$fj, FPR32:$fk, (fneg FPR32:$fa))),
          (FNMSUB_S FPR32:$fj, FPR32:$fk, FPR32:$fa)>;

// fnmsub.s: -fj * fk + fa (the nsz flag on the FMA)
def : Pat<(fma_nsz (fneg FPR32:$fj), FPR32:$fk, FPR32:$fa),
          (FNMSUB_S FPR32:$fj, FPR32:$fk, FPR32:$fa)>;
} // Predicates = [HasBasicF]

let Predicates = [HasBasicF, IsLA64] in {
// GPR -> FPR
def : Pat<(loongarch_movgr2fr_w_la64 GPR:$src), (MOVGR2FR_W GPR:$src)>;
// FPR -> GPR
def : Pat<(loongarch_movfr2gr_s_la64 FPR32:$src),
          (MOVFR2GR_S FPR32:$src)>;
// int -> f32
def : Pat<(f32 (sint_to_fp (i64 (sexti32 (i64 GPR:$src))))),
          (FFINT_S_W (MOVGR2FR_W GPR:$src))>;
// uint -> f32
def : Pat<(f32 (uint_to_fp (i64 (sexti32 (i64 GPR:$src))))),
          (FFINT_S_W (MOVGR2FR_W GPR:$src))>;
} // Predicates = [HasBasicF, IsLA64]

// FP Rounding
let Predicates = [HasBasicF, IsLA64] in {
def : PatFpr<frint, FRINT_S, FPR32>;
} // Predicates = [HasBasicF, IsLA64]

let Predicates = [HasBasicF, IsLA32] in {
// GPR -> FPR
def : Pat<(bitconvert (i32 GPR:$src)), (MOVGR2FR_W GPR:$src)>;
// FPR -> GPR
def : Pat<(i32 (bitconvert FPR32:$src)), (MOVFR2GR_S FPR32:$src)>;
// int -> f32
def : Pat<(f32 (sint_to_fp (i32 GPR:$src))), (FFINT_S_W (MOVGR2FR_W GPR:$src))>;
} // Predicates = [HasBasicF, IsLA32]
