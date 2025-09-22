//===-- TargetLibraryInfo.h - Library information ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TARGETLIBRARYINFO_H
#define LLVM_ANALYSIS_TARGETLIBRARYINFO_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>

namespace llvm {

template <typename T> class ArrayRef;
class Function;
class Module;
class Triple;

/// Provides info so a possible vectorization of a function can be
/// computed. Function 'VectorFnName' is equivalent to 'ScalarFnName'
/// vectorized by a factor 'VectorizationFactor'.
/// The VABIPrefix string holds information about isa, mask, vlen,
/// and vparams so a scalar-to-vector mapping of the form:
///    _ZGV<isa><mask><vlen><vparams>_<scalarname>(<vectorname>)
/// can be constructed where:
///
/// <isa> = "_LLVM_"
/// <mask> = "M" if masked, "N" if no mask.
/// <vlen> = Number of concurrent lanes, stored in the `VectorizationFactor`
///          field of the `VecDesc` struct. If the number of lanes is scalable
///          then 'x' is printed instead.
/// <vparams> = "v", as many as are the numArgs.
/// <scalarname> = the name of the scalar function.
/// <vectorname> = the name of the vector function.
class VecDesc {
  StringRef ScalarFnName;
  StringRef VectorFnName;
  ElementCount VectorizationFactor;
  bool Masked;
  StringRef VABIPrefix;

public:
  VecDesc() = delete;
  VecDesc(StringRef ScalarFnName, StringRef VectorFnName,
          ElementCount VectorizationFactor, bool Masked, StringRef VABIPrefix)
      : ScalarFnName(ScalarFnName), VectorFnName(VectorFnName),
        VectorizationFactor(VectorizationFactor), Masked(Masked),
        VABIPrefix(VABIPrefix) {}

  StringRef getScalarFnName() const { return ScalarFnName; }
  StringRef getVectorFnName() const { return VectorFnName; }
  ElementCount getVectorizationFactor() const { return VectorizationFactor; }
  bool isMasked() const { return Masked; }
  StringRef getVABIPrefix() const { return VABIPrefix; }

  /// Returns a vector function ABI variant string on the form:
  ///    _ZGV<isa><mask><vlen><vparams>_<scalarname>(<vectorname>)
  std::string getVectorFunctionABIVariantString() const;
};

  enum LibFunc : unsigned {
#define TLI_DEFINE_ENUM
#include "llvm/Analysis/TargetLibraryInfo.def"

    NumLibFuncs,
    NotLibFunc
  };

/// Implementation of the target library information.
///
/// This class constructs tables that hold the target library information and
/// make it available. However, it is somewhat expensive to compute and only
/// depends on the triple. So users typically interact with the \c
/// TargetLibraryInfo wrapper below.
class TargetLibraryInfoImpl {
  friend class TargetLibraryInfo;

  unsigned char AvailableArray[(NumLibFuncs+3)/4];
  DenseMap<unsigned, std::string> CustomNames;
  static StringLiteral const StandardNames[NumLibFuncs];
  bool ShouldExtI32Param, ShouldExtI32Return, ShouldSignExtI32Param, ShouldSignExtI32Return;
  unsigned SizeOfInt;

  enum AvailabilityState {
    StandardName = 3, // (memset to all ones)
    CustomName = 1,
    Unavailable = 0  // (memset to all zeros)
  };
  void setState(LibFunc F, AvailabilityState State) {
    AvailableArray[F/4] &= ~(3 << 2*(F&3));
    AvailableArray[F/4] |= State << 2*(F&3);
  }
  AvailabilityState getState(LibFunc F) const {
    return static_cast<AvailabilityState>((AvailableArray[F/4] >> 2*(F&3)) & 3);
  }

  /// Vectorization descriptors - sorted by ScalarFnName.
  std::vector<VecDesc> VectorDescs;
  /// Scalarization descriptors - same content as VectorDescs but sorted based
  /// on VectorFnName rather than ScalarFnName.
  std::vector<VecDesc> ScalarDescs;

