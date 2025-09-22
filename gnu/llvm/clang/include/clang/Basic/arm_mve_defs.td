//===- arm_mve_defs.td - definitions and infrastructure for arm_mve.td ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The definitions in this file are designed to work in close conjunction with
// clang/utils/TableGen/MveEmitter.cpp. Comments in there will probably be
// useful as well.
//
//===----------------------------------------------------------------------===//

// -----------------------------------------------------------------------------
// Forward declarations.
class Type;

// -----------------------------------------------------------------------------
// Dummy record used as the dag operator for the argument list of an intrinsic.
//
// We store arguments as a dag rather than a list<Type> so that we can give
// each one a name, to be used in codegen. For example, (args Vector:$a,
// Scalar:$b) defines the names $a and $b which the specification of the code
// for that intrinsic can refer to.

def args;

// -----------------------------------------------------------------------------
// Family of nodes for use in the codegen dag for an intrinsic, corresponding
// to function calls that return LLVM IR nodes.
class IRBuilderParam<int index_> { int index = index_; }
class IRBuilderAddrParam<int index_> : IRBuilderParam<index_>;
class IRBuilderIntParam<int index_, string type_> : IRBuilderParam<index_> {
  string type = type_;
}
class IRBuilderBase {
  // The prefix of the function call, including an open parenthesis.
  string prefix;

