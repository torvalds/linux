//==--- OpenCLBuiltins.td - OpenCL builtin declarations -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains TableGen definitions for OpenCL builtin function
// declarations.  In case of an unresolved function name in OpenCL, Clang will
// check for a function described in this file when -fdeclare-opencl-builtins
// is specified.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//              Definitions of miscellaneous basic entities.
//===----------------------------------------------------------------------===//
// Versions of OpenCL
class Version<int _Version> {
  int ID = _Version;
}
def CLAll : Version<  0>;
def CL10  : Version<100>;
def CL11  : Version<110>;
def CL12  : Version<120>;
def CL20  : Version<200>;

// Address spaces
// Pointer types need to be assigned an address space.
class AddressSpace<string _AS> {
  string Name = _AS;
}
def DefaultAS    : AddressSpace<"clang::LangAS::Default">;
def PrivateAS    : AddressSpace<"clang::LangAS::opencl_private">;
def GlobalAS     : AddressSpace<"clang::LangAS::opencl_global">;
def ConstantAS   : AddressSpace<"clang::LangAS::opencl_constant">;
def LocalAS      : AddressSpace<"clang::LangAS::opencl_local">;
def GenericAS    : AddressSpace<"clang::LangAS::opencl_generic">;

// OpenCL language extension.
class AbstractExtension<string _Ext> {
  // One or more OpenCL extensions, space separated.  Each extension must be
  // a valid extension name for the opencl extension pragma.
  string ExtName = _Ext;
}

// Extension associated to a builtin function.
class FunctionExtension<string _Ext> : AbstractExtension<_Ext>;

// Extension associated to a type.  This enables implicit conditionalization of
// builtin function overloads containing a type that depends on an extension.
// During overload resolution, when a builtin function overload contains a type
// with a TypeExtension, those overloads are skipped when the extension is
// disabled.
class TypeExtension<string _Ext> : AbstractExtension<_Ext>;

// Concatenate zero or more space-separated extensions in NewExts to Base and
// return the resulting FunctionExtension in ret.
class concatExtension<FunctionExtension Base, string NewExts> {
  FunctionExtension ret = FunctionExtension<
    !cond(
      // Return Base extension if NewExts is empty,
      !empty(NewExts) : Base.ExtName,

      // otherwise, return NewExts if Base extension is empty,
      !empty(Base.ExtName) : NewExts,

      // otherwise, concatenate NewExts to Base.
      true : Base.ExtName # " " # NewExts
    )
  >;
}

// TypeExtension definitions.
def NoTypeExt   : TypeExtension<"">;
def Fp16TypeExt : TypeExtension<"cl_khr_fp16">;
def Fp64TypeExt : TypeExtension<"cl_khr_fp64">;
def Atomic64TypeExt : TypeExtension<"cl_khr_int64_base_atomics cl_khr_int64_extended_atomics">;
def AtomicFp64TypeExt : TypeExtension<"cl_khr_int64_base_atomics cl_khr_int64_extended_atomics cl_khr_fp64">;

// FunctionExtension definitions.
def FuncExtNone                          : FunctionExtension<"">;
def FuncExtKhrSubgroups                  : FunctionExtension<"__opencl_subgroup_builtins">;
def FuncExtKhrSubgroupExtendedTypes      : FunctionExtension<"cl_khr_subgroup_extended_types">;
def FuncExtKhrSubgroupNonUniformVote     : FunctionExtension<"cl_khr_subgroup_non_uniform_vote">;
def FuncExtKhrSubgroupBallot             : FunctionExtension<"cl_khr_subgroup_ballot">;
def FuncExtKhrSubgroupNonUniformArithmetic: FunctionExtension<"cl_khr_subgroup_non_uniform_arithmetic">;
def FuncExtKhrSubgroupShuffle            : FunctionExtension<"cl_khr_subgroup_shuffle">;
def FuncExtKhrSubgroupShuffleRelative    : FunctionExtension<"cl_khr_subgroup_shuffle_relative">;
def FuncExtKhrSubgroupClusteredReduce    : FunctionExtension<"cl_khr_subgroup_clustered_reduce">;
def FuncExtKhrExtendedBitOps             : FunctionExtension<"cl_khr_extended_bit_ops">;
def FuncExtKhrGlobalInt32BaseAtomics     : FunctionExtension<"cl_khr_global_int32_base_atomics">;
def FuncExtKhrGlobalInt32ExtendedAtomics : FunctionExtension<"cl_khr_global_int32_extended_atomics">;
def FuncExtKhrLocalInt32BaseAtomics      : FunctionExtension<"cl_khr_local_int32_base_atomics">;
def FuncExtKhrLocalInt32ExtendedAtomics  : FunctionExtension<"cl_khr_local_int32_extended_atomics">;
def FuncExtKhrInt64BaseAtomics           : FunctionExtension<"cl_khr_int64_base_atomics">;
def FuncExtKhrInt64ExtendedAtomics       : FunctionExtension<"cl_khr_int64_extended_atomics">;
def FuncExtKhrMipmapImage                : FunctionExtension<"cl_khr_mipmap_image">;
def FuncExtKhrMipmapImageWrites          : FunctionExtension<"cl_khr_mipmap_image_writes">;
def FuncExtKhrGlMsaaSharing              : FunctionExtension<"cl_khr_gl_msaa_sharing">;

def FuncExtOpenCLCDeviceEnqueue          : FunctionExtension<"__opencl_c_device_enqueue">;
def FuncExtOpenCLCGenericAddressSpace    : FunctionExtension<"__opencl_c_generic_address_space">;
def FuncExtOpenCLCNamedAddressSpaceBuiltins : FunctionExtension<"__opencl_c_named_address_space_builtins">;
def FuncExtOpenCLCPipes                  : FunctionExtension<"__opencl_c_pipes">;
def FuncExtOpenCLCWGCollectiveFunctions  : FunctionExtension<"__opencl_c_work_group_collective_functions">;
def FuncExtOpenCLCReadWriteImages        : FunctionExtension<"__opencl_c_read_write_images">;
def FuncExtFloatAtomicsFp16GlobalASLoadStore  : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_global_atomic_load_store">;
def FuncExtFloatAtomicsFp16LocalASLoadStore   : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_local_atomic_load_store">;
def FuncExtFloatAtomicsFp16GenericASLoadStore : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_global_atomic_load_store __opencl_c_ext_fp16_local_atomic_load_store">;
def FuncExtFloatAtomicsFp16GlobalASAdd        : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_global_atomic_add">;
def FuncExtFloatAtomicsFp32GlobalASAdd        : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp32_global_atomic_add">;
def FuncExtFloatAtomicsFp64GlobalASAdd        : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp64_global_atomic_add">;
def FuncExtFloatAtomicsFp16LocalASAdd         : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_local_atomic_add">;
def FuncExtFloatAtomicsFp32LocalASAdd         : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp32_local_atomic_add">;
def FuncExtFloatAtomicsFp64LocalASAdd         : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp64_local_atomic_add">;
def FuncExtFloatAtomicsFp16GenericASAdd       : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_local_atomic_add __opencl_c_ext_fp16_global_atomic_add">;
def FuncExtFloatAtomicsFp32GenericASAdd       : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp32_local_atomic_add __opencl_c_ext_fp32_global_atomic_add">;
def FuncExtFloatAtomicsFp64GenericASAdd       : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp64_local_atomic_add __opencl_c_ext_fp64_global_atomic_add">;
def FuncExtFloatAtomicsFp16GlobalASMinMax     : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_global_atomic_min_max">;
def FuncExtFloatAtomicsFp32GlobalASMinMax     : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp32_global_atomic_min_max">;
def FuncExtFloatAtomicsFp64GlobalASMinMax     : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp64_global_atomic_min_max">;
def FuncExtFloatAtomicsFp16LocalASMinMax      : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_local_atomic_min_max">;
def FuncExtFloatAtomicsFp32LocalASMinMax      : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp32_local_atomic_min_max">;
def FuncExtFloatAtomicsFp64LocalASMinMax      : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp64_local_atomic_min_max">;
def FuncExtFloatAtomicsFp16GenericASMinMax    : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp16_local_atomic_min_max __opencl_c_ext_fp16_global_atomic_min_max">;
def FuncExtFloatAtomicsFp32GenericASMinMax    : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp32_local_atomic_min_max __opencl_c_ext_fp32_global_atomic_min_max">;
def FuncExtFloatAtomicsFp64GenericASMinMax    : FunctionExtension<"cl_ext_float_atomics __opencl_c_ext_fp64_local_atomic_min_max __opencl_c_ext_fp64_global_atomic_min_max">;

// Not a real extension, but a workaround to add C++ for OpenCL specific builtins.
def FuncExtOpenCLCxx                     : FunctionExtension<"__cplusplus">;

// Arm extensions.
def ArmIntegerDotProductInt8                   : FunctionExtension<"cl_arm_integer_dot_product_int8">;
def ArmIntegerDotProductAccumulateInt8         : FunctionExtension<"cl_arm_integer_dot_product_accumulate_int8">;
def ArmIntegerDotProductAccumulateInt16        : FunctionExtension<"cl_arm_integer_dot_product_accumulate_int16">;
def ArmIntegerDotProductAccumulateSaturateInt8 : FunctionExtension<"cl_arm_integer_dot_product_accumulate_saturate_int8">;

// Qualified Type.  These map to ASTContext::QualType.
class QualType<string _TypeExpr, bit _IsAbstract=0> {
  // Expression to obtain the QualType inside OCL2Qual.
  // E.g. TypeExpr="Context.IntTy" for the int type.
  string TypeExpr = _TypeExpr;
  // Some QualTypes in this file represent an abstract type for which there is
  // no corresponding AST QualType, e.g. a GenType or an `image2d_t` type
  // without access qualifiers.
  bit IsAbstract = _IsAbstract;
}

// List of integers.
class IntList<string _Name, list<int> _List> {
  string Name = _Name;
  list<int> List = _List;
}

//===----------------------------------------------------------------------===//
//                      OpenCL C classes for types
//===----------------------------------------------------------------------===//
// OpenCL C basic data types (int, float, image2d_t, ...).
// Its child classes can represent concrete types (e.g. VectorType) or
// abstract types (e.g. GenType).
class Type<string _Name, QualType _QTExpr> {
  // Name of the Type.
  string Name = _Name;
  // QualType associated with this type.
  QualType QTExpr = _QTExpr;
  // Size of the vector (if applicable).
  int VecWidth = 1;
  // Is a pointer.
  bit IsPointer = 0;
  // "const" qualifier.
  bit IsConst = 0;
  // "volatile" qualifier.
  bit IsVolatile = 0;
  // Access qualifier. Must be one of ("RO", "WO", "RW").
  string AccessQualifier = "";
  // Address space.
  string AddrSpace = DefaultAS.Name;
  // Extension that needs to be enabled to expose a builtin that uses this type.
  TypeExtension Extension = NoTypeExt;
}

// OpenCL vector types (e.g. int2, int3, int16, float8, ...).
class VectorType<Type _Ty, int _VecWidth> : Type<_Ty.Name, _Ty.QTExpr> {
  let VecWidth = _VecWidth;
  let AccessQualifier = "";
  // Inherited fields
  let IsPointer = _Ty.IsPointer;
  let IsConst = _Ty.IsConst;
  let IsVolatile = _Ty.IsVolatile;
  let AddrSpace = _Ty.AddrSpace;
  let Extension = _Ty.Extension;
}

// OpenCL pointer types (e.g. int*, float*, ...).
class PointerType<Type _Ty, AddressSpace _AS = DefaultAS> :
    Type<_Ty.Name, _Ty.QTExpr> {
  let AddrSpace = _AS.Name;
  // Inherited fields
  let VecWidth = _Ty.VecWidth;
  let IsPointer = 1;
  let IsConst = _Ty.IsConst;
  let IsVolatile = _Ty.IsVolatile;
  let AccessQualifier = _Ty.AccessQualifier;
  let Extension = _Ty.Extension;
}

// OpenCL const types (e.g. const int).
class ConstType<Type _Ty> : Type<_Ty.Name, _Ty.QTExpr> {
  let IsConst = 1;
  // Inherited fields
  let VecWidth = _Ty.VecWidth;
  let IsPointer = _Ty.IsPointer;
  let IsVolatile = _Ty.IsVolatile;
  let AccessQualifier = _Ty.AccessQualifier;
  let AddrSpace = _Ty.AddrSpace;
  let Extension = _Ty.Extension;
}

// OpenCL volatile types (e.g. volatile int).
class VolatileType<Type _Ty> : Type<_Ty.Name, _Ty.QTExpr> {
  let IsVolatile = 1;
  // Inherited fields
  let VecWidth = _Ty.VecWidth;
  let IsPointer = _Ty.IsPointer;
  let IsConst = _Ty.IsConst;
  let AccessQualifier = _Ty.AccessQualifier;
  let AddrSpace = _Ty.AddrSpace;
  let Extension = _Ty.Extension;
}

