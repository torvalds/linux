//===-- llvm/CodeGen/ISDOpcodes.h - CodeGen opcodes -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares codegen opcodes and related utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ISDOPCODES_H
#define LLVM_CODEGEN_ISDOPCODES_H

#include "llvm/CodeGen/ValueTypes.h"

namespace llvm {

/// ISD namespace - This namespace contains an enum which represents all of the
/// SelectionDAG node types and value types.
///
namespace ISD {

//===--------------------------------------------------------------------===//
/// ISD::NodeType enum - This enum defines the target-independent operators
/// for a SelectionDAG.
///
/// Targets may also define target-dependent operator codes for SDNodes. For
/// example, on x86, these are the enum values in the X86ISD namespace.
/// Targets should aim to use target-independent operators to model their
/// instruction sets as much as possible, and only use target-dependent
/// operators when they have special requirements.
///
/// Finally, during and after selection proper, SNodes may use special
/// operator codes that correspond directly with MachineInstr opcodes. These
/// are used to represent selected instructions. See the isMachineOpcode()
/// and getMachineOpcode() member functions of SDNode.
///
enum NodeType {

  /// DELETED_NODE - This is an illegal value that is used to catch
  /// errors.  This opcode is not a legal opcode for any node.
  DELETED_NODE,

  /// EntryToken - This is the marker used to indicate the start of a region.
  EntryToken,

  /// TokenFactor - This node takes multiple tokens as input and produces a
  /// single token result. This is used to represent the fact that the operand
  /// operators are independent of each other.
  TokenFactor,

  /// AssertSext, AssertZext - These nodes record if a register contains a
  /// value that has already been zero or sign extended from a narrower type.
  /// These nodes take two operands.  The first is the node that has already
  /// been extended, and the second is a value type node indicating the width
  /// of the extension.
  /// NOTE: In case of the source value (or any vector element value) is
  /// poisoned the assertion will not be true for that value.
  AssertSext,
  AssertZext,

  /// AssertAlign - These nodes record if a register contains a value that
  /// has a known alignment and the trailing bits are known to be zero.
  /// NOTE: In case of the source value (or any vector element value) is
  /// poisoned the assertion will not be true for that value.
  AssertAlign,

  /// Various leaf nodes.
  BasicBlock,
  VALUETYPE,
  CONDCODE,
  Register,
  RegisterMask,
  Constant,
  ConstantFP,
  GlobalAddress,
  GlobalTLSAddress,
  FrameIndex,
  JumpTable,
  ConstantPool,
  ExternalSymbol,
  BlockAddress,

  /// A ptrauth constant.
  /// ptr, key, addr-disc, disc
  /// Note that the addr-disc can be a non-constant value, to allow representing
  /// a constant global address signed using address-diversification, in code.
  PtrAuthGlobalAddress,

  /// The address of the GOT
  GLOBAL_OFFSET_TABLE,

  /// FRAMEADDR, RETURNADDR - These nodes represent llvm.frameaddress and
  /// llvm.returnaddress on the DAG.  These nodes take one operand, the index
  /// of the frame or return address to return.  An index of zero corresponds
  /// to the current function's frame or return address, an index of one to
  /// the parent's frame or return address, and so on.
  FRAMEADDR,
  RETURNADDR,

  /// ADDROFRETURNADDR - Represents the llvm.addressofreturnaddress intrinsic.
  /// This node takes no operand, returns a target-specific pointer to the
  /// place in the stack frame where the return address of the current
  /// function is stored.
  ADDROFRETURNADDR,

  /// SPONENTRY - Represents the llvm.sponentry intrinsic. Takes no argument
  /// and returns the stack pointer value at the entry of the current
  /// function calling this intrinsic.
  SPONENTRY,

  /// LOCAL_RECOVER - Represents the llvm.localrecover intrinsic.
  /// Materializes the offset from the local object pointer of another
  /// function to a particular local object passed to llvm.localescape. The
  /// operand is the MCSymbol label used to represent this offset, since
  /// typically the offset is not known until after code generation of the
  /// parent.
  LOCAL_RECOVER,

  /// READ_REGISTER, WRITE_REGISTER - This node represents llvm.register on
  /// the DAG, which implements the named register global variables extension.
  READ_REGISTER,
  WRITE_REGISTER,

  /// FRAME_TO_ARGS_OFFSET - This node represents offset from frame pointer to
  /// first (possible) on-stack argument. This is needed for correct stack
  /// adjustment during unwind.
  FRAME_TO_ARGS_OFFSET,

  /// EH_DWARF_CFA - This node represents the pointer to the DWARF Canonical
  /// Frame Address (CFA), generally the value of the stack pointer at the
  /// call site in the previous frame.
  EH_DWARF_CFA,

  /// OUTCHAIN = EH_RETURN(INCHAIN, OFFSET, HANDLER) - This node represents
  /// 'eh_return' gcc dwarf builtin, which is used to return from
  /// exception. The general meaning is: adjust stack by OFFSET and pass
  /// execution to HANDLER. Many platform-related details also :)
  EH_RETURN,

  /// RESULT, OUTCHAIN = EH_SJLJ_SETJMP(INCHAIN, buffer)
  /// This corresponds to the eh.sjlj.setjmp intrinsic.
  /// It takes an input chain and a pointer to the jump buffer as inputs
  /// and returns an outchain.
  EH_SJLJ_SETJMP,

  /// OUTCHAIN = EH_SJLJ_LONGJMP(INCHAIN, buffer)
  /// This corresponds to the eh.sjlj.longjmp intrinsic.
  /// It takes an input chain and a pointer to the jump buffer as inputs
  /// and returns an outchain.
  EH_SJLJ_LONGJMP,

  /// OUTCHAIN = EH_SJLJ_SETUP_DISPATCH(INCHAIN)
  /// The target initializes the dispatch table here.
  EH_SJLJ_SETUP_DISPATCH,

  /// TargetConstant* - Like Constant*, but the DAG does not do any folding,
  /// simplification, or lowering of the constant. They are used for constants
  /// which are known to fit in the immediate fields of their users, or for
  /// carrying magic numbers which are not values which need to be
  /// materialized in registers.
  TargetConstant,
  TargetConstantFP,

  /// TargetGlobalAddress - Like GlobalAddress, but the DAG does no folding or
  /// anything else with this node, and this is valid in the target-specific
  /// dag, turning into a GlobalAddress operand.
  TargetGlobalAddress,
  TargetGlobalTLSAddress,
  TargetFrameIndex,
  TargetJumpTable,
  TargetConstantPool,
  TargetExternalSymbol,
  TargetBlockAddress,

  MCSymbol,

  /// TargetIndex - Like a constant pool entry, but with completely
  /// target-dependent semantics. Holds target flags, a 32-bit index, and a
  /// 64-bit index. Targets can use this however they like.
  TargetIndex,

  /// RESULT = INTRINSIC_WO_CHAIN(INTRINSICID, arg1, arg2, ...)
  /// This node represents a target intrinsic function with no side effects.
  /// The first operand is the ID number of the intrinsic from the
  /// llvm::Intrinsic namespace.  The operands to the intrinsic follow.  The
  /// node returns the result of the intrinsic.
  INTRINSIC_WO_CHAIN,

  /// RESULT,OUTCHAIN = INTRINSIC_W_CHAIN(INCHAIN, INTRINSICID, arg1, ...)
  /// This node represents a target intrinsic function with side effects that
  /// returns a result.  The first operand is a chain pointer.  The second is
  /// the ID number of the intrinsic from the llvm::Intrinsic namespace.  The
  /// operands to the intrinsic follow.  The node has two results, the result
  /// of the intrinsic and an output chain.
  INTRINSIC_W_CHAIN,

  /// OUTCHAIN = INTRINSIC_VOID(INCHAIN, INTRINSICID, arg1, arg2, ...)
  /// This node represents a target intrinsic function with side effects that
  /// does not return a result.  The first operand is a chain pointer.  The
  /// second is the ID number of the intrinsic from the llvm::Intrinsic
  /// namespace.  The operands to the intrinsic follow.
  INTRINSIC_VOID,

  /// CopyToReg - This node has three operands: a chain, a register number to
  /// set to this value, and a value.
  CopyToReg,

  /// CopyFromReg - This node indicates that the input value is a virtual or
  /// physical register that is defined outside of the scope of this
  /// SelectionDAG.  The register is available from the RegisterSDNode object.
  /// Note that CopyFromReg is considered as also freezing the value.
  CopyFromReg,

  /// UNDEF - An undefined node.
  UNDEF,

  // FREEZE - FREEZE(VAL) returns an arbitrary value if VAL is UNDEF (or
  // is evaluated to UNDEF), or returns VAL otherwise. Note that each
  // read of UNDEF can yield different value, but FREEZE(UNDEF) cannot.
  FREEZE,

  /// EXTRACT_ELEMENT - This is used to get the lower or upper (determined by
  /// a Constant, which is required to be operand #1) half of the integer or
  /// float value specified as operand #0.  This is only for use before
  /// legalization, for values that will be broken into multiple registers.
  EXTRACT_ELEMENT,

  /// BUILD_PAIR - This is the opposite of EXTRACT_ELEMENT in some ways.
  /// Given two values of the same integer value type, this produces a value
  /// twice as big.  Like EXTRACT_ELEMENT, this can only be used before
  /// legalization. The lower part of the composite value should be in
  /// element 0 and the upper part should be in element 1.
  BUILD_PAIR,

