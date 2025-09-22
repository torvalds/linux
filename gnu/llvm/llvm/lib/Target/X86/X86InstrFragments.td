//===----------X86InstrFragments - X86 Pattern fragments. --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// X86-specific DAG node.
def SDTX86CmpTest : SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisInt<1>,
                                         SDTCisSameAs<1, 2>]>;
def SDTX86FCmp    : SDTypeProfile<1, 2, [SDTCisVT<0, i32>, SDTCisFP<1>,
                                         SDTCisSameAs<1, 2>]>;

def SDTX86Ccmp    : SDTypeProfile<1, 5,
                                  [SDTCisVT<3, i8>, SDTCisVT<4, i8>, SDTCisVT<5, i32>]>;

// RES = op PTR, PASSTHRU, COND, EFLAGS
def SDTX86Cload    : SDTypeProfile<1, 4,
                                  [SDTCisInt<0>, SDTCisPtrTy<1>, SDTCisSameAs<0, 2>,
                                   SDTCisVT<3, i8>, SDTCisVT<4, i32>]>;
// op VAL, PTR, COND, EFLAGS
def SDTX86Cstore    : SDTypeProfile<0, 4,
                                  [SDTCisInt<0>, SDTCisPtrTy<1>,
                                   SDTCisVT<2, i8>, SDTCisVT<3, i32>]>;

def SDTX86Cmov    : SDTypeProfile<1, 4,
                                  [SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>,
                                   SDTCisVT<3, i8>, SDTCisVT<4, i32>]>;

// Unary and binary operator instructions that set EFLAGS as a side-effect.
def SDTUnaryArithWithFlags : SDTypeProfile<2, 1,
                                           [SDTCisSameAs<0, 2>,
                                            SDTCisInt<0>, SDTCisVT<1, i32>]>;

def SDTBinaryArithWithFlags : SDTypeProfile<2, 2,
                                            [SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>, SDTCisVT<1, i32>]>;

// SDTBinaryArithWithFlagsInOut - RES1, EFLAGS = op LHS, RHS, EFLAGS
def SDTBinaryArithWithFlagsInOut : SDTypeProfile<2, 3,
                                            [SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>,
                                             SDTCisVT<1, i32>,
                                             SDTCisVT<4, i32>]>;
// RES1, RES2, FLAGS = op LHS, RHS
def SDT2ResultBinaryArithWithFlags : SDTypeProfile<3, 2,
                                            [SDTCisSameAs<0, 1>,
                                             SDTCisSameAs<0, 2>,
                                             SDTCisSameAs<0, 3>,
                                             SDTCisInt<0>, SDTCisVT<1, i32>]>;
def SDTX86BrCond  : SDTypeProfile<0, 3,
                                  [SDTCisVT<0, OtherVT>,
                                   SDTCisVT<1, i8>, SDTCisVT<2, i32>]>;

def SDTX86SetCC   : SDTypeProfile<1, 2,
                                  [SDTCisVT<0, i8>,
                                   SDTCisVT<1, i8>, SDTCisVT<2, i32>]>;
def SDTX86SetCC_C : SDTypeProfile<1, 2,
                                  [SDTCisInt<0>,
                                   SDTCisVT<1, i8>, SDTCisVT<2, i32>]>;

def SDTX86sahf : SDTypeProfile<1, 1, [SDTCisVT<0, i32>, SDTCisVT<1, i8>]>;

def SDTX86rdrand : SDTypeProfile<2, 0, [SDTCisInt<0>, SDTCisVT<1, i32>]>;

def SDTX86rdpkru : SDTypeProfile<1, 1, [SDTCisVT<0, i32>, SDTCisVT<1, i32>]>;
def SDTX86wrpkru : SDTypeProfile<0, 3, [SDTCisVT<0, i32>, SDTCisVT<1, i32>,
                                        SDTCisVT<2, i32>]>;

def SDTX86cas : SDTypeProfile<0, 3, [SDTCisPtrTy<0>, SDTCisInt<1>,
                                     SDTCisVT<2, i8>]>;
def SDTX86cas8pair : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;
def SDTX86cas16pair : SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisVT<1, i64>]>;

def SDTLockBinaryArithWithFlags : SDTypeProfile<1, 2, [SDTCisVT<0, i32>,
                                                       SDTCisPtrTy<1>,
                                                       SDTCisInt<2>]>;

def SDTLockUnaryArithWithFlags : SDTypeProfile<1, 1, [SDTCisVT<0, i32>,
                                                      SDTCisPtrTy<1>]>;

def SDTX86Ret     : SDTypeProfile<0, -1, [SDTCisVT<0, i32>]>;

def SDT_X86CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i32>,
                                          SDTCisVT<1, i32>]>;
def SDT_X86CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i32>,
                                        SDTCisVT<1, i32>]>;

def SDT_X86Call   : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;

def SDT_X86NtBrind : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;

def SDT_X86VASTART_SAVE_XMM_REGS : SDTypeProfile<0, -1, [SDTCisVT<0, i8>,
                                                         SDTCisPtrTy<1>]>;

def SDT_X86VAARG : SDTypeProfile<1, -1, [SDTCisPtrTy<0>,
                                         SDTCisPtrTy<1>,
                                         SDTCisVT<2, i32>,
                                         SDTCisVT<3, i8>,
                                         SDTCisVT<4, i32>]>;

def SDTX86RepStr  : SDTypeProfile<0, 1, [SDTCisVT<0, OtherVT>]>;

