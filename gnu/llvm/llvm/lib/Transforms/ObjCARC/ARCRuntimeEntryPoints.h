//===- ARCRuntimeEntryPoints.h - ObjC ARC Optimization ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains a class ARCRuntimeEntryPoints for use in
/// creating/managing references to entry points to the arc objective c runtime.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_OBJCARC_ARCRUNTIMEENTRYPOINTS_H
#define LLVM_LIB_TRANSFORMS_OBJCARC_ARCRUNTIMEENTRYPOINTS_H

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

namespace llvm {

class Function;
class Module;

namespace objcarc {

enum class ARCRuntimeEntryPointKind {
  AutoreleaseRV,
  Release,
  Retain,
  RetainBlock,
  Autorelease,
  StoreStrong,
  RetainRV,
  UnsafeClaimRV,
  RetainAutorelease,
  RetainAutoreleaseRV,
};

/// Declarations for ObjC runtime functions and constants. These are initialized
/// lazily to avoid cluttering up the Module with unused declarations.
class ARCRuntimeEntryPoints {
public:
  ARCRuntimeEntryPoints() = default;

  void init(Module *M) {
    TheModule = M;
    AutoreleaseRV = nullptr;
    Release = nullptr;
    Retain = nullptr;
    RetainBlock = nullptr;
    Autorelease = nullptr;
    StoreStrong = nullptr;
    RetainRV = nullptr;
    UnsafeClaimRV = nullptr;
    RetainAutorelease = nullptr;
    RetainAutoreleaseRV = nullptr;
  }

  Function *get(ARCRuntimeEntryPointKind kind) {
    assert(TheModule != nullptr && "Not initialized.");

    switch (kind) {
    case ARCRuntimeEntryPointKind::AutoreleaseRV:
      return getIntrinsicEntryPoint(AutoreleaseRV,
                                    Intrinsic::objc_autoreleaseReturnValue);
    case ARCRuntimeEntryPointKind::Release:
      return getIntrinsicEntryPoint(Release, Intrinsic::objc_release);
    case ARCRuntimeEntryPointKind::Retain:
      return getIntrinsicEntryPoint(Retain, Intrinsic::objc_retain);
    case ARCRuntimeEntryPointKind::RetainBlock:
      return getIntrinsicEntryPoint(RetainBlock, Intrinsic::objc_retainBlock);
    case ARCRuntimeEntryPointKind::Autorelease:
      return getIntrinsicEntryPoint(Autorelease, Intrinsic::objc_autorelease);
    case ARCRuntimeEntryPointKind::StoreStrong:
      return getIntrinsicEntryPoint(StoreStrong, Intrinsic::objc_storeStrong);
    case ARCRuntimeEntryPointKind::RetainRV:
      return getIntrinsicEntryPoint(RetainRV,
                                Intrinsic::objc_retainAutoreleasedReturnValue);
    case ARCRuntimeEntryPointKind::UnsafeClaimRV:
      return getIntrinsicEntryPoint(
          UnsafeClaimRV, Intrinsic::objc_unsafeClaimAutoreleasedReturnValue);
    case ARCRuntimeEntryPointKind::RetainAutorelease:
      return getIntrinsicEntryPoint(RetainAutorelease,
                                    Intrinsic::objc_retainAutorelease);
    case ARCRuntimeEntryPointKind::RetainAutoreleaseRV:
      return getIntrinsicEntryPoint(RetainAutoreleaseRV,
                                Intrinsic::objc_retainAutoreleaseReturnValue);
    }

    llvm_unreachable("Switch should be a covered switch.");
  }

private:
  /// Cached reference to the module which we will insert declarations into.
  Module *TheModule = nullptr;

  /// Declaration for ObjC runtime function objc_autoreleaseReturnValue.
  Function *AutoreleaseRV = nullptr;

  /// Declaration for ObjC runtime function objc_release.
  Function *Release = nullptr;

  /// Declaration for ObjC runtime function objc_retain.
  Function *Retain = nullptr;

  /// Declaration for ObjC runtime function objc_retainBlock.
  Function *RetainBlock = nullptr;

  /// Declaration for ObjC runtime function objc_autorelease.
  Function *Autorelease = nullptr;

  /// Declaration for objc_storeStrong().
  Function *StoreStrong = nullptr;

  /// Declaration for objc_retainAutoreleasedReturnValue().
  Function *RetainRV = nullptr;

  /// Declaration for objc_unsafeClaimAutoreleasedReturnValue().
  Function *UnsafeClaimRV = nullptr;

  /// Declaration for objc_retainAutorelease().
  Function *RetainAutorelease = nullptr;

  /// Declaration for objc_retainAutoreleaseReturnValue().
  Function *RetainAutoreleaseRV = nullptr;

  Function *getIntrinsicEntryPoint(Function *&Decl, Intrinsic::ID IntID) {
    if (Decl)
      return Decl;

    return Decl = Intrinsic::getDeclaration(TheModule, IntID);
  }
};

} // end namespace objcarc

} // end namespace llvm

#endif // LLVM_LIB_TRANSFORMS_OBJCARC_ARCRUNTIMEENTRYPOINTS_H
