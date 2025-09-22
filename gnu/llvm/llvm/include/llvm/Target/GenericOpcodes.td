//===-- GenericOpcodes.td - Opcodes used with GlobalISel ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the generic opcodes used with GlobalISel.
// After instruction selection, these opcodes should not appear.
//
//===----------------------------------------------------------------------===//

//------------------------------------------------------------------------------
// Unary ops.
//------------------------------------------------------------------------------

class GenericInstruction : StandardPseudoInstruction {
  let isPreISelOpcode = true;

  // When all variadic ops share a type with another operand,
  // this is the type they share. Used by MIR patterns type inference.
  TypedOperand variadicOpsType = ?;
}

// Provide a variant of an instruction with the same operands, but
// different instruction flags. This is intended to provide a
// convenient way to define strict floating point variants of ordinary
// floating point instructions.
class ConstrainedInstruction<GenericInstruction baseInst> :
  GenericInstruction {
  let OutOperandList = baseInst.OutOperandList;
  let InOperandList =  baseInst.InOperandList;
  let isCommutable = baseInst.isCommutable;

  // TODO: Do we need a better way to mark reads from FP mode than
  // hasSideEffects?
  let hasSideEffects = true;
  let mayRaiseFPException = true;
}

// Extend the underlying scalar type of an operation, leaving the high bits
// unspecified.
def G_ANYEXT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

// Sign extend the underlying scalar type of an operation, copying the sign bit
// into the newly-created space.
def G_SEXT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

// Sign extend the a value from an arbitrary bit position, copying the sign bit
// into all bits above it. This is equivalent to a shl + ashr pair with an
// appropriate shift amount. $sz is an immediate (MachineOperand::isImm()
// returns true) to allow targets to have some bitwidths legal and others
// lowered. This opcode is particularly useful if the target has sign-extension
// instructions that are cheaper than the constituent shifts as the optimizer is
// able to make decisions on whether it's better to hang on to the G_SEXT_INREG
// or to lower it and optimize the individual shifts.
def G_SEXT_INREG : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, untyped_imm_0:$sz);
  let hasSideEffects = false;
}

// Zero extend the underlying scalar type of an operation, putting zero bits
// into the newly-created space.
def G_ZEXT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}


// Truncate the underlying scalar type of an operation. This is equivalent to
// G_EXTRACT for scalar types, but acts elementwise on vectors.
def G_TRUNC : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_IMPLICIT_DEF : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins);
  let hasSideEffects = false;
}

def G_PHI : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins variable_ops);
  let hasSideEffects = false;
}

def G_FRAME_INDEX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$src2);
  let hasSideEffects = false;
}

def G_GLOBAL_VALUE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$src);
  let hasSideEffects = false;
}

def G_PTRAUTH_GLOBAL_VALUE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$addr, i32imm:$key, type1:$addrdisc, i64imm:$disc);
  let hasSideEffects = 0;
}

def G_CONSTANT_POOL : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$src);
  let hasSideEffects = false;
}

def G_INTTOPTR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_PTRTOINT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_BITCAST : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

// Only supports scalar result types
def G_CONSTANT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$imm);
  let hasSideEffects = false;
}

// Only supports scalar result types
def G_FCONSTANT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$imm);
  let hasSideEffects = false;
}

def G_VASTART : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins type0:$list);
  let hasSideEffects = false;
  let mayStore = true;
}

def G_VAARG : GenericInstruction {
  let OutOperandList = (outs type0:$val);
  let InOperandList = (ins type1:$list, unknown:$align);
  let hasSideEffects = false;
  let mayLoad = true;
  let mayStore = true;
}

def G_CTLZ : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_CTLZ_ZERO_UNDEF : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_CTTZ : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_CTTZ_ZERO_UNDEF : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_CTPOP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_BSWAP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = false;
}

def G_BITREVERSE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = false;
}

def G_ADDRSPACE_CAST : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_BLOCK_ADDR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$ba);
  let hasSideEffects = false;
}

def G_JUMP_TABLE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$jti);
  let hasSideEffects = false;
}

def G_DYN_STACKALLOC : GenericInstruction {
  let OutOperandList = (outs ptype0:$dst);
  let InOperandList = (ins type1:$size, i32imm:$align);
  let hasSideEffects = true;
}

def G_STACKSAVE : GenericInstruction {
  let OutOperandList = (outs ptype0:$dst);
  let InOperandList = (ins);
  let hasSideEffects = true;
}