  // Any parameters that have types that have to be treated specially by the
  // Tablegen back end. Generally these will be types other than llvm::Value *,
  // although not all other types need special treatment (e.g. llvm::Type *).
  list<IRBuilderParam> special_params = [];
}
class IRBuilder<string func> : IRBuilderBase {
  // The usual case: a method called on the code gen function's instance of
  // llvm::IRBuilder.
  let prefix = "Builder." # func # "(";
}
class IRFunction<string func> : IRBuilderBase {
  // Some other function that doesn't use the IRBuilder at all.
  let prefix = func # "(";
}
class CGHelperFn<string func> : IRBuilderBase {
  // A helper function defined in CGBuiltin.cpp, which takes the IRBuilder as
  // an argument.
  let prefix = func # "(Builder, ";
}
class CGFHelperFn<string func> : IRBuilderBase {
  // Like CGHelperFn, but also takes the CodeGenFunction itself.
  let prefix = func # "(Builder, this, ";
}
def add: IRBuilder<"CreateAdd">;
def mul: IRBuilder<"CreateMul">;
def not: IRBuilder<"CreateNot">;
def or: IRBuilder<"CreateOr">;
def and: IRBuilder<"CreateAnd">;
def xor: IRBuilder<"CreateXor">;
def sub: IRBuilder<"CreateSub">;
def shl: IRBuilder<"CreateShl">;
def lshr: IRBuilder<"CreateLShr">;
def immshr: CGHelperFn<"MVEImmediateShr"> {
  let special_params = [IRBuilderIntParam<1, "unsigned">,
                        IRBuilderIntParam<2, "bool">];
}
def fadd: IRBuilder<"CreateFAdd">;
def fmul: IRBuilder<"CreateFMul">;
def fsub: IRBuilder<"CreateFSub">;
def load: IRBuilder<"CreateLoad"> {
  let special_params = [IRBuilderAddrParam<0>];
}
def store: IRBuilder<"CreateStore"> {
  let special_params = [IRBuilderAddrParam<1>];
}
def xval: IRBuilder<"CreateExtractValue"> {
  let special_params = [IRBuilderIntParam<1, "unsigned">];
}
def ielt_const: IRBuilder<"CreateInsertElement"> {
  let special_params = [IRBuilderIntParam<2, "uint64_t">];
}
def ielt_var: IRBuilder<"CreateInsertElement">;
def xelt_var: IRBuilder<"CreateExtractElement">;
def trunc: IRBuilder<"CreateTrunc">;
def bitcast: IRBuilder<"CreateBitCast">;
def vreinterpret: CGFHelperFn<"ARMMVEVectorReinterpret">;
def extend: CGHelperFn<"SignOrZeroExtend"> {
  let special_params = [IRBuilderIntParam<2, "bool">];
}
def zeroinit: IRFunction<"llvm::Constant::getNullValue">;
def int_min: CGHelperFn<"ARMMVEConstantSplat<1,0>">;
def int_max: CGHelperFn<"ARMMVEConstantSplat<0,1>">;
def uint_max: CGHelperFn<"ARMMVEConstantSplat<1,1>">;
def undef: IRFunction<"UndefValue::get">;
def icmp_eq: IRBuilder<"CreateICmpEQ">;
def icmp_ne: IRBuilder<"CreateICmpNE">;
def icmp_ugt: IRBuilder<"CreateICmpUGT">;
def icmp_uge: IRBuilder<"CreateICmpUGE">;
def icmp_ult: IRBuilder<"CreateICmpULT">;
def icmp_ule: IRBuilder<"CreateICmpULE">;
def icmp_sgt: IRBuilder<"CreateICmpSGT">;
def icmp_sge: IRBuilder<"CreateICmpSGE">;
def icmp_slt: IRBuilder<"CreateICmpSLT">;
def icmp_sle: IRBuilder<"CreateICmpSLE">;
def fcmp_eq: IRBuilder<"CreateFCmpOEQ">;
def fcmp_ne: IRBuilder<"CreateFCmpUNE">; // not O: it must return true on NaNs
def fcmp_gt: IRBuilder<"CreateFCmpOGT">;
def fcmp_ge: IRBuilder<"CreateFCmpOGE">;
def fcmp_lt: IRBuilder<"CreateFCmpOLT">;
def fcmp_le: IRBuilder<"CreateFCmpOLE">;
def splat: CGHelperFn<"ARMMVEVectorSplat">;
def select: IRBuilder<"CreateSelect">;
def fneg: IRBuilder<"CreateFNeg">;
def sitofp: IRBuilder<"CreateSIToFP">;
def uitofp: IRBuilder<"CreateUIToFP">;
def fptosi: IRBuilder<"CreateFPToSI">;
def fptoui: IRBuilder<"CreateFPToUI">;
def vrev: CGHelperFn<"ARMMVEVectorElementReverse"> {
  let special_params = [IRBuilderIntParam<1, "unsigned">];
}
def unzip: CGHelperFn<"VectorUnzip"> {
  let special_params = [IRBuilderIntParam<1, "bool">];
}
def zip: CGHelperFn<"VectorZip">;

// Trivial 'codegen' function that just returns its argument. Useful
// for wrapping up a variable name like $foo into a thing you can pass
// around as type 'dag'.
def id: IRBuilderBase {
  // All the other cases of IRBuilderBase use 'prefix' to specify a function
  // call, including the open parenthesis. MveEmitter puts the closing paren on
  // the end. So if we _just_ specify an open paren with no function name
  // before it, then the generated C++ code will simply wrap the input value in
  // parentheses, returning it unchanged.
  let prefix = "(";
}

// Helper for making boolean flags in IR
def i1: IRBuilderBase {
  let prefix = "llvm::ConstantInt::get(Builder.getInt1Ty(), ";
  let special_params = [IRBuilderIntParam<0, "bool">];
}

// A node that makes an Address out of a pointer-typed Value, by
// providing an alignment as the second argument.
def address;

// Another node class you can use in the codegen dag. This one corresponds to
// an IR intrinsic function, which has to be specialized to a particular list
// of types.
class IRIntBase<string name_, list<Type> params_ = [], bit appendKind_ = 0> {
  string intname = name_;       // base name of the intrinsic
  list<Type> params = params_;  // list of parameter types

