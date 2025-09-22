//===- ARCInstKind.cpp - ObjC ARC Optimization ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines several utility functions used by various ARC
/// optimizations which are IMHO too big to be in a header file.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/IR/Intrinsics.h"

using namespace llvm;
using namespace llvm::objcarc;

raw_ostream &llvm::objcarc::operator<<(raw_ostream &OS,
                                       const ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::Retain:
    return OS << "ARCInstKind::Retain";
  case ARCInstKind::RetainRV:
    return OS << "ARCInstKind::RetainRV";
  case ARCInstKind::UnsafeClaimRV:
    return OS << "ARCInstKind::UnsafeClaimRV";
  case ARCInstKind::RetainBlock:
    return OS << "ARCInstKind::RetainBlock";
  case ARCInstKind::Release:
    return OS << "ARCInstKind::Release";
  case ARCInstKind::Autorelease:
    return OS << "ARCInstKind::Autorelease";
  case ARCInstKind::AutoreleaseRV:
    return OS << "ARCInstKind::AutoreleaseRV";
  case ARCInstKind::AutoreleasepoolPush:
    return OS << "ARCInstKind::AutoreleasepoolPush";
  case ARCInstKind::AutoreleasepoolPop:
    return OS << "ARCInstKind::AutoreleasepoolPop";
  case ARCInstKind::NoopCast:
    return OS << "ARCInstKind::NoopCast";
  case ARCInstKind::FusedRetainAutorelease:
    return OS << "ARCInstKind::FusedRetainAutorelease";
  case ARCInstKind::FusedRetainAutoreleaseRV:
    return OS << "ARCInstKind::FusedRetainAutoreleaseRV";
  case ARCInstKind::LoadWeakRetained:
    return OS << "ARCInstKind::LoadWeakRetained";
  case ARCInstKind::StoreWeak:
    return OS << "ARCInstKind::StoreWeak";
  case ARCInstKind::InitWeak:
    return OS << "ARCInstKind::InitWeak";
  case ARCInstKind::LoadWeak:
    return OS << "ARCInstKind::LoadWeak";
  case ARCInstKind::MoveWeak:
    return OS << "ARCInstKind::MoveWeak";
  case ARCInstKind::CopyWeak:
    return OS << "ARCInstKind::CopyWeak";
  case ARCInstKind::DestroyWeak:
    return OS << "ARCInstKind::DestroyWeak";
  case ARCInstKind::StoreStrong:
    return OS << "ARCInstKind::StoreStrong";
  case ARCInstKind::CallOrUser:
    return OS << "ARCInstKind::CallOrUser";
  case ARCInstKind::Call:
    return OS << "ARCInstKind::Call";
  case ARCInstKind::User:
    return OS << "ARCInstKind::User";
  case ARCInstKind::IntrinsicUser:
    return OS << "ARCInstKind::IntrinsicUser";
  case ARCInstKind::None:
    return OS << "ARCInstKind::None";
  }
  llvm_unreachable("Unknown instruction class!");
}

