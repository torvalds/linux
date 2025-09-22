//===-- PPCInstrInfo.td - The PowerPC Instruction Set ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the subset of the 32-bit PowerPC instruction set, as used
// by the PowerPC instruction selector.
//
//===----------------------------------------------------------------------===//

include "PPCInstrFormats.td"

//===----------------------------------------------------------------------===//
// PowerPC specific type constraints.
//
def SDT_PPCstfiwx : SDTypeProfile<0, 2, [ // stfiwx
  SDTCisVT<0, f64>, SDTCisPtrTy<1>
]>;
def SDT_PPClfiwx : SDTypeProfile<1, 1, [ // lfiw[az]x
  SDTCisVT<0, f64>, SDTCisPtrTy<1>
]>;
def SDT_PPCLxsizx : SDTypeProfile<1, 2, [
  SDTCisVT<0, f64>, SDTCisPtrTy<1>, SDTCisPtrTy<2>
]>;
def SDT_PPCstxsix : SDTypeProfile<0, 3, [
  SDTCisVT<0, f64>, SDTCisPtrTy<1>, SDTCisPtrTy<2>
]>;
def SDT_PPCcv_fp_to_int  : SDTypeProfile<1, 1, [
  SDTCisFP<0>, SDTCisFP<1>
  ]>;
def SDT_PPCstore_scal_int_from_vsr : SDTypeProfile<0, 3, [
  SDTCisFP<0>, SDTCisPtrTy<1>, SDTCisPtrTy<2>
]>;
def SDT_PPCVexts  : SDTypeProfile<1, 2, [
  SDTCisVT<0, f64>, SDTCisVT<1, f64>, SDTCisPtrTy<2>
]>;

def SDT_PPCCallSeqStart : SDCallSeqStart<[ SDTCisVT<0, i32>,
                                           SDTCisVT<1, i32> ]>;
def SDT_PPCCallSeqEnd   : SDCallSeqEnd<[ SDTCisVT<0, i32>,
                                         SDTCisVT<1, i32> ]>;
def SDT_PPCvperm   : SDTypeProfile<1, 3, [
  SDTCisVT<3, v16i8>, SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>
]>;

def SDT_PPCVecSplat : SDTypeProfile<1, 2, [ SDTCisVec<0>,
  SDTCisVec<1>, SDTCisInt<2>
]>;

def SDT_PPCSpToDp : SDTypeProfile<1, 1, [ SDTCisVT<0, v2f64>,
  SDTCisInt<1>
]>;

def SDT_PPCVecShift : SDTypeProfile<1, 3, [ SDTCisVec<0>,
  SDTCisVec<1>, SDTCisVec<2>, SDTCisPtrTy<3>
]>;

def SDT_PPCVecInsert : SDTypeProfile<1, 3, [ SDTCisVec<0>,
  SDTCisVec<1>, SDTCisVec<2>, SDTCisInt<3>
]>;

def SDT_PPCxxpermdi: SDTypeProfile<1, 3, [ SDTCisVec<0>,
  SDTCisVec<1>, SDTCisVec<2>, SDTCisInt<3>
]>;

def SDT_PPCvcmp : SDTypeProfile<1, 3, [
  SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>, SDTCisVT<3, i32>
]>;

def SDT_PPCcondbr : SDTypeProfile<0, 3, [
  SDTCisVT<0, i32>, SDTCisVT<2, OtherVT>
]>;

def SDT_PPCFtsqrt : SDTypeProfile<1, 1, [
  SDTCisVT<0, i32>]>;

def SDT_PPClbrx : SDTypeProfile<1, 2, [
  SDTCisInt<0>, SDTCisPtrTy<1>, SDTCisVT<2, OtherVT>
]>;
def SDT_PPCstbrx : SDTypeProfile<0, 3, [
  SDTCisInt<0>, SDTCisPtrTy<1>, SDTCisVT<2, OtherVT>
]>;
def SDT_StoreCond : SDTypeProfile<0, 3, [
  SDTCisPtrTy<0>, SDTCisInt<1>, SDTCisPtrTy<2>
]>;

def SDT_PPCTC_ret : SDTypeProfile<0, 2, [
  SDTCisPtrTy<0>, SDTCisVT<1, i32>
]>;

def tocentry32 : Operand<iPTR> {
  let MIOperandInfo = (ops i32imm:$imm);
}

def SDT_PPCqvfperm   : SDTypeProfile<1, 3, [
  SDTCisVec<0>, SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisVec<3>
]>;
def SDT_PPCqvgpci   : SDTypeProfile<1, 1, [
  SDTCisVec<0>, SDTCisInt<1>
]>;
def SDT_PPCqvaligni   : SDTypeProfile<1, 3, [
  SDTCisVec<0>, SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisInt<3>
]>;
def SDT_PPCqvesplati   : SDTypeProfile<1, 2, [
  SDTCisVec<0>, SDTCisSameAs<0, 1>, SDTCisInt<2>
]>;

def SDT_PPCqbflt : SDTypeProfile<1, 1, [
  SDTCisVec<0>, SDTCisVec<1>
]>;

def SDT_PPCqvlfsb : SDTypeProfile<1, 1, [
  SDTCisVec<0>, SDTCisPtrTy<1>
]>;

def SDT_PPCextswsli : SDTypeProfile<1, 2, [  // extswsli
  SDTCisInt<0>, SDTCisInt<1>, SDTCisOpSmallerThanOp<1, 0>, SDTCisInt<2>
]>;

def SDT_PPCFPMinMax : SDTypeProfile<1, 2, [
  SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>, SDTCisFP<0>
]>;

//===----------------------------------------------------------------------===//
// PowerPC specific DAG Nodes.
//

def PPCfre    : SDNode<"PPCISD::FRE",     SDTFPUnaryOp, []>;
def PPCfrsqrte: SDNode<"PPCISD::FRSQRTE", SDTFPUnaryOp, []>;
def PPCfsqrt  : SDNode<"PPCISD::FSQRT",   SDTFPUnaryOp, []>;
def PPCftsqrt : SDNode<"PPCISD::FTSQRT",  SDT_PPCFtsqrt,[]>;

def PPCfcfid  : SDNode<"PPCISD::FCFID",   SDTFPUnaryOp, []>;
def PPCfcfidu : SDNode<"PPCISD::FCFIDU",  SDTFPUnaryOp, []>;
def PPCfcfids : SDNode<"PPCISD::FCFIDS",  SDTFPRoundOp, []>;
def PPCfcfidus: SDNode<"PPCISD::FCFIDUS", SDTFPRoundOp, []>;
def PPCfctidz : SDNode<"PPCISD::FCTIDZ", SDTFPUnaryOp, []>;
def PPCfctiwz : SDNode<"PPCISD::FCTIWZ", SDTFPUnaryOp, []>;
def PPCfctiduz: SDNode<"PPCISD::FCTIDUZ",SDTFPUnaryOp, []>;
def PPCfctiwuz: SDNode<"PPCISD::FCTIWUZ",SDTFPUnaryOp, []>;

def PPCstrict_fcfid : SDNode<"PPCISD::STRICT_FCFID",
                             SDTFPUnaryOp, [SDNPHasChain]>;
def PPCstrict_fcfidu : SDNode<"PPCISD::STRICT_FCFIDU",
                              SDTFPUnaryOp, [SDNPHasChain]>;
def PPCstrict_fcfids : SDNode<"PPCISD::STRICT_FCFIDS",
                             SDTFPRoundOp, [SDNPHasChain]>;
def PPCstrict_fcfidus : SDNode<"PPCISD::STRICT_FCFIDUS",
                              SDTFPRoundOp, [SDNPHasChain]>;

def PPCany_fcfid : PatFrags<(ops node:$op),
                             [(PPCfcfid node:$op),
                              (PPCstrict_fcfid node:$op)]>;
def PPCany_fcfidu : PatFrags<(ops node:$op),
                             [(PPCfcfidu node:$op),
                              (PPCstrict_fcfidu node:$op)]>;
def PPCany_fcfids : PatFrags<(ops node:$op),
                              [(PPCfcfids node:$op),
                               (PPCstrict_fcfids node:$op)]>;
def PPCany_fcfidus : PatFrags<(ops node:$op),
                              [(PPCfcfidus node:$op),
                               (PPCstrict_fcfidus node:$op)]>;

def PPCstore_scal_int_from_vsr:
   SDNode<"PPCISD::ST_VSR_SCAL_INT", SDT_PPCstore_scal_int_from_vsr,
           [SDNPHasChain, SDNPMayStore]>;
def PPCstfiwx : SDNode<"PPCISD::STFIWX", SDT_PPCstfiwx,
                       [SDNPHasChain, SDNPMayStore]>;
def PPClfiwax : SDNode<"PPCISD::LFIWAX", SDT_PPClfiwx,
                       [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPClfiwzx : SDNode<"PPCISD::LFIWZX", SDT_PPClfiwx,
                       [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPClxsizx : SDNode<"PPCISD::LXSIZX", SDT_PPCLxsizx,
                       [SDNPHasChain, SDNPMayLoad]>;
def PPCstxsix : SDNode<"PPCISD::STXSIX", SDT_PPCstxsix,
                       [SDNPHasChain, SDNPMayStore]>;
def PPCVexts  : SDNode<"PPCISD::VEXTS", SDT_PPCVexts, []>;

// Extract FPSCR (not modeled at the DAG level).
def PPCmffs   : SDNode<"PPCISD::MFFS",
                       SDTypeProfile<1, 0, [SDTCisVT<0, f64>]>,
                       [SDNPHasChain]>;

// Perform FADD in round-to-zero mode.
def PPCfaddrtz: SDNode<"PPCISD::FADDRTZ", SDTFPBinOp, []>;
def PPCstrict_faddrtz: SDNode<"PPCISD::STRICT_FADDRTZ", SDTFPBinOp,
                              [SDNPHasChain]>;

def PPCany_faddrtz: PatFrags<(ops node:$lhs, node:$rhs),
                             [(PPCfaddrtz node:$lhs, node:$rhs),
                              (PPCstrict_faddrtz node:$lhs, node:$rhs)]>;

def PPCfsel   : SDNode<"PPCISD::FSEL",
   // Type constraint for fsel.
   SDTypeProfile<1, 3, [SDTCisSameAs<0, 2>, SDTCisSameAs<0, 3>,
                        SDTCisFP<0>, SDTCisVT<1, f64>]>, []>;
def PPCxsmaxc : SDNode<"PPCISD::XSMAXC", SDT_PPCFPMinMax, []>;
def PPCxsminc : SDNode<"PPCISD::XSMINC", SDT_PPCFPMinMax, []>;
def PPChi       : SDNode<"PPCISD::Hi", SDTIntBinOp, []>;
def PPClo       : SDNode<"PPCISD::Lo", SDTIntBinOp, []>;
def PPCtoc_entry: SDNode<"PPCISD::TOC_ENTRY", SDTIntBinOp,
                         [SDNPMayLoad, SDNPMemOperand]>;

def PPCppc32GOT : SDNode<"PPCISD::PPC32_GOT", SDTIntLeaf, []>;

def PPCaddisGotTprelHA : SDNode<"PPCISD::ADDIS_GOT_TPREL_HA", SDTIntBinOp>;
def PPCldGotTprelL : SDNode<"PPCISD::LD_GOT_TPREL_L", SDTIntBinOp,
                            [SDNPMayLoad]>;
def PPCaddTls     : SDNode<"PPCISD::ADD_TLS", SDTIntBinOp, []>;
def PPCaddisTlsgdHA : SDNode<"PPCISD::ADDIS_TLSGD_HA", SDTIntBinOp>;
def PPCaddiTlsgdL   : SDNode<"PPCISD::ADDI_TLSGD_L", SDTIntBinOp>;
def PPCgetTlsAddr   : SDNode<"PPCISD::GET_TLS_ADDR", SDTIntBinOp>;
def PPCgetTlsMod   : SDNode<"PPCISD::GET_TLS_MOD_AIX", SDTIntUnaryOp>;
def PPCgetTpointer : SDNode<"PPCISD::GET_TPOINTER", SDTIntLeaf, []>;
def PPCaddiTlsgdLAddr : SDNode<"PPCISD::ADDI_TLSGD_L_ADDR",
                               SDTypeProfile<1, 3, [
                                 SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>,
                                 SDTCisSameAs<0, 3>, SDTCisInt<0> ]>>;
def PPCTlsgdAIX     : SDNode<"PPCISD::TLSGD_AIX", SDTIntBinOp>;
def PPCTlsldAIX     : SDNode<"PPCISD::TLSLD_AIX", SDTIntUnaryOp>;
def PPCaddisTlsldHA : SDNode<"PPCISD::ADDIS_TLSLD_HA", SDTIntBinOp>;
def PPCaddiTlsldL   : SDNode<"PPCISD::ADDI_TLSLD_L", SDTIntBinOp>;
def PPCgetTlsldAddr : SDNode<"PPCISD::GET_TLSLD_ADDR", SDTIntBinOp>;
def PPCaddiTlsldLAddr : SDNode<"PPCISD::ADDI_TLSLD_L_ADDR",
                               SDTypeProfile<1, 3, [
                                 SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>,
                                 SDTCisSameAs<0, 3>, SDTCisInt<0> ]>>;
def PPCaddisDtprelHA : SDNode<"PPCISD::ADDIS_DTPREL_HA", SDTIntBinOp>;
def PPCaddiDtprelL   : SDNode<"PPCISD::ADDI_DTPREL_L", SDTIntBinOp>;
def PPCpaddiDtprel   : SDNode<"PPCISD::PADDI_DTPREL", SDTIntBinOp>;

def PPCvperm     : SDNode<"PPCISD::VPERM", SDT_PPCvperm, []>;
def PPCxxsplt    : SDNode<"PPCISD::XXSPLT", SDT_PPCVecSplat, []>;
def PPCxxspltidp : SDNode<"PPCISD::XXSPLTI_SP_TO_DP", SDT_PPCSpToDp, []>;
def PPCvecinsert : SDNode<"PPCISD::VECINSERT", SDT_PPCVecInsert, []>;
def PPCxxpermdi  : SDNode<"PPCISD::XXPERMDI", SDT_PPCxxpermdi, []>;
def PPCvecshl    : SDNode<"PPCISD::VECSHL", SDT_PPCVecShift, []>;

def PPCcmpb     : SDNode<"PPCISD::CMPB", SDTIntBinOp, []>;

// These nodes represent the 32-bit PPC shifts that operate on 6-bit shift
// amounts.  These nodes are generated by the multi-precision shift code.
def PPCsrl        : SDNode<"PPCISD::SRL"       , SDTIntShiftOp>;
def PPCsra        : SDNode<"PPCISD::SRA"       , SDTIntShiftOp>;
def PPCshl        : SDNode<"PPCISD::SHL"       , SDTIntShiftOp>;

def PPCfnmsub     : SDNode<"PPCISD::FNMSUB"    , SDTFPTernaryOp>;

def PPCextswsli : SDNode<"PPCISD::EXTSWSLI" , SDT_PPCextswsli>;

def PPCstrict_fctidz : SDNode<"PPCISD::STRICT_FCTIDZ",
                              SDTFPUnaryOp, [SDNPHasChain]>;
def PPCstrict_fctiwz : SDNode<"PPCISD::STRICT_FCTIWZ",
                              SDTFPUnaryOp, [SDNPHasChain]>;
def PPCstrict_fctiduz : SDNode<"PPCISD::STRICT_FCTIDUZ",
                               SDTFPUnaryOp, [SDNPHasChain]>;
def PPCstrict_fctiwuz : SDNode<"PPCISD::STRICT_FCTIWUZ",
                                SDTFPUnaryOp, [SDNPHasChain]>;

def PPCany_fctidz : PatFrags<(ops node:$op),
                             [(PPCstrict_fctidz node:$op),
                              (PPCfctidz node:$op)]>;
def PPCany_fctiwz : PatFrags<(ops node:$op),
                             [(PPCstrict_fctiwz node:$op),
                              (PPCfctiwz node:$op)]>;
def PPCany_fctiduz : PatFrags<(ops node:$op),
                              [(PPCstrict_fctiduz node:$op),
                               (PPCfctiduz node:$op)]>;
def PPCany_fctiwuz : PatFrags<(ops node:$op),
                              [(PPCstrict_fctiwuz node:$op),
                               (PPCfctiwuz node:$op)]>;

// Move 2 i64 values into a VSX register
def PPCbuild_fp128: SDNode<"PPCISD::BUILD_FP128",
                           SDTypeProfile<1, 2,
                             [SDTCisFP<0>, SDTCisSameSizeAs<1,2>,
                              SDTCisSameAs<1,2>]>,
                           []>;

def PPCbuild_spe64: SDNode<"PPCISD::BUILD_SPE64",
                           SDTypeProfile<1, 2,
                             [SDTCisVT<0, f64>, SDTCisVT<1,i32>,
                             SDTCisVT<1,i32>]>,
                           []>;

def PPCextract_spe : SDNode<"PPCISD::EXTRACT_SPE",
                            SDTypeProfile<1, 2,
                              [SDTCisVT<0, i32>, SDTCisVT<1, f64>,
                              SDTCisPtrTy<2>]>,
                              []>;

// These are target-independent nodes, but have target-specific formats.
def callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_PPCCallSeqStart,
                           [SDNPHasChain, SDNPOutGlue]>;
def callseq_end   : SDNode<"ISD::CALLSEQ_END",   SDT_PPCCallSeqEnd,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def SDT_PPCCall   : SDTypeProfile<0, -1, [SDTCisInt<0>]>;
def PPCcall  : SDNode<"PPCISD::CALL", SDT_PPCCall,
                      [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                       SDNPVariadic]>;
def PPCcall_nop  : SDNode<"PPCISD::CALL_NOP", SDT_PPCCall,
                          [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                           SDNPVariadic]>;
def PPCcall_notoc : SDNode<"PPCISD::CALL_NOTOC", SDT_PPCCall,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                            SDNPVariadic]>;
def PPCmtctr      : SDNode<"PPCISD::MTCTR", SDT_PPCCall,
                           [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def PPCbctrl : SDNode<"PPCISD::BCTRL", SDTNone,
                      [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                       SDNPVariadic]>;
def PPCbctrl_load_toc : SDNode<"PPCISD::BCTRL_LOAD_TOC",
                               SDTypeProfile<0, 1, []>,
                               [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                SDNPVariadic]>;

// Call nodes for strictfp calls (that define RM).
def PPCcall_rm  : SDNode<"PPCISD::CALL_RM", SDT_PPCCall,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                          SDNPVariadic]>;
def PPCcall_nop_rm  : SDNode<"PPCISD::CALL_NOP_RM", SDT_PPCCall,
                             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                              SDNPVariadic]>;
def PPCcall_notoc_rm : SDNode<"PPCISD::CALL_NOTOC_RM", SDT_PPCCall,
                              [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                               SDNPVariadic]>;
def PPCbctrl_rm : SDNode<"PPCISD::BCTRL_RM", SDTNone,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                          SDNPVariadic]>;
def PPCbctrl_load_toc_rm : SDNode<"PPCISD::BCTRL_LOAD_TOC_RM",
                                  SDTypeProfile<0, 1, []>,
                                  [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                                   SDNPVariadic]>;

def PPCretglue   : SDNode<"PPCISD::RET_GLUE", SDTNone,
                           [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

def PPCtc_return : SDNode<"PPCISD::TC_RETURN", SDT_PPCTC_ret,
                        [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;

def PPCeh_sjlj_setjmp  : SDNode<"PPCISD::EH_SJLJ_SETJMP",
                                SDTypeProfile<1, 1, [SDTCisInt<0>,
                                                     SDTCisPtrTy<1>]>,
                                [SDNPHasChain, SDNPSideEffect]>;
def PPCeh_sjlj_longjmp : SDNode<"PPCISD::EH_SJLJ_LONGJMP",
                                SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>,
                                [SDNPHasChain, SDNPSideEffect]>;

def SDT_PPCsc     : SDTypeProfile<0, 1, [SDTCisInt<0>]>;
def PPCsc         : SDNode<"PPCISD::SC", SDT_PPCsc,
                           [SDNPHasChain, SDNPSideEffect]>;

def PPCclrbhrb    : SDNode<"PPCISD::CLRBHRB", SDTNone,
                           [SDNPHasChain, SDNPSideEffect]>;
def PPCmfbhrbe    : SDNode<"PPCISD::MFBHRBE", SDTIntBinOp, [SDNPHasChain]>;
def PPCrfebb      : SDNode<"PPCISD::RFEBB", SDT_PPCsc,
                           [SDNPHasChain, SDNPSideEffect]>;

def PPCvcmp       : SDNode<"PPCISD::VCMP" , SDT_PPCvcmp, []>;
def PPCvcmp_rec   : SDNode<"PPCISD::VCMP_rec", SDT_PPCvcmp, [SDNPOutGlue]>;

def PPCcondbranch : SDNode<"PPCISD::COND_BRANCH", SDT_PPCcondbr,
                           [SDNPHasChain, SDNPOptInGlue]>;

// PPC-specific atomic operations.
def PPCatomicCmpSwap_8 :
  SDNode<"PPCISD::ATOMIC_CMP_SWAP_8", SDTAtomic3,
         [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def PPCatomicCmpSwap_16 :
  SDNode<"PPCISD::ATOMIC_CMP_SWAP_16", SDTAtomic3,
         [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPMemOperand]>;
def PPClbrx       : SDNode<"PPCISD::LBRX", SDT_PPClbrx,
                           [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def PPCstbrx      : SDNode<"PPCISD::STBRX", SDT_PPCstbrx,
                           [SDNPHasChain, SDNPMayStore]>;
def PPCStoreCond  : SDNode<"PPCISD::STORE_COND", SDT_StoreCond,
                           [SDNPHasChain, SDNPMayStore,
                            SDNPMemOperand, SDNPOutGlue]>;

// Instructions to set/unset CR bit 6 for SVR4 vararg calls
def PPCcr6set   : SDNode<"PPCISD::CR6SET", SDTNone,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def PPCcr6unset : SDNode<"PPCISD::CR6UNSET", SDTNone,
                         [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

// Instructions to support dynamic alloca.
def SDTDynOp  : SDTypeProfile<1, 2, []>;
def SDTDynAreaOp  : SDTypeProfile<1, 1, []>;
def PPCdynalloc   : SDNode<"PPCISD::DYNALLOC", SDTDynOp, [SDNPHasChain]>;
def PPCdynareaoffset   : SDNode<"PPCISD::DYNAREAOFFSET", SDTDynAreaOp, [SDNPHasChain]>;
def PPCprobedalloca : SDNode<"PPCISD::PROBED_ALLOCA", SDTDynOp, [SDNPHasChain]>;

// PC Relative Specific Nodes
def PPCmatpcreladdr : SDNode<"PPCISD::MAT_PCREL_ADDR", SDTIntUnaryOp, []>;
def PPCtlsdynamatpcreladdr : SDNode<"PPCISD::TLS_DYNAMIC_MAT_PCREL_ADDR",
                                    SDTIntUnaryOp, []>;
def PPCtlslocalexecmataddr : SDNode<"PPCISD::TLS_LOCAL_EXEC_MAT_ADDR",
                                    SDTIntUnaryOp, []>;

//===----------------------------------------------------------------------===//
// PowerPC specific transformation functions and pattern fragments.
//

// A floating point immediate that is not a positive zero and can be converted
// to a single precision floating point non-denormal immediate without loss of
// information.
def nzFPImmAsi32 : PatLeaf<(fpimm), [{
  APFloat APFloatOfN = N->getValueAPF();
  return convertToNonDenormSingle(APFloatOfN) && !N->isExactlyValue(+0.0);
}]>;

// A floating point immediate that is exactly an integer (for example 3.0, -5.0)
// and can be represented in 5 bits (range of [-16, 15]).
def nzFPImmExactInti5 : PatLeaf<(fpimm), [{
  APFloat FloatValue = N->getValueAPF();
  bool IsExact;
  APSInt IntResult(16, false);
  FloatValue.convertToInteger(IntResult, APFloat::rmTowardZero, &IsExact);
  return IsExact && IntResult <= 15 && IntResult >= -16 && !FloatValue.isZero();
}]>;

def getFPAs5BitExactInt : SDNodeXForm<fpimm, [{
  APFloat FloatValue = N->getValueAPF();
  bool IsExact;
  APSInt IntResult(32, false);
  FloatValue.convertToInteger(IntResult, APFloat::rmTowardZero, &IsExact);
  return CurDAG->getTargetConstant(IntResult, SDLoc(N), MVT::i32);
}]>;

// Convert the floating point immediate into a 32 bit floating point immediate
// and get a i32 with the resulting bits.
def getFPAs32BitInt : SDNodeXForm<fpimm, [{
  APFloat APFloatOfN = N->getValueAPF();
  convertToNonDenormSingle(APFloatOfN);
  return CurDAG->getTargetConstant(APFloatOfN.bitcastToAPInt().getZExtValue(),
                                   SDLoc(N), MVT::i32);
}]>;

// Check if the value can be converted to be single precision immediate, which
// can be exploited by XXSPLTIDP. Ensure that it cannot be converted to single
// precision before exploiting with XXSPLTI32DX.
def nzFPImmAsi64 : PatLeaf<(fpimm), [{
  APFloat APFloatOfN = N->getValueAPF();
  return !N->isExactlyValue(+0.0) && !checkConvertToNonDenormSingle(APFloatOfN);
}]>;

// Get the Hi bits of a 64 bit immediate.
def getFPAs64BitIntHi : SDNodeXForm<fpimm, [{
  APFloat APFloatOfN = N->getValueAPF();
  bool Unused;
  APFloatOfN.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven,
                     &Unused);
  uint32_t Hi = (uint32_t)((APFloatOfN.bitcastToAPInt().getZExtValue() &
                            0xFFFFFFFF00000000LL) >> 32);
  return CurDAG->getTargetConstant(Hi, SDLoc(N), MVT::i32);
}]>;

// Get the Lo bits of a 64 bit immediate.
def getFPAs64BitIntLo : SDNodeXForm<fpimm, [{
  APFloat APFloatOfN = N->getValueAPF();
  bool Unused;
  APFloatOfN.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven,
                     &Unused);
  uint32_t Lo = (uint32_t)(APFloatOfN.bitcastToAPInt().getZExtValue() &
                           0xFFFFFFFF);
  return CurDAG->getTargetConstant(Lo, SDLoc(N), MVT::i32);
}]>;

def imm34 : PatLeaf<(imm), [{
  return isInt<34>(N->getSExtValue());
}]>;

def getImmAs64BitInt : SDNodeXForm<imm, [{
  return getI64Imm(N->getSExtValue(), SDLoc(N));
}]>;

def SHL32 : SDNodeXForm<imm, [{
  // Transformation function: 31 - imm
  return getI32Imm(31 - N->getZExtValue(), SDLoc(N));
}]>;

def SRL32 : SDNodeXForm<imm, [{
  // Transformation function: 32 - imm
  return N->getZExtValue() ? getI32Imm(32 - N->getZExtValue(), SDLoc(N))
                           : getI32Imm(0, SDLoc(N));
}]>;

def LO16 : SDNodeXForm<imm, [{
  // Transformation function: get the low 16 bits.
  return getI32Imm((unsigned short)N->getZExtValue(), SDLoc(N));
}]>;

def HI16 : SDNodeXForm<imm, [{
  // Transformation function: shift the immediate value down into the low bits.
  return getI32Imm((unsigned)N->getZExtValue() >> 16, SDLoc(N));
}]>;

def HA16 : SDNodeXForm<imm, [{
  // Transformation function: shift the immediate value down into the low bits.
  int64_t Val = N->getZExtValue();
  return getI32Imm((Val - (signed short)Val) >> 16, SDLoc(N));
}]>;
def MB : SDNodeXForm<imm, [{
  // Transformation function: get the start bit of a mask
  unsigned mb = 0, me;
  (void)isRunOfOnes((unsigned)N->getZExtValue(), mb, me);
  return getI32Imm(mb, SDLoc(N));
}]>;

def ME : SDNodeXForm<imm, [{
  // Transformation function: get the end bit of a mask
  unsigned mb, me = 0;
  (void)isRunOfOnes((unsigned)N->getZExtValue(), mb, me);
  return getI32Imm(me, SDLoc(N));
}]>;
def maskimm32 : PatLeaf<(imm), [{
  // maskImm predicate - True if immediate is a run of ones.
  unsigned mb, me;
  if (N->getValueType(0) == MVT::i32)
    return isRunOfOnes((unsigned)N->getZExtValue(), mb, me);
  else
    return false;
}]>;

def imm32SExt16  : Operand<i32>, ImmLeaf<i32, [{
  // imm32SExt16 predicate - True if the i32 immediate fits in a 16-bit
  // sign extended field.  Used by instructions like 'addi'.
  return (int32_t)Imm == (short)Imm;
}]>;
def imm64SExt16  : Operand<i64>, ImmLeaf<i64, [{
  // imm64SExt16 predicate - True if the i64 immediate fits in a 16-bit
  // sign extended field.  Used by instructions like 'addi'.
  return (int64_t)Imm == (short)Imm;
}]>;
def immZExt16  : PatLeaf<(imm), [{
  // immZExt16 predicate - True if the immediate fits in a 16-bit zero extended
  // field.  Used by instructions like 'ori'.
  return (uint64_t)N->getZExtValue() == (unsigned short)N->getZExtValue();
}], LO16>;
def immNonAllOneAnyExt8 : ImmLeaf<i32, [{
  return (isInt<8>(Imm) && (Imm != -1)) || (isUInt<8>(Imm) && (Imm != 0xFF));
}]>;
def i32immNonAllOneNonZero : ImmLeaf<i32, [{ return Imm && (Imm != -1); }]>;
def immSExt5NonZero : ImmLeaf<i32, [{ return Imm && isInt<5>(Imm); }]>;

// imm16Shifted* - These match immediates where the low 16-bits are zero.  There
// are two forms: imm16ShiftedSExt and imm16ShiftedZExt.  These two forms are
// identical in 32-bit mode, but in 64-bit mode, they return true if the
// immediate fits into a sign/zero extended 32-bit immediate (with the low bits
// clear).
def imm16ShiftedZExt : PatLeaf<(imm), [{
  // imm16ShiftedZExt predicate - True if only bits in the top 16-bits of the
  // immediate are set.  Used by instructions like 'xoris'.
  return (N->getZExtValue() & ~uint64_t(0xFFFF0000)) == 0;
}], HI16>;

def imm16ShiftedSExt : PatLeaf<(imm), [{
  // imm16ShiftedSExt predicate - True if only bits in the top 16-bits of the
  // immediate are set.  Used by instructions like 'addis'.  Identical to
  // imm16ShiftedZExt in 32-bit mode.
  if (N->getZExtValue() & 0xFFFF) return false;
  if (N->getValueType(0) == MVT::i32)
    return true;
  // For 64-bit, make sure it is sext right.
  return N->getZExtValue() == (uint64_t)(int)N->getZExtValue();
}], HI16>;

def imm64ZExt32  : Operand<i64>, ImmLeaf<i64, [{
  // imm64ZExt32 predicate - True if the i64 immediate fits in a 32-bit
  // zero extended field.
  return isUInt<32>(Imm);
}]>;

// This is a somewhat weaker condition than actually checking for 4-byte
// alignment. It is simply checking that the displacement can be represented
// as an immediate that is a multiple of 4 (i.e. the requirements for DS-Form
// instructions).
// But some r+i load/store instructions (such as LD, STD, LDU, etc.) that require
// restricted memrix (4-aligned) constants are alignment sensitive. If these
// offsets are hidden behind TOC entries than the values of the lower-order
// bits cannot be checked directly. As a result, we need to also incorporate
// an alignment check into the relevant patterns.

def DSFormLoad : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  return isOffsetMultipleOf(N, 4) || cast<LoadSDNode>(N)->getAlign() >= 4;
}]>;
def DSFormStore : PatFrag<(ops node:$val, node:$ptr),
                            (store node:$val, node:$ptr), [{
  return isOffsetMultipleOf(N, 4) || cast<StoreSDNode>(N)->getAlign() >= 4;
}]>;
def DSFormSextLoadi32 : PatFrag<(ops node:$ptr), (sextloadi32 node:$ptr), [{
  return isOffsetMultipleOf(N, 4) || cast<LoadSDNode>(N)->getAlign() >= 4;
}]>;
def DSFormPreStore : PatFrag<
                          (ops node:$val, node:$base, node:$offset),
                          (pre_store node:$val, node:$base, node:$offset), [{
  return isOffsetMultipleOf(N, 4) || cast<StoreSDNode>(N)->getAlign() >= 4;
}]>;

def NonDSFormLoad : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  return cast<LoadSDNode>(N)->getAlign() < 4 && !isOffsetMultipleOf(N, 4);
}]>;
def NonDSFormStore : PatFrag<(ops node:$val, node:$ptr),
                              (store node:$val, node:$ptr), [{
  return cast<StoreSDNode>(N)->getAlign() < 4 && !isOffsetMultipleOf(N, 4);
}]>;
def NonDSFormSextLoadi32 : PatFrag<(ops node:$ptr), (sextloadi32 node:$ptr), [{
  return cast<LoadSDNode>(N)->getAlign() < 4 && !isOffsetMultipleOf(N, 4);
}]>;

// This is a somewhat weaker condition than actually checking for 16-byte
// alignment. It is simply checking that the displacement can be represented
// as an immediate that is a multiple of 16 (i.e. the requirements for DQ-Form
// instructions).
def quadwOffsetLoad : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  return isOffsetMultipleOf(N, 16);
}]>;
def quadwOffsetStore : PatFrag<(ops node:$val, node:$ptr),
                               (store node:$val, node:$ptr), [{
  return isOffsetMultipleOf(N, 16);
}]>;
def nonQuadwOffsetLoad : PatFrag<(ops node:$ptr), (load node:$ptr), [{
  return !isOffsetMultipleOf(N, 16);
}]>;
def nonQuadwOffsetStore : PatFrag<(ops node:$val, node:$ptr),
                                  (store node:$val, node:$ptr), [{
  return !isOffsetMultipleOf(N, 16);
}]>;

// PatFrag for binary operation whose operands are both non-constant
class BinOpWithoutSImm16Operand<SDNode opcode> :
  PatFrag<(ops node:$left, node:$right), (opcode node:$left, node:$right), [{
    int16_t Imm;
    return !isIntS16Immediate(N->getOperand(0), Imm)
             && !isIntS16Immediate(N->getOperand(1), Imm);
}]>;

def add_without_simm16 : BinOpWithoutSImm16Operand<add>;
def mul_without_simm16 : BinOpWithoutSImm16Operand<mul>;

//===----------------------------------------------------------------------===//
// PowerPC Flag Definitions.

class isPPC64 { bit PPC64 = 1; }
class isRecordForm   { bit RC = 1; }

class RegConstraint<string C> {
  string Constraints = C;
}
class NoEncode<string E> {
  string DisableEncoding = E;
}


// Define PowerPC specific addressing mode.

// d-form
def iaddr    : ComplexPattern<iPTR, 2, "SelectAddrImm",     [], []>; // "stb"
// ds-form
def iaddrX4  : ComplexPattern<iPTR, 2, "SelectAddrImmX4",   [], []>; // "std"
// dq-form
def iaddrX16 : ComplexPattern<iPTR, 2, "SelectAddrImmX16",  [], []>; // "stxv"
// 8LS:d-form
def iaddrX34 : ComplexPattern<iPTR, 2, "SelectAddrImmX34",  [], []>; // "pstxvp"

// Below forms are all x-form addressing mode, use three different ones so we
// can make a accurate check for x-form instructions in ISEL.
// x-form addressing mode whose associated displacement form is D.
def xaddr  : ComplexPattern<iPTR, 2, "SelectAddrIdx",     [], []>;    // "stbx"
// x-form addressing mode whose associated displacement form is DS.
def xaddrX4 : ComplexPattern<iPTR, 2, "SelectAddrIdxX4",    [], []>;  // "stdx"
// x-form addressing mode whose associated displacement form is DQ.
def xaddrX16 : ComplexPattern<iPTR, 2, "SelectAddrIdxX16",   [], []>; // "stxvx"

def xoaddr : ComplexPattern<iPTR, 2, "SelectAddrIdxOnly",[], []>;

// The address in a single register. This is used with the SjLj
// pseudo-instructions.
def addr   : ComplexPattern<iPTR, 1, "SelectAddr",[], []>;

/// This is just the offset part of iaddr, used for preinc.
def iaddroff : ComplexPattern<iPTR, 1, "SelectAddrImmOffs", [], []>;