  // If this flag is set, then the IR intrinsic name will get a suffix _s, _u
  // or _f depending on whether the main parameter type of the ACLE intrinsic
  // being generated is a signed integer, unsigned integer, or float. Mostly
  // this is useful for signed vs unsigned integers, because the ACLE
  // intrinsics and the source-level integer types distinguish them, but at IR
  // level the distinction has moved from the type system into the operations
  // and you just have i32 or i16 etc. So when an IR intrinsic has to vary with
  // signedness, you set this bit, and then you can still put the signed and
  // unsigned versions in the same subclass of Intrinsic, and the Tablegen
  // backend will take care of adding _s or _u as appropriate in each instance.
  bit appendKind = appendKind_;
}

// Mostly we'll be using @llvm.arm.mve.* intrinsics, so here's a trivial
// subclass that puts on that prefix.
class IRInt<string name, list<Type> params = [], bit appendKind = 0>
      : IRIntBase<"arm_mve_" # name, params, appendKind>;

// The 'seq' node in a codegen dag specifies a set of IR operations to be
// performed in order. It has the special ability to define extra variable
// names, on top of the ones that refer to the intrinsic's parameters. For
// example:
//
//   (seq (foo this, that):$a,
//        (bar this, $a):$b
//        (add $a, $b))
//
// defines the name $a to refer to the return value of the 'foo' operation;
// then the 'bar' operation uses $a as one of its arguments, and the return
// value of that is assigned the name $b; finally, $a and $b are added to give
// the return value of the seq construction as a whole.
def seq;

// Another magic operation is 'unsignedflag', which you give a scalar
// _type_ as an argument, and it expands into 1 for an unsigned type
// and 0 for a signed (or floating) one.
def unsignedflag;

// 'bitsize' also takes a scalar type, and expands into an integer
// constant giving its size in bits.
def bitsize;

// If you put CustomCodegen<"foo"> in an intrinsic's codegen field, it
// indicates that the IR generation for that intrinsic is done by handwritten
// C++ and not autogenerated at all. The effect in the MVE builtin codegen
// function is to break out of the main switch and fall through to the
// manual-codegen cases below it, having set the CustomCodeGenType enumerated
// variable to the value given by the 'type' string here.
class CustomCodegen<string type_> { string type = type_; }

// -----------------------------------------------------------------------------
// System for building up complex instances of Type from simple ones.

// ComplexType is used to represent any more complicated type: vectors,
// multivectors, pointers etc. Its dag argument specifies how the type should
// be constructed from simpler types. The operator of the dag will always be an
// instance of ComplexTypeOp, defined below.
class ComplexType<dag spec_>: Type { dag spec = spec_; }

// Operators you can use in the ComplexType spec dag. These are an intermediate
// layer, interpreted by MveEmitter::getType() in the Tablegen backend, and
// only used in the definitions below. Actual intrinsic definitions in
// arm_mve.td will use the defs defined below here.
class ComplexTypeOp;
def CTO_Parameter: ComplexTypeOp;
def CTO_Vec: ComplexTypeOp;
def CTO_Pred: ComplexTypeOp;
class CTO_Tuple<int n_>: ComplexTypeOp { int n = n_; }
class CTO_Pointer<bit const_>: ComplexTypeOp { bit const = const_; }
def CTO_CopyKind: ComplexTypeOp;
class CTO_ScaleSize<int num_, int denom_>: ComplexTypeOp {
  int num = num_;
  int denom = denom_;
}

// -----------------------------------------------------------------------------
// Instances of Type intended to be used directly in the specification of an
// intrinsic in arm_mve.td.

// The type Void can be used for the return type of an intrinsic, and as the
// parameter type for intrinsics that aren't actually parameterised by any kind
// of _s32 / _f16 / _u8 suffix.
def Void : Type;

// A wrapper you can put on an intrinsic's argument type to prevent it from
// being automatically promoted to i32 from a smaller integer type.
class unpromoted<Type t> : Type { Type underlying_type = t; }

