//===--- arm_sve_sme_incl.td - ARM SVE/SME compiler interface -------------===//
//
//  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
//  See https://llvm.org/LICENSE.txt for license information.
//  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines common properites of TableGen definitions use for both
//  SVE and SME intrinsics.
//
//      https://developer.arm.com/architectures/system-architectures/software-standards/acle
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction definitions
//===----------------------------------------------------------------------===//
// Every intrinsic subclasses "Inst". An intrinsic has a name, a prototype and
// a sequence of typespecs.
//
// The name is the base name of the intrinsic, for example "svld1". This is
// then mangled by the tblgen backend to add type information ("svld1_s16").
//
// A typespec is a sequence of uppercase characters (modifiers) followed by one
// lowercase character. A typespec encodes a particular "base type" of the
// intrinsic.
//
// An example typespec is "Us" - unsigned short - svuint16_t. The available
// typespec codes are given below.
//
// The string given to an Inst class is a sequence of typespecs. The intrinsic
// is instantiated for every typespec in the sequence. For example "sdUsUd".
//
// The prototype is a string that defines the return type of the intrinsic
// and the type of each argument. The return type and every argument gets a
// "modifier" that can change in some way the "base type" of the intrinsic.
//
// The modifier 'd' means "default" and does not modify the base type in any
// way. The available modifiers are given below.
//
// Typespecs
// ---------
// c: char
// s: short
// i: int
// l: long
// q: int128_t
// f: float
// h: half-float
// d: double
// b: bfloat

// Typespec modifiers
// ------------------
// P: boolean
// U: unsigned
// Q: svcount

// Prototype modifiers
// -------------------
// prototype: return (arg, arg, ...)
//
// 2,3,4: array of vectors
// .: indicator for multi-vector modifier that will follow (e.g. 2.x)
// v: void
// x: vector of signed integers
// u: vector of unsigned integers
// d: default
// c: const pointer type
// P: predicate type
// s: scalar of element type
// a: scalar of element type (splat to vector type)
// R: scalar of 1/2 width element type (splat to vector type)
// r: scalar of 1/4 width element type (splat to vector type)
// @: unsigned scalar of 1/4 width element type (splat to vector type)
// e: 1/2 width unsigned elements, 2x element count
// b: 1/4 width unsigned elements, 4x element count
// h: 1/2 width elements, 2x element count
// q: 1/4 width elements, 4x element count
// o: 4x width elements, 1/4 element count
//
// w: vector of element type promoted to 64bits, vector maintains
//    signedness of its element type.
// f: element type promoted to uint64_t (splat to vector type)
// j: element type promoted to 64bits (splat to vector type)
// K: element type bitcast to a signed integer (splat to vector type)
// L: element type bitcast to an unsigned integer (splat to vector type)
//
// i: constant uint64_t
// k: int32_t
// l: int64_t
// m: uint32_t
// n: uint64_t

// [: svuint8_t
// t: svint32_t
// z: svuint32_t
// g: svuint64_t
// O: svfloat16_t
// M: svfloat32_t
// N: svfloat64_t
// $: svbfloat16_t

// J: Prefetch type (sv_prfop)

// %: pointer to void

// A: pointer to int8_t
// B: pointer to int16_t
// C: pointer to int32_t
// D: pointer to int64_t

// E: pointer to uint8_t
// F: pointer to uint16_t
// G: pointer to uint32_t
// H: pointer to uint64_t

// Q: const pointer to void

// S: const pointer to int8_t
// T: const pointer to int16_t
// U: const pointer to int32_t
// V: const pointer to int64_t
//
// W: const pointer to uint8_t
// X: const pointer to uint16_t
// Y: const pointer to uint32_t
// Z: const pointer to uint64_t

// Prototype modifiers added for SVE2p1
// {: 128b vector
// }: svcount_t

class MergeType<int val, string suffix=""> {
  int Value = val;
  string Suffix = suffix;
}
def MergeNone    : MergeType<0>;
def MergeAny     : MergeType<1, "_x">;
def MergeOp1     : MergeType<2, "_m">;
def MergeZero    : MergeType<3, "_z">;
def MergeAnyExp  : MergeType<4, "_x">; // Use merged builtin with explicit
def MergeZeroExp : MergeType<5, "_z">; // generation of its inactive argument.