  /// Return true if the function type FTy is valid for the library function
  /// F, regardless of whether the function is available.
  bool isValidProtoForLibFunc(const FunctionType &FTy, LibFunc F,
                              const Module &M) const;

public:
  /// List of known vector-functions libraries.
  ///
  /// The vector-functions library defines, which functions are vectorizable
  /// and with which factor. The library can be specified by either frontend,
  /// or a commandline option, and then used by
  /// addVectorizableFunctionsFromVecLib for filling up the tables of
  /// vectorizable functions.
  enum VectorLibrary {
    NoLibrary,        // Don't use any vector library.
    Accelerate,       // Use Accelerate framework.
    DarwinLibSystemM, // Use Darwin's libsystem_m.
    LIBMVEC_X86,      // GLIBC Vector Math library.
    MASSV,            // IBM MASS vector library.
    SVML,             // Intel short vector math library.
    SLEEFGNUABI, // SLEEF - SIMD Library for Evaluating Elementary Functions.
    ArmPL,       // Arm Performance Libraries.
    AMDLIBM      // AMD Math Vector library.
  };

  TargetLibraryInfoImpl();
  explicit TargetLibraryInfoImpl(const Triple &T);

  // Provide value semantics.
  TargetLibraryInfoImpl(const TargetLibraryInfoImpl &TLI);
  TargetLibraryInfoImpl(TargetLibraryInfoImpl &&TLI);
  TargetLibraryInfoImpl &operator=(const TargetLibraryInfoImpl &TLI);
  TargetLibraryInfoImpl &operator=(TargetLibraryInfoImpl &&TLI);

  /// Searches for a particular function name.
  ///
  /// If it is one of the known library functions, return true and set F to the
  /// corresponding value.
  bool getLibFunc(StringRef funcName, LibFunc &F) const;

  /// Searches for a particular function name, also checking that its type is
  /// valid for the library function matching that name.
  ///
  /// If it is one of the known library functions, return true and set F to the
  /// corresponding value.
  ///
  /// FDecl is assumed to have a parent Module when using this function.
  bool getLibFunc(const Function &FDecl, LibFunc &F) const;

  /// Searches for a function name using an Instruction \p Opcode.
  /// Currently, only the frem instruction is supported.
  bool getLibFunc(unsigned int Opcode, Type *Ty, LibFunc &F) const;

  /// Forces a function to be marked as unavailable.
  void setUnavailable(LibFunc F) {
    setState(F, Unavailable);
  }

  /// Forces a function to be marked as available.
  void setAvailable(LibFunc F) {
    setState(F, StandardName);
  }

  /// Forces a function to be marked as available and provide an alternate name
  /// that must be used.
  void setAvailableWithName(LibFunc F, StringRef Name) {
    if (StandardNames[F] != Name) {
      setState(F, CustomName);
      CustomNames[F] = std::string(Name);
      assert(CustomNames.contains(F));
    } else {
      setState(F, StandardName);
    }
  }

  /// Disables all builtins.
  ///
  /// This can be used for options like -fno-builtin.
  void disableAllFunctions();

  /// Add a set of scalar -> vector mappings, queryable via
  /// getVectorizedFunction and getScalarizedFunction.
  void addVectorizableFunctions(ArrayRef<VecDesc> Fns);

  /// Calls addVectorizableFunctions with a known preset of functions for the
  /// given vector library.
  void addVectorizableFunctionsFromVecLib(enum VectorLibrary VecLib,
                                          const llvm::Triple &TargetTriple);

  /// Return true if the function F has a vector equivalent with vectorization
  /// factor VF.
  bool isFunctionVectorizable(StringRef F, const ElementCount &VF) const {
    return !(getVectorizedFunction(F, VF, false).empty() &&
             getVectorizedFunction(F, VF, true).empty());
  }

  /// Return true if the function F has a vector equivalent with any
  /// vectorization factor.
  bool isFunctionVectorizable(StringRef F) const;

  /// Return the name of the equivalent of F, vectorized with factor VF. If no
  /// such mapping exists, return the empty string.
  StringRef getVectorizedFunction(StringRef F, const ElementCount &VF,
                                  bool Masked) const;

  /// Return a pointer to a VecDesc object holding all info for scalar to vector
  /// mappings in TLI for the equivalent of F, vectorized with factor VF.
  /// If no such mapping exists, return nullpointer.
  const VecDesc *getVectorMappingInfo(StringRef F, const ElementCount &VF,
                                      bool Masked) const;

  /// Set to true iff i32 parameters to library functions should have signext
  /// or zeroext attributes if they correspond to C-level int or unsigned int,
  /// respectively.
  void setShouldExtI32Param(bool Val) {
    ShouldExtI32Param = Val;
  }