// Primitive types: base class, and an instance for the set of scalar integer
// and floating types that MVE uses.
class PrimitiveType<string kind_, int size_>: Type {
  string kind = kind_;
  int size = size_;
  string nameOverride = "";
}

// The type records defined by these foreaches have names like s32, f16, u8.
foreach size = [8, 16, 32, 64] in
  foreach kind = ["u", "s"] in
    def kind # size: PrimitiveType<kind, size>;
foreach size = [16, 32] in
  foreach kind = ["f"] in
    def kind # size: PrimitiveType<kind, size>;

// Sometimes we need to refer to a type by a different name in C, when
// ACLE defines a function parameter to be something like 'unsigned'
// rather than uint32_t.
def uint: PrimitiveType<"u", 32> { let nameOverride = "unsigned"; }
def sint: PrimitiveType<"s", 32> { let nameOverride = "int"; }

// VecOf<t> expects t to be a scalar, and gives a 128-bit vector of whatever it
// is.
class VecOf<Type t>: ComplexType<(CTO_Vec t)>;

// NarrowedVecOf<t,v> expects t to be a scalar type, and v to be a vector
// type. It returns a vector type whose element type is t, and whose lane
// count is the same as the lane count of v. (Used as an intermediate value
// type in the IR representation of a widening load: you load a vector of
// small things out of memory, and then zext/sext them into a full 128-bit
// output vector.)
class NarrowedVecOf<Type t, Type v>: ComplexType<(CTO_Vec t, v)>;

// PredOf expects t to be a scalar, and expands to a predicate vector which
// (logically speaking) has the same number of lanes as VecOf<t> would.
class PredOf<Type t>: ComplexType<(CTO_Pred t)>;

// Scalar expands to whatever is the main parameter type of the current
// intrinsic. Vector and Predicate expand to the vector and predicate types
// corresponding to that.
def Scalar: ComplexType<(CTO_Parameter)>;
def Vector: VecOf<Scalar>;
def Predicate: PredOf<Scalar>;

// MultiVector<n> expands to a type containing n instances of Vector. (There's
// no need to define this for a general underlying vector type, since it's only
// used by vld2q and friends, which don't need that generality.)
class MultiVector<int n>: ComplexType<(CTO_Tuple<n> Vector)>;

// Ptr<t> and CPtr<t> expand to a pointer to t, or a pointer to const t,
// respectively.
class Ptr<Type t>: ComplexType<(CTO_Pointer<0> t)>;
class CPtr<Type t>: ComplexType<(CTO_Pointer<1> t)>;

// CopyKind<s,k> expects s and k to be scalar types. It returns a scalar type
// whose kind (signed, unsigned or float) matches that of k, and whose size
// matches that of s.
class CopyKind<Type s, Type k>: ComplexType<(CTO_CopyKind s, k)>;

// DoubleSize<k> expects k to be a scalar type. It returns a scalar type
// whose kind (signed, unsigned or float) matches that of k, and whose size
// is double that of k, if possible.
class DoubleSize<Type k> : ComplexType<(CTO_ScaleSize<2, 1> k)>;
class HalfSize<Type k>   : ComplexType<(CTO_ScaleSize<1, 2> k)>;

// Unsigned<t> expects t to be a scalar type, and expands to the unsigned
// integer scalar of the same size. So it returns u16 if you give it s16 or
// f16 (or u16 itself). Similarly, Signed<t> makes the type signed.
class Unsigned<Type t>: ComplexType<(CTO_CopyKind t, u32)>;
class Signed<Type t>: ComplexType<(CTO_CopyKind t, s32)>;

// UScalar and UVector expand to the unsigned-integer versions of
// Scalar and Vector. SScalar and SVector are signed-integer versions.
def UScalar: Unsigned<Scalar>;
def UVector: VecOf<UScalar>;
def SScalar: Signed<Scalar>;
def SVector: VecOf<SScalar>;

