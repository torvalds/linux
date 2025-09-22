//===----- SemaCUDA.h ----- Semantic Analysis for CUDA constructs ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis for CUDA constructs.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMACUDA_H
#define LLVM_CLANG_SEMA_SEMACUDA_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Redeclarable.h"
#include "clang/Basic/Cuda.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace clang {

enum class CUDAFunctionTarget;

class SemaCUDA : public SemaBase {
public:
  SemaCUDA(Sema &S);

  /// Increments our count of the number of times we've seen a pragma forcing
  /// functions to be __host__ __device__.  So long as this count is greater
  /// than zero, all functions encountered will be __host__ __device__.
  void PushForceHostDevice();

  /// Decrements our count of the number of times we've seen a pragma forcing
  /// functions to be __host__ __device__.  Returns false if the count is 0
  /// before incrementing, so you can emit an error.
  bool PopForceHostDevice();

  ExprResult ActOnExecConfigExpr(Scope *S, SourceLocation LLLLoc,
                                 MultiExprArg ExecConfig,
                                 SourceLocation GGGLoc);

  /// A pair of a canonical FunctionDecl and a SourceLocation.  When used as the
  /// key in a hashtable, both the FD and location are hashed.
  struct FunctionDeclAndLoc {
    CanonicalDeclPtr<const FunctionDecl> FD;
    SourceLocation Loc;
  };

  /// FunctionDecls and SourceLocations for which CheckCall has emitted a
  /// (maybe deferred) "bad call" diagnostic.  We use this to avoid emitting the
  /// same deferred diag twice.
  llvm::DenseSet<FunctionDeclAndLoc> LocsWithCUDACallDiags;

  /// An inverse call graph, mapping known-emitted functions to one of their
  /// known-emitted callers (plus the location of the call).
  ///
  /// Functions that we can tell a priori must be emitted aren't added to this
  /// map.
  llvm::DenseMap</* Callee = */ CanonicalDeclPtr<const FunctionDecl>,
                 /* Caller = */ FunctionDeclAndLoc>
      DeviceKnownEmittedFns;

  /// Creates a SemaDiagnosticBuilder that emits the diagnostic if the current
  /// context is "used as device code".
  ///
  /// - If CurContext is a __host__ function, does not emit any diagnostics
  ///   unless \p EmitOnBothSides is true.
  /// - If CurContext is a __device__ or __global__ function, emits the
  ///   diagnostics immediately.
  /// - If CurContext is a __host__ __device__ function and we are compiling for
  ///   the device, creates a diagnostic which is emitted if and when we realize
  ///   that the function will be codegen'ed.
  ///
  /// Example usage:
  ///
  ///  // Variable-length arrays are not allowed in CUDA device code.
  ///  if (DiagIfDeviceCode(Loc, diag::err_cuda_vla) << CurrentTarget())
  ///    return ExprError();
  ///  // Otherwise, continue parsing as normal.
  SemaDiagnosticBuilder DiagIfDeviceCode(SourceLocation Loc, unsigned DiagID);

  /// Creates a SemaDiagnosticBuilder that emits the diagnostic if the current
  /// context is "used as host code".
  ///
  /// Same as DiagIfDeviceCode, with "host" and "device" switched.
  SemaDiagnosticBuilder DiagIfHostCode(SourceLocation Loc, unsigned DiagID);

  /// Determines whether the given function is a CUDA device/host/kernel/etc.
  /// function.
  ///
  /// Use this rather than examining the function's attributes yourself -- you
  /// will get it wrong.  Returns CUDAFunctionTarget::Host if D is null.
  CUDAFunctionTarget IdentifyTarget(const FunctionDecl *D,
                                    bool IgnoreImplicitHDAttr = false);
  CUDAFunctionTarget IdentifyTarget(const ParsedAttributesView &Attrs);

  enum CUDAVariableTarget {
    CVT_Device,  /// Emitted on device side with a shadow variable on host side
    CVT_Host,    /// Emitted on host side only
    CVT_Both,    /// Emitted on both sides with different addresses
    CVT_Unified, /// Emitted as a unified address, e.g. managed variables
  };
  /// Determines whether the given variable is emitted on host or device side.
  CUDAVariableTarget IdentifyTarget(const VarDecl *D);

