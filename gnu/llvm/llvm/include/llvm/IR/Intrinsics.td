//===- Intrinsics.td - Defines all LLVM intrinsics ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines properties of all LLVM intrinsics.
//
//===----------------------------------------------------------------------===//

include "llvm/CodeGen/ValueTypes.td"
include "llvm/CodeGen/SDNodeProperties.td"

//===----------------------------------------------------------------------===//
//  Properties we keep track of for intrinsics.
//===----------------------------------------------------------------------===//

class IntrinsicProperty<bit is_default = false> {
  bit IsDefault = is_default;
}

// Intr*Mem - Memory properties.  If no property is set, the worst case
// is assumed (it may read and write any memory it can get access to and it may
// have other side effects).

// IntrNoMem - The intrinsic does not access memory or have any other side
// effects.  It may be CSE'd deleted if dead, etc.
def IntrNoMem : IntrinsicProperty;

// IntrReadMem - This intrinsic only reads from memory. It does not write to
// memory and has no other side effects. Therefore, it cannot be moved across
// potentially aliasing stores. However, it can be reordered otherwise and can
// be deleted if dead.
def IntrReadMem : IntrinsicProperty;

// IntrWriteMem - This intrinsic only writes to memory, but does not read from
// memory, and has no other side effects. This means dead stores before calls
// to this intrinsics may be removed.
def IntrWriteMem : IntrinsicProperty;

// IntrArgMemOnly - This intrinsic only accesses memory that its pointer-typed
// argument(s) points to, but may access an unspecified amount. Other than
// reads from and (possibly volatile) writes to memory, it has no side effects.
def IntrArgMemOnly : IntrinsicProperty;

// IntrInaccessibleMemOnly -- This intrinsic only accesses memory that is not
// accessible by the module being compiled. This is a weaker form of IntrNoMem.
def IntrInaccessibleMemOnly : IntrinsicProperty;

// IntrInaccessibleMemOrArgMemOnly -- This intrinsic only accesses memory that
// its pointer-typed arguments point to or memory that is not accessible
// by the module being compiled. This is a weaker form of IntrArgMemOnly.
def IntrInaccessibleMemOrArgMemOnly : IntrinsicProperty;

// Commutative - This intrinsic is commutative: X op Y == Y op X.
def Commutative : IntrinsicProperty;

// Throws - This intrinsic can throw.
def Throws : IntrinsicProperty;

// Attribute index needs to match `AttrIndex` defined `Attributes.h`.
class AttrIndex<int idx> {
  int Value = idx;
}
def FuncIndex : AttrIndex<-1>;
def RetIndex : AttrIndex<0>;
class ArgIndex<int argNo> : AttrIndex<!add(argNo, 1)>;

// NoCapture - The specified argument pointer is not captured by the intrinsic.
class NoCapture<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

// NoAlias - The specified argument pointer is not aliasing other "noalias" pointer
// arguments of the intrinsic wrt. the intrinsic scope.
class NoAlias<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

// NoUndef - The specified argument is neither undef nor poison.
class NoUndef<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

// NonNull - The specified argument is not null.
class NonNull<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

class Align<AttrIndex idx, int align> : IntrinsicProperty {
  int ArgNo = idx.Value;
  int Align = align;
}

class Dereferenceable<AttrIndex idx, int bytes> : IntrinsicProperty {
  int ArgNo = idx.Value;
  int Bytes = bytes;
}

// Returned - The specified argument is always the return value of the
// intrinsic.
class Returned<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

// ImmArg - The specified argument must be an immediate.
class ImmArg<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

// ReadOnly - The specified argument pointer is not written to through the
// pointer by the intrinsic.
class ReadOnly<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

// WriteOnly - The intrinsic does not read memory through the specified
// argument pointer.
class WriteOnly<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

// ReadNone - The specified argument pointer is not dereferenced by the
// intrinsic.
class ReadNone<AttrIndex idx> : IntrinsicProperty {
  int ArgNo = idx.Value;
}

def IntrNoReturn : IntrinsicProperty;

// Applied by default.
def IntrNoCallback : IntrinsicProperty<1>;

// IntrNoSync - Threads executing the intrinsic will not synchronize using
// memory or other means. Applied by default.
def IntrNoSync : IntrinsicProperty<1>;

// Applied by default.
def IntrNoFree : IntrinsicProperty<1>;

// Applied by default.
def IntrWillReturn : IntrinsicProperty<1>;

// IntrCold - Calls to this intrinsic are cold.
// Parallels the cold attribute on LLVM IR functions.
def IntrCold : IntrinsicProperty;

// IntrNoDuplicate - Calls to this intrinsic cannot be duplicated.
// Parallels the noduplicate attribute on LLVM IR functions.
def IntrNoDuplicate : IntrinsicProperty;

// IntrNoMerge - Calls to this intrinsic cannot be merged
// Parallels the nomerge attribute on LLVM IR functions.
def IntrNoMerge : IntrinsicProperty;

// IntrConvergent - Calls to this intrinsic are convergent and may not be made
// control-dependent on any additional values.
// Parallels the convergent attribute on LLVM IR functions.
def IntrConvergent : IntrinsicProperty;

// This property indicates that the intrinsic is safe to speculate.
def IntrSpeculatable : IntrinsicProperty;

// This property can be used to override the 'has no other side effects'
// language of the IntrNoMem, IntrReadMem, IntrWriteMem, and IntrArgMemOnly
// intrinsic properties.  By default, intrinsics are assumed to have side
// effects, so this property is only necessary if you have defined one of
// the memory properties listed above.
// For this property, 'side effects' has the same meaning as 'side effects'
// defined by the hasSideEffects property of the TableGen Instruction class.
def IntrHasSideEffects : IntrinsicProperty;

//===----------------------------------------------------------------------===//
// IIT constants and utils
//===----------------------------------------------------------------------===//

// llvm::Intrinsic::IITDescriptor::ArgKind::AK_%
def ArgKind {
  int Any        = 0;
  int AnyInteger = 1;
  int AnyFloat   = 2;
  int AnyVector  = 3;
  int AnyPointer = 4;

  int MatchType  = 7;
}

// Encode placeholder.
// [15:8] is the ID used how to resolve ArgCode.

// (ACIdx << 3) | ArgCode
class EncAnyType<int ArgCode=0> {
  int ID = 0x100;
  int ret = !or(ID, ArgCode);
}

// (Mapping[Num] << 3) | AK.MatchType
class EncMatchType<int Num=0> {
  int ID = 0x200;
  int ret = !or(ID, Num);
}

// (Mapping[Num] << 3) | ArgCodes[Mapping[Num]]
class EncSameWidth<int Num=0> {
  int ID = 0x300;
  int ret = !or(ID, Num);
}

// ACIdx
class EncNextArgA<int dummy=0> {
  int ID = 0x400;
  int ret = !or(ID, dummy);
}

// Mapping[Num]
class EncNextArgN<int Num=0> {
  int ID = 0x500;
  int ret = !or(ID, Num);
}

class ResolveArgCode<
    list<int> Mapping,
    list<int> ArgCodes,
    int ACIdx,
    int ax> {
  int ah = !and(ax, 0xFF00);
  int al = !and(ax, 0x00FF);
  int num = Mapping[al];
  int ret = !cond(
    !eq(ah, EncAnyType<>.ID)   : !or(!shl(ACIdx, 3), al),
    !eq(ah, EncMatchType<>.ID) : !or(!shl(num, 3), ArgKind.MatchType),
    !eq(ah, EncSameWidth<>.ID) : !or(!shl(num, 3), ArgCodes[num]),
    !eq(ah, EncNextArgA<>.ID)  : ACIdx,
    !eq(ah, EncNextArgN<>.ID)  : num,
    true : al);
}

//===----------------------------------------------------------------------===//
// IIT_Info
//===----------------------------------------------------------------------===//

class IIT_Base<int num> {
  int Number = num;
  list<ValueType> VTs = ?;
}

class IIT_VT<ValueType vt, int num> : IIT_Base<num> {
  let VTs = [vt];
}

class IIT_Int<int size, int num> : IIT_Base<num> {
  let VTs = !filter(vti, ValueTypes,
    !and(vti.isInteger, !eq(vti.Size, size)));
}

class IIT_Vec<int nelem, int num> : IIT_Base<num> {
  let VTs = !filter(vti, ValueTypes,
    !and(vti.isVector, !eq(vti.nElem, nelem)));
}

defset list<IIT_Base> IIT_all = {
def IIT_Done : IIT_Base<    0>;
def IIT_I1   : IIT_Int<1,   1>;
def IIT_I8   : IIT_Int<8,   2>;
def IIT_I16  : IIT_Int<16,  3>;
def IIT_I32  : IIT_Int<32,  4>;
def IIT_I64  : IIT_Int<64,  5>;
def IIT_F16  : IIT_VT<f16,  6>;
def IIT_F32  : IIT_VT<f32,  7>;
def IIT_F64  : IIT_VT<f64,  8>;
def IIT_V2   : IIT_Vec<2,   9>;
def IIT_V4   : IIT_Vec<4,  10>;
def IIT_V8   : IIT_Vec<8,  11>;
def IIT_V16  : IIT_Vec<16, 12>;
def IIT_V32  : IIT_Vec<32, 13>;
def IIT_PTR  : IIT_Base<   14>;
def IIT_ARG  : IIT_Base<   15>;

def IIT_V64 : IIT_Vec<64, 16>;
def IIT_MMX : IIT_VT<x86mmx, 17>;
def IIT_TOKEN : IIT_VT<token, 18>;
def IIT_METADATA : IIT_VT<MetadataVT, 19>;
def IIT_EMPTYSTRUCT : IIT_VT<OtherVT, 20>;
def IIT_STRUCT2 : IIT_Base<21>;
def IIT_STRUCT3 : IIT_Base<22>;
def IIT_STRUCT4 : IIT_Base<23>;
def IIT_STRUCT5 : IIT_Base<24>;
def IIT_EXTEND_ARG : IIT_Base<25>;
def IIT_TRUNC_ARG : IIT_Base<26>;
def IIT_ANYPTR : IIT_Base<27>;
def IIT_V1 : IIT_Vec<1, 28>;
def IIT_VARARG : IIT_VT<isVoid, 29>;
def IIT_HALF_VEC_ARG : IIT_Base<30>;
def IIT_SAME_VEC_WIDTH_ARG : IIT_Base<31>;
def IIT_VEC_OF_ANYPTRS_TO_ELT : IIT_Base<34>;
def IIT_I128 : IIT_Int<128, 35>;
def IIT_V512 : IIT_Vec<512, 36>;
def IIT_V1024 : IIT_Vec<1024, 37>;
def IIT_STRUCT6 : IIT_Base<38>;
def IIT_STRUCT7 : IIT_Base<39>;
def IIT_STRUCT8 : IIT_Base<40>;
def IIT_F128 : IIT_VT<f128, 41>;
def IIT_VEC_ELEMENT : IIT_Base<42>;
def IIT_SCALABLE_VEC : IIT_Base<43>;
def IIT_SUBDIVIDE2_ARG : IIT_Base<44>;
def IIT_SUBDIVIDE4_ARG : IIT_Base<45>;
def IIT_VEC_OF_BITCASTS_TO_INT : IIT_Base<46>;
def IIT_V128 : IIT_Vec<128, 47>;
def IIT_BF16 : IIT_VT<bf16, 48>;
def IIT_STRUCT9 : IIT_Base<49>;
def IIT_V256 : IIT_Vec<256, 50>;
def IIT_AMX : IIT_VT<x86amx, 51>;
def IIT_PPCF128 : IIT_VT<ppcf128, 52>;
def IIT_V3 : IIT_Vec<3, 53>;
def IIT_EXTERNREF : IIT_VT<externref, 54>;
def IIT_FUNCREF : IIT_VT<funcref, 55>;
def IIT_I2 : IIT_Int<2, 57>;
def IIT_I4 : IIT_Int<4, 58>;
def IIT_AARCH64_SVCOUNT : IIT_VT<aarch64svcount, 59>;
def IIT_V6 : IIT_Vec<6, 60>;
def IIT_V10 : IIT_Vec<10, 61>;
}

defvar IIT_all_FixedTypes = !filter(iit, IIT_all,
  !or(!isa<IIT_VT>(iit), !isa<IIT_Int>(iit)));

defvar IIT_all_VectorTypes = !filter(iit, IIT_all,
  !isa<IIT_Vec>(iit));

defvar IIT_RetNumbers = [
  [IIT_Done.Number],
  []<int>,
  [IIT_STRUCT2.Number],
  [IIT_STRUCT3.Number],
  [IIT_STRUCT4.Number],
  [IIT_STRUCT5.Number],
  [IIT_STRUCT6.Number],
  [IIT_STRUCT7.Number],
  [IIT_STRUCT8.Number],
  [IIT_STRUCT9.Number],
];

//===----------------------------------------------------------------------===//
// Types used by intrinsics.
//===----------------------------------------------------------------------===//