  /// MERGE_VALUES - This node takes multiple discrete operands and returns
  /// them all as its individual results.  This nodes has exactly the same
  /// number of inputs and outputs. This node is useful for some pieces of the
  /// code generator that want to think about a single node with multiple
  /// results, not multiple nodes.
  MERGE_VALUES,

  /// Simple integer binary arithmetic operators.
  ADD,
  SUB,
  MUL,
  SDIV,
  UDIV,
  SREM,
  UREM,

  /// SMUL_LOHI/UMUL_LOHI - Multiply two integers of type iN, producing
  /// a signed/unsigned value of type i[2*N], and return the full value as
  /// two results, each of type iN.
  SMUL_LOHI,
  UMUL_LOHI,

  /// SDIVREM/UDIVREM - Divide two integers and produce both a quotient and
  /// remainder result.
  SDIVREM,
  UDIVREM,

  /// CARRY_FALSE - This node is used when folding other nodes,
  /// like ADDC/SUBC, which indicate the carry result is always false.
  CARRY_FALSE,

  /// Carry-setting nodes for multiple precision addition and subtraction.
  /// These nodes take two operands of the same value type, and produce two
  /// results.  The first result is the normal add or sub result, the second
  /// result is the carry flag result.
  /// FIXME: These nodes are deprecated in favor of UADDO_CARRY and USUBO_CARRY.
  /// They are kept around for now to provide a smooth transition path
  /// toward the use of UADDO_CARRY/USUBO_CARRY and will eventually be removed.
  ADDC,
  SUBC,

  /// Carry-using nodes for multiple precision addition and subtraction. These
  /// nodes take three operands: The first two are the normal lhs and rhs to
  /// the add or sub, and the third is the input carry flag.  These nodes
  /// produce two results; the normal result of the add or sub, and the output
  /// carry flag.  These nodes both read and write a carry flag to allow them
  /// to them to be chained together for add and sub of arbitrarily large
  /// values.
  ADDE,
  SUBE,

  /// Carry-using nodes for multiple precision addition and subtraction.
  /// These nodes take three operands: The first two are the normal lhs and
  /// rhs to the add or sub, and the third is a boolean value that is 1 if and
  /// only if there is an incoming carry/borrow. These nodes produce two
  /// results: the normal result of the add or sub, and a boolean value that is
  /// 1 if and only if there is an outgoing carry/borrow.
  ///
  /// Care must be taken if these opcodes are lowered to hardware instructions
  /// that use the inverse logic -- 0 if and only if there is an
  /// incoming/outgoing carry/borrow.  In such cases, you must preserve the
  /// semantics of these opcodes by inverting the incoming carry/borrow, feeding
  /// it to the add/sub hardware instruction, and then inverting the outgoing
  /// carry/borrow.
  ///
  /// The use of these opcodes is preferable to adde/sube if the target supports
  /// it, as the carry is a regular value rather than a glue, which allows
  /// further optimisation.
  ///
  /// These opcodes are different from [US]{ADD,SUB}O in that
  /// U{ADD,SUB}O_CARRY consume and produce a carry/borrow, whereas
  /// [US]{ADD,SUB}O produce an overflow.
  UADDO_CARRY,
  USUBO_CARRY,

  /// Carry-using overflow-aware nodes for multiple precision addition and
  /// subtraction. These nodes take three operands: The first two are normal lhs
  /// and rhs to the add or sub, and the third is a boolean indicating if there
  /// is an incoming carry. They produce two results: the normal result of the
  /// add or sub, and a boolean that indicates if an overflow occurred (*not*
  /// flag, because it may be a store to memory, etc.). If the type of the
  /// boolean is not i1 then the high bits conform to getBooleanContents.
  SADDO_CARRY,
  SSUBO_CARRY,

  /// RESULT, BOOL = [SU]ADDO(LHS, RHS) - Overflow-aware nodes for addition.
  /// These nodes take two operands: the normal LHS and RHS to the add. They
  /// produce two results: the normal result of the add, and a boolean that
  /// indicates if an overflow occurred (*not* a flag, because it may be store
  /// to memory, etc.).  If the type of the boolean is not i1 then the high
  /// bits conform to getBooleanContents.
  /// These nodes are generated from llvm.[su]add.with.overflow intrinsics.
  SADDO,
  UADDO,

  /// Same for subtraction.
  SSUBO,
  USUBO,

  /// Same for multiplication.
  SMULO,
  UMULO,

  /// RESULT = [US]ADDSAT(LHS, RHS) - Perform saturation addition on 2
  /// integers with the same bit width (W). If the true value of LHS + RHS
  /// exceeds the largest value that can be represented by W bits, the
  /// resulting value is this maximum value. Otherwise, if this value is less
  /// than the smallest value that can be represented by W bits, the
  /// resulting value is this minimum value.
  SADDSAT,
  UADDSAT,

  /// RESULT = [US]SUBSAT(LHS, RHS) - Perform saturation subtraction on 2
  /// integers with the same bit width (W). If the true value of LHS - RHS
  /// exceeds the largest value that can be represented by W bits, the
  /// resulting value is this maximum value. Otherwise, if this value is less
  /// than the smallest value that can be represented by W bits, the
  /// resulting value is this minimum value.
  SSUBSAT,
  USUBSAT,

  /// RESULT = [US]SHLSAT(LHS, RHS) - Perform saturation left shift. The first
  /// operand is the value to be shifted, and the second argument is the amount
  /// to shift by. Both must be integers of the same bit width (W). If the true
  /// value of LHS << RHS exceeds the largest value that can be represented by
  /// W bits, the resulting value is this maximum value, Otherwise, if this
  /// value is less than the smallest value that can be represented by W bits,
  /// the resulting value is this minimum value.
  SSHLSAT,
  USHLSAT,

  /// RESULT = [US]MULFIX(LHS, RHS, SCALE) - Perform fixed point multiplication
  /// on 2 integers with the same width and scale. SCALE represents the scale
  /// of both operands as fixed point numbers. This SCALE parameter must be a
  /// constant integer. A scale of zero is effectively performing
  /// multiplication on 2 integers.
  SMULFIX,
  UMULFIX,

  /// Same as the corresponding unsaturated fixed point instructions, but the
  /// result is clamped between the min and max values representable by the
  /// bits of the first 2 operands.
  SMULFIXSAT,
  UMULFIXSAT,

  /// RESULT = [US]DIVFIX(LHS, RHS, SCALE) - Perform fixed point division on
  /// 2 integers with the same width and scale. SCALE represents the scale
  /// of both operands as fixed point numbers. This SCALE parameter must be a
  /// constant integer.
  SDIVFIX,
  UDIVFIX,

  /// Same as the corresponding unsaturated fixed point instructions, but the
  /// result is clamped between the min and max values representable by the
  /// bits of the first 2 operands.
  SDIVFIXSAT,
  UDIVFIXSAT,

  /// Simple binary floating point operators.
  FADD,
  FSUB,
  FMUL,
  FDIV,
  FREM,

  /// Constrained versions of the binary floating point operators.
  /// These will be lowered to the simple operators before final selection.
  /// They are used to limit optimizations while the DAG is being
  /// optimized.
  STRICT_FADD,
  STRICT_FSUB,
  STRICT_FMUL,
  STRICT_FDIV,
  STRICT_FREM,
  STRICT_FMA,

  /// Constrained versions of libm-equivalent floating point intrinsics.
  /// These will be lowered to the equivalent non-constrained pseudo-op
  /// (or expanded to the equivalent library call) before final selection.
  /// They are used to limit optimizations while the DAG is being optimized.
  STRICT_FSQRT,
  STRICT_FPOW,
  STRICT_FPOWI,
  STRICT_FLDEXP,
  STRICT_FSIN,
  STRICT_FCOS,
  STRICT_FTAN,
  STRICT_FASIN,
  STRICT_FACOS,
  STRICT_FATAN,
  STRICT_FSINH,
  STRICT_FCOSH,
  STRICT_FTANH,
  STRICT_FEXP,
  STRICT_FEXP2,
  STRICT_FLOG,
  STRICT_FLOG10,
  STRICT_FLOG2,
  STRICT_FRINT,
  STRICT_FNEARBYINT,
  STRICT_FMAXNUM,
  STRICT_FMINNUM,
  STRICT_FCEIL,
  STRICT_FFLOOR,
  STRICT_FROUND,
  STRICT_FROUNDEVEN,
  STRICT_FTRUNC,
  STRICT_LROUND,
  STRICT_LLROUND,
  STRICT_LRINT,
  STRICT_LLRINT,
  STRICT_FMAXIMUM,
  STRICT_FMINIMUM,

  /// STRICT_FP_TO_[US]INT - Convert a floating point value to a signed or
  /// unsigned integer. These have the same semantics as fptosi and fptoui
  /// in IR.
  /// They are used to limit optimizations while the DAG is being optimized.
  STRICT_FP_TO_SINT,
  STRICT_FP_TO_UINT,

  /// STRICT_[US]INT_TO_FP - Convert a signed or unsigned integer to
  /// a floating point value. These have the same semantics as sitofp and
  /// uitofp in IR.
  /// They are used to limit optimizations while the DAG is being optimized.
  STRICT_SINT_TO_FP,
  STRICT_UINT_TO_FP,