def SDTX86Void    : SDTypeProfile<0, 0, []>;

def SDTX86Wrapper : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>, SDTCisPtrTy<0>]>;

def SDT_X86TLSADDR : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_X86TLSBASEADDR : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_X86TLSCALL : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_X86DYN_ALLOCA : SDTypeProfile<0, 1, [SDTCisVT<0, iPTR>]>;

def SDT_X86SEG_ALLOCA : SDTypeProfile<1, 1, [SDTCisVT<0, iPTR>, SDTCisVT<1, iPTR>]>;

def SDT_X86PROBED_ALLOCA : SDTypeProfile<1, 1, [SDTCisVT<0, iPTR>, SDTCisVT<1, iPTR>]>;

def SDT_X86EHRET : SDTypeProfile<0, 1, [SDTCisInt<0>]>;

def SDT_X86TCRET : SDTypeProfile<0, 2, [SDTCisPtrTy<0>, SDTCisVT<1, i32>]>;

def SDT_X86ENQCMD : SDTypeProfile<1, 2, [SDTCisVT<0, i32>,
                                         SDTCisPtrTy<1>, SDTCisSameAs<1, 2>]>;

def SDT_X86AESENCDECKL : SDTypeProfile<2, 2, [SDTCisVT<0, v2i64>,
                                              SDTCisVT<1, i32>,
                                              SDTCisVT<2, v2i64>,
                                              SDTCisPtrTy<3>]>;

def SDTX86Cmpccxadd : SDTypeProfile<1, 4, [SDTCisSameAs<0, 2>,
                                           SDTCisPtrTy<1>, SDTCisSameAs<2, 3>,
                                           SDTCisVT<4, i8>]>;

def X86MFence : SDNode<"X86ISD::MFENCE", SDTNone, [SDNPHasChain]>;


def X86bsf     : SDNode<"X86ISD::BSF",      SDTUnaryArithWithFlags>;
def X86bsr     : SDNode<"X86ISD::BSR",      SDTUnaryArithWithFlags>;
def X86fshl    : SDNode<"X86ISD::FSHL",     SDTIntShiftDOp>;
def X86fshr    : SDNode<"X86ISD::FSHR",     SDTIntShiftDOp>;

def X86cmp     : SDNode<"X86ISD::CMP" ,     SDTX86CmpTest>;
def X86fcmp    : SDNode<"X86ISD::FCMP",     SDTX86FCmp>;
def X86strict_fcmp : SDNode<"X86ISD::STRICT_FCMP", SDTX86FCmp, [SDNPHasChain]>;
def X86strict_fcmps : SDNode<"X86ISD::STRICT_FCMPS", SDTX86FCmp, [SDNPHasChain]>;
def X86bt      : SDNode<"X86ISD::BT",       SDTX86CmpTest>;

def X86ccmp    : SDNode<"X86ISD::CCMP",     SDTX86Ccmp>;
def X86ctest   : SDNode<"X86ISD::CTEST",    SDTX86Ccmp>;

def X86cload    : SDNode<"X86ISD::CLOAD",   SDTX86Cload, [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def X86cstore   : SDNode<"X86ISD::CSTORE",  SDTX86Cstore, [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;

def X86cmov    : SDNode<"X86ISD::CMOV",     SDTX86Cmov>;
def X86brcond  : SDNode<"X86ISD::BRCOND",   SDTX86BrCond,
                        [SDNPHasChain]>;
def X86setcc   : SDNode<"X86ISD::SETCC",    SDTX86SetCC>;
def X86setcc_c : SDNode<"X86ISD::SETCC_CARRY", SDTX86SetCC_C>;

def X86rdrand  : SDNode<"X86ISD::RDRAND",   SDTX86rdrand,
                        [SDNPHasChain, SDNPSideEffect]>;

def X86rdseed  : SDNode<"X86ISD::RDSEED",   SDTX86rdrand,
                        [SDNPHasChain, SDNPSideEffect]>;

def X86rdpkru : SDNode<"X86ISD::RDPKRU",    SDTX86rdpkru,
                       [SDNPHasChain, SDNPSideEffect]>;
def X86wrpkru : SDNode<"X86ISD::WRPKRU",    SDTX86wrpkru,
                       [SDNPHasChain, SDNPSideEffect]>;

def X86cas : SDNode<"X86ISD::LCMPXCHG_DAG", SDTX86cas,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore,
                         SDNPMayLoad, SDNPMemOperand]>;
def X86cas8 : SDNode<"X86ISD::LCMPXCHG8_DAG", SDTX86cas8pair,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore,
                         SDNPMayLoad, SDNPMemOperand]>;
def X86cas16 : SDNode<"X86ISD::LCMPXCHG16_DAG", SDTX86cas16pair,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore,
                         SDNPMayLoad, SDNPMemOperand]>;

