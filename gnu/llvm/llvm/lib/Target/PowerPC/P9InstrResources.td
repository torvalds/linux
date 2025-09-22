//===- P9InstrResources.td - P9 Instruction Resource Defs  -*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the resources required by P9 instructions. This is part of
// the P9 processor model used for instruction scheduling. This file should
// contain all the instructions that may be used on Power 9. This is not
// just instructions that are new on Power 9 but also instructions that were
// available on earlier architectures and are still used in Power 9.
//
// The makeup of the P9 CPU is modeled as follows:
//   - Each CPU is made up of two superslices.
//   - Each superslice is made up of two slices. Therefore, there are 4 slices
//   for each CPU.
//   - Up to 6 instructions can be dispatched to each CPU. Three per superslice.
//   - Each CPU has:
//     - One CY (Crypto) unit P9_CY_*
//     - One DFU (Decimal Floating Point and Quad Precision) unit P9_DFU_*
//     - Two PM (Permute) units. One on each superslice. P9_PM_*
//     - Two DIV (Fixed Point Divide) units. One on each superslize. P9_DIV_*
//     - Four ALU (Fixed Point Arithmetic) units. One on each slice. P9_ALU_*
//     - Four DP (Floating Point) units. One on each slice. P9_DP_*
//       This also includes fixed point multiply add.
//     - Four AGEN (Address Generation) units. One for each slice. P9_AGEN_*
//     - Four Load/Store Queues. P9_LS_*
//   - Each set of instructions will require a number of these resources.
//===----------------------------------------------------------------------===//

// Two cycle ALU vector operation that uses an entire superslice.
// Uses both ALU units (the even ALUE and odd ALUO units), two pipelines
// (EXECE, EXECO) and 1 dispatch (DISP) to the given superslice.
def : InstRW<[P9_ALUE_2C, P9_ALUO_2C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    (instregex "VADDU(B|H|W|D)M$"),
    (instregex "VAND(C)?$"),
    (instregex "VEXTS(B|H|W)2(D|W)(s)?$"),
    (instregex "V_SET0(B|H)?$"),
    (instregex "VS(R|L)(B|H|W|D)$"),
    (instregex "VSUBU(B|H|W|D)M$"),
    (instregex "VPOPCNT(B|H)$"),
    (instregex "VRL(B|H|W|D)$"),
    (instregex "VSRA(B|H|W|D)$"),
    (instregex "XV(N)?ABS(D|S)P$"),
    (instregex "XVCPSGN(D|S)P$"),
    (instregex "XV(I|X)EXP(D|S)P$"),
    (instregex "VRL(D|W)(MI|NM)$"),
    (instregex "VMRG(E|O)W$"),
    MTVSRDD,
    VEQV,
    VNAND,
    VNEGD,
    VNEGW,
    VNOR,
    VOR,
    VORC,
    VSEL,
    VXOR,
    XVNEGDP,
    XVNEGSP,
    XXLAND,
    XXLANDC,
    XXLEQV,
    XXLEQVOnes,
    XXLNAND,
    XXLNOR,
    XXLOR,
    XXLORf,
    XXLORC,
    XXLXOR,
    XXLXORdpz,
    XXLXORspz,
    XXLXORz,
    XXSEL,
    XSABSQP,
    XSCPSGNQP,
    XSIEXPQP,
    XSNABSQP,
    XSNEGQP,
    XSXEXPQP
)>;

// Restricted Dispatch ALU operation for 3 cycles. The operation runs on a
// single slice. However, since it is Restricted, it requires all 3 dispatches
// (DISP) for that superslice.
def : InstRW<[P9_ALU_3C, IP_EXEC_1C, DISP_3SLOTS_1C],
      (instrs
    (instregex "TABORT(D|W)C(I)?$"),
    (instregex "MTFSB(0|1)$"),
    (instregex "MFFSC(D)?RN(I)?$"),
    (instregex "CMPRB(8)?$"),
    (instregex "TD(I)?$"),
    (instregex "TW(I)?$"),
    (instregex "FCMP(O|U)(S|D)$"),
    (instregex "XSTSTDC(S|D)P$"),
    FTDIV,
    FTSQRT,
    CMPEQB
)>;

// Standard Dispatch ALU operation for 3 cycles. Only one slice used.
def : InstRW<[P9_ALU_3C, IP_EXEC_1C, DISP_1C],
      (instrs
    (instregex "XSMAX(C|J)?DP$"),
    (instregex "XSMIN(C|J)?DP$"),
    (instregex "XSCMP(EQ|EXP|GE|GT|O|U)DP$"),
    (instregex "CNT(L|T)Z(D|W)(8)?(_rec)?$"),
    (instregex "POPCNT(D|W)$"),
    (instregex "CMPB(8)?$"),
    (instregex "SETB(8)?$"),
    XSTDIVDP,
    XSTSQRTDP,
    XSXSIGDP,
    XSCVSPDPN,
    BPERMD
)>;

// Standard Dispatch ALU operation for 2 cycles. Only one slice used.
def : InstRW<[P9_ALU_2C, IP_EXEC_1C, DISP_1C],
      (instrs
    (instregex "S(L|R)D$"),
    (instregex "SRAD(I)?$"),
    (instregex "EXTSWSLI_32_64$"),
    (instregex "MFV(S)?RD$"),
    (instregex "MTV(S)?RD$"),
    (instregex "MTV(S)?RW(A|Z)$"),
    (instregex "CMP(WI|LWI|W|LW)(8)?$"),
    (instregex "CMP(L)?D(I)?$"),
    (instregex "SUBF(I)?C(8)?(O)?$"),
    (instregex "ANDI(S)?(8)?(_rec)?$"),
    (instregex "ADDC(8)?(O)?$"),
    (instregex "ADDIC(8)?(_rec)?$"),
    (instregex "ADD(8|4)(O)?(_rec)?$"),
    (instregex "ADD(E|ME|ZE)(8)?(O)?(_rec)?$"),
    (instregex "SUBF(E|ME|ZE)?(8)?(O)?(_rec)?$"),
    (instregex "NEG(8)?(O)?(_rec)?$"),
    (instregex "POPCNTB$"),
    (instregex "POPCNTB8$"),
    (instregex "ADD(I|IS)?(8)?$"),
    (instregex "LI(S)?(8)?$"),
    (instregex "(X)?OR(I|IS)?(8)?(_rec)?$"),
    (instregex "NAND(8)?(_rec)?$"),
    (instregex "AND(C)?(8)?(_rec)?$"),
    (instregex "NOR(8)?(_rec)?$"),
    (instregex "OR(C)?(8)?(_rec)?$"),
    (instregex "EQV(8)?(_rec)?$"),
    (instregex "EXTS(B|H|W)(8)?(_32)?(_64)?(_rec)?$"),
    (instregex "ADD(4|8)(TLS)?(_)?$"),
    (instregex "NEG(8)?(O)?$"),
    (instregex "ADDI(S)?toc(HA|L)(8)?$"),
    (instregex "LA(8)?$"),
    COPY,
    MCRF,
    MCRXRX,
    XSNABSDP,
    XSNABSDPs,
    XSXEXPDP,
    XSABSDP,
    XSNEGDP,
    XSCPSGNDP,
    MFVSRWZ,
    MFVRWZ,
    EXTSWSLI,
    SRADI_32,
    RLDIC,
    RFEBB,
    TBEGIN,
    TRECHKPT,
    NOP,
    WAIT
)>;