// OpenCL image types (e.g. image2d).
class ImageType<Type _Ty, string _AccessQualifier> :
    Type<_Ty.Name, QualType<_Ty.QTExpr.TypeExpr # _AccessQualifier # "Ty", 0>> {
  let VecWidth = 0;
  let AccessQualifier = _AccessQualifier;
  // Inherited fields
  let IsPointer = _Ty.IsPointer;
  let IsConst = _Ty.IsConst;
  let IsVolatile = _Ty.IsVolatile;
  let AddrSpace = _Ty.AddrSpace;
  // Add TypeExtensions for writable "image3d_t" and "read_write" image types.
  let Extension = !cond(
      !and(!eq(_Ty.Name, "image3d_t"), !eq(_AccessQualifier, "WO")) : TypeExtension<"cl_khr_3d_image_writes">,
      !and(!eq(_Ty.Name, "image3d_t"), !eq(_AccessQualifier, "RW")) : TypeExtension<"cl_khr_3d_image_writes __opencl_c_read_write_images">,
      !or(!eq(_Ty.Name, "image2d_depth_t"), !eq(_Ty.Name, "image2d_array_depth_t")) : TypeExtension<"cl_khr_depth_images">,
      !eq(_AccessQualifier, "RW") : TypeExtension<"__opencl_c_read_write_images">,
      true : _Ty.Extension);
}

// OpenCL enum type (e.g. memory_scope).
class EnumType<string _Name> :
    Type<_Name, QualType<"getOpenCLEnumType(S, \"" # _Name # "\")", 0>> {
}

// OpenCL typedef type (e.g. cl_mem_fence_flags).
class TypedefType<string _Name> :
    Type<_Name, QualType<"getOpenCLTypedefType(S, \"" # _Name # "\")", 0>> {
}

// List of Types.
class TypeList<list<Type> _Type> {
  list<Type> List = _Type;
}

// A GenericType is an abstract type that defines a set of types as a
// combination of Types and vector sizes.
//
// For example, if TypeList = <int, float> and VectorList = <1, 2, 4>, then it
// represents <int, int2, int4, float, float2, float4>.
//
// Some rules apply when using multiple GenericType arguments in a declaration:
//   1. The number of vector sizes must be equal or 1 for all gentypes in a
//      declaration.
//   2. The number of Types must be equal or 1 for all gentypes in a
//      declaration.
//   3. Generic types are combined by iterating over all generic types at once.
//      For example, for the following GenericTypes
//        GenT1 = GenericType<half, [1, 2]> and
//        GenT2 = GenericType<float, int, [1, 2]>
//      A declaration f(GenT1, GenT2) results in the combinations
//        f(half, float), f(half2, float2), f(half, int), f(half2, int2) .
//   4. "sgentype" from the OpenCL specification is supported by specifying
//      a single vector size.
//      For example, for the following GenericTypes
//        GenT = GenericType<half, int, [1, 2]> and
//        SGenT = GenericType<half, int, [1]>
//      A declaration f(GenT, SGenT) results in the combinations
//        f(half, half), f(half2, half), f(int, int), f(int2, int) .
class GenericType<string _Ty, TypeList _TypeList, IntList _VectorList> :
    Type<_Ty, QualType<"null", 1>> {
  // Possible element types of the generic type.
  TypeList TypeList = _TypeList;
  // Possible vector sizes of the types in the TypeList.
  IntList VectorList = _VectorList;
  // The VecWidth field is ignored for GenericTypes. Use VectorList instead.
  let VecWidth = 0;
}

// Builtin function attributes.
def Attr {
  list<bit> None = [0, 0, 0];
  list<bit> Pure = [1, 0, 0];
  list<bit> Const = [0, 1, 0];
  list<bit> Convergent = [0, 0, 1];
}

//===----------------------------------------------------------------------===//
//                      OpenCL C class for builtin functions
//===----------------------------------------------------------------------===//
class Builtin<string _Name, list<Type> _Signature, list<bit> _Attributes = Attr.None> {
  // Name of the builtin function
  string Name = _Name;
  // List of types used by the function. The first one is the return type and
  // the following are the arguments. The list must have at least one element
  // (the return type).
  list<Type> Signature = _Signature;
  // Function attribute __attribute__((pure))
  bit IsPure = _Attributes[0];
  // Function attribute __attribute__((const))
  bit IsConst = _Attributes[1];
  // Function attribute __attribute__((convergent))
  bit IsConv = _Attributes[2];
  // OpenCL extensions to which the function belongs.
  FunctionExtension Extension = FuncExtNone;
  // Version of OpenCL from which the function is available (e.g.: CL10).
  // MinVersion is inclusive.
  Version MinVersion = CL10;
  // Version of OpenCL from which the function is not supported anymore.
  // MaxVersion is exclusive.
  // CLAll makes the function available for all versions.
  Version MaxVersion = CLAll;
}

//===----------------------------------------------------------------------===//
//                 Definitions of OpenCL C types
//===----------------------------------------------------------------------===//

// OpenCL v1.0/1.2/2.0 s6.1.1: Built-in Scalar Data Types.
def Bool      : Type<"bool",      QualType<"Context.BoolTy">>;
def Char      : Type<"char",      QualType<"Context.CharTy">>;
def UChar     : Type<"uchar",     QualType<"Context.UnsignedCharTy">>;
def Short     : Type<"short",     QualType<"Context.ShortTy">>;
def UShort    : Type<"ushort",    QualType<"Context.UnsignedShortTy">>;
def Int       : Type<"int",       QualType<"Context.IntTy">>;
def UInt      : Type<"uint",      QualType<"Context.UnsignedIntTy">>;
def Long      : Type<"long",      QualType<"Context.LongTy">>;
def ULong     : Type<"ulong",     QualType<"Context.UnsignedLongTy">>;
def Float     : Type<"float",     QualType<"Context.FloatTy">>;
let Extension = Fp64TypeExt in {
  def Double    : Type<"double",    QualType<"Context.DoubleTy">>;
}

// The half type for builtins that require the cl_khr_fp16 extension.
let Extension = Fp16TypeExt in {
  def Half      : Type<"half",      QualType<"Context.HalfTy">>;
}

// Without the cl_khr_fp16 extension, the half type can only be used to declare
// a pointer.  Define const and non-const pointer types in all address spaces.
// Use the "__half" alias to allow the TableGen emitter to distinguish the
// (extensionless) pointee type of these pointer-to-half types from the "half"
// type defined above that already carries the cl_khr_fp16 extension.
foreach AS = [PrivateAS, GlobalAS, ConstantAS, LocalAS, GenericAS] in {
  def "HalfPtr" # AS      : PointerType<Type<"__half", QualType<"Context.HalfTy">>, AS>;
  def "HalfPtrConst" # AS : PointerType<ConstType<Type<"__half", QualType<"Context.HalfTy">>>, AS>;
}

def Size      : Type<"size_t",    QualType<"Context.getSizeType()">>;
def PtrDiff   : Type<"ptrdiff_t", QualType<"Context.getPointerDiffType()">>;
def IntPtr    : Type<"intptr_t",  QualType<"Context.getIntPtrType()">>;
def UIntPtr   : Type<"uintptr_t", QualType<"Context.getUIntPtrType()">>;
def Void      : Type<"void",      QualType<"Context.VoidTy">>;

// OpenCL v1.0/1.2/2.0 s6.1.2: Built-in Vector Data Types.
// Built-in vector data types are created by TableGen's OpenCLBuiltinEmitter.

// OpenCL v1.0/1.2/2.0 s6.1.3: Other Built-in Data Types.
// The image definitions are "abstract".  They should not be used without
// specifying an access qualifier (RO/WO/RW).
def Image1d               : Type<"image1d_t", QualType<"Context.OCLImage1d", 1>>;
def Image2d               : Type<"image2d_t", QualType<"Context.OCLImage2d", 1>>;
def Image3d               : Type<"image3d_t", QualType<"Context.OCLImage3d", 1>>;
def Image1dArray          : Type<"image1d_array_t", QualType<"Context.OCLImage1dArray", 1>>;
def Image1dBuffer         : Type<"image1d_buffer_t", QualType<"Context.OCLImage1dBuffer", 1>>;
def Image2dArray          : Type<"image2d_array_t", QualType<"Context.OCLImage2dArray", 1>>;
def Image2dDepth          : Type<"image2d_depth_t", QualType<"Context.OCLImage2dDepth", 1>>;
def Image2dArrayDepth     : Type<"image2d_array_depth_t", QualType<"Context.OCLImage2dArrayDepth", 1>>;
def Image2dMsaa           : Type<"image2d_msaa_t", QualType<"Context.OCLImage2dMSAA", 1>>;
def Image2dArrayMsaa      : Type<"image2d_array_msaa_t", QualType<"Context.OCLImage2dArrayMSAA", 1>>;
def Image2dMsaaDepth      : Type<"image2d_msaa_depth_t", QualType<"Context.OCLImage2dMSAADepth", 1>>;
def Image2dArrayMsaaDepth : Type<"image2d_array_msaa_depth_t", QualType<"Context.OCLImage2dArrayMSAADepth", 1>>;

def Sampler               : Type<"sampler_t", QualType<"Context.OCLSamplerTy">>;
def ClkEvent              : Type<"clk_event_t", QualType<"Context.OCLClkEventTy">>;
def Event                 : Type<"event_t", QualType<"Context.OCLEventTy">>;
def Queue                 : Type<"queue_t", QualType<"Context.OCLQueueTy">>;
def ReserveId             : Type<"reserve_id_t", QualType<"Context.OCLReserveIDTy">>;
def MemFenceFlags         : TypedefType<"cl_mem_fence_flags">;
def ClkProfilingInfo      : TypedefType<"clk_profiling_info">;
def NDRange               : TypedefType<"ndrange_t">;

// OpenCL v2.0 s6.13.11: Atomic integer and floating-point types.
def AtomicInt             : Type<"atomic_int", QualType<"Context.getAtomicType(Context.IntTy)">>;
def AtomicUInt            : Type<"atomic_uint", QualType<"Context.getAtomicType(Context.UnsignedIntTy)">>;
let Extension = Atomic64TypeExt in {
  def AtomicLong            : Type<"atomic_long", QualType<"Context.getAtomicType(Context.LongTy)">>;
  def AtomicULong           : Type<"atomic_ulong", QualType<"Context.getAtomicType(Context.UnsignedLongTy)">>;
}
def AtomicFloat           : Type<"atomic_float", QualType<"Context.getAtomicType(Context.FloatTy)">>;
let Extension = AtomicFp64TypeExt in {
  def AtomicDouble          : Type<"atomic_double", QualType<"Context.getAtomicType(Context.DoubleTy)">>;
}
def AtomicHalf            : Type<"atomic_half", QualType<"Context.getAtomicType(Context.HalfTy)">>;
def AtomicIntPtr          : Type<"atomic_intptr_t", QualType<"Context.getAtomicType(Context.getIntPtrType())">>;
def AtomicUIntPtr         : Type<"atomic_uintptr_t", QualType<"Context.getAtomicType(Context.getUIntPtrType())">>;
def AtomicSize            : Type<"atomic_size_t", QualType<"Context.getAtomicType(Context.getSizeType())">>;
def AtomicPtrDiff         : Type<"atomic_ptrdiff_t", QualType<"Context.getAtomicType(Context.getPointerDiffType())">>;

def AtomicFlag            : TypedefType<"atomic_flag">;
def MemoryOrder           : EnumType<"memory_order">;
def MemoryScope           : EnumType<"memory_scope">;

//===----------------------------------------------------------------------===//
//                 Definitions of OpenCL gentype variants
//===----------------------------------------------------------------------===//
// The OpenCL specification often uses "gentype" in builtin function
// declarations to indicate that a builtin function is available with various
// argument and return types.  The types represented by "gentype" vary between
// different parts of the specification.  The following definitions capture
// the different type lists for gentypes in different parts of the
// specification.

// Vector width lists.
def VecAndScalar: IntList<"VecAndScalar", [1, 2, 3, 4, 8, 16]>;
def VecNoScalar : IntList<"VecNoScalar", [2, 3, 4, 8, 16]>;
def Vec1        : IntList<"Vec1", [1]>;
def Vec1234     : IntList<"Vec1234", [1, 2, 3, 4]>;

// Type lists.
def TLAll           : TypeList<[Char,  UChar, Short,  UShort, Int,  UInt, Long,  ULong, Float, Double, Half]>;
def TLFloat         : TypeList<[Float, Double, Half]>;
def TLSignedInts    : TypeList<[Char, Short, Int, Long]>;
def TLUnsignedInts  : TypeList<[UChar, UShort, UInt, ULong]>;

def TLIntLongFloats : TypeList<[Int, UInt, Long, ULong, Float, Double, Half]>;

// All unsigned integer types twice, to facilitate unsigned return types for e.g.
// uchar abs(char) and
// uchar abs(uchar).
def TLAllUIntsTwice : TypeList<[UChar, UChar, UShort, UShort, UInt, UInt, ULong, ULong]>;

def TLAllInts       : TypeList<[Char, UChar, Short, UShort, Int, UInt, Long, ULong]>;

