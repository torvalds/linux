//===-- X86InstrFMA.td - FMA Instruction Set ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes FMA (Fused Multiply-Add) instructions.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// FMA3 - Intel 3 operand Fused Multiply-Add instructions
//===----------------------------------------------------------------------===//

// For all FMA opcodes declared in fma3p_rm_* and fma3s_rm_* multiclasses
// defined below, both the register and memory variants are commutable.
// For the register form the commutable operands are 1, 2 and 3.
// For the memory variant the folded operand must be in 3. Thus,
// in that case, only the operands 1 and 2 can be swapped.
// Commuting some of operands may require the opcode change.
// FMA*213*:
//   operands 1 and 2 (memory & register forms): *213* --> *213*(no changes);
//   operands 1 and 3 (register forms only):     *213* --> *231*;
//   operands 2 and 3 (register forms only):     *213* --> *132*.
// FMA*132*:
//   operands 1 and 2 (memory & register forms): *132* --> *231*;
//   operands 1 and 3 (register forms only):     *132* --> *132*(no changes);
//   operands 2 and 3 (register forms only):     *132* --> *213*.
// FMA*231*:
//   operands 1 and 2 (memory & register forms): *231* --> *132*;
//   operands 1 and 3 (register forms only):     *231* --> *213*;
//   operands 2 and 3 (register forms only):     *231* --> *231*(no changes).

multiclass fma3p_rm_213<bits<8> opc, string OpcodeStr, RegisterClass RC,
                        ValueType VT, X86MemOperand x86memop, PatFrag MemFrag,
                        SDPatternOperator Op, X86FoldableSchedWrite sched> {
  def r     : FMA3<opc, MRMSrcReg, (outs RC:$dst),
                   (ins RC:$src1, RC:$src2, RC:$src3),
                   !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                   [(set RC:$dst, (VT (Op RC:$src2, RC:$src1, RC:$src3)))]>,
                   Sched<[sched]>;

  let mayLoad = 1 in
  def m     : FMA3<opc, MRMSrcMem, (outs RC:$dst),
                   (ins RC:$src1, RC:$src2, x86memop:$src3),
                   !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                   [(set RC:$dst, (VT (Op RC:$src2, RC:$src1,
                                          (MemFrag addr:$src3))))]>,
                   Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
}

multiclass fma3p_rm_231<bits<8> opc, string OpcodeStr, RegisterClass RC,
                        ValueType VT, X86MemOperand x86memop, PatFrag MemFrag,
                        SDPatternOperator Op, X86FoldableSchedWrite sched> {
  let hasSideEffects = 0 in
  def r     : FMA3<opc, MRMSrcReg, (outs RC:$dst),
                   (ins RC:$src1, RC:$src2, RC:$src3),
                   !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                   []>, Sched<[sched]>;

  let mayLoad = 1 in
  def m     : FMA3<opc, MRMSrcMem, (outs RC:$dst),
                   (ins RC:$src1, RC:$src2, x86memop:$src3),
                   !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                   [(set RC:$dst, (VT (Op RC:$src2, (MemFrag addr:$src3),
                                          RC:$src1)))]>,
                   Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
}

multiclass fma3p_rm_132<bits<8> opc, string OpcodeStr, RegisterClass RC,
                        ValueType VT, X86MemOperand x86memop, PatFrag MemFrag,
                        SDPatternOperator Op, X86FoldableSchedWrite sched> {
  let hasSideEffects = 0 in
  def r     : FMA3<opc, MRMSrcReg, (outs RC:$dst),
                   (ins RC:$src1, RC:$src2, RC:$src3),
                   !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                   []>, Sched<[sched]>;

  // Pattern is 312 order so that the load is in a different place from the
  // 213 and 231 patterns this helps tablegen's duplicate pattern detection.
  let mayLoad = 1 in
  def m     : FMA3<opc, MRMSrcMem, (outs RC:$dst),
                   (ins RC:$src1, RC:$src2, x86memop:$src3),
                   !strconcat(OpcodeStr,
                              "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                   [(set RC:$dst, (VT (Op (MemFrag addr:$src3), RC:$src1,
                                          RC:$src2)))]>,
                   Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
}

let Constraints = "$src1 = $dst", hasSideEffects = 0, isCommutable = 1,
    Uses = [MXCSR], mayRaiseFPException = 1 in