// Restricted Dispatch ALU operation for 2 cycles. The operation runs on a
// single slice. However, since it is Restricted, it requires all 3 dispatches
// (DISP) for that superslice.
def : InstRW<[P9_ALU_2C, IP_EXEC_1C, DISP_3SLOTS_1C],
      (instrs
    (instregex "RLDC(L|R)$"),
    (instregex "RLWIMI(8)?$"),
    (instregex "RLDIC(L|R)(_32)?(_64)?$"),
    (instregex "M(F|T)OCRF(8)?$"),
    (instregex "CR(6)?(UN)?SET$"),
    (instregex "CR(N)?(OR|AND)(C)?$"),
    (instregex "S(L|R)W(8)?$"),
    (instregex "RLW(INM|NM)(8)?$"),
    (instregex "F(N)?ABS(D|S)$"),
    (instregex "FNEG(D|S)$"),
    (instregex "FCPSGN(D|S)$"),
    (instregex "SRAW(I)?$"),
    (instregex "ISEL(8)?$"),
    RLDIMI,
    XSIEXPDP,
    FMR,
    CREQV,
    CRNOT,
    CRXOR,
    TRECLAIM,
    TSR,
    TABORT
)>;

// Three cycle ALU vector operation that uses an entire superslice.
// Uses both ALU units (the even ALUE and odd ALUO units), two pipelines
// (EXECE, EXECO) and 1 dispatch (DISP) to the given superslice.
def : InstRW<[P9_ALUE_3C, P9_ALUO_3C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    (instregex "M(T|F)VSCR$"),
    (instregex "VCMPNEZ(B|H|W)$"),
    (instregex "VCMPEQU(B|H|W|D)$"),
    (instregex "VCMPNE(B|H|W)$"),
    (instregex "VABSDU(B|H|W)$"),
    (instregex "VADDU(B|H|W)S$"),
    (instregex "VAVG(S|U)(B|H|W)$"),
    (instregex "VCMP(EQ|GE|GT)FP(_rec)?$"),
    (instregex "VCMPBFP(_rec)?$"),
    (instregex "VC(L|T)Z(B|H|W|D)$"),
    (instregex "VADDS(B|H|W)S$"),
    (instregex "V(MIN|MAX)FP$"),
    (instregex "V(MIN|MAX)(S|U)(B|H|W|D)$"),
    VBPERMD,
    VADDCUW,
    VPOPCNTW,
    VPOPCNTD,
    VPRTYBD,
    VPRTYBW,
    VSHASIGMAD,
    VSHASIGMAW,
    VSUBSBS,
    VSUBSHS,
    VSUBSWS,
    VSUBUBS,
    VSUBUHS,
    VSUBUWS,
    VSUBCUW,
    VCMPGTSB,
    VCMPGTSB_rec,
    VCMPGTSD,
    VCMPGTSD_rec,
    VCMPGTSH,
    VCMPGTSH_rec,
    VCMPGTSW,
    VCMPGTSW_rec,
    VCMPGTUB,
    VCMPGTUB_rec,
    VCMPGTUD,
    VCMPGTUD_rec,
    VCMPGTUH,
    VCMPGTUH_rec,
    VCMPGTUW,
    VCMPGTUW_rec,
    VCMPNEB_rec,
    VCMPNEH_rec,
    VCMPNEW_rec,
    VCMPNEZB_rec,
    VCMPNEZH_rec,
    VCMPNEZW_rec,
    VCMPEQUB_rec,
    VCMPEQUD_rec,
    VCMPEQUH_rec,
    VCMPEQUW_rec,
    XVCMPEQDP,
    XVCMPEQDP_rec,
    XVCMPEQSP,
    XVCMPEQSP_rec,
    XVCMPGEDP,
    XVCMPGEDP_rec,
    XVCMPGESP,
    XVCMPGESP_rec,
    XVCMPGTDP,
    XVCMPGTDP_rec,
    XVCMPGTSP,
    XVCMPGTSP_rec,
    XVMAXDP,
    XVMAXSP,
    XVMINDP,
    XVMINSP,
    XVTDIVDP,
    XVTDIVSP,
    XVTSQRTDP,
    XVTSQRTSP,
    XVTSTDCDP,
    XVTSTDCSP,
    XVXSIGDP,
    XVXSIGSP
)>;

// 7 cycle DP vector operation that uses an entire superslice.
// Uses both DP units (the even DPE and odd DPO units), two pipelines (EXECE,
// EXECO) and all three dispatches (DISP) to the given superslice.
def : InstRW<[P9_DPE_7C, P9_DPO_7C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    VADDFP,
    VCTSXS,
    VCTSXS_0,
    VCTUXS,
    VCTUXS_0,
    VEXPTEFP,
    VLOGEFP,
    VMADDFP,
    VMHADDSHS,
    VNMSUBFP,
    VREFP,
    VRFIM,
    VRFIN,
    VRFIP,
    VRFIZ,
    VRSQRTEFP,
    VSUBFP,
    XVADDDP,
    XVADDSP,
    XVCVDPSP,
    XVCVDPSXDS,
    XVCVDPSXWS,
    XVCVDPUXDS,
    XVCVDPUXWS,
    XVCVHPSP,
    XVCVSPDP,
    XVCVSPHP,
    XVCVSPSXDS,
    XVCVSPSXWS,
    XVCVSPUXDS,
    XVCVSPUXWS,
    XVCVSXDDP,
    XVCVSXDSP,
    XVCVSXWDP,
    XVCVSXWSP,
    XVCVUXDDP,
    XVCVUXDSP,
    XVCVUXWDP,
    XVCVUXWSP,
    XVMADDADP,
    XVMADDASP,
    XVMADDMDP,
    XVMADDMSP,
    XVMSUBADP,
    XVMSUBASP,
    XVMSUBMDP,
    XVMSUBMSP,
    XVMULDP,
    XVMULSP,
    XVNMADDADP,
    XVNMADDASP,
    XVNMADDMDP,
    XVNMADDMSP,
    XVNMSUBADP,
    XVNMSUBASP,
    XVNMSUBMDP,
    XVNMSUBMSP,
    XVRDPI,
    XVRDPIC,
    XVRDPIM,
    XVRDPIP,
    XVRDPIZ,
    XVREDP,
    XVRESP,
    XVRSPI,
    XVRSPIC,
    XVRSPIM,
    XVRSPIP,
    XVRSPIZ,
    XVRSQRTEDP,
    XVRSQRTESP,
    XVSUBDP,
    XVSUBSP,
    VCFSX,
    VCFSX_0,
    VCFUX,
    VCFUX_0,
    VMHRADDSHS,
    VMLADDUHM,
    VMSUMMBM,
    VMSUMSHM,
    VMSUMSHS,
    VMSUMUBM,
    VMSUMUHM,
    VMSUMUDM,
    VMSUMUHS,
    VMULESB,
    VMULESH,
    VMULESW,
    VMULEUB,
    VMULEUH,
    VMULEUW,
    VMULOSB,
    VMULOSH,
    VMULOSW,
    VMULOUB,
    VMULOUH,
    VMULOUW,
    VMULUWM,
    VSUM2SWS,
    VSUM4SBS,
    VSUM4SHS,
    VSUM4UBS,
    VSUMSWS
)>;