class LLVMType<ValueType vt> {
  ValueType VT = vt;
  int isAny = vt.isOverloaded;

  int ArgCode = ?;
  int Number = ?;

  list<IIT_Base> IITs = !filter(iit, IIT_all_FixedTypes,
    !not(!empty(!filter(iit_vt, iit.VTs,
      !eq(iit_vt, !if(vt.isVector, vt.ElementType, vt))))));
  assert !le(!size(IITs), 1), "Duplicate type";

  list<IIT_Base> IIT_Vecs = !if(vt.isVector,
    !filter(iit, IIT_all_VectorTypes,
      !not(!empty(!filter(iit_vt, iit.VTs, !and(
        !eq(iit_vt.ElementType, vt.ElementType),
        !eq(iit_vt.nElem, vt.nElem)))))),
    []);
  assert !le(!size(IIT_Vecs), 1), "Duplicate type";

  list<int> Sig = !listconcat(
    !if(vt.isScalable, [IIT_SCALABLE_VEC.Number], []),
    !foreach(iit, IIT_Vecs, iit.Number),
    !foreach(iit, IITs,     iit.Number));
}

class LLVMAnyType<ValueType vt> : LLVMType<vt> {
  let ArgCode = !cond(
    !eq(vt, Any)     : ArgKind.Any,
    !eq(vt, iAny)    : ArgKind.AnyInteger,
    !eq(vt, fAny)    : ArgKind.AnyFloat,
    !eq(vt, vAny)    : ArgKind.AnyVector,
    !eq(vt, iPTRAny) : ArgKind.AnyPointer,
  );
  let Sig = [
    IIT_ARG.Number,
    EncAnyType<ArgCode>.ret,
  ];

  assert isAny, "LLVMAnyType.VT should have isOverloaded";
}

class LLVMQualPointerType<int addrspace>
  : LLVMType<iPTR> {
  assert !and(!le(0, addrspace), !le(addrspace, 255)),
    "Address space exceeds 255";

  let Sig =
    !if(addrspace, [
      IIT_ANYPTR.Number,
      addrspace,
    ], [
      IIT_PTR.Number,
    ]);
}

class LLVMAnyPointerType : LLVMAnyType<iPTRAny> {
  assert isAny, "iPTRAny should have isOverloaded";
}

// Match the type of another intrinsic parameter.  Number is an index into the
// list of overloaded types for the intrinsic, excluding all the fixed types.
// The Number value must refer to a previously listed type.  For example:
//   Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_anyfloat_ty, LLVMMatchType<0>]>
// has two overloaded types, the 2nd and 3rd arguments.  LLVMMatchType<0>
// refers to the first overloaded type, which is the 2nd argument.
class LLVMMatchType<int num, IIT_Base IIT_Info = IIT_ARG>
  : LLVMType<OtherVT>{
  let Number = num;
  let Sig = [
    IIT_Info.Number,
    EncMatchType<num>.ret,
  ];
}

class LLVMMatchTypeNextArg<int num, IIT_Base IIT_Info>
  : LLVMMatchType<num, IIT_Info> {
  let Sig = [
    IIT_Info.Number,
    EncNextArgA<>.ret,
    EncNextArgN<num>.ret,
  ];
}

// Match the type of another intrinsic parameter that is expected to be based on
// an integral type (i.e. either iN or <N x iM>), but change the scalar size to
// be twice as wide or half as wide as the other type.  This is only useful when
// the intrinsic is overloaded, so the matched type should be declared as iAny.
class LLVMExtendedType<int num> : LLVMMatchType<num, IIT_EXTEND_ARG>;
class LLVMTruncatedType<int num> : LLVMMatchType<num, IIT_TRUNC_ARG>;

// Match the scalar/vector of another intrinsic parameter but with a different
// element type. Either both are scalars or both are vectors with the same
// number of elements.
class LLVMScalarOrSameVectorWidth<int idx, LLVMType elty>
  : LLVMMatchType<idx, IIT_SAME_VEC_WIDTH_ARG> {
  let Sig = !listconcat([
    IIT_SAME_VEC_WIDTH_ARG.Number,
    EncSameWidth<idx>.ret,
  ], elty.Sig);
}

class LLVMVectorOfAnyPointersToElt<int num>
  : LLVMMatchTypeNextArg<num, IIT_VEC_OF_ANYPTRS_TO_ELT>;
class LLVMVectorElementType<int num> : LLVMMatchType<num, IIT_VEC_ELEMENT>;

// Match the type of another intrinsic parameter that is expected to be a
// vector type, but change the element count to be half as many.
class LLVMHalfElementsVectorType<int num>
  : LLVMMatchType<num, IIT_HALF_VEC_ARG>;

// Match the type of another intrinsic parameter that is expected to be a
// vector type (i.e. <N x iM>) but with each element subdivided to
// form a vector with more elements that are smaller than the original.
class LLVMSubdivide2VectorType<int num>
  : LLVMMatchType<num, IIT_SUBDIVIDE2_ARG>;
class LLVMSubdivide4VectorType<int num>
  : LLVMMatchType<num, IIT_SUBDIVIDE4_ARG>;

// Match the element count and bit width of another intrinsic parameter, but
// change the element type to an integer.
class LLVMVectorOfBitcastsToInt<int num>
  : LLVMMatchType<num, IIT_VEC_OF_BITCASTS_TO_INT>;

def llvm_void_ty       : LLVMType<isVoid>;

def llvm_any_ty        : LLVMAnyType<Any>;
def llvm_anyint_ty     : LLVMAnyType<iAny>;
def llvm_anyfloat_ty   : LLVMAnyType<fAny>;
def llvm_anyvector_ty  : LLVMAnyType<vAny>;

def llvm_i1_ty         : LLVMType<i1>;
def llvm_i8_ty         : LLVMType<i8>;
def llvm_i16_ty        : LLVMType<i16>;
def llvm_i32_ty        : LLVMType<i32>;
def llvm_i64_ty        : LLVMType<i64>;
def llvm_i128_ty       : LLVMType<i128>;
def llvm_half_ty       : LLVMType<f16>;
def llvm_bfloat_ty     : LLVMType<bf16>;
def llvm_float_ty      : LLVMType<f32>;
def llvm_double_ty     : LLVMType<f64>;
def llvm_f80_ty        : LLVMType<f80>;
def llvm_f128_ty       : LLVMType<f128>;
def llvm_ppcf128_ty    : LLVMType<ppcf128>;
def llvm_ptr_ty        : LLVMQualPointerType<0>; // ptr
def llvm_anyptr_ty     : LLVMAnyPointerType;     // ptr addrspace(N)
def llvm_empty_ty      : LLVMType<OtherVT>;      // { }
def llvm_metadata_ty   : LLVMType<MetadataVT>;   // !{...}
def llvm_token_ty      : LLVMType<token>;        // token

def llvm_x86mmx_ty     : LLVMType<x86mmx>;

def llvm_aarch64_svcount_ty : LLVMType<aarch64svcount>;

def llvm_x86amx_ty     : LLVMType<x86amx>;

def llvm_v2i1_ty       : LLVMType<v2i1>;     //   2 x i1
def llvm_v4i1_ty       : LLVMType<v4i1>;     //   4 x i1
def llvm_v8i1_ty       : LLVMType<v8i1>;     //   8 x i1
def llvm_v16i1_ty      : LLVMType<v16i1>;    //  16 x i1
def llvm_v32i1_ty      : LLVMType<v32i1>;    //  32 x i1
def llvm_v64i1_ty      : LLVMType<v64i1>;    //  64 x i1
def llvm_v128i1_ty     : LLVMType<v128i1>;   // 128 x i1
def llvm_v256i1_ty     : LLVMType<v256i1>;   // 256 x i1
def llvm_v512i1_ty     : LLVMType<v512i1>;   // 512 x i1
def llvm_v1024i1_ty    : LLVMType<v1024i1>;  //1024 x i1
def llvm_v2048i1_ty    : LLVMType<v2048i1>;  //2048 x i1

def llvm_v1i8_ty       : LLVMType<v1i8>;     //  1 x i8
def llvm_v2i8_ty       : LLVMType<v2i8>;     //  2 x i8
def llvm_v3i8_ty       : LLVMType<v3i8>;     //  3 x i8
def llvm_v4i8_ty       : LLVMType<v4i8>;     //  4 x i8
def llvm_v8i8_ty       : LLVMType<v8i8>;     //  8 x i8
def llvm_v16i8_ty      : LLVMType<v16i8>;    // 16 x i8
def llvm_v32i8_ty      : LLVMType<v32i8>;    // 32 x i8
def llvm_v64i8_ty      : LLVMType<v64i8>;    // 64 x i8
def llvm_v128i8_ty     : LLVMType<v128i8>;   //128 x i8
def llvm_v256i8_ty     : LLVMType<v256i8>;   //256 x i8

def llvm_v1i16_ty      : LLVMType<v1i16>;    //  1 x i16
def llvm_v2i16_ty      : LLVMType<v2i16>;    //  2 x i16
def llvm_v4i16_ty      : LLVMType<v4i16>;    //  4 x i16
def llvm_v8i16_ty      : LLVMType<v8i16>;    //  8 x i16
def llvm_v16i16_ty     : LLVMType<v16i16>;   // 16 x i16
def llvm_v32i16_ty     : LLVMType<v32i16>;   // 32 x i16
def llvm_v64i16_ty     : LLVMType<v64i16>;   // 64 x i16
def llvm_v128i16_ty    : LLVMType<v128i16>;  //128 x i16

def llvm_v1i32_ty      : LLVMType<v1i32>;    //  1 x i32
def llvm_v2i32_ty      : LLVMType<v2i32>;    //  2 x i32
def llvm_v3i32_ty      : LLVMType<v3i32>;    //  3 x i32
def llvm_v4i32_ty      : LLVMType<v4i32>;    //  4 x i32
def llvm_v6i32_ty      : LLVMType<v6i32>;    //  6 x i32
def llvm_v8i32_ty      : LLVMType<v8i32>;    //  8 x i32
def llvm_v16i32_ty     : LLVMType<v16i32>;   // 16 x i32
def llvm_v32i32_ty     : LLVMType<v32i32>;   // 32 x i32
def llvm_v64i32_ty     : LLVMType<v64i32>;   // 64 x i32
def llvm_v256i32_ty    : LLVMType<v256i32>;  //256 x i32

def llvm_v1i64_ty      : LLVMType<v1i64>;    //  1 x i64
def llvm_v2i64_ty      : LLVMType<v2i64>;    //  2 x i64
def llvm_v4i64_ty      : LLVMType<v4i64>;    //  4 x i64
def llvm_v8i64_ty      : LLVMType<v8i64>;    //  8 x i64
def llvm_v16i64_ty     : LLVMType<v16i64>;   // 16 x i64
def llvm_v32i64_ty     : LLVMType<v32i64>;   // 32 x i64

def llvm_v1i128_ty     : LLVMType<v1i128>;   //  1 x i128

def llvm_v2f16_ty      : LLVMType<v2f16>;    //  2 x half (__fp16)
def llvm_v4f16_ty      : LLVMType<v4f16>;    //  4 x half (__fp16)
def llvm_v8f16_ty      : LLVMType<v8f16>;    //  8 x half (__fp16)
def llvm_v16f16_ty     : LLVMType<v16f16>;   // 16 x half (__fp16)
def llvm_v32f16_ty     : LLVMType<v32f16>;   // 32 x half (__fp16)
def llvm_v2bf16_ty     : LLVMType<v2bf16>;   //  2 x bfloat (__bf16)
def llvm_v4bf16_ty     : LLVMType<v4bf16>;   //  4 x bfloat (__bf16)
def llvm_v8bf16_ty     : LLVMType<v8bf16>;   //  8 x bfloat (__bf16)
def llvm_v16bf16_ty    : LLVMType<v16bf16>;  // 16 x bfloat (__bf16)
def llvm_v32bf16_ty    : LLVMType<v32bf16>;  // 32 x bfloat (__bf16)
def llvm_v1f32_ty      : LLVMType<v1f32>;    //  1 x float
def llvm_v2f32_ty      : LLVMType<v2f32>;    //  2 x float
def llvm_v3f32_ty      : LLVMType<v3f32>;    //  3 x float
def llvm_v4f32_ty      : LLVMType<v4f32>;    //  4 x float
def llvm_v8f32_ty      : LLVMType<v8f32>;    //  8 x float
def llvm_v16f32_ty     : LLVMType<v16f32>;   // 16 x float
def llvm_v32f32_ty     : LLVMType<v32f32>;   // 32 x float
def llvm_v1f64_ty      : LLVMType<v1f64>;    //  1 x double
def llvm_v2f64_ty      : LLVMType<v2f64>;    //  2 x double
def llvm_v4f64_ty      : LLVMType<v4f64>;    //  4 x double
def llvm_v8f64_ty      : LLVMType<v8f64>;    //  8 x double
def llvm_v16f64_ty     : LLVMType<v16f64>;   // 16 x double

def llvm_vararg_ty     : LLVMType<isVoid>;   // this means vararg here

