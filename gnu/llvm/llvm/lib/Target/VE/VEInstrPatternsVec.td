//===-- VEInstrPatternsVec.td - VEC_-type SDNodes and isel for VE Target --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the VEC_* prefixed intermediate SDNodes and their
// isel patterns.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

// Sub-register replication for packed broadcast.
def: Pat<(i64 (repl_f32 f32:$val)),
            (ORrr
              (SRLri (f2l $val), 32),
              (zero_i32 (f2l $val)))>;
def: Pat<(i64 (repl_i32 i32:$val)),
            (ORrr
              (zero_f32 (i2l $val)),
              (SLLri (i2l $val), 32))>;

///// Mask Load & Store /////

// Store for v256i1, v512i1 are implemented in 2 ways.  These STVM/STVM512
// pseudo instruction is used for frameindex related load/store instructions.
// Custom Lowering is used for other load/store instructions.

def : Pat<(v256i1 (load ADDRrii:$addr)),
          (LDVMrii ADDRrii:$addr)>;
def : Pat<(v512i1 (load ADDRrii:$addr)),
          (LDVM512rii ADDRrii:$addr)>;
def : Pat<(store v256i1:$vx, ADDRrii:$addr),
          (STVMrii ADDRrii:$addr, $vx)>;
def : Pat<(store v512i1:$vx, ADDRrii:$addr),
          (STVM512rii ADDRrii:$addr, $vx)>;

multiclass vbrd_elem32<ValueType v32, ValueType s32, SDPatternOperator ImmOp,
                       SDNodeXForm ImmCast, OutPatFrag SuperRegCast> {
  // VBRDil
  def : Pat<(v32 (vec_broadcast (s32 ImmOp:$sy), i32:$vl)),
            (VBRDil (ImmCast $sy), i32:$vl)>;

  // VBRDrl
  def : Pat<(v32 (vec_broadcast s32:$sy, i32:$vl)),
            (VBRDrl (SuperRegCast $sy), i32:$vl)>;
}

multiclass vbrd_elem64<ValueType v64, ValueType s64,
                       SDPatternOperator ImmOp, SDNodeXForm ImmCast> {
  // VBRDil
  def : Pat<(v64 (vec_broadcast (s64 ImmOp:$sy), i32:$vl)),
            (VBRDil (ImmCast $sy), i32:$vl)>;

  // VBRDrl
  def : Pat<(v64 (vec_broadcast s64:$sy, i32:$vl)),
            (VBRDrl s64:$sy, i32:$vl)>;
}

multiclass extract_insert_elem32<ValueType v32, ValueType s32,
                                 OutPatFrag SubRegCast,
                                 OutPatFrag SuperRegCast> {
  // LVSvi
  def: Pat<(s32 (extractelt v32:$vec, uimm7:$idx)),
           (SubRegCast (LVSvi v32:$vec, (ULO7 $idx)))>;
  // LVSvr
  def: Pat<(s32 (extractelt v32:$vec, i64:$idx)),
           (SubRegCast (LVSvr v32:$vec, $idx))>;

  // LSVir
  def: Pat<(v32 (insertelt v32:$vec, s32:$val, uimm7:$idx)),
           (LSVir_v (ULO7 $idx), (SuperRegCast $val), $vec)>;
  // LSVrr
  def: Pat<(v32 (insertelt v32:$vec, s32:$val, i64:$idx)),
           (LSVrr_v $idx, (SuperRegCast $val), $vec)>;
}

multiclass extract_insert_elem64<ValueType v64, ValueType s64> {
  // LVSvi
  def: Pat<(s64 (extractelt v64:$vec, uimm7:$idx)),
           (LVSvi v64:$vec, (ULO7 $idx))>;
  // LVSvr
  def: Pat<(s64 (extractelt v64:$vec, i64:$idx)),
           (LVSvr v64:$vec, $idx)>;

  // LSVir
  def: Pat<(v64 (insertelt v64:$vec, s64:$val, uimm7:$idx)),
           (LSVir_v (ULO7 $idx), $val, $vec)>;
  // LSVrr
  def: Pat<(v64 (insertelt v64:$vec, s64:$val, i64:$idx)),
           (LSVrr_v $idx, $val, $vec)>;
}