// 5 cycle Restricted DP operation. One DP unit, one EXEC pipeline and all three
// dispatch units for the superslice.
def : InstRW<[P9_DP_5C, IP_EXEC_1C, DISP_3SLOTS_1C],
      (instrs
    (instregex "MADD(HD|HDU|LD|LD8)$"),
    (instregex "MUL(HD|HW|LD|LI|LI8|LW)(U)?(O)?$")
)>;

// 7 cycle Restricted DP operation. One DP unit, one EXEC pipeline and all three
// dispatch units for the superslice.
def : InstRW<[P9_DP_7C, IP_EXEC_1C, DISP_3SLOTS_1C],
      (instrs
    FRSP,
    (instregex "FRI(N|P|Z|M)(D|S)$"),
    (instregex "FRE(S)?$"),
    (instregex "FADD(S)?$"),
    (instregex "FMSUB(S)?$"),
    (instregex "FMADD(S)?$"),
    (instregex "FSUB(S)?$"),
    (instregex "FCFID(U)?(S)?$"),
    (instregex "FCTID(U)?(Z)?$"),
    (instregex "FCTIW(U)?(Z)?$"),
    (instregex "FRSQRTE(S)?$"),
    FNMADDS,
    FNMADD,
    FNMSUBS,
    FNMSUB,
    FSELD,
    FSELS,
    FMULS,
    FMUL,
    XSMADDADP,
    XSMADDASP,
    XSMADDMDP,
    XSMADDMSP,
    XSMSUBADP,
    XSMSUBASP,
    XSMSUBMDP,
    XSMSUBMSP,
    XSMULDP,
    XSMULSP,
    XSNMADDADP,
    XSNMADDASP,
    XSNMADDMDP,
    XSNMADDMSP,
    XSNMSUBADP,
    XSNMSUBASP,
    XSNMSUBMDP,
    XSNMSUBMSP
)>;

// 7 cycle Restricted DP operation and one 3 cycle ALU operation.
// These operations can be done in parallel. The DP is restricted so we need a
// full 4 dispatches.
def : InstRW<[P9_DP_7C, P9_ALU_3C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "FSEL(D|S)_rec$")
)>;

// 5 Cycle Restricted DP operation and one 2 cycle ALU operation.
def : InstRW<[P9_DPOpAndALUOp_7C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "MUL(H|L)(D|W)(U)?(O)?_rec$")
)>;

// 7 cycle Restricted DP operation and one 3 cycle ALU operation.
// These operations must be done sequentially.The DP is restricted so we need a
// full 4 dispatches.
def : InstRW<[P9_DPOpAndALU2Op_10C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "FRI(N|P|Z|M)(D|S)_rec$"),
    (instregex "FRE(S)?_rec$"),
    (instregex "FADD(S)?_rec$"),
    (instregex "FSUB(S)?_rec$"),
    (instregex "F(N)?MSUB(S)?_rec$"),
    (instregex "F(N)?MADD(S)?_rec$"),
    (instregex "FCFID(U)?(S)?_rec$"),
    (instregex "FCTID(U)?(Z)?_rec$"),
    (instregex "FCTIW(U)?(Z)?_rec$"),
    (instregex "FMUL(S)?_rec$"),
    (instregex "FRSQRTE(S)?_rec$"),
    FRSP_rec
)>;

// 7 cycle DP operation. One DP unit, one EXEC pipeline and 1 dispatch units.
def : InstRW<[P9_DP_7C, IP_EXEC_1C, DISP_1C],
      (instrs
    XSADDDP,
    XSADDSP,
    XSCVDPHP,
    XSCVDPSP,
    XSCVDPSXDS,
    XSCVDPSXDSs,
    XSCVDPSXWS,
    XSCVDPUXDS,
    XSCVDPUXDSs,
    XSCVDPUXWS,
    XSCVDPSXWSs,
    XSCVDPUXWSs,
    XSCVHPDP,
    XSCVSPDP,
    XSCVSXDDP,
    XSCVSXDSP,
    XSCVUXDDP,
    XSCVUXDSP,
    XSRDPI,
    XSRDPIC,
    XSRDPIM,
    XSRDPIP,
    XSRDPIZ,
    XSREDP,
    XSRESP,
    XSRSQRTEDP,
    XSRSQRTESP,
    XSSUBDP,
    XSSUBSP,
    XSCVDPSPN,
    XSRSP
)>;