// Load and Store Instruction Selection addressing modes.
def DForm  : ComplexPattern<iPTR, 2, "SelectDForm",    [], [SDNPWantParent]>;
def DSForm : ComplexPattern<iPTR, 2, "SelectDSForm",   [], [SDNPWantParent]>;
def DQForm : ComplexPattern<iPTR, 2, "SelectDQForm",   [], [SDNPWantParent]>;
def XForm  : ComplexPattern<iPTR, 2, "SelectXForm",    [], [SDNPWantParent]>;
def ForceXForm : ComplexPattern<iPTR, 2, "SelectForceXForm", [], [SDNPWantParent]>;
def PCRelForm : ComplexPattern<iPTR, 2, "SelectPCRelForm", [], [SDNPWantParent]>;
def PDForm : ComplexPattern<iPTR, 2, "SelectPDForm",   [], [SDNPWantParent]>;

//===----------------------------------------------------------------------===//
// PowerPC Instruction Predicate Definitions.
def In32BitMode  : Predicate<"!Subtarget->isPPC64()">;
def In64BitMode  : Predicate<"Subtarget->isPPC64()">;
def IsBookE  : Predicate<"Subtarget->isBookE()">;
def IsNotBookE  : Predicate<"!Subtarget->isBookE()">;
def HasOnlyMSYNC : Predicate<"Subtarget->hasOnlyMSYNC()">;
def HasSYNC   : Predicate<"!Subtarget->hasOnlyMSYNC()">;
def IsPPC4xx  : Predicate<"Subtarget->isPPC4xx()">;
def IsPPC6xx  : Predicate<"Subtarget->isPPC6xx()">;
def IsE500  : Predicate<"Subtarget->isE500()">;
def HasSPE  : Predicate<"Subtarget->hasSPE()">;
def HasICBT : Predicate<"Subtarget->hasICBT()">;
def HasPartwordAtomics : Predicate<"Subtarget->hasPartwordAtomics()">;
def HasQuadwordAtomics : Predicate<"Subtarget->hasQuadwordAtomics()">;
def NoNaNsFPMath
    : Predicate<"Subtarget->getTargetMachine().Options.NoNaNsFPMath">;
def NaNsFPMath
    : Predicate<"!Subtarget->getTargetMachine().Options.NoNaNsFPMath">;
def HasBPERMD : Predicate<"Subtarget->hasBPERMD()">;
def HasExtDiv : Predicate<"Subtarget->hasExtDiv()">;
def IsISA2_06 : Predicate<"Subtarget->isISA2_06()">;
def IsISA2_07 : Predicate<"Subtarget->isISA2_07()">;
def IsISA3_0 : Predicate<"Subtarget->isISA3_0()">;
def HasFPU : Predicate<"Subtarget->hasFPU()">;
def PCRelativeMemops : Predicate<"Subtarget->hasPCRelativeMemops()">;
def IsNotISA3_1 : Predicate<"!Subtarget->isISA3_1()">;

// AIX assembler may not be modern enough to support some extended mne.
def ModernAs: Predicate<"!Subtarget->isAIXABI() || Subtarget->HasModernAIXAs">,
                 AssemblerPredicate<(any_of (not AIXOS), FeatureModernAIXAs)>;
def IsAIX : Predicate<"Subtarget->isAIXABI()">;
def NotAIX : Predicate<"!Subtarget->isAIXABI()">;
def IsISAFuture : Predicate<"Subtarget->isISAFuture()">;
def IsNotISAFuture : Predicate<"!Subtarget->isISAFuture()">;

//===----------------------------------------------------------------------===//
// PowerPC Multiclass Definitions.
multiclass XForm_base_r3xo_r<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                      string asmbase, string asmstr, list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XForm_base_r3xo<opcode, xo, OOL, IOL,
                                !strconcat(asmbase, !strconcat(" ", asmstr)),
                                NoItinerary, pattern>, RecFormRel;
    let Defs = [CR1] in
    def _rec : XForm_base_r3xo<opcode, xo, OOL, IOL,
                               !strconcat(asmbase, !strconcat(". ", asmstr)),
                               NoItinerary, []>, isRecordForm, RecFormRel;
  }
}

multiclass XForm_6r<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XForm_6<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : XForm_6<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass XForm_6rc<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                     string asmbase, string asmstr, InstrItinClass itin,
                     list<dag> pattern> {
  let BaseName = asmbase in {
    let Defs = [CARRY] in
    def NAME : XForm_6<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CARRY, CR0] in
    def _rec    : XForm_6<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass XForm_10rc<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                      string asmbase, string asmstr, InstrItinClass itin,
                      list<dag> pattern> {
  let BaseName = asmbase in {
    let Defs = [CARRY] in
    def NAME : XForm_10<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CARRY, CR0] in
    def _rec    : XForm_10<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass XForm_11r<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XForm_11<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : XForm_11<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass XOForm_1r<bits<6> opcode, bits<9> xo, bit oe, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XOForm_1<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : XOForm_1<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

// Multiclass for instructions which have a record overflow form as well
// as a record form but no carry (i.e. mulld, mulldo, subf, subfo, etc.)
multiclass XOForm_1rx<bits<6> opcode, bits<9> xo, dag OOL, dag IOL,
                      string asmbase, string asmstr, InstrItinClass itin,
                      list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XOForm_1<opcode, xo, 0, OOL, IOL,
                        !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                        pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : XOForm_1<opcode, xo, 0, OOL, IOL,
                        !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                        []>, isRecordForm, RecFormRel;
  }
  let BaseName = !strconcat(asmbase, "O") in {
    let Defs = [XER] in
    def O    : XOForm_1<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o ", asmstr)), itin,
                        []>, RecFormRel;
    let Defs = [XER, CR0] in
    def O_rec    : XOForm_1<opcode, xo, 1, OOL, IOL,
                         !strconcat(asmbase, !strconcat("o. ", asmstr)), itin,
                         []>, isRecordForm, RecFormRel;
  }
}

// Multiclass for instructions for which the non record form is not cracked
// and the record form is cracked (i.e. divw, mullw, etc.)
multiclass XOForm_1rcr<bits<6> opcode, bits<9> xo, bit oe, dag OOL, dag IOL,
                      string asmbase, string asmstr, InstrItinClass itin,
                      list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XOForm_1<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : XOForm_1<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel, PPC970_DGroup_First,
                       PPC970_DGroup_Cracked;
  }
  let BaseName = !strconcat(asmbase, "O") in {
    let Defs = [XER] in
    def O    : XOForm_1<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o ", asmstr)), itin,
                        []>, RecFormRel;
    let Defs = [XER, CR0] in
    def O_rec   : XOForm_1<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o. ", asmstr)), itin,
                        []>, isRecordForm, RecFormRel;
  }
}

multiclass XOForm_1rc<bits<6> opcode, bits<9> xo, bit oe, dag OOL, dag IOL,
                      string asmbase, string asmstr, InstrItinClass itin,
                      list<dag> pattern> {
  let BaseName = asmbase in {
    let Defs = [CARRY] in
    def NAME : XOForm_1<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CARRY, CR0] in
    def _rec    : XOForm_1<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
  let BaseName = !strconcat(asmbase, "O") in {
    let Defs = [CARRY, XER] in
    def O    : XOForm_1<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o ", asmstr)), itin,
                        []>, RecFormRel;
    let Defs = [CARRY, XER, CR0] in
    def O_rec   : XOForm_1<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o. ", asmstr)), itin,
                        []>, isRecordForm, RecFormRel;
  }
}

multiclass XOForm_3r<bits<6> opcode, bits<9> xo, bit oe, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XOForm_3<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : XOForm_3<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
  let BaseName = !strconcat(asmbase, "O") in {
    let Defs = [XER] in
    def O    : XOForm_3<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o ", asmstr)), itin,
                        []>, RecFormRel;
    let Defs = [XER, CR0] in
    def O_rec   : XOForm_3<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o. ", asmstr)), itin,
                        []>, isRecordForm, RecFormRel;
  }
}

multiclass XOForm_3rc<bits<6> opcode, bits<9> xo, bit oe, dag OOL, dag IOL,
                      string asmbase, string asmstr, InstrItinClass itin,
                      list<dag> pattern> {
  let BaseName = asmbase in {
    let Defs = [CARRY] in
    def NAME : XOForm_3<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CARRY, CR0] in
    def _rec    : XOForm_3<opcode, xo, oe, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
  let BaseName = !strconcat(asmbase, "O") in {
    let Defs = [CARRY, XER] in
    def O    : XOForm_3<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o ", asmstr)), itin,
                        []>, RecFormRel;
    let Defs = [CARRY, XER, CR0] in
    def O_rec   : XOForm_3<opcode, xo, 1, OOL, IOL,
                        !strconcat(asmbase, !strconcat("o. ", asmstr)), itin,
                        []>, isRecordForm, RecFormRel;
  }
}

multiclass MForm_1r<bits<6> opcode, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : MForm_1<opcode, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : MForm_1<opcode, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass MForm_2r<bits<6> opcode, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : MForm_2<opcode, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : MForm_2<opcode, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass MDForm_1r<bits<6> opcode, bits<3> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : MDForm_1<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : MDForm_1<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass MDSForm_1r<bits<6> opcode, bits<4> xo, dag OOL, dag IOL,
                     string asmbase, string asmstr, InstrItinClass itin,
                     list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : MDSForm_1<opcode, xo, OOL, IOL,
                        !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                        pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : MDSForm_1<opcode, xo, OOL, IOL,
                        !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                        []>, isRecordForm, RecFormRel;
  }
}

multiclass XSForm_1rc<bits<6> opcode, bits<9> xo, dag OOL, dag IOL,
                      string asmbase, string asmstr, InstrItinClass itin,
                      list<dag> pattern> {
  let BaseName = asmbase in {
    let Defs = [CARRY] in
    def NAME : XSForm_1<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CARRY, CR0] in
    def _rec    : XSForm_1<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass XSForm_1r<bits<6> opcode, bits<9> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XSForm_1<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec    : XSForm_1<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass XForm_26r<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XForm_26<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR1] in
    def _rec    : XForm_26<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass XForm_28r<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XForm_28<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR1] in
    def _rec    : XForm_28<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass XForm_SP2_FRTB5r<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                        string asmbase, string asmstr, list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XForm_SP2_FRTB5<opcode, xo, OOL, IOL,
                               !strconcat(asmbase, !strconcat(" ", asmstr)),
                               pattern, NoItinerary>, RecFormRel;
    let Defs = [CR1] in
    def _rec : XForm_SP2_FRTB5<opcode, xo, OOL, IOL,
                               !strconcat(asmbase, !strconcat(". ", asmstr)),
                               [], NoItinerary>, isRecordForm, RecFormRel;
  }
}

multiclass XForm_S1_FRTB5r<bits<6> opcode, bits<10> xo, dag OOL, dag IOL,
                      string asmbase, string asmstr, list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : XForm_S1_FRTB5<opcode, xo, OOL, IOL,
                              !strconcat(asmbase, !strconcat(" ", asmstr)),
                              pattern, NoItinerary>, RecFormRel;
    let Defs = [CR1] in
    def _rec : XForm_S1_FRTB5<opcode, xo, OOL, IOL,
                              !strconcat(asmbase, !strconcat(". ", asmstr)), [],
                              NoItinerary>, isRecordForm, RecFormRel;
  }
}

multiclass AForm_1r<bits<6> opcode, bits<5> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : AForm_1<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR1] in
    def _rec    : AForm_1<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass AForm_2r<bits<6> opcode, bits<5> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : AForm_2<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR1] in
    def _rec    : AForm_2<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass AForm_3r<bits<6> opcode, bits<5> xo, dag OOL, dag IOL,
                    string asmbase, string asmstr, InstrItinClass itin,
                    list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : AForm_3<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(" ", asmstr)), itin,
                       pattern>, RecFormRel;
    let Defs = [CR1] in
    def _rec    : AForm_3<opcode, xo, OOL, IOL,
                       !strconcat(asmbase, !strconcat(". ", asmstr)), itin,
                       []>, isRecordForm, RecFormRel;
  }
}

multiclass
    Z23Form_TE5_FRTB5_RMC2r<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                            string asmbase, string asmstr, list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME
        : Z23Form_TE5_FRTB5_RMC2<opcode, xo, OOL, IOL,
                                 !strconcat(asmbase, !strconcat(" ", asmstr)),
                                 pattern>, RecFormRel;
    let Defs = [CR0] in
    def _rec : Z23Form_TE5_FRTB5_RMC2<opcode, xo, OOL, IOL,
                                 !strconcat(asmbase, !strconcat(". ", asmstr)),
                                 []>, isRecordForm, RecFormRel;
  }
}

multiclass
    Z23Form_FRTAB5_RMC2r<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                         string asmbase, string asmstr, list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : Z23Form_FRTAB5_RMC2<opcode, xo, OOL, IOL,
                                   !strconcat(asmbase, !strconcat(" ", asmstr)),
                                   pattern>, RecFormRel;
    let Defs = [CR1] in
    def _rec : Z23Form_FRTAB5_RMC2<opcode, xo, OOL, IOL,
                              !strconcat(asmbase, !strconcat(". ", asmstr)),
                              []>, isRecordForm, RecFormRel;
  }
}

multiclass
    Z23Form_FRTB5_R1_RMC2r<bits<6> opcode, bits<8> xo, dag OOL, dag IOL,
                           string asmbase, string asmstr, list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : Z23Form_FRTB5_R1_RMC2<opcode, xo, OOL, IOL,
                               !strconcat(asmbase, !strconcat(" ", asmstr)),
                               pattern>, RecFormRel;
    let Defs = [CR1] in
    def _rec : Z23Form_FRTB5_R1_RMC2<opcode, xo, OOL, IOL,
                               !strconcat(asmbase, !strconcat(". ", asmstr)),
                               []>, isRecordForm, RecFormRel;
  }
}

multiclass Z22Form_FRTA5_SH6r<bits<6> opcode, bits<9> xo, dag OOL, dag IOL,
                      string asmbase, string asmstr, list<dag> pattern> {
  let BaseName = asmbase in {
    def NAME : Z22Form_FRTA5_SH6<opcode, xo, OOL, IOL,
                                 !strconcat(asmbase, !strconcat(" ", asmstr)),
                                 pattern, NoItinerary>, RecFormRel;
    let Defs = [CR1] in
    def _rec : Z22Form_FRTA5_SH6<opcode, xo, OOL, IOL,
                                 !strconcat(asmbase, !strconcat(". ", asmstr)),
                                 [], NoItinerary>, isRecordForm, RecFormRel;
  }
}

//===----------------------------------------------------------------------===//
// END OF MULTICLASS DEFINITIONS
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// PowerPC Instruction Definitions.

// Pseudo instructions:

let hasCtrlDep = 1 in {
let Defs = [R1], Uses = [R1] in {
def ADJCALLSTACKDOWN : PPCEmitTimePseudo<(outs), (ins u16imm:$amt1, u16imm:$amt2),
                              "#ADJCALLSTACKDOWN $amt1 $amt2",
                              [(callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : PPCEmitTimePseudo<(outs), (ins u16imm:$amt1, u16imm:$amt2),
                              "#ADJCALLSTACKUP $amt1 $amt2",
                              [(callseq_end timm:$amt1, timm:$amt2)]>;
}
} // hasCtrlDep

let Defs = [R1], Uses = [R1] in
def DYNALLOC : PPCEmitTimePseudo<(outs gprc:$result), (ins gprc:$negsize, memri:$fpsi), "#DYNALLOC",
                       [(set i32:$result,
                             (PPCdynalloc i32:$negsize, iaddr:$fpsi))]>;
def DYNAREAOFFSET : PPCEmitTimePseudo<(outs i32imm:$result), (ins memri:$fpsi), "#DYNAREAOFFSET",
                       [(set i32:$result, (PPCdynareaoffset iaddr:$fpsi))]>;
// Probed alloca to support stack clash protection.
let Defs = [R1], Uses = [R1], hasNoSchedulingInfo = 1 in {
def PROBED_ALLOCA_32 : PPCCustomInserterPseudo<(outs gprc:$result),
                         (ins gprc:$negsize, memri:$fpsi), "#PROBED_ALLOCA_32",
                           [(set i32:$result,
                             (PPCprobedalloca i32:$negsize, iaddr:$fpsi))]>;
def PREPARE_PROBED_ALLOCA_32 : PPCEmitTimePseudo<(outs
    gprc:$fp, gprc:$actual_negsize),
    (ins gprc:$negsize, memri:$fpsi), "#PREPARE_PROBED_ALLOCA_32", []>;
def PREPARE_PROBED_ALLOCA_NEGSIZE_SAME_REG_32 : PPCEmitTimePseudo<(outs
    gprc:$fp, gprc:$actual_negsize),
    (ins gprc:$negsize, memri:$fpsi),
    "#PREPARE_PROBED_ALLOCA_NEGSIZE_SAME_REG_32", []>,
    RegConstraint<"$actual_negsize = $negsize">;
def PROBED_STACKALLOC_32 : PPCEmitTimePseudo<(outs gprc:$scratch, gprc:$temp),
    (ins i64imm:$stacksize),
    "#PROBED_STACKALLOC_32", []>;
}

// SELECT_CC_* - Used to implement the SELECT_CC DAG operation.  Expanded after
// instruction selection into a branch sequence.
let PPC970_Single = 1 in {
  // Note that SELECT_CC_I4 and SELECT_CC_I8 use the no-r0 register classes
  // because either operand might become the first operand in an isel, and
  // that operand cannot be r0.
  def SELECT_CC_I4 : PPCCustomInserterPseudo<(outs gprc:$dst), (ins crrc:$cond,
                              gprc_nor0:$T, gprc_nor0:$F,
                              i32imm:$BROPC), "#SELECT_CC_I4",
                              []>;
  def SELECT_CC_I8 : PPCCustomInserterPseudo<(outs g8rc:$dst), (ins crrc:$cond,
                              g8rc_nox0:$T, g8rc_nox0:$F,
                              i32imm:$BROPC), "#SELECT_CC_I8",
                              []>;
  def SELECT_CC_F4  : PPCCustomInserterPseudo<(outs f4rc:$dst), (ins crrc:$cond, f4rc:$T, f4rc:$F,
                              i32imm:$BROPC), "#SELECT_CC_F4",
                              []>;
  def SELECT_CC_F8  : PPCCustomInserterPseudo<(outs f8rc:$dst), (ins crrc:$cond, f8rc:$T, f8rc:$F,
                              i32imm:$BROPC), "#SELECT_CC_F8",
                              []>;
  def SELECT_CC_F16  : PPCCustomInserterPseudo<(outs vrrc:$dst), (ins crrc:$cond, vrrc:$T, vrrc:$F,
                              i32imm:$BROPC), "#SELECT_CC_F16",
                              []>;
  def SELECT_CC_VRRC: PPCCustomInserterPseudo<(outs vrrc:$dst), (ins crrc:$cond, vrrc:$T, vrrc:$F,
                              i32imm:$BROPC), "#SELECT_CC_VRRC",
                              []>;

  // SELECT_* pseudo instructions, like SELECT_CC_* but taking condition
  // register bit directly.
  def SELECT_I4 : PPCCustomInserterPseudo<(outs gprc:$dst), (ins crbitrc:$cond,
                          gprc_nor0:$T, gprc_nor0:$F), "#SELECT_I4",
                          [(set i32:$dst, (select i1:$cond, i32:$T, i32:$F))]>;
  def SELECT_I8 : PPCCustomInserterPseudo<(outs g8rc:$dst), (ins crbitrc:$cond,
                          g8rc_nox0:$T, g8rc_nox0:$F), "#SELECT_I8",
                          [(set i64:$dst, (select i1:$cond, i64:$T, i64:$F))]>;
let Predicates = [HasFPU] in {
  def SELECT_F4  : PPCCustomInserterPseudo<(outs f4rc:$dst), (ins crbitrc:$cond,
                          f4rc:$T, f4rc:$F), "#SELECT_F4",
                          [(set f32:$dst, (select i1:$cond, f32:$T, f32:$F))]>;
  def SELECT_F8  : PPCCustomInserterPseudo<(outs f8rc:$dst), (ins crbitrc:$cond,
                          f8rc:$T, f8rc:$F), "#SELECT_F8",
                          [(set f64:$dst, (select i1:$cond, f64:$T, f64:$F))]>;
  def SELECT_F16  : PPCCustomInserterPseudo<(outs vrrc:$dst), (ins crbitrc:$cond,
                          vrrc:$T, vrrc:$F), "#SELECT_F16",
                          [(set f128:$dst, (select i1:$cond, f128:$T, f128:$F))]>;
}
  def SELECT_VRRC: PPCCustomInserterPseudo<(outs vrrc:$dst), (ins crbitrc:$cond,
                          vrrc:$T, vrrc:$F), "#SELECT_VRRC",
                          [(set v4i32:$dst,
                                (select i1:$cond, v4i32:$T, v4i32:$F))]>;
}

// SPILL_CR - Indicate that we're dumping the CR register, so we'll need to
// scavenge a register for it.
let mayStore = 1 in {
def SPILL_CR : PPCEmitTimePseudo<(outs), (ins crrc:$cond, memri:$F),
                     "#SPILL_CR", []>;
def SPILL_CRBIT : PPCEmitTimePseudo<(outs), (ins crbitrc:$cond, memri:$F),
                         "#SPILL_CRBIT", []>;
}

// RESTORE_CR - Indicate that we're restoring the CR register (previously
// spilled), so we'll need to scavenge a register for it.
let mayLoad = 1 in {
def RESTORE_CR : PPCEmitTimePseudo<(outs crrc:$cond), (ins memri:$F),
                     "#RESTORE_CR", []>;
def RESTORE_CRBIT : PPCEmitTimePseudo<(outs crbitrc:$cond), (ins memri:$F),
                           "#RESTORE_CRBIT", []>;
}

let isTerminator = 1, isBarrier = 1, PPC970_Unit = 7, hasSideEffects = 0 in {
  let isPredicable = 1, isReturn = 1, Uses = [LR, RM] in
    def BLR : XLForm_2_ext<19, 16, 20, 0, 0, (outs), (ins), "blr", IIC_BrB,
                           [(PPCretglue)]>, Requires<[In32BitMode]>;
  let isBranch = 1, isIndirectBranch = 1, Uses = [CTR] in {
    let isPredicable = 1 in
      def BCTR : XLForm_2_ext<19, 528, 20, 0, 0, (outs), (ins), "bctr", IIC_BrB,
                              []>;

    let isCodeGenOnly = 1 in {
      def BCCCTR : XLForm_2_br<19, 528, 0, (outs), (ins (pred $BIBO, $CR):$cond),
                               "b${cond:cc}ctr${cond:pm} ${cond:reg}", IIC_BrB,
                               []>;

      def BCCTR :  XLForm_2_br2<19, 528, 12, 0, (outs), (ins crbitrc:$BI),
                                "bcctr 12, $BI, 0", IIC_BrB, []>;
      def BCCTRn : XLForm_2_br2<19, 528, 4, 0, (outs), (ins crbitrc:$BI),
                                "bcctr 4, $BI, 0", IIC_BrB, []>;
    }
  }
}

// Set the float rounding mode.
let Uses = [RM], Defs = [RM] in {
def SETRNDi : PPCCustomInserterPseudo<(outs f8rc:$FRT), (ins u2imm:$RND),
                    "#SETRNDi", [(set f64:$FRT, (int_ppc_setrnd (i32 imm:$RND)))]>;

def SETRND : PPCCustomInserterPseudo<(outs f8rc:$FRT), (ins gprc:$in),
                    "#SETRND", [(set f64:$FRT, (int_ppc_setrnd gprc :$in))]>;

def SETFLM : PPCCustomInserterPseudo<(outs f8rc:$FRT), (ins f8rc:$FLM),
                    "#SETFLM", [(set f64:$FRT, (int_ppc_setflm f8rc:$FLM))]>;
}

let isBarrier = 1, hasSideEffects = 1, Defs = [RM] in
def FENCE : PPCEmitTimePseudo<(outs), (ins), "#FENCE", []>;

let Defs = [LR] in
  def MovePCtoLR : PPCEmitTimePseudo<(outs), (ins), "#MovePCtoLR", []>,
                   PPC970_Unit_BRU;
let Defs = [LR] in
  def MoveGOTtoLR : PPCEmitTimePseudo<(outs), (ins), "#MoveGOTtoLR", []>,
                    PPC970_Unit_BRU;

// Pseudo instruction used by retguard.
//
// We need to get the PC into a register in order to calculate the address of
// the retguard cookies. This pseudo will branch immediately forward to get PC
// in LR, and then move LR into the destination register. The current value of
// LR is saved and restored via the given temp register, which is trashed.
let Size = 16 in {
def RETGUARD_LOAD_PC : PPCEmitTimePseudo<(outs gprc:$dest), (ins gprc:$tmp),
                       "#RGLoadPC", []>;
}

// Once we have the PC in a register, we need to load the address of the GOT
// into another register so we can then finally load the local offset of the
// retguard symbol entry from the GOT and then the cookie value.
let Size = 8 in {
def RETGUARD_LOAD_GOT : PPCEmitTimePseudo<(outs gprc:$dest),
                        (ins gprc:$pc, calltarget:$sym), "RGLoadGOT", []>;
}

let Size = 8 in {
// For static linkage, we can load the cookie directly
def RETGUARD_LOAD_COOKIE : PPCEmitTimePseudo<(outs gprc:$dest),
                        (ins calltarget:$sym), "RGLoadCookie", []>;
}

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7,
    hasSideEffects = 0 in {
  let isBarrier = 1 in {
    let isPredicable = 1 in
      def B : IForm<18, 0, 0, (outs), (ins directbrtarget:$LI),
                    "b $LI", IIC_BrB,
                    [(br bb:$LI)]>;
  def BA  : IForm<18, 1, 0, (outs), (ins absdirectbrtarget:$LI),
                  "ba $LI", IIC_BrB, []>;
  }

  // BCC represents an arbitrary conditional branch on a predicate.
  // FIXME: should be able to write a pattern for PPCcondbranch, but can't use
  // a two-value operand where a dag node expects two operands. :(
  let isCodeGenOnly = 1 in {
    class BCC_class : BForm<16, 0, 0, (outs), (ins (pred $BIBO, $CR):$cond, condbrtarget:$BD),
                            "b${cond:cc}${cond:pm} ${cond:reg}, $BD"
                            /*[(PPCcondbranch crrc:$crS, imm:$opc, bb:$BD)]*/>;
    def BCC : BCC_class;

    // The same as BCC, except that it's not a terminator. Used for introducing
    // control flow dependency without creating new blocks.
    let isTerminator = 0 in def CTRL_DEP : BCC_class;

    def BCCA : BForm<16, 1, 0, (outs), (ins (pred $BIBO, $CR):$cond, abscondbrtarget:$BD),
                     "b${cond:cc}a${cond:pm} ${cond:reg}, $BD">;

    let isReturn = 1, Uses = [LR, RM] in
    def BCCLR : XLForm_2_br<19, 16, 0, (outs), (ins (pred $BIBO, $CR):$cond),
                           "b${cond:cc}lr${cond:pm} ${cond:reg}", IIC_BrB, []>;
  }

  let isCodeGenOnly = 1 in {
    let Pattern = [(brcond i1:$BI, bb:$BD)] in
    def BC  : BForm_4<16, 12, 0, 0, (outs), (ins crbitrc:$BI, condbrtarget:$BD),
             "bc 12, $BI, $BD">;

    let Pattern = [(brcond (not i1:$BI), bb:$BD)] in
    def BCn : BForm_4<16, 4, 0, 0, (outs), (ins crbitrc:$BI, condbrtarget:$BD),
             "bc 4, $BI, $BD">;

    let isReturn = 1, Uses = [LR, RM] in {
    def BCLR  : XLForm_2_br2<19, 16, 12, 0, (outs), (ins crbitrc:$BI),
                             "bclr 12, $BI, 0", IIC_BrB, []>;
    def BCLRn : XLForm_2_br2<19, 16, 4, 0, (outs), (ins crbitrc:$BI),
                             "bclr 4, $BI, 0", IIC_BrB, []>;
    }
  }

  let isReturn = 1, Defs = [CTR], Uses = [CTR, LR, RM] in {
   def BDZLR  : XLForm_2_ext<19, 16, 18, 0, 0, (outs), (ins),
                             "bdzlr", IIC_BrB, []>;
   def BDNZLR : XLForm_2_ext<19, 16, 16, 0, 0, (outs), (ins),
                             "bdnzlr", IIC_BrB, []>;
   def BDZLRp : XLForm_2_ext<19, 16, 27, 0, 0, (outs), (ins),
                             "bdzlr+", IIC_BrB, []>;
   def BDNZLRp: XLForm_2_ext<19, 16, 25, 0, 0, (outs), (ins),
                             "bdnzlr+", IIC_BrB, []>;
   def BDZLRm : XLForm_2_ext<19, 16, 26, 0, 0, (outs), (ins),
                             "bdzlr-", IIC_BrB, []>;
   def BDNZLRm: XLForm_2_ext<19, 16, 24, 0, 0, (outs), (ins),
                             "bdnzlr-", IIC_BrB, []>;
  }

  let Defs = [CTR], Uses = [CTR] in {
    def BDZ  : BForm_1<16, 18, 0, 0, (outs), (ins condbrtarget:$BD),
                       "bdz $BD">;
    def BDNZ : BForm_1<16, 16, 0, 0, (outs), (ins condbrtarget:$BD),
                       "bdnz $BD">;
    def BDZA  : BForm_1<16, 18, 1, 0, (outs), (ins abscondbrtarget:$BD),
                        "bdza $BD">;
    def BDNZA : BForm_1<16, 16, 1, 0, (outs), (ins abscondbrtarget:$BD),
                        "bdnza $BD">;
    def BDZp : BForm_1<16, 27, 0, 0, (outs), (ins condbrtarget:$BD),
                       "bdz+ $BD">;
    def BDNZp: BForm_1<16, 25, 0, 0, (outs), (ins condbrtarget:$BD),
                       "bdnz+ $BD">;
    def BDZAp : BForm_1<16, 27, 1, 0, (outs), (ins abscondbrtarget:$BD),
                        "bdza+ $BD">;
    def BDNZAp: BForm_1<16, 25, 1, 0, (outs), (ins abscondbrtarget:$BD),
                        "bdnza+ $BD">;
    def BDZm : BForm_1<16, 26, 0, 0, (outs), (ins condbrtarget:$BD),
                       "bdz- $BD">;
    def BDNZm: BForm_1<16, 24, 0, 0, (outs), (ins condbrtarget:$BD),
                       "bdnz- $BD">;
    def BDZAm : BForm_1<16, 26, 1, 0, (outs), (ins abscondbrtarget:$BD),
                        "bdza- $BD">;
    def BDNZAm: BForm_1<16, 24, 1, 0, (outs), (ins abscondbrtarget:$BD),
                        "bdnza- $BD">;
  }
}

// The unconditional BCL used by the SjLj setjmp code.
let isCall = 1, hasCtrlDep = 1, isCodeGenOnly = 1, PPC970_Unit = 7,
    hasSideEffects = 0 in {
  let Defs = [LR], Uses = [RM] in {
    def BCLalways  : BForm_2<16, 20, 31, 0, 1, (outs), (ins condbrtarget:$BD),
                            "bcl 20, 31, $BD">;
  }
}

let isCall = 1, PPC970_Unit = 7, Defs = [LR] in {
  // Convenient aliases for call instructions
  let Uses = [RM] in {
    def BL  : IForm<18, 0, 1, (outs), (ins calltarget:$LI),
                    "bl $LI", IIC_BrB, []>;  // See Pat patterns below.
    def BLA : IForm<18, 1, 1, (outs), (ins abscalltarget:$LI),
                    "bla $LI", IIC_BrB, [(PPCcall (i32 imm:$LI))]>;

    let isCodeGenOnly = 1 in {
      def BL_TLS  : IForm<18, 0, 1, (outs), (ins tlscall32:$LI),
                          "bl $LI", IIC_BrB, []>;
      def BCCL : BForm<16, 0, 1, (outs), (ins (pred $BIBO, $CR):$cond, condbrtarget:$BD),
                       "b${cond:cc}l${cond:pm} ${cond:reg}, $BD">;
      def BCCLA : BForm<16, 1, 1, (outs), (ins (pred $BIBO, $CR):$cond, abscondbrtarget:$BD),
                        "b${cond:cc}la${cond:pm} ${cond:reg}, $BD">;

      def BCL  : BForm_4<16, 12, 0, 1, (outs),
                         (ins crbitrc:$BI, condbrtarget:$BD),
                         "bcl 12, $BI, $BD">;
      def BCLn : BForm_4<16, 4, 0, 1, (outs),
                         (ins crbitrc:$BI, condbrtarget:$BD),
                         "bcl 4, $BI, $BD">;
      def BL_NOP  : IForm_and_DForm_4_zero<18, 0, 1, 24,
                                           (outs), (ins calltarget:$LI),
                                           "bl $LI\n\tnop", IIC_BrB, []>;
    }
  }
  let Uses = [CTR, RM] in {
    let isPredicable = 1 in
      def BCTRL : XLForm_2_ext<19, 528, 20, 0, 1, (outs), (ins),
                              "bctrl", IIC_BrB, [(PPCbctrl)]>,
                  Requires<[In32BitMode]>;

    let isCodeGenOnly = 1 in {
      def BCCCTRL : XLForm_2_br<19, 528, 1, (outs), (ins (pred $BIBO, $CR):$cond),
                                "b${cond:cc}ctrl${cond:pm} ${cond:reg}", IIC_BrB,
                                []>;

      def BCCTRL  : XLForm_2_br2<19, 528, 12, 1, (outs), (ins crbitrc:$BI),
                                 "bcctrl 12, $BI, 0", IIC_BrB, []>;
      def BCCTRLn : XLForm_2_br2<19, 528, 4, 1, (outs), (ins crbitrc:$BI),
                                 "bcctrl 4, $BI, 0", IIC_BrB, []>;
    }
  }
  let Uses = [LR, RM] in {
    def BLRL : XLForm_2_ext<19, 16, 20, 0, 1, (outs), (ins),
                            "blrl", IIC_BrB, []>;

    let isCodeGenOnly = 1 in {
      def BCCLRL : XLForm_2_br<19, 16, 1, (outs), (ins (pred $BIBO, $CR):$cond),
                              "b${cond:cc}lrl${cond:pm} ${cond:reg}", IIC_BrB,
                              []>;

      def BCLRL  : XLForm_2_br2<19, 16, 12, 1, (outs), (ins crbitrc:$BI),
                                "bclrl 12, $BI, 0", IIC_BrB, []>;
      def BCLRLn : XLForm_2_br2<19, 16, 4, 1, (outs), (ins crbitrc:$BI),
                                "bclrl 4, $BI, 0", IIC_BrB, []>;
    }
  }
  let Defs = [CTR], Uses = [CTR, RM] in {
    def BDZL  : BForm_1<16, 18, 0, 1, (outs), (ins condbrtarget:$BD),
                        "bdzl $BD">;
    def BDNZL : BForm_1<16, 16, 0, 1, (outs), (ins condbrtarget:$BD),
                        "bdnzl $BD">;
    def BDZLA  : BForm_1<16, 18, 1, 1, (outs), (ins abscondbrtarget:$BD),
                         "bdzla $BD">;
    def BDNZLA : BForm_1<16, 16, 1, 1, (outs), (ins abscondbrtarget:$BD),
                         "bdnzla $BD">;
    def BDZLp : BForm_1<16, 27, 0, 1, (outs), (ins condbrtarget:$BD),
                        "bdzl+ $BD">;
    def BDNZLp: BForm_1<16, 25, 0, 1, (outs), (ins condbrtarget:$BD),
                        "bdnzl+ $BD">;
    def BDZLAp : BForm_1<16, 27, 1, 1, (outs), (ins abscondbrtarget:$BD),
                         "bdzla+ $BD">;
    def BDNZLAp: BForm_1<16, 25, 1, 1, (outs), (ins abscondbrtarget:$BD),
                         "bdnzla+ $BD">;
    def BDZLm : BForm_1<16, 26, 0, 1, (outs), (ins condbrtarget:$BD),
                        "bdzl- $BD">;
    def BDNZLm: BForm_1<16, 24, 0, 1, (outs), (ins condbrtarget:$BD),
                        "bdnzl- $BD">;
    def BDZLAm : BForm_1<16, 26, 1, 1, (outs), (ins abscondbrtarget:$BD),
                         "bdzla- $BD">;
    def BDNZLAm: BForm_1<16, 24, 1, 1, (outs), (ins abscondbrtarget:$BD),
                         "bdnzla- $BD">;
  }
  let Defs = [CTR], Uses = [CTR, LR, RM] in {
    def BDZLRL  : XLForm_2_ext<19, 16, 18, 0, 1, (outs), (ins),
                               "bdzlrl", IIC_BrB, []>;
    def BDNZLRL : XLForm_2_ext<19, 16, 16, 0, 1, (outs), (ins),
                               "bdnzlrl", IIC_BrB, []>;
    def BDZLRLp : XLForm_2_ext<19, 16, 27, 0, 1, (outs), (ins),
                               "bdzlrl+", IIC_BrB, []>;
    def BDNZLRLp: XLForm_2_ext<19, 16, 25, 0, 1, (outs), (ins),
                               "bdnzlrl+", IIC_BrB, []>;
    def BDZLRLm : XLForm_2_ext<19, 16, 26, 0, 1, (outs), (ins),
                               "bdzlrl-", IIC_BrB, []>;
    def BDNZLRLm: XLForm_2_ext<19, 16, 24, 0, 1, (outs), (ins),
                               "bdnzlrl-", IIC_BrB, []>;
  }
}

let isCall = 1, PPC970_Unit = 7, Defs = [LR, RM], isCodeGenOnly = 1 in {
  // Convenient aliases for call instructions
  let Uses = [RM] in {
    def BL_RM  : IForm<18, 0, 1, (outs), (ins calltarget:$LI),
                       "bl $LI", IIC_BrB, []>;  // See Pat patterns below.
    def BLA_RM : IForm<18, 1, 1, (outs), (ins abscalltarget:$LI),
                       "bla $LI", IIC_BrB, [(PPCcall_rm (i32 imm:$LI))]>;

    def BL_NOP_RM  : IForm_and_DForm_4_zero<18, 0, 1, 24,
                                            (outs), (ins calltarget:$LI),
                                            "bl $LI\n\tnop", IIC_BrB, []>;
  }
  let Uses = [CTR, RM] in {
    let isPredicable = 1 in
      def BCTRL_RM : XLForm_2_ext<19, 528, 20, 0, 1, (outs), (ins),
                                  "bctrl", IIC_BrB, [(PPCbctrl_rm)]>,
                  Requires<[In32BitMode]>;
  }
}

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNdi :PPCEmitTimePseudo< (outs),
                        (ins calltarget:$dst, i32imm:$offset),
                 "#TC_RETURNd $dst $offset",
                 []>;


let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNai :PPCEmitTimePseudo<(outs), (ins abscalltarget:$func, i32imm:$offset),
                 "#TC_RETURNa $func $offset",
                 [(PPCtc_return (i32 imm:$func), imm:$offset)]>;

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [RM] in
def TCRETURNri : PPCEmitTimePseudo<(outs), (ins CTRRC:$dst, i32imm:$offset),
                 "#TC_RETURNr $dst $offset",
                 []>;

let isCall = 1, PPC970_Unit = 7, isCodeGenOnly = 1,
    Defs = [LR, R2], Uses = [CTR, RM], RST = 2 in {
  def BCTRL_LWZinto_toc:
    XLForm_2_ext_and_DForm_1<19, 528, 20, 0, 1, 32, (outs),
     (ins (memri $D, $RA):$addr), "bctrl\n\tlwz 2, $addr", IIC_BrB,
     [(PPCbctrl_load_toc iaddr:$addr)]>, Requires<[In32BitMode]>;

}

let isCall = 1, PPC970_Unit = 7, isCodeGenOnly = 1,
    Defs = [LR, R2, RM], Uses = [CTR, RM], RST = 2 in {
  def BCTRL_LWZinto_toc_RM:
    XLForm_2_ext_and_DForm_1<19, 528, 20, 0, 1, 32, (outs),
     (ins (memri $D, $RA):$addr), "bctrl\n\tlwz 2, $addr", IIC_BrB,
     [(PPCbctrl_load_toc_rm iaddr:$addr)]>, Requires<[In32BitMode]>;

}

let isCodeGenOnly = 1, hasSideEffects = 0 in {

let isTerminator = 1, isBarrier = 1, PPC970_Unit = 7, isBranch = 1,
    isIndirectBranch = 1, isCall = 1, isReturn = 1, Uses = [CTR, RM]  in
def TAILBCTR : XLForm_2_ext<19, 528, 20, 0, 0, (outs), (ins), "bctr", IIC_BrB,
                            []>, Requires<[In32BitMode]>;

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7,
    isBarrier = 1, isCall = 1, isReturn = 1, Uses = [RM] in
def TAILB   : IForm<18, 0, 0, (outs), (ins calltarget:$LI),
                  "b $LI", IIC_BrB,
                  []>;

let isBranch = 1, isTerminator = 1, hasCtrlDep = 1, PPC970_Unit = 7,
    isBarrier = 1, isCall = 1, isReturn = 1, Uses = [RM] in
def TAILBA   : IForm<18, 0, 0, (outs), (ins abscalltarget:$LI),
                  "ba $LI", IIC_BrB,
                  []>;

}

// While longjmp is a control-flow barrier (fallthrough isn't allowed), setjmp
// is not.
let hasSideEffects = 1 in {
  let Defs = [CTR] in
  def EH_SjLj_SetJmp32  : PPCCustomInserterPseudo<(outs gprc:$dst), (ins memr:$buf),
                            "#EH_SJLJ_SETJMP32",
                            [(set i32:$dst, (PPCeh_sjlj_setjmp addr:$buf))]>,
                          Requires<[In32BitMode]>;
}

let hasSideEffects = 1, isBarrier = 1 in {
  let isTerminator = 1 in
  def EH_SjLj_LongJmp32 : PPCCustomInserterPseudo<(outs), (ins memr:$buf),
                            "#EH_SJLJ_LONGJMP32",
                            [(PPCeh_sjlj_longjmp addr:$buf)]>,
                          Requires<[In32BitMode]>;
}

// This pseudo is never removed from the function, as it serves as
// a terminator.  Size is set to 0 to prevent the builtin assembler
// from emitting it.
let isBranch = 1, isTerminator = 1, Size = 0 in {
  def EH_SjLj_Setup : PPCEmitTimePseudo<(outs), (ins directbrtarget:$dst),
                        "#EH_SjLj_Setup\t$dst", []>;
}

// System call.
let PPC970_Unit = 7 in {
  def SC     : SCForm<17, 1, 0, (outs), (ins i32imm:$LEV),
                      "sc $LEV", IIC_BrB, [(PPCsc (i32 imm:$LEV))]>;
}

// We mark SCV as having no scheduling model since it is only meant to be used
// as inline assembly. If we implement a builtin pattern for it we will need to
// add it to the P9 and P10 scheduling models.
let Predicates = [IsISA3_0], hasNoSchedulingInfo = 1 in {
  def SCV : SCForm<17, 0, 1, (outs), (ins i32imm:$LEV),
                   "scv $LEV", IIC_BrB, []>;
}

// Branch history rolling buffer.
def CLRBHRB : XForm_0<31, 430, (outs), (ins), "clrbhrb", IIC_BrB,
                      [(PPCclrbhrb)]>,
                      PPC970_DGroup_Single;
// The $dmy argument used for MFBHRBE is not needed; however, including
// it avoids automatic generation of PPCFastISel::fastEmit_i(), which
// interferes with necessary special handling (see PPCFastISel.cpp).
def MFBHRBE : XFXForm_3p<31, 302, (outs gprc:$RT),
                         (ins u10imm:$imm, u10imm:$dmy),
                         "mfbhrbe $RT, $imm", IIC_BrB,
                         [(set i32:$RT,
                               (PPCmfbhrbe imm:$imm, imm:$dmy))]>,
                         PPC970_DGroup_First;

def RFEBB : XLForm_S<19, 146, (outs), (ins u1imm:$S), "rfebb $S",
                     IIC_BrB, [(PPCrfebb (i32 imm:$S))]>,
                     PPC970_DGroup_Single;

def : InstAlias<"rfebb", (RFEBB 1)>;

// DCB* instructions.
def DCBA   : DCB_Form<758, 0, (outs), (ins (memrr $RA, $RB):$addr), "dcba $addr",
                      IIC_LdStDCBF, [(int_ppc_dcba xoaddr:$addr)]>,
                      PPC970_DGroup_Single;
def DCBI   : DCB_Form<470, 0, (outs), (ins (memrr $RA, $RB):$addr), "dcbi $addr",
                      IIC_LdStDCBF, [(int_ppc_dcbi xoaddr:$addr)]>,
                      PPC970_DGroup_Single;
def DCBST  : DCB_Form<54, 0, (outs), (ins (memrr $RA, $RB):$addr), "dcbst $addr",
                      IIC_LdStDCBF, [(int_ppc_dcbst xoaddr:$addr)]>,
                      PPC970_DGroup_Single;
def DCBZ   : DCB_Form<1014, 0, (outs), (ins (memrr $RA, $RB):$addr), "dcbz $addr",
                      IIC_LdStDCBF, [(int_ppc_dcbz xoaddr:$addr)]>,
                      PPC970_DGroup_Single;
def DCBZL  : DCB_Form<1014, 1, (outs), (ins (memrr $RA, $RB):$addr), "dcbzl $addr",
                      IIC_LdStDCBF, [(int_ppc_dcbzl xoaddr:$addr)]>,
                      PPC970_DGroup_Single;

def DCBF   : DCB_Form_hint<86, (outs), (ins u3imm:$TH, (memrr $RA, $RB):$addr),
                      "dcbf $addr, $TH", IIC_LdStDCBF, []>,
                      PPC970_DGroup_Single;

let hasSideEffects = 0, mayLoad = 1, mayStore = 1 in {
def DCBT   : DCB_Form_hint<278, (outs), (ins u5imm:$TH, (memrr $RA, $RB):$addr),
                      "dcbt $addr, $TH", IIC_LdStDCBF, []>,
                      PPC970_DGroup_Single;
def DCBTST : DCB_Form_hint<246, (outs), (ins u5imm:$TH, (memrr $RA, $RB):$addr),
                      "dcbtst $addr, $TH", IIC_LdStDCBF, []>,
                      PPC970_DGroup_Single;
} // hasSideEffects = 0

def ICBLC  : XForm_icbt<31, 230, (outs), (ins u4imm:$CT, (memrr $RA, $RB):$addr),
                       "icblc $CT, $addr", IIC_LdStStore>, Requires<[HasICBT]>;
def ICBLQ  : XForm_icbt<31, 198, (outs), (ins u4imm:$CT, (memrr $RA, $RB):$addr),
                       "icblq. $CT, $addr", IIC_LdStLoad>, Requires<[HasICBT]>;
def ICBT  : XForm_icbt<31, 22, (outs), (ins u4imm:$CT, (memrr $RA, $RB):$addr),
                       "icbt $CT, $addr", IIC_LdStLoad>, Requires<[HasICBT]>;
def ICBTLS : XForm_icbt<31, 486, (outs), (ins u4imm:$CT, (memrr $RA, $RB):$addr),
                       "icbtls $CT, $addr", IIC_LdStLoad>, Requires<[HasICBT]>;

def : Pat<(int_ppc_dcbt xoaddr:$dst),
          (DCBT 0, xoaddr:$dst)>;
def : Pat<(int_ppc_dcbtst xoaddr:$dst),
          (DCBTST 0, xoaddr:$dst)>;
def : Pat<(int_ppc_dcbf xoaddr:$dst),
          (DCBF 0, xoaddr:$dst)>;
def : Pat<(int_ppc_icbt xoaddr:$dst),
          (ICBT 0, xoaddr:$dst)>;

def : Pat<(prefetch xoaddr:$dst, (i32 0), timm, (i32 1)),
          (DCBT 0, xoaddr:$dst)>;   // data prefetch for loads
def : Pat<(prefetch xoaddr:$dst, (i32 1), timm, (i32 1)),
          (DCBTST 0, xoaddr:$dst)>; // data prefetch for stores
def : Pat<(prefetch xoaddr:$dst, (i32 0), timm, (i32 0)),
          (ICBT 0, xoaddr:$dst)>, Requires<[HasICBT]>; // inst prefetch (for read)

def : Pat<(int_ppc_dcbt_with_hint xoaddr:$dst, i32:$TH),
          (DCBT i32:$TH, xoaddr:$dst)>;
def : Pat<(int_ppc_dcbtst_with_hint xoaddr:$dst, i32:$TH),
          (DCBTST i32:$TH, xoaddr:$dst)>;

// Atomic operations
// FIXME: some of these might be used with constant operands. This will result
// in constant materialization instructions that may be redundant. We currently
// clean this up in PPCMIPeephole with calls to
// PPCInstrInfo::convertToImmediateForm() but we should probably not emit them
// in the first place.
let Defs = [CR0] in {
  def ATOMIC_LOAD_ADD_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_ADD_I8",
    [(set i32:$dst, (atomic_load_add_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_SUB_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_SUB_I8",
    [(set i32:$dst, (atomic_load_sub_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_AND_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_AND_I8",
    [(set i32:$dst, (atomic_load_and_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_OR_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_OR_I8",
    [(set i32:$dst, (atomic_load_or_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_XOR_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "ATOMIC_LOAD_XOR_I8",
    [(set i32:$dst, (atomic_load_xor_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_NAND_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_NAND_I8",
    [(set i32:$dst, (atomic_load_nand_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_MIN_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_MIN_I8",
    [(set i32:$dst, (atomic_load_min_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_MAX_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_MAX_I8",
    [(set i32:$dst, (atomic_load_max_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_UMIN_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_UMIN_I8",
    [(set i32:$dst, (atomic_load_umin_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_UMAX_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_UMAX_I8",
    [(set i32:$dst, (atomic_load_umax_i8 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_ADD_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_ADD_I16",
    [(set i32:$dst, (atomic_load_add_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_SUB_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_SUB_I16",
    [(set i32:$dst, (atomic_load_sub_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_AND_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_AND_I16",
    [(set i32:$dst, (atomic_load_and_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_OR_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_OR_I16",
    [(set i32:$dst, (atomic_load_or_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_XOR_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_XOR_I16",
    [(set i32:$dst, (atomic_load_xor_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_NAND_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_NAND_I16",
    [(set i32:$dst, (atomic_load_nand_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_MIN_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_MIN_I16",
    [(set i32:$dst, (atomic_load_min_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_MAX_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_MAX_I16",
    [(set i32:$dst, (atomic_load_max_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_UMIN_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_UMIN_I16",
    [(set i32:$dst, (atomic_load_umin_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_UMAX_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_UMAX_I16",
    [(set i32:$dst, (atomic_load_umax_i16 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_ADD_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_ADD_I32",
    [(set i32:$dst, (atomic_load_add_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_SUB_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_SUB_I32",
    [(set i32:$dst, (atomic_load_sub_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_AND_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_AND_I32",
    [(set i32:$dst, (atomic_load_and_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_OR_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_OR_I32",
    [(set i32:$dst, (atomic_load_or_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_XOR_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_XOR_I32",
    [(set i32:$dst, (atomic_load_xor_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_NAND_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_NAND_I32",
    [(set i32:$dst, (atomic_load_nand_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_MIN_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_MIN_I32",
    [(set i32:$dst, (atomic_load_min_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_MAX_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_MAX_I32",
    [(set i32:$dst, (atomic_load_max_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_UMIN_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_UMIN_I32",
    [(set i32:$dst, (atomic_load_umin_i32 ForceXForm:$ptr, i32:$incr))]>;
  def ATOMIC_LOAD_UMAX_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$incr), "#ATOMIC_LOAD_UMAX_I32",
    [(set i32:$dst, (atomic_load_umax_i32 ForceXForm:$ptr, i32:$incr))]>;

  def ATOMIC_CMP_SWAP_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$old, gprc:$new), "#ATOMIC_CMP_SWAP_I8",
    [(set i32:$dst, (atomic_cmp_swap_i8 ForceXForm:$ptr, i32:$old, i32:$new))]>;
  def ATOMIC_CMP_SWAP_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$old, gprc:$new), "#ATOMIC_CMP_SWAP_I16 $dst $ptr $old $new",
    [(set i32:$dst, (atomic_cmp_swap_i16 ForceXForm:$ptr, i32:$old, i32:$new))]>;
  def ATOMIC_CMP_SWAP_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$old, gprc:$new), "#ATOMIC_CMP_SWAP_I32 $dst $ptr $old $new",
    [(set i32:$dst, (atomic_cmp_swap_i32 ForceXForm:$ptr, i32:$old, i32:$new))]>;

  def ATOMIC_SWAP_I8 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$new), "#ATOMIC_SWAP_i8",
    [(set i32:$dst, (atomic_swap_i8 ForceXForm:$ptr, i32:$new))]>;
  def ATOMIC_SWAP_I16 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$new), "#ATOMIC_SWAP_I16",
    [(set i32:$dst, (atomic_swap_i16 ForceXForm:$ptr, i32:$new))]>;
  def ATOMIC_SWAP_I32 : PPCCustomInserterPseudo<
    (outs gprc:$dst), (ins memrr:$ptr, gprc:$new), "#ATOMIC_SWAP_I32",
    [(set i32:$dst, (atomic_swap_i32 ForceXForm:$ptr, i32:$new))]>;
}

def : Pat<(PPCatomicCmpSwap_8 ForceXForm:$ptr, i32:$old, i32:$new),
        (ATOMIC_CMP_SWAP_I8 ForceXForm:$ptr, i32:$old, i32:$new)>;
def : Pat<(PPCatomicCmpSwap_16 ForceXForm:$ptr, i32:$old, i32:$new),
        (ATOMIC_CMP_SWAP_I16 ForceXForm:$ptr, i32:$old, i32:$new)>;

// Instructions to support atomic operations
let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in {
def LBARX : XForm_1_memOp<31,  52, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                    "lbarx $RST, $addr", IIC_LdStLWARX, []>,
                    Requires<[HasPartwordAtomics]>;

def LHARX : XForm_1_memOp<31,  116, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                    "lharx $RST, $addr", IIC_LdStLWARX, []>,
                    Requires<[HasPartwordAtomics]>;

def LWARX : XForm_1_memOp<31,  20, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                    "lwarx $RST, $addr", IIC_LdStLWARX, []>;

// Instructions to support lock versions of atomics
// (EH=1 - see Power ISA 2.07 Book II 4.4.2)
def LBARXL : XForm_1_memOp<31,  52, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                     "lbarx $RST, $addr, 1", IIC_LdStLWARX, []>, isRecordForm,
                     Requires<[HasPartwordAtomics]>;

def LHARXL : XForm_1_memOp<31,  116, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                     "lharx $RST, $addr, 1", IIC_LdStLWARX, []>, isRecordForm,
                     Requires<[HasPartwordAtomics]>;

def LWARXL : XForm_1_memOp<31,  20, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                     "lwarx $RST, $addr, 1", IIC_LdStLWARX, []>, isRecordForm;

// The atomic instructions use the destination register as well as the next one
// or two registers in order (modulo 31).
let hasExtraSrcRegAllocReq = 1 in
def LWAT : X_RD5_RS5_IM5<31, 582, (outs gprc:$RST), (ins gprc:$RA, u5imm:$RB),
                         "lwat $RST, $RA, $RB", IIC_LdStLoad>,
           Requires<[IsISA3_0]>;
}

let Defs = [CR0], mayStore = 1, mayLoad = 0, hasSideEffects = 0 in {
def STBCX : XForm_1_memOp<31, 694, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                    "stbcx. $RST, $addr", IIC_LdStSTWCX, []>,
                    isRecordForm, Requires<[HasPartwordAtomics]>;

def STHCX : XForm_1_memOp<31, 726, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                    "sthcx. $RST, $addr", IIC_LdStSTWCX, []>,
                    isRecordForm, Requires<[HasPartwordAtomics]>;

def STWCX : XForm_1_memOp<31, 150, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                    "stwcx. $RST, $addr", IIC_LdStSTWCX, []>, isRecordForm;
}

let mayStore = 1, mayLoad = 0, hasSideEffects = 0 in
def STWAT : X_RD5_RS5_IM5<31, 710, (outs), (ins gprc:$RST, gprc:$RA, u5imm:$RB),
                          "stwat $RST, $RA, $RB", IIC_LdStStore>,
            Requires<[IsISA3_0]>;

let isTrap = 1, hasCtrlDep = 1 in
def TRAP  : XForm_24<31, 4, (outs), (ins), "trap", IIC_LdStLoad, [(trap)]>;

def TWI : DForm_base<3, (outs), (ins u5imm:$RST, gprc:$RA, s16imm:$D, variable_ops),
                     "twi $RST, $RA, $D", IIC_IntTrapW, []>;
def TW : XForm_1<31, 4, (outs), (ins u5imm:$RST, gprc:$RA, gprc:$RB, variable_ops),
                 "tw $RST, $RA, $RB", IIC_IntTrapW, []>;
def TDI : DForm_base<2, (outs), (ins u5imm:$RST, g8rc:$RA, s16imm:$D, variable_ops),
                     "tdi $RST, $RA, $D", IIC_IntTrapD, []>;
def TD : XForm_1<31, 68, (outs), (ins u5imm:$RST, g8rc:$RA, g8rc:$RB, variable_ops),
                 "td $RST, $RA, $RB", IIC_IntTrapD, []>;

def POPCNTB : XForm_11<31, 122, (outs gprc:$RA), (ins gprc:$RST),
                       "popcntb $RA, $RST", IIC_IntGeneral,
                       [(set i32:$RA, (int_ppc_popcntb i32:$RST))]>;

def CDTBCD : XForm_11<31, 282, (outs gprc:$RA), (ins gprc:$RST),
                      "cdtbcd $RA, $RST", IIC_IntGeneral, []>;
def CBCDTD : XForm_11<31, 314, (outs gprc:$RA), (ins gprc:$RST),
                      "cbcdtd $RA, $RST", IIC_IntGeneral, []>;

def ADDG6S : XOForm_1<31, 74, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                      "addg6s $RT, $RA, $RB", IIC_IntGeneral, []>;

//===----------------------------------------------------------------------===//
// PPC32 Load Instructions.
//

// Unindexed (r+i) Loads.
let PPC970_Unit = 2 in {
def LBZ : DForm_1<34, (outs gprc:$RST), (ins (memri $D, $RA):$addr),
                  "lbz $RST, $addr", IIC_LdStLoad,
                  [(set i32:$RST, (zextloadi8 DForm:$addr))]>, ZExt32To64,
                  SExt32To64;
def LHA : DForm_1<42, (outs gprc:$RST), (ins (memri $D, $RA):$addr),
                  "lha $RST, $addr", IIC_LdStLHA,
                  [(set i32:$RST, (sextloadi16 DForm:$addr))]>,
                  PPC970_DGroup_Cracked, SExt32To64;
def LHZ : DForm_1<40, (outs gprc:$RST), (ins (memri $D, $RA):$addr),
                  "lhz $RST, $addr", IIC_LdStLoad,
                  [(set i32:$RST, (zextloadi16 DForm:$addr))]>, ZExt32To64,
                  SExt32To64;
def LWZ : DForm_1<32, (outs gprc:$RST), (ins (memri $D, $RA):$addr),
                  "lwz $RST, $addr", IIC_LdStLoad,
                  [(set i32:$RST, (load DForm:$addr))]>, ZExt32To64;

let Predicates = [HasFPU] in {
def LFS : DForm_1<48, (outs f4rc:$RST), (ins (memri $D, $RA):$addr),
                  "lfs $RST, $addr", IIC_LdStLFD,
                  [(set f32:$RST, (load DForm:$addr))]>;
def LFD : DForm_1<50, (outs f8rc:$RST), (ins (memri $D, $RA):$addr),
                  "lfd $RST, $addr", IIC_LdStLFD,
                  [(set f64:$RST, (load DForm:$addr))]>;
}


// Unindexed (r+i) Loads with Update (preinc).
let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in {
def LBZU : DForm_1<35, (outs gprc:$RST, ptr_rc_nor0:$ea_result), (ins (memri $D, $RA):$addr),
                   "lbzu $RST, $addr", IIC_LdStLoadUpd,
                   []>, RegConstraint<"$RA = $ea_result">;

def LHAU : DForm_1<43, (outs gprc:$RST, ptr_rc_nor0:$ea_result), (ins (memri $D, $RA):$addr),
                   "lhau $RST, $addr", IIC_LdStLHAU,
                   []>, RegConstraint<"$addr.reg = $ea_result">,
                   NoEncode<"$ea_result">;

def LHZU : DForm_1<41, (outs gprc:$RST, ptr_rc_nor0:$ea_result), (ins (memri $D, $RA):$addr),
                   "lhzu $RST, $addr", IIC_LdStLoadUpd,
                   []>, RegConstraint<"$addr.reg = $ea_result">,
                   NoEncode<"$ea_result">;

def LWZU : DForm_1<33, (outs gprc:$RST, ptr_rc_nor0:$ea_result), (ins (memri $D, $RA):$addr),
                   "lwzu $RST, $addr", IIC_LdStLoadUpd,
                   []>, RegConstraint<"$addr.reg = $ea_result">,
                   NoEncode<"$ea_result">;

let Predicates = [HasFPU] in {
def LFSU : DForm_1<49, (outs f4rc:$RST, ptr_rc_nor0:$ea_result), (ins (memri $D, $RA):$addr),
                  "lfsu $RST, $addr", IIC_LdStLFDU,
                  []>, RegConstraint<"$addr.reg = $ea_result">,
                   NoEncode<"$ea_result">;

def LFDU : DForm_1<51, (outs f8rc:$RST, ptr_rc_nor0:$ea_result), (ins (memri $D, $RA):$addr),
                  "lfdu $RST, $addr", IIC_LdStLFDU,
                  []>, RegConstraint<"$addr.reg = $ea_result">,
                   NoEncode<"$ea_result">;
}


// Indexed (r+r) Loads with Update (preinc).
def LBZUX : XForm_1_memOp<31, 119, (outs gprc:$RST, ptr_rc_nor0:$ea_result),
                   (ins (memrr $RA, $RB):$addr),
                   "lbzux $RST, $addr", IIC_LdStLoadUpdX,
                   []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                   NoEncode<"$ea_result">;

def LHAUX : XForm_1_memOp<31, 375, (outs gprc:$RST, ptr_rc_nor0:$ea_result),
                   (ins (memrr $RA, $RB):$addr),
                   "lhaux $RST, $addr", IIC_LdStLHAUX,
                   []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                   NoEncode<"$ea_result">;

def LHZUX : XForm_1_memOp<31, 311, (outs gprc:$RST, ptr_rc_nor0:$ea_result),
                   (ins (memrr $RA, $RB):$addr),
                   "lhzux $RST, $addr", IIC_LdStLoadUpdX,
                   []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                   NoEncode<"$ea_result">;

def LWZUX : XForm_1_memOp<31, 55, (outs gprc:$RST, ptr_rc_nor0:$ea_result),
                   (ins (memrr $RA, $RB):$addr),
                   "lwzux $RST, $addr", IIC_LdStLoadUpdX,
                   []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                   NoEncode<"$ea_result">;

let Predicates = [HasFPU] in {
def LFSUX : XForm_1_memOp<31, 567, (outs f4rc:$RST, ptr_rc_nor0:$ea_result),
                   (ins (memrr $RA, $RB):$addr),
                   "lfsux $RST, $addr", IIC_LdStLFDUX,
                   []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                   NoEncode<"$ea_result">;

def LFDUX : XForm_1_memOp<31, 631, (outs f8rc:$RST, ptr_rc_nor0:$ea_result),
                   (ins (memrr $RA, $RB):$addr),
                   "lfdux $RST, $addr", IIC_LdStLFDUX,
                   []>, RegConstraint<"$addr.ptrreg = $ea_result">,
                   NoEncode<"$ea_result">;
}
}
}

// Indexed (r+r) Loads.
//
let PPC970_Unit = 2, mayLoad = 1, mayStore = 0 in {
def LBZX : XForm_1_memOp<31,  87, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                   "lbzx $RST, $addr", IIC_LdStLoad,
                   [(set i32:$RST, (zextloadi8 XForm:$addr))]>, ZExt32To64,
                   SExt32To64;
def LHAX : XForm_1_memOp<31, 343, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                   "lhax $RST, $addr", IIC_LdStLHA,
                   [(set i32:$RST, (sextloadi16 XForm:$addr))]>,
                   PPC970_DGroup_Cracked, SExt32To64;
def LHZX : XForm_1_memOp<31, 279, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                   "lhzx $RST, $addr", IIC_LdStLoad,
                   [(set i32:$RST, (zextloadi16 XForm:$addr))]>, ZExt32To64,
                   SExt32To64;
def LWZX : XForm_1_memOp<31,  23, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                   "lwzx $RST, $addr", IIC_LdStLoad,
                   [(set i32:$RST, (load XForm:$addr))]>, ZExt32To64;
def LHBRX : XForm_1_memOp<31, 790, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                   "lhbrx $RST, $addr", IIC_LdStLoad,
                   [(set i32:$RST, (PPClbrx ForceXForm:$addr, i16))]>, ZExt32To64;
def LWBRX : XForm_1_memOp<31,  534, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                   "lwbrx $RST, $addr", IIC_LdStLoad,
                   [(set i32:$RST, (PPClbrx ForceXForm:$addr, i32))]>, ZExt32To64;

let Predicates = [HasFPU] in {
def LFSX   : XForm_25_memOp<31, 535, (outs f4rc:$RST), (ins (memrr $RA, $RB):$addr),
                      "lfsx $RST, $addr", IIC_LdStLFD,
                      [(set f32:$RST, (load XForm:$addr))]>;
def LFDX   : XForm_25_memOp<31, 599, (outs f8rc:$RST), (ins (memrr $RA, $RB):$addr),
                      "lfdx $RST, $addr", IIC_LdStLFD,
                      [(set f64:$RST, (load XForm:$addr))]>;

def LFIWAX : XForm_25_memOp<31, 855, (outs f8rc:$RST), (ins (memrr $RA, $RB):$addr),
                      "lfiwax $RST, $addr", IIC_LdStLFD,
                      [(set f64:$RST, (PPClfiwax ForceXForm:$addr))]>;
def LFIWZX : XForm_25_memOp<31, 887, (outs f8rc:$RST), (ins (memrr $RA, $RB):$addr),
                      "lfiwzx $RST, $addr", IIC_LdStLFD,
                      [(set f64:$RST, (PPClfiwzx ForceXForm:$addr))]>;
}
}

// Load Multiple
let mayLoad = 1, mayStore = 0, hasSideEffects = 0 in
def LMW : DForm_1<46, (outs gprc:$RST), (ins (memri $D, $RA):$src),
                  "lmw $RST, $src", IIC_LdStLMW, []>;

//===----------------------------------------------------------------------===//
// PPC32 Store Instructions.
//

// Unindexed (r+i) Stores.
let PPC970_Unit = 2, mayStore = 1, mayLoad = 0 in {
def STB  : DForm_1<38, (outs), (ins gprc:$RST, (memri $D, $RA):$dst),
                   "stb $RST, $dst", IIC_LdStStore,
                   [(truncstorei8 i32:$RST, DForm:$dst)]>;
def STH  : DForm_1<44, (outs), (ins gprc:$RST, (memri $D, $RA):$dst),
                   "sth $RST, $dst", IIC_LdStStore,
                   [(truncstorei16 i32:$RST, DForm:$dst)]>;
def STW  : DForm_1<36, (outs), (ins gprc:$RST, (memri $D, $RA):$dst),
                   "stw $RST, $dst", IIC_LdStStore,
                   [(store i32:$RST, DForm:$dst)]>;
let Predicates = [HasFPU] in {
def STFS : DForm_1<52, (outs), (ins f4rc:$RST, (memri $D, $RA):$dst),
                   "stfs $RST, $dst", IIC_LdStSTFD,
                   [(store f32:$RST, DForm:$dst)]>;
def STFD : DForm_1<54, (outs), (ins f8rc:$RST, (memri $D, $RA):$dst),
                   "stfd $RST, $dst", IIC_LdStSTFD,
                   [(store f64:$RST, DForm:$dst)]>;
}
}

// Unindexed (r+i) Stores with Update (preinc).
let PPC970_Unit = 2, mayStore = 1, mayLoad = 0 in {
def STBU  : DForm_1<39, (outs ptr_rc_nor0:$ea_res), (ins gprc:$RST, (memri $D, $RA):$dst),
                    "stbu $RST, $dst", IIC_LdStSTU, []>,
                    RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">;
def STHU  : DForm_1<45, (outs ptr_rc_nor0:$ea_res), (ins gprc:$RST, (memri $D, $RA):$dst),
                    "sthu $RST, $dst", IIC_LdStSTU, []>,
                    RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">;
def STWU  : DForm_1<37, (outs ptr_rc_nor0:$ea_res), (ins gprc:$RST, (memri $D, $RA):$dst),
                    "stwu $RST, $dst", IIC_LdStSTU, []>,
                    RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">;
let Predicates = [HasFPU] in {
def STFSU : DForm_1<53, (outs ptr_rc_nor0:$ea_res), (ins f4rc:$RST, (memri $D, $RA):$dst),
                    "stfsu $RST, $dst", IIC_LdStSTFDU, []>,
                    RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">;
def STFDU : DForm_1<55, (outs ptr_rc_nor0:$ea_res), (ins f8rc:$RST, (memri $D, $RA):$dst),
                    "stfdu $RST, $dst", IIC_LdStSTFDU, []>,
                    RegConstraint<"$dst.reg = $ea_res">, NoEncode<"$ea_res">;
}
}

// Patterns to match the pre-inc stores.  We can't put the patterns on
// the instruction definitions directly as ISel wants the address base
// and offset to be separate operands, not a single complex operand.
def : Pat<(pre_truncsti8 i32:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STBU $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(pre_truncsti16 i32:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STHU $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(pre_store i32:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STWU $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(pre_store f32:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STFSU $rS, iaddroff:$ptroff, $ptrreg)>;
def : Pat<(pre_store f64:$rS, iPTR:$ptrreg, iaddroff:$ptroff),
          (STFDU $rS, iaddroff:$ptroff, $ptrreg)>;

// Indexed (r+r) Stores.
let PPC970_Unit = 2 in {
def STBX  : XForm_8_memOp<31, 215, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                   "stbx $RST, $addr", IIC_LdStStore,
                   [(truncstorei8 i32:$RST, XForm:$addr)]>,
                   PPC970_DGroup_Cracked;
def STHX  : XForm_8_memOp<31, 407, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                   "sthx $RST, $addr", IIC_LdStStore,
                   [(truncstorei16 i32:$RST, XForm:$addr)]>,
                   PPC970_DGroup_Cracked;
def STWX  : XForm_8_memOp<31, 151, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                   "stwx $RST, $addr", IIC_LdStStore,
                   [(store i32:$RST, XForm:$addr)]>,
                   PPC970_DGroup_Cracked;

def STHBRX: XForm_8_memOp<31, 918, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                   "sthbrx $RST, $addr", IIC_LdStStore,
                   [(PPCstbrx i32:$RST, ForceXForm:$addr, i16)]>,
                   PPC970_DGroup_Cracked;
def STWBRX: XForm_8_memOp<31, 662, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                   "stwbrx $RST, $addr", IIC_LdStStore,
                   [(PPCstbrx i32:$RST, ForceXForm:$addr, i32)]>,
                   PPC970_DGroup_Cracked;

let Predicates = [HasFPU] in {
def STFIWX: XForm_28_memOp<31, 983, (outs), (ins f8rc:$RST, (memrr $RA, $RB):$addr),
                     "stfiwx $RST, $addr", IIC_LdStSTFD,
                     [(PPCstfiwx f64:$RST, ForceXForm:$addr)]>;

def STFSX : XForm_28_memOp<31, 663, (outs), (ins f4rc:$RST, (memrr $RA, $RB):$addr),
                     "stfsx $RST, $addr", IIC_LdStSTFD,
                     [(store f32:$RST, XForm:$addr)]>;
def STFDX : XForm_28_memOp<31, 727, (outs), (ins f8rc:$RST, (memrr $RA, $RB):$addr),
                     "stfdx $RST, $addr", IIC_LdStSTFD,
                     [(store f64:$RST, XForm:$addr)]>;
}
}

// Indexed (r+r) Stores with Update (preinc).
let PPC970_Unit = 2, mayStore = 1, mayLoad = 0 in {
def STBUX : XForm_8_memOp<31, 247, (outs ptr_rc_nor0:$ea_res),
                          (ins gprc:$RST, (memrr $RA, $RB):$addr),
                          "stbux $RST, $addr", IIC_LdStSTUX, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
def STHUX : XForm_8_memOp<31, 439, (outs ptr_rc_nor0:$ea_res),
                          (ins gprc:$RST, (memrr $RA, $RB):$addr),
                          "sthux $RST, $addr", IIC_LdStSTUX, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
def STWUX : XForm_8_memOp<31, 183, (outs ptr_rc_nor0:$ea_res),
                          (ins gprc:$RST, (memrr $RA, $RB):$addr),
                          "stwux $RST, $addr", IIC_LdStSTUX, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
let Predicates = [HasFPU] in {
def STFSUX: XForm_8_memOp<31, 695, (outs ptr_rc_nor0:$ea_res),
                          (ins f4rc:$RST, (memrr $RA, $RB):$addr),
                          "stfsux $RST, $addr", IIC_LdStSTFDU, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
def STFDUX: XForm_8_memOp<31, 759, (outs ptr_rc_nor0:$ea_res),
                          (ins f8rc:$RST, (memrr $RA, $RB):$addr),
                          "stfdux $RST, $addr", IIC_LdStSTFDU, []>,
                          RegConstraint<"$addr.ptrreg = $ea_res">,
                          NoEncode<"$ea_res">,
                          PPC970_DGroup_Cracked;
}
}

// Patterns to match the pre-inc stores.  We can't put the patterns on
// the instruction definitions directly as ISel wants the address base
// and offset to be separate operands, not a single complex operand.
def : Pat<(pre_truncsti8 i32:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STBUX $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_truncsti16 i32:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STHUX $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_store i32:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STWUX $rS, $ptrreg, $ptroff)>;
let Predicates = [HasFPU] in {
def : Pat<(pre_store f32:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STFSUX $rS, $ptrreg, $ptroff)>;
def : Pat<(pre_store f64:$rS, iPTR:$ptrreg, iPTR:$ptroff),
          (STFDUX $rS, $ptrreg, $ptroff)>;
}

// Store Multiple
let mayStore = 1, mayLoad = 0, hasSideEffects = 0 in
def STMW : DForm_1<47, (outs), (ins gprc:$RST, (memri $D, $RA):$dst),
                   "stmw $RST, $dst", IIC_LdStLMW, []>;

def SYNC : XForm_24_sync<31, 598, (outs), (ins u2imm:$L),
                        "sync $L", IIC_LdStSync, []>;

let isCodeGenOnly = 1 in {
  def MSYNC : XForm_24_sync<31, 598, (outs), (ins),
                           "msync", IIC_LdStSync, []> {
    let L = 0;
  }
}

// We used to have EIEIO as value but E[0-9A-Z] is a reserved name
def EnforceIEIO : XForm_24_eieio<31, 854, (outs), (ins),
                                 "eieio", IIC_LdStLoad, []>;

def PseudoEIEIO : PPCEmitTimePseudo<(outs), (ins), "#PPCEIEIO",
                  [(int_ppc_eieio)]>;

def : Pat<(int_ppc_sync),   (SYNC 0)>, Requires<[HasSYNC]>;
def : Pat<(int_ppc_iospace_sync),   (SYNC 0)>, Requires<[HasSYNC]>;
def : Pat<(int_ppc_lwsync), (SYNC 1)>, Requires<[HasSYNC]>;
def : Pat<(int_ppc_iospace_lwsync), (SYNC 1)>, Requires<[HasSYNC]>;
def : Pat<(int_ppc_sync),   (MSYNC)>, Requires<[HasOnlyMSYNC]>;
def : Pat<(int_ppc_iospace_sync),   (MSYNC)>, Requires<[HasOnlyMSYNC]>;
def : Pat<(int_ppc_lwsync), (MSYNC)>, Requires<[HasOnlyMSYNC]>;
def : Pat<(int_ppc_iospace_lwsync), (MSYNC)>, Requires<[HasOnlyMSYNC]>;
def : Pat<(int_ppc_eieio),  (PseudoEIEIO)>;
def : Pat<(int_ppc_iospace_eieio),  (PseudoEIEIO)>;

//===----------------------------------------------------------------------===//
// PPC32 Arithmetic Instructions.
//

let PPC970_Unit = 1 in {  // FXU Operations.
def ADDI   : DForm_2<14, (outs gprc:$RST), (ins gprc_nor0:$RA, s16imm:$D),
                     "addi $RST, $RA, $D", IIC_IntSimple,
                     [(set i32:$RST, (add i32:$RA, imm32SExt16:$D))]>;
let BaseName = "addic" in {
let Defs = [CARRY] in
def ADDIC  : DForm_2<12, (outs gprc:$RST), (ins gprc:$RA, s16imm:$D),
                     "addic $RST, $RA, $D", IIC_IntGeneral,
                     [(set i32:$RST, (addc i32:$RA, imm32SExt16:$D))]>,
                     RecFormRel, PPC970_DGroup_Cracked;
let Defs = [CARRY, CR0] in
def ADDIC_rec : DForm_2<13, (outs gprc:$RST), (ins gprc:$RA, s16imm:$D),
                     "addic. $RST, $RA, $D", IIC_IntGeneral,
                     []>, isRecordForm, RecFormRel;
}
def ADDIS  : DForm_2<15, (outs gprc:$RST), (ins gprc_nor0:$RA, s17imm:$D),
                     "addis $RST, $RA, $D", IIC_IntSimple,
                     [(set i32:$RST, (add i32:$RA, imm16ShiftedSExt:$D))]>;
let isCodeGenOnly = 1 in
def LA     : DForm_2<14, (outs gprc:$RST), (ins gprc_nor0:$RA, s16imm:$D),
                     "la $RST, $D($RA)", IIC_IntGeneral,
                     [(set i32:$RST, (add i32:$RA,
                                          (PPClo tglobaladdr:$D, 0)))]>;
def MULLI  : DForm_2< 7, (outs gprc:$RST), (ins gprc:$RA, s16imm:$D),
                     "mulli $RST, $RA, $D", IIC_IntMulLI,
                     [(set i32:$RST, (mul i32:$RA, imm32SExt16:$D))]>;
let Defs = [CARRY] in
def SUBFIC : DForm_2< 8, (outs gprc:$RST), (ins gprc:$RA, s16imm:$D),
                     "subfic $RST, $RA, $D", IIC_IntGeneral,
                     [(set i32:$RST, (subc imm32SExt16:$D, i32:$RA))]>;

let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in {
  def LI  : DForm_2_r0<14, (outs gprc:$RST), (ins s16imm:$D),
                       "li $RST, $D", IIC_IntSimple,
                       [(set i32:$RST, imm32SExt16:$D)]>, SExt32To64;
  def LIS : DForm_2_r0<15, (outs gprc:$RST), (ins s17imm:$D),
                       "lis $RST, $D", IIC_IntSimple,
                       [(set i32:$RST, imm16ShiftedSExt:$D)]>, SExt32To64;
}
}

def : InstAlias<"li $rD, $imm", (ADDI gprc:$rD, ZERO, s16imm:$imm)>;
def : InstAlias<"lis $rD, $imm", (ADDIS gprc:$rD, ZERO, s17imm:$imm)>;

let PPC970_Unit = 1 in {  // FXU Operations.
let Defs = [CR0] in {
def ANDI_rec : DForm_4<28, (outs gprc:$RA), (ins gprc:$RST, u16imm:$D),
                    "andi. $RA, $RST, $D", IIC_IntGeneral,
                    [(set i32:$RA, (and i32:$RST, immZExt16:$D))]>,
                    isRecordForm, ZExt32To64, SExt32To64;
def ANDIS_rec : DForm_4<29, (outs gprc:$RA), (ins gprc:$RST, u16imm:$D),
                    "andis. $RA, $RST, $D", IIC_IntGeneral,
                    [(set i32:$RA, (and i32:$RST, imm16ShiftedZExt:$D))]>,
                    isRecordForm, ZExt32To64;
}
def ORI   : DForm_4<24, (outs gprc:$RA), (ins gprc:$RST, u16imm:$D),
                    "ori $RA, $RST, $D", IIC_IntSimple,
                    [(set i32:$RA, (or i32:$RST, immZExt16:$D))]>;
def ORIS  : DForm_4<25, (outs gprc:$RA), (ins gprc:$RST, u16imm:$D),
                    "oris $RA, $RST, $D", IIC_IntSimple,
                    [(set i32:$RA, (or i32:$RST, imm16ShiftedZExt:$D))]>;
def XORI  : DForm_4<26, (outs gprc:$RA), (ins gprc:$RST, u16imm:$D),
                    "xori $RA, $RST, $D", IIC_IntSimple,
                    [(set i32:$RA, (xor i32:$RST, immZExt16:$D))]>;
def XORIS : DForm_4<27, (outs gprc:$RA), (ins gprc:$RST, u16imm:$D),
                    "xoris $RA, $RST, $D", IIC_IntSimple,
                    [(set i32:$RA, (xor i32:$RST, imm16ShiftedZExt:$D))]>;

def NOP   : DForm_4_zero<24, (outs), (ins), "nop", IIC_IntSimple,
                         []>;
let isCodeGenOnly = 1 in {
// The POWER6 and POWER7 have special group-terminating nops.
def NOP_GT_PWR6 : DForm_4_fixedreg_zero<24, 1, (outs), (ins),
                                        "ori 1, 1, 0", IIC_IntSimple, []>;
def NOP_GT_PWR7 : DForm_4_fixedreg_zero<24, 2, (outs), (ins),
                                        "ori 2, 2, 0", IIC_IntSimple, []>;
}

let isCompare = 1, hasSideEffects = 0 in {
  def CMPWI : DForm_5_ext<11, (outs crrc:$BF), (ins gprc:$RA, s16imm:$D),
                          "cmpwi $BF, $RA, $D", IIC_IntCompare>;
  def CMPLWI : DForm_6_ext<10, (outs crrc:$BF), (ins gprc:$RA, u16imm:$D),
                           "cmplwi $BF, $RA, $D", IIC_IntCompare>;
  def CMPRB  : X_BF3_L1_RS5_RS5<31, 192, (outs crrc:$BF),
                                (ins u1imm:$L, gprc:$RA, gprc:$RB),
                                "cmprb $BF, $L, $RA, $RB", IIC_IntCompare, []>,
               Requires<[IsISA3_0]>;
}
}

let PPC970_Unit = 1, hasSideEffects = 0 in {  // FXU Operations.
let isCommutable = 1 in {
defm NAND : XForm_6r<31, 476, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "nand", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i32:$RA, (not (and i32:$RST, i32:$RB)))]>;
defm AND  : XForm_6r<31,  28, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "and", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i32:$RA, (and i32:$RST, i32:$RB))]>;
} // isCommutable
defm ANDC : XForm_6r<31,  60, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "andc", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i32:$RA, (and i32:$RST, (not i32:$RB)))]>;
let isCommutable = 1 in {
defm OR   : XForm_6r<31, 444, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "or", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i32:$RA, (or i32:$RST, i32:$RB))]>;
defm NOR  : XForm_6r<31, 124, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "nor", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i32:$RA, (not (or i32:$RST, i32:$RB)))]>;
} // isCommutable
defm ORC  : XForm_6r<31, 412, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "orc", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i32:$RA, (or i32:$RST, (not i32:$RB)))]>;
let isCommutable = 1 in {
defm EQV  : XForm_6r<31, 284, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "eqv", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i32:$RA, (not (xor i32:$RST, i32:$RB)))]>;
defm XOR  : XForm_6r<31, 316, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "xor", "$RA, $RST, $RB", IIC_IntSimple,
                     [(set i32:$RA, (xor i32:$RST, i32:$RB))]>;
} // isCommutable
defm SLW  : XForm_6r<31,  24, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "slw", "$RA, $RST, $RB", IIC_IntGeneral,
                     [(set i32:$RA, (PPCshl i32:$RST, i32:$RB))]>, ZExt32To64;
defm SRW  : XForm_6r<31, 536, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                     "srw", "$RA, $RST, $RB", IIC_IntGeneral,
                     [(set i32:$RA, (PPCsrl i32:$RST, i32:$RB))]>, ZExt32To64;
defm SRAW : XForm_6rc<31, 792, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                      "sraw", "$RA, $RST, $RB", IIC_IntShift,
                      [(set i32:$RA, (PPCsra i32:$RST, i32:$RB))]>, SExt32To64;
}

def : InstAlias<"mr $rA, $rB", (OR gprc:$rA, gprc:$rB, gprc:$rB)>;
def : InstAlias<"mr. $rA, $rB", (OR_rec gprc:$rA, gprc:$rB, gprc:$rB)>;

def : InstAlias<"not $rA, $rS", (NOR gprc:$rA, gprc:$rS, gprc:$rS)>;
def : InstAlias<"not. $rA, $rS", (NOR_rec gprc:$rA, gprc:$rS, gprc:$rS)>;

def : InstAlias<"nop", (ORI R0, R0, 0)>;

let PPC970_Unit = 1 in {  // FXU Operations.
let hasSideEffects = 0 in {
defm SRAWI : XForm_10rc<31, 824, (outs gprc:$RA), (ins gprc:$RST, u5imm:$RB),
                        "srawi", "$RA, $RST, $RB", IIC_IntShift,
                        [(set i32:$RA, (sra i32:$RST, (i32 imm:$RB)))]>,
                        SExt32To64;
defm CNTLZW : XForm_11r<31,  26, (outs gprc:$RA), (ins gprc:$RST),
                        "cntlzw", "$RA, $RST", IIC_IntGeneral,
                        [(set i32:$RA, (ctlz i32:$RST))]>, ZExt32To64;
defm CNTTZW : XForm_11r<31, 538, (outs gprc:$RA), (ins gprc:$RST),
                        "cnttzw", "$RA, $RST", IIC_IntGeneral,
                        [(set i32:$RA, (cttz i32:$RST))]>, Requires<[IsISA3_0]>,
                        ZExt32To64;
defm EXTSB  : XForm_11r<31, 954, (outs gprc:$RA), (ins gprc:$RST),
                        "extsb", "$RA, $RST", IIC_IntSimple,
                        [(set i32:$RA, (sext_inreg i32:$RST, i8))]>, SExt32To64;
defm EXTSH  : XForm_11r<31, 922, (outs gprc:$RA), (ins gprc:$RST),
                        "extsh", "$RA, $RST", IIC_IntSimple,
                        [(set i32:$RA, (sext_inreg i32:$RST, i16))]>, SExt32To64;

let isCommutable = 1 in
def CMPB : XForm_6<31, 508, (outs gprc:$RA), (ins gprc:$RST, gprc:$RB),
                   "cmpb $RA, $RST, $RB", IIC_IntGeneral,
                   [(set i32:$RA, (PPCcmpb i32:$RST, i32:$RB))]>;
}
let isCompare = 1, hasSideEffects = 0 in {
  def CMPW   : XForm_16_ext<31, 0, (outs crrc:$BF), (ins gprc:$RA, gprc:$RB),
                            "cmpw $BF, $RA, $RB", IIC_IntCompare>;
  def CMPLW  : XForm_16_ext<31, 32, (outs crrc:$BF), (ins gprc:$RA, gprc:$RB),
                            "cmplw $BF, $RA, $RB", IIC_IntCompare>;
}
}
let PPC970_Unit = 3, Predicates = [HasFPU] in {  // FPU Operations.
let isCompare = 1, mayRaiseFPException = 1, hasSideEffects = 0 in {
  def FCMPUS : XForm_17<63, 0, (outs crrc:$BF), (ins f4rc:$RA, f4rc:$RB),
                        "fcmpu $BF, $RA, $RB", IIC_FPCompare>;
  def FCMPOS : XForm_17<63, 32, (outs crrc:$BF), (ins f4rc:$RA, f4rc:$RB),
                        "fcmpo $BF, $RA, $RB", IIC_FPCompare>;
  let Interpretation64Bit = 1, isCodeGenOnly = 1 in {
    def FCMPUD : XForm_17<63, 0, (outs crrc:$BF), (ins f8rc:$RA, f8rc:$RB),
                          "fcmpu $BF, $RA, $RB", IIC_FPCompare>;
    def FCMPOD : XForm_17<63, 32, (outs crrc:$BF), (ins f8rc:$RA, f8rc:$RB),
                          "fcmpo $BF, $RA, $RB", IIC_FPCompare>;
  }
}

def FTDIV: XForm_17<63, 128, (outs crrc:$BF), (ins f8rc:$RA, f8rc:$RB),
                      "ftdiv $BF, $RA, $RB", IIC_FPCompare>;
def FTSQRT: XForm_17a<63, 160, (outs crrc:$BF), (ins f8rc:$RB),
                      "ftsqrt $BF, $RB", IIC_FPCompare,
                      [(set i32:$BF, (PPCftsqrt f64:$RB))]>;

let mayRaiseFPException = 1, hasSideEffects = 0 in {
  let Interpretation64Bit = 1, isCodeGenOnly = 1 in
  defm FRIND  : XForm_26r<63, 392, (outs f8rc:$RST), (ins f8rc:$RB),
                          "frin", "$RST, $RB", IIC_FPGeneral,
                          [(set f64:$RST, (any_fround f64:$RB))]>;
  defm FRINS  : XForm_26r<63, 392, (outs f4rc:$RST), (ins f4rc:$RB),
                          "frin", "$RST, $RB", IIC_FPGeneral,
                          [(set f32:$RST, (any_fround f32:$RB))]>;

  let Interpretation64Bit = 1, isCodeGenOnly = 1 in
  defm FRIPD  : XForm_26r<63, 456, (outs f8rc:$RST), (ins f8rc:$RB),
                          "frip", "$RST, $RB", IIC_FPGeneral,
                          [(set f64:$RST, (any_fceil f64:$RB))]>;
  defm FRIPS  : XForm_26r<63, 456, (outs f4rc:$RST), (ins f4rc:$RB),
                          "frip", "$RST, $RB", IIC_FPGeneral,
                          [(set f32:$RST, (any_fceil f32:$RB))]>;
  let Interpretation64Bit = 1, isCodeGenOnly = 1 in
  defm FRIZD  : XForm_26r<63, 424, (outs f8rc:$RST), (ins f8rc:$RB),
                          "friz", "$RST, $RB", IIC_FPGeneral,
                          [(set f64:$RST, (any_ftrunc f64:$RB))]>;
  defm FRIZS  : XForm_26r<63, 424, (outs f4rc:$RST), (ins f4rc:$RB),
                          "friz", "$RST, $RB", IIC_FPGeneral,
                          [(set f32:$RST, (any_ftrunc f32:$RB))]>;
  let Interpretation64Bit = 1, isCodeGenOnly = 1 in
  defm FRIMD  : XForm_26r<63, 488, (outs f8rc:$RST), (ins f8rc:$RB),
                          "frim", "$RST, $RB", IIC_FPGeneral,
                          [(set f64:$RST, (any_ffloor f64:$RB))]>;
  defm FRIMS  : XForm_26r<63, 488, (outs f4rc:$RST), (ins f4rc:$RB),
                          "frim", "$RST, $RB", IIC_FPGeneral,
                          [(set f32:$RST, (any_ffloor f32:$RB))]>;
}

let Uses = [RM], mayRaiseFPException = 1, hasSideEffects = 0 in {
  defm FCTIW  : XForm_26r<63, 14, (outs f8rc:$RST), (ins f8rc:$RB),
                          "fctiw", "$RST, $RB", IIC_FPGeneral,
                          []>;
  defm FCTIWU  : XForm_26r<63, 142, (outs f8rc:$RST), (ins f8rc:$RB),
                          "fctiwu", "$RST, $RB", IIC_FPGeneral,
                          []>;
  defm FCTIWZ : XForm_26r<63, 15, (outs f8rc:$RST), (ins f8rc:$RB),
                          "fctiwz", "$RST, $RB", IIC_FPGeneral,
                          [(set f64:$RST, (PPCany_fctiwz f64:$RB))]>;

  defm FRSP   : XForm_26r<63, 12, (outs f4rc:$RST), (ins f8rc:$RB),
                          "frsp", "$RST, $RB", IIC_FPGeneral,
                          [(set f32:$RST, (any_fpround f64:$RB))]>;

  defm FSQRT  : XForm_26r<63, 22, (outs f8rc:$RST), (ins f8rc:$RB),
                          "fsqrt", "$RST, $RB", IIC_FPSqrtD,
                          [(set f64:$RST, (any_fsqrt f64:$RB))]>;
  defm FSQRTS : XForm_26r<59, 22, (outs f4rc:$RST), (ins f4rc:$RB),
                          "fsqrts", "$RST, $RB", IIC_FPSqrtS,
                          [(set f32:$RST, (any_fsqrt f32:$RB))]>;
}
}

def : Pat<(PPCfsqrt f64:$frA), (FSQRT $frA)>;

/// Note that FMR is defined as pseudo-ops on the PPC970 because they are
/// often coalesced away and we don't want the dispatch group builder to think
/// that they will fill slots (which could cause the load of a LSU reject to
/// sneak into a d-group with a store).
let hasSideEffects = 0, Predicates = [HasFPU] in
defm FMR   : XForm_26r<63, 72, (outs f4rc:$RST), (ins f4rc:$RB),
                       "fmr", "$RST, $RB", IIC_FPGeneral,
                       []>,  // (set f32:$RST, f32:$RB)
                       PPC970_Unit_Pseudo;

let PPC970_Unit = 3, hasSideEffects = 0, Predicates = [HasFPU] in {  // FPU Operations.
// These are artificially split into two different forms, for 4/8 byte FP.
defm FABSS  : XForm_26r<63, 264, (outs f4rc:$RST), (ins f4rc:$RB),
                        "fabs", "$RST, $RB", IIC_FPGeneral,
                        [(set f32:$RST, (fabs f32:$RB))]>;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm FABSD  : XForm_26r<63, 264, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fabs", "$RST, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (fabs f64:$RB))]>;
defm FNABSS : XForm_26r<63, 136, (outs f4rc:$RST), (ins f4rc:$RB),
                        "fnabs", "$RST, $RB", IIC_FPGeneral,
                        [(set f32:$RST, (fneg (fabs f32:$RB)))]>;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm FNABSD : XForm_26r<63, 136, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fnabs", "$RST, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (fneg (fabs f64:$RB)))]>;
defm FNEGS  : XForm_26r<63, 40, (outs f4rc:$RST), (ins f4rc:$RB),
                        "fneg", "$RST, $RB", IIC_FPGeneral,
                        [(set f32:$RST, (fneg f32:$RB))]>;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm FNEGD  : XForm_26r<63, 40, (outs f8rc:$RST), (ins f8rc:$RB),
                        "fneg", "$RST, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (fneg f64:$RB))]>;

defm FCPSGNS : XForm_28r<63, 8, (outs f4rc:$RST), (ins f4rc:$RA, f4rc:$RB),
                        "fcpsgn", "$RST, $RA, $RB", IIC_FPGeneral,
                        [(set f32:$RST, (fcopysign f32:$RB, f32:$RA))]>;
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm FCPSGND : XForm_28r<63, 8, (outs f8rc:$RST), (ins f8rc:$RA, f8rc:$RB),
                        "fcpsgn", "$RST, $RA, $RB", IIC_FPGeneral,
                        [(set f64:$RST, (fcopysign f64:$RB, f64:$RA))]>;

// Reciprocal estimates.
let mayRaiseFPException = 1 in {
defm FRE      : XForm_26r<63, 24, (outs f8rc:$RST), (ins f8rc:$RB),
                          "fre", "$RST, $RB", IIC_FPGeneral,
                          [(set f64:$RST, (PPCfre f64:$RB))]>;
defm FRES     : XForm_26r<59, 24, (outs f4rc:$RST), (ins f4rc:$RB),
                          "fres", "$RST, $RB", IIC_FPGeneral,
                          [(set f32:$RST, (PPCfre f32:$RB))]>;
defm FRSQRTE  : XForm_26r<63, 26, (outs f8rc:$RST), (ins f8rc:$RB),
                          "frsqrte", "$RST, $RB", IIC_FPGeneral,
                          [(set f64:$RST, (PPCfrsqrte f64:$RB))]>;
defm FRSQRTES : XForm_26r<59, 26, (outs f4rc:$RST), (ins f4rc:$RB),
                          "frsqrtes", "$RST, $RB", IIC_FPGeneral,
                          [(set f32:$RST, (PPCfrsqrte f32:$RB))]>;
}
}

// XL-Form instructions.  condition register logical ops.
//
let hasSideEffects = 0 in
def MCRF   : XLForm_3<19, 0, (outs crrc:$BF), (ins crrc:$BFA),
                      "mcrf $BF, $BFA", IIC_BrMCR>,
             PPC970_DGroup_First, PPC970_Unit_CRU;

// FIXME: According to the ISA (section 2.5.1 of version 2.06), the
// condition-register logical instructions have preferred forms. Specifically,
// it is preferred that the bit specified by the BT field be in the same
// condition register as that specified by the bit BB. We might want to account
// for this via hinting the register allocator and anti-dep breakers, or we
// could constrain the register class to force this constraint and then loosen
// it during register allocation via convertToThreeAddress or some similar
// mechanism.

let isCommutable = 1 in {
def CRAND  : XLForm_1<19, 257, (outs crbitrc:$CRD),
                               (ins crbitrc:$CRA, crbitrc:$CRB),
                      "crand $CRD, $CRA, $CRB", IIC_BrCR,
                      [(set i1:$CRD, (and i1:$CRA, i1:$CRB))]>;

def CRNAND : XLForm_1<19, 225, (outs crbitrc:$CRD),
                               (ins crbitrc:$CRA, crbitrc:$CRB),
                      "crnand $CRD, $CRA, $CRB", IIC_BrCR,
                      [(set i1:$CRD, (not (and i1:$CRA, i1:$CRB)))]>;

def CROR   : XLForm_1<19, 449, (outs crbitrc:$CRD),
                               (ins crbitrc:$CRA, crbitrc:$CRB),
                      "cror $CRD, $CRA, $CRB", IIC_BrCR,
                      [(set i1:$CRD, (or i1:$CRA, i1:$CRB))]>;

def CRXOR  : XLForm_1<19, 193, (outs crbitrc:$CRD),
                               (ins crbitrc:$CRA, crbitrc:$CRB),
                      "crxor $CRD, $CRA, $CRB", IIC_BrCR,
                      [(set i1:$CRD, (xor i1:$CRA, i1:$CRB))]>;

def CRNOR  : XLForm_1<19, 33, (outs crbitrc:$CRD),
                              (ins crbitrc:$CRA, crbitrc:$CRB),
                      "crnor $CRD, $CRA, $CRB", IIC_BrCR,
                      [(set i1:$CRD, (not (or i1:$CRA, i1:$CRB)))]>;
def CREQV  : XLForm_1<19, 289, (outs crbitrc:$CRD),
                               (ins crbitrc:$CRA, crbitrc:$CRB),
                      "creqv $CRD, $CRA, $CRB", IIC_BrCR,
                      [(set i1:$CRD, (not (xor i1:$CRA, i1:$CRB)))]>;
} // isCommutable

let isCodeGenOnly = 1 in
def CRNOT  : XLForm_1s<19, 33, (outs crbitrc:$CRD), (ins crbitrc:$CRA),
                       "crnot $CRD, $CRA", IIC_BrCR,
                       [(set i1:$CRD, (not i1:$CRA))]>;

def CRANDC : XLForm_1<19, 129, (outs crbitrc:$CRD),
                               (ins crbitrc:$CRA, crbitrc:$CRB),
                      "crandc $CRD, $CRA, $CRB", IIC_BrCR,
                      [(set i1:$CRD, (and i1:$CRA, (not i1:$CRB)))]>;

def CRORC  : XLForm_1<19, 417, (outs crbitrc:$CRD),
                               (ins crbitrc:$CRA, crbitrc:$CRB),
                      "crorc $CRD, $CRA, $CRB", IIC_BrCR,
                      [(set i1:$CRD, (or i1:$CRA, (not i1:$CRB)))]>;

let isCodeGenOnly = 1 in {
let isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def CRSET  : XLForm_1_ext<19, 289, (outs crbitrc:$CRD), (ins),
              "creqv $CRD, $CRD, $CRD", IIC_BrCR,
              [(set i1:$CRD, 1)]>;

def CRUNSET: XLForm_1_ext<19, 193, (outs crbitrc:$CRD), (ins),
              "crxor $CRD, $CRD, $CRD", IIC_BrCR,
              [(set i1:$CRD, 0)]>;
}

let Defs = [CR1EQ], CRD = 6 in {
def CR6SET  : XLForm_1_ext<19, 289, (outs), (ins),
              "creqv 6, 6, 6", IIC_BrCR,
              [(PPCcr6set)]>;

def CR6UNSET: XLForm_1_ext<19, 193, (outs), (ins),
              "crxor 6, 6, 6", IIC_BrCR,
              [(PPCcr6unset)]>;
}
}

// XFX-Form instructions.  Instructions that deal with SPRs.
//

def MFSPR : XFXForm_1<31, 339, (outs gprc:$RST), (ins i32imm:$SPR),
                      "mfspr $RST, $SPR", IIC_SprMFSPR>;
def MTSPR : XFXForm_1<31, 467, (outs), (ins i32imm:$SPR, gprc:$RST),
                      "mtspr $SPR, $RST", IIC_SprMTSPR>;

def MFTB : XFXForm_1<31, 371, (outs gprc:$RST), (ins i32imm:$SPR),
                     "mftb $RST, $SPR", IIC_SprMFTB>;

def MFPMR : XFXForm_1<31, 334, (outs gprc:$RST), (ins i32imm:$SPR),
                     "mfpmr $RST, $SPR", IIC_SprMFPMR>;

def MTPMR : XFXForm_1<31, 462, (outs), (ins i32imm:$SPR, gprc:$RST),
                     "mtpmr $SPR, $RST", IIC_SprMTPMR>;


// A pseudo-instruction used to implement the read of the 64-bit cycle counter
// on a 32-bit target.
let hasSideEffects = 1 in
def ReadTB : PPCCustomInserterPseudo<(outs gprc:$lo, gprc:$hi), (ins),
                    "#ReadTB", []>;

let Uses = [CTR] in {
def MFCTR : XFXForm_1_ext<31, 339, 9, (outs gprc:$RST), (ins),
                          "mfctr $RST", IIC_SprMFSPR>,
            PPC970_DGroup_First, PPC970_Unit_FXU;
}
let Defs = [CTR], Pattern = [(PPCmtctr i32:$RST)] in {
def MTCTR : XFXForm_1_ext<31, 467, 9, (outs), (ins gprc:$RST),
                          "mtctr $RST", IIC_SprMTSPR>,
            PPC970_DGroup_First, PPC970_Unit_FXU;
}
let hasSideEffects = 1, isCodeGenOnly = 1, isNotDuplicable = 1, Defs = [CTR] in {
let Pattern = [(int_set_loop_iterations i32:$RST)] in
def MTCTRloop : XFXForm_1_ext<31, 467, 9, (outs), (ins gprc:$RST),
                              "mtctr $RST", IIC_SprMTSPR>,
                PPC970_DGroup_First, PPC970_Unit_FXU;
}

let hasSideEffects = 1, hasNoSchedulingInfo = 1, isNotDuplicable = 1, Uses = [CTR], Defs = [CTR] in
def DecreaseCTRloop : PPCEmitTimePseudo<(outs crbitrc:$rT), (ins i32imm:$stride),
                                       "#DecreaseCTRloop", [(set i1:$rT, (int_loop_decrement (i32 imm:$stride)))]>;

let hasSideEffects = 0 in {
let Defs = [LR] in {
def MTLR  : XFXForm_1_ext<31, 467, 8, (outs), (ins gprc:$RST),
                          "mtlr $RST", IIC_SprMTSPR>,
            PPC970_DGroup_First, PPC970_Unit_FXU;
}
let Uses = [LR] in {
def MFLR  : XFXForm_1_ext<31, 339, 8, (outs gprc:$RST), (ins),
                          "mflr $RST", IIC_SprMFSPR>,
            PPC970_DGroup_First, PPC970_Unit_FXU;
}
}

let hasSideEffects = 1 in {
  def MTUDSCR : XFXForm_1_ext<31, 467, 3, (outs), (ins gprc:$RST),
                              "mtspr 3, $RST", IIC_SprMTSPR>,
                PPC970_DGroup_Single, PPC970_Unit_FXU;
  def MFUDSCR : XFXForm_1_ext<31, 339, 3, (outs gprc:$RST), (ins),
                              "mfspr $RST, 3", IIC_SprMFSPR>,
                PPC970_DGroup_First, PPC970_Unit_FXU;
}

// Disable these alias on AIX since they are not supported.
let Predicates = [ModernAs] in {
// Aliases for moving to/from dscr to mtspr/mfspr
def : InstAlias<"mtudscr $Rx", (MTUDSCR gprc:$Rx)>;
def : InstAlias<"mfudscr $Rx", (MFUDSCR gprc:$Rx)>;
}

let isCodeGenOnly = 1 in {
  // Move to/from VRSAVE: despite being a SPR, the VRSAVE register is renamed
  // like a GPR on the PPC970.  As such, copies in and out have the same
  // performance characteristics as an OR instruction.
  def MTVRSAVE : XFXForm_1_ext<31, 467, 256, (outs), (ins gprc:$RST),
                               "mtspr 256, $RST", IIC_IntGeneral>,
                 PPC970_DGroup_Single, PPC970_Unit_FXU;
  def MFVRSAVE : XFXForm_1_ext<31, 339, 256, (outs gprc:$RST), (ins),
                               "mfspr $RST, 256", IIC_IntGeneral>,
                 PPC970_DGroup_First, PPC970_Unit_FXU;

  def MTVRSAVEv : XFXForm_1_ext<31, 467, 256,
                                (outs VRSAVERC:$SPR), (ins gprc:$RST),
                                "mtspr 256, $RST", IIC_IntGeneral>,
                  PPC970_DGroup_Single, PPC970_Unit_FXU;
  def MFVRSAVEv : XFXForm_1_ext<31, 339, 256, (outs gprc:$RST),
                                (ins VRSAVERC:$SPR),
                                "mfspr $RST, 256", IIC_IntGeneral>,
                  PPC970_DGroup_First, PPC970_Unit_FXU;
}

// Aliases for mtvrsave/mfvrsave to mfspr/mtspr.
def : InstAlias<"mtvrsave $rS", (MTVRSAVE gprc:$rS)>;
def : InstAlias<"mfvrsave $rS", (MFVRSAVE gprc:$rS)>;

let hasSideEffects = 0 in {
// mtocrf's input needs to be prepared by shifting by an amount dependent
// on the cr register selected. Thus, post-ra anti-dep breaking must not
// later change that register assignment.
let hasExtraDefRegAllocReq = 1 in {
def MTOCRF: XFXForm_5a<31, 144, (outs crbitm:$FXM), (ins gprc:$RST),
                       "mtocrf $FXM, $RST", IIC_BrMCRX>,
            PPC970_DGroup_First, PPC970_Unit_CRU;

// Similarly to mtocrf, the mask for mtcrf must be prepared in a way that
// is dependent on the cr fields being set.
def MTCRF : XFXForm_5<31, 144, (outs), (ins i32imm:$FXM, gprc:$RST),
                      "mtcrf $FXM, $RST", IIC_BrMCRX>,
            PPC970_MicroCode, PPC970_Unit_CRU;
} // hasExtraDefRegAllocReq = 1

// mfocrf's input needs to be prepared by shifting by an amount dependent
// on the cr register selected. Thus, post-ra anti-dep breaking must not
// later change that register assignment.
let hasExtraSrcRegAllocReq = 1 in {
def MFOCRF: XFXForm_5a<31, 19, (outs gprc:$RST), (ins crbitm:$FXM),
                       "mfocrf $RST, $FXM", IIC_SprMFCRF>,
            PPC970_DGroup_First, PPC970_Unit_CRU;

// Similarly to mfocrf, the mask for mfcrf must be prepared in a way that
// is dependent on the cr fields being copied.
def MFCR : XFXForm_3<31, 19, (outs gprc:$RT), (ins),
                     "mfcr $RT", IIC_SprMFCR>,
                     PPC970_MicroCode, PPC970_Unit_CRU;
} // hasExtraSrcRegAllocReq = 1

def MCRXRX : X_BF3<31, 576, (outs crrc:$BF), (ins),
                   "mcrxrx $BF", IIC_BrMCRX>, Requires<[IsISA3_0]>;
} // hasSideEffects = 0

def : InstAlias<"mtcr $rA", (MTCRF 255, gprc:$rA)>;

let Predicates = [HasFPU] in {
// Custom inserter instruction to perform FADD in round-to-zero mode.
let Uses = [RM], mayRaiseFPException = 1 in {
  def FADDrtz: PPCCustomInserterPseudo<(outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRB), "",
                      [(set f64:$FRT, (PPCany_faddrtz f64:$FRA, f64:$FRB))]>;
}

// The above pseudo gets expanded to make use of the following instructions
// to manipulate FPSCR.  Note that FPSCR is not modeled at the DAG level.

// When FM is 30/31, we are setting the 62/63 bit of FPSCR, the implicit-def
// RM should be set.
let hasSideEffects = 1, Defs = [RM] in {
def MTFSB0 : XForm_43<63, 70, (outs), (ins u5imm:$FM),
                      "mtfsb0 $FM", IIC_IntMTFSB0,
                      [(int_ppc_mtfsb0 timm:$FM)]>,
             PPC970_DGroup_Single, PPC970_Unit_FPU;
def MTFSB1 : XForm_43<63, 38, (outs), (ins u5imm:$FM),
                      "mtfsb1 $FM", IIC_IntMTFSB0,
                      [(int_ppc_mtfsb1 timm:$FM)]>,
             PPC970_DGroup_Single, PPC970_Unit_FPU;
}

let Defs = [RM], hasSideEffects = 1 in {
  let isCodeGenOnly = 1 in
  def MTFSFb  : XFLForm<63, 711, (outs), (ins i32imm:$FM, f8rc:$RT),
                        "mtfsf $FM, $RT", IIC_IntMTFSB0,
                        [(int_ppc_mtfsf timm:$FM, f64:$RT)]>,
                PPC970_DGroup_Single, PPC970_Unit_FPU;
}
let Uses = [RM], hasSideEffects = 1 in {
  def MFFS   : XForm_42<63, 583, (outs f8rc:$RST), (ins),
                         "mffs $RST", IIC_IntMFFS,
                         [(set f64:$RST, (PPCmffs))]>,
               PPC970_DGroup_Single, PPC970_Unit_FPU;

  let Defs = [CR1] in
  def MFFS_rec : XForm_42<63, 583, (outs f8rc:$RST), (ins),
                      "mffs. $RST", IIC_IntMFFS, []>, isRecordForm;

  def MFFSCE : X_FRT5_XO2_XO3_XO10<63, 0, 1, 583, (outs f8rc:$RST), (ins),
                                  "mffsce $RST", IIC_IntMFFS, []>,
               PPC970_DGroup_Single, PPC970_Unit_FPU;

  def MFFSCDRN : X_FRT5_XO2_XO3_FRB5_XO10<63, 2, 4, 583, (outs f8rc:$RST),
                                         (ins f8rc:$FRB), "mffscdrn $RST, $FRB",
                                         IIC_IntMFFS, []>,
                 PPC970_DGroup_Single, PPC970_Unit_FPU;

  def MFFSCDRNI : X_FRT5_XO2_XO3_DRM3_XO10<63, 2, 5, 583, (outs f8rc:$RST),
                                          (ins u3imm:$DRM),
                                          "mffscdrni $RST, $DRM",
                                          IIC_IntMFFS, []>,
                  PPC970_DGroup_Single, PPC970_Unit_FPU;

  def MFFSCRN : X_FRT5_XO2_XO3_FRB5_XO10<63, 2, 6, 583, (outs f8rc:$RST),
                                        (ins f8rc:$FRB), "mffscrn $RST, $FRB",
                                        IIC_IntMFFS, []>,
                PPC970_DGroup_Single, PPC970_Unit_FPU;

  def MFFSCRNI : X_FRT5_XO2_XO3_RM2_X10<63, 2, 7, 583, (outs f8rc:$RST),
                                       (ins u2imm:$RM), "mffscrni $RST, $RM",
                                       IIC_IntMFFS, []>,
                 PPC970_DGroup_Single, PPC970_Unit_FPU;

  def MFFSL  : X_FRT5_XO2_XO3_XO10<63, 3, 0, 583, (outs f8rc:$RST), (ins),
                                  "mffsl $RST", IIC_IntMFFS, []>,
               PPC970_DGroup_Single, PPC970_Unit_FPU;
}
}

let Predicates = [IsISA3_0] in {
def MODSW : XForm_8<31, 779, (outs gprc:$RST), (ins gprc:$RA, gprc:$RB),
                        "modsw $RST, $RA, $RB", IIC_IntDivW,
                        [(set i32:$RST, (srem i32:$RA, i32:$RB))]>;
def MODUW : XForm_8<31, 267, (outs gprc:$RST), (ins gprc:$RA, gprc:$RB),
                        "moduw $RST, $RA, $RB", IIC_IntDivW,
                        [(set i32:$RST, (urem i32:$RA, i32:$RB))]>;
let hasSideEffects = 1 in
def ADDEX : Z23Form_RTAB5_CY2<31, 170, (outs gprc:$RT),
                              (ins gprc:$RA, gprc:$RB, u2imm:$CY),
                              "addex $RT, $RA, $RB, $CY", IIC_IntGeneral, []>;
}

let PPC970_Unit = 1, hasSideEffects = 0 in {  // FXU Operations.
// XO-Form instructions.  Arithmetic instructions that can set overflow bit
let isCommutable = 1 in
defm ADD4  : XOForm_1rx<31, 266, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                        "add", "$RT, $RA, $RB", IIC_IntSimple,
                        [(set i32:$RT, (add i32:$RA, i32:$RB))]>;
let isCodeGenOnly = 1 in
def ADD4TLS  : XOForm_1<31, 266, 0, (outs gprc:$RT), (ins gprc:$RA, tlsreg32:$RB),
                       "add $RT, $RA, $RB", IIC_IntSimple,
                       [(set i32:$RT, (add i32:$RA, tglobaltlsaddr:$RB))]>;
let isCommutable = 1 in
defm ADDC  : XOForm_1rc<31, 10, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                        "addc", "$RT, $RA, $RB", IIC_IntGeneral,
                        [(set i32:$RT, (addc i32:$RA, i32:$RB))]>,
                        PPC970_DGroup_Cracked;

defm DIVW  : XOForm_1rcr<31, 491, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                          "divw", "$RT, $RA, $RB", IIC_IntDivW,
                          [(set i32:$RT, (sdiv i32:$RA, i32:$RB))]>;
defm DIVWU : XOForm_1rcr<31, 459, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                          "divwu", "$RT, $RA, $RB", IIC_IntDivW,
                          [(set i32:$RT, (udiv i32:$RA, i32:$RB))]>;
defm DIVWE : XOForm_1rcr<31, 427, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                         "divwe", "$RT, $RA, $RB", IIC_IntDivW,
                         [(set i32:$RT, (int_ppc_divwe gprc:$RA, gprc:$RB))]>,
                         Requires<[HasExtDiv]>;
defm DIVWEU : XOForm_1rcr<31, 395, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                          "divweu", "$RT, $RA, $RB", IIC_IntDivW,
                          [(set i32:$RT, (int_ppc_divweu gprc:$RA, gprc:$RB))]>,
                          Requires<[HasExtDiv]>;
let isCommutable = 1 in {
defm MULHW : XOForm_1r<31, 75, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                       "mulhw", "$RT, $RA, $RB", IIC_IntMulHW,
                       [(set i32:$RT, (mulhs i32:$RA, i32:$RB))]>;
defm MULHWU : XOForm_1r<31, 11, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                       "mulhwu", "$RT, $RA, $RB", IIC_IntMulHWU,
                       [(set i32:$RT, (mulhu i32:$RA, i32:$RB))]>;
defm MULLW : XOForm_1rx<31, 235, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                        "mullw", "$RT, $RA, $RB", IIC_IntMulHW,
                        [(set i32:$RT, (mul i32:$RA, i32:$RB))]>;
} // isCommutable
defm SUBF  : XOForm_1rx<31, 40, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                        "subf", "$RT, $RA, $RB", IIC_IntGeneral,
                        [(set i32:$RT, (sub i32:$RB, i32:$RA))]>;
defm SUBFC : XOForm_1rc<31, 8, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                        "subfc", "$RT, $RA, $RB", IIC_IntGeneral,
                        [(set i32:$RT, (subc i32:$RB, i32:$RA))]>,
                        PPC970_DGroup_Cracked;
defm NEG    : XOForm_3r<31, 104, 0, (outs gprc:$RT), (ins gprc:$RA),
                        "neg", "$RT, $RA", IIC_IntSimple,
                        [(set i32:$RT, (ineg i32:$RA))]>;
let Uses = [CARRY] in {
let isCommutable = 1 in
defm ADDE  : XOForm_1rc<31, 138, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                        "adde", "$RT, $RA, $RB", IIC_IntGeneral,
                        [(set i32:$RT, (adde i32:$RA, i32:$RB))]>;
defm ADDME  : XOForm_3rc<31, 234, 0, (outs gprc:$RT), (ins gprc:$RA),
                         "addme", "$RT, $RA", IIC_IntGeneral,
                         [(set i32:$RT, (adde i32:$RA, -1))]>;
defm ADDZE  : XOForm_3rc<31, 202, 0, (outs gprc:$RT), (ins gprc:$RA),
                         "addze", "$RT, $RA", IIC_IntGeneral,
                         [(set i32:$RT, (adde i32:$RA, 0))]>;
defm SUBFE : XOForm_1rc<31, 136, 0, (outs gprc:$RT), (ins gprc:$RA, gprc:$RB),
                        "subfe", "$RT, $RA, $RB", IIC_IntGeneral,
                        [(set i32:$RT, (sube i32:$RB, i32:$RA))]>;
defm SUBFME : XOForm_3rc<31, 232, 0, (outs gprc:$RT), (ins gprc:$RA),
                         "subfme", "$RT, $RA", IIC_IntGeneral,
                         [(set i32:$RT, (sube -1, i32:$RA))]>;
defm SUBFZE : XOForm_3rc<31, 200, 0, (outs gprc:$RT), (ins gprc:$RA),
                         "subfze", "$RT, $RA", IIC_IntGeneral,
                         [(set i32:$RT, (sube 0, i32:$RA))]>;
}
}

def : InstAlias<"sub $rA, $rB, $rC", (SUBF gprc:$rA, gprc:$rC, gprc:$rB)>;
def : InstAlias<"sub. $rA, $rB, $rC", (SUBF_rec gprc:$rA, gprc:$rC, gprc:$rB)>;
def : InstAlias<"subc $rA, $rB, $rC", (SUBFC gprc:$rA, gprc:$rC, gprc:$rB)>;
def : InstAlias<"subc. $rA, $rB, $rC", (SUBFC_rec gprc:$rA, gprc:$rC, gprc:$rB)>;

// A-Form instructions.  Most of the instructions executed in the FPU are of
// this type.
//
let PPC970_Unit = 3, hasSideEffects = 0, Predicates = [HasFPU] in {  // FPU Operations.
let mayRaiseFPException = 1, Uses = [RM] in {
let isCommutable = 1 in {
  defm FMADD : AForm_1r<63, 29,
                      (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRC, f8rc:$FRB),
                      "fmadd", "$FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set f64:$FRT, (any_fma f64:$FRA, f64:$FRC, f64:$FRB))]>;
  defm FMADDS : AForm_1r<59, 29,
                      (outs f4rc:$FRT), (ins f4rc:$FRA, f4rc:$FRC, f4rc:$FRB),
                      "fmadds", "$FRT, $FRA, $FRC, $FRB", IIC_FPGeneral,
                      [(set f32:$FRT, (any_fma f32:$FRA, f32:$FRC, f32:$FRB))]>;
  defm FMSUB : AForm_1r<63, 28,
                      (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRC, f8rc:$FRB),
                      "fmsub", "$FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set f64:$FRT,
                            (any_fma f64:$FRA, f64:$FRC, (fneg f64:$FRB)))]>;
  defm FMSUBS : AForm_1r<59, 28,
                      (outs f4rc:$FRT), (ins f4rc:$FRA, f4rc:$FRC, f4rc:$FRB),
                      "fmsubs", "$FRT, $FRA, $FRC, $FRB", IIC_FPGeneral,
                      [(set f32:$FRT,
                            (any_fma f32:$FRA, f32:$FRC, (fneg f32:$FRB)))]>;
  defm FNMADD : AForm_1r<63, 31,
                      (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRC, f8rc:$FRB),
                      "fnmadd", "$FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set f64:$FRT,
                            (fneg (any_fma f64:$FRA, f64:$FRC, f64:$FRB)))]>;
  defm FNMADDS : AForm_1r<59, 31,
                      (outs f4rc:$FRT), (ins f4rc:$FRA, f4rc:$FRC, f4rc:$FRB),
                      "fnmadds", "$FRT, $FRA, $FRC, $FRB", IIC_FPGeneral,
                      [(set f32:$FRT,
                            (fneg (any_fma f32:$FRA, f32:$FRC, f32:$FRB)))]>;
  defm FNMSUB : AForm_1r<63, 30,
                      (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRC, f8rc:$FRB),
                      "fnmsub", "$FRT, $FRA, $FRC, $FRB", IIC_FPFused,
                      [(set f64:$FRT, (fneg (any_fma f64:$FRA, f64:$FRC,
                                                 (fneg f64:$FRB))))]>;
  defm FNMSUBS : AForm_1r<59, 30,
                      (outs f4rc:$FRT), (ins f4rc:$FRA, f4rc:$FRC, f4rc:$FRB),
                      "fnmsubs", "$FRT, $FRA, $FRC, $FRB", IIC_FPGeneral,
                      [(set f32:$FRT, (fneg (any_fma f32:$FRA, f32:$FRC,
                                                 (fneg f32:$FRB))))]>;
} // isCommutable
}
// FSEL is artificially split into 4 and 8-byte forms for the result.  To avoid
// having 4 of these, force the comparison to always be an 8-byte double (code
// should use an FMRSD if the input comparison value really wants to be a float)
// and 4/8 byte forms for the result and operand type..
let Interpretation64Bit = 1, isCodeGenOnly = 1 in
defm FSELD : AForm_1r<63, 23,
                      (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRC, f8rc:$FRB),
                      "fsel", "$FRT, $FRA, $FRC, $FRB", IIC_FPGeneral,
                      [(set f64:$FRT, (PPCfsel f64:$FRA, f64:$FRC, f64:$FRB))]>;
defm FSELS : AForm_1r<63, 23,
                      (outs f4rc:$FRT), (ins f8rc:$FRA, f4rc:$FRC, f4rc:$FRB),
                      "fsel", "$FRT, $FRA, $FRC, $FRB", IIC_FPGeneral,
                      [(set f32:$FRT, (PPCfsel f64:$FRA, f32:$FRC, f32:$FRB))]>;
let Uses = [RM], mayRaiseFPException = 1 in {
  let isCommutable = 1 in {
  defm FADD  : AForm_2r<63, 21,
                        (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRB),
                        "fadd", "$FRT, $FRA, $FRB", IIC_FPAddSub,
                        [(set f64:$FRT, (any_fadd f64:$FRA, f64:$FRB))]>;
  defm FADDS : AForm_2r<59, 21,
                        (outs f4rc:$FRT), (ins f4rc:$FRA, f4rc:$FRB),
                        "fadds", "$FRT, $FRA, $FRB", IIC_FPGeneral,
                        [(set f32:$FRT, (any_fadd f32:$FRA, f32:$FRB))]>;
  } // isCommutable
  defm FDIV  : AForm_2r<63, 18,
                        (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRB),
                        "fdiv", "$FRT, $FRA, $FRB", IIC_FPDivD,
                        [(set f64:$FRT, (any_fdiv f64:$FRA, f64:$FRB))]>;
  defm FDIVS : AForm_2r<59, 18,
                        (outs f4rc:$FRT), (ins f4rc:$FRA, f4rc:$FRB),
                        "fdivs", "$FRT, $FRA, $FRB", IIC_FPDivS,
                        [(set f32:$FRT, (any_fdiv f32:$FRA, f32:$FRB))]>;
  let isCommutable = 1 in {
  defm FMUL  : AForm_3r<63, 25,
                        (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRC),
                        "fmul", "$FRT, $FRA, $FRC", IIC_FPFused,
                        [(set f64:$FRT, (any_fmul f64:$FRA, f64:$FRC))]>;
  defm FMULS : AForm_3r<59, 25,
                        (outs f4rc:$FRT), (ins f4rc:$FRA, f4rc:$FRC),
                        "fmuls", "$FRT, $FRA, $FRC", IIC_FPGeneral,
                        [(set f32:$FRT, (any_fmul f32:$FRA, f32:$FRC))]>;
  } // isCommutable
  defm FSUB  : AForm_2r<63, 20,
                        (outs f8rc:$FRT), (ins f8rc:$FRA, f8rc:$FRB),
                        "fsub", "$FRT, $FRA, $FRB", IIC_FPAddSub,
                        [(set f64:$FRT, (any_fsub f64:$FRA, f64:$FRB))]>;
  defm FSUBS : AForm_2r<59, 20,
                        (outs f4rc:$FRT), (ins f4rc:$FRA, f4rc:$FRB),
                        "fsubs", "$FRT, $FRA, $FRB", IIC_FPGeneral,
                        [(set f32:$FRT, (any_fsub f32:$FRA, f32:$FRB))]>;
  }
}

let hasSideEffects = 0 in {
let PPC970_Unit = 1 in {  // FXU Operations.
  let isSelect = 1 in
  def ISEL  : AForm_4<31, 15,
                     (outs gprc:$RT), (ins gprc_nor0:$RA, gprc:$RB, crbitrc:$COND),
                     "isel $RT, $RA, $RB, $COND", IIC_IntISEL,
                     []>;
}

let PPC970_Unit = 1 in {  // FXU Operations.
// M-Form instructions.  rotate and mask instructions.
//
let isCommutable = 1 in {
// RLWIMI can be commuted if the rotate amount is zero.
defm RLWIMI : MForm_2r<20, (outs gprc:$RA),
                       (ins gprc:$RAi, gprc:$RS, u5imm:$SH, u5imm:$MB,
                       u5imm:$ME), "rlwimi", "$RA, $RS, $SH, $MB, $ME",
                       IIC_IntRotate, []>, PPC970_DGroup_Cracked,
                       RegConstraint<"$RAi = $RA">, NoEncode<"$RAi">;
}
let BaseName = "rlwinm" in {
def RLWINM : MForm_2<21,
                     (outs gprc:$RA), (ins gprc:$RS, u5imm:$SH, u5imm:$MB, u5imm:$ME),
                     "rlwinm $RA, $RS, $SH, $MB, $ME", IIC_IntGeneral,
                     []>, RecFormRel;
let Defs = [CR0] in
def RLWINM_rec : MForm_2<21,
                      (outs gprc:$RA), (ins gprc:$RS, u5imm:$SH, u5imm:$MB, u5imm:$ME),
                      "rlwinm. $RA, $RS, $SH, $MB, $ME", IIC_IntGeneral,
                      []>, isRecordForm, RecFormRel, PPC970_DGroup_Cracked;
}
defm RLWNM  : MForm_1r<23, (outs gprc:$RA),
                       (ins gprc:$RS, gprc:$RB, u5imm:$MB, u5imm:$ME),
                       "rlwnm", "$RA, $RS, $RB, $MB, $ME", IIC_IntGeneral,
                       []>;
}
} // hasSideEffects = 0

//===----------------------------------------------------------------------===//
// PowerPC Instruction Patterns
//

// Arbitrary immediate support.  Implement in terms of LIS/ORI.
def : Pat<(i32 imm:$imm),
          (ORI (LIS (HI16 imm:$imm)), (LO16 imm:$imm))>;

// Implement the 'not' operation with the NOR instruction.
def i32not : OutPatFrag<(ops node:$in),
                        (NOR $in, $in)>;
def        : Pat<(not i32:$in),
                 (i32not $in)>;

// ADD an arbitrary immediate.
def : Pat<(add i32:$in, imm:$imm),
          (ADDIS (ADDI $in, (LO16 imm:$imm)), (HA16 imm:$imm))>;
// OR an arbitrary immediate.
def : Pat<(or i32:$in, imm:$imm),
          (ORIS (ORI $in, (LO16 imm:$imm)), (HI16 imm:$imm))>;
// XOR an arbitrary immediate.
def : Pat<(xor i32:$in, imm:$imm),
          (XORIS (XORI $in, (LO16 imm:$imm)), (HI16 imm:$imm))>;
// SUBFIC
def : Pat<(sub imm32SExt16:$imm, i32:$in),
          (SUBFIC $in, imm:$imm)>;

// SHL/SRL
def : Pat<(shl i32:$in, (i32 imm:$imm)),
          (RLWINM $in, imm:$imm, 0, (SHL32 imm:$imm))>;
def : Pat<(srl i32:$in, (i32 imm:$imm)),
          (RLWINM $in, (SRL32 imm:$imm), imm:$imm, 31)>;

// ROTL
def : Pat<(rotl i32:$in, i32:$sh),
          (RLWNM $in, $sh, 0, 31)>;
def : Pat<(rotl i32:$in, (i32 imm:$imm)),
          (RLWINM $in, imm:$imm, 0, 31)>;

// RLWNM
def : Pat<(and (rotl i32:$in, i32:$sh), maskimm32:$imm),
          (RLWNM $in, $sh, (MB maskimm32:$imm), (ME maskimm32:$imm))>;

// Calls
def : Pat<(PPCcall (i32 tglobaladdr:$dst)),
          (BL tglobaladdr:$dst)>;

def : Pat<(PPCcall (i32 texternalsym:$dst)),
          (BL texternalsym:$dst)>;

def : Pat<(PPCcall_rm (i32 tglobaladdr:$dst)),
          (BL_RM tglobaladdr:$dst)>;

def : Pat<(PPCcall_rm (i32 texternalsym:$dst)),
          (BL_RM texternalsym:$dst)>;

// Calls for AIX only
def : Pat<(PPCcall (i32 mcsym:$dst)),
          (BL mcsym:$dst)>;

def : Pat<(PPCcall_nop (i32 mcsym:$dst)),
          (BL_NOP mcsym:$dst)>;

def : Pat<(PPCcall_nop (i32 texternalsym:$dst)),
          (BL_NOP texternalsym:$dst)>;

def : Pat<(PPCcall_rm (i32 mcsym:$dst)),
          (BL_RM mcsym:$dst)>;

def : Pat<(PPCcall_nop_rm (i32 mcsym:$dst)),
          (BL_NOP_RM mcsym:$dst)>;

def : Pat<(PPCcall_nop_rm (i32 texternalsym:$dst)),
          (BL_NOP_RM texternalsym:$dst)>;

def : Pat<(PPCtc_return (i32 tglobaladdr:$dst),  imm:$imm),
          (TCRETURNdi tglobaladdr:$dst, imm:$imm)>;

def : Pat<(PPCtc_return (i32 texternalsym:$dst), imm:$imm),
          (TCRETURNdi texternalsym:$dst, imm:$imm)>;

def : Pat<(PPCtc_return CTRRC:$dst, imm:$imm),
          (TCRETURNri CTRRC:$dst, imm:$imm)>;

def : Pat<(int_ppc_fence), (FENCE)>;
def : Pat<(int_ppc_readflm), (MFFS)>;
def : Pat<(int_ppc_mffsl), (MFFSL)>;

// Hi and Lo for Darwin Global Addresses.
def : Pat<(PPChi tglobaladdr:$in, 0), (LIS tglobaladdr:$in)>;
def : Pat<(PPClo tglobaladdr:$in, 0), (LI tglobaladdr:$in)>;
def : Pat<(PPChi tconstpool:$in, 0), (LIS tconstpool:$in)>;
def : Pat<(PPClo tconstpool:$in, 0), (LI tconstpool:$in)>;
def : Pat<(PPChi tjumptable:$in, 0), (LIS tjumptable:$in)>;
def : Pat<(PPClo tjumptable:$in, 0), (LI tjumptable:$in)>;
def : Pat<(PPChi tblockaddress:$in, 0), (LIS tblockaddress:$in)>;
def : Pat<(PPClo tblockaddress:$in, 0), (LI tblockaddress:$in)>;
def : Pat<(PPChi tglobaltlsaddr:$g, i32:$in),
          (ADDIS $in, tglobaltlsaddr:$g)>;
def : Pat<(PPClo tglobaltlsaddr:$g, i32:$in),
          (ADDI $in, tglobaltlsaddr:$g)>;
def : Pat<(add i32:$in, (PPChi tglobaladdr:$g, 0)),
          (ADDIS $in, tglobaladdr:$g)>;
def : Pat<(add i32:$in, (PPChi tconstpool:$g, 0)),
          (ADDIS $in, tconstpool:$g)>;
def : Pat<(add i32:$in, (PPChi tjumptable:$g, 0)),
          (ADDIS $in, tjumptable:$g)>;
def : Pat<(add i32:$in, (PPChi tblockaddress:$g, 0)),
          (ADDIS $in, tblockaddress:$g)>;

// Support for thread-local storage.
def PPC32GOT: PPCEmitTimePseudo<(outs gprc:$rD), (ins), "#PPC32GOT",
                [(set i32:$rD, (PPCppc32GOT))]>;

// Get the _GLOBAL_OFFSET_TABLE_ in PIC mode.
// This uses two output registers, the first as the real output, the second as a
// temporary register, used internally in code generation.
def PPC32PICGOT: PPCEmitTimePseudo<(outs gprc:$rD, gprc:$rT), (ins), "#PPC32PICGOT",
                []>, NoEncode<"$rT">;

def LDgotTprelL32: PPCEmitTimePseudo<(outs gprc_nor0:$rD), (ins s16imm:$disp, gprc_nor0:$reg),
                           "#LDgotTprelL32",
                           [(set i32:$rD,
                             (PPCldGotTprelL tglobaltlsaddr:$disp, i32:$reg))]>;
def : Pat<(PPCaddTls i32:$in, tglobaltlsaddr:$g),
          (ADD4TLS $in, tglobaltlsaddr:$g)>;

def ADDItlsgdL32 : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc_nor0:$reg, s16imm:$disp),
                         "#ADDItlsgdL32",
                         [(set i32:$rD,
                           (PPCaddiTlsgdL i32:$reg, tglobaltlsaddr:$disp))]>;
// LR is a true define, while the rest of the Defs are clobbers.  R3 is
// explicitly defined when this op is created, so not mentioned here.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [R0,R4,R5,R6,R7,R8,R9,R10,R11,R12,LR,CTR,CR0,CR1,CR5,CR6,CR7] in
def GETtlsADDR32 : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc:$reg, tlsgd32:$sym),
                          "GETtlsADDR32",
                          [(set i32:$rD,
                            (PPCgetTlsAddr i32:$reg, tglobaltlsaddr:$sym))]>;
// R3 is explicitly defined when this op is created, so not mentioned here.
// The rest of the Defs are the exact set of registers that will be clobbered by
// the call.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [R0,R4,R5,R11,LR,CR0] in {
def GETtlsADDR32AIX : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc:$offset, gprc:$handle),
                          "GETtlsADDR32AIX",
                          [(set i32:$rD,
                            (PPCgetTlsAddr i32:$offset, i32:$handle))]>;
def GETtlsMOD32AIX : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc:$handle),
                          "GETtlsMOD32AIX",
                          [(set i32:$rD,
                            (PPCgetTlsMod i32:$handle))]>;
}

// For local-exec accesses on 32-bit AIX, a call to .__get_tpointer is
// generated to retrieve the thread pointer. GETtlsTpointer32AIX clobbers both
// R3 and the LR (link register).
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [R3,LR] in
def GETtlsTpointer32AIX : PPCEmitTimePseudo<(outs gprc:$rD), (ins),
                          "GETtlsTpointer32AIX",
                          [(set i32:$rD, (PPCgetTpointer))]>;

// The following pattern matches local- and initial-exec TLS accesses on 32-bit AIX.
// PPCaddTls is used in local- and initial-exec accesses in order to:
//   - Get the address of a variable (add the variable offset to the thread
//     pointer, retrieved by calling .__get_tpointer).
//   - Create an opportunity to optimize the user of the loaded address.
def : Pat<(PPCaddTls i32:$in, i32:$addr),
          (ADD4TLS $in, $addr)>;

// Combined op for ADDItlsgdL32 and GETtlsADDR32, late expanded.  R3 and LR
// are true defines while the rest of the Defs are clobbers.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [R0,R3,R4,R5,R6,R7,R8,R9,R10,R11,R12,LR,CTR,CR0,CR1,CR5,CR6,CR7] in
def ADDItlsgdLADDR32 : PPCEmitTimePseudo<(outs gprc:$rD),
                              (ins gprc_nor0:$reg, s16imm:$disp, tlsgd32:$sym),
                              "#ADDItlsgdLADDR32",
                              [(set i32:$rD,
                                (PPCaddiTlsgdLAddr i32:$reg,
                                                   tglobaltlsaddr:$disp,
                                                   tglobaltlsaddr:$sym))]>;
def ADDItlsldL32 : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc_nor0:$reg, s16imm:$disp),
                          "#ADDItlsldL32",
                          [(set i32:$rD,
                            (PPCaddiTlsldL i32:$reg, tglobaltlsaddr:$disp))]>;
// This pseudo is expanded to two copies to put the variable offset in R4 and
// the region handle in R3 and GETtlsADDR32AIX.
def TLSGDAIX : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc:$offset, gprc:$handle),
                          "#TLSGDAIX",
                          [(set i32:$rD,
                            (PPCTlsgdAIX i32:$offset, i32:$handle))]>;
// This pseudo is expanded to the call to GETtlsMOD32AIX.
def TLSLDAIX : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc:$handle),
                          "#TLSLDAIX", [(set i32:$rD, (PPCTlsldAIX i32:$handle))]>;
// LR is a true define, while the rest of the Defs are clobbers.  R3 is
// explicitly defined when this op is created, so not mentioned here.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [R0,R4,R5,R6,R7,R8,R9,R10,R11,R12,LR,CTR,CR0,CR1,CR5,CR6,CR7] in
def GETtlsldADDR32 : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc:$reg, tlsgd32:$sym),
                            "GETtlsldADDR32",
                            [(set i32:$rD,
                              (PPCgetTlsldAddr i32:$reg,
                                               tglobaltlsaddr:$sym))]>;
// Combined op for ADDItlsldL32 and GETtlsADDR32, late expanded.  R3 and LR
// are true defines while the rest of the Defs are clobbers.
let hasExtraSrcRegAllocReq = 1, hasExtraDefRegAllocReq = 1,
    Defs = [R0,R3,R4,R5,R6,R7,R8,R9,R10,R11,R12,LR,CTR,CR0,CR1,CR5,CR6,CR7] in
def ADDItlsldLADDR32 : PPCEmitTimePseudo<(outs gprc:$rD),
                              (ins gprc_nor0:$reg, s16imm:$disp, tlsgd32:$sym),
                              "#ADDItlsldLADDR32",
                              [(set i32:$rD,
                                (PPCaddiTlsldLAddr i32:$reg,
                                                   tglobaltlsaddr:$disp,
                                                   tglobaltlsaddr:$sym))]>;
def ADDIdtprelL32 : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc_nor0:$reg, s16imm:$disp),
                           "#ADDIdtprelL32",
                           [(set i32:$rD,
                             (PPCaddiDtprelL i32:$reg, tglobaltlsaddr:$disp))]>;
def ADDISdtprelHA32 : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc_nor0:$reg, s16imm:$disp),
                            "#ADDISdtprelHA32",
                            [(set i32:$rD,
                              (PPCaddisDtprelHA i32:$reg,
                                                tglobaltlsaddr:$disp))]>;

// Support for Position-independent code
def LWZtoc : PPCEmitTimePseudo<(outs gprc:$rD), (ins tocentry32:$disp, gprc:$reg),
                   "#LWZtoc",
                   [(set i32:$rD,
                     (PPCtoc_entry tglobaladdr:$disp, i32:$reg))]>;
def LWZtocL : PPCEmitTimePseudo<(outs gprc:$rD), (ins tocentry32:$disp, gprc_nor0:$reg),
                    "#LWZtocL",
                    [(set i32:$rD,
                      (PPCtoc_entry tglobaladdr:$disp, i32:$reg))]>;
def ADDIStocHA : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc_nor0:$reg, tocentry32:$disp),
                       "#ADDIStocHA", []>;
// TOC Data Transform on AIX
def ADDItoc : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc:$reg, tocentry32:$disp),
                   "#ADDItoc", []>;
def ADDItocL : PPCEmitTimePseudo<(outs gprc:$rD), (ins gprc_nor0:$reg, tocentry32:$disp),
                   "#ADDItocL", []>;

// Get Global (GOT) Base Register offset, from the word immediately preceding
// the function label.
def UpdateGBR : PPCEmitTimePseudo<(outs gprc:$rD, gprc:$rT), (ins gprc:$rI), "#UpdateGBR", []>;

// Pseudo-instruction marked for deletion. When deleting the instruction would
// cause iterator invalidation in MIR transformation passes, this pseudo can be
// used instead. It will be removed unconditionally at pre-emit time (prior to
// branch selection).
def UNENCODED_NOP: PPCEmitTimePseudo<(outs), (ins), "#UNENCODED_NOP", []>;

// Standard shifts.  These are represented separately from the real shifts above
// so that we can distinguish between shifts that allow 5-bit and 6-bit shift
// amounts.
def : Pat<(sra i32:$rS, i32:$rB),
          (SRAW $rS, $rB)>;
def : Pat<(srl i32:$rS, i32:$rB),
          (SRW $rS, $rB)>;
def : Pat<(shl i32:$rS, i32:$rB),
          (SLW $rS, $rB)>;

def : Pat<(i32 (zextloadi1 DForm:$src)),
          (LBZ DForm:$src)>;
def : Pat<(i32 (zextloadi1 XForm:$src)),
          (LBZX XForm:$src)>;
def : Pat<(i32 (extloadi1 DForm:$src)),
          (LBZ DForm:$src)>;
def : Pat<(i32 (extloadi1 XForm:$src)),
          (LBZX XForm:$src)>;
def : Pat<(i32 (extloadi8 DForm:$src)),
          (LBZ DForm:$src)>;
def : Pat<(i32 (extloadi8 XForm:$src)),
          (LBZX XForm:$src)>;
def : Pat<(i32 (extloadi16 DForm:$src)),
          (LHZ DForm:$src)>;
def : Pat<(i32 (extloadi16 XForm:$src)),
          (LHZX XForm:$src)>;
let Predicates = [HasFPU] in {
def : Pat<(f64 (extloadf32 DForm:$src)),
          (COPY_TO_REGCLASS (LFS DForm:$src), F8RC)>;
def : Pat<(f64 (extloadf32 XForm:$src)),
          (COPY_TO_REGCLASS (LFSX XForm:$src), F8RC)>;

def : Pat<(f64 (any_fpextend f32:$src)),
          (COPY_TO_REGCLASS $src, F8RC)>;
}

// Only seq_cst fences require the heavyweight sync (SYNC 0).
// All others can use the lightweight sync (SYNC 1).
// source: http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
// The rule for seq_cst is duplicated to work with both 64 bits and 32 bits
// versions of Power.
def : Pat<(atomic_fence (i64 7), (timm)), (SYNC 0)>, Requires<[HasSYNC]>;
def : Pat<(atomic_fence (i32 7), (timm)), (SYNC 0)>, Requires<[HasSYNC]>;
def : Pat<(atomic_fence (timm), (timm)), (SYNC 1)>, Requires<[HasSYNC]>;
def : Pat<(atomic_fence (timm), (timm)), (MSYNC)>, Requires<[HasOnlyMSYNC]>;

let Predicates = [HasFPU] in {
// Additional fnmsub patterns for custom node
def : Pat<(PPCfnmsub f64:$A, f64:$B, f64:$C),
          (FNMSUB $A, $B, $C)>;
def : Pat<(PPCfnmsub f32:$A, f32:$B, f32:$C),
          (FNMSUBS $A, $B, $C)>;
def : Pat<(fneg (PPCfnmsub f64:$A, f64:$B, f64:$C)),
          (FMSUB $A, $B, $C)>;
def : Pat<(fneg (PPCfnmsub f32:$A, f32:$B, f32:$C)),
          (FMSUBS $A, $B, $C)>;
def : Pat<(PPCfnmsub f64:$A, f64:$B, (fneg f64:$C)),
          (FNMADD $A, $B, $C)>;
def : Pat<(PPCfnmsub f32:$A, f32:$B, (fneg f32:$C)),
          (FNMADDS $A, $B, $C)>;

// FCOPYSIGN's operand types need not agree.
def : Pat<(fcopysign f64:$frB, f32:$frA),
          (FCPSGND (COPY_TO_REGCLASS $frA, F8RC), $frB)>;
def : Pat<(fcopysign f32:$frB, f64:$frA),
          (FCPSGNS (COPY_TO_REGCLASS $frA, F4RC), $frB)>;
}

// XL Compat intrinsics.
def : Pat<(int_ppc_fmsub f64:$A, f64:$B, f64:$C), (FMSUB $A, $B, $C)>;
def : Pat<(int_ppc_fmsubs f32:$A, f32:$B, f32:$C), (FMSUBS $A, $B, $C)>;
def : Pat<(int_ppc_fnmadd f64:$A, f64:$B, f64:$C), (FNMADD $A, $B, $C)>;
def : Pat<(int_ppc_fnmadds f32:$A, f32:$B, f32:$C), (FNMADDS $A, $B, $C)>;
def : Pat<(int_ppc_fre f64:$A), (FRE $A)>;
def : Pat<(int_ppc_fres f32:$A), (FRES $A)>;
def : Pat<(int_ppc_fnabs f64:$A), (FNABSD $A)>;
def : Pat<(int_ppc_fnabss f32:$A), (FNABSS $A)>;

include "PPCInstrAltivec.td"
include "PPCInstrSPE.td"
include "PPCInstr64Bit.td"
include "PPCInstrVSX.td"
include "PPCInstrHTM.td"

def crnot : OutPatFrag<(ops node:$in),
                       (CRNOT $in)>;
def       : Pat<(not i1:$in),
                (crnot $in)>;

// Pseudo-instructions for alternate assembly syntax (never used by codegen).
// These are aliases that require C++ handling to convert to the target
// instruction, while InstAliases can be handled directly by tblgen.
class PPCAsmPseudo<string asm, dag iops>
  : Instruction {
  let Namespace = "PPC";
  bit PPC64 = 0;  // Default value, override with isPPC64

  let OutOperandList = (outs);
  let InOperandList = iops;
  let Pattern = [];
  let AsmString = asm;
  let isAsmParserOnly = 1;
  let isPseudo = 1;
  let hasNoSchedulingInfo = 1;
}

// Prefixed instructions may require access to the above defs at a later
// time so we include this after the def.
include "PPCInstrP10.td"
include "PPCInstrFutureMMA.td"
include "PPCInstrFuture.td"
include "PPCInstrMMA.td"
include "PPCInstrDFP.td"

// Patterns for arithmetic i1 operations.
def : Pat<(add i1:$a, i1:$b),
          (CRXOR $a, $b)>;
def : Pat<(sub i1:$a, i1:$b),
          (CRXOR $a, $b)>;
def : Pat<(mul i1:$a, i1:$b),
          (CRAND $a, $b)>;

// We're sometimes asked to materialize i1 -1, which is just 1 in this case
// (-1 is used to mean all bits set).
def : Pat<(i1 -1), (CRSET)>;

// i1 extensions, implemented in terms of isel.
def : Pat<(i32 (zext i1:$in)),
          (SELECT_I4 $in, (LI 1), (LI 0))>;
def : Pat<(i32 (sext i1:$in)),
          (SELECT_I4 $in, (LI -1), (LI 0))>;

def : Pat<(i64 (zext i1:$in)),
          (SELECT_I8 $in, (LI8 1), (LI8 0))>;
def : Pat<(i64 (sext i1:$in)),
          (SELECT_I8 $in, (LI8 -1), (LI8 0))>;

// FIXME: We should choose either a zext or a sext based on other constants
// already around.
def : Pat<(i32 (anyext i1:$in)),
          (SELECT_I4 $in, (LI 1), (LI 0))>;
def : Pat<(i64 (anyext i1:$in)),
          (SELECT_I8 $in, (LI8 1), (LI8 0))>;

// match setcc on i1 variables.
// CRANDC is:
//   1 1 : F
//   1 0 : T
//   0 1 : F
//   0 0 : F
//
// LT is:
//  -1 -1  : F
//  -1  0  : T
//   0 -1  : F
//   0  0  : F
//
// ULT is:
//   1 1 : F
//   1 0 : F
//   0 1 : T
//   0 0 : F
def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETLT)),
          (CRANDC $s1, $s2)>;
def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETULT)),
          (CRANDC $s2, $s1)>;
// CRORC is:
//   1 1 : T
//   1 0 : T
//   0 1 : F
//   0 0 : T
//
// LE is:
//  -1 -1 : T
//  -1  0 : T
//   0 -1 : F
//   0  0 : T
//
// ULE is:
//   1 1 : T
//   1 0 : F
//   0 1 : T
//   0 0 : T
def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETLE)),
          (CRORC $s1, $s2)>;
def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETULE)),
          (CRORC $s2, $s1)>;

def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETEQ)),
          (CREQV $s1, $s2)>;

// GE is:
//  -1 -1 : T
//  -1  0 : F
//   0 -1 : T
//   0  0 : T
//
// UGE is:
//   1 1 : T
//   1 0 : T
//   0 1 : F
//   0 0 : T
def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETGE)),
          (CRORC $s2, $s1)>;
def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETUGE)),
          (CRORC $s1, $s2)>;

// GT is:
//  -1 -1 : F
//  -1  0 : F
//   0 -1 : T
//   0  0 : F
//
// UGT is:
//  1 1 : F
//  1 0 : T
//  0 1 : F
//  0 0 : F
def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETGT)),
          (CRANDC $s2, $s1)>;
def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETUGT)),
          (CRANDC $s1, $s2)>;

def : Pat<(i1 (setcc i1:$s1, i1:$s2, SETNE)),
          (CRXOR $s1, $s2)>;

// match setcc on non-i1 (non-vector) variables. Note that SETUEQ, SETOGE,
// SETOLE, SETONE, SETULT and SETUGT should be expanded by legalize for
// floating-point types.

multiclass CRNotPat<dag pattern, dag result> {
  def : Pat<pattern, (crnot result)>;
  def : Pat<(not pattern), result>;

  // We can also fold the crnot into an extension:
  def : Pat<(i32 (zext pattern)),
            (SELECT_I4 result, (LI 0), (LI 1))>;
  def : Pat<(i32 (sext pattern)),
            (SELECT_I4 result, (LI 0), (LI -1))>;

  // We can also fold the crnot into an extension:
  def : Pat<(i64 (zext pattern)),
            (SELECT_I8 result, (LI8 0), (LI8 1))>;
  def : Pat<(i64 (sext pattern)),
            (SELECT_I8 result, (LI8 0), (LI8 -1))>;

  // FIXME: We should choose either a zext or a sext based on other constants
  // already around.
  def : Pat<(i32 (anyext pattern)),
            (SELECT_I4 result, (LI 0), (LI 1))>;

  def : Pat<(i64 (anyext pattern)),
            (SELECT_I8 result, (LI8 0), (LI8 1))>;
}

// FIXME: Because of what seems like a bug in TableGen's type-inference code,
// we need to write imm:$imm in the output patterns below, not just $imm, or
// else the resulting matcher will not correctly add the immediate operand
// (making it a register operand instead).

// extended SETCC.
multiclass ExtSetCCPat<CondCode cc, PatFrag pfrag,
                       OutPatFrag rfrag, OutPatFrag rfrag8> {
  def : Pat<(i32 (zext (i1 (pfrag i32:$s1, cc)))),
            (rfrag $s1)>;
  def : Pat<(i64 (zext (i1 (pfrag i64:$s1, cc)))),
            (rfrag8 $s1)>;
  def : Pat<(i64 (zext (i1 (pfrag i32:$s1, cc)))),
            (INSERT_SUBREG (i64 (IMPLICIT_DEF)), (rfrag $s1), sub_32)>;
  def : Pat<(i32 (zext (i1 (pfrag i64:$s1, cc)))),
            (EXTRACT_SUBREG (rfrag8 $s1), sub_32)>;

  def : Pat<(i32 (anyext (i1 (pfrag i32:$s1, cc)))),
            (rfrag $s1)>;
  def : Pat<(i64 (anyext (i1 (pfrag i64:$s1, cc)))),
            (rfrag8 $s1)>;
  def : Pat<(i64 (anyext (i1 (pfrag i32:$s1, cc)))),
            (INSERT_SUBREG (i64 (IMPLICIT_DEF)), (rfrag $s1), sub_32)>;
  def : Pat<(i32 (anyext (i1 (pfrag i64:$s1, cc)))),
            (EXTRACT_SUBREG (rfrag8 $s1), sub_32)>;
}

// Note that we do all inversions below with i(32|64)not, instead of using
// (xori x, 1) because on the A2 nor has single-cycle latency while xori
// has 2-cycle latency.

defm : ExtSetCCPat<SETEQ,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, 0, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM (CNTLZW $in), 27, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL (CNTLZD $in), 58, 63)> >;

defm : ExtSetCCPat<SETNE,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, 0, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM (i32not (CNTLZW $in)), 27, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL (i64not (CNTLZD $in)), 58, 63)> >;

defm : ExtSetCCPat<SETLT,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, 0, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM $in, 1, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL $in, 1, 63)> >;

defm : ExtSetCCPat<SETGE,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, 0, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM (i32not $in), 1, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL (i64not $in), 1, 63)> >;

defm : ExtSetCCPat<SETGT,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, 0, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM (ANDC (NEG $in), $in), 1, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL (ANDC8 (NEG8 $in), $in), 1, 63)> >;

defm : ExtSetCCPat<SETLE,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, 0, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM (ORC $in, (NEG $in)), 1, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL (ORC8 $in, (NEG8 $in)), 1, 63)> >;

defm : ExtSetCCPat<SETLT,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, -1, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM (AND $in, (ADDI $in, 1)), 1, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL (AND8 $in, (ADDI8 $in, 1)), 1, 63)> >;

defm : ExtSetCCPat<SETGE,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, -1, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM (NAND $in, (ADDI $in, 1)), 1, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL (NAND8 $in, (ADDI8 $in, 1)), 1, 63)> >;

defm : ExtSetCCPat<SETGT,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, -1, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM (i32not $in), 1, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL (i64not $in), 1, 63)> >;

defm : ExtSetCCPat<SETLE,
                   PatFrag<(ops node:$in, node:$cc),
                           (setcc $in, -1, $cc)>,
                   OutPatFrag<(ops node:$in),
                              (RLWINM $in, 1, 31, 31)>,
                   OutPatFrag<(ops node:$in),
                              (RLDICL $in, 1, 63)> >;

// An extended SETCC with shift amount.
multiclass ExtSetCCShiftPat<CondCode cc, PatFrag pfrag,
                            OutPatFrag rfrag, OutPatFrag rfrag8> {
  def : Pat<(i32 (zext (i1 (pfrag i32:$s1, i32:$sa, cc)))),
            (rfrag $s1, $sa)>;
  def : Pat<(i64 (zext (i1 (pfrag i64:$s1, i32:$sa, cc)))),
            (rfrag8 $s1, $sa)>;
  def : Pat<(i64 (zext (i1 (pfrag i32:$s1, i32:$sa, cc)))),
            (INSERT_SUBREG (i64 (IMPLICIT_DEF)), (rfrag $s1, $sa), sub_32)>;
  def : Pat<(i32 (zext (i1 (pfrag i64:$s1, i32:$sa, cc)))),
            (EXTRACT_SUBREG (rfrag8 $s1, $sa), sub_32)>;

  def : Pat<(i32 (anyext (i1 (pfrag i32:$s1, i32:$sa, cc)))),
            (rfrag $s1, $sa)>;
  def : Pat<(i64 (anyext (i1 (pfrag i64:$s1, i32:$sa, cc)))),
            (rfrag8 $s1, $sa)>;
  def : Pat<(i64 (anyext (i1 (pfrag i32:$s1, i32:$sa, cc)))),
            (INSERT_SUBREG (i64 (IMPLICIT_DEF)), (rfrag $s1, $sa), sub_32)>;
  def : Pat<(i32 (anyext (i1 (pfrag i64:$s1, i32:$sa, cc)))),
            (EXTRACT_SUBREG (rfrag8 $s1, $sa), sub_32)>;
}

defm : ExtSetCCShiftPat<SETNE,
                        PatFrag<(ops node:$in, node:$sa, node:$cc),
                                (setcc (and $in, (shl 1, $sa)), 0, $cc)>,
                        OutPatFrag<(ops node:$in, node:$sa),
                                   (RLWNM $in, (SUBFIC $sa, 32), 31, 31)>,
                        OutPatFrag<(ops node:$in, node:$sa),
                                   (RLDCL $in, (SUBFIC $sa, 64), 63)> >;

defm : ExtSetCCShiftPat<SETEQ,
                        PatFrag<(ops node:$in, node:$sa, node:$cc),
                                (setcc (and $in, (shl 1, $sa)), 0, $cc)>,
                        OutPatFrag<(ops node:$in, node:$sa),
                                   (RLWNM (i32not $in),
                                          (SUBFIC $sa, 32), 31, 31)>,
                        OutPatFrag<(ops node:$in, node:$sa),
                                   (RLDCL (i64not $in),
                                          (SUBFIC $sa, 64), 63)> >;

// SETCC for i32.
def : Pat<(i1 (setcc i32:$s1, immZExt16:$imm, SETULT)),
          (EXTRACT_SUBREG (CMPLWI $s1, imm:$imm), sub_lt)>;
def : Pat<(i1 (setcc i32:$s1, imm32SExt16:$imm, SETLT)),
          (EXTRACT_SUBREG (CMPWI $s1, imm:$imm), sub_lt)>;
def : Pat<(i1 (setcc i32:$s1, immZExt16:$imm, SETUGT)),
          (EXTRACT_SUBREG (CMPLWI $s1, imm:$imm), sub_gt)>;
def : Pat<(i1 (setcc i32:$s1, imm32SExt16:$imm, SETGT)),
          (EXTRACT_SUBREG (CMPWI $s1, imm:$imm), sub_gt)>;
def : Pat<(i1 (setcc i32:$s1, imm32SExt16:$imm, SETEQ)),
          (EXTRACT_SUBREG (CMPWI $s1, imm:$imm), sub_eq)>;
def : Pat<(i1 (setcc i32:$s1, immZExt16:$imm, SETEQ)),
          (EXTRACT_SUBREG (CMPLWI $s1, imm:$imm), sub_eq)>;

// For non-equality comparisons, the default code would materialize the
// constant, then compare against it, like this:
//   lis r2, 4660
//   ori r2, r2, 22136
//   cmpw cr0, r3, r2
//   beq cr0,L6
// Since we are just comparing for equality, we can emit this instead:
//   xoris r0,r3,0x1234
//   cmplwi cr0,r0,0x5678
//   beq cr0,L6

def : Pat<(i1 (setcc i32:$s1, imm:$imm, SETEQ)),
          (EXTRACT_SUBREG (CMPLWI (XORIS $s1, (HI16 imm:$imm)),
                                  (LO16 imm:$imm)), sub_eq)>;

def : Pat<(i1 (setcc i32:$s1, i32:$s2, SETULT)),
          (EXTRACT_SUBREG (CMPLW $s1, $s2), sub_lt)>;
def : Pat<(i1 (setcc i32:$s1, i32:$s2, SETLT)),
          (EXTRACT_SUBREG (CMPW $s1, $s2), sub_lt)>;
def : Pat<(i1 (setcc i32:$s1, i32:$s2, SETUGT)),
          (EXTRACT_SUBREG (CMPLW $s1, $s2), sub_gt)>;
def : Pat<(i1 (setcc i32:$s1, i32:$s2, SETGT)),
          (EXTRACT_SUBREG (CMPW $s1, $s2), sub_gt)>;
def : Pat<(i1 (setcc i32:$s1, i32:$s2, SETEQ)),
          (EXTRACT_SUBREG (CMPW $s1, $s2), sub_eq)>;

// SETCC for i64.
def : Pat<(i1 (setcc i64:$s1, immZExt16:$imm, SETULT)),
          (EXTRACT_SUBREG (CMPLDI $s1, imm:$imm), sub_lt)>;
def : Pat<(i1 (setcc i64:$s1, imm64SExt16:$imm, SETLT)),
          (EXTRACT_SUBREG (CMPDI $s1, imm:$imm), sub_lt)>;
def : Pat<(i1 (setcc i64:$s1, immZExt16:$imm, SETUGT)),
          (EXTRACT_SUBREG (CMPLDI $s1, imm:$imm), sub_gt)>;
def : Pat<(i1 (setcc i64:$s1, imm64SExt16:$imm, SETGT)),
          (EXTRACT_SUBREG (CMPDI $s1, imm:$imm), sub_gt)>;
def : Pat<(i1 (setcc i64:$s1, imm64SExt16:$imm, SETEQ)),
          (EXTRACT_SUBREG (CMPDI $s1, imm:$imm), sub_eq)>;
def : Pat<(i1 (setcc i64:$s1, immZExt16:$imm, SETEQ)),
          (EXTRACT_SUBREG (CMPLDI $s1, imm:$imm), sub_eq)>;

// For non-equality comparisons, the default code would materialize the
// constant, then compare against it, like this:
//   lis r2, 4660
//   ori r2, r2, 22136
//   cmpd cr0, r3, r2
//   beq cr0,L6
// Since we are just comparing for equality, we can emit this instead:
//   xoris r0,r3,0x1234
//   cmpldi cr0,r0,0x5678
//   beq cr0,L6

def : Pat<(i1 (setcc i64:$s1, imm64ZExt32:$imm, SETEQ)),
          (EXTRACT_SUBREG (CMPLDI (XORIS8 $s1, (HI16 imm:$imm)),
                                  (LO16 imm:$imm)), sub_eq)>;

def : Pat<(i1 (setcc i64:$s1, i64:$s2, SETULT)),
          (EXTRACT_SUBREG (CMPLD $s1, $s2), sub_lt)>;
def : Pat<(i1 (setcc i64:$s1, i64:$s2, SETLT)),
          (EXTRACT_SUBREG (CMPD $s1, $s2), sub_lt)>;
def : Pat<(i1 (setcc i64:$s1, i64:$s2, SETUGT)),
          (EXTRACT_SUBREG (CMPLD $s1, $s2), sub_gt)>;
def : Pat<(i1 (setcc i64:$s1, i64:$s2, SETGT)),
          (EXTRACT_SUBREG (CMPD $s1, $s2), sub_gt)>;
def : Pat<(i1 (setcc i64:$s1, i64:$s2, SETEQ)),
          (EXTRACT_SUBREG (CMPD $s1, $s2), sub_eq)>;

let Predicates = [IsNotISA3_1] in {
// Instantiations of CRNotPat for i32.
defm : CRNotPat<(i1 (setcc i32:$s1, immZExt16:$imm, SETUGE)),
                (EXTRACT_SUBREG (CMPLWI $s1, imm:$imm), sub_lt)>;
defm : CRNotPat<(i1 (setcc i32:$s1, imm32SExt16:$imm, SETGE)),
                (EXTRACT_SUBREG (CMPWI $s1, imm:$imm), sub_lt)>;
defm : CRNotPat<(i1 (setcc i32:$s1, immZExt16:$imm, SETULE)),
                (EXTRACT_SUBREG (CMPLWI $s1, imm:$imm), sub_gt)>;
defm : CRNotPat<(i1 (setcc i32:$s1, imm32SExt16:$imm, SETLE)),
                (EXTRACT_SUBREG (CMPWI $s1, imm:$imm), sub_gt)>;
defm : CRNotPat<(i1 (setcc i32:$s1, imm32SExt16:$imm, SETNE)),
                (EXTRACT_SUBREG (CMPWI $s1, imm:$imm), sub_eq)>;
defm : CRNotPat<(i1 (setcc i32:$s1, immZExt16:$imm, SETNE)),
                (EXTRACT_SUBREG (CMPLWI $s1, imm:$imm), sub_eq)>;

defm : CRNotPat<(i1 (setcc i32:$s1, imm:$imm, SETNE)),
                (EXTRACT_SUBREG (CMPLWI (XORIS $s1, (HI16 imm:$imm)),
                                        (LO16 imm:$imm)), sub_eq)>;

defm : CRNotPat<(i1 (setcc i32:$s1, i32:$s2, SETUGE)),
                (EXTRACT_SUBREG (CMPLW $s1, $s2), sub_lt)>;
defm : CRNotPat<(i1 (setcc i32:$s1, i32:$s2, SETGE)),
                (EXTRACT_SUBREG (CMPW $s1, $s2), sub_lt)>;
defm : CRNotPat<(i1 (setcc i32:$s1, i32:$s2, SETULE)),
                (EXTRACT_SUBREG (CMPLW $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (setcc i32:$s1, i32:$s2, SETLE)),
                (EXTRACT_SUBREG (CMPW $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (setcc i32:$s1, i32:$s2, SETNE)),
                (EXTRACT_SUBREG (CMPW $s1, $s2), sub_eq)>;

// Instantiations of CRNotPat for i64.
defm : CRNotPat<(i1 (setcc i64:$s1, immZExt16:$imm, SETUGE)),
                (EXTRACT_SUBREG (CMPLDI $s1, imm:$imm), sub_lt)>;
defm : CRNotPat<(i1 (setcc i64:$s1, imm64SExt16:$imm, SETGE)),
                (EXTRACT_SUBREG (CMPDI $s1, imm:$imm), sub_lt)>;
defm : CRNotPat<(i1 (setcc i64:$s1, immZExt16:$imm, SETULE)),
                (EXTRACT_SUBREG (CMPLDI $s1, imm:$imm), sub_gt)>;
defm : CRNotPat<(i1 (setcc i64:$s1, imm64SExt16:$imm, SETLE)),
                (EXTRACT_SUBREG (CMPDI $s1, imm:$imm), sub_gt)>;
defm : CRNotPat<(i1 (setcc i64:$s1, imm64SExt16:$imm, SETNE)),
                (EXTRACT_SUBREG (CMPDI $s1, imm:$imm), sub_eq)>;
defm : CRNotPat<(i1 (setcc i64:$s1, immZExt16:$imm, SETNE)),
                (EXTRACT_SUBREG (CMPLDI $s1, imm:$imm), sub_eq)>;

defm : CRNotPat<(i1 (setcc i64:$s1, imm64ZExt32:$imm, SETNE)),
                (EXTRACT_SUBREG (CMPLDI (XORIS8 $s1, (HI16 imm:$imm)),
                                        (LO16 imm:$imm)), sub_eq)>;

defm : CRNotPat<(i1 (setcc i64:$s1, i64:$s2, SETUGE)),
                (EXTRACT_SUBREG (CMPLD $s1, $s2), sub_lt)>;
defm : CRNotPat<(i1 (setcc i64:$s1, i64:$s2, SETGE)),
                (EXTRACT_SUBREG (CMPD $s1, $s2), sub_lt)>;
defm : CRNotPat<(i1 (setcc i64:$s1, i64:$s2, SETULE)),
                (EXTRACT_SUBREG (CMPLD $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (setcc i64:$s1, i64:$s2, SETLE)),
                (EXTRACT_SUBREG (CMPD $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (setcc i64:$s1, i64:$s2, SETNE)),
                (EXTRACT_SUBREG (CMPD $s1, $s2), sub_eq)>;
}

multiclass FSetCCPat<SDPatternOperator SetCC, ValueType Ty, I FCmp> {
  defm : CRNotPat<(i1 (SetCC Ty:$s1, Ty:$s2, SETUGE)),
                  (EXTRACT_SUBREG (FCmp $s1, $s2), sub_lt)>;
  defm : CRNotPat<(i1 (SetCC Ty:$s1, Ty:$s2, SETGE)),
                  (EXTRACT_SUBREG (FCmp $s1, $s2), sub_lt)>;
  defm : CRNotPat<(i1 (SetCC Ty:$s1, Ty:$s2, SETULE)),
                  (EXTRACT_SUBREG (FCmp $s1, $s2), sub_gt)>;
  defm : CRNotPat<(i1 (SetCC Ty:$s1, Ty:$s2, SETLE)),
                  (EXTRACT_SUBREG (FCmp $s1, $s2), sub_gt)>;
  defm : CRNotPat<(i1 (SetCC Ty:$s1, Ty:$s2, SETUNE)),
                  (EXTRACT_SUBREG (FCmp $s1, $s2), sub_eq)>;
  defm : CRNotPat<(i1 (SetCC Ty:$s1, Ty:$s2, SETNE)),
                  (EXTRACT_SUBREG (FCmp $s1, $s2), sub_eq)>;
  defm : CRNotPat<(i1 (SetCC Ty:$s1, Ty:$s2, SETO)),
                  (EXTRACT_SUBREG (FCmp $s1, $s2), sub_un)>;

  def : Pat<(i1 (SetCC Ty:$s1, Ty:$s2, SETOLT)),
            (EXTRACT_SUBREG (FCmp $s1, $s2), sub_lt)>;
  def : Pat<(i1 (SetCC Ty:$s1, Ty:$s2, SETLT)),
            (EXTRACT_SUBREG (FCmp $s1, $s2), sub_lt)>;
  def : Pat<(i1 (SetCC Ty:$s1, Ty:$s2, SETOGT)),
            (EXTRACT_SUBREG (FCmp $s1, $s2), sub_gt)>;
  def : Pat<(i1 (SetCC Ty:$s1, Ty:$s2, SETGT)),
            (EXTRACT_SUBREG (FCmp $s1, $s2), sub_gt)>;
  def : Pat<(i1 (SetCC Ty:$s1, Ty:$s2, SETOEQ)),
            (EXTRACT_SUBREG (FCmp $s1, $s2), sub_eq)>;
  def : Pat<(i1 (SetCC Ty:$s1, Ty:$s2, SETEQ)),
            (EXTRACT_SUBREG (FCmp $s1, $s2), sub_eq)>;
  def : Pat<(i1 (SetCC Ty:$s1, Ty:$s2, SETUO)),
            (EXTRACT_SUBREG (FCmp $s1, $s2), sub_un)>;
}

let Predicates = [HasFPU] in {
// FCMPU: If either of the operands is a Signaling NaN, then VXSNAN is set.
// SETCC for f32.
defm : FSetCCPat<any_fsetcc, f32, FCMPUS>;

// SETCC for f64.
defm : FSetCCPat<any_fsetcc, f64, FCMPUD>;

// SETCC for f128.
defm : FSetCCPat<any_fsetcc, f128, XSCMPUQP>;

// FCMPO: If either of the operands is a Signaling NaN, then VXSNAN is set and,
// if neither operand is a Signaling NaN but at least one operand is a Quiet NaN,
// then VXVC is set.
// SETCCS for f32.
defm : FSetCCPat<strict_fsetccs, f32, FCMPOS>;

// SETCCS for f64.
defm : FSetCCPat<strict_fsetccs, f64, FCMPOD>;

// SETCCS for f128.
defm : FSetCCPat<strict_fsetccs, f128, XSCMPOQP>;
}

// This must be in this file because it relies on patterns defined in this file
// after the inclusion of the instruction sets.
let Predicates = [HasSPE] in {
// SETCC for f32.
def : Pat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETOLT)),
          (EXTRACT_SUBREG (EFSCMPLT $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETLT)),
          (EXTRACT_SUBREG (EFSCMPLT $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETOGT)),
          (EXTRACT_SUBREG (EFSCMPGT $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETGT)),
          (EXTRACT_SUBREG (EFSCMPGT $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETOEQ)),
          (EXTRACT_SUBREG (EFSCMPEQ $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETEQ)),
          (EXTRACT_SUBREG (EFSCMPEQ $s1, $s2), sub_gt)>;

defm : CRNotPat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETUGE)),
                (EXTRACT_SUBREG (EFSCMPLT $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETGE)),
                (EXTRACT_SUBREG (EFSCMPLT $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETULE)),
                (EXTRACT_SUBREG (EFSCMPGT $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETLE)),
                (EXTRACT_SUBREG (EFSCMPGT $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETUNE)),
                (EXTRACT_SUBREG (EFSCMPEQ $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f32:$s1, f32:$s2, SETNE)),
                (EXTRACT_SUBREG (EFSCMPEQ $s1, $s2), sub_gt)>;

// SETCC for f64.
def : Pat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETOLT)),
          (EXTRACT_SUBREG (EFDCMPLT $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETLT)),
          (EXTRACT_SUBREG (EFDCMPLT $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETOGT)),
          (EXTRACT_SUBREG (EFDCMPGT $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETGT)),
          (EXTRACT_SUBREG (EFDCMPGT $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETOEQ)),
          (EXTRACT_SUBREG (EFDCMPEQ $s1, $s2), sub_gt)>;
def : Pat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETEQ)),
          (EXTRACT_SUBREG (EFDCMPEQ $s1, $s2), sub_gt)>;

defm : CRNotPat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETUGE)),
                (EXTRACT_SUBREG (EFDCMPLT $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETGE)),
                (EXTRACT_SUBREG (EFDCMPLT $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETULE)),
                (EXTRACT_SUBREG (EFDCMPGT $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETLE)),
                (EXTRACT_SUBREG (EFDCMPGT $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETUNE)),
                (EXTRACT_SUBREG (EFDCMPEQ $s1, $s2), sub_gt)>;
defm : CRNotPat<(i1 (any_fsetccs f64:$s1, f64:$s2, SETNE)),
                (EXTRACT_SUBREG (EFDCMPEQ $s1, $s2), sub_gt)>;
}
// match select on i1 variables:
def : Pat<(i1 (select i1:$cond, i1:$tval, i1:$fval)),
          (CROR (CRAND        $cond , $tval),
                (CRAND (crnot $cond), $fval))>;

// match selectcc on i1 variables:
//   select (lhs == rhs), tval, fval is:
//   ((lhs == rhs) & tval) | (!(lhs == rhs) & fval)
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETLT)),
           (CROR (CRAND (CRANDC $lhs, $rhs), $tval),
                 (CRAND (CRORC  $rhs, $lhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETULT)),
           (CROR (CRAND (CRANDC $rhs, $lhs), $tval),
                 (CRAND (CRORC  $lhs, $rhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETLE)),
           (CROR (CRAND (CRORC  $lhs, $rhs), $tval),
                 (CRAND (CRANDC $rhs, $lhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETULE)),
           (CROR (CRAND (CRORC  $rhs, $lhs), $tval),
                 (CRAND (CRANDC $lhs, $rhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETEQ)),
           (CROR (CRAND (CREQV $lhs, $rhs), $tval),
                 (CRAND (CRXOR $lhs, $rhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETGE)),
           (CROR (CRAND (CRORC  $rhs, $lhs), $tval),
                 (CRAND (CRANDC $lhs, $rhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETUGE)),
           (CROR (CRAND (CRORC  $lhs, $rhs), $tval),
                 (CRAND (CRANDC $rhs, $lhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETGT)),
           (CROR (CRAND (CRANDC $rhs, $lhs), $tval),
                 (CRAND (CRORC  $lhs, $rhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETUGT)),
           (CROR (CRAND (CRANDC $lhs, $rhs), $tval),
                 (CRAND (CRORC  $rhs, $lhs), $fval))>;
def : Pat <(i1 (selectcc i1:$lhs, i1:$rhs, i1:$tval, i1:$fval, SETNE)),
           (CROR (CRAND (CREQV $lhs, $rhs), $fval),
                 (CRAND (CRXOR $lhs, $rhs), $tval))>;

// match selectcc on i1 variables with non-i1 output.
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETLT)),
          (SELECT_I4 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETULT)),
          (SELECT_I4 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETLE)),
          (SELECT_I4 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETULE)),
          (SELECT_I4 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETEQ)),
          (SELECT_I4 (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETGE)),
          (SELECT_I4 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETUGE)),
          (SELECT_I4 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETGT)),
          (SELECT_I4 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETUGT)),
          (SELECT_I4 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(i32 (selectcc i1:$lhs, i1:$rhs, i32:$tval, i32:$fval, SETNE)),
          (SELECT_I4 (CRXOR $lhs, $rhs), $tval, $fval)>;

def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETLT)),
          (SELECT_I8 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETULT)),
          (SELECT_I8 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETLE)),
          (SELECT_I8 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETULE)),
          (SELECT_I8 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETEQ)),
          (SELECT_I8 (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETGE)),
          (SELECT_I8 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETUGE)),
          (SELECT_I8 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETGT)),
          (SELECT_I8 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETUGT)),
          (SELECT_I8 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(i64 (selectcc i1:$lhs, i1:$rhs, i64:$tval, i64:$fval, SETNE)),
          (SELECT_I8 (CRXOR $lhs, $rhs), $tval, $fval)>;

let Predicates = [HasFPU] in {
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETLT)),
          (SELECT_F4 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETULT)),
          (SELECT_F4 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETLE)),
          (SELECT_F4 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETULE)),
          (SELECT_F4 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETEQ)),
          (SELECT_F4 (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETGE)),
          (SELECT_F4 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETUGE)),
          (SELECT_F4 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETGT)),
          (SELECT_F4 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETUGT)),
          (SELECT_F4 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f32 (selectcc i1:$lhs, i1:$rhs, f32:$tval, f32:$fval, SETNE)),
          (SELECT_F4 (CRXOR $lhs, $rhs), $tval, $fval)>;