  /// X = STRICT_FP_ROUND(Y, TRUNC) - Rounding 'Y' from a larger floating
  /// point type down to the precision of the destination VT.  TRUNC is a
  /// flag, which is always an integer that is zero or one.  If TRUNC is 0,
  /// this is a normal rounding, if it is 1, this FP_ROUND is known to not
  /// change the value of Y.
  ///
  /// The TRUNC = 1 case is used in cases where we know that the value will
  /// not be modified by the node, because Y is not using any of the extra
  /// precision of source type.  This allows certain transformations like
  /// STRICT_FP_EXTEND(STRICT_FP_ROUND(X,1)) -> X which are not safe for
  /// STRICT_FP_EXTEND(STRICT_FP_ROUND(X,0)) because the extra bits aren't
  /// removed.
  /// It is used to limit optimizations while the DAG is being optimized.
  STRICT_FP_ROUND,

  /// X = STRICT_FP_EXTEND(Y) - Extend a smaller FP type into a larger FP
  /// type.
  /// It is used to limit optimizations while the DAG is being optimized.
  STRICT_FP_EXTEND,

  /// STRICT_FSETCC/STRICT_FSETCCS - Constrained versions of SETCC, used
  /// for floating-point operands only.  STRICT_FSETCC performs a quiet
  /// comparison operation, while STRICT_FSETCCS performs a signaling
  /// comparison operation.
  STRICT_FSETCC,
  STRICT_FSETCCS,

  // FPTRUNC_ROUND - This corresponds to the fptrunc_round intrinsic.
  FPTRUNC_ROUND,

  /// FMA - Perform a * b + c with no intermediate rounding step.
  FMA,

  /// FMAD - Perform a * b + c, while getting the same result as the
  /// separately rounded operations.
  FMAD,

  /// FCOPYSIGN(X, Y) - Return the value of X with the sign of Y.  NOTE: This
  /// DAG node does not require that X and Y have the same type, just that
  /// they are both floating point.  X and the result must have the same type.
  /// FCOPYSIGN(f32, f64) is allowed.
  FCOPYSIGN,

  /// INT = FGETSIGN(FP) - Return the sign bit of the specified floating point
  /// value as an integer 0/1 value.
  FGETSIGN,

  /// Returns platform specific canonical encoding of a floating point number.
  FCANONICALIZE,

  /// Performs a check of floating point class property, defined by IEEE-754.
  /// The first operand is the floating point value to check. The second operand
  /// specifies the checked property and is a TargetConstant which specifies
  /// test in the same way as intrinsic 'is_fpclass'.
  /// Returns boolean value.
  IS_FPCLASS,

  /// BUILD_VECTOR(ELT0, ELT1, ELT2, ELT3,...) - Return a fixed-width vector
  /// with the specified, possibly variable, elements. The types of the
  /// operands must match the vector element type, except that integer types
  /// are allowed to be larger than the element type, in which case the
  /// operands are implicitly truncated. The types of the operands must all
  /// be the same.
  BUILD_VECTOR,

  /// INSERT_VECTOR_ELT(VECTOR, VAL, IDX) - Returns VECTOR with the element
  /// at IDX replaced with VAL. If the type of VAL is larger than the vector
  /// element type then VAL is truncated before replacement.
  ///
  /// If VECTOR is a scalable vector, then IDX may be larger than the minimum
  /// vector width. IDX is not first scaled by the runtime scaling factor of
  /// VECTOR.
  INSERT_VECTOR_ELT,

  /// EXTRACT_VECTOR_ELT(VECTOR, IDX) - Returns a single element from VECTOR
  /// identified by the (potentially variable) element number IDX. If the return
  /// type is an integer type larger than the element type of the vector, the
  /// result is extended to the width of the return type. In that case, the high
  /// bits are undefined.
  ///
  /// If VECTOR is a scalable vector, then IDX may be larger than the minimum
  /// vector width. IDX is not first scaled by the runtime scaling factor of
  /// VECTOR.
  EXTRACT_VECTOR_ELT,

  /// CONCAT_VECTORS(VECTOR0, VECTOR1, ...) - Given a number of values of
  /// vector type with the same length and element type, this produces a
  /// concatenated vector result value, with length equal to the sum of the
  /// lengths of the input vectors. If VECTOR0 is a fixed-width vector, then
  /// VECTOR1..VECTORN must all be fixed-width vectors. Similarly, if VECTOR0
  /// is a scalable vector, then VECTOR1..VECTORN must all be scalable vectors.
  CONCAT_VECTORS,

  /// INSERT_SUBVECTOR(VECTOR1, VECTOR2, IDX) - Returns a vector with VECTOR2
  /// inserted into VECTOR1. IDX represents the starting element number at which
  /// VECTOR2 will be inserted. IDX must be a constant multiple of T's known
  /// minimum vector length. Let the type of VECTOR2 be T, then if T is a
  /// scalable vector, IDX is first scaled by the runtime scaling factor of T.
  /// The elements of VECTOR1 starting at IDX are overwritten with VECTOR2.
  /// Elements IDX through (IDX + num_elements(T) - 1) must be valid VECTOR1
  /// indices. If this condition cannot be determined statically but is false at
  /// runtime, then the result vector is undefined. The IDX parameter must be a
  /// vector index constant type, which for most targets will be an integer
  /// pointer type.
  ///
  /// This operation supports inserting a fixed-width vector into a scalable
  /// vector, but not the other way around.
  INSERT_SUBVECTOR,

  /// EXTRACT_SUBVECTOR(VECTOR, IDX) - Returns a subvector from VECTOR.
  /// Let the result type be T, then IDX represents the starting element number
  /// from which a subvector of type T is extracted. IDX must be a constant
  /// multiple of T's known minimum vector length. If T is a scalable vector,
  /// IDX is first scaled by the runtime scaling factor of T. Elements IDX
  /// through (IDX + num_elements(T) - 1) must be valid VECTOR indices. If this
  /// condition cannot be determined statically but is false at runtime, then
  /// the result vector is undefined. The IDX parameter must be a vector index
  /// constant type, which for most targets will be an integer pointer type.
  ///
  /// This operation supports extracting a fixed-width vector from a scalable
  /// vector, but not the other way around.
  EXTRACT_SUBVECTOR,

  /// VECTOR_DEINTERLEAVE(VEC1, VEC2) - Returns two vectors with all input and
  /// output vectors having the same type. The first output contains the even
  /// indices from CONCAT_VECTORS(VEC1, VEC2), with the second output
  /// containing the odd indices. The relative order of elements within an
  /// output match that of the concatenated input.
  VECTOR_DEINTERLEAVE,

  /// VECTOR_INTERLEAVE(VEC1, VEC2) - Returns two vectors with all input and
  /// output vectors having the same type. The first output contains the
  /// result of interleaving the low half of CONCAT_VECTORS(VEC1, VEC2), with
  /// the second output containing the result of interleaving the high half.
  VECTOR_INTERLEAVE,

  /// VECTOR_REVERSE(VECTOR) - Returns a vector, of the same type as VECTOR,
  /// whose elements are shuffled using the following algorithm:
  ///   RESULT[i] = VECTOR[VECTOR.ElementCount - 1 - i]
  VECTOR_REVERSE,

  /// VECTOR_SHUFFLE(VEC1, VEC2) - Returns a vector, of the same type as
  /// VEC1/VEC2.  A VECTOR_SHUFFLE node also contains an array of constant int
  /// values that indicate which value (or undef) each result element will
  /// get.  These constant ints are accessible through the
  /// ShuffleVectorSDNode class.  This is quite similar to the Altivec
  /// 'vperm' instruction, except that the indices must be constants and are
  /// in terms of the element size of VEC1/VEC2, not in terms of bytes.
  VECTOR_SHUFFLE,

  /// VECTOR_SPLICE(VEC1, VEC2, IMM) - Returns a subvector of the same type as
  /// VEC1/VEC2 from CONCAT_VECTORS(VEC1, VEC2), based on the IMM in two ways.
  /// Let the result type be T, if IMM is positive it represents the starting
  /// element number (an index) from which a subvector of type T is extracted
  /// from CONCAT_VECTORS(VEC1, VEC2). If IMM is negative it represents a count
  /// specifying the number of trailing elements to extract from VEC1, where the
  /// elements of T are selected using the following algorithm:
  ///   RESULT[i] = CONCAT_VECTORS(VEC1,VEC2)[VEC1.ElementCount - ABS(IMM) + i]
  /// If IMM is not in the range [-VL, VL-1] the result vector is undefined. IMM
  /// is a constant integer.
  VECTOR_SPLICE,

  /// SCALAR_TO_VECTOR(VAL) - This represents the operation of loading a
  /// scalar value into element 0 of the resultant vector type.  The top
  /// elements 1 to N-1 of the N-element vector are undefined.  The type
  /// of the operand must match the vector element type, except when they
  /// are integer types.  In this case the operand is allowed to be wider
  /// than the vector element type, and is implicitly truncated to it.
  SCALAR_TO_VECTOR,

  /// SPLAT_VECTOR(VAL) - Returns a vector with the scalar value VAL
  /// duplicated in all lanes. The type of the operand must match the vector
  /// element type, except when they are integer types.  In this case the
  /// operand is allowed to be wider than the vector element type, and is
  /// implicitly truncated to it.
  SPLAT_VECTOR,