multiclass fma3p_forms<bits<8> opc132, bits<8> opc213, bits<8> opc231,
                       string OpcodeStr, string PackTy, string Suff,
                       PatFrag MemFrag128, PatFrag MemFrag256,
                       SDPatternOperator Op, ValueType OpTy128, ValueType OpTy256,
                       X86SchedWriteWidths sched> {
  defm NAME#213#Suff : fma3p_rm_213<opc213, !strconcat(OpcodeStr, "213", PackTy),
                                    VR128, OpTy128, f128mem, MemFrag128, Op, sched.XMM>;
  defm NAME#231#Suff : fma3p_rm_231<opc231, !strconcat(OpcodeStr, "231", PackTy),
                                    VR128, OpTy128, f128mem, MemFrag128, Op, sched.XMM>;
  defm NAME#132#Suff : fma3p_rm_132<opc132, !strconcat(OpcodeStr, "132", PackTy),
                                    VR128, OpTy128, f128mem, MemFrag128, Op, sched.XMM>;

  defm NAME#213#Suff#Y : fma3p_rm_213<opc213, !strconcat(OpcodeStr, "213", PackTy),
                                      VR256, OpTy256, f256mem, MemFrag256, Op, sched.YMM>,
                                      VEX_L;
  defm NAME#231#Suff#Y : fma3p_rm_231<opc231, !strconcat(OpcodeStr, "231", PackTy),
                                      VR256, OpTy256, f256mem, MemFrag256, Op, sched.YMM>,
                                      VEX_L;
  defm NAME#132#Suff#Y : fma3p_rm_132<opc132, !strconcat(OpcodeStr, "132", PackTy),
                                      VR256, OpTy256, f256mem, MemFrag256, Op, sched.YMM>,
                                      VEX_L;
}

// Fused Multiply-Add
let ExeDomain = SSEPackedSingle in {
  defm VFMADD    : fma3p_forms<0x98, 0xA8, 0xB8, "vfmadd", "ps", "PS",
                               loadv4f32, loadv8f32, any_fma, v4f32, v8f32,
                               SchedWriteFMA>;
  defm VFMSUB    : fma3p_forms<0x9A, 0xAA, 0xBA, "vfmsub", "ps", "PS",
                               loadv4f32, loadv8f32, X86any_Fmsub, v4f32, v8f32,
                               SchedWriteFMA>;
  defm VFMADDSUB : fma3p_forms<0x96, 0xA6, 0xB6, "vfmaddsub", "ps", "PS",
                               loadv4f32, loadv8f32, X86Fmaddsub, v4f32, v8f32,
                               SchedWriteFMA>;
  defm VFMSUBADD : fma3p_forms<0x97, 0xA7, 0xB7, "vfmsubadd", "ps", "PS",
                               loadv4f32, loadv8f32, X86Fmsubadd, v4f32, v8f32,
                               SchedWriteFMA>;
}

let ExeDomain = SSEPackedDouble in {
  defm VFMADD    : fma3p_forms<0x98, 0xA8, 0xB8, "vfmadd", "pd", "PD",
                               loadv2f64, loadv4f64, any_fma, v2f64,
                               v4f64, SchedWriteFMA>, REX_W;
  defm VFMSUB    : fma3p_forms<0x9A, 0xAA, 0xBA, "vfmsub", "pd", "PD",
                               loadv2f64, loadv4f64, X86any_Fmsub, v2f64,
                               v4f64, SchedWriteFMA>, REX_W;
  defm VFMADDSUB : fma3p_forms<0x96, 0xA6, 0xB6, "vfmaddsub", "pd", "PD",
                               loadv2f64, loadv4f64, X86Fmaddsub,
                               v2f64, v4f64, SchedWriteFMA>, REX_W;
  defm VFMSUBADD : fma3p_forms<0x97, 0xA7, 0xB7, "vfmsubadd", "pd", "PD",
                               loadv2f64, loadv4f64, X86Fmsubadd,
                               v2f64, v4f64, SchedWriteFMA>, REX_W;
}

// Fused Negative Multiply-Add
let ExeDomain = SSEPackedSingle in {
  defm VFNMADD : fma3p_forms<0x9C, 0xAC, 0xBC, "vfnmadd", "ps", "PS", loadv4f32,
                             loadv8f32, X86any_Fnmadd, v4f32, v8f32, SchedWriteFMA>;
  defm VFNMSUB : fma3p_forms<0x9E, 0xAE, 0xBE, "vfnmsub", "ps", "PS", loadv4f32,
                             loadv8f32, X86any_Fnmsub, v4f32, v8f32, SchedWriteFMA>;
}
let ExeDomain = SSEPackedDouble in {
  defm VFNMADD : fma3p_forms<0x9C, 0xAC, 0xBC, "vfnmadd", "pd", "PD", loadv2f64,
                             loadv4f64, X86any_Fnmadd, v2f64, v4f64, SchedWriteFMA>, REX_W;
  defm VFNMSUB : fma3p_forms<0x9E, 0xAE, 0xBE, "vfnmsub", "pd", "PD", loadv2f64,
                             loadv4f64, X86any_Fnmsub, v2f64, v4f64, SchedWriteFMA>, REX_W;
}