def llvm_externref_ty  : LLVMType<externref>;
def llvm_funcref_ty    : LLVMType<funcref>;
def llvm_exnref_ty     : LLVMType<exnref>;

//===----------------------------------------------------------------------===//

class MakeIdx<list<int> Set> {
  list<int> IdxsR = !foreach(i, !range(Set),
    !if(Set[i],
      !foldl(0, !range(0, i), m, j, !add(m, Set[j])),
      -1));

  list<int> RIdxsR = !foreach(i, !range(Set),
    !foldl(-1, !range(Set), m, j,
      !if(!and(Set[j], !eq(IdxsR[j], i)), j, m)));

  list<int> Idxs  = !foreach(a, IdxsR,  !if(!ge(a, 0), a, ?));
  list<int> RIdxs = !foreach(a, RIdxsR, !if(!ge(a, 0), a, ?));
}

class TypeInfoGen<
    list<LLVMType> RetTypes,
    list<LLVMType> ParamTypes> {
  list<LLVMType> AllTypes = !listconcat(RetTypes, ParamTypes);

  // ArgCodes for NextArg -- isAny or MatchTypeNextArg
  list<int> ACIdxs = MakeIdx<
    !foreach(ty, AllTypes,
      !or(ty.isAny, !isa<LLVMMatchTypeNextArg>(ty)))>.Idxs;

  // ArgCodes (only for isAny or MatchTypeNextArg)
  list<LLVMType> ACTys = !filter(ty, AllTypes,
    !or(ty.isAny, !isa<LLVMMatchTypeNextArg>(ty)));

  list<int> ArgCodes = !foreach(ty, ACTys, ty.ArgCode);

  // Mappings MatchTypeIdx to ACTys
  list<int> MappingRIdxs = MakeIdx<
    !foreach(ty, ACTys, ty.isAny)>.RIdxs;

  // D63507: Exclude LLVMPointerType<llvm_any_ty>
  bit isOverloaded = !not(!empty(!filter(ty, AllTypes,
    !isa<LLVMAnyType>(ty))));

  list<LLVMType> Types = !foreach(ty, AllTypes,
    !if(!isa<LLVMMatchType>(ty), ACTys[MappingRIdxs[ty.Number]], ty));

  list<list<int>> TypeSig = !listconcat(
    [IIT_RetNumbers[!size(RetTypes)]],
    !foreach(i, !range(AllTypes),
      !foreach(a, AllTypes[i].Sig,
        ResolveArgCode<
          MappingRIdxs,
          ArgCodes,
          ACIdxs[i],
          a>.ret)));
}

//===----------------------------------------------------------------------===//
// Intrinsic Definitions.
//===----------------------------------------------------------------------===//

// Intrinsic class - This is used to define one LLVM intrinsic.  The name of the
// intrinsic definition should start with "int_", then match the LLVM intrinsic
// name with the "llvm." prefix removed, and all "."s turned into "_"s.  For
// example, llvm.bswap.i16 -> int_bswap_i16.
//
//  * RetTypes is a list containing the return types expected for the
//    intrinsic.
//  * ParamTypes is a list containing the parameter types expected for the
//    intrinsic.
//  * Properties can be set to describe the behavior of the intrinsic.
//
class Intrinsic<list<LLVMType> ret_types,
                list<LLVMType> param_types = [],
                list<IntrinsicProperty> intr_properties = [],
                string name = "",
                list<SDNodeProperty> sd_properties = [],
                bit disable_default_attributes = true> : SDPatternOperator {
  string LLVMName = name;
  string TargetPrefix = "";   // Set to a prefix for target-specific intrinsics.
  list<LLVMType> RetTypes = ret_types;
  list<LLVMType> ParamTypes = param_types;
  list<IntrinsicProperty> IntrProperties = intr_properties;
  let Properties = sd_properties;

  // Disable applying IntrinsicProperties that are marked default with
  // IntrinsicProperty<1>
  bit DisableDefaultAttributes = disable_default_attributes;

  bit isTarget = false;

  TypeInfoGen TypeInfo = TypeInfoGen<RetTypes, ParamTypes>;
  bit isOverloaded = TypeInfo.isOverloaded;
  list<LLVMType> Types = TypeInfo.Types;
  list<list<int>> TypeSig = TypeInfo.TypeSig;
}

// Intrinsic with default attributes (disable_default_attributes = false).
class DefaultAttrsIntrinsic<list<LLVMType> ret_types,
                list<LLVMType> param_types = [],
                list<IntrinsicProperty> intr_properties = [],
                string name = "",
                list<SDNodeProperty> sd_properties = []>
                : Intrinsic<ret_types, param_types,
                            intr_properties, name,
                            sd_properties, /*disable_default_attributes*/ 0> {}

/// ClangBuiltin - If this intrinsic exactly corresponds to a Clang builtin, this
/// specifies the name of the builtin.  This provides automatic CBE and CFE
/// support.
class ClangBuiltin<string name> {
  string ClangBuiltinName = name;
}

class MSBuiltin<string name> {
  string MSBuiltinName = name;
}

#ifndef TEST_INTRINSICS_SUPPRESS_DEFS

//===--------------- Variable Argument Handling Intrinsics ----------------===//
//

def int_vastart : DefaultAttrsIntrinsic<[],
                                        [llvm_anyptr_ty], [], "llvm.va_start">;
def int_vacopy  : DefaultAttrsIntrinsic<[],
                                        [llvm_anyptr_ty, LLVMMatchType<0>], [],
                                        "llvm.va_copy">;
def int_vaend   : DefaultAttrsIntrinsic<[],
                                        [llvm_anyptr_ty], [], "llvm.va_end">;

//===------------------- Garbage Collection Intrinsics --------------------===//
//
def int_gcroot  : Intrinsic<[],
                            [llvm_ptr_ty, llvm_ptr_ty]>;
def int_gcread  : Intrinsic<[llvm_ptr_ty],
                            [llvm_ptr_ty, llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_gcwrite : Intrinsic<[],
                            [llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
                            [IntrArgMemOnly, NoCapture<ArgIndex<1>>,
                             NoCapture<ArgIndex<2>>]>;

//===------------------- ObjC ARC runtime Intrinsics --------------------===//
//
// Note these are to support the Objective-C ARC optimizer which wants to
// eliminate retain and releases where possible.

def int_objc_autorelease                    : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty],
                                                        [Returned<ArgIndex<0>>]>;
def int_objc_autoreleasePoolPop             : Intrinsic<[], [llvm_ptr_ty]>;
def int_objc_autoreleasePoolPush            : Intrinsic<[llvm_ptr_ty], []>;
def int_objc_autoreleaseReturnValue         : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty],
                                                        [Returned<ArgIndex<0>>]>;
def int_objc_copyWeak                       : Intrinsic<[],
                                                        [llvm_ptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_destroyWeak                    : Intrinsic<[], [llvm_ptr_ty]>;
def int_objc_initWeak                       : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_loadWeak                       : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_loadWeakRetained               : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_moveWeak                       : Intrinsic<[],
                                                        [llvm_ptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_release                        : Intrinsic<[], [llvm_ptr_ty]>;
def int_objc_retain                         : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty],
                                                        [Returned<ArgIndex<0>>]>;
def int_objc_retainAutorelease              : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty],
                                                        [Returned<ArgIndex<0>>]>;
def int_objc_retainAutoreleaseReturnValue   : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty],
                                                        [Returned<ArgIndex<0>>]>;
