//==--- CodeGenABITypes.cpp - Convert Clang types to LLVM types for ABI ----==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CodeGenABITypes is a simple interface for getting LLVM types for
// the parameters and the return value of a function given the Clang
// types.
//
// The class is implemented as a public wrapper around the private
// CodeGenTypes class in lib/CodeGen.
//
//===----------------------------------------------------------------------===//

#include "clang/CodeGen/CodeGenABITypes.h"
#include "CGCXXABI.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/PreprocessorOptions.h"

using namespace clang;
using namespace CodeGen;

void CodeGen::addDefaultFunctionDefinitionAttributes(CodeGenModule &CGM,
                                                     llvm::AttrBuilder &attrs) {
  CGM.addDefaultFunctionDefinitionAttributes(attrs);
}

const CGFunctionInfo &
CodeGen::arrangeObjCMessageSendSignature(CodeGenModule &CGM,
                                         const ObjCMethodDecl *MD,
                                         QualType receiverType) {
  return CGM.getTypes().arrangeObjCMessageSendSignature(MD, receiverType);
}

const CGFunctionInfo &
CodeGen::arrangeFreeFunctionType(CodeGenModule &CGM,
                                 CanQual<FunctionProtoType> Ty) {
  return CGM.getTypes().arrangeFreeFunctionType(Ty);
}

const CGFunctionInfo &
CodeGen::arrangeFreeFunctionType(CodeGenModule &CGM,
                                 CanQual<FunctionNoProtoType> Ty) {
  return CGM.getTypes().arrangeFreeFunctionType(Ty);
}

const CGFunctionInfo &
CodeGen::arrangeCXXMethodType(CodeGenModule &CGM,
                              const CXXRecordDecl *RD,
                              const FunctionProtoType *FTP,
                              const CXXMethodDecl *MD) {
  return CGM.getTypes().arrangeCXXMethodType(RD, FTP, MD);
}

const CGFunctionInfo &
CodeGen::arrangeFreeFunctionCall(CodeGenModule &CGM,
                                 CanQualType returnType,
                                 ArrayRef<CanQualType> argTypes,
                                 FunctionType::ExtInfo info,
                                 RequiredArgs args) {
  return CGM.getTypes().arrangeLLVMFunctionInfo(returnType, FnInfoOpts::None,
                                                argTypes, info, {}, args);
}

ImplicitCXXConstructorArgs
CodeGen::getImplicitCXXConstructorArgs(CodeGenModule &CGM,
                                       const CXXConstructorDecl *D) {
  // We have to create a dummy CodeGenFunction here to pass to
  // getImplicitConstructorArgs(). In some cases (base and delegating
  // constructor calls), getImplicitConstructorArgs() can reach into the
  // CodeGenFunction to find parameters of the calling constructor to pass on to
  // the called constructor, but that can't happen here because we're asking for
  // the args for a complete, non-delegating constructor call.
  CodeGenFunction CGF(CGM, /* suppressNewContext= */ true);
  CGCXXABI::AddedStructorArgs addedArgs =
      CGM.getCXXABI().getImplicitConstructorArgs(CGF, D, Ctor_Complete,
                                                 /* ForVirtualBase= */ false,
                                                 /* Delegating= */ false);
  ImplicitCXXConstructorArgs implicitArgs;
  for (const auto &arg : addedArgs.Prefix) {
    implicitArgs.Prefix.push_back(arg.Value);
  }
  for (const auto &arg : addedArgs.Suffix) {
    implicitArgs.Suffix.push_back(arg.Value);
  }
  return implicitArgs;
}

llvm::FunctionType *
CodeGen::convertFreeFunctionType(CodeGenModule &CGM, const FunctionDecl *FD) {
  assert(FD != nullptr && "Expected a non-null function declaration!");
  llvm::Type *T = CGM.getTypes().ConvertType(FD->getType());

  if (auto FT = dyn_cast<llvm::FunctionType>(T))
    return FT;

  return nullptr;
}

llvm::Type *
CodeGen::convertTypeForMemory(CodeGenModule &CGM, QualType T) {
  return CGM.getTypes().ConvertTypeForMem(T);
}

unsigned CodeGen::getLLVMFieldNumber(CodeGenModule &CGM,
                                     const RecordDecl *RD,
                                     const FieldDecl *FD) {
  return CGM.getTypes().getCGRecordLayout(RD).getLLVMFieldNo(FD);
}

llvm::Value *CodeGen::getCXXDestructorImplicitParam(
    CodeGenModule &CGM, llvm::BasicBlock *InsertBlock,
    llvm::BasicBlock::iterator InsertPoint, const CXXDestructorDecl *D,
    CXXDtorType Type, bool ForVirtualBase, bool Delegating) {
  CodeGenFunction CGF(CGM, /*suppressNewContext=*/true);
  CGF.CurCodeDecl = D;
  CGF.CurFuncDecl = D;
  CGF.CurFn = InsertBlock->getParent();
  CGF.Builder.SetInsertPoint(InsertBlock, InsertPoint);
  return CGM.getCXXABI().getCXXDestructorImplicitParam(
      CGF, D, Type, ForVirtualBase, Delegating);
}
