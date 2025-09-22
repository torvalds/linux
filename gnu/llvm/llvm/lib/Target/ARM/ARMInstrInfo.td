//===- ARMInstrInfo.td - Target Description for ARM Target -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the ARM instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// ARM specific DAG Nodes.
//

// Type profiles.
def SDT_ARMCallSeqStart : SDCallSeqStart<[ SDTCisVT<0, i32>,
                                           SDTCisVT<1, i32> ]>;
def SDT_ARMCallSeqEnd   : SDCallSeqEnd<[ SDTCisVT<0, i32>, SDTCisVT<1, i32> ]>;
def SDT_ARMStructByVal : SDTypeProfile<0, 4,
                                       [SDTCisVT<0, i32>, SDTCisVT<1, i32>,
                                        SDTCisVT<2, i32>, SDTCisVT<3, i32>]>;

def SDT_ARMSaveCallPC : SDTypeProfile<0, 1, []>;

def SDT_ARMcall    : SDTypeProfile<0, -1, [SDTCisPtrTy<0>]>;

def SDT_ARMCMov    : SDTypeProfile<1, 3,
                                   [SDTCisSameAs<0, 1>, SDTCisSameAs<0, 2>,
                                    SDTCisVT<3, i32>]>;

def SDT_ARMBrcond  : SDTypeProfile<0, 2,
                                   [SDTCisVT<0, OtherVT>, SDTCisVT<1, i32>]>;

def SDT_ARMBrJT    : SDTypeProfile<0, 2,
                                  [SDTCisPtrTy<0>, SDTCisVT<1, i32>]>;

def SDT_ARMBr2JT   : SDTypeProfile<0, 3,
                                  [SDTCisPtrTy<0>, SDTCisVT<1, i32>,
                                   SDTCisVT<2, i32>]>;

def SDT_ARMBCC_i64 : SDTypeProfile<0, 6,
                                  [SDTCisVT<0, i32>,
                                   SDTCisVT<1, i32>, SDTCisVT<2, i32>,
                                   SDTCisVT<3, i32>, SDTCisVT<4, i32>,
                                   SDTCisVT<5, OtherVT>]>;

def SDT_ARMAnd     : SDTypeProfile<1, 2,
                                   [SDTCisVT<0, i32>, SDTCisVT<1, i32>,
                                    SDTCisVT<2, i32>]>;

def SDT_ARMCmp     : SDTypeProfile<0, 2, [SDTCisSameAs<0, 1>]>;

def SDT_ARMPICAdd  : SDTypeProfile<1, 2, [SDTCisSameAs<0, 1>,
                                          SDTCisPtrTy<1>, SDTCisVT<2, i32>]>;

def SDT_ARMThreadPointer : SDTypeProfile<1, 0, [SDTCisPtrTy<0>]>;
def SDT_ARMEH_SJLJ_Setjmp : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisPtrTy<1>,
                                                 SDTCisInt<2>]>;
def SDT_ARMEH_SJLJ_Longjmp: SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisInt<1>]>;
def SDT_ARMEH_SJLJ_SetupDispatch: SDTypeProfile<0, 0, []>;

def SDT_ARMMEMBARRIER     : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_ARMPREFETCH : SDTypeProfile<0, 3, [SDTCisPtrTy<0>, SDTCisSameAs<1, 2>,
                                           SDTCisInt<1>]>;

def SDT_ARMTCRET : SDTypeProfile<0, 2, [SDTCisPtrTy<0>]>;

def SDT_ARMBFI : SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisVT<1, i32>,
                                      SDTCisVT<2, i32>, SDTCisVT<3, i32>]>;

def SDT_WIN__DBZCHK : SDTypeProfile<0, 1, [SDTCisVT<0, i32>]>;

def SDT_ARMMEMCPY  : SDTypeProfile<2, 3, [SDTCisVT<0, i32>, SDTCisVT<1, i32>,
                                          SDTCisVT<2, i32>, SDTCisVT<3, i32>,
                                          SDTCisVT<4, i32>]>;

def SDTBinaryArithWithFlags : SDTypeProfile<2, 2,
                                            [SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>, SDTCisVT<1, i32>]>;

// SDTBinaryArithWithFlagsInOut - RES1, CPSR = op LHS, RHS, CPSR
def SDTBinaryArithWithFlagsInOut : SDTypeProfile<2, 3,
                                            [SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVT<4, i32>]>;

def SDT_LongMac  : SDTypeProfile<2, 4, [SDTCisVT<0, i32>,
                                        SDTCisSameAs<0, 1>,
                                        SDTCisSameAs<0, 2>,
                                        SDTCisSameAs<0, 3>,
                                        SDTCisSameAs<0, 4>,
                                        SDTCisSameAs<0, 5>]>;

// ARMlsll, ARMlsrl, ARMasrl
def SDT_ARMIntShiftParts : SDTypeProfile<2, 3, [SDTCisSameAs<0, 1>,
                                              SDTCisSameAs<0, 2>,
                                              SDTCisSameAs<0, 3>,
                                              SDTCisInt<0>,
                                              SDTCisInt<4>]>;

def ARMSmlald        : SDNode<"ARMISD::SMLALD", SDT_LongMac>;
def ARMSmlaldx       : SDNode<"ARMISD::SMLALDX", SDT_LongMac>;
def ARMSmlsld        : SDNode<"ARMISD::SMLSLD", SDT_LongMac>;
def ARMSmlsldx       : SDNode<"ARMISD::SMLSLDX", SDT_LongMac>;

def SDT_ARMCSel      : SDTypeProfile<1, 3,
                                   [SDTCisSameAs<0, 1>,
                                    SDTCisSameAs<0, 2>,
                                    SDTCisInt<3>,
                                    SDTCisVT<3, i32>]>;

def ARMcsinv         : SDNode<"ARMISD::CSINV", SDT_ARMCSel, [SDNPOptInGlue]>;
def ARMcsneg         : SDNode<"ARMISD::CSNEG", SDT_ARMCSel, [SDNPOptInGlue]>;
def ARMcsinc         : SDNode<"ARMISD::CSINC", SDT_ARMCSel, [SDNPOptInGlue]>;

def SDT_MulHSR       : SDTypeProfile<1, 3, [SDTCisVT<0,i32>,
                                            SDTCisSameAs<0, 1>,
                                            SDTCisSameAs<0, 2>,
                                            SDTCisSameAs<0, 3>]>;

def ARMsmmlar      : SDNode<"ARMISD::SMMLAR", SDT_MulHSR>;
def ARMsmmlsr      : SDNode<"ARMISD::SMMLSR", SDT_MulHSR>;

// Node definitions.
def ARMWrapper       : SDNode<"ARMISD::Wrapper",     SDTIntUnaryOp>;
def ARMWrapperPIC    : SDNode<"ARMISD::WrapperPIC",  SDTIntUnaryOp>;
def ARMWrapperJT     : SDNode<"ARMISD::WrapperJT",   SDTIntUnaryOp>;

def ARMcallseq_start : SDNode<"ISD::CALLSEQ_START", SDT_ARMCallSeqStart,
                              [SDNPHasChain, SDNPSideEffect, SDNPOutGlue]>;
def ARMcallseq_end   : SDNode<"ISD::CALLSEQ_END",   SDT_ARMCallSeqEnd,
                              [SDNPHasChain, SDNPSideEffect,
                               SDNPOptInGlue, SDNPOutGlue]>;
def ARMcopystructbyval : SDNode<"ARMISD::COPY_STRUCT_BYVAL" ,
                                SDT_ARMStructByVal,
                                [SDNPHasChain, SDNPInGlue, SDNPOutGlue,
                                 SDNPMayStore, SDNPMayLoad]>;

def ARMcall          : SDNode<"ARMISD::CALL", SDT_ARMcall,
                              [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                               SDNPVariadic]>;
def ARMcall_pred    : SDNode<"ARMISD::CALL_PRED", SDT_ARMcall,
                              [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                               SDNPVariadic]>;
def ARMcall_nolink   : SDNode<"ARMISD::CALL_NOLINK", SDT_ARMcall,
                              [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue,
                               SDNPVariadic]>;

def ARMretglue       : SDNode<"ARMISD::RET_GLUE", SDTNone,
                              [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def ARMseretglue     : SDNode<"ARMISD::SERET_GLUE", SDTNone,
                              [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def ARMintretglue    : SDNode<"ARMISD::INTRET_GLUE", SDT_ARMcall,
                              [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def ARMcmov          : SDNode<"ARMISD::CMOV", SDT_ARMCMov,
                              [SDNPInGlue]>;

def ARMssat   : SDNode<"ARMISD::SSAT", SDTIntSatNoShOp, []>;

def ARMusat   : SDNode<"ARMISD::USAT", SDTIntSatNoShOp, []>;

def ARMbrcond        : SDNode<"ARMISD::BRCOND", SDT_ARMBrcond,
                              [SDNPHasChain, SDNPInGlue, SDNPOutGlue]>;

def ARMbrjt          : SDNode<"ARMISD::BR_JT", SDT_ARMBrJT,
                              [SDNPHasChain]>;
def ARMbr2jt         : SDNode<"ARMISD::BR2_JT", SDT_ARMBr2JT,
                              [SDNPHasChain]>;

def ARMBcci64        : SDNode<"ARMISD::BCC_i64", SDT_ARMBCC_i64,
                              [SDNPHasChain]>;

def ARMcmp           : SDNode<"ARMISD::CMP", SDT_ARMCmp,
                              [SDNPOutGlue]>;

def ARMcmn           : SDNode<"ARMISD::CMN", SDT_ARMCmp,
                              [SDNPOutGlue]>;

def ARMcmpZ          : SDNode<"ARMISD::CMPZ", SDT_ARMCmp,
                              [SDNPOutGlue, SDNPCommutative]>;

def ARMpic_add       : SDNode<"ARMISD::PIC_ADD", SDT_ARMPICAdd>;

def ARMasrl          : SDNode<"ARMISD::ASRL", SDT_ARMIntShiftParts, []>;
def ARMlsrl          : SDNode<"ARMISD::LSRL", SDT_ARMIntShiftParts, []>;
def ARMlsll          : SDNode<"ARMISD::LSLL", SDT_ARMIntShiftParts, []>;

def ARMsrl_glue      : SDNode<"ARMISD::SRL_GLUE", SDTIntUnaryOp, [SDNPOutGlue]>;
def ARMsra_glue      : SDNode<"ARMISD::SRA_GLUE", SDTIntUnaryOp, [SDNPOutGlue]>;
def ARMrrx           : SDNode<"ARMISD::RRX"     , SDTIntUnaryOp, [SDNPInGlue ]>;

def ARMaddc          : SDNode<"ARMISD::ADDC",  SDTBinaryArithWithFlags,
                              [SDNPCommutative]>;
def ARMsubc          : SDNode<"ARMISD::SUBC",  SDTBinaryArithWithFlags>;
def ARMlsls          : SDNode<"ARMISD::LSLS",  SDTBinaryArithWithFlags>;
def ARMadde          : SDNode<"ARMISD::ADDE",  SDTBinaryArithWithFlagsInOut>;
def ARMsube          : SDNode<"ARMISD::SUBE",  SDTBinaryArithWithFlagsInOut>;

def ARMthread_pointer: SDNode<"ARMISD::THREAD_POINTER", SDT_ARMThreadPointer>;
def ARMeh_sjlj_setjmp: SDNode<"ARMISD::EH_SJLJ_SETJMP",
                               SDT_ARMEH_SJLJ_Setjmp,
                               [SDNPHasChain, SDNPSideEffect]>;
def ARMeh_sjlj_longjmp: SDNode<"ARMISD::EH_SJLJ_LONGJMP",
                               SDT_ARMEH_SJLJ_Longjmp,
                               [SDNPHasChain, SDNPSideEffect]>;
def ARMeh_sjlj_setup_dispatch: SDNode<"ARMISD::EH_SJLJ_SETUP_DISPATCH",
                                      SDT_ARMEH_SJLJ_SetupDispatch,
                                      [SDNPHasChain, SDNPSideEffect]>;

def ARMMemBarrierMCR  : SDNode<"ARMISD::MEMBARRIER_MCR", SDT_ARMMEMBARRIER,
                               [SDNPHasChain, SDNPSideEffect]>;
def ARMPreload        : SDNode<"ARMISD::PRELOAD", SDT_ARMPREFETCH,
                               [SDNPHasChain, SDNPMayLoad, SDNPMayStore]>;

def ARMtcret         : SDNode<"ARMISD::TC_RETURN", SDT_ARMTCRET,
                        [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;

def ARMbfi           : SDNode<"ARMISD::BFI", SDT_ARMBFI>;

def ARMmemcopy : SDNode<"ARMISD::MEMCPY", SDT_ARMMEMCPY,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue,
                         SDNPMayStore, SDNPMayLoad]>;

def ARMsmulwb       : SDNode<"ARMISD::SMULWB", SDTIntBinOp, []>;
def ARMsmulwt       : SDNode<"ARMISD::SMULWT", SDTIntBinOp, []>;
def ARMsmlalbb      : SDNode<"ARMISD::SMLALBB", SDT_LongMac, []>;
def ARMsmlalbt      : SDNode<"ARMISD::SMLALBT", SDT_LongMac, []>;
def ARMsmlaltb      : SDNode<"ARMISD::SMLALTB", SDT_LongMac, []>;
def ARMsmlaltt      : SDNode<"ARMISD::SMLALTT", SDT_LongMac, []>;

def ARMqadd8b       : SDNode<"ARMISD::QADD8b", SDT_ARMAnd, []>;
def ARMqsub8b       : SDNode<"ARMISD::QSUB8b", SDT_ARMAnd, []>;
def ARMqadd16b      : SDNode<"ARMISD::QADD16b", SDT_ARMAnd, []>;
def ARMqsub16b      : SDNode<"ARMISD::QSUB16b", SDT_ARMAnd, []>;

def ARMuqadd8b       : SDNode<"ARMISD::UQADD8b", SDT_ARMAnd, []>;
def ARMuqsub8b       : SDNode<"ARMISD::UQSUB8b", SDT_ARMAnd, []>;
def ARMuqadd16b      : SDNode<"ARMISD::UQADD16b", SDT_ARMAnd, []>;
def ARMuqsub16b      : SDNode<"ARMISD::UQSUB16b", SDT_ARMAnd, []>;

def SDT_ARMldrd     : SDTypeProfile<2, 1, [SDTCisVT<0, i32>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;
def ARMldrd         : SDNode<"ARMISD::LDRD", SDT_ARMldrd, [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;

def SDT_ARMstrd     : SDTypeProfile<0, 3, [SDTCisVT<0, i32>, SDTCisSameAs<0, 1>, SDTCisPtrTy<2>]>;
def ARMstrd         : SDNode<"ARMISD::STRD", SDT_ARMstrd, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

// Vector operations shared between NEON and MVE

def ARMvdup      : SDNode<"ARMISD::VDUP", SDTypeProfile<1, 1, [SDTCisVec<0>]>>;

// VDUPLANE can produce a quad-register result from a double-register source,
// so the result is not constrained to match the source.
def ARMvduplane  : SDNode<"ARMISD::VDUPLANE",
                          SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisVec<1>,
                                               SDTCisVT<2, i32>]>>;

def SDTARMVIDUP  : SDTypeProfile<2, 2, [SDTCisVec<0>, SDTCisVT<1, i32>,
                                          SDTCisVT<2, i32>, SDTCisVT<3, i32>]>;
def ARMvidup    : SDNode<"ARMISD::VIDUP", SDTARMVIDUP>;

def SDTARMVSHUF   : SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisSameAs<0, 1>]>;
def ARMvrev64    : SDNode<"ARMISD::VREV64", SDTARMVSHUF>;
def ARMvrev32    : SDNode<"ARMISD::VREV32", SDTARMVSHUF>;
def ARMvrev16    : SDNode<"ARMISD::VREV16", SDTARMVSHUF>;

def SDTARMVGETLN  : SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisVec<1>,
                                         SDTCisVT<2, i32>]>;
def ARMvgetlaneu : SDNode<"ARMISD::VGETLANEu", SDTARMVGETLN>;
def ARMvgetlanes : SDNode<"ARMISD::VGETLANEs", SDTARMVGETLN>;

def SDTARMVMOVIMM : SDTypeProfile<1, 1, [SDTCisVec<0>, SDTCisVT<1, i32>]>;
def ARMvmovImm   : SDNode<"ARMISD::VMOVIMM", SDTARMVMOVIMM>;
def ARMvmvnImm   : SDNode<"ARMISD::VMVNIMM", SDTARMVMOVIMM>;
def ARMvmovFPImm : SDNode<"ARMISD::VMOVFPIMM", SDTARMVMOVIMM>;

def SDTARMVORRIMM : SDTypeProfile<1, 2, [SDTCisVec<0>, SDTCisSameAs<0, 1>,
                                           SDTCisVT<2, i32>]>;
def ARMvorrImm   : SDNode<"ARMISD::VORRIMM", SDTARMVORRIMM>;
def ARMvbicImm   : SDNode<"ARMISD::VBICIMM", SDTARMVORRIMM>;

def SDTARMVSHIMM : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisSameAs<0, 1>,
                                        SDTCisVT<2, i32>]>;
def SDTARMVSH : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisSameAs<0, 1>,
                                     SDTCisSameAs<0, 2>,]>;
def ARMvshlImm   : SDNode<"ARMISD::VSHLIMM", SDTARMVSHIMM>;
def ARMvshrsImm  : SDNode<"ARMISD::VSHRsIMM", SDTARMVSHIMM>;
def ARMvshruImm  : SDNode<"ARMISD::VSHRuIMM", SDTARMVSHIMM>;
def ARMvshls     : SDNode<"ARMISD::VSHLs", SDTARMVSH>;
def ARMvshlu     : SDNode<"ARMISD::VSHLu", SDTARMVSH>;

def SDTARMVMULL   : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisInt<1>,
                                         SDTCisSameAs<1, 2>]>;
def ARMvmulls    : SDNode<"ARMISD::VMULLs", SDTARMVMULL>;
def ARMvmullu    : SDNode<"ARMISD::VMULLu", SDTARMVMULL>;

def SDTARMVCMP    : SDTypeProfile<1, 3, [SDTCisInt<0>, SDTCisSameAs<1, 2>,
                                         SDTCisInt<3>]>;
def SDTARMVCMPZ   : SDTypeProfile<1, 2, [SDTCisInt<2>]>;

def ARMvcmp      : SDNode<"ARMISD::VCMP", SDTARMVCMP>;
def ARMvcmpz     : SDNode<"ARMISD::VCMPZ", SDTARMVCMPZ>;

// 'VECTOR_REG_CAST' is an operation that reinterprets the contents of a
// vector register as a different vector type, without changing the contents of
// the register. It differs from 'bitconvert' in that bitconvert reinterprets
// the _memory_ storage format of the vector, whereas VECTOR_REG_CAST
// reinterprets the _register_ format - and in big-endian, the memory and
// register formats are different, so they are different operations.
//
// For example, 'VECTOR_REG_CAST' between v8i16 and v16i8 will map the LSB of
// the zeroth i16 lane to the zeroth i8 lane, regardless of system endianness,
// whereas 'bitconvert' will map it to the high byte in big-endian mode,
// because that's what (MVE) VSTRH.16 followed by VLDRB.8 would do. So the
// bitconvert would have to emit a VREV16.8 instruction, whereas the
// VECTOR_REG_CAST emits no code at all if the vector is already in a register.
def ARMVectorRegCastImpl : SDNode<"ARMISD::VECTOR_REG_CAST", SDTUnaryOp>;

// In little-endian, VECTOR_REG_CAST is often turned into bitconvert during
// lowering (because in that situation they're identical). So an isel pattern
// that needs to match something that's _logically_ a VECTOR_REG_CAST must
// _physically_ match a different node type depending on endianness.
//
// This 'PatFrags' instance is a centralized facility to make that easy. It
// matches VECTOR_REG_CAST in either endianness, and also bitconvert in the
// endianness where it's equivalent.
def ARMVectorRegCast: PatFrags<
    (ops node:$x), [(ARMVectorRegCastImpl node:$x), (bitconvert node:$x)], [{
       // Reject a match against bitconvert (aka ISD::BITCAST) if big-endian
       return !(CurDAG->getDataLayout().isBigEndian() &&
                N->getOpcode() == ISD::BITCAST);
    }]>;

//===----------------------------------------------------------------------===//
// ARM Flag Definitions.

class RegConstraint<string C> {
  string Constraints = C;
}

// ARMCC condition codes. See ARMCC::CondCodes
def ARMCCeq : PatLeaf<(i32 0)>;
def ARMCCne : PatLeaf<(i32 1)>;
def ARMCChs : PatLeaf<(i32 2)>;
def ARMCClo : PatLeaf<(i32 3)>;
def ARMCCmi : PatLeaf<(i32 4)>;
def ARMCCpl : PatLeaf<(i32 5)>;
def ARMCCvs : PatLeaf<(i32 6)>;
def ARMCCvc : PatLeaf<(i32 7)>;
def ARMCChi : PatLeaf<(i32 8)>;
def ARMCCls : PatLeaf<(i32 9)>;
def ARMCCge : PatLeaf<(i32 10)>;
def ARMCClt : PatLeaf<(i32 11)>;
def ARMCCgt : PatLeaf<(i32 12)>;
def ARMCCle : PatLeaf<(i32 13)>;
def ARMCCal : PatLeaf<(i32 14)>;

// VCC predicates. See ARMVCC::VPTCodes
def ARMVCCNone : PatLeaf<(i32 0)>;
def ARMVCCThen : PatLeaf<(i32 1)>;
def ARMVCCElse : PatLeaf<(i32 2)>;

//===----------------------------------------------------------------------===//
//  ARM specific transformation functions and pattern fragments.
//

// imm_neg_XFORM - Return the negation of an i32 immediate value.
def imm_neg_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(-(int)N->getZExtValue(), SDLoc(N), MVT::i32);
}]>;

// imm_not_XFORM - Return the complement of a i32 immediate value.
def imm_not_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(~(int)N->getZExtValue(), SDLoc(N), MVT::i32);
}]>;
def gi_imm_not_XFORM : GICustomOperandRenderer<"renderInvertedImm">,
  GISDNodeXFormEquiv<imm_not_XFORM>;

// asr_imm_XFORM - Returns a shift immediate with bit {5} set to 1
def asr_imm_XFORM : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(0x20 | N->getZExtValue(), SDLoc(N), MVT:: i32);
}]>;

/// imm16_31 predicate - True if the 32-bit immediate is in the range [16,31].
def imm16_31 : ImmLeaf<i32, [{
  return (int32_t)Imm >= 16 && (int32_t)Imm < 32;
}]>;

// sext_16_node predicate - True if the SDNode is sign-extended 16 or more bits.
def sext_16_node : PatLeaf<(i32 GPR:$a), [{
  return CurDAG->ComputeNumSignBits(SDValue(N,0)) >= 17;
}]>;

def sext_bottom_16 : PatFrag<(ops node:$a),
                             (sext_inreg node:$a, i16)>;
def sext_top_16 : PatFrag<(ops node:$a),
                          (i32 (sra node:$a, (i32 16)))>;

def bb_mul : PatFrag<(ops node:$a, node:$b),
                     (mul (sext_bottom_16 node:$a), (sext_bottom_16 node:$b))>;
def bt_mul : PatFrag<(ops node:$a, node:$b),
                     (mul (sext_bottom_16 node:$a), (sra node:$b, (i32 16)))>;
def tb_mul : PatFrag<(ops node:$a, node:$b),
                     (mul (sra node:$a, (i32 16)), (sext_bottom_16 node:$b))>;
def tt_mul : PatFrag<(ops node:$a, node:$b),
                     (mul (sra node:$a, (i32 16)), (sra node:$b, (i32 16)))>;

/// Split a 32-bit immediate into two 16 bit parts.
def hi16 : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((uint32_t)N->getZExtValue() >> 16, SDLoc(N),
                                   MVT::i32);
}]>;

def lo16AllZero : PatLeaf<(i32 imm), [{
  // Returns true if all low 16-bits are 0.
  return (((uint32_t)N->getZExtValue()) & 0xFFFFUL) == 0;
}], hi16>;

// top16Zero - answer true if the upper 16 bits of $src are 0, false otherwise
def top16Zero: PatLeaf<(i32 GPR:$src), [{
  return !SDValue(N,0)->getValueType(0).isVector() &&
         CurDAG->MaskedValueIsZero(SDValue(N,0), APInt::getHighBitsSet(32, 16));
  }]>;

// topbitsallzero - Return true if all bits except the lowest bit are known zero
def topbitsallzero32 : PatLeaf<(i32 GPRwithZR:$src), [{
  return SDValue(N,0)->getValueType(0) == MVT::i32 &&
         CurDAG->MaskedValueIsZero(SDValue(N,0), APInt::getHighBitsSet(32, 31));
  }]>;

class BinOpFrag<dag res> : PatFrag<(ops node:$LHS, node:$RHS), res>;
class UnOpFrag <dag res> : PatFrag<(ops node:$Src), res>;

// An 'and' node with a single use.
def and_su : PatFrag<(ops node:$lhs, node:$rhs), (and node:$lhs, node:$rhs), [{
  return N->hasOneUse();
}]>;

// An 'xor' node with a single use.
def xor_su : PatFrag<(ops node:$lhs, node:$rhs), (xor node:$lhs, node:$rhs), [{
  return N->hasOneUse();
}]>;

// An 'fmul' node with a single use.
def fmul_su : PatFrag<(ops node:$lhs, node:$rhs), (fmul node:$lhs, node:$rhs),[{
  return N->hasOneUse();
}]>;

// An 'fadd' node which checks for single non-hazardous use.
def fadd_mlx : PatFrag<(ops node:$lhs, node:$rhs),(fadd node:$lhs, node:$rhs),[{
  return hasNoVMLxHazardUse(N);
}]>;

// An 'fsub' node which checks for single non-hazardous use.
def fsub_mlx : PatFrag<(ops node:$lhs, node:$rhs),(fsub node:$lhs, node:$rhs),[{
  return hasNoVMLxHazardUse(N);
}]>;

// An 'fadd' node which can be contracted into a fma
def fadd_contract : PatFrag<(ops node:$lhs, node:$rhs),(fadd node:$lhs, node:$rhs),[{
  return N->getFlags().hasAllowContract();
}]>;

def imm_even : ImmLeaf<i32, [{ return (Imm & 1) == 0; }]>;
def imm_odd : ImmLeaf<i32, [{ return (Imm & 1) == 1; }]>;

def asr_imm : ImmLeaf<i32, [{ return Imm > 0 && Imm <= 32; }], asr_imm_XFORM>;

//===----------------------------------------------------------------------===//
// NEON/MVE pattern fragments
//

// Extract D sub-registers of Q registers.
def DSubReg_i8_reg  : SDNodeXForm<imm, [{
  assert(ARM::dsub_7 == ARM::dsub_0+7 && "Unexpected subreg numbering");
  return CurDAG->getTargetConstant(ARM::dsub_0 + N->getZExtValue()/8, SDLoc(N),
                                   MVT::i32);
}]>;
def DSubReg_i16_reg : SDNodeXForm<imm, [{
  assert(ARM::dsub_7 == ARM::dsub_0+7 && "Unexpected subreg numbering");
  return CurDAG->getTargetConstant(ARM::dsub_0 + N->getZExtValue()/4, SDLoc(N),
                                   MVT::i32);
}]>;
def DSubReg_i32_reg : SDNodeXForm<imm, [{
  assert(ARM::dsub_7 == ARM::dsub_0+7 && "Unexpected subreg numbering");
  return CurDAG->getTargetConstant(ARM::dsub_0 + N->getZExtValue()/2, SDLoc(N),
                                   MVT::i32);
}]>;
def DSubReg_f64_reg : SDNodeXForm<imm, [{
  assert(ARM::dsub_7 == ARM::dsub_0+7 && "Unexpected subreg numbering");
  return CurDAG->getTargetConstant(ARM::dsub_0 + N->getZExtValue(), SDLoc(N),
                                   MVT::i32);
}]>;

// Extract S sub-registers of Q/D registers.
def SSubReg_f32_reg : SDNodeXForm<imm, [{
  assert(ARM::ssub_3 == ARM::ssub_0+3 && "Unexpected subreg numbering");
  return CurDAG->getTargetConstant(ARM::ssub_0 + N->getZExtValue(), SDLoc(N),
                                   MVT::i32);
}]>;

// Extract S sub-registers of Q/D registers containing a given f16/bf16 lane.
def SSubReg_f16_reg : SDNodeXForm<imm, [{
  assert(ARM::ssub_3 == ARM::ssub_0+3 && "Unexpected subreg numbering");
  return CurDAG->getTargetConstant(ARM::ssub_0 + N->getZExtValue()/2, SDLoc(N),
                                   MVT::i32);
}]>;

// Translate lane numbers from Q registers to D subregs.
def SubReg_i8_lane  : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() & 7, SDLoc(N), MVT::i32);
}]>;
def SubReg_i16_lane : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() & 3, SDLoc(N), MVT::i32);
}]>;
def SubReg_i32_lane : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(N->getZExtValue() & 1, SDLoc(N), MVT::i32);
}]>;


def ARMimmAllZerosV: PatLeaf<(bitconvert (v4i32 (ARMvmovImm (i32 0))))>;
def ARMimmAllZerosD: PatLeaf<(bitconvert (v2i32 (ARMvmovImm (i32 0))))>;
def ARMimmAllOnesV: PatLeaf<(bitconvert (v16i8 (ARMvmovImm (i32 0xEFF))))>;
def ARMimmAllOnesD: PatLeaf<(bitconvert (v8i8 (ARMvmovImm (i32 0xEFF))))>;

def ARMimmOneV: PatLeaf<(ARMvmovImm (i32 timm)), [{
  ConstantSDNode *ConstVal = cast<ConstantSDNode>(N->getOperand(0));
  unsigned EltBits = 0;
  uint64_t EltVal = ARM_AM::decodeVMOVModImm(ConstVal->getZExtValue(), EltBits);
  return (EltBits == N->getValueType(0).getScalarSizeInBits() && EltVal == 0x01);
}]>;


//===----------------------------------------------------------------------===//
// Operand Definitions.
//

// Immediate operands with a shared generic asm render method.
class ImmAsmOperand<int Low, int High> : AsmOperandClass {
  let RenderMethod = "addImmOperands";
  let PredicateMethod = "isImmediate<" # Low # "," # High # ">";
  let DiagnosticString = "operand must be an immediate in the range [" # Low # "," # High # "]";
}

class ImmAsmOperandMinusOne<int Low, int High> : AsmOperandClass {
  let PredicateMethod = "isImmediate<" # Low # "," # High # ">";
  let DiagnosticType = "ImmRange" # Low # "_" # High;
  let DiagnosticString = "operand must be an immediate in the range [" # Low # "," # High # "]";
}

// Operands that are part of a memory addressing mode.
class MemOperand : Operand<i32> { let OperandType = "OPERAND_MEMORY"; }

// Branch target.
// FIXME: rename brtarget to t2_brtarget
def brtarget : Operand<OtherVT> {
  let EncoderMethod = "getBranchTargetOpValue";
  let OperandType = "OPERAND_PCREL";
  let DecoderMethod = "DecodeT2BROperand";
}

// Branches targeting ARM-mode must be divisible by 4 if they're a raw
// immediate.
def ARMBranchTarget : AsmOperandClass {
  let Name = "ARMBranchTarget";
}

// Branches targeting Thumb-mode must be divisible by 2 if they're a raw
// immediate.
def ThumbBranchTarget : AsmOperandClass {
  let Name = "ThumbBranchTarget";
}

def arm_br_target : Operand<OtherVT> {
  let ParserMatchClass = ARMBranchTarget;
  let EncoderMethod = "getARMBranchTargetOpValue";
  let OperandType = "OPERAND_PCREL";
}

// Call target for ARM. Handles conditional/unconditional
// FIXME: rename bl_target to t2_bltarget?
def arm_bl_target : Operand<i32> {
  let ParserMatchClass = ARMBranchTarget;
  let EncoderMethod = "getARMBLTargetOpValue";
  let OperandType = "OPERAND_PCREL";
}

// Target for BLX *from* ARM mode.
def arm_blx_target : Operand<i32> {
  let ParserMatchClass = ThumbBranchTarget;
  let EncoderMethod = "getARMBLXTargetOpValue";
  let OperandType = "OPERAND_PCREL";
}

// A list of registers separated by comma. Used by load/store multiple.
def RegListAsmOperand : AsmOperandClass { let Name = "RegList"; }
def reglist : Operand<i32> {
  let EncoderMethod = "getRegisterListOpValue";
  let ParserMatchClass = RegListAsmOperand;
  let PrintMethod = "printRegisterList";
  let DecoderMethod = "DecodeRegListOperand";
}

// A list of general purpose registers and APSR separated by comma.
// Used by CLRM
def RegListWithAPSRAsmOperand : AsmOperandClass { let Name = "RegListWithAPSR"; }
def reglist_with_apsr : Operand<i32> {
  let EncoderMethod = "getRegisterListOpValue";
  let ParserMatchClass = RegListWithAPSRAsmOperand;
  let PrintMethod = "printRegisterList";
  let DecoderMethod = "DecodeRegListOperand";
}

def GPRPairOp : RegisterOperand<GPRPair, "printGPRPairOperand">;

def DPRRegListAsmOperand : AsmOperandClass {
  let Name = "DPRRegList";
  let DiagnosticType = "DPR_RegList";
}
def dpr_reglist : Operand<i32> {
  let EncoderMethod = "getRegisterListOpValue";
  let ParserMatchClass = DPRRegListAsmOperand;
  let PrintMethod = "printRegisterList";
  let DecoderMethod = "DecodeDPRRegListOperand";
}

def SPRRegListAsmOperand : AsmOperandClass {
  let Name = "SPRRegList";
  let DiagnosticString = "operand must be a list of registers in range [s0, s31]";
}
def spr_reglist : Operand<i32> {
  let EncoderMethod = "getRegisterListOpValue";
  let ParserMatchClass = SPRRegListAsmOperand;
  let PrintMethod = "printRegisterList";
  let DecoderMethod = "DecodeSPRRegListOperand";
}

def FPSRegListWithVPRAsmOperand : AsmOperandClass { let Name =
    "FPSRegListWithVPR"; }
def fp_sreglist_with_vpr : Operand<i32> {
  let EncoderMethod = "getRegisterListOpValue";
  let ParserMatchClass = FPSRegListWithVPRAsmOperand;
  let PrintMethod = "printRegisterList";
}
def FPDRegListWithVPRAsmOperand : AsmOperandClass { let Name =
    "FPDRegListWithVPR"; }
def fp_dreglist_with_vpr : Operand<i32> {
  let EncoderMethod = "getRegisterListOpValue";
  let ParserMatchClass = FPDRegListWithVPRAsmOperand;
  let PrintMethod = "printRegisterList";
}

// An operand for the CONSTPOOL_ENTRY pseudo-instruction.
def cpinst_operand : Operand<i32> {
  let PrintMethod = "printCPInstOperand";
}

// Local PC labels.
def pclabel : Operand<i32> {
  let PrintMethod = "printPCLabel";
}

// ADR instruction labels.
def AdrLabelAsmOperand : AsmOperandClass { let Name = "AdrLabel"; }
def adrlabel : Operand<i32> {
  let EncoderMethod = "getAdrLabelOpValue";
  let ParserMatchClass = AdrLabelAsmOperand;
  let PrintMethod = "printAdrLabelOperand<0>";
}

def neon_vcvt_imm32 : Operand<i32> {
  let EncoderMethod = "getNEONVcvtImm32OpValue";
  let DecoderMethod = "DecodeVCVTImmOperand";
}

// rot_imm: An integer that encodes a rotate amount. Must be 8, 16, or 24.
def rot_imm_XFORM: SDNodeXForm<imm, [{
  switch (N->getZExtValue()){
  default: llvm_unreachable(nullptr);
  case 0:  return CurDAG->getTargetConstant(0, SDLoc(N), MVT::i32);
  case 8:  return CurDAG->getTargetConstant(1, SDLoc(N), MVT::i32);
  case 16: return CurDAG->getTargetConstant(2, SDLoc(N), MVT::i32);
  case 24: return CurDAG->getTargetConstant(3, SDLoc(N), MVT::i32);
  }
}]>;
def RotImmAsmOperand : AsmOperandClass {
  let Name = "RotImm";
  let ParserMethod = "parseRotImm";
}
def rot_imm : Operand<i32>, PatLeaf<(i32 imm), [{
    int32_t v = N->getZExtValue();
    return v == 8 || v == 16 || v == 24; }],
    rot_imm_XFORM> {
  let PrintMethod = "printRotImmOperand";
  let ParserMatchClass = RotImmAsmOperand;
}

// Power-of-two operand for MVE VIDUP and friends, which encode
// {1,2,4,8} as its log to base 2, i.e. as {0,1,2,3} respectively
def MVE_VIDUP_imm_asmoperand : AsmOperandClass {
  let Name = "VIDUP_imm";
  let PredicateMethod = "isPowerTwoInRange<1,8>";
  let RenderMethod = "addPowerTwoOperands";
  let DiagnosticString = "vector increment immediate must be 1, 2, 4 or 8";
}
def MVE_VIDUP_imm : Operand<i32> {
  let EncoderMethod = "getPowerTwoOpValue";
  let DecoderMethod = "DecodePowerTwoOperand<0,3>";
  let ParserMatchClass = MVE_VIDUP_imm_asmoperand;
}

// Pair vector indexing
class MVEPairVectorIndexOperand<string start, string end> : AsmOperandClass {
  let Name = "MVEPairVectorIndex"#start;
  let RenderMethod = "addMVEPairVectorIndexOperands";
  let PredicateMethod = "isMVEPairVectorIndex<"#start#", "#end#">";
}

class MVEPairVectorIndex<string opval> : Operand<i32> {
  let PrintMethod = "printVectorIndex";
  let EncoderMethod = "getMVEPairVectorIndexOpValue<"#opval#">";
  let DecoderMethod = "DecodeMVEPairVectorIndexOperand<"#opval#">";
  let MIOperandInfo = (ops i32imm);
}

def MVEPairVectorIndex0 : MVEPairVectorIndex<"0"> {
  let ParserMatchClass = MVEPairVectorIndexOperand<"0", "1">;
}

def MVEPairVectorIndex2 : MVEPairVectorIndex<"2"> {
  let ParserMatchClass = MVEPairVectorIndexOperand<"2", "3">;
}

// Vector indexing
class MVEVectorIndexOperand<int NumLanes> : AsmOperandClass {
  let Name = "MVEVectorIndex"#NumLanes;
  let RenderMethod = "addMVEVectorIndexOperands";
  let PredicateMethod = "isVectorIndexInRange<"#NumLanes#">";
}

class MVEVectorIndex<int NumLanes> : Operand<i32> {
  let PrintMethod = "printVectorIndex";
  let ParserMatchClass = MVEVectorIndexOperand<NumLanes>;
  let MIOperandInfo = (ops i32imm);
}

// shift_imm: An integer that encodes a shift amount and the type of shift
// (asr or lsl). The 6-bit immediate encodes as:
//    {5}     0 ==> lsl
//            1     asr
//    {4-0}   imm5 shift amount.
//            asr #32 encoded as imm5 == 0.
def ShifterImmAsmOperand : AsmOperandClass {
  let Name = "ShifterImm";
  let ParserMethod = "parseShifterImm";
}
def shift_imm : Operand<i32> {
  let PrintMethod = "printShiftImmOperand";
  let ParserMatchClass = ShifterImmAsmOperand;
}

// shifter_operand operands: so_reg_reg, so_reg_imm, and mod_imm.
def ShiftedRegAsmOperand : AsmOperandClass { let Name = "RegShiftedReg"; }
def so_reg_reg : Operand<i32>,  // reg reg imm
                 ComplexPattern<i32, 3, "SelectRegShifterOperand",
                                [shl, srl, sra, rotr]> {
  let EncoderMethod = "getSORegRegOpValue";
  let PrintMethod = "printSORegRegOperand";
  let DecoderMethod = "DecodeSORegRegOperand";
  let ParserMatchClass = ShiftedRegAsmOperand;
  let MIOperandInfo = (ops GPRnopc, GPRnopc, i32imm);
}

def ShiftedImmAsmOperand : AsmOperandClass { let Name = "RegShiftedImm"; }
def so_reg_imm : Operand<i32>, // reg imm
                 ComplexPattern<i32, 2, "SelectImmShifterOperand",
                                [shl, srl, sra, rotr]> {
  let EncoderMethod = "getSORegImmOpValue";
  let PrintMethod = "printSORegImmOperand";
  let DecoderMethod = "DecodeSORegImmOperand";
  let ParserMatchClass = ShiftedImmAsmOperand;
  let MIOperandInfo = (ops GPR, i32imm);
}

// FIXME: Does this need to be distinct from so_reg?
def shift_so_reg_reg : Operand<i32>,    // reg reg imm
                   ComplexPattern<i32, 3, "SelectShiftRegShifterOperand",
                                  [shl,srl,sra,rotr]> {
  let EncoderMethod = "getSORegRegOpValue";
  let PrintMethod = "printSORegRegOperand";
  let DecoderMethod = "DecodeSORegRegOperand";
  let ParserMatchClass = ShiftedRegAsmOperand;
  let MIOperandInfo = (ops GPR, GPR, i32imm);
}

// FIXME: Does this need to be distinct from so_reg?
def shift_so_reg_imm : Operand<i32>,    // reg reg imm
                   ComplexPattern<i32, 2, "SelectShiftImmShifterOperand",
                                  [shl,srl,sra,rotr]> {
  let EncoderMethod = "getSORegImmOpValue";
  let PrintMethod = "printSORegImmOperand";
  let DecoderMethod = "DecodeSORegImmOperand";
  let ParserMatchClass = ShiftedImmAsmOperand;
  let MIOperandInfo = (ops GPR, i32imm);
}

// mod_imm: match a 32-bit immediate operand, which can be encoded into
// a 12-bit immediate; an 8-bit integer and a 4-bit rotator (See ARMARM
// - "Modified Immediate Constants"). Within the MC layer we keep this
// immediate in its encoded form.
def ModImmAsmOperand: AsmOperandClass {
  let Name = "ModImm";
  let ParserMethod = "parseModImm";
}
def mod_imm : Operand<i32>, ImmLeaf<i32, [{
    return ARM_AM::getSOImmVal(Imm) != -1;
  }]> {
  let EncoderMethod = "getModImmOpValue";
  let PrintMethod = "printModImmOperand";
  let ParserMatchClass = ModImmAsmOperand;
}

// Note: the patterns mod_imm_not and mod_imm_neg do not require an encoder
// method and such, as they are only used on aliases (Pat<> and InstAlias<>).
// The actual parsing, encoding, decoding are handled by the destination
// instructions, which use mod_imm.

def ModImmNotAsmOperand : AsmOperandClass { let Name = "ModImmNot"; }
def mod_imm_not : Operand<i32>, ImmLeaf<i32, [{
    return ARM_AM::getSOImmVal(~(uint32_t)Imm) != -1;
  }], imm_not_XFORM> {
  let ParserMatchClass = ModImmNotAsmOperand;
}

def ModImmNegAsmOperand : AsmOperandClass { let Name = "ModImmNeg"; }
def mod_imm_neg : Operand<i32>, PatLeaf<(imm), [{
    unsigned Value = -(unsigned)N->getZExtValue();
    return Value && ARM_AM::getSOImmVal(Value) != -1;
  }], imm_neg_XFORM> {
  let ParserMatchClass = ModImmNegAsmOperand;
}

/// arm_i32imm - True for +V6T2, or when isSOImmTwoParVal()
def arm_i32imm : IntImmLeaf<i32, [{
  if (Subtarget->useMovt())
    return true;
  if (ARM_AM::isSOImmTwoPartVal(Imm.getZExtValue()))
    return true;
  return ARM_AM::isSOImmTwoPartValNeg(Imm.getZExtValue());
}]>;

/// imm0_1 predicate - Immediate in the range [0,1].
def Imm0_1AsmOperand: ImmAsmOperand<0,1> { let Name = "Imm0_1"; }
def imm0_1 : Operand<i32> { let ParserMatchClass = Imm0_1AsmOperand; }

/// imm0_3 predicate - Immediate in the range [0,3].
def Imm0_3AsmOperand: ImmAsmOperand<0,3> { let Name = "Imm0_3"; }
def imm0_3 : Operand<i32> { let ParserMatchClass = Imm0_3AsmOperand; }

/// imm0_7 predicate - Immediate in the range [0,7].
def Imm0_7AsmOperand: ImmAsmOperand<0,7> {
  let Name = "Imm0_7";
}
def imm0_7 : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm < 8;
}]> {
  let ParserMatchClass = Imm0_7AsmOperand;
}

/// imm8_255 predicate - Immediate in the range [8,255].
def Imm8_255AsmOperand: ImmAsmOperand<8,255> { let Name = "Imm8_255"; }
def imm8_255 : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 8 && Imm < 256;
}]> {
  let ParserMatchClass = Imm8_255AsmOperand;
}

/// imm8 predicate - Immediate is exactly 8.
def Imm8AsmOperand: ImmAsmOperand<8,8> { let Name = "Imm8"; }
def imm8 : Operand<i32>, ImmLeaf<i32, [{ return Imm == 8; }]> {
  let ParserMatchClass = Imm8AsmOperand;
}

/// imm16 predicate - Immediate is exactly 16.
def Imm16AsmOperand: ImmAsmOperand<16,16> { let Name = "Imm16"; }
def imm16 : Operand<i32>, ImmLeaf<i32, [{ return Imm == 16; }]> {
  let ParserMatchClass = Imm16AsmOperand;
}

/// imm32 predicate - Immediate is exactly 32.
def Imm32AsmOperand: ImmAsmOperand<32,32> { let Name = "Imm32"; }
def imm32 : Operand<i32>, ImmLeaf<i32, [{ return Imm == 32; }]> {
  let ParserMatchClass = Imm32AsmOperand;
}

def imm8_or_16 : ImmLeaf<i32, [{ return Imm == 8 || Imm == 16;}]>;

/// imm1_7 predicate - Immediate in the range [1,7].
def Imm1_7AsmOperand: ImmAsmOperand<1,7> { let Name = "Imm1_7"; }
def imm1_7 : Operand<i32>, ImmLeaf<i32, [{ return Imm > 0 && Imm < 8; }]> {
  let ParserMatchClass = Imm1_7AsmOperand;
}

/// imm1_15 predicate - Immediate in the range [1,15].
def Imm1_15AsmOperand: ImmAsmOperand<1,15> { let Name = "Imm1_15"; }
def imm1_15 : Operand<i32>, ImmLeaf<i32, [{ return Imm > 0 && Imm < 16; }]> {
  let ParserMatchClass = Imm1_15AsmOperand;
}

/// imm1_31 predicate - Immediate in the range [1,31].
def Imm1_31AsmOperand: ImmAsmOperand<1,31> { let Name = "Imm1_31"; }
def imm1_31 : Operand<i32>, ImmLeaf<i32, [{ return Imm > 0 && Imm < 32; }]> {
  let ParserMatchClass = Imm1_31AsmOperand;
}

/// imm0_15 predicate - Immediate in the range [0,15].
def Imm0_15AsmOperand: ImmAsmOperand<0,15> {
  let Name = "Imm0_15";
}
def imm0_15 : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm < 16;
}]> {
  let ParserMatchClass = Imm0_15AsmOperand;
}

/// imm0_31 predicate - True if the 32-bit immediate is in the range [0,31].
def Imm0_31AsmOperand: ImmAsmOperand<0,31> { let Name = "Imm0_31"; }
def imm0_31 : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm < 32;
}]> {
  let ParserMatchClass = Imm0_31AsmOperand;
}

/// imm0_32 predicate - True if the 32-bit immediate is in the range [0,32].
def Imm0_32AsmOperand: ImmAsmOperand<0,32> { let Name = "Imm0_32"; }
def imm0_32 : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm < 33;
}]> {
  let ParserMatchClass = Imm0_32AsmOperand;
}

/// imm0_63 predicate - True if the 32-bit immediate is in the range [0,63].
def Imm0_63AsmOperand: ImmAsmOperand<0,63> { let Name = "Imm0_63"; }
def imm0_63 : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm < 64;
}]> {
  let ParserMatchClass = Imm0_63AsmOperand;
}

/// imm0_239 predicate - Immediate in the range [0,239].
def Imm0_239AsmOperand : ImmAsmOperand<0,239> {
  let Name = "Imm0_239";
}
def imm0_239 : Operand<i32>, ImmLeaf<i32, [{ return Imm >= 0 && Imm < 240; }]> {
  let ParserMatchClass = Imm0_239AsmOperand;
}

/// imm0_255 predicate - Immediate in the range [0,255].
def Imm0_255AsmOperand : ImmAsmOperand<0,255> { let Name = "Imm0_255"; }
def imm0_255 : Operand<i32>, ImmLeaf<i32, [{ return Imm >= 0 && Imm < 256; }]> {
  let ParserMatchClass = Imm0_255AsmOperand;
}

// imm0_255_expr - For Thumb1 movs/adds - 8-bit immediate that can also reference
// a relocatable expression.
def Imm0_255ExprAsmOperand: AsmOperandClass {
  let Name = "Imm0_255Expr";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "operand must be an immediate in the range [0,255] or a relocatable expression";
}

def imm0_255_expr : Operand<i32>, ImmLeaf<i32, [{ return Imm >= 0 && Imm < 256; }]> {
  let EncoderMethod = "getHiLoImmOpValue";
  let ParserMatchClass = Imm0_255ExprAsmOperand;
}

/// imm0_65535 - An immediate is in the range [0,65535].
def Imm0_65535AsmOperand: ImmAsmOperand<0,65535> { let Name = "Imm0_65535"; }
def imm0_65535 : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm < 65536;
}]> {
  let ParserMatchClass = Imm0_65535AsmOperand;
}

// imm0_65535_neg - An immediate whose negative value is in the range [0.65535].
def imm0_65535_neg : Operand<i32>, ImmLeaf<i32, [{
  return -Imm >= 0 && -Imm < 65536;
}]>;

// imm0_65535_expr - For movt/movw - 16-bit immediate that can also reference
// a relocatable expression.
//
// FIXME: This really needs a Thumb version separate from the ARM version.
// While the range is the same, and can thus use the same match class,
// the encoding is different so it should have a different encoder method.
def Imm0_65535ExprAsmOperand: AsmOperandClass {
  let Name = "Imm0_65535Expr";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "operand must be an immediate in the range [0,0xffff] or a relocatable expression";
}

def imm0_65535_expr : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm < 65536;
}]> {
  let EncoderMethod = "getHiLoImmOpValue";
  let ParserMatchClass = Imm0_65535ExprAsmOperand;
}

def Imm256_65535ExprAsmOperand: ImmAsmOperand<256,65535> { let Name = "Imm256_65535Expr"; }
def imm256_65535_expr : Operand<i32> {
  let ParserMatchClass = Imm256_65535ExprAsmOperand;
}

/// imm24b - True if the 32-bit immediate is encodable in 24 bits.
def Imm24bitAsmOperand: ImmAsmOperand<0,0xffffff> {
  let Name = "Imm24bit";
  let DiagnosticString = "operand must be an immediate in the range [0,0xffffff]";
}
def imm24b : Operand<i32>, ImmLeaf<i32, [{
  return Imm >= 0 && Imm <= 0xffffff;
}]> {
  let ParserMatchClass = Imm24bitAsmOperand;
}


/// bf_inv_mask_imm predicate - An AND mask to clear an arbitrary width bitfield
/// e.g., 0xf000ffff
def BitfieldAsmOperand : AsmOperandClass {
  let Name = "Bitfield";
  let ParserMethod = "parseBitfield";
}

def bf_inv_mask_imm : Operand<i32>,
                      PatLeaf<(imm), [{
  return ARM::isBitFieldInvertedMask(N->getZExtValue());
}] > {
  let EncoderMethod = "getBitfieldInvertedMaskOpValue";
  let PrintMethod = "printBitfieldInvMaskImmOperand";
  let DecoderMethod = "DecodeBitfieldMaskOperand";
  let ParserMatchClass = BitfieldAsmOperand;
  let GISelPredicateCode = [{
    // There's better methods of implementing this check. IntImmLeaf<> would be
    // equivalent and have less boilerplate but we need a test for C++
    // predicates and this one causes new rules to be imported into GlobalISel
    // without requiring additional features first.
    const auto &MO = MI.getOperand(1);
    if (!MO.isCImm())
      return false;
    return ARM::isBitFieldInvertedMask(MO.getCImm()->getZExtValue());
  }];
}

def imm1_32_XFORM: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((int)N->getZExtValue() - 1, SDLoc(N),
                                   MVT::i32);
}]>;
def Imm1_32AsmOperand: ImmAsmOperandMinusOne<1,32> {
  let Name = "Imm1_32";
}
def imm1_32 : Operand<i32>, PatLeaf<(imm), [{
   uint64_t Imm = N->getZExtValue();
   return Imm > 0 && Imm <= 32;
 }],
    imm1_32_XFORM> {
  let PrintMethod = "printImmPlusOneOperand";
  let ParserMatchClass = Imm1_32AsmOperand;
}

def imm1_16_XFORM: SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant((int)N->getZExtValue() - 1, SDLoc(N),
                                   MVT::i32);
}]>;
def Imm1_16AsmOperand: ImmAsmOperandMinusOne<1,16> { let Name = "Imm1_16"; }
def imm1_16 : Operand<i32>, ImmLeaf<i32, [{
    return Imm > 0 && Imm <= 16;
  }],
    imm1_16_XFORM> {
  let PrintMethod = "printImmPlusOneOperand";
  let ParserMatchClass = Imm1_16AsmOperand;
}

def MVEShiftImm1_7AsmOperand: ImmAsmOperand<1,7> {
  let Name = "MVEShiftImm1_7";
  // Reason we're doing this is because instruction vshll.s8 t1 encoding
  // accepts 1,7 but the t2 encoding accepts 8.  By doing this we can get a
  // better diagnostic message if someone uses bigger immediate than the t1/t2
  // encodings allow.
  let DiagnosticString = "operand must be an immediate in the range [1,8]";
}
def mve_shift_imm1_7 : Operand<i32>,
    // SelectImmediateInRange / isScaledConstantInRange uses a
    // half-open interval, so the parameters <1,8> mean 1-7 inclusive
    ComplexPattern<i32, 1, "SelectImmediateInRange<1,8>", [], []> {
  let ParserMatchClass = MVEShiftImm1_7AsmOperand;
  let EncoderMethod = "getMVEShiftImmOpValue";
}

def MVEShiftImm1_15AsmOperand: ImmAsmOperand<1,15> {
  let Name = "MVEShiftImm1_15";
  // Reason we're doing this is because instruction vshll.s16 t1 encoding
  // accepts 1,15 but the t2 encoding accepts 16.  By doing this we can get a
  // better diagnostic message if someone uses bigger immediate than the t1/t2
  // encodings allow.
  let DiagnosticString = "operand must be an immediate in the range [1,16]";
}
def mve_shift_imm1_15 : Operand<i32>,
    // SelectImmediateInRange / isScaledConstantInRange uses a
    // half-open interval, so the parameters <1,16> mean 1-15 inclusive
    ComplexPattern<i32, 1, "SelectImmediateInRange<1,16>", [], []> {
  let ParserMatchClass = MVEShiftImm1_15AsmOperand;
  let EncoderMethod = "getMVEShiftImmOpValue";
}

// Define ARM specific addressing modes.
// addrmode_imm12 := reg +/- imm12
//
def MemImm12OffsetAsmOperand : AsmOperandClass { let Name = "MemImm12Offset"; }
class AddrMode_Imm12 : MemOperand,
                     ComplexPattern<i32, 2, "SelectAddrModeImm12", []> {
  // 12-bit immediate operand. Note that instructions using this encode
  // #0 and #-0 differently. We flag #-0 as the magic value INT32_MIN. All other
  // immediate values are as normal.

  let EncoderMethod = "getAddrModeImm12OpValue";
  let DecoderMethod = "DecodeAddrModeImm12Operand";
  let ParserMatchClass = MemImm12OffsetAsmOperand;
  let MIOperandInfo = (ops GPR:$base, i32imm:$offsimm);
}

def addrmode_imm12 : AddrMode_Imm12 {
  let PrintMethod = "printAddrModeImm12Operand<false>";
}

def addrmode_imm12_pre : AddrMode_Imm12 {
  let PrintMethod = "printAddrModeImm12Operand<true>";
}

// ldst_so_reg := reg +/- reg shop imm
//
def MemRegOffsetAsmOperand : AsmOperandClass { let Name = "MemRegOffset"; }
def ldst_so_reg : MemOperand,
                  ComplexPattern<i32, 3, "SelectLdStSOReg", []> {
  let EncoderMethod = "getLdStSORegOpValue";
  // FIXME: Simplify the printer
  let PrintMethod = "printAddrMode2Operand";
  let DecoderMethod = "DecodeSORegMemOperand";
  let ParserMatchClass = MemRegOffsetAsmOperand;
  let MIOperandInfo = (ops GPR:$base, GPRnopc:$offsreg, i32imm:$shift);
}

// postidx_imm8 := +/- [0,255]
//
// 9 bit value:
//  {8}       1 is imm8 is non-negative. 0 otherwise.
//  {7-0}     [0,255] imm8 value.
def PostIdxImm8AsmOperand : AsmOperandClass { let Name = "PostIdxImm8"; }
def postidx_imm8 : MemOperand {
  let PrintMethod = "printPostIdxImm8Operand";
  let ParserMatchClass = PostIdxImm8AsmOperand;
  let MIOperandInfo = (ops i32imm);
}

// postidx_imm8s4 := +/- [0,1020]
//
// 9 bit value:
//  {8}       1 is imm8 is non-negative. 0 otherwise.
//  {7-0}     [0,255] imm8 value, scaled by 4.
def PostIdxImm8s4AsmOperand : AsmOperandClass { let Name = "PostIdxImm8s4"; }
def postidx_imm8s4 : MemOperand {
  let PrintMethod = "printPostIdxImm8s4Operand";
  let ParserMatchClass = PostIdxImm8s4AsmOperand;
  let MIOperandInfo = (ops i32imm);
}


// postidx_reg := +/- reg
//
def PostIdxRegAsmOperand : AsmOperandClass {
  let Name = "PostIdxReg";
  let ParserMethod = "parsePostIdxReg";
}
def postidx_reg : MemOperand {
  let EncoderMethod = "getPostIdxRegOpValue";
  let DecoderMethod = "DecodePostIdxReg";
  let PrintMethod = "printPostIdxRegOperand";
  let ParserMatchClass = PostIdxRegAsmOperand;
  let MIOperandInfo = (ops GPRnopc, i32imm);
}

def PostIdxRegShiftedAsmOperand : AsmOperandClass {
  let Name = "PostIdxRegShifted";
  let ParserMethod = "parsePostIdxReg";
}
def am2offset_reg : MemOperand,
                ComplexPattern<i32, 2, "SelectAddrMode2OffsetReg",
                [], [SDNPWantRoot]> {
  let EncoderMethod = "getAddrMode2OffsetOpValue";
  let PrintMethod = "printAddrMode2OffsetOperand";
  // When using this for assembly, it's always as a post-index offset.
  let ParserMatchClass = PostIdxRegShiftedAsmOperand;
  let MIOperandInfo = (ops GPRnopc, i32imm);
}

// FIXME: am2offset_imm should only need the immediate, not the GPR. Having
// the GPR is purely vestigal at this point.
def AM2OffsetImmAsmOperand : AsmOperandClass { let Name = "AM2OffsetImm"; }
def am2offset_imm : MemOperand,
                ComplexPattern<i32, 2, "SelectAddrMode2OffsetImm",
                [], [SDNPWantRoot]> {
  let EncoderMethod = "getAddrMode2OffsetOpValue";
  let PrintMethod = "printAddrMode2OffsetOperand";
  let ParserMatchClass = AM2OffsetImmAsmOperand;
  let MIOperandInfo = (ops GPRnopc, i32imm);
}


// addrmode3 := reg +/- reg
// addrmode3 := reg +/- imm8
//
// FIXME: split into imm vs. reg versions.
def AddrMode3AsmOperand : AsmOperandClass { let Name = "AddrMode3"; }
class AddrMode3 : MemOperand,
                  ComplexPattern<i32, 3, "SelectAddrMode3", []> {
  let EncoderMethod = "getAddrMode3OpValue";
  let ParserMatchClass = AddrMode3AsmOperand;
  let MIOperandInfo = (ops GPR:$base, GPR:$offsreg, i32imm:$offsimm);
}

def addrmode3 : AddrMode3
{
  let PrintMethod = "printAddrMode3Operand<false>";
}

def addrmode3_pre : AddrMode3
{
  let PrintMethod = "printAddrMode3Operand<true>";
}

// FIXME: split into imm vs. reg versions.
// FIXME: parser method to handle +/- register.
def AM3OffsetAsmOperand : AsmOperandClass {
  let Name = "AM3Offset";
  let ParserMethod = "parseAM3Offset";
}
def am3offset : MemOperand,
                ComplexPattern<i32, 2, "SelectAddrMode3Offset",
                               [], [SDNPWantRoot]> {
  let EncoderMethod = "getAddrMode3OffsetOpValue";
  let PrintMethod = "printAddrMode3OffsetOperand";
  let ParserMatchClass = AM3OffsetAsmOperand;
  let MIOperandInfo = (ops GPR, i32imm);
}

// ldstm_mode := {ia, ib, da, db}
//
def ldstm_mode : OptionalDefOperand<OtherVT, (ops i32), (ops (i32 1))> {
  let EncoderMethod = "getLdStmModeOpValue";
  let PrintMethod = "printLdStmModeOperand";
}

// addrmode5 := reg +/- imm8*4
//
def AddrMode5AsmOperand : AsmOperandClass { let Name = "AddrMode5"; }
class AddrMode5 : MemOperand,
                  ComplexPattern<i32, 2, "SelectAddrMode5", []> {
  let EncoderMethod = "getAddrMode5OpValue";
  let DecoderMethod = "DecodeAddrMode5Operand";
  let ParserMatchClass = AddrMode5AsmOperand;
  let MIOperandInfo = (ops GPR:$base, i32imm);
}

def addrmode5 : AddrMode5 {
   let PrintMethod = "printAddrMode5Operand<false>";
}

def addrmode5_pre : AddrMode5 {
   let PrintMethod = "printAddrMode5Operand<true>";
}

// addrmode5fp16 := reg +/- imm8*2
//
def AddrMode5FP16AsmOperand : AsmOperandClass { let Name = "AddrMode5FP16"; }
class AddrMode5FP16 : MemOperand,
                      ComplexPattern<i32, 2, "SelectAddrMode5FP16", []> {
  let EncoderMethod = "getAddrMode5FP16OpValue";
  let DecoderMethod = "DecodeAddrMode5FP16Operand";
  let ParserMatchClass = AddrMode5FP16AsmOperand;
  let MIOperandInfo = (ops GPR:$base, i32imm);
}

def addrmode5fp16 : AddrMode5FP16 {
   let PrintMethod = "printAddrMode5FP16Operand<false>";
}

// addrmode6 := reg with optional alignment
//
def AddrMode6AsmOperand : AsmOperandClass { let Name = "AlignedMemory"; }
def addrmode6 : MemOperand,
                ComplexPattern<i32, 2, "SelectAddrMode6", [], [SDNPWantParent]>{
  let PrintMethod = "printAddrMode6Operand";
  let MIOperandInfo = (ops GPR:$addr, i32imm:$align);
  let EncoderMethod = "getAddrMode6AddressOpValue";
  let DecoderMethod = "DecodeAddrMode6Operand";
  let ParserMatchClass = AddrMode6AsmOperand;
}

def am6offset : MemOperand,
                ComplexPattern<i32, 1, "SelectAddrMode6Offset",
                               [], [SDNPWantRoot]> {
  let PrintMethod = "printAddrMode6OffsetOperand";
  let MIOperandInfo = (ops GPR);
  let EncoderMethod = "getAddrMode6OffsetOpValue";
  let DecoderMethod = "DecodeGPRRegisterClass";
}

// Special version of addrmode6 to handle alignment encoding for VST1/VLD1
// (single element from one lane) for size 32.
def addrmode6oneL32 : MemOperand,
                ComplexPattern<i32, 2, "SelectAddrMode6", [], [SDNPWantParent]>{
  let PrintMethod = "printAddrMode6Operand";
  let MIOperandInfo = (ops GPR:$addr, i32imm);
  let EncoderMethod = "getAddrMode6OneLane32AddressOpValue";
}

// Base class for addrmode6 with specific alignment restrictions.
class AddrMode6Align : MemOperand,
                ComplexPattern<i32, 2, "SelectAddrMode6", [], [SDNPWantParent]>{
  let PrintMethod = "printAddrMode6Operand";
  let MIOperandInfo = (ops GPR:$addr, i32imm:$align);
  let EncoderMethod = "getAddrMode6AddressOpValue";
  let DecoderMethod = "DecodeAddrMode6Operand";
}

// Special version of addrmode6 to handle no allowed alignment encoding for
// VLD/VST instructions and checking the alignment is not specified.
def AddrMode6AlignNoneAsmOperand : AsmOperandClass {
  let Name = "AlignedMemoryNone";
  let DiagnosticString = "alignment must be omitted";
}
def addrmode6alignNone : AddrMode6Align {
  // The alignment specifier can only be omitted.
  let ParserMatchClass = AddrMode6AlignNoneAsmOperand;
}

// Special version of addrmode6 to handle 16-bit alignment encoding for
// VLD/VST instructions and checking the alignment value.
def AddrMode6Align16AsmOperand : AsmOperandClass {
  let Name = "AlignedMemory16";
  let DiagnosticString = "alignment must be 16 or omitted";
}
def addrmode6align16 : AddrMode6Align {
  // The alignment specifier can only be 16 or omitted.
  let ParserMatchClass = AddrMode6Align16AsmOperand;
}

// Special version of addrmode6 to handle 32-bit alignment encoding for
// VLD/VST instructions and checking the alignment value.
def AddrMode6Align32AsmOperand : AsmOperandClass {
  let Name = "AlignedMemory32";
  let DiagnosticString = "alignment must be 32 or omitted";
}
def addrmode6align32 : AddrMode6Align {
  // The alignment specifier can only be 32 or omitted.
  let ParserMatchClass = AddrMode6Align32AsmOperand;
}

// Special version of addrmode6 to handle 64-bit alignment encoding for
// VLD/VST instructions and checking the alignment value.
def AddrMode6Align64AsmOperand : AsmOperandClass {
  let Name = "AlignedMemory64";
  let DiagnosticString = "alignment must be 64 or omitted";
}
def addrmode6align64 : AddrMode6Align {
  // The alignment specifier can only be 64 or omitted.
  let ParserMatchClass = AddrMode6Align64AsmOperand;
}

// Special version of addrmode6 to handle 64-bit or 128-bit alignment encoding
// for VLD/VST instructions and checking the alignment value.
def AddrMode6Align64or128AsmOperand : AsmOperandClass {
  let Name = "AlignedMemory64or128";
  let DiagnosticString = "alignment must be 64, 128 or omitted";
}
def addrmode6align64or128 : AddrMode6Align {
  // The alignment specifier can only be 64, 128 or omitted.
  let ParserMatchClass = AddrMode6Align64or128AsmOperand;
}

// Special version of addrmode6 to handle 64-bit, 128-bit or 256-bit alignment
// encoding for VLD/VST instructions and checking the alignment value.
def AddrMode6Align64or128or256AsmOperand : AsmOperandClass {
  let Name = "AlignedMemory64or128or256";
  let DiagnosticString = "alignment must be 64, 128, 256 or omitted";
}
def addrmode6align64or128or256 : AddrMode6Align {
  // The alignment specifier can only be 64, 128, 256 or omitted.
  let ParserMatchClass = AddrMode6Align64or128or256AsmOperand;
}

// Special version of addrmode6 to handle alignment encoding for VLD-dup
// instructions, specifically VLD4-dup.
def addrmode6dup : MemOperand,
                ComplexPattern<i32, 2, "SelectAddrMode6", [], [SDNPWantParent]>{
  let PrintMethod = "printAddrMode6Operand";
  let MIOperandInfo = (ops GPR:$addr, i32imm);
  let EncoderMethod = "getAddrMode6DupAddressOpValue";
  // FIXME: This is close, but not quite right. The alignment specifier is
  // different.
  let ParserMatchClass = AddrMode6AsmOperand;
}

// Base class for addrmode6dup with specific alignment restrictions.
class AddrMode6DupAlign : MemOperand,
                ComplexPattern<i32, 2, "SelectAddrMode6", [], [SDNPWantParent]>{
  let PrintMethod = "printAddrMode6Operand";
  let MIOperandInfo = (ops GPR:$addr, i32imm);
  let EncoderMethod = "getAddrMode6DupAddressOpValue";
}

// Special version of addrmode6 to handle no allowed alignment encoding for
// VLD-dup instruction and checking the alignment is not specified.
def AddrMode6dupAlignNoneAsmOperand : AsmOperandClass {
  let Name = "DupAlignedMemoryNone";
  let DiagnosticString = "alignment must be omitted";
}
def addrmode6dupalignNone : AddrMode6DupAlign {
  // The alignment specifier can only be omitted.
  let ParserMatchClass = AddrMode6dupAlignNoneAsmOperand;
}

// Special version of addrmode6 to handle 16-bit alignment encoding for VLD-dup
// instruction and checking the alignment value.
def AddrMode6dupAlign16AsmOperand : AsmOperandClass {
  let Name = "DupAlignedMemory16";
  let DiagnosticString = "alignment must be 16 or omitted";
}
def addrmode6dupalign16 : AddrMode6DupAlign {
  // The alignment specifier can only be 16 or omitted.
  let ParserMatchClass = AddrMode6dupAlign16AsmOperand;
}

// Special version of addrmode6 to handle 32-bit alignment encoding for VLD-dup
// instruction and checking the alignment value.
def AddrMode6dupAlign32AsmOperand : AsmOperandClass {
  let Name = "DupAlignedMemory32";
  let DiagnosticString = "alignment must be 32 or omitted";
}
def addrmode6dupalign32 : AddrMode6DupAlign {
  // The alignment specifier can only be 32 or omitted.
  let ParserMatchClass = AddrMode6dupAlign32AsmOperand;
}

// Special version of addrmode6 to handle 64-bit alignment encoding for VLD
// instructions and checking the alignment value.
def AddrMode6dupAlign64AsmOperand : AsmOperandClass {
  let Name = "DupAlignedMemory64";
  let DiagnosticString = "alignment must be 64 or omitted";
}
def addrmode6dupalign64 : AddrMode6DupAlign {
  // The alignment specifier can only be 64 or omitted.
  let ParserMatchClass = AddrMode6dupAlign64AsmOperand;
}

// Special version of addrmode6 to handle 64-bit or 128-bit alignment encoding
// for VLD instructions and checking the alignment value.
def AddrMode6dupAlign64or128AsmOperand : AsmOperandClass {
  let Name = "DupAlignedMemory64or128";
  let DiagnosticString = "alignment must be 64, 128 or omitted";
}
def addrmode6dupalign64or128 : AddrMode6DupAlign {
  // The alignment specifier can only be 64, 128 or omitted.
  let ParserMatchClass = AddrMode6dupAlign64or128AsmOperand;
}

// addrmodepc := pc + reg
//
def addrmodepc : MemOperand,
                 ComplexPattern<i32, 2, "SelectAddrModePC", []> {
  let PrintMethod = "printAddrModePCOperand";
  let MIOperandInfo = (ops GPR, i32imm);
}

// addr_offset_none := reg
//
def MemNoOffsetAsmOperand : AsmOperandClass { let Name = "MemNoOffset"; }
def addr_offset_none : MemOperand,
                       ComplexPattern<i32, 1, "SelectAddrOffsetNone", []> {
  let PrintMethod = "printAddrMode7Operand";
  let DecoderMethod = "DecodeAddrMode7Operand";
  let ParserMatchClass = MemNoOffsetAsmOperand;
  let MIOperandInfo = (ops GPR:$base);
}

// t_addr_offset_none := reg [r0-r7]
def MemNoOffsetTAsmOperand : AsmOperandClass { let Name = "MemNoOffsetT"; }
def t_addr_offset_none : MemOperand {
  let PrintMethod = "printAddrMode7Operand";
  let DecoderMethod = "DecodetGPRRegisterClass";
  let ParserMatchClass = MemNoOffsetTAsmOperand;
  let MIOperandInfo = (ops tGPR:$base);
}

def nohash_imm : Operand<i32> {
  let PrintMethod = "printNoHashImmediate";
}

def CoprocNumAsmOperand : AsmOperandClass {
  let Name = "CoprocNum";
  let ParserMethod = "parseCoprocNumOperand";
}
def p_imm : Operand<i32> {
  let PrintMethod = "printPImmediate";
  let ParserMatchClass = CoprocNumAsmOperand;
  let DecoderMethod = "DecodeCoprocessor";
}

def CoprocRegAsmOperand : AsmOperandClass {
  let Name = "CoprocReg";
  let ParserMethod = "parseCoprocRegOperand";
}
def c_imm : Operand<i32> {
  let PrintMethod = "printCImmediate";
  let ParserMatchClass = CoprocRegAsmOperand;
}
def CoprocOptionAsmOperand : AsmOperandClass {
  let Name = "CoprocOption";
  let ParserMethod = "parseCoprocOptionOperand";
}
def coproc_option_imm : Operand<i32> {
  let PrintMethod = "printCoprocOptionImm";
  let ParserMatchClass = CoprocOptionAsmOperand;
}

//===----------------------------------------------------------------------===//

include "ARMInstrFormats.td"

//===----------------------------------------------------------------------===//
// Multiclass helpers...
//

/// AsI1_bin_irs - Defines a set of (op r, {mod_imm|r|so_reg}) patterns for a
/// binop that produces a value.
let TwoOperandAliasConstraint = "$Rn = $Rd" in
multiclass AsI1_bin_irs<bits<4> opcod, string opc,
                     InstrItinClass iii, InstrItinClass iir, InstrItinClass iis,
                     SDPatternOperator opnode, bit Commutable = 0> {
  // The register-immediate version is re-materializable. This is useful
  // in particular for taking the address of a local.
  let isReMaterializable = 1 in {
  def ri : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, mod_imm:$imm), DPFrm,
               iii, opc, "\t$Rd, $Rn, $imm",
               [(set GPR:$Rd, (opnode GPR:$Rn, mod_imm:$imm))]>,
           Sched<[WriteALU, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> imm;
    let Inst{25} = 1;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-0} = imm;
  }
  }
  def rr : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm), DPFrm,
               iir, opc, "\t$Rd, $Rn, $Rm",
               [(set GPR:$Rd, (opnode GPR:$Rn, GPR:$Rm))]>,
           Sched<[WriteALU, ReadALU, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<4> Rm;
    let Inst{25} = 0;
    let isCommutable = Commutable;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-4} = 0b00000000;
    let Inst{3-0} = Rm;
  }

  def rsi : AsI1<opcod, (outs GPR:$Rd),
               (ins GPR:$Rn, so_reg_imm:$shift), DPSoRegImmFrm,
               iis, opc, "\t$Rd, $Rn, $shift",
               [(set GPR:$Rd, (opnode GPR:$Rn, so_reg_imm:$shift))]>,
            Sched<[WriteALUsi, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-5} = shift{11-5};
    let Inst{4} = 0;
    let Inst{3-0} = shift{3-0};
  }

  def rsr : AsI1<opcod, (outs GPR:$Rd),
               (ins GPR:$Rn, so_reg_reg:$shift), DPSoRegRegFrm,
               iis, opc, "\t$Rd, $Rn, $shift",
               [(set GPR:$Rd, (opnode GPR:$Rn, so_reg_reg:$shift))]>,
            Sched<[WriteALUsr, ReadALUsr]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-8} = shift{11-8};
    let Inst{7} = 0;
    let Inst{6-5} = shift{6-5};
    let Inst{4} = 1;
    let Inst{3-0} = shift{3-0};
  }
}

/// AsI1_rbin_irs - Same as AsI1_bin_irs except the order of operands are
/// reversed.  The 'rr' form is only defined for the disassembler; for codegen
/// it is equivalent to the AsI1_bin_irs counterpart.
let TwoOperandAliasConstraint = "$Rn = $Rd" in
multiclass AsI1_rbin_irs<bits<4> opcod, string opc,
                     InstrItinClass iii, InstrItinClass iir, InstrItinClass iis,
                     SDNode opnode> {
  // The register-immediate version is re-materializable. This is useful
  // in particular for taking the address of a local.
  let isReMaterializable = 1 in {
  def ri : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, mod_imm:$imm), DPFrm,
               iii, opc, "\t$Rd, $Rn, $imm",
               [(set GPR:$Rd, (opnode mod_imm:$imm, GPR:$Rn))]>,
           Sched<[WriteALU, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> imm;
    let Inst{25} = 1;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-0} = imm;
  }
  }
  def rr : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm), DPFrm,
               iir, opc, "\t$Rd, $Rn, $Rm",
               [/* pattern left blank */]>,
           Sched<[WriteALU, ReadALU, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<4> Rm;
    let Inst{11-4} = 0b00000000;
    let Inst{25} = 0;
    let Inst{3-0} = Rm;
    let Inst{15-12} = Rd;
    let Inst{19-16} = Rn;
  }

  def rsi : AsI1<opcod, (outs GPR:$Rd),
               (ins GPR:$Rn, so_reg_imm:$shift), DPSoRegImmFrm,
               iis, opc, "\t$Rd, $Rn, $shift",
               [(set GPR:$Rd, (opnode so_reg_imm:$shift, GPR:$Rn))]>,
            Sched<[WriteALUsi, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-5} = shift{11-5};
    let Inst{4} = 0;
    let Inst{3-0} = shift{3-0};
  }

  def rsr : AsI1<opcod, (outs GPR:$Rd),
               (ins GPR:$Rn, so_reg_reg:$shift), DPSoRegRegFrm,
               iis, opc, "\t$Rd, $Rn, $shift",
               [(set GPR:$Rd, (opnode so_reg_reg:$shift, GPR:$Rn))]>,
            Sched<[WriteALUsr, ReadALUsr]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-8} = shift{11-8};
    let Inst{7} = 0;
    let Inst{6-5} = shift{6-5};
    let Inst{4} = 1;
    let Inst{3-0} = shift{3-0};
  }
}

/// AsI1_bin_s_irs - Same as AsI1_bin_irs except it sets the 's' bit by default.
///
/// These opcodes will be converted to the real non-S opcodes by
/// AdjustInstrPostInstrSelection after giving them an optional CPSR operand.
let hasPostISelHook = 1, Defs = [CPSR] in {
multiclass AsI1_bin_s_irs<InstrItinClass iii, InstrItinClass iir,
                          InstrItinClass iis, SDNode opnode,
                          bit Commutable = 0> {
  def ri : ARMPseudoInst<(outs GPR:$Rd), (ins GPR:$Rn, mod_imm:$imm, pred:$p),
                         4, iii,
                         [(set GPR:$Rd, CPSR, (opnode GPR:$Rn, mod_imm:$imm))]>,
                         Sched<[WriteALU, ReadALU]>;

  def rr : ARMPseudoInst<(outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm, pred:$p),
                         4, iir,
                         [(set GPR:$Rd, CPSR, (opnode GPR:$Rn, GPR:$Rm))]>,
                         Sched<[WriteALU, ReadALU, ReadALU]> {
    let isCommutable = Commutable;
  }
  def rsi : ARMPseudoInst<(outs GPR:$Rd),
                          (ins GPR:$Rn, so_reg_imm:$shift, pred:$p),
                          4, iis,
                          [(set GPR:$Rd, CPSR, (opnode GPR:$Rn,
                                                so_reg_imm:$shift))]>,
                          Sched<[WriteALUsi, ReadALU]>;

  def rsr : ARMPseudoInst<(outs GPR:$Rd),
                          (ins GPR:$Rn, so_reg_reg:$shift, pred:$p),
                          4, iis,
                          [(set GPR:$Rd, CPSR, (opnode GPR:$Rn,
                                                so_reg_reg:$shift))]>,
                          Sched<[WriteALUSsr, ReadALUsr]>;
}
}

/// AsI1_rbin_s_is - Same as AsI1_bin_s_irs, except selection DAG
/// operands are reversed.
let hasPostISelHook = 1, Defs = [CPSR] in {
multiclass AsI1_rbin_s_is<InstrItinClass iii,
                          InstrItinClass iis, SDNode opnode> {
  def ri : ARMPseudoInst<(outs GPR:$Rd), (ins GPR:$Rn, mod_imm:$imm, pred:$p),
                         4, iii,
                         [(set GPR:$Rd, CPSR, (opnode mod_imm:$imm, GPR:$Rn))]>,
           Sched<[WriteALU, ReadALU]>;

  def rsi : ARMPseudoInst<(outs GPR:$Rd),
                          (ins GPR:$Rn, so_reg_imm:$shift, pred:$p),
                          4, iis,
                          [(set GPR:$Rd, CPSR, (opnode so_reg_imm:$shift,
                                             GPR:$Rn))]>,
            Sched<[WriteALUsi, ReadALU]>;

  def rsr : ARMPseudoInst<(outs GPR:$Rd),
                          (ins GPR:$Rn, so_reg_reg:$shift, pred:$p),
                          4, iis,
                          [(set GPR:$Rd, CPSR, (opnode so_reg_reg:$shift,
                                             GPR:$Rn))]>,
            Sched<[WriteALUSsr, ReadALUsr]>;
}
}

/// AI1_cmp_irs - Defines a set of (op r, {mod_imm|r|so_reg}) cmp / test
/// patterns. Similar to AsI1_bin_irs except the instruction does not produce
/// a explicit result, only implicitly set CPSR.
let isCompare = 1, Defs = [CPSR] in {
multiclass AI1_cmp_irs<bits<4> opcod, string opc,
                     InstrItinClass iii, InstrItinClass iir, InstrItinClass iis,
                     SDPatternOperator opnode, bit Commutable = 0,
                     string rrDecoderMethod = ""> {
  def ri : AI1<opcod, (outs), (ins GPR:$Rn, mod_imm:$imm), DPFrm, iii,
               opc, "\t$Rn, $imm",
               [(opnode GPR:$Rn, mod_imm:$imm)]>,
           Sched<[WriteCMP, ReadALU]> {
    bits<4> Rn;
    bits<12> imm;
    let Inst{25} = 1;
    let Inst{20} = 1;
    let Inst{19-16} = Rn;
    let Inst{15-12} = 0b0000;
    let Inst{11-0} = imm;

    let Unpredictable{15-12} = 0b1111;
  }
  def rr : AI1<opcod, (outs), (ins GPR:$Rn, GPR:$Rm), DPFrm, iir,
               opc, "\t$Rn, $Rm",
               [(opnode GPR:$Rn, GPR:$Rm)]>,
           Sched<[WriteCMP, ReadALU, ReadALU]> {
    bits<4> Rn;
    bits<4> Rm;
    let isCommutable = Commutable;
    let Inst{25} = 0;
    let Inst{20} = 1;
    let Inst{19-16} = Rn;
    let Inst{15-12} = 0b0000;
    let Inst{11-4} = 0b00000000;
    let Inst{3-0} = Rm;
    let DecoderMethod = rrDecoderMethod;

    let Unpredictable{15-12} = 0b1111;
  }
  def rsi : AI1<opcod, (outs),
               (ins GPR:$Rn, so_reg_imm:$shift), DPSoRegImmFrm, iis,
               opc, "\t$Rn, $shift",
               [(opnode GPR:$Rn, so_reg_imm:$shift)]>,
            Sched<[WriteCMPsi, ReadALU]> {
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{20} = 1;
    let Inst{19-16} = Rn;
    let Inst{15-12} = 0b0000;
    let Inst{11-5} = shift{11-5};
    let Inst{4} = 0;
    let Inst{3-0} = shift{3-0};

    let Unpredictable{15-12} = 0b1111;
  }
  def rsr : AI1<opcod, (outs),
               (ins GPRnopc:$Rn, so_reg_reg:$shift), DPSoRegRegFrm, iis,
               opc, "\t$Rn, $shift",
               [(opnode GPRnopc:$Rn, so_reg_reg:$shift)]>,
            Sched<[WriteCMPsr, ReadALU]> {
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{20} = 1;
    let Inst{19-16} = Rn;
    let Inst{15-12} = 0b0000;
    let Inst{11-8} = shift{11-8};
    let Inst{7} = 0;
    let Inst{6-5} = shift{6-5};
    let Inst{4} = 1;
    let Inst{3-0} = shift{3-0};

    let Unpredictable{15-12} = 0b1111;
  }

}
}

/// AI_ext_rrot - A unary operation with two forms: one whose operand is a
/// register and one whose operand is a register rotated by 8/16/24.
/// FIXME: Remove the 'r' variant. Its rot_imm is zero.
class AI_ext_rrot<bits<8> opcod, string opc, PatFrag opnode>
  : AExtI<opcod, (outs GPRnopc:$Rd), (ins GPRnopc:$Rm, rot_imm:$rot),
          IIC_iEXTr, opc, "\t$Rd, $Rm$rot",
          [(set GPRnopc:$Rd, (opnode (rotr GPRnopc:$Rm, rot_imm:$rot)))]>,
       Requires<[IsARM, HasV6]>, Sched<[WriteALUsi]> {
  bits<4> Rd;
  bits<4> Rm;
  bits<2> rot;
  let Inst{19-16} = 0b1111;
  let Inst{15-12} = Rd;
  let Inst{11-10} = rot;
  let Inst{3-0}   = Rm;
}

class AI_ext_rrot_np<bits<8> opcod, string opc>
  : AExtI<opcod, (outs GPRnopc:$Rd), (ins GPRnopc:$Rm, rot_imm:$rot),
          IIC_iEXTr, opc, "\t$Rd, $Rm$rot", []>,
       Requires<[IsARM, HasV6]>, Sched<[WriteALUsi]> {
  bits<2> rot;
  let Inst{19-16} = 0b1111;
  let Inst{11-10} = rot;
 }

/// AI_exta_rrot - A binary operation with two forms: one whose operand is a
/// register and one whose operand is a register rotated by 8/16/24.
class AI_exta_rrot<bits<8> opcod, string opc, PatFrag opnode>
  : AExtI<opcod, (outs GPRnopc:$Rd), (ins GPR:$Rn, GPRnopc:$Rm, rot_imm:$rot),
          IIC_iEXTAr, opc, "\t$Rd, $Rn, $Rm$rot",
          [(set GPRnopc:$Rd, (opnode GPR:$Rn,
                                     (rotr GPRnopc:$Rm, rot_imm:$rot)))]>,
        Requires<[IsARM, HasV6]>, Sched<[WriteALUsr]> {
  bits<4> Rd;
  bits<4> Rm;
  bits<4> Rn;
  bits<2> rot;
  let Inst{19-16} = Rn;
  let Inst{15-12} = Rd;
  let Inst{11-10} = rot;
  let Inst{9-4}   = 0b000111;
  let Inst{3-0}   = Rm;
}

class AI_exta_rrot_np<bits<8> opcod, string opc>
  : AExtI<opcod, (outs GPRnopc:$Rd), (ins GPR:$Rn, GPRnopc:$Rm, rot_imm:$rot),
          IIC_iEXTAr, opc, "\t$Rd, $Rn, $Rm$rot", []>,
       Requires<[IsARM, HasV6]>, Sched<[WriteALUsr]> {
  bits<4> Rn;
  bits<2> rot;
  let Inst{19-16} = Rn;
  let Inst{11-10} = rot;
}

/// AI1_adde_sube_irs - Define instructions and patterns for adde and sube.
let TwoOperandAliasConstraint = "$Rn = $Rd" in
multiclass AI1_adde_sube_irs<bits<4> opcod, string opc, SDNode opnode,
                             bit Commutable = 0> {
  let hasPostISelHook = 1, Defs = [CPSR], Uses = [CPSR] in {
  def ri : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, mod_imm:$imm),
                DPFrm, IIC_iALUi, opc, "\t$Rd, $Rn, $imm",
               [(set GPR:$Rd, CPSR, (opnode GPR:$Rn, mod_imm:$imm, CPSR))]>,
               Requires<[IsARM]>,
           Sched<[WriteALU, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> imm;
    let Inst{25} = 1;
    let Inst{15-12} = Rd;
    let Inst{19-16} = Rn;
    let Inst{11-0} = imm;
  }
  def rr : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
                DPFrm, IIC_iALUr, opc, "\t$Rd, $Rn, $Rm",
               [(set GPR:$Rd, CPSR, (opnode GPR:$Rn, GPR:$Rm, CPSR))]>,
               Requires<[IsARM]>,
           Sched<[WriteALU, ReadALU, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<4> Rm;
    let Inst{11-4} = 0b00000000;
    let Inst{25} = 0;
    let isCommutable = Commutable;
    let Inst{3-0} = Rm;
    let Inst{15-12} = Rd;
    let Inst{19-16} = Rn;
  }
  def rsi : AsI1<opcod, (outs GPR:$Rd),
                (ins GPR:$Rn, so_reg_imm:$shift),
                DPSoRegImmFrm, IIC_iALUsr, opc, "\t$Rd, $Rn, $shift",
              [(set GPR:$Rd, CPSR, (opnode GPR:$Rn, so_reg_imm:$shift, CPSR))]>,
               Requires<[IsARM]>,
            Sched<[WriteALUsi, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-5} = shift{11-5};
    let Inst{4} = 0;
    let Inst{3-0} = shift{3-0};
  }
  def rsr : AsI1<opcod, (outs GPRnopc:$Rd),
                (ins GPRnopc:$Rn, so_reg_reg:$shift),
                DPSoRegRegFrm, IIC_iALUsr, opc, "\t$Rd, $Rn, $shift",
              [(set GPRnopc:$Rd, CPSR,
                    (opnode GPRnopc:$Rn, so_reg_reg:$shift, CPSR))]>,
               Requires<[IsARM]>,
            Sched<[WriteALUsr, ReadALUsr]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-8} = shift{11-8};
    let Inst{7} = 0;
    let Inst{6-5} = shift{6-5};
    let Inst{4} = 1;
    let Inst{3-0} = shift{3-0};
  }
  }
}

/// AI1_rsc_irs - Define instructions and patterns for rsc
let TwoOperandAliasConstraint = "$Rn = $Rd" in
multiclass AI1_rsc_irs<bits<4> opcod, string opc, SDNode opnode> {
  let hasPostISelHook = 1, Defs = [CPSR], Uses = [CPSR] in {
  def ri : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, mod_imm:$imm),
                DPFrm, IIC_iALUi, opc, "\t$Rd, $Rn, $imm",
               [(set GPR:$Rd, CPSR, (opnode mod_imm:$imm, GPR:$Rn, CPSR))]>,
               Requires<[IsARM]>,
           Sched<[WriteALU, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> imm;
    let Inst{25} = 1;
    let Inst{15-12} = Rd;
    let Inst{19-16} = Rn;
    let Inst{11-0} = imm;
  }
  def rr : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
                DPFrm, IIC_iALUr, opc, "\t$Rd, $Rn, $Rm",
               [/* pattern left blank */]>,
           Sched<[WriteALU, ReadALU, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<4> Rm;
    let Inst{11-4} = 0b00000000;
    let Inst{25} = 0;
    let Inst{3-0} = Rm;
    let Inst{15-12} = Rd;
    let Inst{19-16} = Rn;
  }
  def rsi : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, so_reg_imm:$shift),
                DPSoRegImmFrm, IIC_iALUsr, opc, "\t$Rd, $Rn, $shift",
              [(set GPR:$Rd, CPSR, (opnode so_reg_imm:$shift, GPR:$Rn, CPSR))]>,
               Requires<[IsARM]>,
            Sched<[WriteALUsi, ReadALU]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-5} = shift{11-5};
    let Inst{4} = 0;
    let Inst{3-0} = shift{3-0};
  }
  def rsr : AsI1<opcod, (outs GPR:$Rd), (ins GPR:$Rn, so_reg_reg:$shift),
                DPSoRegRegFrm, IIC_iALUsr, opc, "\t$Rd, $Rn, $shift",
              [(set GPR:$Rd, CPSR, (opnode so_reg_reg:$shift, GPR:$Rn, CPSR))]>,
               Requires<[IsARM]>,
            Sched<[WriteALUsr, ReadALUsr]> {
    bits<4> Rd;
    bits<4> Rn;
    bits<12> shift;
    let Inst{25} = 0;
    let Inst{19-16} = Rn;
    let Inst{15-12} = Rd;
    let Inst{11-8} = shift{11-8};
    let Inst{7} = 0;
    let Inst{6-5} = shift{6-5};
    let Inst{4} = 1;
    let Inst{3-0} = shift{3-0};
  }
  }
}

let canFoldAsLoad = 1, isReMaterializable = 1 in {
multiclass AI_ldr1<bit isByte, string opc, InstrItinClass iii,
           InstrItinClass iir, PatFrag opnode> {
  // Note: We use the complex addrmode_imm12 rather than just an input
  // GPR and a constrained immediate so that we can use this to match
  // frame index references and avoid matching constant pool references.
  def i12: AI2ldst<0b010, 1, isByte, (outs GPR:$Rt), (ins addrmode_imm12:$addr),
                   AddrMode_i12, LdFrm, iii, opc, "\t$Rt, $addr",
                  [(set GPR:$Rt, (opnode addrmode_imm12:$addr))]> {
    bits<4>  Rt;
    bits<17> addr;
    let Inst{23}    = addr{12};     // U (add = ('U' == 1))
    let Inst{19-16} = addr{16-13};  // Rn
    let Inst{15-12} = Rt;
    let Inst{11-0}  = addr{11-0};   // imm12
  }
  def rs : AI2ldst<0b011, 1, isByte, (outs GPR:$Rt), (ins ldst_so_reg:$shift),
                  AddrModeNone, LdFrm, iir, opc, "\t$Rt, $shift",
                 [(set GPR:$Rt, (opnode ldst_so_reg:$shift))]> {
    bits<4>  Rt;
    bits<17> shift;
    let shift{4}    = 0;            // Inst{4} = 0
    let Inst{23}    = shift{12};    // U (add = ('U' == 1))
    let Inst{19-16} = shift{16-13}; // Rn
    let Inst{15-12} = Rt;
    let Inst{11-0}  = shift{11-0};
  }
}
}

let canFoldAsLoad = 1, isReMaterializable = 1 in {
multiclass AI_ldr1nopc<bit isByte, string opc, InstrItinClass iii,
           InstrItinClass iir, PatFrag opnode> {
  // Note: We use the complex addrmode_imm12 rather than just an input
  // GPR and a constrained immediate so that we can use this to match
  // frame index references and avoid matching constant pool references.
  def i12: AI2ldst<0b010, 1, isByte, (outs GPRnopc:$Rt),
                   (ins addrmode_imm12:$addr),
                   AddrMode_i12, LdFrm, iii, opc, "\t$Rt, $addr",
                   [(set GPRnopc:$Rt, (opnode addrmode_imm12:$addr))]> {
    bits<4>  Rt;
    bits<17> addr;
    let Inst{23}    = addr{12};     // U (add = ('U' == 1))
    let Inst{19-16} = addr{16-13};  // Rn
    let Inst{15-12} = Rt;
    let Inst{11-0}  = addr{11-0};   // imm12
  }
  def rs : AI2ldst<0b011, 1, isByte, (outs GPRnopc:$Rt),
                   (ins ldst_so_reg:$shift),
                   AddrModeNone, LdFrm, iir, opc, "\t$Rt, $shift",
                   [(set GPRnopc:$Rt, (opnode ldst_so_reg:$shift))]> {
    bits<4>  Rt;
    bits<17> shift;
    let shift{4}    = 0;            // Inst{4} = 0
    let Inst{23}    = shift{12};    // U (add = ('U' == 1))
    let Inst{19-16} = shift{16-13}; // Rn
    let Inst{15-12} = Rt;
    let Inst{11-0}  = shift{11-0};
  }
}
}


multiclass AI_str1<bit isByte, string opc, InstrItinClass iii,
           InstrItinClass iir, PatFrag opnode> {
  // Note: We use the complex addrmode_imm12 rather than just an input
  // GPR and a constrained immediate so that we can use this to match
  // frame index references and avoid matching constant pool references.
  def i12 : AI2ldst<0b010, 0, isByte, (outs),
                   (ins GPR:$Rt, addrmode_imm12:$addr),
                   AddrMode_i12, StFrm, iii, opc, "\t$Rt, $addr",
                  [(opnode GPR:$Rt, addrmode_imm12:$addr)]> {
    bits<4> Rt;
    bits<17> addr;
    let Inst{23}    = addr{12};     // U (add = ('U' == 1))
    let Inst{19-16} = addr{16-13};  // Rn
    let Inst{15-12} = Rt;
    let Inst{11-0}  = addr{11-0};   // imm12
  }
  def rs : AI2ldst<0b011, 0, isByte, (outs), (ins GPR:$Rt, ldst_so_reg:$shift),
                  AddrModeNone, StFrm, iir, opc, "\t$Rt, $shift",
                 [(opnode GPR:$Rt, ldst_so_reg:$shift)]> {
    bits<4> Rt;
    bits<17> shift;
    let shift{4}    = 0;            // Inst{4} = 0
    let Inst{23}    = shift{12};    // U (add = ('U' == 1))
    let Inst{19-16} = shift{16-13}; // Rn
    let Inst{15-12} = Rt;
    let Inst{11-0}  = shift{11-0};
  }
}

multiclass AI_str1nopc<bit isByte, string opc, InstrItinClass iii,
           InstrItinClass iir, PatFrag opnode> {
  // Note: We use the complex addrmode_imm12 rather than just an input
  // GPR and a constrained immediate so that we can use this to match
  // frame index references and avoid matching constant pool references.
  def i12 : AI2ldst<0b010, 0, isByte, (outs),
                   (ins GPRnopc:$Rt, addrmode_imm12:$addr),
                   AddrMode_i12, StFrm, iii, opc, "\t$Rt, $addr",
                  [(opnode GPRnopc:$Rt, addrmode_imm12:$addr)]> {
    bits<4> Rt;
    bits<17> addr;
    let Inst{23}    = addr{12};     // U (add = ('U' == 1))
    let Inst{19-16} = addr{16-13};  // Rn
    let Inst{15-12} = Rt;
    let Inst{11-0}  = addr{11-0};   // imm12
  }
  def rs : AI2ldst<0b011, 0, isByte, (outs),
                   (ins GPRnopc:$Rt, ldst_so_reg:$shift),
                   AddrModeNone, StFrm, iir, opc, "\t$Rt, $shift",
                   [(opnode GPRnopc:$Rt, ldst_so_reg:$shift)]> {
    bits<4> Rt;
    bits<17> shift;
    let shift{4}    = 0;            // Inst{4} = 0
    let Inst{23}    = shift{12};    // U (add = ('U' == 1))
    let Inst{19-16} = shift{16-13}; // Rn
    let Inst{15-12} = Rt;
    let Inst{11-0}  = shift{11-0};
  }
}


//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Miscellaneous Instructions.
//

/// CONSTPOOL_ENTRY - This instruction represents a floating constant pool in
/// the function.  The first operand is the ID# for this instruction, the second
/// is the index into the MachineConstantPool that this is, the third is the
/// size in bytes of this constant pool entry.
let hasSideEffects = 0, isNotDuplicable = 1, hasNoSchedulingInfo = 1 in
def CONSTPOOL_ENTRY :
PseudoInst<(outs), (ins cpinst_operand:$instid, cpinst_operand:$cpidx,
                    i32imm:$size), NoItinerary, []>;

/// A jumptable consisting of direct 32-bit addresses of the destination basic
/// blocks (either absolute, or relative to the start of the jump-table in PIC
/// mode). Used mostly in ARM and Thumb-1 modes.
def JUMPTABLE_ADDRS :
PseudoInst<(outs), (ins cpinst_operand:$instid, cpinst_operand:$cpidx,
                        i32imm:$size), NoItinerary, []>;

/// A jumptable consisting of 32-bit jump instructions. Used for Thumb-2 tables
/// that cannot be optimised to use TBB or TBH.
def JUMPTABLE_INSTS :
PseudoInst<(outs), (ins cpinst_operand:$instid, cpinst_operand:$cpidx,
                        i32imm:$size), NoItinerary, []>;

/// A jumptable consisting of 8-bit unsigned integers representing offsets from
/// a TBB instruction.
def JUMPTABLE_TBB :
PseudoInst<(outs), (ins cpinst_operand:$instid, cpinst_operand:$cpidx,
                        i32imm:$size), NoItinerary, []>;

/// A jumptable consisting of 16-bit unsigned integers representing offsets from
/// a TBH instruction.
def JUMPTABLE_TBH :
PseudoInst<(outs), (ins cpinst_operand:$instid, cpinst_operand:$cpidx,
                        i32imm:$size), NoItinerary, []>;


// FIXME: Marking these as hasSideEffects is necessary to prevent machine DCE
// from removing one half of the matched pairs. That breaks PEI, which assumes
// these will always be in pairs, and asserts if it finds otherwise. Better way?
let Defs = [SP], Uses = [SP], hasSideEffects = 1 in {
def ADJCALLSTACKUP :
PseudoInst<(outs), (ins i32imm:$amt1, i32imm:$amt2, pred:$p), NoItinerary,
           [(ARMcallseq_end timm:$amt1, timm:$amt2)]>;

def ADJCALLSTACKDOWN :
PseudoInst<(outs), (ins i32imm:$amt, i32imm:$amt2, pred:$p), NoItinerary,
           [(ARMcallseq_start timm:$amt, timm:$amt2)]>;
}

def HINT : AI<(outs), (ins imm0_239:$imm), MiscFrm, NoItinerary,
              "hint", "\t$imm", [(int_arm_hint imm0_239:$imm)]>,
           Requires<[IsARM, HasV6]> {
  bits<8> imm;
  let Inst{27-8} = 0b00110010000011110000;
  let Inst{7-0} = imm;
  let DecoderMethod = "DecodeHINTInstruction";
}

def : InstAlias<"nop$p", (HINT 0, pred:$p)>, Requires<[IsARM, HasV6K]>;
def : InstAlias<"yield$p", (HINT 1, pred:$p)>, Requires<[IsARM, HasV6K]>;
def : InstAlias<"wfe$p", (HINT 2, pred:$p)>, Requires<[IsARM, HasV6K]>;
def : InstAlias<"wfi$p", (HINT 3, pred:$p)>, Requires<[IsARM, HasV6K]>;
def : InstAlias<"sev$p", (HINT 4, pred:$p)>, Requires<[IsARM, HasV6K]>;
def : InstAlias<"sevl$p", (HINT 5, pred:$p)>, Requires<[IsARM, HasV8]>;
def : InstAlias<"esb$p", (HINT 16, pred:$p)>, Requires<[IsARM, HasRAS]>;
def : InstAlias<"csdb$p", (HINT 20, pred:$p)>, Requires<[IsARM, HasV6K]>;

// Clear BHB instruction
def : InstAlias<"clrbhb$p", (HINT 22, pred:$p), 0>, Requires<[IsARM, HasV8]>;
def : InstAlias<"clrbhb$p", (HINT 22, pred:$p), 1>, Requires<[IsARM, HasV8, HasCLRBHB]>;

def SEL : AI<(outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm), DPFrm, NoItinerary, "sel",
             "\t$Rd, $Rn, $Rm",
             [(set GPR:$Rd, (int_arm_sel GPR:$Rn, GPR:$Rm))]>,
             Requires<[IsARM, HasV6]> {
  bits<4> Rd;
  bits<4> Rn;
  bits<4> Rm;
  let Inst{3-0} = Rm;
  let Inst{15-12} = Rd;
  let Inst{19-16} = Rn;
  let Inst{27-20} = 0b01101000;
  let Inst{7-4} = 0b1011;
  let Inst{11-8} = 0b1111;
  let Unpredictable{11-8} = 0b1111;
}

// The 16-bit operand $val can be used by a debugger to store more information
// about the breakpoint.
def BKPT : AInoP<(outs), (ins imm0_65535:$val), MiscFrm, NoItinerary,
                 "bkpt", "\t$val", []>, Requires<[IsARM]> {
  bits<16> val;
  let Inst{3-0} = val{3-0};
  let Inst{19-8} = val{15-4};
  let Inst{27-20} = 0b00010010;
  let Inst{31-28} = 0xe; // AL
  let Inst{7-4} = 0b0111;
}
// default immediate for breakpoint mnemonic
def : InstAlias<"bkpt", (BKPT 0), 0>, Requires<[IsARM]>;

def HLT : AInoP<(outs), (ins imm0_65535:$val), MiscFrm, NoItinerary,
                 "hlt", "\t$val", []>, Requires<[IsARM, HasV8]> {
  bits<16> val;
  let Inst{3-0} = val{3-0};
  let Inst{19-8} = val{15-4};
  let Inst{27-20} = 0b00010000;
  let Inst{31-28} = 0xe; // AL
  let Inst{7-4} = 0b0111;
}

// Change Processor State
// FIXME: We should use InstAlias to handle the optional operands.
class CPS<dag iops, string asm_ops>
  : AXI<(outs), iops, MiscFrm, NoItinerary, !strconcat("cps", asm_ops),
        []>, Requires<[IsARM]> {
  bits<2> imod;
  bits<3> iflags;
  bits<5> mode;
  bit M;

  let Inst{31-28} = 0b1111;
  let Inst{27-20} = 0b00010000;
  let Inst{19-18} = imod;
  let Inst{17}    = M; // Enabled if mode is set;
  let Inst{16-9}  = 0b00000000;
  let Inst{8-6}   = iflags;
  let Inst{5}     = 0;
  let Inst{4-0}   = mode;
}

let DecoderMethod = "DecodeCPSInstruction" in {
let M = 1 in
  def CPS3p : CPS<(ins imod_op:$imod, iflags_op:$iflags, imm0_31:$mode),
                  "$imod\t$iflags, $mode">;
let mode = 0, M = 0 in
  def CPS2p : CPS<(ins imod_op:$imod, iflags_op:$iflags), "$imod\t$iflags">;

let imod = 0, iflags = 0, M = 1 in
  def CPS1p : CPS<(ins imm0_31:$mode), "\t$mode">;
}

// Preload signals the memory system of possible future data/instruction access.
multiclass APreLoad<bits<1> read, bits<1> data, string opc> {

  def i12 : AXIM<(outs), (ins addrmode_imm12:$addr), AddrMode_i12, MiscFrm,
                IIC_Preload, !strconcat(opc, "\t$addr"),
                [(ARMPreload addrmode_imm12:$addr, (i32 read), (i32 data))]>,
                Sched<[WritePreLd]> {
    bits<4> Rt;
    bits<17> addr;
    let Inst{31-26} = 0b111101;
    let Inst{25} = 0; // 0 for immediate form
    let Inst{24} = data;
    let Inst{23} = addr{12};        // U (add = ('U' == 1))
    let Inst{22} = read;
    let Inst{21-20} = 0b01;
    let Inst{19-16} = addr{16-13};  // Rn
    let Inst{15-12} = 0b1111;
    let Inst{11-0}  = addr{11-0};   // imm12
  }

  def rs : AXI<(outs), (ins ldst_so_reg:$shift), MiscFrm, IIC_Preload,
               !strconcat(opc, "\t$shift"),
               [(ARMPreload ldst_so_reg:$shift, (i32 read), (i32 data))]>,
               Sched<[WritePreLd]> {
    bits<17> shift;
    let Inst{31-26} = 0b111101;
    let Inst{25} = 1; // 1 for register form
    let Inst{24} = data;
    let Inst{23} = shift{12};    // U (add = ('U' == 1))
    let Inst{22} = read;
    let Inst{21-20} = 0b01;
    let Inst{19-16} = shift{16-13}; // Rn
    let Inst{15-12} = 0b1111;
    let Inst{11-0}  = shift{11-0};
    let Inst{4} = 0;
  }
}

defm PLD  : APreLoad<1, 1, "pld">,  Requires<[IsARM]>;
defm PLDW : APreLoad<0, 1, "pldw">, Requires<[IsARM,HasV7,HasMP]>;
defm PLI  : APreLoad<1, 0, "pli">,  Requires<[IsARM,HasV7]>;

def SETEND : AXI<(outs), (ins setend_op:$end), MiscFrm, NoItinerary,
                 "setend\t$end", []>, Requires<[IsARM]>, Deprecated<HasV8Ops> {
  bits<1> end;
  let Inst{31-10} = 0b1111000100000001000000;
  let Inst{9} = end;
  let Inst{8-0} = 0;
}

def DBG : AI<(outs), (ins imm0_15:$opt), MiscFrm, NoItinerary, "dbg", "\t$opt",
             [(int_arm_dbg imm0_15:$opt)]>, Requires<[IsARM, HasV7]> {
  bits<4> opt;
  let Inst{27-4} = 0b001100100000111100001111;
  let Inst{3-0} = opt;
}

// A8.8.247  UDF - Undefined (Encoding A1)
def UDF : AInoP<(outs), (ins imm0_65535:$imm16), MiscFrm, NoItinerary,
                "udf", "\t$imm16", [(int_arm_undefined imm0_65535:$imm16)]> {
  bits<16> imm16;
  let Inst{31-28} = 0b1110; // AL
  let Inst{27-25} = 0b011;
  let Inst{24-20} = 0b11111;
  let Inst{19-8} = imm16{15-4};
  let Inst{7-4} = 0b1111;
  let Inst{3-0} = imm16{3-0};
}

/*
 * A5.4 Permanently UNDEFINED instructions.
 *
 * For most targets use UDF #65006, for which the OS will generate SIGTRAP.
 * Other UDF encodings generate SIGILL.
 *
 * NaCl's OS instead chooses an ARM UDF encoding that's also a UDF in Thumb.
 * Encoding A1:
 *  1110 0111 1111 iiii iiii iiii 1111 iiii
 * Encoding T1:
 *  1101 1110 iiii iiii
 * It uses the following encoding:
 *  1110 0111 1111 1110 1101 1110 1111 0000
 *  - In ARM: UDF #60896;
 *  - In Thumb: UDF #254 followed by a branch-to-self.
 */
let isBarrier = 1, isTerminator = 1 in
def TRAPNaCl : AXI<(outs), (ins), MiscFrm, NoItinerary,
               "trap", [(trap)]>,
           Requires<[IsARM,UseNaClTrap]> {
  let Inst = 0xe7fedef0;
}
let isBarrier = 1, isTerminator = 1 in
def TRAP : AXI<(outs), (ins), MiscFrm, NoItinerary,
               "trap", [(trap)]>,
           Requires<[IsARM,DontUseNaClTrap]> {
  let Inst = 0xe7ffdefe;
}

def : Pat<(debugtrap), (BKPT 0)>, Requires<[IsARM, HasV5T]>;
def : Pat<(debugtrap), (UDF 254)>, Requires<[IsARM, NoV5T]>;

// Address computation and loads and stores in PIC mode.
let isNotDuplicable = 1 in {
def PICADD  : ARMPseudoInst<(outs GPR:$dst), (ins GPR:$a, pclabel:$cp, pred:$p),
                            4, IIC_iALUr,
                            [(set GPR:$dst, (ARMpic_add GPR:$a, imm:$cp))]>,
                            Sched<[WriteALU, ReadALU]>;

let AddedComplexity = 10 in {
def PICLDR  : ARMPseudoInst<(outs GPR:$dst), (ins addrmodepc:$addr, pred:$p),
                            4, IIC_iLoad_r,
                            [(set GPR:$dst, (load addrmodepc:$addr))]>;

def PICLDRH : ARMPseudoInst<(outs GPR:$Rt), (ins addrmodepc:$addr, pred:$p),
                            4, IIC_iLoad_bh_r,
                            [(set GPR:$Rt, (zextloadi16 addrmodepc:$addr))]>;

def PICLDRB : ARMPseudoInst<(outs GPR:$Rt), (ins addrmodepc:$addr, pred:$p),
                            4, IIC_iLoad_bh_r,
                            [(set GPR:$Rt, (zextloadi8 addrmodepc:$addr))]>;

def PICLDRSH : ARMPseudoInst<(outs GPR:$Rt), (ins addrmodepc:$addr, pred:$p),
                            4, IIC_iLoad_bh_r,
                            [(set GPR:$Rt, (sextloadi16 addrmodepc:$addr))]>;

def PICLDRSB : ARMPseudoInst<(outs GPR:$Rt), (ins addrmodepc:$addr, pred:$p),
                            4, IIC_iLoad_bh_r,
                            [(set GPR:$Rt, (sextloadi8 addrmodepc:$addr))]>;
}
let AddedComplexity = 10 in {
def PICSTR  : ARMPseudoInst<(outs), (ins GPR:$src, addrmodepc:$addr, pred:$p),
      4, IIC_iStore_r, [(store GPR:$src, addrmodepc:$addr)]>;

def PICSTRH : ARMPseudoInst<(outs), (ins GPR:$src, addrmodepc:$addr, pred:$p),
      4, IIC_iStore_bh_r, [(truncstorei16 GPR:$src,
                                                   addrmodepc:$addr)]>;

def PICSTRB : ARMPseudoInst<(outs), (ins GPR:$src, addrmodepc:$addr, pred:$p),
      4, IIC_iStore_bh_r, [(truncstorei8 GPR:$src, addrmodepc:$addr)]>;
}
} // isNotDuplicable = 1


// LEApcrel - Load a pc-relative address into a register without offending the
// assembler.
let hasSideEffects = 0, isReMaterializable = 1 in
// The 'adr' mnemonic encodes differently if the label is before or after
// the instruction. The {24-21} opcode bits are set by the fixup, as we don't
// know until then which form of the instruction will be used.
def ADR : AI1<{0,?,?,0}, (outs GPR:$Rd), (ins adrlabel:$label),
                 MiscFrm, IIC_iALUi, "adr", "\t$Rd, $label", []>,
                 Sched<[WriteALU, ReadALU]> {
  bits<4> Rd;
  bits<14> label;
  let Inst{27-25} = 0b001;
  let Inst{24} = 0;
  let Inst{23-22} = label{13-12};
  let Inst{21} = 0;
  let Inst{20} = 0;
  let Inst{19-16} = 0b1111;
  let Inst{15-12} = Rd;
  let Inst{11-0} = label{11-0};
}

let hasSideEffects = 1 in {
def LEApcrel : ARMPseudoInst<(outs GPR:$Rd), (ins i32imm:$label, pred:$p),
                    4, IIC_iALUi, []>, Sched<[WriteALU, ReadALU]>;

def LEApcrelJT : ARMPseudoInst<(outs GPR:$Rd),
                      (ins i32imm:$label, pred:$p),
                      4, IIC_iALUi, []>, Sched<[WriteALU, ReadALU]>;
}

//===----------------------------------------------------------------------===//
//  Control Flow Instructions.
//

let isReturn = 1, isTerminator = 1, isBarrier = 1 in {
  // ARMV4T and above
  def BX_RET : AI<(outs), (ins), BrMiscFrm, IIC_Br,
                  "bx", "\tlr", [(ARMretglue)]>,
               Requires<[IsARM, HasV4T]>, Sched<[WriteBr]> {
    let Inst{27-0}  = 0b0001001011111111111100011110;
  }

  // ARMV4 only
  def MOVPCLR : AI<(outs), (ins), BrMiscFrm, IIC_Br,
                  "mov", "\tpc, lr", [(ARMretglue)]>,
               Requires<[IsARM, NoV4T]>, Sched<[WriteBr]> {
    let Inst{27-0} = 0b0001101000001111000000001110;
  }

  // Exception return: N.b. doesn't set CPSR as far as we're concerned (it sets
  // the user-space one).
  def SUBS_PC_LR : ARMPseudoInst<(outs), (ins i32imm:$offset, pred:$p),
                                 4, IIC_Br,
                                 [(ARMintretglue imm:$offset)]>;
}

// Indirect branches
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in {
  // ARMV4T and above
  def BX : AXI<(outs), (ins GPR:$dst), BrMiscFrm, IIC_Br, "bx\t$dst",
                  [(brind GPR:$dst)]>,
              Requires<[IsARM, HasV4T]>, Sched<[WriteBr]> {
    bits<4> dst;
    let Inst{31-4} = 0b1110000100101111111111110001;
    let Inst{3-0}  = dst;
  }

  def BX_pred : AI<(outs), (ins GPR:$dst), BrMiscFrm, IIC_Br,
                  "bx", "\t$dst", [/* pattern left blank */]>,
              Requires<[IsARM, HasV4T]>, Sched<[WriteBr]> {
    bits<4> dst;
    let Inst{27-4} = 0b000100101111111111110001;
    let Inst{3-0}  = dst;
  }
}

// SP is marked as a use to prevent stack-pointer assignments that appear
// immediately before calls from potentially appearing dead.
let isCall = 1,
  // FIXME:  Do we really need a non-predicated version? If so, it should
  // at least be a pseudo instruction expanding to the predicated version
  // at MC lowering time.
  Defs = [LR], Uses = [SP] in {
  def BL  : ABXI<0b1011, (outs), (ins arm_bl_target:$func),
                IIC_Br, "bl\t$func",
                [(ARMcall tglobaladdr:$func)]>,
            Requires<[IsARM]>, Sched<[WriteBrL]> {
    let Inst{31-28} = 0b1110;
    bits<24> func;
    let Inst{23-0} = func;
    let DecoderMethod = "DecodeBranchImmInstruction";
  }

  def BL_pred : ABI<0b1011, (outs), (ins arm_bl_target:$func),
                   IIC_Br, "bl", "\t$func",
                   [(ARMcall_pred tglobaladdr:$func)]>,
                Requires<[IsARM]>, Sched<[WriteBrL]> {
    bits<24> func;
    let Inst{23-0} = func;
    let DecoderMethod = "DecodeBranchImmInstruction";
  }

  // ARMv5T and above
  def BLX : AXI<(outs), (ins GPR:$func), BrMiscFrm, IIC_Br, "blx\t$func", []>,
            Requires<[IsARM, HasV5T]>, Sched<[WriteBrL]> {
    bits<4> func;
    let Inst{31-4} = 0b1110000100101111111111110011;
    let Inst{3-0}  = func;
  }
  def BLX_noip :  ARMPseudoExpand<(outs), (ins GPRnoip:$func),
                   4, IIC_Br, [], (BLX GPR:$func)>,
                  Requires<[IsARM, HasV5T]>, Sched<[WriteBrL]>;


  def BLX_pred : AI<(outs), (ins GPR:$func), BrMiscFrm,
                    IIC_Br, "blx", "\t$func", []>,
                 Requires<[IsARM, HasV5T]>, Sched<[WriteBrL]> {
    bits<4> func;
    let Inst{27-4} = 0b000100101111111111110011;
    let Inst{3-0}  = func;
  }
  def BLX_pred_noip :  ARMPseudoExpand<(outs), (ins GPRnoip:$func),
                   4, IIC_Br, [],
                   (BLX_pred GPR:$func, (ops 14, zero_reg))>,
                   Requires<[IsARM, HasV5T]>, Sched<[WriteBrL]>;


  // ARMv4T
  // Note: Restrict $func to the tGPR regclass to prevent it being in LR.
  def BX_CALL : ARMPseudoInst<(outs), (ins tGPR:$func),
                   8, IIC_Br, [(ARMcall_nolink tGPR:$func)]>,
                   Requires<[IsARM, HasV4T]>, Sched<[WriteBr]>;

  // ARMv4
  def BMOVPCRX_CALL : ARMPseudoInst<(outs), (ins tGPR:$func),
                   8, IIC_Br, [(ARMcall_nolink tGPR:$func)]>,
                   Requires<[IsARM, NoV4T]>, Sched<[WriteBr]>;

  // mov lr, pc; b if callee is marked noreturn to avoid confusing the
  // return stack predictor.
  def BMOVPCB_CALL : ARMPseudoInst<(outs), (ins arm_bl_target:$func),
                               8, IIC_Br, [(ARMcall_nolink tglobaladdr:$func)]>,
                      Requires<[IsARM]>, Sched<[WriteBr]>;

  // push lr before the call
  def BL_PUSHLR : ARMPseudoInst<(outs), (ins GPRlr:$ra, arm_bl_target:$func),
                  4, IIC_Br,
                  []>,
             Requires<[IsARM]>, Sched<[WriteBr]>;
}

def : ARMPat<(ARMcall GPR:$func), (BLX $func)>,
      Requires<[IsARM, HasV5T, NoSLSBLRMitigation]>;
def : ARMPat<(ARMcall GPRnoip:$func), (BLX_noip $func)>,
      Requires<[IsARM, HasV5T, SLSBLRMitigation]>;
def : ARMPat<(ARMcall_pred GPR:$func), (BLX_pred $func)>,
      Requires<[IsARM, HasV5T, NoSLSBLRMitigation]>;
def : ARMPat<(ARMcall_pred GPRnoip:$func), (BLX_pred_noip $func)>,
      Requires<[IsARM, HasV5T, SLSBLRMitigation]>;


let isBranch = 1, isTerminator = 1 in {
  // FIXME: should be able to write a pattern for ARMBrcond, but can't use
  // a two-value operand where a dag node expects two operands. :(
  def Bcc : ABI<0b1010, (outs), (ins arm_br_target:$target),
               IIC_Br, "b", "\t$target",
               [/*(ARMbrcond bb:$target, imm:$cc, CCR:$ccr)*/]>,
               Sched<[WriteBr]>  {
    bits<24> target;
    let Inst{23-0} = target;
    let DecoderMethod = "DecodeBranchImmInstruction";
  }

  let isBarrier = 1 in {
    // B is "predicable" since it's just a Bcc with an 'always' condition.
    let isPredicable = 1 in
    // FIXME: We shouldn't need this pseudo at all. Just using Bcc directly
    // should be sufficient.
    // FIXME: Is B really a Barrier? That doesn't seem right.
    def B : ARMPseudoExpand<(outs), (ins arm_br_target:$target), 4, IIC_Br,
                [(br bb:$target)], (Bcc arm_br_target:$target,
                (ops 14, zero_reg))>,
                Sched<[WriteBr]>;

    let Size = 4, isNotDuplicable = 1, isIndirectBranch = 1 in {
    def BR_JTr : ARMPseudoInst<(outs),
                      (ins GPR:$target, i32imm:$jt),
                      0, IIC_Br,
                      [(ARMbrjt GPR:$target, tjumptable:$jt)]>,
                      Sched<[WriteBr]>;
    def BR_JTm_i12 : ARMPseudoInst<(outs),
                     (ins addrmode_imm12:$target, i32imm:$jt),
                     0, IIC_Br,
                     [(ARMbrjt (i32 (load addrmode_imm12:$target)),
                               tjumptable:$jt)]>, Sched<[WriteBrTbl]>;
    def BR_JTm_rs : ARMPseudoInst<(outs),
                     (ins ldst_so_reg:$target, i32imm:$jt),
                     0, IIC_Br,
                     [(ARMbrjt (i32 (load ldst_so_reg:$target)),
                               tjumptable:$jt)]>, Sched<[WriteBrTbl]>;
    def BR_JTadd : ARMPseudoInst<(outs),
                   (ins GPR:$target, GPR:$idx, i32imm:$jt),
                   0, IIC_Br,
                   [(ARMbrjt (add GPR:$target, GPR:$idx), tjumptable:$jt)]>,
                   Sched<[WriteBrTbl]>;
    } // isNotDuplicable = 1, isIndirectBranch = 1
  } // isBarrier = 1

}

// BLX (immediate)
def BLXi : AXI<(outs), (ins arm_blx_target:$target), BrMiscFrm, NoItinerary,
               "blx\t$target", []>,
           Requires<[IsARM, HasV5T]>, Sched<[WriteBrL]> {
  let Inst{31-25} = 0b1111101;
  bits<25> target;
  let Inst{23-0} = target{24-1};
  let Inst{24} = target{0};
  let isCall = 1;
}

// Branch and Exchange Jazelle
def BXJ : ABI<0b0001, (outs), (ins GPR:$func), NoItinerary, "bxj", "\t$func",
              [/* pattern left blank */]>, Sched<[WriteBr]> {
  bits<4> func;
  let Inst{23-20} = 0b0010;
  let Inst{19-8} = 0xfff;
  let Inst{7-4} = 0b0010;
  let Inst{3-0} = func;
  let isBranch = 1;
  let isIndirectBranch = 1;
}

// Tail calls.

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [SP] in {
  def TCRETURNdi : PseudoInst<(outs), (ins i32imm:$dst, i32imm:$SPDiff), IIC_Br, []>,
                   Sched<[WriteBr]>;

  def TCRETURNri : PseudoInst<(outs), (ins tcGPR:$dst, i32imm:$SPDiff), IIC_Br, []>,
                   Sched<[WriteBr]>;

  def TCRETURNrinotr12 : PseudoInst<(outs), (ins tcGPRnotr12:$dst, i32imm:$SPDiff), IIC_Br, []>,
                   Sched<[WriteBr]>;

  def TAILJMPd : ARMPseudoExpand<(outs), (ins arm_br_target:$dst),
                                 4, IIC_Br, [],
                                 (Bcc arm_br_target:$dst, (ops 14, zero_reg))>,
                                 Requires<[IsARM]>, Sched<[WriteBr]>;

  def TAILJMPr : ARMPseudoExpand<(outs), (ins tcGPR:$dst),
                                 4, IIC_Br, [],
                                 (BX GPR:$dst)>, Sched<[WriteBr]>,
                                 Requires<[IsARM, HasV4T]>;
}

// Secure Monitor Call is a system instruction.
def SMC : ABI<0b0001, (outs), (ins imm0_15:$opt), NoItinerary, "smc", "\t$opt",
              []>, Requires<[IsARM, HasTrustZone]> {
  bits<4> opt;
  let Inst{23-4} = 0b01100000000000000111;
  let Inst{3-0} = opt;
}
def : MnemonicAlias<"smi", "smc">;

// Supervisor Call (Software Interrupt)
let isCall = 1, Uses = [SP] in {
def SVC : ABI<0b1111, (outs), (ins imm24b:$svc), IIC_Br, "svc", "\t$svc", []>,
          Sched<[WriteBr]> {
  bits<24> svc;
  let Inst{23-0} = svc;
}
}

// Store Return State
class SRSI<bit wb, string asm>
  : XI<(outs), (ins imm0_31:$mode), AddrModeNone, 4, IndexModeNone, BrFrm,
       NoItinerary, asm, "", []> {
  bits<5> mode;
  let Inst{31-28} = 0b1111;
  let Inst{27-25} = 0b100;
  let Inst{22} = 1;
  let Inst{21} = wb;
  let Inst{20} = 0;
  let Inst{19-16} = 0b1101;  // SP
  let Inst{15-5} = 0b00000101000;
  let Inst{4-0} = mode;
}

def SRSDA : SRSI<0, "srsda\tsp, $mode"> {
  let Inst{24-23} = 0;
}
def SRSDA_UPD : SRSI<1, "srsda\tsp!, $mode"> {
  let Inst{24-23} = 0;
}
def SRSDB : SRSI<0, "srsdb\tsp, $mode"> {
  let Inst{24-23} = 0b10;
}
def SRSDB_UPD : SRSI<1, "srsdb\tsp!, $mode"> {
  let Inst{24-23} = 0b10;
}
def SRSIA : SRSI<0, "srsia\tsp, $mode"> {
  let Inst{24-23} = 0b01;
}
def SRSIA_UPD : SRSI<1, "srsia\tsp!, $mode"> {
  let Inst{24-23} = 0b01;
}
def SRSIB : SRSI<0, "srsib\tsp, $mode"> {
  let Inst{24-23} = 0b11;
}
def SRSIB_UPD : SRSI<1, "srsib\tsp!, $mode"> {
  let Inst{24-23} = 0b11;
}

def : ARMInstAlias<"srsda $mode", (SRSDA imm0_31:$mode)>;
def : ARMInstAlias<"srsda $mode!", (SRSDA_UPD imm0_31:$mode)>;

def : ARMInstAlias<"srsdb $mode", (SRSDB imm0_31:$mode)>;
def : ARMInstAlias<"srsdb $mode!", (SRSDB_UPD imm0_31:$mode)>;

def : ARMInstAlias<"srsia $mode", (SRSIA imm0_31:$mode)>;
def : ARMInstAlias<"srsia $mode!", (SRSIA_UPD imm0_31:$mode)>;

def : ARMInstAlias<"srsib $mode", (SRSIB imm0_31:$mode)>;
def : ARMInstAlias<"srsib $mode!", (SRSIB_UPD imm0_31:$mode)>;

// Return From Exception
class RFEI<bit wb, string asm>
  : XI<(outs), (ins GPR:$Rn), AddrModeNone, 4, IndexModeNone, BrFrm,
       NoItinerary, asm, "", []> {
  bits<4> Rn;
  let Inst{31-28} = 0b1111;
  let Inst{27-25} = 0b100;
  let Inst{22} = 0;
  let Inst{21} = wb;
  let Inst{20} = 1;
  let Inst{19-16} = Rn;
  let Inst{15-0} = 0xa00;
}

def RFEDA : RFEI<0, "rfeda\t$Rn"> {
  let Inst{24-23} = 0;
}
def RFEDA_UPD : RFEI<1, "rfeda\t$Rn!"> {
  let Inst{24-23} = 0;
}
def RFEDB : RFEI<0, "rfedb\t$Rn"> {
  let Inst{24-23} = 0b10;
}
def RFEDB_UPD : RFEI<1, "rfedb\t$Rn!"> {
  let Inst{24-23} = 0b10;
}
def RFEIA : RFEI<0, "rfeia\t$Rn"> {
  let Inst{24-23} = 0b01;
}
def RFEIA_UPD : RFEI<1, "rfeia\t$Rn!"> {
  let Inst{24-23} = 0b01;
}
def RFEIB : RFEI<0, "rfeib\t$Rn"> {
  let Inst{24-23} = 0b11;
}
def RFEIB_UPD : RFEI<1, "rfeib\t$Rn!"> {
  let Inst{24-23} = 0b11;
}

// Hypervisor Call is a system instruction
let isCall = 1 in {
def HVC : AInoP< (outs), (ins imm0_65535:$imm), BrFrm, NoItinerary,
                "hvc", "\t$imm", []>,
          Requires<[IsARM, HasVirtualization]> {
  bits<16> imm;

  // Even though HVC isn't predicable, it's encoding includes a condition field.
  // The instruction is undefined if the condition field is 0xf otherwise it is
  // unpredictable if it isn't condition AL (0xe).
  let Inst{31-28} = 0b1110;
  let Unpredictable{31-28} = 0b1111;
  let Inst{27-24} = 0b0001;
  let Inst{23-20} = 0b0100;
  let Inst{19-8} = imm{15-4};
  let Inst{7-4} = 0b0111;
  let Inst{3-0} = imm{3-0};
}
}

// Return from exception in Hypervisor mode.
let isReturn = 1, isBarrier = 1, isTerminator = 1, Defs = [PC] in
def ERET : ABI<0b0001, (outs), (ins), NoItinerary, "eret", "", []>,
    Requires<[IsARM, HasVirtualization]> {
    let Inst{23-0} = 0b011000000000000001101110;
}

//===----------------------------------------------------------------------===//
//  Load / Store Instructions.
//

// Load


defm LDR  : AI_ldr1<0, "ldr", IIC_iLoad_r, IIC_iLoad_si, load>;
defm LDRB : AI_ldr1nopc<1, "ldrb", IIC_iLoad_bh_r, IIC_iLoad_bh_si,
                        zextloadi8>;
defm STR  : AI_str1<0, "str", IIC_iStore_r, IIC_iStore_si, store>;
defm STRB : AI_str1nopc<1, "strb", IIC_iStore_bh_r, IIC_iStore_bh_si,
                        truncstorei8>;

// Special LDR for loads from non-pc-relative constpools.
let canFoldAsLoad = 1, mayLoad = 1, hasSideEffects = 0,
    isReMaterializable = 1, isCodeGenOnly = 1 in
def LDRcp : AI2ldst<0b010, 1, 0, (outs GPR:$Rt), (ins addrmode_imm12:$addr),
                 AddrMode_i12, LdFrm, IIC_iLoad_r, "ldr", "\t$Rt, $addr",
                 []> {
  bits<4> Rt;
  bits<17> addr;
  let Inst{23}    = addr{12};     // U (add = ('U' == 1))
  let Inst{19-16} = 0b1111;
  let Inst{15-12} = Rt;
  let Inst{11-0}  = addr{11-0};   // imm12
}

// Loads with zero extension
def LDRH  : AI3ld<0b1011, 1, (outs GPR:$Rt), (ins addrmode3:$addr), LdMiscFrm,
                  IIC_iLoad_bh_r, "ldrh", "\t$Rt, $addr",
                  [(set GPR:$Rt, (zextloadi16 addrmode3:$addr))]>;

// Loads with sign extension
def LDRSH : AI3ld<0b1111, 1, (outs GPR:$Rt), (ins addrmode3:$addr), LdMiscFrm,
                   IIC_iLoad_bh_r, "ldrsh", "\t$Rt, $addr",
                   [(set GPR:$Rt, (sextloadi16 addrmode3:$addr))]>;

def LDRSB : AI3ld<0b1101, 1, (outs GPR:$Rt), (ins addrmode3:$addr), LdMiscFrm,
                   IIC_iLoad_bh_r, "ldrsb", "\t$Rt, $addr",
                   [(set GPR:$Rt, (sextloadi8 addrmode3:$addr))]>;

let mayLoad = 1, hasSideEffects = 0, hasExtraDefRegAllocReq = 1 in {
  // Load doubleword
  def LDRD : AI3ld<0b1101, 0, (outs GPR:$Rt, GPR:$Rt2), (ins addrmode3:$addr),
                   LdMiscFrm, IIC_iLoad_d_r, "ldrd", "\t$Rt, $Rt2, $addr", []>,
             Requires<[IsARM, HasV5TE]>;
}

let mayLoad = 1, hasSideEffects = 0, hasNoSchedulingInfo = 1 in {
def LOADDUAL : ARMPseudoInst<(outs GPRPairOp:$Rt), (ins addrmode3:$addr),
                             64, IIC_iLoad_d_r, []>,
               Requires<[IsARM, HasV5TE]> {
  let AM = AddrMode3;
}
}

def LDA : AIldracq<0b00, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                    NoItinerary, "lda", "\t$Rt, $addr", []>;
def LDAB : AIldracq<0b10, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                    NoItinerary, "ldab", "\t$Rt, $addr", []>;
def LDAH : AIldracq<0b11, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                    NoItinerary, "ldah", "\t$Rt, $addr", []>;

// Indexed loads
multiclass AI2_ldridx<bit isByte, string opc,
                      InstrItinClass iii, InstrItinClass iir> {
  def _PRE_IMM  : AI2ldstidx<1, isByte, 1, (outs GPR:$Rt, GPR:$Rn_wb),
                      (ins addrmode_imm12_pre:$addr), IndexModePre, LdFrm, iii,
                      opc, "\t$Rt, $addr!", "$addr.base = $Rn_wb", []> {
    bits<17> addr;
    let Inst{25} = 0;
    let Inst{23} = addr{12};
    let Inst{19-16} = addr{16-13};
    let Inst{11-0} = addr{11-0};
    let DecoderMethod = "DecodeLDRPreImm";
  }

  def _PRE_REG  : AI2ldstidx<1, isByte, 1, (outs GPR:$Rt, GPR:$Rn_wb),
                      (ins ldst_so_reg:$addr), IndexModePre, LdFrm, iir,
                      opc, "\t$Rt, $addr!", "$addr.base = $Rn_wb", []> {
    bits<17> addr;
    let Inst{25} = 1;
    let Inst{23} = addr{12};
    let Inst{19-16} = addr{16-13};
    let Inst{11-0} = addr{11-0};
    let Inst{4} = 0;
    let DecoderMethod = "DecodeLDRPreReg";
  }

  def _POST_REG : AI2ldstidx<1, isByte, 0, (outs GPR:$Rt, GPR:$Rn_wb),
                       (ins addr_offset_none:$addr, am2offset_reg:$offset),
                       IndexModePost, LdFrm, iir,
                       opc, "\t$Rt, $addr, $offset",
                       "$addr.base = $Rn_wb", []> {
     // {12}     isAdd
     // {11-0}   imm12/Rm
     bits<14> offset;
     bits<4> addr;
     let Inst{25} = 1;
     let Inst{23} = offset{12};
     let Inst{19-16} = addr;
     let Inst{11-0} = offset{11-0};
     let Inst{4} = 0;

    let DecoderMethod = "DecodeAddrMode2IdxInstruction";
   }

   def _POST_IMM : AI2ldstidx<1, isByte, 0, (outs GPR:$Rt, GPR:$Rn_wb),
                       (ins addr_offset_none:$addr, am2offset_imm:$offset),
                      IndexModePost, LdFrm, iii,
                      opc, "\t$Rt, $addr, $offset",
                      "$addr.base = $Rn_wb", []> {
    // {12}     isAdd
    // {11-0}   imm12/Rm
    bits<14> offset;
    bits<4> addr;
    let Inst{25} = 0;
    let Inst{23} = offset{12};
    let Inst{19-16} = addr;
    let Inst{11-0} = offset{11-0};

    let DecoderMethod = "DecodeAddrMode2IdxInstruction";
  }

}

let mayLoad = 1, hasSideEffects = 0 in {
// FIXME: for LDR_PRE_REG etc. the itinerary should be either IIC_iLoad_ru or
// IIC_iLoad_siu depending on whether it the offset register is shifted.
defm LDR  : AI2_ldridx<0, "ldr", IIC_iLoad_iu, IIC_iLoad_ru>;
defm LDRB : AI2_ldridx<1, "ldrb", IIC_iLoad_bh_iu, IIC_iLoad_bh_ru>;
}

multiclass AI3_ldridx<bits<4> op, string opc, InstrItinClass itin> {
  def _PRE  : AI3ldstidx<op, 1, 1, (outs GPR:$Rt, GPR:$Rn_wb),
                        (ins addrmode3_pre:$addr), IndexModePre,
                        LdMiscFrm, itin,
                        opc, "\t$Rt, $addr!", "$addr.base = $Rn_wb", []> {
    bits<14> addr;
    let Inst{23}    = addr{8};      // U bit
    let Inst{22}    = addr{13};     // 1 == imm8, 0 == Rm
    let Inst{19-16} = addr{12-9};   // Rn
    let Inst{11-8}  = addr{7-4};    // imm7_4/zero
    let Inst{3-0}   = addr{3-0};    // imm3_0/Rm
    let DecoderMethod = "DecodeAddrMode3Instruction";
  }
  def _POST : AI3ldstidx<op, 1, 0, (outs GPR:$Rt, GPR:$Rn_wb),
                        (ins addr_offset_none:$addr, am3offset:$offset),
                        IndexModePost, LdMiscFrm, itin,
                        opc, "\t$Rt, $addr, $offset", "$addr.base = $Rn_wb",
                        []> {
    bits<10> offset;
    bits<4> addr;
    let Inst{23}    = offset{8};      // U bit
    let Inst{22}    = offset{9};      // 1 == imm8, 0 == Rm
    let Inst{19-16} = addr;
    let Inst{11-8}  = offset{7-4};    // imm7_4/zero
    let Inst{3-0}   = offset{3-0};    // imm3_0/Rm
    let DecoderMethod = "DecodeAddrMode3Instruction";
  }
}

let mayLoad = 1, hasSideEffects = 0 in {
defm LDRH  : AI3_ldridx<0b1011, "ldrh", IIC_iLoad_bh_ru>;
defm LDRSH : AI3_ldridx<0b1111, "ldrsh", IIC_iLoad_bh_ru>;
defm LDRSB : AI3_ldridx<0b1101, "ldrsb", IIC_iLoad_bh_ru>;
let hasExtraDefRegAllocReq = 1 in {
def LDRD_PRE : AI3ldstidx<0b1101, 0, 1, (outs GPR:$Rt, GPR:$Rt2, GPR:$Rn_wb),
                          (ins addrmode3_pre:$addr), IndexModePre,
                          LdMiscFrm, IIC_iLoad_d_ru,
                          "ldrd", "\t$Rt, $Rt2, $addr!",
                          "$addr.base = $Rn_wb", []> {
  bits<14> addr;
  let Inst{23}    = addr{8};      // U bit
  let Inst{22}    = addr{13};     // 1 == imm8, 0 == Rm
  let Inst{19-16} = addr{12-9};   // Rn
  let Inst{11-8}  = addr{7-4};    // imm7_4/zero
  let Inst{3-0}   = addr{3-0};    // imm3_0/Rm
  let DecoderMethod = "DecodeAddrMode3Instruction";
}
def LDRD_POST: AI3ldstidx<0b1101, 0, 0, (outs GPR:$Rt, GPR:$Rt2, GPR:$Rn_wb),
                          (ins addr_offset_none:$addr, am3offset:$offset),
                          IndexModePost, LdMiscFrm, IIC_iLoad_d_ru,
                          "ldrd", "\t$Rt, $Rt2, $addr, $offset",
                          "$addr.base = $Rn_wb", []> {
  bits<10> offset;
  bits<4> addr;
  let Inst{23}    = offset{8};      // U bit
  let Inst{22}    = offset{9};      // 1 == imm8, 0 == Rm
  let Inst{19-16} = addr;
  let Inst{11-8}  = offset{7-4};    // imm7_4/zero
  let Inst{3-0}   = offset{3-0};    // imm3_0/Rm
  let DecoderMethod = "DecodeAddrMode3Instruction";
}
} // hasExtraDefRegAllocReq = 1
} // mayLoad = 1, hasSideEffects = 0

// LDRT, LDRBT, LDRSBT, LDRHT, LDRSHT.
let mayLoad = 1, hasSideEffects = 0 in {
def LDRT_POST_REG : AI2ldstidx<1, 0, 0, (outs GPR:$Rt, GPR:$Rn_wb),
                    (ins addr_offset_none:$addr, am2offset_reg:$offset),
                    IndexModePost, LdFrm, IIC_iLoad_ru,
                    "ldrt", "\t$Rt, $addr, $offset",
                    "$addr.base = $Rn_wb", []> {
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> addr;
  let Inst{25} = 1;
  let Inst{23} = offset{12};
  let Inst{21} = 1; // overwrite
  let Inst{19-16} = addr;
  let Inst{11-5} = offset{11-5};
  let Inst{4} = 0;
  let Inst{3-0} = offset{3-0};
  let DecoderMethod = "DecodeAddrMode2IdxInstruction";
}

def LDRT_POST_IMM
  : AI2ldstidx<1, 0, 0, (outs GPR:$Rt, GPR:$Rn_wb),
               (ins addr_offset_none:$addr, am2offset_imm:$offset),
               IndexModePost, LdFrm, IIC_iLoad_ru,
               "ldrt", "\t$Rt, $addr, $offset", "$addr.base = $Rn_wb", []> {
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> addr;
  let Inst{25} = 0;
  let Inst{23} = offset{12};
  let Inst{21} = 1; // overwrite
  let Inst{19-16} = addr;
  let Inst{11-0} = offset{11-0};
  let DecoderMethod = "DecodeAddrMode2IdxInstruction";
}

def LDRBT_POST_REG : AI2ldstidx<1, 1, 0, (outs GPR:$Rt, GPR:$Rn_wb),
                     (ins addr_offset_none:$addr, am2offset_reg:$offset),
                     IndexModePost, LdFrm, IIC_iLoad_bh_ru,
                     "ldrbt", "\t$Rt, $addr, $offset",
                     "$addr.base = $Rn_wb", []> {
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> addr;
  let Inst{25} = 1;
  let Inst{23} = offset{12};
  let Inst{21} = 1; // overwrite
  let Inst{19-16} = addr;
  let Inst{11-5} = offset{11-5};
  let Inst{4} = 0;
  let Inst{3-0} = offset{3-0};
  let DecoderMethod = "DecodeAddrMode2IdxInstruction";
}

def LDRBT_POST_IMM
  : AI2ldstidx<1, 1, 0, (outs GPR:$Rt, GPR:$Rn_wb),
               (ins addr_offset_none:$addr, am2offset_imm:$offset),
               IndexModePost, LdFrm, IIC_iLoad_bh_ru,
               "ldrbt", "\t$Rt, $addr, $offset", "$addr.base = $Rn_wb", []> {
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> addr;
  let Inst{25} = 0;
  let Inst{23} = offset{12};
  let Inst{21} = 1; // overwrite
  let Inst{19-16} = addr;
  let Inst{11-0} = offset{11-0};
  let DecoderMethod = "DecodeAddrMode2IdxInstruction";
}

multiclass AI3ldrT<bits<4> op, string opc> {
  def i : AI3ldstidxT<op, 1, (outs GPR:$Rt, GPR:$base_wb),
                      (ins addr_offset_none:$addr, postidx_imm8:$offset),
                      IndexModePost, LdMiscFrm, IIC_iLoad_bh_ru, opc,
                      "\t$Rt, $addr, $offset", "$addr.base = $base_wb", []> {
    bits<9> offset;
    let Inst{23} = offset{8};
    let Inst{22} = 1;
    let Inst{11-8} = offset{7-4};
    let Inst{3-0} = offset{3-0};
  }
  def r : AI3ldstidxT<op, 1, (outs GPRnopc:$Rt, GPRnopc:$base_wb),
                      (ins addr_offset_none:$addr, postidx_reg:$Rm),
                      IndexModePost, LdMiscFrm, IIC_iLoad_bh_ru, opc,
                      "\t$Rt, $addr, $Rm", "$addr.base = $base_wb", []> {
    bits<5> Rm;
    let Inst{23} = Rm{4};
    let Inst{22} = 0;
    let Inst{11-8} = 0;
    let Unpredictable{11-8} = 0b1111;
    let Inst{3-0} = Rm{3-0};
    let DecoderMethod = "DecodeLDR";
  }

  def ii : ARMAsmPseudo<!strconcat(opc, "${p} $Rt, $addr"),
                        (ins addr_offset_none:$addr, pred:$p), (outs GPR:$Rt)>;
}

defm LDRSBT : AI3ldrT<0b1101, "ldrsbt">;
defm LDRHT  : AI3ldrT<0b1011, "ldrht">;
defm LDRSHT : AI3ldrT<0b1111, "ldrsht">;
}

def LDRT_POST
  : ARMAsmPseudo<"ldrt${q} $Rt, $addr", (ins addr_offset_none:$addr, pred:$q),
                 (outs GPR:$Rt)>;

def LDRBT_POST
  : ARMAsmPseudo<"ldrbt${q} $Rt, $addr", (ins addr_offset_none:$addr, pred:$q),
                 (outs GPR:$Rt)>;

// Pseudo instruction ldr Rt, =immediate
def LDRConstPool
  : ARMAsmPseudo<"ldr${q} $Rt, $immediate",
                 (ins const_pool_asm_imm:$immediate, pred:$q),
                 (outs GPR:$Rt)>;

// Store

// Stores with truncate
def STRH : AI3str<0b1011, (outs), (ins GPR:$Rt, addrmode3:$addr), StMiscFrm,
               IIC_iStore_bh_r, "strh", "\t$Rt, $addr",
               [(truncstorei16 GPR:$Rt, addrmode3:$addr)]>;

// Store doubleword
let mayStore = 1, hasSideEffects = 0, hasExtraSrcRegAllocReq = 1 in {
  def STRD : AI3str<0b1111, (outs), (ins GPR:$Rt, GPR:$Rt2, addrmode3:$addr),
                    StMiscFrm, IIC_iStore_d_r, "strd", "\t$Rt, $Rt2, $addr", []>,
             Requires<[IsARM, HasV5TE]> {
    let Inst{21} = 0;
  }
}

let mayStore = 1, hasSideEffects = 0, hasNoSchedulingInfo = 1 in {
def STOREDUAL : ARMPseudoInst<(outs), (ins GPRPairOp:$Rt, addrmode3:$addr),
                              64, IIC_iStore_d_r, []>,
                Requires<[IsARM, HasV5TE]> {
  let AM = AddrMode3;
}
}

// Indexed stores
multiclass AI2_stridx<bit isByte, string opc,
                      InstrItinClass iii, InstrItinClass iir> {
  def _PRE_IMM : AI2ldstidx<0, isByte, 1, (outs GPR:$Rn_wb),
                            (ins GPR:$Rt, addrmode_imm12_pre:$addr), IndexModePre,
                            StFrm, iii,
                            opc, "\t$Rt, $addr!",
                            "$addr.base = $Rn_wb,@earlyclobber $Rn_wb", []> {
    bits<17> addr;
    let Inst{25} = 0;
    let Inst{23}    = addr{12};     // U (add = ('U' == 1))
    let Inst{19-16} = addr{16-13};  // Rn
    let Inst{11-0}  = addr{11-0};   // imm12
    let DecoderMethod = "DecodeSTRPreImm";
  }

  def _PRE_REG  : AI2ldstidx<0, isByte, 1, (outs GPR:$Rn_wb),
                      (ins GPR:$Rt, ldst_so_reg:$addr),
                      IndexModePre, StFrm, iir,
                      opc, "\t$Rt, $addr!",
                      "$addr.base = $Rn_wb,@earlyclobber $Rn_wb", []> {
    bits<17> addr;
    let Inst{25} = 1;
    let Inst{23}    = addr{12};    // U (add = ('U' == 1))
    let Inst{19-16} = addr{16-13}; // Rn
    let Inst{11-0}  = addr{11-0};
    let Inst{4}     = 0;           // Inst{4} = 0
    let DecoderMethod = "DecodeSTRPreReg";
  }
  def _POST_REG : AI2ldstidx<0, isByte, 0, (outs GPR:$Rn_wb),
                (ins GPR:$Rt, addr_offset_none:$addr, am2offset_reg:$offset),
                IndexModePost, StFrm, iir,
                opc, "\t$Rt, $addr, $offset",
                "$addr.base = $Rn_wb,@earlyclobber $Rn_wb", []> {
     // {12}     isAdd
     // {11-0}   imm12/Rm
     bits<14> offset;
     bits<4> addr;
     let Inst{25} = 1;
     let Inst{23} = offset{12};
     let Inst{19-16} = addr;
     let Inst{11-0} = offset{11-0};
     let Inst{4} = 0;

    let DecoderMethod = "DecodeAddrMode2IdxInstruction";
   }

   def _POST_IMM : AI2ldstidx<0, isByte, 0, (outs GPR:$Rn_wb),
                (ins GPR:$Rt, addr_offset_none:$addr, am2offset_imm:$offset),
                IndexModePost, StFrm, iii,
                opc, "\t$Rt, $addr, $offset",
                "$addr.base = $Rn_wb,@earlyclobber $Rn_wb", []> {
    // {12}     isAdd
    // {11-0}   imm12/Rm
    bits<14> offset;
    bits<4> addr;
    let Inst{25} = 0;
    let Inst{23} = offset{12};
    let Inst{19-16} = addr;
    let Inst{11-0} = offset{11-0};

    let DecoderMethod = "DecodeAddrMode2IdxInstruction";
  }
}

let mayStore = 1, hasSideEffects = 0 in {
// FIXME: for STR_PRE_REG etc. the itinerary should be either IIC_iStore_ru or
// IIC_iStore_siu depending on whether it the offset register is shifted.
defm STR  : AI2_stridx<0, "str", IIC_iStore_iu, IIC_iStore_ru>;
defm STRB : AI2_stridx<1, "strb", IIC_iStore_bh_iu, IIC_iStore_bh_ru>;
}

def : ARMPat<(post_store GPR:$Rt, addr_offset_none:$addr,
                         am2offset_reg:$offset),
             (STR_POST_REG GPR:$Rt, addr_offset_none:$addr,
                           am2offset_reg:$offset)>;
def : ARMPat<(post_store GPR:$Rt, addr_offset_none:$addr,
                         am2offset_imm:$offset),
             (STR_POST_IMM GPR:$Rt, addr_offset_none:$addr,
                           am2offset_imm:$offset)>;
def : ARMPat<(post_truncsti8 GPR:$Rt, addr_offset_none:$addr,
                             am2offset_reg:$offset),
             (STRB_POST_REG GPR:$Rt, addr_offset_none:$addr,
                            am2offset_reg:$offset)>;
def : ARMPat<(post_truncsti8 GPR:$Rt, addr_offset_none:$addr,
                             am2offset_imm:$offset),
             (STRB_POST_IMM GPR:$Rt, addr_offset_none:$addr,
                            am2offset_imm:$offset)>;

// Pseudo-instructions for pattern matching the pre-indexed stores. We can't
// put the patterns on the instruction definitions directly as ISel wants
// the address base and offset to be separate operands, not a single
// complex operand like we represent the instructions themselves. The
// pseudos map between the two.
let usesCustomInserter = 1,
    Constraints = "$Rn = $Rn_wb,@earlyclobber $Rn_wb" in {
def STRi_preidx: ARMPseudoInst<(outs GPR:$Rn_wb),
               (ins GPR:$Rt, GPR:$Rn, am2offset_imm:$offset, pred:$p),
               4, IIC_iStore_ru,
            [(set GPR:$Rn_wb,
                  (pre_store GPR:$Rt, GPR:$Rn, am2offset_imm:$offset))]>;
def STRr_preidx: ARMPseudoInst<(outs GPR:$Rn_wb),
               (ins GPR:$Rt, GPR:$Rn, am2offset_reg:$offset, pred:$p),
               4, IIC_iStore_ru,
            [(set GPR:$Rn_wb,
                  (pre_store GPR:$Rt, GPR:$Rn, am2offset_reg:$offset))]>;
def STRBi_preidx: ARMPseudoInst<(outs GPR:$Rn_wb),
               (ins GPR:$Rt, GPR:$Rn, am2offset_imm:$offset, pred:$p),
               4, IIC_iStore_ru,
            [(set GPR:$Rn_wb,
                  (pre_truncsti8 GPR:$Rt, GPR:$Rn, am2offset_imm:$offset))]>;
def STRBr_preidx: ARMPseudoInst<(outs GPR:$Rn_wb),
               (ins GPR:$Rt, GPR:$Rn, am2offset_reg:$offset, pred:$p),
               4, IIC_iStore_ru,
            [(set GPR:$Rn_wb,
                  (pre_truncsti8 GPR:$Rt, GPR:$Rn, am2offset_reg:$offset))]>;
def STRH_preidx: ARMPseudoInst<(outs GPR:$Rn_wb),
               (ins GPR:$Rt, GPR:$Rn, am3offset:$offset, pred:$p),
               4, IIC_iStore_ru,
            [(set GPR:$Rn_wb,
                  (pre_truncsti16 GPR:$Rt, GPR:$Rn, am3offset:$offset))]>;
}



def STRH_PRE  : AI3ldstidx<0b1011, 0, 1, (outs GPR:$Rn_wb),
                           (ins GPR:$Rt, addrmode3_pre:$addr), IndexModePre,
                           StMiscFrm, IIC_iStore_bh_ru,
                           "strh", "\t$Rt, $addr!",
                           "$addr.base = $Rn_wb,@earlyclobber $Rn_wb", []> {
  bits<14> addr;
  let Inst{23}    = addr{8};      // U bit
  let Inst{22}    = addr{13};     // 1 == imm8, 0 == Rm
  let Inst{19-16} = addr{12-9};   // Rn
  let Inst{11-8}  = addr{7-4};    // imm7_4/zero
  let Inst{3-0}   = addr{3-0};    // imm3_0/Rm
  let DecoderMethod = "DecodeAddrMode3Instruction";
}

def STRH_POST : AI3ldstidx<0b1011, 0, 0, (outs GPR:$Rn_wb),
                       (ins GPR:$Rt, addr_offset_none:$addr, am3offset:$offset),
                       IndexModePost, StMiscFrm, IIC_iStore_bh_ru,
                       "strh", "\t$Rt, $addr, $offset",
                       "$addr.base = $Rn_wb,@earlyclobber $Rn_wb",
                   [(set GPR:$Rn_wb, (post_truncsti16 GPR:$Rt,
                                                      addr_offset_none:$addr,
                                                      am3offset:$offset))]> {
  bits<10> offset;
  bits<4> addr;
  let Inst{23}    = offset{8};      // U bit
  let Inst{22}    = offset{9};      // 1 == imm8, 0 == Rm
  let Inst{19-16} = addr;
  let Inst{11-8}  = offset{7-4};    // imm7_4/zero
  let Inst{3-0}   = offset{3-0};    // imm3_0/Rm
  let DecoderMethod = "DecodeAddrMode3Instruction";
}

let mayStore = 1, hasSideEffects = 0, hasExtraSrcRegAllocReq = 1 in {
def STRD_PRE : AI3ldstidx<0b1111, 0, 1, (outs GPR:$Rn_wb),
                          (ins GPR:$Rt, GPR:$Rt2, addrmode3_pre:$addr),
                          IndexModePre, StMiscFrm, IIC_iStore_d_ru,
                          "strd", "\t$Rt, $Rt2, $addr!",
                          "$addr.base = $Rn_wb", []> {
  bits<14> addr;
  let Inst{23}    = addr{8};      // U bit
  let Inst{22}    = addr{13};     // 1 == imm8, 0 == Rm
  let Inst{19-16} = addr{12-9};   // Rn
  let Inst{11-8}  = addr{7-4};    // imm7_4/zero
  let Inst{3-0}   = addr{3-0};    // imm3_0/Rm
  let DecoderMethod = "DecodeAddrMode3Instruction";
}

def STRD_POST: AI3ldstidx<0b1111, 0, 0, (outs GPR:$Rn_wb),
                          (ins GPR:$Rt, GPR:$Rt2, addr_offset_none:$addr,
                               am3offset:$offset),
                          IndexModePost, StMiscFrm, IIC_iStore_d_ru,
                          "strd", "\t$Rt, $Rt2, $addr, $offset",
                          "$addr.base = $Rn_wb", []> {
  bits<10> offset;
  bits<4> addr;
  let Inst{23}    = offset{8};      // U bit
  let Inst{22}    = offset{9};      // 1 == imm8, 0 == Rm
  let Inst{19-16} = addr;
  let Inst{11-8}  = offset{7-4};    // imm7_4/zero
  let Inst{3-0}   = offset{3-0};    // imm3_0/Rm
  let DecoderMethod = "DecodeAddrMode3Instruction";
}
} // mayStore = 1, hasSideEffects = 0, hasExtraSrcRegAllocReq = 1

// STRT, STRBT, and STRHT

def STRBT_POST_REG : AI2ldstidx<0, 1, 0, (outs GPR:$Rn_wb),
                   (ins GPR:$Rt, addr_offset_none:$addr, am2offset_reg:$offset),
                   IndexModePost, StFrm, IIC_iStore_bh_ru,
                   "strbt", "\t$Rt, $addr, $offset",
                   "$addr.base = $Rn_wb", []> {
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> addr;
  let Inst{25} = 1;
  let Inst{23} = offset{12};
  let Inst{21} = 1; // overwrite
  let Inst{19-16} = addr;
  let Inst{11-5} = offset{11-5};
  let Inst{4} = 0;
  let Inst{3-0} = offset{3-0};
  let DecoderMethod = "DecodeAddrMode2IdxInstruction";
}

def STRBT_POST_IMM
  : AI2ldstidx<0, 1, 0, (outs GPR:$Rn_wb),
               (ins GPR:$Rt, addr_offset_none:$addr, am2offset_imm:$offset),
               IndexModePost, StFrm, IIC_iStore_bh_ru,
               "strbt", "\t$Rt, $addr, $offset", "$addr.base = $Rn_wb", []> {
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> addr;
  let Inst{25} = 0;
  let Inst{23} = offset{12};
  let Inst{21} = 1; // overwrite
  let Inst{19-16} = addr;
  let Inst{11-0} = offset{11-0};
  let DecoderMethod = "DecodeAddrMode2IdxInstruction";
}

def STRBT_POST
  : ARMAsmPseudo<"strbt${q} $Rt, $addr",
                 (ins GPR:$Rt, addr_offset_none:$addr, pred:$q)>;

let mayStore = 1, hasSideEffects = 0 in {
def STRT_POST_REG : AI2ldstidx<0, 0, 0, (outs GPR:$Rn_wb),
                   (ins GPR:$Rt, addr_offset_none:$addr, am2offset_reg:$offset),
                   IndexModePost, StFrm, IIC_iStore_ru,
                   "strt", "\t$Rt, $addr, $offset",
                   "$addr.base = $Rn_wb", []> {
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> addr;
  let Inst{25} = 1;
  let Inst{23} = offset{12};
  let Inst{21} = 1; // overwrite
  let Inst{19-16} = addr;
  let Inst{11-5} = offset{11-5};
  let Inst{4} = 0;
  let Inst{3-0} = offset{3-0};
  let DecoderMethod = "DecodeAddrMode2IdxInstruction";
}

def STRT_POST_IMM
  : AI2ldstidx<0, 0, 0, (outs GPR:$Rn_wb),
               (ins GPR:$Rt, addr_offset_none:$addr, am2offset_imm:$offset),
               IndexModePost, StFrm, IIC_iStore_ru,
               "strt", "\t$Rt, $addr, $offset", "$addr.base = $Rn_wb", []> {
  // {12}     isAdd
  // {11-0}   imm12/Rm
  bits<14> offset;
  bits<4> addr;
  let Inst{25} = 0;
  let Inst{23} = offset{12};
  let Inst{21} = 1; // overwrite
  let Inst{19-16} = addr;
  let Inst{11-0} = offset{11-0};
  let DecoderMethod = "DecodeAddrMode2IdxInstruction";
}
}

def STRT_POST
  : ARMAsmPseudo<"strt${q} $Rt, $addr",
                 (ins GPR:$Rt, addr_offset_none:$addr, pred:$q)>;

multiclass AI3strT<bits<4> op, string opc> {
  def i : AI3ldstidxT<op, 0, (outs GPR:$base_wb),
                    (ins GPR:$Rt, addr_offset_none:$addr, postidx_imm8:$offset),
                    IndexModePost, StMiscFrm, IIC_iStore_bh_ru, opc,
                    "\t$Rt, $addr, $offset", "$addr.base = $base_wb", []> {
    bits<9> offset;
    let Inst{23} = offset{8};
    let Inst{22} = 1;
    let Inst{11-8} = offset{7-4};
    let Inst{3-0} = offset{3-0};
  }
  def r : AI3ldstidxT<op, 0, (outs GPR:$base_wb),
                      (ins GPR:$Rt, addr_offset_none:$addr, postidx_reg:$Rm),
                      IndexModePost, StMiscFrm, IIC_iStore_bh_ru, opc,
                      "\t$Rt, $addr, $Rm", "$addr.base = $base_wb", []> {
    bits<5> Rm;
    let Inst{23} = Rm{4};
    let Inst{22} = 0;
    let Inst{11-8} = 0;
    let Inst{3-0} = Rm{3-0};
  }
}


defm STRHT : AI3strT<0b1011, "strht">;

def STL : AIstrrel<0b00, (outs), (ins GPR:$Rt, addr_offset_none:$addr),
                   NoItinerary, "stl", "\t$Rt, $addr", []>;
def STLB : AIstrrel<0b10, (outs), (ins GPR:$Rt, addr_offset_none:$addr),
                    NoItinerary, "stlb", "\t$Rt, $addr", []>;
def STLH : AIstrrel<0b11, (outs), (ins GPR:$Rt, addr_offset_none:$addr),
                    NoItinerary, "stlh", "\t$Rt, $addr", []>;

//===----------------------------------------------------------------------===//
//  Load / store multiple Instructions.
//

multiclass arm_ldst_mult<string asm, string sfx, bit L_bit, bit P_bit, Format f,
                         InstrItinClass itin, InstrItinClass itin_upd> {
  // IA is the default, so no need for an explicit suffix on the
  // mnemonic here. Without it is the canonical spelling.
  def IA :
    AXI4<(outs), (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops),
         IndexModeNone, f, itin,
         !strconcat(asm, "${p}\t$Rn, $regs", sfx), "", []> {
    let Inst{24-23} = 0b01;       // Increment After
    let Inst{22}    = P_bit;
    let Inst{21}    = 0;          // No writeback
    let Inst{20}    = L_bit;
  }
  def IA_UPD :
    AXI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops),
         IndexModeUpd, f, itin_upd,
         !strconcat(asm, "${p}\t$Rn!, $regs", sfx), "$Rn = $wb", []> {
    let Inst{24-23} = 0b01;       // Increment After
    let Inst{22}    = P_bit;
    let Inst{21}    = 1;          // Writeback
    let Inst{20}    = L_bit;

    let DecoderMethod = "DecodeMemMultipleWritebackInstruction";
  }
  def DA :
    AXI4<(outs), (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops),
         IndexModeNone, f, itin,
         !strconcat(asm, "da${p}\t$Rn, $regs", sfx), "", []> {
    let Inst{24-23} = 0b00;       // Decrement After
    let Inst{22}    = P_bit;
    let Inst{21}    = 0;          // No writeback
    let Inst{20}    = L_bit;
  }
  def DA_UPD :
    AXI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops),
         IndexModeUpd, f, itin_upd,
         !strconcat(asm, "da${p}\t$Rn!, $regs", sfx), "$Rn = $wb", []> {
    let Inst{24-23} = 0b00;       // Decrement After
    let Inst{22}    = P_bit;
    let Inst{21}    = 1;          // Writeback
    let Inst{20}    = L_bit;

    let DecoderMethod = "DecodeMemMultipleWritebackInstruction";
  }
  def DB :
    AXI4<(outs), (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops),
         IndexModeNone, f, itin,
         !strconcat(asm, "db${p}\t$Rn, $regs", sfx), "", []> {
    let Inst{24-23} = 0b10;       // Decrement Before
    let Inst{22}    = P_bit;
    let Inst{21}    = 0;          // No writeback
    let Inst{20}    = L_bit;
  }
  def DB_UPD :
    AXI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops),
         IndexModeUpd, f, itin_upd,
         !strconcat(asm, "db${p}\t$Rn!, $regs", sfx), "$Rn = $wb", []> {
    let Inst{24-23} = 0b10;       // Decrement Before
    let Inst{22}    = P_bit;
    let Inst{21}    = 1;          // Writeback
    let Inst{20}    = L_bit;

    let DecoderMethod = "DecodeMemMultipleWritebackInstruction";
  }
  def IB :
    AXI4<(outs), (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops),
         IndexModeNone, f, itin,
         !strconcat(asm, "ib${p}\t$Rn, $regs", sfx), "", []> {
    let Inst{24-23} = 0b11;       // Increment Before
    let Inst{22}    = P_bit;
    let Inst{21}    = 0;          // No writeback
    let Inst{20}    = L_bit;
  }
  def IB_UPD :
    AXI4<(outs GPR:$wb), (ins GPR:$Rn, pred:$p, reglist:$regs, variable_ops),
         IndexModeUpd, f, itin_upd,
         !strconcat(asm, "ib${p}\t$Rn!, $regs", sfx), "$Rn = $wb", []> {
    let Inst{24-23} = 0b11;       // Increment Before
    let Inst{22}    = P_bit;
    let Inst{21}    = 1;          // Writeback
    let Inst{20}    = L_bit;

    let DecoderMethod = "DecodeMemMultipleWritebackInstruction";
  }
}

let hasSideEffects = 0 in {

let mayLoad = 1, hasExtraDefRegAllocReq = 1, variadicOpsAreDefs = 1 in
defm LDM : arm_ldst_mult<"ldm", "", 1, 0, LdStMulFrm, IIC_iLoad_m,
                         IIC_iLoad_mu>, ComplexDeprecationPredicate<"ARMLoad">;

let mayStore = 1, hasExtraSrcRegAllocReq = 1 in
defm STM : arm_ldst_mult<"stm", "", 0, 0, LdStMulFrm, IIC_iStore_m,
                         IIC_iStore_mu>,
           ComplexDeprecationPredicate<"ARMStore">;

} // hasSideEffects

// FIXME: remove when we have a way to marking a MI with these properties.
// FIXME: Should pc be an implicit operand like PICADD, etc?
let isReturn = 1, isTerminator = 1, isBarrier = 1, mayLoad = 1,
    hasExtraDefRegAllocReq = 1, isCodeGenOnly = 1 in
def LDMIA_RET : ARMPseudoExpand<(outs GPR:$wb), (ins GPR:$Rn, pred:$p,
                                                 reglist:$regs, variable_ops),
                     4, IIC_iLoad_mBr, [],
                     (LDMIA_UPD GPR:$wb, GPR:$Rn, pred:$p, reglist:$regs)>,
      RegConstraint<"$Rn = $wb">;

let mayLoad = 1, hasExtraDefRegAllocReq = 1 in
defm sysLDM : arm_ldst_mult<"ldm", " ^", 1, 1, LdStMulFrm, IIC_iLoad_m,
                               IIC_iLoad_mu>;

let mayStore = 1, hasExtraSrcRegAllocReq = 1 in
defm sysSTM : arm_ldst_mult<"stm", " ^", 0, 1, LdStMulFrm, IIC_iStore_m,
                               IIC_iStore_mu>;



//===----------------------------------------------------------------------===//
//  Move Instructions.
//

let hasSideEffects = 0, isMoveReg = 1 in
def MOVr : AsI1<0b1101, (outs GPR:$Rd), (ins GPR:$Rm), DPFrm, IIC_iMOVr,
                "mov", "\t$Rd, $Rm", []>, UnaryDP, Sched<[WriteALU]> {
  bits<4> Rd;
  bits<4> Rm;

  let Inst{19-16} = 0b0000;
  let Inst{11-4} = 0b00000000;
  let Inst{25} = 0;
  let Inst{3-0} = Rm;
  let Inst{15-12} = Rd;
}

// A version for the smaller set of tail call registers.
let hasSideEffects = 0 in
def MOVr_TC : AsI1<0b1101, (outs tcGPR:$Rd), (ins tcGPR:$Rm), DPFrm,
                IIC_iMOVr, "mov", "\t$Rd, $Rm", []>, UnaryDP, Sched<[WriteALU]> {
  bits<4> Rd;
  bits<4> Rm;

  let Inst{11-4} = 0b00000000;
  let Inst{25} = 0;
  let Inst{3-0} = Rm;
  let Inst{15-12} = Rd;
}

def MOVsr : AsI1<0b1101, (outs GPRnopc:$Rd), (ins shift_so_reg_reg:$src),
                DPSoRegRegFrm, IIC_iMOVsr,
                "mov", "\t$Rd, $src",
                [(set GPRnopc:$Rd, shift_so_reg_reg:$src)]>, UnaryDP,
                Sched<[WriteALU]> {
  bits<4> Rd;
  bits<12> src;
  let Inst{15-12} = Rd;
  let Inst{19-16} = 0b0000;
  let Inst{11-8} = src{11-8};
  let Inst{7} = 0;
  let Inst{6-5} = src{6-5};
  let Inst{4} = 1;
  let Inst{3-0} = src{3-0};
  let Inst{25} = 0;
}

def MOVsi : AsI1<0b1101, (outs GPR:$Rd), (ins shift_so_reg_imm:$src),
                DPSoRegImmFrm, IIC_iMOVsr,
                "mov", "\t$Rd, $src", [(set GPR:$Rd, shift_so_reg_imm:$src)]>,
                UnaryDP, Sched<[WriteALU]> {
  bits<4> Rd;
  bits<12> src;
  let Inst{15-12} = Rd;
  let Inst{19-16} = 0b0000;
  let Inst{11-5} = src{11-5};
  let Inst{4} = 0;
  let Inst{3-0} = src{3-0};
  let Inst{25} = 0;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in
def MOVi : AsI1<0b1101, (outs GPR:$Rd), (ins mod_imm:$imm), DPFrm, IIC_iMOVi,
                "mov", "\t$Rd, $imm", [(set GPR:$Rd, mod_imm:$imm)]>, UnaryDP,
                Sched<[WriteALU]> {
  bits<4> Rd;
  bits<12> imm;
  let Inst{25} = 1;
  let Inst{15-12} = Rd;
  let Inst{19-16} = 0b0000;
  let Inst{11-0} = imm;
}

let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in
def MOVi16 : AI1<0b1000, (outs GPR:$Rd), (ins imm0_65535_expr:$imm),
                 DPFrm, IIC_iMOVi,
                 "movw", "\t$Rd, $imm",
                 [(set GPR:$Rd, imm0_65535:$imm)]>,
                 Requires<[IsARM, HasV6T2]>, UnaryDP, Sched<[WriteALU]> {
  bits<4> Rd;
  bits<16> imm;
  let Inst{15-12} = Rd;
  let Inst{11-0}  = imm{11-0};
  let Inst{19-16} = imm{15-12};
  let Inst{20} = 0;
  let Inst{25} = 1;
  let DecoderMethod = "DecodeArmMOVTWInstruction";
}

def : InstAlias<"mov${p} $Rd, $imm",
                (MOVi16 GPR:$Rd, imm0_65535_expr:$imm, pred:$p), 0>,
        Requires<[IsARM, HasV6T2]>;

// This gets lowered to a single 4-byte instructions
let Size = 4 in
def MOVi16_ga_pcrel : PseudoInst<(outs GPR:$Rd),
                                (ins i32imm:$addr, pclabel:$id), IIC_iMOVi, []>,
                      Sched<[WriteALU]>;

let Constraints = "$src = $Rd" in {
def MOVTi16 : AI1<0b1010, (outs GPRnopc:$Rd),
                  (ins GPR:$src, imm0_65535_expr:$imm),
                  DPFrm, IIC_iMOVi,
                  "movt", "\t$Rd, $imm",
                  [(set GPRnopc:$Rd,
                        (or (and GPR:$src, 0xffff),
                            lo16AllZero:$imm))]>, UnaryDP,
                  Requires<[IsARM, HasV6T2]>, Sched<[WriteALU]> {
  bits<4> Rd;
  bits<16> imm;
  let Inst{15-12} = Rd;
  let Inst{11-0}  = imm{11-0};
  let Inst{19-16} = imm{15-12};
  let Inst{20} = 0;
  let Inst{25} = 1;
  let DecoderMethod = "DecodeArmMOVTWInstruction";
}

// This gets lowered to a single 4-byte instructions
let Size = 4 in
def MOVTi16_ga_pcrel : PseudoInst<(outs GPR:$Rd),
                      (ins GPR:$src, i32imm:$addr, pclabel:$id), IIC_iMOVi, []>,
                      Sched<[WriteALU]>;

} // Constraints

def : ARMPat<(or GPR:$src, 0xffff0000), (MOVTi16 GPR:$src, 0xffff)>,
      Requires<[IsARM, HasV6T2]>;

let Uses = [CPSR] in
def RRX: PseudoInst<(outs GPR:$Rd), (ins GPR:$Rm), IIC_iMOVsi,
                    [(set GPR:$Rd, (ARMrrx GPR:$Rm))]>, UnaryDP,
                    Requires<[IsARM]>, Sched<[WriteALU]>;

// These aren't really mov instructions, but we have to define them this way
// due to glue operands.

let Defs = [CPSR] in {
def MOVsrl_glue : PseudoInst<(outs GPR:$dst), (ins GPR:$src), IIC_iMOVsi,
                      [(set GPR:$dst, (ARMsrl_glue GPR:$src))]>, UnaryDP,
                      Sched<[WriteALU]>, Requires<[IsARM]>;
def MOVsra_glue : PseudoInst<(outs GPR:$dst), (ins GPR:$src), IIC_iMOVsi,
                      [(set GPR:$dst, (ARMsra_glue GPR:$src))]>, UnaryDP,
                      Sched<[WriteALU]>, Requires<[IsARM]>;
}

//===----------------------------------------------------------------------===//
//  Extend Instructions.
//

// Sign extenders

def SXTB  : AI_ext_rrot<0b01101010,
                         "sxtb", UnOpFrag<(sext_inreg node:$Src, i8)>>;
def SXTH  : AI_ext_rrot<0b01101011,
                         "sxth", UnOpFrag<(sext_inreg node:$Src, i16)>>;

def SXTAB : AI_exta_rrot<0b01101010,
               "sxtab", BinOpFrag<(add node:$LHS, (sext_inreg node:$RHS, i8))>>;
def SXTAH : AI_exta_rrot<0b01101011,
               "sxtah", BinOpFrag<(add node:$LHS, (sext_inreg node:$RHS,i16))>>;

def : ARMV6Pat<(add rGPR:$Rn, (sext_inreg (srl rGPR:$Rm, rot_imm:$rot), i8)),
               (SXTAB rGPR:$Rn, rGPR:$Rm, rot_imm:$rot)>;
def : ARMV6Pat<(add rGPR:$Rn, (sext_inreg (srl rGPR:$Rm, imm8_or_16:$rot),
                                          i16)),
               (SXTAH rGPR:$Rn, rGPR:$Rm, rot_imm:$rot)>;

def SXTB16  : AI_ext_rrot_np<0b01101000, "sxtb16">;
def : ARMV6Pat<(int_arm_sxtb16 GPR:$Src),
               (SXTB16 GPR:$Src, 0)>;
def : ARMV6Pat<(int_arm_sxtb16 (rotr GPR:$Src, rot_imm:$rot)),
               (SXTB16 GPR:$Src, rot_imm:$rot)>;

def SXTAB16 : AI_exta_rrot_np<0b01101000, "sxtab16">;
def : ARMV6Pat<(int_arm_sxtab16 GPR:$LHS, GPR:$RHS),
               (SXTAB16 GPR:$LHS, GPR:$RHS, 0)>;
def : ARMV6Pat<(int_arm_sxtab16 GPR:$LHS, (rotr GPR:$RHS, rot_imm:$rot)),
               (SXTAB16 GPR:$LHS, GPR:$RHS, rot_imm:$rot)>;

// Zero extenders

let AddedComplexity = 16 in {
def UXTB   : AI_ext_rrot<0b01101110,
                          "uxtb"  , UnOpFrag<(and node:$Src, 0x000000FF)>>;
def UXTH   : AI_ext_rrot<0b01101111,
                          "uxth"  , UnOpFrag<(and node:$Src, 0x0000FFFF)>>;
def UXTB16 : AI_ext_rrot<0b01101100,
                          "uxtb16", UnOpFrag<(and node:$Src, 0x00FF00FF)>>;

// FIXME: This pattern incorrectly assumes the shl operator is a rotate.
//        The transformation should probably be done as a combiner action
//        instead so we can include a check for masking back in the upper
//        eight bits of the source into the lower eight bits of the result.
//def : ARMV6Pat<(and (shl GPR:$Src, (i32 8)), 0xFF00FF),
//               (UXTB16r_rot GPR:$Src, 3)>;
def : ARMV6Pat<(and (srl GPR:$Src, (i32 8)), 0xFF00FF),
               (UXTB16 GPR:$Src, 1)>;
def : ARMV6Pat<(int_arm_uxtb16 GPR:$Src),
               (UXTB16 GPR:$Src, 0)>;
def : ARMV6Pat<(int_arm_uxtb16 (rotr GPR:$Src, rot_imm:$rot)),
               (UXTB16 GPR:$Src, rot_imm:$rot)>;

def UXTAB : AI_exta_rrot<0b01101110, "uxtab",
                        BinOpFrag<(add node:$LHS, (and node:$RHS, 0x00FF))>>;
def UXTAH : AI_exta_rrot<0b01101111, "uxtah",
                        BinOpFrag<(add node:$LHS, (and node:$RHS, 0xFFFF))>>;

def : ARMV6Pat<(add rGPR:$Rn, (and (srl rGPR:$Rm, rot_imm:$rot), 0xFF)),
               (UXTAB rGPR:$Rn, rGPR:$Rm, rot_imm:$rot)>;
def : ARMV6Pat<(add rGPR:$Rn, (and (srl rGPR:$Rm, imm8_or_16:$rot), 0xFFFF)),
               (UXTAH rGPR:$Rn, rGPR:$Rm, rot_imm:$rot)>;
}

// This isn't safe in general, the add is two 16-bit units, not a 32-bit add.
def UXTAB16 : AI_exta_rrot_np<0b01101100, "uxtab16">;
def : ARMV6Pat<(int_arm_uxtab16 GPR:$LHS, GPR:$RHS),
               (UXTAB16 GPR:$LHS, GPR:$RHS, 0)>;
def : ARMV6Pat<(int_arm_uxtab16 GPR:$LHS, (rotr GPR:$RHS, rot_imm:$rot)),
               (UXTAB16 GPR:$LHS, GPR:$RHS, rot_imm:$rot)>;


def SBFX  : I<(outs GPRnopc:$Rd),
              (ins GPRnopc:$Rn, imm0_31:$lsb, imm1_32:$width),
               AddrMode1, 4, IndexModeNone, DPFrm, IIC_iUNAsi,
               "sbfx", "\t$Rd, $Rn, $lsb, $width", "", []>,
               Requires<[IsARM, HasV6T2]> {
  bits<4> Rd;
  bits<4> Rn;
  bits<5> lsb;
  bits<5> width;
  let Inst{27-21} = 0b0111101;
  let Inst{6-4}   = 0b101;
  let Inst{20-16} = width;
  let Inst{15-12} = Rd;
  let Inst{11-7}  = lsb;
  let Inst{3-0}   = Rn;
}

def UBFX  : I<(outs GPRnopc:$Rd),
              (ins GPRnopc:$Rn, imm0_31:$lsb, imm1_32:$width),
               AddrMode1, 4, IndexModeNone, DPFrm, IIC_iUNAsi,
               "ubfx", "\t$Rd, $Rn, $lsb, $width", "", []>,
               Requires<[IsARM, HasV6T2]> {
  bits<4> Rd;
  bits<4> Rn;
  bits<5> lsb;
  bits<5> width;
  let Inst{27-21} = 0b0111111;
  let Inst{6-4}   = 0b101;
  let Inst{20-16} = width;
  let Inst{15-12} = Rd;
  let Inst{11-7}  = lsb;
  let Inst{3-0}   = Rn;
}

//===----------------------------------------------------------------------===//
//  Arithmetic Instructions.
//

let isAdd = 1 in
defm ADD  : AsI1_bin_irs<0b0100, "add",
                         IIC_iALUi, IIC_iALUr, IIC_iALUsr, add, 1>;
defm SUB  : AsI1_bin_irs<0b0010, "sub",
                         IIC_iALUi, IIC_iALUr, IIC_iALUsr, sub>;

// ADD and SUB with 's' bit set.
//
// Currently, ADDS/SUBS are pseudo opcodes that exist only in the
// selection DAG. They are "lowered" to real ADD/SUB opcodes by
// AdjustInstrPostInstrSelection where we determine whether or not to
// set the "s" bit based on CPSR liveness.
//
// FIXME: Eliminate ADDS/SUBS pseudo opcodes after adding tablegen
// support for an optional CPSR definition that corresponds to the DAG
// node's second value. We can then eliminate the implicit def of CPSR.
let isAdd = 1 in
defm ADDS : AsI1_bin_s_irs<IIC_iALUi, IIC_iALUr, IIC_iALUsr, ARMaddc, 1>;
defm SUBS : AsI1_bin_s_irs<IIC_iALUi, IIC_iALUr, IIC_iALUsr, ARMsubc>;

let isAdd = 1 in
defm ADC : AI1_adde_sube_irs<0b0101, "adc", ARMadde, 1>;
defm SBC : AI1_adde_sube_irs<0b0110, "sbc", ARMsube>;

defm RSB  : AsI1_rbin_irs<0b0011, "rsb",
                          IIC_iALUi, IIC_iALUr, IIC_iALUsr,
                          sub>;

// FIXME: Eliminate them if we can write def : Pat patterns which defines
// CPSR and the implicit def of CPSR is not needed.
defm RSBS : AsI1_rbin_s_is<IIC_iALUi, IIC_iALUsr, ARMsubc>;

defm RSC : AI1_rsc_irs<0b0111, "rsc", ARMsube>;

// (sub X, imm) gets canonicalized to (add X, -imm).  Match this form.
// The assume-no-carry-in form uses the negation of the input since add/sub
// assume opposite meanings of the carry flag (i.e., carry == !borrow).
// See the definition of AddWithCarry() in the ARM ARM A2.2.1 for the gory
// details.
def : ARMPat<(add     GPR:$src, mod_imm_neg:$imm),
             (SUBri   GPR:$src, mod_imm_neg:$imm)>;
def : ARMPat<(ARMaddc GPR:$src, mod_imm_neg:$imm),
             (SUBSri  GPR:$src, mod_imm_neg:$imm)>;

def : ARMPat<(add     GPR:$src, imm0_65535_neg:$imm),
             (SUBrr   GPR:$src, (MOVi16 (imm_neg_XFORM imm:$imm)))>,
             Requires<[IsARM, HasV6T2]>;
def : ARMPat<(ARMaddc GPR:$src, imm0_65535_neg:$imm),
             (SUBSrr  GPR:$src, (MOVi16 (imm_neg_XFORM imm:$imm)))>,
             Requires<[IsARM, HasV6T2]>;

// The with-carry-in form matches bitwise not instead of the negation.
// Effectively, the inverse interpretation of the carry flag already accounts
// for part of the negation.
def : ARMPat<(ARMadde GPR:$src, mod_imm_not:$imm, CPSR),
             (SBCri   GPR:$src, mod_imm_not:$imm)>;
def : ARMPat<(ARMadde GPR:$src, imm0_65535_neg:$imm, CPSR),
             (SBCrr   GPR:$src, (MOVi16 (imm_not_XFORM imm:$imm)))>,
             Requires<[IsARM, HasV6T2]>;

// Note: These are implemented in C++ code, because they have to generate
// ADD/SUBrs instructions, which use a complex pattern that a xform function
// cannot produce.
// (mul X, 2^n+1) -> (add (X << n), X)
// (mul X, 2^n-1) -> (rsb X, (X << n))

// ARM Arithmetic Instruction
// GPR:$dst = GPR:$a op GPR:$b
class AAI<bits<8> op27_20, bits<8> op11_4, string opc,
          list<dag> pattern = [],
          dag iops = (ins GPRnopc:$Rn, GPRnopc:$Rm),
          string asm = "\t$Rd, $Rn, $Rm">
  : AI<(outs GPRnopc:$Rd), iops, DPFrm, IIC_iALUr, opc, asm, pattern>,
    Sched<[WriteALU, ReadALU, ReadALU]> {
  bits<4> Rn;
  bits<4> Rd;
  bits<4> Rm;
  let Inst{27-20} = op27_20;
  let Inst{11-4} = op11_4;
  let Inst{19-16} = Rn;
  let Inst{15-12} = Rd;
  let Inst{3-0}   = Rm;

  let Unpredictable{11-8} = 0b1111;
}

// Wrappers around the AAI class
class AAIRevOpr<bits<8> op27_20, bits<8> op11_4, string opc,
                list<dag> pattern = []>
  : AAI<op27_20, op11_4, opc,
        pattern,
        (ins GPRnopc:$Rm, GPRnopc:$Rn),
        "\t$Rd, $Rm, $Rn">;

class AAIIntrinsic<bits<8> op27_20, bits<8> op11_4, string opc,
                 Intrinsic intrinsic>
  : AAI<op27_20, op11_4, opc,
        [(set GPRnopc:$Rd, (intrinsic GPRnopc:$Rn, GPRnopc:$Rm))]>;

// Saturating add/subtract
let hasSideEffects = 1 in {
def QADD8   : AAIIntrinsic<0b01100010, 0b11111001, "qadd8", int_arm_qadd8>;
def QADD16  : AAIIntrinsic<0b01100010, 0b11110001, "qadd16", int_arm_qadd16>;
def QSUB16  : AAIIntrinsic<0b01100010, 0b11110111, "qsub16", int_arm_qsub16>;
def QSUB8   : AAIIntrinsic<0b01100010, 0b11111111, "qsub8", int_arm_qsub8>;

def QDADD   : AAIRevOpr<0b00010100, 0b00000101, "qdadd",
              [(set GPRnopc:$Rd, (int_arm_qadd GPRnopc:$Rm,
                                  (int_arm_qadd GPRnopc:$Rn, GPRnopc:$Rn)))]>;
def QDSUB   : AAIRevOpr<0b00010110, 0b00000101, "qdsub",
              [(set GPRnopc:$Rd, (int_arm_qsub GPRnopc:$Rm,
                                  (int_arm_qadd GPRnopc:$Rn, GPRnopc:$Rn)))]>;
def QSUB    : AAIRevOpr<0b00010010, 0b00000101, "qsub",
              [(set GPRnopc:$Rd, (int_arm_qsub GPRnopc:$Rm, GPRnopc:$Rn))]>;
let DecoderMethod = "DecodeQADDInstruction" in
  def QADD    : AAIRevOpr<0b00010000, 0b00000101, "qadd",
                [(set GPRnopc:$Rd, (int_arm_qadd GPRnopc:$Rm, GPRnopc:$Rn))]>;
}

def : ARMV5TEPat<(saddsat GPR:$a, GPR:$b),
                 (QADD GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(ssubsat GPR:$a, GPR:$b),
                 (QSUB GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(saddsat rGPR:$Rm, (saddsat rGPR:$Rn, rGPR:$Rn)),
                 (QDADD rGPR:$Rm, rGPR:$Rn)>;
def : ARMV5TEPat<(ssubsat rGPR:$Rm, (saddsat rGPR:$Rn, rGPR:$Rn)),
                 (QDSUB rGPR:$Rm, rGPR:$Rn)>;

def : ARMV6Pat<(ARMqadd8b rGPR:$Rm, rGPR:$Rn),
               (QADD8 rGPR:$Rm, rGPR:$Rn)>;
def : ARMV6Pat<(ARMqsub8b rGPR:$Rm, rGPR:$Rn),
               (QSUB8 rGPR:$Rm, rGPR:$Rn)>;
def : ARMV6Pat<(ARMqadd16b rGPR:$Rm, rGPR:$Rn),
               (QADD16 rGPR:$Rm, rGPR:$Rn)>;
def : ARMV6Pat<(ARMqsub16b rGPR:$Rm, rGPR:$Rn),
               (QSUB16 rGPR:$Rm, rGPR:$Rn)>;

def UQADD16 : AAIIntrinsic<0b01100110, 0b11110001, "uqadd16", int_arm_uqadd16>;
def UQADD8  : AAIIntrinsic<0b01100110, 0b11111001, "uqadd8", int_arm_uqadd8>;
def UQSUB16 : AAIIntrinsic<0b01100110, 0b11110111, "uqsub16", int_arm_uqsub16>;
def UQSUB8  : AAIIntrinsic<0b01100110, 0b11111111, "uqsub8", int_arm_uqsub8>;
def QASX    : AAIIntrinsic<0b01100010, 0b11110011, "qasx", int_arm_qasx>;
def QSAX    : AAIIntrinsic<0b01100010, 0b11110101, "qsax", int_arm_qsax>;
def UQASX   : AAIIntrinsic<0b01100110, 0b11110011, "uqasx", int_arm_uqasx>;
def UQSAX   : AAIIntrinsic<0b01100110, 0b11110101, "uqsax", int_arm_uqsax>;

def : ARMV6Pat<(ARMuqadd8b rGPR:$Rm, rGPR:$Rn),
               (UQADD8 rGPR:$Rm, rGPR:$Rn)>;
def : ARMV6Pat<(ARMuqsub8b rGPR:$Rm, rGPR:$Rn),
               (UQSUB8 rGPR:$Rm, rGPR:$Rn)>;
def : ARMV6Pat<(ARMuqadd16b rGPR:$Rm, rGPR:$Rn),
               (UQADD16 rGPR:$Rm, rGPR:$Rn)>;
def : ARMV6Pat<(ARMuqsub16b rGPR:$Rm, rGPR:$Rn),
               (UQSUB16 rGPR:$Rm, rGPR:$Rn)>;


// Signed/Unsigned add/subtract

def SASX   : AAIIntrinsic<0b01100001, 0b11110011, "sasx", int_arm_sasx>;
def SADD16 : AAIIntrinsic<0b01100001, 0b11110001, "sadd16", int_arm_sadd16>;
def SADD8  : AAIIntrinsic<0b01100001, 0b11111001, "sadd8", int_arm_sadd8>;
def SSAX   : AAIIntrinsic<0b01100001, 0b11110101, "ssax", int_arm_ssax>;
def SSUB16 : AAIIntrinsic<0b01100001, 0b11110111, "ssub16", int_arm_ssub16>;
def SSUB8  : AAIIntrinsic<0b01100001, 0b11111111, "ssub8", int_arm_ssub8>;
def UASX   : AAIIntrinsic<0b01100101, 0b11110011, "uasx", int_arm_uasx>;
def UADD16 : AAIIntrinsic<0b01100101, 0b11110001, "uadd16", int_arm_uadd16>;
def UADD8  : AAIIntrinsic<0b01100101, 0b11111001, "uadd8", int_arm_uadd8>;
def USAX   : AAIIntrinsic<0b01100101, 0b11110101, "usax", int_arm_usax>;
def USUB16 : AAIIntrinsic<0b01100101, 0b11110111, "usub16", int_arm_usub16>;
def USUB8  : AAIIntrinsic<0b01100101, 0b11111111, "usub8", int_arm_usub8>;

// Signed/Unsigned halving add/subtract

def SHASX   : AAIIntrinsic<0b01100011, 0b11110011, "shasx", int_arm_shasx>;
def SHADD16 : AAIIntrinsic<0b01100011, 0b11110001, "shadd16", int_arm_shadd16>;
def SHADD8  : AAIIntrinsic<0b01100011, 0b11111001, "shadd8", int_arm_shadd8>;
def SHSAX   : AAIIntrinsic<0b01100011, 0b11110101, "shsax", int_arm_shsax>;
def SHSUB16 : AAIIntrinsic<0b01100011, 0b11110111, "shsub16", int_arm_shsub16>;
def SHSUB8  : AAIIntrinsic<0b01100011, 0b11111111, "shsub8", int_arm_shsub8>;
def UHASX   : AAIIntrinsic<0b01100111, 0b11110011, "uhasx", int_arm_uhasx>;
def UHADD16 : AAIIntrinsic<0b01100111, 0b11110001, "uhadd16", int_arm_uhadd16>;
def UHADD8  : AAIIntrinsic<0b01100111, 0b11111001, "uhadd8", int_arm_uhadd8>;
def UHSAX   : AAIIntrinsic<0b01100111, 0b11110101, "uhsax", int_arm_uhsax>;
def UHSUB16 : AAIIntrinsic<0b01100111, 0b11110111, "uhsub16", int_arm_uhsub16>;
def UHSUB8  : AAIIntrinsic<0b01100111, 0b11111111, "uhsub8", int_arm_uhsub8>;

// Unsigned Sum of Absolute Differences [and Accumulate].

def USAD8  : AI<(outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
                MulFrm /* for convenience */, NoItinerary, "usad8",
                "\t$Rd, $Rn, $Rm",
             [(set GPR:$Rd, (int_arm_usad8 GPR:$Rn, GPR:$Rm))]>,
             Requires<[IsARM, HasV6]>, Sched<[WriteALU, ReadALU, ReadALU]> {
  bits<4> Rd;
  bits<4> Rn;
  bits<4> Rm;
  let Inst{27-20} = 0b01111000;
  let Inst{15-12} = 0b1111;
  let Inst{7-4} = 0b0001;
  let Inst{19-16} = Rd;
  let Inst{11-8} = Rm;
  let Inst{3-0} = Rn;
}
def USADA8 : AI<(outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm, GPR:$Ra),
                MulFrm /* for convenience */, NoItinerary, "usada8",
                "\t$Rd, $Rn, $Rm, $Ra",
             [(set GPR:$Rd, (int_arm_usada8 GPR:$Rn, GPR:$Rm, GPR:$Ra))]>,
             Requires<[IsARM, HasV6]>, Sched<[WriteALU, ReadALU, ReadALU]>{
  bits<4> Rd;
  bits<4> Rn;
  bits<4> Rm;
  bits<4> Ra;
  let Inst{27-20} = 0b01111000;
  let Inst{7-4} = 0b0001;
  let Inst{19-16} = Rd;
  let Inst{15-12} = Ra;
  let Inst{11-8} = Rm;
  let Inst{3-0} = Rn;
}

// Signed/Unsigned saturate
def SSAT : AI<(outs GPRnopc:$Rd),
              (ins imm1_32:$sat_imm, GPRnopc:$Rn, shift_imm:$sh),
              SatFrm, NoItinerary, "ssat", "\t$Rd, $sat_imm, $Rn$sh", []>,
              Requires<[IsARM,HasV6]>{
  bits<4> Rd;
  bits<5> sat_imm;
  bits<4> Rn;
  bits<8> sh;
  let Inst{27-21} = 0b0110101;
  let Inst{5-4} = 0b01;
  let Inst{20-16} = sat_imm;
  let Inst{15-12} = Rd;
  let Inst{11-7} = sh{4-0};
  let Inst{6} = sh{5};
  let Inst{3-0} = Rn;
}

def SSAT16 : AI<(outs GPRnopc:$Rd),
                (ins imm1_16:$sat_imm, GPRnopc:$Rn), SatFrm,
                NoItinerary, "ssat16", "\t$Rd, $sat_imm, $Rn", []>,
                Requires<[IsARM,HasV6]>{
  bits<4> Rd;
  bits<4> sat_imm;
  bits<4> Rn;
  let Inst{27-20} = 0b01101010;
  let Inst{11-4} = 0b11110011;
  let Inst{15-12} = Rd;
  let Inst{19-16} = sat_imm;
  let Inst{3-0} = Rn;
}

def USAT : AI<(outs GPRnopc:$Rd),
              (ins imm0_31:$sat_imm, GPRnopc:$Rn, shift_imm:$sh),
              SatFrm, NoItinerary, "usat", "\t$Rd, $sat_imm, $Rn$sh", []>,
              Requires<[IsARM,HasV6]> {
  bits<4> Rd;
  bits<5> sat_imm;
  bits<4> Rn;
  bits<8> sh;
  let Inst{27-21} = 0b0110111;
  let Inst{5-4} = 0b01;
  let Inst{15-12} = Rd;
  let Inst{11-7} = sh{4-0};
  let Inst{6} = sh{5};
  let Inst{20-16} = sat_imm;
  let Inst{3-0} = Rn;
}

def USAT16 : AI<(outs GPRnopc:$Rd),
                (ins imm0_15:$sat_imm, GPRnopc:$Rn), SatFrm,
                NoItinerary, "usat16", "\t$Rd, $sat_imm, $Rn", []>,
                Requires<[IsARM,HasV6]>{
  bits<4> Rd;
  bits<4> sat_imm;
  bits<4> Rn;
  let Inst{27-20} = 0b01101110;
  let Inst{11-4} = 0b11110011;
  let Inst{15-12} = Rd;
  let Inst{19-16} = sat_imm;
  let Inst{3-0} = Rn;
}

def : ARMV6Pat<(int_arm_ssat GPRnopc:$a, imm1_32:$pos),
               (SSAT imm1_32:$pos, GPRnopc:$a, 0)>;
def : ARMV6Pat<(int_arm_usat GPRnopc:$a, imm0_31:$pos),
               (USAT imm0_31:$pos, GPRnopc:$a, 0)>;
def : ARMPat<(ARMssat GPRnopc:$Rn, imm0_31:$imm),
             (SSAT imm0_31:$imm, GPRnopc:$Rn, 0)>;
def : ARMPat<(ARMusat GPRnopc:$Rn, imm0_31:$imm),
             (USAT imm0_31:$imm, GPRnopc:$Rn, 0)>;
def : ARMV6Pat<(int_arm_ssat16 GPRnopc:$a, imm1_16:$pos),
               (SSAT16 imm1_16:$pos, GPRnopc:$a)>;
def : ARMV6Pat<(int_arm_usat16 GPRnopc:$a, imm0_15:$pos),
               (USAT16 imm0_15:$pos, GPRnopc:$a)>;
def : ARMV6Pat<(int_arm_ssat (shl GPRnopc:$a, imm0_31:$shft), imm1_32:$pos),
               (SSAT imm1_32:$pos, GPRnopc:$a, imm0_31:$shft)>;
def : ARMV6Pat<(int_arm_ssat (sra GPRnopc:$a, asr_imm:$shft), imm1_32:$pos),
               (SSAT imm1_32:$pos, GPRnopc:$a, asr_imm:$shft)>;
def : ARMV6Pat<(int_arm_usat (shl GPRnopc:$a, imm0_31:$shft), imm0_31:$pos),
               (USAT imm0_31:$pos, GPRnopc:$a, imm0_31:$shft)>;
def : ARMV6Pat<(int_arm_usat (sra GPRnopc:$a, asr_imm:$shft), imm0_31:$pos),
               (USAT imm0_31:$pos, GPRnopc:$a, asr_imm:$shft)>;
def : ARMPat<(ARMssat (shl GPRnopc:$Rn, imm0_31:$shft), imm0_31:$pos),
               (SSAT imm0_31:$pos, GPRnopc:$Rn, imm0_31:$shft)>;                            
def : ARMPat<(ARMssat (sra GPRnopc:$Rn, asr_imm:$shft), imm0_31:$pos),
               (SSAT imm0_31:$pos, GPRnopc:$Rn, asr_imm:$shft)>;
def : ARMPat<(ARMusat (shl GPRnopc:$Rn, imm0_31:$shft), imm0_31:$pos),
               (USAT imm0_31:$pos, GPRnopc:$Rn, imm0_31:$shft)>;  
def : ARMPat<(ARMusat (sra GPRnopc:$Rn, asr_imm:$shft), imm0_31:$pos),
               (USAT imm0_31:$pos, GPRnopc:$Rn, asr_imm:$shft)>;


//===----------------------------------------------------------------------===//
//  Bitwise Instructions.
//

defm AND   : AsI1_bin_irs<0b0000, "and",
                          IIC_iBITi, IIC_iBITr, IIC_iBITsr, and, 1>;
defm ORR   : AsI1_bin_irs<0b1100, "orr",
                          IIC_iBITi, IIC_iBITr, IIC_iBITsr, or, 1>;
defm EOR   : AsI1_bin_irs<0b0001, "eor",
                          IIC_iBITi, IIC_iBITr, IIC_iBITsr, xor, 1>;
defm BIC   : AsI1_bin_irs<0b1110, "bic",
                          IIC_iBITi, IIC_iBITr, IIC_iBITsr,
                          BinOpFrag<(and node:$LHS, (not node:$RHS))>>;

// FIXME: bf_inv_mask_imm should be two operands, the lsb and the msb, just
// like in the actual instruction encoding. The complexity of mapping the mask
// to the lsb/msb pair should be handled by ISel, not encapsulated in the
// instruction description.
def BFC    : I<(outs GPR:$Rd), (ins GPR:$src, bf_inv_mask_imm:$imm),
               AddrMode1, 4, IndexModeNone, DPFrm, IIC_iUNAsi,
               "bfc", "\t$Rd, $imm", "$src = $Rd",
               [(set GPR:$Rd, (and GPR:$src, bf_inv_mask_imm:$imm))]>,
               Requires<[IsARM, HasV6T2]> {
  bits<4> Rd;
  bits<10> imm;
  let Inst{27-21} = 0b0111110;
  let Inst{6-0}   = 0b0011111;
  let Inst{15-12} = Rd;
  let Inst{11-7}  = imm{4-0}; // lsb
  let Inst{20-16} = imm{9-5}; // msb
}

// A8.6.18  BFI - Bitfield insert (Encoding A1)
def BFI:I<(outs GPRnopc:$Rd), (ins GPRnopc:$src, GPR:$Rn, bf_inv_mask_imm:$imm),
          AddrMode1, 4, IndexModeNone, DPFrm, IIC_iUNAsi,
          "bfi", "\t$Rd, $Rn, $imm", "$src = $Rd",
          [(set GPRnopc:$Rd, (ARMbfi GPRnopc:$src, GPR:$Rn,
                           bf_inv_mask_imm:$imm))]>,
          Requires<[IsARM, HasV6T2]> {
  bits<4> Rd;
  bits<4> Rn;
  bits<10> imm;
  let Inst{27-21} = 0b0111110;
  let Inst{6-4}   = 0b001; // Rn: Inst{3-0} != 15
  let Inst{15-12} = Rd;
  let Inst{11-7}  = imm{4-0}; // lsb
  let Inst{20-16} = imm{9-5}; // width
  let Inst{3-0}   = Rn;
}

def  MVNr  : AsI1<0b1111, (outs GPR:$Rd), (ins GPR:$Rm), DPFrm, IIC_iMVNr,
                  "mvn", "\t$Rd, $Rm",
                  [(set GPR:$Rd, (not GPR:$Rm))]>, UnaryDP, Sched<[WriteALU]> {
  bits<4> Rd;
  bits<4> Rm;
  let Inst{25} = 0;
  let Inst{19-16} = 0b0000;
  let Inst{11-4} = 0b00000000;
  let Inst{15-12} = Rd;
  let Inst{3-0} = Rm;

  let Unpredictable{19-16} = 0b1111;
}
def  MVNsi  : AsI1<0b1111, (outs GPR:$Rd), (ins so_reg_imm:$shift),
                  DPSoRegImmFrm, IIC_iMVNsr, "mvn", "\t$Rd, $shift",
                  [(set GPR:$Rd, (not so_reg_imm:$shift))]>, UnaryDP,
                  Sched<[WriteALU]> {
  bits<4> Rd;
  bits<12> shift;
  let Inst{25} = 0;
  let Inst{19-16} = 0b0000;
  let Inst{15-12} = Rd;
  let Inst{11-5} = shift{11-5};
  let Inst{4} = 0;
  let Inst{3-0} = shift{3-0};

  let Unpredictable{19-16} = 0b1111;
}
def  MVNsr  : AsI1<0b1111, (outs GPRnopc:$Rd), (ins so_reg_reg:$shift),
                  DPSoRegRegFrm, IIC_iMVNsr, "mvn", "\t$Rd, $shift",
                  [(set GPRnopc:$Rd, (not so_reg_reg:$shift))]>, UnaryDP,
                  Sched<[WriteALU]> {
  bits<4> Rd;
  bits<12> shift;
  let Inst{25} = 0;
  let Inst{19-16} = 0b0000;
  let Inst{15-12} = Rd;
  let Inst{11-8} = shift{11-8};
  let Inst{7} = 0;
  let Inst{6-5} = shift{6-5};
  let Inst{4} = 1;
  let Inst{3-0} = shift{3-0};

  let Unpredictable{19-16} = 0b1111;
}
let isReMaterializable = 1, isAsCheapAsAMove = 1, isMoveImm = 1 in
def  MVNi  : AsI1<0b1111, (outs GPR:$Rd), (ins mod_imm:$imm), DPFrm,
                  IIC_iMVNi, "mvn", "\t$Rd, $imm",
                  [(set GPR:$Rd, mod_imm_not:$imm)]>,UnaryDP, Sched<[WriteALU]> {
  bits<4> Rd;
  bits<12> imm;
  let Inst{25} = 1;
  let Inst{19-16} = 0b0000;
  let Inst{15-12} = Rd;
  let Inst{11-0} = imm;
}

let AddedComplexity = 1 in
def : ARMPat<(and   GPR:$src, mod_imm_not:$imm),
             (BICri GPR:$src, mod_imm_not:$imm)>;

//===----------------------------------------------------------------------===//
//  Multiply Instructions.
//
class AsMul1I32<bits<7> opcod, dag oops, dag iops, InstrItinClass itin,
             string opc, string asm, list<dag> pattern>
  : AsMul1I<opcod, oops, iops, itin, opc, asm, pattern> {
  bits<4> Rd;
  bits<4> Rm;
  bits<4> Rn;
  let Inst{19-16} = Rd;
  let Inst{11-8}  = Rm;
  let Inst{3-0}   = Rn;
}
class AsMul1I64<bits<7> opcod, dag oops, dag iops, InstrItinClass itin,
             string opc, string asm, list<dag> pattern>
  : AsMul1I<opcod, oops, iops, itin, opc, asm, pattern> {
  bits<4> RdLo;
  bits<4> RdHi;
  bits<4> Rm;
  bits<4> Rn;
  let Inst{19-16} = RdHi;
  let Inst{15-12} = RdLo;
  let Inst{11-8}  = Rm;
  let Inst{3-0}   = Rn;
}
class AsMla1I64<bits<7> opcod, dag oops, dag iops, InstrItinClass itin,
             string opc, string asm, list<dag> pattern>
  : AsMul1I<opcod, oops, iops, itin, opc, asm, pattern> {
  bits<4> RdLo;
  bits<4> RdHi;
  bits<4> Rm;
  bits<4> Rn;
  let Inst{19-16} = RdHi;
  let Inst{15-12} = RdLo;
  let Inst{11-8}  = Rm;
  let Inst{3-0}   = Rn;
}

// FIXME: The v5 pseudos are only necessary for the additional Constraint
//        property. Remove them when it's possible to add those properties
//        on an individual MachineInstr, not just an instruction description.
let isCommutable = 1, TwoOperandAliasConstraint = "$Rn = $Rd" in {
def MUL : AsMul1I32<0b0000000, (outs GPRnopc:$Rd),
                    (ins GPRnopc:$Rn, GPRnopc:$Rm),
                    IIC_iMUL32, "mul", "\t$Rd, $Rn, $Rm",
                  [(set GPRnopc:$Rd, (mul GPRnopc:$Rn, GPRnopc:$Rm))]>,
                  Requires<[IsARM, HasV6]>,
         Sched<[WriteMUL32, ReadMUL, ReadMUL]> {
  let Inst{15-12} = 0b0000;
  let Unpredictable{15-12} = 0b1111;
}

let Constraints = "@earlyclobber $Rd" in
def MULv5: ARMPseudoExpand<(outs GPRnopc:$Rd), (ins GPRnopc:$Rn, GPRnopc:$Rm,
                                                    pred:$p, cc_out:$s),
                           4, IIC_iMUL32,
               [(set GPRnopc:$Rd, (mul GPRnopc:$Rn, GPRnopc:$Rm))],
               (MUL GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, pred:$p, cc_out:$s)>,
               Requires<[IsARM, NoV6, UseMulOps]>,
           Sched<[WriteMUL32, ReadMUL, ReadMUL]>;
}

def MLA  : AsMul1I32<0b0000001, (outs GPRnopc:$Rd),
                     (ins GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$Ra),
                     IIC_iMAC32, "mla", "\t$Rd, $Rn, $Rm, $Ra",
        [(set GPRnopc:$Rd, (add (mul GPRnopc:$Rn, GPRnopc:$Rm), GPRnopc:$Ra))]>,
                     Requires<[IsARM, HasV6, UseMulOps]>,
        Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]> {
  bits<4> Ra;
  let Inst{15-12} = Ra;
}

let Constraints = "@earlyclobber $Rd" in
def MLAv5: ARMPseudoExpand<(outs GPRnopc:$Rd),
                           (ins GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$Ra,
                            pred:$p, cc_out:$s), 4, IIC_iMAC32,
         [(set GPRnopc:$Rd, (add (mul GPRnopc:$Rn, GPRnopc:$Rm), GPRnopc:$Ra))],
  (MLA GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$Ra, pred:$p, cc_out:$s)>,
                           Requires<[IsARM, NoV6]>,
           Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]>;

def MLS  : AMul1I<0b0000011, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm, GPR:$Ra),
                   IIC_iMAC32, "mls", "\t$Rd, $Rn, $Rm, $Ra",
                   [(set GPR:$Rd, (sub GPR:$Ra, (mul GPR:$Rn, GPR:$Rm)))]>,
                   Requires<[IsARM, HasV6T2, UseMulOps]>,
          Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]> {
  bits<4> Rd;
  bits<4> Rm;
  bits<4> Rn;
  bits<4> Ra;
  let Inst{19-16} = Rd;
  let Inst{15-12} = Ra;
  let Inst{11-8}  = Rm;
  let Inst{3-0}   = Rn;
}

// Extra precision multiplies with low / high results
let hasSideEffects = 0 in {
let isCommutable = 1 in {
def SMULL : AsMul1I64<0b0000110, (outs GPR:$RdLo, GPR:$RdHi),
                                 (ins GPR:$Rn, GPR:$Rm), IIC_iMUL64,
                    "smull", "\t$RdLo, $RdHi, $Rn, $Rm",
                    [(set GPR:$RdLo, GPR:$RdHi,
                          (smullohi GPR:$Rn, GPR:$Rm))]>,
                    Requires<[IsARM, HasV6]>,
           Sched<[WriteMUL64Lo, WriteMUL64Hi, ReadMUL, ReadMUL]>;

def UMULL : AsMul1I64<0b0000100, (outs GPR:$RdLo, GPR:$RdHi),
                                 (ins GPR:$Rn, GPR:$Rm), IIC_iMUL64,
                    "umull", "\t$RdLo, $RdHi, $Rn, $Rm",
                    [(set GPR:$RdLo, GPR:$RdHi,
                          (umullohi GPR:$Rn, GPR:$Rm))]>,
                    Requires<[IsARM, HasV6]>,
           Sched<[WriteMAC64Lo, WriteMAC64Hi, ReadMUL, ReadMUL]>;

let Constraints = "@earlyclobber $RdLo,@earlyclobber $RdHi" in {
def SMULLv5 : ARMPseudoExpand<(outs GPR:$RdLo, GPR:$RdHi),
                            (ins GPR:$Rn, GPR:$Rm, pred:$p, cc_out:$s),
                            4, IIC_iMUL64,
                            [(set GPR:$RdLo, GPR:$RdHi,
                                  (smullohi GPR:$Rn, GPR:$Rm))],
          (SMULL GPR:$RdLo, GPR:$RdHi, GPR:$Rn, GPR:$Rm, pred:$p, cc_out:$s)>,
                           Requires<[IsARM, NoV6]>,
              Sched<[WriteMUL64Lo, WriteMUL64Hi, ReadMUL, ReadMUL]>;

def UMULLv5 : ARMPseudoExpand<(outs GPR:$RdLo, GPR:$RdHi),
                            (ins GPR:$Rn, GPR:$Rm, pred:$p, cc_out:$s),
                            4, IIC_iMUL64,
                            [(set GPR:$RdLo, GPR:$RdHi,
                                  (umullohi GPR:$Rn, GPR:$Rm))],
          (UMULL GPR:$RdLo, GPR:$RdHi, GPR:$Rn, GPR:$Rm, pred:$p, cc_out:$s)>,
                           Requires<[IsARM, NoV6]>,
             Sched<[WriteMUL64Lo, WriteMUL64Hi, ReadMUL, ReadMUL]>;
}
}

// Multiply + accumulate
def SMLAL : AsMla1I64<0b0000111, (outs GPR:$RdLo, GPR:$RdHi),
                        (ins GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi), IIC_iMAC64,
                    "smlal", "\t$RdLo, $RdHi, $Rn, $Rm", []>,
         RegConstraint<"$RLo = $RdLo, $RHi = $RdHi">, Requires<[IsARM, HasV6]>,
           Sched<[WriteMAC64Lo, WriteMAC64Hi, ReadMUL, ReadMUL, ReadMAC, ReadMAC]>;
def UMLAL : AsMla1I64<0b0000101, (outs GPR:$RdLo, GPR:$RdHi),
                        (ins GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi), IIC_iMAC64,
                    "umlal", "\t$RdLo, $RdHi, $Rn, $Rm", []>,
         RegConstraint<"$RLo = $RdLo, $RHi = $RdHi">, Requires<[IsARM, HasV6]>,
            Sched<[WriteMAC64Lo, WriteMAC64Hi, ReadMUL, ReadMUL, ReadMAC, ReadMAC]>;

def UMAAL : AMul1I <0b0000010, (outs GPR:$RdLo, GPR:$RdHi),
                               (ins GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi),
                               IIC_iMAC64,
                    "umaal", "\t$RdLo, $RdHi, $Rn, $Rm", []>,
         RegConstraint<"$RLo = $RdLo, $RHi = $RdHi">, Requires<[IsARM, HasV6]>,
            Sched<[WriteMAC64Lo, WriteMAC64Hi, ReadMUL, ReadMUL, ReadMAC, ReadMAC]> {
  bits<4> RdLo;
  bits<4> RdHi;
  bits<4> Rm;
  bits<4> Rn;
  let Inst{19-16} = RdHi;
  let Inst{15-12} = RdLo;
  let Inst{11-8}  = Rm;
  let Inst{3-0}   = Rn;
}

let Constraints =
    "@earlyclobber $RdLo,@earlyclobber $RdHi,$RLo = $RdLo,$RHi = $RdHi" in {
def SMLALv5 : ARMPseudoExpand<(outs GPR:$RdLo, GPR:$RdHi),
                (ins GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi, pred:$p, cc_out:$s),
                              4, IIC_iMAC64, [],
             (SMLAL GPR:$RdLo, GPR:$RdHi, GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi,
                           pred:$p, cc_out:$s)>,
                           Requires<[IsARM, NoV6]>,
              Sched<[WriteMAC64Lo, WriteMAC64Hi, ReadMUL, ReadMUL, ReadMAC, ReadMAC]>;
def UMLALv5 : ARMPseudoExpand<(outs GPR:$RdLo, GPR:$RdHi),
                (ins GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi, pred:$p, cc_out:$s),
                              4, IIC_iMAC64, [],
             (UMLAL GPR:$RdLo, GPR:$RdHi, GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi,
                           pred:$p, cc_out:$s)>,
                           Requires<[IsARM, NoV6]>,
              Sched<[WriteMAC64Lo, WriteMAC64Hi, ReadMUL, ReadMUL, ReadMAC, ReadMAC]>;
}

} // hasSideEffects

// Most significant word multiply
def SMMUL : AMul2I <0b0111010, 0b0001, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
               IIC_iMUL32, "smmul", "\t$Rd, $Rn, $Rm",
               [(set GPR:$Rd, (mulhs GPR:$Rn, GPR:$Rm))]>,
            Requires<[IsARM, HasV6]>,
            Sched<[WriteMUL32, ReadMUL, ReadMUL]> {
  let Inst{15-12} = 0b1111;
}

def SMMULR : AMul2I <0b0111010, 0b0011, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
               IIC_iMUL32, "smmulr", "\t$Rd, $Rn, $Rm",
               [(set GPR:$Rd, (ARMsmmlar GPR:$Rn, GPR:$Rm, (i32 0)))]>,
            Requires<[IsARM, HasV6]>,
             Sched<[WriteMUL32, ReadMUL, ReadMUL]>  {
  let Inst{15-12} = 0b1111;
}

def SMMLA : AMul2Ia <0b0111010, 0b0001, (outs GPR:$Rd),
               (ins GPR:$Rn, GPR:$Rm, GPR:$Ra),
               IIC_iMAC32, "smmla", "\t$Rd, $Rn, $Rm, $Ra",
               [(set GPR:$Rd, (add (mulhs GPR:$Rn, GPR:$Rm), GPR:$Ra))]>,
            Requires<[IsARM, HasV6, UseMulOps]>,
            Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]>;

def SMMLAR : AMul2Ia <0b0111010, 0b0011, (outs GPR:$Rd),
               (ins GPR:$Rn, GPR:$Rm, GPR:$Ra),
               IIC_iMAC32, "smmlar", "\t$Rd, $Rn, $Rm, $Ra",
               [(set GPR:$Rd, (ARMsmmlar GPR:$Rn, GPR:$Rm, GPR:$Ra))]>,
            Requires<[IsARM, HasV6]>,
             Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]>;

def SMMLS : AMul2Ia <0b0111010, 0b1101, (outs GPR:$Rd),
               (ins GPR:$Rn, GPR:$Rm, GPR:$Ra),
               IIC_iMAC32, "smmls", "\t$Rd, $Rn, $Rm, $Ra", []>,
            Requires<[IsARM, HasV6, UseMulOps]>,
            Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]>;

def SMMLSR : AMul2Ia <0b0111010, 0b1111, (outs GPR:$Rd),
               (ins GPR:$Rn, GPR:$Rm, GPR:$Ra),
               IIC_iMAC32, "smmlsr", "\t$Rd, $Rn, $Rm, $Ra",
               [(set GPR:$Rd, (ARMsmmlsr GPR:$Rn, GPR:$Rm, GPR:$Ra))]>,
            Requires<[IsARM, HasV6]>,
             Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]>;

multiclass AI_smul<string opc> {
  def BB : AMulxyI<0b0001011, 0b00, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
              IIC_iMUL16, !strconcat(opc, "bb"), "\t$Rd, $Rn, $Rm",
              [(set GPR:$Rd, (bb_mul GPR:$Rn, GPR:$Rm))]>,
           Requires<[IsARM, HasV5TE]>,
           Sched<[WriteMUL16, ReadMUL, ReadMUL]>;

  def BT : AMulxyI<0b0001011, 0b10, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
              IIC_iMUL16, !strconcat(opc, "bt"), "\t$Rd, $Rn, $Rm",
              [(set GPR:$Rd, (bt_mul GPR:$Rn, GPR:$Rm))]>,
           Requires<[IsARM, HasV5TE]>,
           Sched<[WriteMUL16, ReadMUL, ReadMUL]>;

  def TB : AMulxyI<0b0001011, 0b01, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
              IIC_iMUL16, !strconcat(opc, "tb"), "\t$Rd, $Rn, $Rm",
              [(set GPR:$Rd, (tb_mul GPR:$Rn, GPR:$Rm))]>,
           Requires<[IsARM, HasV5TE]>,
           Sched<[WriteMUL16, ReadMUL, ReadMUL]>;

  def TT : AMulxyI<0b0001011, 0b11, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
              IIC_iMUL16, !strconcat(opc, "tt"), "\t$Rd, $Rn, $Rm",
              [(set GPR:$Rd, (tt_mul GPR:$Rn, GPR:$Rm))]>,
            Requires<[IsARM, HasV5TE]>,
           Sched<[WriteMUL16, ReadMUL, ReadMUL]>;

  def WB : AMulxyI<0b0001001, 0b01, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
              IIC_iMUL16, !strconcat(opc, "wb"), "\t$Rd, $Rn, $Rm",
              [(set GPR:$Rd, (ARMsmulwb GPR:$Rn, GPR:$Rm))]>,
           Requires<[IsARM, HasV5TE]>,
           Sched<[WriteMUL16, ReadMUL, ReadMUL]>;

  def WT : AMulxyI<0b0001001, 0b11, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm),
              IIC_iMUL16, !strconcat(opc, "wt"), "\t$Rd, $Rn, $Rm",
              [(set GPR:$Rd, (ARMsmulwt GPR:$Rn, GPR:$Rm))]>,
            Requires<[IsARM, HasV5TE]>,
           Sched<[WriteMUL16, ReadMUL, ReadMUL]>;
}


multiclass AI_smla<string opc> {
  let DecoderMethod = "DecodeSMLAInstruction" in {
  def BB : AMulxyIa<0b0001000, 0b00, (outs GPRnopc:$Rd),
              (ins GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
              IIC_iMAC16, !strconcat(opc, "bb"), "\t$Rd, $Rn, $Rm, $Ra",
              [(set GPRnopc:$Rd, (add GPR:$Ra,
                                      (bb_mul GPRnopc:$Rn, GPRnopc:$Rm)))]>,
           Requires<[IsARM, HasV5TE, UseMulOps]>,
           Sched<[WriteMAC16, ReadMUL, ReadMUL, ReadMAC]>;

  def BT : AMulxyIa<0b0001000, 0b10, (outs GPRnopc:$Rd),
              (ins GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
              IIC_iMAC16, !strconcat(opc, "bt"), "\t$Rd, $Rn, $Rm, $Ra",
              [(set GPRnopc:$Rd, (add GPR:$Ra,
                                      (bt_mul GPRnopc:$Rn, GPRnopc:$Rm)))]>,
           Requires<[IsARM, HasV5TE, UseMulOps]>,
           Sched<[WriteMAC16, ReadMUL, ReadMUL, ReadMAC]>;

  def TB : AMulxyIa<0b0001000, 0b01, (outs GPRnopc:$Rd),
              (ins GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
              IIC_iMAC16, !strconcat(opc, "tb"), "\t$Rd, $Rn, $Rm, $Ra",
              [(set GPRnopc:$Rd, (add GPR:$Ra,
                                      (tb_mul GPRnopc:$Rn, GPRnopc:$Rm)))]>,
           Requires<[IsARM, HasV5TE, UseMulOps]>,
           Sched<[WriteMAC16, ReadMUL, ReadMUL, ReadMAC]>;

  def TT : AMulxyIa<0b0001000, 0b11, (outs GPRnopc:$Rd),
              (ins GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
              IIC_iMAC16, !strconcat(opc, "tt"), "\t$Rd, $Rn, $Rm, $Ra",
             [(set GPRnopc:$Rd, (add GPR:$Ra,
                                     (tt_mul GPRnopc:$Rn, GPRnopc:$Rm)))]>,
            Requires<[IsARM, HasV5TE, UseMulOps]>,
            Sched<[WriteMAC16, ReadMUL, ReadMUL, ReadMAC]>;

  def WB : AMulxyIa<0b0001001, 0b00, (outs GPRnopc:$Rd),
              (ins GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
              IIC_iMAC16, !strconcat(opc, "wb"), "\t$Rd, $Rn, $Rm, $Ra",
              [(set GPRnopc:$Rd,
                    (add GPR:$Ra, (ARMsmulwb GPRnopc:$Rn, GPRnopc:$Rm)))]>,
           Requires<[IsARM, HasV5TE, UseMulOps]>,
           Sched<[WriteMAC16, ReadMUL, ReadMUL, ReadMAC]>;

  def WT : AMulxyIa<0b0001001, 0b10, (outs GPRnopc:$Rd),
              (ins GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
              IIC_iMAC16, !strconcat(opc, "wt"), "\t$Rd, $Rn, $Rm, $Ra",
              [(set GPRnopc:$Rd,
                    (add GPR:$Ra, (ARMsmulwt GPRnopc:$Rn, GPRnopc:$Rm)))]>,
            Requires<[IsARM, HasV5TE, UseMulOps]>,
            Sched<[WriteMAC16, ReadMUL, ReadMUL, ReadMAC]>;
  }
}

defm SMUL : AI_smul<"smul">;
defm SMLA : AI_smla<"smla">;

// Halfword multiply accumulate long: SMLAL<x><y>.
class SMLAL<bits<2> opc1, string asm>
 : AMulxyI64<0b0001010, opc1,
        (outs GPRnopc:$RdLo, GPRnopc:$RdHi),
        (ins GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi),
        IIC_iMAC64, asm, "\t$RdLo, $RdHi, $Rn, $Rm", []>,
        RegConstraint<"$RLo = $RdLo, $RHi = $RdHi">,
        Requires<[IsARM, HasV5TE]>,
        Sched<[WriteMAC64Lo, WriteMAC64Hi, ReadMUL, ReadMUL, ReadMAC, ReadMAC]>;

def SMLALBB : SMLAL<0b00, "smlalbb">;
def SMLALBT : SMLAL<0b10, "smlalbt">;
def SMLALTB : SMLAL<0b01, "smlaltb">;
def SMLALTT : SMLAL<0b11, "smlaltt">;

def : ARMV5TEPat<(ARMsmlalbb GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi),
                 (SMLALBB $Rn, $Rm, $RLo, $RHi)>;
def : ARMV5TEPat<(ARMsmlalbt GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi),
                 (SMLALBT $Rn, $Rm, $RLo, $RHi)>;
def : ARMV5TEPat<(ARMsmlaltb GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi),
                 (SMLALTB $Rn, $Rm, $RLo, $RHi)>;
def : ARMV5TEPat<(ARMsmlaltt GPR:$Rn, GPR:$Rm, GPR:$RLo, GPR:$RHi),
                 (SMLALTT $Rn, $Rm, $RLo, $RHi)>;

// Helper class for AI_smld.
class AMulDualIbase<bit long, bit sub, bit swap, dag oops, dag iops,
                    InstrItinClass itin, string opc, string asm>
  : AI<oops, iops, MulFrm, itin, opc, asm, []>,
       Requires<[IsARM, HasV6]> {
  bits<4> Rn;
  bits<4> Rm;
  let Inst{27-23} = 0b01110;
  let Inst{22}    = long;
  let Inst{21-20} = 0b00;
  let Inst{11-8}  = Rm;
  let Inst{7}     = 0;
  let Inst{6}     = sub;
  let Inst{5}     = swap;
  let Inst{4}     = 1;
  let Inst{3-0}   = Rn;
}
class AMulDualI<bit long, bit sub, bit swap, dag oops, dag iops,
                InstrItinClass itin, string opc, string asm>
  : AMulDualIbase<long, sub, swap, oops, iops, itin, opc, asm> {
  bits<4> Rd;
  let Inst{15-12} = 0b1111;
  let Inst{19-16} = Rd;
}
class AMulDualIa<bit long, bit sub, bit swap, dag oops, dag iops,
                InstrItinClass itin, string opc, string asm>
  : AMulDualIbase<long, sub, swap, oops, iops, itin, opc, asm> {
  bits<4> Ra;
  bits<4> Rd;
  let Inst{19-16} = Rd;
  let Inst{15-12} = Ra;
}
class AMulDualI64<bit long, bit sub, bit swap, dag oops, dag iops,
                  InstrItinClass itin, string opc, string asm>
  : AMulDualIbase<long, sub, swap, oops, iops, itin, opc, asm> {
  bits<4> RdLo;
  bits<4> RdHi;
  let Inst{19-16} = RdHi;
  let Inst{15-12} = RdLo;
}

multiclass AI_smld<bit sub, string opc> {

  def D : AMulDualIa<0, sub, 0, (outs GPRnopc:$Rd),
                  (ins GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
                  NoItinerary, !strconcat(opc, "d"), "\t$Rd, $Rn, $Rm, $Ra">,
          Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]>;

  def DX: AMulDualIa<0, sub, 1, (outs GPRnopc:$Rd),
                  (ins GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
                  NoItinerary, !strconcat(opc, "dx"), "\t$Rd, $Rn, $Rm, $Ra">,
          Sched<[WriteMAC32, ReadMUL, ReadMUL, ReadMAC]>;

  def LD: AMulDualI64<1, sub, 0, (outs GPRnopc:$RdLo, GPRnopc:$RdHi),
                  (ins GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi),
                  NoItinerary,
                  !strconcat(opc, "ld"), "\t$RdLo, $RdHi, $Rn, $Rm">,
                  RegConstraint<"$RLo = $RdLo, $RHi = $RdHi">,
          Sched<[WriteMAC64Lo, WriteMAC64Hi, ReadMUL, ReadMUL, ReadMAC, ReadMAC]>;

  def LDX : AMulDualI64<1, sub, 1, (outs GPRnopc:$RdLo, GPRnopc:$RdHi),
                  (ins GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi),
                  NoItinerary,
                  !strconcat(opc, "ldx"),"\t$RdLo, $RdHi, $Rn, $Rm">,
                  RegConstraint<"$RLo = $RdLo, $RHi = $RdHi">,
             Sched<[WriteMUL64Lo, WriteMUL64Hi, ReadMUL, ReadMUL]>;
}

defm SMLA : AI_smld<0, "smla">;
defm SMLS : AI_smld<1, "smls">;

def : ARMV6Pat<(int_arm_smlad GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
               (SMLAD GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$Ra)>;
def : ARMV6Pat<(int_arm_smladx GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
               (SMLADX GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$Ra)>;
def : ARMV6Pat<(int_arm_smlsd GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
               (SMLSD GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$Ra)>;
def : ARMV6Pat<(int_arm_smlsdx GPRnopc:$Rn, GPRnopc:$Rm, GPR:$Ra),
               (SMLSDX GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$Ra)>;
def : ARMV6Pat<(ARMSmlald GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi),
               (SMLALD GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi)>;
def : ARMV6Pat<(ARMSmlaldx GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi),
               (SMLALDX GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi)>;
def : ARMV6Pat<(ARMSmlsld GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi),
               (SMLSLD GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi)>;
def : ARMV6Pat<(ARMSmlsldx GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi),
               (SMLSLDX GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$RLo, GPRnopc:$RHi)>;

multiclass AI_sdml<bit sub, string opc> {

  def D:AMulDualI<0, sub, 0, (outs GPRnopc:$Rd), (ins GPRnopc:$Rn, GPRnopc:$Rm),
                  NoItinerary, !strconcat(opc, "d"), "\t$Rd, $Rn, $Rm">,
        Sched<[WriteMUL32, ReadMUL, ReadMUL]>;
  def DX:AMulDualI<0, sub, 1, (outs GPRnopc:$Rd),(ins GPRnopc:$Rn, GPRnopc:$Rm),
                  NoItinerary, !strconcat(opc, "dx"), "\t$Rd, $Rn, $Rm">,
         Sched<[WriteMUL32, ReadMUL, ReadMUL]>;
}

defm SMUA : AI_sdml<0, "smua">;
defm SMUS : AI_sdml<1, "smus">;

def : ARMV6Pat<(int_arm_smuad GPRnopc:$Rn, GPRnopc:$Rm),
               (SMUAD GPRnopc:$Rn, GPRnopc:$Rm)>;
def : ARMV6Pat<(int_arm_smuadx GPRnopc:$Rn, GPRnopc:$Rm),
               (SMUADX GPRnopc:$Rn, GPRnopc:$Rm)>;
def : ARMV6Pat<(int_arm_smusd GPRnopc:$Rn, GPRnopc:$Rm),
               (SMUSD GPRnopc:$Rn, GPRnopc:$Rm)>;
def : ARMV6Pat<(int_arm_smusdx GPRnopc:$Rn, GPRnopc:$Rm),
               (SMUSDX GPRnopc:$Rn, GPRnopc:$Rm)>;

//===----------------------------------------------------------------------===//
//  Division Instructions (ARMv7-A with virtualization extension)
//
def SDIV : ADivA1I<0b001, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm), IIC_iDIV,
                   "sdiv", "\t$Rd, $Rn, $Rm",
                   [(set GPR:$Rd, (sdiv GPR:$Rn, GPR:$Rm))]>,
           Requires<[IsARM, HasDivideInARM]>,
           Sched<[WriteDIV]>;

def UDIV : ADivA1I<0b011, (outs GPR:$Rd), (ins GPR:$Rn, GPR:$Rm), IIC_iDIV,
                   "udiv", "\t$Rd, $Rn, $Rm",
                   [(set GPR:$Rd, (udiv GPR:$Rn, GPR:$Rm))]>,
           Requires<[IsARM, HasDivideInARM]>,
           Sched<[WriteDIV]>;

//===----------------------------------------------------------------------===//
//  Misc. Arithmetic Instructions.
//

def CLZ  : AMiscA1I<0b00010110, 0b0001, (outs GPR:$Rd), (ins GPR:$Rm),
              IIC_iUNAr, "clz", "\t$Rd, $Rm",
              [(set GPR:$Rd, (ctlz GPR:$Rm))]>, Requires<[IsARM, HasV5T]>,
           Sched<[WriteALU]>;

def RBIT : AMiscA1I<0b01101111, 0b0011, (outs GPR:$Rd), (ins GPR:$Rm),
              IIC_iUNAr, "rbit", "\t$Rd, $Rm",
              [(set GPR:$Rd, (bitreverse GPR:$Rm))]>,
           Requires<[IsARM, HasV6T2]>,
           Sched<[WriteALU]>;

def REV  : AMiscA1I<0b01101011, 0b0011, (outs GPR:$Rd), (ins GPR:$Rm),
              IIC_iUNAr, "rev", "\t$Rd, $Rm",
              [(set GPR:$Rd, (bswap GPR:$Rm))]>, Requires<[IsARM, HasV6]>,
           Sched<[WriteALU]>;

let AddedComplexity = 5 in
def REV16 : AMiscA1I<0b01101011, 0b1011, (outs GPR:$Rd), (ins GPR:$Rm),
               IIC_iUNAr, "rev16", "\t$Rd, $Rm",
               [(set GPR:$Rd, (rotr (bswap GPR:$Rm), (i32 16)))]>,
               Requires<[IsARM, HasV6]>,
           Sched<[WriteALU]>;

def : ARMV6Pat<(srl (bswap (extloadi16 addrmode3:$addr)), (i32 16)),
              (REV16 (LDRH addrmode3:$addr))>;
def : ARMV6Pat<(truncstorei16 (srl (bswap GPR:$Rn), (i32 16)), addrmode3:$addr),
               (STRH (REV16 GPR:$Rn), addrmode3:$addr)>;
def : ARMV6Pat<(srl (bswap top16Zero:$Rn), (i32 16)),
               (REV16 GPR:$Rn)>;

let AddedComplexity = 5 in
def REVSH : AMiscA1I<0b01101111, 0b1011, (outs GPR:$Rd), (ins GPR:$Rm),
               IIC_iUNAr, "revsh", "\t$Rd, $Rm",
               [(set GPR:$Rd, (sra (bswap GPR:$Rm), (i32 16)))]>,
               Requires<[IsARM, HasV6]>,
           Sched<[WriteALU]>;

def : ARMV6Pat<(or (sra (shl GPR:$Rm, (i32 24)), (i32 16)),
                   (and (srl GPR:$Rm, (i32 8)), 0xFF)),
               (REVSH GPR:$Rm)>;

def PKHBT : APKHI<0b01101000, 0, (outs GPRnopc:$Rd),
                              (ins GPRnopc:$Rn, GPRnopc:$Rm, pkh_lsl_amt:$sh),
               IIC_iALUsi, "pkhbt", "\t$Rd, $Rn, $Rm$sh",
               [(set GPRnopc:$Rd, (or (and GPRnopc:$Rn, 0xFFFF),
                                      (and (shl GPRnopc:$Rm, pkh_lsl_amt:$sh),
                                           0xFFFF0000)))]>,
               Requires<[IsARM, HasV6]>,
           Sched<[WriteALUsi, ReadALU]>;

// Alternate cases for PKHBT where identities eliminate some nodes.
def : ARMV6Pat<(or (and GPRnopc:$Rn, 0xFFFF), (and GPRnopc:$Rm, 0xFFFF0000)),
               (PKHBT GPRnopc:$Rn, GPRnopc:$Rm, 0)>;
def : ARMV6Pat<(or (and GPRnopc:$Rn, 0xFFFF), (shl GPRnopc:$Rm, imm16_31:$sh)),
               (PKHBT GPRnopc:$Rn, GPRnopc:$Rm, imm16_31:$sh)>;

// Note: Shifts of 1-15 bits will be transformed to srl instead of sra and
// will match the pattern below.
def PKHTB : APKHI<0b01101000, 1, (outs GPRnopc:$Rd),
                              (ins GPRnopc:$Rn, GPRnopc:$Rm, pkh_asr_amt:$sh),
               IIC_iBITsi, "pkhtb", "\t$Rd, $Rn, $Rm$sh",
               [(set GPRnopc:$Rd, (or (and GPRnopc:$Rn, 0xFFFF0000),
                                      (and (sra GPRnopc:$Rm, pkh_asr_amt:$sh),
                                           0xFFFF)))]>,
               Requires<[IsARM, HasV6]>,
           Sched<[WriteALUsi, ReadALU]>;

// Alternate cases for PKHTB where identities eliminate some nodes.  Note that
// a shift amount of 0 is *not legal* here, it is PKHBT instead.
// We also can not replace a srl (17..31) by an arithmetic shift we would use in
// pkhtb src1, src2, asr (17..31).
def : ARMV6Pat<(or (and GPRnopc:$src1, 0xFFFF0000),
                   (srl GPRnopc:$src2, imm16:$sh)),
               (PKHTB GPRnopc:$src1, GPRnopc:$src2, imm16:$sh)>;
def : ARMV6Pat<(or (and GPRnopc:$src1, 0xFFFF0000),
                   (sra GPRnopc:$src2, imm16_31:$sh)),
               (PKHTB GPRnopc:$src1, GPRnopc:$src2, imm16_31:$sh)>;
def : ARMV6Pat<(or (and GPRnopc:$src1, 0xFFFF0000),
                   (and (srl GPRnopc:$src2, imm1_15:$sh), 0xFFFF)),
               (PKHTB GPRnopc:$src1, GPRnopc:$src2, imm1_15:$sh)>;

//===----------------------------------------------------------------------===//
// CRC Instructions
//
// Polynomials:
// + CRC32{B,H,W}       0x04C11DB7
// + CRC32C{B,H,W}      0x1EDC6F41
//

class AI_crc32<bit C, bits<2> sz, string suffix, SDPatternOperator builtin>
  : AInoP<(outs GPRnopc:$Rd), (ins GPRnopc:$Rn, GPRnopc:$Rm), MiscFrm, NoItinerary,
               !strconcat("crc32", suffix), "\t$Rd, $Rn, $Rm",
               [(set GPRnopc:$Rd, (builtin GPRnopc:$Rn, GPRnopc:$Rm))]>,
               Requires<[IsARM, HasCRC]> {
  bits<4> Rd;
  bits<4> Rn;
  bits<4> Rm;

  let Inst{31-28} = 0b1110;
  let Inst{27-23} = 0b00010;
  let Inst{22-21} = sz;
  let Inst{20}    = 0;
  let Inst{19-16} = Rn;
  let Inst{15-12} = Rd;
  let Inst{11-10} = 0b00;
  let Inst{9}     = C;
  let Inst{8}     = 0;
  let Inst{7-4}   = 0b0100;
  let Inst{3-0}   = Rm;

  let Unpredictable{11-8} = 0b1101;
}

def CRC32B  : AI_crc32<0, 0b00, "b", int_arm_crc32b>;
def CRC32CB : AI_crc32<1, 0b00, "cb", int_arm_crc32cb>;
def CRC32H  : AI_crc32<0, 0b01, "h", int_arm_crc32h>;
def CRC32CH : AI_crc32<1, 0b01, "ch", int_arm_crc32ch>;
def CRC32W  : AI_crc32<0, 0b10, "w", int_arm_crc32w>;
def CRC32CW : AI_crc32<1, 0b10, "cw", int_arm_crc32cw>;

//===----------------------------------------------------------------------===//
// ARMv8.1a Privilege Access Never extension
//
// SETPAN #imm1

def SETPAN : AInoP<(outs), (ins imm0_1:$imm), MiscFrm, NoItinerary, "setpan",
                "\t$imm", []>, Requires<[IsARM, HasV8, HasV8_1a]> {
  bits<1> imm;

  let Inst{31-28} = 0b1111;
  let Inst{27-20} = 0b00010001;
  let Inst{19-16} = 0b0000;
  let Inst{15-10} = 0b000000;
  let Inst{9} = imm;
  let Inst{8} = 0b0;
  let Inst{7-4} = 0b0000;
  let Inst{3-0} = 0b0000;

  let Unpredictable{19-16} = 0b1111;
  let Unpredictable{15-10} = 0b111111;
  let Unpredictable{8} = 0b1;
  let Unpredictable{3-0} = 0b1111;
}

//===----------------------------------------------------------------------===//
//  Comparison Instructions...
//

defm CMP  : AI1_cmp_irs<0b1010, "cmp",
                        IIC_iCMPi, IIC_iCMPr, IIC_iCMPsr, ARMcmp>;

// ARMcmpZ can re-use the above instruction definitions.
def : ARMPat<(ARMcmpZ GPR:$src, mod_imm:$imm),
             (CMPri   GPR:$src, mod_imm:$imm)>;
def : ARMPat<(ARMcmpZ GPR:$src, GPR:$rhs),
             (CMPrr   GPR:$src, GPR:$rhs)>;
def : ARMPat<(ARMcmpZ GPR:$src, so_reg_imm:$rhs),
             (CMPrsi   GPR:$src, so_reg_imm:$rhs)>;
def : ARMPat<(ARMcmpZ GPR:$src, so_reg_reg:$rhs),
             (CMPrsr   GPR:$src, so_reg_reg:$rhs)>;
// Following patterns aimed to prevent usage of CMPrsi and CMPrsr for a comparison
// with zero. Usage of CMPri in these cases helps to replace cmp with S-versions of
// shift instructions during peephole optimizations pass.
def : ARMPat<(ARMcmpZ so_reg_imm:$rhs, 0),
             (CMPri (MOVsi so_reg_imm:$rhs), 0)>;
def : ARMPat<(ARMcmpZ so_reg_reg:$rhs, 0),
             (CMPri (MOVsr so_reg_reg:$rhs), 0)>;

// CMN register-integer
let isCompare = 1, Defs = [CPSR] in {
def CMNri : AI1<0b1011, (outs), (ins GPR:$Rn, mod_imm:$imm), DPFrm, IIC_iCMPi,
                "cmn", "\t$Rn, $imm",
                [(ARMcmn GPR:$Rn, mod_imm:$imm)]>,
                Sched<[WriteCMP, ReadALU]> {
  bits<4> Rn;
  bits<12> imm;
  let Inst{25} = 1;
  let Inst{20} = 1;
  let Inst{19-16} = Rn;
  let Inst{15-12} = 0b0000;
  let Inst{11-0} = imm;

  let Unpredictable{15-12} = 0b1111;
}

// CMN register-register/shift
def CMNzrr : AI1<0b1011, (outs), (ins GPR:$Rn, GPR:$Rm), DPFrm, IIC_iCMPr,
                 "cmn", "\t$Rn, $Rm",
                 [(BinOpFrag<(ARMcmpZ node:$LHS,(ineg node:$RHS))>
                   GPR:$Rn, GPR:$Rm)]>, Sched<[WriteCMP, ReadALU, ReadALU]> {
  bits<4> Rn;
  bits<4> Rm;
  let isCommutable = 1;
  let Inst{25} = 0;
  let Inst{20} = 1;
  let Inst{19-16} = Rn;
  let Inst{15-12} = 0b0000;
  let Inst{11-4} = 0b00000000;
  let Inst{3-0} = Rm;

  let Unpredictable{15-12} = 0b1111;
}

def CMNzrsi : AI1<0b1011, (outs),
                  (ins GPR:$Rn, so_reg_imm:$shift), DPSoRegImmFrm, IIC_iCMPsr,
                  "cmn", "\t$Rn, $shift",
                  [(BinOpFrag<(ARMcmpZ node:$LHS,(ineg node:$RHS))>
                    GPR:$Rn, so_reg_imm:$shift)]>,
                    Sched<[WriteCMPsi, ReadALU]> {
  bits<4> Rn;
  bits<12> shift;
  let Inst{25} = 0;
  let Inst{20} = 1;
  let Inst{19-16} = Rn;
  let Inst{15-12} = 0b0000;
  let Inst{11-5} = shift{11-5};
  let Inst{4} = 0;
  let Inst{3-0} = shift{3-0};

  let Unpredictable{15-12} = 0b1111;
}

def CMNzrsr : AI1<0b1011, (outs),
                  (ins GPRnopc:$Rn, so_reg_reg:$shift), DPSoRegRegFrm, IIC_iCMPsr,
                  "cmn", "\t$Rn, $shift",
                  [(BinOpFrag<(ARMcmpZ node:$LHS,(ineg node:$RHS))>
                    GPRnopc:$Rn, so_reg_reg:$shift)]>,
                    Sched<[WriteCMPsr, ReadALU]> {
  bits<4> Rn;
  bits<12> shift;
  let Inst{25} = 0;
  let Inst{20} = 1;
  let Inst{19-16} = Rn;
  let Inst{15-12} = 0b0000;
  let Inst{11-8} = shift{11-8};
  let Inst{7} = 0;
  let Inst{6-5} = shift{6-5};
  let Inst{4} = 1;
  let Inst{3-0} = shift{3-0};

  let Unpredictable{15-12} = 0b1111;
}

}

def : ARMPat<(ARMcmp  GPR:$src, mod_imm_neg:$imm),
             (CMNri   GPR:$src, mod_imm_neg:$imm)>;

def : ARMPat<(ARMcmpZ GPR:$src, mod_imm_neg:$imm),
             (CMNri   GPR:$src, mod_imm_neg:$imm)>;

// Note that TST/TEQ don't set all the same flags that CMP does!
defm TST  : AI1_cmp_irs<0b1000, "tst",
                        IIC_iTSTi, IIC_iTSTr, IIC_iTSTsr,
                      BinOpFrag<(ARMcmpZ (and_su node:$LHS, node:$RHS), 0)>, 1,
                      "DecodeTSTInstruction">;
defm TEQ  : AI1_cmp_irs<0b1001, "teq",
                        IIC_iTSTi, IIC_iTSTr, IIC_iTSTsr,
                      BinOpFrag<(ARMcmpZ (xor_su node:$LHS, node:$RHS), 0)>, 1>;

// Pseudo i64 compares for some floating point compares.
let usesCustomInserter = 1, isBranch = 1, isTerminator = 1,
    Defs = [CPSR] in {
def BCCi64 : PseudoInst<(outs),
    (ins i32imm:$cc, GPR:$lhs1, GPR:$lhs2, GPR:$rhs1, GPR:$rhs2, brtarget:$dst),
     IIC_Br,
    [(ARMBcci64 imm:$cc, GPR:$lhs1, GPR:$lhs2, GPR:$rhs1, GPR:$rhs2, bb:$dst)]>,
    Sched<[WriteBr]>;

def BCCZi64 : PseudoInst<(outs),
     (ins i32imm:$cc, GPR:$lhs1, GPR:$lhs2, brtarget:$dst), IIC_Br,
    [(ARMBcci64 imm:$cc, GPR:$lhs1, GPR:$lhs2, 0, 0, bb:$dst)]>,
    Sched<[WriteBr]>;
} // usesCustomInserter


// Conditional moves
let hasSideEffects = 0 in {

let isCommutable = 1, isSelect = 1 in
def MOVCCr : ARMPseudoInst<(outs GPR:$Rd),
                           (ins GPR:$false, GPR:$Rm, cmovpred:$p),
                           4, IIC_iCMOVr,
                           [(set GPR:$Rd, (ARMcmov GPR:$false, GPR:$Rm,
                                                   cmovpred:$p))]>,
             RegConstraint<"$false = $Rd">, Sched<[WriteALU]>;

def MOVCCsi : ARMPseudoInst<(outs GPR:$Rd),
                            (ins GPR:$false, so_reg_imm:$shift, cmovpred:$p),
                            4, IIC_iCMOVsr,
                            [(set GPR:$Rd,
                                  (ARMcmov GPR:$false, so_reg_imm:$shift,
                                           cmovpred:$p))]>,
      RegConstraint<"$false = $Rd">, Sched<[WriteALU]>;
def MOVCCsr : ARMPseudoInst<(outs GPR:$Rd),
                            (ins GPR:$false, so_reg_reg:$shift, cmovpred:$p),
                           4, IIC_iCMOVsr,
  [(set GPR:$Rd, (ARMcmov GPR:$false, so_reg_reg:$shift,
                            cmovpred:$p))]>,
      RegConstraint<"$false = $Rd">, Sched<[WriteALU]>;


let isMoveImm = 1 in
def MOVCCi16
    : ARMPseudoInst<(outs GPR:$Rd),
                    (ins GPR:$false, imm0_65535_expr:$imm, cmovpred:$p),
                    4, IIC_iMOVi,
                    [(set GPR:$Rd, (ARMcmov GPR:$false, imm0_65535:$imm,
                                            cmovpred:$p))]>,
      RegConstraint<"$false = $Rd">, Requires<[IsARM, HasV6T2]>,
      Sched<[WriteALU]>;

let isMoveImm = 1 in
def MOVCCi : ARMPseudoInst<(outs GPR:$Rd),
                           (ins GPR:$false, mod_imm:$imm, cmovpred:$p),
                           4, IIC_iCMOVi,
                           [(set GPR:$Rd, (ARMcmov GPR:$false, mod_imm:$imm,
                                                   cmovpred:$p))]>,
      RegConstraint<"$false = $Rd">, Sched<[WriteALU]>;

// Two instruction predicate mov immediate.
let isMoveImm = 1 in
def MOVCCi32imm
    : ARMPseudoInst<(outs GPR:$Rd),
                    (ins GPR:$false, i32imm:$src, cmovpred:$p),
                    8, IIC_iCMOVix2,
                    [(set GPR:$Rd, (ARMcmov GPR:$false, imm:$src,
                                            cmovpred:$p))]>,
      RegConstraint<"$false = $Rd">, Requires<[IsARM, HasV6T2]>;

let isMoveImm = 1 in
def MVNCCi : ARMPseudoInst<(outs GPR:$Rd),
                           (ins GPR:$false, mod_imm:$imm, cmovpred:$p),
                           4, IIC_iCMOVi,
                           [(set GPR:$Rd, (ARMcmov GPR:$false, mod_imm_not:$imm,
                                                   cmovpred:$p))]>,
                RegConstraint<"$false = $Rd">, Sched<[WriteALU]>;

} // hasSideEffects


//===----------------------------------------------------------------------===//
// Atomic operations intrinsics
//

def MemBarrierOptOperand : AsmOperandClass {
  let Name = "MemBarrierOpt";
  let ParserMethod = "parseMemBarrierOptOperand";
}
def memb_opt : Operand<i32> {
  let PrintMethod = "printMemBOption";
  let ParserMatchClass = MemBarrierOptOperand;
  let DecoderMethod = "DecodeMemBarrierOption";
}

def InstSyncBarrierOptOperand : AsmOperandClass {
  let Name = "InstSyncBarrierOpt";
  let ParserMethod = "parseInstSyncBarrierOptOperand";
}
def instsyncb_opt : Operand<i32> {
  let PrintMethod = "printInstSyncBOption";
  let ParserMatchClass = InstSyncBarrierOptOperand;
  let DecoderMethod = "DecodeInstSyncBarrierOption";
}

def TraceSyncBarrierOptOperand : AsmOperandClass {
  let Name = "TraceSyncBarrierOpt";
  let ParserMethod = "parseTraceSyncBarrierOptOperand";
}
def tsb_opt : Operand<i32> {
  let PrintMethod = "printTraceSyncBOption";
  let ParserMatchClass = TraceSyncBarrierOptOperand;
}

// Memory barriers protect the atomic sequences
let hasSideEffects = 1 in {
def DMB : AInoP<(outs), (ins memb_opt:$opt), MiscFrm, NoItinerary,
                "dmb", "\t$opt", [(int_arm_dmb (i32 imm0_15:$opt))]>,
                Requires<[IsARM, HasDB]> {
  bits<4> opt;
  let Inst{31-4} = 0xf57ff05;
  let Inst{3-0} = opt;
}

def DSB : AInoP<(outs), (ins memb_opt:$opt), MiscFrm, NoItinerary,
                "dsb", "\t$opt", [(int_arm_dsb (i32 imm0_15:$opt))]>,
                Requires<[IsARM, HasDB]> {
  bits<4> opt;
  let Inst{31-4} = 0xf57ff04;
  let Inst{3-0} = opt;
}

// ISB has only full system option
def ISB : AInoP<(outs), (ins instsyncb_opt:$opt), MiscFrm, NoItinerary,
                "isb", "\t$opt", [(int_arm_isb (i32 imm0_15:$opt))]>,
                Requires<[IsARM, HasDB]> {
  bits<4> opt;
  let Inst{31-4} = 0xf57ff06;
  let Inst{3-0} = opt;
}

let hasNoSchedulingInfo = 1 in
def TSB : AInoP<(outs), (ins tsb_opt:$opt), MiscFrm, NoItinerary,
                "tsb", "\t$opt", []>, Requires<[IsARM, HasV8_4a]> {
  let Inst{31-0} = 0xe320f012;
  let DecoderMethod = "DecodeTSBInstruction";
}

}

// Armv8.5-A speculation barrier
def SB : AInoP<(outs), (ins), MiscFrm, NoItinerary, "sb", "", []>,
         Requires<[IsARM, HasSB]>, Sched<[]> {
  let Inst{31-0} = 0xf57ff070;
  let Unpredictable = 0x000fff0f;
  let hasSideEffects = 1;
}

let usesCustomInserter = 1, Defs = [CPSR], hasNoSchedulingInfo = 1 in {
  // Pseudo instruction that combines movs + predicated rsbmi
  // to implement integer ABS
  def ABS : ARMPseudoInst<(outs GPR:$dst), (ins GPR:$src), 8, NoItinerary, []>;
}

let usesCustomInserter = 1, Defs = [CPSR], hasNoSchedulingInfo = 1 in {
    def COPY_STRUCT_BYVAL_I32 : PseudoInst<
      (outs), (ins GPR:$dst, GPR:$src, i32imm:$size, i32imm:$alignment),
      NoItinerary,
      [(ARMcopystructbyval GPR:$dst, GPR:$src, imm:$size, imm:$alignment)]>;
}

let hasPostISelHook = 1, Constraints = "$newdst = $dst, $newsrc = $src" in {
    // %newsrc, %newdst = MEMCPY %dst, %src, N, ...N scratch regs...
    // Copies N registers worth of memory from address %src to address %dst
    // and returns the incremented addresses.  N scratch register will
    // be attached for the copy to use.
    def MEMCPY : PseudoInst<
      (outs GPR:$newdst, GPR:$newsrc),
      (ins GPR:$dst, GPR:$src, i32imm:$nreg, variable_ops),
      NoItinerary,
      [(set GPR:$newdst, GPR:$newsrc,
            (ARMmemcopy GPR:$dst, GPR:$src, imm:$nreg))]>;
}

def ldrex_1 : PatFrag<(ops node:$ptr), (int_arm_ldrex node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def ldrex_2 : PatFrag<(ops node:$ptr), (int_arm_ldrex node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def ldrex_4 : PatFrag<(ops node:$ptr), (int_arm_ldrex node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

def strex_1 : PatFrag<(ops node:$val, node:$ptr),
                      (int_arm_strex node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def strex_2 : PatFrag<(ops node:$val, node:$ptr),
                      (int_arm_strex node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def strex_4 : PatFrag<(ops node:$val, node:$ptr),
                      (int_arm_strex node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

def ldaex_1 : PatFrag<(ops node:$ptr), (int_arm_ldaex node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def ldaex_2 : PatFrag<(ops node:$ptr), (int_arm_ldaex node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def ldaex_4 : PatFrag<(ops node:$ptr), (int_arm_ldaex node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

def stlex_1 : PatFrag<(ops node:$val, node:$ptr),
                      (int_arm_stlex node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i8;
}]>;

def stlex_2 : PatFrag<(ops node:$val, node:$ptr),
                      (int_arm_stlex node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;

def stlex_4 : PatFrag<(ops node:$val, node:$ptr),
                      (int_arm_stlex node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

let mayLoad = 1 in {
def LDREXB : AIldrex<0b10, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                     NoItinerary, "ldrexb", "\t$Rt, $addr",
                     [(set GPR:$Rt, (ldrex_1 addr_offset_none:$addr))]>;
def LDREXH : AIldrex<0b11, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                     NoItinerary, "ldrexh", "\t$Rt, $addr",
                     [(set GPR:$Rt, (ldrex_2 addr_offset_none:$addr))]>;
def LDREX  : AIldrex<0b00, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                     NoItinerary, "ldrex", "\t$Rt, $addr",
                     [(set GPR:$Rt, (ldrex_4 addr_offset_none:$addr))]>;
let hasExtraDefRegAllocReq = 1 in
def LDREXD : AIldrex<0b01, (outs GPRPairOp:$Rt),(ins addr_offset_none:$addr),
                      NoItinerary, "ldrexd", "\t$Rt, $addr", []> {
  let DecoderMethod = "DecodeDoubleRegLoad";
}

def LDAEXB : AIldaex<0b10, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                     NoItinerary, "ldaexb", "\t$Rt, $addr",
                     [(set GPR:$Rt, (ldaex_1 addr_offset_none:$addr))]>;
def LDAEXH : AIldaex<0b11, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                     NoItinerary, "ldaexh", "\t$Rt, $addr",
                    [(set GPR:$Rt, (ldaex_2 addr_offset_none:$addr))]>;
def LDAEX  : AIldaex<0b00, (outs GPR:$Rt), (ins addr_offset_none:$addr),
                     NoItinerary, "ldaex", "\t$Rt, $addr",
                    [(set GPR:$Rt, (ldaex_4 addr_offset_none:$addr))]>;
let hasExtraDefRegAllocReq = 1 in
def LDAEXD : AIldaex<0b01, (outs GPRPairOp:$Rt),(ins addr_offset_none:$addr),
                      NoItinerary, "ldaexd", "\t$Rt, $addr", []> {
  let DecoderMethod = "DecodeDoubleRegLoad";
}
}

let mayStore = 1, Constraints = "@earlyclobber $Rd" in {
def STREXB: AIstrex<0b10, (outs GPR:$Rd), (ins GPR:$Rt, addr_offset_none:$addr),
                    NoItinerary, "strexb", "\t$Rd, $Rt, $addr",
                    [(set GPR:$Rd, (strex_1 GPR:$Rt,
                                            addr_offset_none:$addr))]>;
def STREXH: AIstrex<0b11, (outs GPR:$Rd), (ins GPR:$Rt, addr_offset_none:$addr),
                    NoItinerary, "strexh", "\t$Rd, $Rt, $addr",
                    [(set GPR:$Rd, (strex_2 GPR:$Rt,
                                            addr_offset_none:$addr))]>;
def STREX : AIstrex<0b00, (outs GPR:$Rd), (ins GPR:$Rt, addr_offset_none:$addr),
                    NoItinerary, "strex", "\t$Rd, $Rt, $addr",
                    [(set GPR:$Rd, (strex_4 GPR:$Rt,
                                            addr_offset_none:$addr))]>;
let hasExtraSrcRegAllocReq = 1 in
def STREXD : AIstrex<0b01, (outs GPR:$Rd),
                    (ins GPRPairOp:$Rt, addr_offset_none:$addr),
                    NoItinerary, "strexd", "\t$Rd, $Rt, $addr", []> {
  let DecoderMethod = "DecodeDoubleRegStore";
}
def STLEXB: AIstlex<0b10, (outs GPR:$Rd), (ins GPR:$Rt, addr_offset_none:$addr),
                    NoItinerary, "stlexb", "\t$Rd, $Rt, $addr",
                    [(set GPR:$Rd,
                          (stlex_1 GPR:$Rt, addr_offset_none:$addr))]>;
def STLEXH: AIstlex<0b11, (outs GPR:$Rd), (ins GPR:$Rt, addr_offset_none:$addr),
                    NoItinerary, "stlexh", "\t$Rd, $Rt, $addr",
                    [(set GPR:$Rd,
                          (stlex_2 GPR:$Rt, addr_offset_none:$addr))]>;
def STLEX : AIstlex<0b00, (outs GPR:$Rd), (ins GPR:$Rt, addr_offset_none:$addr),
                    NoItinerary, "stlex", "\t$Rd, $Rt, $addr",
                    [(set GPR:$Rd,
                          (stlex_4 GPR:$Rt, addr_offset_none:$addr))]>;
let hasExtraSrcRegAllocReq = 1 in
def STLEXD : AIstlex<0b01, (outs GPR:$Rd),
                    (ins GPRPairOp:$Rt, addr_offset_none:$addr),
                    NoItinerary, "stlexd", "\t$Rd, $Rt, $addr", []> {
  let DecoderMethod = "DecodeDoubleRegStore";
}
}

def CLREX : AXI<(outs), (ins), MiscFrm, NoItinerary, "clrex",
                [(int_arm_clrex)]>,
            Requires<[IsARM, HasV6K]>  {
  let Inst{31-0} = 0b11110101011111111111000000011111;
}

def : ARMPat<(strex_1 (and GPR:$Rt, 0xff), addr_offset_none:$addr),
             (STREXB GPR:$Rt, addr_offset_none:$addr)>;
def : ARMPat<(strex_2 (and GPR:$Rt, 0xffff), addr_offset_none:$addr),
             (STREXH GPR:$Rt, addr_offset_none:$addr)>;

def : ARMPat<(stlex_1 (and GPR:$Rt, 0xff), addr_offset_none:$addr),
             (STLEXB GPR:$Rt, addr_offset_none:$addr)>;
def : ARMPat<(stlex_2 (and GPR:$Rt, 0xffff), addr_offset_none:$addr),
             (STLEXH GPR:$Rt, addr_offset_none:$addr)>;

class acquiring_load<PatFrag base>
  : PatFrag<(ops node:$ptr), (base node:$ptr), [{
  AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getSuccessOrdering();
  return isAcquireOrStronger(Ordering);
}]>;

def atomic_load_acquire_8  : acquiring_load<atomic_load_8>;
def atomic_load_acquire_16 : acquiring_load<atomic_load_16>;
def atomic_load_acquire_32 : acquiring_load<atomic_load_32>;

class releasing_store<PatFrag base>
  : PatFrag<(ops node:$ptr, node:$val), (base node:$val, node:$ptr), [{
  AtomicOrdering Ordering = cast<AtomicSDNode>(N)->getSuccessOrdering();
  return isReleaseOrStronger(Ordering);
}]>;

def atomic_store_release_8  : releasing_store<atomic_store_8>;
def atomic_store_release_16 : releasing_store<atomic_store_16>;
def atomic_store_release_32 : releasing_store<atomic_store_32>;

let AddedComplexity = 8 in {
  def : ARMPat<(atomic_load_acquire_8 addr_offset_none:$addr),  (LDAB addr_offset_none:$addr)>;
  def : ARMPat<(atomic_load_acquire_16 addr_offset_none:$addr), (LDAH addr_offset_none:$addr)>;
  def : ARMPat<(atomic_load_acquire_32 addr_offset_none:$addr), (LDA  addr_offset_none:$addr)>;
  def : ARMPat<(atomic_store_release_8 addr_offset_none:$addr, GPR:$val),  (STLB GPR:$val, addr_offset_none:$addr)>;
  def : ARMPat<(atomic_store_release_16 addr_offset_none:$addr, GPR:$val), (STLH GPR:$val, addr_offset_none:$addr)>;
  def : ARMPat<(atomic_store_release_32 addr_offset_none:$addr, GPR:$val), (STL  GPR:$val, addr_offset_none:$addr)>;
}

// SWP/SWPB are deprecated in V6/V7 and optional in v7VE.
// FIXME Use InstAlias to generate LDREX/STREX pairs instead.
let mayLoad = 1, mayStore = 1 in {
def SWP : AIswp<0, (outs GPRnopc:$Rt),
                (ins GPRnopc:$Rt2, addr_offset_none:$addr), "swp", []>,
                Requires<[IsARM,PreV8]>;
def SWPB: AIswp<1, (outs GPRnopc:$Rt),
                (ins GPRnopc:$Rt2, addr_offset_none:$addr), "swpb", []>,
                Requires<[IsARM,PreV8]>;
}

//===----------------------------------------------------------------------===//
// Coprocessor Instructions.
//

def CDP : ABI<0b1110, (outs), (ins p_imm:$cop, imm0_15:$opc1,
            c_imm:$CRd, c_imm:$CRn, c_imm:$CRm, imm0_7:$opc2),
            NoItinerary, "cdp", "\t$cop, $opc1, $CRd, $CRn, $CRm, $opc2",
            [(int_arm_cdp timm:$cop, timm:$opc1, timm:$CRd, timm:$CRn,
                          timm:$CRm, timm:$opc2)]>,
            Requires<[IsARM,PreV8]> {
  bits<4> opc1;
  bits<4> CRn;
  bits<4> CRd;
  bits<4> cop;
  bits<3> opc2;
  bits<4> CRm;

  let Inst{3-0}   = CRm;
  let Inst{4}     = 0;
  let Inst{7-5}   = opc2;
  let Inst{11-8}  = cop;
  let Inst{15-12} = CRd;
  let Inst{19-16} = CRn;
  let Inst{23-20} = opc1;

  let DecoderNamespace = "CoProc";
}

def CDP2 : ABXI<0b1110, (outs), (ins p_imm:$cop, imm0_15:$opc1,
               c_imm:$CRd, c_imm:$CRn, c_imm:$CRm, imm0_7:$opc2),
               NoItinerary, "cdp2\t$cop, $opc1, $CRd, $CRn, $CRm, $opc2",
               [(int_arm_cdp2 timm:$cop, timm:$opc1, timm:$CRd, timm:$CRn,
                              timm:$CRm, timm:$opc2)]>,
               Requires<[IsARM,PreV8]> {
  let Inst{31-28} = 0b1111;
  bits<4> opc1;
  bits<4> CRn;
  bits<4> CRd;
  bits<4> cop;
  bits<3> opc2;
  bits<4> CRm;

  let Inst{3-0}   = CRm;
  let Inst{4}     = 0;
  let Inst{7-5}   = opc2;
  let Inst{11-8}  = cop;
  let Inst{15-12} = CRd;
  let Inst{19-16} = CRn;
  let Inst{23-20} = opc1;

  let DecoderNamespace = "CoProc";
}

class ACI<dag oops, dag iops, string opc, string asm,
            list<dag> pattern, IndexMode im = IndexModeNone,
            AddrMode am = AddrModeNone>
  : I<oops, iops, am, 4, im, BrFrm, NoItinerary,
      opc, asm, "", pattern> {
  let Inst{27-25} = 0b110;
}
class ACInoP<dag oops, dag iops, string opc, string asm,
          list<dag> pattern, IndexMode im = IndexModeNone,
          AddrMode am = AddrModeNone>
  : InoP<oops, iops, am, 4, im, BrFrm, NoItinerary,
         opc, asm, "", pattern> {
  let Inst{31-28} = 0b1111;
  let Inst{27-25} = 0b110;
}

let DecoderNamespace = "CoProc" in {
multiclass LdStCop<bit load, bit Dbit, string asm, list<dag> pattern> {
  def _OFFSET : ACI<(outs), (ins p_imm:$cop, c_imm:$CRd, addrmode5:$addr),
                    asm, "\t$cop, $CRd, $addr", pattern, IndexModeNone,
                    AddrMode5> {
    bits<13> addr;
    bits<4> cop;
    bits<4> CRd;
    let Inst{24} = 1; // P = 1
    let Inst{23} = addr{8};
    let Inst{22} = Dbit;
    let Inst{21} = 0; // W = 0
    let Inst{20} = load;
    let Inst{19-16} = addr{12-9};
    let Inst{15-12} = CRd;
    let Inst{11-8} = cop;
    let Inst{7-0} = addr{7-0};
    let DecoderMethod = "DecodeCopMemInstruction";
  }
  def _PRE : ACI<(outs), (ins p_imm:$cop, c_imm:$CRd, addrmode5_pre:$addr),
                 asm, "\t$cop, $CRd, $addr!", [], IndexModePre> {
    bits<13> addr;
    bits<4> cop;
    bits<4> CRd;
    let Inst{24} = 1; // P = 1
    let Inst{23} = addr{8};
    let Inst{22} = Dbit;
    let Inst{21} = 1; // W = 1
    let Inst{20} = load;
    let Inst{19-16} = addr{12-9};
    let Inst{15-12} = CRd;
    let Inst{11-8} = cop;
    let Inst{7-0} = addr{7-0};
    let DecoderMethod = "DecodeCopMemInstruction";
  }
  def _POST: ACI<(outs), (ins p_imm:$cop, c_imm:$CRd, addr_offset_none:$addr,
                              postidx_imm8s4:$offset),
                 asm, "\t$cop, $CRd, $addr, $offset", [], IndexModePost> {
    bits<9> offset;
    bits<4> addr;
    bits<4> cop;
    bits<4> CRd;
    let Inst{24} = 0; // P = 0
    let Inst{23} = offset{8};
    let Inst{22} = Dbit;
    let Inst{21} = 1; // W = 1
    let Inst{20} = load;
    let Inst{19-16} = addr;
    let Inst{15-12} = CRd;
    let Inst{11-8} = cop;
    let Inst{7-0} = offset{7-0};
    let DecoderMethod = "DecodeCopMemInstruction";
  }
  def _OPTION : ACI<(outs),
                    (ins p_imm:$cop, c_imm:$CRd, addr_offset_none:$addr,
                         coproc_option_imm:$option),
      asm, "\t$cop, $CRd, $addr, $option", []> {
    bits<8> option;
    bits<4> addr;
    bits<4> cop;
    bits<4> CRd;
    let Inst{24} = 0; // P = 0
    let Inst{23} = 1; // U = 1
    let Inst{22} = Dbit;
    let Inst{21} = 0; // W = 0
    let Inst{20} = load;
    let Inst{19-16} = addr;
    let Inst{15-12} = CRd;
    let Inst{11-8} = cop;
    let Inst{7-0} = option;
    let DecoderMethod = "DecodeCopMemInstruction";
  }
}
multiclass LdSt2Cop<bit load, bit Dbit, string asm, list<dag> pattern> {
  def _OFFSET : ACInoP<(outs), (ins p_imm:$cop, c_imm:$CRd, addrmode5:$addr),
                       asm, "\t$cop, $CRd, $addr", pattern, IndexModeNone,
                       AddrMode5> {
    bits<13> addr;
    bits<4> cop;
    bits<4> CRd;
    let Inst{24} = 1; // P = 1
    let Inst{23} = addr{8};
    let Inst{22} = Dbit;
    let Inst{21} = 0; // W = 0
    let Inst{20} = load;
    let Inst{19-16} = addr{12-9};
    let Inst{15-12} = CRd;
    let Inst{11-8} = cop;
    let Inst{7-0} = addr{7-0};
    let DecoderMethod = "DecodeCopMemInstruction";
  }
  def _PRE : ACInoP<(outs), (ins p_imm:$cop, c_imm:$CRd, addrmode5_pre:$addr),
                    asm, "\t$cop, $CRd, $addr!", [], IndexModePre> {
    bits<13> addr;
    bits<4> cop;
    bits<4> CRd;
    let Inst{24} = 1; // P = 1
    let Inst{23} = addr{8};
    let Inst{22} = Dbit;
    let Inst{21} = 1; // W = 1
    let Inst{20} = load;
    let Inst{19-16} = addr{12-9};
    let Inst{15-12} = CRd;
    let Inst{11-8} = cop;
    let Inst{7-0} = addr{7-0};
    let DecoderMethod = "DecodeCopMemInstruction";
  }
  def _POST: ACInoP<(outs), (ins p_imm:$cop, c_imm:$CRd, addr_offset_none:$addr,
                                 postidx_imm8s4:$offset),
                 asm, "\t$cop, $CRd, $addr, $offset", [], IndexModePost> {
    bits<9> offset;
    bits<4> addr;
    bits<4> cop;
    bits<4> CRd;
    let Inst{24} = 0; // P = 0
    let Inst{23} = offset{8};
    let Inst{22} = Dbit;
    let Inst{21} = 1; // W = 1
    let Inst{20} = load;
    let Inst{19-16} = addr;
    let Inst{15-12} = CRd;
    let Inst{11-8} = cop;
    let Inst{7-0} = offset{7-0};
    let DecoderMethod = "DecodeCopMemInstruction";
  }
  def _OPTION : ACInoP<(outs),
                       (ins p_imm:$cop, c_imm:$CRd, addr_offset_none:$addr,
                            coproc_option_imm:$option),
      asm, "\t$cop, $CRd, $addr, $option", []> {
    bits<8> option;
    bits<4> addr;
    bits<4> cop;
    bits<4> CRd;
    let Inst{24} = 0; // P = 0
    let Inst{23} = 1; // U = 1
    let Inst{22} = Dbit;
    let Inst{21} = 0; // W = 0
    let Inst{20} = load;
    let Inst{19-16} = addr;
    let Inst{15-12} = CRd;
    let Inst{11-8} = cop;
    let Inst{7-0} = option;
    let DecoderMethod = "DecodeCopMemInstruction";
  }
}

defm LDC   : LdStCop <1, 0, "ldc", [(int_arm_ldc timm:$cop, timm:$CRd, addrmode5:$addr)]>;
defm LDCL  : LdStCop <1, 1, "ldcl", [(int_arm_ldcl timm:$cop, timm:$CRd, addrmode5:$addr)]>;
defm LDC2  : LdSt2Cop<1, 0, "ldc2", [(int_arm_ldc2 timm:$cop, timm:$CRd, addrmode5:$addr)]>, Requires<[IsARM,PreV8]>;
defm LDC2L : LdSt2Cop<1, 1, "ldc2l", [(int_arm_ldc2l timm:$cop, timm:$CRd, addrmode5:$addr)]>, Requires<[IsARM,PreV8]>;

defm STC   : LdStCop <0, 0, "stc", [(int_arm_stc timm:$cop, timm:$CRd, addrmode5:$addr)]>;
defm STCL  : LdStCop <0, 1, "stcl", [(int_arm_stcl timm:$cop, timm:$CRd, addrmode5:$addr)]>;
defm STC2  : LdSt2Cop<0, 0, "stc2", [(int_arm_stc2 timm:$cop, timm:$CRd, addrmode5:$addr)]>, Requires<[IsARM,PreV8]>;
defm STC2L : LdSt2Cop<0, 1, "stc2l", [(int_arm_stc2l timm:$cop, timm:$CRd, addrmode5:$addr)]>, Requires<[IsARM,PreV8]>;

} // DecoderNamespace = "CoProc"

//===----------------------------------------------------------------------===//
// Move between coprocessor and ARM core register.
//

class MovRCopro<string opc, bit direction, dag oops, dag iops,
                list<dag> pattern>
  : ABI<0b1110, oops, iops, NoItinerary, opc,
        "\t$cop, $opc1, $Rt, $CRn, $CRm, $opc2", pattern> {
  let Inst{20} = direction;
  let Inst{4} = 1;

  bits<4> Rt;
  bits<4> cop;
  bits<3> opc1;
  bits<3> opc2;
  bits<4> CRm;
  bits<4> CRn;

  let Inst{15-12} = Rt;
  let Inst{11-8}  = cop;
  let Inst{23-21} = opc1;
  let Inst{7-5}   = opc2;
  let Inst{3-0}   = CRm;
  let Inst{19-16} = CRn;

  let DecoderNamespace = "CoProc";
}

def MCR : MovRCopro<"mcr", 0 /* from ARM core register to coprocessor */,
                    (outs),
                    (ins p_imm:$cop, imm0_7:$opc1, GPR:$Rt, c_imm:$CRn,
                         c_imm:$CRm, imm0_7:$opc2),
                    [(int_arm_mcr timm:$cop, timm:$opc1, GPR:$Rt, timm:$CRn,
                                  timm:$CRm, timm:$opc2)]>,
                    ComplexDeprecationPredicate<"MCR">;
def : ARMInstAlias<"mcr${p} $cop, $opc1, $Rt, $CRn, $CRm",
                   (MCR p_imm:$cop, imm0_7:$opc1, GPR:$Rt, c_imm:$CRn,
                        c_imm:$CRm, 0, pred:$p)>;
def MRC : MovRCopro<"mrc", 1 /* from coprocessor to ARM core register */,
                    (outs GPRwithAPSR:$Rt),
                    (ins p_imm:$cop, imm0_7:$opc1, c_imm:$CRn, c_imm:$CRm,
                         imm0_7:$opc2), []>,
                    ComplexDeprecationPredicate<"MRC">;
def : ARMInstAlias<"mrc${p} $cop, $opc1, $Rt, $CRn, $CRm",
                   (MRC GPRwithAPSR:$Rt, p_imm:$cop, imm0_7:$opc1, c_imm:$CRn,
                        c_imm:$CRm, 0, pred:$p)>;

def : ARMPat<(int_arm_mrc timm:$cop, timm:$opc1, timm:$CRn, timm:$CRm, timm:$opc2),
             (MRC p_imm:$cop, imm0_7:$opc1, c_imm:$CRn, c_imm:$CRm, imm0_7:$opc2)>;

class MovRCopro2<string opc, bit direction, dag oops, dag iops,
                 list<dag> pattern>
  : ABXI<0b1110, oops, iops, NoItinerary,
         !strconcat(opc, "\t$cop, $opc1, $Rt, $CRn, $CRm, $opc2"), pattern> {
  let Inst{31-24} = 0b11111110;
  let Inst{20} = direction;
  let Inst{4} = 1;

  bits<4> Rt;
  bits<4> cop;
  bits<3> opc1;
  bits<3> opc2;
  bits<4> CRm;
  bits<4> CRn;

  let Inst{15-12} = Rt;
  let Inst{11-8}  = cop;
  let Inst{23-21} = opc1;
  let Inst{7-5}   = opc2;
  let Inst{3-0}   = CRm;
  let Inst{19-16} = CRn;

  let DecoderNamespace = "CoProc";
}

def MCR2 : MovRCopro2<"mcr2", 0 /* from ARM core register to coprocessor */,
                      (outs),
                      (ins p_imm:$cop, imm0_7:$opc1, GPR:$Rt, c_imm:$CRn,
                           c_imm:$CRm, imm0_7:$opc2),
                      [(int_arm_mcr2 timm:$cop, timm:$opc1, GPR:$Rt, timm:$CRn,
                                     timm:$CRm, timm:$opc2)]>,
                      Requires<[IsARM,PreV8]>;
def : ARMInstAlias<"mcr2 $cop, $opc1, $Rt, $CRn, $CRm",
                   (MCR2 p_imm:$cop, imm0_7:$opc1, GPR:$Rt, c_imm:$CRn,
                         c_imm:$CRm, 0)>;
def MRC2 : MovRCopro2<"mrc2", 1 /* from coprocessor to ARM core register */,
                      (outs GPRwithAPSR:$Rt),
                      (ins p_imm:$cop, imm0_7:$opc1, c_imm:$CRn, c_imm:$CRm,
                           imm0_7:$opc2), []>,
                      Requires<[IsARM,PreV8]>;
def : ARMInstAlias<"mrc2 $cop, $opc1, $Rt, $CRn, $CRm",
                   (MRC2 GPRwithAPSR:$Rt, p_imm:$cop, imm0_7:$opc1, c_imm:$CRn,
                         c_imm:$CRm, 0)>;

def : ARMV5TPat<(int_arm_mrc2 timm:$cop, timm:$opc1, timm:$CRn,
                              timm:$CRm, timm:$opc2),
                (MRC2 p_imm:$cop, imm0_7:$opc1, c_imm:$CRn, c_imm:$CRm, imm0_7:$opc2)>;

class MovRRCopro<string opc, bit direction, dag oops, dag iops, list<dag>
                 pattern = []>
  : ABI<0b1100, oops, iops, NoItinerary, opc, "\t$cop, $opc1, $Rt, $Rt2, $CRm",
        pattern> {

  let Inst{23-21} = 0b010;
  let Inst{20} = direction;

  bits<4> Rt;
  bits<4> Rt2;
  bits<4> cop;
  bits<4> opc1;
  bits<4> CRm;

  let Inst{15-12} = Rt;
  let Inst{19-16} = Rt2;
  let Inst{11-8}  = cop;
  let Inst{7-4}   = opc1;
  let Inst{3-0}   = CRm;
}

def MCRR : MovRRCopro<"mcrr", 0 /* from ARM core register to coprocessor */,
                      (outs), (ins p_imm:$cop, imm0_15:$opc1, GPRnopc:$Rt,
                      GPRnopc:$Rt2, c_imm:$CRm),
                      [(int_arm_mcrr timm:$cop, timm:$opc1, GPRnopc:$Rt,
                                     GPRnopc:$Rt2, timm:$CRm)]>;
def MRRC : MovRRCopro<"mrrc", 1 /* from coprocessor to ARM core register */,
                      (outs GPRnopc:$Rt, GPRnopc:$Rt2),
                      (ins p_imm:$cop, imm0_15:$opc1, c_imm:$CRm), []>;

class MovRRCopro2<string opc, bit direction, dag oops, dag iops,
                  list<dag> pattern = []>
  : ABXI<0b1100, oops, iops, NoItinerary,
         !strconcat(opc, "\t$cop, $opc1, $Rt, $Rt2, $CRm"), pattern>,
    Requires<[IsARM,PreV8]> {
  let Inst{31-28} = 0b1111;
  let Inst{23-21} = 0b010;
  let Inst{20} = direction;

  bits<4> Rt;
  bits<4> Rt2;
  bits<4> cop;
  bits<4> opc1;
  bits<4> CRm;

  let Inst{15-12} = Rt;
  let Inst{19-16} = Rt2;
  let Inst{11-8}  = cop;
  let Inst{7-4}   = opc1;
  let Inst{3-0}   = CRm;

  let DecoderMethod = "DecoderForMRRC2AndMCRR2";
}

def MCRR2 : MovRRCopro2<"mcrr2", 0 /* from ARM core register to coprocessor */,
                        (outs), (ins p_imm:$cop, imm0_15:$opc1, GPRnopc:$Rt,
                        GPRnopc:$Rt2, c_imm:$CRm),
                        [(int_arm_mcrr2 timm:$cop, timm:$opc1, GPRnopc:$Rt,
                                        GPRnopc:$Rt2, timm:$CRm)]>;

def MRRC2 : MovRRCopro2<"mrrc2", 1 /* from coprocessor to ARM core register */,
                       (outs GPRnopc:$Rt, GPRnopc:$Rt2),
                       (ins p_imm:$cop, imm0_15:$opc1, c_imm:$CRm), []>;

//===----------------------------------------------------------------------===//
// Move between special register and ARM core register
//

// Move to ARM core register from Special Register
def MRS : ABI<0b0001, (outs GPRnopc:$Rd), (ins), NoItinerary,
              "mrs", "\t$Rd, apsr", []> {
  bits<4> Rd;
  let Inst{23-16} = 0b00001111;
  let Unpredictable{19-17} = 0b111;

  let Inst{15-12} = Rd;

  let Inst{11-0} = 0b000000000000;
  let Unpredictable{11-0} = 0b110100001111;
}

def : InstAlias<"mrs${p} $Rd, cpsr", (MRS GPRnopc:$Rd, pred:$p), 0>,
         Requires<[IsARM]>;

// The MRSsys instruction is the MRS instruction from the ARM ARM,
// section B9.3.9, with the R bit set to 1.
def MRSsys : ABI<0b0001, (outs GPRnopc:$Rd), (ins), NoItinerary,
                 "mrs", "\t$Rd, spsr", []> {
  bits<4> Rd;
  let Inst{23-16} = 0b01001111;
  let Unpredictable{19-16} = 0b1111;

  let Inst{15-12} = Rd;

  let Inst{11-0} = 0b000000000000;
  let Unpredictable{11-0} = 0b110100001111;
}

// However, the MRS (banked register) system instruction (ARMv7VE) *does* have a
// separate encoding (distinguished by bit 5.
def MRSbanked : ABI<0b0001, (outs GPRnopc:$Rd), (ins banked_reg:$banked),
                    NoItinerary, "mrs", "\t$Rd, $banked", []>,
                Requires<[IsARM, HasVirtualization]> {
  bits<6> banked;
  bits<4> Rd;

  let Inst{23} = 0;
  let Inst{22} = banked{5}; // R bit
  let Inst{21-20} = 0b00;
  let Inst{19-16} = banked{3-0};
  let Inst{15-12} = Rd;
  let Inst{11-9} = 0b001;
  let Inst{8} = banked{4};
  let Inst{7-0} = 0b00000000;
}

// Move from ARM core register to Special Register
//
// No need to have both system and application versions of MSR (immediate) or
// MSR (register), the encodings are the same and the assembly parser has no way
// to distinguish between them. The mask operand contains the special register
// (R Bit) in bit 4 and bits 3-0 contains the mask with the fields to be
// accessed in the special register.
let Defs = [CPSR] in
def MSR : ABI<0b0001, (outs), (ins msr_mask:$mask, GPR:$Rn), NoItinerary,
              "msr", "\t$mask, $Rn", []> {
  bits<5> mask;
  bits<4> Rn;

  let Inst{23} = 0;
  let Inst{22} = mask{4}; // R bit
  let Inst{21-20} = 0b10;
  let Inst{19-16} = mask{3-0};
  let Inst{15-12} = 0b1111;
  let Inst{11-4} = 0b00000000;
  let Inst{3-0} = Rn;
}

let Defs = [CPSR] in
def MSRi : ABI<0b0011, (outs), (ins msr_mask:$mask,  mod_imm:$imm), NoItinerary,
               "msr", "\t$mask, $imm", []> {
  bits<5> mask;
  bits<12> imm;

  let Inst{23} = 0;
  let Inst{22} = mask{4}; // R bit
  let Inst{21-20} = 0b10;
  let Inst{19-16} = mask{3-0};
  let Inst{15-12} = 0b1111;
  let Inst{11-0} = imm;
}

// However, the MSR (banked register) system instruction (ARMv7VE) *does* have a
// separate encoding (distinguished by bit 5.
def MSRbanked : ABI<0b0001, (outs), (ins banked_reg:$banked, GPRnopc:$Rn),
                    NoItinerary, "msr", "\t$banked, $Rn", []>,
                Requires<[IsARM, HasVirtualization]> {
  bits<6> banked;
  bits<4> Rn;

  let Inst{23} = 0;
  let Inst{22} = banked{5}; // R bit
  let Inst{21-20} = 0b10;
  let Inst{19-16} = banked{3-0};
  let Inst{15-12} = 0b1111;
  let Inst{11-9} = 0b001;
  let Inst{8} = banked{4};
  let Inst{7-4} = 0b0000;
  let Inst{3-0} = Rn;
}

// Dynamic stack allocation yields a _chkstk for Windows targets.  These calls
// are needed to probe the stack when allocating more than
// 4k bytes in one go. Touching the stack at 4K increments is necessary to
// ensure that the guard pages used by the OS virtual memory manager are
// allocated in correct sequence.
// The main point of having separate instruction are extra unmodelled effects
// (compared to ordinary calls) like stack pointer change.

def win__chkstk : SDNode<"ARMISD::WIN__CHKSTK", SDTNone,
                      [SDNPHasChain, SDNPSideEffect]>;
let usesCustomInserter = 1, Uses = [R4], Defs = [R4, SP], hasNoSchedulingInfo = 1 in
  def WIN__CHKSTK : PseudoInst<(outs), (ins), NoItinerary, [(win__chkstk)]>;

def win__dbzchk : SDNode<"ARMISD::WIN__DBZCHK", SDT_WIN__DBZCHK,
                         [SDNPHasChain, SDNPSideEffect, SDNPOutGlue]>;
let usesCustomInserter = 1, Defs = [CPSR], hasNoSchedulingInfo = 1 in
  def WIN__DBZCHK : PseudoInst<(outs), (ins tGPR:$divisor), NoItinerary,
                               [(win__dbzchk tGPR:$divisor)]>;

//===----------------------------------------------------------------------===//
// TLS Instructions
//

// __aeabi_read_tp preserves the registers r1-r3.
// This is a pseudo inst so that we can get the encoding right,
// complete with fixup for the aeabi_read_tp function.
// TPsoft is valid for ARM mode only, in case of Thumb mode a tTPsoft pattern
// is defined in "ARMInstrThumb.td".
let isCall = 1,
  Defs = [R0, R12, LR, CPSR], Uses = [SP] in {
  def TPsoft : ARMPseudoInst<(outs), (ins), 4, IIC_Br,
               [(set R0, ARMthread_pointer)]>, Sched<[WriteBr]>,
               Requires<[IsARM, IsReadTPSoft]>;
}

// Reading thread pointer from coprocessor register
def : ARMPat<(ARMthread_pointer), (MRC 15, 0, 13, 0, 2)>,
      Requires<[IsARM, IsReadTPTPIDRURW]>;
def : ARMPat<(ARMthread_pointer), (MRC 15, 0, 13, 0, 3)>,
      Requires<[IsARM, IsReadTPTPIDRURO]>;
def : ARMPat<(ARMthread_pointer), (MRC 15, 0, 13, 0, 4)>,
      Requires<[IsARM, IsReadTPTPIDRPRW]>;

//===----------------------------------------------------------------------===//
// SJLJ Exception handling intrinsics
//   eh_sjlj_setjmp() is an instruction sequence to store the return
//   address and save #0 in R0 for the non-longjmp case.
//   Since by its nature we may be coming from some other function to get
//   here, and we're using the stack frame for the containing function to
//   save/restore registers, we can't keep anything live in regs across
//   the eh_sjlj_setjmp(), else it will almost certainly have been tromped upon
//   when we get here from a longjmp(). We force everything out of registers
//   except for our own input by listing the relevant registers in Defs. By
//   doing so, we also cause the prologue/epilogue code to actively preserve
//   all of the callee-saved registers, which is exactly what we want.
//   A constant value is passed in $val, and we use the location as a scratch.
//
// These are pseudo-instructions and are lowered to individual MC-insts, so
// no encoding information is necessary.
// This gets lowered to an instruction sequence of 20 bytes
let Defs =
  [ R0,  R1,  R2,  R3,  R4,  R5,  R6,  R7,  R8,  R9,  R10, R11, R12, LR, CPSR,
    Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7, Q8, Q9, Q10, Q11, Q12, Q13, Q14, Q15 ],
  hasSideEffects = 1, isBarrier = 1, usesCustomInserter = 1, Size = 20 in {
  def Int_eh_sjlj_setjmp : PseudoInst<(outs), (ins GPR:$src, GPR:$val),
                               NoItinerary,
                         [(set R0, (ARMeh_sjlj_setjmp GPR:$src, GPR:$val))]>,
                           Requires<[IsARM, HasVFP2]>;
}

// This gets lowered to an instruction sequence of 20 bytes
let Defs =
  [ R0,  R1,  R2,  R3,  R4,  R5,  R6,  R7,  R8,  R9,  R10, R11, R12, LR, CPSR ],
  hasSideEffects = 1, isBarrier = 1, usesCustomInserter = 1, Size = 20 in {
  def Int_eh_sjlj_setjmp_nofp : PseudoInst<(outs), (ins GPR:$src, GPR:$val),
                                   NoItinerary,
                         [(set R0, (ARMeh_sjlj_setjmp GPR:$src, GPR:$val))]>,
                                Requires<[IsARM, NoVFP]>;
}

// This gets lowered to an instruction sequence of 16 bytes
// FIXME: Non-IOS version(s)
let isBarrier = 1, hasSideEffects = 1, isTerminator = 1, Size = 16,
    Defs = [ R7, LR, SP ] in {
def Int_eh_sjlj_longjmp : PseudoInst<(outs), (ins GPR:$src, GPR:$scratch),
                             NoItinerary,
                         [(ARMeh_sjlj_longjmp GPR:$src, GPR:$scratch)]>,
                                Requires<[IsARM]>;
}

let isBarrier = 1, hasSideEffects = 1, usesCustomInserter = 1 in
def Int_eh_sjlj_setup_dispatch : PseudoInst<(outs), (ins), NoItinerary,
            [(ARMeh_sjlj_setup_dispatch)]>;

// eh.sjlj.dispatchsetup pseudo-instruction.
// This pseudo is used for both ARM and Thumb. Any differences are handled when
// the pseudo is expanded (which happens before any passes that need the
// instruction size).
let isBarrier = 1 in
def Int_eh_sjlj_dispatchsetup : PseudoInst<(outs), (ins), NoItinerary, []>;


//===----------------------------------------------------------------------===//
// Non-Instruction Patterns
//

// ARMv4 indirect branch using (MOVr PC, dst)
let isBranch = 1, isTerminator = 1, isBarrier = 1, isIndirectBranch = 1 in
  def MOVPCRX : ARMPseudoExpand<(outs), (ins GPR:$dst),
                    4, IIC_Br, [(brind GPR:$dst)],
                    (MOVr PC, GPR:$dst, (ops 14, zero_reg), zero_reg)>,
                  Requires<[IsARM, NoV4T]>, Sched<[WriteBr]>;

let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1, Uses = [SP] in
  def TAILJMPr4 : ARMPseudoExpand<(outs), (ins GPR:$dst),
                    4, IIC_Br, [],
                    (MOVr PC, GPR:$dst, (ops 14, zero_reg), zero_reg)>,
                  Requires<[IsARM, NoV4T]>, Sched<[WriteBr]>;

// Large immediate handling.

// 32-bit immediate using two piece mod_imms or movw + movt.
// This is a single pseudo instruction, the benefit is that it can be remat'd
// as a single unit instead of having to handle reg inputs.
// FIXME: Remove this when we can do generalized remat.
let isReMaterializable = 1, isMoveImm = 1, Size = 8 in
def MOVi32imm : PseudoInst<(outs GPR:$dst), (ins i32imm:$src), IIC_iMOVix2,
                           [(set GPR:$dst, (arm_i32imm:$src))]>,
                           Requires<[IsARM]>;

def LDRLIT_ga_abs : PseudoInst<(outs GPR:$dst), (ins i32imm:$src), IIC_iLoad_i,
                               [(set GPR:$dst, (ARMWrapper tglobaladdr:$src))]>,
                    Requires<[IsARM, DontUseMovt]>;

// Pseudo instruction that combines movw + movt + add pc (if PIC).
// It also makes it possible to rematerialize the instructions.
// FIXME: Remove this when we can do generalized remat and when machine licm
// can properly the instructions.
let isReMaterializable = 1 in {
def MOV_ga_pcrel : PseudoInst<(outs GPR:$dst), (ins i32imm:$addr),
                              IIC_iMOVix2addpc,
                        [(set GPR:$dst, (ARMWrapperPIC tglobaladdr:$addr))]>,
                        Requires<[IsARM, UseMovtInPic]>;

def LDRLIT_ga_pcrel : PseudoInst<(outs GPR:$dst), (ins i32imm:$addr),
                                 IIC_iLoadiALU,
                                 [(set GPR:$dst,
                                       (ARMWrapperPIC tglobaladdr:$addr))]>,
                      Requires<[IsARM, DontUseMovtInPic]>;

let AddedComplexity = 10 in
def LDRLIT_ga_pcrel_ldr : PseudoInst<(outs GPR:$dst), (ins i32imm:$addr),
                              NoItinerary,
                              [(set GPR:$dst,
                                    (load (ARMWrapperPIC tglobaladdr:$addr)))]>,
                          Requires<[IsARM, DontUseMovtInPic]>;

let AddedComplexity = 10 in
def MOV_ga_pcrel_ldr : PseudoInst<(outs GPR:$dst), (ins i32imm:$addr),
                                IIC_iMOVix2ld,
                    [(set GPR:$dst, (load (ARMWrapperPIC tglobaladdr:$addr)))]>,
                    Requires<[IsARM, UseMovtInPic]>;
} // isReMaterializable

// The many different faces of TLS access.
def : ARMPat<(ARMWrapper tglobaltlsaddr :$dst),
             (MOVi32imm tglobaltlsaddr :$dst)>,
      Requires<[IsARM, UseMovt]>;

def : Pat<(ARMWrapper tglobaltlsaddr:$src),
          (LDRLIT_ga_abs tglobaltlsaddr:$src)>,
      Requires<[IsARM, DontUseMovt]>;

def : Pat<(ARMWrapperPIC tglobaltlsaddr:$addr),
          (MOV_ga_pcrel tglobaltlsaddr:$addr)>, Requires<[IsARM, UseMovtInPic]>;

def : Pat<(ARMWrapperPIC tglobaltlsaddr:$addr),
          (LDRLIT_ga_pcrel tglobaltlsaddr:$addr)>,
      Requires<[IsARM, DontUseMovtInPic]>;
let AddedComplexity = 10 in
def : Pat<(load (ARMWrapperPIC tglobaltlsaddr:$addr)),
          (MOV_ga_pcrel_ldr tglobaltlsaddr:$addr)>,
      Requires<[IsARM, UseMovtInPic]>;


// ConstantPool, GlobalAddress, and JumpTable
def : ARMPat<(ARMWrapper  tconstpool  :$dst), (LEApcrel tconstpool  :$dst)>;
def : ARMPat<(ARMWrapper  tglobaladdr :$dst), (MOVi32imm tglobaladdr :$dst)>,
            Requires<[IsARM, UseMovt]>;
def : ARMPat<(ARMWrapper texternalsym :$dst), (MOVi32imm texternalsym :$dst)>,
            Requires<[IsARM, UseMovt]>;
def : ARMPat<(ARMWrapperJT tjumptable:$dst),
             (LEApcrelJT tjumptable:$dst)>;

// TODO: add,sub,and, 3-instr forms?

// Tail calls. These patterns also apply to Thumb mode.
// Regular indirect tail call
def : Pat<(ARMtcret tcGPR:$dst, (i32 timm:$SPDiff)),
          (TCRETURNri tcGPR:$dst, timm:$SPDiff)>,
          Requires<[NoSignRetAddr]>;
// Indirect tail call when PACBTI is enabled
def : Pat<(ARMtcret tcGPRnotr12:$dst, (i32 timm:$SPDiff)),
          (TCRETURNrinotr12 tcGPRnotr12:$dst, timm:$SPDiff)>,
          Requires<[SignRetAddr]>;
def : Pat<(ARMtcret (i32 tglobaladdr:$dst), (i32 timm:$SPDiff)),
          (TCRETURNdi texternalsym:$dst, (i32 timm:$SPDiff))>;
def : Pat<(ARMtcret (i32 texternalsym:$dst), (i32 timm:$SPDiff)),
          (TCRETURNdi texternalsym:$dst, i32imm:$SPDiff)>;

// Direct calls
def : ARMPat<(ARMcall texternalsym:$func), (BL texternalsym:$func)>;
def : ARMPat<(ARMcall_nolink texternalsym:$func),
             (BMOVPCB_CALL texternalsym:$func)>;

// zextload i1 -> zextload i8
def : ARMPat<(zextloadi1 addrmode_imm12:$addr), (LDRBi12 addrmode_imm12:$addr)>;
def : ARMPat<(zextloadi1 ldst_so_reg:$addr),    (LDRBrs ldst_so_reg:$addr)>;

// extload -> zextload
def : ARMPat<(extloadi1 addrmode_imm12:$addr),  (LDRBi12 addrmode_imm12:$addr)>;
def : ARMPat<(extloadi1 ldst_so_reg:$addr),     (LDRBrs ldst_so_reg:$addr)>;
def : ARMPat<(extloadi8 addrmode_imm12:$addr),  (LDRBi12 addrmode_imm12:$addr)>;
def : ARMPat<(extloadi8 ldst_so_reg:$addr),     (LDRBrs ldst_so_reg:$addr)>;

def : ARMPat<(extloadi16 addrmode3:$addr),  (LDRH addrmode3:$addr)>;

def : ARMPat<(extloadi8  addrmodepc:$addr), (PICLDRB addrmodepc:$addr)>;
def : ARMPat<(extloadi16 addrmodepc:$addr), (PICLDRH addrmodepc:$addr)>;

// smul* and smla*
def : ARMV5TEPat<(mul sext_16_node:$a, sext_16_node:$b),
                 (SMULBB GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(mul sext_16_node:$a, (sext_bottom_16 GPR:$b)),
                 (SMULBB GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(mul sext_16_node:$a, (sext_top_16 GPR:$b)),
                 (SMULBT GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(mul (sext_top_16 GPR:$a), sext_16_node:$b),
                 (SMULTB GPR:$a, GPR:$b)>;
def : ARMV5MOPat<(add GPR:$acc, (mul sext_16_node:$a, sext_16_node:$b)),
                 (SMLABB GPR:$a, GPR:$b, GPR:$acc)>;
def : ARMV5MOPat<(add GPR:$acc, (mul sext_16_node:$a, (sext_bottom_16 GPR:$b))),
                 (SMLABB GPR:$a, GPR:$b, GPR:$acc)>;
def : ARMV5MOPat<(add GPR:$acc, (mul sext_16_node:$a, (sext_top_16 GPR:$b))),
                 (SMLABT GPR:$a, GPR:$b, GPR:$acc)>;
def : ARMV5MOPat<(add GPR:$acc, (mul (sext_top_16 GPR:$a), sext_16_node:$b)),
                 (SMLATB GPR:$a, GPR:$b, GPR:$acc)>;

def : ARMV5TEPat<(int_arm_smulbb GPR:$a, GPR:$b),
                 (SMULBB GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(int_arm_smulbt GPR:$a, GPR:$b),
                 (SMULBT GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(int_arm_smultb GPR:$a, GPR:$b),
                 (SMULTB GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(int_arm_smultt GPR:$a, GPR:$b),
                 (SMULTT GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(int_arm_smulwb GPR:$a, GPR:$b),
                 (SMULWB GPR:$a, GPR:$b)>;
def : ARMV5TEPat<(int_arm_smulwt GPR:$a, GPR:$b),
                 (SMULWT GPR:$a, GPR:$b)>;

def : ARMV5TEPat<(int_arm_smlabb GPR:$a, GPR:$b, GPR:$acc),
                 (SMLABB GPR:$a, GPR:$b, GPR:$acc)>;
def : ARMV5TEPat<(int_arm_smlabt GPR:$a, GPR:$b, GPR:$acc),
                 (SMLABT GPR:$a, GPR:$b, GPR:$acc)>;
def : ARMV5TEPat<(int_arm_smlatb GPR:$a, GPR:$b, GPR:$acc),
                 (SMLATB GPR:$a, GPR:$b, GPR:$acc)>;
def : ARMV5TEPat<(int_arm_smlatt GPR:$a, GPR:$b, GPR:$acc),
                 (SMLATT GPR:$a, GPR:$b, GPR:$acc)>;
def : ARMV5TEPat<(int_arm_smlawb GPR:$a, GPR:$b, GPR:$acc),
                 (SMLAWB GPR:$a, GPR:$b, GPR:$acc)>;
def : ARMV5TEPat<(int_arm_smlawt GPR:$a, GPR:$b, GPR:$acc),
                 (SMLAWT GPR:$a, GPR:$b, GPR:$acc)>;

// Pre-v7 uses MCR for synchronization barriers.
def : ARMPat<(ARMMemBarrierMCR GPR:$zero), (MCR 15, 0, GPR:$zero, 7, 10, 5)>,
         Requires<[IsARM, HasV6]>;

// SXT/UXT with no rotate
let AddedComplexity = 16 in {
def : ARMV6Pat<(and GPR:$Src, 0x000000FF), (UXTB GPR:$Src, 0)>;
def : ARMV6Pat<(and GPR:$Src, 0x0000FFFF), (UXTH GPR:$Src, 0)>;
def : ARMV6Pat<(and GPR:$Src, 0x00FF00FF), (UXTB16 GPR:$Src, 0)>;
def : ARMV6Pat<(add GPR:$Rn, (and GPR:$Rm, 0x00FF)),
               (UXTAB GPR:$Rn, GPR:$Rm, 0)>;
def : ARMV6Pat<(add GPR:$Rn, (and GPR:$Rm, 0xFFFF)),
               (UXTAH GPR:$Rn, GPR:$Rm, 0)>;
}

def : ARMV6Pat<(sext_inreg GPR:$Src, i8),  (SXTB GPR:$Src, 0)>;
def : ARMV6Pat<(sext_inreg GPR:$Src, i16), (SXTH GPR:$Src, 0)>;

def : ARMV6Pat<(add GPR:$Rn, (sext_inreg GPRnopc:$Rm, i8)),
               (SXTAB GPR:$Rn, GPRnopc:$Rm, 0)>;
def : ARMV6Pat<(add GPR:$Rn, (sext_inreg GPRnopc:$Rm, i16)),
               (SXTAH GPR:$Rn, GPRnopc:$Rm, 0)>;

// Atomic load/store patterns
def : ARMPat<(atomic_load_8 ldst_so_reg:$src),
             (LDRBrs ldst_so_reg:$src)>;
def : ARMPat<(atomic_load_8 addrmode_imm12:$src),
             (LDRBi12 addrmode_imm12:$src)>;
def : ARMPat<(atomic_load_16 addrmode3:$src),
             (LDRH addrmode3:$src)>;
def : ARMPat<(atomic_load_32 ldst_so_reg:$src),
             (LDRrs ldst_so_reg:$src)>;
def : ARMPat<(atomic_load_32 addrmode_imm12:$src),
             (LDRi12 addrmode_imm12:$src)>;
def : ARMPat<(atomic_store_8 GPR:$val, ldst_so_reg:$ptr),
             (STRBrs GPR:$val, ldst_so_reg:$ptr)>;
def : ARMPat<(atomic_store_8 GPR:$val, addrmode_imm12:$ptr),
             (STRBi12 GPR:$val, addrmode_imm12:$ptr)>;
def : ARMPat<(atomic_store_16 GPR:$val, addrmode3:$ptr),
             (STRH GPR:$val, addrmode3:$ptr)>;
def : ARMPat<(atomic_store_32 GPR:$val, ldst_so_reg:$ptr),
             (STRrs GPR:$val, ldst_so_reg:$ptr)>;
def : ARMPat<(atomic_store_32 GPR:$val, addrmode_imm12:$ptr),
             (STRi12 GPR:$val, addrmode_imm12:$ptr)>;


//===----------------------------------------------------------------------===//
// Thumb Support
//

include "ARMInstrThumb.td"

//===----------------------------------------------------------------------===//
// Thumb2 Support
//

include "ARMInstrThumb2.td"

//===----------------------------------------------------------------------===//
// Floating Point Support
//

include "ARMInstrVFP.td"

//===----------------------------------------------------------------------===//
// Advanced SIMD (NEON) Support
//

include "ARMInstrNEON.td"

//===----------------------------------------------------------------------===//
// MVE Support
//

include "ARMInstrMVE.td"

//===----------------------------------------------------------------------===//
// CDE (Custom Datapath Extension)
//

include "ARMInstrCDE.td"

//===----------------------------------------------------------------------===//
// Assembler aliases
//

// Memory barriers
def : InstAlias<"dmb", (DMB 0xf), 0>, Requires<[IsARM, HasDB]>;
def : InstAlias<"dsb", (DSB 0xf), 0>, Requires<[IsARM, HasDB]>;
def : InstAlias<"ssbb", (DSB 0x0), 1>, Requires<[IsARM, HasDB]>;
def : InstAlias<"pssbb", (DSB 0x4), 1>, Requires<[IsARM, HasDB]>;
def : InstAlias<"isb", (ISB 0xf), 0>, Requires<[IsARM, HasDB]>;
// Armv8-R 'Data Full Barrier'
def : InstAlias<"dfb", (DSB 0xc), 1>, Requires<[IsARM, HasDFB]>;

// System instructions
def : MnemonicAlias<"swi", "svc">;

// Load / Store Multiple
def : MnemonicAlias<"ldmfd", "ldm">;
def : MnemonicAlias<"ldmia", "ldm">;
def : MnemonicAlias<"ldmea", "ldmdb">;
def : MnemonicAlias<"stmfd", "stmdb">;
def : MnemonicAlias<"stmia", "stm">;
def : MnemonicAlias<"stmea", "stm">;

// PKHBT/PKHTB with default shift amount. PKHTB is equivalent to PKHBT with the
// input operands swapped when the shift amount is zero (i.e., unspecified).
def : InstAlias<"pkhbt${p} $Rd, $Rn, $Rm",
                (PKHBT GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, 0, pred:$p), 0>,
        Requires<[IsARM, HasV6]>;
def : InstAlias<"pkhtb${p} $Rd, $Rn, $Rm",
                (PKHBT GPRnopc:$Rd, GPRnopc:$Rm, GPRnopc:$Rn, 0, pred:$p), 0>,
        Requires<[IsARM, HasV6]>;

// PUSH/POP aliases for STM/LDM
def : ARMInstAlias<"push${p} $regs", (STMDB_UPD SP, pred:$p, reglist:$regs)>;
def : ARMInstAlias<"pop${p} $regs", (LDMIA_UPD SP, pred:$p, reglist:$regs)>;

// SSAT/USAT optional shift operand.
def : ARMInstAlias<"ssat${p} $Rd, $sat_imm, $Rn",
                (SSAT GPRnopc:$Rd, imm1_32:$sat_imm, GPRnopc:$Rn, 0, pred:$p)>;
def : ARMInstAlias<"usat${p} $Rd, $sat_imm, $Rn",
                (USAT GPRnopc:$Rd, imm0_31:$sat_imm, GPRnopc:$Rn, 0, pred:$p)>;


// Extend instruction optional rotate operand.
def : ARMInstAlias<"sxtab${p} $Rd, $Rn, $Rm",
                (SXTAB GPRnopc:$Rd, GPR:$Rn, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"sxtah${p} $Rd, $Rn, $Rm",
                (SXTAH GPRnopc:$Rd, GPR:$Rn, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"sxtab16${p} $Rd, $Rn, $Rm",
                (SXTAB16 GPRnopc:$Rd, GPR:$Rn, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"sxtb${p} $Rd, $Rm",
                (SXTB GPRnopc:$Rd, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"sxtb16${p} $Rd, $Rm",
                (SXTB16 GPRnopc:$Rd, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"sxth${p} $Rd, $Rm",
                (SXTH GPRnopc:$Rd, GPRnopc:$Rm, 0, pred:$p)>;

def : ARMInstAlias<"uxtab${p} $Rd, $Rn, $Rm",
                (UXTAB GPRnopc:$Rd, GPR:$Rn, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"uxtah${p} $Rd, $Rn, $Rm",
                (UXTAH GPRnopc:$Rd, GPR:$Rn, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"uxtab16${p} $Rd, $Rn, $Rm",
                (UXTAB16 GPRnopc:$Rd, GPR:$Rn, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"uxtb${p} $Rd, $Rm",
                (UXTB GPRnopc:$Rd, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"uxtb16${p} $Rd, $Rm",
                (UXTB16 GPRnopc:$Rd, GPRnopc:$Rm, 0, pred:$p)>;
def : ARMInstAlias<"uxth${p} $Rd, $Rm",
                (UXTH GPRnopc:$Rd, GPRnopc:$Rm, 0, pred:$p)>;


// RFE aliases
def : MnemonicAlias<"rfefa", "rfeda">;
def : MnemonicAlias<"rfeea", "rfedb">;
def : MnemonicAlias<"rfefd", "rfeia">;
def : MnemonicAlias<"rfeed", "rfeib">;
def : MnemonicAlias<"rfe", "rfeia">;

// SRS aliases
def : MnemonicAlias<"srsfa", "srsib">;
def : MnemonicAlias<"srsea", "srsia">;
def : MnemonicAlias<"srsfd", "srsdb">;
def : MnemonicAlias<"srsed", "srsda">;
def : MnemonicAlias<"srs", "srsia">;

// QSAX == QSUBADDX
def : MnemonicAlias<"qsubaddx", "qsax">;
// SASX == SADDSUBX
def : MnemonicAlias<"saddsubx", "sasx">;
// SHASX == SHADDSUBX
def : MnemonicAlias<"shaddsubx", "shasx">;
// SHSAX == SHSUBADDX
def : MnemonicAlias<"shsubaddx", "shsax">;
// SSAX == SSUBADDX
def : MnemonicAlias<"ssubaddx", "ssax">;
// UASX == UADDSUBX
def : MnemonicAlias<"uaddsubx", "uasx">;
// UHASX == UHADDSUBX
def : MnemonicAlias<"uhaddsubx", "uhasx">;
// UHSAX == UHSUBADDX
def : MnemonicAlias<"uhsubaddx", "uhsax">;
// UQASX == UQADDSUBX
def : MnemonicAlias<"uqaddsubx", "uqasx">;
// UQSAX == UQSUBADDX
def : MnemonicAlias<"uqsubaddx", "uqsax">;
// USAX == USUBADDX
def : MnemonicAlias<"usubaddx", "usax">;

// "mov Rd, mod_imm_not" can be handled via "mvn" in assembly, just like
// for isel.
def : ARMInstSubst<"mov${s}${p} $Rd, $imm",
                   (MVNi rGPR:$Rd, mod_imm_not:$imm, pred:$p, cc_out:$s)>;
def : ARMInstSubst<"mvn${s}${p} $Rd, $imm",
                   (MOVi rGPR:$Rd, mod_imm_not:$imm, pred:$p, cc_out:$s)>;
// Same for AND <--> BIC
def : ARMInstSubst<"bic${s}${p} $Rd, $Rn, $imm",
                   (ANDri GPR:$Rd, GPR:$Rn, mod_imm_not:$imm,
                          pred:$p, cc_out:$s)>;
def : ARMInstSubst<"bic${s}${p} $Rdn, $imm",
                   (ANDri GPR:$Rdn, GPR:$Rdn, mod_imm_not:$imm,
                          pred:$p, cc_out:$s)>;
def : ARMInstSubst<"and${s}${p} $Rd, $Rn, $imm",
                   (BICri GPR:$Rd, GPR:$Rn, mod_imm_not:$imm,
                          pred:$p, cc_out:$s)>;
def : ARMInstSubst<"and${s}${p} $Rdn, $imm",
                   (BICri GPR:$Rdn, GPR:$Rdn, mod_imm_not:$imm,
                          pred:$p, cc_out:$s)>;

// Likewise, "add Rd, mod_imm_neg" -> sub
def : ARMInstSubst<"add${s}${p} $Rd, $Rn, $imm",
                 (SUBri GPR:$Rd, GPR:$Rn, mod_imm_neg:$imm, pred:$p, cc_out:$s)>;
def : ARMInstSubst<"add${s}${p} $Rd, $imm",
                 (SUBri GPR:$Rd, GPR:$Rd, mod_imm_neg:$imm, pred:$p, cc_out:$s)>;
// Likewise, "sub Rd, mod_imm_neg" -> add
def : ARMInstSubst<"sub${s}${p} $Rd, $Rn, $imm",
                 (ADDri GPR:$Rd, GPR:$Rn, mod_imm_neg:$imm, pred:$p, cc_out:$s)>;
def : ARMInstSubst<"sub${s}${p} $Rd, $imm",
                 (ADDri GPR:$Rd, GPR:$Rd, mod_imm_neg:$imm, pred:$p, cc_out:$s)>;


def : ARMInstSubst<"adc${s}${p} $Rd, $Rn, $imm",
                 (SBCri GPR:$Rd, GPR:$Rn, mod_imm_not:$imm, pred:$p, cc_out:$s)>;
def : ARMInstSubst<"adc${s}${p} $Rdn, $imm",
                 (SBCri GPR:$Rdn, GPR:$Rdn, mod_imm_not:$imm, pred:$p, cc_out:$s)>;
def : ARMInstSubst<"sbc${s}${p} $Rd, $Rn, $imm",
                 (ADCri GPR:$Rd, GPR:$Rn, mod_imm_not:$imm, pred:$p, cc_out:$s)>;
def : ARMInstSubst<"sbc${s}${p} $Rdn, $imm",
                 (ADCri GPR:$Rdn, GPR:$Rdn, mod_imm_not:$imm, pred:$p, cc_out:$s)>;

// Same for CMP <--> CMN via mod_imm_neg
def : ARMInstSubst<"cmp${p} $Rd, $imm",
                   (CMNri rGPR:$Rd, mod_imm_neg:$imm, pred:$p)>;
def : ARMInstSubst<"cmn${p} $Rd, $imm",
                   (CMPri rGPR:$Rd, mod_imm_neg:$imm, pred:$p)>;

// The shifter forms of the MOV instruction are aliased to the ASR, LSL,
// LSR, ROR, and RRX instructions.
// FIXME: We need C++ parser hooks to map the alias to the MOV
//        encoding. It seems we should be able to do that sort of thing
//        in tblgen, but it could get ugly.
let TwoOperandAliasConstraint = "$Rm = $Rd" in {
def ASRi : ARMAsmPseudo<"asr${s}${p} $Rd, $Rm, $imm",
                        (ins GPR:$Rd, GPR:$Rm, imm0_32:$imm, pred:$p,
                             cc_out:$s)>;
def LSRi : ARMAsmPseudo<"lsr${s}${p} $Rd, $Rm, $imm",
                        (ins GPR:$Rd, GPR:$Rm, imm0_32:$imm, pred:$p,
                             cc_out:$s)>;
def LSLi : ARMAsmPseudo<"lsl${s}${p} $Rd, $Rm, $imm",
                        (ins GPR:$Rd, GPR:$Rm, imm0_31:$imm, pred:$p,
                             cc_out:$s)>;
def RORi : ARMAsmPseudo<"ror${s}${p} $Rd, $Rm, $imm",
                        (ins GPR:$Rd, GPR:$Rm, imm0_31:$imm, pred:$p,
                             cc_out:$s)>;
}
def RRXi : ARMAsmPseudo<"rrx${s}${p} $Rd, $Rm",
                        (ins GPR:$Rd, GPR:$Rm, pred:$p, cc_out:$s)>;
let TwoOperandAliasConstraint = "$Rn = $Rd" in {
def ASRr : ARMAsmPseudo<"asr${s}${p} $Rd, $Rn, $Rm",
                        (ins GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, pred:$p,
                             cc_out:$s)>;
def LSRr : ARMAsmPseudo<"lsr${s}${p} $Rd, $Rn, $Rm",
                        (ins GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, pred:$p,
                             cc_out:$s)>;
def LSLr : ARMAsmPseudo<"lsl${s}${p} $Rd, $Rn, $Rm",
                        (ins GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, pred:$p,
                             cc_out:$s)>;
def RORr : ARMAsmPseudo<"ror${s}${p} $Rd, $Rn, $Rm",
                        (ins GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, pred:$p,
                             cc_out:$s)>;
}

// "neg" is and alias for "rsb rd, rn, #0"
def : ARMInstAlias<"neg${s}${p} $Rd, $Rm",
                   (RSBri GPR:$Rd, GPR:$Rm, 0, pred:$p, cc_out:$s)>;

// Pre-v6, 'mov r0, r0' was used as a NOP encoding.
def : InstAlias<"nop${p}", (MOVr R0, R0, pred:$p, zero_reg), 0>,
         Requires<[IsARM, NoV6]>;

// MUL/UMLAL/SMLAL/UMULL/SMULL are available on all arches, but
// the instruction definitions need difference constraints pre-v6.
// Use these aliases for the assembly parsing on pre-v6.
def : InstAlias<"mul${s}${p} $Rd, $Rn, $Rm",
            (MUL GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, pred:$p, cc_out:$s), 0>,
         Requires<[IsARM, NoV6]>;
def : InstAlias<"mla${s}${p} $Rd, $Rn, $Rm, $Ra",
            (MLA GPRnopc:$Rd, GPRnopc:$Rn, GPRnopc:$Rm, GPRnopc:$Ra,
             pred:$p, cc_out:$s), 0>,
         Requires<[IsARM, NoV6]>;
def : InstAlias<"smlal${s}${p} $RdLo, $RdHi, $Rn, $Rm",
            (SMLAL GPR:$RdLo, GPR:$RdHi, GPR:$Rn, GPR:$Rm, pred:$p, cc_out:$s), 0>,
         Requires<[IsARM, NoV6]>;
def : InstAlias<"umlal${s}${p} $RdLo, $RdHi, $Rn, $Rm",
            (UMLAL GPR:$RdLo, GPR:$RdHi, GPR:$Rn, GPR:$Rm, pred:$p, cc_out:$s), 0>,
         Requires<[IsARM, NoV6]>;
def : InstAlias<"smull${s}${p} $RdLo, $RdHi, $Rn, $Rm",
            (SMULL GPR:$RdLo, GPR:$RdHi, GPR:$Rn, GPR:$Rm, pred:$p, cc_out:$s), 0>,
         Requires<[IsARM, NoV6]>;
def : InstAlias<"umull${s}${p} $RdLo, $RdHi, $Rn, $Rm",
            (UMULL GPR:$RdLo, GPR:$RdHi, GPR:$Rn, GPR:$Rm, pred:$p, cc_out:$s), 0>,
         Requires<[IsARM, NoV6]>;

// 'it' blocks in ARM mode just validate the predicates. The IT itself
// is discarded.
def ITasm : ARMAsmPseudo<"it$mask $cc", (ins it_pred:$cc, it_mask:$mask)>;

let mayLoad = 1, mayStore =1, hasSideEffects = 1, hasNoSchedulingInfo = 1 in
def SPACE : PseudoInst<(outs GPR:$Rd), (ins i32imm:$size, GPR:$Rn),
                       NoItinerary,
                       [(set GPR:$Rd, (int_arm_space timm:$size, GPR:$Rn))]>;

// SpeculationBarrierEndBB must only be used after an unconditional control
// flow, i.e. after a terminator for which isBarrier is True.
let hasSideEffects = 1, isCodeGenOnly = 1, isTerminator = 1, isBarrier = 1 in {
  // This gets lowered to a pair of 4-byte instructions
  let Size = 8 in
  def SpeculationBarrierISBDSBEndBB
      : PseudoInst<(outs), (ins), NoItinerary, []>, Sched<[]>;
  // This gets lowered to a single 4-byte instructions
  let Size = 4 in
  def SpeculationBarrierSBEndBB
      : PseudoInst<(outs), (ins), NoItinerary, []>, Sched<[]>;
}

//===----------------------------------
// Atomic cmpxchg for -O0
//===----------------------------------

// The fast register allocator used during -O0 inserts spills to cover any VRegs
// live across basic block boundaries. When this happens between an LDXR and an
// STXR it can clear the exclusive monitor, causing all cmpxchg attempts to
// fail.

// Unfortunately, this means we have to have an alternative (expanded
// post-regalloc) path for -O0 compilations. Fortunately this path can be
// significantly more naive than the standard expansion: we conservatively
// assume seq_cst, strong cmpxchg and omit clrex on failure.

let Constraints = "@earlyclobber $Rd,@earlyclobber $temp",
    mayLoad = 1, mayStore = 1 in {
def CMP_SWAP_8 : PseudoInst<(outs GPR:$Rd, GPR:$temp),
                            (ins GPR:$addr, GPR:$desired, GPR:$new),
                            NoItinerary, []>, Sched<[]>;

def CMP_SWAP_16 : PseudoInst<(outs GPR:$Rd, GPR:$temp),
                             (ins GPR:$addr, GPR:$desired, GPR:$new),
                             NoItinerary, []>, Sched<[]>;

def CMP_SWAP_32 : PseudoInst<(outs GPR:$Rd, GPR:$temp),
                             (ins GPR:$addr, GPR:$desired, GPR:$new),
                             NoItinerary, []>, Sched<[]>;

def CMP_SWAP_64 : PseudoInst<(outs GPRPair:$Rd, GPR:$temp),
                             (ins GPR:$addr, GPRPair:$desired, GPRPair:$new),
                             NoItinerary, []>, Sched<[]>;
}

def : Pat<(atomic_fence (timm), 0), (MEMBARRIER)>;

//===----------------------------------------------------------------------===//
// Instructions used for emitting unwind opcodes on Windows.
//===----------------------------------------------------------------------===//
let isPseudo = 1 in {
  def SEH_StackAlloc : PseudoInst<(outs), (ins i32imm:$size, i32imm:$wide), NoItinerary, []>, Sched<[]>;
  def SEH_SaveRegs : PseudoInst<(outs), (ins i32imm:$mask, i32imm:$wide), NoItinerary, []>, Sched<[]>;
  let isTerminator = 1 in
  def SEH_SaveRegs_Ret : PseudoInst<(outs), (ins i32imm:$mask, i32imm:$wide), NoItinerary, []>, Sched<[]>;
  def SEH_SaveSP : PseudoInst<(outs), (ins i32imm:$reg), NoItinerary, []>, Sched<[]>;
  def SEH_SaveFRegs : PseudoInst<(outs), (ins i32imm:$first, i32imm:$last), NoItinerary, []>, Sched<[]>;
  let isTerminator = 1 in
  def SEH_SaveLR : PseudoInst<(outs), (ins i32imm:$offst), NoItinerary, []>, Sched<[]>;
  def SEH_Nop : PseudoInst<(outs), (ins i32imm:$wide), NoItinerary, []>, Sched<[]>;
  let isTerminator = 1 in
  def SEH_Nop_Ret : PseudoInst<(outs), (ins i32imm:$wide), NoItinerary, []>, Sched<[]>;
  def SEH_PrologEnd : PseudoInst<(outs), (ins), NoItinerary, []>, Sched<[]>;
  def SEH_EpilogStart : PseudoInst<(outs), (ins), NoItinerary, []>, Sched<[]>;
  let isTerminator = 1 in
  def SEH_EpilogEnd : PseudoInst<(outs), (ins), NoItinerary, []>, Sched<[]>;
}


//===----------------------------------------------------------------------===//
// Pseudo Instructions for use when early-clobber is defined and Greedy Register
// Allocation is used. This ensures the constraint is used properly.
//===----------------------------------------------------------------------===//
let isCodeGenOnly = 1, hasNoSchedulingInfo = 1 in {
  def PseudoARMInitUndefMQPR      :  PseudoInst<(outs MQPR:$vd), (ins), NoItinerary, []>;
  def PseudoARMInitUndefSPR      :  PseudoInst<(outs SPR:$sd), (ins), NoItinerary, []>;
  def PseudoARMInitUndefDPR_VFP2      :  PseudoInst<(outs DPR_VFP2:$dd), (ins), NoItinerary, []>;
  def PseudoARMInitUndefGPR     :  PseudoInst<(outs GPR:$rd), (ins), NoItinerary, []>;
}