ARCInstKind llvm::objcarc::GetFunctionClass(const Function *F) {

  Intrinsic::ID ID = F->getIntrinsicID();
  switch (ID) {
  default:
    return ARCInstKind::CallOrUser;
  case Intrinsic::objc_autorelease:
    return ARCInstKind::Autorelease;
  case Intrinsic::objc_autoreleasePoolPop:
    return ARCInstKind::AutoreleasepoolPop;
  case Intrinsic::objc_autoreleasePoolPush:
    return ARCInstKind::AutoreleasepoolPush;
  case Intrinsic::objc_autoreleaseReturnValue:
    return ARCInstKind::AutoreleaseRV;
  case Intrinsic::objc_copyWeak:
    return ARCInstKind::CopyWeak;
  case Intrinsic::objc_destroyWeak:
    return ARCInstKind::DestroyWeak;
  case Intrinsic::objc_initWeak:
    return ARCInstKind::InitWeak;
  case Intrinsic::objc_loadWeak:
    return ARCInstKind::LoadWeak;
  case Intrinsic::objc_loadWeakRetained:
    return ARCInstKind::LoadWeakRetained;
  case Intrinsic::objc_moveWeak:
    return ARCInstKind::MoveWeak;
  case Intrinsic::objc_release:
    return ARCInstKind::Release;
  case Intrinsic::objc_retain:
    return ARCInstKind::Retain;
  case Intrinsic::objc_retainAutorelease:
    return ARCInstKind::FusedRetainAutorelease;
  case Intrinsic::objc_retainAutoreleaseReturnValue:
    return ARCInstKind::FusedRetainAutoreleaseRV;
  case Intrinsic::objc_retainAutoreleasedReturnValue:
    return ARCInstKind::RetainRV;
  case Intrinsic::objc_retainBlock:
    return ARCInstKind::RetainBlock;
  case Intrinsic::objc_storeStrong:
    return ARCInstKind::StoreStrong;
  case Intrinsic::objc_storeWeak:
    return ARCInstKind::StoreWeak;
  case Intrinsic::objc_clang_arc_use:
    return ARCInstKind::IntrinsicUser;
  case Intrinsic::objc_unsafeClaimAutoreleasedReturnValue:
    return ARCInstKind::UnsafeClaimRV;
  case Intrinsic::objc_retainedObject:
    return ARCInstKind::NoopCast;
  case Intrinsic::objc_unretainedObject:
    return ARCInstKind::NoopCast;
  case Intrinsic::objc_unretainedPointer:
    return ARCInstKind::NoopCast;
  case Intrinsic::objc_retain_autorelease:
    return ARCInstKind::FusedRetainAutorelease;
  case Intrinsic::objc_sync_enter:
    return ARCInstKind::User;
  case Intrinsic::objc_sync_exit:
    return ARCInstKind::User;
  case Intrinsic::objc_clang_arc_noop_use:
  case Intrinsic::objc_arc_annotation_topdown_bbstart:
  case Intrinsic::objc_arc_annotation_topdown_bbend:
  case Intrinsic::objc_arc_annotation_bottomup_bbstart:
  case Intrinsic::objc_arc_annotation_bottomup_bbend:
    // Ignore annotation calls. This is important to stop the
    // optimizer from treating annotations as uses which would
    // make the state of the pointers they are attempting to
    // elucidate to be incorrect.
    return ARCInstKind::None;
  }
}

// A list of intrinsics that we know do not use objc pointers or decrement
// ref counts.
static bool isInertIntrinsic(unsigned ID) {
  // TODO: Make this into a covered switch.
  switch (ID) {
  case Intrinsic::returnaddress:
  case Intrinsic::addressofreturnaddress:
  case Intrinsic::frameaddress:
  case Intrinsic::stacksave:
  case Intrinsic::stackrestore:
  case Intrinsic::vastart:
  case Intrinsic::vacopy:
  case Intrinsic::vaend:
  case Intrinsic::objectsize:
  case Intrinsic::prefetch:
  case Intrinsic::stackprotector:
  case Intrinsic::eh_return_i32:
  case Intrinsic::eh_return_i64:
  case Intrinsic::eh_typeid_for:
  case Intrinsic::eh_dwarf_cfa:
  case Intrinsic::eh_sjlj_lsda:
  case Intrinsic::eh_sjlj_functioncontext:
  case Intrinsic::init_trampoline:
  case Intrinsic::adjust_trampoline:
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
  case Intrinsic::invariant_start:
  case Intrinsic::invariant_end:
  // Don't let dbg info affect our results.
  case Intrinsic::dbg_declare:
  case Intrinsic::dbg_value:
  case Intrinsic::dbg_label:
    // Short cut: Some intrinsics obviously don't use ObjC pointers.
    return true;
  default:
    return false;
  }
}

// A list of intrinsics that we know do not use objc pointers or decrement
// ref counts.
static bool isUseOnlyIntrinsic(unsigned ID) {
  // We are conservative and even though intrinsics are unlikely to touch
  // reference counts, we white list them for safety.
  //
  // TODO: Expand this into a covered switch. There is a lot more here.
  switch (ID) {
  case Intrinsic::memcpy:
  case Intrinsic::memmove:
  case Intrinsic::memset:
    return true;
  default:
    return false;
  }
}

