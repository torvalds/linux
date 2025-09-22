//===- README_P9.txt - Notes for improving Power9 code gen ----------------===//

TODO: Instructions Need Implement Instrinstics or Map to LLVM IR

Altivec:
- Vector Compare Not Equal (Zero):
  vcmpneb(.) vcmpneh(.) vcmpnew(.)
  vcmpnezb(.) vcmpnezh(.) vcmpnezw(.)
  . Same as other VCMP*, use VCMP/VCMPo form (support intrinsic)

- Vector Extract Unsigned: vextractub vextractuh vextractuw vextractd
  . Don't use llvm extractelement because they have different semantics
  . Use instrinstics:
    (set v2i64:$vD, (int_ppc_altivec_vextractub v16i8:$vA, imm:$UIMM))
    (set v2i64:$vD, (int_ppc_altivec_vextractuh v8i16:$vA, imm:$UIMM))
    (set v2i64:$vD, (int_ppc_altivec_vextractuw v4i32:$vA, imm:$UIMM))
    (set v2i64:$vD, (int_ppc_altivec_vextractd  v2i64:$vA, imm:$UIMM))

- Vector Extract Unsigned Byte Left/Right-Indexed:
  vextublx vextubrx vextuhlx vextuhrx vextuwlx vextuwrx
  . Use instrinstics:
    // Left-Indexed
    (set i64:$rD, (int_ppc_altivec_vextublx i64:$rA, v16i8:$vB))
    (set i64:$rD, (int_ppc_altivec_vextuhlx i64:$rA, v8i16:$vB))
    (set i64:$rD, (int_ppc_altivec_vextuwlx i64:$rA, v4i32:$vB))

    // Right-Indexed
    (set i64:$rD, (int_ppc_altivec_vextubrx i64:$rA, v16i8:$vB))
    (set i64:$rD, (int_ppc_altivec_vextuhrx i64:$rA, v8i16:$vB))
    (set i64:$rD, (int_ppc_altivec_vextuwrx i64:$rA, v4i32:$vB))

- Vector Insert Element Instructions: vinsertb vinsertd vinserth vinsertw
    (set v16i8:$vD, (int_ppc_altivec_vinsertb v16i8:$vA, imm:$UIMM))
    (set v8i16:$vD, (int_ppc_altivec_vinsertd v8i16:$vA, imm:$UIMM))
    (set v4i32:$vD, (int_ppc_altivec_vinserth v4i32:$vA, imm:$UIMM))
    (set v2i64:$vD, (int_ppc_altivec_vinsertw v2i64:$vA, imm:$UIMM))

- Vector Count Leading/Trailing Zero LSB. Result is placed into GPR[rD]:
  vclzlsbb vctzlsbb
  . Use intrinsic:
    (set i64:$rD, (int_ppc_altivec_vclzlsbb v16i8:$vB))
    (set i64:$rD, (int_ppc_altivec_vctzlsbb v16i8:$vB))

- Vector Count Trailing Zeros: vctzb vctzh vctzw vctzd
  . Map to llvm cttz
    (set v16i8:$vD, (cttz v16i8:$vB))     // vctzb
    (set v8i16:$vD, (cttz v8i16:$vB))     // vctzh
    (set v4i32:$vD, (cttz v4i32:$vB))     // vctzw
    (set v2i64:$vD, (cttz v2i64:$vB))     // vctzd

- Vector Extend Sign: vextsb2w vextsh2w vextsb2d vextsh2d vextsw2d
  . vextsb2w:
    (set v4i32:$vD, (sext v4i8:$vB))

    // PowerISA_V3.0:
    do i = 0 to 3
       VR[VRT].word[i] ← EXTS32(VR[VRB].word[i].byte[3])
    end

  . vextsh2w:
    (set v4i32:$vD, (sext v4i16:$vB))

    // PowerISA_V3.0:
    do i = 0 to 3
       VR[VRT].word[i] ← EXTS32(VR[VRB].word[i].hword[1])
    end

  . vextsb2d
    (set v2i64:$vD, (sext v2i8:$vB))

    // PowerISA_V3.0:
    do i = 0 to 1
       VR[VRT].dword[i] ← EXTS64(VR[VRB].dword[i].byte[7])
    end

  . vextsh2d
    (set v2i64:$vD, (sext v2i16:$vB))

    // PowerISA_V3.0:
    do i = 0 to 1
       VR[VRT].dword[i] ← EXTS64(VR[VRB].dword[i].hword[3])
    end

  . vextsw2d
    (set v2i64:$vD, (sext v2i32:$vB))

    // PowerISA_V3.0:
    do i = 0 to 1
       VR[VRT].dword[i] ← EXTS64(VR[VRB].dword[i].word[1])
    end