def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETLT)),
          (SELECT_F8 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETULT)),
          (SELECT_F8 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETLE)),
          (SELECT_F8 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETULE)),
          (SELECT_F8 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETEQ)),
          (SELECT_F8 (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETGE)),
          (SELECT_F8 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETUGE)),
          (SELECT_F8 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETGT)),
          (SELECT_F8 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETUGT)),
          (SELECT_F8 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f64 (selectcc i1:$lhs, i1:$rhs, f64:$tval, f64:$fval, SETNE)),
          (SELECT_F8 (CRXOR $lhs, $rhs), $tval, $fval)>;
}

def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETLT)),
          (SELECT_F16 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETULT)),
          (SELECT_F16 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETLE)),
          (SELECT_F16 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETULE)),
          (SELECT_F16 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETEQ)),
          (SELECT_F16 (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETGE)),
         (SELECT_F16 (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETUGE)),
          (SELECT_F16 (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETGT)),
          (SELECT_F16 (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETUGT)),
          (SELECT_F16 (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(f128 (selectcc i1:$lhs, i1:$rhs, f128:$tval, f128:$fval, SETNE)),
          (SELECT_F16 (CRXOR $lhs, $rhs), $tval, $fval)>;

def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETLT)),
          (SELECT_VRRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETULT)),
          (SELECT_VRRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETLE)),
          (SELECT_VRRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETULE)),
          (SELECT_VRRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETEQ)),
          (SELECT_VRRC (CREQV $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETGE)),
          (SELECT_VRRC (CRORC  $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETUGE)),
          (SELECT_VRRC (CRORC  $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETGT)),
          (SELECT_VRRC (CRANDC $rhs, $lhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETUGT)),
          (SELECT_VRRC (CRANDC $lhs, $rhs), $tval, $fval)>;