def X86retglue : SDNode<"X86ISD::RET_GLUE", SDTX86Ret,
                        [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def X86iret : SDNode<"X86ISD::IRET", SDTX86Ret,
                        [SDNPHasChain, SDNPOptInGlue]>;

def X86vastart_save_xmm_regs :
                 SDNode<"X86ISD::VASTART_SAVE_XMM_REGS",
                        SDT_X86VASTART_SAVE_XMM_REGS,
                        [SDNPHasChain, SDNPMayStore, SDNPMemOperand, SDNPVariadic]>;
def X86vaarg64 :
                 SDNode<"X86ISD::VAARG_64", SDT_X86VAARG,
                        [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                         SDNPMemOperand]>;
def X86vaargx32 :
                 SDNode<"X86ISD::VAARG_X32", SDT_X86VAARG,
                        [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                         SDNPMemOperand]>;
def X86callseq_start :
                 SDNode<"ISD::CALLSEQ_START", SDT_X86CallSeqStart,
                        [SDNPHasChain, SDNPOutGlue]>;
def X86callseq_end :
                 SDNode<"ISD::CALLSEQ_END",   SDT_X86CallSeqEnd,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86call    : SDNode<"X86ISD::CALL",     SDT_X86Call,
                        [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                         SDNPVariadic]>;

def X86call_rvmarker  : SDNode<"X86ISD::CALL_RVMARKER",     SDT_X86Call,
                        [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                         SDNPVariadic]>;


def X86NoTrackCall : SDNode<"X86ISD::NT_CALL", SDT_X86Call,
                            [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                             SDNPVariadic]>;
def X86NoTrackBrind : SDNode<"X86ISD::NT_BRIND", SDT_X86NtBrind,
                             [SDNPHasChain]>;

def X86rep_stos: SDNode<"X86ISD::REP_STOS", SDTX86RepStr,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore]>;
def X86rep_movs: SDNode<"X86ISD::REP_MOVS", SDTX86RepStr,
                        [SDNPHasChain, SDNPInGlue, SDNPOutGlue, SDNPMayStore,
                         SDNPMayLoad]>;

def X86Wrapper    : SDNode<"X86ISD::Wrapper",     SDTX86Wrapper>;
def X86WrapperRIP : SDNode<"X86ISD::WrapperRIP",  SDTX86Wrapper>;

def X86RecoverFrameAlloc : SDNode<"ISD::LOCAL_RECOVER",
                                  SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                                       SDTCisInt<1>]>>;

def X86tlsaddr : SDNode<"X86ISD::TLSADDR", SDT_X86TLSADDR,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86tlsbaseaddr : SDNode<"X86ISD::TLSBASEADDR", SDT_X86TLSBASEADDR,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86tlsdesc : SDNode<"X86ISD::TLSDESC", SDT_X86TLSADDR,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86ehret : SDNode<"X86ISD::EH_RETURN", SDT_X86EHRET,
                        [SDNPHasChain]>;

def X86eh_sjlj_setjmp  : SDNode<"X86ISD::EH_SJLJ_SETJMP",
                                SDTypeProfile<1, 1, [SDTCisInt<0>,
                                                     SDTCisPtrTy<1>]>,
                                [SDNPHasChain, SDNPSideEffect]>;
def X86eh_sjlj_longjmp : SDNode<"X86ISD::EH_SJLJ_LONGJMP",
                                SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>,
                                [SDNPHasChain, SDNPSideEffect]>;
def X86eh_sjlj_setup_dispatch : SDNode<"X86ISD::EH_SJLJ_SETUP_DISPATCH",
                                       SDTypeProfile<0, 0, []>,
                                       [SDNPHasChain, SDNPSideEffect]>;

def X86tcret : SDNode<"X86ISD::TC_RETURN", SDT_X86TCRET,
                        [SDNPHasChain,  SDNPOptInGlue, SDNPVariadic]>;

def X86add_flag  : SDNode<"X86ISD::ADD",  SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86sub_flag  : SDNode<"X86ISD::SUB",  SDTBinaryArithWithFlags>;
def X86smul_flag : SDNode<"X86ISD::SMUL", SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86umul_flag : SDNode<"X86ISD::UMUL", SDT2ResultBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86adc_flag  : SDNode<"X86ISD::ADC",  SDTBinaryArithWithFlagsInOut>;
def X86sbb_flag  : SDNode<"X86ISD::SBB",  SDTBinaryArithWithFlagsInOut>;

def X86or_flag   : SDNode<"X86ISD::OR",   SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86xor_flag  : SDNode<"X86ISD::XOR",  SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;
def X86and_flag  : SDNode<"X86ISD::AND",  SDTBinaryArithWithFlags,
                          [SDNPCommutative]>;

def X86lock_add  : SDNode<"X86ISD::LADD",  SDTLockBinaryArithWithFlags,
                          [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                           SDNPMemOperand]>;
def X86lock_sub  : SDNode<"X86ISD::LSUB",  SDTLockBinaryArithWithFlags,
                          [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                           SDNPMemOperand]>;
def X86lock_or  : SDNode<"X86ISD::LOR",  SDTLockBinaryArithWithFlags,
                         [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                          SDNPMemOperand]>;
def X86lock_xor  : SDNode<"X86ISD::LXOR",  SDTLockBinaryArithWithFlags,
                          [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                           SDNPMemOperand]>;
def X86lock_and  : SDNode<"X86ISD::LAND",  SDTLockBinaryArithWithFlags,
                          [SDNPHasChain, SDNPMayStore, SDNPMayLoad,
                           SDNPMemOperand]>;

def X86bextr  : SDNode<"X86ISD::BEXTR",  SDTIntBinOp>;
def X86bextri : SDNode<"X86ISD::BEXTRI", SDTIntBinOp>;

def X86bzhi   : SDNode<"X86ISD::BZHI",   SDTIntBinOp>;

def X86pdep   : SDNode<"X86ISD::PDEP",   SDTIntBinOp>;
def X86pext   : SDNode<"X86ISD::PEXT",   SDTIntBinOp>;