class EltType<int val> {
  int Value = val;
}
def EltTyInvalid : EltType<0>;
def EltTyInt8    : EltType<1>;
def EltTyInt16   : EltType<2>;
def EltTyInt32   : EltType<3>;
def EltTyInt64   : EltType<4>;
def EltTyInt128  : EltType<5>;
def EltTyFloat16 : EltType<6>;
def EltTyFloat32 : EltType<7>;
def EltTyFloat64 : EltType<8>;
def EltTyBool8   : EltType<9>;
def EltTyBool16  : EltType<10>;
def EltTyBool32  : EltType<11>;
def EltTyBool64  : EltType<12>;
def EltTyBFloat16 : EltType<13>;

class MemEltType<int val> {
  int Value = val;
}
def MemEltTyDefault   : MemEltType<0>;
def MemEltTyInt8      : MemEltType<1>;
def MemEltTyInt16     : MemEltType<2>;
def MemEltTyInt32     : MemEltType<3>;
def MemEltTyInt64     : MemEltType<4>;

class FlagType<int val> {
  int Value = val;
}

// These must be kept in sync with the flags in utils/TableGen/SveEmitter.h
// and include/clang/Basic/TargetBuiltins.h
def NoFlags                   : FlagType<0x00000000>;
def FirstEltType              : FlagType<0x00000001>;
//      :                                     :
//      :                                     :
def EltTypeMask               : FlagType<0x0000000f>;
def FirstMemEltType           : FlagType<0x00000010>;
//      :                                     :
//      :                                     :
def MemEltTypeMask            : FlagType<0x00000070>;
def FirstMergeTypeMask        : FlagType<0x00000080>;
//      :                                     :
//      :                                     :
def MergeTypeMask             : FlagType<0x00000380>;
def FirstSplatOperand         : FlagType<0x00000400>;
//      :                                     :
// These flags are used to specify which scalar operand
// needs to be duplicated/splatted into a vector.
//      :                                     :
def SplatOperandMask                : FlagType<0x00001C00>;
def IsLoad                          : FlagType<0x00002000>;
def IsStore                         : FlagType<0x00004000>;
def IsGatherLoad                    : FlagType<0x00008000>;
def IsScatterStore                  : FlagType<0x00010000>;
def IsStructLoad                    : FlagType<0x00020000>;
def IsStructStore                   : FlagType<0x00040000>;
def IsZExtReturn                    : FlagType<0x00080000>; // Return value is sign-extend by default
def IsOverloadNone                  : FlagType<0x00100000>; // Intrinsic does not take any overloaded types.
def IsOverloadWhileOrMultiVecCvt    : FlagType<0x00200000>; // Use {default type, typeof(operand1)} as overloaded types.
def IsOverloadWhileRW               : FlagType<0x00400000>; // Use {pred(default type), typeof(operand0)} as overloaded types.
def IsOverloadCvt                   : FlagType<0x00800000>; // Use {typeof(operand0), typeof(last operand)} as overloaded types.
def OverloadKindMask                : FlagType<0x00E00000>; // When the masked values are all '0', the default type is used as overload type.
def IsByteIndexed                   : FlagType<0x01000000>;
def IsAppendSVALL                   : FlagType<0x02000000>; // Appends SV_ALL as the last operand.
def IsInsertOp1SVALL                : FlagType<0x04000000>; // Inserts SV_ALL as the second operand.
def IsPrefetch                      : FlagType<0x08000000>; // Contiguous prefetches.
def IsGatherPrefetch                : FlagType<0x10000000>;
def ReverseCompare                  : FlagType<0x20000000>; // Compare operands must be swapped.
def ReverseUSDOT                    : FlagType<0x40000000>; // Unsigned/signed operands must be swapped.
def IsUndef                         : FlagType<0x80000000>; // Codegen `undef` of given type.
def IsTupleCreate                   : FlagType<0x100000000>;
def IsTupleGet                      : FlagType<0x200000000>;
def IsTupleSet                      : FlagType<0x400000000>;
def ReverseMergeAnyBinOp            : FlagType<0x800000000>; // e.g. Implement SUBR_X using SUB_X.
def ReverseMergeAnyAccOp            : FlagType<0x1000000000>; // e.g. Implement MSB_X using MLS_X.
def IsStreaming                     : FlagType<0x2000000000>;
def IsStreamingCompatible           : FlagType<0x4000000000>;
def IsReadZA                        : FlagType<0x8000000000>;
def IsWriteZA                       : FlagType<0x10000000000>;
def IsReductionQV                   : FlagType<0x20000000000>;
def VerifyRuntimeMode               : FlagType<0x40000000000>; // Use for intrinsics that are common between SVE and SME.
def IsInZA                          : FlagType<0x80000000000>;
def IsOutZA                         : FlagType<0x100000000000>;
def IsInOutZA                       : FlagType<0x200000000000>;
def IsInZT0                         : FlagType<0x400000000000>;
def IsOutZT0                        : FlagType<0x800000000000>;
def IsInOutZT0                      : FlagType<0x1000000000000>;