// All source register operands of FMA opcodes defined in fma3s_rm multiclass
// can be commuted. In many cases such commute transformation requires an opcode
// adjustment, for example, commuting the operands 1 and 2 in FMA*132 form
// would require an opcode change to FMA*231:
//     FMA*132* reg1, reg2, reg3; // reg1 * reg3 + reg2;
//     -->
//     FMA*231* reg2, reg1, reg3; // reg1 * reg3 + reg2;
// Please see more detailed comment at the very beginning of the section
// defining FMA3 opcodes above.
multiclass fma3s_rm_213<bits<8> opc, string OpcodeStr,
                        X86MemOperand x86memop, RegisterClass RC,
                        SDPatternOperator OpNode,
                        X86FoldableSchedWrite sched> {
  def r : FMA3S<opc, MRMSrcReg, (outs RC:$dst),
                (ins RC:$src1, RC:$src2, RC:$src3),
                !strconcat(OpcodeStr,
                           "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                [(set RC:$dst, (OpNode RC:$src2, RC:$src1, RC:$src3))]>,
                Sched<[sched]>;

  let mayLoad = 1 in
  def m : FMA3S<opc, MRMSrcMem, (outs RC:$dst),
                (ins RC:$src1, RC:$src2, x86memop:$src3),
                !strconcat(OpcodeStr,
                           "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                [(set RC:$dst,
                  (OpNode RC:$src2, RC:$src1, (load addr:$src3)))]>,
                Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
}

multiclass fma3s_rm_231<bits<8> opc, string OpcodeStr,
                        X86MemOperand x86memop, RegisterClass RC,
                        SDPatternOperator OpNode, X86FoldableSchedWrite sched> {
  let hasSideEffects = 0 in
  def r : FMA3S<opc, MRMSrcReg, (outs RC:$dst),
                (ins RC:$src1, RC:$src2, RC:$src3),
                !strconcat(OpcodeStr,
                           "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                []>, Sched<[sched]>;

  let mayLoad = 1 in
  def m : FMA3S<opc, MRMSrcMem, (outs RC:$dst),
                (ins RC:$src1, RC:$src2, x86memop:$src3),
                !strconcat(OpcodeStr,
                           "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                [(set RC:$dst,
                  (OpNode RC:$src2, (load addr:$src3), RC:$src1))]>,
                Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
}

multiclass fma3s_rm_132<bits<8> opc, string OpcodeStr,
                        X86MemOperand x86memop, RegisterClass RC,
                        SDPatternOperator OpNode, X86FoldableSchedWrite sched> {
  let hasSideEffects = 0 in
  def r : FMA3S<opc, MRMSrcReg, (outs RC:$dst),
                (ins RC:$src1, RC:$src2, RC:$src3),
                !strconcat(OpcodeStr,
                           "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                []>, Sched<[sched]>;

  // Pattern is 312 order so that the load is in a different place from the
  // 213 and 231 patterns this helps tablegen's duplicate pattern detection.
  let mayLoad = 1 in
  def m : FMA3S<opc, MRMSrcMem, (outs RC:$dst),
                (ins RC:$src1, RC:$src2, x86memop:$src3),
                !strconcat(OpcodeStr,
                           "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                [(set RC:$dst,
                  (OpNode (load addr:$src3), RC:$src1, RC:$src2))]>,
                Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
}

let Constraints = "$src1 = $dst", isCommutable = 1, isCodeGenOnly = 1,
    hasSideEffects = 0, Uses = [MXCSR], mayRaiseFPException = 1 in
multiclass fma3s_forms<bits<8> opc132, bits<8> opc213, bits<8> opc231,
                       string OpStr, string PackTy, string Suff,
                       SDPatternOperator OpNode, RegisterClass RC,
                       X86MemOperand x86memop, X86FoldableSchedWrite sched> {
  defm NAME#213#Suff : fma3s_rm_213<opc213, !strconcat(OpStr, "213", PackTy),
                                    x86memop, RC, OpNode, sched>;
  defm NAME#231#Suff : fma3s_rm_231<opc231, !strconcat(OpStr, "231", PackTy),
                                    x86memop, RC, OpNode, sched>;
  defm NAME#132#Suff : fma3s_rm_132<opc132, !strconcat(OpStr, "132", PackTy),
                                    x86memop, RC, OpNode, sched>;
}