  /// Set to true iff i32 results from library functions should have signext
  /// or zeroext attributes if they correspond to C-level int or unsigned int,
  /// respectively.
  void setShouldExtI32Return(bool Val) {
    ShouldExtI32Return = Val;
  }

  /// Set to true iff i32 parameters to library functions should have signext
  /// attribute if they correspond to C-level int or unsigned int.
  void setShouldSignExtI32Param(bool Val) {
    ShouldSignExtI32Param = Val;
  }

  /// Set to true iff i32 results from library functions should have signext
  /// attribute if they correspond to C-level int or unsigned int.
  void setShouldSignExtI32Return(bool Val) {
    ShouldSignExtI32Return = Val;
  }

  /// Returns the size of the wchar_t type in bytes or 0 if the size is unknown.
  /// This queries the 'wchar_size' metadata.
  unsigned getWCharSize(const Module &M) const;

  /// Returns the size of the size_t type in bits.
  unsigned getSizeTSize(const Module &M) const;

  /// Get size of a C-level int or unsigned int, in bits.
  unsigned getIntSize() const {
    return SizeOfInt;
  }

  /// Initialize the C-level size of an integer.
  void setIntSize(unsigned Bits) {
    SizeOfInt = Bits;
  }

  /// Returns the largest vectorization factor used in the list of
  /// vector functions.
  void getWidestVF(StringRef ScalarF, ElementCount &FixedVF,
                   ElementCount &Scalable) const;

  /// Returns true if call site / callee has cdecl-compatible calling
  /// conventions.
  static bool isCallingConvCCompatible(CallBase *CI);
  static bool isCallingConvCCompatible(Function *Callee);
};

/// Provides information about what library functions are available for
/// the current target.
///
/// This both allows optimizations to handle them specially and frontends to
/// disable such optimizations through -fno-builtin etc.
class TargetLibraryInfo {
  friend class TargetLibraryAnalysis;
  friend class TargetLibraryInfoWrapperPass;

  /// The global (module level) TLI info.
  const TargetLibraryInfoImpl *Impl;

  /// Support for -fno-builtin* options as function attributes, overrides
  /// information in global TargetLibraryInfoImpl.
  BitVector OverrideAsUnavailable;

public:
  explicit TargetLibraryInfo(const TargetLibraryInfoImpl &Impl,
                             std::optional<const Function *> F = std::nullopt)
      : Impl(&Impl), OverrideAsUnavailable(NumLibFuncs) {
    if (!F)
      return;
    if ((*F)->hasFnAttribute("no-builtins"))
      disableAllFunctions();
    else {
      // Disable individual libc/libm calls in TargetLibraryInfo.
      LibFunc LF;
      AttributeSet FnAttrs = (*F)->getAttributes().getFnAttrs();
      for (const Attribute &Attr : FnAttrs) {
        if (!Attr.isStringAttribute())
          continue;
        auto AttrStr = Attr.getKindAsString();
        if (!AttrStr.consume_front("no-builtin-"))
          continue;
        if (getLibFunc(AttrStr, LF))
          setUnavailable(LF);
      }
    }
  }

  // Provide value semantics.
  TargetLibraryInfo(const TargetLibraryInfo &TLI) = default;
  TargetLibraryInfo(TargetLibraryInfo &&TLI) = default;
  TargetLibraryInfo &operator=(const TargetLibraryInfo &TLI) = default;
  TargetLibraryInfo &operator=(TargetLibraryInfo &&TLI) = default;

  /// Determine whether a callee with the given TLI can be inlined into
  /// caller with this TLI, based on 'nobuiltin' attributes. When requested,
  /// allow inlining into a caller with a superset of the callee's nobuiltin
  /// attributes, which is conservatively correct.
  bool areInlineCompatible(const TargetLibraryInfo &CalleeTLI,
                           bool AllowCallerSuperset) const {
    if (!AllowCallerSuperset)
      return OverrideAsUnavailable == CalleeTLI.OverrideAsUnavailable;
    // We can inline if the callee's nobuiltin attributes are no stricter than
    // the caller's.
    return !CalleeTLI.OverrideAsUnavailable.test(OverrideAsUnavailable);
  }

  /// Return true if the function type FTy is valid for the library function
  /// F, regardless of whether the function is available.
  bool isValidProtoForLibFunc(const FunctionType &FTy, LibFunc F,
                              const Module &M) const {
    return Impl->isValidProtoForLibFunc(FTy, F, M);
  }

