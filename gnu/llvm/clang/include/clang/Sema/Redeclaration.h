//===- Redeclaration.h - Redeclarations--------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines RedeclarationKind enum.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_REDECLARATION_H
#define LLVM_CLANG_SEMA_REDECLARATION_H

/// Specifies whether (or how) name lookup is being performed for a
/// redeclaration (vs. a reference).
enum class RedeclarationKind {
  /// The lookup is a reference to this name that is not for the
  /// purpose of redeclaring the name.
  NotForRedeclaration = 0,
  /// The lookup results will be used for redeclaration of a name,
  /// if an entity by that name already exists and is visible.
  ForVisibleRedeclaration,
  /// The lookup results will be used for redeclaration of a name
  /// with external linkage; non-visible lookup results with external linkage
  /// may also be found.
  ForExternalRedeclaration
};

#endif // LLVM_CLANG_SEMA_REDECLARATION_H