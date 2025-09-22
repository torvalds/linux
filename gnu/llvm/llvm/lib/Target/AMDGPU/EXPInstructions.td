//===-- EXPInstructions.td - Export Instruction Definitions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// EXP classes
//===----------------------------------------------------------------------===//

class EXPCommon<bit _row, bit _done, string asm = ""> : InstSI<
  (outs),
  (ins exp_tgt:$tgt,
       ExpSrc0:$src0, ExpSrc1:$src1, ExpSrc2:$src2, ExpSrc3:$src3,
       exp_vm:$vm, exp_compr:$compr, i32imm:$en),
  asm> {
  let EXP = 1;
  let EXP_CNT = 1;
  let mayLoad = _done;
  let mayStore = 1;
  let maybeAtomic = 0;
  let UseNamedOperandTable = 1;
  let Uses = !if(_row, [EXEC, M0], [EXEC]);
  let SchedRW = [WriteExport];
  let DisableWQM = 1;

  bit row = _row;
  bit done = _done;
}

class EXP_Pseudo<bit row, bit done>
  : EXPCommon<row, done>, SIMCInstr<NAME, SIEncodingFamily.NONE> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

// Real instruction with optional asm operands "compr" and "vm".
class EXP_Real_ComprVM<EXP_Pseudo ps, int subtarget>
  : EXPCommon<0, ps.done, "exp$tgt $src0, $src1, $src2, $src3"
                       #!if(ps.done, " done", "")#"$compr$vm">,
    SIMCInstr<ps.PseudoInstr, subtarget> {
  let AsmMatchConverter = "cvtExp";
}

// Real instruction with optional asm operand "row_en".
class EXP_Real_Row<EXP_Pseudo ps, int subtarget, string name = "exp">
  : EXPCommon<ps.row, ps.done, name#"$tgt $src0, $src1, $src2, $src3"
                         #!if(ps.done, " done", "")#!if(ps.row, " row_en", "")>,
    SIMCInstr<ps.PseudoInstr, subtarget> {
  let AsmMatchConverter = "cvtExp";
}

//===----------------------------------------------------------------------===//
// EXP Instructions
//===----------------------------------------------------------------------===//

// DONE variants have mayLoad = 1.
// ROW variants have an implicit use of M0.
let SubtargetPredicate = HasExportInsts in {
def EXP          : EXP_Pseudo<0, 0>;
def EXP_DONE     : EXP_Pseudo<0, 1>;
def EXP_ROW      : EXP_Pseudo<1, 0>;
def EXP_ROW_DONE : EXP_Pseudo<1, 1>;
} // let SubtargetPredicate = HasExportInsts

//===----------------------------------------------------------------------===//
// SI, VI, GFX10.
//===----------------------------------------------------------------------===//

multiclass EXP_Real_si {
  defvar ps = !cast<EXP_Pseudo>(NAME);
  def _si : EXP_Real_ComprVM<ps, SIEncodingFamily.SI>, EXPe_ComprVM {
    let AssemblerPredicate = isGFX6GFX7;
    let DecoderNamespace = "GFX6GFX7";
    let done = ps.done;
  }
}

multiclass EXP_Real_vi {
  defvar ps = !cast<EXP_Pseudo>(NAME);
  def _vi : EXP_Real_ComprVM<ps, SIEncodingFamily.VI>, EXPe_vi {
    let AssemblerPredicate = isGFX8GFX9;
    let SubtargetPredicate = isNotGFX90APlus;
    let DecoderNamespace = "GFX8";
    let done = ps.done;
  }
}

multiclass EXP_Real_gfx10 {
  defvar ps = !cast<EXP_Pseudo>(NAME);
  def _gfx10 : EXP_Real_ComprVM<ps, SIEncodingFamily.GFX10>, EXPe_ComprVM {
    let AssemblerPredicate = isGFX10Only;
    let DecoderNamespace = "GFX10";
    let done = ps.done;
  }
}

defm EXP      : EXP_Real_si, EXP_Real_vi, EXP_Real_gfx10;
defm EXP_DONE : EXP_Real_si, EXP_Real_vi, EXP_Real_gfx10;