def G_STACKRESTORE : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptype0:$src);
  let hasSideEffects = true;
}

def G_FREEZE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = false;
}

def G_LROUND: GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_LLROUND: GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

//------------------------------------------------------------------------------
// Binary ops.
//------------------------------------------------------------------------------

// Generic addition.
def G_ADD : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic subtraction.
def G_SUB : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic multiplication.
def G_MUL : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic signed division.
def G_SDIV : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic unsigned division.
def G_UDIV : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic signed remainder.
def G_SREM : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic unsigned remainder.
def G_UREM : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic signed division and remainder.
def G_SDIVREM : GenericInstruction {
  let OutOperandList = (outs type0:$div, type0:$rem);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic unsigned division and remainder.
def G_UDIVREM : GenericInstruction {
  let OutOperandList = (outs type0:$div, type0:$rem);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic bitwise and.
def G_AND : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic bitwise or.
def G_OR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic bitwise xor.
def G_XOR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic left-shift.
def G_SHL : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type1:$src2);
  let hasSideEffects = false;
}

// Generic logical right-shift.
def G_LSHR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type1:$src2);
  let hasSideEffects = false;
}

// Generic arithmetic right-shift.
def G_ASHR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type1:$src2);
  let hasSideEffects = false;
}

/// Funnel 'double' shifts take 3 operands, 2 inputs and the shift amount.
/// fshl(X,Y,Z): (X << (Z % bitwidth)) | (Y >> (bitwidth - (Z % bitwidth)))
def G_FSHL : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2, type1:$src3);
  let hasSideEffects = false;
}

/// Funnel 'double' shifts take 3 operands, 2 inputs and the shift amount.
/// fshr(X,Y,Z): (X << (bitwidth - (Z % bitwidth))) | (Y >> (Z % bitwidth))
def G_FSHR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2, type1:$src3);
  let hasSideEffects = false;
}

/// Rotate bits right.
def G_ROTR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type1:$src2);
  let hasSideEffects = false;
}

/// Rotate bits left.
def G_ROTL : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type1:$src2);
  let hasSideEffects = false;
}

// Generic integer comparison.
def G_ICMP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$tst, type1:$src1, type1:$src2);
  let hasSideEffects = false;
}

// Generic floating-point comparison.
def G_FCMP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$tst, type1:$src1, type1:$src2);
  let hasSideEffects = false;
}

// Generic signed three-way comparison.
def G_SCMP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src1, type1:$src2);
  let hasSideEffects = false;
}

// Generic unsigned three-way comparison.
def G_UCMP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src1, type1:$src2);
  let hasSideEffects = false;
}

// Generic select
def G_SELECT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$tst, type0:$src1, type0:$src2);
  let hasSideEffects = false;
}

// Generic pointer offset.
def G_PTR_ADD : GenericInstruction {
  let OutOperandList = (outs ptype0:$dst);
  let InOperandList = (ins ptype0:$src1, type1:$src2);
  let hasSideEffects = false;
}

// Generic pointer mask. type1 should be an integer with the same
// bitwidth as the pointer type.
def G_PTRMASK : GenericInstruction {
  let OutOperandList = (outs ptype0:$dst);
  let InOperandList = (ins ptype0:$src, type1:$bits);
  let hasSideEffects = false;
}

// Generic signed integer minimum.
def G_SMIN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic signed integer maximum.
def G_SMAX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic unsigned integer minimum.
def G_UMIN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic unsigned integer maximum.
def G_UMAX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic integer absolute value.
def G_ABS : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = false;
}

//------------------------------------------------------------------------------
// Overflow ops
//------------------------------------------------------------------------------

// Generic unsigned addition producing a carry flag.
def G_UADDO : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic unsigned addition consuming and producing a carry flag.
def G_UADDE : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2, type1:$carry_in);
  let hasSideEffects = false;
}

// Generic signed addition producing a carry flag.
def G_SADDO : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic signed addition consuming and producing a carry flag.
def G_SADDE : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2, type1:$carry_in);
  let hasSideEffects = false;
}

// Generic unsigned subtraction producing a carry flag.
def G_USUBO : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}
// Generic unsigned subtraction consuming and producing a carry flag.
def G_USUBE : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2, type1:$carry_in);
  let hasSideEffects = false;
}