// GenType definitions for multiple base types (e.g. all floating point types,
// or all integer types).
// All types
def AGenType1              : GenericType<"AGenType1", TLAll, Vec1>;
def AGenTypeN              : GenericType<"AGenTypeN", TLAll, VecAndScalar>;
def AGenTypeNNoScalar      : GenericType<"AGenTypeNNoScalar", TLAll, VecNoScalar>;
// All integer
def AIGenType1             : GenericType<"AIGenType1", TLAllInts, Vec1>;
def AIGenTypeN             : GenericType<"AIGenTypeN", TLAllInts, VecAndScalar>;
def AIGenTypeNNoScalar     : GenericType<"AIGenTypeNNoScalar", TLAllInts, VecNoScalar>;
// All integer to unsigned
def AI2UGenTypeN           : GenericType<"AI2UGenTypeN", TLAllUIntsTwice, VecAndScalar>;
// Signed integer
def SGenTypeN              : GenericType<"SGenTypeN", TLSignedInts, VecAndScalar>;
// Unsigned integer
def UGenTypeN              : GenericType<"UGenTypeN", TLUnsignedInts, VecAndScalar>;
// Float
def FGenTypeN              : GenericType<"FGenTypeN", TLFloat, VecAndScalar>;
// (u)int, (u)long, and all floats
def IntLongFloatGenType1   : GenericType<"IntLongFloatGenType1", TLIntLongFloats, Vec1>;
// (u)char and (u)short
def CharShortGenType1      : GenericType<"CharShortGenType1",
                                 TypeList<[Char, UChar, Short, UShort]>, Vec1>;

// GenType definitions for every single base type (e.g. fp32 only).
// Names are like: GenTypeFloatVecAndScalar.
foreach Type = [Char, UChar, Short, UShort,
                Int, UInt, Long, ULong,
                Float, Double, Half] in {
  foreach VecSizes = [VecAndScalar, VecNoScalar] in {
    def "GenType" # Type # VecSizes :
              GenericType<"GenType" # Type # VecSizes,
                          TypeList<[Type]>, VecSizes>;
  }
}

// GenType definitions for vec1234.
foreach Type = [Float, Double, Half] in {
  def "GenType" # Type # Vec1234 :
              GenericType<"GenType" # Type # Vec1234,
                          TypeList<[Type]>, Vec1234>;
}


//===----------------------------------------------------------------------===//
//                 Definitions of OpenCL builtin functions
//===----------------------------------------------------------------------===//
//--------------------------------------------------------------------
// OpenCL v1.1/1.2/2.0 s6.2.3 - Explicit conversions.
// OpenCL v2.0 Extensions s5.1.1 and s6.1.1 - Conversions.

// Generate the convert_* builtins functions.
foreach RType = [Float, Double, Half, Char, UChar, Short,
                 UShort, Int, UInt, Long, ULong] in {
  foreach IType = [Float, Double, Half, Char, UChar, Short,
                   UShort, Int, UInt, Long, ULong] in {
    // Conversions to integer type have a sat and non-sat variant.
    foreach sat = !cond(!eq(RType.Name, "float") : [""],
                        !eq(RType.Name, "double") : [""],
                        !eq(RType.Name, "half") : [""],
                        1 : ["", "_sat"]) in {
      foreach rnd = ["", "_rte", "_rtn", "_rtp", "_rtz"] in {
        def : Builtin<"convert_" # RType.Name # sat # rnd, [RType, IType],
                      Attr.Const>;
        foreach v = [2, 3, 4, 8, 16] in {
          def : Builtin<"convert_" # RType.Name # v # sat # rnd,
                        [VectorType<RType, v>, VectorType<IType, v>],
                        Attr.Const>;
        }
      }
    }
  }
}

//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.1, v1.2 s6.12.1, v2.0 s6.13.1 - Work-item Functions
// --- Table 7 ---
def : Builtin<"get_work_dim", [UInt], Attr.Const>;
foreach name = ["get_global_size", "get_global_id", "get_local_size",
                "get_local_id", "get_num_groups", "get_group_id",
                "get_global_offset"] in {
  def : Builtin<name, [Size, UInt], Attr.Const>;
}

let MinVersion = CL20 in {
  def : Builtin<"get_enqueued_local_size", [Size, UInt]>;
  foreach name = ["get_global_linear_id", "get_local_linear_id"] in {
    def : Builtin<name, [Size]>;
  }
}


//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.2, v1.2 s6.12.2, v2.0 s6.13.2 - Math functions
// OpenCL Extension v2.0 s5.1.2 and s6.1.2 - Math Functions
// --- Table 8 ---
// --- 1 argument ---
foreach name = ["acos", "acosh", "acospi",
                "asin", "asinh", "asinpi",
                "atan", "atanh", "atanpi",
                "cbrt", "ceil",
                "cos", "cosh", "cospi",
                "erfc", "erf",
                "exp", "exp2", "exp10", "expm1",
                "fabs", "floor",
                "log", "log2", "log10", "log1p", "logb",
                "rint", "round", "rsqrt",
                "sin", "sinh", "sinpi",
                "sqrt",
                "tan", "tanh", "tanpi",
                "tgamma", "trunc",
                "lgamma"] in {
    def : Builtin<name, [FGenTypeN, FGenTypeN], Attr.Const>;
}
foreach name = ["nan"] in {
  def : Builtin<name, [GenTypeFloatVecAndScalar, GenTypeUIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecAndScalar, GenTypeULongVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecAndScalar, GenTypeUShortVecAndScalar], Attr.Const>;
}

// --- 2 arguments ---
foreach name = ["atan2", "atan2pi", "copysign", "fdim", "fmod", "hypot",
                "maxmag", "minmag", "nextafter", "pow", "powr",
                "remainder"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN, FGenTypeN], Attr.Const>;
}
foreach name = ["fmax", "fmin"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN, FGenTypeN], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecNoScalar, GenTypeFloatVecNoScalar, Float], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecNoScalar, GenTypeDoubleVecNoScalar, Double], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecNoScalar, GenTypeHalfVecNoScalar, Half], Attr.Const>;
}
foreach name = ["ilogb"] in {
  def : Builtin<name, [GenTypeIntVecAndScalar, GenTypeFloatVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeIntVecAndScalar, GenTypeDoubleVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeIntVecAndScalar, GenTypeHalfVecAndScalar], Attr.Const>;
}
foreach name = ["ldexp"] in {
  def : Builtin<name, [GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecNoScalar, GenTypeFloatVecNoScalar, Int], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecAndScalar, GenTypeDoubleVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecNoScalar, GenTypeDoubleVecNoScalar, Int], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecAndScalar, GenTypeHalfVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecNoScalar, GenTypeHalfVecNoScalar, Int], Attr.Const>;
}
foreach name = ["pown", "rootn"] in {
  def : Builtin<name, [GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecAndScalar, GenTypeDoubleVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecAndScalar, GenTypeHalfVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
}

// --- 3 arguments ---
foreach name = ["fma", "mad"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN, FGenTypeN, FGenTypeN], Attr.Const>;
}

// The following math builtins take pointer arguments.  Which overloads are
// available depends on whether the generic address space feature is enabled.
multiclass MathWithPointer<list<AddressSpace> addrspaces> {
  foreach AS = addrspaces in {
    foreach name = ["fract", "modf", "sincos"] in {
      def : Builtin<name, [FGenTypeN, FGenTypeN, PointerType<FGenTypeN, AS>]>;
    }
    foreach name = ["frexp", "lgamma_r"] in {
      foreach Type = [GenTypeFloatVecAndScalar, GenTypeDoubleVecAndScalar, GenTypeHalfVecAndScalar] in {
        def : Builtin<name, [Type, Type, PointerType<GenTypeIntVecAndScalar, AS>]>;
      }
    }
    foreach name = ["remquo"] in {
      foreach Type = [GenTypeFloatVecAndScalar, GenTypeDoubleVecAndScalar, GenTypeHalfVecAndScalar] in {
        def : Builtin<name, [Type, Type, Type, PointerType<GenTypeIntVecAndScalar, AS>]>;
      }
    }
  }
}

let Extension = FuncExtOpenCLCNamedAddressSpaceBuiltins in {
  defm : MathWithPointer<[GlobalAS, LocalAS, PrivateAS]>;
}
let Extension = FuncExtOpenCLCGenericAddressSpace in {
  defm : MathWithPointer<[GenericAS]>;
}

// --- Table 9 ---
foreach name = ["half_cos",
                "half_exp", "half_exp2", "half_exp10",
                "half_log", "half_log2", "half_log10",
                "half_recip", "half_rsqrt",
                "half_sin", "half_sqrt", "half_tan",
                "native_cos",
                "native_exp", "native_exp2", "native_exp10",
                "native_log", "native_log2", "native_log10",
                "native_recip", "native_rsqrt",
                "native_sin", "native_sqrt", "native_tan"] in {
  def : Builtin<name, [GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar], Attr.Const>;
}
foreach name = ["half_divide", "half_powr",
                "native_divide", "native_powr"] in {
  def : Builtin<name, [GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar], Attr.Const>;
}

//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.3, v1.2 s6.12.3, v2.0 s6.13.3 - Integer Functions
// --- Table 10 ---
// --- 1 argument ---
foreach name = ["abs"] in {
  def : Builtin<name, [AI2UGenTypeN, AIGenTypeN], Attr.Const>;
}
def : Builtin<"clz", [AIGenTypeN, AIGenTypeN], Attr.Const>;
let MinVersion = CL12 in {
  def : Builtin<"popcount", [AIGenTypeN, AIGenTypeN], Attr.Const>;
}
let MinVersion = CL20 in {
  foreach name = ["ctz"] in {
    def : Builtin<name, [AIGenTypeN, AIGenTypeN], Attr.Const>;
  }
}

// --- 2 arguments ---
foreach name = ["abs_diff"] in {
  def : Builtin<name, [AI2UGenTypeN, AIGenTypeN, AIGenTypeN], Attr.Const>;
}
foreach name = ["add_sat", "hadd", "rhadd", "mul_hi", "rotate", "sub_sat"] in {
  def : Builtin<name, [AIGenTypeN, AIGenTypeN, AIGenTypeN], Attr.Const>;
}
foreach name = ["max", "min"] in {
  def : Builtin<name, [AIGenTypeN, AIGenTypeN, AIGenTypeN], Attr.Const>;
  def : Builtin<name, [AIGenTypeNNoScalar, AIGenTypeNNoScalar, AIGenType1], Attr.Const>;
}
foreach name = ["upsample"] in {
  def : Builtin<name, [GenTypeShortVecAndScalar, GenTypeCharVecAndScalar, GenTypeUCharVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeUShortVecAndScalar, GenTypeUCharVecAndScalar, GenTypeUCharVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeIntVecAndScalar, GenTypeShortVecAndScalar, GenTypeUShortVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeUIntVecAndScalar, GenTypeUShortVecAndScalar, GenTypeUShortVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeLongVecAndScalar, GenTypeIntVecAndScalar, GenTypeUIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeULongVecAndScalar, GenTypeUIntVecAndScalar, GenTypeUIntVecAndScalar], Attr.Const>;
}

// --- 3 arguments ---
foreach name = ["clamp"] in {
  def : Builtin<name, [AIGenTypeN, AIGenTypeN, AIGenTypeN, AIGenTypeN], Attr.Const>;
  def : Builtin<name, [AIGenTypeNNoScalar, AIGenTypeNNoScalar, AIGenType1, AIGenType1], Attr.Const>;
}
foreach name = ["mad_hi", "mad_sat"] in {
  def : Builtin<name, [AIGenTypeN, AIGenTypeN, AIGenTypeN, AIGenTypeN], Attr.Const>;
}

// --- Table 11 ---
foreach name = ["mad24"] in {
  def : Builtin<name, [GenTypeIntVecAndScalar, GenTypeIntVecAndScalar, GenTypeIntVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeUIntVecAndScalar, GenTypeUIntVecAndScalar, GenTypeUIntVecAndScalar, GenTypeUIntVecAndScalar], Attr.Const>;
}
foreach name = ["mul24"] in {
  def : Builtin<name, [GenTypeIntVecAndScalar, GenTypeIntVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeUIntVecAndScalar, GenTypeUIntVecAndScalar, GenTypeUIntVecAndScalar], Attr.Const>;
}

//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.4, v1.2 s6.12.4, v2.0 s6.13.4 - Common Functions
// OpenCL Extension v2.0 s5.1.3 and s6.1.3 - Common Functions
// --- Table 12 ---
// --- 1 argument ---
foreach name = ["degrees", "radians", "sign"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN], Attr.Const>;
}

// --- 2 arguments ---
foreach name = ["max", "min"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN, FGenTypeN], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecNoScalar, GenTypeFloatVecNoScalar, Float], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecNoScalar, GenTypeDoubleVecNoScalar, Double], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecNoScalar, GenTypeHalfVecNoScalar, Half], Attr.Const>;
}
foreach name = ["step"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN, FGenTypeN], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecNoScalar, Float, GenTypeFloatVecNoScalar], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecNoScalar, Double, GenTypeDoubleVecNoScalar], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecNoScalar, Half, GenTypeHalfVecNoScalar], Attr.Const>;
}