  /// SPLAT_VECTOR_PARTS(SCALAR1, SCALAR2, ...) - Returns a vector with the
  /// scalar values joined together and then duplicated in all lanes. This
  /// represents a SPLAT_VECTOR that has had its scalar operand expanded. This
  /// allows representing a 64-bit splat on a target with 32-bit integers. The
  /// total width of the scalars must cover the element width. SCALAR1 contains
  /// the least significant bits of the value regardless of endianness and all
  /// scalars should have the same type.
  SPLAT_VECTOR_PARTS,

  /// STEP_VECTOR(IMM) - Returns a scalable vector whose lanes are comprised
  /// of a linear sequence of unsigned values starting from 0 with a step of
  /// IMM, where IMM must be a TargetConstant with type equal to the vector
  /// element type. The arithmetic is performed modulo the bitwidth of the
  /// element.
  ///
  /// The operation does not support returning fixed-width vectors or
  /// non-constant operands.
  STEP_VECTOR,

  /// VECTOR_COMPRESS(Vec, Mask, Passthru)
  /// consecutively place vector elements based on mask
  /// e.g., vec = {A, B, C, D} and mask = {1, 0, 1, 0}
  ///         --> {A, C, ?, ?} where ? is undefined
  /// If passthru is defined, ?s are replaced with elements from passthru.
  /// If passthru is undef, ?s remain undefined.
  VECTOR_COMPRESS,

  /// MULHU/MULHS - Multiply high - Multiply two integers of type iN,
  /// producing an unsigned/signed value of type i[2*N], then return the top
  /// part.
  MULHU,
  MULHS,

  /// AVGFLOORS/AVGFLOORU - Averaging add - Add two integers using an integer of
  /// type i[N+1], halving the result by shifting it one bit right.
  /// shr(add(ext(X), ext(Y)), 1)
  AVGFLOORS,
  AVGFLOORU,
  /// AVGCEILS/AVGCEILU - Rounding averaging add - Add two integers using an
  /// integer of type i[N+2], add 1 and halve the result by shifting it one bit
  /// right. shr(add(ext(X), ext(Y), 1), 1)
  AVGCEILS,
  AVGCEILU,

  // ABDS/ABDU - Absolute difference - Return the absolute difference between
  // two numbers interpreted as signed/unsigned.
  // i.e trunc(abs(sext(Op0) - sext(Op1))) becomes abds(Op0, Op1)
  //  or trunc(abs(zext(Op0) - zext(Op1))) becomes abdu(Op0, Op1)
  ABDS,
  ABDU,

  /// [US]{MIN/MAX} - Binary minimum or maximum of signed or unsigned
  /// integers.
  SMIN,
  SMAX,
  UMIN,
  UMAX,

  /// [US]CMP - 3-way comparison of signed or unsigned integers. Returns -1, 0,
  /// or 1 depending on whether Op0 <, ==, or > Op1. The operands can have type
  /// different to the result.
  SCMP,
  UCMP,

  /// Bitwise operators - logical and, logical or, logical xor.
  AND,
  OR,
  XOR,

  /// ABS - Determine the unsigned absolute value of a signed integer value of
  /// the same bitwidth.
  /// Note: A value of INT_MIN will return INT_MIN, no saturation or overflow
  /// is performed.
  ABS,

  /// Shift and rotation operations.  After legalization, the type of the
  /// shift amount is known to be TLI.getShiftAmountTy().  Before legalization
  /// the shift amount can be any type, but care must be taken to ensure it is
  /// large enough.  TLI.getShiftAmountTy() is i8 on some targets, but before
  /// legalization, types like i1024 can occur and i8 doesn't have enough bits
  /// to represent the shift amount.
  /// When the 1st operand is a vector, the shift amount must be in the same
  /// type. (TLI.getShiftAmountTy() will return the same type when the input
  /// type is a vector.)
  /// For rotates and funnel shifts, the shift amount is treated as an unsigned
  /// amount modulo the element size of the first operand.
  ///
  /// Funnel 'double' shifts take 3 operands, 2 inputs and the shift amount.
  /// fshl(X,Y,Z): (X << (Z % BW)) | (Y >> (BW - (Z % BW)))
  /// fshr(X,Y,Z): (X << (BW - (Z % BW))) | (Y >> (Z % BW))
  SHL,
  SRA,
  SRL,
  ROTL,
  ROTR,
  FSHL,
  FSHR,

  /// Byte Swap and Counting operators.
  BSWAP,
  CTTZ,
  CTLZ,
  CTPOP,
  BITREVERSE,
  PARITY,

  /// Bit counting operators with an undefined result for zero inputs.
  CTTZ_ZERO_UNDEF,
  CTLZ_ZERO_UNDEF,

  /// Select(COND, TRUEVAL, FALSEVAL).  If the type of the boolean COND is not
  /// i1 then the high bits must conform to getBooleanContents.
  SELECT,

  /// Select with a vector condition (op #0) and two vector operands (ops #1
  /// and #2), returning a vector result.  All vectors have the same length.
  /// Much like the scalar select and setcc, each bit in the condition selects
  /// whether the corresponding result element is taken from op #1 or op #2.
  /// At first, the VSELECT condition is of vXi1 type. Later, targets may
  /// change the condition type in order to match the VSELECT node using a
  /// pattern. The condition follows the BooleanContent format of the target.
  VSELECT,

  /// Select with condition operator - This selects between a true value and
  /// a false value (ops #2 and #3) based on the boolean result of comparing
  /// the lhs and rhs (ops #0 and #1) of a conditional expression with the
  /// condition code in op #4, a CondCodeSDNode.
  SELECT_CC,

  /// SetCC operator - This evaluates to a true value iff the condition is
  /// true.  If the result value type is not i1 then the high bits conform
  /// to getBooleanContents.  The operands to this are the left and right
  /// operands to compare (ops #0, and #1) and the condition code to compare
  /// them with (op #2) as a CondCodeSDNode. If the operands are vector types
  /// then the result type must also be a vector type.
  SETCC,

  /// Like SetCC, ops #0 and #1 are the LHS and RHS operands to compare, but
  /// op #2 is a boolean indicating if there is an incoming carry. This
  /// operator checks the result of "LHS - RHS - Carry", and can be used to
  /// compare two wide integers:
  /// (setcccarry lhshi rhshi (usubo_carry lhslo rhslo) cc).
  /// Only valid for integers.
  SETCCCARRY,

  /// SHL_PARTS/SRA_PARTS/SRL_PARTS - These operators are used for expanded
  /// integer shift operations.  The operation ordering is:
  ///       [Lo,Hi] = op [LoLHS,HiLHS], Amt
  SHL_PARTS,
  SRA_PARTS,
  SRL_PARTS,

  /// Conversion operators.  These are all single input single output
  /// operations.  For all of these, the result type must be strictly
  /// wider or narrower (depending on the operation) than the source
  /// type.

  /// SIGN_EXTEND - Used for integer types, replicating the sign bit
  /// into new bits.
  SIGN_EXTEND,

  /// ZERO_EXTEND - Used for integer types, zeroing the new bits. Can carry
  /// the NonNeg SDNodeFlag to indicate that the input is known to be
  /// non-negative. If the flag is present and the input is negative, the result
  /// is poison.
  ZERO_EXTEND,

  /// ANY_EXTEND - Used for integer types.  The high bits are undefined.
  ANY_EXTEND,

  /// TRUNCATE - Completely drop the high bits.
  TRUNCATE,

  /// [SU]INT_TO_FP - These operators convert integers (whose interpreted sign
  /// depends on the first letter) to floating point.
  SINT_TO_FP,
  UINT_TO_FP,

  /// SIGN_EXTEND_INREG - This operator atomically performs a SHL/SRA pair to
  /// sign extend a small value in a large integer register (e.g. sign
  /// extending the low 8 bits of a 32-bit register to fill the top 24 bits
  /// with the 7th bit).  The size of the smaller type is indicated by the 1th
  /// operand, a ValueType node.
  SIGN_EXTEND_INREG,

  /// ANY_EXTEND_VECTOR_INREG(Vector) - This operator represents an
  /// in-register any-extension of the low lanes of an integer vector. The
  /// result type must have fewer elements than the operand type, and those
  /// elements must be larger integer types such that the total size of the
  /// operand type is less than or equal to the size of the result type. Each
  /// of the low operand elements is any-extended into the corresponding,
  /// wider result elements with the high bits becoming undef.
  /// NOTE: The type legalizer prefers to make the operand and result size
  /// the same to allow expansion to shuffle vector during op legalization.
  ANY_EXTEND_VECTOR_INREG,

  /// SIGN_EXTEND_VECTOR_INREG(Vector) - This operator represents an
  /// in-register sign-extension of the low lanes of an integer vector. The
  /// result type must have fewer elements than the operand type, and those
  /// elements must be larger integer types such that the total size of the
  /// operand type is less than or equal to the size of the result type. Each
  /// of the low operand elements is sign-extended into the corresponding,
  /// wider result elements.
  /// NOTE: The type legalizer prefers to make the operand and result size
  /// the same to allow expansion to shuffle vector during op legalization.
  SIGN_EXTEND_VECTOR_INREG,