multiclass patterns_elem32<ValueType v32, ValueType s32,
                           SDPatternOperator ImmOp, SDNodeXForm ImmCast,
                           OutPatFrag SubRegCast, OutPatFrag SuperRegCast> {
  defm : vbrd_elem32<v32, s32, ImmOp, ImmCast, SuperRegCast>;
  defm : extract_insert_elem32<v32, s32, SubRegCast, SuperRegCast>;
}

multiclass patterns_elem64<ValueType v64, ValueType s64,
                           SDPatternOperator ImmOp, SDNodeXForm ImmCast> {
  defm : vbrd_elem64<v64, s64, ImmOp, ImmCast>;
  defm : extract_insert_elem64<v64, s64>;
}

defm : patterns_elem32<v256i32, i32, simm7, LO7, l2i, i2l>;
defm : patterns_elem32<v256f32, f32, simm7fp, LO7FP, l2f, f2l>;

defm : patterns_elem64<v256i64, i64, simm7, LO7>;
defm : patterns_elem64<v256f64, f64, simm7fp, LO7FP>;

defm : vbrd_elem64<v512i32, i64, simm7, LO7>;
defm : vbrd_elem64<v512f32, i64, simm7, LO7>;
defm : vbrd_elem64<v512i32, f64, simm7fp, LO7FP>;
defm : vbrd_elem64<v512f32, f64, simm7fp, LO7FP>;

class Mask_Binary<ValueType MaskVT, SDPatternOperator MaskOp, string InstName> :
  Pat<(MaskVT (MaskOp MaskVT:$ma, MaskVT:$mb)), (!cast<Instruction>(InstName#"mm") $ma, $mb)>;

def: Mask_Binary<v256i1, and, "ANDM">;
def: Mask_Binary<v256i1, or,  "ORM">;
def: Mask_Binary<v256i1, xor, "XORM">;

///// Packing support /////

// v256i1 <> v512i1
def : Pat<(v256i1 (vec_unpack_lo v512i1:$vm, (i32 srcvalue))),
          (EXTRACT_SUBREG $vm, sub_vm_odd)>;
def : Pat<(v256i1 (vec_unpack_hi v512i1:$vm, (i32 srcvalue))),
          (EXTRACT_SUBREG $vm, sub_vm_even)>;
def : Pat<(v512i1 (vec_pack v256i1:$vlo, v256i1:$vhi, (i32 srcvalue))),
          (INSERT_SUBREG (INSERT_SUBREG
                         (v512i1 (IMPLICIT_DEF)),
                         $vlo, sub_vm_odd),
                         $vhi, sub_vm_even)>;

// v256.32 <> v512.32
multiclass Packing<ValueType PackVT> {
  // no-op unpacks
  def : Pat<(v256i32 (vec_unpack_lo PackVT:$vp, (i32 srcvalue))),
            (COPY_TO_REGCLASS $vp, V64)>;
  def : Pat<(v256f32 (vec_unpack_hi PackVT:$vp, (i32 srcvalue))),
            (COPY_TO_REGCLASS $vp, V64)>;

  // shuffle unpacks
  def : Pat<(v256f32 (vec_unpack_lo PackVT:$vp, i32:$avl)),
            (VSHFvvil $vp, $vp, 4, $avl)>; // always pick lo
  def : Pat<(v256i32 (vec_unpack_hi PackVT:$vp, i32:$avl)),
            (VSHFvvil $vp, $vp, 0, $avl)>; // always pick hi
}

defm : Packing<v512i32>;
defm : Packing<v512f32>;

def : Pat<(v512i32 (vec_pack v256i32:$vlo, v256i32:$vhi, i32:$avl)),
          (VSHFvvil $vlo, $vhi, 13, $avl)>;
def : Pat<(v512f32 (vec_pack v256f32:$vlo, v256f32:$vhi, i32:$avl)),
          (VSHFvvil $vlo, $vhi, 8, $avl)>;