// --- 3 arguments ---
foreach name = ["clamp"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN, FGenTypeN, FGenTypeN], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecNoScalar, GenTypeFloatVecNoScalar, Float, Float], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecNoScalar, GenTypeDoubleVecNoScalar, Double, Double], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecNoScalar, GenTypeHalfVecNoScalar, Half, Half], Attr.Const>;
}
foreach name = ["mix"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN, FGenTypeN, FGenTypeN], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecNoScalar, GenTypeFloatVecNoScalar, GenTypeFloatVecNoScalar, Float], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecNoScalar, GenTypeDoubleVecNoScalar, GenTypeDoubleVecNoScalar, Double], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecNoScalar, GenTypeHalfVecNoScalar, GenTypeHalfVecNoScalar, Half], Attr.Const>;
}
foreach name = ["smoothstep"] in {
  def : Builtin<name, [FGenTypeN, FGenTypeN, FGenTypeN, FGenTypeN], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecNoScalar, Float, Float, GenTypeFloatVecNoScalar], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecNoScalar, Double, Double, GenTypeDoubleVecNoScalar], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecNoScalar, Half, Half, GenTypeHalfVecNoScalar], Attr.Const>;
}


//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.5, v1.2 s6.12.5, v2.0 s6.13.5 - Geometric Functions
// OpenCL Extension v2.0 s5.1.4 and s6.1.4 - Geometric Functions
// --- Table 13 ---
// --- 1 argument ---
foreach name = ["length"] in {
  def : Builtin<name, [Float, GenTypeFloatVec1234], Attr.Const>;
  def : Builtin<name, [Double, GenTypeDoubleVec1234], Attr.Const>;
  def : Builtin<name, [Half, GenTypeHalfVec1234], Attr.Const>;
}
foreach name = ["normalize"] in {
  def : Builtin<name, [GenTypeFloatVec1234, GenTypeFloatVec1234], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVec1234, GenTypeDoubleVec1234], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVec1234, GenTypeHalfVec1234], Attr.Const>;
}
foreach name = ["fast_length"] in {
  def : Builtin<name, [Float, GenTypeFloatVec1234], Attr.Const>;
}
foreach name = ["fast_normalize"] in {
  def : Builtin<name, [GenTypeFloatVec1234, GenTypeFloatVec1234], Attr.Const>;
}

// --- 2 arguments ---
foreach name = ["cross"] in {
  foreach VSize = [3, 4] in {
    def : Builtin<name, [VectorType<Float, VSize>, VectorType<Float, VSize>, VectorType<Float, VSize>], Attr.Const>;
    def : Builtin<name, [VectorType<Double, VSize>, VectorType<Double, VSize>, VectorType<Double, VSize>], Attr.Const>;
    def : Builtin<name, [VectorType<Half, VSize>, VectorType<Half, VSize>, VectorType<Half, VSize>], Attr.Const>;
  }
}
foreach name = ["dot", "distance"] in {
  def : Builtin<name, [Float, GenTypeFloatVec1234, GenTypeFloatVec1234], Attr.Const>;
  def : Builtin<name, [Double, GenTypeDoubleVec1234, GenTypeDoubleVec1234], Attr.Const>;
  def : Builtin<name, [Half, GenTypeHalfVec1234, GenTypeHalfVec1234], Attr.Const>;
}
foreach name = ["fast_distance"] in {
  def : Builtin<name, [Float, GenTypeFloatVec1234, GenTypeFloatVec1234], Attr.Const>;
}


//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.6, v1.2 s6.12.6, v2.0 s6.13.6 - Relational Functions
// OpenCL Extension v2.0 s5.1.5 and s6.1.5 - Relational Functions
// --- Table 14 ---
// --- 1 argument ---
foreach name = ["isfinite", "isinf", "isnan", "isnormal", "signbit"] in {
  def : Builtin<name, [GenTypeIntVecAndScalar, GenTypeFloatVecAndScalar], Attr.Const>;
  def : Builtin<name, [Int, Double], Attr.Const>;
  def : Builtin<name, [GenTypeLongVecNoScalar, GenTypeDoubleVecNoScalar], Attr.Const>;
  def : Builtin<name, [Int, Half], Attr.Const>;
  def : Builtin<name, [GenTypeShortVecNoScalar, GenTypeHalfVecNoScalar], Attr.Const>;
}
foreach name = ["any", "all"] in {
  def : Builtin<name, [Int, SGenTypeN], Attr.Const>;
}

// --- 2 arguments ---
foreach name = ["isequal", "isnotequal", "isgreater", "isgreaterequal",
                "isless", "islessequal", "islessgreater", "isordered",
                "isunordered"] in {
  def : Builtin<name, [GenTypeIntVecAndScalar, GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar], Attr.Const>;
  def : Builtin<name, [Int, Double, Double], Attr.Const>;
  def : Builtin<name, [GenTypeLongVecNoScalar, GenTypeDoubleVecNoScalar, GenTypeDoubleVecNoScalar], Attr.Const>;
  def : Builtin<name, [Int, Half, Half], Attr.Const>;
  def : Builtin<name, [GenTypeShortVecNoScalar, GenTypeHalfVecNoScalar, GenTypeHalfVecNoScalar], Attr.Const>;
}

// --- 3 arguments ---
foreach name = ["bitselect"] in {
  def : Builtin<name, [AGenTypeN, AGenTypeN, AGenTypeN, AGenTypeN], Attr.Const>;
}
foreach name = ["select"] in {
  def : Builtin<name, [SGenTypeN, SGenTypeN, SGenTypeN, SGenTypeN], Attr.Const>;
  def : Builtin<name, [SGenTypeN, SGenTypeN, SGenTypeN, UGenTypeN], Attr.Const>;
  def : Builtin<name, [UGenTypeN, UGenTypeN, UGenTypeN, UGenTypeN], Attr.Const>;
  def : Builtin<name, [UGenTypeN, UGenTypeN, UGenTypeN, SGenTypeN], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar, GenTypeIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar, GenTypeFloatVecAndScalar, GenTypeUIntVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecAndScalar, GenTypeDoubleVecAndScalar, GenTypeDoubleVecAndScalar, GenTypeLongVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeDoubleVecAndScalar, GenTypeDoubleVecAndScalar, GenTypeDoubleVecAndScalar, GenTypeULongVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecAndScalar, GenTypeHalfVecAndScalar, GenTypeHalfVecAndScalar, GenTypeShortVecAndScalar], Attr.Const>;
  def : Builtin<name, [GenTypeHalfVecAndScalar, GenTypeHalfVecAndScalar, GenTypeHalfVecAndScalar, GenTypeUShortVecAndScalar], Attr.Const>;
}


//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.7, v1.2 s6.12.7, v2.0 s6.13.7 - Vector Data Load and Store Functions
// OpenCL Extension v1.1 s9.3.6 and s9.6.6, v1.2 s9.5.6, v2.0 s5.1.6 and s6.1.6 - Vector Data Load and Store Functions
// --- Table 15 ---
multiclass VloadVstore<list<AddressSpace> addrspaces, bit defStores> {
  foreach AS = addrspaces in {
    foreach VSize = [2, 3, 4, 8, 16] in {
      foreach name = ["vload" # VSize] in {
        def : Builtin<name, [VectorType<Char, VSize>, Size, PointerType<ConstType<Char>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<UChar, VSize>, Size, PointerType<ConstType<UChar>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<Short, VSize>, Size, PointerType<ConstType<Short>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<UShort, VSize>, Size, PointerType<ConstType<UShort>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<Int, VSize>, Size, PointerType<ConstType<Int>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<UInt, VSize>, Size, PointerType<ConstType<UInt>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<Long, VSize>, Size, PointerType<ConstType<Long>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<ULong, VSize>, Size, PointerType<ConstType<ULong>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<Float, VSize>, Size, PointerType<ConstType<Float>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<Double, VSize>, Size, PointerType<ConstType<Double>, AS>], Attr.Pure>;
        def : Builtin<name, [VectorType<Half, VSize>, Size, PointerType<ConstType<Half>, AS>], Attr.Pure>;
      }
      if defStores then {
        foreach name = ["vstore" # VSize] in {
          def : Builtin<name, [Void, VectorType<Char, VSize>, Size, PointerType<Char, AS>]>;
          def : Builtin<name, [Void, VectorType<UChar, VSize>, Size, PointerType<UChar, AS>]>;
          def : Builtin<name, [Void, VectorType<Short, VSize>, Size, PointerType<Short, AS>]>;
          def : Builtin<name, [Void, VectorType<UShort, VSize>, Size, PointerType<UShort, AS>]>;
          def : Builtin<name, [Void, VectorType<Int, VSize>, Size, PointerType<Int, AS>]>;
          def : Builtin<name, [Void, VectorType<UInt, VSize>, Size, PointerType<UInt, AS>]>;
          def : Builtin<name, [Void, VectorType<Long, VSize>, Size, PointerType<Long, AS>]>;
          def : Builtin<name, [Void, VectorType<ULong, VSize>, Size, PointerType<ULong, AS>]>;
          def : Builtin<name, [Void, VectorType<Float, VSize>, Size, PointerType<Float, AS>]>;
          def : Builtin<name, [Void, VectorType<Double, VSize>, Size, PointerType<Double, AS>]>;
          def : Builtin<name, [Void, VectorType<Half, VSize>, Size, PointerType<Half, AS>]>;
        }
      }
    }
  }
}

let Extension = FuncExtOpenCLCNamedAddressSpaceBuiltins in {
  defm : VloadVstore<[GlobalAS, LocalAS, PrivateAS], 1>;
}
let Extension = FuncExtOpenCLCGenericAddressSpace in {
  defm : VloadVstore<[GenericAS], 1>;
}
// vload with constant address space is available regardless of version.
defm : VloadVstore<[ConstantAS], 0>;