def : Pat<(v4i32 (selectcc i1:$lhs, i1:$rhs, v4i32:$tval, v4i32:$fval, SETNE)),
          (SELECT_VRRC (CRXOR $lhs, $rhs), $tval, $fval)>;

let Defs = [CR0] in {
def ANDI_rec_1_EQ_BIT : PPCCustomInserterPseudo<(outs crbitrc:$dst), (ins gprc:$in),
                             "#ANDI_rec_1_EQ_BIT",
                             [(set i1:$dst, (trunc (not i32:$in)))]>;
def ANDI_rec_1_GT_BIT : PPCCustomInserterPseudo<(outs crbitrc:$dst), (ins gprc:$in),
                             "#ANDI_rec_1_GT_BIT",
                             [(set i1:$dst, (trunc i32:$in))]>;

def ANDI_rec_1_EQ_BIT8 : PPCCustomInserterPseudo<(outs crbitrc:$dst), (ins g8rc:$in),
                              "#ANDI_rec_1_EQ_BIT8",
                              [(set i1:$dst, (trunc (not i64:$in)))]>;
def ANDI_rec_1_GT_BIT8 : PPCCustomInserterPseudo<(outs crbitrc:$dst), (ins g8rc:$in),
                              "#ANDI_rec_1_GT_BIT8",
                              [(set i1:$dst, (trunc i64:$in))]>;
}

