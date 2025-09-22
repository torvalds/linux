//===--- SemaCUDA.cpp - Semantic Analysis for CUDA constructs -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements semantic analysis for CUDA constructs.
///
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaCUDA.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/Cuda.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
using namespace clang;

SemaCUDA::SemaCUDA(Sema &S) : SemaBase(S) {}

template <typename AttrT> static bool hasExplicitAttr(const VarDecl *D) {
  if (!D)
    return false;
  if (auto *A = D->getAttr<AttrT>())
    return !A->isImplicit();
  return false;
}

void SemaCUDA::PushForceHostDevice() {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  ForceHostDeviceDepth++;
}

bool SemaCUDA::PopForceHostDevice() {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  if (ForceHostDeviceDepth == 0)
    return false;
  ForceHostDeviceDepth--;
  return true;
}

ExprResult SemaCUDA::ActOnExecConfigExpr(Scope *S, SourceLocation LLLLoc,
                                         MultiExprArg ExecConfig,
                                         SourceLocation GGGLoc) {
  FunctionDecl *ConfigDecl = getASTContext().getcudaConfigureCallDecl();
  if (!ConfigDecl)
    return ExprError(Diag(LLLLoc, diag::err_undeclared_var_use)
                     << getConfigureFuncName());
  QualType ConfigQTy = ConfigDecl->getType();

  DeclRefExpr *ConfigDR = new (getASTContext()) DeclRefExpr(
      getASTContext(), ConfigDecl, false, ConfigQTy, VK_LValue, LLLLoc);
  SemaRef.MarkFunctionReferenced(LLLLoc, ConfigDecl);

  return SemaRef.BuildCallExpr(S, ConfigDR, LLLLoc, ExecConfig, GGGLoc, nullptr,
                               /*IsExecConfig=*/true);
}

CUDAFunctionTarget SemaCUDA::IdentifyTarget(const ParsedAttributesView &Attrs) {
  bool HasHostAttr = false;
  bool HasDeviceAttr = false;
  bool HasGlobalAttr = false;
  bool HasInvalidTargetAttr = false;
  for (const ParsedAttr &AL : Attrs) {
    switch (AL.getKind()) {
    case ParsedAttr::AT_CUDAGlobal:
      HasGlobalAttr = true;
      break;
    case ParsedAttr::AT_CUDAHost:
      HasHostAttr = true;
      break;
    case ParsedAttr::AT_CUDADevice:
      HasDeviceAttr = true;
      break;
    case ParsedAttr::AT_CUDAInvalidTarget:
      HasInvalidTargetAttr = true;
      break;
    default:
      break;
    }
  }

  if (HasInvalidTargetAttr)
    return CUDAFunctionTarget::InvalidTarget;

  if (HasGlobalAttr)
    return CUDAFunctionTarget::Global;

  if (HasHostAttr && HasDeviceAttr)
    return CUDAFunctionTarget::HostDevice;

  if (HasDeviceAttr)
    return CUDAFunctionTarget::Device;

  return CUDAFunctionTarget::Host;
}

template <typename A>
static bool hasAttr(const Decl *D, bool IgnoreImplicitAttr) {
  return D->hasAttrs() && llvm::any_of(D->getAttrs(), [&](Attr *Attribute) {
           return isa<A>(Attribute) &&
                  !(IgnoreImplicitAttr && Attribute->isImplicit());
         });
}

SemaCUDA::CUDATargetContextRAII::CUDATargetContextRAII(
    SemaCUDA &S_, SemaCUDA::CUDATargetContextKind K, Decl *D)
    : S(S_) {
  SavedCtx = S.CurCUDATargetCtx;
  assert(K == SemaCUDA::CTCK_InitGlobalVar);
  auto *VD = dyn_cast_or_null<VarDecl>(D);
  if (VD && VD->hasGlobalStorage() && !VD->isStaticLocal()) {
    auto Target = CUDAFunctionTarget::Host;
    if ((hasAttr<CUDADeviceAttr>(VD, /*IgnoreImplicit=*/true) &&
         !hasAttr<CUDAHostAttr>(VD, /*IgnoreImplicit=*/true)) ||
        hasAttr<CUDASharedAttr>(VD, /*IgnoreImplicit=*/true) ||
        hasAttr<CUDAConstantAttr>(VD, /*IgnoreImplicit=*/true))
      Target = CUDAFunctionTarget::Device;
    S.CurCUDATargetCtx = {Target, K, VD};
  }
}

/// IdentifyTarget - Determine the CUDA compilation target for this function
CUDAFunctionTarget SemaCUDA::IdentifyTarget(const FunctionDecl *D,
                                            bool IgnoreImplicitHDAttr) {
  // Code that lives outside a function gets the target from CurCUDATargetCtx.
  if (D == nullptr)
    return CurCUDATargetCtx.Target;

  if (D->hasAttr<CUDAInvalidTargetAttr>())
    return CUDAFunctionTarget::InvalidTarget;

  if (D->hasAttr<CUDAGlobalAttr>())
    return CUDAFunctionTarget::Global;

  if (hasAttr<CUDADeviceAttr>(D, IgnoreImplicitHDAttr)) {
    if (hasAttr<CUDAHostAttr>(D, IgnoreImplicitHDAttr))
      return CUDAFunctionTarget::HostDevice;
    return CUDAFunctionTarget::Device;
  } else if (hasAttr<CUDAHostAttr>(D, IgnoreImplicitHDAttr)) {
    return CUDAFunctionTarget::Host;
  } else if ((D->isImplicit() || !D->isUserProvided()) &&
             !IgnoreImplicitHDAttr) {
    // Some implicit declarations (like intrinsic functions) are not marked.
    // Set the most lenient target on them for maximal flexibility.
    return CUDAFunctionTarget::HostDevice;
  }

  return CUDAFunctionTarget::Host;
}