  /// Defines kinds of CUDA global host/device context where a function may be
  /// called.
  enum CUDATargetContextKind {
    CTCK_Unknown,       /// Unknown context
    CTCK_InitGlobalVar, /// Function called during global variable
                        /// initialization
  };

  /// Define the current global CUDA host/device context where a function may be
  /// called. Only used when a function is called outside of any functions.
  struct CUDATargetContext {
    CUDAFunctionTarget Target = CUDAFunctionTarget::HostDevice;
    CUDATargetContextKind Kind = CTCK_Unknown;
    Decl *D = nullptr;
  } CurCUDATargetCtx;

  struct CUDATargetContextRAII {
    SemaCUDA &S;
    SemaCUDA::CUDATargetContext SavedCtx;
    CUDATargetContextRAII(SemaCUDA &S_, SemaCUDA::CUDATargetContextKind K,
                          Decl *D);
    ~CUDATargetContextRAII() { S.CurCUDATargetCtx = SavedCtx; }
  };

  /// Gets the CUDA target for the current context.
  CUDAFunctionTarget CurrentTarget() {
    return IdentifyTarget(dyn_cast<FunctionDecl>(SemaRef.CurContext));
  }

  static bool isImplicitHostDeviceFunction(const FunctionDecl *D);

  // CUDA function call preference. Must be ordered numerically from
  // worst to best.
  enum CUDAFunctionPreference {
    CFP_Never,      // Invalid caller/callee combination.
    CFP_WrongSide,  // Calls from host-device to host or device
                    // function that do not match current compilation
                    // mode.
    CFP_HostDevice, // Any calls to host/device functions.
    CFP_SameSide,   // Calls from host-device to host or device
                    // function matching current compilation mode.
    CFP_Native,     // host-to-host or device-to-device calls.
  };

  /// Identifies relative preference of a given Caller/Callee
  /// combination, based on their host/device attributes.
  /// \param Caller function which needs address of \p Callee.
  ///               nullptr in case of global context.
  /// \param Callee target function
  ///
  /// \returns preference value for particular Caller/Callee combination.
  CUDAFunctionPreference IdentifyPreference(const FunctionDecl *Caller,
                                            const FunctionDecl *Callee);

  /// Determines whether Caller may invoke Callee, based on their CUDA
  /// host/device attributes.  Returns false if the call is not allowed.
  ///
  /// Note: Will return true for CFP_WrongSide calls.  These may appear in
  /// semantically correct CUDA programs, but only if they're never codegen'ed.
  bool IsAllowedCall(const FunctionDecl *Caller, const FunctionDecl *Callee) {
    return IdentifyPreference(Caller, Callee) != CFP_Never;
  }

  /// May add implicit CUDAHostAttr and CUDADeviceAttr attributes to FD,
  /// depending on FD and the current compilation settings.
  void maybeAddHostDeviceAttrs(FunctionDecl *FD, const LookupResult &Previous);

  /// May add implicit CUDAConstantAttr attribute to VD, depending on VD
  /// and current compilation settings.
  void MaybeAddConstantAttr(VarDecl *VD);

  /// Check whether we're allowed to call Callee from the current context.
  ///
  /// - If the call is never allowed in a semantically-correct program
  ///   (CFP_Never), emits an error and returns false.
  ///
  /// - If the call is allowed in semantically-correct programs, but only if
  ///   it's never codegen'ed (CFP_WrongSide), creates a deferred diagnostic to
  ///   be emitted if and when the caller is codegen'ed, and returns true.
  ///
  ///   Will only create deferred diagnostics for a given SourceLocation once,
  ///   so you can safely call this multiple times without generating duplicate
  ///   deferred errors.
  ///
  /// - Otherwise, returns true without emitting any diagnostics.
  bool CheckCall(SourceLocation Loc, FunctionDecl *Callee);

  void CheckLambdaCapture(CXXMethodDecl *D, const sema::Capture &Capture);

  /// Set __device__ or __host__ __device__ attributes on the given lambda
  /// operator() method.
  ///
  /// CUDA lambdas by default is host device function unless it has explicit
  /// host or device attribute.
  void SetLambdaAttrs(CXXMethodDecl *Method);