// These FMA*_Int instructions are defined specially for being used when
// the scalar FMA intrinsics are lowered to machine instructions, and in that
// sense, they are similar to existing ADD*_Int, SUB*_Int, MUL*_Int, etc.
// instructions.
//
// All of the FMA*_Int opcodes are defined as commutable here.
// Commuting the 2nd and 3rd source register operands of FMAs is quite trivial
// and the corresponding optimizations have been developed.
// Commuting the 1st operand of FMA*_Int requires some additional analysis,
// the commute optimization is legal only if all users of FMA*_Int use only
// the lowest element of the FMA*_Int instruction. Even though such analysis
// may be not implemented yet we allow the routines doing the actual commute
// transformation to decide if one or another instruction is commutable or not.
let Constraints = "$src1 = $dst", isCommutable = 1, hasSideEffects = 0,
    Uses = [MXCSR], mayRaiseFPException = 1 in
multiclass fma3s_rm_int<bits<8> opc, string OpcodeStr,
                        Operand memopr, RegisterClass RC,
                        X86FoldableSchedWrite sched> {
  def r_Int : FMA3S_Int<opc, MRMSrcReg, (outs RC:$dst),
                        (ins RC:$src1, RC:$src2, RC:$src3),
                        !strconcat(OpcodeStr,
                                   "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                        []>, Sched<[sched]>;

  let mayLoad = 1 in
  def m_Int : FMA3S_Int<opc, MRMSrcMem, (outs RC:$dst),
                        (ins RC:$src1, RC:$src2, memopr:$src3),
                        !strconcat(OpcodeStr,
                                   "\t{$src3, $src2, $dst|$dst, $src2, $src3}"),
                        []>, Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
}

// The FMA 213 form is created for lowering of scalar FMA intrinsics
// to machine instructions.
// The FMA 132 form can trivially be get by commuting the 2nd and 3rd operands
// of FMA 213 form.
// The FMA 231 form can be get only by commuting the 1st operand of 213 or 132
// forms and is possible only after special analysis of all uses of the initial
// instruction. Such analysis do not exist yet and thus introducing the 231
// form of FMA*_Int instructions is done using an optimistic assumption that
// such analysis will be implemented eventually.
multiclass fma3s_int_forms<bits<8> opc132, bits<8> opc213, bits<8> opc231,
                           string OpStr, string PackTy, string Suff,
                           RegisterClass RC, Operand memop,
                           X86FoldableSchedWrite sched> {
  defm NAME#132#Suff : fma3s_rm_int<opc132, !strconcat(OpStr, "132", PackTy),
                                    memop, RC, sched>;
  defm NAME#213#Suff : fma3s_rm_int<opc213, !strconcat(OpStr, "213", PackTy),
                                    memop, RC, sched>;
  defm NAME#231#Suff : fma3s_rm_int<opc231, !strconcat(OpStr, "231", PackTy),
                                    memop, RC, sched>;
}

multiclass fma3s<bits<8> opc132, bits<8> opc213, bits<8> opc231,
                 string OpStr, SDPatternOperator OpNode, X86FoldableSchedWrite sched> {
  let ExeDomain = SSEPackedSingle in
  defm NAME : fma3s_forms<opc132, opc213, opc231, OpStr, "ss", "SS", OpNode,
                          FR32, f32mem, sched>,
              fma3s_int_forms<opc132, opc213, opc231, OpStr, "ss", "SS",
                              VR128, ssmem, sched>;

  let ExeDomain = SSEPackedDouble in
  defm NAME : fma3s_forms<opc132, opc213, opc231, OpStr, "sd", "SD", OpNode,
                        FR64, f64mem, sched>,
              fma3s_int_forms<opc132, opc213, opc231, OpStr, "sd", "SD",
                              VR128, sdmem, sched>, REX_W;
}

defm VFMADD : fma3s<0x99, 0xA9, 0xB9, "vfmadd", any_fma,
                    SchedWriteFMA.Scl>, VEX_LIG;
defm VFMSUB : fma3s<0x9B, 0xAB, 0xBB, "vfmsub", X86any_Fmsub,
                    SchedWriteFMA.Scl>, VEX_LIG;

defm VFNMADD : fma3s<0x9D, 0xAD, 0xBD, "vfnmadd", X86any_Fnmadd,
                     SchedWriteFMA.Scl>, VEX_LIG;
defm VFNMSUB : fma3s<0x9F, 0xAF, 0xBF, "vfnmsub", X86any_Fnmsub,
                     SchedWriteFMA.Scl>, VEX_LIG;

multiclass scalar_fma_patterns<SDPatternOperator Op, string Prefix, string Suffix,
                               SDNode Move, ValueType VT, ValueType EltVT,
                               RegisterClass RC, PatFrag mem_frag> {
  let Predicates = [HasFMA, NoAVX512] in {
    def : Pat<(VT (Move (VT VR128:$src1), (VT (scalar_to_vector
                (Op RC:$src2,
                    (EltVT (extractelt (VT VR128:$src1), (iPTR 0))),
                    RC:$src3))))),
              (!cast<Instruction>(Prefix#"213"#Suffix#"r_Int")
               VR128:$src1, (VT (COPY_TO_REGCLASS RC:$src2, VR128)),
               (VT (COPY_TO_REGCLASS RC:$src3, VR128)))>;

    def : Pat<(VT (Move (VT VR128:$src1), (VT (scalar_to_vector
                (Op RC:$src2, RC:$src3,
                    (EltVT (extractelt (VT VR128:$src1), (iPTR 0)))))))),
              (!cast<Instruction>(Prefix#"231"#Suffix#"r_Int")
               VR128:$src1, (VT (COPY_TO_REGCLASS RC:$src2, VR128)),
               (VT (COPY_TO_REGCLASS RC:$src3, VR128)))>;

    def : Pat<(VT (Move (VT VR128:$src1), (VT (scalar_to_vector
                (Op RC:$src2,
                    (EltVT (extractelt (VT VR128:$src1), (iPTR 0))),
                    (mem_frag addr:$src3)))))),
              (!cast<Instruction>(Prefix#"213"#Suffix#"m_Int")
               VR128:$src1, (VT (COPY_TO_REGCLASS RC:$src2, VR128)),
               addr:$src3)>;

    def : Pat<(VT (Move (VT VR128:$src1), (VT (scalar_to_vector
                (Op (EltVT (extractelt (VT VR128:$src1), (iPTR 0))),
                    (mem_frag addr:$src3), RC:$src2))))),
              (!cast<Instruction>(Prefix#"132"#Suffix#"m_Int")
               VR128:$src1, (VT (COPY_TO_REGCLASS RC:$src2, VR128)),
               addr:$src3)>;

    def : Pat<(VT (Move (VT VR128:$src1), (VT (scalar_to_vector
                (Op RC:$src2, (mem_frag addr:$src3),
                    (EltVT (extractelt (VT VR128:$src1), (iPTR 0)))))))),
              (!cast<Instruction>(Prefix#"231"#Suffix#"m_Int")
               VR128:$src1, (VT (COPY_TO_REGCLASS RC:$src2, VR128)),
               addr:$src3)>;
  }
}

defm : scalar_fma_patterns<any_fma, "VFMADD", "SS", X86Movss, v4f32, f32, FR32, loadf32>;
defm : scalar_fma_patterns<X86any_Fmsub, "VFMSUB", "SS", X86Movss, v4f32, f32, FR32, loadf32>;
defm : scalar_fma_patterns<X86any_Fnmadd, "VFNMADD", "SS", X86Movss, v4f32, f32, FR32, loadf32>;
defm : scalar_fma_patterns<X86any_Fnmsub, "VFNMSUB", "SS", X86Movss, v4f32, f32, FR32, loadf32>;

defm : scalar_fma_patterns<any_fma, "VFMADD", "SD", X86Movsd, v2f64, f64, FR64, loadf64>;
defm : scalar_fma_patterns<X86any_Fmsub, "VFMSUB", "SD", X86Movsd, v2f64, f64, FR64, loadf64>;
defm : scalar_fma_patterns<X86any_Fnmadd, "VFNMADD", "SD", X86Movsd, v2f64, f64, FR64, loadf64>;
defm : scalar_fma_patterns<X86any_Fnmsub, "VFNMSUB", "SD", X86Movsd, v2f64, f64, FR64, loadf64>;

//===----------------------------------------------------------------------===//
// FMA4 - AMD 4 operand Fused Multiply-Add instructions
//===----------------------------------------------------------------------===//

let Uses = [MXCSR], mayRaiseFPException = 1 in
multiclass fma4s<bits<8> opc, string OpcodeStr, RegisterClass RC,
                 X86MemOperand x86memop, ValueType OpVT, SDPatternOperator OpNode,
                 PatFrag mem_frag, X86FoldableSchedWrite sched> {
  let isCommutable = 1 in
  def rr : FMA4S<opc, MRMSrcRegOp4, (outs RC:$dst),
           (ins RC:$src1, RC:$src2, RC:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set RC:$dst,
             (OpVT (OpNode RC:$src1, RC:$src2, RC:$src3)))]>, REX_W, VEX_LIG,
           Sched<[sched]>;
  def rm : FMA4S<opc, MRMSrcMemOp4, (outs RC:$dst),
           (ins RC:$src1, RC:$src2, x86memop:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set RC:$dst, (OpNode RC:$src1, RC:$src2,
                           (mem_frag addr:$src3)))]>, REX_W, VEX_LIG,
           Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
  def mr : FMA4S<opc, MRMSrcMem, (outs RC:$dst),
           (ins RC:$src1, x86memop:$src2, RC:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set RC:$dst,
             (OpNode RC:$src1, (mem_frag addr:$src2), RC:$src3))]>, VEX_LIG,
           Sched<[sched.Folded, sched.ReadAfterFold,
                  // x86memop:$src2
                  ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                  ReadDefault,
                  // RC:$src3
                  sched.ReadAfterFold]>;
// For disassembler
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in
  def rr_REV : FMA4S<opc, MRMSrcReg, (outs RC:$dst),
               (ins RC:$src1, RC:$src2, RC:$src3),
               !strconcat(OpcodeStr,
               "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"), []>,
               VEX_LIG, Sched<[sched]>;
}

multiclass fma4s_int<bits<8> opc, string OpcodeStr, Operand memop,
                     X86FoldableSchedWrite sched> {
let isCodeGenOnly = 1, hasSideEffects = 0,
    Uses = [MXCSR], mayRaiseFPException = 1 in {
  def rr_Int : FMA4S_Int<opc, MRMSrcRegOp4, (outs VR128:$dst),
               (ins VR128:$src1, VR128:$src2, VR128:$src3),
               !strconcat(OpcodeStr,
               "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
               []>, REX_W, VEX_LIG, Sched<[sched]>;
  let mayLoad = 1 in
  def rm_Int : FMA4S_Int<opc, MRMSrcMemOp4, (outs VR128:$dst),
               (ins VR128:$src1, VR128:$src2, memop:$src3),
               !strconcat(OpcodeStr,
               "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
               []>, REX_W, VEX_LIG,
               Sched<[sched.Folded, sched.ReadAfterFold, sched.ReadAfterFold]>;
  let mayLoad = 1 in
  def mr_Int : FMA4S_Int<opc, MRMSrcMem, (outs VR128:$dst),
               (ins VR128:$src1, memop:$src2, VR128:$src3),
               !strconcat(OpcodeStr,
               "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
               []>,
               VEX_LIG, Sched<[sched.Folded, sched.ReadAfterFold,
                               // memop:$src2
                               ReadDefault, ReadDefault, ReadDefault,
                               ReadDefault, ReadDefault,
                               // VR128::$src3
                               sched.ReadAfterFold]>;
  def rr_Int_REV : FMA4S_Int<opc, MRMSrcReg, (outs VR128:$dst),
               (ins VR128:$src1, VR128:$src2, VR128:$src3),
               !strconcat(OpcodeStr,
               "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
               []>, VEX_LIG, Sched<[sched]>;
} // isCodeGenOnly = 1
}

let Uses = [MXCSR], mayRaiseFPException = 1 in
multiclass fma4p<bits<8> opc, string OpcodeStr, SDPatternOperator OpNode,
                 ValueType OpVT128, ValueType OpVT256,
                 PatFrag ld_frag128, PatFrag ld_frag256,
                 X86SchedWriteWidths sched> {
  let isCommutable = 1 in
  def rr : FMA4<opc, MRMSrcRegOp4, (outs VR128:$dst),
           (ins VR128:$src1, VR128:$src2, VR128:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set VR128:$dst,
             (OpVT128 (OpNode VR128:$src1, VR128:$src2, VR128:$src3)))]>,
           REX_W, Sched<[sched.XMM]>;
  def rm : FMA4<opc, MRMSrcMemOp4, (outs VR128:$dst),
           (ins VR128:$src1, VR128:$src2, f128mem:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set VR128:$dst, (OpNode VR128:$src1, VR128:$src2,
                              (ld_frag128 addr:$src3)))]>, REX_W,
           Sched<[sched.XMM.Folded, sched.XMM.ReadAfterFold, sched.XMM.ReadAfterFold]>;
  def mr : FMA4<opc, MRMSrcMem, (outs VR128:$dst),
           (ins VR128:$src1, f128mem:$src2, VR128:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set VR128:$dst,
             (OpNode VR128:$src1, (ld_frag128 addr:$src2), VR128:$src3))]>,
           Sched<[sched.XMM.Folded, sched.XMM.ReadAfterFold,
                  // f128mem:$src2
                  ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                  ReadDefault,
                  // VR128::$src3
                  sched.XMM.ReadAfterFold]>;
  let isCommutable = 1 in
  def Yrr : FMA4<opc, MRMSrcRegOp4, (outs VR256:$dst),
           (ins VR256:$src1, VR256:$src2, VR256:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set VR256:$dst,
             (OpVT256 (OpNode VR256:$src1, VR256:$src2, VR256:$src3)))]>,
           REX_W, VEX_L, Sched<[sched.YMM]>;
  def Yrm : FMA4<opc, MRMSrcMemOp4, (outs VR256:$dst),
           (ins VR256:$src1, VR256:$src2, f256mem:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set VR256:$dst, (OpNode VR256:$src1, VR256:$src2,
                              (ld_frag256 addr:$src3)))]>, REX_W, VEX_L,
           Sched<[sched.YMM.Folded, sched.YMM.ReadAfterFold, sched.YMM.ReadAfterFold]>;
  def Ymr : FMA4<opc, MRMSrcMem, (outs VR256:$dst),
           (ins VR256:$src1, f256mem:$src2, VR256:$src3),
           !strconcat(OpcodeStr,
           "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"),
           [(set VR256:$dst, (OpNode VR256:$src1,
                              (ld_frag256 addr:$src2), VR256:$src3))]>, VEX_L,
           Sched<[sched.YMM.Folded, sched.YMM.ReadAfterFold,
                  // f256mem:$src2
                  ReadDefault, ReadDefault, ReadDefault, ReadDefault,
                  ReadDefault,
                  // VR256::$src3
                  sched.YMM.ReadAfterFold]>;
// For disassembler
let isCodeGenOnly = 1, ForceDisassemble = 1, hasSideEffects = 0 in {
  def rr_REV : FMA4<opc, MRMSrcReg, (outs VR128:$dst),
               (ins VR128:$src1, VR128:$src2, VR128:$src3),
               !strconcat(OpcodeStr,
               "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"), []>,
               Sched<[sched.XMM]>;
  def Yrr_REV : FMA4<opc, MRMSrcReg, (outs VR256:$dst),
                (ins VR256:$src1, VR256:$src2, VR256:$src3),
                !strconcat(OpcodeStr,
                "\t{$src3, $src2, $src1, $dst|$dst, $src1, $src2, $src3}"), []>,
                VEX_L, Sched<[sched.YMM]>;
} // isCodeGenOnly = 1
}

let ExeDomain = SSEPackedSingle in {
  // Scalar Instructions
  defm VFMADDSS4  : fma4s<0x6A, "vfmaddss", FR32, f32mem, f32, any_fma, loadf32,
                          SchedWriteFMA.Scl>,
                    fma4s_int<0x6A, "vfmaddss", ssmem, SchedWriteFMA.Scl>;
  defm VFMSUBSS4  : fma4s<0x6E, "vfmsubss", FR32, f32mem, f32, X86any_Fmsub, loadf32,
                          SchedWriteFMA.Scl>,
                    fma4s_int<0x6E, "vfmsubss", ssmem, SchedWriteFMA.Scl>;
  defm VFNMADDSS4 : fma4s<0x7A, "vfnmaddss", FR32, f32mem, f32,
                          X86any_Fnmadd, loadf32, SchedWriteFMA.Scl>,
                    fma4s_int<0x7A, "vfnmaddss", ssmem, SchedWriteFMA.Scl>;
  defm VFNMSUBSS4 : fma4s<0x7E, "vfnmsubss", FR32, f32mem, f32,
                          X86any_Fnmsub, loadf32, SchedWriteFMA.Scl>,
                    fma4s_int<0x7E, "vfnmsubss", ssmem, SchedWriteFMA.Scl>;
  // Packed Instructions
  defm VFMADDPS4    : fma4p<0x68, "vfmaddps", any_fma, v4f32, v8f32,
                            loadv4f32, loadv8f32, SchedWriteFMA>;
  defm VFMSUBPS4    : fma4p<0x6C, "vfmsubps", X86any_Fmsub, v4f32, v8f32,
                            loadv4f32, loadv8f32, SchedWriteFMA>;
  defm VFNMADDPS4   : fma4p<0x78, "vfnmaddps", X86any_Fnmadd, v4f32, v8f32,
                            loadv4f32, loadv8f32, SchedWriteFMA>;
  defm VFNMSUBPS4   : fma4p<0x7C, "vfnmsubps", X86any_Fnmsub, v4f32, v8f32,
                            loadv4f32, loadv8f32, SchedWriteFMA>;
  defm VFMADDSUBPS4 : fma4p<0x5C, "vfmaddsubps", X86Fmaddsub, v4f32, v8f32,
                            loadv4f32, loadv8f32, SchedWriteFMA>;
  defm VFMSUBADDPS4 : fma4p<0x5E, "vfmsubaddps", X86Fmsubadd, v4f32, v8f32,
                            loadv4f32, loadv8f32, SchedWriteFMA>;
}

let ExeDomain = SSEPackedDouble in {
  // Scalar Instructions
  defm VFMADDSD4  : fma4s<0x6B, "vfmaddsd", FR64, f64mem, f64, any_fma, loadf64,
                          SchedWriteFMA.Scl>,
                    fma4s_int<0x6B, "vfmaddsd", sdmem, SchedWriteFMA.Scl>;
  defm VFMSUBSD4  : fma4s<0x6F, "vfmsubsd", FR64, f64mem, f64, X86any_Fmsub, loadf64,
                          SchedWriteFMA.Scl>,
                    fma4s_int<0x6F, "vfmsubsd", sdmem, SchedWriteFMA.Scl>;
  defm VFNMADDSD4 : fma4s<0x7B, "vfnmaddsd", FR64, f64mem, f64,
                          X86any_Fnmadd, loadf64, SchedWriteFMA.Scl>,
                    fma4s_int<0x7B, "vfnmaddsd", sdmem, SchedWriteFMA.Scl>;
  defm VFNMSUBSD4 : fma4s<0x7F, "vfnmsubsd", FR64, f64mem, f64,
                          X86any_Fnmsub, loadf64, SchedWriteFMA.Scl>,
                    fma4s_int<0x7F, "vfnmsubsd", sdmem, SchedWriteFMA.Scl>;
  // Packed Instructions
  defm VFMADDPD4    : fma4p<0x69, "vfmaddpd", any_fma, v2f64, v4f64,
                            loadv2f64, loadv4f64, SchedWriteFMA>;
  defm VFMSUBPD4    : fma4p<0x6D, "vfmsubpd", X86any_Fmsub, v2f64, v4f64,
                            loadv2f64, loadv4f64, SchedWriteFMA>;
  defm VFNMADDPD4   : fma4p<0x79, "vfnmaddpd", X86any_Fnmadd, v2f64, v4f64,
                            loadv2f64, loadv4f64, SchedWriteFMA>;
  defm VFNMSUBPD4   : fma4p<0x7D, "vfnmsubpd", X86any_Fnmsub, v2f64, v4f64,
                            loadv2f64, loadv4f64, SchedWriteFMA>;
  defm VFMADDSUBPD4 : fma4p<0x5D, "vfmaddsubpd", X86Fmaddsub, v2f64, v4f64,
                            loadv2f64, loadv4f64, SchedWriteFMA>;
  defm VFMSUBADDPD4 : fma4p<0x5F, "vfmsubaddpd", X86Fmsubadd, v2f64, v4f64,
                            loadv2f64, loadv4f64, SchedWriteFMA>;
}

multiclass scalar_fma4_patterns<SDPatternOperator Op, string Name,
                                ValueType VT, RegisterClass RC,
                                PatFrag mem_frag> {
  let Predicates = [HasFMA4] in {
    def : Pat<(VT (X86vzmovl (VT (scalar_to_vector
                                  (Op RC:$src1, RC:$src2, RC:$src3))))),
              (!cast<Instruction>(Name#"rr_Int")
               (VT (COPY_TO_REGCLASS RC:$src1, VR128)),
               (VT (COPY_TO_REGCLASS RC:$src2, VR128)),
               (VT (COPY_TO_REGCLASS RC:$src3, VR128)))>;

    def : Pat<(VT (X86vzmovl (VT (scalar_to_vector
                                  (Op RC:$src1, RC:$src2,
                                      (mem_frag addr:$src3)))))),
              (!cast<Instruction>(Name#"rm_Int")
               (VT (COPY_TO_REGCLASS RC:$src1, VR128)),
               (VT (COPY_TO_REGCLASS RC:$src2, VR128)), addr:$src3)>;

    def : Pat<(VT (X86vzmovl (VT (scalar_to_vector
                                  (Op RC:$src1, (mem_frag addr:$src2),
                                      RC:$src3))))),
              (!cast<Instruction>(Name#"mr_Int")
               (VT (COPY_TO_REGCLASS RC:$src1, VR128)), addr:$src2,
               (VT (COPY_TO_REGCLASS RC:$src3, VR128)))>;
  }
}

defm : scalar_fma4_patterns<any_fma, "VFMADDSS4", v4f32, FR32, loadf32>;
defm : scalar_fma4_patterns<X86any_Fmsub, "VFMSUBSS4", v4f32, FR32, loadf32>;
defm : scalar_fma4_patterns<X86any_Fnmadd, "VFNMADDSS4", v4f32, FR32, loadf32>;
defm : scalar_fma4_patterns<X86any_Fnmsub, "VFNMSUBSS4", v4f32, FR32, loadf32>;

defm : scalar_fma4_patterns<any_fma, "VFMADDSD4", v2f64, FR64, loadf64>;
defm : scalar_fma4_patterns<X86any_Fmsub, "VFMSUBSD4", v2f64, FR64, loadf64>;
defm : scalar_fma4_patterns<X86any_Fnmadd, "VFNMADDSD4", v2f64, FR64, loadf64>;
defm : scalar_fma4_patterns<X86any_Fnmsub, "VFNMSUBSD4", v2f64, FR64, loadf64>;
