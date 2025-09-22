//===- VFABIDemangler.h - Vector Function ABI demangler ------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the VFABI demangling utility.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VFABIDEMANGLER_H
#define LLVM_IR_VFABIDEMANGLER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {

/// Describes the type of Parameters
enum class VFParamKind {
  Vector,            // No semantic information.
  OMP_Linear,        // declare simd linear(i)
  OMP_LinearRef,     // declare simd linear(ref(i))
  OMP_LinearVal,     // declare simd linear(val(i))
  OMP_LinearUVal,    // declare simd linear(uval(i))
  OMP_LinearPos,     // declare simd linear(i:c) uniform(c)
  OMP_LinearValPos,  // declare simd linear(val(i:c)) uniform(c)
  OMP_LinearRefPos,  // declare simd linear(ref(i:c)) uniform(c)
  OMP_LinearUValPos, // declare simd linear(uval(i:c)) uniform(c)
  OMP_Uniform,       // declare simd uniform(i)
  GlobalPredicate,   // Global logical predicate that acts on all lanes
                     // of the input and output mask concurrently. For
                     // example, it is implied by the `M` token in the
                     // Vector Function ABI mangled name.
  Unknown
};

/// Describes the type of Instruction Set Architecture
enum class VFISAKind {
  AdvancedSIMD, // AArch64 Advanced SIMD (NEON)
  SVE,          // AArch64 Scalable Vector Extension
  SSE,          // x86 SSE
  AVX,          // x86 AVX
  AVX2,         // x86 AVX2
  AVX512,       // x86 AVX512
  LLVM,         // LLVM internal ISA for functions that are not
  // attached to an existing ABI via name mangling.
  Unknown // Unknown ISA
};

/// Encapsulates information needed to describe a parameter.
///
/// The description of the parameter is not linked directly to
/// OpenMP or any other vector function description. This structure
/// is extendible to handle other paradigms that describe vector
/// functions and their parameters.
struct VFParameter {
  unsigned ParamPos;         // Parameter Position in Scalar Function.
  VFParamKind ParamKind;     // Kind of Parameter.
  int LinearStepOrPos = 0;   // Step or Position of the Parameter.
  Align Alignment = Align(); // Optional alignment in bytes, defaulted to 1.

  // Comparison operator.
  bool operator==(const VFParameter &Other) const {
    return std::tie(ParamPos, ParamKind, LinearStepOrPos, Alignment) ==
           std::tie(Other.ParamPos, Other.ParamKind, Other.LinearStepOrPos,
                    Other.Alignment);
  }
};

/// Contains the information about the kind of vectorization
/// available.
///
/// This object in independent on the paradigm used to
/// represent vector functions. in particular, it is not attached to
/// any target-specific ABI.
struct VFShape {
  ElementCount VF;                        // Vectorization factor.
  SmallVector<VFParameter, 8> Parameters; // List of parameter information.
  // Comparison operator.
  bool operator==(const VFShape &Other) const {
    return std::tie(VF, Parameters) == std::tie(Other.VF, Other.Parameters);
  }

  /// Update the parameter in position P.ParamPos to P.
  void updateParam(VFParameter P) {
    assert(P.ParamPos < Parameters.size() && "Invalid parameter position.");
    Parameters[P.ParamPos] = P;
    assert(hasValidParameterList() && "Invalid parameter list");
  }

  /// Retrieve the VFShape that can be used to map a scalar function to itself,
  /// with VF = 1.
  static VFShape getScalarShape(const FunctionType *FTy) {
    return VFShape::get(FTy, ElementCount::getFixed(1),
                        /*HasGlobalPredicate*/ false);
  }

  /// Retrieve the basic vectorization shape of the function, where all
  /// parameters are mapped to VFParamKind::Vector with \p EC lanes. Specifies
  /// whether the function has a Global Predicate argument via \p HasGlobalPred.
  static VFShape get(const FunctionType *FTy, ElementCount EC,
                     bool HasGlobalPred) {
    SmallVector<VFParameter, 8> Parameters;
    for (unsigned I = 0; I < FTy->getNumParams(); ++I)
      Parameters.push_back(VFParameter({I, VFParamKind::Vector}));
    if (HasGlobalPred)
      Parameters.push_back(
          VFParameter({FTy->getNumParams(), VFParamKind::GlobalPredicate}));

    return {EC, Parameters};
  }
  /// Validation check on the Parameters in the VFShape.
  bool hasValidParameterList() const;
};

