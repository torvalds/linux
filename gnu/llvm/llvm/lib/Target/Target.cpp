//===-- Target.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the common infrastructure (including C bindings) for
// libLLVMTarget.a, which implements target information.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Target.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include <cstring>

using namespace llvm;

// Avoid including "llvm-c/Core.h" for compile time, fwd-declare this instead.
extern "C" LLVMContextRef LLVMGetGlobalContext(void);

inline TargetLibraryInfoImpl *unwrap(LLVMTargetLibraryInfoRef P) {
  return reinterpret_cast<TargetLibraryInfoImpl*>(P);
}

inline LLVMTargetLibraryInfoRef wrap(const TargetLibraryInfoImpl *P) {
  TargetLibraryInfoImpl *X = const_cast<TargetLibraryInfoImpl*>(P);
  return reinterpret_cast<LLVMTargetLibraryInfoRef>(X);
}

void llvm::initializeTarget(PassRegistry &Registry) {
  initializeTargetLibraryInfoWrapperPassPass(Registry);
  initializeTargetTransformInfoWrapperPassPass(Registry);
}

LLVMTargetDataRef LLVMGetModuleDataLayout(LLVMModuleRef M) {
  return wrap(&unwrap(M)->getDataLayout());
}

void LLVMSetModuleDataLayout(LLVMModuleRef M, LLVMTargetDataRef DL) {
  unwrap(M)->setDataLayout(*unwrap(DL));
}

LLVMTargetDataRef LLVMCreateTargetData(const char *StringRep) {
  return wrap(new DataLayout(StringRep));
}

void LLVMDisposeTargetData(LLVMTargetDataRef TD) {
  delete unwrap(TD);
}

void LLVMAddTargetLibraryInfo(LLVMTargetLibraryInfoRef TLI,
                              LLVMPassManagerRef PM) {
  unwrap(PM)->add(new TargetLibraryInfoWrapperPass(*unwrap(TLI)));
}

char *LLVMCopyStringRepOfTargetData(LLVMTargetDataRef TD) {
  std::string StringRep = unwrap(TD)->getStringRepresentation();
  return strdup(StringRep.c_str());
}

LLVMByteOrdering LLVMByteOrder(LLVMTargetDataRef TD) {
  return unwrap(TD)->isLittleEndian() ? LLVMLittleEndian : LLVMBigEndian;
}

unsigned LLVMPointerSize(LLVMTargetDataRef TD) {
  return unwrap(TD)->getPointerSize(0);
}

unsigned LLVMPointerSizeForAS(LLVMTargetDataRef TD, unsigned AS) {
  return unwrap(TD)->getPointerSize(AS);
}

LLVMTypeRef LLVMIntPtrType(LLVMTargetDataRef TD) {
  return wrap(unwrap(TD)->getIntPtrType(*unwrap(LLVMGetGlobalContext())));
}

LLVMTypeRef LLVMIntPtrTypeForAS(LLVMTargetDataRef TD, unsigned AS) {
  return wrap(unwrap(TD)->getIntPtrType(*unwrap(LLVMGetGlobalContext()), AS));
}

LLVMTypeRef LLVMIntPtrTypeInContext(LLVMContextRef C, LLVMTargetDataRef TD) {
  return wrap(unwrap(TD)->getIntPtrType(*unwrap(C)));
}

LLVMTypeRef LLVMIntPtrTypeForASInContext(LLVMContextRef C, LLVMTargetDataRef TD, unsigned AS) {
  return wrap(unwrap(TD)->getIntPtrType(*unwrap(C), AS));
}

unsigned long long LLVMSizeOfTypeInBits(LLVMTargetDataRef TD, LLVMTypeRef Ty) {
  return unwrap(TD)->getTypeSizeInBits(unwrap(Ty));
}

unsigned long long LLVMStoreSizeOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty) {
  return unwrap(TD)->getTypeStoreSize(unwrap(Ty));
}

unsigned long long LLVMABISizeOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty) {
  return unwrap(TD)->getTypeAllocSize(unwrap(Ty));
}

unsigned LLVMABIAlignmentOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty) {
  return unwrap(TD)->getABITypeAlign(unwrap(Ty)).value();
}

unsigned LLVMCallFrameAlignmentOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty) {
  return unwrap(TD)->getABITypeAlign(unwrap(Ty)).value();
}

unsigned LLVMPreferredAlignmentOfType(LLVMTargetDataRef TD, LLVMTypeRef Ty) {
  return unwrap(TD)->getPrefTypeAlign(unwrap(Ty)).value();
}

unsigned LLVMPreferredAlignmentOfGlobal(LLVMTargetDataRef TD,
                                        LLVMValueRef GlobalVar) {
  return unwrap(TD)
      ->getPreferredAlign(unwrap<GlobalVariable>(GlobalVar))
      .value();
}

unsigned LLVMElementAtOffset(LLVMTargetDataRef TD, LLVMTypeRef StructTy,
                             unsigned long long Offset) {
  StructType *STy = unwrap<StructType>(StructTy);
  return unwrap(TD)->getStructLayout(STy)->getElementContainingOffset(Offset);
}

unsigned long long LLVMOffsetOfElement(LLVMTargetDataRef TD, LLVMTypeRef StructTy,
                                       unsigned Element) {
  StructType *STy = unwrap<StructType>(StructTy);
  return unwrap(TD)->getStructLayout(STy)->getElementOffset(Element);
}
