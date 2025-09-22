//===- RISCVInstrInfoVSDPatterns.td - RVV SDNode patterns --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file contains the required infrastructure and SDNode patterns to
/// support code generation for the standard 'V' (Vector) extension, version
/// version 1.0.
///
/// This file is included from and depends upon RISCVInstrInfoVPseudos.td
///
/// Note: the patterns for RVV intrinsics are found in
/// RISCVInstrInfoVPseudos.td.
///
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Helpers to define the SDNode patterns.
//===----------------------------------------------------------------------===//

def rvv_vnot : PatFrag<(ops node:$in),
                       (xor node:$in, (riscv_vmset_vl (XLenVT srcvalue)))>;

multiclass VPatUSLoadStoreSDNode<ValueType type,
                                 int log2sew,
                                 LMULInfo vlmul,
                                 OutPatFrag avl,
                                 VReg reg_class,
                                 int sew = !shl(1, log2sew)> {
  defvar load_instr = !cast<Instruction>("PseudoVLE"#sew#"_V_"#vlmul.MX);
  defvar store_instr = !cast<Instruction>("PseudoVSE"#sew#"_V_"#vlmul.MX);
  // Load
  def : Pat<(type (load GPR:$rs1)),
            (load_instr (type (IMPLICIT_DEF)), GPR:$rs1, avl,
                        log2sew, TA_MA)>;
  // Store
  def : Pat<(store type:$rs2, GPR:$rs1),
            (store_instr reg_class:$rs2, GPR:$rs1, avl, log2sew)>;
}

multiclass VPatUSLoadStoreWholeVRSDNode<ValueType type,
                                        int log2sew,
                                        LMULInfo vlmul,
                                        VReg reg_class,
                                        int sew = !shl(1, log2sew)> {
  defvar load_instr =
    !cast<Instruction>("VL"#!substr(vlmul.MX, 1)#"RE"#sew#"_V");
  defvar store_instr =
    !cast<Instruction>("VS"#!substr(vlmul.MX, 1)#"R_V");

  // Load
  def : Pat<(type (load GPR:$rs1)),
            (load_instr GPR:$rs1)>;
  // Store
  def : Pat<(store type:$rs2, GPR:$rs1),
            (store_instr reg_class:$rs2, GPR:$rs1)>;
}

multiclass VPatUSLoadStoreMaskSDNode<MTypeInfo m> {
  defvar load_instr = !cast<Instruction>("PseudoVLM_V_"#m.BX);
  defvar store_instr = !cast<Instruction>("PseudoVSM_V_"#m.BX);
  // Load
  def : Pat<(m.Mask (load GPR:$rs1)),
            (load_instr (m.Mask (IMPLICIT_DEF)), GPR:$rs1, m.AVL,
                         m.Log2SEW, TA_MA)>;
  // Store
  def : Pat<(store m.Mask:$rs2, GPR:$rs1),
            (store_instr VR:$rs2, GPR:$rs1, m.AVL, m.Log2SEW)>;
}

class VPatBinarySDNode_VV<SDPatternOperator vop,
                          string instruction_name,
                          ValueType result_type,
                          ValueType op_type,
                          int log2sew,
                          LMULInfo vlmul,
                          OutPatFrag avl,
                          VReg op_reg_class,
                          bit isSEWAware = 0> :
    Pat<(result_type (vop
                     (op_type op_reg_class:$rs1),
                     (op_type op_reg_class:$rs2))),
        (!cast<Instruction>(
                     !if(isSEWAware,
                         instruction_name#"_VV_"# vlmul.MX#"_E"#!shl(1, log2sew),
                         instruction_name#"_VV_"# vlmul.MX))
                     (result_type (IMPLICIT_DEF)),
                     op_reg_class:$rs1,
                     op_reg_class:$rs2,
                     avl, log2sew, TA_MA)>;

class VPatBinarySDNode_VV_RM<SDPatternOperator vop,
                             string instruction_name,
                             ValueType result_type,
                             ValueType op_type,
                             int log2sew,
                             LMULInfo vlmul,
                             OutPatFrag avl,
                             VReg op_reg_class,
                             bit isSEWAware = 0> :
    Pat<(result_type (vop
                     (op_type op_reg_class:$rs1),
                     (op_type op_reg_class:$rs2))),
        (!cast<Instruction>(
                     !if(isSEWAware,
                         instruction_name#"_VV_"# vlmul.MX#"_E"#!shl(1, log2sew),
                         instruction_name#"_VV_"# vlmul.MX))
                     (result_type (IMPLICIT_DEF)),
                     op_reg_class:$rs1,
                     op_reg_class:$rs2,
                     // Value to indicate no rounding mode change in
                     // RISCVInsertReadWriteCSR
                     FRM_DYN,
                     avl, log2sew, TA_MA)>;

class VPatBinarySDNode_XI<SDPatternOperator vop,
                          string instruction_name,
                          string suffix,
                          ValueType result_type,
                          ValueType vop_type,
                          int log2sew,
                          LMULInfo vlmul,
                          OutPatFrag avl,
                          VReg vop_reg_class,
                          ComplexPattern SplatPatKind,
                          DAGOperand xop_kind,
                          bit isSEWAware = 0> :
    Pat<(result_type (vop
                     (vop_type vop_reg_class:$rs1),
                     (vop_type (SplatPatKind (XLenVT xop_kind:$rs2))))),
        (!cast<Instruction>(
                     !if(isSEWAware,
                         instruction_name#_#suffix#_# vlmul.MX#"_E"#!shl(1, log2sew),
                         instruction_name#_#suffix#_# vlmul.MX))
                     (result_type (IMPLICIT_DEF)),
                     vop_reg_class:$rs1,
                     xop_kind:$rs2,
                     avl, log2sew, TA_MA)>;

multiclass VPatBinarySDNode_VV_VX<SDPatternOperator vop, string instruction_name,
                                  list<VTypeInfo> vtilist = AllIntegerVectors,
                                  bit isSEWAware = 0> {
  foreach vti = vtilist in {
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : VPatBinarySDNode_VV<vop, instruction_name,
                                vti.Vector, vti.Vector, vti.Log2SEW,
                                vti.LMul, vti.AVL, vti.RegClass, isSEWAware>;
      def : VPatBinarySDNode_XI<vop, instruction_name, "VX",
                                vti.Vector, vti.Vector, vti.Log2SEW,
                                vti.LMul, vti.AVL, vti.RegClass,
                                SplatPat, GPR, isSEWAware>;
    }
  }
}

multiclass VPatBinarySDNode_VV_VX_VI<SDPatternOperator vop, string instruction_name,
                                     Operand ImmType = simm5>
    : VPatBinarySDNode_VV_VX<vop, instruction_name> {
  foreach vti = AllIntegerVectors in {
    let Predicates = GetVTypePredicates<vti>.Predicates in
    def : VPatBinarySDNode_XI<vop, instruction_name, "VI",
                              vti.Vector, vti.Vector, vti.Log2SEW,
                              vti.LMul, vti.AVL, vti.RegClass,
                              !cast<ComplexPattern>(SplatPat#_#ImmType),
                              ImmType>;
  }
}

class VPatBinarySDNode_VF<SDPatternOperator vop,
                          string instruction_name,
                          ValueType result_type,
                          ValueType vop_type,
                          ValueType xop_type,
                          int log2sew,
                          LMULInfo vlmul,
                          OutPatFrag avl,
                          VReg vop_reg_class,
                          DAGOperand xop_kind,
                          bit isSEWAware = 0> :
    Pat<(result_type (vop (vop_type vop_reg_class:$rs1),
                          (vop_type (SplatFPOp xop_kind:$rs2)))),
        (!cast<Instruction>(
                     !if(isSEWAware,
                         instruction_name#"_"#vlmul.MX#"_E"#!shl(1, log2sew),
                         instruction_name#"_"#vlmul.MX))
                     (result_type (IMPLICIT_DEF)),
                     vop_reg_class:$rs1,
                     (xop_type xop_kind:$rs2),
                     avl, log2sew, TA_MA)>;

class VPatBinarySDNode_VF_RM<SDPatternOperator vop,
                             string instruction_name,
                             ValueType result_type,
                             ValueType vop_type,
                             ValueType xop_type,
                             int log2sew,
                             LMULInfo vlmul,
                             OutPatFrag avl,
                             VReg vop_reg_class,
                             DAGOperand xop_kind,
                             bit isSEWAware = 0> :
    Pat<(result_type (vop (vop_type vop_reg_class:$rs1),
                          (vop_type (SplatFPOp xop_kind:$rs2)))),
        (!cast<Instruction>(
                     !if(isSEWAware,
                         instruction_name#"_"#vlmul.MX#"_E"#!shl(1, log2sew),
                         instruction_name#"_"#vlmul.MX))
                     (result_type (IMPLICIT_DEF)),
                     vop_reg_class:$rs1,
                     (xop_type xop_kind:$rs2),
                     // Value to indicate no rounding mode change in
                     // RISCVInsertReadWriteCSR
                     FRM_DYN,
                     avl, log2sew, TA_MA)>;

multiclass VPatBinaryFPSDNode_VV_VF<SDPatternOperator vop, string instruction_name,
                                    bit isSEWAware = 0> {
  foreach vti = AllFloatVectors in {
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : VPatBinarySDNode_VV<vop, instruction_name,
                                vti.Vector, vti.Vector, vti.Log2SEW,
                                vti.LMul, vti.AVL, vti.RegClass, isSEWAware>;
      def : VPatBinarySDNode_VF<vop, instruction_name#"_V"#vti.ScalarSuffix,
                                vti.Vector, vti.Vector, vti.Scalar,
                                vti.Log2SEW, vti.LMul, vti.AVL, vti.RegClass,
                                vti.ScalarRegClass, isSEWAware>;
    }
  }
}

multiclass VPatBinaryFPSDNode_VV_VF_RM<SDPatternOperator vop, string instruction_name,
                                       bit isSEWAware = 0> {
  foreach vti = AllFloatVectors in {
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : VPatBinarySDNode_VV_RM<vop, instruction_name,
                                   vti.Vector, vti.Vector, vti.Log2SEW,
                                   vti.LMul, vti.AVL, vti.RegClass, isSEWAware>;
      def : VPatBinarySDNode_VF_RM<vop, instruction_name#"_V"#vti.ScalarSuffix,
                                   vti.Vector, vti.Vector, vti.Scalar,
                                   vti.Log2SEW, vti.LMul, vti.AVL, vti.RegClass,
                                   vti.ScalarRegClass, isSEWAware>;
    }
  }
}

multiclass VPatBinaryFPSDNode_R_VF<SDPatternOperator vop, string instruction_name,
                                   bit isSEWAware = 0> {
  foreach fvti = AllFloatVectors in
    let Predicates = GetVTypePredicates<fvti>.Predicates in
    def : Pat<(fvti.Vector (vop (fvti.Vector (SplatFPOp fvti.Scalar:$rs2)),
                                (fvti.Vector fvti.RegClass:$rs1))),
              (!cast<Instruction>(
                           !if(isSEWAware,
                             instruction_name#"_V"#fvti.ScalarSuffix#"_"#fvti.LMul.MX#"_E"#fvti.SEW,
                             instruction_name#"_V"#fvti.ScalarSuffix#"_"#fvti.LMul.MX))
                           (fvti.Vector (IMPLICIT_DEF)),
                           fvti.RegClass:$rs1,
                           (fvti.Scalar fvti.ScalarRegClass:$rs2),
                           fvti.AVL, fvti.Log2SEW, TA_MA)>;
}

multiclass VPatBinaryFPSDNode_R_VF_RM<SDPatternOperator vop, string instruction_name,
                                   bit isSEWAware = 0> {
  foreach fvti = AllFloatVectors in
    let Predicates = GetVTypePredicates<fvti>.Predicates in
    def : Pat<(fvti.Vector (vop (fvti.Vector (SplatFPOp fvti.Scalar:$rs2)),
                                (fvti.Vector fvti.RegClass:$rs1))),
              (!cast<Instruction>(
                           !if(isSEWAware,
                             instruction_name#"_V"#fvti.ScalarSuffix#"_"#fvti.LMul.MX#"_E"#fvti.SEW,
                             instruction_name#"_V"#fvti.ScalarSuffix#"_"#fvti.LMul.MX))
                           (fvti.Vector (IMPLICIT_DEF)),
                           fvti.RegClass:$rs1,
                           (fvti.Scalar fvti.ScalarRegClass:$rs2),
                           // Value to indicate no rounding mode change in
                           // RISCVInsertReadWriteCSR
                           FRM_DYN,
                           fvti.AVL, fvti.Log2SEW, TA_MA)>;
}

multiclass VPatIntegerSetCCSDNode_VV<string instruction_name,
                                     CondCode cc> {
  foreach vti = AllIntegerVectors in {
    defvar instruction = !cast<Instruction>(instruction_name#"_VV_"#vti.LMul.MX);
    let Predicates = GetVTypePredicates<vti>.Predicates in
    def : Pat<(vti.Mask (setcc (vti.Vector vti.RegClass:$rs1),
                               (vti.Vector vti.RegClass:$rs2), cc)),
              (instruction vti.RegClass:$rs1, vti.RegClass:$rs2, vti.AVL,
              vti.Log2SEW)>;
  }
}

multiclass VPatIntegerSetCCSDNode_VV_Swappable<string instruction_name,
                                               CondCode cc, CondCode invcc>
    : VPatIntegerSetCCSDNode_VV<instruction_name, cc> {
  foreach vti = AllIntegerVectors in {
    defvar instruction = !cast<Instruction>(instruction_name#"_VV_"#vti.LMul.MX);
    let Predicates = GetVTypePredicates<vti>.Predicates in
    def : Pat<(vti.Mask (setcc (vti.Vector vti.RegClass:$rs2),
                               (vti.Vector vti.RegClass:$rs1), invcc)),
              (instruction vti.RegClass:$rs1, vti.RegClass:$rs2, vti.AVL,
              vti.Log2SEW)>;
  }
}

multiclass VPatIntegerSetCCSDNode_XI_Swappable<string instruction_name,
                                               CondCode cc, CondCode invcc,
                                               string kind,
                                               ComplexPattern SplatPatKind,
                                               DAGOperand xop_kind> {
  foreach vti = AllIntegerVectors in {
    defvar instruction = !cast<Instruction>(instruction_name#_#kind#_#vti.LMul.MX);
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : Pat<(vti.Mask (setcc (vti.Vector vti.RegClass:$rs1),
                                 (vti.Vector (SplatPatKind (XLenVT xop_kind:$rs2))), cc)),
                (instruction vti.RegClass:$rs1, xop_kind:$rs2, vti.AVL, vti.Log2SEW)>;
      def : Pat<(vti.Mask (setcc (vti.Vector (SplatPatKind (XLenVT xop_kind:$rs2))),
                                 (vti.Vector vti.RegClass:$rs1), invcc)),
                (instruction vti.RegClass:$rs1, xop_kind:$rs2, vti.AVL, vti.Log2SEW)>;
    }
  }
}

multiclass VPatIntegerSetCCSDNode_VX_Swappable<string instruction_name,
                                               CondCode cc, CondCode invcc>
    : VPatIntegerSetCCSDNode_XI_Swappable<instruction_name, cc, invcc, "VX",
                                          SplatPat, GPR>;

multiclass VPatIntegerSetCCSDNode_VI_Swappable<string instruction_name,
                                               CondCode cc, CondCode invcc>
    : VPatIntegerSetCCSDNode_XI_Swappable<instruction_name, cc, invcc, "VI",
                                          SplatPat_simm5, simm5>;

multiclass VPatIntegerSetCCSDNode_VIPlus1_Swappable<string instruction_name,
                                                    CondCode cc, CondCode invcc,
                                                    ComplexPattern splatpat_kind> {
  foreach vti = AllIntegerVectors in {
    defvar instruction = !cast<Instruction>(instruction_name#"_VI_"#vti.LMul.MX);
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : Pat<(vti.Mask (setcc (vti.Vector vti.RegClass:$rs1),
                                 (vti.Vector (splatpat_kind simm5:$rs2)),
                                 cc)),
                (instruction vti.RegClass:$rs1, (DecImm simm5:$rs2),
                             vti.AVL, vti.Log2SEW)>;
      def : Pat<(vti.Mask (setcc (vti.Vector (splatpat_kind simm5:$rs2)),
                                 (vti.Vector vti.RegClass:$rs1),
                                 invcc)),
                (instruction vti.RegClass:$rs1, (DecImm simm5:$rs2),
                             vti.AVL, vti.Log2SEW)>;
    }
  }
}

multiclass VPatFPSetCCSDNode_VV_VF_FV<CondCode cc,
                                      string inst_name,
                                      string swapped_op_inst_name> {
  foreach fvti = AllFloatVectors in {
    let Predicates = GetVTypePredicates<fvti>.Predicates in {
      def : Pat<(fvti.Mask (setcc (fvti.Vector fvti.RegClass:$rs1),
                                  (fvti.Vector fvti.RegClass:$rs2),
                                  cc)),
                (!cast<Instruction>(inst_name#"_VV_"#fvti.LMul.MX)
                    fvti.RegClass:$rs1, fvti.RegClass:$rs2, fvti.AVL, fvti.Log2SEW)>;
      def : Pat<(fvti.Mask (setcc (fvti.Vector fvti.RegClass:$rs1),
                                  (SplatFPOp fvti.ScalarRegClass:$rs2),
                                  cc)),
                (!cast<Instruction>(inst_name#"_V"#fvti.ScalarSuffix#"_"#fvti.LMul.MX)
                    fvti.RegClass:$rs1, fvti.ScalarRegClass:$rs2,
                    fvti.AVL, fvti.Log2SEW)>;
      def : Pat<(fvti.Mask (setcc (SplatFPOp fvti.ScalarRegClass:$rs2),
                                  (fvti.Vector fvti.RegClass:$rs1),
                                  cc)),
                (!cast<Instruction>(swapped_op_inst_name#"_V"#fvti.ScalarSuffix#"_"#fvti.LMul.MX)
                    fvti.RegClass:$rs1, fvti.ScalarRegClass:$rs2,
                    fvti.AVL, fvti.Log2SEW)>;
    }
  }
}

multiclass VPatExtendSDNode_V<list<SDNode> ops, string inst_name, string suffix,
                              list <VTypeInfoToFraction> fraction_list> {
  foreach vtiTofti = fraction_list in {
    defvar vti = vtiTofti.Vti;
    defvar fti = vtiTofti.Fti;
    foreach op = ops in
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<fti>.Predicates) in
      def : Pat<(vti.Vector (op (fti.Vector fti.RegClass:$rs2))),
                (!cast<Instruction>(inst_name#"_"#suffix#"_"#vti.LMul.MX)
                    (vti.Vector (IMPLICIT_DEF)),
                    fti.RegClass:$rs2, fti.AVL, vti.Log2SEW, TA_MA)>;
  }
}

multiclass VPatConvertI2FPSDNode_V_RM<SDPatternOperator vop,
                                      string instruction_name> {
  foreach fvti = AllFloatVectors in {
    defvar ivti = GetIntVTypeInfo<fvti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<ivti>.Predicates) in
    def : Pat<(fvti.Vector (vop (ivti.Vector ivti.RegClass:$rs1))),
              (!cast<Instruction>(instruction_name#"_"#fvti.LMul.MX#"_E"#fvti.SEW)
                  (fvti.Vector (IMPLICIT_DEF)),
                  ivti.RegClass:$rs1,
                  // Value to indicate no rounding mode change in
                  // RISCVInsertReadWriteCSR
                  FRM_DYN,
                  fvti.AVL, fvti.Log2SEW, TA_MA)>;
  }
}

multiclass VPatConvertFP2ISDNode_V<SDPatternOperator vop,
                                   string instruction_name> {
  foreach fvti = AllFloatVectors in {
    defvar ivti = GetIntVTypeInfo<fvti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<ivti>.Predicates) in
    def : Pat<(ivti.Vector (vop (fvti.Vector fvti.RegClass:$rs1))),
              (!cast<Instruction>(instruction_name#"_"#ivti.LMul.MX)
                  (ivti.Vector (IMPLICIT_DEF)),
                  fvti.RegClass:$rs1, ivti.AVL, ivti.Log2SEW, TA_MA)>;
  }
}

multiclass VPatWConvertI2FPSDNode_V<SDPatternOperator vop,
                                       string instruction_name> {
  foreach vtiToWti = AllWidenableIntToFloatVectors in {
    defvar ivti = vtiToWti.Vti;
    defvar fwti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<ivti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    def : Pat<(fwti.Vector (vop (ivti.Vector ivti.RegClass:$rs1))),
              (!cast<Instruction>(instruction_name#"_"#ivti.LMul.MX#"_E"#ivti.SEW)
                  (fwti.Vector (IMPLICIT_DEF)),
                  ivti.RegClass:$rs1,
                  ivti.AVL, ivti.Log2SEW, TA_MA)>;
  }
}

multiclass VPatWConvertFP2ISDNode_V<SDPatternOperator vop,
                                    string instruction_name> {
  foreach fvtiToFWti = AllWidenableFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar iwti = GetIntVTypeInfo<fvtiToFWti.Wti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<iwti>.Predicates) in
    def : Pat<(iwti.Vector (vop (fvti.Vector fvti.RegClass:$rs1))),
              (!cast<Instruction>(instruction_name#"_"#fvti.LMul.MX)
                  (iwti.Vector (IMPLICIT_DEF)),
                  fvti.RegClass:$rs1, fvti.AVL, fvti.Log2SEW, TA_MA)>;
  }
}

multiclass VPatNConvertI2FPSDNode_W_RM<SDPatternOperator vop,
                                       string instruction_name> {
  foreach fvtiToFWti = AllWidenableFloatVectors in {
    defvar fvti = fvtiToFWti.Vti;
    defvar iwti = GetIntVTypeInfo<fvtiToFWti.Wti>.Vti;
    let Predicates = !listconcat(GetVTypePredicates<fvti>.Predicates,
                                 GetVTypePredicates<iwti>.Predicates) in
    def : Pat<(fvti.Vector (vop (iwti.Vector iwti.RegClass:$rs1))),
              (!cast<Instruction>(instruction_name#"_"#fvti.LMul.MX#"_E"#fvti.SEW)
                  (fvti.Vector (IMPLICIT_DEF)),
                  iwti.RegClass:$rs1,
                  // Value to indicate no rounding mode change in
                  // RISCVInsertReadWriteCSR
                  FRM_DYN,
                  fvti.AVL, fvti.Log2SEW, TA_MA)>;
  }
}

multiclass VPatNConvertFP2ISDNode_W<SDPatternOperator vop,
                                    string instruction_name> {
  foreach vtiToWti = AllWidenableIntToFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar fwti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<fwti>.Predicates) in
    def : Pat<(vti.Vector (vop (fwti.Vector fwti.RegClass:$rs1))),
              (!cast<Instruction>(instruction_name#"_"#vti.LMul.MX)
                  (vti.Vector (IMPLICIT_DEF)),
                  fwti.RegClass:$rs1, vti.AVL, vti.Log2SEW, TA_MA)>;
  }
}

multiclass VPatWidenBinarySDNode_VV_VX<SDNode op, PatFrags extop1, PatFrags extop2,
                                       string instruction_name> {
  foreach vtiToWti = AllWidenableIntVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(op (wti.Vector (extop1 (vti.Vector vti.RegClass:$rs2))),
                    (wti.Vector (extop2 (vti.Vector vti.RegClass:$rs1)))),
                (!cast<Instruction>(instruction_name#"_VV_"#vti.LMul.MX)
                   (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs2,
                   vti.RegClass:$rs1, vti.AVL, vti.Log2SEW, TA_MA)>;
      def : Pat<(op (wti.Vector (extop1 (vti.Vector vti.RegClass:$rs2))),
                    (wti.Vector (extop2 (vti.Vector (SplatPat (XLenVT GPR:$rs1)))))),
                (!cast<Instruction>(instruction_name#"_VX_"#vti.LMul.MX)
                   (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs2,
                   GPR:$rs1, vti.AVL, vti.Log2SEW, TA_MA)>;
    }
  }
}

multiclass VPatWidenBinarySDNode_WV_WX<SDNode op, PatFrags extop,
                                       string instruction_name> {
  foreach vtiToWti = AllWidenableIntVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(op (wti.Vector wti.RegClass:$rs2),
                    (wti.Vector (extop (vti.Vector vti.RegClass:$rs1)))),
                (!cast<Instruction>(instruction_name#"_WV_"#vti.LMul.MX#"_TIED")
                   wti.RegClass:$rs2, vti.RegClass:$rs1, vti.AVL, vti.Log2SEW,
                   TAIL_AGNOSTIC)>;
      def : Pat<(op (wti.Vector wti.RegClass:$rs2),
                    (wti.Vector (extop (vti.Vector (SplatPat (XLenVT GPR:$rs1)))))),
                (!cast<Instruction>(instruction_name#"_WX_"#vti.LMul.MX)
                   (wti.Vector (IMPLICIT_DEF)), wti.RegClass:$rs2, GPR:$rs1,
                   vti.AVL, vti.Log2SEW, TA_MA)>;
    }
  }
}

multiclass VPatWidenBinarySDNode_VV_VX_WV_WX<SDNode op, PatFrags extop,
                                             string instruction_name>
    : VPatWidenBinarySDNode_VV_VX<op, extop, extop, instruction_name>,
      VPatWidenBinarySDNode_WV_WX<op, extop, instruction_name>;

multiclass VPatWidenMulAddSDNode_VV<PatFrags extop1, PatFrags extop2, string instruction_name> {
  foreach vtiToWti = AllWidenableIntVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in
    def : Pat<
      (add (wti.Vector wti.RegClass:$rd),
        (mul_oneuse (wti.Vector (extop1 (vti.Vector vti.RegClass:$rs1))),
                    (wti.Vector (extop2 (vti.Vector vti.RegClass:$rs2))))),
      (!cast<Instruction>(instruction_name#"_VV_"#vti.LMul.MX)
        wti.RegClass:$rd, vti.RegClass:$rs1, vti.RegClass:$rs2,
        vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC
      )>;
  }
}
multiclass VPatWidenMulAddSDNode_VX<PatFrags extop1, PatFrags extop2, string instruction_name> {
  foreach vtiToWti = AllWidenableIntVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in
    def : Pat<
      (add (wti.Vector wti.RegClass:$rd),
        (mul_oneuse (wti.Vector (extop1 (vti.Vector (SplatPat (XLenVT GPR:$rs1))))),
                    (wti.Vector (extop2 (vti.Vector vti.RegClass:$rs2))))),
      (!cast<Instruction>(instruction_name#"_VX_"#vti.LMul.MX)
        wti.RegClass:$rd, GPR:$rs1, vti.RegClass:$rs2,
        vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC
      )>;
  }
}

multiclass VPatWidenBinaryFPSDNode_VV_VF<SDNode op, string instruction_name> {
  foreach vtiToWti = AllWidenableFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(op (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs2),
                                     (vti.Mask true_mask), (XLenVT srcvalue))),
                    (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs1),
                                     (vti.Mask true_mask), (XLenVT srcvalue)))),
                (!cast<Instruction>(instruction_name#"_VV_"#vti.LMul.MX)
                  (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs2,
                  vti.RegClass:$rs1, vti.AVL, vti.Log2SEW, TA_MA)>;
      def : Pat<(op (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs2),
                                     (vti.Mask true_mask), (XLenVT srcvalue))),
                    (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector (SplatFPOp vti.ScalarRegClass:$rs1)),
                                     (vti.Mask true_mask), (XLenVT srcvalue)))),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#vti.LMul.MX)
                   (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs2,
                   vti.ScalarRegClass:$rs1, vti.AVL, vti.Log2SEW, TA_MA)>;
      def : Pat<(op (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs2),
                                     (vti.Mask true_mask), (XLenVT srcvalue))),
                    (wti.Vector (SplatFPOp (fpext_oneuse vti.ScalarRegClass:$rs1)))),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#vti.LMul.MX)
                   (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs2,
                   vti.ScalarRegClass:$rs1, vti.AVL, vti.Log2SEW, TA_MA)>;
    }
  }
}

multiclass VPatWidenBinaryFPSDNode_VV_VF_RM<SDNode op, string instruction_name> {
  foreach vtiToWti = AllWidenableFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(op (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs2),
                                     (vti.Mask true_mask), (XLenVT srcvalue))),
                    (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs1),
                                     (vti.Mask true_mask), (XLenVT srcvalue)))),
                (!cast<Instruction>(instruction_name#"_VV_"#vti.LMul.MX#"_E"#vti.SEW)
                  (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs2,
                  vti.RegClass:$rs1,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                  vti.AVL, vti.Log2SEW, TA_MA)>;
      def : Pat<(op (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs2),
                                     (vti.Mask true_mask), (XLenVT srcvalue))),
                    (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector (SplatFPOp (vti.Scalar vti.ScalarRegClass:$rs1))),
                                     (vti.Mask true_mask), (XLenVT srcvalue)))),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#vti.LMul.MX#"_E"#vti.SEW)
                   (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs2,
                   vti.ScalarRegClass:$rs1,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TA_MA)>;
      def : Pat<(op (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs2),
                                     (vti.Mask true_mask), (XLenVT srcvalue))),
                    (wti.Vector (SplatFPOp (fpext_oneuse (vti.Scalar vti.ScalarRegClass:$rs1))))),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#vti.LMul.MX#"_E"#vti.SEW)
                   (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs2,
                   vti.ScalarRegClass:$rs1,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TA_MA)>;
    }
  }
}

multiclass VPatWidenBinaryFPSDNode_WV_WF_RM<SDNode op, string instruction_name> {
  foreach vtiToWti = AllWidenableFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(op (wti.Vector wti.RegClass:$rs2),
                    (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector vti.RegClass:$rs1),
                                     (vti.Mask true_mask), (XLenVT srcvalue)))),
                (!cast<Instruction>(instruction_name#"_WV_"#vti.LMul.MX#"_E"#vti.SEW#"_TIED")
                   wti.RegClass:$rs2, vti.RegClass:$rs1,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW,
                   TAIL_AGNOSTIC)>;
      def : Pat<(op (wti.Vector wti.RegClass:$rs2),
                    (wti.Vector (riscv_fpextend_vl_oneuse
                                     (vti.Vector (SplatFPOp vti.ScalarRegClass:$rs1)),
                                     (vti.Mask true_mask), (XLenVT srcvalue)))),
                (!cast<Instruction>(instruction_name#"_W"#vti.ScalarSuffix#"_"#vti.LMul.MX#"_E"#vti.SEW)
                   (wti.Vector (IMPLICIT_DEF)), wti.RegClass:$rs2,
                   vti.ScalarRegClass:$rs1,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TA_MA)>;
      def : Pat<(op (wti.Vector wti.RegClass:$rs2),
                    (wti.Vector (SplatFPOp (fpext_oneuse (vti.Scalar vti.ScalarRegClass:$rs1))))),
                (!cast<Instruction>(instruction_name#"_W"#vti.ScalarSuffix#"_"#vti.LMul.MX#"_E"#vti.SEW)
                   (wti.Vector (IMPLICIT_DEF)), wti.RegClass:$rs2,
                   vti.ScalarRegClass:$rs1,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TA_MA)>;
    }
  }
}

multiclass VPatWidenBinaryFPSDNode_VV_VF_WV_WF_RM<SDNode op,
                                                  string instruction_name>
    : VPatWidenBinaryFPSDNode_VV_VF_RM<op, instruction_name>,
      VPatWidenBinaryFPSDNode_WV_WF_RM<op, instruction_name>;

multiclass VPatWidenFPMulAccSDNode_VV_VF_RM<string instruction_name> {
  foreach vtiToWti = AllWidenableFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    defvar suffix = vti.LMul.MX # "_E" # vti.SEW;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(fma (wti.Vector (riscv_fpextend_vl_oneuse
                                      (vti.Vector vti.RegClass:$rs1),
                                      (vti.Mask true_mask), (XLenVT srcvalue))),
                     (wti.Vector (riscv_fpextend_vl_oneuse
                                      (vti.Vector vti.RegClass:$rs2),
                                      (vti.Mask true_mask), (XLenVT srcvalue))),
                     (wti.Vector wti.RegClass:$rd)),
                (!cast<Instruction>(instruction_name#"_VV_"#suffix)
                   wti.RegClass:$rd, vti.RegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
      def : Pat<(fma (wti.Vector (SplatFPOp
                                      (fpext_oneuse (vti.Scalar vti.ScalarRegClass:$rs1)))),
                     (wti.Vector (riscv_fpextend_vl_oneuse
                                      (vti.Vector vti.RegClass:$rs2),
                                      (vti.Mask true_mask), (XLenVT srcvalue))),
                     (wti.Vector wti.RegClass:$rd)),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#suffix)
                   wti.RegClass:$rd, vti.ScalarRegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
    }
  }
}

multiclass VPatWidenFPNegMulAccSDNode_VV_VF_RM<string instruction_name> {
  foreach vtiToWti = AllWidenableFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    defvar suffix = vti.LMul.MX # "_E" # vti.SEW;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(fma (fneg (wti.Vector (riscv_fpextend_vl_oneuse
                                            (vti.Vector vti.RegClass:$rs1),
                                            (vti.Mask true_mask), (XLenVT srcvalue)))),
                     (riscv_fpextend_vl_oneuse (vti.Vector vti.RegClass:$rs2),
                                               (vti.Mask true_mask), (XLenVT srcvalue)),
                     (fneg wti.RegClass:$rd)),
                (!cast<Instruction>(instruction_name#"_VV_"#suffix)
                   wti.RegClass:$rd, vti.RegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
      def : Pat<(fma (SplatFPOp (fpext_oneuse (vti.Scalar vti.ScalarRegClass:$rs1))),
                     (fneg (wti.Vector (riscv_fpextend_vl_oneuse
                                            (vti.Vector vti.RegClass:$rs2),
                                            (vti.Mask true_mask), (XLenVT srcvalue)))),
                     (fneg wti.RegClass:$rd)),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#suffix)
                   wti.RegClass:$rd, vti.ScalarRegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
      def : Pat<(fma (fneg (wti.Vector (SplatFPOp (fpext_oneuse (vti.Scalar vti.ScalarRegClass:$rs1))))),
                     (riscv_fpextend_vl_oneuse (vti.Vector vti.RegClass:$rs2),
                                               (vti.Mask true_mask), (XLenVT srcvalue)),
                     (fneg wti.RegClass:$rd)),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#suffix)
                   wti.RegClass:$rd, vti.ScalarRegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
    }
  }
}