// Generic signed subtraction producing a carry flag.
def G_SSUBO : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}

// Generic signed subtraction consuming and producing a carry flag.
def G_SSUBE : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2, type1:$carry_in);
  let hasSideEffects = false;
}

// Generic unsigned multiplication producing a carry flag.
def G_UMULO : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic signed multiplication producing a carry flag.
def G_SMULO : GenericInstruction {
  let OutOperandList = (outs type0:$dst, type1:$carry_out);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Multiply two numbers at twice the incoming bit width (unsigned) and return
// the high half of the result.
def G_UMULH : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Multiply two numbers at twice the incoming bit width (signed) and return
// the high half of the result.
def G_SMULH : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

//------------------------------------------------------------------------------
// Saturating ops
//------------------------------------------------------------------------------

// Generic saturating unsigned addition.
def G_UADDSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic saturating signed addition.
def G_SADDSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic saturating unsigned subtraction.
def G_USUBSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic saturating signed subtraction.
def G_SSUBSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic saturating unsigned left shift.
def G_USHLSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type1:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic saturating signed left shift.
def G_SSHLSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type1:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

/// RESULT = [US]MULFIX(LHS, RHS, SCALE) - Perform fixed point
/// multiplication on 2 integers with the same width and scale. SCALE
/// represents the scale of both operands as fixed point numbers. This
/// SCALE parameter must be a constant integer. A scale of zero is
/// effectively performing multiplication on 2 integers.
def G_SMULFIX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, untyped_imm_0:$scale);
  let hasSideEffects = false;
  let isCommutable = true;
}

def G_UMULFIX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, untyped_imm_0:$scale);
  let hasSideEffects = false;
  let isCommutable = true;
}

/// Same as the corresponding unsaturated fixed point instructions, but the
/// result is clamped between the min and max values representable by the
/// bits of the first 2 operands.
def G_SMULFIXSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, untyped_imm_0:$scale);
  let hasSideEffects = false;
  let isCommutable = true;
}

def G_UMULFIXSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, untyped_imm_0:$scale);
  let hasSideEffects = false;
  let isCommutable = true;
}

/// RESULT = [US]DIVFIX(LHS, RHS, SCALE) - Perform fixed point division on
/// 2 integers with the same width and scale. SCALE represents the scale
/// of both operands as fixed point numbers. This SCALE parameter must be a
/// constant integer.
def G_SDIVFIX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, untyped_imm_0:$scale);
  let hasSideEffects = false;
  let isCommutable = false;
}

def G_UDIVFIX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, untyped_imm_0:$scale);
  let hasSideEffects = false;
  let isCommutable = false;
}

/// Same as the corresponding unsaturated fixed point instructions,
/// but the result is clamped between the min and max values
/// representable by the bits of the first 2 operands.
def G_SDIVFIXSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, untyped_imm_0:$scale);
  let hasSideEffects = false;
  let isCommutable = false;
}

def G_UDIVFIXSAT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type0:$src1, untyped_imm_0:$scale);
  let hasSideEffects = false;
  let isCommutable = false;
}

//------------------------------------------------------------------------------
// Floating Point Unary Ops.
//------------------------------------------------------------------------------

def G_FNEG : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = false;
}

def G_FPEXT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_FPTRUNC : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_FPTOSI : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_FPTOUI : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_SITOFP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_UITOFP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_FABS : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = false;
}

def G_FCOPYSIGN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type1:$src1);
  let hasSideEffects = false;
}

def G_FCANONICALIZE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = false;
}

// Generic opcode equivalent to the llvm.is_fpclass intrinsic.
def G_IS_FPCLASS: GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src, unknown:$test);
  let hasSideEffects = false;
}

// FMINNUM/FMAXNUM - Perform floating-point minimum or maximum on two
// values.
//
// In the case where a single input is a NaN (either signaling or quiet),
// the non-NaN input is returned.
//
// The return value of (FMINNUM 0.0, -0.0) could be either 0.0 or -0.0.
def G_FMINNUM : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