  /// Searches for a particular function name.
  ///
  /// If it is one of the known library functions, return true and set F to the
  /// corresponding value.
  bool getLibFunc(StringRef funcName, LibFunc &F) const {
    return Impl->getLibFunc(funcName, F);
  }

  bool getLibFunc(const Function &FDecl, LibFunc &F) const {
    return Impl->getLibFunc(FDecl, F);
  }

  /// If a callbase does not have the 'nobuiltin' attribute, return if the
  /// called function is a known library function and set F to that function.
  bool getLibFunc(const CallBase &CB, LibFunc &F) const {
    return !CB.isNoBuiltin() && CB.getCalledFunction() &&
           getLibFunc(*(CB.getCalledFunction()), F);
  }

  /// Searches for a function name using an Instruction \p Opcode.
  /// Currently, only the frem instruction is supported.
  bool getLibFunc(unsigned int Opcode, Type *Ty, LibFunc &F) const {
    return Impl->getLibFunc(Opcode, Ty, F);
  }

  /// Disables all builtins.
  ///
  /// This can be used for options like -fno-builtin.
  void disableAllFunctions() LLVM_ATTRIBUTE_UNUSED {
    OverrideAsUnavailable.set();
  }

  /// Forces a function to be marked as unavailable.
  void setUnavailable(LibFunc F) LLVM_ATTRIBUTE_UNUSED {
    OverrideAsUnavailable.set(F);
  }

  TargetLibraryInfoImpl::AvailabilityState getState(LibFunc F) const {
    if (OverrideAsUnavailable[F])
      return TargetLibraryInfoImpl::Unavailable;
    return Impl->getState(F);
  }

  /// Tests whether a library function is available.
  bool has(LibFunc F) const {
    return getState(F) != TargetLibraryInfoImpl::Unavailable;
  }
  bool isFunctionVectorizable(StringRef F, const ElementCount &VF) const {
    return Impl->isFunctionVectorizable(F, VF);
  }
  bool isFunctionVectorizable(StringRef F) const {
    return Impl->isFunctionVectorizable(F);
  }
  StringRef getVectorizedFunction(StringRef F, const ElementCount &VF,
                                  bool Masked = false) const {
    return Impl->getVectorizedFunction(F, VF, Masked);
  }
  const VecDesc *getVectorMappingInfo(StringRef F, const ElementCount &VF,
                                      bool Masked) const {
    return Impl->getVectorMappingInfo(F, VF, Masked);
  }

  /// Tests if the function is both available and a candidate for optimized code
  /// generation.
  bool hasOptimizedCodeGen(LibFunc F) const {
    if (getState(F) == TargetLibraryInfoImpl::Unavailable)
      return false;
    switch (F) {
    default: break;
      // clang-format off
    case LibFunc_copysign:     case LibFunc_copysignf:  case LibFunc_copysignl:
    case LibFunc_fabs:         case LibFunc_fabsf:      case LibFunc_fabsl:
    case LibFunc_sin:          case LibFunc_sinf:       case LibFunc_sinl:
    case LibFunc_cos:          case LibFunc_cosf:       case LibFunc_cosl:
    case LibFunc_tan:          case LibFunc_tanf:       case LibFunc_tanl:
    case LibFunc_sqrt:         case LibFunc_sqrtf:      case LibFunc_sqrtl:
    case LibFunc_sqrt_finite:  case LibFunc_sqrtf_finite:
                                                   case LibFunc_sqrtl_finite:
    case LibFunc_fmax:         case LibFunc_fmaxf:      case LibFunc_fmaxl:
    case LibFunc_fmin:         case LibFunc_fminf:      case LibFunc_fminl:
    case LibFunc_floor:        case LibFunc_floorf:     case LibFunc_floorl:
    case LibFunc_nearbyint:    case LibFunc_nearbyintf: case LibFunc_nearbyintl:
    case LibFunc_ceil:         case LibFunc_ceilf:      case LibFunc_ceill:
    case LibFunc_rint:         case LibFunc_rintf:      case LibFunc_rintl:
    case LibFunc_round:        case LibFunc_roundf:     case LibFunc_roundl:
    case LibFunc_trunc:        case LibFunc_truncf:     case LibFunc_truncl:
    case LibFunc_log2:         case LibFunc_log2f:      case LibFunc_log2l:
    case LibFunc_exp2:         case LibFunc_exp2f:      case LibFunc_exp2l:
    case LibFunc_ldexp:        case LibFunc_ldexpf:     case LibFunc_ldexpl:
    case LibFunc_memcpy:       case LibFunc_memset:     case LibFunc_memmove:
    case LibFunc_memcmp:       case LibFunc_bcmp:       case LibFunc_strcmp:
    case LibFunc_strcpy:       case LibFunc_stpcpy:     case LibFunc_strlen:
    case LibFunc_strnlen:      case LibFunc_memchr:     case LibFunc_mempcpy:
      // clang-format on
      return true;
    }
    return false;
  }