// These must be kept in sync with the flags in include/clang/Basic/TargetBuiltins.h
class ImmCheckType<int val> {
  int Value = val;
}
def ImmCheck0_31                : ImmCheckType<0>;  // 0..31 (used for e.g. predicate patterns)
def ImmCheck1_16                : ImmCheckType<1>;  // 1..16
def ImmCheckExtract             : ImmCheckType<2>;  // 0..(2048/sizeinbits(elt) - 1)
def ImmCheckShiftRight          : ImmCheckType<3>;  // 1..sizeinbits(elt)
def ImmCheckShiftRightNarrow    : ImmCheckType<4>;  // 1..sizeinbits(elt)/2
def ImmCheckShiftLeft           : ImmCheckType<5>;  // 0..(sizeinbits(elt) - 1)
def ImmCheck0_7                 : ImmCheckType<6>;  // 0..7
def ImmCheckLaneIndex           : ImmCheckType<7>;  // 0..(128/(1*sizeinbits(elt)) - 1)
def ImmCheckLaneIndexCompRotate : ImmCheckType<8>;  // 0..(128/(2*sizeinbits(elt)) - 1)
def ImmCheckLaneIndexDot        : ImmCheckType<9>;  // 0..(128/(4*sizeinbits(elt)) - 1)
def ImmCheckComplexRot90_270    : ImmCheckType<10>; // [90,270]
def ImmCheckComplexRotAll90     : ImmCheckType<11>; // [0, 90, 180,270]
def ImmCheck0_13                : ImmCheckType<12>; // 0..13
def ImmCheck0_1                 : ImmCheckType<13>; // 0..1
def ImmCheck0_2                 : ImmCheckType<14>; // 0..2
def ImmCheck0_3                 : ImmCheckType<15>; // 0..3
def ImmCheck0_0                 : ImmCheckType<16>; // 0..0
def ImmCheck0_15                : ImmCheckType<17>; // 0..15
def ImmCheck0_255               : ImmCheckType<18>; // 0..255
def ImmCheck2_4_Mul2            : ImmCheckType<19>; // 2, 4
def ImmCheck1_1                 : ImmCheckType<20>; // 1..1
def ImmCheck1_3                 : ImmCheckType<21>; // 1..3
def ImmCheck1_7                 : ImmCheckType<22>; // 1..7

class ImmCheck<int arg, ImmCheckType kind, int eltSizeArg = -1> {
  int Arg = arg;
  int EltSizeArg = eltSizeArg;
  ImmCheckType Kind = kind;
}

defvar InvalidMode = "";

class Inst<string n, string p, string t, MergeType mt, string i,
           list<FlagType> ft, list<ImmCheck> ch, MemEltType met = MemEltTyDefault> {
  string Name = n;
  string Prototype = p;
  string Types = t;
  string SVETargetGuard = "sve";
  string SMETargetGuard = "sme";
  int Merge = mt.Value;
  string MergeSuffix = mt.Suffix;
  string LLVMIntrinsic = i;
  list<FlagType> Flags = ft;
  list<ImmCheck> ImmChecks = ch;
  int MemEltType = met.Value;
}

// SInst: Instruction with signed/unsigned suffix (e.g., "s8", "u8")
class SInst<string n, string p, string t, MergeType mt, string i = "",
            list<FlagType> ft = [], list<ImmCheck> ch = []>
    : Inst<n, p, t, mt, i, ft, ch, MemEltTyDefault> {
}

// MInst: Instructions which access memory
class MInst<string n, string p, string t, list<FlagType> f,
            MemEltType met = MemEltTyDefault, string i = "",
            list<ImmCheck> ch = []>
    : Inst<n, p, t, MergeNone, i, f, ch, met> {
}