def G_FMAXNUM : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// FMINNUM_IEEE/FMAXNUM_IEEE - Perform floating-point minimumNumber or
// maximumNumber on two values, following IEEE-754 definitions. This
// differs from FMINNUM/FMAXNUM in the handling of signaling NaNs, and
// signed zero.
//
// If one input is a signaling NaN, returns a quiet NaN. This matches
// IEEE-754 2008's minnum/maxnum behavior for signaling NaNs (which
// differs from 2019).
//
// These treat -0 as ordered less than +0, matching the behavior of
// IEEE-754 2019's minimumNumber/maximumNumber.
def G_FMINNUM_IEEE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

def G_FMAXNUM_IEEE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// FMINIMUM/FMAXIMUM - NaN-propagating minimum/maximum that also treat -0.0
// as less than 0.0. While FMINNUM_IEEE/FMAXNUM_IEEE follow IEEE 754-2008
// semantics, FMINIMUM/FMAXIMUM follow IEEE 754-2019 semantics.
def G_FMINIMUM : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

def G_FMAXIMUM : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

//------------------------------------------------------------------------------
// Floating Point Binary ops.
//------------------------------------------------------------------------------

// Generic FP addition.
def G_FADD : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic FP subtraction.
def G_FSUB : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic FP multiplication.
def G_FMUL : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
  let isCommutable = true;
}

// Generic fused multiply-add instruction.
// Behaves like llvm fma intrinsic ie src1 * src2 + src3
def G_FMA : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2, type0:$src3);
  let hasSideEffects = false;
  let isCommutable = false;
}

/// Generic FP multiply and add. Perform a * b + c, while getting the
/// same result as the separately rounded operations, unlike G_FMA.
def G_FMAD : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2, type0:$src3);
  let hasSideEffects = false;
  let isCommutable = false;
}

// Generic FP division.
def G_FDIV : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}

// Generic FP remainder.
def G_FREM : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}

// Floating point exponentiation.
def G_FPOW : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1, type0:$src2);
  let hasSideEffects = false;
}

// Floating point exponentiation, with an integer power.
def G_FPOWI : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type1:$src1);
  let hasSideEffects = false;
}

// Floating point base-e exponential of a value.
def G_FEXP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point base-2 exponential of a value.
def G_FEXP2 : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point base-10 exponential of a value.
def G_FEXP10 : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point base-e logarithm of a value.
def G_FLOG : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point base-2 logarithm of a value.
def G_FLOG2 : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point base-10 logarithm of a value.
def G_FLOG10 : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point x * 2^n
def G_FLDEXP : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type1:$src1);
  let hasSideEffects = false;
}

// Floating point frexp
def G_FFREXP : GenericInstruction {
  let OutOperandList = (outs type0:$dst0, type1:$dst1);
  let InOperandList = (ins type0:$src0);
  let hasSideEffects = false;
}

// Floating point ceiling of a value.
def G_FCEIL : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point cosine of a value.
def G_FCOS : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point sine of a value.
def G_FSIN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point tangent of a value.
def G_FTAN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point arccosine of a value.
def G_FACOS : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point arcsine of a value.
def G_FASIN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point arctangent of a value.
def G_FATAN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point hyperbolic cosine of a value.
def G_FCOSH : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point hyperbolic sine of a value.
def G_FSINH : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point hyperbolic tangent of a value.
def G_FTANH : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point square root of a value.
// This returns NaN for negative nonzero values.
// NOTE: Unlike libm sqrt(), this never sets errno. In all other respects it's
// libm-conformant.
def G_FSQRT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point floor of a value.
def G_FFLOOR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point round to next integer.
def G_FRINT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

// Floating point round to the nearest integer.
def G_FNEARBYINT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

//------------------------------------------------------------------------------
// Access to floating-point environment.
//------------------------------------------------------------------------------

// These operations read/write floating-point environment. The interaction with
// it is modeled as a side effect, because constrained intrinsics use the same
// method.

// Reading floating-point environment.
def G_GET_FPENV : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins);
  let hasSideEffects = true;
}

// Setting floating-point environment.
def G_SET_FPENV : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = true;
}

// Setting default floating-point environment.
def G_RESET_FPENV : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let hasSideEffects = true;
}

// Reading floating-point control modes.
def G_GET_FPMODE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins);
  let hasSideEffects = true;
}

// Setting floating-point control modes.
def G_SET_FPMODE : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = true;
}

// Setting floating-point control modes to default state.
def G_RESET_FPMODE : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let hasSideEffects = true;
}

//------------------------------------------------------------------------------
// Opcodes for LLVM Intrinsics
//------------------------------------------------------------------------------
def G_INTRINSIC_FPTRUNC_ROUND : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src1, i32imm:$round_mode);
  let hasSideEffects = false;
}