def X86mul_imm : SDNode<"X86ISD::MUL_IMM", SDTIntBinOp>;

def X86DynAlloca : SDNode<"X86ISD::DYN_ALLOCA", SDT_X86DYN_ALLOCA,
                          [SDNPHasChain, SDNPOutGlue]>;

def X86SegAlloca : SDNode<"X86ISD::SEG_ALLOCA", SDT_X86SEG_ALLOCA,
                          [SDNPHasChain]>;

def X86ProbedAlloca : SDNode<"X86ISD::PROBED_ALLOCA", SDT_X86PROBED_ALLOCA,
                          [SDNPHasChain]>;

def X86TLSCall : SDNode<"X86ISD::TLSCALL", SDT_X86TLSCALL,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def X86lwpins : SDNode<"X86ISD::LWPINS",
                       SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisInt<1>,
                                            SDTCisVT<2, i32>, SDTCisVT<3, i32>]>,
                       [SDNPHasChain, SDNPMayStore, SDNPMayLoad, SDNPSideEffect]>;

def X86umwait : SDNode<"X86ISD::UMWAIT",
                       SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisInt<1>,
                                            SDTCisVT<2, i32>, SDTCisVT<3, i32>]>,
                       [SDNPHasChain, SDNPSideEffect]>;

def X86tpause : SDNode<"X86ISD::TPAUSE",
                       SDTypeProfile<1, 3, [SDTCisVT<0, i32>, SDTCisInt<1>,
                                            SDTCisVT<2, i32>, SDTCisVT<3, i32>]>,
                       [SDNPHasChain, SDNPSideEffect]>;

def X86enqcmd : SDNode<"X86ISD::ENQCMD", SDT_X86ENQCMD,
                       [SDNPHasChain, SDNPSideEffect]>;
def X86enqcmds : SDNode<"X86ISD::ENQCMDS", SDT_X86ENQCMD,
                       [SDNPHasChain, SDNPSideEffect]>;
def X86testui : SDNode<"X86ISD::TESTUI",
                       SDTypeProfile<1, 0, [SDTCisVT<0, i32>]>,
                       [SDNPHasChain, SDNPSideEffect]>;

def X86aesenc128kl : SDNode<"X86ISD::AESENC128KL", SDT_X86AESENCDECKL,
                            [SDNPHasChain, SDNPMayLoad, SDNPSideEffect,
                             SDNPMemOperand]>;
def X86aesdec128kl : SDNode<"X86ISD::AESDEC128KL", SDT_X86AESENCDECKL,
                            [SDNPHasChain, SDNPMayLoad, SDNPSideEffect,
                             SDNPMemOperand]>;
def X86aesenc256kl : SDNode<"X86ISD::AESENC256KL", SDT_X86AESENCDECKL,
                            [SDNPHasChain, SDNPMayLoad, SDNPSideEffect,
                             SDNPMemOperand]>;
def X86aesdec256kl : SDNode<"X86ISD::AESDEC256KL", SDT_X86AESENCDECKL,
                            [SDNPHasChain, SDNPMayLoad, SDNPSideEffect,
                             SDNPMemOperand]>;

def X86cmpccxadd : SDNode<"X86ISD::CMPCCXADD", SDTX86Cmpccxadd,
                          [SDNPHasChain, SDNPMayLoad, SDNPMayStore,
                           SDNPMemOperand]>;

// Define X86-specific addressing mode.
def addr      : ComplexPattern<iPTR, 5, "selectAddr", [], [SDNPWantParent]>;
def lea32addr : ComplexPattern<i32, 5, "selectLEAAddr",
                               [add, sub, mul, X86mul_imm, shl, or, xor, frameindex],
                               []>;
// In 64-bit mode 32-bit LEAs can use RIP-relative addressing.
def lea64_32addr : ComplexPattern<i32, 5, "selectLEA64_32Addr",
                                  [add, sub, mul, X86mul_imm, shl, or, xor,
                                   frameindex, X86WrapperRIP],
                                  []>;

def tls32addr : ComplexPattern<i32, 5, "selectTLSADDRAddr",
                               [tglobaltlsaddr], []>;

def tls32baseaddr : ComplexPattern<i32, 5, "selectTLSADDRAddr",
                               [tglobaltlsaddr], []>;

def lea64addr : ComplexPattern<i64, 5, "selectLEAAddr",
                        [add, sub, mul, X86mul_imm, shl, or, xor, frameindex,
                         X86WrapperRIP], []>;

def tls64addr : ComplexPattern<i64, 5, "selectTLSADDRAddr",
                               [tglobaltlsaddr], []>;

def tls64baseaddr : ComplexPattern<i64, 5, "selectTLSADDRAddr",
                               [tglobaltlsaddr], []>;

def vectoraddr : ComplexPattern<iPTR, 5, "selectVectorAddr", [],[SDNPWantParent]>;

// A relocatable immediate is an operand that can be relocated by the linker to
// an immediate, such as a regular symbol in non-PIC code.
def relocImm : ComplexPattern<iAny, 1, "selectRelocImm",
                              [X86Wrapper], [], 0>;