multiclass VloadVstoreHalf<list<AddressSpace> addrspaces, bit defStores> {
  foreach AS = addrspaces in {
    def : Builtin<"vload_half", [Float, Size, !cast<Type>("HalfPtrConst" # AS)], Attr.Pure>;
    foreach VSize = [2, 3, 4, 8, 16] in {
      foreach name = ["vload_half" # VSize, "vloada_half" # VSize] in {
        def : Builtin<name, [VectorType<Float, VSize>, Size, !cast<Type>("HalfPtrConst" # AS)], Attr.Pure>;
      }
    }
    if defStores then {
      foreach rnd = ["", "_rte", "_rtz", "_rtp", "_rtn"] in {
        foreach name = ["vstore_half" # rnd] in {
          def : Builtin<name, [Void, Float, Size, !cast<Type>("HalfPtr" # AS)]>;
          def : Builtin<name, [Void, Double, Size, !cast<Type>("HalfPtr" # AS)]>;
        }
        foreach VSize = [2, 3, 4, 8, 16] in {
          foreach name = ["vstore_half" # VSize # rnd, "vstorea_half" # VSize # rnd] in {
            def : Builtin<name, [Void, VectorType<Float, VSize>, Size, !cast<Type>("HalfPtr" # AS)]>;
            def : Builtin<name, [Void, VectorType<Double, VSize>, Size, !cast<Type>("HalfPtr" # AS)]>;
          }
        }
      }
    }
  }
}

let Extension = FuncExtOpenCLCNamedAddressSpaceBuiltins in {
  defm : VloadVstoreHalf<[GlobalAS, LocalAS, PrivateAS], 1>;
}
let Extension = FuncExtOpenCLCGenericAddressSpace in {
  defm : VloadVstoreHalf<[GenericAS], 1>;
}
// vload_half and vloada_half with constant address space are available regardless of version.
defm : VloadVstoreHalf<[ConstantAS], 0>;

// OpenCL v3.0 s6.15.8 - Synchronization Functions.
def : Builtin<"barrier", [Void, MemFenceFlags], Attr.Convergent>;
let MinVersion = CL20 in {
  def : Builtin<"work_group_barrier", [Void, MemFenceFlags], Attr.Convergent>;
  def : Builtin<"work_group_barrier", [Void, MemFenceFlags, MemoryScope], Attr.Convergent>;
}

// OpenCL v3.0 s6.15.9 - Legacy Explicit Memory Fence Functions.
def : Builtin<"mem_fence", [Void, MemFenceFlags]>;
def : Builtin<"read_mem_fence", [Void, MemFenceFlags]>;
def : Builtin<"write_mem_fence", [Void, MemFenceFlags]>;

// OpenCL v3.0 s6.15.10 - Address Space Qualifier Functions.
// to_global, to_local, to_private are declared in Builtins.td.

let Extension = FuncExtOpenCLCGenericAddressSpace in {
  // The OpenCL 3.0 specification defines these with a "gentype" argument indicating any builtin
  // type or user-defined type, which cannot be represented currently.  Hence we slightly diverge
  // by providing only the following overloads with a void pointer.
  def : Builtin<"get_fence", [MemFenceFlags, PointerType<Void, GenericAS>]>;
  def : Builtin<"get_fence", [MemFenceFlags, PointerType<ConstType<Void>, GenericAS>]>;
}

//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.10, v1.2 s6.12.10, v2.0 s6.13.10: Async Copies from Global to Local Memory, Local to Global Memory, and Prefetch
// OpenCL Extension v2.0 s5.1.7 and s6.1.7: Async Copies from Global to Local Memory, Local to Global Memory, and Prefetch
// --- Table 18 ---
foreach name = ["async_work_group_copy"] in {
  def : Builtin<name, [Event, PointerType<AGenTypeN, LocalAS>, PointerType<ConstType<AGenTypeN>, GlobalAS>, Size, Event]>;
  def : Builtin<name, [Event, PointerType<AGenTypeN, GlobalAS>, PointerType<ConstType<AGenTypeN>, LocalAS>, Size, Event]>;
}
foreach name = ["async_work_group_strided_copy"] in {
  def : Builtin<name, [Event, PointerType<AGenTypeN, LocalAS>, PointerType<ConstType<AGenTypeN>, GlobalAS>, Size, Size, Event]>;
  def : Builtin<name, [Event, PointerType<AGenTypeN, GlobalAS>, PointerType<ConstType<AGenTypeN>, LocalAS>, Size, Size, Event]>;
}
foreach name = ["wait_group_events"] in {
  def : Builtin<name, [Void, Int, PointerType<Event, GenericAS>]>;
}
foreach name = ["prefetch"] in {
  def : Builtin<name, [Void, PointerType<ConstType<AGenTypeN>, GlobalAS>, Size]>;
}

//--------------------------------------------------------------------
// OpenCL v2.0 s6.13.11 - Atomics Functions.
// Functions that use memory_order and cl_mem_fence_flags enums are not
// declared here as the TableGen backend does not handle enums.

// OpenCL v1.0 s9.5, s9.6, s9.7 - Atomic Functions for 32-bit integers
// --- Table 9.1 ---
let Extension = FuncExtKhrGlobalInt32BaseAtomics in {
  foreach Type = [Int, UInt] in {
    foreach name = ["atom_add", "atom_sub", "atom_xchg"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, GlobalAS>, Type]>;
    }
    foreach name = ["atom_inc", "atom_dec"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, GlobalAS>]>;
    }
    foreach name = ["atom_cmpxchg"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, GlobalAS>, Type, Type]>;
    }
  }
}
// --- Table 9.3 ---
let Extension = FuncExtKhrLocalInt32BaseAtomics in {
  foreach Type = [Int, UInt] in {
    foreach name = ["atom_add", "atom_sub", "atom_xchg"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, LocalAS>, Type]>;
    }
    foreach name = ["atom_inc", "atom_dec"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, LocalAS>]>;
    }
    foreach name = ["atom_cmpxchg"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, LocalAS>, Type, Type]>;
    }
  }
}
// --- Table 9.5 ---
let Extension = FuncExtKhrInt64BaseAtomics in {
  foreach AS = [GlobalAS, LocalAS] in {
    foreach Type = [Long, ULong] in {
      foreach name = ["atom_add", "atom_sub", "atom_xchg"] in {
        def : Builtin<name, [Type, PointerType<VolatileType<Type>, AS>, Type]>;
      }
      foreach name = ["atom_inc", "atom_dec"] in {
        def : Builtin<name, [Type, PointerType<VolatileType<Type>, AS>]>;
      }
      foreach name = ["atom_cmpxchg"] in {
        def : Builtin<name, [Type, PointerType<VolatileType<Type>, AS>, Type, Type]>;
      }
    }
  }
}
// --- Table 9.2 ---
let Extension = FuncExtKhrGlobalInt32ExtendedAtomics in {
  foreach Type = [Int, UInt] in {
    foreach name = ["atom_min", "atom_max", "atom_and",
                    "atom_or", "atom_xor"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, GlobalAS>, Type]>;
    }
  }
}
// --- Table 9.4 ---
let Extension = FuncExtKhrLocalInt32ExtendedAtomics in {
  foreach Type = [Int, UInt] in {
    foreach name = ["atom_min", "atom_max", "atom_and",
                    "atom_or", "atom_xor"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, LocalAS>, Type]>;
    }
  }
}
// --- Table 9.6 ---
let Extension = FuncExtKhrInt64ExtendedAtomics in {
  foreach AS = [GlobalAS, LocalAS] in {
    foreach Type = [Long, ULong] in {
      foreach name = ["atom_min", "atom_max", "atom_and",
                      "atom_or", "atom_xor"] in {
        def : Builtin<name, [Type, PointerType<VolatileType<Type>, AS>, Type]>;
      }
    }
  }
}
// OpenCL v1.1 s6.11.1, v1.2 s6.12.11 - Atomic Functions
foreach AS = [GlobalAS, LocalAS] in {
  def : Builtin<"atomic_xchg", [Float, PointerType<VolatileType<Float>, AS>, Float]>;
  foreach Type = [Int, UInt] in {
    foreach name = ["atomic_add", "atomic_sub", "atomic_xchg",
                    "atomic_min", "atomic_max", "atomic_and",
                    "atomic_or", "atomic_xor"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, AS>, Type]>;
    }
    foreach name = ["atomic_inc", "atomic_dec"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, AS>]>;
    }
    foreach name = ["atomic_cmpxchg"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, AS>, Type, Type]>;
    }
  }
}

let Extension = FuncExtOpenCLCxx in {
  foreach Type = [Int, UInt] in {
    foreach name = ["atomic_add", "atomic_sub", "atomic_xchg",
                    "atomic_min", "atomic_max", "atomic_and",
                    "atomic_or", "atomic_xor"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, GenericAS>, Type]>;
    }
    foreach name = ["atomic_inc", "atomic_dec"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, GenericAS>]>;
    }
    foreach name = ["atomic_cmpxchg"] in {
      def : Builtin<name, [Type, PointerType<VolatileType<Type>, GenericAS>, Type, Type]>;
    }
  }
}

// OpenCL v2.0 s6.13.11 - Atomic Functions.

// An atomic builtin with 2 additional _explicit variants.
multiclass BuiltinAtomicExplicit<string Name, list<Type> Types, FunctionExtension BaseExt> {
  // Without explicit MemoryOrder or MemoryScope.
  let Extension = concatExtension<BaseExt, "__opencl_c_atomic_order_seq_cst __opencl_c_atomic_scope_device">.ret in {
    def : Builtin<Name, Types>;
  }

  // With an explicit MemoryOrder argument.
  let Extension = concatExtension<BaseExt, "__opencl_c_atomic_scope_device">.ret in {
    def : Builtin<Name # "_explicit", !listconcat(Types, [MemoryOrder])>;
  }

  // With explicit MemoryOrder and MemoryScope arguments.
  let Extension = BaseExt in {
    def : Builtin<Name # "_explicit", !listconcat(Types, [MemoryOrder, MemoryScope])>;
  }
}

// OpenCL 2.0 atomic functions that have a pointer argument in a given address space.
multiclass OpenCL2Atomics<AddressSpace addrspace, FunctionExtension BaseExt> {
  foreach TypePair = [[AtomicInt, Int], [AtomicUInt, UInt],
                      [AtomicLong, Long], [AtomicULong, ULong],
                      [AtomicFloat, Float], [AtomicDouble, Double]] in {
    let Extension = BaseExt in {
      def : Builtin<"atomic_init",
          [Void, PointerType<VolatileType<TypePair[0]>, addrspace>, TypePair[1]]>;
    }
    defm : BuiltinAtomicExplicit<"atomic_store",
        [Void, PointerType<VolatileType<TypePair[0]>, addrspace>, TypePair[1]], BaseExt>;
    defm : BuiltinAtomicExplicit<"atomic_load",
        [TypePair[1], PointerType<VolatileType<TypePair[0]>, addrspace>], BaseExt>;
    defm : BuiltinAtomicExplicit<"atomic_exchange",
        [TypePair[1], PointerType<VolatileType<TypePair[0]>, addrspace>, TypePair[1]], BaseExt>;
    foreach Variant = ["weak", "strong"] in {
      foreach exp_ptr_addrspace = !cond(
            !eq(BaseExt, FuncExtOpenCLCGenericAddressSpace): [GenericAS],
            !eq(BaseExt, FuncExtOpenCLCNamedAddressSpaceBuiltins): [GlobalAS, LocalAS, PrivateAS])
          in {
        let Extension = concatExtension<BaseExt, "__opencl_c_atomic_order_seq_cst __opencl_c_atomic_scope_device">.ret in {
          def : Builtin<"atomic_compare_exchange_" # Variant,
              [Bool, PointerType<VolatileType<TypePair[0]>, addrspace>,
               PointerType<TypePair[1], exp_ptr_addrspace>, TypePair[1]]>;
        }
        let Extension = concatExtension<BaseExt, "__opencl_c_atomic_scope_device">.ret in {
          def : Builtin<"atomic_compare_exchange_" # Variant # "_explicit",
              [Bool, PointerType<VolatileType<TypePair[0]>, addrspace>,
               PointerType<TypePair[1], exp_ptr_addrspace>, TypePair[1], MemoryOrder, MemoryOrder]>;
        }
        let Extension = BaseExt in {
          def : Builtin<"atomic_compare_exchange_" # Variant # "_explicit",
              [Bool, PointerType<VolatileType<TypePair[0]>, addrspace>,
               PointerType<TypePair[1], exp_ptr_addrspace>, TypePair[1], MemoryOrder, MemoryOrder, MemoryScope]>;
        }
      }
    }
  }

  foreach TypePair = [[AtomicInt, Int, Int], [AtomicUInt, UInt, UInt],
                      [AtomicLong, Long, Long], [AtomicULong, ULong, ULong],
                      [AtomicUIntPtr, UIntPtr, PtrDiff]] in {
    foreach ModOp = ["add", "sub"] in {
      defm : BuiltinAtomicExplicit<"atomic_fetch_" # ModOp,
          [TypePair[1], PointerType<VolatileType<TypePair[0]>, addrspace>, TypePair[2]], BaseExt>;
    }
  }
  foreach TypePair = [[AtomicInt, Int, Int], [AtomicUInt, UInt, UInt],
                      [AtomicLong, Long, Long], [AtomicULong, ULong, ULong]] in {
    foreach ModOp = ["or", "xor", "and", "min", "max"] in {
      defm : BuiltinAtomicExplicit<"atomic_fetch_" # ModOp,
          [TypePair[1], PointerType<VolatileType<TypePair[0]>, addrspace>, TypePair[2]], BaseExt>;
    }
  }

  defm : BuiltinAtomicExplicit<"atomic_flag_clear",
      [Void, PointerType<VolatileType<AtomicFlag>, addrspace>], BaseExt>;

  defm : BuiltinAtomicExplicit<"atomic_flag_test_and_set",
      [Bool, PointerType<VolatileType<AtomicFlag>, addrspace>], BaseExt>;
}

let MinVersion = CL20 in {
  def : Builtin<"atomic_work_item_fence", [Void, MemFenceFlags, MemoryOrder, MemoryScope]>;

  defm : OpenCL2Atomics<GenericAS, FuncExtOpenCLCGenericAddressSpace>;
  defm : OpenCL2Atomics<GlobalAS, FuncExtOpenCLCNamedAddressSpaceBuiltins>;
  defm : OpenCL2Atomics<LocalAS, FuncExtOpenCLCNamedAddressSpaceBuiltins>;
}