/// IdentifyTarget - Determine the CUDA compilation target for this variable.
SemaCUDA::CUDAVariableTarget SemaCUDA::IdentifyTarget(const VarDecl *Var) {
  if (Var->hasAttr<HIPManagedAttr>())
    return CVT_Unified;
  // Only constexpr and const variabless with implicit constant attribute
  // are emitted on both sides. Such variables are promoted to device side
  // only if they have static constant intializers on device side.
  if ((Var->isConstexpr() || Var->getType().isConstQualified()) &&
      Var->hasAttr<CUDAConstantAttr>() &&
      !hasExplicitAttr<CUDAConstantAttr>(Var))
    return CVT_Both;
  if (Var->hasAttr<CUDADeviceAttr>() || Var->hasAttr<CUDAConstantAttr>() ||
      Var->hasAttr<CUDASharedAttr>() ||
      Var->getType()->isCUDADeviceBuiltinSurfaceType() ||
      Var->getType()->isCUDADeviceBuiltinTextureType())
    return CVT_Device;
  // Function-scope static variable without explicit device or constant
  // attribute are emitted
  //  - on both sides in host device functions
  //  - on device side in device or global functions
  if (auto *FD = dyn_cast<FunctionDecl>(Var->getDeclContext())) {
    switch (IdentifyTarget(FD)) {
    case CUDAFunctionTarget::HostDevice:
      return CVT_Both;
    case CUDAFunctionTarget::Device:
    case CUDAFunctionTarget::Global:
      return CVT_Device;
    default:
      return CVT_Host;
    }
  }
  return CVT_Host;
}

// * CUDA Call preference table
//
// F - from,
// T - to
// Ph - preference in host mode
// Pd - preference in device mode
// H  - handled in (x)
// Preferences: N:native, SS:same side, HD:host-device, WS:wrong side, --:never.
//
// | F  | T  | Ph  | Pd  |  H  |
// |----+----+-----+-----+-----+
// | d  | d  | N   | N   | (c) |
// | d  | g  | --  | --  | (a) |
// | d  | h  | --  | --  | (e) |
// | d  | hd | HD  | HD  | (b) |
// | g  | d  | N   | N   | (c) |
// | g  | g  | --  | --  | (a) |
// | g  | h  | --  | --  | (e) |
// | g  | hd | HD  | HD  | (b) |
// | h  | d  | --  | --  | (e) |
// | h  | g  | N   | N   | (c) |
// | h  | h  | N   | N   | (c) |
// | h  | hd | HD  | HD  | (b) |
// | hd | d  | WS  | SS  | (d) |
// | hd | g  | SS  | --  |(d/a)|
// | hd | h  | SS  | WS  | (d) |
// | hd | hd | HD  | HD  | (b) |

SemaCUDA::CUDAFunctionPreference
SemaCUDA::IdentifyPreference(const FunctionDecl *Caller,
                             const FunctionDecl *Callee) {
  assert(Callee && "Callee must be valid.");

  // Treat ctor/dtor as host device function in device var initializer to allow
  // trivial ctor/dtor without device attr to be used. Non-trivial ctor/dtor
  // will be diagnosed by checkAllowedInitializer.
  if (Caller == nullptr && CurCUDATargetCtx.Kind == CTCK_InitGlobalVar &&
      CurCUDATargetCtx.Target == CUDAFunctionTarget::Device &&
      (isa<CXXConstructorDecl>(Callee) || isa<CXXDestructorDecl>(Callee)))
    return CFP_HostDevice;

  CUDAFunctionTarget CallerTarget = IdentifyTarget(Caller);
  CUDAFunctionTarget CalleeTarget = IdentifyTarget(Callee);

  // If one of the targets is invalid, the check always fails, no matter what
  // the other target is.
  if (CallerTarget == CUDAFunctionTarget::InvalidTarget ||
      CalleeTarget == CUDAFunctionTarget::InvalidTarget)
    return CFP_Never;

  // (a) Can't call global from some contexts until we support CUDA's
  // dynamic parallelism.
  if (CalleeTarget == CUDAFunctionTarget::Global &&
      (CallerTarget == CUDAFunctionTarget::Global ||
       CallerTarget == CUDAFunctionTarget::Device))
    return CFP_Never;

  // (b) Calling HostDevice is OK for everyone.
  if (CalleeTarget == CUDAFunctionTarget::HostDevice)
    return CFP_HostDevice;

  // (c) Best case scenarios
  if (CalleeTarget == CallerTarget ||
      (CallerTarget == CUDAFunctionTarget::Host &&
       CalleeTarget == CUDAFunctionTarget::Global) ||
      (CallerTarget == CUDAFunctionTarget::Global &&
       CalleeTarget == CUDAFunctionTarget::Device))
    return CFP_Native;

  // HipStdPar mode is special, in that assessing whether a device side call to
  // a host target is deferred to a subsequent pass, and cannot unambiguously be
  // adjudicated in the AST, hence we optimistically allow them to pass here.
  if (getLangOpts().HIPStdPar &&
      (CallerTarget == CUDAFunctionTarget::Global ||
       CallerTarget == CUDAFunctionTarget::Device ||
       CallerTarget == CUDAFunctionTarget::HostDevice) &&
      CalleeTarget == CUDAFunctionTarget::Host)
    return CFP_HostDevice;

  // (d) HostDevice behavior depends on compilation mode.
  if (CallerTarget == CUDAFunctionTarget::HostDevice) {
    // It's OK to call a compilation-mode matching function from an HD one.
    if ((getLangOpts().CUDAIsDevice &&
         CalleeTarget == CUDAFunctionTarget::Device) ||
        (!getLangOpts().CUDAIsDevice &&
         (CalleeTarget == CUDAFunctionTarget::Host ||
          CalleeTarget == CUDAFunctionTarget::Global)))
      return CFP_SameSide;

    // Calls from HD to non-mode-matching functions (i.e., to host functions
    // when compiling in device mode or to device functions when compiling in
    // host mode) are allowed at the sema level, but eventually rejected if
    // they're ever codegened.  TODO: Reject said calls earlier.
    return CFP_WrongSide;
  }

  // (e) Calling across device/host boundary is not something you should do.
  if ((CallerTarget == CUDAFunctionTarget::Host &&
       CalleeTarget == CUDAFunctionTarget::Device) ||
      (CallerTarget == CUDAFunctionTarget::Device &&
       CalleeTarget == CUDAFunctionTarget::Host) ||
      (CallerTarget == CUDAFunctionTarget::Global &&
       CalleeTarget == CUDAFunctionTarget::Host))
    return CFP_Never;

  llvm_unreachable("All cases should've been handled by now.");
}