def G_INTRINSIC_TRUNC : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

def G_INTRINSIC_ROUND : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

def G_INTRINSIC_LRINT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_INTRINSIC_LLRINT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
}

def G_INTRINSIC_ROUNDEVEN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
}

def G_READCYCLECOUNTER : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins);
  let hasSideEffects = true;
}

def G_READSTEADYCOUNTER : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins);
  let hasSideEffects = true;
}

//------------------------------------------------------------------------------
// Memory ops
//------------------------------------------------------------------------------

// Generic load. Expects a MachineMemOperand in addition to explicit
// operands. If the result size is larger than the memory size, the
// high bits are undefined. If the result is a vector type and larger
// than the memory size, the high elements are undefined (i.e. this is
// not a per-element, vector anyextload)
def G_LOAD : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins ptype1:$addr);
  let hasSideEffects = false;
  let mayLoad = true;
}

// Generic sign-extended load. Expects a MachineMemOperand in addition to explicit operands.
def G_SEXTLOAD : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins ptype1:$addr);
  let hasSideEffects = false;
  let mayLoad = true;
}

// Generic zero-extended load. Expects a MachineMemOperand in addition to explicit operands.
def G_ZEXTLOAD : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins ptype1:$addr);
  let hasSideEffects = false;
  let mayLoad = true;
}

// Generic indexed load. Combines a GEP with a load. $newaddr is set to $base + $offset.
// If $am is 0 (post-indexed), then the value is loaded from $base; if $am is 1 (pre-indexed)
//  then the value is loaded from $newaddr.
def G_INDEXED_LOAD : GenericInstruction {
  let OutOperandList = (outs type0:$dst, ptype1:$newaddr);
  let InOperandList = (ins ptype1:$base, type2:$offset, unknown:$am);
  let hasSideEffects = false;
  let mayLoad = true;
}

// Same as G_INDEXED_LOAD except that the load performed is sign-extending, as with G_SEXTLOAD.
def G_INDEXED_SEXTLOAD : GenericInstruction {
  let OutOperandList = (outs type0:$dst, ptype1:$newaddr);
  let InOperandList = (ins ptype1:$base, type2:$offset, unknown:$am);
  let hasSideEffects = false;
  let mayLoad = true;
}

// Same as G_INDEXED_LOAD except that the load performed is zero-extending, as with G_ZEXTLOAD.
def G_INDEXED_ZEXTLOAD : GenericInstruction {
  let OutOperandList = (outs type0:$dst, ptype1:$newaddr);
  let InOperandList = (ins ptype1:$base, type2:$offset, unknown:$am);
  let hasSideEffects = false;
  let mayLoad = true;
}

// Generic store. Expects a MachineMemOperand in addition to explicit operands.
def G_STORE : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins type0:$src, ptype1:$addr);
  let hasSideEffects = false;
  let mayStore = true;
}

// Combines a store with a GEP. See description of G_INDEXED_LOAD for indexing behaviour.
def G_INDEXED_STORE : GenericInstruction {
  let OutOperandList = (outs ptype0:$newaddr);
  let InOperandList = (ins type1:$src, ptype0:$base, ptype2:$offset,
                           unknown:$am);
  let hasSideEffects = false;
  let mayStore = true;
}

// Generic atomic cmpxchg with internal success check. Expects a
// MachineMemOperand in addition to explicit operands.
def G_ATOMIC_CMPXCHG_WITH_SUCCESS : GenericInstruction {
  let OutOperandList = (outs type0:$oldval, type1:$success);
  let InOperandList = (ins type2:$addr, type0:$cmpval, type0:$newval);
  let hasSideEffects = false;
  let mayLoad = true;
  let mayStore = true;
}

// Generic atomic cmpxchg. Expects a MachineMemOperand in addition to explicit
// operands.
def G_ATOMIC_CMPXCHG : GenericInstruction {
  let OutOperandList = (outs type0:$oldval);
  let InOperandList = (ins ptype1:$addr, type0:$cmpval, type0:$newval);
  let hasSideEffects = false;
  let mayLoad = true;
  let mayStore = true;
}