/// Determine what kind of construct V is.
ARCInstKind llvm::objcarc::GetARCInstKind(const Value *V) {
  if (const Instruction *I = dyn_cast<Instruction>(V)) {
    // Any instruction other than bitcast and gep with a pointer operand have a
    // use of an objc pointer. Bitcasts, GEPs, Selects, PHIs transfer a pointer
    // to a subsequent use, rather than using it themselves, in this sense.
    // As a short cut, several other opcodes are known to have no pointer
    // operands of interest. And ret is never followed by a release, so it's
    // not interesting to examine.
    switch (I->getOpcode()) {
    case Instruction::Call: {
      const CallInst *CI = cast<CallInst>(I);
      // See if we have a function that we know something about.
      if (const Function *F = CI->getCalledFunction()) {
        ARCInstKind Class = GetFunctionClass(F);
        if (Class != ARCInstKind::CallOrUser)
          return Class;
        Intrinsic::ID ID = F->getIntrinsicID();
        if (isInertIntrinsic(ID))
          return ARCInstKind::None;
        if (isUseOnlyIntrinsic(ID))
          return ARCInstKind::User;
      }

      // Otherwise, be conservative.
      return GetCallSiteClass(*CI);
    }
    case Instruction::Invoke:
      // Otherwise, be conservative.
      return GetCallSiteClass(cast<InvokeInst>(*I));
    case Instruction::BitCast:
    case Instruction::GetElementPtr:
    case Instruction::Select:
    case Instruction::PHI:
    case Instruction::Ret:
    case Instruction::Br:
    case Instruction::Switch:
    case Instruction::IndirectBr:
    case Instruction::Alloca:
    case Instruction::VAArg:
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::SDiv:
    case Instruction::UDiv:
    case Instruction::FDiv:
    case Instruction::SRem:
    case Instruction::URem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::SExt:
    case Instruction::ZExt:
    case Instruction::Trunc:
    case Instruction::IntToPtr:
    case Instruction::FCmp:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::InsertElement:
    case Instruction::ExtractElement:
    case Instruction::ShuffleVector:
    case Instruction::ExtractValue:
      break;
    case Instruction::ICmp:
      // Comparing a pointer with null, or any other constant, isn't an
      // interesting use, because we don't care what the pointer points to, or
      // about the values of any other dynamic reference-counted pointers.
      if (IsPotentialRetainableObjPtr(I->getOperand(1)))
        return ARCInstKind::User;
      break;
    default:
      // For anything else, check all the operands.
      // Note that this includes both operands of a Store: while the first
      // operand isn't actually being dereferenced, it is being stored to
      // memory where we can no longer track who might read it and dereference
      // it, so we have to consider it potentially used.
      for (const Use &U : I->operands())
        if (IsPotentialRetainableObjPtr(U))
          return ARCInstKind::User;
    }
  }

  // Otherwise, it's totally inert for ARC purposes.
  return ARCInstKind::None;
}