- Vector Integer Negate: vnegw vnegd
  . Map to llvm ineg
    (set v4i32:$rT, (ineg v4i32:$rA))       // vnegw
    (set v2i64:$rT, (ineg v2i64:$rA))       // vnegd

- Vector Parity Byte: vprtybw vprtybd vprtybq
  . Use intrinsic:
    (set v4i32:$rD, (int_ppc_altivec_vprtybw v4i32:$vB))
    (set v2i64:$rD, (int_ppc_altivec_vprtybd v2i64:$vB))
    (set v1i128:$rD, (int_ppc_altivec_vprtybq v1i128:$vB))

- Vector (Bit) Permute (Right-indexed):
  . vbpermd: Same as "vbpermq", use VX1_Int_Ty2:
    VX1_Int_Ty2<1484, "vbpermd", int_ppc_altivec_vbpermd, v2i64, v2i64>;

  . vpermr: use VA1a_Int_Ty3
    VA1a_Int_Ty3<59, "vpermr", int_ppc_altivec_vpermr, v16i8, v16i8, v16i8>;

- Vector Rotate Left Mask/Mask-Insert: vrlwnm vrlwmi vrldnm vrldmi
  . Use intrinsic:
    VX1_Int_Ty<389, "vrlwnm", int_ppc_altivec_vrlwnm, v4i32>;
    VX1_Int_Ty<133, "vrlwmi", int_ppc_altivec_vrlwmi, v4i32>;
    VX1_Int_Ty<453, "vrldnm", int_ppc_altivec_vrldnm, v2i64>;
    VX1_Int_Ty<197, "vrldmi", int_ppc_altivec_vrldmi, v2i64>;

- Vector Shift Left/Right: vslv vsrv
  . Use intrinsic, don't map to llvm shl and lshr, because they have different
    semantics, e.g. vslv:

      do i = 0 to 15
         sh ← VR[VRB].byte[i].bit[5:7]
         VR[VRT].byte[i] ← src.byte[i:i+1].bit[sh:sh+7]
      end

    VR[VRT].byte[i] is composed of 2 bytes from src.byte[i:i+1]

  . VX1_Int_Ty<1860, "vslv", int_ppc_altivec_vslv, v16i8>;
    VX1_Int_Ty<1796, "vsrv", int_ppc_altivec_vsrv, v16i8>;

- Vector Multiply-by-10 (& Write Carry) Unsigned Quadword:
  vmul10uq vmul10cuq
  . Use intrinsic:
    VX1_Int_Ty<513, "vmul10uq",   int_ppc_altivec_vmul10uq,  v1i128>;
    VX1_Int_Ty<  1, "vmul10cuq",  int_ppc_altivec_vmul10cuq, v1i128>;

- Vector Multiply-by-10 Extended (& Write Carry) Unsigned Quadword:
  vmul10euq vmul10ecuq
  . Use intrinsic:
    VX1_Int_Ty<577, "vmul10euq",  int_ppc_altivec_vmul10euq, v1i128>;
    VX1_Int_Ty< 65, "vmul10ecuq", int_ppc_altivec_vmul10ecuq, v1i128>;

- Decimal Convert From/to National/Zoned/Signed-QWord:
  bcdcfn. bcdcfz. bcdctn. bcdctz. bcdcfsq. bcdctsq.
  . Use instrinstics:
    (set v1i128:$vD, (int_ppc_altivec_bcdcfno  v1i128:$vB, i1:$PS))
    (set v1i128:$vD, (int_ppc_altivec_bcdcfzo  v1i128:$vB, i1:$PS))
    (set v1i128:$vD, (int_ppc_altivec_bcdctno  v1i128:$vB))
    (set v1i128:$vD, (int_ppc_altivec_bcdctzo  v1i128:$vB, i1:$PS))
    (set v1i128:$vD, (int_ppc_altivec_bcdcfsqo v1i128:$vB, i1:$PS))
    (set v1i128:$vD, (int_ppc_altivec_bcdctsqo v1i128:$vB))