// DblVector expands to a vector of scalars of size twice the size of Scalar.
// DblPredicate expands to a predicate corresponding to DblVector
// HalfVector, similarly, expands to a vector of half-sized scalars. And
// UHalfVector is a vector of half-sized _unsigned integers_.
def DblVector: VecOf<DoubleSize<Scalar>>;
def DblPredicate: PredOf<DoubleSize<Scalar>>;
def HalfScalar: HalfSize<Scalar>;
def HalfVector: VecOf<HalfScalar>;
def UHalfScalar: Unsigned<HalfSize<Scalar>>;
def UHalfVector: VecOf<UHalfScalar>;

// Expands to the 32-bit integer of the same signedness as Scalar.
def Scalar32: CopyKind<u32, Scalar>;
// Expands to the 64-bit integer of the same signedness as Scalar.
def Scalar64: CopyKind<u64, Scalar>;

// -----------------------------------------------------------------------------
// Internal definitions for specifying immediate arguments for an intrinsic.

class ImmediateBounds;
class Immediate<Type type_, ImmediateBounds bounds_>: Type {
  Type type = type_;
  ImmediateBounds bounds = bounds_;
  string extra;
  string extraarg;
}
class IB_ConstRange<int lo_, int hi_> : ImmediateBounds {
  int lo = lo_;
  int hi = hi_;
}
def IB_UEltValue : ImmediateBounds;
def IB_LaneIndex : ImmediateBounds;
class IB_EltBit<int base_, Type type_ = Scalar> : ImmediateBounds {
  int base = base_;
  Type type = type_;
}
def IB_ExtraArg_LaneSize;

// -----------------------------------------------------------------------------
// End-user definitions for immediate arguments.

// imm_simd and imm_simd_restrictive are used for the immediate operands to
// intrinsics like vmvnq or vorrq. imm_simd_restrictive has to be an 8-bit
// value shifted left by a whole number of bytes; imm_simd_vmvn can also be of
// the form 0xXXFF for some byte value XX.
def imm_simd_restrictive : Immediate<Scalar, IB_UEltValue> {
  let extra = "ShiftedByte";
  let extraarg = "!lanesize";
}
def imm_simd_vmvn : Immediate<Scalar, IB_UEltValue> {
  let extra = "ShiftedByteOrXXFF";
  let extraarg = "!lanesize";
}

// imm_1toN can take any value from 1 to N inclusive, where N is the number of
// bits in the main parameter type. (E.g. an immediate shift count, in an
// intrinsic that shifts every lane of a vector by the same amount.)
//
// imm_0toNm1 is the same but with the range offset by 1, i.e. 0 to N-1
// inclusive.
//
// imm_1toHalfN is like imm_1toN, but applied to a half-width type.
// (So if Scalar is s16, for example, it'll give you the range 1 to 8.)
def imm_1toN : Immediate<sint, IB_EltBit<1>>;
def imm_0toNm1 : Immediate<sint, IB_EltBit<0>>;
def imm_1toHalfN : Immediate<sint, IB_EltBit<1, HalfSize<Scalar>>>;

// imm_lane has to be the index of a vector lane in the main vector type, i.e
// it can range from 0 to (128 / size of scalar)-1 inclusive. (e.g. vgetq_lane)
def imm_lane : Immediate<sint, IB_LaneIndex>;

// imm_1to32 can be in the range 1 to 32, unconditionally. (e.g. scalar shift
// intrinsics)
def imm_1to32 : Immediate<sint, IB_ConstRange<1, 32>>;

// imm_1248 can be 1, 2, 4 or 8. (e.g. vidupq)
def imm_1248 : Immediate<sint, IB_ConstRange<1, 8>> {
  let extra = "Power2";
}