template <typename AttrT> static bool hasImplicitAttr(const FunctionDecl *D) {
  if (!D)
    return false;
  if (auto *A = D->getAttr<AttrT>())
    return A->isImplicit();
  return D->isImplicit();
}

bool SemaCUDA::isImplicitHostDeviceFunction(const FunctionDecl *D) {
  bool IsImplicitDevAttr = hasImplicitAttr<CUDADeviceAttr>(D);
  bool IsImplicitHostAttr = hasImplicitAttr<CUDAHostAttr>(D);
  return IsImplicitDevAttr && IsImplicitHostAttr;
}

void SemaCUDA::EraseUnwantedMatches(
    const FunctionDecl *Caller,
    SmallVectorImpl<std::pair<DeclAccessPair, FunctionDecl *>> &Matches) {
  if (Matches.size() <= 1)
    return;

  using Pair = std::pair<DeclAccessPair, FunctionDecl*>;

  // Gets the CUDA function preference for a call from Caller to Match.
  auto GetCFP = [&](const Pair &Match) {
    return IdentifyPreference(Caller, Match.second);
  };

  // Find the best call preference among the functions in Matches.
  CUDAFunctionPreference BestCFP = GetCFP(*std::max_element(
      Matches.begin(), Matches.end(),
      [&](const Pair &M1, const Pair &M2) { return GetCFP(M1) < GetCFP(M2); }));

  // Erase all functions with lower priority.
  llvm::erase_if(Matches,
                 [&](const Pair &Match) { return GetCFP(Match) < BestCFP; });
}

/// When an implicitly-declared special member has to invoke more than one
/// base/field special member, conflicts may occur in the targets of these
/// members. For example, if one base's member __host__ and another's is
/// __device__, it's a conflict.
/// This function figures out if the given targets \param Target1 and
/// \param Target2 conflict, and if they do not it fills in
/// \param ResolvedTarget with a target that resolves for both calls.
/// \return true if there's a conflict, false otherwise.
static bool
resolveCalleeCUDATargetConflict(CUDAFunctionTarget Target1,
                                CUDAFunctionTarget Target2,
                                CUDAFunctionTarget *ResolvedTarget) {
  // Only free functions and static member functions may be global.
  assert(Target1 != CUDAFunctionTarget::Global);
  assert(Target2 != CUDAFunctionTarget::Global);

  if (Target1 == CUDAFunctionTarget::HostDevice) {
    *ResolvedTarget = Target2;
  } else if (Target2 == CUDAFunctionTarget::HostDevice) {
    *ResolvedTarget = Target1;
  } else if (Target1 != Target2) {
    return true;
  } else {
    *ResolvedTarget = Target1;
  }

  return false;
}