// Three Cycle PM operation. Only one PM unit per superslice so we use the whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and one
// dispatch.
def : InstRW<[P9_PM_3C, IP_EXECO_1C, IP_EXECE_1C, DISP_1C],
      (instrs
    (instregex "LVS(L|R)$"),
    (instregex "VSPLTIS(W|H|B)$"),
    (instregex "VSPLT(W|H|B)(s)?$"),
    (instregex "V_SETALLONES(B|H)?$"),
    (instregex "VEXTRACTU(B|H|W)$"),
    (instregex "VINSERT(B|H|W|D)$"),
    MFVSRLD,
    MTVSRWS,
    VBPERMQ,
    VCLZLSBB,
    VCTZLSBB,
    VEXTRACTD,
    VEXTUBLX,
    VEXTUBRX,
    VEXTUHLX,
    VEXTUHRX,
    VEXTUWLX,
    VEXTUWRX,
    VGBBD,
    VMRGHB,
    VMRGHH,
    VMRGHW,
    VMRGLB,
    VMRGLH,
    VMRGLW,
    VPERM,
    VPERMR,
    VPERMXOR,
    VPKPX,
    VPKSDSS,
    VPKSDUS,
    VPKSHSS,
    VPKSHUS,
    VPKSWSS,
    VPKSWUS,
    VPKUDUM,
    VPKUDUS,
    VPKUHUM,
    VPKUHUS,
    VPKUWUM,
    VPKUWUS,
    VPRTYBQ,
    VSL,
    VSLDOI,
    VSLO,
    VSLV,
    VSR,
    VSRO,
    VSRV,
    VUPKHPX,
    VUPKHSB,
    VUPKHSH,
    VUPKHSW,
    VUPKLPX,
    VUPKLSB,
    VUPKLSH,
    VUPKLSW,
    XXBRD,
    XXBRH,
    XXBRQ,
    XXBRW,
    XXEXTRACTUW,
    XXINSERTW,
    XXMRGHW,
    XXMRGLW,
    XXPERM,
    XXPERMR,
    XXSLDWI,
    XXSLDWIs,
    XXSPLTIB,
    XXSPLTW,
    XXSPLTWs,
    XXPERMDI,
    XXPERMDIs,
    VADDCUQ,
    VADDECUQ,
    VADDEUQM,
    VADDUQM,
    VMUL10CUQ,
    VMUL10ECUQ,
    VMUL10EUQ,
    VMUL10UQ,
    VSUBCUQ,
    VSUBECUQ,
    VSUBEUQM,
    VSUBUQM,
    XSCMPEXPQP,
    XSCMPOQP,
    XSCMPUQP,
    XSTSTDCQP,
    XSXSIGQP,
    BCDCFN_rec,
    BCDCFZ_rec,
    BCDCPSGN_rec,
    BCDCTN_rec,
    BCDCTZ_rec,
    BCDSETSGN_rec,
    BCDS_rec,
    BCDTRUNC_rec,
    BCDUS_rec,
    BCDUTRUNC_rec,
    BCDADD_rec,
    BCDSUB_rec
)>;

// 12 Cycle DFU operation. Only one DFU unit per CPU so we use a whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and one
// dispatch.
def : InstRW<[P9_DFU_12C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    BCDSR_rec,
    XSADDQP,
    XSADDQPO,
    XSCVDPQP,
    XSCVQPDP,
    XSCVQPDPO,
    XSCVQPSDZ,
    XSCVQPSWZ,
    XSCVQPUDZ,
    XSCVQPUWZ,
    XSCVSDQP,
    XSCVUDQP,
    XSRQPI,
    XSRQPIX,
    XSRQPXP,
    XSSUBQP,
    XSSUBQPO
)>;

// 23 Cycle DFU operation. Only one DFU unit per CPU so we use a whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and one
// dispatch.
def : InstRW<[P9_DFU_23C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    BCDCTSQ_rec
)>;

// 24 Cycle DFU operation. Only one DFU unit per CPU so we use a whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and one
// dispatch.
def : InstRW<[P9_DFU_24C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    XSMADDQP,
    XSMADDQPO,
    XSMSUBQP,
    XSMSUBQPO,
    XSMULQP,
    XSMULQPO,
    XSNMADDQP,
    XSNMADDQPO,
    XSNMSUBQP,
    XSNMSUBQPO
)>;

// 37 Cycle DFU operation. Only one DFU unit per CPU so we use a whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and one
// dispatch.
def : InstRW<[P9_DFU_37C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    BCDCFSQ_rec
)>;

// 58 Cycle DFU operation. Only one DFU unit per CPU so we use a whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and one
// dispatch.
def : InstRW<[P9_DFU_58C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    XSDIVQP,
    XSDIVQPO
)>;

// 76 Cycle DFU operation. Only one DFU unit per CPU so we use a whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and all three
// dispatches.
def : InstRW<[P9_DFU_76C, IP_EXECE_1C, IP_EXECO_1C, DISP_1C],
      (instrs
    XSSQRTQP,
    XSSQRTQPO
)>;

// 6 Cycle Load uses a single slice.
def : InstRW<[P9_LS_6C, IP_AGEN_1C, DISP_1C],
      (instrs
    (instregex "LXVL(L)?")
)>;

// 5 Cycle Load uses a single slice.
def : InstRW<[P9_LS_5C, IP_AGEN_1C, DISP_1C],
      (instrs
    (instregex "LVE(B|H|W)X$"),
    (instregex "LVX(L)?"),
    (instregex "LXSI(B|H)ZX$"),
    LXSDX,
    LXVB16X,
    LXVD2X,
    LXVWSX,
    LXSIWZX,
    LXV,
    LXVX,
    LXSD,
    DFLOADf64,
    XFLOADf64,
    LIWZX
)>;

// 4 Cycle Load uses a single slice.
def : InstRW<[P9_LS_4C, IP_AGEN_1C, DISP_1C],
      (instrs
    (instregex "DCB(F|T|ST)(EP)?$"),
    (instregex "DCBZ(L)?(EP)?$"),
    (instregex "DCBTST(EP)?$"),
    (instregex "CP_COPY(8)?$"),
    (instregex "ICBI(EP)?$"),
    (instregex "ICBT(LS)?$"),
    (instregex "LBARX(L)?$"),
    (instregex "LBZ(CIX|8|X|X8|XTLS|XTLS_32)?(_)?$"),
    (instregex "LD(ARX|ARXL|BRX|CIX|X|XTLS)?(_)?$"),
    (instregex "LH(A|B)RX(L)?(8)?$"),
    (instregex "LHZ(8|CIX|X|X8|XTLS|XTLS_32)?(_)?$"),
    (instregex "LWARX(L)?$"),
    (instregex "LWBRX(8)?$"),
    (instregex "LWZ(8|CIX|X|X8|XTLS|XTLS_32)?(_)?$"),
    CP_ABORT,
    DARN,
    EnforceIEIO,
    ISYNC,
    MSGSYNC,
    TLBSYNC,
    SYNC,
    LMW,
    LSWI
)>;

// 4 Cycle Restricted load uses a single slice but the dispatch for the whole
// superslice.
def : InstRW<[P9_LS_4C, IP_AGEN_1C, DISP_3SLOTS_1C],
      (instrs
    LFIWZX,
    LFDX,
    (instregex "LFDXTLS?(_)?$"),
    LFD
)>;

// Cracked Load Instructions.
// Load instructions that can be done in parallel.
def : InstRW<[P9_LS_4C, P9_LS_4C, IP_AGEN_1C, IP_AGEN_1C,
              DISP_PAIR_1C],
      (instrs
    SLBIA,
    SLBIE,
    SLBMFEE,
    SLBMFEV,
    SLBMTE,
    TLBIEL
)>;

