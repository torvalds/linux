//===------ CGGPUBuiltin.cpp - Codegen for GPU builtins -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generates code for built-in GPU calls which are not runtime-specific.
// (Runtime-specific codegen lives in programming model specific files.)
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "clang/Basic/Builtins.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/Utils/AMDGPUEmitPrintf.h"

using namespace clang;
using namespace CodeGen;

namespace {
llvm::Function *GetVprintfDeclaration(llvm::Module &M) {
  llvm::Type *ArgTypes[] = {llvm::PointerType::getUnqual(M.getContext()),
                            llvm::PointerType::getUnqual(M.getContext())};
  llvm::FunctionType *VprintfFuncType = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(M.getContext()), ArgTypes, false);

  if (auto *F = M.getFunction("vprintf")) {
    // Our CUDA system header declares vprintf with the right signature, so
    // nobody else should have been able to declare vprintf with a bogus
    // signature.
    assert(F->getFunctionType() == VprintfFuncType);
    return F;
  }

  // vprintf doesn't already exist; create a declaration and insert it into the
  // module.
  return llvm::Function::Create(
      VprintfFuncType, llvm::GlobalVariable::ExternalLinkage, "vprintf", &M);
}

llvm::Function *GetOpenMPVprintfDeclaration(CodeGenModule &CGM) {
  const char *Name = "__llvm_omp_vprintf";
  llvm::Module &M = CGM.getModule();
  llvm::Type *ArgTypes[] = {llvm::PointerType::getUnqual(M.getContext()),
                            llvm::PointerType::getUnqual(M.getContext()),
                            llvm::Type::getInt32Ty(M.getContext())};
  llvm::FunctionType *VprintfFuncType = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(M.getContext()), ArgTypes, false);

  if (auto *F = M.getFunction(Name)) {
    if (F->getFunctionType() != VprintfFuncType) {
      CGM.Error(SourceLocation(),
                "Invalid type declaration for __llvm_omp_vprintf");
      return nullptr;
    }
    return F;
  }

  return llvm::Function::Create(
      VprintfFuncType, llvm::GlobalVariable::ExternalLinkage, Name, &M);
}

// Transforms a call to printf into a call to the NVPTX vprintf syscall (which
// isn't particularly special; it's invoked just like a regular function).
// vprintf takes two args: A format string, and a pointer to a buffer containing
// the varargs.
//
// For example, the call
//
//   printf("format string", arg1, arg2, arg3);
//
// is converted into something resembling
//
//   struct Tmp {
//     Arg1 a1;
//     Arg2 a2;
//     Arg3 a3;
//   };
//   char* buf = alloca(sizeof(Tmp));
//   *(Tmp*)buf = {a1, a2, a3};
//   vprintf("format string", buf);
//
// buf is aligned to the max of {alignof(Arg1), ...}.  Furthermore, each of the
// args is itself aligned to its preferred alignment.
//
// Note that by the time this function runs, E's args have already undergone the
// standard C vararg promotion (short -> int, float -> double, etc.).

std::pair<llvm::Value *, llvm::TypeSize>
packArgsIntoNVPTXFormatBuffer(CodeGenFunction *CGF, const CallArgList &Args) {
  const llvm::DataLayout &DL = CGF->CGM.getDataLayout();
  llvm::LLVMContext &Ctx = CGF->CGM.getLLVMContext();
  CGBuilderTy &Builder = CGF->Builder;

  // Construct and fill the args buffer that we'll pass to vprintf.
  if (Args.size() <= 1) {
    // If there are no args, pass a null pointer and size 0
    llvm::Value *BufferPtr =
        llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(Ctx));
    return {BufferPtr, llvm::TypeSize::getFixed(0)};
  } else {
    llvm::SmallVector<llvm::Type *, 8> ArgTypes;
    for (unsigned I = 1, NumArgs = Args.size(); I < NumArgs; ++I)
      ArgTypes.push_back(Args[I].getRValue(*CGF).getScalarVal()->getType());

    // Using llvm::StructType is correct only because printf doesn't accept
    // aggregates.  If we had to handle aggregates here, we'd have to manually
    // compute the offsets within the alloca -- we wouldn't be able to assume
    // that the alignment of the llvm type was the same as the alignment of the
    // clang type.
    llvm::Type *AllocaTy = llvm::StructType::create(ArgTypes, "printf_args");
    llvm::Value *Alloca = CGF->CreateTempAlloca(AllocaTy);

    for (unsigned I = 1, NumArgs = Args.size(); I < NumArgs; ++I) {
      llvm::Value *P = Builder.CreateStructGEP(AllocaTy, Alloca, I - 1);
      llvm::Value *Arg = Args[I].getRValue(*CGF).getScalarVal();
      Builder.CreateAlignedStore(Arg, P, DL.getPrefTypeAlign(Arg->getType()));
    }
    llvm::Value *BufferPtr =
        Builder.CreatePointerCast(Alloca, llvm::PointerType::getUnqual(Ctx));
    return {BufferPtr, DL.getTypeAllocSize(AllocaTy)};
  }
}