/// Test if the given class is a kind of user.
bool llvm::objcarc::IsUser(ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::User:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::IntrinsicUser:
    return true;
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::RetainBlock:
  case ARCInstKind::Release:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::NoopCast:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::Call:
  case ARCInstKind::None:
  case ARCInstKind::UnsafeClaimRV:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test if the given class is objc_retain or equivalent.
bool llvm::objcarc::IsRetain(ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
    return true;
  // I believe we treat retain block as not a retain since it can copy its
  // block.
  case ARCInstKind::RetainBlock:
  case ARCInstKind::Release:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::NoopCast:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::User:
  case ARCInstKind::None:
  case ARCInstKind::UnsafeClaimRV:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test if the given class is objc_autorelease or equivalent.
bool llvm::objcarc::IsAutorelease(ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
    return true;
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV:
  case ARCInstKind::RetainBlock:
  case ARCInstKind::Release:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::NoopCast:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::User:
  case ARCInstKind::None:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test if the given class represents instructions which return their
/// argument verbatim.
bool llvm::objcarc::IsForwarding(ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::NoopCast:
    return true;
  case ARCInstKind::RetainBlock:
  case ARCInstKind::Release:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::User:
  case ARCInstKind::None:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test if the given class represents instructions which do nothing if
/// passed a null pointer.
bool llvm::objcarc::IsNoopOnNull(ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV:
  case ARCInstKind::Release:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::RetainBlock:
    return true;
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::User:
  case ARCInstKind::None:
  case ARCInstKind::NoopCast:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test if the given class represents instructions which do nothing if
/// passed a global variable.
bool llvm::objcarc::IsNoopOnGlobal(ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV:
  case ARCInstKind::Release:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::RetainBlock:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
    return true;
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::User:
  case ARCInstKind::None:
  case ARCInstKind::NoopCast:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test if the given class represents instructions which are always safe
/// to mark with the "tail" keyword.
bool llvm::objcarc::IsAlwaysTail(ARCInstKind Class) {
  // ARCInstKind::RetainBlock may be given a stack argument.
  switch (Class) {
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV:
  case ARCInstKind::AutoreleaseRV:
    return true;
  case ARCInstKind::Release:
  case ARCInstKind::Autorelease:
  case ARCInstKind::RetainBlock:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::User:
  case ARCInstKind::None:
  case ARCInstKind::NoopCast:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test if the given class represents instructions which are never safe
/// to mark with the "tail" keyword.
bool llvm::objcarc::IsNeverTail(ARCInstKind Class) {
  /// It is never safe to tail call objc_autorelease since by tail calling
  /// objc_autorelease: fast autoreleasing causing our object to be potentially
  /// reclaimed from the autorelease pool which violates the semantics of
  /// __autoreleasing types in ARC.
  switch (Class) {
  case ARCInstKind::Autorelease:
    return true;
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::Release:
  case ARCInstKind::RetainBlock:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::User:
  case ARCInstKind::None:
  case ARCInstKind::NoopCast:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test if the given class represents instructions which are always safe
/// to mark with the nounwind attribute.
bool llvm::objcarc::IsNoThrow(ARCInstKind Class) {
  // objc_retainBlock is not nounwind because it calls user copy constructors
  // which could theoretically throw.
  switch (Class) {
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV:
  case ARCInstKind::Release:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
    return true;
  case ARCInstKind::RetainBlock:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::User:
  case ARCInstKind::None:
  case ARCInstKind::NoopCast:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

/// Test whether the given instruction can autorelease any pointer or cause an
/// autoreleasepool pop.
///
/// This means that it *could* interrupt the RV optimization.
bool llvm::objcarc::CanInterruptRV(ARCInstKind Class) {
  switch (Class) {
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
    return true;
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::UnsafeClaimRV:
  case ARCInstKind::Release:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::RetainBlock:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::User:
  case ARCInstKind::None:
  case ARCInstKind::NoopCast:
    return false;
  }
  llvm_unreachable("covered switch isn't covered?");
}

bool llvm::objcarc::CanDecrementRefCount(ARCInstKind Kind) {
  switch (Kind) {
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::NoopCast:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
  case ARCInstKind::IntrinsicUser:
  case ARCInstKind::User:
  case ARCInstKind::None:
    return false;

  // The cases below are conservative.

  // RetainBlock can result in user defined copy constructors being called
  // implying releases may occur.
  case ARCInstKind::RetainBlock:
  case ARCInstKind::Release:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::AutoreleasepoolPop:
  case ARCInstKind::LoadWeakRetained:
  case ARCInstKind::StoreWeak:
  case ARCInstKind::InitWeak:
  case ARCInstKind::LoadWeak:
  case ARCInstKind::MoveWeak:
  case ARCInstKind::CopyWeak:
  case ARCInstKind::DestroyWeak:
  case ARCInstKind::StoreStrong:
  case ARCInstKind::CallOrUser:
  case ARCInstKind::Call:
  case ARCInstKind::UnsafeClaimRV:
    return true;
  }

  llvm_unreachable("covered switch isn't covered?");
}