def : Pat<(i1 (not (trunc i32:$in))),
           (ANDI_rec_1_EQ_BIT $in)>;
def : Pat<(i1 (not (trunc i64:$in))),
           (ANDI_rec_1_EQ_BIT8 $in)>;

def : Pat<(int_ppc_fsel f8rc:$FRA, f8rc:$FRC, f8rc:$FRB), (FSELD $FRA, $FRC, $FRB)>;
def : Pat<(int_ppc_frsqrte f8rc:$frB), (FRSQRTE $frB)>;
def : Pat<(int_ppc_frsqrtes f4rc:$frB), (FRSQRTES $frB)>;

//===----------------------------------------------------------------------===//
// PowerPC Instructions used for assembler/disassembler only
//

// FIXME: For B=0 or B > 8, the registers following RT are used.
// WARNING: Do not add patterns for this instruction without fixing this.
def LSWI  : XForm_base_r3xo_memOp<31, 597, (outs gprc:$RST),
                                  (ins gprc:$RA, u5imm:$RB),
                                  "lswi $RST, $RA, $RB", IIC_LdStLoad, []>;

// FIXME: For B=0 or B > 8, the registers following RT are used.
// WARNING: Do not add patterns for this instruction without fixing this.
def STSWI : XForm_base_r3xo_memOp<31, 725, (outs),
                                  (ins gprc:$RST, gprc:$RA, u5imm:$RB),
                                  "stswi $RST, $RA, $RB", IIC_LdStLoad, []>;

