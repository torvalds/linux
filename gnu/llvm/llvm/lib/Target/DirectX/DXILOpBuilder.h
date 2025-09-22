//===- DXILOpBuilder.h - Helper class for build DIXLOp functions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains class to help build DXIL op functions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_DIRECTX_DXILOPBUILDER_H
#define LLVM_LIB_TARGET_DIRECTX_DXILOPBUILDER_H

#include "DXILConstants.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
class Module;
class IRBuilderBase;
class CallInst;
class Value;
class Type;
class FunctionType;
class Use;

namespace dxil {

class DXILOpBuilder {
public:
  DXILOpBuilder(Module &M, IRBuilderBase &B) : M(M), B(B) {}
  /// Create an instruction that calls DXIL Op with return type, specified
  /// opcode, and call arguments. \param OpCode Opcode of the DXIL Op call
  /// constructed \param ReturnTy Return type of the DXIL Op call constructed
  /// \param OverloadTy Overload type of the DXIL Op call constructed
  /// \return DXIL Op call constructed
  CallInst *createDXILOpCall(dxil::OpCode OpCode, Type *ReturnTy,
                             Type *OverloadTy, SmallVector<Value *> Args);
  Type *getOverloadTy(dxil::OpCode OpCode, FunctionType *FT);
  static const char *getOpCodeName(dxil::OpCode DXILOp);

private:
  Module &M;
  IRBuilderBase &B;
};

} // namespace dxil
} // namespace llvm

#endif