  StringRef getName(LibFunc F) const {
    auto State = getState(F);
    if (State == TargetLibraryInfoImpl::Unavailable)
      return StringRef();
    if (State == TargetLibraryInfoImpl::StandardName)
      return Impl->StandardNames[F];
    assert(State == TargetLibraryInfoImpl::CustomName);
    return Impl->CustomNames.find(F)->second;
  }

  static void initExtensionsForTriple(bool &ShouldExtI32Param,
                                      bool &ShouldExtI32Return,
                                      bool &ShouldSignExtI32Param,
                                      bool &ShouldSignExtI32Return,
                                      const Triple &T) {
    ShouldExtI32Param     = ShouldExtI32Return     = false;
    ShouldSignExtI32Param = ShouldSignExtI32Return = false;

    // PowerPC64, Sparc64, SystemZ need signext/zeroext on i32 parameters and
    // returns corresponding to C-level ints and unsigned ints.
    if (T.isPPC64() || T.getArch() == Triple::sparcv9 ||
        T.getArch() == Triple::systemz) {
      ShouldExtI32Param = true;
      ShouldExtI32Return = true;
    }
    // LoongArch, Mips, and riscv64, on the other hand, need signext on i32
    // parameters corresponding to both signed and unsigned ints.
    if (T.isLoongArch() || T.isMIPS() || T.isRISCV64()) {
      ShouldSignExtI32Param = true;
    }
    // LoongArch and riscv64 need signext on i32 returns corresponding to both
    // signed and unsigned ints.
    if (T.isLoongArch() || T.isRISCV64()) {
      ShouldSignExtI32Return = true;
    }
  }

  /// Returns extension attribute kind to be used for i32 parameters
  /// corresponding to C-level int or unsigned int.  May be zeroext, signext,
  /// or none.
private:
  static Attribute::AttrKind getExtAttrForI32Param(bool ShouldExtI32Param_,
                                                   bool ShouldSignExtI32Param_,
                                                   bool Signed = true) {
    if (ShouldExtI32Param_)
      return Signed ? Attribute::SExt : Attribute::ZExt;
    if (ShouldSignExtI32Param_)
      return Attribute::SExt;
    return Attribute::None;
  }

public:
  static Attribute::AttrKind getExtAttrForI32Param(const Triple &T,
                                                   bool Signed = true) {
    bool ShouldExtI32Param, ShouldExtI32Return;
    bool ShouldSignExtI32Param, ShouldSignExtI32Return;
    initExtensionsForTriple(ShouldExtI32Param, ShouldExtI32Return,
                            ShouldSignExtI32Param, ShouldSignExtI32Return, T);
    return getExtAttrForI32Param(ShouldExtI32Param, ShouldSignExtI32Param,
                                 Signed);
  }

  Attribute::AttrKind getExtAttrForI32Param(bool Signed = true) const {
    return getExtAttrForI32Param(Impl->ShouldExtI32Param,
                                 Impl->ShouldSignExtI32Param, Signed);
  }

  /// Returns extension attribute kind to be used for i32 return values
  /// corresponding to C-level int or unsigned int.  May be zeroext, signext,
  /// or none.
private:
  static Attribute::AttrKind getExtAttrForI32Return(bool ShouldExtI32Return_,
                                                    bool ShouldSignExtI32Return_,
                                                    bool Signed) {
    if (ShouldExtI32Return_)
      return Signed ? Attribute::SExt : Attribute::ZExt;
    if (ShouldSignExtI32Return_)
      return Attribute::SExt;
    return Attribute::None;
  }

public:
  static Attribute::AttrKind getExtAttrForI32Return(const Triple &T,
                                                   bool Signed = true) {
    bool ShouldExtI32Param, ShouldExtI32Return;
    bool ShouldSignExtI32Param, ShouldSignExtI32Return;
    initExtensionsForTriple(ShouldExtI32Param, ShouldExtI32Return,
                            ShouldSignExtI32Param, ShouldSignExtI32Return, T);
    return getExtAttrForI32Return(ShouldExtI32Return, ShouldSignExtI32Return,
                                  Signed);
  }

