//===-- PPCScheduleP8.td - PPC P8 Scheduling Definitions ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the SchedModel for the POWER8 processor.
//
//===----------------------------------------------------------------------===//

def P8Model : SchedMachineModel {
  let IssueWidth = 8;
  let LoadLatency = 3;
  let MispredictPenalty = 16;
  let LoopMicroOpBufferSize = 60;
  let MicroOpBufferSize = 64;
  // TODO: Due to limitation of instruction definitions, non-P8 instructions
  // are required to be listed here. Change this after it got fixed.
  let CompleteModel = 0;
  let UnsupportedFeatures = [HasSPE, PrefixInstrs, MMA,
                             PairedVectorMemops, PCRelativeMemops,
                             IsISA3_0, IsISA3_1, IsISAFuture];
}

let SchedModel = P8Model in {
  // Power8 Pipeline Units:

  def P8_LU_LS_FX : ProcResource<6>;
  def P8_LU_LS : ProcResource<4> { let Super = P8_LU_LS_FX; }
  def P8_LS : ProcResource<2> { let Super = P8_LU_LS; }
  def P8_LU : ProcResource<2> { let Super = P8_LU_LS; }
  def P8_FX : ProcResource<2> { let Super = P8_LU_LS_FX; }
  def P8_DFU : ProcResource<1>;
  def P8_BR : ProcResource<1> { let BufferSize = 16; }
  def P8_CY : ProcResource<1>;
  def P8_CRL : ProcResource<1>;
  def P8_VMX : ProcResource<2>;
  def P8_PM : ProcResource<2> {
    // This is workaround for scheduler to respect latency of long permute chain.
    let BufferSize = 1;
    let Super = P8_VMX;
  }
  def P8_XS : ProcResource<2> { let Super = P8_VMX; }
  def P8_VX : ProcResource<2> { let Super = P8_VMX; }
  def P8_FPU : ProcResource<4>;
  // Units for scalar, 2xDouble and 4xSingle
  def P8_FP_Scal : ProcResource<2> { let Super = P8_FPU; }
  def P8_FP_2x64 : ProcResource<2> { let Super = P8_FPU; }
  def P8_FP_4x32 : ProcResource<2> { let Super = P8_FPU; }

  // Power8 Dispatch Ports:
  // Two ports to do loads or fixed-point operations.
  // Two ports to do stores, fixed-point loads, or fixed-point operations.
  // Two ports for fixed-point operations.
  // Two issue ports shared by 2 DFP/2 VSX/2 VMX/1 CY/1 DFP operations.
  // One for branch operations.
  // One for condition register operations.

  // TODO: Model dispatch of cracked instructions.

  // Six ports in total are available for fixed-point operations.
  def P8_PORT_ALLFX : ProcResource<6>;
  // Four ports in total are available for fixed-point load operations.
  def P8_PORT_FXLD : ProcResource<4> { let Super = P8_PORT_ALLFX; }
  // Two ports to do loads or fixed-point operations.
  def P8_PORT_LD_FX : ProcResource<2> { let Super = P8_PORT_FXLD; }
  // Two ports to do stores, fixed-point loads, or fixed-point operations.
  def P8_PORT_ST_FXLD_FX : ProcResource<2> { let Super = P8_PORT_FXLD; }
  // Two issue ports shared by two floating-point, two VSX, two VMX, one crypto,
  // and one DFP operations.
  def P8_PORT_VMX_FP : ProcResource<2>;
  // One port for branch operation.
  def P8_PORT_BR : ProcResource<1>;
  // One port for condition register operation.
  def P8_PORT_CR : ProcResource<1>;

  def P8_ISSUE_FX : SchedWriteRes<[P8_PORT_ALLFX]>;
  def P8_ISSUE_FXLD : SchedWriteRes<[P8_PORT_FXLD]>;
  def P8_ISSUE_LD : SchedWriteRes<[P8_PORT_LD_FX]>;
  def P8_ISSUE_ST : SchedWriteRes<[P8_PORT_ST_FXLD_FX]>;
  def P8_ISSUE_VSX : SchedWriteRes<[P8_PORT_VMX_FP]>;
  def P8_ISSUE_BR : SchedWriteRes<[P8_PORT_BR]>;
  def P8_ISSUE_CR : SchedWriteRes<[P8_PORT_CR]>;

  // Power8 Instruction Latency & Port Groups:

  def P8_LS_LU_NONE : SchedWriteRes<[P8_LU, P8_LS]>;
  def P8_LS_FP_NONE : SchedWriteRes<[P8_LS, P8_FPU]>;
  def P8_LU_or_LS_3C : SchedWriteRes<[P8_LU_LS]> { let Latency = 3; }
  def P8_LS_FX_3C : SchedWriteRes<[P8_LS, P8_FX]> { let Latency = 3; }
  def P8_LU_or_LS_or_FX_2C : SchedWriteRes<[P8_LU_LS_FX]> { let Latency = 2; }
  def P8_LU_or_LS_FX_3C : SchedWriteRes<[P8_LU_LS, P8_FX]> { let Latency = 3; }
  def P8_FX_NONE : SchedWriteRes<[P8_FX]>;
  def P8_FX_1C : SchedWriteRes<[P8_FX]> { let Latency = 1; }
  def P8_FX_2C : SchedWriteRes<[P8_FX]> { let Latency = 2; }
  def P8_FX_3C : SchedWriteRes<[P8_FX]> { let Latency = 3; }
  def P8_FX_5C : SchedWriteRes<[P8_FX]> { let Latency = 5; }
  def P8_FX_10C : SchedWriteRes<[P8_FX]> { let Latency = 10; }
  def P8_FX_23C : SchedWriteRes<[P8_FX]> { let Latency = 23; }
  def P8_FX_15C : SchedWriteRes<[P8_FX]> { let Latency = 15; }
  def P8_FX_41C : SchedWriteRes<[P8_FX]> { let Latency = 41; }
  def P8_BR_2C : SchedWriteRes<[P8_BR]> { let Latency = 2; }
  def P8_CR_NONE : SchedWriteRes<[P8_CRL]>;
  def P8_CR_3C : SchedWriteRes<[P8_CRL]> { let Latency = 3; }
  def P8_CR_5C : SchedWriteRes<[P8_CRL]> { let Latency = 5; }
  def P8_LU_5C : SchedWriteRes<[P8_LU]> { let Latency = 5; }
  def P8_LU_FX_5C : SchedWriteRes<[P8_LU, P8_FX]> { let Latency = 5; }
  def P8_LS_FP_FX_2C : SchedWriteRes<[P8_LS, P8_FPU, P8_FX]> { let Latency = 2; }
  def P8_LS_FP_FX_3C : SchedWriteRes<[P8_LS, P8_FPU, P8_FX]> { let Latency = 3; }
  def P8_LS_3C : SchedWriteRes<[P8_LS]> { let Latency = 3; }
  def P8_FP_3C : SchedWriteRes<[P8_FPU]> { let Latency = 3; }
  def P8_FP_Scal_6C : SchedWriteRes<[P8_FP_Scal]> { let Latency = 6; }
  def P8_FP_4x32_6C : SchedWriteRes<[P8_FP_4x32]> { let Latency = 6; }
  def P8_FP_2x64_6C : SchedWriteRes<[P8_FP_2x64]> { let Latency = 6; }
  def P8_FP_26C : SchedWriteRes<[P8_FP_Scal]> { let Latency = 26; }
  def P8_FP_28C : SchedWriteRes<[P8_FP_4x32]> { let Latency = 28; }
  def P8_FP_31C : SchedWriteRes<[P8_FP_Scal]> { let Latency = 31; }
  def P8_FP_Scal_32C : SchedWriteRes<[P8_FP_Scal]> { let Latency = 32; }
  def P8_FP_2x64_32C : SchedWriteRes<[P8_FP_2x64]> { let Latency = 32; }
  def P8_FP_4x32_32C : SchedWriteRes<[P8_FP_4x32]> { let Latency = 32; }
  def P8_FP_Scal_43C : SchedWriteRes<[P8_FP_Scal]> { let Latency = 43; }
  def P8_FP_2x64_43C : SchedWriteRes<[P8_FP_2x64]> { let Latency = 43; }
  def P8_XS_2C : SchedWriteRes<[P8_XS]> { let Latency = 2; }
  def P8_PM_2C : SchedWriteRes<[P8_PM]> { let Latency = 2; }
  def P8_XS_4C : SchedWriteRes<[P8_XS]> { let Latency = 4; }
  def P8_VX_7C : SchedWriteRes<[P8_VX]> { let Latency = 7; }
  def P8_XS_9C : SchedWriteRes<[P8_XS]> { let Latency = 9; }
  def P8_CY_6C : SchedWriteRes<[P8_CY]> { let Latency = 6; }
  def P8_DFU_13C : SchedWriteRes<[P8_DFU]> { let Latency = 13; }
  def P8_DFU_15C : SchedWriteRes<[P8_DFU]> { let Latency = 15; }
  def P8_DFU_17C : SchedWriteRes<[P8_DFU]> { let Latency = 17; }
  def P8_DFU_25C : SchedWriteRes<[P8_DFU]> { let Latency = 25; }
  def P8_DFU_32C : SchedWriteRes<[P8_DFU]> { let Latency = 32; }
  def P8_DFU_34C : SchedWriteRes<[P8_DFU]> { let Latency = 34; }
  def P8_DFU_40C : SchedWriteRes<[P8_DFU]> { let Latency = 40; }
  def P8_DFU_90C : SchedWriteRes<[P8_DFU]> { let Latency = 90; }
  def P8_DFU_96C : SchedWriteRes<[P8_DFU]> { let Latency = 96; }
  def P8_DFU_172C : SchedWriteRes<[P8_DFU]> { let Latency = 172; }
  // Direct move instructions
  def P8_DM_5C : SchedWriteRes<[]> { let Latency = 5; }

  // Instructions of CR pipeline

  def : InstRW<[P8_CR_NONE, P8_ISSUE_CR], (instrs MFCR, MFCR8)>;
  def : InstRW<[P8_CR_3C, P8_ISSUE_CR], (instrs MFOCRF, MFOCRF8)>;
  def : InstRW<[P8_CR_5C, P8_ISSUE_CR], (instrs MFLR, MFLR8, MFCTR, MFCTR8)>;

  // Instructions of CY pipeline

  def : InstRW<[P8_CY_6C, P8_ISSUE_VSX], (instrs
    VCIPHER, VCIPHERLAST, VNCIPHER, VNCIPHERLAST, VPMSUMB, VPMSUMD, VPMSUMH, VPMSUMW, VSBOX)>;

  // Instructions of FPU pipeline

  def : InstRW<[P8_FP_26C, P8_ISSUE_VSX], (instrs (instregex "^FDIVS(_rec)?$"), XSDIVSP)>;
  def : InstRW<[P8_FP_28C, P8_ISSUE_VSX], (instrs XVDIVSP)>;
  def : InstRW<[P8_FP_31C, P8_ISSUE_VSX], (instregex "^FSQRTS(_rec)?$")>;
  def : InstRW<[P8_FP_Scal_32C, P8_ISSUE_VSX], (instrs FDIV, FDIV_rec, XSDIVDP)>;
  def : InstRW<[P8_FP_2x64_32C, P8_ISSUE_VSX], (instrs XVDIVDP)>;
  def : InstRW<[P8_FP_4x32_32C, P8_ISSUE_VSX], (instrs XVSQRTSP)>;
  def : InstRW<[P8_FP_Scal_43C, P8_ISSUE_VSX], (instrs FSQRT, FSQRT_rec, XSSQRTDP)>;
  def : InstRW<[P8_FP_2x64_43C, P8_ISSUE_VSX], (instrs XVSQRTDP)>;

  def : InstRW<[P8_FP_3C, P8_ISSUE_VSX], (instrs
    MTFSFI_rec, MTFSF_rec, MTFSFI, MTFSFIb, MTFSF, MTFSFb, MTFSB0, MTFSB1)>;

  def : InstRW<[P8_FP_Scal_6C, P8_ISSUE_VSX], (instrs
    (instregex "^F(N)?M(ADD|SUB)(S)?(_rec)?$"),
    (instregex "^XS(N)?M(ADD|SUB)(A|M)(D|S)P$"),
    (instregex "^FC(F|T)I(D|W)(U)?(S|Z)?(_rec)?$"),
    (instregex "^(F|XS)(ABS|CPSGN|ADD|MUL|NABS|RE|NEG|SUB|SEL|RSQRTE)(D|S)?(P)?(s)?(_rec)?$"),
    (instregex "^FRI(M|N|P|Z)(D|S)(_rec)?$"),
    (instregex "^XSCVDP(S|U)X(W|D)S(s)?$"),
    (instregex "^XSCV(S|U)XD(D|S)P$"),
    (instregex "^XSCV(D|S)P(S|D)P(N)?$"),
    (instregex "^XSRDPI(C|M|P|Z)?$"),
    FMR, FRSP, FMR_rec, FRSP_rec, XSRSP)>;

  def : InstRW<[P8_FP_4x32_6C, P8_ISSUE_VSX], (instrs
    (instregex "^XV(N)?M(ADD|SUB)(A|M)SP$"),
    (instregex "^VRFI(M|N|P|Z)$"),
    XVRSQRTESP, XVSUBSP, VADDFP, VEXPTEFP, VLOGEFP, VMADDFP, VNMSUBFP, VREFP,
    VRSQRTEFP, VSUBFP, XVCVSXWSP, XVCVUXWSP, XVMULSP, XVNABSSP, XVNEGSP, XVRESP,
    XVCVDPSP, XVCVSXDSP, XVCVUXDSP, XVABSSP, XVADDSP, XVCPSGNSP)>;

  def : InstRW<[P8_FP_2x64_6C, P8_ISSUE_VSX], (instrs
    (instregex "^XVR(D|S)PI(C|M|P|Z)?$"),
    (instregex "^XVCV(S|U)X(D|W)DP$"),
    (instregex "^XVCV(D|W|S)P(S|U)X(D|W)S$"),
    (instregex "^XV(N)?(M)?(RSQRTE|CPSGN|SUB|ADD|ABS|UL|NEG|RE)(A|M)?DP$"),
    XVCVSPDP)>;

  // Instructions of FX, LU or LS pipeline

  def : InstRW<[P8_FX_NONE, P8_ISSUE_FX], (instrs TDI, TWI, TD, TW, MTCRF, MTCRF8, MTOCRF, MTOCRF8)>;
  def : InstRW<[P8_FX_1C, P8_ISSUE_FX], (instregex "^RLWIMI(8)?$")>;
  // TODO: Pipeline of logical instructions might be LS or FX
  def : InstRW<[P8_FX_2C, P8_ISSUE_FX], (instrs
    (instregex "^(N|X)?(EQV|AND|OR)(I)?(S|C)?(8)?(_rec)?$"),
    (instregex "^EXTS(B|H|W)(8)?(_32)?(_64)?(_rec)?$"),
    (instregex "^RL(D|W)(I)?(NM|C)(L|R)?(8)?(_32)?(_64)?(_rec)?$"),
    (instregex "^S(L|R)(A)?(W|D)(I)?(8)?(_rec|_32)?$"),
    (instregex "^(ADD|SUBF)(M|Z)?(C|E)?(4|8)?O(_rec)?$"),
    (instregex "^(ADD|SUBF)(M|Z)?E(8)?_rec$"),
    (instregex "^(ADD|SUBF|NEG)(4|8)?_rec$"),
    NOP, ADDG6S, ADDG6S8, ADDZE, ADDZE8, ADDIC_rec, NEGO_rec, ADDC, ADDC8, SUBFC, SUBFC8,
    ADDC_rec, ADDC8_rec, SUBFC_rec, SUBFC8_rec, COPY, NEG8O_rec,
    RLDIMI, RLDIMI_rec, RLWIMI8_rec, RLWIMI_rec)>;

  def : InstRW<[P8_FX_3C], (instregex "^(POP)?CNT(LZ)?(B|W|D)(8)?(_rec)?$")>;
  def : InstRW<[P8_FX_5C, P8_ISSUE_FX], (instrs
    (instregex "^MUL(H|L)(I|W|D)(8)?(U|O)?(_rec)?$"),
    CMPDI,CMPWI,CMPD,CMPW,CMPLDI,CMPLWI,CMPLD,CMPLW,
    ISEL, ISEL8, MTLR, MTLR8, MTCTR, MTCTR8, MTCTR8loop, MTCTRloop)>;

  def : InstRW<[P8_FX_10C, P8_ISSUE_VSX], (instregex "^MFTB(8)?$")>;
  def : InstRW<[P8_FX_15C, P8_ISSUE_FX], (instregex "^DIVW(U)?$")>;

  def : InstRW<[P8_FX_23C, P8_ISSUE_FX], (instregex "^DIV(D|WE)(U)?$")>;
  def : InstRW<[P8_FX_41C], (instrs
    (instregex "^DIV(D|W)(E)?(U)?O(_rec)?$"),
    (instregex "^DIV(D|W)(E)?(U)?_rec$"),
    DIVDE, DIVDEU)>;

  def : InstRW<[P8_LS_3C, P8_ISSUE_FX], (instrs MFSR, MFSRIN)>;

  def : InstRW<[P8_LU_5C, P8_ISSUE_LD], (instrs
    LFS, LFSX, LFD, LFDX, LFDXTLS, LFDXTLS_, LXVD2X, LXVW4X, LXVDSX, LVEBX, LVEHX, LVEWX,
    LVX, LVXL, LXSDX, LFIWAX, LFIWZX, LFSXTLS, LFSXTLS_, LXVB16X, LXVD2X, LXSIWZX,
    DFLOADf64, XFLOADf64, LIWZX)>;

  def : InstRW<[P8_LS_FX_3C, P8_ISSUE_FXLD], (instrs LQ)>;
  def : InstRW<[P8_LU_FX_5C, P8_ISSUE_LD], (instregex "^LF(D|S)U(X)?$")>;

  def : InstRW<[P8_LS_FP_NONE, P8_ISSUE_ST], (instrs
    STXSDX, STXVD2X, STXVW4X, STFIWX, STFS, STFSX, STFD, STFDX,
    STFDEPX, STFDXTLS, STFDXTLS_, STFSXTLS, STFSXTLS_, STXSIWX, STXSSP, STXSSPX)>;

  def : InstRW<[P8_LS_FP_FX_2C, P8_ISSUE_ST], (instrs STVEBX, STVEHX, STVEWX, STVX, STVXL)>;
  def : InstRW<[P8_LS_FP_FX_3C, P8_ISSUE_ST], (instregex "^STF(D|S)U(X)?$")>;

  def : InstRW<[P8_LS_LU_NONE, P8_ISSUE_ST], (instrs
    (instregex "^ST(B|H|W|D)(U)?(X)?(8|TLS)?(_)?(32)?$"),
    STBCIX, STBCX, STBEPX, STDBRX, STDCIX, STDCX, STHBRX, STHCIX, STHCX, STHEPX,
    STMW, STSWI, STWBRX, STWCIX, STWCX, STWEPX)>;

  def : InstRW<[P8_LU_or_LS_FX_3C, P8_ISSUE_FXLD],
    (instregex "^L(B|H|W|D)(A|Z)?(U)?(X)?(8|TLS)?(_)?(32)?$")>;

  def : InstRW<[P8_LU_or_LS_3C, P8_ISSUE_FXLD], (instrs
    LBARX, LBARXL, LBEPX, LBZCIX, LDARX, LDARXL, LDBRX, LDCIX, LFDEPX, LHARX, LHARXL, LHBRX, LXSIWAX,
    LHBRX8, LHEPX, LHZCIX, LMW, LSWI, LVSL, LVSR, LWARX, LWARXL, LWBRX, LWBRX8, LWEPX, LWZCIX)>;

  def : InstRW<[P8_LU_or_LS_or_FX_2C, P8_ISSUE_FX], (instrs
    (instregex "^ADDI(C)?(dtprel|tlsgd|toc)?(L)?(ADDR)?(32|8)?$"),
    (instregex "^ADDIS(dtprel|tlsgd|toc|gotTprel)?(HA)?(32|8)?$"),
    (instregex "^LI(S)?(8)?$"),
    (instregex "^ADD(M)?(E)?(4|8)?(TLS)?(_)?$"),
    (instregex "^SUBF(M|Z)?(E)?(IC)?(4|8)?$"),
    (instregex "^NEG(8)?(O)?$"))>;

  // Instructions of PM pipeline

  def : InstRW<[P8_PM_2C, P8_ISSUE_VSX], (instrs
    (instregex "^VPK(S|U)(H|W|D)(S|U)(M|S)$"),
    (instregex "^VUPK(H|L)(P|S)(H|B|W|X)$"),
    (instregex "^VSPLT(IS)?(B|H|W)(s)?$"),
    (instregex "^(XX|V)MRG(E|O|H|L)(B|H|W)$"),
    XXPERMDI, XXPERMDIs, XXSEL, XXSLDWI, XXSLDWIs, XXSPLTW, XXSPLTWs, VPERMXOR,
    VPKPX, VPERM, VBPERMQ, VGBBD, VSEL, VSL, VSLDOI, VSLO, VSR, VSRO)>;

  def : InstRW<[P8_XS_2C, P8_ISSUE_VSX], (instrs
    (instregex "^V(ADD|SUB)(S|U)(B|H|W|D)(M|S)$"),
    (instregex "^X(S|V)(MAX|MIN)(D|S)P$"),
    (instregex "^V(S)?(R)?(L)?(A)?(B|D|H|W)$"),
    (instregex "^VAVG(S|U)(B|H|W)$"),
    (instregex "^VM(AX|IN)(S|U)(B|H|W|D)$"),
    (instregex "^(XX|V)(L)?(N)?(X)?(AND|OR|EQV)(C)?$"),
    (instregex "^(X)?VCMP(EQ|GT|GE|B)(F|S|U)?(B|H|W|D|P|S)(P)?(_rec)?$"),
    (instregex "^VCLZ(B|H|W|D)$"),
    (instregex "^VPOPCNT(B|H|W)$"),
    XXLORf, XXLXORdpz, XXLXORspz, XXLXORz, VEQV, VMAXFP, VMINFP,
    VSHASIGMAD, VSHASIGMAW, VSUBCUW, VADDCUW, MFVSCR, MTVSCR)>;

  def : InstRW<[P8_XS_4C, P8_ISSUE_VSX], (instrs
    (instregex "^V(ADD|SUB)(E)?(C)?UQ(M)?$"),
    VPOPCNTD)>;

  def : InstRW<[P8_XS_9C, P8_ISSUE_CR], (instrs
    (instregex "^(F|XS)CMP(O|U)(D|S)(P)?$"),
    (instregex "^(F|XS|XV)T(DIV|SQRT)((D|S)P)?$"))>;

  // Instructions of VX pipeline

  def : InstRW<[P8_VX_7C, P8_ISSUE_VSX], (instrs
    (instregex "^V(M)?SUM(2|4)?(M|S|U)(B|H|W)(M|S)$"),
    (instregex "^VMUL(E|O)?(S|U)(B|H|W)(M)?$"),
    VMHADDSHS, VMHRADDSHS, VMLADDUHM)>;

  // Instructions of BR pipeline

  def : InstRW<[P8_BR_2C, P8_ISSUE_BR], (instrs
    (instregex "^(g)?B(C)?(C)?(CTR)?(L)?(A)?(R)?(L)?(8)?(_LD|_LWZ)?(always|into_toc|at)?(_RM)?(n)?$"),
    (instregex "^BD(N)?Z(L)?(R|A)?(L)?(m|p|8)?$"),
    (instregex "^BL(R|A)?(8)?(_NOP)?(_TLS)?(_)?(RM)?$"))>;

  // Instructions of DFP pipeline
  // DFP operations also use float/vector/crypto issue ports.
  def : InstRW<[P8_DFU_13C, P8_ISSUE_VSX], (instrs
    (instregex "^DTST(D|S)(C|F|G)(Q)?$"),
    (instregex "^D(Q|X)EX(Q)?(_rec)?$"),
    (instregex "^D(ADD|SUB|IEX|QUA|RRND|RINTX|RINTN|CTDP|DEDPD|ENBCD)(_rec)?$"),
    (instregex "^DSC(L|R)I(_rec)?$"),
    BCDADD_rec, BCDSUB_rec, DCMPO, DCMPU, DTSTEX, DQUAI)>;

  def : InstRW<[P8_DFU_15C, P8_ISSUE_VSX], (instrs
    (instregex "^DRINT(N|X)Q(_rec)?$"),
    DCMPOQ, DCMPUQ, DRRNDQ, DRRNDQ_rec, DIEXQ, DIEXQ_rec, DQUAIQ, DQUAIQ_rec,
    DTSTEXQ, DDEDPDQ, DDEDPDQ_rec, DENBCDQ, DENBCDQ_rec, DSCLIQ, DSCLIQ_rec,
    DSCRIQ, DSCRIQ_rec, DCTQPQ, DCTQPQ_rec)>;

  def : InstRW<[P8_DFU_17C, P8_ISSUE_VSX], (instregex "^D(ADD|SUB|QUA)Q(_rec)?$")>;
  def : InstRW<[P8_DFU_25C, P8_ISSUE_VSX], (instrs DRSP, DRSP_rec, DCTFIX, DCTFIX_rec)>;
  def : InstRW<[P8_DFU_32C, P8_ISSUE_VSX], (instrs DCFFIX, DCFFIX_rec)>;
  def : InstRW<[P8_DFU_34C, P8_ISSUE_VSX], (instrs DCFFIXQ, DCFFIXQ_rec)>;
  def : InstRW<[P8_DFU_40C, P8_ISSUE_VSX], (instrs DMUL, DMUL_rec)>;
  def : InstRW<[P8_DFU_90C, P8_ISSUE_VSX], (instrs DMULQ, DMULQ_rec)>;
  def : InstRW<[P8_DFU_96C, P8_ISSUE_VSX], (instrs DDIV, DDIV_rec)>;
  def : InstRW<[P8_DFU_172C, P8_ISSUE_VSX], (instrs DDIVQ, DDIVQ_rec)>;

  // Direct move instructions

   def : InstRW<[P8_DM_5C, P8_ISSUE_VSX], (instrs
     MFVRD, MFVSRD, MFVRWZ, MFVSRWZ, MTVRD, MTVSRD, MTVRWA, MTVSRWA, MTVRWZ, MTVSRWZ)>;
}