// Generic atomicrmw. Expects a MachineMemOperand in addition to explicit
// operands.
class G_ATOMICRMW_OP : GenericInstruction {
  let OutOperandList = (outs type0:$oldval);
  let InOperandList = (ins ptype1:$addr, type0:$val);
  let hasSideEffects = false;
  let mayLoad = true;
  let mayStore = true;
}

def G_ATOMICRMW_XCHG : G_ATOMICRMW_OP;
def G_ATOMICRMW_ADD : G_ATOMICRMW_OP;
def G_ATOMICRMW_SUB : G_ATOMICRMW_OP;
def G_ATOMICRMW_AND : G_ATOMICRMW_OP;
def G_ATOMICRMW_NAND : G_ATOMICRMW_OP;
def G_ATOMICRMW_OR : G_ATOMICRMW_OP;
def G_ATOMICRMW_XOR : G_ATOMICRMW_OP;
def G_ATOMICRMW_MAX : G_ATOMICRMW_OP;
def G_ATOMICRMW_MIN : G_ATOMICRMW_OP;
def G_ATOMICRMW_UMAX : G_ATOMICRMW_OP;
def G_ATOMICRMW_UMIN : G_ATOMICRMW_OP;
def G_ATOMICRMW_FADD : G_ATOMICRMW_OP;
def G_ATOMICRMW_FSUB : G_ATOMICRMW_OP;
def G_ATOMICRMW_FMAX : G_ATOMICRMW_OP;
def G_ATOMICRMW_FMIN : G_ATOMICRMW_OP;
def G_ATOMICRMW_UINC_WRAP : G_ATOMICRMW_OP;
def G_ATOMICRMW_UDEC_WRAP : G_ATOMICRMW_OP;

def G_FENCE : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i32imm:$ordering, i32imm:$scope);
  let hasSideEffects = true;
}

// Generic opcode equivalent to the llvm.prefetch intrinsic.
def G_PREFETCH : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptype0:$address, i32imm:$rw, i32imm:$locality, i32imm:$cachetype);
  let hasSideEffects = true;
  let mayLoad = true;
  let mayStore = true;
}

//------------------------------------------------------------------------------
// Variadic ops
//------------------------------------------------------------------------------

// Extract a register of the specified size, starting from the block given by
// index. This will almost certainly be mapped to sub-register COPYs after
// register banks have been selected.
def G_EXTRACT : GenericInstruction {
  let OutOperandList = (outs type0:$res);
  let InOperandList = (ins type1:$src, untyped_imm_0:$offset);
  let hasSideEffects = false;
}

// Extract multiple registers specified size, starting from blocks given by
// indexes. This will almost certainly be mapped to sub-register COPYs after
// register banks have been selected.
// The output operands are always ordered from lowest bits to highest:
//   %bits_0_7:(s8), %bits_8_15:(s8),
//       %bits_16_23:(s8), %bits_24_31:(s8) = G_UNMERGE_VALUES %0:(s32)
def G_UNMERGE_VALUES : GenericInstruction {
  let OutOperandList = (outs type0:$dst0, variable_ops);
  let InOperandList = (ins type1:$src);
  let hasSideEffects = false;
  let variadicOpsType = type0;
}

// Insert a smaller register into a larger one at the specified bit-index.
def G_INSERT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, type1:$op, untyped_imm_0:$offset);
  let hasSideEffects = false;
}

// Concatenate multiple registers of the same size into a wider register.
// The input operands are always ordered from lowest bits to highest:
//   %0:(s32) = G_MERGE_VALUES %bits_0_7:(s8), %bits_8_15:(s8),
//                             %bits_16_23:(s8), %bits_24_31:(s8)
def G_MERGE_VALUES : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src0, variable_ops);
  let hasSideEffects = false;
  let variadicOpsType = type1;
}

// Generic vscale.
// Puts the value of the runtime vscale multiplied by the value in the source
// operand into the destination register.
def G_VSCALE : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$src);
  let hasSideEffects = false;
}

/// Create a vector from multiple scalar registers. No implicit
/// conversion is performed (i.e. the result element type must be the
/// same as all source operands)
def G_BUILD_VECTOR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src0, variable_ops);
  let hasSideEffects = false;
  let variadicOpsType = type1;
}

/// Like G_BUILD_VECTOR, but truncates the larger operand types to fit the
/// destination vector elt type.
def G_BUILD_VECTOR_TRUNC : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src0, variable_ops);
  let hasSideEffects = false;
}