- Decimal Copy-Sign/Set-Sign: bcdcpsgn. bcdsetsgn.
  . Use instrinstics:
    (set v1i128:$vD, (int_ppc_altivec_bcdcpsgno v1i128:$vA, v1i128:$vB))
    (set v1i128:$vD, (int_ppc_altivec_bcdsetsgno v1i128:$vB, i1:$PS))

- Decimal Shift/Unsigned-Shift/Shift-and-Round: bcds. bcdus. bcdsr.
  . Use instrinstics:
    (set v1i128:$vD, (int_ppc_altivec_bcdso  v1i128:$vA, v1i128:$vB, i1:$PS))
    (set v1i128:$vD, (int_ppc_altivec_bcduso v1i128:$vA, v1i128:$vB))
    (set v1i128:$vD, (int_ppc_altivec_bcdsro v1i128:$vA, v1i128:$vB, i1:$PS))

  . Note! Their VA is accessed only 1 byte, i.e. VA.byte[7]

- Decimal (Unsigned) Truncate: bcdtrunc. bcdutrunc.
  . Use instrinstics:
    (set v1i128:$vD, (int_ppc_altivec_bcdso  v1i128:$vA, v1i128:$vB, i1:$PS))
    (set v1i128:$vD, (int_ppc_altivec_bcduso v1i128:$vA, v1i128:$vB))

  . Note! Their VA is accessed only 2 byte, i.e. VA.hword[3] (VA.bit[48:63])

VSX:
- QP Copy Sign: xscpsgnqp
  . Similar to xscpsgndp
  . (set f128:$vT, (fcopysign f128:$vB, f128:$vA)

- QP Absolute/Negative-Absolute/Negate: xsabsqp xsnabsqp xsnegqp
  . Similar to xsabsdp/xsnabsdp/xsnegdp
  . (set f128:$vT, (fabs f128:$vB))             // xsabsqp
    (set f128:$vT, (fneg (fabs f128:$vB)))      // xsnabsqp
    (set f128:$vT, (fneg f128:$vB))             // xsnegqp

- QP Add/Divide/Multiply/Subtract/Square-Root:
  xsaddqp xsdivqp xsmulqp xssubqp xssqrtqp
  . Similar to xsadddp
  . isCommutable = 1
    (set f128:$vT, (fadd f128:$vA, f128:$vB))   // xsaddqp
    (set f128:$vT, (fmul f128:$vA, f128:$vB))   // xsmulqp

  . isCommutable = 0
    (set f128:$vT, (fdiv f128:$vA, f128:$vB))   // xsdivqp
    (set f128:$vT, (fsub f128:$vA, f128:$vB))   // xssubqp
    (set f128:$vT, (fsqrt f128:$vB)))           // xssqrtqp

- Round to Odd of QP Add/Divide/Multiply/Subtract/Square-Root:
  xsaddqpo xsdivqpo xsmulqpo xssubqpo xssqrtqpo
  . Similar to xsrsqrtedp??
      def XSRSQRTEDP : XX2Form<60, 74,
                               (outs vsfrc:$XT), (ins vsfrc:$XB),
                               "xsrsqrtedp $XT, $XB", IIC_VecFP,
                               [(set f64:$XT, (PPCfrsqrte f64:$XB))]>;

  . Define DAG Node in PPCInstrInfo.td:
    def PPCfaddrto: SDNode<"PPCISD::FADDRTO", SDTFPBinOp, []>;
    def PPCfdivrto: SDNode<"PPCISD::FDIVRTO", SDTFPBinOp, []>;
    def PPCfmulrto: SDNode<"PPCISD::FMULRTO", SDTFPBinOp, []>;
    def PPCfsubrto: SDNode<"PPCISD::FSUBRTO", SDTFPBinOp, []>;
    def PPCfsqrtrto: SDNode<"PPCISD::FSQRTRTO", SDTFPUnaryOp, []>;

    DAG patterns of each instruction (PPCInstrVSX.td):
    . isCommutable = 1
      (set f128:$vT, (PPCfaddrto f128:$vA, f128:$vB))   // xsaddqpo
      (set f128:$vT, (PPCfmulrto f128:$vA, f128:$vB))   // xsmulqpo

    . isCommutable = 0
      (set f128:$vT, (PPCfdivrto f128:$vA, f128:$vB))   // xsdivqpo
      (set f128:$vT, (PPCfsubrto f128:$vA, f128:$vB))   // xssubqpo
      (set f128:$vT, (PPCfsqrtrto f128:$vB))            // xssqrtqpo