  /// ZERO_EXTEND_VECTOR_INREG(Vector) - This operator represents an
  /// in-register zero-extension of the low lanes of an integer vector. The
  /// result type must have fewer elements than the operand type, and those
  /// elements must be larger integer types such that the total size of the
  /// operand type is less than or equal to the size of the result type. Each
  /// of the low operand elements is zero-extended into the corresponding,
  /// wider result elements.
  /// NOTE: The type legalizer prefers to make the operand and result size
  /// the same to allow expansion to shuffle vector during op legalization.
  ZERO_EXTEND_VECTOR_INREG,

  /// FP_TO_[US]INT - Convert a floating point value to a signed or unsigned
  /// integer. These have the same semantics as fptosi and fptoui in IR. If
  /// the FP value cannot fit in the integer type, the results are undefined.
  FP_TO_SINT,
  FP_TO_UINT,

  /// FP_TO_[US]INT_SAT - Convert floating point value in operand 0 to a
  /// signed or unsigned scalar integer type given in operand 1 with the
  /// following semantics:
  ///
  ///  * If the value is NaN, zero is returned.
  ///  * If the value is larger/smaller than the largest/smallest integer,
  ///    the largest/smallest integer is returned (saturation).
  ///  * Otherwise the result of rounding the value towards zero is returned.
  ///
  /// The scalar width of the type given in operand 1 must be equal to, or
  /// smaller than, the scalar result type width. It may end up being smaller
  /// than the result width as a result of integer type legalization.
  ///
  /// After converting to the scalar integer type in operand 1, the value is
  /// extended to the result VT. FP_TO_SINT_SAT sign extends and FP_TO_UINT_SAT
  /// zero extends.
  FP_TO_SINT_SAT,
  FP_TO_UINT_SAT,

  /// X = FP_ROUND(Y, TRUNC) - Rounding 'Y' from a larger floating point type
  /// down to the precision of the destination VT.  TRUNC is a flag, which is
  /// always an integer that is zero or one.  If TRUNC is 0, this is a
  /// normal rounding, if it is 1, this FP_ROUND is known to not change the
  /// value of Y.
  ///
  /// The TRUNC = 1 case is used in cases where we know that the value will
  /// not be modified by the node, because Y is not using any of the extra
  /// precision of source type.  This allows certain transformations like
  /// FP_EXTEND(FP_ROUND(X,1)) -> X which are not safe for
  /// FP_EXTEND(FP_ROUND(X,0)) because the extra bits aren't removed.
  FP_ROUND,

  /// Returns current rounding mode:
  /// -1 Undefined
  ///  0 Round to 0
  ///  1 Round to nearest, ties to even
  ///  2 Round to +inf
  ///  3 Round to -inf
  ///  4 Round to nearest, ties to zero
  ///  Other values are target dependent.
  /// Result is rounding mode and chain. Input is a chain.
  GET_ROUNDING,

  /// Set rounding mode.
  /// The first operand is a chain pointer. The second specifies the required
  /// rounding mode, encoded in the same way as used in '``GET_ROUNDING``'.
  SET_ROUNDING,

  /// X = FP_EXTEND(Y) - Extend a smaller FP type into a larger FP type.
  FP_EXTEND,

  /// BITCAST - This operator converts between integer, vector and FP
  /// values, as if the value was stored to memory with one type and loaded
  /// from the same address with the other type (or equivalently for vector
  /// format conversions, etc).  The source and result are required to have
  /// the same bit size (e.g.  f32 <-> i32).  This can also be used for
  /// int-to-int or fp-to-fp conversions, but that is a noop, deleted by
  /// getNode().
  ///
  /// This operator is subtly different from the bitcast instruction from
  /// LLVM-IR since this node may change the bits in the register. For
  /// example, this occurs on big-endian NEON and big-endian MSA where the
  /// layout of the bits in the register depends on the vector type and this
  /// operator acts as a shuffle operation for some vector type combinations.
  BITCAST,

  /// ADDRSPACECAST - This operator converts between pointers of different
  /// address spaces.
  ADDRSPACECAST,

  /// FP16_TO_FP, FP_TO_FP16 - These operators are used to perform promotions
  /// and truncation for half-precision (16 bit) floating numbers. These nodes
  /// form a semi-softened interface for dealing with f16 (as an i16), which
  /// is often a storage-only type but has native conversions.
  FP16_TO_FP,
  FP_TO_FP16,
  STRICT_FP16_TO_FP,
  STRICT_FP_TO_FP16,

  /// BF16_TO_FP, FP_TO_BF16 - These operators are used to perform promotions
  /// and truncation for bfloat16. These nodes form a semi-softened interface
  /// for dealing with bf16 (as an i16), which is often a storage-only type but
  /// has native conversions.
  BF16_TO_FP,
  FP_TO_BF16,
  STRICT_BF16_TO_FP,
  STRICT_FP_TO_BF16,

  /// Perform various unary floating-point operations inspired by libm. For
  /// FPOWI, the result is undefined if the integer operand doesn't fit into
  /// sizeof(int).
  FNEG,
  FABS,
  FSQRT,
  FCBRT,
  FSIN,
  FCOS,
  FTAN,
  FASIN,
  FACOS,
  FATAN,
  FSINH,
  FCOSH,
  FTANH,
  FPOW,
  FPOWI,
  /// FLDEXP - ldexp, inspired by libm (op0 * 2**op1).
  FLDEXP,

  /// FFREXP - frexp, extract fractional and exponent component of a
  /// floating-point value. Returns the two components as separate return
  /// values.
  FFREXP,

  FLOG,
  FLOG2,
  FLOG10,
  FEXP,
  FEXP2,
  FEXP10,
  FCEIL,
  FTRUNC,
  FRINT,
  FNEARBYINT,
  FROUND,
  FROUNDEVEN,
  FFLOOR,
  LROUND,
  LLROUND,
  LRINT,
  LLRINT,

  /// FMINNUM/FMAXNUM - Perform floating-point minimum or maximum on two
  /// values.
  //
  /// In the case where a single input is a NaN (either signaling or quiet),
  /// the non-NaN input is returned.
  ///
  /// The return value of (FMINNUM 0.0, -0.0) could be either 0.0 or -0.0.
  FMINNUM,
  FMAXNUM,

  /// FMINNUM_IEEE/FMAXNUM_IEEE - Perform floating-point minimumNumber or
  /// maximumNumber on two values, following IEEE-754 definitions. This differs
  /// from FMINNUM/FMAXNUM in the handling of signaling NaNs, and signed zero.
  ///
  /// If one input is a signaling NaN, returns a quiet NaN. This matches
  /// IEEE-754 2008's minnum/maxnum behavior for signaling NaNs (which differs
  /// from 2019).
  ///
  /// These treat -0 as ordered less than +0, matching the behavior of IEEE-754
  /// 2019's minimumNumber/maximumNumber.
  FMINNUM_IEEE,
  FMAXNUM_IEEE,

  /// FMINIMUM/FMAXIMUM - NaN-propagating minimum/maximum that also treat -0.0
  /// as less than 0.0. While FMINNUM_IEEE/FMAXNUM_IEEE follow IEEE 754-2008
  /// semantics, FMINIMUM/FMAXIMUM follow IEEE 754-2019 semantics.
  FMINIMUM,
  FMAXIMUM,

  /// FSINCOS - Compute both fsin and fcos as a single operation.
  FSINCOS,

  /// Gets the current floating-point environment. The first operand is a token
  /// chain. The results are FP environment, represented by an integer value,
  /// and a token chain.
  GET_FPENV,

  /// Sets the current floating-point environment. The first operand is a token
  /// chain, the second is FP environment, represented by an integer value. The
  /// result is a token chain.
  SET_FPENV,

  /// Set floating-point environment to default state. The first operand and the
  /// result are token chains.
  RESET_FPENV,

  /// Gets the current floating-point environment. The first operand is a token
  /// chain, the second is a pointer to memory, where FP environment is stored
  /// to. The result is a token chain.
  GET_FPENV_MEM,

  /// Sets the current floating point environment. The first operand is a token
  /// chain, the second is a pointer to memory, where FP environment is loaded
  /// from. The result is a token chain.
  SET_FPENV_MEM,

  /// Reads the current dynamic floating-point control modes. The operand is
  /// a token chain.
  GET_FPMODE,

  /// Sets the current dynamic floating-point control modes. The first operand
  /// is a token chain, the second is control modes set represented as integer
  /// value.
  SET_FPMODE,

  /// Sets default dynamic floating-point control modes. The operand is a
  /// token chain.
  RESET_FPMODE,

  /// LOAD and STORE have token chains as their first operand, then the same
  /// operands as an LLVM load/store instruction, then an offset node that
  /// is added / subtracted from the base pointer to form the address (for
  /// indexed memory ops).
  LOAD,
  STORE,

  /// DYNAMIC_STACKALLOC - Allocate some number of bytes on the stack aligned
  /// to a specified boundary.  This node always has two return values: a new
  /// stack pointer value and a chain. The first operand is the token chain,
  /// the second is the number of bytes to allocate, and the third is the
  /// alignment boundary.  The size is guaranteed to be a multiple of the
  /// stack alignment, and the alignment is guaranteed to be bigger than the
  /// stack alignment (if required) or 0 to get standard stack alignment.
  DYNAMIC_STACKALLOC,