// Cracked Load Instruction.
// Requires Load and ALU pieces totaling 6 cycles. The Load and ALU
// operations can be run in parallel.
def : InstRW<[P9_LS_4C, P9_ALU_2C, IP_EXEC_1C, IP_AGEN_1C,
              DISP_PAIR_1C, DISP_PAIR_1C],
      (instrs
    (instregex "L(W|H)ZU(X)?(8)?$")
)>;

// Cracked TEND Instruction.
// Requires Load and ALU pieces totaling 6 cycles. The Load and ALU
// operations can be run in parallel.
def : InstRW<[P9_LS_4C, P9_ALU_2C, IP_EXEC_1C, IP_AGEN_1C,
              DISP_1C, DISP_1C],
      (instrs
    TEND
)>;


// Cracked Store Instruction
// Consecutive Store and ALU instructions. The store is restricted and requires
// three dispatches.
def : InstRW<[P9_StoreAndALUOp_3C, IP_EXEC_1C, IP_EXEC_1C, IP_AGEN_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "ST(B|H|W|D)CX$")
)>;

// Cracked Load instruction.
// Requires consecutive Load and ALU pieces totaling 6 cycles. The Load and ALU
// operations cannot be done at the same time and so their latencies are added.
def : InstRW<[P9_LoadAndALUOp_6C, IP_EXEC_1C, IP_AGEN_1C,
              DISP_1C, DISP_1C],
      (instrs
    (instregex "LHA(X)?(TLS)?(8)?(_32)?(_)?$"),
    (instregex "CP_PASTE(8)?_rec$"),
    (instregex "LWA(X)?(TLS)?(_32)?(_)?$"),
    TCHECK
)>;

// Cracked Restricted Load instruction.
// Requires consecutive Load and ALU pieces totaling 6 cycles. The Load and ALU
// operations cannot be done at the same time and so their latencies are added.
// Full 6 dispatches are required as this is both cracked and restricted.
def : InstRW<[P9_LoadAndALUOp_6C, IP_EXEC_1C, IP_AGEN_1C,
              DISP_3SLOTS_1C, DISP_3SLOTS_1C],
      (instrs
    LFIWAX
)>;

// Cracked Load instruction.
// Requires consecutive Load and ALU pieces totaling 7 cycles. The Load and ALU
// operations cannot be done at the same time and so their latencies are added.
// Full 4 dispatches are required as this is a cracked instruction.
def : InstRW<[P9_LoadAndALUOp_7C, IP_AGEN_1C, IP_EXEC_1C, DISP_1C, DISP_1C],
      (instrs
    LXSIWAX,
    LIWAX
)>;

// Cracked Load instruction.
// Requires consecutive Load (4 cycles) and ALU (3 cycles) pieces totaling 7
// cycles. The Load and ALU operations cannot be done at the same time and so
// their latencies are added.
// Full 6 dispatches are required as this is a restricted instruction.
def : InstRW<[P9_LoadAndALU2Op_7C, IP_AGEN_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_3SLOTS_1C],
      (instrs
    LFSX,
    (instregex "LFSXTLS?(_)?$"),
    LFS
)>;

// Cracked Load instruction.
// Requires consecutive Load and ALU pieces totaling 8 cycles. The Load and ALU
// operations cannot be done at the same time and so their latencies are added.
// Full 4 dispatches are required as this is a cracked instruction.
def : InstRW<[P9_LoadAndALU2Op_8C, IP_AGEN_1C, IP_EXEC_1C, DISP_1C, DISP_1C],
      (instrs
    LXSSP,
    LXSSPX,
    XFLOADf32,
    DFLOADf32
)>;

// Cracked 3-Way Load Instruction
// Load with two ALU operations that depend on each other
def : InstRW<[P9_LoadAndALUOp_6C, P9_ALU_2C, IP_AGEN_1C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_PAIR_1C, DISP_PAIR_1C, DISP_1C],
      (instrs
    (instregex "LHAU(X)?(8)?$"),
    LWAUX
)>;

// Cracked Load that requires the PM resource.
// Since the Load and the PM cannot be done at the same time the latencies are
// added. Requires 8 cycles. Since the PM requires the full superslice we need
// both EXECE, EXECO pipelines as well as 1 dispatch for the PM. The Load
// requires the remaining 1 dispatch.
def : InstRW<[P9_LoadAndPMOp_8C, IP_AGEN_1C, IP_EXECE_1C, IP_EXECO_1C,
              DISP_1C, DISP_1C],
      (instrs
    LXVH8X,
    LXVDSX,
    LXVW4X
)>;

// Single slice Restricted store operation. The restricted operation requires
// all three dispatches for the superslice.
def : InstRW<[P9_LS_1C, IP_EXEC_1C, IP_AGEN_1C, DISP_3SLOTS_1C],
      (instrs
    (instregex "STF(S|D|IWX|SX|DX|SXTLS|DXTLS|SXTLS_|DXTLS_)$"),
    (instregex "STXS(D|DX|SPX|IWX|IBX|IHX|SP)(v)?$"),
    (instregex "STW(8)?$"),
    (instregex "(D|X)FSTORE(f32|f64)$"),
    (instregex "ST(W|H|D)BRX$"),
    (instregex "ST(B|H|D)(8)?$"),
    (instregex "ST(B|W|H|D)(CI)?X(TLS|TLS_32)?(8)?(_)?$"),
    STIWX,
    SLBIEG,
    STMW,
    STSWI,
    TLBIE
)>;

// Vector Store Instruction
// Requires the whole superslice and therefore requires one dispatch
// as well as both the Even and Odd exec pipelines.
def : InstRW<[P9_LS_1C, IP_EXECE_1C, IP_EXECO_1C, IP_AGEN_1C, DISP_1C],
      (instrs
    (instregex "STVE(B|H|W)X$"),
    (instregex "STVX(L)?$"),
    (instregex "STXV(B16X|H8X|W4X|D2X|L|LL|X)?$")
)>;

// 5 Cycle DIV operation. Only one DIV unit per superslice so we use the whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and two
// dispatches.
def : InstRW<[P9_DIV_5C, IP_EXECE_1C, IP_EXECO_1C, DISP_EVEN_1C],
      (instrs
    (instregex "MTCTR(8)?(loop)?$"),
    (instregex "MTLR(8)?$")
)>;

// 12 Cycle DIV operation. Only one DIV unit per superslice so we use the whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and two
// dispatches.
def : InstRW<[P9_DIV_12C, IP_EXECE_1C, IP_EXECO_1C, DISP_EVEN_1C],
      (instrs
    (instregex "M(T|F)VRSAVE(v)?$"),
    (instregex "M(T|F)PMR$"),
    (instregex "M(T|F)TB(8)?$"),
    (instregex "MF(SPR|CTR|LR)(8)?$"),
    (instregex "M(T|F)MSR(D)?$"),
    (instregex "M(T|F)(U)?DSCR$"),
    (instregex "MTSPR(8)?$")
)>;