- QP (Negative) Multiply-{Add/Subtract}: xsmaddqp xsmsubqp xsnmaddqp xsnmsubqp
  . Ref: xsmaddadp/xsmsubadp/xsnmaddadp/xsnmsubadp

  . isCommutable = 1
    // xsmaddqp
    [(set f128:$vT, (fma f128:$vA, f128:$vB, f128:$vTi))]>,
    RegConstraint<"$vTi = $vT">, NoEncode<"$vTi">,
    AltVSXFMARel;

    // xsmsubqp
    [(set f128:$vT, (fma f128:$vA, f128:$vB, (fneg f128:$vTi)))]>,
    RegConstraint<"$vTi = $vT">, NoEncode<"$vTi">,
    AltVSXFMARel;

    // xsnmaddqp
    [(set f128:$vT, (fneg (fma f128:$vA, f128:$vB, f128:$vTi)))]>,
    RegConstraint<"$vTi = $vT">, NoEncode<"$vTi">,
    AltVSXFMARel;

    // xsnmsubqp
    [(set f128:$vT, (fneg (fma f128:$vA, f128:$vB, (fneg f128:$vTi))))]>,
    RegConstraint<"$vTi = $vT">, NoEncode<"$vTi">,
    AltVSXFMARel;

- Round to Odd of QP (Negative) Multiply-{Add/Subtract}:
  xsmaddqpo xsmsubqpo xsnmaddqpo xsnmsubqpo
  . Similar to xsrsqrtedp??

  . Define DAG Node in PPCInstrInfo.td:
    def PPCfmarto: SDNode<"PPCISD::FMARTO", SDTFPTernaryOp, []>;

    It looks like we only need to define "PPCfmarto" for these instructions,
    because according to PowerISA_V3.0, these instructions perform RTO on
    fma's result:
        xsmaddqp(o)
        v      ← bfp_MULTIPLY_ADD(src1, src3, src2)
        rnd    ← bfp_ROUND_TO_BFP128(RO, FPSCR.RN, v)
        result ← bfp_CONVERT_TO_BFP128(rnd)

        xsmsubqp(o)
        v      ← bfp_MULTIPLY_ADD(src1, src3, bfp_NEGATE(src2))
        rnd    ← bfp_ROUND_TO_BFP128(RO, FPSCR.RN, v)
        result ← bfp_CONVERT_TO_BFP128(rnd)

        xsnmaddqp(o)
        v      ← bfp_MULTIPLY_ADD(src1,src3,src2)
        rnd    ← bfp_NEGATE(bfp_ROUND_TO_BFP128(RO, FPSCR.RN, v))
        result ← bfp_CONVERT_TO_BFP128(rnd)

        xsnmsubqp(o)
        v      ← bfp_MULTIPLY_ADD(src1, src3, bfp_NEGATE(src2))
        rnd    ← bfp_NEGATE(bfp_ROUND_TO_BFP128(RO, FPSCR.RN, v))
        result ← bfp_CONVERT_TO_BFP128(rnd)

    DAG patterns of each instruction (PPCInstrVSX.td):
    . isCommutable = 1
      // xsmaddqpo
      [(set f128:$vT, (PPCfmarto f128:$vA, f128:$vB, f128:$vTi))]>,
      RegConstraint<"$vTi = $vT">, NoEncode<"$vTi">,
      AltVSXFMARel;

      // xsmsubqpo
      [(set f128:$vT, (PPCfmarto f128:$vA, f128:$vB, (fneg f128:$vTi)))]>,
      RegConstraint<"$vTi = $vT">, NoEncode<"$vTi">,
      AltVSXFMARel;

      // xsnmaddqpo
      [(set f128:$vT, (fneg (PPCfmarto f128:$vA, f128:$vB, f128:$vTi)))]>,
      RegConstraint<"$vTi = $vT">, NoEncode<"$vTi">,
      AltVSXFMARel;

      // xsnmsubqpo
      [(set f128:$vT, (fneg (PPCfmarto f128:$vA, f128:$vB, (fneg f128:$vTi))))]>,
      RegConstraint<"$vTi = $vT">, NoEncode<"$vTi">,
      AltVSXFMARel;