  /// Control flow instructions.  These all have token chains.

  /// BR - Unconditional branch.  The first operand is the chain
  /// operand, the second is the MBB to branch to.
  BR,

  /// BRIND - Indirect branch.  The first operand is the chain, the second
  /// is the value to branch to, which must be of the same type as the
  /// target's pointer type.
  BRIND,

  /// BR_JT - Jumptable branch. The first operand is the chain, the second
  /// is the jumptable index, the last one is the jumptable entry index.
  BR_JT,

  /// JUMP_TABLE_DEBUG_INFO - Jumptable debug info. The first operand is the
  /// chain, the second is the jumptable index.
  JUMP_TABLE_DEBUG_INFO,

  /// BRCOND - Conditional branch.  The first operand is the chain, the
  /// second is the condition, the third is the block to branch to if the
  /// condition is true.  If the type of the condition is not i1, then the
  /// high bits must conform to getBooleanContents. If the condition is undef,
  /// it nondeterministically jumps to the block.
  /// TODO: Its semantics w.r.t undef requires further discussion; we need to
  /// make it sure that it is consistent with optimizations in MIR & the
  /// meaning of IMPLICIT_DEF. See https://reviews.llvm.org/D92015
  BRCOND,

  /// BR_CC - Conditional branch.  The behavior is like that of SELECT_CC, in
  /// that the condition is represented as condition code, and two nodes to
  /// compare, rather than as a combined SetCC node.  The operands in order
  /// are chain, cc, lhs, rhs, block to branch to if condition is true. If
  /// condition is undef, it nondeterministically jumps to the block.
  BR_CC,

  /// INLINEASM - Represents an inline asm block.  This node always has two
  /// return values: a chain and a flag result.  The inputs are as follows:
  ///   Operand #0  : Input chain.
  ///   Operand #1  : a ExternalSymbolSDNode with a pointer to the asm string.
  ///   Operand #2  : a MDNodeSDNode with the !srcloc metadata.
  ///   Operand #3  : HasSideEffect, IsAlignStack bits.
  ///   After this, it is followed by a list of operands with this format:
  ///     ConstantSDNode: Flags that encode whether it is a mem or not, the
  ///                     of operands that follow, etc.  See InlineAsm.h.
  ///     ... however many operands ...
  ///   Operand #last: Optional, an incoming flag.
  ///
  /// The variable width operands are required to represent target addressing
  /// modes as a single "operand", even though they may have multiple
  /// SDOperands.
  INLINEASM,

  /// INLINEASM_BR - Branching version of inline asm. Used by asm-goto.
  INLINEASM_BR,

  /// EH_LABEL - Represents a label in mid basic block used to track
  /// locations needed for debug and exception handling tables.  These nodes
  /// take a chain as input and return a chain.
  EH_LABEL,

  /// ANNOTATION_LABEL - Represents a mid basic block label used by
  /// annotations. This should remain within the basic block and be ordered
  /// with respect to other call instructions, but loads and stores may float
  /// past it.
  ANNOTATION_LABEL,

  /// CATCHRET - Represents a return from a catch block funclet. Used for
  /// MSVC compatible exception handling. Takes a chain operand and a
  /// destination basic block operand.
  CATCHRET,

  /// CLEANUPRET - Represents a return from a cleanup block funclet.  Used for
  /// MSVC compatible exception handling. Takes only a chain operand.
  CLEANUPRET,

  /// STACKSAVE - STACKSAVE has one operand, an input chain.  It produces a
  /// value, the same type as the pointer type for the system, and an output
  /// chain.
  STACKSAVE,

  /// STACKRESTORE has two operands, an input chain and a pointer to restore
  /// to it returns an output chain.
  STACKRESTORE,

  /// CALLSEQ_START/CALLSEQ_END - These operators mark the beginning and end
  /// of a call sequence, and carry arbitrary information that target might
  /// want to know.  The first operand is a chain, the rest are specified by
  /// the target and not touched by the DAG optimizers.
  /// Targets that may use stack to pass call arguments define additional
  /// operands:
  /// - size of the call frame part that must be set up within the
  ///   CALLSEQ_START..CALLSEQ_END pair,
  /// - part of the call frame prepared prior to CALLSEQ_START.
  /// Both these parameters must be constants, their sum is the total call
  /// frame size.
  /// CALLSEQ_START..CALLSEQ_END pairs may not be nested.
  CALLSEQ_START, // Beginning of a call sequence
  CALLSEQ_END,   // End of a call sequence

  /// VAARG - VAARG has four operands: an input chain, a pointer, a SRCVALUE,
  /// and the alignment. It returns a pair of values: the vaarg value and a
  /// new chain.
  VAARG,

  /// VACOPY - VACOPY has 5 operands: an input chain, a destination pointer,
  /// a source pointer, a SRCVALUE for the destination, and a SRCVALUE for the
  /// source.
  VACOPY,

  /// VAEND, VASTART - VAEND and VASTART have three operands: an input chain,
  /// pointer, and a SRCVALUE.
  VAEND,
  VASTART,

  // PREALLOCATED_SETUP - This has 2 operands: an input chain and a SRCVALUE
  // with the preallocated call Value.
  PREALLOCATED_SETUP,
  // PREALLOCATED_ARG - This has 3 operands: an input chain, a SRCVALUE
  // with the preallocated call Value, and a constant int.
  PREALLOCATED_ARG,

  /// SRCVALUE - This is a node type that holds a Value* that is used to
  /// make reference to a value in the LLVM IR.
  SRCVALUE,

  /// MDNODE_SDNODE - This is a node that holdes an MDNode*, which is used to
  /// reference metadata in the IR.
  MDNODE_SDNODE,

  /// PCMARKER - This corresponds to the pcmarker intrinsic.
  PCMARKER,

  /// READCYCLECOUNTER - This corresponds to the readcyclecounter intrinsic.
  /// It produces a chain and one i64 value. The only operand is a chain.
  /// If i64 is not legal, the result will be expanded into smaller values.
  /// Still, it returns an i64, so targets should set legality for i64.
  /// The result is the content of the architecture-specific cycle
  /// counter-like register (or other high accuracy low latency clock source).
  READCYCLECOUNTER,

  /// READSTEADYCOUNTER - This corresponds to the readfixedcounter intrinsic.
  /// It has the same semantics as the READCYCLECOUNTER implementation except
  /// that the result is the content of the architecture-specific fixed
  /// frequency counter suitable for measuring elapsed time.
  READSTEADYCOUNTER,

  /// HANDLENODE node - Used as a handle for various purposes.
  HANDLENODE,

  /// INIT_TRAMPOLINE - This corresponds to the init_trampoline intrinsic.  It
  /// takes as input a token chain, the pointer to the trampoline, the pointer
  /// to the nested function, the pointer to pass for the 'nest' parameter, a
  /// SRCVALUE for the trampoline and another for the nested function
  /// (allowing targets to access the original Function*).
  /// It produces a token chain as output.
  INIT_TRAMPOLINE,

  /// ADJUST_TRAMPOLINE - This corresponds to the adjust_trampoline intrinsic.
  /// It takes a pointer to the trampoline and produces a (possibly) new
  /// pointer to the same trampoline with platform-specific adjustments
  /// applied.  The pointer it returns points to an executable block of code.
  ADJUST_TRAMPOLINE,

  /// TRAP - Trapping instruction
  TRAP,

  /// DEBUGTRAP - Trap intended to get the attention of a debugger.
  DEBUGTRAP,

  /// UBSANTRAP - Trap with an immediate describing the kind of sanitizer
  /// failure.
  UBSANTRAP,

  /// PREFETCH - This corresponds to a prefetch intrinsic. The first operand
  /// is the chain.  The other operands are the address to prefetch,
  /// read / write specifier, locality specifier and instruction / data cache
  /// specifier.
  PREFETCH,

  /// ARITH_FENCE - This corresponds to a arithmetic fence intrinsic. Both its
  /// operand and output are the same floating type.
  ARITH_FENCE,

  /// MEMBARRIER - Compiler barrier only; generate a no-op.
  MEMBARRIER,

  /// OUTCHAIN = ATOMIC_FENCE(INCHAIN, ordering, scope)
  /// This corresponds to the fence instruction. It takes an input chain, and
  /// two integer constants: an AtomicOrdering and a SynchronizationScope.
  ATOMIC_FENCE,

  /// Val, OUTCHAIN = ATOMIC_LOAD(INCHAIN, ptr)
  /// This corresponds to "load atomic" instruction.
  ATOMIC_LOAD,

  /// OUTCHAIN = ATOMIC_STORE(INCHAIN, ptr, val)
  /// This corresponds to "store atomic" instruction.
  ATOMIC_STORE,

  /// Val, OUTCHAIN = ATOMIC_CMP_SWAP(INCHAIN, ptr, cmp, swap)
  /// For double-word atomic operations:
  /// ValLo, ValHi, OUTCHAIN = ATOMIC_CMP_SWAP(INCHAIN, ptr, cmpLo, cmpHi,
  ///                                          swapLo, swapHi)
  /// This corresponds to the cmpxchg instruction.
  ATOMIC_CMP_SWAP,