// 16 Cycle DIV operation. Only one DIV unit per superslice so we use the whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and two
// dispatches.
def : InstRW<[P9_DIV_16C_8, IP_EXECO_1C, IP_EXECE_1C, DISP_EVEN_1C],
      (instrs
    DIVW,
    DIVWO,
    DIVWU,
    DIVWUO,
    MODSW
)>;

// 24 Cycle DIV operation. Only one DIV unit per superslice so we use the whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and two
// dispatches.
def : InstRW<[P9_DIV_24C_8, IP_EXECO_1C, IP_EXECE_1C, DISP_EVEN_1C],
      (instrs
    DIVWE,
    DIVWEO,
    DIVD,
    DIVDO,
    DIVWEU,
    DIVWEUO,
    DIVDU,
    DIVDUO,
    MODSD,
    MODUD,
    MODUW
)>;

// 40 Cycle DIV operation. Only one DIV unit per superslice so we use the whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and all three
// dispatches.
def : InstRW<[P9_DIV_40C_8, IP_EXECO_1C, IP_EXECE_1C, DISP_EVEN_1C],
      (instrs
    DIVDE,
    DIVDEO,
    DIVDEU,
    DIVDEUO
)>;

// Cracked DIV and ALU operation. Requires one full slice for the ALU operation
// and one full superslice for the DIV operation since there is only one DIV per
// superslice. Latency of DIV plus ALU is 26.
def : InstRW<[P9_IntDivAndALUOp_18C_8, IP_EXECE_1C, IP_EXECO_1C, IP_EXEC_1C,
              DISP_EVEN_1C, DISP_1C],
      (instrs
    (instregex "DIVW(U)?(O)?_rec$")
)>;

// Cracked DIV and ALU operation. Requires one full slice for the ALU operation
// and one full superslice for the DIV operation since there is only one DIV per
// superslice. Latency of DIV plus ALU is 26.
def : InstRW<[P9_IntDivAndALUOp_26C_8, IP_EXECE_1C, IP_EXECO_1C, IP_EXEC_1C,
              DISP_EVEN_1C, DISP_1C],
      (instrs
    DIVD_rec,
    DIVDO_rec,
    DIVDU_rec,
    DIVDUO_rec,
    DIVWE_rec,
    DIVWEO_rec,
    DIVWEU_rec,
    DIVWEUO_rec
)>;

// Cracked DIV and ALU operation. Requires one full slice for the ALU operation
// and one full superslice for the DIV operation since there is only one DIV per
// superslice. Latency of DIV plus ALU is 42.
def : InstRW<[P9_IntDivAndALUOp_42C_8, IP_EXECE_1C, IP_EXECO_1C, IP_EXEC_1C,
              DISP_EVEN_1C, DISP_1C],
      (instrs
    DIVDE_rec,
    DIVDEO_rec,
    DIVDEU_rec,
    DIVDEUO_rec
)>;

// CR access instructions in _BrMCR, IIC_BrMCRX.

// Cracked, restricted, ALU operations.
// Here the two ALU ops can actually be done in parallel and therefore the
// latencies are not added together. Otherwise this is like having two
// instructions running together on two pipelines and 6 dispatches. ALU ops are
// 2 cycles each.
def : InstRW<[P9_ALU_2C, P9_ALU_2C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_3SLOTS_1C],
      (instrs
    MTCRF,
    MTCRF8
)>;

// Cracked ALU operations.
// Here the two ALU ops can actually be done in parallel and therefore the
// latencies are not added together. Otherwise this is like having two
// instructions running together on two pipelines and 2 dispatches. ALU ops are
// 2 cycles each.
def : InstRW<[P9_ALU_2C, P9_ALU_2C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_1C, DISP_1C],
      (instrs
    (instregex "ADDC(8)?(O)?_rec$"),
    (instregex "SUBFC(8)?(O)?_rec$")
)>;

// Cracked ALU operations.
// Two ALU ops can be done in parallel.
// One is three cycle ALU the ohter is a two cycle ALU.
// One of the ALU ops is restricted the other is not so we have a total of
// 5 dispatches.
def : InstRW<[P9_ALU_2C, P9_ALU_3C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "F(N)?ABS(D|S)_rec$"),
    (instregex "FCPSGN(D|S)_rec$"),
    (instregex "FNEG(D|S)_rec$"),
    FMR_rec
)>;

// Cracked ALU operations.
// Here the two ALU ops can actually be done in parallel and therefore the
// latencies are not added together. Otherwise this is like having two
// instructions running together on two pipelines and 2 dispatches.
// ALU ops are 3 cycles each.
def : InstRW<[P9_ALU_3C, P9_ALU_3C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_1C, DISP_1C],
      (instrs
    MCRFS
)>;

// Cracked Restricted ALU operations.
// Here the two ALU ops can actually be done in parallel and therefore the
// latencies are not added together. Otherwise this is like having two
// instructions running together on two pipelines and 6 dispatches.
// ALU ops are 3 cycles each.
def : InstRW<[P9_ALU_3C, P9_ALU_3C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_3SLOTS_1C],
      (instrs
    (instregex "MTFSF(b|_rec)?$"),
    (instregex "MTFSFI(_rec)?$"),
    MTFSFIb
)>;

// Cracked instruction made of two ALU ops.
// The two ops cannot be done in parallel.
// One of the ALU ops is restricted and takes 3 dispatches.
def : InstRW<[P9_ALUOpAndALUOp_4C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "RLD(I)?C(R|L)_rec$"),
    (instregex "RLW(IMI|INM|NM)(8)?_rec$"),
    (instregex "SLW(8)?_rec$"),
    (instregex "SRAW(I)?_rec$"),
    (instregex "SRW(8)?_rec$"),
    RLDICL_32_rec,
    RLDIMI_rec
)>;

// Cracked instruction made of two ALU ops.
// The two ops cannot be done in parallel.
// Both of the ALU ops are restricted and take 3 dispatches.
def : InstRW<[P9_ALU2OpAndALU2Op_6C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_3SLOTS_1C],
      (instrs
    (instregex "MFFS(L|CE|_rec)?$")
)>;

// Cracked ALU instruction composed of three consecutive 2 cycle loads for a
// total of 6 cycles. All of the ALU operations are also restricted so each
// takes 3 dispatches for a total of 9.
def : InstRW<[P9_ALUOpAndALUOpAndALUOp_6C, IP_EXEC_1C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_3SLOTS_1C, DISP_3SLOTS_1C],
      (instrs
    (instregex "MFCR(8)?$")
)>;