/// Create a vector by concatenating vectors together.
def G_CONCAT_VECTORS : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src0, variable_ops);
  let hasSideEffects = false;
}

// Intrinsic without side effects.
def G_INTRINSIC : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = false;
}

// Intrinsic with side effects.
def G_INTRINSIC_W_SIDE_EFFECTS : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = true;
  let mayLoad = true;
  let mayStore = true;
}

// Convergent intrinsic without side effects.
def G_INTRINSIC_CONVERGENT : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = false;
  let isConvergent = true;
}

// Convergent intrinsic with side effects.
def G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$intrin, variable_ops);
  let hasSideEffects = true;
  let mayLoad = true;
  let mayStore = true;
  let isConvergent = true;
}

//------------------------------------------------------------------------------
// Branches.
//------------------------------------------------------------------------------

// Generic unconditional branch.
def G_BR : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$src1);
  let hasSideEffects = false;
  let isBranch = true;
  let isTerminator = true;
  let isBarrier = true;
}

// Generic conditional branch.
def G_BRCOND : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins type0:$tst, unknown:$truebb);
  let hasSideEffects = false;
  let isBranch = true;
  let isTerminator = true;
}

// Generic indirect branch.
def G_BRINDIRECT : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins type0:$src1);
  let hasSideEffects = false;
  let isBranch = true;
  let isTerminator = true;
  let isBarrier = true;
  let isIndirectBranch = true;
}

// Generic branch to jump table entry
def G_BRJT : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptype0:$tbl, unknown:$jti, type1:$idx);
  let hasSideEffects = false;
  let isBranch = true;
  let isTerminator = true;
  let isBarrier = true;
  let isIndirectBranch = true;
}

// A marker to signal the following code is an invoke region, that may throw
// an exception and therefore not return.
def G_INVOKE_REGION_START : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let isTerminator = true; // This must be a terminator.
  let hasSideEffects = false;
}

def G_READ_REGISTER : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins unknown:$register);
  let hasSideEffects = true;

  // Assume convergent. It's probably not worth the effort of somehow
  // modeling convergent and nonconvergent register accesses.
  let isConvergent = true;
}

def G_WRITE_REGISTER : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins unknown:$register, type0:$value);
  let hasSideEffects = true;

  // Assume convergent. It's probably not worth the effort of somehow
  // modeling convergent and nonconvergent register accesses.
  let isConvergent = true;
}

//------------------------------------------------------------------------------
// Vector ops
//------------------------------------------------------------------------------

// Generic insert subvector.
def G_INSERT_SUBVECTOR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src0, type1:$src1, untyped_imm_0:$idx);
  let hasSideEffects = false;
}

// Generic extract subvector.
def G_EXTRACT_SUBVECTOR : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, untyped_imm_0:$idx);
  let hasSideEffects = false;
}

// Generic insertelement.
def G_INSERT_VECTOR_ELT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, type1:$elt, type2:$idx);
  let hasSideEffects = false;
}

// Generic extractelement.
def G_EXTRACT_VECTOR_ELT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$src, type2:$idx);
  let hasSideEffects = false;
}

// Generic shufflevector.
//
// The mask operand should be an IR Constant which exactly matches the
// corresponding mask for the IR shufflevector instruction.
def G_SHUFFLE_VECTOR: GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$v1, type1:$v2, unknown:$mask);
  let hasSideEffects = false;
}

// Generic splatvector.
def G_SPLAT_VECTOR: GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$val);
  let hasSideEffects = false;
}

// Generic masked compress.
def G_VECTOR_COMPRESS: GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$vec, type1:$mask, type0:$passthru);
  let hasSideEffects = false;
}

//------------------------------------------------------------------------------
// Vector reductions
//------------------------------------------------------------------------------

class VectorReduction : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$v);
  let hasSideEffects = false;
}

def G_VECREDUCE_SEQ_FADD : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$acc, type2:$v);
  let hasSideEffects = false;
}

def G_VECREDUCE_SEQ_FMUL : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type1:$acc, type2:$v);
  let hasSideEffects = false;
}

def G_VECREDUCE_FADD : VectorReduction;
def G_VECREDUCE_FMUL : VectorReduction;

def G_VECREDUCE_FMAX : VectorReduction;
def G_VECREDUCE_FMIN : VectorReduction;
def G_VECREDUCE_FMAXIMUM : VectorReduction;
def G_VECREDUCE_FMINIMUM : VectorReduction;

