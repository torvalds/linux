//===--- Specifiers.h - Declaration and Type Specifiers ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines various enumerations that describe declaration and
/// type specifiers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_SPECIFIERS_H
#define LLVM_CLANG_BASIC_SPECIFIERS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {
class raw_ostream;
} // namespace llvm
namespace clang {

  /// Define the meaning of possible values of the kind in ExplicitSpecifier.
  enum class ExplicitSpecKind : unsigned {
    ResolvedFalse,
    ResolvedTrue,
    Unresolved,
  };

  /// Define the kind of constexpr specifier.
  enum class ConstexprSpecKind { Unspecified, Constexpr, Consteval, Constinit };

  /// In an if statement, this denotes whether the statement is
  /// a constexpr or consteval if statement.
  enum class IfStatementKind : unsigned {
    Ordinary,
    Constexpr,
    ConstevalNonNegated,
    ConstevalNegated
  };

  /// Specifies the width of a type, e.g., short, long, or long long.
  enum class TypeSpecifierWidth { Unspecified, Short, Long, LongLong };

  /// Specifies the signedness of a type, e.g., signed or unsigned.
  enum class TypeSpecifierSign { Unspecified, Signed, Unsigned };

  enum class TypeSpecifiersPipe { Unspecified, Pipe };

  /// Specifies the kind of type.
  enum TypeSpecifierType {
    TST_unspecified,
    TST_void,
    TST_char,
    TST_wchar,  // C++ wchar_t
    TST_char8,  // C++20 char8_t (proposed)
    TST_char16, // C++11 char16_t
    TST_char32, // C++11 char32_t
    TST_int,
    TST_int128,
    TST_bitint,  // Bit-precise integer types.
    TST_half,    // OpenCL half, ARM NEON __fp16
    TST_Float16, // C11 extension ISO/IEC TS 18661-3
    TST_Accum,   // ISO/IEC JTC1 SC22 WG14 N1169 Extension
    TST_Fract,
    TST_BFloat16,
    TST_float,
    TST_double,
    TST_float128,
    TST_ibm128,
    TST_bool,       // _Bool
    TST_decimal32,  // _Decimal32
    TST_decimal64,  // _Decimal64
    TST_decimal128, // _Decimal128
    TST_enum,
    TST_union,
    TST_struct,
    TST_class,             // C++ class type
    TST_interface,         // C++ (Microsoft-specific) __interface type
    TST_typename,          // Typedef, C++ class-name or enum name, etc.
    TST_typeofType,        // C23 (and GNU extension) typeof(type-name)
    TST_typeofExpr,        // C23 (and GNU extension) typeof(expression)
    TST_typeof_unqualType, // C23 typeof_unqual(type-name)
    TST_typeof_unqualExpr, // C23 typeof_unqual(expression)
    TST_decltype,          // C++11 decltype
#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait) TST_##Trait,
#include "clang/Basic/TransformTypeTraits.def"
    TST_auto,            // C++11 auto
    TST_decltype_auto,   // C++1y decltype(auto)
    TST_auto_type,       // __auto_type extension
    TST_unknown_anytype, // __unknown_anytype extension
    TST_atomic,          // C11 _Atomic
    TST_typename_pack_indexing,
#define GENERIC_IMAGE_TYPE(ImgType, Id)                                      \
    TST_##ImgType##_t, // OpenCL image types
#include "clang/Basic/OpenCLImageTypes.def"
    TST_error // erroneous type
  };

  /// Structure that packs information about the type specifiers that
  /// were written in a particular type specifier sequence.
  struct WrittenBuiltinSpecs {
    static_assert(TST_error < 1 << 7, "Type bitfield not wide enough for TST");
    LLVM_PREFERRED_TYPE(TypeSpecifierType)
    unsigned Type : 7;
    LLVM_PREFERRED_TYPE(TypeSpecifierSign)
    unsigned Sign : 2;
    LLVM_PREFERRED_TYPE(TypeSpecifierWidth)
    unsigned Width : 2;
    LLVM_PREFERRED_TYPE(bool)
    unsigned ModeAttr : 1;
  };

  /// A C++ access specifier (public, private, protected), plus the
  /// special value "none" which means different things in different contexts.
  enum AccessSpecifier {
    AS_public,
    AS_protected,
    AS_private,
    AS_none
  };

  /// The categorization of expression values, currently following the
  /// C++11 scheme.
  enum ExprValueKind {
    /// A pr-value expression (in the C++11 taxonomy)
    /// produces a temporary value.
    VK_PRValue,

    /// An l-value expression is a reference to an object with
    /// independent storage.
    VK_LValue,

    /// An x-value expression is a reference to an object with
    /// independent storage but which can be "moved", i.e.
    /// efficiently cannibalized for its resources.
    VK_XValue
  };

  /// A further classification of the kind of object referenced by an
  /// l-value or x-value.
  enum ExprObjectKind {
    /// An ordinary object is located at an address in memory.
    OK_Ordinary,

