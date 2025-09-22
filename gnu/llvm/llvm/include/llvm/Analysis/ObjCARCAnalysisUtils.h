//===- ObjCARCAnalysisUtils.h - ObjC ARC Analysis Utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines common analysis utilities used by the ObjC ARC Optimizer.
/// ARC stands for Automatic Reference Counting and is a system for managing
/// reference counts for objects in Objective C.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OBJCARCANALYSISUTILS_H
#define LLVM_ANALYSIS_OBJCARCANALYSISUTILS_H

#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include <optional>

namespace llvm {

class AAResults;

namespace objcarc {

/// A handy option to enable/disable all ARC Optimizations.
extern bool EnableARCOpts;

/// Test if the given module looks interesting to run ARC optimization
/// on.
inline bool ModuleHasARC(const Module &M) {
  return
    M.getNamedValue("llvm.objc.retain") ||
    M.getNamedValue("llvm.objc.release") ||
    M.getNamedValue("llvm.objc.autorelease") ||
    M.getNamedValue("llvm.objc.retainAutoreleasedReturnValue") ||
    M.getNamedValue("llvm.objc.unsafeClaimAutoreleasedReturnValue") ||
    M.getNamedValue("llvm.objc.retainBlock") ||
    M.getNamedValue("llvm.objc.autoreleaseReturnValue") ||
    M.getNamedValue("llvm.objc.autoreleasePoolPush") ||
    M.getNamedValue("llvm.objc.loadWeakRetained") ||
    M.getNamedValue("llvm.objc.loadWeak") ||
    M.getNamedValue("llvm.objc.destroyWeak") ||
    M.getNamedValue("llvm.objc.storeWeak") ||
    M.getNamedValue("llvm.objc.initWeak") ||
    M.getNamedValue("llvm.objc.moveWeak") ||
    M.getNamedValue("llvm.objc.copyWeak") ||
    M.getNamedValue("llvm.objc.retainedObject") ||
    M.getNamedValue("llvm.objc.unretainedObject") ||
    M.getNamedValue("llvm.objc.unretainedPointer") ||
    M.getNamedValue("llvm.objc.clang.arc.use");
}

/// This is a wrapper around getUnderlyingObject which also knows how to
/// look through objc_retain and objc_autorelease calls, which we know to return
/// their argument verbatim.
inline const Value *GetUnderlyingObjCPtr(const Value *V) {
  for (;;) {
    V = getUnderlyingObject(V);
    if (!IsForwarding(GetBasicARCInstKind(V)))
      break;
    V = cast<CallInst>(V)->getArgOperand(0);
  }

  return V;
}

/// A wrapper for GetUnderlyingObjCPtr used for results memoization.
inline const Value *GetUnderlyingObjCPtrCached(
    const Value *V,
    DenseMap<const Value *, std::pair<WeakVH, WeakTrackingVH>> &Cache) {
  // The entry is invalid if either value handle is null.
  auto InCache = Cache.lookup(V);
  if (InCache.first && InCache.second)
    return InCache.second;

  const Value *Computed = GetUnderlyingObjCPtr(V);
  Cache[V] =
      std::make_pair(const_cast<Value *>(V), const_cast<Value *>(Computed));
  return Computed;
}

/// The RCIdentity root of a value \p V is a dominating value U for which
/// retaining or releasing U is equivalent to retaining or releasing V. In other
/// words, ARC operations on \p V are equivalent to ARC operations on \p U.
///
/// We use this in the ARC optimizer to make it easier to match up ARC
/// operations by always mapping ARC operations to RCIdentityRoots instead of
/// pointers themselves.
///
/// The two ways that we see RCIdentical values in ObjC are via:
///
///   1. PointerCasts
///   2. Forwarding Calls that return their argument verbatim.
///
/// Thus this function strips off pointer casts and forwarding calls. *NOTE*
/// This implies that two RCIdentical values must alias.
inline const Value *GetRCIdentityRoot(const Value *V) {
  for (;;) {
    V = V->stripPointerCasts();
    if (!IsForwarding(GetBasicARCInstKind(V)))
      break;
    V = cast<CallInst>(V)->getArgOperand(0);
  }
  return V;
}

/// Helper which calls const Value *GetRCIdentityRoot(const Value *V) and just
/// casts away the const of the result. For documentation about what an
/// RCIdentityRoot (and by extension GetRCIdentityRoot is) look at that
/// function.
inline Value *GetRCIdentityRoot(Value *V) {
  return const_cast<Value *>(GetRCIdentityRoot((const Value *)V));
}

/// Assuming the given instruction is one of the special calls such as
/// objc_retain or objc_release, return the RCIdentity root of the argument of
/// the call.
inline Value *GetArgRCIdentityRoot(Value *Inst) {
  return GetRCIdentityRoot(cast<CallInst>(Inst)->getArgOperand(0));
}

inline bool IsNullOrUndef(const Value *V) {
  return isa<ConstantPointerNull>(V) || isa<UndefValue>(V);
}

inline bool IsNoopInstruction(const Instruction *I) {
  return isa<BitCastInst>(I) ||
    (isa<GetElementPtrInst>(I) &&
     cast<GetElementPtrInst>(I)->hasAllZeroIndices());
}

/// Test whether the given value is possible a retainable object pointer.
inline bool IsPotentialRetainableObjPtr(const Value *Op) {
  // Pointers to static or stack storage are not valid retainable object
  // pointers.
  if (isa<Constant>(Op) || isa<AllocaInst>(Op))
    return false;
  // Special arguments can not be a valid retainable object pointer.
  if (const Argument *Arg = dyn_cast<Argument>(Op))
    if (Arg->hasPassPointeeByValueCopyAttr() || Arg->hasNestAttr() ||
        Arg->hasStructRetAttr())
      return false;
  // Only consider values with pointer types.
  //
  // It seemes intuitive to exclude function pointer types as well, since
  // functions are never retainable object pointers, however clang occasionally
  // bitcasts retainable object pointers to function-pointer type temporarily.
  PointerType *Ty = dyn_cast<PointerType>(Op->getType());
  if (!Ty)
    return false;
  // Conservatively assume anything else is a potential retainable object
  // pointer.
  return true;
}

bool IsPotentialRetainableObjPtr(const Value *Op, AAResults &AA);

/// Helper for GetARCInstKind. Determines what kind of construct CS
/// is.
inline ARCInstKind GetCallSiteClass(const CallBase &CB) {
  for (const Use &U : CB.args())
    if (IsPotentialRetainableObjPtr(U))
      return CB.onlyReadsMemory() ? ARCInstKind::User : ARCInstKind::CallOrUser;

  return CB.onlyReadsMemory() ? ARCInstKind::None : ARCInstKind::Call;
}

/// Return true if this value refers to a distinct and identifiable
/// object.
///
/// This is similar to AliasAnalysis's isIdentifiedObject, except that it uses
/// special knowledge of ObjC conventions.
inline bool IsObjCIdentifiedObject(const Value *V) {
  // Assume that call results and arguments have their own "provenance".
  // Constants (including GlobalVariables) and Allocas are never
  // reference-counted.
  if (isa<CallInst>(V) || isa<InvokeInst>(V) ||
      isa<Argument>(V) || isa<Constant>(V) ||
      isa<AllocaInst>(V))
    return true;

  if (const LoadInst *LI = dyn_cast<LoadInst>(V)) {
    const Value *Pointer =
      GetRCIdentityRoot(LI->getPointerOperand());
    if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(Pointer)) {
      // A constant pointer can't be pointing to an object on the heap. It may
      // be reference-counted, but it won't be deleted.
      if (GV->isConstant())
        return true;
      StringRef Name = GV->getName();
      // These special variables are known to hold values which are not
      // reference-counted pointers.
      if (Name.starts_with("\01l_objc_msgSend_fixup_"))
        return true;

      StringRef Section = GV->getSection();
      if (Section.contains("__message_refs") ||
          Section.contains("__objc_classrefs") ||
          Section.contains("__objc_superrefs") ||
          Section.contains("__objc_methname") || Section.contains("__cstring"))
        return true;
    }
  }