- QP Compare Ordered/Unordered: xscmpoqp xscmpuqp
  . ref: XSCMPUDP
      def XSCMPUDP : XX3Form_1<60, 35,
                               (outs crrc:$crD), (ins vsfrc:$XA, vsfrc:$XB),
                               "xscmpudp $crD, $XA, $XB", IIC_FPCompare, []>;

  . No SDAG, intrinsic, builtin are required??
    Or llvm fcmp order/unorder compare??

- DP/QP Compare Exponents: xscmpexpdp xscmpexpqp
  . No SDAG, intrinsic, builtin are required?

- DP Compare ==, >=, >, !=: xscmpeqdp xscmpgedp xscmpgtdp xscmpnedp
  . I checked existing instruction "XSCMPUDP". They are different in target
    register. "XSCMPUDP" write to CR field, xscmp*dp write to VSX register

  . Use intrinsic:
    (set i128:$XT, (int_ppc_vsx_xscmpeqdp f64:$XA, f64:$XB))
    (set i128:$XT, (int_ppc_vsx_xscmpgedp f64:$XA, f64:$XB))
    (set i128:$XT, (int_ppc_vsx_xscmpgtdp f64:$XA, f64:$XB))
    (set i128:$XT, (int_ppc_vsx_xscmpnedp f64:$XA, f64:$XB))

- Vector Compare Not Equal: xvcmpnedp xvcmpnedp. xvcmpnesp xvcmpnesp.
  . Similar to xvcmpeqdp:
      defm XVCMPEQDP : XX3Form_Rcr<60, 99,
                                 "xvcmpeqdp", "$XT, $XA, $XB", IIC_VecFPCompare,
                                 int_ppc_vsx_xvcmpeqdp, v2i64, v2f64>;

  . So we should use "XX3Form_Rcr" to implement intrinsic

- Convert DP -> QP: xscvdpqp
  . Similar to XSCVDPSP:
      def XSCVDPSP : XX2Form<60, 265,
                          (outs vsfrc:$XT), (ins vsfrc:$XB),
                          "xscvdpsp $XT, $XB", IIC_VecFP, []>;
  . So, No SDAG, intrinsic, builtin are required??

- Round & Convert QP -> DP (dword[1] is set to zero): xscvqpdp xscvqpdpo
  . Similar to XSCVDPSP
  . No SDAG, intrinsic, builtin are required??

- Truncate & Convert QP -> (Un)Signed (D)Word (dword[1] is set to zero):
  xscvqpsdz xscvqpswz xscvqpudz xscvqpuwz
  . According to PowerISA_V3.0, these are similar to "XSCVDPSXDS", "XSCVDPSXWS",
    "XSCVDPUXDS", "XSCVDPUXWS"

  . DAG patterns:
    (set f128:$XT, (PPCfctidz f128:$XB))    // xscvqpsdz
    (set f128:$XT, (PPCfctiwz f128:$XB))    // xscvqpswz
    (set f128:$XT, (PPCfctiduz f128:$XB))   // xscvqpudz
    (set f128:$XT, (PPCfctiwuz f128:$XB))   // xscvqpuwz

- Convert (Un)Signed DWord -> QP: xscvsdqp xscvudqp
  . Similar to XSCVSXDSP
  . (set f128:$XT, (PPCfcfids f64:$XB))     // xscvsdqp
    (set f128:$XT, (PPCfcfidus f64:$XB))    // xscvudqp

- (Round &) Convert DP <-> HP: xscvdphp xscvhpdp
  . Similar to XSCVDPSP
  . No SDAG, intrinsic, builtin are required??

