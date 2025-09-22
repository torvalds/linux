//===--- CodeGenOptions.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines frontend codegen options common to clang and flang
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_DRIVER_CODEGENOPTIONS_H
#define LLVM_FRONTEND_DRIVER_CODEGENOPTIONS_H

namespace llvm {
class Triple;
class TargetLibraryInfoImpl;
} // namespace llvm

namespace llvm::driver {

/// Vector library option used with -fveclib=
enum class VectorLibrary {
  NoLibrary,          // Don't use any vector library.
  Accelerate,         // Use the Accelerate framework.
  LIBMVEC,            // GLIBC vector math library.
  MASSV,              // IBM MASS vector library.
  SVML,               // Intel short vector math library.
  SLEEF,              // SLEEF SIMD Library for Evaluating Elementary Functions.
  Darwin_libsystem_m, // Use Darwin's libsystem_m vector functions.
  ArmPL,              // Arm Performance Libraries.
  AMDLIBM             // AMD vector math library.
};

TargetLibraryInfoImpl *createTLII(llvm::Triple &TargetTriple,
                                  VectorLibrary Veclib);

} // end namespace llvm::driver

#endif