def int_objc_retainAutoreleasedReturnValue  : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_retainBlock                    : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_storeStrong                    : Intrinsic<[],
                                                        [llvm_ptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_storeWeak                      : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_clang_arc_use                  : Intrinsic<[],
                                                        [llvm_vararg_ty]>;
def int_objc_clang_arc_noop_use : DefaultAttrsIntrinsic<[],
                                                        [llvm_vararg_ty],
                                                        [IntrInaccessibleMemOnly]>;
def int_objc_unsafeClaimAutoreleasedReturnValue : Intrinsic<[llvm_ptr_ty],
                                                            [llvm_ptr_ty]>;
def int_objc_retainedObject                 : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_unretainedObject               : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_unretainedPointer              : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_retain_autorelease             : Intrinsic<[llvm_ptr_ty],
                                                        [llvm_ptr_ty],
                                                        [Returned<ArgIndex<0>>]>;
def int_objc_sync_enter                     : Intrinsic<[llvm_i32_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_sync_exit                      : Intrinsic<[llvm_i32_ty],
                                                        [llvm_ptr_ty]>;
def int_objc_arc_annotation_topdown_bbstart : Intrinsic<[],
                                                        [llvm_ptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_arc_annotation_topdown_bbend   : Intrinsic<[],
                                                        [llvm_ptr_ty,
                                                         llvm_ptr_ty]>;
def int_objc_arc_annotation_bottomup_bbstart  : Intrinsic<[],
                                                          [llvm_ptr_ty,
                                                           llvm_ptr_ty]>;
def int_objc_arc_annotation_bottomup_bbend  : Intrinsic<[],
                                                        [llvm_ptr_ty,
                                                         llvm_ptr_ty]>;
//===--------------- Swift asynchronous context intrinsics ----------------===//

// Returns the location of the Swift asynchronous context (usually stored just
// before the frame pointer), and triggers the creation of a null context if it
// would otherwise be unneeded.
def int_swift_async_context_addr : Intrinsic<[llvm_ptr_ty], [], []>;

//===--------------------- Code Generator Intrinsics ----------------------===//
//
def int_returnaddress : DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_i32_ty],
                                  [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_addressofreturnaddress : DefaultAttrsIntrinsic<[llvm_anyptr_ty], [], [IntrNoMem]>;
def int_frameaddress : DefaultAttrsIntrinsic<[llvm_anyptr_ty], [llvm_i32_ty],
                                 [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_sponentry  : DefaultAttrsIntrinsic<[llvm_anyptr_ty], [], [IntrNoMem]>;
def int_read_register  : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_metadata_ty],
                                   [IntrReadMem], "llvm.read_register">;
def int_write_register : Intrinsic<[], [llvm_metadata_ty, llvm_anyint_ty],
                                   [IntrNoCallback], "llvm.write_register">;
def int_read_volatile_register  : Intrinsic<[llvm_anyint_ty], [llvm_metadata_ty],
                                            [IntrHasSideEffects],
                                             "llvm.read_volatile_register">;

// Gets the address of the local variable area. This is typically a copy of the
// stack, frame, or base pointer depending on the type of prologue.
def int_localaddress : DefaultAttrsIntrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;

// Escapes local variables to allow access from other functions.
def int_localescape : DefaultAttrsIntrinsic<[], [llvm_vararg_ty]>;

// Given a function and the localaddress of a parent frame, returns a pointer
// to an escaped allocation indicated by the index.
def int_localrecover : DefaultAttrsIntrinsic<[llvm_ptr_ty],
                                 [llvm_ptr_ty, llvm_ptr_ty, llvm_i32_ty],
                                 [IntrNoMem, ImmArg<ArgIndex<2>>]>;

// Given the frame pointer passed into an SEH filter function, returns a
// pointer to the local variable area suitable for use with llvm.localrecover.
def int_eh_recoverfp : DefaultAttrsIntrinsic<[llvm_ptr_ty],
                                 [llvm_ptr_ty, llvm_ptr_ty],
                                 [IntrNoMem]>;

// To mark the beginning/end of a try-scope for Windows SEH -EHa
//  calls/invokes to these intrinsics are placed to model control flows
//    caused by HW exceptions under option -EHa.
//  calls/invokes to these intrinsics will be discarded during a codegen pass
//   after EH tables are generated
def int_seh_try_begin : Intrinsic<[], [], [IntrWriteMem, IntrWillReturn]>;
def int_seh_try_end : Intrinsic<[], [], [IntrWriteMem, IntrWillReturn]>;
def int_seh_scope_begin : Intrinsic<[], [], [IntrNoMem]>;
def int_seh_scope_end : Intrinsic<[], [], [IntrNoMem]>;

// Note: we treat stacksave/stackrestore as writemem because we don't otherwise
// model their dependencies on allocas.
def int_stacksave     : DefaultAttrsIntrinsic<[llvm_anyptr_ty]>,
                        ClangBuiltin<"__builtin_stack_save">;
def int_stackrestore  : DefaultAttrsIntrinsic<[], [llvm_anyptr_ty]>,
                        ClangBuiltin<"__builtin_stack_restore">;

def int_get_dynamic_area_offset : DefaultAttrsIntrinsic<[llvm_anyint_ty]>;

def int_thread_pointer : DefaultAttrsIntrinsic<[llvm_ptr_ty], [], [IntrNoMem]>,
                         ClangBuiltin<"__builtin_thread_pointer">;

// IntrInaccessibleMemOrArgMemOnly is a little more pessimistic than strictly
// necessary for prefetch, however it does conveniently prevent the prefetch
// from being reordered overly much with respect to nearby access to the same
// memory while not impeding optimization.
def int_prefetch
    : DefaultAttrsIntrinsic<[], [ llvm_anyptr_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty ],
                [IntrInaccessibleMemOrArgMemOnly, IntrWillReturn,
                 ReadOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>,
                 ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
def int_pcmarker      : DefaultAttrsIntrinsic<[], [llvm_i32_ty]>;

def int_readcyclecounter : DefaultAttrsIntrinsic<[llvm_i64_ty]>;

def int_readsteadycounter : DefaultAttrsIntrinsic<[llvm_i64_ty]>;

// The assume intrinsic is marked InaccessibleMemOnly so that proper control
// dependencies will be maintained.
def int_assume : DefaultAttrsIntrinsic<
    [], [llvm_i1_ty], [IntrWriteMem, IntrInaccessibleMemOnly, NoUndef<ArgIndex<0>>]>;

// 'llvm.experimental.noalias.scope.decl' intrinsic: Inserted at the location of
// noalias scope declaration. Makes it possible to identify that a noalias scope
// is only valid inside the body of a loop.
//
// Purpose of the different arguments:
// - arg0: id.scope: metadata representing the scope declaration.
def int_experimental_noalias_scope_decl
    : DefaultAttrsIntrinsic<[], [llvm_metadata_ty],
        [IntrInaccessibleMemOnly]>; // blocks LICM and some more

// Stack Protector Intrinsic - The stackprotector intrinsic writes the stack
// guard to the correct place on the stack frame.
def int_stackprotector : DefaultAttrsIntrinsic<[], [llvm_ptr_ty, llvm_ptr_ty], []>;
def int_stackguard : DefaultAttrsIntrinsic<[llvm_ptr_ty], [], []>;

// A cover for instrumentation based profiling.
def int_instrprof_cover : Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty,
                                         llvm_i32_ty, llvm_i32_ty]>;

// A counter increment for instrumentation based profiling.
def int_instrprof_increment : Intrinsic<[],
                                        [llvm_ptr_ty, llvm_i64_ty,
                                         llvm_i32_ty, llvm_i32_ty]>;

// A counter increment with step for instrumentation based profiling.
def int_instrprof_increment_step : Intrinsic<[],
                                        [llvm_ptr_ty, llvm_i64_ty,
                                         llvm_i32_ty, llvm_i32_ty, llvm_i64_ty]>;

// Callsite instrumentation for contextual profiling
def int_instrprof_callsite : Intrinsic<[],
                                        [llvm_ptr_ty, llvm_i64_ty,
                                         llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty]>;

// A timestamp for instrumentation based profiling.
def int_instrprof_timestamp : Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty,
                                             llvm_i32_ty, llvm_i32_ty]>;

// A call to profile runtime for value profiling of target expressions
// through instrumentation based profiling.
def int_instrprof_value_profile : Intrinsic<[],
                                            [llvm_ptr_ty, llvm_i64_ty,
                                             llvm_i64_ty, llvm_i32_ty,
                                             llvm_i32_ty]>;

// A parameter configuration for instrumentation based MCDC profiling.
def int_instrprof_mcdc_parameters : Intrinsic<[],
                                              [llvm_ptr_ty, llvm_i64_ty,
                                               llvm_i32_ty]>;

// A test vector bitmap update for instrumentation based MCDC profiling.
def int_instrprof_mcdc_tvbitmap_update : Intrinsic<[],
                                        [llvm_ptr_ty, llvm_i64_ty,
                                         llvm_i32_ty, llvm_ptr_ty]>;

def int_call_preallocated_setup : DefaultAttrsIntrinsic<[llvm_token_ty], [llvm_i32_ty]>;
def int_call_preallocated_arg : DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_token_ty, llvm_i32_ty]>;
def int_call_preallocated_teardown : DefaultAttrsIntrinsic<[], [llvm_token_ty]>;

// This intrinsic is intentionally undocumented and users shouldn't call it;
// it's produced then quickly consumed during codegen.
def int_callbr_landingpad : Intrinsic<[llvm_any_ty], [LLVMMatchType<0>],
                                      [IntrNoMerge]>;

//===------------------- Standard C Library Intrinsics --------------------===//
//

def int_memcpy  : Intrinsic<[],
                            [llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty,
                             llvm_i1_ty],
                            [IntrArgMemOnly, IntrWillReturn, IntrNoFree,
                             IntrNoCallback,
                             NoCapture<ArgIndex<0>>, NoCapture<ArgIndex<1>>,
                             NoAlias<ArgIndex<0>>, NoAlias<ArgIndex<1>>,
                             WriteOnly<ArgIndex<0>>, ReadOnly<ArgIndex<1>>,
                             ImmArg<ArgIndex<3>>]>;

// Memcpy semantic that is guaranteed to be inlined.
// In particular this means that the generated code is not allowed to call any
// external function.
def int_memcpy_inline
    : Intrinsic<[],
      [llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty, llvm_i1_ty],
      [IntrArgMemOnly, IntrWillReturn, IntrNoFree, IntrNoCallback,
       NoCapture<ArgIndex<0>>, NoCapture<ArgIndex<1>>,
       NoAlias<ArgIndex<0>>, NoAlias<ArgIndex<1>>,
       WriteOnly<ArgIndex<0>>, ReadOnly<ArgIndex<1>>,
       ImmArg<ArgIndex<3>>]>;

def int_memmove : Intrinsic<[],
                            [llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty,
                             llvm_i1_ty],
                            [IntrArgMemOnly, IntrWillReturn, IntrNoFree,
                             IntrNoCallback,
                             NoCapture<ArgIndex<0>>, NoCapture<ArgIndex<1>>,
                             WriteOnly<ArgIndex<0>>, ReadOnly<ArgIndex<1>>,
                             ImmArg<ArgIndex<3>>]>;
def int_memset  : Intrinsic<[],
                            [llvm_anyptr_ty, llvm_i8_ty, llvm_anyint_ty,
                             llvm_i1_ty],
                            [IntrWriteMem, IntrArgMemOnly, IntrWillReturn,
                             IntrNoFree, IntrNoCallback,
                             NoCapture<ArgIndex<0>>, WriteOnly<ArgIndex<0>>,
                             ImmArg<ArgIndex<3>>]>;

// Memset version that is guaranteed to be inlined.
// In particular this means that the generated code is not allowed to call any
// external function.
// The third argument (specifying the size) must be a constant.
def int_memset_inline
    : Intrinsic<[],
      [llvm_anyptr_ty, llvm_i8_ty, llvm_anyint_ty, llvm_i1_ty],
      [IntrWriteMem, IntrArgMemOnly, IntrWillReturn, IntrNoFree, IntrNoCallback,
       NoCapture<ArgIndex<0>>, WriteOnly<ArgIndex<0>>,
       ImmArg<ArgIndex<3>>]>;

// FIXME: Add version of these floating point intrinsics which allow non-default
// rounding modes and FP exception handling.

let IntrProperties = [IntrNoMem, IntrSpeculatable, IntrWillReturn] in {
  def int_fma  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
                           [LLVMMatchType<0>, LLVMMatchType<0>,
                            LLVMMatchType<0>]>;
  def int_fmuladd : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
                              [LLVMMatchType<0>, LLVMMatchType<0>,
                               LLVMMatchType<0>]>;

  // These functions do not read memory, but are sensitive to the
  // rounding mode. LLVM purposely does not model changes to the FP
  // environment so they can be treated as readnone.
  def int_sqrt : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_powi : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>, llvm_anyint_ty]>;
  def int_asin : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_acos : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_atan : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_sin  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_cos  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_tan  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_sinh : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_cosh : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_tanh : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_pow  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
                           [LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_log  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_log10: DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_log2 : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_exp  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_exp2 : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_exp10 : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_fabs : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_copysign : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
                               [LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_floor : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_ceil  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_trunc : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_rint  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_nearbyint : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_round : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
  def int_roundeven    : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;

  // Truncate a floating point number with a specific rounding mode
  def int_fptrunc_round : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                [ llvm_anyfloat_ty, llvm_metadata_ty ]>;

  def int_canonicalize : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>],
                                   [IntrNoMem]>;
  // Arithmetic fence intrinsic.
  def int_arithmetic_fence : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>],
                                                   [IntrNoMem]>;

  def int_lround : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty]>;
  def int_llround : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty]>;
  def int_lrint : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty]>;
  def int_llrint : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty]>;

  // TODO: int operand should be constrained to same number of elements as the result.
  def int_ldexp : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>,
                                                             llvm_anyint_ty]>;

  // TODO: Should constrain all element counts to match
  def int_frexp : DefaultAttrsIntrinsic<[llvm_anyfloat_ty, llvm_anyint_ty], [LLVMMatchType<0>]>;
}

def int_minnum : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable, IntrWillReturn, Commutative]
>;
def int_maxnum : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable, IntrWillReturn, Commutative]
>;
def int_minimum : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable, IntrWillReturn, Commutative]
>;
def int_maximum : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable, IntrWillReturn, Commutative]
>;

// Internal interface for object size checking
def int_objectsize : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                               [llvm_anyptr_ty, llvm_i1_ty,
                                llvm_i1_ty, llvm_i1_ty],
                               [IntrNoMem, IntrSpeculatable, IntrWillReturn,
                                ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>,
                                ImmArg<ArgIndex<3>>]>,
                               ClangBuiltin<"__builtin_object_size">;

//===--------------- Access to Floating Point Environment -----------------===//
//

let IntrProperties = [IntrInaccessibleMemOnly, IntrWillReturn] in {
  def int_get_rounding  : DefaultAttrsIntrinsic<[llvm_i32_ty], []>;
  def int_set_rounding  : DefaultAttrsIntrinsic<[], [llvm_i32_ty]>;
  def int_get_fpenv     : DefaultAttrsIntrinsic<[llvm_anyint_ty], []>;
  def int_set_fpenv     : DefaultAttrsIntrinsic<[], [llvm_anyint_ty]>;
  def int_reset_fpenv   : DefaultAttrsIntrinsic<[], []>;
  def int_get_fpmode    : DefaultAttrsIntrinsic<[llvm_anyint_ty], []>;
  def int_set_fpmode    : DefaultAttrsIntrinsic<[], [llvm_anyint_ty]>;
  def int_reset_fpmode  : DefaultAttrsIntrinsic<[], []>;
}

//===--------------- Floating Point Properties ----------------------------===//
//