  /// Val, Success, OUTCHAIN
  ///     = ATOMIC_CMP_SWAP_WITH_SUCCESS(INCHAIN, ptr, cmp, swap)
  /// N.b. this is still a strong cmpxchg operation, so
  /// Success == "Val == cmp".
  ATOMIC_CMP_SWAP_WITH_SUCCESS,

  /// Val, OUTCHAIN = ATOMIC_SWAP(INCHAIN, ptr, amt)
  /// Val, OUTCHAIN = ATOMIC_LOAD_[OpName](INCHAIN, ptr, amt)
  /// For double-word atomic operations:
  /// ValLo, ValHi, OUTCHAIN = ATOMIC_SWAP(INCHAIN, ptr, amtLo, amtHi)
  /// ValLo, ValHi, OUTCHAIN = ATOMIC_LOAD_[OpName](INCHAIN, ptr, amtLo, amtHi)
  /// These correspond to the atomicrmw instruction.
  ATOMIC_SWAP,
  ATOMIC_LOAD_ADD,
  ATOMIC_LOAD_SUB,
  ATOMIC_LOAD_AND,
  ATOMIC_LOAD_CLR,
  ATOMIC_LOAD_OR,
  ATOMIC_LOAD_XOR,
  ATOMIC_LOAD_NAND,
  ATOMIC_LOAD_MIN,
  ATOMIC_LOAD_MAX,
  ATOMIC_LOAD_UMIN,
  ATOMIC_LOAD_UMAX,
  ATOMIC_LOAD_FADD,
  ATOMIC_LOAD_FSUB,
  ATOMIC_LOAD_FMAX,
  ATOMIC_LOAD_FMIN,
  ATOMIC_LOAD_UINC_WRAP,
  ATOMIC_LOAD_UDEC_WRAP,

  // Masked load and store - consecutive vector load and store operations
  // with additional mask operand that prevents memory accesses to the
  // masked-off lanes.
  //
  // Val, OutChain = MLOAD(BasePtr, Mask, PassThru)
  // OutChain = MSTORE(Value, BasePtr, Mask)
  MLOAD,
  MSTORE,

  // Masked gather and scatter - load and store operations for a vector of
  // random addresses with additional mask operand that prevents memory
  // accesses to the masked-off lanes.
  //
  // Val, OutChain = GATHER(InChain, PassThru, Mask, BasePtr, Index, Scale)
  // OutChain = SCATTER(InChain, Value, Mask, BasePtr, Index, Scale)
  //
  // The Index operand can have more vector elements than the other operands
  // due to type legalization. The extra elements are ignored.
  MGATHER,
  MSCATTER,

  /// This corresponds to the llvm.lifetime.* intrinsics. The first operand
  /// is the chain and the second operand is the alloca pointer.
  LIFETIME_START,
  LIFETIME_END,

  /// GC_TRANSITION_START/GC_TRANSITION_END - These operators mark the
  /// beginning and end of GC transition  sequence, and carry arbitrary
  /// information that target might need for lowering.  The first operand is
  /// a chain, the rest are specified by the target and not touched by the DAG
  /// optimizers. GC_TRANSITION_START..GC_TRANSITION_END pairs may not be
  /// nested.
  GC_TRANSITION_START,
  GC_TRANSITION_END,

  /// GET_DYNAMIC_AREA_OFFSET - get offset from native SP to the address of
  /// the most recent dynamic alloca. For most targets that would be 0, but
  /// for some others (e.g. PowerPC, PowerPC64) that would be compile-time
  /// known nonzero constant. The only operand here is the chain.
  GET_DYNAMIC_AREA_OFFSET,

  /// Pseudo probe for AutoFDO, as a place holder in a basic block to improve
  /// the sample counts quality.
  PSEUDO_PROBE,

  /// VSCALE(IMM) - Returns the runtime scaling factor used to calculate the
  /// number of elements within a scalable vector. IMM is a constant integer
  /// multiplier that is applied to the runtime value.
  VSCALE,

  /// Generic reduction nodes. These nodes represent horizontal vector
  /// reduction operations, producing a scalar result.
  /// The SEQ variants perform reductions in sequential order. The first
  /// operand is an initial scalar accumulator value, and the second operand
  /// is the vector to reduce.
  /// E.g. RES = VECREDUCE_SEQ_FADD f32 ACC, <4 x f32> SRC_VEC
  ///  ... is equivalent to
  /// RES = (((ACC + SRC_VEC[0]) + SRC_VEC[1]) + SRC_VEC[2]) + SRC_VEC[3]
  VECREDUCE_SEQ_FADD,
  VECREDUCE_SEQ_FMUL,

  /// These reductions have relaxed evaluation order semantics, and have a
  /// single vector operand. The order of evaluation is unspecified. For
  /// pow-of-2 vectors, one valid legalizer expansion is to use a tree
  /// reduction, i.e.:
  /// For RES = VECREDUCE_FADD <8 x f16> SRC_VEC
  ///   PART_RDX = FADD SRC_VEC[0:3], SRC_VEC[4:7]
  ///   PART_RDX2 = FADD PART_RDX[0:1], PART_RDX[2:3]
  ///   RES = FADD PART_RDX2[0], PART_RDX2[1]
  /// For non-pow-2 vectors, this can be computed by extracting each element
  /// and performing the operation as if it were scalarized.
  VECREDUCE_FADD,
  VECREDUCE_FMUL,
  /// FMIN/FMAX nodes can have flags, for NaN/NoNaN variants.
  VECREDUCE_FMAX,
  VECREDUCE_FMIN,
  /// FMINIMUM/FMAXIMUM nodes propatate NaNs and signed zeroes using the
  /// llvm.minimum and llvm.maximum semantics.
  VECREDUCE_FMAXIMUM,
  VECREDUCE_FMINIMUM,
  /// Integer reductions may have a result type larger than the vector element
  /// type. However, the reduction is performed using the vector element type
  /// and the value in the top bits is unspecified.
  VECREDUCE_ADD,
  VECREDUCE_MUL,
  VECREDUCE_AND,
  VECREDUCE_OR,
  VECREDUCE_XOR,
  VECREDUCE_SMAX,
  VECREDUCE_SMIN,
  VECREDUCE_UMAX,
  VECREDUCE_UMIN,

  // The `llvm.experimental.stackmap` intrinsic.
  // Operands: input chain, glue, <id>, <numShadowBytes>, [live0[, live1...]]
  // Outputs: output chain, glue
  STACKMAP,

  // The `llvm.experimental.patchpoint.*` intrinsic.
  // Operands: input chain, [glue], reg-mask, <id>, <numShadowBytes>, callee,
  //   <numArgs>, cc, ...
  // Outputs: [rv], output chain, glue
  PATCHPOINT,

// Vector Predication
#define BEGIN_REGISTER_VP_SDNODE(VPSDID, ...) VPSDID,
#include "llvm/IR/VPIntrinsics.def"

  // The `llvm.experimental.convergence.*` intrinsics.
  CONVERGENCECTRL_ANCHOR,
  CONVERGENCECTRL_ENTRY,
  CONVERGENCECTRL_LOOP,
  // This does not correspond to any convergence control intrinsic. It is used
  // to glue a convergence control token to a convergent operation in the DAG,
  // which is later translated to an implicit use in the MIR.
  CONVERGENCECTRL_GLUE,

  // Experimental vector histogram intrinsic
  // Operands: Input Chain, Inc, Mask, Base, Index, Scale, ID
  // Output: Output Chain
  EXPERIMENTAL_VECTOR_HISTOGRAM,

  // llvm.clear_cache intrinsic
  // Operands: Input Chain, Start Addres, End Address
  // Outputs: Output Chain
  CLEAR_CACHE,