bool SemaCUDA::inferTargetForImplicitSpecialMember(CXXRecordDecl *ClassDecl,
                                                   CXXSpecialMemberKind CSM,
                                                   CXXMethodDecl *MemberDecl,
                                                   bool ConstRHS,
                                                   bool Diagnose) {
  // If the defaulted special member is defined lexically outside of its
  // owning class, or the special member already has explicit device or host
  // attributes, do not infer.
  bool InClass = MemberDecl->getLexicalParent() == MemberDecl->getParent();
  bool HasH = MemberDecl->hasAttr<CUDAHostAttr>();
  bool HasD = MemberDecl->hasAttr<CUDADeviceAttr>();
  bool HasExplicitAttr =
      (HasD && !MemberDecl->getAttr<CUDADeviceAttr>()->isImplicit()) ||
      (HasH && !MemberDecl->getAttr<CUDAHostAttr>()->isImplicit());
  if (!InClass || HasExplicitAttr)
    return false;

  std::optional<CUDAFunctionTarget> InferredTarget;

  // We're going to invoke special member lookup; mark that these special
  // members are called from this one, and not from its caller.
  Sema::ContextRAII MethodContext(SemaRef, MemberDecl);

  // Look for special members in base classes that should be invoked from here.
  // Infer the target of this member base on the ones it should call.
  // Skip direct and indirect virtual bases for abstract classes.
  llvm::SmallVector<const CXXBaseSpecifier *, 16> Bases;
  for (const auto &B : ClassDecl->bases()) {
    if (!B.isVirtual()) {
      Bases.push_back(&B);
    }
  }

  if (!ClassDecl->isAbstract()) {
    llvm::append_range(Bases, llvm::make_pointer_range(ClassDecl->vbases()));
  }

  for (const auto *B : Bases) {
    const RecordType *BaseType = B->getType()->getAs<RecordType>();
    if (!BaseType) {
      continue;
    }

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(BaseType->getDecl());
    Sema::SpecialMemberOverloadResult SMOR =
        SemaRef.LookupSpecialMember(BaseClassDecl, CSM,
                                    /* ConstArg */ ConstRHS,
                                    /* VolatileArg */ false,
                                    /* RValueThis */ false,
                                    /* ConstThis */ false,
                                    /* VolatileThis */ false);

    if (!SMOR.getMethod())
      continue;

    CUDAFunctionTarget BaseMethodTarget = IdentifyTarget(SMOR.getMethod());
    if (!InferredTarget) {
      InferredTarget = BaseMethodTarget;
    } else {
      bool ResolutionError = resolveCalleeCUDATargetConflict(
          *InferredTarget, BaseMethodTarget, &*InferredTarget);
      if (ResolutionError) {
        if (Diagnose) {
          Diag(ClassDecl->getLocation(),
               diag::note_implicit_member_target_infer_collision)
              << (unsigned)CSM << llvm::to_underlying(*InferredTarget)
              << llvm::to_underlying(BaseMethodTarget);
        }
        MemberDecl->addAttr(
            CUDAInvalidTargetAttr::CreateImplicit(getASTContext()));
        return true;
      }
    }
  }

  // Same as for bases, but now for special members of fields.
  for (const auto *F : ClassDecl->fields()) {
    if (F->isInvalidDecl()) {
      continue;
    }

    const RecordType *FieldType =
        getASTContext().getBaseElementType(F->getType())->getAs<RecordType>();
    if (!FieldType) {
      continue;
    }

    CXXRecordDecl *FieldRecDecl = cast<CXXRecordDecl>(FieldType->getDecl());
    Sema::SpecialMemberOverloadResult SMOR =
        SemaRef.LookupSpecialMember(FieldRecDecl, CSM,
                                    /* ConstArg */ ConstRHS && !F->isMutable(),
                                    /* VolatileArg */ false,
                                    /* RValueThis */ false,
                                    /* ConstThis */ false,
                                    /* VolatileThis */ false);

    if (!SMOR.getMethod())
      continue;

    CUDAFunctionTarget FieldMethodTarget = IdentifyTarget(SMOR.getMethod());
    if (!InferredTarget) {
      InferredTarget = FieldMethodTarget;
    } else {
      bool ResolutionError = resolveCalleeCUDATargetConflict(
          *InferredTarget, FieldMethodTarget, &*InferredTarget);
      if (ResolutionError) {
        if (Diagnose) {
          Diag(ClassDecl->getLocation(),
               diag::note_implicit_member_target_infer_collision)
              << (unsigned)CSM << llvm::to_underlying(*InferredTarget)
              << llvm::to_underlying(FieldMethodTarget);
        }
        MemberDecl->addAttr(
            CUDAInvalidTargetAttr::CreateImplicit(getASTContext()));
        return true;
      }
    }
  }


  // If no target was inferred, mark this member as __host__ __device__;
  // it's the least restrictive option that can be invoked from any target.
  bool NeedsH = true, NeedsD = true;
  if (InferredTarget) {
    if (*InferredTarget == CUDAFunctionTarget::Device)
      NeedsH = false;
    else if (*InferredTarget == CUDAFunctionTarget::Host)
      NeedsD = false;
  }

  // We either setting attributes first time, or the inferred ones must match
  // previously set ones.
  if (NeedsD && !HasD)
    MemberDecl->addAttr(CUDADeviceAttr::CreateImplicit(getASTContext()));
  if (NeedsH && !HasH)
    MemberDecl->addAttr(CUDAHostAttr::CreateImplicit(getASTContext()));

  return false;
}

bool SemaCUDA::isEmptyConstructor(SourceLocation Loc, CXXConstructorDecl *CD) {
  if (!CD->isDefined() && CD->isTemplateInstantiation())
    SemaRef.InstantiateFunctionDefinition(Loc, CD->getFirstDecl());

  // (E.2.3.1, CUDA 7.5) A constructor for a class type is considered
  // empty at a point in the translation unit, if it is either a
  // trivial constructor
  if (CD->isTrivial())
    return true;

  // ... or it satisfies all of the following conditions:
  // The constructor function has been defined.
  // The constructor function has no parameters,
  // and the function body is an empty compound statement.
  if (!(CD->hasTrivialBody() && CD->getNumParams() == 0))
    return false;

  // Its class has no virtual functions and no virtual base classes.
  if (CD->getParent()->isDynamicClass())
    return false;

  // Union ctor does not call ctors of its data members.
  if (CD->getParent()->isUnion())
    return true;

  // The only form of initializer allowed is an empty constructor.
  // This will recursively check all base classes and member initializers
  if (!llvm::all_of(CD->inits(), [&](const CXXCtorInitializer *CI) {
        if (const CXXConstructExpr *CE =
                dyn_cast<CXXConstructExpr>(CI->getInit()))
          return isEmptyConstructor(Loc, CE->getConstructor());
        return false;
      }))
    return false;

  return true;
}

bool SemaCUDA::isEmptyDestructor(SourceLocation Loc, CXXDestructorDecl *DD) {
  // No destructor -> no problem.
  if (!DD)
    return true;

  if (!DD->isDefined() && DD->isTemplateInstantiation())
    SemaRef.InstantiateFunctionDefinition(Loc, DD->getFirstDecl());

  // (E.2.3.1, CUDA 7.5) A destructor for a class type is considered
  // empty at a point in the translation unit, if it is either a
  // trivial constructor
  if (DD->isTrivial())
    return true;

  // ... or it satisfies all of the following conditions:
  // The destructor function has been defined.
  // and the function body is an empty compound statement.
  if (!DD->hasTrivialBody())
    return false;

  const CXXRecordDecl *ClassDecl = DD->getParent();

  // Its class has no virtual functions and no virtual base classes.
  if (ClassDecl->isDynamicClass())
    return false;

  // Union does not have base class and union dtor does not call dtors of its
  // data members.
  if (DD->getParent()->isUnion())
    return true;

  // Only empty destructors are allowed. This will recursively check
  // destructors for all base classes...
  if (!llvm::all_of(ClassDecl->bases(), [&](const CXXBaseSpecifier &BS) {
        if (CXXRecordDecl *RD = BS.getType()->getAsCXXRecordDecl())
          return isEmptyDestructor(Loc, RD->getDestructor());
        return true;
      }))
    return false;

  // ... and member fields.
  if (!llvm::all_of(ClassDecl->fields(), [&](const FieldDecl *Field) {
        if (CXXRecordDecl *RD = Field->getType()
                                    ->getBaseElementTypeUnsafe()
                                    ->getAsCXXRecordDecl())
          return isEmptyDestructor(Loc, RD->getDestructor());
        return true;
      }))
    return false;

  return true;
}