    /// A bitfield object is a bitfield on a C or C++ record.
    OK_BitField,

    /// A vector component is an element or range of elements on a vector.
    OK_VectorComponent,

    /// An Objective-C property is a logical field of an Objective-C
    /// object which is read and written via Objective-C method calls.
    OK_ObjCProperty,

    /// An Objective-C array/dictionary subscripting which reads an
    /// object or writes at the subscripted array/dictionary element via
    /// Objective-C method calls.
    OK_ObjCSubscript,

    /// A matrix component is a single element of a matrix.
    OK_MatrixComponent
  };

  /// The reason why a DeclRefExpr does not constitute an odr-use.
  enum NonOdrUseReason {
    /// This is an odr-use.
    NOUR_None = 0,
    /// This name appears in an unevaluated operand.
    NOUR_Unevaluated,
    /// This name appears as a potential result of an lvalue-to-rvalue
    /// conversion that is a constant expression.
    NOUR_Constant,
    /// This name appears as a potential result of a discarded value
    /// expression.
    NOUR_Discarded,
  };

  /// Describes the kind of template specialization that a
  /// particular template specialization declaration represents.
  enum TemplateSpecializationKind {
    /// This template specialization was formed from a template-id but
    /// has not yet been declared, defined, or instantiated.
    TSK_Undeclared = 0,
    /// This template specialization was implicitly instantiated from a
    /// template. (C++ [temp.inst]).
    TSK_ImplicitInstantiation,
    /// This template specialization was declared or defined by an
    /// explicit specialization (C++ [temp.expl.spec]) or partial
    /// specialization (C++ [temp.class.spec]).
    TSK_ExplicitSpecialization,
    /// This template specialization was instantiated from a template
    /// due to an explicit instantiation declaration request
    /// (C++11 [temp.explicit]).
    TSK_ExplicitInstantiationDeclaration,
    /// This template specialization was instantiated from a template
    /// due to an explicit instantiation definition request
    /// (C++ [temp.explicit]).
    TSK_ExplicitInstantiationDefinition
  };

  /// Determine whether this template specialization kind refers
  /// to an instantiation of an entity (as opposed to a non-template or
  /// an explicit specialization).
  inline bool isTemplateInstantiation(TemplateSpecializationKind Kind) {
    return Kind != TSK_Undeclared && Kind != TSK_ExplicitSpecialization;
  }

  /// True if this template specialization kind is an explicit
  /// specialization, explicit instantiation declaration, or explicit
  /// instantiation definition.
  inline bool isTemplateExplicitInstantiationOrSpecialization(
      TemplateSpecializationKind Kind) {
    switch (Kind) {
    case TSK_ExplicitSpecialization:
    case TSK_ExplicitInstantiationDeclaration:
    case TSK_ExplicitInstantiationDefinition:
      return true;

    case TSK_Undeclared:
    case TSK_ImplicitInstantiation:
      return false;
    }
    llvm_unreachable("bad template specialization kind");
  }

  /// Thread storage-class-specifier.
  enum ThreadStorageClassSpecifier {
    TSCS_unspecified,
    /// GNU __thread.
    TSCS___thread,
    /// C++11 thread_local. Implies 'static' at block scope, but not at
    /// class scope.
    TSCS_thread_local,
    /// C11 _Thread_local. Must be combined with either 'static' or 'extern'
    /// if used at block scope.
    TSCS__Thread_local
  };

  /// Storage classes.
  enum StorageClass {
    // These are legal on both functions and variables.
    SC_None,
    SC_Extern,
    SC_Static,
    SC_PrivateExtern,

    // These are only legal on variables.
    SC_Auto,
    SC_Register
  };

  /// Checks whether the given storage class is legal for functions.
  inline bool isLegalForFunction(StorageClass SC) {
    return SC <= SC_PrivateExtern;
  }

  /// Checks whether the given storage class is legal for variables.
  inline bool isLegalForVariable(StorageClass SC) {
    return true;
  }

  /// In-class initialization styles for non-static data members.
  enum InClassInitStyle {
    ICIS_NoInit,   ///< No in-class initializer.
    ICIS_CopyInit, ///< Copy initialization.
    ICIS_ListInit  ///< Direct list-initialization.
  };