// X86 specific condition code. These correspond to CondCode in
// X86InstrInfo.h. They must be kept in synch.
def X86_COND_O   : PatLeaf<(i8 0)>;
def X86_COND_NO  : PatLeaf<(i8 1)>;
def X86_COND_B   : PatLeaf<(i8 2)>;  // alt. COND_C
def X86_COND_AE  : PatLeaf<(i8 3)>;  // alt. COND_NC
def X86_COND_E   : PatLeaf<(i8 4)>;  // alt. COND_Z
def X86_COND_NE  : PatLeaf<(i8 5)>;  // alt. COND_NZ
def X86_COND_BE  : PatLeaf<(i8 6)>;  // alt. COND_NA
def X86_COND_A   : PatLeaf<(i8 7)>;  // alt. COND_NBE
def X86_COND_S   : PatLeaf<(i8 8)>;
def X86_COND_NS  : PatLeaf<(i8 9)>;
def X86_COND_P   : PatLeaf<(i8 10)>; // alt. COND_PE
def X86_COND_NP  : PatLeaf<(i8 11)>; // alt. COND_PO
def X86_COND_L   : PatLeaf<(i8 12)>; // alt. COND_NGE
def X86_COND_GE  : PatLeaf<(i8 13)>; // alt. COND_NL
def X86_COND_LE  : PatLeaf<(i8 14)>; // alt. COND_NG
def X86_COND_G   : PatLeaf<(i8 15)>; // alt. COND_NLE

def i16immSExt8  : ImmLeaf<i16, [{ return isInt<8>(Imm); }]>;
def i32immSExt8  : ImmLeaf<i32, [{ return isInt<8>(Imm); }]>;
def i64immSExt8  : ImmLeaf<i64, [{ return isInt<8>(Imm); }]>;
def i64immSExt32 : ImmLeaf<i64, [{ return isInt<32>(Imm); }]>;
def i64timmSExt32 : TImmLeaf<i64, [{ return isInt<32>(Imm); }]>;

def i16relocImmSExt8 : PatLeaf<(i16 relocImm), [{
  return isSExtAbsoluteSymbolRef(8, N);
}]>;
def i32relocImmSExt8 : PatLeaf<(i32 relocImm), [{
  return isSExtAbsoluteSymbolRef(8, N);
}]>;
def i64relocImmSExt8 : PatLeaf<(i64 relocImm), [{
  return isSExtAbsoluteSymbolRef(8, N);
}]>;
def i64relocImmSExt32 : PatLeaf<(i64 relocImm), [{
  return isSExtAbsoluteSymbolRef(32, N);
}]>;

// If we have multiple users of an immediate, it's much smaller to reuse
// the register, rather than encode the immediate in every instruction.
// This has the risk of increasing register pressure from stretched live
// ranges, however, the immediates should be trivial to rematerialize by
// the RA in the event of high register pressure.
// TODO : This is currently enabled for stores and binary ops. There are more
// cases for which this can be enabled, though this catches the bulk of the
// issues.
// TODO2 : This should really also be enabled under O2, but there's currently
// an issue with RA where we don't pull the constants into their users
// when we rematerialize them. I'll follow-up on enabling O2 after we fix that
// issue.
// TODO3 : This is currently limited to single basic blocks (DAG creation
// pulls block immediates to the top and merges them if necessary).
// Eventually, it would be nice to allow ConstantHoisting to merge constants
// globally for potentially added savings.
//
def imm_su : PatLeaf<(imm), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i64immSExt32_su : PatLeaf<(i64immSExt32), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;

def relocImm8_su : PatLeaf<(i8 relocImm), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def relocImm16_su : PatLeaf<(i16 relocImm), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def relocImm32_su : PatLeaf<(i32 relocImm), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;

def i16relocImmSExt8_su : PatLeaf<(i16relocImmSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i32relocImmSExt8_su : PatLeaf<(i32relocImmSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i64relocImmSExt8_su : PatLeaf<(i64relocImmSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i64relocImmSExt32_su : PatLeaf<(i64relocImmSExt32), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;

def i16immSExt8_su : PatLeaf<(i16immSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i32immSExt8_su : PatLeaf<(i32immSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;
def i64immSExt8_su : PatLeaf<(i64immSExt8), [{
    return !shouldAvoidImmediateInstFormsForSize(N);
}]>;

// i64immZExt32 predicate - True if the 64-bit immediate fits in a 32-bit
// unsigned field.
def i64immZExt32 : ImmLeaf<i64, [{ return isUInt<32>(Imm); }]>;

def i64immZExt32SExt8 : ImmLeaf<i64, [{
  return isUInt<32>(Imm) && isInt<8>(static_cast<int32_t>(Imm));
}]>;

// Helper fragments for loads.

// It's safe to fold a zextload/extload from i1 as a regular i8 load. The
// upper bits are guaranteed to be zero and we were going to emit a MOV8rm
// which might get folded during peephole anyway.
def loadi8 : PatFrag<(ops node:$ptr), (i8 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  return ExtType == ISD::NON_EXTLOAD || ExtType == ISD::EXTLOAD ||
         ExtType == ISD::ZEXTLOAD;
}]>;

// It's always safe to treat a anyext i16 load as a i32 load if the i16 is
// known to be 32-bit aligned or better. Ditto for i8 to i16.
def loadi16 : PatFrag<(ops node:$ptr), (i16 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD)
    return true;
  if (ExtType == ISD::EXTLOAD && EnablePromoteAnyextLoad)
    return LD->getAlign() >= 2 && LD->isSimple();
  return false;
}]>;

def loadi32 : PatFrag<(ops node:$ptr), (i32 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD)
    return true;
  if (ExtType == ISD::EXTLOAD && EnablePromoteAnyextLoad)
    return LD->getAlign() >= 4 && LD->isSimple();
  return false;
}]>;