namespace {
enum CUDAInitializerCheckKind {
  CICK_DeviceOrConstant, // Check initializer for device/constant variable
  CICK_Shared,           // Check initializer for shared variable
};

bool IsDependentVar(VarDecl *VD) {
  if (VD->getType()->isDependentType())
    return true;
  if (const auto *Init = VD->getInit())
    return Init->isValueDependent();
  return false;
}

// Check whether a variable has an allowed initializer for a CUDA device side
// variable with global storage. \p VD may be a host variable to be checked for
// potential promotion to device side variable.
//
// CUDA/HIP allows only empty constructors as initializers for global
// variables (see E.2.3.1, CUDA 7.5). The same restriction also applies to all
// __shared__ variables whether they are local or not (they all are implicitly
// static in CUDA). One exception is that CUDA allows constant initializers
// for __constant__ and __device__ variables.
bool HasAllowedCUDADeviceStaticInitializer(SemaCUDA &S, VarDecl *VD,
                                           CUDAInitializerCheckKind CheckKind) {
  assert(!VD->isInvalidDecl() && VD->hasGlobalStorage());
  assert(!IsDependentVar(VD) && "do not check dependent var");
  const Expr *Init = VD->getInit();
  auto IsEmptyInit = [&](const Expr *Init) {
    if (!Init)
      return true;
    if (const auto *CE = dyn_cast<CXXConstructExpr>(Init)) {
      return S.isEmptyConstructor(VD->getLocation(), CE->getConstructor());
    }
    return false;
  };
  auto IsConstantInit = [&](const Expr *Init) {
    assert(Init);
    ASTContext::CUDAConstantEvalContextRAII EvalCtx(S.getASTContext(),
                                                    /*NoWronSidedVars=*/true);
    return Init->isConstantInitializer(S.getASTContext(),
                                       VD->getType()->isReferenceType());
  };
  auto HasEmptyDtor = [&](VarDecl *VD) {
    if (const auto *RD = VD->getType()->getAsCXXRecordDecl())
      return S.isEmptyDestructor(VD->getLocation(), RD->getDestructor());
    return true;
  };
  if (CheckKind == CICK_Shared)
    return IsEmptyInit(Init) && HasEmptyDtor(VD);
  return S.getLangOpts().GPUAllowDeviceInit ||
         ((IsEmptyInit(Init) || IsConstantInit(Init)) && HasEmptyDtor(VD));
}
} // namespace

void SemaCUDA::checkAllowedInitializer(VarDecl *VD) {
  // Return early if VD is inside a non-instantiated template function since
  // the implicit constructor is not defined yet.
  if (const FunctionDecl *FD =
          dyn_cast_or_null<FunctionDecl>(VD->getDeclContext()))
    if (FD->isDependentContext())
      return;

  // Do not check dependent variables since the ctor/dtor/initializer are not
  // determined. Do it after instantiation.
  if (VD->isInvalidDecl() || !VD->hasInit() || !VD->hasGlobalStorage() ||
      IsDependentVar(VD))
    return;
  const Expr *Init = VD->getInit();
  bool IsSharedVar = VD->hasAttr<CUDASharedAttr>();
  bool IsDeviceOrConstantVar =
      !IsSharedVar &&
      (VD->hasAttr<CUDADeviceAttr>() || VD->hasAttr<CUDAConstantAttr>());
  if (IsDeviceOrConstantVar || IsSharedVar) {
    if (HasAllowedCUDADeviceStaticInitializer(
            *this, VD, IsSharedVar ? CICK_Shared : CICK_DeviceOrConstant))
      return;
    Diag(VD->getLocation(),
         IsSharedVar ? diag::err_shared_var_init : diag::err_dynamic_var_init)
        << Init->getSourceRange();
    VD->setInvalidDecl();
  } else {
    // This is a host-side global variable.  Check that the initializer is
    // callable from the host side.
    const FunctionDecl *InitFn = nullptr;
    if (const CXXConstructExpr *CE = dyn_cast<CXXConstructExpr>(Init)) {
      InitFn = CE->getConstructor();
    } else if (const CallExpr *CE = dyn_cast<CallExpr>(Init)) {
      InitFn = CE->getDirectCallee();
    }
    if (InitFn) {
      CUDAFunctionTarget InitFnTarget = IdentifyTarget(InitFn);
      if (InitFnTarget != CUDAFunctionTarget::Host &&
          InitFnTarget != CUDAFunctionTarget::HostDevice) {
        Diag(VD->getLocation(), diag::err_ref_bad_target_global_initializer)
            << llvm::to_underlying(InitFnTarget) << InitFn;
        Diag(InitFn->getLocation(), diag::note_previous_decl) << InitFn;
        VD->setInvalidDecl();
      }
    }
  }
}

void SemaCUDA::RecordImplicitHostDeviceFuncUsedByDevice(
    const FunctionDecl *Callee) {
  FunctionDecl *Caller = SemaRef.getCurFunctionDecl(/*AllowLambda=*/true);
  if (!Caller)
    return;

  if (!isImplicitHostDeviceFunction(Callee))
    return;

  CUDAFunctionTarget CallerTarget = IdentifyTarget(Caller);

  // Record whether an implicit host device function is used on device side.
  if (CallerTarget != CUDAFunctionTarget::Device &&
      CallerTarget != CUDAFunctionTarget::Global &&
      (CallerTarget != CUDAFunctionTarget::HostDevice ||
       (isImplicitHostDeviceFunction(Caller) &&
        !getASTContext().CUDAImplicitHostDeviceFunUsedByDevice.count(Caller))))
    return;

  getASTContext().CUDAImplicitHostDeviceFunUsedByDevice.insert(Callee);
}