// imm_mem7bit<n> is a valid immediate offset for a load/store intrinsic whose
// memory access size is n bytes (e.g. 1 for vldrb_[whatever], 2 for vldrh,
// ...). The set of valid immediates for these is {-127*n, ..., -1*n, 0*n, 1*n,
// ..., 127*n}.
class imm_mem7bit<int membytes>
  : Immediate<sint, IB_ConstRange<!mul(membytes, -127), !mul(membytes, 127)>> {
  let extra = !if(!eq(membytes, 1), ?, "Multiple");
  let extraarg = !cast<string>(membytes);
}

// -----------------------------------------------------------------------------
// Specification of ways that the full name of an intrinsic can be mapped to
// its shorter polymorphic name.

class PolymorphicNameType<int nt_, string x_> {
  int NumTypeSuffixesToDiscard = nt_;
  string ExtraSuffixToDiscard = x_;
}

// PNT_None: the intrinsic is not polymorphic at all, so its short name is the
// same as its long name. (E.g. scalar shift intrinsics such as uqshl.)
def PNT_None:   PolymorphicNameType<0, ?>;

// PNT_Type: the usual case, in which the polymorphic name is made by dropping
// the type suffix, so it ends up the same as the Tablegen record name. E.g.
// vaddq_u16 -> vaddq.
def PNT_Type:   PolymorphicNameType<1, ?>;

// PNT_2Type: the polymorphic name is made by dropping _two_ type suffixes.
// E.g. vcvtq_f16_u16 -> vcvtq.
def PNT_2Type:  PolymorphicNameType<2, ?>;

// PNT_NType: the polymorphic name is made by dropping an "_n" suffix and a
// type. E.g. vaddq_n_u16 -> vaddq.
def PNT_NType:  PolymorphicNameType<1, "n">;

// PNT_NType: the polymorphic name is made by just dropping an "_n" suffix
// (even if it isn't at the end of the name). E.g. vidupq_n_u16 -> vidupq_u16.
def PNT_N:      PolymorphicNameType<0, "n">;

// PNT_WBType: the polymorphic name is made by dropping an "_wb" suffix and a
// type. E.g. vidupq_m_wb_u16 -> vidupq_m.
def PNT_WBType: PolymorphicNameType<1, "wb">;

// PNT_WB: the polymorphic name is made by just dropping "_wb". E.g.
// vidupq_wb_u16 -> vidupq_u16.
def PNT_WB:     PolymorphicNameType<0, "wb">;

// -----------------------------------------------------------------------------
// The main class Intrinsic. Define one of these for each family of ACLE
// intrinsics which are the same apart from some final type suffix (e.g.
// vaddq_{s8,u8,f16,...}.
//
// The record's name plus that type suffix is taken to be the full unambiguous
// name of the function. Its shorter polymorphic name is constructed from that
// in turn, in a way specified by the PolymorphicNameType system above.

class Intrinsic<Type ret_, dag args_, dag codegen_> {
  // List of parameter types to suffix to this intrinsic's name. A separate
  // actual ACLE intrinsic will be generated for each of these. Set it to
  // [Void] if the intrinsic is not polymorphic at all.
  list<Type> params;

  // Return type and arguments for the intrinsic.
  Type ret = ret_;
  dag args = args_;

  // Specification of how to generate its IR.
  dag codegen = codegen_;

  // Default to PNT_Type, which is by far the most common case.
  PolymorphicNameType pnt = PNT_Type;

  // A very few intrinsics _only_ have a polymorphic name.
  bit polymorphicOnly = 0;

  // True if the builtin has to avoid evaluating its arguments.
  bit nonEvaluating = 0;

  // True if the intrinsic needs only the C header part (no codegen, semantic
  // checks, etc). Used for redeclaring MVE intrinsics in the arm_cde.h header.
  bit headerOnly = 0;

  // Use to override the suffix letter to make e.g.vfooq_p16
  // with an override suffix letter of "p".
  string overrideKindLetter = "";

  // Name of the architecture extension, used in the Clang builtin name
  string builtinExtension = "mve";
}