multiclass VPatWidenFPMulSacSDNode_VV_VF_RM<string instruction_name> {
  foreach vtiToWti = AllWidenableFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    defvar suffix = vti.LMul.MX # "_E" # vti.SEW;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(fma (wti.Vector (riscv_fpextend_vl_oneuse
                                      (vti.Vector vti.RegClass:$rs1),
                                      (vti.Mask true_mask), (XLenVT srcvalue))),
                     (riscv_fpextend_vl_oneuse (vti.Vector vti.RegClass:$rs2),
                                               (vti.Mask true_mask), (XLenVT srcvalue)),
                     (fneg wti.RegClass:$rd)),
                (!cast<Instruction>(instruction_name#"_VV_"#suffix)
                   wti.RegClass:$rd, vti.RegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
      def : Pat<(fma (wti.Vector (SplatFPOp (fpext_oneuse (vti.Scalar vti.ScalarRegClass:$rs1)))),
                     (riscv_fpextend_vl_oneuse (vti.Vector vti.RegClass:$rs2),
                                               (vti.Mask true_mask), (XLenVT srcvalue)),
                     (fneg wti.RegClass:$rd)),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#suffix)
                   wti.RegClass:$rd, vti.ScalarRegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
    }
  }
}

multiclass VPatWidenFPNegMulSacSDNode_VV_VF_RM<string instruction_name> {
  foreach vtiToWti = AllWidenableFloatVectors in {
    defvar vti = vtiToWti.Vti;
    defvar wti = vtiToWti.Wti;
    defvar suffix = vti.LMul.MX # "_E" # vti.SEW;
    let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                                 GetVTypePredicates<wti>.Predicates) in {
      def : Pat<(fma (fneg (wti.Vector (riscv_fpextend_vl_oneuse
                                            (vti.Vector vti.RegClass:$rs1),
                                            (vti.Mask true_mask), (XLenVT srcvalue)))),
                     (riscv_fpextend_vl_oneuse (vti.Vector vti.RegClass:$rs2),
                                               (vti.Mask true_mask), (XLenVT srcvalue)),
                     wti.RegClass:$rd),
                (!cast<Instruction>(instruction_name#"_VV_"#suffix)
                   wti.RegClass:$rd, vti.RegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
      def : Pat<(fma (wti.Vector (SplatFPOp (fpext_oneuse (vti.Scalar vti.ScalarRegClass:$rs1)))),
                     (fneg (wti.Vector (riscv_fpextend_vl_oneuse
                                            (vti.Vector vti.RegClass:$rs2),
                                            (vti.Mask true_mask), (XLenVT srcvalue)))),
                     wti.RegClass:$rd),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#suffix)
                   wti.RegClass:$rd, vti.ScalarRegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
      def : Pat<(fma (fneg (wti.Vector (SplatFPOp (fpext_oneuse (vti.Scalar vti.ScalarRegClass:$rs1))))),
                     (riscv_fpextend_vl_oneuse (vti.Vector vti.RegClass:$rs2),
                                               (vti.Mask true_mask), (XLenVT srcvalue)),
                     wti.RegClass:$rd),
                (!cast<Instruction>(instruction_name#"_V"#vti.ScalarSuffix#"_"#suffix)
                   wti.RegClass:$rd, vti.ScalarRegClass:$rs1, vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
    }
  }
}

multiclass VPatMultiplyAddSDNode_VV_VX<SDNode op, string instruction_name> {
  foreach vti = AllIntegerVectors in {
    defvar suffix = vti.LMul.MX;
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      // NOTE: We choose VMADD because it has the most commuting freedom. So it
      // works best with how TwoAddressInstructionPass tries commuting.
      def : Pat<(vti.Vector (op vti.RegClass:$rs2,
                                (mul_oneuse vti.RegClass:$rs1, vti.RegClass:$rd))),
                (!cast<Instruction>(instruction_name#"_VV_"# suffix)
                   vti.RegClass:$rd, vti.RegClass:$rs1, vti.RegClass:$rs2,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
      // The choice of VMADD here is arbitrary, vmadd.vx and vmacc.vx are equally
      // commutable.
      def : Pat<(vti.Vector (op vti.RegClass:$rs2,
                                (mul_oneuse (SplatPat XLenVT:$rs1), vti.RegClass:$rd))),
                (!cast<Instruction>(instruction_name#"_VX_" # suffix)
                   vti.RegClass:$rd, vti.ScalarRegClass:$rs1, vti.RegClass:$rs2,
                   vti.AVL, vti.Log2SEW, TAIL_AGNOSTIC)>;
    }
  }
}

multiclass VPatAVGADD_VV_VX_RM<SDNode vop, int vxrm, string suffix = ""> {
  foreach vti = AllIntegerVectors in {
    let Predicates = GetVTypePredicates<vti>.Predicates in {
      def : Pat<(vop (vti.Vector vti.RegClass:$rs1),
                     (vti.Vector vti.RegClass:$rs2)),
                (!cast<Instruction>("PseudoVAADD"#suffix#"_VV_"#vti.LMul.MX)
                  (vti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1, vti.RegClass:$rs2,
                  vxrm, vti.AVL, vti.Log2SEW, TA_MA)>;
      def : Pat<(vop (vti.Vector vti.RegClass:$rs1),
                     (vti.Vector (SplatPat (XLenVT GPR:$rs2)))),
                (!cast<Instruction>("PseudoVAADD"#suffix#"_VX_"#vti.LMul.MX)
                  (vti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1, GPR:$rs2,
                  vxrm, vti.AVL, vti.Log2SEW, TA_MA)>;
    }
  }
}

//===----------------------------------------------------------------------===//
// Patterns.
//===----------------------------------------------------------------------===//

// 7.4. Vector Unit-Stride Instructions
foreach vti = !listconcat(FractionalGroupIntegerVectors,
                          FractionalGroupFloatVectors,
                          FractionalGroupBFloatVectors) in
  let Predicates = !if(!eq(vti.Scalar, f16), [HasVInstructionsF16Minimal],
                       GetVTypePredicates<vti>.Predicates) in 
  defm : VPatUSLoadStoreSDNode<vti.Vector, vti.Log2SEW, vti.LMul,
                               vti.AVL, vti.RegClass>;
foreach vti = [VI8M1, VI16M1, VI32M1, VI64M1, VBF16M1, VF16M1, VF32M1, VF64M1] in
  let Predicates = !if(!eq(vti.Scalar, f16), [HasVInstructionsF16Minimal],
                       GetVTypePredicates<vti>.Predicates) in 
  defm : VPatUSLoadStoreWholeVRSDNode<vti.Vector, vti.Log2SEW, vti.LMul,
                                      vti.RegClass>;
foreach vti = !listconcat(GroupIntegerVectors, GroupFloatVectors, GroupBFloatVectors) in
  let Predicates = !if(!eq(vti.Scalar, f16), [HasVInstructionsF16Minimal],
                       GetVTypePredicates<vti>.Predicates) in 
  defm : VPatUSLoadStoreWholeVRSDNode<vti.Vector, vti.Log2SEW, vti.LMul,
                                      vti.RegClass>;
foreach mti = AllMasks in
  let Predicates = [HasVInstructions] in
  defm : VPatUSLoadStoreMaskSDNode<mti>;

// 11. Vector Integer Arithmetic Instructions

// 11.1. Vector Single-Width Integer Add and Subtract
defm : VPatBinarySDNode_VV_VX_VI<add, "PseudoVADD">;
defm : VPatBinarySDNode_VV_VX<sub, "PseudoVSUB">;
// Handle VRSUB specially since it's the only integer binary op with reversed
// pattern operands
foreach vti = AllIntegerVectors in {
  // FIXME: The AddedComplexity here is covering up a missing matcher for
  // widening vwsub.vx which can recognize a extended folded into the
  // scalar of the splat.
  let AddedComplexity = 20 in
  let Predicates = GetVTypePredicates<vti>.Predicates in {
    def : Pat<(sub (vti.Vector (SplatPat (XLenVT GPR:$rs2))),
                   (vti.Vector vti.RegClass:$rs1)),
              (!cast<Instruction>("PseudoVRSUB_VX_"# vti.LMul.MX)
                   (vti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1, GPR:$rs2,
                   vti.AVL, vti.Log2SEW, TA_MA)>;
    def : Pat<(sub (vti.Vector (SplatPat_simm5 simm5:$rs2)),
                   (vti.Vector vti.RegClass:$rs1)),
              (!cast<Instruction>("PseudoVRSUB_VI_"# vti.LMul.MX)
                   (vti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1,
                   simm5:$rs2, vti.AVL, vti.Log2SEW, TA_MA)>;
  }
}

// 11.2. Vector Widening Integer Add and Subtract
defm : VPatWidenBinarySDNode_VV_VX_WV_WX<add, sext_oneuse, "PseudoVWADD">;
defm : VPatWidenBinarySDNode_VV_VX_WV_WX<add, zext_oneuse, "PseudoVWADDU">;
defm : VPatWidenBinarySDNode_VV_VX_WV_WX<add, anyext_oneuse, "PseudoVWADDU">;

defm : VPatWidenBinarySDNode_VV_VX_WV_WX<sub, sext_oneuse, "PseudoVWSUB">;
defm : VPatWidenBinarySDNode_VV_VX_WV_WX<sub, zext_oneuse, "PseudoVWSUBU">;
defm : VPatWidenBinarySDNode_VV_VX_WV_WX<sub, anyext_oneuse, "PseudoVWSUBU">;

// shl (ext v, splat 1) is a special case of widening add.
foreach vtiToWti = AllWidenableIntVectors in {
  defvar vti = vtiToWti.Vti;
  defvar wti = vtiToWti.Wti;
  let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                               GetVTypePredicates<wti>.Predicates) in {
    def : Pat<(shl (wti.Vector (sext_oneuse (vti.Vector vti.RegClass:$rs1))),
                   (wti.Vector (riscv_vmv_v_x_vl (wti.Vector undef), 1, (XLenVT srcvalue)))),
              (!cast<Instruction>("PseudoVWADD_VV_"#vti.LMul.MX)
                  (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1, vti.RegClass:$rs1,
                  vti.AVL, vti.Log2SEW, TA_MA)>;
    def : Pat<(shl (wti.Vector (zext_oneuse (vti.Vector vti.RegClass:$rs1))),
                   (wti.Vector (riscv_vmv_v_x_vl (wti.Vector undef), 1, (XLenVT srcvalue)))),
              (!cast<Instruction>("PseudoVWADDU_VV_"#vti.LMul.MX)
                  (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1, vti.RegClass:$rs1,
                  vti.AVL, vti.Log2SEW, TA_MA)>;
    def : Pat<(shl (wti.Vector (anyext_oneuse (vti.Vector vti.RegClass:$rs1))),
                   (wti.Vector (riscv_vmv_v_x_vl (wti.Vector undef), 1, (XLenVT srcvalue)))),
              (!cast<Instruction>("PseudoVWADDU_VV_"#vti.LMul.MX)
                  (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1, vti.RegClass:$rs1,
                  vti.AVL, vti.Log2SEW, TA_MA)>;
    def : Pat<(shl (wti.Vector (riscv_sext_vl_oneuse (vti.Vector vti.RegClass:$rs1), (vti.Mask V0), VLOpFrag)),
                   (wti.Vector (riscv_vmv_v_x_vl (wti.Vector undef), 1, (XLenVT srcvalue)))),
              (!cast<Instruction>("PseudoVWADD_VV_"#vti.LMul.MX#"_MASK")
                  (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1, vti.RegClass:$rs1,
                  (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;
    def : Pat<(shl (wti.Vector (riscv_zext_vl_oneuse (vti.Vector vti.RegClass:$rs1), (vti.Mask V0), VLOpFrag)),
                   (wti.Vector (riscv_vmv_v_x_vl (wti.Vector undef), 1, (XLenVT srcvalue)))),
              (!cast<Instruction>("PseudoVWADDU_VV_"#vti.LMul.MX#"_MASK")
                  (wti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1, vti.RegClass:$rs1,
                  (vti.Mask V0), GPR:$vl, vti.Log2SEW, TAIL_AGNOSTIC)>;
  }
}

// 11.3. Vector Integer Extension
defm : VPatExtendSDNode_V<[zext, anyext], "PseudoVZEXT", "VF2",
                          AllFractionableVF2IntVectors>;
defm : VPatExtendSDNode_V<[sext],         "PseudoVSEXT", "VF2",
                          AllFractionableVF2IntVectors>;
defm : VPatExtendSDNode_V<[zext, anyext], "PseudoVZEXT", "VF4",
                          AllFractionableVF4IntVectors>;
defm : VPatExtendSDNode_V<[sext],         "PseudoVSEXT", "VF4",
                          AllFractionableVF4IntVectors>;
defm : VPatExtendSDNode_V<[zext, anyext], "PseudoVZEXT", "VF8",
                          AllFractionableVF8IntVectors>;
defm : VPatExtendSDNode_V<[sext],         "PseudoVSEXT", "VF8",
                          AllFractionableVF8IntVectors>;

// 11.5. Vector Bitwise Logical Instructions
defm : VPatBinarySDNode_VV_VX_VI<and, "PseudoVAND">;
defm : VPatBinarySDNode_VV_VX_VI<or, "PseudoVOR">;
defm : VPatBinarySDNode_VV_VX_VI<xor, "PseudoVXOR">;

// 11.6. Vector Single-Width Bit Shift Instructions
defm : VPatBinarySDNode_VV_VX_VI<shl, "PseudoVSLL", uimm5>;
defm : VPatBinarySDNode_VV_VX_VI<srl, "PseudoVSRL", uimm5>;
defm : VPatBinarySDNode_VV_VX_VI<sra, "PseudoVSRA", uimm5>;

foreach vti = AllIntegerVectors in {
  // Emit shift by 1 as an add since it might be faster.
  let Predicates = GetVTypePredicates<vti>.Predicates in
  def : Pat<(shl (vti.Vector vti.RegClass:$rs1),
                 (vti.Vector (riscv_vmv_v_x_vl (vti.Vector undef), 1, (XLenVT srcvalue)))),
            (!cast<Instruction>("PseudoVADD_VV_"# vti.LMul.MX)
                 (vti.Vector (IMPLICIT_DEF)), vti.RegClass:$rs1,
                 vti.RegClass:$rs1, vti.AVL, vti.Log2SEW, TA_MA)>;

}

// 11.8. Vector Integer Comparison Instructions
defm : VPatIntegerSetCCSDNode_VV<"PseudoVMSEQ", SETEQ>;
defm : VPatIntegerSetCCSDNode_VV<"PseudoVMSNE", SETNE>;

defm : VPatIntegerSetCCSDNode_VV_Swappable<"PseudoVMSLT",  SETLT, SETGT>;
defm : VPatIntegerSetCCSDNode_VV_Swappable<"PseudoVMSLTU", SETULT, SETUGT>;
defm : VPatIntegerSetCCSDNode_VV_Swappable<"PseudoVMSLE",  SETLE,  SETGE>;
defm : VPatIntegerSetCCSDNode_VV_Swappable<"PseudoVMSLEU", SETULE, SETUGE>;

defm : VPatIntegerSetCCSDNode_VX_Swappable<"PseudoVMSEQ",  SETEQ,  SETEQ>;
defm : VPatIntegerSetCCSDNode_VX_Swappable<"PseudoVMSNE",  SETNE,  SETNE>;
defm : VPatIntegerSetCCSDNode_VX_Swappable<"PseudoVMSLT",  SETLT,  SETGT>;
defm : VPatIntegerSetCCSDNode_VX_Swappable<"PseudoVMSLTU", SETULT, SETUGT>;
defm : VPatIntegerSetCCSDNode_VX_Swappable<"PseudoVMSLE",  SETLE,  SETGE>;
defm : VPatIntegerSetCCSDNode_VX_Swappable<"PseudoVMSLEU", SETULE, SETUGE>;
defm : VPatIntegerSetCCSDNode_VX_Swappable<"PseudoVMSGT",  SETGT,  SETLT>;
defm : VPatIntegerSetCCSDNode_VX_Swappable<"PseudoVMSGTU", SETUGT, SETULT>;
// There is no VMSGE(U)_VX instruction

defm : VPatIntegerSetCCSDNode_VI_Swappable<"PseudoVMSEQ",  SETEQ, SETEQ>;
defm : VPatIntegerSetCCSDNode_VI_Swappable<"PseudoVMSNE",  SETNE, SETNE>;
defm : VPatIntegerSetCCSDNode_VI_Swappable<"PseudoVMSLE",  SETLE, SETGE>;
defm : VPatIntegerSetCCSDNode_VI_Swappable<"PseudoVMSLEU", SETULE, SETUGE>;
defm : VPatIntegerSetCCSDNode_VI_Swappable<"PseudoVMSGT",  SETGT, SETLT>;
defm : VPatIntegerSetCCSDNode_VI_Swappable<"PseudoVMSGTU", SETUGT, SETULT>;

defm : VPatIntegerSetCCSDNode_VIPlus1_Swappable<"PseudoVMSLE", SETLT, SETGT,
                                                SplatPat_simm5_plus1>;
defm : VPatIntegerSetCCSDNode_VIPlus1_Swappable<"PseudoVMSLEU", SETULT, SETUGT,
                                                SplatPat_simm5_plus1_nonzero>;
defm : VPatIntegerSetCCSDNode_VIPlus1_Swappable<"PseudoVMSGT", SETGE, SETLE,
                                                SplatPat_simm5_plus1>;
defm : VPatIntegerSetCCSDNode_VIPlus1_Swappable<"PseudoVMSGTU", SETUGE, SETULE,
                                                SplatPat_simm5_plus1_nonzero>;

// 11.9. Vector Integer Min/Max Instructions
defm : VPatBinarySDNode_VV_VX<umin, "PseudoVMINU">;
defm : VPatBinarySDNode_VV_VX<smin, "PseudoVMIN">;
defm : VPatBinarySDNode_VV_VX<umax, "PseudoVMAXU">;
defm : VPatBinarySDNode_VV_VX<smax, "PseudoVMAX">;

// 11.10. Vector Single-Width Integer Multiply Instructions
defm : VPatBinarySDNode_VV_VX<mul, "PseudoVMUL">;

defm : VPatBinarySDNode_VV_VX<mulhs, "PseudoVMULH", IntegerVectorsExceptI64>;
defm : VPatBinarySDNode_VV_VX<mulhu, "PseudoVMULHU", IntegerVectorsExceptI64>;

let Predicates = [HasVInstructionsFullMultiply] in {
  defm : VPatBinarySDNode_VV_VX<mulhs, "PseudoVMULH", I64IntegerVectors>;
  defm : VPatBinarySDNode_VV_VX<mulhu, "PseudoVMULHU", I64IntegerVectors>;
}

// 11.11. Vector Integer Divide Instructions
defm : VPatBinarySDNode_VV_VX<udiv, "PseudoVDIVU", isSEWAware=1>;
defm : VPatBinarySDNode_VV_VX<sdiv, "PseudoVDIV", isSEWAware=1>;
defm : VPatBinarySDNode_VV_VX<urem, "PseudoVREMU", isSEWAware=1>;
defm : VPatBinarySDNode_VV_VX<srem, "PseudoVREM", isSEWAware=1>;

foreach vtiTowti = AllWidenableIntVectors in {
  defvar vti = vtiTowti.Vti;
  defvar wti = vtiTowti.Wti;
  let Predicates = !listconcat(GetVTypePredicates<vti>.Predicates,
                               GetVTypePredicates<wti>.Predicates) in {
  def : Pat<
    (vti.Vector
      (riscv_trunc_vector_vl
        (srem (wti.Vector (sext_oneuse (vti.Vector vti.RegClass:$rs1))),
              (wti.Vector (sext_oneuse (vti.Vector vti.RegClass:$rs2)))),
        (vti.Mask true_mask), (XLenVT srcvalue))),
      (!cast<Instruction>("PseudoVREM_VV_"#vti.LMul.MX#"_E"#!shl(1, vti.Log2SEW))
        (vti.Vector (IMPLICIT_DEF)),
        vti.RegClass:$rs1, vti.RegClass:$rs2, vti.AVL, vti.Log2SEW, TA_MA)>;
  }
}

// 11.12. Vector Widening Integer Multiply Instructions
defm : VPatWidenBinarySDNode_VV_VX<mul, sext_oneuse, sext_oneuse,
                                   "PseudoVWMUL">;
defm : VPatWidenBinarySDNode_VV_VX<mul, zext_oneuse, zext_oneuse,
                                   "PseudoVWMULU">;
defm : VPatWidenBinarySDNode_VV_VX<mul, anyext_oneuse, anyext_oneuse,
                                   "PseudoVWMULU">;
defm : VPatWidenBinarySDNode_VV_VX<mul, zext_oneuse, anyext_oneuse,
                                   "PseudoVWMULU">;
defm : VPatWidenBinarySDNode_VV_VX<mul, sext_oneuse, zext_oneuse,
                                   "PseudoVWMULSU">;
defm : VPatWidenBinarySDNode_VV_VX<mul, sext_oneuse, anyext_oneuse,
                                   "PseudoVWMULSU">;

// 11.13 Vector Single-Width Integer Multiply-Add Instructions.
defm : VPatMultiplyAddSDNode_VV_VX<add, "PseudoVMADD">;
defm : VPatMultiplyAddSDNode_VV_VX<sub, "PseudoVNMSUB">;

// 11.14 Vector Widening Integer Multiply-Add Instructions
defm : VPatWidenMulAddSDNode_VV<sext_oneuse, sext_oneuse, "PseudoVWMACC">;
defm : VPatWidenMulAddSDNode_VX<sext_oneuse, sext_oneuse, "PseudoVWMACC">;
defm : VPatWidenMulAddSDNode_VV<zext_oneuse, zext_oneuse, "PseudoVWMACCU">;
defm : VPatWidenMulAddSDNode_VX<zext_oneuse, zext_oneuse, "PseudoVWMACCU">;
defm : VPatWidenMulAddSDNode_VV<sext_oneuse, zext_oneuse, "PseudoVWMACCSU">;
defm : VPatWidenMulAddSDNode_VX<sext_oneuse, zext_oneuse, "PseudoVWMACCSU">;
defm : VPatWidenMulAddSDNode_VX<zext_oneuse, sext_oneuse, "PseudoVWMACCUS">;

// 11.15. Vector Integer Merge Instructions
foreach vti = AllIntegerVectors in {
  let Predicates = GetVTypePredicates<vti>.Predicates in {
    def : Pat<(vti.Vector (vselect (vti.Mask V0), vti.RegClass:$rs1,
                                                        vti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVMERGE_VVM_"#vti.LMul.MX)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs2, vti.RegClass:$rs1, (vti.Mask V0),
                   vti.AVL, vti.Log2SEW)>;

    def : Pat<(vti.Vector (vselect (vti.Mask V0), (SplatPat XLenVT:$rs1),
                                                        vti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVMERGE_VXM_"#vti.LMul.MX)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs2, GPR:$rs1, (vti.Mask V0), vti.AVL, vti.Log2SEW)>;

    def : Pat<(vti.Vector (vselect (vti.Mask V0), (SplatPat_simm5 simm5:$rs1),
                                                        vti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVMERGE_VIM_"#vti.LMul.MX)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs2, simm5:$rs1, (vti.Mask V0), vti.AVL, vti.Log2SEW)>;
  }
}

// 12. Vector Fixed-Point Arithmetic Instructions

// 12.1. Vector Single-Width Saturating Add and Subtract
defm : VPatBinarySDNode_VV_VX_VI<saddsat, "PseudoVSADD">;
defm : VPatBinarySDNode_VV_VX_VI<uaddsat, "PseudoVSADDU">;
defm : VPatBinarySDNode_VV_VX<ssubsat, "PseudoVSSUB">;
defm : VPatBinarySDNode_VV_VX<usubsat, "PseudoVSSUBU">;

// 12.2. Vector Single-Width Averaging Add and Subtract
defm : VPatAVGADD_VV_VX_RM<avgfloors, 0b10>;
defm : VPatAVGADD_VV_VX_RM<avgflooru, 0b10, suffix = "U">;
defm : VPatAVGADD_VV_VX_RM<avgceils, 0b00>;
defm : VPatAVGADD_VV_VX_RM<avgceilu, 0b00, suffix = "U">;

// 15. Vector Mask Instructions

// 15.1. Vector Mask-Register Logical Instructions
foreach mti = AllMasks in {
  let Predicates = [HasVInstructions] in {
    def : Pat<(mti.Mask (and VR:$rs1, VR:$rs2)),
              (!cast<Instruction>("PseudoVMAND_MM_"#mti.LMul.MX)
                   VR:$rs1, VR:$rs2, mti.AVL, mti.Log2SEW)>;
    def : Pat<(mti.Mask (or VR:$rs1, VR:$rs2)),
              (!cast<Instruction>("PseudoVMOR_MM_"#mti.LMul.MX)
                   VR:$rs1, VR:$rs2, mti.AVL, mti.Log2SEW)>;
    def : Pat<(mti.Mask (xor VR:$rs1, VR:$rs2)),
              (!cast<Instruction>("PseudoVMXOR_MM_"#mti.LMul.MX)
                   VR:$rs1, VR:$rs2, mti.AVL, mti.Log2SEW)>;

    def : Pat<(mti.Mask (rvv_vnot (and VR:$rs1, VR:$rs2))),
              (!cast<Instruction>("PseudoVMNAND_MM_"#mti.LMul.MX)
                   VR:$rs1, VR:$rs2, mti.AVL, mti.Log2SEW)>;
    def : Pat<(mti.Mask (rvv_vnot (or VR:$rs1, VR:$rs2))),
              (!cast<Instruction>("PseudoVMNOR_MM_"#mti.LMul.MX)
                   VR:$rs1, VR:$rs2, mti.AVL, mti.Log2SEW)>;
    def : Pat<(mti.Mask (rvv_vnot (xor VR:$rs1, VR:$rs2))),
              (!cast<Instruction>("PseudoVMXNOR_MM_"#mti.LMul.MX)
                   VR:$rs1, VR:$rs2, mti.AVL, mti.Log2SEW)>;

    def : Pat<(mti.Mask (and VR:$rs1, (rvv_vnot VR:$rs2))),
              (!cast<Instruction>("PseudoVMANDN_MM_"#mti.LMul.MX)
                   VR:$rs1, VR:$rs2, mti.AVL, mti.Log2SEW)>;
    def : Pat<(mti.Mask (or VR:$rs1, (rvv_vnot VR:$rs2))),
              (!cast<Instruction>("PseudoVMORN_MM_"#mti.LMul.MX)
                   VR:$rs1, VR:$rs2, mti.AVL, mti.Log2SEW)>;

    // Handle rvv_vnot the same as the vmnot.m pseudoinstruction.
    def : Pat<(mti.Mask (rvv_vnot VR:$rs)),
              (!cast<Instruction>("PseudoVMNAND_MM_"#mti.LMul.MX)
                   VR:$rs, VR:$rs, mti.AVL, mti.Log2SEW)>;
  }
}

// 13. Vector Floating-Point Instructions

// 13.2. Vector Single-Width Floating-Point Add/Subtract Instructions
defm : VPatBinaryFPSDNode_VV_VF_RM<any_fadd, "PseudoVFADD", isSEWAware=1>;
defm : VPatBinaryFPSDNode_VV_VF_RM<any_fsub, "PseudoVFSUB", isSEWAware=1>;
defm : VPatBinaryFPSDNode_R_VF_RM<any_fsub, "PseudoVFRSUB", isSEWAware=1>;

// 13.3. Vector Widening Floating-Point Add/Subtract Instructions
defm : VPatWidenBinaryFPSDNode_VV_VF_WV_WF_RM<fadd, "PseudoVFWADD">;
defm : VPatWidenBinaryFPSDNode_VV_VF_WV_WF_RM<fsub, "PseudoVFWSUB">;

// 13.4. Vector Single-Width Floating-Point Multiply/Divide Instructions
defm : VPatBinaryFPSDNode_VV_VF_RM<any_fmul, "PseudoVFMUL", isSEWAware=1>;
defm : VPatBinaryFPSDNode_VV_VF_RM<any_fdiv, "PseudoVFDIV", isSEWAware=1>;
defm : VPatBinaryFPSDNode_R_VF_RM<any_fdiv, "PseudoVFRDIV", isSEWAware=1>;

// 13.5. Vector Widening Floating-Point Multiply Instructions
defm : VPatWidenBinaryFPSDNode_VV_VF_RM<fmul, "PseudoVFWMUL">;

// 13.6 Vector Single-Width Floating-Point Fused Multiply-Add Instructions.
foreach fvti = AllFloatVectors in {
  // NOTE: We choose VFMADD because it has the most commuting freedom. So it
  // works best with how TwoAddressInstructionPass tries commuting.
  defvar suffix = fvti.LMul.MX # "_E" # fvti.SEW;
  let Predicates = GetVTypePredicates<fvti>.Predicates in {
    def : Pat<(fvti.Vector (any_fma fvti.RegClass:$rs1, fvti.RegClass:$rd,
                                    fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVFMADD_VV_"# suffix)
                   fvti.RegClass:$rd, fvti.RegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;
    def : Pat<(fvti.Vector (any_fma fvti.RegClass:$rs1, fvti.RegClass:$rd,
                                    (fneg fvti.RegClass:$rs2))),
              (!cast<Instruction>("PseudoVFMSUB_VV_"# suffix)
                   fvti.RegClass:$rd, fvti.RegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;
    def : Pat<(fvti.Vector (any_fma (fneg fvti.RegClass:$rs1), fvti.RegClass:$rd,
                                    (fneg fvti.RegClass:$rs2))),
              (!cast<Instruction>("PseudoVFNMADD_VV_"# suffix)
                   fvti.RegClass:$rd, fvti.RegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;
    def : Pat<(fvti.Vector (any_fma (fneg fvti.RegClass:$rs1), fvti.RegClass:$rd,
                                    fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVFNMSUB_VV_"# suffix)
                   fvti.RegClass:$rd, fvti.RegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;

    // The choice of VFMADD here is arbitrary, vfmadd.vf and vfmacc.vf are equally
    // commutable.
    def : Pat<(fvti.Vector (any_fma (SplatFPOp fvti.ScalarRegClass:$rs1),
                                    fvti.RegClass:$rd, fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVFMADD_V" # fvti.ScalarSuffix # "_" # suffix)
                   fvti.RegClass:$rd, fvti.ScalarRegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;
    def : Pat<(fvti.Vector (any_fma (SplatFPOp fvti.ScalarRegClass:$rs1),
                                    fvti.RegClass:$rd, (fneg fvti.RegClass:$rs2))),
              (!cast<Instruction>("PseudoVFMSUB_V" # fvti.ScalarSuffix # "_" # suffix)
                   fvti.RegClass:$rd, fvti.ScalarRegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;

    def : Pat<(fvti.Vector (any_fma (SplatFPOp fvti.ScalarRegClass:$rs1),
                                    (fneg fvti.RegClass:$rd), (fneg fvti.RegClass:$rs2))),
              (!cast<Instruction>("PseudoVFNMADD_V" # fvti.ScalarSuffix # "_" # suffix)
                   fvti.RegClass:$rd, fvti.ScalarRegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;
    def : Pat<(fvti.Vector (any_fma (SplatFPOp fvti.ScalarRegClass:$rs1),
                                    (fneg fvti.RegClass:$rd), fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVFNMSUB_V" # fvti.ScalarSuffix # "_" # suffix)
                   fvti.RegClass:$rd, fvti.ScalarRegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;

    // The splat might be negated.
    def : Pat<(fvti.Vector (any_fma (fneg (SplatFPOp fvti.ScalarRegClass:$rs1)),
                                    fvti.RegClass:$rd, (fneg fvti.RegClass:$rs2))),
              (!cast<Instruction>("PseudoVFNMADD_V" # fvti.ScalarSuffix # "_" # suffix)
                   fvti.RegClass:$rd, fvti.ScalarRegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;
    def : Pat<(fvti.Vector (any_fma (fneg (SplatFPOp fvti.ScalarRegClass:$rs1)),
                                    fvti.RegClass:$rd, fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVFNMSUB_V" # fvti.ScalarSuffix # "_" # suffix)
                   fvti.RegClass:$rd, fvti.ScalarRegClass:$rs1, fvti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   fvti.AVL, fvti.Log2SEW, TAIL_AGNOSTIC)>;
  }
}

// 13.7. Vector Widening Floating-Point Fused Multiply-Add Instructions
defm : VPatWidenFPMulAccSDNode_VV_VF_RM<"PseudoVFWMACC">;
defm : VPatWidenFPNegMulAccSDNode_VV_VF_RM<"PseudoVFWNMACC">;
defm : VPatWidenFPMulSacSDNode_VV_VF_RM<"PseudoVFWMSAC">;
defm : VPatWidenFPNegMulSacSDNode_VV_VF_RM<"PseudoVFWNMSAC">;

foreach vti = AllFloatVectors in {
  let Predicates = GetVTypePredicates<vti>.Predicates in {
    // 13.8. Vector Floating-Point Square-Root Instruction
    def : Pat<(any_fsqrt (vti.Vector vti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVFSQRT_V_"# vti.LMul.MX#"_E"#vti.SEW)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs2,
                   // Value to indicate no rounding mode change in
                   // RISCVInsertReadWriteCSR
                   FRM_DYN,
                   vti.AVL, vti.Log2SEW, TA_MA)>;

    // 13.12. Vector Floating-Point Sign-Injection Instructions
    def : Pat<(fabs (vti.Vector vti.RegClass:$rs)),
              (!cast<Instruction>("PseudoVFSGNJX_VV_"# vti.LMul.MX#"_E"#vti.SEW)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs, vti.RegClass:$rs, vti.AVL, vti.Log2SEW, TA_MA)>;
    // Handle fneg with VFSGNJN using the same input for both operands.
    def : Pat<(fneg (vti.Vector vti.RegClass:$rs)),
              (!cast<Instruction>("PseudoVFSGNJN_VV_"# vti.LMul.MX#"_E"#vti.SEW)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs, vti.RegClass:$rs, vti.AVL, vti.Log2SEW, TA_MA)>;

    def : Pat<(vti.Vector (fcopysign (vti.Vector vti.RegClass:$rs1),
                                     (vti.Vector vti.RegClass:$rs2))),
              (!cast<Instruction>("PseudoVFSGNJ_VV_"# vti.LMul.MX#"_E"#vti.SEW)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs1, vti.RegClass:$rs2, vti.AVL, vti.Log2SEW, TA_MA)>;
    def : Pat<(vti.Vector (fcopysign (vti.Vector vti.RegClass:$rs1),
                                     (vti.Vector (SplatFPOp vti.ScalarRegClass:$rs2)))),
              (!cast<Instruction>("PseudoVFSGNJ_V"#vti.ScalarSuffix#"_"#vti.LMul.MX#"_E"#vti.SEW)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs1, vti.ScalarRegClass:$rs2, vti.AVL, vti.Log2SEW, TA_MA)>;

    def : Pat<(vti.Vector (fcopysign (vti.Vector vti.RegClass:$rs1),
                                     (vti.Vector (fneg vti.RegClass:$rs2)))),
              (!cast<Instruction>("PseudoVFSGNJN_VV_"# vti.LMul.MX#"_E"#vti.SEW)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs1, vti.RegClass:$rs2, vti.AVL, vti.Log2SEW, TA_MA)>;
    def : Pat<(vti.Vector (fcopysign (vti.Vector vti.RegClass:$rs1),
                                     (vti.Vector (fneg (SplatFPOp vti.ScalarRegClass:$rs2))))),
              (!cast<Instruction>("PseudoVFSGNJN_V"#vti.ScalarSuffix#"_"#vti.LMul.MX#"_E"#vti.SEW)
                   (vti.Vector (IMPLICIT_DEF)),
                   vti.RegClass:$rs1, vti.ScalarRegClass:$rs2, vti.AVL, vti.Log2SEW, TA_MA)>;
  }
}

// 13.11. Vector Floating-Point MIN/MAX Instructions
defm : VPatBinaryFPSDNode_VV_VF<fminnum, "PseudoVFMIN", isSEWAware=1>;
defm : VPatBinaryFPSDNode_VV_VF<fmaxnum, "PseudoVFMAX", isSEWAware=1>;

// 13.13. Vector Floating-Point Compare Instructions
defm : VPatFPSetCCSDNode_VV_VF_FV<SETEQ,  "PseudoVMFEQ", "PseudoVMFEQ">;
defm : VPatFPSetCCSDNode_VV_VF_FV<SETOEQ, "PseudoVMFEQ", "PseudoVMFEQ">;

defm : VPatFPSetCCSDNode_VV_VF_FV<SETNE,  "PseudoVMFNE", "PseudoVMFNE">;
defm : VPatFPSetCCSDNode_VV_VF_FV<SETUNE, "PseudoVMFNE", "PseudoVMFNE">;

defm : VPatFPSetCCSDNode_VV_VF_FV<SETLT,  "PseudoVMFLT", "PseudoVMFGT">;
defm : VPatFPSetCCSDNode_VV_VF_FV<SETOLT, "PseudoVMFLT", "PseudoVMFGT">;

defm : VPatFPSetCCSDNode_VV_VF_FV<SETLE,  "PseudoVMFLE", "PseudoVMFGE">;
defm : VPatFPSetCCSDNode_VV_VF_FV<SETOLE, "PseudoVMFLE", "PseudoVMFGE">;

// Floating-point vselects:
// 11.15. Vector Integer Merge Instructions
// 13.15. Vector Floating-Point Merge Instruction
foreach fvti = !listconcat(AllFloatVectors, AllBFloatVectors) in {
  defvar ivti = GetIntVTypeInfo<fvti>.Vti;
  let Predicates = GetVTypePredicates<ivti>.Predicates in {
    def : Pat<(fvti.Vector (vselect (fvti.Mask V0), fvti.RegClass:$rs1,
                                                          fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVMERGE_VVM_"#fvti.LMul.MX)
                   (fvti.Vector (IMPLICIT_DEF)),
                   fvti.RegClass:$rs2, fvti.RegClass:$rs1, (fvti.Mask V0),
                   fvti.AVL, fvti.Log2SEW)>;

    def : Pat<(fvti.Vector (vselect (fvti.Mask V0),
                                    (SplatFPOp (SelectFPImm (XLenVT GPR:$imm))),
                                    fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVMERGE_VXM_"#fvti.LMul.MX)
                   (fvti.Vector (IMPLICIT_DEF)),
                   fvti.RegClass:$rs2, GPR:$imm, (fvti.Mask V0), fvti.AVL, fvti.Log2SEW)>;

    def : Pat<(fvti.Vector (vselect (fvti.Mask V0),
                                    (SplatFPOp (fvti.Scalar fpimm0)),
                                    fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVMERGE_VIM_"#fvti.LMul.MX)
                   (fvti.Vector (IMPLICIT_DEF)),
                   fvti.RegClass:$rs2, 0, (fvti.Mask V0), fvti.AVL, fvti.Log2SEW)>;
  }
}

foreach fvti = AllFloatVectors in {
  let Predicates = GetVTypePredicates<fvti>.Predicates in
    def : Pat<(fvti.Vector (vselect (fvti.Mask V0),
                                    (SplatFPOp fvti.ScalarRegClass:$rs1),
                                    fvti.RegClass:$rs2)),
              (!cast<Instruction>("PseudoVFMERGE_V"#fvti.ScalarSuffix#"M_"#fvti.LMul.MX)
                   (fvti.Vector (IMPLICIT_DEF)),
                   fvti.RegClass:$rs2,
                   (fvti.Scalar fvti.ScalarRegClass:$rs1),
                   (fvti.Mask V0), fvti.AVL, fvti.Log2SEW)>;
}

// 13.17. Vector Single-Width Floating-Point/Integer Type-Convert Instructions
defm : VPatConvertFP2ISDNode_V<any_fp_to_sint, "PseudoVFCVT_RTZ_X_F_V">;
defm : VPatConvertFP2ISDNode_V<any_fp_to_uint, "PseudoVFCVT_RTZ_XU_F_V">;
defm : VPatConvertI2FPSDNode_V_RM<any_sint_to_fp, "PseudoVFCVT_F_X_V">;
defm : VPatConvertI2FPSDNode_V_RM<any_uint_to_fp, "PseudoVFCVT_F_XU_V">;

// 13.18. Widening Floating-Point/Integer Type-Convert Instructions
defm : VPatWConvertFP2ISDNode_V<any_fp_to_sint, "PseudoVFWCVT_RTZ_X_F_V">;
defm : VPatWConvertFP2ISDNode_V<any_fp_to_uint, "PseudoVFWCVT_RTZ_XU_F_V">;
defm : VPatWConvertI2FPSDNode_V<any_sint_to_fp, "PseudoVFWCVT_F_X_V">;
defm : VPatWConvertI2FPSDNode_V<any_uint_to_fp, "PseudoVFWCVT_F_XU_V">;

// 13.19. Narrowing Floating-Point/Integer Type-Convert Instructions
defm : VPatNConvertFP2ISDNode_W<any_fp_to_sint, "PseudoVFNCVT_RTZ_X_F_W">;
defm : VPatNConvertFP2ISDNode_W<any_fp_to_uint, "PseudoVFNCVT_RTZ_XU_F_W">;
defm : VPatNConvertI2FPSDNode_W_RM<any_sint_to_fp, "PseudoVFNCVT_F_X_W">;
defm : VPatNConvertI2FPSDNode_W_RM<any_uint_to_fp, "PseudoVFNCVT_F_XU_W">;
foreach fvtiToFWti = AllWidenableFloatVectors in {
  defvar fvti = fvtiToFWti.Vti;
  defvar fwti = fvtiToFWti.Wti;
  let Predicates = !if(!eq(fvti.Scalar, f16), [HasVInstructionsF16Minimal],
                       !listconcat(GetVTypePredicates<fvti>.Predicates,
                                   GetVTypePredicates<fwti>.Predicates)) in
  def : Pat<(fvti.Vector (fpround (fwti.Vector fwti.RegClass:$rs1))),
            (!cast<Instruction>("PseudoVFNCVT_F_F_W_"#fvti.LMul.MX#"_E"#fvti.SEW)
                (fvti.Vector (IMPLICIT_DEF)),
                fwti.RegClass:$rs1,
                // Value to indicate no rounding mode change in
                // RISCVInsertReadWriteCSR
                FRM_DYN,
                fvti.AVL, fvti.Log2SEW, TA_MA)>;
}

foreach fvtiToFWti = AllWidenableBFloatToFloatVectors in {
  defvar fvti = fvtiToFWti.Vti;
  defvar fwti = fvtiToFWti.Wti;
  let Predicates = [HasVInstructionsBF16] in
  def : Pat<(fvti.Vector (fpround (fwti.Vector fwti.RegClass:$rs1))),
            (!cast<Instruction>("PseudoVFNCVTBF16_F_F_W_"#fvti.LMul.MX#"_E"#fvti.SEW)
                (fvti.Vector (IMPLICIT_DEF)),
                fwti.RegClass:$rs1,
                // Value to indicate no rounding mode change in
                // RISCVInsertReadWriteCSR
                FRM_DYN,
                fvti.AVL, fvti.Log2SEW, TA_MA)>;
}

//===----------------------------------------------------------------------===//
// Vector Element Extracts
//===----------------------------------------------------------------------===//
foreach vti = AllFloatVectors in {
  defvar vmv_f_s_inst = !cast<Instruction>(!strconcat("PseudoVFMV_",
                                                       vti.ScalarSuffix,
                                                       "_S_", vti.LMul.MX));
  // Only pattern-match extract-element operations where the index is 0. Any
  // other index will have been custom-lowered to slide the vector correctly
  // into place.
  let Predicates = GetVTypePredicates<vti>.Predicates in
  def : Pat<(vti.Scalar (extractelt (vti.Vector vti.RegClass:$rs2), 0)),
            (vmv_f_s_inst vti.RegClass:$rs2, vti.Log2SEW)>;
}