def ISYNC : XLForm_2_ext<19, 150, 0, 0, 0, (outs), (ins),
                         "isync", IIC_SprISYNC, []>;

def ICBI : XForm_1a<31, 982, (outs), (ins (memrr $RA, $RB):$addr),
                    "icbi $addr", IIC_LdStICBI, []>;

def WAIT : XForm_24_sync<31, 30, (outs), (ins u2imm:$L),
                         "wait $L", IIC_LdStLoad, []>;

def MBAR : XForm_mbar<31, 854, (outs), (ins u5imm:$MO),
                         "mbar $MO", IIC_LdStLoad>, Requires<[IsBookE]>;

def MTSR: XForm_sr<31, 210, (outs), (ins gprc:$RS, u4imm:$SR),
            "mtsr $SR, $RS", IIC_SprMTSR>;

def MFSR: XForm_sr<31, 595, (outs gprc:$RS), (ins u4imm:$SR),
            "mfsr $RS, $SR", IIC_SprMFSR>;

def MTSRIN: XForm_srin<31, 242, (outs), (ins gprc:$RS, gprc:$RB),
            "mtsrin $RS, $RB", IIC_SprMTSR>;

def MFSRIN: XForm_srin<31, 659, (outs gprc:$RS), (ins gprc:$RB),
            "mfsrin $RS, $RB", IIC_SprMFSR>;

def MTMSR: XForm_mtmsr<31, 146, (outs), (ins gprc:$RS, u1imm:$L),
                    "mtmsr $RS, $L", IIC_SprMTMSR>;

def WRTEE: XForm_mtmsr<31, 131, (outs), (ins gprc:$RS),
                    "wrtee $RS", IIC_SprMTMSR>, Requires<[IsBookE]> {
  let L = 0;
}

def WRTEEI: I<31, (outs), (ins i1imm:$E), "wrteei $E", IIC_SprMTMSR>,
              Requires<[IsBookE]> {
  bits<1> E;

  let Inst{16} = E;
  let Inst{21-30} = 163;
}

def DCCCI : XForm_tlb<454, (outs), (ins gprc:$RA, gprc:$RB),
               "dccci $RA, $RB", IIC_LdStLoad>, Requires<[IsPPC4xx]>;
def ICCCI : XForm_tlb<966, (outs), (ins gprc:$RA, gprc:$RB),
               "iccci $RA, $RB", IIC_LdStLoad>, Requires<[IsPPC4xx]>;

def : InstAlias<"dci 0", (DCCCI R0, R0)>, Requires<[IsPPC4xx]>;
def : InstAlias<"dccci", (DCCCI R0, R0)>, Requires<[IsPPC4xx]>;
def : InstAlias<"ici 0", (ICCCI R0, R0)>, Requires<[IsPPC4xx]>;
def : InstAlias<"iccci", (ICCCI R0, R0)>, Requires<[IsPPC4xx]>;

def MFMSR : XForm_rs<31, 83, (outs gprc:$RST), (ins),
                  "mfmsr $RST", IIC_SprMFMSR, []>;

def MTMSRD : XForm_mtmsr<31, 178, (outs), (ins gprc:$RS, u1imm:$L),
                    "mtmsrd $RS, $L", IIC_SprMTMSRD>;

def MCRFS : XLForm_3<63, 64, (outs crrc:$BF), (ins crrc:$BFA),
                     "mcrfs $BF, $BFA", IIC_BrMCR>;

// All MTFSF variants may change the rounding mode so conservatively set it
// as an implicit def for all of them.
let Predicates = [HasFPU] in {
let Defs = [RM], hasSideEffects = 1 in {
let isCodeGenOnly = 1,
    Pattern = [(int_ppc_mtfsfi timm:$BF, timm:$U)], W = 0 in
def MTFSFIb : XLForm_4<63, 134, (outs), (ins u3imm:$BF, u4imm:$U),
                       "mtfsfi $BF, $U", IIC_IntMFFS>;
def MTFSFI : XLForm_4<63, 134, (outs), (ins u3imm:$BF, u4imm:$U, i32imm:$W),
                      "mtfsfi $BF, $U, $W", IIC_IntMFFS>;
let Defs = [CR1] in
def MTFSFI_rec : XLForm_4<63, 134, (outs), (ins u3imm:$BF, u4imm:$U, u1imm:$W),
                       "mtfsfi. $BF, $U, $W", IIC_IntMFFS>, isRecordForm;

def MTFSF : XFLForm_1<63, 711, (outs),
                      (ins i32imm:$FLM, f8rc:$FRB, u1imm:$L, i32imm:$W),
                      "mtfsf $FLM, $FRB, $L, $W", IIC_IntMFFS, []>;
let Defs = [CR1] in
def MTFSF_rec : XFLForm_1<63, 711, (outs),
                       (ins i32imm:$FLM, f8rc:$FRB, u1imm:$L, i32imm:$W),
                       "mtfsf. $FLM, $FRB, $L, $W", IIC_IntMFFS, []>, isRecordForm;
}

def : InstAlias<"mtfsfi $BF, $U", (MTFSFI u3imm:$BF, u4imm:$U, 0)>;
def : InstAlias<"mtfsfi. $BF, $U", (MTFSFI_rec u3imm:$BF, u4imm:$U, 0)>;
def : InstAlias<"mtfsf $FLM, $FRB", (MTFSF i32imm:$FLM, f8rc:$FRB, 0, 0)>;
def : InstAlias<"mtfsf. $FLM, $FRB", (MTFSF_rec i32imm:$FLM, f8rc:$FRB, 0, 0)>;
}

def SLBIE : XForm_16b<31, 434, (outs), (ins gprc:$RB),
                        "slbie $RB", IIC_SprSLBIE, []>;

def SLBMTE : XForm_26<31, 402, (outs), (ins gprc:$RST, gprc:$RB),
                    "slbmte $RST, $RB", IIC_SprSLBMTE, []>;

def SLBMFEE : XForm_26<31, 915, (outs gprc:$RST), (ins gprc:$RB),
                       "slbmfee $RST, $RB", IIC_SprSLBMFEE, []>;

def SLBMFEV : XLForm_1_gen<31, 851, (outs gprc:$RT), (ins gprc:$RB),
                       "slbmfev $RT, $RB", IIC_SprSLBMFEV, []>;

def SLBIA : XForm_0<31, 498, (outs), (ins), "slbia", IIC_SprSLBIA, []>;

let Defs = [CR0] in
def SLBFEE_rec : XForm_26<31, 979, (outs gprc:$RST), (ins gprc:$RB),
                         "slbfee. $RST, $RB", IIC_SprSLBFEE, []>, isRecordForm;

def TLBIA : XForm_0<31, 370, (outs), (ins),
                        "tlbia", IIC_SprTLBIA, []>;

def TLBSYNC : XForm_0<31, 566, (outs), (ins),
                        "tlbsync", IIC_SprTLBSYNC, []>;

def TLBIEL : XForm_16b<31, 274, (outs), (ins gprc:$RB),
                          "tlbiel $RB", IIC_SprTLBIEL, []>;

def TLBLD : XForm_16b<31, 978, (outs), (ins gprc:$RB),
                          "tlbld $RB", IIC_LdStLoad, []>, Requires<[IsPPC6xx]>;
def TLBLI : XForm_16b<31, 1010, (outs), (ins gprc:$RB),
                          "tlbli $RB", IIC_LdStLoad, []>, Requires<[IsPPC6xx]>;

def TLBIE : XForm_26<31, 306, (outs), (ins gprc:$RST, gprc:$RB),
                          "tlbie $RB,$RST", IIC_SprTLBIE, []>;

def TLBSX : XForm_tlb<914, (outs), (ins gprc:$RA, gprc:$RB), "tlbsx $RA, $RB",
                IIC_LdStLoad>, Requires<[IsBookE]>;

def TLBIVAX : XForm_tlb<786, (outs), (ins gprc:$RA, gprc:$RB), "tlbivax $RA, $RB",
                IIC_LdStLoad>, Requires<[IsBookE]>;

def TLBILX : XForm_tlbilx<18, (outs), (ins u2imm:$T, gprc:$RA, gprc:$RB),
    "tlbilx $T, $RA, $RB", IIC_LdStLoad>, Requires<[IsBookE]>;

def TLBRE : XForm_24_eieio<31, 946, (outs), (ins),
                           "tlbre", IIC_LdStLoad, []>, Requires<[IsBookE]>;

def TLBWE : XForm_24_eieio<31, 978, (outs), (ins),
                           "tlbwe", IIC_LdStLoad, []>, Requires<[IsBookE]>;

def TLBRE2 : XForm_tlbws<31, 946, (outs gprc:$RST), (ins gprc:$RA, i1imm:$WS),
               "tlbre $RST, $RA, $WS", IIC_LdStLoad, []>, Requires<[IsPPC4xx]>;

def TLBWE2 : XForm_tlbws<31, 978, (outs), (ins gprc:$RST, gprc:$RA, i1imm:$WS),
               "tlbwe $RST, $RA, $WS", IIC_LdStLoad, []>, Requires<[IsPPC4xx]>;

def TLBSX2 : XForm_base_r3xo<31, 914, (outs), (ins gprc:$RST, gprc:$RA, gprc:$RB),
                             "tlbsx $RST, $RA, $RB", IIC_LdStLoad, []>,
                             Requires<[IsPPC4xx]>;
def TLBSX2D : XForm_base_r3xo<31, 914, (outs),
                              (ins gprc:$RST, gprc:$RA, gprc:$RB),
                              "tlbsx. $RST, $RA, $RB", IIC_LdStLoad, []>,
                              Requires<[IsPPC4xx]>, isRecordForm;

def RFID : XForm_0<19, 18, (outs), (ins), "rfid", IIC_IntRFID, []>;

def RFI : XForm_0<19, 50, (outs), (ins), "rfi", IIC_SprRFI, []>,
                  Requires<[IsBookE]>;
def RFCI : XForm_0<19, 51, (outs), (ins), "rfci", IIC_BrB, []>,
                   Requires<[IsBookE]>;

def RFDI : XForm_0<19, 39, (outs), (ins), "rfdi", IIC_BrB, []>,
                   Requires<[IsE500]>;
def RFMCI : XForm_0<19, 38, (outs), (ins), "rfmci", IIC_BrB, []>,
                    Requires<[IsE500]>;

def MFDCR : XFXForm_1<31, 323, (outs gprc:$RST), (ins i32imm:$SPR),
                      "mfdcr $RST, $SPR", IIC_SprMFSPR>, Requires<[IsPPC4xx]>;
def MTDCR : XFXForm_1<31, 451, (outs), (ins gprc:$RST, i32imm:$SPR),
                      "mtdcr $SPR, $RST", IIC_SprMTSPR>, Requires<[IsPPC4xx]>;

def HRFID : XLForm_1_np<19, 274, (outs), (ins), "hrfid", IIC_BrB, []>;
def NAP   : XLForm_1_np<19, 434, (outs), (ins), "nap", IIC_BrB, []>;

def ATTN : XForm_attn<0, 256, (outs), (ins), "attn", IIC_BrB>;

def LBZCIX : XForm_base_r3xo_memOp<31, 853, (outs gprc:$RST),
                                  (ins gprc:$RA, gprc:$RB),
                                  "lbzcix $RST, $RA, $RB", IIC_LdStLoad, []>;
def LHZCIX : XForm_base_r3xo_memOp<31, 821, (outs gprc:$RST),
                                  (ins gprc:$RA, gprc:$RB),
                                  "lhzcix $RST, $RA, $RB", IIC_LdStLoad, []>;
def LWZCIX : XForm_base_r3xo_memOp<31, 789, (outs gprc:$RST),
                                  (ins gprc:$RA, gprc:$RB),
                                  "lwzcix $RST, $RA, $RB", IIC_LdStLoad, []>;
def LDCIX :  XForm_base_r3xo_memOp<31, 885, (outs gprc:$RST),
                                  (ins gprc:$RA, gprc:$RB),
                                  "ldcix $RST, $RA, $RB", IIC_LdStLoad, []>;

def STBCIX : XForm_base_r3xo_memOp<31, 981, (outs),
                                  (ins gprc:$RST, gprc:$RA, gprc:$RB),
                                  "stbcix $RST, $RA, $RB", IIC_LdStLoad, []>;
def STHCIX : XForm_base_r3xo_memOp<31, 949, (outs),
                                  (ins gprc:$RST, gprc:$RA, gprc:$RB),
                                  "sthcix $RST, $RA, $RB", IIC_LdStLoad, []>;
def STWCIX : XForm_base_r3xo_memOp<31, 917, (outs),
                                  (ins gprc:$RST, gprc:$RA, gprc:$RB),
                                  "stwcix $RST, $RA, $RB", IIC_LdStLoad, []>;
def STDCIX : XForm_base_r3xo_memOp<31, 1013, (outs),
                                  (ins gprc:$RST, gprc:$RA, gprc:$RB),
                                  "stdcix $RST, $RA, $RB", IIC_LdStLoad, []>;

// External PID Load Store Instructions

def LBEPX   : XForm_1<31, 95, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                      "lbepx $RST, $addr", IIC_LdStLoad, []>,
                      Requires<[IsE500]>;

def LFDEPX  : XForm_25<31, 607, (outs f8rc:$RST), (ins (memrr $RA, $RB):$addr),
                      "lfdepx $RST, $addr", IIC_LdStLFD, []>,
                      Requires<[IsE500]>;

def LHEPX   : XForm_1<31, 287, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                      "lhepx $RST, $addr", IIC_LdStLoad, []>,
                      Requires<[IsE500]>;

def LWEPX   : XForm_1<31, 31, (outs gprc:$RST), (ins (memrr $RA, $RB):$addr),
                      "lwepx $RST, $addr", IIC_LdStLoad, []>,
                      Requires<[IsE500]>;

def STBEPX  : XForm_8<31, 223, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                      "stbepx $RST, $addr", IIC_LdStStore, []>,
                      Requires<[IsE500]>;

def STFDEPX : XForm_28_memOp<31, 735, (outs), (ins f8rc:$RST, (memrr $RA, $RB):$addr),
                      "stfdepx $RST, $addr", IIC_LdStSTFD, []>,
                      Requires<[IsE500]>;

def STHEPX  : XForm_8<31, 415, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                      "sthepx $RST, $addr", IIC_LdStStore, []>,
                      Requires<[IsE500]>;

def STWEPX  : XForm_8<31, 159, (outs), (ins gprc:$RST, (memrr $RA, $RB):$addr),
                      "stwepx $RST, $addr", IIC_LdStStore, []>,
                      Requires<[IsE500]>;

def DCBFEP  : DCB_Form<127, 0, (outs), (ins (memrr $RA, $RB):$addr), "dcbfep $addr",
                      IIC_LdStDCBF, []>, Requires<[IsE500]>;

def DCBSTEP : DCB_Form<63, 0, (outs), (ins (memrr $RA, $RB):$addr), "dcbstep $addr",
                      IIC_LdStDCBF, []>, Requires<[IsE500]>;

def DCBTEP  : DCB_Form_hint<319, (outs), (ins (memrr $RA, $RB):$addr, u5imm:$TH),
                      "dcbtep $TH, $addr", IIC_LdStDCBF, []>,
                      Requires<[IsE500]>;

def DCBTSTEP : DCB_Form_hint<255, (outs), (ins (memrr $RA, $RB):$addr, u5imm:$TH),
                      "dcbtstep $TH, $addr", IIC_LdStDCBF, []>,
                      Requires<[IsE500]>;

def DCBZEP  : DCB_Form<1023, 0, (outs), (ins (memrr $RA, $RB):$addr), "dcbzep $addr",
                      IIC_LdStDCBF, []>, Requires<[IsE500]>;

def DCBZLEP : DCB_Form<1023, 1, (outs), (ins (memrr $RA, $RB):$addr), "dcbzlep $addr",
                      IIC_LdStDCBF, []>, Requires<[IsE500]>;

def ICBIEP  : XForm_1a<31, 991, (outs), (ins (memrr $RA, $RB):$addr), "icbiep $addr",
                      IIC_LdStICBI, []>, Requires<[IsE500]>;

//===----------------------------------------------------------------------===//
// PowerPC Assembler Instruction Aliases
//

def : InstAlias<"sc", (SC 0)>;

def : InstAlias<"sync", (SYNC 0)>, Requires<[HasSYNC]>;
def : InstAlias<"hwsync", (SYNC 0), 0>, Requires<[HasSYNC]>;
def : InstAlias<"msync", (SYNC 0), 0>, Requires<[HasSYNC]>;
def : InstAlias<"lwsync", (SYNC 1)>, Requires<[HasSYNC]>;
def : InstAlias<"ptesync", (SYNC 2)>, Requires<[HasSYNC]>;

def : InstAlias<"wait", (WAIT 0)>;
def : InstAlias<"waitrsv", (WAIT 1)>;
def : InstAlias<"waitimpl", (WAIT 2)>;

def : InstAlias<"mbar", (MBAR 0)>, Requires<[IsBookE]>;

def DCBTx   : PPCAsmPseudo<"dcbt $dst", (ins memrr:$dst)>;
def DCBTSTx : PPCAsmPseudo<"dcbtst $dst", (ins memrr:$dst)>;

def DCBTCT : PPCAsmPseudo<"dcbtct $dst, $TH", (ins memrr:$dst, u5imm:$TH)>;
def DCBTDS : PPCAsmPseudo<"dcbtds $dst, $TH", (ins memrr:$dst, u5imm:$TH)>;
def DCBTT  : PPCAsmPseudo<"dcbtt $dst", (ins memrr:$dst)>;

def DCBTSTCT : PPCAsmPseudo<"dcbtstct $dst, $TH", (ins memrr:$dst, u5imm:$TH)>;
def DCBTSTDS : PPCAsmPseudo<"dcbtstds $dst, $TH", (ins memrr:$dst, u5imm:$TH)>;
def DCBTSTT  : PPCAsmPseudo<"dcbtstt $dst", (ins memrr:$dst)>;

def DCBFx  : PPCAsmPseudo<"dcbf $dst", (ins memrr:$dst)>;
def DCBFL  : PPCAsmPseudo<"dcbfl $dst", (ins memrr:$dst)>;
def DCBFLP : PPCAsmPseudo<"dcbflp $dst", (ins memrr:$dst)>;

def : Pat<(int_ppc_isync),  (ISYNC)>;
def : Pat<(int_ppc_dcbfl xoaddr:$dst),
          (DCBF 1, xoaddr:$dst)>;
def : Pat<(int_ppc_dcbflp xoaddr:$dst),
          (DCBF 3, xoaddr:$dst)>;

let Predicates = [IsISA3_1] in {
  def DCBFPS  : PPCAsmPseudo<"dcbfps $dst", (ins memrr:$dst)>;
  def DCBSTPS : PPCAsmPseudo<"dcbstps $dst", (ins memrr:$dst)>;

  def : Pat<(int_ppc_dcbfps xoaddr:$dst),
            (DCBF 4, xoaddr:$dst)>;
  def : Pat<(int_ppc_dcbstps xoaddr:$dst),
            (DCBF 6, xoaddr:$dst)>;
}

def : InstAlias<"crset $bx", (CREQV crbitrc:$bx, crbitrc:$bx, crbitrc:$bx)>;
def : InstAlias<"crclr $bx", (CRXOR crbitrc:$bx, crbitrc:$bx, crbitrc:$bx)>;
def : InstAlias<"crmove $bx, $by", (CROR crbitrc:$bx, crbitrc:$by, crbitrc:$by)>;
def : InstAlias<"crnot $bx, $by", (CRNOR crbitrc:$bx, crbitrc:$by, crbitrc:$by)>;

def : InstAlias<"mftb $Rx", (MFTB gprc:$Rx, 268)>;
def : InstAlias<"mftbl $Rx", (MFTB gprc:$Rx, 268)>;
def : InstAlias<"mftbu $Rx", (MFTB gprc:$Rx, 269)>;

def : InstAlias<"xnop", (XORI R0, R0, 0)>;

def : InstAlias<"mtxer $Rx", (MTSPR 1, gprc:$Rx)>;
def : InstAlias<"mfxer $Rx", (MFSPR gprc:$Rx, 1)>;

//Disable this alias on AIX for now because as does not support them.
let Predicates = [ModernAs] in {

foreach BR = 0-7 in {
    def : InstAlias<"mfbr"#BR#" $Rx",
                    (MFDCR gprc:$Rx, !add(BR, 0x80))>,
                    Requires<[IsPPC4xx]>;
    def : InstAlias<"mtbr"#BR#" $Rx",
                    (MTDCR gprc:$Rx, !add(BR, 0x80))>,
                    Requires<[IsPPC4xx]>;
}

def : InstAlias<"mtmsrd $RS", (MTMSRD gprc:$RS, 0)>;
def : InstAlias<"mtmsr $RS", (MTMSR gprc:$RS, 0)>;
def : InstAlias<"mtudscr $Rx", (MTSPR 3, gprc:$Rx)>;
def : InstAlias<"mfudscr $Rx", (MFSPR gprc:$Rx, 3)>;

def : InstAlias<"mfrtcu $Rx", (MFSPR gprc:$Rx, 4)>;
def : InstAlias<"mfrtcl $Rx", (MFSPR gprc:$Rx, 5)>;

def : InstAlias<"mtlr $Rx", (MTSPR 8, gprc:$Rx)>;
def : InstAlias<"mflr $Rx", (MFSPR gprc:$Rx, 8)>;

def : InstAlias<"mtctr $Rx", (MTSPR 9, gprc:$Rx)>;
def : InstAlias<"mfctr $Rx", (MFSPR gprc:$Rx, 9)>;

def : InstAlias<"mtuamr $Rx", (MTSPR 13, gprc:$Rx)>;
def : InstAlias<"mfuamr $Rx", (MFSPR gprc:$Rx, 13)>;

def : InstAlias<"mtdscr $Rx", (MTSPR 17, gprc:$Rx)>;
def : InstAlias<"mfdscr $Rx", (MFSPR gprc:$Rx, 17)>;

def : InstAlias<"mtdsisr $Rx", (MTSPR 18, gprc:$Rx)>;
def : InstAlias<"mfdsisr $Rx", (MFSPR gprc:$Rx, 18)>;

def : InstAlias<"mtdar $Rx", (MTSPR 19, gprc:$Rx)>;
def : InstAlias<"mfdar $Rx", (MFSPR gprc:$Rx, 19)>;

def : InstAlias<"mtdec $Rx", (MTSPR 22, gprc:$Rx)>;
def : InstAlias<"mfdec $Rx", (MFSPR gprc:$Rx, 22)>;

def : InstAlias<"mtsdr1 $Rx", (MTSPR 25, gprc:$Rx)>;
def : InstAlias<"mfsdr1 $Rx", (MFSPR gprc:$Rx, 25)>;

def : InstAlias<"mtsrr0 $Rx", (MTSPR 26, gprc:$Rx)>;
def : InstAlias<"mfsrr0 $Rx", (MFSPR gprc:$Rx, 26)>;

def : InstAlias<"mtsrr1 $Rx", (MTSPR 27, gprc:$Rx)>;
def : InstAlias<"mfsrr1 $Rx", (MFSPR gprc:$Rx, 27)>;

def : InstAlias<"mtcfar $Rx", (MTSPR 28, gprc:$Rx)>;
def : InstAlias<"mfcfar $Rx", (MFSPR gprc:$Rx, 28)>;

def : InstAlias<"mtamr $Rx", (MTSPR 29, gprc:$Rx)>;
def : InstAlias<"mfamr $Rx", (MFSPR gprc:$Rx, 29)>;

def : InstAlias<"mtpid $Rx", (MTSPR 48, gprc:$Rx)>, Requires<[IsBookE]>;
def : InstAlias<"mfpid $Rx", (MFSPR gprc:$Rx, 48)>, Requires<[IsBookE]>;

foreach SPRG = 4-7 in {
  def : InstAlias<"mfsprg $RT, "#SPRG, (MFSPR gprc:$RT, !add(SPRG, 256))>,
                  Requires<[IsBookE]>;
  def : InstAlias<"mfsprg"#SPRG#" $RT", (MFSPR gprc:$RT, !add(SPRG, 256))>,
                  Requires<[IsBookE]>;
  def : InstAlias<"mtsprg "#SPRG#", $RT", (MTSPR !add(SPRG, 256), gprc:$RT)>,
                  Requires<[IsBookE]>;
  def : InstAlias<"mtsprg"#SPRG#" $RT", (MTSPR !add(SPRG, 256), gprc:$RT)>,
                  Requires<[IsBookE]>;
}

foreach SPRG = 0-3 in {
  def : InstAlias<"mfsprg $RT, "#SPRG, (MFSPR gprc:$RT, !add(SPRG, 272))>;
  def : InstAlias<"mfsprg"#SPRG#" $RT", (MFSPR gprc:$RT, !add(SPRG, 272))>;
  def : InstAlias<"mtsprg "#SPRG#", $RT", (MTSPR !add(SPRG, 272), gprc:$RT)>;
  def : InstAlias<"mtsprg"#SPRG#" $RT", (MTSPR !add(SPRG, 272), gprc:$RT)>;
}

def : InstAlias<"mfasr $RT", (MFSPR gprc:$RT, 280)>;
def : InstAlias<"mtasr $RT", (MTSPR 280, gprc:$RT)>;

def : InstAlias<"mttbl $Rx", (MTSPR 284, gprc:$Rx)>;
def : InstAlias<"mttbu $Rx", (MTSPR 285, gprc:$Rx)>;

def : InstAlias<"mfpvr $RT", (MFSPR gprc:$RT, 287)>;

def : InstAlias<"mfspefscr $Rx", (MFSPR gprc:$Rx, 512)>;
def : InstAlias<"mtspefscr $Rx", (MTSPR 512, gprc:$Rx)>;

foreach BATR = 0-3 in {
    def : InstAlias<"mtdbatu "#BATR#", $Rx",
                    (MTSPR !add(BATR, !add(BATR, 536)), gprc:$Rx)>,
                    Requires<[IsPPC6xx]>;
    def : InstAlias<"mfdbatu $Rx, "#BATR,
                    (MFSPR gprc:$Rx, !add(BATR, !add(BATR, 536)))>,
                    Requires<[IsPPC6xx]>;
    def : InstAlias<"mtdbatl "#BATR#", $Rx",
                    (MTSPR !add(BATR, !add(BATR, 537)), gprc:$Rx)>,
                    Requires<[IsPPC6xx]>;
    def : InstAlias<"mfdbatl $Rx, "#BATR,
                    (MFSPR gprc:$Rx, !add(BATR, !add(BATR, 537)))>,
                    Requires<[IsPPC6xx]>;
    def : InstAlias<"mtibatu "#BATR#", $Rx",
                    (MTSPR !add(BATR, !add(BATR, 528)), gprc:$Rx)>,
                    Requires<[IsPPC6xx]>;
    def : InstAlias<"mfibatu $Rx, "#BATR,
                    (MFSPR gprc:$Rx, !add(BATR, !add(BATR, 528)))>,
                    Requires<[IsPPC6xx]>;
    def : InstAlias<"mtibatl "#BATR#", $Rx",
                    (MTSPR !add(BATR, !add(BATR, 529)), gprc:$Rx)>,
                    Requires<[IsPPC6xx]>;
    def : InstAlias<"mfibatl $Rx, "#BATR,
                    (MFSPR gprc:$Rx, !add(BATR, !add(BATR, 529)))>,
                    Requires<[IsPPC6xx]>;
}

def : InstAlias<"mtppr $RT", (MTSPR 896, gprc:$RT)>;
def : InstAlias<"mfppr $RT", (MFSPR gprc:$RT, 896)>;

def : InstAlias<"mtesr $Rx", (MTSPR 980, gprc:$Rx)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mfesr $Rx", (MFSPR gprc:$Rx, 980)>, Requires<[IsPPC4xx]>;

def : InstAlias<"mtdear $Rx", (MTSPR 981, gprc:$Rx)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mfdear $Rx", (MFSPR gprc:$Rx, 981)>, Requires<[IsPPC4xx]>;

def : InstAlias<"mttcr $Rx", (MTSPR 986, gprc:$Rx)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mftcr $Rx", (MFSPR gprc:$Rx, 986)>, Requires<[IsPPC4xx]>;

def : InstAlias<"mftbhi $Rx", (MFSPR gprc:$Rx, 988)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mttbhi $Rx", (MTSPR 988, gprc:$Rx)>, Requires<[IsPPC4xx]>;

def : InstAlias<"mftblo $Rx", (MFSPR gprc:$Rx, 989)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mttblo $Rx", (MTSPR 989, gprc:$Rx)>, Requires<[IsPPC4xx]>;

def : InstAlias<"mtsrr2 $Rx", (MTSPR 990, gprc:$Rx)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mfsrr2 $Rx", (MFSPR gprc:$Rx, 990)>, Requires<[IsPPC4xx]>;

def : InstAlias<"mtsrr3 $Rx", (MTSPR 991, gprc:$Rx)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mfsrr3 $Rx", (MFSPR gprc:$Rx, 991)>, Requires<[IsPPC4xx]>;

def : InstAlias<"mtdccr $Rx", (MTSPR 1018, gprc:$Rx)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mfdccr $Rx", (MFSPR gprc:$Rx, 1018)>, Requires<[IsPPC4xx]>;

def : InstAlias<"mticcr $Rx", (MTSPR 1019, gprc:$Rx)>, Requires<[IsPPC4xx]>;
def : InstAlias<"mficcr $Rx", (MFSPR gprc:$Rx, 1019)>, Requires<[IsPPC4xx]>;

}

def : InstAlias<"tlbie $RB", (TLBIE R0, gprc:$RB)>;

def : InstAlias<"tlbrehi $RS, $A", (TLBRE2 gprc:$RS, gprc:$A, 0)>,
                Requires<[IsPPC4xx]>;
def : InstAlias<"tlbrelo $RS, $A", (TLBRE2 gprc:$RS, gprc:$A, 1)>,
                Requires<[IsPPC4xx]>;
def : InstAlias<"tlbwehi $RS, $A", (TLBWE2 gprc:$RS, gprc:$A, 0)>,
                Requires<[IsPPC4xx]>;
def : InstAlias<"tlbwelo $RS, $A", (TLBWE2 gprc:$RS, gprc:$A, 1)>,
                Requires<[IsPPC4xx]>;

def : InstAlias<"tlbilxlpid", (TLBILX 0, R0, R0)>, Requires<[IsBookE]>;
def : InstAlias<"tlbilxpid", (TLBILX 1, R0, R0)>, Requires<[IsBookE]>;
def : InstAlias<"tlbilxva $RA, $RB", (TLBILX 3, gprc:$RA, gprc:$RB)>,
                Requires<[IsBookE]>;
def : InstAlias<"tlbilxva $RB", (TLBILX 3, R0, gprc:$RB)>, Requires<[IsBookE]>;

def LAx : PPCAsmPseudo<"la $rA, $addr", (ins gprc:$rA, memri:$addr)>;

def SUBI : PPCAsmPseudo<"subi $rA, $rB, $imm",
                        (ins gprc:$rA, gprc:$rB, s16imm:$imm)>;
def SUBIS : PPCAsmPseudo<"subis $rA, $rB, $imm",
                         (ins gprc:$rA, gprc:$rB, s16imm:$imm)>;
def SUBIC : PPCAsmPseudo<"subic $rA, $rB, $imm",
                         (ins gprc:$rA, gprc:$rB, s16imm:$imm)>;
def SUBIC_rec : PPCAsmPseudo<"subic. $rA, $rB, $imm",
                          (ins gprc:$rA, gprc:$rB, s16imm:$imm)>;

def EXTLWI : PPCAsmPseudo<"extlwi $rA, $rS, $n, $b",
                          (ins gprc:$rA, gprc:$rS, u5imm:$n, u5imm:$b)>,
                          ZExt32To64;
def EXTLWI_rec : PPCAsmPseudo<"extlwi. $rA, $rS, $n, $b",
                           (ins gprc:$rA, gprc:$rS, u5imm:$n, u5imm:$b)>,
                           ZExt32To64;
def EXTRWI : PPCAsmPseudo<"extrwi $rA, $rS, $n, $b",
                          (ins gprc:$rA, gprc:$rS, u5imm:$n, u5imm:$b)>;
def EXTRWI_rec : PPCAsmPseudo<"extrwi. $rA, $rS, $n, $b",
                           (ins gprc:$rA, gprc:$rS, u5imm:$n, u5imm:$b)>;
def INSLWI : PPCAsmPseudo<"inslwi $rA, $rS, $n, $b",
                          (ins gprc:$rA, gprc:$rS, u5imm:$n, u5imm:$b)>;
def INSLWI_rec : PPCAsmPseudo<"inslwi. $rA, $rS, $n, $b",
                           (ins gprc:$rA, gprc:$rS, u5imm:$n, u5imm:$b)>;
def INSRWI : PPCAsmPseudo<"insrwi $rA, $rS, $n, $b",
                          (ins gprc:$rA, gprc:$rS, u5imm:$n, u5imm:$b)>;
def INSRWI_rec : PPCAsmPseudo<"insrwi. $rA, $rS, $n, $b",
                           (ins gprc:$rA, gprc:$rS, u5imm:$n, u5imm:$b)>;
def ROTRWI : PPCAsmPseudo<"rotrwi $rA, $rS, $n",
                          (ins gprc:$rA, gprc:$rS, u5imm:$n)>, ZExt32To64;
def ROTRWI_rec : PPCAsmPseudo<"rotrwi. $rA, $rS, $n",
                           (ins gprc:$rA, gprc:$rS, u5imm:$n)>, ZExt32To64;
def SLWI : PPCAsmPseudo<"slwi $rA, $rS, $n",
                        (ins gprc:$rA, gprc:$rS, u5imm:$n)>, ZExt32To64;
def SLWI_rec : PPCAsmPseudo<"slwi. $rA, $rS, $n",
                         (ins gprc:$rA, gprc:$rS, u5imm:$n)>, ZExt32To64;
def SRWI : PPCAsmPseudo<"srwi $rA, $rS, $n",
                        (ins gprc:$rA, gprc:$rS, u5imm:$n)>, ZExt32To64;
def SRWI_rec : PPCAsmPseudo<"srwi. $rA, $rS, $n",
                         (ins gprc:$rA, gprc:$rS, u5imm:$n)>, ZExt32To64;
def CLRRWI : PPCAsmPseudo<"clrrwi $rA, $rS, $n",
                          (ins gprc:$rA, gprc:$rS, u5imm:$n)>;
def CLRRWI_rec : PPCAsmPseudo<"clrrwi. $rA, $rS, $n",
                           (ins gprc:$rA, gprc:$rS, u5imm:$n)>;
def CLRLSLWI : PPCAsmPseudo<"clrlslwi $rA, $rS, $b, $n",
                            (ins gprc:$rA, gprc:$rS, u5imm:$b, u5imm:$n)>;
def CLRLSLWI_rec : PPCAsmPseudo<"clrlslwi. $rA, $rS, $b, $n",
                             (ins gprc:$rA, gprc:$rS, u5imm:$b, u5imm:$n)>;

def : InstAlias<"isellt $rT, $rA, $rB",
                (ISEL gprc:$rT, gprc_nor0:$rA, gprc:$rB, CR0LT)>;
def : InstAlias<"iselgt $rT, $rA, $rB",
                (ISEL gprc:$rT, gprc_nor0:$rA, gprc:$rB, CR0GT)>;
def : InstAlias<"iseleq $rT, $rA, $rB",
                (ISEL gprc:$rT, gprc_nor0:$rA, gprc:$rB, CR0EQ)>;

def : InstAlias<"rotlwi $rA, $rS, $n", (RLWINM gprc:$rA, gprc:$rS, u5imm:$n, 0, 31)>;
def : InstAlias<"rotlwi. $rA, $rS, $n", (RLWINM_rec gprc:$rA, gprc:$rS, u5imm:$n, 0, 31)>;
def : InstAlias<"rotlw $rA, $rS, $rB", (RLWNM gprc:$rA, gprc:$rS, gprc:$rB, 0, 31)>;
def : InstAlias<"rotlw. $rA, $rS, $rB", (RLWNM_rec gprc:$rA, gprc:$rS, gprc:$rB, 0, 31)>;
def : InstAlias<"clrlwi $rA, $rS, $n", (RLWINM gprc:$rA, gprc:$rS, 0, u5imm:$n, 31)>;
def : InstAlias<"clrlwi. $rA, $rS, $n", (RLWINM_rec gprc:$rA, gprc:$rS, 0, u5imm:$n, 31)>;