// Cracked instruction made of two ALU ops.
// The two ops cannot be done in parallel.
def : InstRW<[P9_ALUOpAndALUOp_4C, IP_EXEC_1C, IP_EXEC_1C, DISP_1C, DISP_1C],
      (instrs
    (instregex "EXTSWSLI_32_64_rec$"),
    (instregex "SRAD(I)?_rec$"),
    EXTSWSLI_rec,
    SLD_rec,
    SRD_rec,
    RLDIC_rec
)>;

// 33 Cycle DP Instruction Restricted. Takes one slice and 3 dispatches.
def : InstRW<[P9_DP_33C_8, IP_EXEC_1C, DISP_3SLOTS_1C],
      (instrs
    FDIV
)>;

// 33 Cycle DP Instruction Restricted and Cracked with 3 Cycle ALU.
def : InstRW<[P9_DPOpAndALU2Op_36C_8, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    FDIV_rec
)>;

// 36 Cycle DP Instruction.
// Instruction can be done on a single slice.
def : InstRW<[P9_DP_36C_10, IP_EXEC_1C, DISP_1C],
      (instrs
    XSSQRTDP
)>;

// 36 Cycle DP Instruction Restricted. Takes one slice and 3 dispatches.
def : InstRW<[P9_DP_36C_10, IP_EXEC_1C, DISP_3SLOTS_1C],
      (instrs
    FSQRT
)>;

// 36 Cycle DP Vector Instruction.
def : InstRW<[P9_DPE_36C_10, P9_DPO_36C_10, IP_EXECE_1C, IP_EXECO_1C,
              DISP_1C],
      (instrs
    XVSQRTDP
)>;

// 27 Cycle DP Vector Instruction.
def : InstRW<[P9_DPE_27C_10, P9_DPO_27C_10, IP_EXECE_1C, IP_EXECO_1C,
              DISP_1C],
      (instrs
    XVSQRTSP
)>;

// 36 Cycle DP Instruction Restricted and Cracked with 3 Cycle ALU.
def : InstRW<[P9_DPOpAndALU2Op_39C_10, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    FSQRT_rec
)>;

// 26 Cycle DP Instruction.
def : InstRW<[P9_DP_26C_5, IP_EXEC_1C, DISP_1C],
      (instrs
    XSSQRTSP
)>;

// 26 Cycle DP Instruction Restricted. Takes one slice and 3 dispatches.
def : InstRW<[P9_DP_26C_5, IP_EXEC_1C, DISP_3SLOTS_1C],
      (instrs
    FSQRTS
)>;

// 26 Cycle DP Instruction Restricted and Cracked with 3 Cycle ALU.
def : InstRW<[P9_DPOpAndALU2Op_29C_5, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    FSQRTS_rec
)>;

// 33 Cycle DP Instruction. Takes one slice and 1 dispatch.
def : InstRW<[P9_DP_33C_8, IP_EXEC_1C, DISP_1C],
      (instrs
    XSDIVDP
)>;

// 22 Cycle DP Instruction Restricted. Takes one slice and 3 dispatches.
def : InstRW<[P9_DP_22C_5, IP_EXEC_1C, DISP_3SLOTS_1C],
      (instrs
    FDIVS
)>;

// 22 Cycle DP Instruction Restricted and Cracked with 2 Cycle ALU.
def : InstRW<[P9_DPOpAndALU2Op_25C_5, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    FDIVS_rec
)>;

// 22 Cycle DP Instruction. Takes one slice and 1 dispatch.
def : InstRW<[P9_DP_22C_5, IP_EXEC_1C, DISP_1C],
      (instrs
    XSDIVSP
)>;

// 24 Cycle DP Vector Instruction. Takes one full superslice.
// Includes both EXECE, EXECO pipelines and 1 dispatch for the given
// superslice.
def : InstRW<[P9_DPE_24C_8, P9_DPO_24C_8, IP_EXECE_1C, IP_EXECO_1C,
              DISP_1C],
      (instrs
    XVDIVSP
)>;

// 33 Cycle DP Vector Instruction. Takes one full superslice.
// Includes both EXECE, EXECO pipelines and 1 dispatch for the given
// superslice.
def : InstRW<[P9_DPE_33C_8, P9_DPO_33C_8, IP_EXECE_1C, IP_EXECO_1C,
              DISP_1C],
      (instrs
    XVDIVDP
)>;

// Instruction cracked into three pieces. One Load and two ALU operations.
// The Load and one of the ALU ops cannot be run at the same time and so the
// latencies are added together for 6 cycles. The remainaing ALU is 2 cycles.
// Both the load and the ALU that depends on it are restricted and so they take
// a total of 7 dispatches. The final 2 dispatches come from the second ALU op.
// The two EXEC pipelines are for the 2 ALUs while the AGEN is for the load.
def : InstRW<[P9_LoadAndALU2Op_7C, P9_ALU_2C,
              IP_AGEN_1C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "LF(SU|SUX)$")
)>;

// Cracked instruction made up of a Store and an ALU. The ALU does not depend on
// the store and so it can be run at the same time as the store. The store is
// also restricted.
def : InstRW<[P9_LS_1C, P9_ALU_2C, IP_AGEN_1C, IP_EXEC_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "STF(S|D)U(X)?$"),
    (instregex "ST(B|H|W|D)U(X)?(8)?$")
)>;

// Cracked instruction made up of a Load and an ALU. The ALU does not depend on
// the load and so it can be run at the same time as the load.
def : InstRW<[P9_LS_4C, P9_ALU_2C, IP_AGEN_1C, IP_EXEC_1C,
              DISP_PAIR_1C, DISP_PAIR_1C],
      (instrs
    (instregex "LBZU(X)?(8)?$"),
    (instregex "LDU(X)?$")
)>;

// Cracked instruction made up of a Load and an ALU. The ALU does not depend on
// the load and so it can be run at the same time as the load. The load is also
// restricted. 3 dispatches are from the restricted load while the other two
// are from the ALU. The AGEN pipeline is from the load and the EXEC pipeline
// is required for the ALU.
def : InstRW<[P9_LS_4C, P9_ALU_2C, IP_AGEN_1C, IP_EXEC_1C,
              DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "LF(DU|DUX)$")
)>;

// Crypto Instructions

// 6 Cycle CY operation. Only one CY unit per CPU so we use a whole
// superslice. That includes both exec pipelines (EXECO, EXECE) and one
// dispatch.
def : InstRW<[P9_CY_6C, IP_EXECO_1C, IP_EXECE_1C, DISP_1C],
      (instrs
    (instregex "VPMSUM(B|H|W|D)$"),
    (instregex "V(N)?CIPHER(LAST)?$"),
    VSBOX
)>;

// Branch Instructions