  return false;
}

enum class ARCMDKindID {
  ImpreciseRelease,
  CopyOnEscape,
  NoObjCARCExceptions,
};

/// A cache of MDKinds used by various ARC optimizations.
class ARCMDKindCache {
  Module *M;

  /// The Metadata Kind for clang.imprecise_release metadata.
  std::optional<unsigned> ImpreciseReleaseMDKind;

  /// The Metadata Kind for clang.arc.copy_on_escape metadata.
  std::optional<unsigned> CopyOnEscapeMDKind;

  /// The Metadata Kind for clang.arc.no_objc_arc_exceptions metadata.
  std::optional<unsigned> NoObjCARCExceptionsMDKind;

public:
  void init(Module *Mod) {
    M = Mod;
    ImpreciseReleaseMDKind = std::nullopt;
    CopyOnEscapeMDKind = std::nullopt;
    NoObjCARCExceptionsMDKind = std::nullopt;
  }

  unsigned get(ARCMDKindID ID) {
    switch (ID) {
    case ARCMDKindID::ImpreciseRelease:
      if (!ImpreciseReleaseMDKind)
        ImpreciseReleaseMDKind =
            M->getContext().getMDKindID("clang.imprecise_release");
      return *ImpreciseReleaseMDKind;
    case ARCMDKindID::CopyOnEscape:
      if (!CopyOnEscapeMDKind)
        CopyOnEscapeMDKind =
            M->getContext().getMDKindID("clang.arc.copy_on_escape");
      return *CopyOnEscapeMDKind;
    case ARCMDKindID::NoObjCARCExceptions:
      if (!NoObjCARCExceptionsMDKind)
        NoObjCARCExceptionsMDKind =
            M->getContext().getMDKindID("clang.arc.no_objc_arc_exceptions");
      return *NoObjCARCExceptionsMDKind;
    }
    llvm_unreachable("Covered switch isn't covered?!");
  }
};

} // end namespace objcarc
} // end namespace llvm

#endif
