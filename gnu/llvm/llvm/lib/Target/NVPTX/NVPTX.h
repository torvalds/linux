//===-- NVPTX.h - Top-level interface for NVPTX representation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in
// the LLVM NVPTX back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTX_H
#define LLVM_LIB_TARGET_NVPTX_NVPTX_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class FunctionPass;
class MachineFunctionPass;
class NVPTXTargetMachine;
class PassRegistry;

namespace NVPTXCC {
enum CondCodes {
  EQ,
  NE,
  LT,
  LE,
  GT,
  GE
};
}

FunctionPass *createNVPTXISelDag(NVPTXTargetMachine &TM,
                                 llvm::CodeGenOptLevel OptLevel);
ModulePass *createNVPTXAssignValidGlobalNamesPass();
ModulePass *createGenericToNVVMLegacyPass();
ModulePass *createNVPTXCtorDtorLoweringLegacyPass();
FunctionPass *createNVVMIntrRangePass();
FunctionPass *createNVVMReflectPass(unsigned int SmVersion);
MachineFunctionPass *createNVPTXPrologEpilogPass();
MachineFunctionPass *createNVPTXReplaceImageHandlesPass();
FunctionPass *createNVPTXImageOptimizerPass();
FunctionPass *createNVPTXLowerArgsPass();
FunctionPass *createNVPTXLowerAllocaPass();
FunctionPass *createNVPTXLowerUnreachablePass(bool TrapUnreachable,
                                              bool NoTrapAfterNoreturn);
MachineFunctionPass *createNVPTXPeephole();
MachineFunctionPass *createNVPTXProxyRegErasurePass();

struct NVVMIntrRangePass : PassInfoMixin<NVVMIntrRangePass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct NVVMReflectPass : PassInfoMixin<NVVMReflectPass> {
  NVVMReflectPass();
  NVVMReflectPass(unsigned SmVersion) : SmVersion(SmVersion) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  unsigned SmVersion;
};

struct GenericToNVVMPass : PassInfoMixin<GenericToNVVMPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

namespace NVPTX {
enum DrvInterface {
  NVCL,
  CUDA
};

// A field inside TSFlags needs a shift and a mask. The usage is
// always as follows :
// ((TSFlags & fieldMask) >> fieldShift)
// The enum keeps the mask, the shift, and all valid values of the
// field in one place.
enum VecInstType {
  VecInstTypeShift = 0,
  VecInstTypeMask = 0xF,

  VecNOP = 0,
  VecLoad = 1,
  VecStore = 2,
  VecBuild = 3,
  VecShuffle = 4,
  VecExtract = 5,
  VecInsert = 6,
  VecDest = 7,
  VecOther = 15
};

enum SimpleMove {
  SimpleMoveMask = 0x10,
  SimpleMoveShift = 4
};
enum LoadStore {
  isLoadMask = 0x20,
  isLoadShift = 5,
  isStoreMask = 0x40,
  isStoreShift = 6
};

namespace PTXLdStInstCode {
enum AddressSpace {
  GENERIC = 0,
  GLOBAL = 1,
  CONSTANT = 2,
  SHARED = 3,
  PARAM = 4,
  LOCAL = 5
};
enum FromType {
  Unsigned = 0,
  Signed,
  Float,
  Untyped
};
enum VecType {
  Scalar = 1,
  V2 = 2,
  V4 = 4
};
}

/// PTXCvtMode - Conversion code enumeration
namespace PTXCvtMode {
enum CvtMode {
  NONE = 0,
  RNI,
  RZI,
  RMI,
  RPI,
  RN,
  RZ,
  RM,
  RP,
  RNA,

  BASE_MASK = 0x0F,
  FTZ_FLAG = 0x10,
  SAT_FLAG = 0x20,
  RELU_FLAG = 0x40
};
}

/// PTXCmpMode - Comparison mode enumeration
namespace PTXCmpMode {
enum CmpMode {
  EQ = 0,
  NE,
  LT,
  LE,
  GT,
  GE,
  LO,
  LS,
  HI,
  HS,
  EQU,
  NEU,
  LTU,
  LEU,
  GTU,
  GEU,
  NUM,
  // NAN is a MACRO
  NotANumber,

  BASE_MASK = 0xFF,
  FTZ_FLAG = 0x100
};
}

namespace PTXPrmtMode {
enum PrmtMode {
  NONE,
  F4E,
  B4E,
  RC8,
  ECL,
  ECR,
  RC16,
};
}
}
void initializeNVPTXDAGToDAGISelLegacyPass(PassRegistry &);
} // namespace llvm

// Defines symbolic names for NVPTX registers.  This defines a mapping from
// register name to register number.
#define GET_REGINFO_ENUM
#include "NVPTXGenRegisterInfo.inc"

// Defines symbolic names for the NVPTX instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "NVPTXGenInstrInfo.inc"

#endif