- Vector HP -> SP: xvcvhpsp xvcvsphp
  . Similar to XVCVDPSP:
      def XVCVDPSP : XX2Form<60, 393,
                          (outs vsrc:$XT), (ins vsrc:$XB),
                          "xvcvdpsp $XT, $XB", IIC_VecFP, []>;
  . No SDAG, intrinsic, builtin are required??

- Round to Quad-Precision Integer: xsrqpi xsrqpix
  . These are combination of "XSRDPI", "XSRDPIC", "XSRDPIM", .., because you
    need to assign rounding mode in instruction
  . Provide builtin?
    (set f128:$vT, (int_ppc_vsx_xsrqpi f128:$vB))
    (set f128:$vT, (int_ppc_vsx_xsrqpix f128:$vB))

- Round Quad-Precision to Double-Extended Precision (fp80): xsrqpxp
  . Provide builtin?
    (set f128:$vT, (int_ppc_vsx_xsrqpxp f128:$vB))

Fixed Point Facility:

- Exploit cmprb and cmpeqb (perhaps for something like
  isalpha/isdigit/isupper/islower and isspace respectivelly). This can
  perhaps be done through a builtin.

- Provide testing for cnttz[dw]
- Insert Exponent DP/QP: xsiexpdp xsiexpqp
  . Use intrinsic?
  . xsiexpdp:
    // Note: rA and rB are the unsigned integer value.
    (set f128:$XT, (int_ppc_vsx_xsiexpdp i64:$rA, i64:$rB))

  . xsiexpqp:
    (set f128:$vT, (int_ppc_vsx_xsiexpqp f128:$vA, f64:$vB))

- Extract Exponent/Significand DP/QP: xsxexpdp xsxsigdp xsxexpqp xsxsigqp
  . Use intrinsic?
  . (set i64:$rT, (int_ppc_vsx_xsxexpdp f64$XB))    // xsxexpdp
    (set i64:$rT, (int_ppc_vsx_xsxsigdp f64$XB))    // xsxsigdp
    (set f128:$vT, (int_ppc_vsx_xsxexpqp f128$vB))  // xsxexpqp
    (set f128:$vT, (int_ppc_vsx_xsxsigqp f128$vB))  // xsxsigqp

- Vector Insert Word: xxinsertw
  - Useful for inserting f32/i32 elements into vectors (the element to be
    inserted needs to be prepared)
  . Note: llvm has insertelem in "Vector Operations"
    ; yields <n x <ty>>
    <result> = insertelement <n x <ty>> <val>, <ty> <elt>, <ty2> <idx>

    But how to map to it??
    [(set v1f128:$XT, (insertelement v1f128:$XTi, f128:$XB, i4:$UIMM))]>,
    RegConstraint<"$XTi = $XT">, NoEncode<"$XTi">,

  . Or use intrinsic?
    (set v1f128:$XT, (int_ppc_vsx_xxinsertw v1f128:$XTi, f128:$XB, i4:$UIMM))

- Vector Extract Unsigned Word: xxextractuw
  - Not useful for extraction of f32 from v4f32 (the current pattern is better -
    shift->convert)
  - It is useful for (uint_to_fp (vector_extract v4i32, N))
  - Unfortunately, it can't be used for (sint_to_fp (vector_extract v4i32, N))
  . Note: llvm has extractelement in "Vector Operations"
    ; yields <ty>
    <result> = extractelement <n x <ty>> <val>, <ty2> <idx>

    How to map to it??
    [(set f128:$XT, (extractelement v1f128:$XB, i4:$UIMM))]

  . Or use intrinsic?
    (set f128:$XT, (int_ppc_vsx_xxextractuw v1f128:$XB, i4:$UIMM))

- Vector Insert Exponent DP/SP: xviexpdp xviexpsp
  . Use intrinsic
    (set v2f64:$XT, (int_ppc_vsx_xviexpdp v2f64:$XA, v2f64:$XB))
    (set v4f32:$XT, (int_ppc_vsx_xviexpsp v4f32:$XA, v4f32:$XB))