def : InstAlias<"cntlzw $rA, $rS", (CNTLZW gprc:$rA, gprc:$rS)>;
def : InstAlias<"cntlzw. $rA, $rS", (CNTLZW_rec gprc:$rA, gprc:$rS)>;
// The POWER variant
def : MnemonicAlias<"cntlz",  "cntlzw">;
def : MnemonicAlias<"cntlz.", "cntlzw.">;

def EXTLDI : PPCAsmPseudo<"extldi $rA, $rS, $n, $b",
                          (ins g8rc:$rA, g8rc:$rS, u6imm:$n, u6imm:$b)>;
def EXTLDI_rec : PPCAsmPseudo<"extldi. $rA, $rS, $n, $b",
                           (ins g8rc:$rA, g8rc:$rS, u6imm:$n, u6imm:$b)>;
def EXTRDI : PPCAsmPseudo<"extrdi $rA, $rS, $n, $b",
                          (ins g8rc:$rA, g8rc:$rS, u6imm:$n, u6imm:$b)>;
def EXTRDI_rec : PPCAsmPseudo<"extrdi. $rA, $rS, $n, $b",
                           (ins g8rc:$rA, g8rc:$rS, u6imm:$n, u6imm:$b)>;
def INSRDI : PPCAsmPseudo<"insrdi $rA, $rS, $n, $b",
                          (ins g8rc:$rA, g8rc:$rS, u6imm:$n, u6imm:$b)>;
def INSRDI_rec : PPCAsmPseudo<"insrdi. $rA, $rS, $n, $b",
                           (ins g8rc:$rA, g8rc:$rS, u6imm:$n, u6imm:$b)>;
def ROTRDI : PPCAsmPseudo<"rotrdi $rA, $rS, $n",
                          (ins g8rc:$rA, g8rc:$rS, u6imm:$n)>;
def ROTRDI_rec : PPCAsmPseudo<"rotrdi. $rA, $rS, $n",
                           (ins g8rc:$rA, g8rc:$rS, u6imm:$n)>;
def SLDI : PPCAsmPseudo<"sldi $rA, $rS, $n",
                        (ins g8rc:$rA, g8rc:$rS, u6imm:$n)>;
def SLDI_rec : PPCAsmPseudo<"sldi. $rA, $rS, $n",
                         (ins g8rc:$rA, g8rc:$rS, u6imm:$n)>;
def SRDI : PPCAsmPseudo<"srdi $rA, $rS, $n",
                        (ins g8rc:$rA, g8rc:$rS, u6imm:$n)>;
def SRDI_rec : PPCAsmPseudo<"srdi. $rA, $rS, $n",
                         (ins g8rc:$rA, g8rc:$rS, u6imm:$n)>;
def CLRRDI : PPCAsmPseudo<"clrrdi $rA, $rS, $n",
                          (ins g8rc:$rA, g8rc:$rS, u6imm:$n)>;
def CLRRDI_rec : PPCAsmPseudo<"clrrdi. $rA, $rS, $n",
                           (ins g8rc:$rA, g8rc:$rS, u6imm:$n)>;
def CLRLSLDI : PPCAsmPseudo<"clrlsldi $rA, $rS, $b, $n",
                            (ins g8rc:$rA, g8rc:$rS, u6imm:$b, u6imm:$n)>;
def CLRLSLDI_rec : PPCAsmPseudo<"clrlsldi. $rA, $rS, $b, $n",
                             (ins g8rc:$rA, g8rc:$rS, u6imm:$b, u6imm:$n)>;
def SUBPCIS : PPCAsmPseudo<"subpcis $RT, $D", (ins g8rc:$RT, s16imm:$D)>;

def : InstAlias<"rotldi $rA, $rS, $n", (RLDICL g8rc:$rA, g8rc:$rS, u6imm:$n, 0)>;
def : InstAlias<"rotldi $rA, $rS, $n",
                (RLDICL_32_64 g8rc:$rA, gprc:$rS, u6imm:$n, 0)>;
def : InstAlias<"rotldi. $rA, $rS, $n", (RLDICL_rec g8rc:$rA, g8rc:$rS, u6imm:$n, 0)>;
def : InstAlias<"rotld $rA, $rS, $rB", (RLDCL g8rc:$rA, g8rc:$rS, gprc:$rB, 0)>;
def : InstAlias<"rotld. $rA, $rS, $rB", (RLDCL_rec g8rc:$rA, g8rc:$rS, gprc:$rB, 0)>;
def : InstAlias<"clrldi $rA, $rS, $n", (RLDICL g8rc:$rA, g8rc:$rS, 0, u6imm:$n)>;
def : InstAlias<"clrldi $rA, $rS, $n",
                (RLDICL_32_64 g8rc:$rA, gprc:$rS, 0, u6imm:$n)>;
def : InstAlias<"clrldi. $rA, $rS, $n", (RLDICL_rec g8rc:$rA, g8rc:$rS, 0, u6imm:$n)>;
def : InstAlias<"lnia $RT", (ADDPCIS g8rc:$RT, 0)>;

def RLWINMbm : PPCAsmPseudo<"rlwinm $rA, $rS, $n, $b",
                            (ins g8rc:$rA, g8rc:$rS, u5imm:$n, i32imm:$b)>;
def RLWINMbm_rec : PPCAsmPseudo<"rlwinm. $rA, $rS, $n, $b",
                            (ins g8rc:$rA, g8rc:$rS, u5imm:$n, i32imm:$b)>;
def RLWIMIbm : PPCAsmPseudo<"rlwimi $rA, $rS, $n, $b",
                           (ins g8rc:$rA, g8rc:$rS, u5imm:$n, i32imm:$b)>;
def RLWIMIbm_rec : PPCAsmPseudo<"rlwimi. $rA, $rS, $n, $b",
                            (ins g8rc:$rA, g8rc:$rS, u5imm:$n, i32imm:$b)>;
def RLWNMbm : PPCAsmPseudo<"rlwnm $rA, $rS, $n, $b",
                          (ins g8rc:$rA, g8rc:$rS, u5imm:$n, i32imm:$b)>;
def RLWNMbm_rec : PPCAsmPseudo<"rlwnm. $rA, $rS, $n, $b",
                           (ins g8rc:$rA, g8rc:$rS, u5imm:$n, i32imm:$b)>;
def PPCLdFixedAddr :
  PPCPostRAExpPseudo<(outs gprc:$rT), (ins i32imm:$imm), "#FA_LOAD",
                     [(set i32:$rT, (int_ppc_fixed_addr_ld timm:$imm))]>;

// These generic branch instruction forms are used for the assembler parser only.
// Defs and Uses are conservative, since we don't know the BO value.
let PPC970_Unit = 7, isBranch = 1, hasSideEffects = 0 in {
  let Defs = [CTR], Uses = [CTR, RM] in {
    def gBC : BForm_3<16, 0, 0, (outs),
                      (ins u5imm:$BO, crbitrc:$BI, condbrtarget:$BD),
                      "bc $BO, $BI, $BD">;
    def gBCA : BForm_3<16, 1, 0, (outs),
                       (ins u5imm:$BO, crbitrc:$BI, abscondbrtarget:$BD),
                       "bca $BO, $BI, $BD">;
    let isAsmParserOnly = 1 in {
      def gBCat : BForm_3_at<16, 0, 0, (outs),
                             (ins u5imm:$BO, atimm:$at, crbitrc:$BI,
                                  condbrtarget:$BD),
                                  "bc$at $BO, $BI, $BD">;
      def gBCAat : BForm_3_at<16, 1, 0, (outs),
                              (ins u5imm:$BO, atimm:$at, crbitrc:$BI,
                                   abscondbrtarget:$BD),
                                   "bca$at $BO, $BI, $BD">;
    } // isAsmParserOnly = 1
  }
  let Defs = [LR, CTR], Uses = [CTR, RM] in {
    def gBCL : BForm_3<16, 0, 1, (outs),
                       (ins u5imm:$BO, crbitrc:$BI, condbrtarget:$BD),
                       "bcl $BO, $BI, $BD">;
    def gBCLA : BForm_3<16, 1, 1, (outs),
                        (ins u5imm:$BO, crbitrc:$BI, abscondbrtarget:$BD),
                        "bcla $BO, $BI, $BD">;
    let isAsmParserOnly = 1 in {
      def gBCLat : BForm_3_at<16, 0, 1, (outs),
                         (ins u5imm:$BO, atimm:$at, crbitrc:$BI,
                              condbrtarget:$BD),
                              "bcl$at $BO, $BI, $BD">;
      def gBCLAat : BForm_3_at<16, 1, 1, (outs),
                          (ins u5imm:$BO, atimm:$at, crbitrc:$BI,
                               abscondbrtarget:$BD),
                               "bcla$at $BO, $BI, $BD">;
    } // // isAsmParserOnly = 1
  }
  let Defs = [CTR], Uses = [CTR, LR, RM] in
    def gBCLR : XLForm_2<19, 16, 0, (outs),
                         (ins u5imm:$BO, crbitrc:$BI, i32imm:$BH),
                         "bclr $BO, $BI, $BH", IIC_BrB, []>;
  let Defs = [LR, CTR], Uses = [CTR, LR, RM] in
    def gBCLRL : XLForm_2<19, 16, 1, (outs),
                          (ins u5imm:$BO, crbitrc:$BI, i32imm:$BH),
                          "bclrl $BO, $BI, $BH", IIC_BrB, []>;
  let Defs = [CTR], Uses = [CTR, LR, RM] in
    def gBCCTR : XLForm_2<19, 528, 0, (outs),
                          (ins u5imm:$BO, crbitrc:$BI, i32imm:$BH),
                          "bcctr $BO, $BI, $BH", IIC_BrB, []>;
  let Defs = [LR, CTR], Uses = [CTR, LR, RM] in
    def gBCCTRL : XLForm_2<19, 528, 1, (outs),
                           (ins u5imm:$BO, crbitrc:$BI, i32imm:$BH),
                           "bcctrl $BO, $BI, $BH", IIC_BrB, []>;
}

multiclass BranchSimpleMnemonicAT<string pm, int at> {
  def : InstAlias<"bc"#pm#" $bo, $bi, $dst", (gBCat u5imm:$bo, at, crbitrc:$bi,
                                                    condbrtarget:$dst)>;
  def : InstAlias<"bca"#pm#" $bo, $bi, $dst", (gBCAat u5imm:$bo, at, crbitrc:$bi,
                                                      condbrtarget:$dst)>;
  def : InstAlias<"bcl"#pm#" $bo, $bi, $dst", (gBCLat u5imm:$bo, at, crbitrc:$bi,
                                                      condbrtarget:$dst)>;
  def : InstAlias<"bcla"#pm#" $bo, $bi, $dst", (gBCLAat u5imm:$bo, at, crbitrc:$bi,
                                                        condbrtarget:$dst)>;
}
defm : BranchSimpleMnemonicAT<"+", 3>;
defm : BranchSimpleMnemonicAT<"-", 2>;

def : InstAlias<"bclr $bo, $bi", (gBCLR u5imm:$bo, crbitrc:$bi, 0)>;
def : InstAlias<"bclrl $bo, $bi", (gBCLRL u5imm:$bo, crbitrc:$bi, 0)>;
def : InstAlias<"bcctr $bo, $bi", (gBCCTR u5imm:$bo, crbitrc:$bi, 0)>;
def : InstAlias<"bcctrl $bo, $bi", (gBCCTRL u5imm:$bo, crbitrc:$bi, 0)>;

multiclass BranchSimpleMnemonic1<string name, string pm, int bo> {
  def : InstAlias<"b"#name#pm#" $bi, $dst", (gBC bo, crbitrc:$bi, condbrtarget:$dst)>;
  def : InstAlias<"b"#name#"a"#pm#" $bi, $dst", (gBCA bo, crbitrc:$bi, abscondbrtarget:$dst)>;
  def : InstAlias<"b"#name#"lr"#pm#" $bi", (gBCLR bo, crbitrc:$bi, 0)>;
  def : InstAlias<"b"#name#"l"#pm#" $bi, $dst", (gBCL bo, crbitrc:$bi, condbrtarget:$dst)>;
  def : InstAlias<"b"#name#"la"#pm#" $bi, $dst", (gBCLA bo, crbitrc:$bi, abscondbrtarget:$dst)>;
  def : InstAlias<"b"#name#"lrl"#pm#" $bi", (gBCLRL bo, crbitrc:$bi, 0)>;
}
multiclass BranchSimpleMnemonic2<string name, string pm, int bo>
  : BranchSimpleMnemonic1<name, pm, bo> {
  def : InstAlias<"b"#name#"ctr"#pm#" $bi", (gBCCTR bo, crbitrc:$bi, 0)>;
  def : InstAlias<"b"#name#"ctrl"#pm#" $bi", (gBCCTRL bo, crbitrc:$bi, 0)>;
}
defm : BranchSimpleMnemonic2<"t", "", 12>;
defm : BranchSimpleMnemonic2<"f", "", 4>;
defm : BranchSimpleMnemonic2<"t", "-", 14>;
defm : BranchSimpleMnemonic2<"f", "-", 6>;
defm : BranchSimpleMnemonic2<"t", "+", 15>;
defm : BranchSimpleMnemonic2<"f", "+", 7>;
defm : BranchSimpleMnemonic1<"dnzt", "", 8>;
defm : BranchSimpleMnemonic1<"dnzf", "", 0>;
defm : BranchSimpleMnemonic1<"dzt", "", 10>;
defm : BranchSimpleMnemonic1<"dzf", "", 2>;

multiclass BranchExtendedMnemonicPM<string name, string pm, int bibo> {
  def : InstAlias<"b"#name#pm#" $cc, $dst",
                  (BCC bibo, crrc:$cc, condbrtarget:$dst)>;
  def : InstAlias<"b"#name#pm#" $dst",
                  (BCC bibo, CR0, condbrtarget:$dst)>;

  def : InstAlias<"b"#name#"a"#pm#" $cc, $dst",
                  (BCCA bibo, crrc:$cc, abscondbrtarget:$dst)>;
  def : InstAlias<"b"#name#"a"#pm#" $dst",
                  (BCCA bibo, CR0, abscondbrtarget:$dst)>;

  def : InstAlias<"b"#name#"lr"#pm#" $cc",
                  (BCCLR bibo, crrc:$cc)>;
  def : InstAlias<"b"#name#"lr"#pm,
                  (BCCLR bibo, CR0)>;

  def : InstAlias<"b"#name#"ctr"#pm#" $cc",
                  (BCCCTR bibo, crrc:$cc)>;
  def : InstAlias<"b"#name#"ctr"#pm,
                  (BCCCTR bibo, CR0)>;

  def : InstAlias<"b"#name#"l"#pm#" $cc, $dst",
                  (BCCL bibo, crrc:$cc, condbrtarget:$dst)>;
  def : InstAlias<"b"#name#"l"#pm#" $dst",
                  (BCCL bibo, CR0, condbrtarget:$dst)>;

  def : InstAlias<"b"#name#"la"#pm#" $cc, $dst",
                  (BCCLA bibo, crrc:$cc, abscondbrtarget:$dst)>;
  def : InstAlias<"b"#name#"la"#pm#" $dst",
                  (BCCLA bibo, CR0, abscondbrtarget:$dst)>;

  def : InstAlias<"b"#name#"lrl"#pm#" $cc",
                  (BCCLRL bibo, crrc:$cc)>;
  def : InstAlias<"b"#name#"lrl"#pm,
                  (BCCLRL bibo, CR0)>;

  def : InstAlias<"b"#name#"ctrl"#pm#" $cc",
                  (BCCCTRL bibo, crrc:$cc)>;
  def : InstAlias<"b"#name#"ctrl"#pm,
                  (BCCCTRL bibo, CR0)>;
}
multiclass BranchExtendedMnemonic<string name, int bibo> {
  defm : BranchExtendedMnemonicPM<name, "", bibo>;
  defm : BranchExtendedMnemonicPM<name, "-", !add(bibo, 2)>;
  defm : BranchExtendedMnemonicPM<name, "+", !add(bibo, 3)>;
}
defm : BranchExtendedMnemonic<"lt", 12>;
defm : BranchExtendedMnemonic<"gt", 44>;
defm : BranchExtendedMnemonic<"eq", 76>;
defm : BranchExtendedMnemonic<"un", 108>;
defm : BranchExtendedMnemonic<"so", 108>;
defm : BranchExtendedMnemonic<"ge", 4>;
defm : BranchExtendedMnemonic<"nl", 4>;
defm : BranchExtendedMnemonic<"le", 36>;
defm : BranchExtendedMnemonic<"ng", 36>;
defm : BranchExtendedMnemonic<"ne", 68>;
defm : BranchExtendedMnemonic<"nu", 100>;
defm : BranchExtendedMnemonic<"ns", 100>;

def : InstAlias<"cmpwi $rA, $imm", (CMPWI CR0, gprc:$rA, s16imm:$imm)>;
def : InstAlias<"cmpw $rA, $rB", (CMPW CR0, gprc:$rA, gprc:$rB)>;
def : InstAlias<"cmplwi $rA, $imm", (CMPLWI CR0, gprc:$rA, u16imm:$imm)>;
def : InstAlias<"cmplw $rA, $rB", (CMPLW CR0, gprc:$rA, gprc:$rB)>;
def : InstAlias<"cmpdi $rA, $imm", (CMPDI CR0, g8rc:$rA, s16imm64:$imm)>;
def : InstAlias<"cmpd $rA, $rB", (CMPD CR0, g8rc:$rA, g8rc:$rB)>;
def : InstAlias<"cmpldi $rA, $imm", (CMPLDI CR0, g8rc:$rA, u16imm64:$imm)>;
def : InstAlias<"cmpld $rA, $rB", (CMPLD CR0, g8rc:$rA, g8rc:$rB)>;

def : InstAlias<"cmpi $bf, 0, $rA, $imm", (CMPWI crrc:$bf, gprc:$rA, s16imm:$imm)>;
def : InstAlias<"cmp $bf, 0, $rA, $rB", (CMPW crrc:$bf, gprc:$rA, gprc:$rB)>;
def : InstAlias<"cmpli $bf, 0, $rA, $imm", (CMPLWI crrc:$bf, gprc:$rA, u16imm:$imm)>;
def : InstAlias<"cmpl $bf, 0, $rA, $rB", (CMPLW crrc:$bf, gprc:$rA, gprc:$rB)>;
def : InstAlias<"cmpi $bf, 1, $rA, $imm", (CMPDI crrc:$bf, g8rc:$rA, s16imm64:$imm)>;
def : InstAlias<"cmp $bf, 1, $rA, $rB", (CMPD crrc:$bf, g8rc:$rA, g8rc:$rB)>;
def : InstAlias<"cmpli $bf, 1, $rA, $imm", (CMPLDI crrc:$bf, g8rc:$rA, u16imm64:$imm)>;
def : InstAlias<"cmpl $bf, 1, $rA, $rB", (CMPLD crrc:$bf, g8rc:$rA, g8rc:$rB)>;

def : InstAlias<"trap", (TW 31, R0, R0)>;

multiclass TrapExtendedMnemonic<string name, int to> {
  def : InstAlias<"td"#name#"i $rA, $imm", (TDI to, g8rc:$rA, s16imm:$imm)>;
  def : InstAlias<"td"#name#" $rA, $rB", (TD to, g8rc:$rA, g8rc:$rB)>;
  def : InstAlias<"tw"#name#"i $rA, $imm", (TWI to, gprc:$rA, s16imm:$imm)>;
  def : InstAlias<"tw"#name#" $rA, $rB", (TW to, gprc:$rA, gprc:$rB)>;
}
defm : TrapExtendedMnemonic<"lt", 16>;
defm : TrapExtendedMnemonic<"le", 20>;
defm : TrapExtendedMnemonic<"eq", 4>;
defm : TrapExtendedMnemonic<"ge", 12>;
defm : TrapExtendedMnemonic<"gt", 8>;
defm : TrapExtendedMnemonic<"nl", 12>;
defm : TrapExtendedMnemonic<"ne", 24>;
defm : TrapExtendedMnemonic<"ng", 20>;
defm : TrapExtendedMnemonic<"llt", 2>;
defm : TrapExtendedMnemonic<"lle", 6>;
defm : TrapExtendedMnemonic<"lge", 5>;
defm : TrapExtendedMnemonic<"lgt", 1>;
defm : TrapExtendedMnemonic<"lnl", 5>;
defm : TrapExtendedMnemonic<"lng", 6>;
defm : TrapExtendedMnemonic<"u", 31>;

// Atomic loads
def : Pat<(i32 (atomic_load_8  DForm:$src)), (LBZ  memri:$src)>;
def : Pat<(i32 (atomic_load_16 DForm:$src)), (LHZ  memri:$src)>;
def : Pat<(i32 (atomic_load_32 DForm:$src)), (LWZ  memri:$src)>;
def : Pat<(i32 (atomic_load_8  XForm:$src)), (LBZX memrr:$src)>;
def : Pat<(i32 (atomic_load_16 XForm:$src)), (LHZX memrr:$src)>;
def : Pat<(i32 (atomic_load_32 XForm:$src)), (LWZX memrr:$src)>;

// Atomic stores
def : Pat<(atomic_store_8  i32:$val, DForm:$ptr), (STB  gprc:$val, memri:$ptr)>;
def : Pat<(atomic_store_16 i32:$val, DForm:$ptr), (STH  gprc:$val, memri:$ptr)>;
def : Pat<(atomic_store_32 i32:$val, DForm:$ptr), (STW  gprc:$val, memri:$ptr)>;
def : Pat<(atomic_store_8  i32:$val, XForm:$ptr), (STBX gprc:$val, memrr:$ptr)>;
def : Pat<(atomic_store_16 i32:$val, XForm:$ptr), (STHX gprc:$val, memrr:$ptr)>;
def : Pat<(atomic_store_32 i32:$val, XForm:$ptr), (STWX gprc:$val, memrr:$ptr)>;

let Predicates = [IsISA3_0] in {

// Copy-Paste Facility
// We prefix 'CP' to COPY due to name conflict in Target.td. We also prefix to
// PASTE for naming consistency.
let mayLoad = 1 in
def CP_COPY   : X_RA5_RB5<31, 774, "copy"  , gprc, IIC_LdStCOPY, []>;

let mayStore = 1, Defs = [CR0] in
def CP_PASTE_rec : X_L1_RA5_RB5<31, 902, "paste.", gprc, IIC_LdStPASTE, []>, isRecordForm;

def : InstAlias<"paste. $RA, $RB", (CP_PASTE_rec gprc:$RA, gprc:$RB, 1)>;
def CP_ABORT : XForm_0<31, 838, (outs), (ins), "cpabort", IIC_SprABORT, []>;

// Message Synchronize
def MSGSYNC : XForm_0<31, 886, (outs), (ins), "msgsync", IIC_SprMSGSYNC, []>;

// Power-Saving Mode Instruction:
def STOP : XForm_0<19, 370, (outs), (ins), "stop", IIC_SprSTOP, []>;

def SETB : XForm_44<31, 128, (outs gprc:$RT), (ins crrc:$BFA),
                       "setb $RT, $BFA", IIC_IntGeneral>, SExt32To64;
} // IsISA3_0

let Predicates = [IsISA3_0] in {
def : Pat<(i32 (int_ppc_cmprb i32:$a, gprc:$b, gprc:$c)),
          (i32 (SETB (CMPRB u1imm:$a, $b, $c)))>;
}
def : Pat<(i32 (int_ppc_mulhw gprc:$a, gprc:$b)),
          (i32 (MULHW $a, $b))>;
def : Pat<(i32 (int_ppc_mulhwu gprc:$a, gprc:$b)),
          (i32 (MULHWU $a, $b))>;
def : Pat<(i32 (int_ppc_cmpb gprc:$a, gprc:$b)),
          (i32 (CMPB $a, $b))>;

def : Pat<(int_ppc_load2r ForceXForm:$ptr),
          (LHBRX ForceXForm:$ptr)>;
def : Pat<(int_ppc_load4r ForceXForm:$ptr),
          (LWBRX ForceXForm:$ptr)>;
def : Pat<(int_ppc_store2r gprc:$a, ForceXForm:$ptr),
          (STHBRX gprc:$a, ForceXForm:$ptr)>;
def : Pat<(int_ppc_store4r gprc:$a, ForceXForm:$ptr),
          (STWBRX gprc:$a, ForceXForm:$ptr)>;


// Fast 32-bit reverse bits algorithm:
// Step 1: 1-bit swap (swap odd 1-bit and even 1-bit):
// n = ((n >> 1) & 0x55555555) | ((n << 1) & 0xAAAAAAAA);
// Step 2: 2-bit swap (swap odd 2-bit and even 2-bit):
// n = ((n >> 2) & 0x33333333) | ((n << 2) & 0xCCCCCCCC);
// Step 3: 4-bit swap (swap odd 4-bit and even 4-bit):
// n = ((n >> 4) & 0x0F0F0F0F) | ((n << 4) & 0xF0F0F0F0);
// Step 4: byte reverse (Suppose n = [B1,B2,B3,B4]):
// Step 4.1: Put B4,B2 in the right position (rotate left 3 bytes):
// n' = (n rotl 24);  After which n' = [B4, B1, B2, B3]
// Step 4.2: Insert B3 to the right position:
// n' = rlwimi n', n, 8, 8, 15;  After which n' = [B4, B3, B2, B3]
// Step 4.3: Insert B1 to the right position:
// n' = rlwimi n', n, 8, 24, 31;  After which n' = [B4, B3, B2, B1]
def MaskValues {
  dag Lo1 = (ORI (LIS 0x5555), 0x5555);
  dag Hi1 = (ORI (LIS 0xAAAA), 0xAAAA);
  dag Lo2 = (ORI (LIS 0x3333), 0x3333);
  dag Hi2 = (ORI (LIS 0xCCCC), 0xCCCC);
  dag Lo4 = (ORI (LIS 0x0F0F), 0x0F0F);
  dag Hi4 = (ORI (LIS 0xF0F0), 0xF0F0);
}

def Shift1 {
  dag Right = (RLWINM $A, 31, 1, 31);
  dag Left = (RLWINM $A, 1, 0, 30);
}

def Swap1 {
  dag Bit = (OR (AND Shift1.Right, MaskValues.Lo1),
   (AND Shift1.Left, MaskValues.Hi1));
}

def Shift2 {
  dag Right = (RLWINM Swap1.Bit, 30, 2, 31);
  dag Left = (RLWINM Swap1.Bit, 2, 0, 29);
}

def Swap2 {
  dag Bits = (OR (AND Shift2.Right, MaskValues.Lo2),
                 (AND Shift2.Left, MaskValues.Hi2));
}

def Shift4 {
  dag Right = (RLWINM Swap2.Bits, 28, 4, 31);
  dag Left = (RLWINM Swap2.Bits, 4, 0, 27);
}

def Swap4 {
  dag Bits = (OR (AND Shift4.Right, MaskValues.Lo4),
                 (AND Shift4.Left, MaskValues.Hi4));
}

def Rotate {
  dag Left3Bytes = (RLWINM Swap4.Bits, 24, 0, 31);
}

def RotateInsertByte3 {
  dag Left = (RLWIMI Rotate.Left3Bytes, Swap4.Bits, 8, 8, 15);
}

def RotateInsertByte1 {
  dag Left = (RLWIMI RotateInsertByte3.Left, Swap4.Bits, 8, 24, 31);
}

// Clear the upper half of the register when in 64-bit mode
let Predicates = [In64BitMode] in
def : Pat<(i32 (bitreverse i32:$A)), (RLDICL_32 RotateInsertByte1.Left, 0, 32)>;
let Predicates = [In32BitMode] in
def : Pat<(i32 (bitreverse i32:$A)), RotateInsertByte1.Left>;

// Fast 64-bit reverse bits algorithm:
// Step 1: 1-bit swap (swap odd 1-bit and even 1-bit):
// n = ((n >> 1) & 0x5555555555555555) | ((n << 1) & 0xAAAAAAAAAAAAAAAA);
// Step 2: 2-bit swap (swap odd 2-bit and even 2-bit):
// n = ((n >> 2) & 0x3333333333333333) | ((n << 2) & 0xCCCCCCCCCCCCCCCC);
// Step 3: 4-bit swap (swap odd 4-bit and even 4-bit):
// n = ((n >> 4) & 0x0F0F0F0F0F0F0F0F) | ((n << 4) & 0xF0F0F0F0F0F0F0F0);
// Step 4: byte reverse (Suppose n = [B0,B1,B2,B3,B4,B5,B6,B7]):
// Apply the same byte reverse algorithm mentioned above for the fast 32-bit
// reverse to both the high 32 bit and low 32 bit of the 64 bit value. And
// then OR them together to get the final result.
def MaskValues64 {
  dag Lo1 = (i64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), MaskValues.Lo1, sub_32));
  dag Hi1 = (i64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), MaskValues.Hi1, sub_32));
  dag Lo2 = (i64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), MaskValues.Lo2, sub_32));
  dag Hi2 = (i64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), MaskValues.Hi2, sub_32));
  dag Lo4 = (i64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), MaskValues.Lo4, sub_32));
  dag Hi4 = (i64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), MaskValues.Hi4, sub_32));
}

def DWMaskValues {
  dag Lo1 = (ORI8 (ORIS8 (RLDICR MaskValues64.Lo1, 32, 31), 0x5555), 0x5555);
  dag Hi1 = (ORI8 (ORIS8 (RLDICR MaskValues64.Hi1, 32, 31), 0xAAAA), 0xAAAA);
  dag Lo2 = (ORI8 (ORIS8 (RLDICR MaskValues64.Lo2, 32, 31), 0x3333), 0x3333);
  dag Hi2 = (ORI8 (ORIS8 (RLDICR MaskValues64.Hi2, 32, 31), 0xCCCC), 0xCCCC);
  dag Lo4 = (ORI8 (ORIS8 (RLDICR MaskValues64.Lo4, 32, 31), 0x0F0F), 0x0F0F);
  dag Hi4 = (ORI8 (ORIS8 (RLDICR MaskValues64.Hi4, 32, 31), 0xF0F0), 0xF0F0);
}

def DWSwapInByte {
  dag Swap1 = (OR8 (AND8 (RLDICL $A, 63, 1), DWMaskValues.Lo1),
                   (AND8 (RLDICR $A, 1, 62), DWMaskValues.Hi1));
  dag Swap2 = (OR8 (AND8 (RLDICL Swap1, 62, 2), DWMaskValues.Lo2),
                   (AND8 (RLDICR Swap1, 2, 61), DWMaskValues.Hi2));
  dag Swap4 = (OR8 (AND8 (RLDICL Swap2, 60, 4), DWMaskValues.Lo4),
                   (AND8 (RLDICR Swap2, 4, 59), DWMaskValues.Hi4));
}

// Intra-byte swap is done, now start inter-byte swap.
def DWBytes4567 {
  dag Word = (i32 (EXTRACT_SUBREG DWSwapInByte.Swap4, sub_32));
}

def DWBytes7456 {
  dag Word = (RLWINM DWBytes4567.Word, 24, 0, 31);
}

def DWBytes7656 {
  dag Word = (RLWIMI DWBytes7456.Word, DWBytes4567.Word, 8, 8, 15);
}

// B7 B6 B5 B4 in the right order
def DWBytes7654 {
  dag Word = (RLWIMI DWBytes7656.Word, DWBytes4567.Word, 8, 24, 31);
  dag DWord =
    (i64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), Word, sub_32));
}

def DWBytes0123 {
  dag Word = (i32 (EXTRACT_SUBREG (RLDICL DWSwapInByte.Swap4, 32, 32), sub_32));
}

def DWBytes3012 {
  dag Word = (RLWINM DWBytes0123.Word, 24, 0, 31);
}

def DWBytes3212 {
  dag Word = (RLWIMI DWBytes3012.Word, DWBytes0123.Word, 8, 8, 15);
}

// B3 B2 B1 B0 in the right order
def DWBytes3210 {
  dag Word = (RLWIMI DWBytes3212.Word, DWBytes0123.Word, 8, 24, 31);
  dag DWord =
    (i64 (INSERT_SUBREG (i64 (IMPLICIT_DEF)), Word, sub_32));
}

// These instructions store a hash computed from the value of the link register
// and the value of the stack pointer.
let mayStore = 1 in {
def HASHST : XForm_XD6_RA5_RB5<31, 722, (outs),
                               (ins gprc:$RB, (memrihash $D, $RA):$addr),
                               "hashst $RB, $addr", IIC_IntGeneral, []>;
def HASHSTP : XForm_XD6_RA5_RB5<31, 658, (outs),
                                (ins gprc:$RB, (memrihash $D, $RA):$addr),
                                "hashstp $RB, $addr", IIC_IntGeneral, []>;
}

// These instructions check a hash computed from the value of the link register
// and the value of the stack pointer. The hasSideEffects flag is needed as the
// instruction may TRAP if the hash does not match the hash stored at the
// specified address.
let mayLoad = 1, hasSideEffects = 1 in {
def HASHCHK : XForm_XD6_RA5_RB5<31, 754, (outs),
                                (ins gprc:$RB, (memrihash $D, $RA):$addr),
                                "hashchk $RB, $addr", IIC_IntGeneral, []>;
def HASHCHKP : XForm_XD6_RA5_RB5<31, 690, (outs),
                                 (ins gprc:$RB, (memrihash $D, $RA):$addr),
                                 "hashchkp $RB, $addr", IIC_IntGeneral, []>;
}

let Defs = [CR7], Itinerary = IIC_LdStSync in
def CFENCE : PPCPostRAExpPseudo<(outs), (ins gprc:$cr), "#CFENCE", []>;

// Now both high word and low word are reversed, next
// swap the high word and low word.
def : Pat<(i64 (bitreverse i64:$A)),
  (OR8 (RLDICR DWBytes7654.DWord, 32, 31), DWBytes3210.DWord)>;

def : Pat<(int_ppc_stwcx ForceXForm:$dst, gprc:$A),
          (RLWINM (STWCX gprc:$A, ForceXForm:$dst), 31, 31, 31)>;
def : Pat<(PPCStoreCond ForceXForm:$dst, gprc:$A, 4),
          (RLWINM (STWCX gprc:$A, ForceXForm:$dst), 31, 31, 31)>;
def : Pat<(int_ppc_stbcx ForceXForm:$dst, gprc:$A),
          (RLWINM (STBCX gprc:$A, ForceXForm:$dst), 31, 31, 31)>;
def : Pat<(PPCStoreCond ForceXForm:$dst, gprc:$A, 1),
          (RLWINM (STBCX gprc:$A, ForceXForm:$dst), 31, 31, 31)>;

def : Pat<(int_ppc_fcfid f64:$A),
        (XSCVSXDDP $A)>;
def : Pat<(int_ppc_fcfud f64:$A),
        (XSCVUXDDP $A)>;
def : Pat<(int_ppc_fctid f64:$A),
        (FCTID $A)>;
def : Pat<(int_ppc_fctidz f64:$A),
        (XSCVDPSXDS $A)>;
def : Pat<(int_ppc_fctiw f64:$A),
        (FCTIW $A)>;
def : Pat<(int_ppc_fctiwz f64:$A),
        (XSCVDPSXWS $A)>;
def : Pat<(int_ppc_fctudz f64:$A),
        (XSCVDPUXDS $A)>;
def : Pat<(int_ppc_fctuwz f64:$A),
        (XSCVDPUXWS $A)>;

def : Pat<(int_ppc_mfmsr), (MFMSR)>;
def : Pat<(int_ppc_mftbu), (MFTB 269)>;
def : Pat<(i32 (int_ppc_mfspr timm:$SPR)),
          (MFSPR $SPR)>;
def : Pat<(int_ppc_mtspr timm:$SPR, gprc:$RT),
          (MTSPR $SPR, $RT)>;
def : Pat<(int_ppc_mtmsr gprc:$RS),
          (MTMSR $RS, 0)>;

let Predicates = [IsISA2_07] in {
  def : Pat<(int_ppc_sthcx ForceXForm:$dst, gprc:$A),
            (RLWINM (STHCX gprc:$A, ForceXForm:$dst), 31, 31, 31)>;
  def : Pat<(PPCStoreCond ForceXForm:$dst, gprc:$A, 2),
            (RLWINM (STHCX gprc:$A, ForceXForm:$dst), 31, 31, 31)>;
}
def : Pat<(int_ppc_dcbtstt ForceXForm:$dst),
          (DCBTST 16, ForceXForm:$dst)>;
def : Pat<(int_ppc_dcbtt ForceXForm:$dst),
          (DCBT 16, ForceXForm:$dst)>;

def : Pat<(int_ppc_stfiw ForceXForm:$dst, f64:$XT),
          (STFIWX f64:$XT, ForceXForm:$dst)>;