  /// BUILTIN_OP_END - This must be the last enum value in this list.
  /// The target-specific pre-isel opcode values start here.
  BUILTIN_OP_END
};

/// FIRST_TARGET_STRICTFP_OPCODE - Target-specific pre-isel operations
/// which cannot raise FP exceptions should be less than this value.
/// Those that do must not be less than this value.
static const int FIRST_TARGET_STRICTFP_OPCODE = BUILTIN_OP_END + 400;

/// FIRST_TARGET_MEMORY_OPCODE - Target-specific pre-isel operations
/// which do not reference a specific memory location should be less than
/// this value. Those that do must not be less than this value, and can
/// be used with SelectionDAG::getMemIntrinsicNode.
static const int FIRST_TARGET_MEMORY_OPCODE = BUILTIN_OP_END + 500;

/// Whether this is bitwise logic opcode.
inline bool isBitwiseLogicOp(unsigned Opcode) {
  return Opcode == ISD::AND || Opcode == ISD::OR || Opcode == ISD::XOR;
}

/// Get underlying scalar opcode for VECREDUCE opcode.
/// For example ISD::AND for ISD::VECREDUCE_AND.
NodeType getVecReduceBaseOpcode(unsigned VecReduceOpcode);

/// Whether this is a vector-predicated Opcode.
bool isVPOpcode(unsigned Opcode);

/// Whether this is a vector-predicated binary operation opcode.
bool isVPBinaryOp(unsigned Opcode);

/// Whether this is a vector-predicated reduction opcode.
bool isVPReduction(unsigned Opcode);

/// The operand position of the vector mask.
std::optional<unsigned> getVPMaskIdx(unsigned Opcode);

/// The operand position of the explicit vector length parameter.
std::optional<unsigned> getVPExplicitVectorLengthIdx(unsigned Opcode);

/// Translate this VP Opcode to its corresponding non-VP Opcode.
std::optional<unsigned> getBaseOpcodeForVP(unsigned Opcode, bool hasFPExcept);

/// Translate this non-VP Opcode to its corresponding VP Opcode.
unsigned getVPForBaseOpcode(unsigned Opcode);

//===--------------------------------------------------------------------===//
/// MemIndexedMode enum - This enum defines the load / store indexed
/// addressing modes.
///
/// UNINDEXED    "Normal" load / store. The effective address is already
///              computed and is available in the base pointer. The offset
///              operand is always undefined. In addition to producing a
///              chain, an unindexed load produces one value (result of the
///              load); an unindexed store does not produce a value.
///
/// PRE_INC      Similar to the unindexed mode where the effective address is
/// PRE_DEC      the value of the base pointer add / subtract the offset.
///              It considers the computation as being folded into the load /
///              store operation (i.e. the load / store does the address
///              computation as well as performing the memory transaction).
///              The base operand is always undefined. In addition to
///              producing a chain, pre-indexed load produces two values
///              (result of the load and the result of the address
///              computation); a pre-indexed store produces one value (result
///              of the address computation).
///
/// POST_INC     The effective address is the value of the base pointer. The
/// POST_DEC     value of the offset operand is then added to / subtracted
///              from the base after memory transaction. In addition to
///              producing a chain, post-indexed load produces two values
///              (the result of the load and the result of the base +/- offset
///              computation); a post-indexed store produces one value (the
///              the result of the base +/- offset computation).
enum MemIndexedMode { UNINDEXED = 0, PRE_INC, PRE_DEC, POST_INC, POST_DEC };

static const int LAST_INDEXED_MODE = POST_DEC + 1;

//===--------------------------------------------------------------------===//
/// MemIndexType enum - This enum defines how to interpret MGATHER/SCATTER's
/// index parameter when calculating addresses.
///
/// SIGNED_SCALED     Addr = Base + ((signed)Index * Scale)
/// UNSIGNED_SCALED   Addr = Base + ((unsigned)Index * Scale)
///
/// NOTE: The value of Scale is typically only known to the node owning the
/// IndexType, with a value of 1 the equivalent of being unscaled.
enum MemIndexType { SIGNED_SCALED = 0, UNSIGNED_SCALED };

static const int LAST_MEM_INDEX_TYPE = UNSIGNED_SCALED + 1;

inline bool isIndexTypeSigned(MemIndexType IndexType) {
  return IndexType == SIGNED_SCALED;
}

//===--------------------------------------------------------------------===//
/// LoadExtType enum - This enum defines the three variants of LOADEXT
/// (load with extension).
///
/// SEXTLOAD loads the integer operand and sign extends it to a larger
///          integer result type.
/// ZEXTLOAD loads the integer operand and zero extends it to a larger
///          integer result type.
/// EXTLOAD  is used for two things: floating point extending loads and
///          integer extending loads [the top bits are undefined].
enum LoadExtType { NON_EXTLOAD = 0, EXTLOAD, SEXTLOAD, ZEXTLOAD };

static const int LAST_LOADEXT_TYPE = ZEXTLOAD + 1;

NodeType getExtForLoadExtType(bool IsFP, LoadExtType);

//===--------------------------------------------------------------------===//
/// ISD::CondCode enum - These are ordered carefully to make the bitfields
/// below work out, when considering SETFALSE (something that never exists
/// dynamically) as 0.  "U" -> Unsigned (for integer operands) or Unordered
/// (for floating point), "L" -> Less than, "G" -> Greater than, "E" -> Equal
/// to.  If the "N" column is 1, the result of the comparison is undefined if
/// the input is a NAN.
///
/// All of these (except for the 'always folded ops') should be handled for
/// floating point.  For integer, only the SETEQ,SETNE,SETLT,SETLE,SETGT,
/// SETGE,SETULT,SETULE,SETUGT, and SETUGE opcodes are used.
///
/// Note that these are laid out in a specific order to allow bit-twiddling
/// to transform conditions.
enum CondCode {
  // Opcode       N U L G E       Intuitive operation
  SETFALSE, //      0 0 0 0       Always false (always folded)
  SETOEQ,   //      0 0 0 1       True if ordered and equal
  SETOGT,   //      0 0 1 0       True if ordered and greater than
  SETOGE,   //      0 0 1 1       True if ordered and greater than or equal
  SETOLT,   //      0 1 0 0       True if ordered and less than
  SETOLE,   //      0 1 0 1       True if ordered and less than or equal
  SETONE,   //      0 1 1 0       True if ordered and operands are unequal
  SETO,     //      0 1 1 1       True if ordered (no nans)
  SETUO,    //      1 0 0 0       True if unordered: isnan(X) | isnan(Y)
  SETUEQ,   //      1 0 0 1       True if unordered or equal
  SETUGT,   //      1 0 1 0       True if unordered or greater than
  SETUGE,   //      1 0 1 1       True if unordered, greater than, or equal
  SETULT,   //      1 1 0 0       True if unordered or less than
  SETULE,   //      1 1 0 1       True if unordered, less than, or equal
  SETUNE,   //      1 1 1 0       True if unordered or not equal
  SETTRUE,  //      1 1 1 1       Always true (always folded)
  // Don't care operations: undefined if the input is a nan.
  SETFALSE2, //   1 X 0 0 0       Always false (always folded)
  SETEQ,     //   1 X 0 0 1       True if equal
  SETGT,     //   1 X 0 1 0       True if greater than
  SETGE,     //   1 X 0 1 1       True if greater than or equal
  SETLT,     //   1 X 1 0 0       True if less than
  SETLE,     //   1 X 1 0 1       True if less than or equal
  SETNE,     //   1 X 1 1 0       True if not equal
  SETTRUE2,  //   1 X 1 1 1       Always true (always folded)

  SETCC_INVALID // Marker value.
};

/// Return true if this is a setcc instruction that performs a signed
/// comparison when used with integer operands.
inline bool isSignedIntSetCC(CondCode Code) {
  return Code == SETGT || Code == SETGE || Code == SETLT || Code == SETLE;
}

/// Return true if this is a setcc instruction that performs an unsigned
/// comparison when used with integer operands.
inline bool isUnsignedIntSetCC(CondCode Code) {
  return Code == SETUGT || Code == SETUGE || Code == SETULT || Code == SETULE;
}

/// Return true if this is a setcc instruction that performs an equality
/// comparison when used with integer operands.
inline bool isIntEqualitySetCC(CondCode Code) {
  return Code == SETEQ || Code == SETNE;
}

/// Return true if this is a setcc instruction that performs an equality
/// comparison when used with floating point operands.
inline bool isFPEqualitySetCC(CondCode Code) {
  return Code == SETOEQ || Code == SETONE || Code == SETUEQ || Code == SETUNE;
}

/// Return true if the specified condition returns true if the two operands to
/// the condition are equal. Note that if one of the two operands is a NaN,
/// this value is meaningless.
inline bool isTrueWhenEqual(CondCode Cond) { return ((int)Cond & 1) != 0; }

/// This function returns 0 if the condition is always false if an operand is
/// a NaN, 1 if the condition is always true if the operand is a NaN, and 2 if
/// the condition is undefined if the operand is a NaN.
inline unsigned getUnorderedFlavor(CondCode Cond) {
  return ((int)Cond >> 3) & 3;
}

/// Return the operation corresponding to !(X op Y), where 'op' is a valid
/// SetCC operation.
CondCode getSetCCInverse(CondCode Operation, EVT Type);

inline bool isExtOpcode(unsigned Opcode) {
  return Opcode == ISD::ANY_EXTEND || Opcode == ISD::ZERO_EXTEND ||
         Opcode == ISD::SIGN_EXTEND;
}

inline bool isExtVecInRegOpcode(unsigned Opcode) {
  return Opcode == ISD::ANY_EXTEND_VECTOR_INREG ||
         Opcode == ISD::ZERO_EXTEND_VECTOR_INREG ||
         Opcode == ISD::SIGN_EXTEND_VECTOR_INREG;
}

namespace GlobalISel {
/// Return the operation corresponding to !(X op Y), where 'op' is a valid
/// SetCC operation. The U bit of the condition code has different meanings
/// between floating point and integer comparisons and LLT's don't provide
/// this distinction. As such we need to be told whether the comparison is
/// floating point or integer-like. Pointers should use integer-like
/// comparisons.
CondCode getSetCCInverse(CondCode Operation, bool isIntegerLike);
} // end namespace GlobalISel

/// Return the operation corresponding to (Y op X) when given the operation
/// for (X op Y).
CondCode getSetCCSwappedOperands(CondCode Operation);

/// Return the result of a logical OR between different comparisons of
/// identical values: ((X op1 Y) | (X op2 Y)). This function returns
/// SETCC_INVALID if it is not possible to represent the resultant comparison.
CondCode getSetCCOrOperation(CondCode Op1, CondCode Op2, EVT Type);

/// Return the result of a logical AND between different comparisons of
/// identical values: ((X op1 Y) & (X op2 Y)). This function returns
/// SETCC_INVALID if it is not possible to represent the resultant comparison.
CondCode getSetCCAndOperation(CondCode Op1, CondCode Op2, EVT Type);

} // namespace ISD

} // namespace llvm

#endif