//===----------------------------------------------------------------------===//
// GFX11, GFX12.
//===----------------------------------------------------------------------===//

multiclass EXP_Real_gfx11 {
  defvar ps = !cast<EXP_Pseudo>(NAME);
  def _gfx11 : EXP_Real_Row<ps, SIEncodingFamily.GFX11>, EXPe_Row {
    let AssemblerPredicate = isGFX11Only;
    let DecoderNamespace = "GFX11";
    let row = ps.row;
    let done = ps.done;
  }
}

multiclass VEXPORT_Real_gfx12 {
  defvar ps = !cast<EXP_Pseudo>(NAME);
  def _gfx12 : EXP_Real_Row<ps, SIEncodingFamily.GFX12, "export">,
    EXPe_Row {
    let AssemblerPredicate = isGFX12Only;
    let DecoderNamespace = "GFX12";
    let row = ps.row;
    let done = ps.done;
  }
  def : AMDGPUMnemonicAlias<"exp", "export"> {
    let AssemblerPredicate = isGFX12Plus;
  }
}

defm EXP          : EXP_Real_gfx11, VEXPORT_Real_gfx12;
defm EXP_DONE     : EXP_Real_gfx11, VEXPORT_Real_gfx12;
defm EXP_ROW      : EXP_Real_gfx11, VEXPORT_Real_gfx12;
defm EXP_ROW_DONE : EXP_Real_gfx11, VEXPORT_Real_gfx12;

//===----------------------------------------------------------------------===//
// EXP Patterns
//===----------------------------------------------------------------------===//

class ExpPattern<ValueType vt, Instruction Inst, int done_val> : GCNPat<
  (int_amdgcn_exp timm:$tgt, timm:$en,
                  (vt ExpSrc0:$src0), (vt ExpSrc1:$src1),
                  (vt ExpSrc2:$src2), (vt ExpSrc3:$src3),
                  done_val, timm:$vm),
  (Inst timm:$tgt, ExpSrc0:$src0, ExpSrc1:$src1,
        ExpSrc2:$src2, ExpSrc3:$src3, timm:$vm, 0, timm:$en)
>;

class ExpRowPattern<ValueType vt, Instruction Inst, int done_val> : GCNPat<
  (int_amdgcn_exp_row timm:$tgt, timm:$en,
                      (vt ExpSrc0:$src0), (vt ExpSrc1:$src1),
                      (vt ExpSrc2:$src2), (vt ExpSrc3:$src3),
                      done_val, M0),
  (Inst timm:$tgt, ExpSrc0:$src0, ExpSrc1:$src1,
        ExpSrc2:$src2, ExpSrc3:$src3, 0, 0, timm:$en)
>;

class ExpComprPattern<ValueType vt, Instruction Inst, int done_val> : GCNPat<
  (int_amdgcn_exp_compr timm:$tgt, timm:$en,
                        (vt ExpSrc0:$src0), (vt ExpSrc1:$src1),
                        done_val, timm:$vm),
  (Inst timm:$tgt, ExpSrc0:$src0, ExpSrc1:$src1,
        (IMPLICIT_DEF), (IMPLICIT_DEF), timm:$vm, 1, timm:$en)
>;

// FIXME: The generated DAG matcher seems to have strange behavior
// with a 1-bit literal to match, so use a -1 for checking a true
// 1-bit value.
def : ExpPattern<i32, EXP, 0>;
def : ExpPattern<i32, EXP_DONE, -1>;
def : ExpPattern<f32, EXP, 0>;
def : ExpPattern<f32, EXP_DONE, -1>;

def : ExpRowPattern<i32, EXP_ROW, 0>;
def : ExpRowPattern<i32, EXP_ROW_DONE, -1>;
def : ExpRowPattern<f32, EXP_ROW, 0>;
def : ExpRowPattern<f32, EXP_ROW_DONE, -1>;

def : ExpComprPattern<v2i16, EXP, 0>;
def : ExpComprPattern<v2i16, EXP_DONE, -1>;
def : ExpComprPattern<v2f16, EXP, 0>;
def : ExpComprPattern<v2f16, EXP_DONE, -1>;