def loadi64  : PatFrag<(ops node:$ptr), (i64 (load node:$ptr))>;
def loadf16  : PatFrag<(ops node:$ptr), (f16 (load node:$ptr))>;
def loadf32  : PatFrag<(ops node:$ptr), (f32 (load node:$ptr))>;
def loadf64  : PatFrag<(ops node:$ptr), (f64 (load node:$ptr))>;
def loadf80  : PatFrag<(ops node:$ptr), (f80 (load node:$ptr))>;
def loadf128 : PatFrag<(ops node:$ptr), (f128 (load node:$ptr))>;
def alignedloadf128 : PatFrag<(ops node:$ptr), (f128 (load node:$ptr)), [{
  LoadSDNode *Ld = cast<LoadSDNode>(N);
  return Ld->getAlign() >= Ld->getMemoryVT().getStoreSize();
}]>;
def memopf128 : PatFrag<(ops node:$ptr), (f128 (load node:$ptr)), [{
  LoadSDNode *Ld = cast<LoadSDNode>(N);
  return Subtarget->hasSSEUnalignedMem() ||
         Ld->getAlign() >= Ld->getMemoryVT().getStoreSize();
}]>;

def sextloadi16i8  : PatFrag<(ops node:$ptr), (i16 (sextloadi8 node:$ptr))>;
def sextloadi32i8  : PatFrag<(ops node:$ptr), (i32 (sextloadi8 node:$ptr))>;
def sextloadi32i16 : PatFrag<(ops node:$ptr), (i32 (sextloadi16 node:$ptr))>;
def sextloadi64i8  : PatFrag<(ops node:$ptr), (i64 (sextloadi8 node:$ptr))>;
def sextloadi64i16 : PatFrag<(ops node:$ptr), (i64 (sextloadi16 node:$ptr))>;
def sextloadi64i32 : PatFrag<(ops node:$ptr), (i64 (sextloadi32 node:$ptr))>;

def zextloadi8i1   : PatFrag<(ops node:$ptr), (i8  (zextloadi1 node:$ptr))>;
def zextloadi16i1  : PatFrag<(ops node:$ptr), (i16 (zextloadi1 node:$ptr))>;
def zextloadi32i1  : PatFrag<(ops node:$ptr), (i32 (zextloadi1 node:$ptr))>;
def zextloadi16i8  : PatFrag<(ops node:$ptr), (i16 (zextloadi8 node:$ptr))>;
def zextloadi32i8  : PatFrag<(ops node:$ptr), (i32 (zextloadi8 node:$ptr))>;
def zextloadi32i16 : PatFrag<(ops node:$ptr), (i32 (zextloadi16 node:$ptr))>;
def zextloadi64i1  : PatFrag<(ops node:$ptr), (i64 (zextloadi1 node:$ptr))>;
def zextloadi64i8  : PatFrag<(ops node:$ptr), (i64 (zextloadi8 node:$ptr))>;
def zextloadi64i16 : PatFrag<(ops node:$ptr), (i64 (zextloadi16 node:$ptr))>;
def zextloadi64i32 : PatFrag<(ops node:$ptr), (i64 (zextloadi32 node:$ptr))>;

def extloadi8i1    : PatFrag<(ops node:$ptr), (i8  (extloadi1 node:$ptr))>;
def extloadi16i1   : PatFrag<(ops node:$ptr), (i16 (extloadi1 node:$ptr))>;
def extloadi32i1   : PatFrag<(ops node:$ptr), (i32 (extloadi1 node:$ptr))>;
def extloadi16i8   : PatFrag<(ops node:$ptr), (i16 (extloadi8 node:$ptr))>;
def extloadi32i8   : PatFrag<(ops node:$ptr), (i32 (extloadi8 node:$ptr))>;
def extloadi32i16  : PatFrag<(ops node:$ptr), (i32 (extloadi16 node:$ptr))>;
def extloadi64i1   : PatFrag<(ops node:$ptr), (i64 (extloadi1 node:$ptr))>;
def extloadi64i8   : PatFrag<(ops node:$ptr), (i64 (extloadi8 node:$ptr))>;
def extloadi64i16  : PatFrag<(ops node:$ptr), (i64 (extloadi16 node:$ptr))>;

// We can treat an i8/i16 extending load to i64 as a 32 bit load if its known
// to be 4 byte aligned or better.
def extloadi64i32  : PatFrag<(ops node:$ptr), (i64 (unindexedload node:$ptr)), [{
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType != ISD::EXTLOAD)
    return false;
  if (LD->getMemoryVT() == MVT::i32)
    return true;

  return LD->getAlign() >= 4 && LD->isSimple();
}]>;

// binary op with only one user
class binop_oneuse<SDPatternOperator operator>
    : PatFrag<(ops node:$A, node:$B),
              (operator node:$A, node:$B), [{
  return N->hasOneUse();
}]>;

def add_su : binop_oneuse<add>;
def and_su : binop_oneuse<and>;
def srl_su : binop_oneuse<srl>;

class binop_twouses<SDPatternOperator operator>
    : PatFrag<(ops node:$A, node:$B),
              (operator node:$A, node:$B), [{
  return N->hasNUsesOfValue(2, 0);
}]>;

def and_du : binop_twouses<and>;

// unary op with only one user
class unop_oneuse<SDPatternOperator operator>
    : PatFrag<(ops node:$A),
              (operator node:$A), [{
  return N->hasOneUse();
}]>;