// Two Cycle Branch
def : InstRW<[P9_BR_2C, DISP_BR_1C],
      (instrs
  (instregex "BCCCTR(L)?(8)?$"),
  (instregex "BCCL(A|R|RL)?$"),
  (instregex "BCCTR(L)?(8)?(n)?$"),
  (instregex "BD(N)?Z(8|A|Am|Ap|m|p)?$"),
  (instregex "BD(N)?ZL(A|Am|Ap|R|R8|RL|RLm|RLp|Rm|Rp|m|p)?$"),
  (instregex "BL(_TLS|_NOP)?(_RM)?$"),
  (instregex "BL8(_TLS|_NOP|_NOP_TLS|_TLS_)?(_RM)?$"),
  (instregex "BLA(8|8_NOP)?(_RM)?$"),
  (instregex "BLR(8|L)?$"),
  (instregex "TAILB(A)?(8)?$"),
  (instregex "TAILBCTR(8)?$"),
  (instregex "gBC(A|Aat|CTR|CTRL|L|LA|LAat|LR|LRL|Lat|at)?$"),
  (instregex "BCLR(L)?(n)?$"),
  (instregex "BCTR(L)?(8)?(_RM)?$"),
  B,
  BA,
  BC,
  BCC,
  BCCA,
  BCL,
  BCLalways,
  BCLn,
  BCTRL8_LDinto_toc,
  BCTRL_LWZinto_toc,
  BCTRL8_LDinto_toc_RM,
  BCTRL_LWZinto_toc_RM,
  BCn,
  CTRL_DEP
)>;

// Five Cycle Branch with a 2 Cycle ALU Op
// Operations must be done consecutively and not in parallel.
def : InstRW<[P9_BROpAndALUOp_7C, IP_EXEC_1C, DISP_BR_1C, DISP_1C],
      (instrs
    ADDPCIS
)>;

// Special Extracted Instructions For Atomics

// Atomic Load
def : InstRW<[P9_LS_1C, P9_LS_1C, P9_LS_4C, P9_LS_4C, P9_LS_4C,
              IP_EXEC_1C, IP_EXEC_1C, IP_AGEN_1C, IP_AGEN_1C, IP_AGEN_1C,
              IP_AGEN_1C, IP_AGEN_1C, DISP_1C, DISP_3SLOTS_1C, 
              DISP_3SLOTS_1C, DISP_1C, DISP_1C, DISP_1C],
      (instrs
    (instregex "L(D|W)AT$")
)>;

// Atomic Store
def : InstRW<[P9_LS_1C, P9_LS_4C, P9_LS_4C, IP_EXEC_1C, IP_AGEN_1C, IP_AGEN_1C,
              IP_AGEN_1C, DISP_1C, DISP_3SLOTS_1C, DISP_1C],
      (instrs
    (instregex "ST(D|W)AT$")
)>;

// Signal Processing Engine (SPE) Instructions
// These instructions are not supported on Power 9
def : InstRW<[],
    (instrs
  BRINC,
  EVABS,
  EVEQV,
  EVMRA,
  EVNAND,
  EVNEG,
  (instregex "EVADD(I)?W$"),
  (instregex "EVADD(SM|SS|UM|US)IAAW$"),
  (instregex "EVAND(C)?$"),
  (instregex "EVCMP(EQ|GTS|GTU|LTS|LTU)$"),
  (instregex "EVCNTL(S|Z)W$"),
  (instregex "EVDIVW(S|U)$"),
  (instregex "EVEXTS(B|H)$"),
  (instregex "EVLD(H|W|D)(X)?$"),
  (instregex "EVLHH(E|OS|OU)SPLAT(X)?$"),
  (instregex "EVLWHE(X)?$"),
  (instregex "EVLWHO(S|U)(X)?$"),
  (instregex "EVLW(H|W)SPLAT(X)?$"),
  (instregex "EVMERGE(HI|LO|HILO|LOHI)$"),
  (instregex "EVMHEG(S|U)M(F|I)A(A|N)$"),
  (instregex "EVMHES(M|S)(F|I)(A|AA|AAW|ANW)?$"),
  (instregex "EVMHEU(M|S)I(A|AA|AAW|ANW)?$"),
  (instregex "EVMHOG(U|S)M(F|I)A(A|N)$"),
  (instregex "EVMHOS(M|S)(F|I)(A|AA|AAW|ANW)?$"),
  (instregex "EVMHOU(M|S)I(A|AA|ANW|AAW)?$"),
  (instregex "EVMWHS(M|S)(F|FA|I|IA)$"),
  (instregex "EVMWHUMI(A)?$"),
  (instregex "EVMWLS(M|S)IA(A|N)W$"),
  (instregex "EVMWLU(M|S)I(A|AA|AAW|ANW)?$"),
  (instregex "EVMWSM(F|I)(A|AA|AN)?$"),
  (instregex "EVMWSSF(A|AA|AN)?$"),
  (instregex "EVMWUMI(A|AA|AN)?$"),
  (instregex "EV(N|X)?OR(C)?$"),
  (instregex "EVR(LW|LWI|NDW)$"),
  (instregex "EVSLW(I)?$"),
  (instregex "EVSPLAT(F)?I$"),
  (instregex "EVSRW(I)?(S|U)$"),
  (instregex "EVST(DD|DH|DW|WHE|WHO|WWE|WWO)(X)?$"),
  (instregex "EVSUBF(S|U)(M|S)IAAW$"),
  (instregex "EVSUB(I)?FW$")
)> { let Unsupported = 1; }

// General Instructions without scheduling support.
def : InstRW<[],
    (instrs
  (instregex "(H)?RFI(D)?$"),
  (instregex "DSS(ALL)?$"),
  (instregex "DST(ST)?(T)?(64)?$"),
  (instregex "ICBL(C|Q)$"),
  (instregex "L(W|H|B)EPX$"),
  (instregex "ST(W|H|B)EPX$"),
  (instregex "(L|ST)FDEPX$"),
  (instregex "M(T|F)SR(IN)?$"),
  (instregex "M(T|F)DCR$"),
  (instregex "NOP_GT_PWR(6|7)$"),
  (instregex "TLB(IA|IVAX|SX|SX2|SX2D|LD|LI|RE|RE2|WE|WE2)$"),
  (instregex "WRTEE(I)?$"),
  (instregex "HASH(ST|STP|CHK|CHKP)(8)?$"),
  ATTN,
  CLRBHRB,
  MFBHRBE,
  MBAR,
  MSYNC,
  SLBSYNC,
  SLBFEE_rec,
  NAP,
  STOP,
  TRAP,
  RFCI,
  RFDI,
  RFMCI,
  SC,
  DCBA,
  DCBI,
  DCCCI,
  ICCCI,
  ADDEX,
  ADDEX8,
  CDTBCD, CDTBCD8,
  CBCDTD, CBCDTD8,
  ADDG6S, ADDG6S8
)> { let Unsupported = 1; }