// With -fcuda-host-device-constexpr, an unattributed constexpr function is
// treated as implicitly __host__ __device__, unless:
//  * it is a variadic function (device-side variadic functions are not
//    allowed), or
//  * a __device__ function with this signature was already declared, in which
//    case in which case we output an error, unless the __device__ decl is in a
//    system header, in which case we leave the constexpr function unattributed.
//
// In addition, all function decls are treated as __host__ __device__ when
// ForceHostDeviceDepth > 0 (corresponding to code within a
//   #pragma clang force_cuda_host_device_begin/end
// pair).
void SemaCUDA::maybeAddHostDeviceAttrs(FunctionDecl *NewD,
                                       const LookupResult &Previous) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");

  if (ForceHostDeviceDepth > 0) {
    if (!NewD->hasAttr<CUDAHostAttr>())
      NewD->addAttr(CUDAHostAttr::CreateImplicit(getASTContext()));
    if (!NewD->hasAttr<CUDADeviceAttr>())
      NewD->addAttr(CUDADeviceAttr::CreateImplicit(getASTContext()));
    return;
  }

  // If a template function has no host/device/global attributes,
  // make it implicitly host device function.
  if (getLangOpts().OffloadImplicitHostDeviceTemplates &&
      !NewD->hasAttr<CUDAHostAttr>() && !NewD->hasAttr<CUDADeviceAttr>() &&
      !NewD->hasAttr<CUDAGlobalAttr>() &&
      (NewD->getDescribedFunctionTemplate() ||
       NewD->isFunctionTemplateSpecialization())) {
    NewD->addAttr(CUDAHostAttr::CreateImplicit(getASTContext()));
    NewD->addAttr(CUDADeviceAttr::CreateImplicit(getASTContext()));
    return;
  }

  if (!getLangOpts().CUDAHostDeviceConstexpr || !NewD->isConstexpr() ||
      NewD->isVariadic() || NewD->hasAttr<CUDAHostAttr>() ||
      NewD->hasAttr<CUDADeviceAttr>() || NewD->hasAttr<CUDAGlobalAttr>())
    return;

  // Is D a __device__ function with the same signature as NewD, ignoring CUDA
  // attributes?
  auto IsMatchingDeviceFn = [&](NamedDecl *D) {
    if (UsingShadowDecl *Using = dyn_cast<UsingShadowDecl>(D))
      D = Using->getTargetDecl();
    FunctionDecl *OldD = D->getAsFunction();
    return OldD && OldD->hasAttr<CUDADeviceAttr>() &&
           !OldD->hasAttr<CUDAHostAttr>() &&
           !SemaRef.IsOverload(NewD, OldD,
                               /* UseMemberUsingDeclRules = */ false,
                               /* ConsiderCudaAttrs = */ false);
  };
  auto It = llvm::find_if(Previous, IsMatchingDeviceFn);
  if (It != Previous.end()) {
    // We found a __device__ function with the same name and signature as NewD
    // (ignoring CUDA attrs).  This is an error unless that function is defined
    // in a system header, in which case we simply return without making NewD
    // host+device.
    NamedDecl *Match = *It;
    if (!SemaRef.getSourceManager().isInSystemHeader(Match->getLocation())) {
      Diag(NewD->getLocation(),
           diag::err_cuda_unattributed_constexpr_cannot_overload_device)
          << NewD;
      Diag(Match->getLocation(),
           diag::note_cuda_conflicting_device_function_declared_here);
    }
    return;
  }

  NewD->addAttr(CUDAHostAttr::CreateImplicit(getASTContext()));
  NewD->addAttr(CUDADeviceAttr::CreateImplicit(getASTContext()));
}

// TODO: `__constant__` memory may be a limited resource for certain targets.
// A safeguard may be needed at the end of compilation pipeline if
// `__constant__` memory usage goes beyond limit.
void SemaCUDA::MaybeAddConstantAttr(VarDecl *VD) {
  // Do not promote dependent variables since the cotr/dtor/initializer are
  // not determined. Do it after instantiation.
  if (getLangOpts().CUDAIsDevice && !VD->hasAttr<CUDAConstantAttr>() &&
      !VD->hasAttr<CUDASharedAttr>() &&
      (VD->isFileVarDecl() || VD->isStaticDataMember()) &&
      !IsDependentVar(VD) &&
      ((VD->isConstexpr() || VD->getType().isConstQualified()) &&
       HasAllowedCUDADeviceStaticInitializer(*this, VD,
                                             CICK_DeviceOrConstant))) {
    VD->addAttr(CUDAConstantAttr::CreateImplicit(getASTContext()));
  }
}

SemaBase::SemaDiagnosticBuilder SemaCUDA::DiagIfDeviceCode(SourceLocation Loc,
                                                           unsigned DiagID) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  FunctionDecl *CurFunContext =
      SemaRef.getCurFunctionDecl(/*AllowLambda=*/true);
  SemaDiagnosticBuilder::Kind DiagKind = [&] {
    if (!CurFunContext)
      return SemaDiagnosticBuilder::K_Nop;
    switch (CurrentTarget()) {
    case CUDAFunctionTarget::Global:
    case CUDAFunctionTarget::Device:
      return SemaDiagnosticBuilder::K_Immediate;
    case CUDAFunctionTarget::HostDevice:
      // An HD function counts as host code if we're compiling for host, and
      // device code if we're compiling for device.  Defer any errors in device
      // mode until the function is known-emitted.
      if (!getLangOpts().CUDAIsDevice)
        return SemaDiagnosticBuilder::K_Nop;
      if (SemaRef.IsLastErrorImmediate &&
          getDiagnostics().getDiagnosticIDs()->isBuiltinNote(DiagID))
        return SemaDiagnosticBuilder::K_Immediate;
      return (SemaRef.getEmissionStatus(CurFunContext) ==
              Sema::FunctionEmissionStatus::Emitted)
                 ? SemaDiagnosticBuilder::K_ImmediateWithCallStack
                 : SemaDiagnosticBuilder::K_Deferred;
    default:
      return SemaDiagnosticBuilder::K_Nop;
    }
  }();
  return SemaDiagnosticBuilder(DiagKind, Loc, DiagID, CurFunContext, SemaRef);
}