  /// Record \p FD if it is a CUDA/HIP implicit host device function used on
  /// device side in device compilation.
  void RecordImplicitHostDeviceFuncUsedByDevice(const FunctionDecl *FD);

  /// Finds a function in \p Matches with highest calling priority
  /// from \p Caller context and erases all functions with lower
  /// calling priority.
  void EraseUnwantedMatches(
      const FunctionDecl *Caller,
      llvm::SmallVectorImpl<std::pair<DeclAccessPair, FunctionDecl *>>
          &Matches);

  /// Given a implicit special member, infer its CUDA target from the
  /// calls it needs to make to underlying base/field special members.
  /// \param ClassDecl the class for which the member is being created.
  /// \param CSM the kind of special member.
  /// \param MemberDecl the special member itself.
  /// \param ConstRHS true if this is a copy operation with a const object on
  ///        its RHS.
  /// \param Diagnose true if this call should emit diagnostics.
  /// \return true if there was an error inferring.
  /// The result of this call is implicit CUDA target attribute(s) attached to
  /// the member declaration.
  bool inferTargetForImplicitSpecialMember(CXXRecordDecl *ClassDecl,
                                           CXXSpecialMemberKind CSM,
                                           CXXMethodDecl *MemberDecl,
                                           bool ConstRHS, bool Diagnose);

  /// \return true if \p CD can be considered empty according to CUDA
  /// (E.2.3.1 in CUDA 7.5 Programming guide).
  bool isEmptyConstructor(SourceLocation Loc, CXXConstructorDecl *CD);
  bool isEmptyDestructor(SourceLocation Loc, CXXDestructorDecl *CD);

  // \brief Checks that initializers of \p Var satisfy CUDA restrictions. In
  // case of error emits appropriate diagnostic and invalidates \p Var.
  //
  // \details CUDA allows only empty constructors as initializers for global
  // variables (see E.2.3.1, CUDA 7.5). The same restriction also applies to all
  // __shared__ variables whether they are local or not (they all are implicitly
  // static in CUDA). One exception is that CUDA allows constant initializers
  // for __constant__ and __device__ variables.
  void checkAllowedInitializer(VarDecl *VD);

  /// Check whether NewFD is a valid overload for CUDA. Emits
  /// diagnostics and invalidates NewFD if not.
  void checkTargetOverload(FunctionDecl *NewFD, const LookupResult &Previous);
  /// Copies target attributes from the template TD to the function FD.
  void inheritTargetAttrs(FunctionDecl *FD, const FunctionTemplateDecl &TD);

  /// Returns the name of the launch configuration function.  This is the name
  /// of the function that will be called to configure kernel call, with the
  /// parameters specified via <<<>>>.
  std::string getConfigureFuncName() const;

private:
  unsigned ForceHostDeviceDepth = 0;

  friend class ASTReader;
  friend class ASTWriter;
};

} // namespace clang

namespace llvm {
// Hash a FunctionDeclAndLoc by looking at both its FunctionDecl and its
// SourceLocation.
template <> struct DenseMapInfo<clang::SemaCUDA::FunctionDeclAndLoc> {
  using FunctionDeclAndLoc = clang::SemaCUDA::FunctionDeclAndLoc;
  using FDBaseInfo =
      DenseMapInfo<clang::CanonicalDeclPtr<const clang::FunctionDecl>>;

  static FunctionDeclAndLoc getEmptyKey() {
    return {FDBaseInfo::getEmptyKey(), clang::SourceLocation()};
  }

  static FunctionDeclAndLoc getTombstoneKey() {
    return {FDBaseInfo::getTombstoneKey(), clang::SourceLocation()};
  }

  static unsigned getHashValue(const FunctionDeclAndLoc &FDL) {
    return hash_combine(FDBaseInfo::getHashValue(FDL.FD),
                        FDL.Loc.getHashValue());
  }

  static bool isEqual(const FunctionDeclAndLoc &LHS,
                      const FunctionDeclAndLoc &RHS) {
    return LHS.FD == RHS.FD && LHS.Loc == RHS.Loc;
  }
};
} // namespace llvm

#endif // LLVM_CLANG_SEMA_SEMACUDA_H
