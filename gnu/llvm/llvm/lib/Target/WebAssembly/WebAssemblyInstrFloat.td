// WebAssemblyInstrFloat.td-WebAssembly Float codegen support ---*- tablegen -*-
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly Floating-point operand code-gen constructs.
///
//===----------------------------------------------------------------------===//

multiclass UnaryFP<SDNode node, string name, bits<32> f32Inst,
                   bits<32> f64Inst> {
  defm _F32 : I<(outs F32:$dst), (ins F32:$src), (outs), (ins),
                [(set F32:$dst, (node F32:$src))],
                !strconcat("f32.", !strconcat(name, "\t$dst, $src")),
                !strconcat("f32.", name), f32Inst>;
  defm _F64 : I<(outs F64:$dst), (ins F64:$src), (outs), (ins),
                [(set F64:$dst, (node F64:$src))],
                !strconcat("f64.", !strconcat(name, "\t$dst, $src")),
                !strconcat("f64.", name), f64Inst>;
}
multiclass BinaryFP<SDNode node, string name, bits<32> f32Inst,
                    bits<32> f64Inst> {
  defm _F32 : I<(outs F32:$dst), (ins F32:$lhs, F32:$rhs), (outs), (ins),
                [(set F32:$dst, (node F32:$lhs, F32:$rhs))],
                !strconcat("f32.", !strconcat(name, "\t$dst, $lhs, $rhs")),
                !strconcat("f32.", name), f32Inst>;
  defm _F64 : I<(outs F64:$dst), (ins F64:$lhs, F64:$rhs), (outs), (ins),
                [(set F64:$dst, (node F64:$lhs, F64:$rhs))],
                !strconcat("f64.", !strconcat(name, "\t$dst, $lhs, $rhs")),
                !strconcat("f64.", name), f64Inst>;
}
multiclass ComparisonFP<CondCode cond, string name, bits<32> f32Inst, bits<32> f64Inst> {
  defm _F32 : I<(outs I32:$dst), (ins F32:$lhs, F32:$rhs), (outs), (ins),
                [(set I32:$dst, (setcc F32:$lhs, F32:$rhs, cond))],
                !strconcat("f32.", !strconcat(name, "\t$dst, $lhs, $rhs")),
                !strconcat("f32.", name), f32Inst>;
  defm  _F64 : I<(outs I32:$dst), (ins F64:$lhs, F64:$rhs), (outs), (ins),
                [(set I32:$dst, (setcc F64:$lhs, F64:$rhs, cond))],
                !strconcat("f64.", !strconcat(name, "\t$dst, $lhs, $rhs")),
                !strconcat("f64.", name), f64Inst>;
}

let isCommutable = 1 in
defm ADD : BinaryFP<fadd, "add ", 0x92, 0xa0>;
defm SUB : BinaryFP<fsub, "sub ", 0x93, 0xa1>;
let isCommutable = 1 in
defm MUL : BinaryFP<fmul, "mul ", 0x94, 0xa2>;
defm DIV : BinaryFP<fdiv, "div ", 0x95, 0xa3>;
defm SQRT : UnaryFP<fsqrt, "sqrt", 0x91, 0x9f>;

defm ABS : UnaryFP<fabs, "abs ", 0x8b, 0x99>;
defm NEG : UnaryFP<fneg, "neg ", 0x8c, 0x9a>;
defm COPYSIGN : BinaryFP<fcopysign, "copysign", 0x98, 0xa6>;

let isCommutable = 1 in {
defm MIN : BinaryFP<fminimum, "min ", 0x96, 0xa4>;
defm MAX : BinaryFP<fmaximum, "max ", 0x97, 0xa5>;
} // isCommutable = 1

defm CEIL : UnaryFP<fceil, "ceil", 0x8d, 0x9b>;
defm FLOOR : UnaryFP<ffloor, "floor", 0x8e, 0x9c>;
defm TRUNC : UnaryFP<ftrunc, "trunc", 0x8f, 0x9d>;
defm NEAREST : UnaryFP<fnearbyint, "nearest", 0x90, 0x9e>;

// DAGCombine oddly folds casts into the rhs of copysign. Unfold them.
def : Pat<(fcopysign F64:$lhs, F32:$rhs),
          (COPYSIGN_F64 F64:$lhs, (F64_PROMOTE_F32 F32:$rhs))>;
def : Pat<(fcopysign F32:$lhs, F64:$rhs),
          (COPYSIGN_F32 F32:$lhs, (F32_DEMOTE_F64 F64:$rhs))>;

