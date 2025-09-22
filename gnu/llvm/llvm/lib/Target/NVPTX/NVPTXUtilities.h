//===-- NVPTXUtilities - Utilities -----------------------------*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the NVVM specific utility functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXUTILITIES_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXUTILITIES_H

#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"
#include <cstdarg>
#include <set>
#include <string>
#include <vector>

namespace llvm {

class TargetMachine;

void clearAnnotationCache(const Module *);

bool findOneNVVMAnnotation(const GlobalValue *, const std::string &,
                           unsigned &);
bool findAllNVVMAnnotation(const GlobalValue *, const std::string &,
                           std::vector<unsigned> &);

bool isTexture(const Value &);
bool isSurface(const Value &);
bool isSampler(const Value &);
bool isImage(const Value &);
bool isImageReadOnly(const Value &);
bool isImageWriteOnly(const Value &);
bool isImageReadWrite(const Value &);
bool isManaged(const Value &);

std::string getTextureName(const Value &);
std::string getSurfaceName(const Value &);
std::string getSamplerName(const Value &);

std::optional<unsigned> getMaxNTIDx(const Function &);
std::optional<unsigned> getMaxNTIDy(const Function &);
std::optional<unsigned> getMaxNTIDz(const Function &);
std::optional<unsigned> getMaxNTID(const Function &F);

std::optional<unsigned> getReqNTIDx(const Function &);
std::optional<unsigned> getReqNTIDy(const Function &);
std::optional<unsigned> getReqNTIDz(const Function &);
std::optional<unsigned> getReqNTID(const Function &);

bool getMaxClusterRank(const Function &, unsigned &);
bool getMinCTASm(const Function &, unsigned &);
bool getMaxNReg(const Function &, unsigned &);
bool isKernelFunction(const Function &);
bool isParamGridConstant(const Value &);

MaybeAlign getAlign(const Function &, unsigned);
MaybeAlign getAlign(const CallInst &, unsigned);
Function *getMaybeBitcastedCallee(const CallBase *CB);

// PTX ABI requires all scalar argument/return values to have
// bit-size as a power of two of at least 32 bits.
inline unsigned promoteScalarArgumentSize(unsigned size) {
  if (size <= 32)
    return 32;
  else if (size <= 64)
    return 64;
  else
    return size;
}

bool shouldEmitPTXNoReturn(const Value *V, const TargetMachine &TM);

bool Isv2x16VT(EVT VT);
}

#endif