def ineg_su : unop_oneuse<ineg>;
def trunc_su : unop_oneuse<trunc>;

def X86add_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                               (X86add_flag node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 1));
}]>;

def X86sub_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                               (X86sub_flag node:$lhs, node:$rhs), [{
  // Only use DEC if the result is used.
  return !SDValue(N, 0).use_empty() && hasNoCarryFlagUses(SDValue(N, 1));
}]>;

def X86testpat : PatFrag<(ops node:$lhs, node:$rhs),
                         (X86cmp (and_su node:$lhs, node:$rhs), 0)>;
def X86ctestpat : PatFrag<(ops node:$lhs, node:$rhs, node:$dcf, node:$cond),
                          (X86ctest (and_du node:$lhs, node:$rhs),
                            (and_du node:$lhs, node:$rhs), node:$dcf,
                            node:$cond, EFLAGS)>;

def X86any_fcmp : PatFrags<(ops node:$lhs, node:$rhs),
                          [(X86strict_fcmp node:$lhs, node:$rhs),
                           (X86fcmp node:$lhs, node:$rhs)]>;

def PrefetchWLevel : PatFrag<(ops), (i32 timm), [{
  return N->getSExtValue() <= 3;
}]>;

def X86lock_add_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                               (X86lock_add node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 0));
}]>;

def X86lock_sub_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                               (X86lock_sub node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 0));
}]>;

def X86tcret_6regs : PatFrag<(ops node:$ptr, node:$off),
                             (X86tcret node:$ptr, node:$off), [{
  // X86tcret args: (*chain, ptr, imm, regs..., glue)
  unsigned NumRegs = 0;
  for (unsigned i = 3, e = N->getNumOperands(); i != e; ++i)
    if (isa<RegisterSDNode>(N->getOperand(i)) && ++NumRegs > 6)
      return false;
  return true;
}]>;

def X86tcret_1reg : PatFrag<(ops node:$ptr, node:$off),
                             (X86tcret node:$ptr, node:$off), [{
  // X86tcret args: (*chain, ptr, imm, regs..., glue)
  unsigned NumRegs = 1;
  const SDValue& BasePtr = cast<LoadSDNode>(N->getOperand(1))->getBasePtr();
  if (isa<FrameIndexSDNode>(BasePtr))
    NumRegs = 3;
  else if (BasePtr->getNumOperands() && isa<GlobalAddressSDNode>(BasePtr->getOperand(0)))
    NumRegs = 3;
  for (unsigned i = 3, e = N->getNumOperands(); i != e; ++i)
    if (isa<RegisterSDNode>(N->getOperand(i)) && ( NumRegs-- == 0))
      return false;
  return true;
}]>;

// If this is an anyext of the remainder of an 8-bit sdivrem, use a MOVSX
// instead of a MOVZX. The sdivrem lowering will emit emit a MOVSX to move
// %ah to the lower byte of a register. By using a MOVSX here we allow a
// post-isel peephole to merge the two MOVSX instructions into one.
def anyext_sdiv : PatFrag<(ops node:$lhs), (anyext node:$lhs),[{
  return (N->getOperand(0).getOpcode() == ISD::SDIVREM &&
          N->getOperand(0).getResNo() == 1);
}]>;

// Any instruction that defines a 32-bit result leaves the high half of the
// register. Truncate can be lowered to EXTRACT_SUBREG. CopyFromReg may
// be copying from a truncate. AssertSext/AssertZext/AssertAlign aren't saying
// anything about the upper 32 bits, they're probably just qualifying a
// CopyFromReg. FREEZE may be coming from a a truncate. Any other 32-bit
// operation will zero-extend up to 64 bits.
def def32 : PatLeaf<(i32 GR32:$src), [{
  return N->getOpcode() != ISD::TRUNCATE &&
         N->getOpcode() != TargetOpcode::EXTRACT_SUBREG &&
         N->getOpcode() != ISD::CopyFromReg &&
         N->getOpcode() != ISD::AssertSext &&
         N->getOpcode() != ISD::AssertZext &&
         N->getOpcode() != ISD::AssertAlign &&
         N->getOpcode() != ISD::FREEZE;
}]>;

// Treat an 'or' node is as an 'add' if the or'ed bits are known to be zero.
def or_is_add : PatFrag<(ops node:$lhs, node:$rhs), (or node:$lhs, node:$rhs),[{
  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N->getOperand(1)))
    return CurDAG->MaskedValueIsZero(N->getOperand(0), CN->getAPIntValue());

  KnownBits Known0 = CurDAG->computeKnownBits(N->getOperand(0), 0);
  KnownBits Known1 = CurDAG->computeKnownBits(N->getOperand(1), 0);
  return (~Known0.Zero & ~Known1.Zero) == 0;
}]>;

def shiftMask8 : PatFrag<(ops node:$lhs), (and node:$lhs, imm), [{
  return isUnneededShiftMask(N, 3);
}]>;

def shiftMask16 : PatFrag<(ops node:$lhs), (and node:$lhs, imm), [{
  return isUnneededShiftMask(N, 4);
}]>;

def shiftMask32 : PatFrag<(ops node:$lhs), (and node:$lhs, imm), [{
  return isUnneededShiftMask(N, 5);
}]>;

def shiftMask64 : PatFrag<(ops node:$lhs), (and node:$lhs, imm), [{
  return isUnneededShiftMask(N, 6);
}]>;