def G_VECREDUCE_ADD : VectorReduction;
def G_VECREDUCE_MUL : VectorReduction;
def G_VECREDUCE_AND : VectorReduction;
def G_VECREDUCE_OR : VectorReduction;
def G_VECREDUCE_XOR : VectorReduction;
def G_VECREDUCE_SMAX : VectorReduction;
def G_VECREDUCE_SMIN : VectorReduction;
def G_VECREDUCE_UMAX : VectorReduction;
def G_VECREDUCE_UMIN : VectorReduction;

//------------------------------------------------------------------------------
// Constrained floating point ops
//------------------------------------------------------------------------------

def G_STRICT_FADD : ConstrainedInstruction<G_FADD>;
def G_STRICT_FSUB : ConstrainedInstruction<G_FSUB>;
def G_STRICT_FMUL : ConstrainedInstruction<G_FMUL>;
def G_STRICT_FDIV : ConstrainedInstruction<G_FDIV>;
def G_STRICT_FREM : ConstrainedInstruction<G_FREM>;
def G_STRICT_FMA : ConstrainedInstruction<G_FMA>;
def G_STRICT_FSQRT : ConstrainedInstruction<G_FSQRT>;
def G_STRICT_FLDEXP : ConstrainedInstruction<G_FLDEXP>;

//------------------------------------------------------------------------------
// Memory intrinsics
//------------------------------------------------------------------------------

def G_MEMCPY : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptype0:$dst_addr, ptype1:$src_addr, type2:$size, untyped_imm_0:$tailcall);
  let hasSideEffects = false;
  let mayLoad = true;
  let mayStore = true;
}

def G_MEMCPY_INLINE : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptype0:$dst_addr, ptype1:$src_addr, type2:$size);
  let hasSideEffects = false;
  let mayLoad = true;
  let mayStore = true;
}

def G_MEMMOVE : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptype0:$dst_addr, ptype1:$src_addr, type2:$size, untyped_imm_0:$tailcall);
  let hasSideEffects = false;
  let mayLoad = true;
  let mayStore = true;
}

def G_MEMSET : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptype0:$dst_addr, type1:$value, type2:$size, untyped_imm_0:$tailcall);
  let hasSideEffects = false;
  let mayStore = true;
}

def G_BZERO : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins ptype0:$dst_addr, type1:$size, untyped_imm_0:$tailcall);
  let hasSideEffects = false;
  let mayStore = true;
}

//------------------------------------------------------------------------------
// Trap intrinsics
//------------------------------------------------------------------------------
def G_TRAP : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let hasSideEffects = true;
  let mayStore = true;
}

def G_DEBUGTRAP : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins);
  let hasSideEffects = true;
}

def G_UBSANTRAP : GenericInstruction {
  let OutOperandList = (outs);
  let InOperandList = (ins i8imm:$kind);
  let hasSideEffects = true;
}

//------------------------------------------------------------------------------
// Bitfield extraction.
//------------------------------------------------------------------------------

// Generic signed bitfield extraction. The operands are in the range
// 0 <= lsb < lsb + width <= src bitwidth, where all values are unsigned.
def G_SBFX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, type1:$lsb, type1:$width);
  let hasSideEffects = false;
}

// Generic unsigned bitfield extraction. The operands are in the range
// 0 <= lsb < lsb + width <= src bitwidth, where all values are unsigned.
def G_UBFX : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, type1:$lsb, type1:$width);
  let hasSideEffects = false;
}

//------------------------------------------------------------------------------
// Optimization hints
//------------------------------------------------------------------------------

// Asserts that an operation has already been zero-extended from a specific
// type.
def G_ASSERT_ZEXT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, untyped_imm_0:$sz);
  let hasSideEffects = false;
}

// Asserts that an operation has already been sign-extended from a specific
// type.
def G_ASSERT_SEXT : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, untyped_imm_0:$sz);
  let hasSideEffects = false;
}

// Asserts that a value has at least the given alignment.
def G_ASSERT_ALIGN : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src, untyped_imm_0:$align);
  let hasSideEffects = false;
}

// Prevent constant folding of the source value with any users.
def G_CONSTANT_FOLD_BARRIER : GenericInstruction {
  let OutOperandList = (outs type0:$dst);
  let InOperandList = (ins type0:$src);
  let hasSideEffects = false;
}