Sema::SemaDiagnosticBuilder SemaCUDA::DiagIfHostCode(SourceLocation Loc,
                                                     unsigned DiagID) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  FunctionDecl *CurFunContext =
      SemaRef.getCurFunctionDecl(/*AllowLambda=*/true);
  SemaDiagnosticBuilder::Kind DiagKind = [&] {
    if (!CurFunContext)
      return SemaDiagnosticBuilder::K_Nop;
    switch (CurrentTarget()) {
    case CUDAFunctionTarget::Host:
      return SemaDiagnosticBuilder::K_Immediate;
    case CUDAFunctionTarget::HostDevice:
      // An HD function counts as host code if we're compiling for host, and
      // device code if we're compiling for device.  Defer any errors in device
      // mode until the function is known-emitted.
      if (getLangOpts().CUDAIsDevice)
        return SemaDiagnosticBuilder::K_Nop;
      if (SemaRef.IsLastErrorImmediate &&
          getDiagnostics().getDiagnosticIDs()->isBuiltinNote(DiagID))
        return SemaDiagnosticBuilder::K_Immediate;
      return (SemaRef.getEmissionStatus(CurFunContext) ==
              Sema::FunctionEmissionStatus::Emitted)
                 ? SemaDiagnosticBuilder::K_ImmediateWithCallStack
                 : SemaDiagnosticBuilder::K_Deferred;
    default:
      return SemaDiagnosticBuilder::K_Nop;
    }
  }();
  return SemaDiagnosticBuilder(DiagKind, Loc, DiagID, CurFunContext, SemaRef);
}

bool SemaCUDA::CheckCall(SourceLocation Loc, FunctionDecl *Callee) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  assert(Callee && "Callee may not be null.");

  const auto &ExprEvalCtx = SemaRef.currentEvaluationContext();
  if (ExprEvalCtx.isUnevaluated() || ExprEvalCtx.isConstantEvaluated())
    return true;

  // FIXME: Is bailing out early correct here?  Should we instead assume that
  // the caller is a global initializer?
  FunctionDecl *Caller = SemaRef.getCurFunctionDecl(/*AllowLambda=*/true);
  if (!Caller)
    return true;

  // If the caller is known-emitted, mark the callee as known-emitted.
  // Otherwise, mark the call in our call graph so we can traverse it later.
  bool CallerKnownEmitted = SemaRef.getEmissionStatus(Caller) ==
                            Sema::FunctionEmissionStatus::Emitted;
  SemaDiagnosticBuilder::Kind DiagKind = [this, Caller, Callee,
                                          CallerKnownEmitted] {
    switch (IdentifyPreference(Caller, Callee)) {
    case CFP_Never:
    case CFP_WrongSide:
      assert(Caller && "Never/wrongSide calls require a non-null caller");
      // If we know the caller will be emitted, we know this wrong-side call
      // will be emitted, so it's an immediate error.  Otherwise, defer the
      // error until we know the caller is emitted.
      return CallerKnownEmitted
                 ? SemaDiagnosticBuilder::K_ImmediateWithCallStack
                 : SemaDiagnosticBuilder::K_Deferred;
    default:
      return SemaDiagnosticBuilder::K_Nop;
    }
  }();

  if (DiagKind == SemaDiagnosticBuilder::K_Nop) {
    // For -fgpu-rdc, keep track of external kernels used by host functions.
    if (getLangOpts().CUDAIsDevice && getLangOpts().GPURelocatableDeviceCode &&
        Callee->hasAttr<CUDAGlobalAttr>() && !Callee->isDefined() &&
        (!Caller || (!Caller->getDescribedFunctionTemplate() &&
                     getASTContext().GetGVALinkageForFunction(Caller) ==
                         GVA_StrongExternal)))
      getASTContext().CUDAExternalDeviceDeclODRUsedByHost.insert(Callee);
    return true;
  }

  // Avoid emitting this error twice for the same location.  Using a hashtable
  // like this is unfortunate, but because we must continue parsing as normal
  // after encountering a deferred error, it's otherwise very tricky for us to
  // ensure that we only emit this deferred error once.
  if (!LocsWithCUDACallDiags.insert({Caller, Loc}).second)
    return true;

  SemaDiagnosticBuilder(DiagKind, Loc, diag::err_ref_bad_target, Caller,
                        SemaRef)
      << llvm::to_underlying(IdentifyTarget(Callee)) << /*function*/ 0 << Callee
      << llvm::to_underlying(IdentifyTarget(Caller));
  if (!Callee->getBuiltinID())
    SemaDiagnosticBuilder(DiagKind, Callee->getLocation(),
                          diag::note_previous_decl, Caller, SemaRef)
        << Callee;
  return DiagKind != SemaDiagnosticBuilder::K_Immediate &&
         DiagKind != SemaDiagnosticBuilder::K_ImmediateWithCallStack;
}