bool containsNonScalarVarargs(CodeGenFunction *CGF, const CallArgList &Args) {
  return llvm::any_of(llvm::drop_begin(Args), [&](const CallArg &A) {
    return !A.getRValue(*CGF).isScalar();
  });
}

RValue EmitDevicePrintfCallExpr(const CallExpr *E, CodeGenFunction *CGF,
                                llvm::Function *Decl, bool WithSizeArg) {
  CodeGenModule &CGM = CGF->CGM;
  CGBuilderTy &Builder = CGF->Builder;
  assert(E->getBuiltinCallee() == Builtin::BIprintf ||
         E->getBuiltinCallee() == Builtin::BI__builtin_printf);
  assert(E->getNumArgs() >= 1); // printf always has at least one arg.

  // Uses the same format as nvptx for the argument packing, but also passes
  // an i32 for the total size of the passed pointer
  CallArgList Args;
  CGF->EmitCallArgs(Args,
                    E->getDirectCallee()->getType()->getAs<FunctionProtoType>(),
                    E->arguments(), E->getDirectCallee(),
                    /* ParamsToSkip = */ 0);

  // We don't know how to emit non-scalar varargs.
  if (containsNonScalarVarargs(CGF, Args)) {
    CGM.ErrorUnsupported(E, "non-scalar arg to printf");
    return RValue::get(llvm::ConstantInt::get(CGF->IntTy, 0));
  }

  auto r = packArgsIntoNVPTXFormatBuffer(CGF, Args);
  llvm::Value *BufferPtr = r.first;

  llvm::SmallVector<llvm::Value *, 3> Vec = {
      Args[0].getRValue(*CGF).getScalarVal(), BufferPtr};
  if (WithSizeArg) {
    // Passing > 32bit of data as a local alloca doesn't work for nvptx or
    // amdgpu
    llvm::Constant *Size =
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(CGM.getLLVMContext()),
                               static_cast<uint32_t>(r.second.getFixedValue()));

    Vec.push_back(Size);
  }
  return RValue::get(Builder.CreateCall(Decl, Vec));
}
} // namespace

RValue CodeGenFunction::EmitNVPTXDevicePrintfCallExpr(const CallExpr *E) {
  assert(getTarget().getTriple().isNVPTX());
  return EmitDevicePrintfCallExpr(
      E, this, GetVprintfDeclaration(CGM.getModule()), false);
}

RValue CodeGenFunction::EmitAMDGPUDevicePrintfCallExpr(const CallExpr *E) {
  assert(getTarget().getTriple().isAMDGCN() ||
         (getTarget().getTriple().isSPIRV() &&
          getTarget().getTriple().getVendor() == llvm::Triple::AMD));
  assert(E->getBuiltinCallee() == Builtin::BIprintf ||
         E->getBuiltinCallee() == Builtin::BI__builtin_printf);
  assert(E->getNumArgs() >= 1); // printf always has at least one arg.

  CallArgList CallArgs;
  EmitCallArgs(CallArgs,
               E->getDirectCallee()->getType()->getAs<FunctionProtoType>(),
               E->arguments(), E->getDirectCallee(),
               /* ParamsToSkip = */ 0);

  SmallVector<llvm::Value *, 8> Args;
  for (const auto &A : CallArgs) {
    // We don't know how to emit non-scalar varargs.
    if (!A.getRValue(*this).isScalar()) {
      CGM.ErrorUnsupported(E, "non-scalar arg to printf");
      return RValue::get(llvm::ConstantInt::get(IntTy, -1));
    }

    llvm::Value *Arg = A.getRValue(*this).getScalarVal();
    Args.push_back(Arg);
  }

  llvm::IRBuilder<> IRB(Builder.GetInsertBlock(), Builder.GetInsertPoint());
  IRB.SetCurrentDebugLocation(Builder.getCurrentDebugLocation());

  bool isBuffered = (CGM.getTarget().getTargetOpts().AMDGPUPrintfKindVal ==
                     clang::TargetOptions::AMDGPUPrintfKind::Buffered);
  auto Printf = llvm::emitAMDGPUPrintfCall(IRB, Args, isBuffered);
  Builder.SetInsertPoint(IRB.GetInsertBlock(), IRB.GetInsertPoint());
  return RValue::get(Printf);
}

RValue CodeGenFunction::EmitOpenMPDevicePrintfCallExpr(const CallExpr *E) {
  assert(getTarget().getTriple().isNVPTX() ||
         getTarget().getTriple().isAMDGCN());
  return EmitDevicePrintfCallExpr(E, this, GetOpenMPVprintfDeclaration(CGM),
                                  true);
}