// Sometimes you have to use two separate Intrinsic declarations to
// declare intrinsics that are logically the same family (e.g. vaddq,
// because it needs to expand to an Add or FAdd IR node depending on
// type). For that purpose, you can derive from NameOverride to
// specify the intrinsic's base name independently of the Tablegen
// record name.

class NameOverride<string basename_> {
  string basename = basename_;
}

// A wrapper to define both _m and _x versions of a predicated
// intrinsic.
//
// We provide optional parameters to override the polymorphic name
// types separately for the _m and _x variants, because sometimes they
// polymorph differently (typically because the type of the inactive
// parameter can be used as a disambiguator if it's present).
multiclass IntrinsicMX<Type rettype, dag arguments, dag cg,
                       bit wantXVariant = 1,
                       string nameSuffix = "",
                       PolymorphicNameType pnt_m = PNT_Type,
                       PolymorphicNameType pnt_x = PNT_Type> {
  // The _m variant takes an initial parameter called $inactive, which
  // provides the input value of the output register, i.e. all the
  // inactive lanes in the predicated operation take their values from
  // this.
  def : Intrinsic<rettype, !con((args rettype:$inactive), arguments), cg>,
        NameOverride<NAME # "_m" # nameSuffix> {
    let pnt = pnt_m;
  }

  if wantXVariant then {
    // The _x variant leaves off that parameter, and simply uses an
    // undef value of the same type.

    def : Intrinsic<rettype, arguments, (seq (undef rettype):$inactive, cg)>,
          NameOverride<NAME # "_x" # nameSuffix> {
      let pnt = pnt_x;
    }
  }
}

// Same as above, but with an additional parameter 'basename' which overrides
// the C intrinsic base name
multiclass IntrinsicMXNameOverride<Type rettype, dag arguments, dag cg,
                                   string basename, bit wantXVariant = 1,
                                   string nameSuffix = "",
                                   PolymorphicNameType pnt_m = PNT_Type,
                                   PolymorphicNameType pnt_x = PNT_Type> {
  def "_m" # nameSuffix:
     Intrinsic<rettype, !con((args rettype:$inactive), arguments), cg>,
     NameOverride<basename # "_m" # nameSuffix> {
    let pnt = pnt_m;
  }

  if wantXVariant then {
    def "_x" # nameSuffix:
      Intrinsic<rettype, arguments, (seq (undef rettype):$inactive, cg)>,
      NameOverride<basename # "_x" # nameSuffix> {
      let pnt = pnt_x;
    }
  }
}


// -----------------------------------------------------------------------------
// Convenience lists of parameter types. 'T' is just a container record, so you
// can define a typical intrinsic with 'let Params = T.Usual', or similar,
// instead of having to repeat a long list every time.

def T {
  list<Type> None = [Void];
  list<Type> Signed = [s8, s16, s32];
  list<Type> Unsigned = [u8, u16, u32];
  list<Type> Int = Signed # Unsigned;
  list<Type> Float = [f16, f32];
  list<Type> Usual = Int # Float;
  list<Type> Int8 = [s8, u8];
  list<Type> Int16 = [s16, u16];
  list<Type> Int32 = [s32, u32];
  list<Type> Int64 = [s64, u64];
  list<Type> Poly = [u8, u16]; // Actually p8 and p16
  list<Type> All8 = Int8;
  list<Type> All16 = Int16 # [f16];
  list<Type> All32 = Int32 # [f32];
  list<Type> All64 = Int64;
  list<Type> All = Usual # All64;
}

// -----------------------------------------------------------------------------
// Container record for DAG constant values. These constants are used because
// bit/int class/multiclass parameters cannot be used to produce a dag node:
// for example (u32 x) where x is 0 is transformed into (u32 { 0 }) by the
// Tablegen parser.
def V {
  dag False = (u32 0);
  dag True  = (u32 1);
}