/// Holds the VFShape for a specific scalar to vector function mapping.
struct VFInfo {
  VFShape Shape;          /// Classification of the vector function.
  std::string ScalarName; /// Scalar Function Name.
  std::string VectorName; /// Vector Function Name associated to this VFInfo.
  VFISAKind ISA;          /// Instruction Set Architecture.

  /// Returns the index of the first parameter with the kind 'GlobalPredicate',
  /// if any exist.
  std::optional<unsigned> getParamIndexForOptionalMask() const {
    unsigned ParamCount = Shape.Parameters.size();
    for (unsigned i = 0; i < ParamCount; ++i)
      if (Shape.Parameters[i].ParamKind == VFParamKind::GlobalPredicate)
        return i;

    return std::nullopt;
  }

  /// Returns true if at least one of the operands to the vectorized function
  /// has the kind 'GlobalPredicate'.
  bool isMasked() const { return getParamIndexForOptionalMask().has_value(); }
};

namespace VFABI {
/// LLVM Internal VFABI ISA token for vector functions.
static constexpr char const *_LLVM_ = "_LLVM_";
/// Prefix for internal name redirection for vector function that
/// tells the compiler to scalarize the call using the scalar name
/// of the function. For example, a mangled name like
/// `_ZGV_LLVM_N2v_foo(_LLVM_Scalarize_foo)` would tell the
/// vectorizer to vectorize the scalar call `foo`, and to scalarize
/// it once vectorization is done.
static constexpr char const *_LLVM_Scalarize_ = "_LLVM_Scalarize_";

/// Function to construct a VFInfo out of a mangled names in the
/// following format:
///
/// <VFABI_name>{(<redirection>)}
///
/// where <VFABI_name> is the name of the vector function, mangled according
/// to the rules described in the Vector Function ABI of the target vector
/// extension (or <isa> from now on). The <VFABI_name> is in the following
/// format:
///
/// _ZGV<isa><mask><vlen><parameters>_<scalarname>[(<redirection>)]
///
/// This methods support demangling rules for the following <isa>:
///
/// * AArch64: https://developer.arm.com/docs/101129/latest
///
/// * x86 (libmvec): https://sourceware.org/glibc/wiki/libmvec and
///  https://sourceware.org/glibc/wiki/libmvec?action=AttachFile&do=view&target=VectorABI.txt
///
/// \param MangledName -> input string in the format
/// _ZGV<isa><mask><vlen><parameters>_<scalarname>[(<redirection>)].
/// \param FTy -> FunctionType of the scalar function which we're trying to find
/// a vectorized variant for. This is required to determine the vectorization
/// factor for scalable vectors, since the mangled name doesn't encode that;
/// it needs to be derived from the widest element types of vector arguments
/// or return values.
std::optional<VFInfo> tryDemangleForVFABI(StringRef MangledName,
                                          const FunctionType *FTy);

/// Retrieve the `VFParamKind` from a string token.
VFParamKind getVFParamKindFromString(const StringRef Token);

// Name of the attribute where the variant mappings are stored.
static constexpr char const *MappingsAttrName = "vector-function-abi-variant";

/// Populates a set of strings representing the Vector Function ABI variants
/// associated to the CallInst CI. If the CI does not contain the
/// vector-function-abi-variant attribute, we return without populating
/// VariantMappings, i.e. callers of getVectorVariantNames need not check for
/// the presence of the attribute (see InjectTLIMappings).
void getVectorVariantNames(const CallInst &CI,
                           SmallVectorImpl<std::string> &VariantMappings);

/// Constructs a FunctionType by applying vector function information to the
/// type of a matching scalar function.
/// \param Info gets the vectorization factor (VF) and the VFParamKind of the
/// parameters.
/// \param ScalarFTy gets the Type information of parameters, as it is not
/// stored in \p Info.
/// \returns a pointer to a newly created vector FunctionType
FunctionType *createFunctionType(const VFInfo &Info,
                                 const FunctionType *ScalarFTy);

/// Overwrite the Vector Function ABI variants attribute with the names provide
/// in \p VariantMappings.
void setVectorVariantNames(CallInst *CI, ArrayRef<std::string> VariantMappings);

} // end namespace VFABI

} // namespace llvm

#endif // LLVM_IR_VFABIDEMANGLER_H