//===----------------------------------------------------------------------===//
// Pattern fragments to auto generate BMI instructions.
//===----------------------------------------------------------------------===//

def or_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                           (X86or_flag node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 1));
}]>;

def xor_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                            (X86xor_flag node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 1));
}]>;

def and_flag_nocf : PatFrag<(ops node:$lhs, node:$rhs),
                            (X86and_flag node:$lhs, node:$rhs), [{
  return hasNoCarryFlagUses(SDValue(N, 1));
}]>;

//===----------------------------------------------------------------------===//
// FPStack specific DAG Nodes.
//===----------------------------------------------------------------------===//

def SDTX86Fld       : SDTypeProfile<1, 1, [SDTCisFP<0>,
                                           SDTCisPtrTy<1>]>;
def SDTX86Fst       : SDTypeProfile<0, 2, [SDTCisFP<0>,
                                           SDTCisPtrTy<1>]>;
def SDTX86Fild      : SDTypeProfile<1, 1, [SDTCisFP<0>, SDTCisPtrTy<1>]>;
def SDTX86Fist      : SDTypeProfile<0, 2, [SDTCisFP<0>, SDTCisPtrTy<1>]>;

def SDTX86CwdStore  : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;
def SDTX86CwdLoad   : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;
def SDTX86FPEnv     : SDTypeProfile<0, 1, [SDTCisPtrTy<0>]>;

def X86fp80_add     : SDNode<"X86ISD::FP80_ADD", SDTFPBinOp, [SDNPCommutative]>;
def X86strict_fp80_add : SDNode<"X86ISD::STRICT_FP80_ADD", SDTFPBinOp,
                        [SDNPHasChain,SDNPCommutative]>;
def any_X86fp80_add : PatFrags<(ops node:$lhs, node:$rhs),
                               [(X86strict_fp80_add node:$lhs, node:$rhs),
                                (X86fp80_add node:$lhs, node:$rhs)]>;

def X86fld          : SDNode<"X86ISD::FLD", SDTX86Fld,
                             [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def X86fst          : SDNode<"X86ISD::FST", SDTX86Fst,
                             [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86fild         : SDNode<"X86ISD::FILD", SDTX86Fild,
                             [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def X86fist         : SDNode<"X86ISD::FIST", SDTX86Fist,
                             [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86fp_to_mem : SDNode<"X86ISD::FP_TO_INT_IN_MEM", SDTX86Fst,
                          [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def X86fp_cwd_get16 : SDNode<"X86ISD::FNSTCW16m",          SDTX86CwdStore,
                             [SDNPHasChain, SDNPMayStore, SDNPSideEffect,
                              SDNPMemOperand]>;
def X86fp_cwd_set16 : SDNode<"X86ISD::FLDCW16m",           SDTX86CwdLoad,
                             [SDNPHasChain, SDNPMayLoad, SDNPSideEffect,
                              SDNPMemOperand]>;
def X86fpenv_get    : SDNode<"X86ISD::FNSTENVm",           SDTX86FPEnv,
                             [SDNPHasChain, SDNPMayStore, SDNPSideEffect,
                              SDNPMemOperand]>;
def X86fpenv_set    : SDNode<"X86ISD::FLDENVm",            SDTX86FPEnv,
                             [SDNPHasChain, SDNPMayLoad, SDNPSideEffect,
                              SDNPMemOperand]>;

def X86fstf32 : PatFrag<(ops node:$val, node:$ptr),
                        (X86fst node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::f32;
}]>;
def X86fstf64 : PatFrag<(ops node:$val, node:$ptr),
                        (X86fst node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::f64;
}]>;
def X86fstf80 : PatFrag<(ops node:$val, node:$ptr),
                        (X86fst node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::f80;
}]>;

def X86fldf32 : PatFrag<(ops node:$ptr), (X86fld node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::f32;
}]>;
def X86fldf64 : PatFrag<(ops node:$ptr), (X86fld node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::f64;
}]>;
def X86fldf80 : PatFrag<(ops node:$ptr), (X86fld node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::f80;
}]>;

def X86fild16 : PatFrag<(ops node:$ptr), (X86fild node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;
def X86fild32 : PatFrag<(ops node:$ptr), (X86fild node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;
def X86fild64 : PatFrag<(ops node:$ptr), (X86fild node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;

def X86fist32 : PatFrag<(ops node:$val, node:$ptr),
                        (X86fist node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;

def X86fist64 : PatFrag<(ops node:$val, node:$ptr),
                        (X86fist node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;

def X86fp_to_i16mem : PatFrag<(ops node:$val, node:$ptr),
                              (X86fp_to_mem node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i16;
}]>;
def X86fp_to_i32mem : PatFrag<(ops node:$val, node:$ptr),
                              (X86fp_to_mem node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i32;
}]>;
def X86fp_to_i64mem : PatFrag<(ops node:$val, node:$ptr),
                              (X86fp_to_mem node:$val, node:$ptr), [{
  return cast<MemIntrinsicSDNode>(N)->getMemoryVT() == MVT::i64;
}]>;

//===----------------------------------------------------------------------===//
// FPStack pattern fragments
//===----------------------------------------------------------------------===//

def fpimm0 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(+0.0);
}]>;

def fpimmneg0 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(-0.0);
}]>;

def fpimm1 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(+1.0);
}]>;

def fpimmneg1 : FPImmLeaf<fAny, [{
  return Imm.isExactlyValue(-1.0);
}]>;