// The functionality added by cl_ext_float_atomics extension
let MinVersion = CL20 in {
  foreach addrspace = [GlobalAS, LocalAS, GenericAS] in {
    defvar extension_fp16 = !cast<FunctionExtension>("FuncExtFloatAtomicsFp16" # addrspace # "LoadStore");

    defm : BuiltinAtomicExplicit<"atomic_store",
        [Void, PointerType<VolatileType<AtomicHalf>, addrspace>, AtomicHalf], extension_fp16>;
    defm : BuiltinAtomicExplicit<"atomic_load",
        [Half, PointerType<VolatileType<AtomicHalf>, addrspace>], extension_fp16>;
    defm : BuiltinAtomicExplicit<"atomic_exchange",
        [Half, PointerType<VolatileType<AtomicHalf>, addrspace>, Half], extension_fp16>;

    foreach ModOp = ["add", "sub"] in {
      defvar extension_fp16 = !cast<FunctionExtension>("FuncExtFloatAtomicsFp16" # addrspace # "Add");
      defvar extension_fp32 = !cast<FunctionExtension>("FuncExtFloatAtomicsFp32" # addrspace # "Add");
      defvar extension_fp64 = !cast<FunctionExtension>("FuncExtFloatAtomicsFp64" # addrspace # "Add");

      defm : BuiltinAtomicExplicit<"atomic_fetch_" # ModOp,
          [Half, PointerType<VolatileType<AtomicHalf>, addrspace>, Half], extension_fp16>;
      defm : BuiltinAtomicExplicit<"atomic_fetch_" # ModOp,
          [Float, PointerType<VolatileType<AtomicFloat>, addrspace>, Float], extension_fp32>;
      defm : BuiltinAtomicExplicit<"atomic_fetch_" # ModOp,
          [Double, PointerType<VolatileType<AtomicDouble>, addrspace>, Double], extension_fp64>;
    }

    foreach ModOp = ["min", "max"] in {
      defvar extension_fp16 = !cast<FunctionExtension>("FuncExtFloatAtomicsFp16" # addrspace # "MinMax");
      defvar extension_fp32 = !cast<FunctionExtension>("FuncExtFloatAtomicsFp32" # addrspace # "MinMax");
      defvar extension_fp64 = !cast<FunctionExtension>("FuncExtFloatAtomicsFp64" # addrspace # "MinMax");

      defm : BuiltinAtomicExplicit<"atomic_fetch_" # ModOp,
          [Half, PointerType<VolatileType<AtomicHalf>, addrspace>, Half], extension_fp16>;
      defm : BuiltinAtomicExplicit<"atomic_fetch_" # ModOp,
          [Float, PointerType<VolatileType<AtomicFloat>, addrspace>, Float], extension_fp32>;
      defm : BuiltinAtomicExplicit<"atomic_fetch_" # ModOp,
          [Double, PointerType<VolatileType<AtomicDouble>, addrspace>, Double], extension_fp64>;
    }
  }
}

//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.12, v1.2 s6.12.12, v2.0 s6.13.12 - Miscellaneous Vector Functions
// --- Table 19 ---
foreach VSize1 = [2, 4, 8, 16] in {
  foreach VSize2 = [2, 4, 8, 16] in {
    foreach VecAndMaskType = [[Char, UChar], [UChar, UChar],
                              [Short, UShort], [UShort, UShort],
                              [Int, UInt], [UInt, UInt],
                              [Long, ULong], [ULong, ULong],
                              [Float, UInt], [Double, ULong], [Half, UShort]] in {
      def : Builtin<"shuffle", [VectorType<VecAndMaskType[0], VSize1>,
                                VectorType<VecAndMaskType[0], VSize2>,
                                VectorType<VecAndMaskType[1], VSize1>],
                               Attr.Const>;
    }
  }
}
foreach VSize1 = [2, 4, 8, 16] in {
  foreach VSize2 = [2, 4, 8, 16] in {
    foreach VecAndMaskType = [[Char, UChar], [UChar, UChar],
                              [Short, UShort], [UShort, UShort],
                              [Int, UInt], [UInt, UInt],
                              [Long, ULong], [ULong, ULong],
                              [Float, UInt], [Double, ULong], [Half, UShort]] in {
      def : Builtin<"shuffle2", [VectorType<VecAndMaskType[0], VSize1>,
                                 VectorType<VecAndMaskType[0], VSize2>,
                                 VectorType<VecAndMaskType[0], VSize2>,
                                 VectorType<VecAndMaskType[1], VSize1>],
                                Attr.Const>;
    }
  }
}

//--------------------------------------------------------------------
// OpenCL v1.1 s6.11.3, v1.2 s6.12.14, v2.0 s6.13.14: Image Read and Write Functions
// OpenCL Extension v2.0 s5.1.8 and s6.1.8: Image Read and Write Functions
// --- Table 22: Image Read Functions with Samplers ---
foreach imgTy = [Image1d] in {
  foreach coordTy = [Int, Float] in {
    def : Builtin<"read_imagef", [VectorType<Float, 4>, ImageType<imgTy, "RO">, Sampler, coordTy], Attr.Pure>;
    def : Builtin<"read_imagei", [VectorType<Int, 4>, ImageType<imgTy, "RO">, Sampler, coordTy], Attr.Pure>;
    def : Builtin<"read_imageui", [VectorType<UInt, 4>, ImageType<imgTy, "RO">, Sampler, coordTy], Attr.Pure>;
  }
}
foreach imgTy = [Image2d, Image1dArray] in {
  foreach coordTy = [Int, Float] in {
    def : Builtin<"read_imagef", [VectorType<Float, 4>, ImageType<imgTy, "RO">, Sampler, VectorType<coordTy, 2>], Attr.Pure>;
    def : Builtin<"read_imagei", [VectorType<Int, 4>, ImageType<imgTy, "RO">, Sampler, VectorType<coordTy, 2>], Attr.Pure>;
    def : Builtin<"read_imageui", [VectorType<UInt, 4>, ImageType<imgTy, "RO">, Sampler, VectorType<coordTy, 2>], Attr.Pure>;
  }
}
foreach imgTy = [Image3d, Image2dArray] in {
  foreach coordTy = [Int, Float] in {
    def : Builtin<"read_imagef", [VectorType<Float, 4>, ImageType<imgTy, "RO">, Sampler, VectorType<coordTy, 4>], Attr.Pure>;
    def : Builtin<"read_imagei", [VectorType<Int, 4>, ImageType<imgTy, "RO">, Sampler, VectorType<coordTy, 4>], Attr.Pure>;
    def : Builtin<"read_imageui", [VectorType<UInt, 4>, ImageType<imgTy, "RO">, Sampler, VectorType<coordTy, 4>], Attr.Pure>;
  }
}
foreach coordTy = [Int, Float] in {
  def : Builtin<"read_imagef", [Float, ImageType<Image2dDepth, "RO">, Sampler, VectorType<coordTy, 2>], Attr.Pure>;
  def : Builtin<"read_imagef", [Float, ImageType<Image2dArrayDepth, "RO">, Sampler, VectorType<coordTy, 4>], Attr.Pure>;
}

// --- Table 23: Sampler-less Read Functions ---
multiclass ImageReadSamplerless<string aQual> {
  foreach imgTy = [Image2d, Image1dArray] in {
    def : Builtin<"read_imagef", [VectorType<Float, 4>, ImageType<imgTy, aQual>, VectorType<Int, 2>], Attr.Pure>;
    def : Builtin<"read_imagei", [VectorType<Int, 4>, ImageType<imgTy, aQual>, VectorType<Int, 2>], Attr.Pure>;
    def : Builtin<"read_imageui", [VectorType<UInt, 4>, ImageType<imgTy, aQual>, VectorType<Int, 2>], Attr.Pure>;
  }
  foreach imgTy = [Image3d, Image2dArray] in {
    def : Builtin<"read_imagef", [VectorType<Float, 4>, ImageType<imgTy, aQual>, VectorType<Int, 4>], Attr.Pure>;
    def : Builtin<"read_imagei", [VectorType<Int, 4>, ImageType<imgTy, aQual>, VectorType<Int, 4>], Attr.Pure>;
    def : Builtin<"read_imageui", [VectorType<UInt, 4>, ImageType<imgTy, aQual>, VectorType<Int, 4>], Attr.Pure>;
  }
  foreach imgTy = [Image1d, Image1dBuffer] in {
    def : Builtin<"read_imagef", [VectorType<Float, 4>, ImageType<imgTy, aQual>, Int], Attr.Pure>;
    def : Builtin<"read_imagei", [VectorType<Int, 4>, ImageType<imgTy, aQual>, Int], Attr.Pure>;
    def : Builtin<"read_imageui", [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Int], Attr.Pure>;
  }
  def : Builtin<"read_imagef", [Float, ImageType<Image2dDepth, aQual>, VectorType<Int, 2>], Attr.Pure>;
  def : Builtin<"read_imagef", [Float, ImageType<Image2dArrayDepth, aQual>, VectorType<Int, 4>], Attr.Pure>;
}

let MinVersion = CL12 in {
  defm : ImageReadSamplerless<"RO">;
  let Extension = FuncExtOpenCLCReadWriteImages in {
    defm : ImageReadSamplerless<"RW">;
  }
}

// --- Table 24: Image Write Functions ---
multiclass ImageWrite<string aQual> {
  foreach imgTy = [Image2d] in {
    def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, VectorType<Float, 4>]>;
    def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, VectorType<Int, 4>]>;
    def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, VectorType<UInt, 4>]>;
  }
  foreach imgTy = [Image2dArray] in {
    def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, VectorType<Float, 4>]>;
    def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, VectorType<Int, 4>]>;
    def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, VectorType<UInt, 4>]>;
  }
  foreach imgTy = [Image1d, Image1dBuffer] in {
    def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, Int, VectorType<Float, 4>]>;
    def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, Int, VectorType<Int, 4>]>;
    def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, Int, VectorType<UInt, 4>]>;
  }
  foreach imgTy = [Image1dArray] in {
    def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, VectorType<Float, 4>]>;
    def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, VectorType<Int, 4>]>;
    def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, VectorType<UInt, 4>]>;
  }
  foreach imgTy = [Image3d] in {
    def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, VectorType<Float, 4>]>;
    def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, VectorType<Int, 4>]>;
    def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, VectorType<UInt, 4>]>;
  }
  def : Builtin<"write_imagef", [Void, ImageType<Image2dDepth, aQual>, VectorType<Int, 2>, Float]>;
  def : Builtin<"write_imagef", [Void, ImageType<Image2dArrayDepth, aQual>, VectorType<Int, 4>, Float]>;
}

defm : ImageWrite<"WO">;
let Extension = FuncExtOpenCLCReadWriteImages in {
  defm : ImageWrite<"RW">;
}

// --- Table 25: Image Query Functions ---
multiclass ImageQuery<string aQual> {
  foreach imgTy = [Image1d, Image1dBuffer, Image2d, Image3d,
                   Image1dArray, Image2dArray, Image2dDepth,
                   Image2dArrayDepth] in {
    foreach name = ["get_image_width", "get_image_channel_data_type",
                    "get_image_channel_order"] in {
      def : Builtin<name, [Int, ImageType<imgTy, aQual>], Attr.Const>;
    }
  }
  foreach imgTy = [Image2d, Image3d, Image2dArray, Image2dDepth,
                   Image2dArrayDepth] in {
    def : Builtin<"get_image_height", [Int, ImageType<imgTy, aQual>], Attr.Const>;
  }
  def : Builtin<"get_image_depth", [Int, ImageType<Image3d, aQual>], Attr.Const>;
  foreach imgTy = [Image2d, Image2dArray, Image2dDepth,
                   Image2dArrayDepth] in {
    def : Builtin<"get_image_dim", [VectorType<Int, 2>, ImageType<imgTy, aQual>], Attr.Const>;
  }
  def : Builtin<"get_image_dim", [VectorType<Int, 4>, ImageType<Image3d, aQual>], Attr.Const>;
  foreach imgTy = [Image1dArray, Image2dArray, Image2dArrayDepth] in {
    def : Builtin<"get_image_array_size", [Size, ImageType<imgTy, aQual>], Attr.Const>;
  }
}

defm : ImageQuery<"RO">;
defm : ImageQuery<"WO">;
let Extension = FuncExtOpenCLCReadWriteImages in {
  defm : ImageQuery<"RW">;
}

// OpenCL extension v2.0 s5.1.9: Built-in Image Read Functions
// --- Table 8 ---
foreach aQual = ["RO"] in {
  foreach name = ["read_imageh"] in {
    foreach coordTy = [Int, Float] in {
      foreach imgTy = [Image2d, Image1dArray] in {
        def : Builtin<name, [VectorType<Half, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<coordTy, 2>], Attr.Pure>;
      }
      foreach imgTy = [Image3d, Image2dArray] in {
        def : Builtin<name, [VectorType<Half, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<coordTy, 4>], Attr.Pure>;
      }
      foreach imgTy = [Image1d] in {
        def : Builtin<name, [VectorType<Half, 4>, ImageType<imgTy, aQual>, Sampler, coordTy], Attr.Pure>;
      }
    }
  }
}
// OpenCL extension v2.0 s5.1.10: Built-in Image Sampler-less Read Functions
// --- Table 9 ---
let MinVersion = CL12 in {
  multiclass ImageReadHalf<string aQual> {
    foreach name = ["read_imageh"] in {
      foreach imgTy = [Image2d, Image1dArray] in {
        def : Builtin<name, [VectorType<Half, 4>, ImageType<imgTy, aQual>, VectorType<Int, 2>], Attr.Pure>;
      }
      foreach imgTy = [Image3d, Image2dArray] in {
        def : Builtin<name, [VectorType<Half, 4>, ImageType<imgTy, aQual>, VectorType<Int, 4>], Attr.Pure>;
      }
      foreach imgTy = [Image1d, Image1dBuffer] in {
        def : Builtin<name, [VectorType<Half, 4>, ImageType<imgTy, aQual>, Int], Attr.Pure>;
      }
    }
  }
  defm : ImageReadHalf<"RO">;
  let Extension = FuncExtOpenCLCReadWriteImages in {
    defm : ImageReadHalf<"RW">;
  }
}
// OpenCL extension v2.0 s5.1.11: Built-in Image Write Functions
// --- Table 10 ---
multiclass ImageWriteHalf<string aQual> {
  foreach name = ["write_imageh"] in {
    def : Builtin<name, [Void, ImageType<Image2d, aQual>, VectorType<Int, 2>, VectorType<Half, 4>]>;
    def : Builtin<name, [Void, ImageType<Image2dArray, aQual>, VectorType<Int, 4>, VectorType<Half, 4>]>;
    def : Builtin<name, [Void, ImageType<Image1d, aQual>, Int, VectorType<Half, 4>]>;
    def : Builtin<name, [Void, ImageType<Image1dBuffer, aQual>, Int, VectorType<Half, 4>]>;
    def : Builtin<name, [Void, ImageType<Image1dArray, aQual>, VectorType<Int, 2>, VectorType<Half, 4>]>;
    def : Builtin<name, [Void, ImageType<Image3d, aQual>, VectorType<Int, 4>, VectorType<Half, 4>]>;
  }
}