  Attribute::AttrKind getExtAttrForI32Return(bool Signed = true) const {
    return getExtAttrForI32Return(Impl->ShouldExtI32Return,
                                  Impl->ShouldSignExtI32Return, Signed);
  }

  // Helper to create an AttributeList for args (and ret val) which all have
  // the same signedness. Attributes in AL may be passed in to include them
  // as well in the returned AttributeList.
  AttributeList getAttrList(LLVMContext *C, ArrayRef<unsigned> ArgNos,
                            bool Signed, bool Ret = false,
                            AttributeList AL = AttributeList()) const {
    if (auto AK = getExtAttrForI32Param(Signed))
      for (auto ArgNo : ArgNos)
        AL = AL.addParamAttribute(*C, ArgNo, AK);
    if (Ret)
      if (auto AK = getExtAttrForI32Return(Signed))
        AL = AL.addRetAttribute(*C, AK);
    return AL;
  }

  /// \copydoc TargetLibraryInfoImpl::getWCharSize()
  unsigned getWCharSize(const Module &M) const {
    return Impl->getWCharSize(M);
  }

  /// \copydoc TargetLibraryInfoImpl::getSizeTSize()
  unsigned getSizeTSize(const Module &M) const { return Impl->getSizeTSize(M); }

  /// \copydoc TargetLibraryInfoImpl::getIntSize()
  unsigned getIntSize() const {
    return Impl->getIntSize();
  }

  /// Handle invalidation from the pass manager.
  ///
  /// If we try to invalidate this info, just return false. It cannot become
  /// invalid even if the module or function changes.
  bool invalidate(Module &, const PreservedAnalyses &,
                  ModuleAnalysisManager::Invalidator &) {
    return false;
  }
  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &) {
    return false;
  }
  /// Returns the largest vectorization factor used in the list of
  /// vector functions.
  void getWidestVF(StringRef ScalarF, ElementCount &FixedVF,
                   ElementCount &ScalableVF) const {
    Impl->getWidestVF(ScalarF, FixedVF, ScalableVF);
  }

  /// Check if the function "F" is listed in a library known to LLVM.
  bool isKnownVectorFunctionInLibrary(StringRef F) const {
    return this->isFunctionVectorizable(F);
  }
};

/// Analysis pass providing the \c TargetLibraryInfo.
///
/// Note that this pass's result cannot be invalidated, it is immutable for the
/// life of the module.
class TargetLibraryAnalysis : public AnalysisInfoMixin<TargetLibraryAnalysis> {
public:
  typedef TargetLibraryInfo Result;

  /// Default construct the library analysis.
  ///
  /// This will use the module's triple to construct the library info for that
  /// module.
  TargetLibraryAnalysis() = default;

  /// Construct a library analysis with baseline Module-level info.
  ///
  /// This will be supplemented with Function-specific info in the Result.
  TargetLibraryAnalysis(TargetLibraryInfoImpl BaselineInfoImpl)
      : BaselineInfoImpl(std::move(BaselineInfoImpl)) {}

  TargetLibraryInfo run(const Function &F, FunctionAnalysisManager &);

private:
  friend AnalysisInfoMixin<TargetLibraryAnalysis>;
  static AnalysisKey Key;

  std::optional<TargetLibraryInfoImpl> BaselineInfoImpl;
};

class TargetLibraryInfoWrapperPass : public ImmutablePass {
  TargetLibraryAnalysis TLA;
  std::optional<TargetLibraryInfo> TLI;

  virtual void anchor();

public:
  static char ID;
  TargetLibraryInfoWrapperPass();
  explicit TargetLibraryInfoWrapperPass(const Triple &T);
  explicit TargetLibraryInfoWrapperPass(const TargetLibraryInfoImpl &TLI);

  // FIXME: This should be removed when PlaceSafepoints is fixed to not create a
  // PassManager inside a pass.
  explicit TargetLibraryInfoWrapperPass(const TargetLibraryInfo &TLI);

  TargetLibraryInfo &getTLI(const Function &F) {
    FunctionAnalysisManager DummyFAM;
    TLI = TLA.run(F, DummyFAM);
    return *TLI;
  }
};

} // end namespace llvm

#endif