- Vector Extract Exponent/Significand DP/SP: xvxexpdp xvxexpsp xvxsigdp xvxsigsp
  . Use intrinsic
    (set v2f64:$XT, (int_ppc_vsx_xvxexpdp v2f64:$XB))
    (set v4f32:$XT, (int_ppc_vsx_xvxexpsp v4f32:$XB))
    (set v2f64:$XT, (int_ppc_vsx_xvxsigdp v2f64:$XB))
    (set v4f32:$XT, (int_ppc_vsx_xvxsigsp v4f32:$XB))

- Test Data Class SP/DP/QP: xststdcsp xststdcdp xststdcqp
  . No SDAG, intrinsic, builtin are required?
    Because it seems that we have no way to map BF field?

    Instruction Form: [PO T XO B XO BX TX]
    Asm: xststd* BF,XB,DCMX

    BF is an index to CR register field.

- Vector Test Data Class SP/DP: xvtstdcsp xvtstdcdp
  . Use intrinsic
    (set v4f32:$XT, (int_ppc_vsx_xvtstdcsp v4f32:$XB, i7:$DCMX))
    (set v2f64:$XT, (int_ppc_vsx_xvtstdcdp v2f64:$XB, i7:$DCMX))

- Maximum/Minimum Type-C/Type-J DP: xsmaxcdp xsmaxjdp xsmincdp xsminjdp
  . PowerISA_V3.0:
    "xsmaxcdp can be used to implement the C/C++/Java conditional operation
     (x>y)?x:y for single-precision and double-precision arguments."

    Note! c type and j type have different behavior when:
    1. Either input is NaN
    2. Both input are +-Infinity, +-Zero

  . dtype map to llvm fmaxnum/fminnum
    jtype use intrinsic

  . xsmaxcdp xsmincdp
    (set f64:$XT, (fmaxnum f64:$XA, f64:$XB))
    (set f64:$XT, (fminnum f64:$XA, f64:$XB))

  . xsmaxjdp xsminjdp
    (set f64:$XT, (int_ppc_vsx_xsmaxjdp f64:$XA, f64:$XB))
    (set f64:$XT, (int_ppc_vsx_xsminjdp f64:$XA, f64:$XB))

- Vector Byte-Reverse H/W/D/Q Word: xxbrh xxbrw xxbrd xxbrq
  . Use intrinsic
    (set v8i16:$XT, (int_ppc_vsx_xxbrh v8i16:$XB))
    (set v4i32:$XT, (int_ppc_vsx_xxbrw v4i32:$XB))
    (set v2i64:$XT, (int_ppc_vsx_xxbrd v2i64:$XB))
    (set v1i128:$XT, (int_ppc_vsx_xxbrq v1i128:$XB))

- Vector Permute: xxperm xxpermr
  . I have checked "PPCxxswapd" in PPCInstrVSX.td, but they are different
  . Use intrinsic
    (set v16i8:$XT, (int_ppc_vsx_xxperm v16i8:$XA, v16i8:$XB))
    (set v16i8:$XT, (int_ppc_vsx_xxpermr v16i8:$XA, v16i8:$XB))

- Vector Splat Immediate Byte: xxspltib
  . Similar to XXSPLTW:
      def XXSPLTW : XX2Form_2<60, 164,
                           (outs vsrc:$XT), (ins vsrc:$XB, u2imm:$UIM),
                           "xxspltw $XT, $XB, $UIM", IIC_VecPerm, []>;

  . No SDAG, intrinsic, builtin are required?

- Load/Store Vector: lxv stxv
  . Has likely SDAG match:
    (set v?:$XT, (load ix16addr:$src))
    (set v?:$XT, (store ix16addr:$dst))

  . Need define ix16addr in PPCInstrInfo.td
    ix16addr: 16-byte aligned, see "def memrix16" in PPCInstrInfo.td

- Load/Store Vector Indexed: lxvx stxvx
  . Has likely SDAG match:
    (set v?:$XT, (load xoaddr:$src))
    (set v?:$XT, (store xoaddr:$dst))

- Load/Store DWord: lxsd stxsd
  . Similar to lxsdx/stxsdx:
    def LXSDX : XX1Form<31, 588,
                        (outs vsfrc:$XT), (ins memrr:$src),
                        "lxsdx $XT, $src", IIC_LdStLFD,
                        [(set f64:$XT, (load xoaddr:$src))]>;

  . (set f64:$XT, (load iaddrX4:$src))
    (set f64:$XT, (store iaddrX4:$dst))