defm : ImageWriteHalf<"WO">;
let Extension = FuncExtOpenCLCReadWriteImages in {
  defm : ImageWriteHalf<"RW">;
}



//--------------------------------------------------------------------
// OpenCL v2.0 s6.13.15 - Work-group Functions
// --- Table 26 ---
let Extension = FuncExtOpenCLCWGCollectiveFunctions in {
  foreach name = ["work_group_all", "work_group_any"] in {
    def : Builtin<name, [Int, Int], Attr.Convergent>;
  }
  foreach name = ["work_group_broadcast"] in {
    def : Builtin<name, [IntLongFloatGenType1, IntLongFloatGenType1, Size], Attr.Convergent>;
    def : Builtin<name, [IntLongFloatGenType1, IntLongFloatGenType1, Size, Size], Attr.Convergent>;
    def : Builtin<name, [IntLongFloatGenType1, IntLongFloatGenType1, Size, Size, Size], Attr.Convergent>;
  }
  foreach op = ["add", "min", "max"] in {
    foreach name = ["work_group_reduce_", "work_group_scan_exclusive_",
                    "work_group_scan_inclusive_"] in {
      def : Builtin<name # op, [IntLongFloatGenType1, IntLongFloatGenType1], Attr.Convergent>;
    }
  }
}


//--------------------------------------------------------------------
// OpenCL2.0 : 6.13.16 : Pipe Functions
// --- Table 27 ---
// Defined in Builtins.td

// --- Table 28 ---
// Builtins taking pipe arguments are defined in Builtins.td
let Extension = FuncExtOpenCLCPipes in {
  def : Builtin<"is_valid_reserve_id", [Bool, ReserveId]>;
}

// --- Table 29 ---
// Defined in Builtins.td


//--------------------------------------------------------------------
// OpenCL2.0 : 6.13.17 : Enqueuing Kernels
// --- Table 30 ---
// Defined in Builtins.td

// --- Table 32 ---
// Defined in Builtins.td

// --- Table 33 ---
let Extension = FuncExtOpenCLCDeviceEnqueue in {
  def : Builtin<"enqueue_marker",
      [Int, Queue, UInt, PointerType<ConstType<ClkEvent>, GenericAS>, PointerType<ClkEvent, GenericAS>]>;

  // --- Table 34 ---
  def : Builtin<"retain_event", [Void, ClkEvent]>;
  def : Builtin<"release_event", [Void, ClkEvent]>;
  def : Builtin<"create_user_event", [ClkEvent]>;
  def : Builtin<"is_valid_event", [Bool, ClkEvent]>;
  def : Builtin<"set_user_event_status", [Void, ClkEvent, Int]>;
  def : Builtin<"capture_event_profiling_info",
      [Void, ClkEvent, ClkProfilingInfo, PointerType<Void, GlobalAS>]>;

  // --- Table 35 ---
  def : Builtin<"get_default_queue", [Queue]>;

  def : Builtin<"ndrange_1D", [NDRange, Size]>;
  def : Builtin<"ndrange_1D", [NDRange, Size, Size]>;
  def : Builtin<"ndrange_1D", [NDRange, Size, Size, Size]>;
  def : Builtin<"ndrange_2D", [NDRange, PointerType<ConstType<Size>, PrivateAS>]>;
  def : Builtin<"ndrange_2D", [NDRange, PointerType<ConstType<Size>, PrivateAS>,
                                        PointerType<ConstType<Size>, PrivateAS>]>;
  def : Builtin<"ndrange_2D", [NDRange, PointerType<ConstType<Size>, PrivateAS>,
                                        PointerType<ConstType<Size>, PrivateAS>,
                                        PointerType<ConstType<Size>, PrivateAS>]>;
  def : Builtin<"ndrange_3D", [NDRange, PointerType<ConstType<Size>, PrivateAS>]>;
  def : Builtin<"ndrange_3D", [NDRange, PointerType<ConstType<Size>, PrivateAS>,
                                        PointerType<ConstType<Size>, PrivateAS>]>;
  def : Builtin<"ndrange_3D", [NDRange, PointerType<ConstType<Size>, PrivateAS>,
                                        PointerType<ConstType<Size>, PrivateAS>,
                                        PointerType<ConstType<Size>, PrivateAS>]>;
}


//--------------------------------------------------------------------
// End of the builtin functions defined in the OpenCL C specification.
// Builtin functions defined in the OpenCL C Extension are below.
//--------------------------------------------------------------------


// OpenCL Extension v2.0 s9.18 - Mipmaps
let Extension = FuncExtKhrMipmapImage in {
  // Added to section 6.13.14.2.
  foreach aQual = ["RO"] in {
    foreach imgTy = [Image2d] in {
      foreach name = ["read_imagef"] in {
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, VectorType<Float, 2>, VectorType<Float, 2>], Attr.Pure>;
      }
      foreach name = ["read_imagei"] in {
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, VectorType<Float, 2>, VectorType<Float, 2>], Attr.Pure>;
      }
      foreach name = ["read_imageui"] in {
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, VectorType<Float, 2>, VectorType<Float, 2>], Attr.Pure>;
      }
    }
    foreach imgTy = [Image2dDepth] in {
      foreach name = ["read_imagef"] in {
        def : Builtin<name, [Float, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float], Attr.Pure>;
        def : Builtin<name, [Float, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, VectorType<Float, 2>, VectorType<Float, 2>], Attr.Pure>;
      }
    }
    foreach imgTy = [Image1d] in {
      foreach name = ["read_imagef"] in {
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, Float, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, Float, Float, Float], Attr.Pure>;
      }
      foreach name = ["read_imagei"] in {
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, Float, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, Float, Float, Float], Attr.Pure>;
      }
      foreach name = ["read_imageui"] in {
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, Float, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, Float, Float, Float], Attr.Pure>;
      }
    }
    foreach imgTy = [Image3d] in {
      foreach name = ["read_imagef"] in {
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, VectorType<Float, 4>, VectorType<Float, 4>], Attr.Pure>;
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, Float], Attr.Pure>;
      }
      foreach name = ["read_imagei"] in {
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, VectorType<Float, 4>, VectorType<Float, 4>], Attr.Pure>;
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, Float], Attr.Pure>;
      }
      foreach name = ["read_imageui"] in {
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, VectorType<Float, 4>, VectorType<Float, 4>], Attr.Pure>;
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, Float], Attr.Pure>;
      }
    }
    foreach imgTy = [Image1dArray] in {
      foreach name = ["read_imagef"] in {
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float, Float], Attr.Pure>;
      }
      foreach name = ["read_imagei"] in {
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float, Float], Attr.Pure>;
      }
      foreach name = ["read_imageui"] in {
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 2>, Float, Float], Attr.Pure>;
      }
    }
    foreach imgTy = [Image2dArray] in {
      foreach name = ["read_imagef"] in {
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<Float, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, VectorType<Float, 2>, VectorType<Float, 2>], Attr.Pure>;
      }
      foreach name = ["read_imagei"] in {
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<Int, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, VectorType<Float, 2>, VectorType<Float, 2>], Attr.Pure>;
      }
      foreach name = ["read_imageui"] in {
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, Float], Attr.Pure>;
        def : Builtin<name, [VectorType<UInt, 4>, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, VectorType<Float, 2>, VectorType<Float, 2>], Attr.Pure>;
      }
    }
    foreach imgTy = [Image2dArrayDepth] in {
      foreach name = ["read_imagef"] in {
        def : Builtin<name, [Float, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, Float], Attr.Pure>;
        def : Builtin<name, [Float, ImageType<imgTy, aQual>, Sampler, VectorType<Float, 4>, VectorType<Float, 2>, VectorType<Float, 2>], Attr.Pure>;
      }
    }
  }
}

// Added to section 6.13.14.5
multiclass ImageQueryNumMipLevels<string aQual> {
  foreach imgTy = [Image1d, Image2d, Image3d, Image1dArray, Image2dArray, Image2dDepth, Image2dArrayDepth] in {
    def : Builtin<"get_image_num_mip_levels", [Int, ImageType<imgTy, aQual>]>;
  }
}

let Extension = FuncExtKhrMipmapImage in {
  defm : ImageQueryNumMipLevels<"RO">;
  defm : ImageQueryNumMipLevels<"WO">;
  defm : ImageQueryNumMipLevels<"RW">;
}

// Write functions are enabled using a separate extension.
let Extension = FuncExtKhrMipmapImageWrites in {
  // Added to section 6.13.14.4.
  foreach aQual = ["WO"] in {
    foreach imgTy = [Image2d] in {
      def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int, VectorType<Float, 4>]>;
      def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int, VectorType<Int, 4>]>;
      def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int, VectorType<UInt, 4>]>;
    }
    def : Builtin<"write_imagef", [Void, ImageType<Image2dDepth, aQual>, VectorType<Int, 2>, Int, Float]>;
    foreach imgTy = [Image1d] in {
      def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, Int, Int, VectorType<Float, 4>]>;
      def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, Int, Int, VectorType<Int, 4>]>;
      def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, Int, Int, VectorType<UInt, 4>]>;
    }
    foreach imgTy = [Image1dArray] in {
      def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int, VectorType<Float, 4>]>;
      def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int, VectorType<Int, 4>]>;
      def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int, VectorType<UInt, 4>]>;
    }
    foreach imgTy = [Image2dArray] in {
      def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int, VectorType<Float, 4>]>;
      def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int, VectorType<Int, 4>]>;
      def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int, VectorType<UInt, 4>]>;
    }
    def : Builtin<"write_imagef", [Void, ImageType<Image2dArrayDepth, aQual>, VectorType<Int, 4>, Int, Float]>;
    foreach imgTy = [Image3d] in {
      def : Builtin<"write_imagef", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int, VectorType<Float, 4>]>;
      def : Builtin<"write_imagei", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int, VectorType<Int, 4>]>;
      def : Builtin<"write_imageui", [Void, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int, VectorType<UInt, 4>]>;
    }
  }
}

//--------------------------------------------------------------------
// OpenCL Extension v2.0 s18.3 - Creating OpenCL Memory Objects from OpenGL MSAA Textures
// --- Table 6.13.14.3 ---
multiclass ImageReadMsaa<string aQual> {
  foreach imgTy = [Image2dMsaa] in {
    def : Builtin<"read_imagef", [VectorType<Float, 4>, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int], Attr.Pure>;
    def : Builtin<"read_imagei", [VectorType<Int, 4>, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int], Attr.Pure>;
    def : Builtin<"read_imageui", [VectorType<UInt, 4>, ImageType<imgTy, aQual>, VectorType<Int, 2>, Int], Attr.Pure>;
  }
  foreach imgTy = [Image2dArrayMsaa] in {
    def : Builtin<"read_imagef", [VectorType<Float, 4>, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int], Attr.Pure>;
    def : Builtin<"read_imagei", [VectorType<Int, 4>, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int], Attr.Pure>;
    def : Builtin<"read_imageui", [VectorType<UInt, 4>, ImageType<imgTy, aQual>, VectorType<Int, 4>, Int], Attr.Pure>;
  }
  foreach name = ["read_imagef"] in {
    def : Builtin<name, [Float, ImageType<Image2dMsaaDepth, aQual>, VectorType<Int, 2>, Int], Attr.Pure>;
    def : Builtin<name, [Float, ImageType<Image2dArrayMsaaDepth, aQual>, VectorType<Int, 4>, Int], Attr.Pure>;
  }
}

// --- Table 6.13.14.5 ---
multiclass ImageQueryMsaa<string aQual> {
  foreach imgTy = [Image2dMsaa, Image2dArrayMsaa, Image2dMsaaDepth, Image2dArrayMsaaDepth] in {
    foreach name = ["get_image_width", "get_image_height",
                    "get_image_channel_data_type", "get_image_channel_order",
                    "get_image_num_samples"] in {
      def : Builtin<name, [Int, ImageType<imgTy, aQual>], Attr.Const>;
    }
    def : Builtin<"get_image_dim", [VectorType<Int, 2>, ImageType<imgTy, aQual>], Attr.Const>;
  }
  foreach imgTy = [Image2dArrayMsaa, Image2dArrayMsaaDepth] in {
    def : Builtin<"get_image_array_size", [Size, ImageType<imgTy, aQual>], Attr.Const>;
  }
}