def int_is_fpclass
    : DefaultAttrsIntrinsic<[LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                            [llvm_anyfloat_ty, llvm_i32_ty],
                            [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<1>>]>;

//===--------------- Constrained Floating Point Intrinsics ----------------===//
//

/// IntrStrictFP - The intrinsic is allowed to be used in an alternate
/// floating point environment.
def IntrStrictFP : IntrinsicProperty;

let IntrProperties = [IntrInaccessibleMemOnly, IntrWillReturn, IntrStrictFP] in {
  def int_experimental_constrained_fadd : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_fsub : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_fmul : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_fdiv : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_frem : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;

  def int_experimental_constrained_fma : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;

  def int_experimental_constrained_fmuladd : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                       [ LLVMMatchType<0>,
                                                         LLVMMatchType<0>,
                                                         LLVMMatchType<0>,
                                                         llvm_metadata_ty,
                                                         llvm_metadata_ty ]>;

  def int_experimental_constrained_fptosi : DefaultAttrsIntrinsic<[ llvm_anyint_ty ],
                                                    [ llvm_anyfloat_ty,
                                                      llvm_metadata_ty ]>;

  def int_experimental_constrained_fptoui : DefaultAttrsIntrinsic<[ llvm_anyint_ty ],
                                                    [ llvm_anyfloat_ty,
                                                      llvm_metadata_ty ]>;

  def int_experimental_constrained_sitofp : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                       [ llvm_anyint_ty,
                                                         llvm_metadata_ty,
                                                         llvm_metadata_ty ]>;

  def int_experimental_constrained_uitofp : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                       [ llvm_anyint_ty,
                                                         llvm_metadata_ty,
                                                         llvm_metadata_ty ]>;

  def int_experimental_constrained_fptrunc : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                       [ llvm_anyfloat_ty,
                                                         llvm_metadata_ty,
                                                         llvm_metadata_ty ]>;

  def int_experimental_constrained_fpext : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                     [ llvm_anyfloat_ty,
                                                       llvm_metadata_ty ]>;

  // These intrinsics are sensitive to the rounding mode so we need constrained
  // versions of each of them.  When strict rounding and exception control are
  // not required the non-constrained versions of these intrinsics should be
  // used.
  def int_experimental_constrained_sqrt : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_powi : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_i32_ty,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_ldexp : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_anyint_ty,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_asin  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_acos  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_atan  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_sin  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_cos  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_tan  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_sinh  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_cosh  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_tanh  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_pow  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_log  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_log10: DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_log2 : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_exp  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_exp2 : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_rint  : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                     [ LLVMMatchType<0>,
                                                       llvm_metadata_ty,
                                                       llvm_metadata_ty ]>;
  def int_experimental_constrained_nearbyint : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                         [ LLVMMatchType<0>,
                                                           llvm_metadata_ty,
                                                           llvm_metadata_ty ]>;
  def int_experimental_constrained_lrint : DefaultAttrsIntrinsic<[ llvm_anyint_ty ],
                                                     [ llvm_anyfloat_ty,
                                                       llvm_metadata_ty,
                                                       llvm_metadata_ty ]>;
  def int_experimental_constrained_llrint : DefaultAttrsIntrinsic<[ llvm_anyint_ty ],
                                                      [ llvm_anyfloat_ty,
                                                        llvm_metadata_ty,
                                                        llvm_metadata_ty ]>;
  def int_experimental_constrained_maxnum : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                      [ LLVMMatchType<0>,
                                                        LLVMMatchType<0>,
                                                        llvm_metadata_ty ]>;
  def int_experimental_constrained_minnum : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                      [ LLVMMatchType<0>,
                                                        LLVMMatchType<0>,
                                                        llvm_metadata_ty ]>;
  def int_experimental_constrained_maximum : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                       [ LLVMMatchType<0>,
                                                         LLVMMatchType<0>,
                                                         llvm_metadata_ty ]>;
  def int_experimental_constrained_minimum : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                       [ LLVMMatchType<0>,
                                                         LLVMMatchType<0>,
                                                         llvm_metadata_ty ]>;
  def int_experimental_constrained_ceil : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                    [ LLVMMatchType<0>,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_floor : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                     [ LLVMMatchType<0>,
                                                       llvm_metadata_ty ]>;
  def int_experimental_constrained_lround : DefaultAttrsIntrinsic<[ llvm_anyint_ty ],
                                                      [ llvm_anyfloat_ty,
                                                        llvm_metadata_ty ]>;
  def int_experimental_constrained_llround : DefaultAttrsIntrinsic<[ llvm_anyint_ty ],
                                                       [ llvm_anyfloat_ty,
                                                         llvm_metadata_ty ]>;
  def int_experimental_constrained_round : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                     [ LLVMMatchType<0>,
                                                      llvm_metadata_ty ]>;
  def int_experimental_constrained_roundeven : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                         [ LLVMMatchType<0>,
                                                           llvm_metadata_ty ]>;
  def int_experimental_constrained_trunc : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ],
                                                     [ LLVMMatchType<0>,
                                                       llvm_metadata_ty ]>;

  // Constrained floating-point comparison (quiet and signaling variants).
  // Third operand is the predicate represented as a metadata string.
  def int_experimental_constrained_fcmp
      : DefaultAttrsIntrinsic<[ LLVMScalarOrSameVectorWidth<0, llvm_i1_ty> ],
                  [ llvm_anyfloat_ty, LLVMMatchType<0>,
                    llvm_metadata_ty, llvm_metadata_ty ]>;
  def int_experimental_constrained_fcmps
      : DefaultAttrsIntrinsic<[ LLVMScalarOrSameVectorWidth<0, llvm_i1_ty> ],
                  [ llvm_anyfloat_ty, LLVMMatchType<0>,
                    llvm_metadata_ty, llvm_metadata_ty ]>;
}
// FIXME: Consider maybe adding intrinsics for sitofp, uitofp.


//===------------------------- Expect Intrinsics --------------------------===//
//
def int_expect : DefaultAttrsIntrinsic<[llvm_anyint_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem, IntrWillReturn]>;

def int_expect_with_probability : DefaultAttrsIntrinsic<[llvm_anyint_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>, llvm_double_ty],
  [IntrNoMem, IntrWillReturn, ImmArg<ArgIndex<2>>]>;

//===-------------------- Bit Manipulation Intrinsics ---------------------===//
//

// None of these intrinsics accesses memory at all.
let IntrProperties = [IntrNoMem, IntrSpeculatable, IntrWillReturn] in {
  def int_bswap: DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>]>;
  def int_ctpop: DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>]>;
  def int_bitreverse : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>]>;
  def int_fshl : DefaultAttrsIntrinsic<[llvm_anyint_ty],
      [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_fshr : DefaultAttrsIntrinsic<[llvm_anyint_ty],
      [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>]>;
}

let IntrProperties = [IntrNoMem, IntrSpeculatable, IntrWillReturn,
                      ImmArg<ArgIndex<1>>] in {
  def int_ctlz : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, llvm_i1_ty]>;
  def int_cttz : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, llvm_i1_ty]>;
}

//===------------------------ Debugger Intrinsics -------------------------===//
//

// None of these intrinsics accesses memory at all...but that doesn't
// mean the optimizers can change them aggressively.  Special handling
// needed in a few places. These synthetic intrinsics have no
// side-effects and just mark information about their operands.
let IntrProperties = [IntrNoMem, IntrSpeculatable, IntrWillReturn] in {
  def int_dbg_declare      : DefaultAttrsIntrinsic<[],
                                       [llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty]>;
  def int_dbg_value        : DefaultAttrsIntrinsic<[],
                                       [llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty]>;
  def int_dbg_assign        : DefaultAttrsIntrinsic<[],
                                       [llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty,
                                        llvm_metadata_ty]>;
  def int_dbg_label        : DefaultAttrsIntrinsic<[],
                                       [llvm_metadata_ty]>;
}

//===------------------ Exception Handling Intrinsics----------------------===//
//

// The result of eh.typeid.for depends on the enclosing function, but inside a
// given function it is 'const' and may be CSE'd etc.
def int_eh_typeid_for : Intrinsic<[llvm_i32_ty], [llvm_anyptr_ty], [IntrNoMem]>;

def int_eh_return_i32 : Intrinsic<[], [llvm_i32_ty, llvm_ptr_ty]>;
def int_eh_return_i64 : Intrinsic<[], [llvm_i64_ty, llvm_ptr_ty]>;

// eh.exceptionpointer returns the pointer to the exception caught by
// the given `catchpad`.
def int_eh_exceptionpointer : Intrinsic<[llvm_anyptr_ty], [llvm_token_ty],
                                        [IntrNoMem]>;

// Gets the exception code from a catchpad token. Only used on some platforms.
def int_eh_exceptioncode : Intrinsic<[llvm_i32_ty], [llvm_token_ty], [IntrNoMem]>;

// __builtin_unwind_init is an undocumented GCC intrinsic that causes all
// callee-saved registers to be saved and restored (regardless of whether they
// are used) in the calling function. It is used by libgcc_eh.
def int_eh_unwind_init: Intrinsic<[]>,
                        ClangBuiltin<"__builtin_unwind_init">;

def int_eh_dwarf_cfa  : Intrinsic<[llvm_ptr_ty], [llvm_i32_ty]>;

def int_eh_sjlj_lsda             : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;
def int_eh_sjlj_callsite         : Intrinsic<[], [llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<0>>]>;

def int_eh_sjlj_functioncontext : Intrinsic<[], [llvm_ptr_ty]>;
def int_eh_sjlj_setjmp          : Intrinsic<[llvm_i32_ty], [llvm_ptr_ty]>;
def int_eh_sjlj_longjmp         : Intrinsic<[], [llvm_ptr_ty], [IntrNoReturn]>;
def int_eh_sjlj_setup_dispatch  : Intrinsic<[], []>;

//===---------------- Generic Variable Attribute Intrinsics----------------===//
//
def int_var_annotation : DefaultAttrsIntrinsic<
    [], [llvm_anyptr_ty, llvm_anyptr_ty, LLVMMatchType<1>, llvm_i32_ty, LLVMMatchType<1>],
    [IntrInaccessibleMemOnly], "llvm.var.annotation">;

def int_ptr_annotation : DefaultAttrsIntrinsic<
    [llvm_anyptr_ty],
    [LLVMMatchType<0>, llvm_anyptr_ty, LLVMMatchType<1>, llvm_i32_ty, LLVMMatchType<1>],
    [IntrInaccessibleMemOnly], "llvm.ptr.annotation">;

def int_annotation : DefaultAttrsIntrinsic<
    [llvm_anyint_ty],
    [LLVMMatchType<0>, llvm_anyptr_ty, LLVMMatchType<1>, llvm_i32_ty],
    [IntrInaccessibleMemOnly], "llvm.annotation">;

// Annotates the current program point with metadata strings which are emitted
// as CodeView debug info records. This is expensive, as it disables inlining
// and is modelled as having side effects.
def int_codeview_annotation : DefaultAttrsIntrinsic<[], [llvm_metadata_ty],
                                        [IntrInaccessibleMemOnly, IntrNoDuplicate, IntrWillReturn],
                                        "llvm.codeview.annotation">;