// Check the wrong-sided reference capture of lambda for CUDA/HIP.
// A lambda function may capture a stack variable by reference when it is
// defined and uses the capture by reference when the lambda is called. When
// the capture and use happen on different sides, the capture is invalid and
// should be diagnosed.
void SemaCUDA::CheckLambdaCapture(CXXMethodDecl *Callee,
                                  const sema::Capture &Capture) {
  // In host compilation we only need to check lambda functions emitted on host
  // side. In such lambda functions, a reference capture is invalid only
  // if the lambda structure is populated by a device function or kernel then
  // is passed to and called by a host function. However that is impossible,
  // since a device function or kernel can only call a device function, also a
  // kernel cannot pass a lambda back to a host function since we cannot
  // define a kernel argument type which can hold the lambda before the lambda
  // itself is defined.
  if (!getLangOpts().CUDAIsDevice)
    return;

  // File-scope lambda can only do init captures for global variables, which
  // results in passing by value for these global variables.
  FunctionDecl *Caller = SemaRef.getCurFunctionDecl(/*AllowLambda=*/true);
  if (!Caller)
    return;

  // In device compilation, we only need to check lambda functions which are
  // emitted on device side. For such lambdas, a reference capture is invalid
  // only if the lambda structure is populated by a host function then passed
  // to and called in a device function or kernel.
  bool CalleeIsDevice = Callee->hasAttr<CUDADeviceAttr>();
  bool CallerIsHost =
      !Caller->hasAttr<CUDAGlobalAttr>() && !Caller->hasAttr<CUDADeviceAttr>();
  bool ShouldCheck = CalleeIsDevice && CallerIsHost;
  if (!ShouldCheck || !Capture.isReferenceCapture())
    return;
  auto DiagKind = SemaDiagnosticBuilder::K_Deferred;
  if (Capture.isVariableCapture() && !getLangOpts().HIPStdPar) {
    SemaDiagnosticBuilder(DiagKind, Capture.getLocation(),
                          diag::err_capture_bad_target, Callee, SemaRef)
        << Capture.getVariable();
  } else if (Capture.isThisCapture()) {
    // Capture of this pointer is allowed since this pointer may be pointing to
    // managed memory which is accessible on both device and host sides. It only
    // results in invalid memory access if this pointer points to memory not
    // accessible on device side.
    SemaDiagnosticBuilder(DiagKind, Capture.getLocation(),
                          diag::warn_maybe_capture_bad_target_this_ptr, Callee,
                          SemaRef);
  }
}

void SemaCUDA::SetLambdaAttrs(CXXMethodDecl *Method) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  if (Method->hasAttr<CUDAHostAttr>() || Method->hasAttr<CUDADeviceAttr>())
    return;
  Method->addAttr(CUDADeviceAttr::CreateImplicit(getASTContext()));
  Method->addAttr(CUDAHostAttr::CreateImplicit(getASTContext()));
}

void SemaCUDA::checkTargetOverload(FunctionDecl *NewFD,
                                   const LookupResult &Previous) {
  assert(getLangOpts().CUDA && "Should only be called during CUDA compilation");
  CUDAFunctionTarget NewTarget = IdentifyTarget(NewFD);
  for (NamedDecl *OldND : Previous) {
    FunctionDecl *OldFD = OldND->getAsFunction();
    if (!OldFD)
      continue;

    CUDAFunctionTarget OldTarget = IdentifyTarget(OldFD);
    // Don't allow HD and global functions to overload other functions with the
    // same signature.  We allow overloading based on CUDA attributes so that
    // functions can have different implementations on the host and device, but
    // HD/global functions "exist" in some sense on both the host and device, so
    // should have the same implementation on both sides.
    if (NewTarget != OldTarget &&
        !SemaRef.IsOverload(NewFD, OldFD, /* UseMemberUsingDeclRules = */ false,
                            /* ConsiderCudaAttrs = */ false)) {
      if ((NewTarget == CUDAFunctionTarget::HostDevice &&
           !(getLangOpts().OffloadImplicitHostDeviceTemplates &&
             isImplicitHostDeviceFunction(NewFD) &&
             OldTarget == CUDAFunctionTarget::Device)) ||
          (OldTarget == CUDAFunctionTarget::HostDevice &&
           !(getLangOpts().OffloadImplicitHostDeviceTemplates &&
             isImplicitHostDeviceFunction(OldFD) &&
             NewTarget == CUDAFunctionTarget::Device)) ||
          (NewTarget == CUDAFunctionTarget::Global) ||
          (OldTarget == CUDAFunctionTarget::Global)) {
        Diag(NewFD->getLocation(), diag::err_cuda_ovl_target)
            << llvm::to_underlying(NewTarget) << NewFD->getDeclName()
            << llvm::to_underlying(OldTarget) << OldFD;
        Diag(OldFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        break;
      }
      if ((NewTarget == CUDAFunctionTarget::Host &&
           OldTarget == CUDAFunctionTarget::Device) ||
          (NewTarget == CUDAFunctionTarget::Device &&
           OldTarget == CUDAFunctionTarget::Host)) {
        Diag(NewFD->getLocation(), diag::warn_offload_incompatible_redeclare)
            << llvm::to_underlying(NewTarget) << llvm::to_underlying(OldTarget);
        Diag(OldFD->getLocation(), diag::note_previous_declaration);
      }
    }
  }
}

template <typename AttrTy>
static void copyAttrIfPresent(Sema &S, FunctionDecl *FD,
                              const FunctionDecl &TemplateFD) {
  if (AttrTy *Attribute = TemplateFD.getAttr<AttrTy>()) {
    AttrTy *Clone = Attribute->clone(S.Context);
    Clone->setInherited(true);
    FD->addAttr(Clone);
  }
}

void SemaCUDA::inheritTargetAttrs(FunctionDecl *FD,
                                  const FunctionTemplateDecl &TD) {
  const FunctionDecl &TemplateFD = *TD.getTemplatedDecl();
  copyAttrIfPresent<CUDAGlobalAttr>(SemaRef, FD, TemplateFD);
  copyAttrIfPresent<CUDAHostAttr>(SemaRef, FD, TemplateFD);
  copyAttrIfPresent<CUDADeviceAttr>(SemaRef, FD, TemplateFD);
}

std::string SemaCUDA::getConfigureFuncName() const {
  if (getLangOpts().HIP)
    return getLangOpts().HIPUseNewLaunchAPI ? "__hipPushCallConfiguration"
                                            : "hipConfigureCall";

  // New CUDA kernel launch sequence.
  if (CudaFeatureEnabled(getASTContext().getTargetInfo().getSDKVersion(),
                         CudaFeature::CUDA_USES_NEW_LAUNCH))
    return "__cudaPushCallConfiguration";

  // Legacy CUDA kernel configuration call
  return "cudaConfigureCall";
}