let Extension = FuncExtKhrGlMsaaSharing in {
  defm : ImageReadMsaa<"RO">;
  defm : ImageQueryMsaa<"RO">;
  defm : ImageQueryMsaa<"WO">;
  defm : ImageReadMsaa<"RW">;
  defm : ImageQueryMsaa<"RW">;
}

//--------------------------------------------------------------------
// OpenCL Extension v2.0 s28 - Subgroups
// --- Table 28.2.1 ---
let Extension = FuncExtKhrSubgroups in {
  foreach name = ["get_sub_group_size", "get_max_sub_group_size",
                  "get_num_sub_groups", "get_sub_group_id",
                  "get_sub_group_local_id"] in {
    def : Builtin<name, [UInt]>;
  }
  let MinVersion = CL20 in {
    foreach name = ["get_enqueued_num_sub_groups"] in {
      def : Builtin<name, [UInt]>;
    }
  }
}

// --- Table 28.2.2 ---
let Extension = FuncExtKhrSubgroups in {
  def : Builtin<"sub_group_barrier", [Void, MemFenceFlags], Attr.Convergent>;
  let MinVersion = CL20 in {
    def : Builtin<"sub_group_barrier", [Void, MemFenceFlags, MemoryScope], Attr.Convergent>;
  }
}

// --- Table 28.2.4 ---
let Extension = FuncExtKhrSubgroups in {
  foreach name = ["sub_group_all", "sub_group_any"] in {
    def : Builtin<name, [Int, Int], Attr.Convergent>;
  }
  foreach name = ["sub_group_broadcast"] in {
    def : Builtin<name, [IntLongFloatGenType1, IntLongFloatGenType1, UInt], Attr.Convergent>;
  }
  foreach name = ["sub_group_reduce_", "sub_group_scan_exclusive_",
                  "sub_group_scan_inclusive_"] in {
    foreach op = ["add", "min", "max"] in {
      def : Builtin<name # op, [IntLongFloatGenType1, IntLongFloatGenType1], Attr.Convergent>;
    }
  }
}

// OpenCL Extension v3.0 s38 - Extended Subgroup Functions

// Section 38.4.1 - cl_khr_subgroup_extended_types
let Extension = FuncExtKhrSubgroupExtendedTypes in {
  // For sub_group_broadcast, add scalar char, uchar, short, and ushort support,
  def : Builtin<"sub_group_broadcast", [CharShortGenType1, CharShortGenType1, UInt], Attr.Convergent>;
  // gentype may additionally be one of the supported built-in vector data types.
  def : Builtin<"sub_group_broadcast", [AGenTypeNNoScalar, AGenTypeNNoScalar, UInt], Attr.Convergent>;

  foreach name = ["sub_group_reduce_", "sub_group_scan_exclusive_",
                  "sub_group_scan_inclusive_"] in {
    foreach op = ["add", "min", "max"] in {
      def : Builtin<name # op, [CharShortGenType1, CharShortGenType1], Attr.Convergent>;
    }
  }
}

// Section 38.5.1 - cl_khr_subgroup_non_uniform_vote
let Extension = FuncExtKhrSubgroupNonUniformVote in {
  def : Builtin<"sub_group_elect", [Int]>;
  def : Builtin<"sub_group_non_uniform_all", [Int, Int]>;
  def : Builtin<"sub_group_non_uniform_any", [Int, Int]>;
  def : Builtin<"sub_group_non_uniform_all_equal", [Int, AGenType1]>;
}

// Section 38.6.1 - cl_khr_subgroup_ballot
let Extension = FuncExtKhrSubgroupBallot in {
  def : Builtin<"sub_group_non_uniform_broadcast", [AGenTypeN, AGenTypeN, UInt]>;
  def : Builtin<"sub_group_broadcast_first", [AGenType1, AGenType1]>;
  def : Builtin<"sub_group_ballot", [VectorType<UInt, 4>, Int]>;
  def : Builtin<"sub_group_inverse_ballot", [Int, VectorType<UInt, 4>], Attr.Const>;
  def : Builtin<"sub_group_ballot_bit_extract", [Int, VectorType<UInt, 4>, UInt], Attr.Const>;
  def : Builtin<"sub_group_ballot_bit_count", [UInt, VectorType<UInt, 4>], Attr.Const>;
  def : Builtin<"sub_group_ballot_inclusive_scan", [UInt, VectorType<UInt, 4>]>;
  def : Builtin<"sub_group_ballot_exclusive_scan", [UInt, VectorType<UInt, 4>]>;
  def : Builtin<"sub_group_ballot_find_lsb", [UInt, VectorType<UInt, 4>]>;
  def : Builtin<"sub_group_ballot_find_msb", [UInt, VectorType<UInt, 4>]>;

  foreach op = ["eq", "ge", "gt", "le", "lt"] in {
    def : Builtin<"get_sub_group_" # op # "_mask", [VectorType<UInt, 4>], Attr.Const>;
  }
}

// Section 38.7.1 - cl_khr_subgroup_non_uniform_arithmetic
let Extension = FuncExtKhrSubgroupNonUniformArithmetic in {
  foreach name = ["reduce_", "scan_exclusive_", "scan_inclusive_"] in {
    foreach op = ["add", "min", "max", "mul"] in {
      def : Builtin<"sub_group_non_uniform_" # name # op, [AGenType1, AGenType1]>;
    }
    foreach op = ["and", "or", "xor"] in {
      def : Builtin<"sub_group_non_uniform_" # name # op, [AIGenType1, AIGenType1]>;
    }
    foreach op = ["and", "or", "xor"] in {
      def : Builtin<"sub_group_non_uniform_" # name # "logical_" # op, [Int, Int]>;
    }
  }
}

// Section 38.8.1 - cl_khr_subgroup_shuffle
let Extension = FuncExtKhrSubgroupShuffle in {
  def : Builtin<"sub_group_shuffle", [AGenType1, AGenType1, UInt]>;
  def : Builtin<"sub_group_shuffle_xor", [AGenType1, AGenType1, UInt]>;
}

// Section 38.9.1 - cl_khr_subgroup_shuffle_relative
let Extension = FuncExtKhrSubgroupShuffleRelative in {
  def : Builtin<"sub_group_shuffle_up", [AGenType1, AGenType1, UInt]>;
  def : Builtin<"sub_group_shuffle_down", [AGenType1, AGenType1, UInt]>;
}

// Section 38.10.1 - cl_khr_subgroup_clustered_reduce
let Extension = FuncExtKhrSubgroupClusteredReduce in {
  foreach op = ["add", "min", "max", "mul"] in {
    def : Builtin<"sub_group_clustered_reduce_" # op, [AGenType1, AGenType1, UInt]>;
  }
  foreach op = ["and", "or", "xor"] in {
    def : Builtin<"sub_group_clustered_reduce_" # op, [AIGenType1, AIGenType1, UInt]>;
  }
  foreach op = ["and", "or", "xor"] in {
    def : Builtin<"sub_group_clustered_reduce_logical_" # op, [Int, Int, UInt]>;
  }
}

// Section 40.3.1 - cl_khr_extended_bit_ops
let Extension = FuncExtKhrExtendedBitOps in {
  def : Builtin<"bitfield_insert", [AIGenTypeN, AIGenTypeN, AIGenTypeN, UInt, UInt], Attr.Const>;
  def : Builtin<"bitfield_extract_signed", [SGenTypeN, SGenTypeN, UInt, UInt], Attr.Const>;
  def : Builtin<"bitfield_extract_signed", [SGenTypeN, UGenTypeN, UInt, UInt], Attr.Const>;
  def : Builtin<"bitfield_extract_unsigned", [UGenTypeN, SGenTypeN, UInt, UInt], Attr.Const>;
  def : Builtin<"bitfield_extract_unsigned", [UGenTypeN, UGenTypeN, UInt, UInt], Attr.Const>;
  def : Builtin<"bit_reverse", [AIGenTypeN, AIGenTypeN], Attr.Const>;
}

// Section 42.3 - cl_khr_integer_dot_product
let Extension = FunctionExtension<"__opencl_c_integer_dot_product_input_4x8bit"> in {
  def : Builtin<"dot", [UInt, VectorType<UChar, 4>, VectorType<UChar, 4>], Attr.Const>;
  def : Builtin<"dot", [Int, VectorType<Char, 4>, VectorType<Char, 4>], Attr.Const>;
  def : Builtin<"dot", [Int, VectorType<UChar, 4>, VectorType<Char, 4>], Attr.Const>;
  def : Builtin<"dot", [Int, VectorType<Char, 4>, VectorType<UChar, 4>], Attr.Const>;

  def : Builtin<"dot_acc_sat", [UInt, VectorType<UChar, 4>, VectorType<UChar, 4>, UInt], Attr.Const>;
  def : Builtin<"dot_acc_sat", [Int, VectorType<Char, 4>, VectorType<Char, 4>, Int], Attr.Const>;
  def : Builtin<"dot_acc_sat", [Int, VectorType<UChar, 4>, VectorType<Char, 4>, Int], Attr.Const>;
  def : Builtin<"dot_acc_sat", [Int, VectorType<Char, 4>, VectorType<UChar, 4>, Int], Attr.Const>;
}

let Extension = FunctionExtension<"__opencl_c_integer_dot_product_input_4x8bit_packed"> in {
  def : Builtin<"dot_4x8packed_uu_uint", [UInt, UInt, UInt], Attr.Const>;
  def : Builtin<"dot_4x8packed_ss_int", [Int, UInt, UInt], Attr.Const>;
  def : Builtin<"dot_4x8packed_us_int", [Int, UInt, UInt], Attr.Const>;
  def : Builtin<"dot_4x8packed_su_int", [Int, UInt, UInt], Attr.Const>;

  def : Builtin<"dot_acc_sat_4x8packed_uu_uint", [UInt, UInt, UInt, UInt], Attr.Const>;
  def : Builtin<"dot_acc_sat_4x8packed_ss_int", [Int, UInt, UInt, Int], Attr.Const>;
  def : Builtin<"dot_acc_sat_4x8packed_us_int", [Int, UInt, UInt, Int], Attr.Const>;
  def : Builtin<"dot_acc_sat_4x8packed_su_int", [Int, UInt, UInt, Int], Attr.Const>;
}

// Section 48.3 - cl_khr_subgroup_rotate
let Extension = FunctionExtension<"cl_khr_subgroup_rotate"> in {
  def : Builtin<"sub_group_rotate", [AGenType1, AGenType1, Int], Attr.Convergent>;
  def : Builtin<"sub_group_clustered_rotate", [AGenType1, AGenType1, Int, UInt], Attr.Convergent>;
}

// cl_khr_kernel_clock
let Extension = FunctionExtension<"cl_khr_kernel_clock __opencl_c_kernel_clock_scope_device"> in {
  def : Builtin<"clock_read_device", [ULong]>;
  def : Builtin<"clock_read_hilo_device", [VectorType<UInt, 2>]>;
}
let Extension = FunctionExtension<"cl_khr_kernel_clock __opencl_c_kernel_clock_scope_work_group"> in {
  def : Builtin<"clock_read_work_group", [ULong]>;
  def : Builtin<"clock_read_hilo_work_group", [VectorType<UInt, 2>]>;
}
let Extension = FunctionExtension<"cl_khr_kernel_clock __opencl_c_kernel_clock_scope_sub_group"> in {
  def : Builtin<"clock_read_sub_group", [ULong]>;
  def : Builtin<"clock_read_hilo_sub_group", [VectorType<UInt, 2>]>;
}

//--------------------------------------------------------------------
// Arm extensions.
let Extension = ArmIntegerDotProductInt8 in {
  foreach name = ["arm_dot"] in {
    def : Builtin<name, [UInt, VectorType<UChar, 4>, VectorType<UChar, 4>]>;
    def : Builtin<name, [Int, VectorType<Char, 4>, VectorType<Char, 4>]>;
  }
}
let Extension = ArmIntegerDotProductAccumulateInt8 in {
  foreach name = ["arm_dot_acc"] in {
    def : Builtin<name, [UInt, VectorType<UChar, 4>, VectorType<UChar, 4>, UInt]>;
    def : Builtin<name, [Int, VectorType<Char, 4>, VectorType<Char, 4>, Int]>;
  }
}
let Extension = ArmIntegerDotProductAccumulateInt16 in {
  foreach name = ["arm_dot_acc"] in {
    def : Builtin<name, [UInt, VectorType<UShort, 2>, VectorType<UShort, 2>, UInt]>;
    def : Builtin<name, [Int, VectorType<Short, 2>, VectorType<Short, 2>, Int]>;
  }
}
let Extension = ArmIntegerDotProductAccumulateSaturateInt8 in {
  foreach name = ["arm_dot_acc_sat"] in {
    def : Builtin<name, [UInt, VectorType<UChar, 4>, VectorType<UChar, 4>, UInt]>;
    def : Builtin<name, [Int, VectorType<Char, 4>, VectorType<Char, 4>, Int]>;
  }
}