//===------------------------ Trampoline Intrinsics -----------------------===//
//
def int_init_trampoline : DefaultAttrsIntrinsic<
    [], [llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
    [IntrArgMemOnly, NoCapture<ArgIndex<0>>, WriteOnly<ArgIndex<0>>,
     ReadNone<ArgIndex<1>>, ReadNone<ArgIndex<2>>]>,
    ClangBuiltin<"__builtin_init_trampoline">;

def int_adjust_trampoline : DefaultAttrsIntrinsic<
    [llvm_ptr_ty], [llvm_ptr_ty], [IntrReadMem, IntrArgMemOnly]>,
    ClangBuiltin<"__builtin_adjust_trampoline">;

//===------------------------ Overflow Intrinsics -------------------------===//
//

// Expose the carry flag from add operations on two integrals.
let IntrProperties = [IntrNoMem, IntrSpeculatable, IntrWillReturn] in {
  def int_sadd_with_overflow : DefaultAttrsIntrinsic<[llvm_anyint_ty,
                                          LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                                         [LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_uadd_with_overflow : DefaultAttrsIntrinsic<[llvm_anyint_ty,
                                          LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                                         [LLVMMatchType<0>, LLVMMatchType<0>]>;

  def int_ssub_with_overflow : DefaultAttrsIntrinsic<[llvm_anyint_ty,
                                          LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                                         [LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_usub_with_overflow : DefaultAttrsIntrinsic<[llvm_anyint_ty,
                                          LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                                         [LLVMMatchType<0>, LLVMMatchType<0>]>;

  def int_smul_with_overflow : DefaultAttrsIntrinsic<[llvm_anyint_ty,
                                          LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                                         [LLVMMatchType<0>, LLVMMatchType<0>]>;
  def int_umul_with_overflow : DefaultAttrsIntrinsic<[llvm_anyint_ty,
                                          LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                                         [LLVMMatchType<0>, LLVMMatchType<0>]>;
}
//===------------------------- Saturation Arithmetic Intrinsics ---------------------===//
//
def int_sadd_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable, IntrWillReturn, Commutative]>;
def int_uadd_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable, IntrWillReturn, Commutative]>;
def int_ssub_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_usub_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_sshl_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_ushl_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>],
                             [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===------------------------- Fixed Point Arithmetic Intrinsics ---------------------===//
//
def int_smul_fix : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                             [IntrNoMem, IntrSpeculatable, IntrWillReturn,
                              Commutative, ImmArg<ArgIndex<2>>]>;

def int_umul_fix : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                             [IntrNoMem, IntrSpeculatable, IntrWillReturn,
                              Commutative, ImmArg<ArgIndex<2>>]>;

def int_sdiv_fix : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                             [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_udiv_fix : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                             [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                             [IntrNoMem, ImmArg<ArgIndex<2>>]>;

//===------------------- Fixed Point Saturation Arithmetic Intrinsics ----------------===//
//
def int_smul_fix_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                                 [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                                 [IntrNoMem, IntrSpeculatable, IntrWillReturn,
                                  Commutative, ImmArg<ArgIndex<2>>]>;
def int_umul_fix_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                                 [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                                 [IntrNoMem, IntrSpeculatable, IntrWillReturn,
                                  Commutative, ImmArg<ArgIndex<2>>]>;

def int_sdiv_fix_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                                 [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                                 [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_udiv_fix_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                                 [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
                                 [IntrNoMem, ImmArg<ArgIndex<2>>]>;

//===------------------ Integer Min/Max/Abs Intrinsics --------------------===//
//
def int_abs : DefaultAttrsIntrinsic<
    [llvm_anyint_ty], [LLVMMatchType<0>, llvm_i1_ty],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn, ImmArg<ArgIndex<1>>]>;

def int_smax : DefaultAttrsIntrinsic<
    [llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_smin : DefaultAttrsIntrinsic<
    [llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_umax : DefaultAttrsIntrinsic<
    [llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_umin : DefaultAttrsIntrinsic<
    [llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_scmp : DefaultAttrsIntrinsic<
    [llvm_anyint_ty], [llvm_anyint_ty, LLVMMatchType<1>],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;
def int_ucmp : DefaultAttrsIntrinsic<
    [llvm_anyint_ty], [llvm_anyint_ty, LLVMMatchType<1>],
    [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

//===------------------------- Memory Use Markers -------------------------===//
//
def int_lifetime_start  : DefaultAttrsIntrinsic<[],
                                    [llvm_i64_ty, llvm_anyptr_ty],
                                    [IntrArgMemOnly, IntrWillReturn,
                                     NoCapture<ArgIndex<1>>,
                                     ImmArg<ArgIndex<0>>]>;
def int_lifetime_end    : DefaultAttrsIntrinsic<[],
                                    [llvm_i64_ty, llvm_anyptr_ty],
                                    [IntrArgMemOnly, IntrWillReturn,
                                     NoCapture<ArgIndex<1>>,
                                     ImmArg<ArgIndex<0>>]>;
def int_invariant_start : DefaultAttrsIntrinsic<[llvm_ptr_ty],
                                    [llvm_i64_ty, llvm_anyptr_ty],
                                    [IntrArgMemOnly, IntrWillReturn,
                                     NoCapture<ArgIndex<1>>,
                                     ImmArg<ArgIndex<0>>]>;
def int_invariant_end   : DefaultAttrsIntrinsic<[],
                                    [llvm_ptr_ty, llvm_i64_ty,
                                     llvm_anyptr_ty],
                                    [IntrArgMemOnly, IntrWillReturn,
                                     NoCapture<ArgIndex<2>>,
                                     ImmArg<ArgIndex<1>>]>;

// launder.invariant.group can't be marked with 'readnone' (IntrNoMem),
// because it would cause CSE of two barriers with the same argument.
// Inaccessiblememonly says that the barrier doesn't read the argument,
// but it changes state not accessible to this module. This way
// we can DSE through the barrier because it doesn't read the value
// after store. Although the barrier doesn't modify any memory it
// can't be marked as readonly, because it would be possible to
// CSE 2 barriers with store in between.
// The argument also can't be marked with 'returned' attribute, because
// it would remove barrier.
// Note that it is still experimental, which means that its semantics
// might change in the future.
def int_launder_invariant_group : DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                                            [LLVMMatchType<0>],
                                            [IntrInaccessibleMemOnly, IntrSpeculatable, IntrWillReturn]>;


def int_strip_invariant_group : DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                                          [LLVMMatchType<0>],
                                          [IntrSpeculatable, IntrNoMem, IntrWillReturn]>;

//===------------------------ Stackmap Intrinsics -------------------------===//
//
def int_experimental_stackmap : DefaultAttrsIntrinsic<[],
                                  [llvm_i64_ty, llvm_i32_ty, llvm_vararg_ty],
                                  [Throws]>;
def int_experimental_patchpoint_void : Intrinsic<[],
                                                 [llvm_i64_ty, llvm_i32_ty,
                                                  llvm_ptr_ty, llvm_i32_ty,
                                                  llvm_vararg_ty],
                                                  [Throws]>;
def int_experimental_patchpoint : Intrinsic<[llvm_any_ty],
                                            [llvm_i64_ty, llvm_i32_ty,
                                             llvm_ptr_ty, llvm_i32_ty,
                                             llvm_vararg_ty],
                                            [Throws]>;


//===------------------------ Garbage Collection Intrinsics ---------------===//
// These are documented in docs/Statepoint.rst

def int_experimental_gc_statepoint : Intrinsic<[llvm_token_ty],
                               [llvm_i64_ty, llvm_i32_ty,
                                llvm_anyptr_ty, llvm_i32_ty,
                                llvm_i32_ty, llvm_vararg_ty],
                               [Throws, ImmArg<ArgIndex<0>>,
                                ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<3>>,
                                ImmArg<ArgIndex<4>>]>;

def int_experimental_gc_result : DefaultAttrsIntrinsic<
    [llvm_any_ty], [llvm_token_ty], [IntrNoMem]>;

def int_experimental_gc_relocate : DefaultAttrsIntrinsic<
    [llvm_any_ty], [llvm_token_ty, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

def int_experimental_gc_get_pointer_base : DefaultAttrsIntrinsic<
    [llvm_anyptr_ty], [llvm_anyptr_ty],
    [IntrNoMem, IntrWillReturn, ReadNone<ArgIndex<0>>, NoCapture<ArgIndex<0>>]>;

def int_experimental_gc_get_pointer_offset : DefaultAttrsIntrinsic<
    [llvm_i64_ty], [llvm_anyptr_ty],
    [IntrNoMem, IntrWillReturn, ReadNone<ArgIndex<0>>, NoCapture<ArgIndex<0>>]>;

//===------------------------ Coroutine Intrinsics ---------------===//
// These are documented in docs/Coroutines.rst

// Coroutine Structure Intrinsics.

def int_coro_id : DefaultAttrsIntrinsic<[llvm_token_ty],
    [llvm_i32_ty, llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
    [IntrArgMemOnly, IntrReadMem, ReadNone<ArgIndex<1>>, ReadOnly<ArgIndex<2>>,
     NoCapture<ArgIndex<2>>]>;
def int_coro_id_retcon : Intrinsic<[llvm_token_ty],
    [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty,
     llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
    []>;
def int_coro_id_retcon_once : Intrinsic<[llvm_token_ty],
    [llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty,
     llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
    []>;
def int_coro_alloc : Intrinsic<[llvm_i1_ty], [llvm_token_ty], []>;
def int_coro_id_async : Intrinsic<[llvm_token_ty],
  [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_ptr_ty],
  []>;
def int_coro_async_context_alloc : Intrinsic<[llvm_ptr_ty],
    [llvm_ptr_ty, llvm_ptr_ty],
    []>;
def int_coro_async_context_dealloc : Intrinsic<[],
    [llvm_ptr_ty],
    []>;
def int_coro_async_resume : Intrinsic<[llvm_ptr_ty],
    [],
    [IntrNoMerge]>;
def int_coro_async_size_replace : Intrinsic<[], [llvm_ptr_ty, llvm_ptr_ty], []>;
def int_coro_suspend_async
    : Intrinsic<[llvm_any_ty],
                [llvm_i32_ty, llvm_ptr_ty, llvm_ptr_ty, llvm_vararg_ty],
                [IntrNoMerge]>;
def int_coro_prepare_async : Intrinsic<[llvm_ptr_ty], [llvm_ptr_ty],
                                       [IntrNoMem]>;
def int_coro_begin : Intrinsic<[llvm_ptr_ty], [llvm_token_ty, llvm_ptr_ty],
                               [WriteOnly<ArgIndex<1>>]>;

def int_coro_free : Intrinsic<[llvm_ptr_ty], [llvm_token_ty, llvm_ptr_ty],
                              [IntrReadMem, IntrArgMemOnly,
                               ReadOnly<ArgIndex<1>>,
                               NoCapture<ArgIndex<1>>]>;
def int_coro_end : Intrinsic<[llvm_i1_ty], [llvm_ptr_ty, llvm_i1_ty, llvm_token_ty], []>;
def int_coro_end_results : Intrinsic<[llvm_token_ty], [llvm_vararg_ty]>;
def int_coro_end_async
    : Intrinsic<[llvm_i1_ty], [llvm_ptr_ty, llvm_i1_ty, llvm_vararg_ty], []>;

def int_coro_frame : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;
def int_coro_noop : Intrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;
def int_coro_size : Intrinsic<[llvm_anyint_ty], [], [IntrNoMem]>;
def int_coro_align : Intrinsic<[llvm_anyint_ty], [], [IntrNoMem]>;

def int_coro_save : Intrinsic<[llvm_token_ty], [llvm_ptr_ty], [IntrNoMerge]>;
def int_coro_suspend : Intrinsic<[llvm_i8_ty], [llvm_token_ty, llvm_i1_ty], []>;
def int_coro_suspend_retcon : Intrinsic<[llvm_any_ty], [llvm_vararg_ty], []>;
def int_coro_prepare_retcon : Intrinsic<[llvm_ptr_ty], [llvm_ptr_ty],
                                        [IntrNoMem]>;
def int_coro_alloca_alloc : Intrinsic<[llvm_token_ty],
                                      [llvm_anyint_ty, llvm_i32_ty], []>;
def int_coro_alloca_get : Intrinsic<[llvm_ptr_ty], [llvm_token_ty], []>;
def int_coro_alloca_free : Intrinsic<[], [llvm_token_ty], []>;

// Coroutine Manipulation Intrinsics.

def int_coro_resume : Intrinsic<[], [llvm_ptr_ty], [Throws]>;
def int_coro_destroy : Intrinsic<[], [llvm_ptr_ty], [Throws]>;
def int_coro_done : Intrinsic<[llvm_i1_ty], [llvm_ptr_ty],
                              [IntrArgMemOnly, ReadOnly<ArgIndex<0>>,
                               NoCapture<ArgIndex<0>>]>;
def int_coro_promise : Intrinsic<[llvm_ptr_ty],
                                 [llvm_ptr_ty, llvm_i32_ty, llvm_i1_ty],
                                 [IntrNoMem, NoCapture<ArgIndex<0>>]>;

def int_coro_await_suspend_void : Intrinsic<[],
                                       [llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
                                       [Throws]>;

def int_coro_await_suspend_bool : Intrinsic<[llvm_i1_ty],
                                            [llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
                                            [Throws]>;

def int_coro_await_suspend_handle : Intrinsic<[],
                                              [llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty],
                                              [Throws]>;

// Coroutine Lowering Intrinsics. Used internally by coroutine passes.

def int_coro_subfn_addr : DefaultAttrsIntrinsic<
    [llvm_ptr_ty], [llvm_ptr_ty, llvm_i8_ty],
    [IntrReadMem, IntrArgMemOnly, ReadOnly<ArgIndex<0>>,
     NoCapture<ArgIndex<0>>]>;

///===-------------------------- Other Intrinsics --------------------------===//
//
// TODO: We should introduce a new memory kind fo traps (and other side effects
//       we only model to keep things alive).
def int_trap : Intrinsic<[], [], [IntrNoReturn, IntrCold, IntrInaccessibleMemOnly,
               IntrWriteMem]>, ClangBuiltin<"__builtin_trap">;
def int_debugtrap : Intrinsic<[]>,
                    ClangBuiltin<"__builtin_debugtrap">;
def int_ubsantrap : Intrinsic<[], [llvm_i8_ty],
                              [IntrNoReturn, IntrCold, ImmArg<ArgIndex<0>>]>;

// Return true if ubsan check is allowed.
def int_allow_ubsan_check : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_i8_ty],
    [IntrInaccessibleMemOnly, IntrWriteMem, ImmArg<ArgIndex<0>>, NoUndef<RetIndex>]>;

// Return true if runtime check is allowed.
def int_allow_runtime_check : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_metadata_ty],
    [IntrInaccessibleMemOnly, IntrWriteMem, NoUndef<RetIndex>]>,
    ClangBuiltin<"__builtin_allow_runtime_check">;

// Support for dynamic deoptimization (or de-specialization)
def int_experimental_deoptimize : Intrinsic<[llvm_any_ty], [llvm_vararg_ty],
                                            [Throws]>;

// Support for speculative runtime guards
def int_experimental_guard : Intrinsic<[], [llvm_i1_ty, llvm_vararg_ty],
                                       [Throws]>;

// Supports widenable conditions for guards represented as explicit branches.
def int_experimental_widenable_condition : DefaultAttrsIntrinsic<[llvm_i1_ty], [],
        [IntrInaccessibleMemOnly, IntrWillReturn, IntrSpeculatable, NoUndef<RetIndex>]>;

// NOP: calls/invokes to this intrinsic are removed by codegen
def int_donothing : DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrWillReturn]>;

// This instruction has no actual effect, though it is treated by the optimizer
// has having opaque side effects. This may be inserted into loops to ensure
// that they are not removed even if they turn out to be empty, for languages
// which specify that infinite loops must be preserved.
def int_sideeffect : DefaultAttrsIntrinsic<[], [], [IntrInaccessibleMemOnly, IntrWillReturn]>;

// The pseudoprobe intrinsic works as a place holder to the block it probes.
// Like the sideeffect intrinsic defined above, this intrinsic is treated by the
// optimizer as having opaque side effects so that it won't be get rid of or moved
// out of the block it probes.
def int_pseudoprobe : DefaultAttrsIntrinsic<[], [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i64_ty],
                                    [IntrInaccessibleMemOnly, IntrWillReturn]>;

// Intrinsics to support half precision floating point format
let IntrProperties = [IntrNoMem, IntrWillReturn] in {
def int_convert_to_fp16   : DefaultAttrsIntrinsic<[llvm_i16_ty], [llvm_anyfloat_ty]>;
def int_convert_from_fp16 : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [llvm_i16_ty]>;
}

// Saturating floating point to integer intrinsics
let IntrProperties = [IntrNoMem, IntrSpeculatable, IntrWillReturn] in {
def int_fptoui_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty]>;
def int_fptosi_sat : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty]>;
}

// Clear cache intrinsic, default to ignore (ie. emit nothing)
// maps to void __clear_cache() on supporting platforms
def int_clear_cache : Intrinsic<[], [llvm_ptr_ty, llvm_ptr_ty],
                                [], "llvm.clear_cache">;

// Intrinsic to detect whether its argument is a constant.
def int_is_constant : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_any_ty],
                                [IntrNoMem, IntrWillReturn, IntrConvergent],
                                "llvm.is.constant">;

// Intrinsic to mask out bits of a pointer.
// First argument must be pointer or vector of pointer. This is checked by the
// verifier.
def int_ptrmask: DefaultAttrsIntrinsic<[llvm_any_ty], [LLVMMatchType<0>, llvm_anyint_ty],
                           [IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

// Intrinsic to wrap a thread local variable.
def int_threadlocal_address : DefaultAttrsIntrinsic<[llvm_anyptr_ty], [LLVMMatchType<0>],
                                                    [NonNull<RetIndex>, NonNull<ArgIndex<0>>,
                                                     IntrNoMem, IntrSpeculatable, IntrWillReturn]>;

def int_experimental_stepvector : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                                                        [], [IntrNoMem]>;

//===---------------- Vector Predication Intrinsics --------------===//
// Memory Intrinsics
def int_vp_store : DefaultAttrsIntrinsic<[],
                             [ llvm_anyvector_ty,
                               llvm_anyptr_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty],
                             [ NoCapture<ArgIndex<1>>, IntrNoSync, IntrWriteMem, IntrArgMemOnly, IntrWillReturn ]>;

def int_vp_load  : DefaultAttrsIntrinsic<[ llvm_anyvector_ty],
                             [ llvm_anyptr_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty],
                             [ NoCapture<ArgIndex<0>>, IntrNoSync, IntrReadMem, IntrWillReturn, IntrArgMemOnly ]>;

def int_vp_gather: DefaultAttrsIntrinsic<[ llvm_anyvector_ty],
                             [ LLVMVectorOfAnyPointersToElt<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty],
                             [ IntrReadMem, IntrNoSync, IntrWillReturn]>;

def int_vp_scatter: DefaultAttrsIntrinsic<[],
                             [ llvm_anyvector_ty,
                               LLVMVectorOfAnyPointersToElt<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty],
                             [ IntrNoSync, IntrWillReturn ]>; // TODO allow IntrNoCapture for vectors of pointers

// Experimental strided memory accesses
def int_experimental_vp_strided_store : DefaultAttrsIntrinsic<[],
                             [ llvm_anyvector_ty,
                               llvm_anyptr_ty,
                               llvm_anyint_ty, // Stride in bytes
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty],
                             [ NoCapture<ArgIndex<1>>, IntrNoSync, IntrWriteMem, IntrArgMemOnly, IntrWillReturn ]>;

def int_experimental_vp_strided_load  : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                             [ llvm_anyptr_ty,
                               llvm_anyint_ty, // Stride in bytes
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty],
                             [ NoCapture<ArgIndex<0>>, IntrNoSync, IntrReadMem, IntrWillReturn, IntrArgMemOnly ]>;

// Experimental histogram
def int_experimental_vector_histogram_add : DefaultAttrsIntrinsic<[],
                             [ llvm_anyvector_ty, // Vector of pointers
                               llvm_anyint_ty,    // Increment
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>], // Mask
                             [ IntrArgMemOnly ]>;

// Operators
let IntrProperties = [IntrNoMem, IntrNoSync, IntrWillReturn] in {
  // Integer arithmetic
  def int_vp_add : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_sub : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_mul  : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_ashr : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_lshr : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_shl : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_or : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_and : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_xor : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_sdiv : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_udiv : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_srem : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_urem : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_abs : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               llvm_i1_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_smin : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_smax : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_umin : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_umax : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_bswap : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_bitreverse : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_ctpop : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fshl : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fshr : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_sadd_sat : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_uadd_sat : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_ssub_sat : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_usub_sat : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;

  // Floating-point arithmetic
  def int_vp_fadd : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fsub : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fmul  : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fdiv : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_frem : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fneg : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fabs : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_sqrt : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fma : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fmuladd : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_minnum : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_maxnum : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_minimum : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_maximum : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_copysign : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_ceil : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_floor : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_round : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_roundeven : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_roundtozero : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_rint : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_nearbyint : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_lrint : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_llrint : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;

  // Casts
  def int_vp_trunc : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_zext : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_sext : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fptrunc : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fpext : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fptoui : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_fptosi : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_uitofp : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_sitofp : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_ptrtoint : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_inttoptr : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  // Shuffles
  def int_vp_select : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               llvm_i32_ty]>;
  def int_vp_merge : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               LLVMMatchType<0>,
                               LLVMMatchType<0>,
                               llvm_i32_ty]>;

  // Comparisons
  def int_vp_fcmp : DefaultAttrsIntrinsic<[ LLVMScalarOrSameVectorWidth<0, llvm_i1_ty> ],
                             [ llvm_anyvector_ty,
                               LLVMMatchType<0>,
                               llvm_metadata_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_icmp : DefaultAttrsIntrinsic<[ LLVMScalarOrSameVectorWidth<0, llvm_i1_ty> ],
                             [ llvm_anyvector_ty,
                               LLVMMatchType<0>,
                               llvm_metadata_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;

  // Reductions
  def int_vp_reduce_fadd : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_fmul : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_add  : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_mul : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_and : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_or : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_xor : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_smax : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_smin : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_umax : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_umin : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_fmax : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_fmin : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                             [ LLVMVectorElementType<0>,
                               llvm_anyvector_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_reduce_fmaximum : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                 [ LLVMVectorElementType<0>,
                                   llvm_anyvector_ty,
                                   LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                                   llvm_i32_ty]>;
  def int_vp_reduce_fminimum : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                 [ LLVMVectorElementType<0>,
                                   llvm_anyvector_ty,
                                   LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                                   llvm_i32_ty]>;
}

let IntrProperties = [IntrNoMem, IntrNoSync, IntrWillReturn, ImmArg<ArgIndex<1>>] in {
  def int_vp_ctlz : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               llvm_i1_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;
  def int_vp_cttz : DefaultAttrsIntrinsic<[ llvm_anyvector_ty ],
                             [ LLVMMatchType<0>,
                               llvm_i1_ty,
                               LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                               llvm_i32_ty]>;

  def int_vp_cttz_elts : DefaultAttrsIntrinsic<[ llvm_anyint_ty ],
                                  [ llvm_anyvector_ty,
                                    llvm_i1_ty,
                                    LLVMScalarOrSameVectorWidth<1, llvm_i1_ty>,
                                    llvm_i32_ty]>;
}

def int_get_active_lane_mask:
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
            [llvm_anyint_ty, LLVMMatchType<1>],
            [IntrNoMem, IntrNoSync, IntrWillReturn]>;

def int_experimental_get_vector_length:
  DefaultAttrsIntrinsic<[llvm_i32_ty],
                        [llvm_anyint_ty, llvm_i32_ty, llvm_i1_ty],
                        [IntrNoMem, IntrNoSync, IntrWillReturn,
                         ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

def int_experimental_cttz_elts:
  DefaultAttrsIntrinsic<[llvm_anyint_ty],
            [llvm_anyvector_ty, llvm_i1_ty],
            [IntrNoMem, IntrNoSync, IntrWillReturn, ImmArg<ArgIndex<1>>]>;

def int_experimental_vp_splice:
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
            [LLVMMatchType<0>,
             LLVMMatchType<0>,
             llvm_i32_ty,
             LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
             llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_experimental_vp_reverse:
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>,
                         LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                         llvm_i32_ty],
                        [IntrNoMem]>;

def int_experimental_vp_splat:
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMVectorElementType<0>,
                         LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                         llvm_i32_ty],
                        [IntrNoMem]>;

def int_vp_is_fpclass:
      DefaultAttrsIntrinsic<[ LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                              [ llvm_anyvector_ty,
                                llvm_i32_ty,
                                LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                                llvm_i32_ty],
                              [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<1>>]>;

//===-------------------------- Masked Intrinsics -------------------------===//
//
def int_masked_load:
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
            [llvm_anyptr_ty, llvm_i32_ty,
             LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, LLVMMatchType<0>],
            [IntrReadMem, IntrArgMemOnly, IntrWillReturn, ImmArg<ArgIndex<1>>,
             NoCapture<ArgIndex<0>>]>;

def int_masked_store:
  DefaultAttrsIntrinsic<[],
            [llvm_anyvector_ty, llvm_anyptr_ty,
             llvm_i32_ty, LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
            [IntrWriteMem, IntrArgMemOnly, IntrWillReturn,
             ImmArg<ArgIndex<2>>, NoCapture<ArgIndex<1>>]>;

def int_masked_gather:
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
            [LLVMVectorOfAnyPointersToElt<0>, llvm_i32_ty,
             LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, LLVMMatchType<0>],
            [IntrReadMem, IntrWillReturn, ImmArg<ArgIndex<1>>]>;

def int_masked_scatter:
  DefaultAttrsIntrinsic<[],
            [llvm_anyvector_ty, LLVMVectorOfAnyPointersToElt<0>, llvm_i32_ty,
             LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
            [IntrWriteMem, IntrWillReturn, ImmArg<ArgIndex<2>>]>;

def int_masked_expandload:
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
            [llvm_ptr_ty, LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
             LLVMMatchType<0>],
            [IntrReadMem, IntrWillReturn, NoCapture<ArgIndex<0>>]>;

def int_masked_compressstore:
  DefaultAttrsIntrinsic<[],
            [llvm_anyvector_ty, llvm_ptr_ty,
             LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
            [IntrWriteMem, IntrArgMemOnly, IntrWillReturn,
             NoCapture<ArgIndex<1>>]>;

def int_experimental_vector_compress:
    DefaultAttrsIntrinsic<[llvm_anyvector_ty],
              [LLVMMatchType<0>, LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, LLVMMatchType<0>],
              [IntrNoMem, IntrWillReturn]>;

// Test whether a pointer is associated with a type metadata identifier.
def int_type_test : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty, llvm_metadata_ty],
                              [IntrNoMem, IntrWillReturn, IntrSpeculatable]>;

// Safely loads a function pointer from a virtual table pointer using type metadata.
def int_type_checked_load : DefaultAttrsIntrinsic<[llvm_ptr_ty, llvm_i1_ty],
                                      [llvm_ptr_ty, llvm_i32_ty, llvm_metadata_ty],
                                      [IntrNoMem, IntrWillReturn]>;

// Safely loads a relative function pointer from a virtual table pointer using type metadata.
def int_type_checked_load_relative : DefaultAttrsIntrinsic<[llvm_ptr_ty, llvm_i1_ty],
                                      [llvm_ptr_ty, llvm_i32_ty, llvm_metadata_ty],
                                      [IntrNoMem, IntrWillReturn]>;

// Test whether a pointer is associated with a type metadata identifier. Used
// for public visibility classes that may later be refined to private
// visibility.
def int_public_type_test : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty, llvm_metadata_ty],
                              [IntrNoMem, IntrWillReturn, IntrSpeculatable]>;

// Create a branch funnel that implements an indirect call to a limited set of
// callees. This needs to be a musttail call.
def int_icall_branch_funnel : DefaultAttrsIntrinsic<[], [llvm_vararg_ty], []>;

def int_load_relative: DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_ptr_ty, llvm_anyint_ty],
                                 [IntrReadMem, IntrArgMemOnly]>;

def int_asan_check_memaccess :
  Intrinsic<[],[llvm_ptr_ty, llvm_i32_ty], [ImmArg<ArgIndex<1>>]>;

// HWASan intrinsics to test whether a pointer is addressable.
//===----------------------------------------------------------------------===//
//
// Variant 1) is the OG memaccess intrinsic
// Parameters: Shadow base (passed in a register), pointer to be checked for
// validity, AccessInfo (AccessInfo is defined in HWAddressSanitizer.h)
def int_hwasan_check_memaccess :
  Intrinsic<[], [llvm_ptr_ty, llvm_ptr_ty, llvm_i32_ty],
            [ImmArg<ArgIndex<2>>]>;

// Variant 2) supports short granule checks
// Parameters: same as Variant 1
def int_hwasan_check_memaccess_shortgranules :
  Intrinsic<[], [llvm_ptr_ty, llvm_ptr_ty, llvm_i32_ty],
            [ImmArg<ArgIndex<2>>]>;

// Variant 3) assumes a fixed shadow offset
// Parameters: Pointer to be checked for validity, AccessInfo, Shadow base
def int_hwasan_check_memaccess_fixedshadow :
  Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i64_ty],
            [ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

// Variant 4) supports short granule checks and assumes a fixed shadow offset
// Parameters: same as Variant 3
def int_hwasan_check_memaccess_shortgranules_fixedshadow :
  Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i64_ty],
            [ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

// Xray intrinsics
//===----------------------------------------------------------------------===//
// Custom event logging for x-ray.
// Takes a pointer to a string and the length of the string.
def int_xray_customevent : Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty],
                                     [IntrWriteMem, NoCapture<ArgIndex<0>>,
                                      ReadOnly<ArgIndex<0>>]>;
// Typed event logging for x-ray.
// Takes a numeric type tag, a pointer to a string and the length of the string.
def int_xray_typedevent : Intrinsic<[], [llvm_i64_ty, llvm_ptr_ty, llvm_i64_ty],
                                        [IntrWriteMem, NoCapture<ArgIndex<1>>,
                                         ReadOnly<ArgIndex<1>>]>;
//===----------------------------------------------------------------------===//

//===------ Memory intrinsics with element-wise atomicity guarantees ------===//
//

// @llvm.memcpy.element.unordered.atomic.*(dest, src, length, elementsize)
def int_memcpy_element_unordered_atomic
    : Intrinsic<[],
                [llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty, llvm_i32_ty],
                [IntrArgMemOnly, IntrWillReturn, IntrNoSync,
                 NoCapture<ArgIndex<0>>, NoCapture<ArgIndex<1>>,
                 WriteOnly<ArgIndex<0>>, ReadOnly<ArgIndex<1>>,
                 ImmArg<ArgIndex<3>>]>;

// @llvm.memmove.element.unordered.atomic.*(dest, src, length, elementsize)
def int_memmove_element_unordered_atomic
    : Intrinsic<[],
                [llvm_anyptr_ty, llvm_anyptr_ty, llvm_anyint_ty, llvm_i32_ty],
                [IntrArgMemOnly, IntrWillReturn, IntrNoSync,
                 NoCapture<ArgIndex<0>>, NoCapture<ArgIndex<1>>,
                 WriteOnly<ArgIndex<0>>, ReadOnly<ArgIndex<1>>,
                 ImmArg<ArgIndex<3>>]>;

// @llvm.memset.element.unordered.atomic.*(dest, value, length, elementsize)
def int_memset_element_unordered_atomic
    : Intrinsic<[], [llvm_anyptr_ty, llvm_i8_ty, llvm_anyint_ty, llvm_i32_ty],
                [IntrWriteMem, IntrArgMemOnly, IntrWillReturn, IntrNoSync,
                 NoCapture<ArgIndex<0>>, WriteOnly<ArgIndex<0>>,
                 ImmArg<ArgIndex<3>>]>;

//===------------------------ Reduction Intrinsics ------------------------===//
//
let IntrProperties = [IntrNoMem, IntrSpeculatable] in {

  def int_vector_reduce_fadd : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [LLVMVectorElementType<0>,
                                          llvm_anyvector_ty]>;
  def int_vector_reduce_fmul : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [LLVMVectorElementType<0>,
                                          llvm_anyvector_ty]>;
  def int_vector_reduce_add : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                        [llvm_anyvector_ty]>;
  def int_vector_reduce_mul : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                        [llvm_anyvector_ty]>;
  def int_vector_reduce_and : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                        [llvm_anyvector_ty]>;
  def int_vector_reduce_or : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                       [llvm_anyvector_ty]>;
  def int_vector_reduce_xor : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                        [llvm_anyvector_ty]>;
  def int_vector_reduce_smax : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [llvm_anyvector_ty]>;
  def int_vector_reduce_smin : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [llvm_anyvector_ty]>;
  def int_vector_reduce_umax : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [llvm_anyvector_ty]>;
  def int_vector_reduce_umin : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [llvm_anyvector_ty]>;
  def int_vector_reduce_fmax : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [llvm_anyvector_ty]>;
  def int_vector_reduce_fmin : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [llvm_anyvector_ty]>;
  def int_vector_reduce_fminimum: DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [llvm_anyvector_ty]>;
  def int_vector_reduce_fmaximum: DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                                         [llvm_anyvector_ty]>;
}

//===----- Matrix intrinsics ---------------------------------------------===//

def int_matrix_transpose
  : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
              [LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty],
              [ IntrNoSync, IntrWillReturn, IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<1>>,
               ImmArg<ArgIndex<2>>]>;

def int_matrix_multiply
  : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
              [llvm_anyvector_ty, llvm_anyvector_ty, llvm_i32_ty, llvm_i32_ty,
               llvm_i32_ty],
              [IntrNoSync, IntrWillReturn, IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<2>>,
               ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>]>;

def int_matrix_column_major_load
  : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
              [llvm_ptr_ty, llvm_anyint_ty, llvm_i1_ty,
               llvm_i32_ty, llvm_i32_ty],
              [IntrNoSync, IntrWillReturn, IntrArgMemOnly, IntrReadMem,
               NoCapture<ArgIndex<0>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>,
               ImmArg<ArgIndex<4>>]>;

def int_matrix_column_major_store
  : DefaultAttrsIntrinsic<[],
              [llvm_anyvector_ty, llvm_ptr_ty,
               llvm_anyint_ty, llvm_i1_ty, llvm_i32_ty, llvm_i32_ty],
              [IntrNoSync, IntrWillReturn, IntrArgMemOnly, IntrWriteMem,
               WriteOnly<ArgIndex<1>>, NoCapture<ArgIndex<1>>,
               ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>]>;

//===---------- Intrinsics to control hardware supported loops ----------===//

// Specify that the value given is the number of iterations that the next loop
// will execute.
def int_set_loop_iterations :
  DefaultAttrsIntrinsic<[], [llvm_anyint_ty], [IntrNoDuplicate]>;

// Same as the above, but produces a value (the same as the input operand) to
// be fed into the loop.
def int_start_loop_iterations :
  DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoDuplicate]>;

// Specify that the value given is the number of iterations that the next loop
// will execute. Also test that the given count is not zero, allowing it to
// control entry to a 'while' loop.
def int_test_set_loop_iterations :
  DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_anyint_ty], [IntrNoDuplicate]>;

// Same as the above, but produces an extra value (the same as the input
// operand) to be fed into the loop.
def int_test_start_loop_iterations :
  DefaultAttrsIntrinsic<[llvm_anyint_ty, llvm_i1_ty], [LLVMMatchType<0>],
                        [IntrNoDuplicate]>;

// Decrement loop counter by the given argument. Return false if the loop
// should exit.
def int_loop_decrement :
  DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_anyint_ty], [IntrNoDuplicate]>;

// Decrement the first operand (the loop counter) by the second operand (the
// maximum number of elements processed in an iteration). Return the remaining
// number of iterations still to be executed. This is effectively a sub which
// can be used with a phi, icmp and br to control the number of iterations
// executed, as usual. Any optimisations are allowed to treat it is a sub, and
// it's scevable, so it's the backends responsibility to handle cases where it
// may be optimised.
def int_loop_decrement_reg :
  DefaultAttrsIntrinsic<[llvm_anyint_ty],
            [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoDuplicate]>;

//===----- Intrinsics that are used to provide predicate information -----===//

def int_ssa_copy : DefaultAttrsIntrinsic<[llvm_any_ty], [LLVMMatchType<0>],
                             [IntrNoMem, Returned<ArgIndex<0>>]>;

//===------- Intrinsics that are used to preserve debug information -------===//

def int_preserve_array_access_index : DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                                                [llvm_anyptr_ty, llvm_i32_ty,
                                                 llvm_i32_ty],
                                                [IntrNoMem,
                                                 ImmArg<ArgIndex<1>>,
                                                 ImmArg<ArgIndex<2>>]>;
def int_preserve_union_access_index : DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                                                [llvm_anyptr_ty, llvm_i32_ty],
                                                [IntrNoMem,
                                                 ImmArg<ArgIndex<1>>]>;
def int_preserve_struct_access_index : DefaultAttrsIntrinsic<[llvm_anyptr_ty],
                                                 [llvm_anyptr_ty, llvm_i32_ty,
                                                  llvm_i32_ty],
                                                 [IntrNoMem,
                                                  ImmArg<ArgIndex<1>>,
                                                  ImmArg<ArgIndex<2>>]>;
def int_preserve_static_offset : DefaultAttrsIntrinsic<[llvm_ptr_ty],
                                                       [llvm_ptr_ty],
                                                       [IntrNoMem, IntrSpeculatable,
                                                        ReadNone <ArgIndex<0>>]>;

//===------------ Intrinsics to perform common vector shuffles ------------===//

def int_vector_reverse : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                                               [LLVMMatchType<0>],
                                               [IntrNoMem]>;

def int_vector_splice : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                                              [LLVMMatchType<0>,
                                               LLVMMatchType<0>,
                                               llvm_i32_ty],
                                              [IntrNoMem, ImmArg<ArgIndex<2>>]>;

//===---------- Intrinsics to query properties of scalable vectors --------===//
def int_vscale : DefaultAttrsIntrinsic<[llvm_anyint_ty], [], [IntrNoMem]>;

//===---------- Intrinsics to perform subvector insertion/extraction ------===//
def int_vector_insert : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                                              [LLVMMatchType<0>, llvm_anyvector_ty, llvm_i64_ty],
                                              [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<2>>]>;

def int_vector_extract : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                                               [llvm_anyvector_ty, llvm_i64_ty],
                                               [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<1>>]>;


def int_vector_interleave2   : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                                                     [LLVMHalfElementsVectorType<0>,
                                                      LLVMHalfElementsVectorType<0>],
                                                     [IntrNoMem]>;

def int_vector_deinterleave2 : DefaultAttrsIntrinsic<[LLVMHalfElementsVectorType<0>,
                                                      LLVMHalfElementsVectorType<0>],
                                                     [llvm_anyvector_ty],
                                                     [IntrNoMem]>;

//===-------------- Intrinsics to perform partial reduction ---------------===//

def int_experimental_vector_partial_reduce_add : DefaultAttrsIntrinsic<[LLVMMatchType<0>],
                                                                       [llvm_anyvector_ty, llvm_anyvector_ty],
                                                                       [IntrNoMem]>;

//===----------------- Pointer Authentication Intrinsics ------------------===//
//

// Sign an unauthenticated pointer using the specified key and discriminator,
// passed in that order.
// Returns the first argument, with some known bits replaced with a signature.
def int_ptrauth_sign :
  DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty],
                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;

// Authenticate a signed pointer, using the specified key and discriminator.
// Returns the first argument, with the signature bits removed.
// The signature must be valid.
def int_ptrauth_auth : Intrinsic<[llvm_i64_ty],
                                 [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty],
                                 [IntrNoMem,ImmArg<ArgIndex<1>>]>;

// Authenticate a signed pointer and resign it.
// The second (key) and third (discriminator) arguments specify the signing
// schema used for authenticating.
// The fourth and fifth arguments specify the schema used for signing.
// The signature must be valid.
// This is a combined form of @llvm.ptrauth.sign and @llvm.ptrauth.auth, with
// an additional integrity guarantee on the intermediate value.
def int_ptrauth_resign : Intrinsic<[llvm_i64_ty],
                                   [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty,
                                    llvm_i32_ty, llvm_i64_ty],
                                   [IntrNoMem, ImmArg<ArgIndex<1>>,
                                    ImmArg<ArgIndex<3>>]>;

// Strip the embedded signature out of a signed pointer.
// The second argument specifies the key.
// This behaves like @llvm.ptrauth.auth, but doesn't require the signature to
// be valid.
def int_ptrauth_strip :
  DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;

// Blend a small integer discriminator with an address discriminator, producing
// a new discriminator value.
def int_ptrauth_blend :
  DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;

// Compute the signature of a value, using a given discriminator.
// This differs from @llvm.ptrauth.sign in that it doesn't embed the computed
// signature in the pointer, but instead returns the signature as a value.
// That allows it to be used to sign non-pointer data: in that sense, it is
// generic.  There is no generic @llvm.ptrauth.auth: instead, the signature
// can be computed using @llvm.ptrauth.sign_generic, and compared with icmp.
def int_ptrauth_sign_generic :
  DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;

//===----------------------------------------------------------------------===//
//===------- Convergence Intrinsics ---------------------------------------===//

def int_experimental_convergence_entry
  : DefaultAttrsIntrinsic<[llvm_token_ty], [], [IntrNoMem, IntrConvergent]>;
def int_experimental_convergence_anchor
  : DefaultAttrsIntrinsic<[llvm_token_ty], [], [IntrNoMem, IntrConvergent]>;
def int_experimental_convergence_loop
  : DefaultAttrsIntrinsic<[llvm_token_ty], [], [IntrNoMem, IntrConvergent]>;

//===----------------------------------------------------------------------===//
// Target-specific intrinsics
//===----------------------------------------------------------------------===//

include "llvm/IR/IntrinsicsPowerPC.td"
include "llvm/IR/IntrinsicsX86.td"
include "llvm/IR/IntrinsicsARM.td"
include "llvm/IR/IntrinsicsAArch64.td"
include "llvm/IR/IntrinsicsXCore.td"
include "llvm/IR/IntrinsicsHexagon.td"
include "llvm/IR/IntrinsicsNVVM.td"
include "llvm/IR/IntrinsicsMips.td"
include "llvm/IR/IntrinsicsAMDGPU.td"
include "llvm/IR/IntrinsicsBPF.td"
include "llvm/IR/IntrinsicsSystemZ.td"
include "llvm/IR/IntrinsicsWebAssembly.td"
include "llvm/IR/IntrinsicsRISCV.td"
include "llvm/IR/IntrinsicsSPIRV.td"
include "llvm/IR/IntrinsicsVE.td"
include "llvm/IR/IntrinsicsDirectX.td"
include "llvm/IR/IntrinsicsLoongArch.td"

#endif // TEST_INTRINSICS_SUPPRESS_DEFS