- Load/Store SP, with conversion from/to DP: lxssp stxssp
  . Similar to lxsspx/stxsspx:
    def LXSSPX : XX1Form<31, 524, (outs vssrc:$XT), (ins memrr:$src),
                         "lxsspx $XT, $src", IIC_LdStLFD,
                         [(set f32:$XT, (load xoaddr:$src))]>;

  . (set f32:$XT, (load iaddrX4:$src))
    (set f32:$XT, (store iaddrX4:$dst))

- Load as Integer Byte/Halfword & Zero Indexed: lxsibzx lxsihzx
  . Similar to lxsiwzx:
    def LXSIWZX : XX1Form<31, 12, (outs vsfrc:$XT), (ins memrr:$src),
                          "lxsiwzx $XT, $src", IIC_LdStLFD,
                          [(set f64:$XT, (PPClfiwzx xoaddr:$src))]>;

  . (set f64:$XT, (PPClfiwzx xoaddr:$src))

- Store as Integer Byte/Halfword Indexed: stxsibx stxsihx
  . Similar to stxsiwx:
    def STXSIWX : XX1Form<31, 140, (outs), (ins vsfrc:$XT, memrr:$dst),
                          "stxsiwx $XT, $dst", IIC_LdStSTFD,
                          [(PPCstfiwx f64:$XT, xoaddr:$dst)]>;

  . (PPCstfiwx f64:$XT, xoaddr:$dst)

- Load Vector Halfword*8/Byte*16 Indexed: lxvh8x lxvb16x
  . Similar to lxvd2x/lxvw4x:
    def LXVD2X : XX1Form<31, 844,
                         (outs vsrc:$XT), (ins memrr:$src),
                         "lxvd2x $XT, $src", IIC_LdStLFD,
                         [(set v2f64:$XT, (int_ppc_vsx_lxvd2x xoaddr:$src))]>;

  . (set v8i16:$XT, (int_ppc_vsx_lxvh8x xoaddr:$src))
    (set v16i8:$XT, (int_ppc_vsx_lxvb16x xoaddr:$src))

- Store Vector Halfword*8/Byte*16 Indexed: stxvh8x stxvb16x
  . Similar to stxvd2x/stxvw4x:
    def STXVD2X : XX1Form<31, 972,
                         (outs), (ins vsrc:$XT, memrr:$dst),
                         "stxvd2x $XT, $dst", IIC_LdStSTFD,
                         [(store v2f64:$XT, xoaddr:$dst)]>;

  . (store v8i16:$XT, xoaddr:$dst)
    (store v16i8:$XT, xoaddr:$dst)

- Load/Store Vector (Left-justified) with Length: lxvl lxvll stxvl stxvll
  . Likely needs an intrinsic
  . (set v?:$XT, (int_ppc_vsx_lxvl xoaddr:$src))
    (set v?:$XT, (int_ppc_vsx_lxvll xoaddr:$src))

  . (int_ppc_vsx_stxvl xoaddr:$dst))
    (int_ppc_vsx_stxvll xoaddr:$dst))

- Load Vector Word & Splat Indexed: lxvwsx
  . Likely needs an intrinsic
  . (set v?:$XT, (int_ppc_vsx_lxvwsx xoaddr:$src))

Atomic operations (l[dw]at, st[dw]at):
- Provide custom lowering for common atomic operations to use these
  instructions with the correct Function Code
- Ensure the operands are in the correct register (i.e. RT+1, RT+2)
- Provide builtins since not all FC's necessarily have an existing LLVM
  atomic operation

Move to CR from XER Extended (mcrxrx):
- Is there a use for this in LLVM?

Fixed Point Facility:

- Copy-Paste Facility: copy copy_first cp_abort paste paste. paste_last
  . Use instrinstics:
    (int_ppc_copy_first i32:$rA, i32:$rB)
    (int_ppc_copy i32:$rA, i32:$rB)

    (int_ppc_paste i32:$rA, i32:$rB)
    (int_ppc_paste_last i32:$rA, i32:$rB)

    (int_cp_abort)

- Message Synchronize: msgsync
- SLB*: slbieg slbsync
- stop
  . No instrinstics