// WebAssembly doesn't expose inexact exceptions, so map frint to fnearbyint.
def : Pat<(frint f32:$src), (NEAREST_F32 f32:$src)>;
def : Pat<(frint f64:$src), (NEAREST_F64 f64:$src)>;

// WebAssembly always rounds ties-to-even, so map froundeven to fnearbyint.
def : Pat<(froundeven f32:$src), (NEAREST_F32 f32:$src)>;
def : Pat<(froundeven f64:$src), (NEAREST_F64 f64:$src)>;

let isCommutable = 1 in {
defm EQ : ComparisonFP<SETOEQ, "eq  ", 0x5b, 0x61>;
defm NE : ComparisonFP<SETUNE, "ne  ", 0x5c, 0x62>;
} // isCommutable = 1
defm LT : ComparisonFP<SETOLT, "lt  ", 0x5d, 0x63>;
defm LE : ComparisonFP<SETOLE, "le  ", 0x5f, 0x65>;
defm GT : ComparisonFP<SETOGT, "gt  ", 0x5e, 0x64>;
defm GE : ComparisonFP<SETOGE, "ge  ", 0x60, 0x66>;

// Don't care floating-point comparisons, supported via other comparisons.
def : Pat<(seteq f32:$lhs, f32:$rhs), (EQ_F32 f32:$lhs, f32:$rhs)>;
def : Pat<(setne f32:$lhs, f32:$rhs), (NE_F32 f32:$lhs, f32:$rhs)>;
def : Pat<(setlt f32:$lhs, f32:$rhs), (LT_F32 f32:$lhs, f32:$rhs)>;
def : Pat<(setle f32:$lhs, f32:$rhs), (LE_F32 f32:$lhs, f32:$rhs)>;
def : Pat<(setgt f32:$lhs, f32:$rhs), (GT_F32 f32:$lhs, f32:$rhs)>;
def : Pat<(setge f32:$lhs, f32:$rhs), (GE_F32 f32:$lhs, f32:$rhs)>;
def : Pat<(seteq f64:$lhs, f64:$rhs), (EQ_F64 f64:$lhs, f64:$rhs)>;
def : Pat<(setne f64:$lhs, f64:$rhs), (NE_F64 f64:$lhs, f64:$rhs)>;
def : Pat<(setlt f64:$lhs, f64:$rhs), (LT_F64 f64:$lhs, f64:$rhs)>;
def : Pat<(setle f64:$lhs, f64:$rhs), (LE_F64 f64:$lhs, f64:$rhs)>;
def : Pat<(setgt f64:$lhs, f64:$rhs), (GT_F64 f64:$lhs, f64:$rhs)>;
def : Pat<(setge f64:$lhs, f64:$rhs), (GE_F64 f64:$lhs, f64:$rhs)>;

defm SELECT_F32 : I<(outs F32:$dst), (ins F32:$lhs, F32:$rhs, I32:$cond),
                    (outs), (ins),
                    [(set F32:$dst, (select I32:$cond, F32:$lhs, F32:$rhs))],
                    "f32.select\t$dst, $lhs, $rhs, $cond", "f32.select", 0x1b>;
defm SELECT_F64 : I<(outs F64:$dst), (ins F64:$lhs, F64:$rhs, I32:$cond),
                    (outs), (ins),
                    [(set F64:$dst, (select I32:$cond, F64:$lhs, F64:$rhs))],
                    "f64.select\t$dst, $lhs, $rhs, $cond", "f64.select", 0x1b>;

// ISD::SELECT requires its operand to conform to getBooleanContents, but
// WebAssembly's select interprets any non-zero value as true, so we can fold
// a setne with 0 into a select.
def : Pat<(select (i32 (setne I32:$cond, 0)), F32:$lhs, F32:$rhs),
          (SELECT_F32 F32:$lhs, F32:$rhs, I32:$cond)>;
def : Pat<(select (i32 (setne I32:$cond, 0)), F64:$lhs, F64:$rhs),
          (SELECT_F64 F64:$lhs, F64:$rhs, I32:$cond)>;

// And again, this time with seteq instead of setne and the arms reversed.
def : Pat<(select (i32 (seteq I32:$cond, 0)), F32:$lhs, F32:$rhs),
          (SELECT_F32 F32:$rhs, F32:$lhs, I32:$cond)>;
def : Pat<(select (i32 (seteq I32:$cond, 0)), F64:$lhs, F64:$rhs),
          (SELECT_F64 F64:$rhs, F64:$lhs, I32:$cond)>;
