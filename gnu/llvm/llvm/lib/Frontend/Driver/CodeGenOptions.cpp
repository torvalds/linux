//===--- CodeGenOptions.cpp - Shared codegen option handling --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Frontend/Driver/CodeGenOptions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm::driver {

TargetLibraryInfoImpl *createTLII(llvm::Triple &TargetTriple,
                                  driver::VectorLibrary Veclib) {
  TargetLibraryInfoImpl *TLII = new TargetLibraryInfoImpl(TargetTriple);

  using VectorLibrary = llvm::driver::VectorLibrary;
  switch (Veclib) {
  case VectorLibrary::Accelerate:
    TLII->addVectorizableFunctionsFromVecLib(TargetLibraryInfoImpl::Accelerate,
                                             TargetTriple);
    break;
  case VectorLibrary::LIBMVEC:
    TLII->addVectorizableFunctionsFromVecLib(TargetLibraryInfoImpl::LIBMVEC_X86,
                                             TargetTriple);
    break;
  case VectorLibrary::MASSV:
    TLII->addVectorizableFunctionsFromVecLib(TargetLibraryInfoImpl::MASSV,
                                             TargetTriple);
    break;
  case VectorLibrary::SVML:
    TLII->addVectorizableFunctionsFromVecLib(TargetLibraryInfoImpl::SVML,
                                             TargetTriple);
    break;
  case VectorLibrary::SLEEF:
    TLII->addVectorizableFunctionsFromVecLib(TargetLibraryInfoImpl::SLEEFGNUABI,
                                             TargetTriple);
    break;
  case VectorLibrary::Darwin_libsystem_m:
    TLII->addVectorizableFunctionsFromVecLib(
        TargetLibraryInfoImpl::DarwinLibSystemM, TargetTriple);
    break;
  case VectorLibrary::ArmPL:
    TLII->addVectorizableFunctionsFromVecLib(TargetLibraryInfoImpl::ArmPL,
                                             TargetTriple);
    break;
  case VectorLibrary::AMDLIBM:
    TLII->addVectorizableFunctionsFromVecLib(TargetLibraryInfoImpl::AMDLIBM,
                                             TargetTriple);
    break;
  default:
    break;
  }
  return TLII;
}

} // namespace llvm::driver