  /// CallingConv - Specifies the calling convention that a function uses.
  enum CallingConv {
    CC_C,                 // __attribute__((cdecl))
    CC_X86StdCall,        // __attribute__((stdcall))
    CC_X86FastCall,       // __attribute__((fastcall))
    CC_X86ThisCall,       // __attribute__((thiscall))
    CC_X86VectorCall,     // __attribute__((vectorcall))
    CC_X86Pascal,         // __attribute__((pascal))
    CC_Win64,             // __attribute__((ms_abi))
    CC_X86_64SysV,        // __attribute__((sysv_abi))
    CC_X86RegCall,        // __attribute__((regcall))
    CC_AAPCS,             // __attribute__((pcs("aapcs")))
    CC_AAPCS_VFP,         // __attribute__((pcs("aapcs-vfp")))
    CC_IntelOclBicc,      // __attribute__((intel_ocl_bicc))
    CC_SpirFunction,      // default for OpenCL functions on SPIR target
    CC_OpenCLKernel,      // inferred for OpenCL kernels
    CC_Swift,             // __attribute__((swiftcall))
    CC_SwiftAsync,        // __attribute__((swiftasynccall))
    CC_PreserveMost,      // __attribute__((preserve_most))
    CC_PreserveAll,       // __attribute__((preserve_all))
    CC_AArch64VectorCall, // __attribute__((aarch64_vector_pcs))
    CC_AArch64SVEPCS,     // __attribute__((aarch64_sve_pcs))
    CC_AMDGPUKernelCall,  // __attribute__((amdgpu_kernel))
    CC_M68kRTD,           // __attribute__((m68k_rtd))
    CC_PreserveNone,      // __attribute__((preserve_none))
    CC_RISCVVectorCall,   // __attribute__((riscv_vector_cc))
  };

  /// Checks whether the given calling convention supports variadic
  /// calls. Unprototyped calls also use the variadic call rules.
  inline bool supportsVariadicCall(CallingConv CC) {
    switch (CC) {
    case CC_X86StdCall:
    case CC_X86FastCall:
    case CC_X86ThisCall:
    case CC_X86RegCall:
    case CC_X86Pascal:
    case CC_X86VectorCall:
    case CC_SpirFunction:
    case CC_OpenCLKernel:
    case CC_Swift:
    case CC_SwiftAsync:
    case CC_M68kRTD:
      return false;
    default:
      return true;
    }
  }

  /// The storage duration for an object (per C++ [basic.stc]).
  enum StorageDuration {
    SD_FullExpression, ///< Full-expression storage duration (for temporaries).
    SD_Automatic,      ///< Automatic storage duration (most local variables).
    SD_Thread,         ///< Thread storage duration.
    SD_Static,         ///< Static storage duration.
    SD_Dynamic         ///< Dynamic storage duration.
  };

  /// Describes the nullability of a particular type.
  enum class NullabilityKind : uint8_t {
    /// Values of this type can never be null.
    NonNull = 0,
    /// Values of this type can be null.
    Nullable,
    /// Whether values of this type can be null is (explicitly)
    /// unspecified. This captures a (fairly rare) case where we
    /// can't conclude anything about the nullability of the type even
    /// though it has been considered.
    Unspecified,
    // Generally behaves like Nullable, except when used in a block parameter
    // that was imported into a swift async method. There, swift will assume
    // that the parameter can get null even if no error occurred. _Nullable
    // parameters are assumed to only get null on error.
    NullableResult,
  };
  /// Prints human-readable debug representation.
  llvm::raw_ostream &operator<<(llvm::raw_ostream&, NullabilityKind);

  /// Return true if \p L has a weaker nullability annotation than \p R. The
  /// ordering is: Unspecified < Nullable < NonNull.
  inline bool hasWeakerNullability(NullabilityKind L, NullabilityKind R) {
    return uint8_t(L) > uint8_t(R);
  }

  /// Retrieve the spelling of the given nullability kind.
  llvm::StringRef getNullabilitySpelling(NullabilityKind kind,
                                         bool isContextSensitive = false);

  /// Kinds of parameter ABI.
  enum class ParameterABI {
    /// This parameter uses ordinary ABI rules for its type.
    Ordinary,

    /// This parameter (which must have pointer type) is a Swift
    /// indirect result parameter.
    SwiftIndirectResult,

    /// This parameter (which must have pointer-to-pointer type) uses
    /// the special Swift error-result ABI treatment.  There can be at
    /// most one parameter on a given function that uses this treatment.
    SwiftErrorResult,

    /// This parameter (which must have pointer type) uses the special
    /// Swift context-pointer ABI treatment.  There can be at
    /// most one parameter on a given function that uses this treatment.
    SwiftContext,

    /// This parameter (which must have pointer type) uses the special
    /// Swift asynchronous context-pointer ABI treatment.  There can be at
    /// most one parameter on a given function that uses this treatment.
    SwiftAsyncContext,
  };

  /// Assigned inheritance model for a class in the MS C++ ABI. Must match order
  /// of spellings in MSInheritanceAttr.
  enum class MSInheritanceModel {
    Single = 0,
    Multiple = 1,
    Virtual = 2,
    Unspecified = 3,
  };

  llvm::StringRef getParameterABISpelling(ParameterABI kind);

  inline llvm::StringRef getAccessSpelling(AccessSpecifier AS) {
    switch (AS) {
    case AccessSpecifier::AS_public:
      return "public";
    case AccessSpecifier::AS_protected:
      return "protected";
    case AccessSpecifier::AS_private:
      return "private";
    case AccessSpecifier::AS_none:
      return {};
    }
    llvm_unreachable("Unknown AccessSpecifier");
  }
} // end namespace clang

#endif // LLVM_CLANG_BASIC_SPECIFIERS_H
