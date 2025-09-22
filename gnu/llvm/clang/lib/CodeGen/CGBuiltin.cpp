//===---- CGBuiltin.cpp - Emit LLVM Code for builtins ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Builtin calls as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "ABIInfo.h"
#include "CGCUDARuntime.h"
#include "CGCXXABI.h"
#include "CGHLSLRuntime.h"
#include "CGObjCRuntime.h"
#include "CGOpenCLRuntime.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "PatternInit.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/OSLog.h"
#include "clang/AST/OperationKinds.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/IntrinsicsBPF.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsHexagon.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/IR/IntrinsicsPowerPC.h"
#include "llvm/IR/IntrinsicsR600.h"
#include "llvm/IR/IntrinsicsRISCV.h"
#include "llvm/IR/IntrinsicsS390.h"
#include "llvm/IR/IntrinsicsVE.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/X86TargetParser.h"
#include <optional>
#include <sstream>

using namespace clang;
using namespace CodeGen;
using namespace llvm;

static void initializeAlloca(CodeGenFunction &CGF, AllocaInst *AI, Value *Size,
                             Align AlignmentInBytes) {
  ConstantInt *Byte;
  switch (CGF.getLangOpts().getTrivialAutoVarInit()) {
  case LangOptions::TrivialAutoVarInitKind::Uninitialized:
    // Nothing to initialize.
    return;
  case LangOptions::TrivialAutoVarInitKind::Zero:
    Byte = CGF.Builder.getInt8(0x00);
    break;
  case LangOptions::TrivialAutoVarInitKind::Pattern: {
    llvm::Type *Int8 = llvm::IntegerType::getInt8Ty(CGF.CGM.getLLVMContext());
    Byte = llvm::dyn_cast<llvm::ConstantInt>(
        initializationPatternFor(CGF.CGM, Int8));
    break;
  }
  }
  if (CGF.CGM.stopAutoInit())
    return;
  auto *I = CGF.Builder.CreateMemSet(AI, Byte, Size, AlignmentInBytes);
  I->addAnnotationMetadata("auto-init");
}

/// getBuiltinLibFunction - Given a builtin id for a function like
/// "__builtin_fabsf", return a Function* for "fabsf".
llvm::Constant *CodeGenModule::getBuiltinLibFunction(const FunctionDecl *FD,
                                                     unsigned BuiltinID) {
  assert(Context.BuiltinInfo.isLibFunction(BuiltinID));

  // Get the name, skip over the __builtin_ prefix (if necessary).
  StringRef Name;
  GlobalDecl D(FD);

  // TODO: This list should be expanded or refactored after all GCC-compatible
  // std libcall builtins are implemented.
  static SmallDenseMap<unsigned, StringRef, 64> F128Builtins{
      {Builtin::BI__builtin___fprintf_chk, "__fprintf_chkieee128"},
      {Builtin::BI__builtin___printf_chk, "__printf_chkieee128"},
      {Builtin::BI__builtin___snprintf_chk, "__snprintf_chkieee128"},
      {Builtin::BI__builtin___sprintf_chk, "__sprintf_chkieee128"},
      {Builtin::BI__builtin___vfprintf_chk, "__vfprintf_chkieee128"},
      {Builtin::BI__builtin___vprintf_chk, "__vprintf_chkieee128"},
      {Builtin::BI__builtin___vsnprintf_chk, "__vsnprintf_chkieee128"},
      {Builtin::BI__builtin___vsprintf_chk, "__vsprintf_chkieee128"},
      {Builtin::BI__builtin_fprintf, "__fprintfieee128"},
      {Builtin::BI__builtin_printf, "__printfieee128"},
      {Builtin::BI__builtin_snprintf, "__snprintfieee128"},
      {Builtin::BI__builtin_sprintf, "__sprintfieee128"},
      {Builtin::BI__builtin_vfprintf, "__vfprintfieee128"},
      {Builtin::BI__builtin_vprintf, "__vprintfieee128"},
      {Builtin::BI__builtin_vsnprintf, "__vsnprintfieee128"},
      {Builtin::BI__builtin_vsprintf, "__vsprintfieee128"},
      {Builtin::BI__builtin_fscanf, "__fscanfieee128"},
      {Builtin::BI__builtin_scanf, "__scanfieee128"},
      {Builtin::BI__builtin_sscanf, "__sscanfieee128"},
      {Builtin::BI__builtin_vfscanf, "__vfscanfieee128"},
      {Builtin::BI__builtin_vscanf, "__vscanfieee128"},
      {Builtin::BI__builtin_vsscanf, "__vsscanfieee128"},
      {Builtin::BI__builtin_nexttowardf128, "__nexttowardieee128"},
  };

  // The AIX library functions frexpl, ldexpl, and modfl are for 128-bit
  // IBM 'long double' (i.e. __ibm128). Map to the 'double' versions
  // if it is 64-bit 'long double' mode.
  static SmallDenseMap<unsigned, StringRef, 4> AIXLongDouble64Builtins{
      {Builtin::BI__builtin_frexpl, "frexp"},
      {Builtin::BI__builtin_ldexpl, "ldexp"},
      {Builtin::BI__builtin_modfl, "modf"},
  };

  // If the builtin has been declared explicitly with an assembler label,
  // use the mangled name. This differs from the plain label on platforms
  // that prefix labels.
  if (FD->hasAttr<AsmLabelAttr>())
    Name = getMangledName(D);
  else {
    // TODO: This mutation should also be applied to other targets other than
    // PPC, after backend supports IEEE 128-bit style libcalls.
    if (getTriple().isPPC64() &&
        &getTarget().getLongDoubleFormat() == &llvm::APFloat::IEEEquad() &&
        F128Builtins.contains(BuiltinID))
      Name = F128Builtins[BuiltinID];
    else if (getTriple().isOSAIX() &&
             &getTarget().getLongDoubleFormat() ==
                 &llvm::APFloat::IEEEdouble() &&
             AIXLongDouble64Builtins.contains(BuiltinID))
      Name = AIXLongDouble64Builtins[BuiltinID];
    else
      Name = Context.BuiltinInfo.getName(BuiltinID).substr(10);
  }

  llvm::FunctionType *Ty =
    cast<llvm::FunctionType>(getTypes().ConvertType(FD->getType()));

  return GetOrCreateLLVMFunction(Name, Ty, D, /*ForVTable=*/false);
}

/// Emit the conversions required to turn the given value into an
/// integer of the given size.
static Value *EmitToInt(CodeGenFunction &CGF, llvm::Value *V,
                        QualType T, llvm::IntegerType *IntType) {
  V = CGF.EmitToMemory(V, T);

  if (V->getType()->isPointerTy())
    return CGF.Builder.CreatePtrToInt(V, IntType);

  assert(V->getType() == IntType);
  return V;
}

static Value *EmitFromInt(CodeGenFunction &CGF, llvm::Value *V,
                          QualType T, llvm::Type *ResultType) {
  V = CGF.EmitFromMemory(V, T);

  if (ResultType->isPointerTy())
    return CGF.Builder.CreateIntToPtr(V, ResultType);

  assert(V->getType() == ResultType);
  return V;
}

static Address CheckAtomicAlignment(CodeGenFunction &CGF, const CallExpr *E) {
  ASTContext &Ctx = CGF.getContext();
  Address Ptr = CGF.EmitPointerWithAlignment(E->getArg(0));
  unsigned Bytes = Ptr.getElementType()->isPointerTy()
                       ? Ctx.getTypeSizeInChars(Ctx.VoidPtrTy).getQuantity()
                       : Ptr.getElementType()->getScalarSizeInBits() / 8;
  unsigned Align = Ptr.getAlignment().getQuantity();
  if (Align % Bytes != 0) {
    DiagnosticsEngine &Diags = CGF.CGM.getDiags();
    Diags.Report(E->getBeginLoc(), diag::warn_sync_op_misaligned);
    // Force address to be at least naturally-aligned.
    return Ptr.withAlignment(CharUnits::fromQuantity(Bytes));
  }
  return Ptr;
}

/// Utility to insert an atomic instruction based on Intrinsic::ID
/// and the expression node.
static Value *MakeBinaryAtomicValue(
    CodeGenFunction &CGF, llvm::AtomicRMWInst::BinOp Kind, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {

  QualType T = E->getType();
  assert(E->getArg(0)->getType()->isPointerType());
  assert(CGF.getContext().hasSameUnqualifiedType(T,
                                  E->getArg(0)->getType()->getPointeeType()));
  assert(CGF.getContext().hasSameUnqualifiedType(T, E->getArg(1)->getType()));

  Address DestAddr = CheckAtomicAlignment(CGF, E);

  llvm::IntegerType *IntType = llvm::IntegerType::get(
      CGF.getLLVMContext(), CGF.getContext().getTypeSize(T));

  llvm::Value *Val = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Val->getType();
  Val = EmitToInt(CGF, Val, T, IntType);

  llvm::Value *Result =
      CGF.Builder.CreateAtomicRMW(Kind, DestAddr, Val, Ordering);
  return EmitFromInt(CGF, Result, T, ValueType);
}

static Value *EmitNontemporalStore(CodeGenFunction &CGF, const CallExpr *E) {
  Value *Val = CGF.EmitScalarExpr(E->getArg(0));
  Address Addr = CGF.EmitPointerWithAlignment(E->getArg(1));

  Val = CGF.EmitToMemory(Val, E->getArg(0)->getType());
  LValue LV = CGF.MakeAddrLValue(Addr, E->getArg(0)->getType());
  LV.setNontemporal(true);
  CGF.EmitStoreOfScalar(Val, LV, false);
  return nullptr;
}

static Value *EmitNontemporalLoad(CodeGenFunction &CGF, const CallExpr *E) {
  Address Addr = CGF.EmitPointerWithAlignment(E->getArg(0));

  LValue LV = CGF.MakeAddrLValue(Addr, E->getType());
  LV.setNontemporal(true);
  return CGF.EmitLoadOfScalar(LV, E->getExprLoc());
}

static RValue EmitBinaryAtomic(CodeGenFunction &CGF,
                               llvm::AtomicRMWInst::BinOp Kind,
                               const CallExpr *E) {
  return RValue::get(MakeBinaryAtomicValue(CGF, Kind, E));
}

/// Utility to insert an atomic instruction based Intrinsic::ID and
/// the expression node, where the return value is the result of the
/// operation.
static RValue EmitBinaryAtomicPost(CodeGenFunction &CGF,
                                   llvm::AtomicRMWInst::BinOp Kind,
                                   const CallExpr *E,
                                   Instruction::BinaryOps Op,
                                   bool Invert = false) {
  QualType T = E->getType();
  assert(E->getArg(0)->getType()->isPointerType());
  assert(CGF.getContext().hasSameUnqualifiedType(T,
                                  E->getArg(0)->getType()->getPointeeType()));
  assert(CGF.getContext().hasSameUnqualifiedType(T, E->getArg(1)->getType()));

  Address DestAddr = CheckAtomicAlignment(CGF, E);

  llvm::IntegerType *IntType = llvm::IntegerType::get(
      CGF.getLLVMContext(), CGF.getContext().getTypeSize(T));

  llvm::Value *Val = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Val->getType();
  Val = EmitToInt(CGF, Val, T, IntType);

  llvm::Value *Result = CGF.Builder.CreateAtomicRMW(
      Kind, DestAddr, Val, llvm::AtomicOrdering::SequentiallyConsistent);
  Result = CGF.Builder.CreateBinOp(Op, Result, Val);
  if (Invert)
    Result =
        CGF.Builder.CreateBinOp(llvm::Instruction::Xor, Result,
                                llvm::ConstantInt::getAllOnesValue(IntType));
  Result = EmitFromInt(CGF, Result, T, ValueType);
  return RValue::get(Result);
}

/// Utility to insert an atomic cmpxchg instruction.
///
/// @param CGF The current codegen function.
/// @param E   Builtin call expression to convert to cmpxchg.
///            arg0 - address to operate on
///            arg1 - value to compare with
///            arg2 - new value
/// @param ReturnBool Specifies whether to return success flag of
///                   cmpxchg result or the old value.
///
/// @returns result of cmpxchg, according to ReturnBool
///
/// Note: In order to lower Microsoft's _InterlockedCompareExchange* intrinsics
/// invoke the function EmitAtomicCmpXchgForMSIntrin.
static Value *MakeAtomicCmpXchgValue(CodeGenFunction &CGF, const CallExpr *E,
                                     bool ReturnBool) {
  QualType T = ReturnBool ? E->getArg(1)->getType() : E->getType();
  Address DestAddr = CheckAtomicAlignment(CGF, E);

  llvm::IntegerType *IntType = llvm::IntegerType::get(
      CGF.getLLVMContext(), CGF.getContext().getTypeSize(T));

  Value *Cmp = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Type *ValueType = Cmp->getType();
  Cmp = EmitToInt(CGF, Cmp, T, IntType);
  Value *New = EmitToInt(CGF, CGF.EmitScalarExpr(E->getArg(2)), T, IntType);

  Value *Pair = CGF.Builder.CreateAtomicCmpXchg(
      DestAddr, Cmp, New, llvm::AtomicOrdering::SequentiallyConsistent,
      llvm::AtomicOrdering::SequentiallyConsistent);
  if (ReturnBool)
    // Extract boolean success flag and zext it to int.
    return CGF.Builder.CreateZExt(CGF.Builder.CreateExtractValue(Pair, 1),
                                  CGF.ConvertType(E->getType()));
  else
    // Extract old value and emit it using the same type as compare value.
    return EmitFromInt(CGF, CGF.Builder.CreateExtractValue(Pair, 0), T,
                       ValueType);
}

/// This function should be invoked to emit atomic cmpxchg for Microsoft's
/// _InterlockedCompareExchange* intrinsics which have the following signature:
/// T _InterlockedCompareExchange(T volatile *Destination,
///                               T Exchange,
///                               T Comparand);
///
/// Whereas the llvm 'cmpxchg' instruction has the following syntax:
/// cmpxchg *Destination, Comparand, Exchange.
/// So we need to swap Comparand and Exchange when invoking
/// CreateAtomicCmpXchg. That is the reason we could not use the above utility
/// function MakeAtomicCmpXchgValue since it expects the arguments to be
/// already swapped.

static
Value *EmitAtomicCmpXchgForMSIntrin(CodeGenFunction &CGF, const CallExpr *E,
    AtomicOrdering SuccessOrdering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());
  assert(CGF.getContext().hasSameUnqualifiedType(
      E->getType(), E->getArg(0)->getType()->getPointeeType()));
  assert(CGF.getContext().hasSameUnqualifiedType(E->getType(),
                                                 E->getArg(1)->getType()));
  assert(CGF.getContext().hasSameUnqualifiedType(E->getType(),
                                                 E->getArg(2)->getType()));

  Address DestAddr = CheckAtomicAlignment(CGF, E);

  auto *Comparand = CGF.EmitScalarExpr(E->getArg(2));
  auto *Exchange = CGF.EmitScalarExpr(E->getArg(1));

  // For Release ordering, the failure ordering should be Monotonic.
  auto FailureOrdering = SuccessOrdering == AtomicOrdering::Release ?
                         AtomicOrdering::Monotonic :
                         SuccessOrdering;

  // The atomic instruction is marked volatile for consistency with MSVC. This
  // blocks the few atomics optimizations that LLVM has. If we want to optimize
  // _Interlocked* operations in the future, we will have to remove the volatile
  // marker.
  auto *Result = CGF.Builder.CreateAtomicCmpXchg(
      DestAddr, Comparand, Exchange, SuccessOrdering, FailureOrdering);
  Result->setVolatile(true);
  return CGF.Builder.CreateExtractValue(Result, 0);
}

// 64-bit Microsoft platforms support 128 bit cmpxchg operations. They are
// prototyped like this:
//
// unsigned char _InterlockedCompareExchange128...(
//     __int64 volatile * _Destination,
//     __int64 _ExchangeHigh,
//     __int64 _ExchangeLow,
//     __int64 * _ComparandResult);
//
// Note that Destination is assumed to be at least 16-byte aligned, despite
// being typed int64.

static Value *EmitAtomicCmpXchg128ForMSIntrin(CodeGenFunction &CGF,
                                              const CallExpr *E,
                                              AtomicOrdering SuccessOrdering) {
  assert(E->getNumArgs() == 4);
  llvm::Value *DestPtr = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *ExchangeHigh = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Value *ExchangeLow = CGF.EmitScalarExpr(E->getArg(2));
  Address ComparandAddr = CGF.EmitPointerWithAlignment(E->getArg(3));

  assert(DestPtr->getType()->isPointerTy());
  assert(!ExchangeHigh->getType()->isPointerTy());
  assert(!ExchangeLow->getType()->isPointerTy());

  // For Release ordering, the failure ordering should be Monotonic.
  auto FailureOrdering = SuccessOrdering == AtomicOrdering::Release
                             ? AtomicOrdering::Monotonic
                             : SuccessOrdering;

  // Convert to i128 pointers and values. Alignment is also overridden for
  // destination pointer.
  llvm::Type *Int128Ty = llvm::IntegerType::get(CGF.getLLVMContext(), 128);
  Address DestAddr(DestPtr, Int128Ty,
                   CGF.getContext().toCharUnitsFromBits(128));
  ComparandAddr = ComparandAddr.withElementType(Int128Ty);

  // (((i128)hi) << 64) | ((i128)lo)
  ExchangeHigh = CGF.Builder.CreateZExt(ExchangeHigh, Int128Ty);
  ExchangeLow = CGF.Builder.CreateZExt(ExchangeLow, Int128Ty);
  ExchangeHigh =
      CGF.Builder.CreateShl(ExchangeHigh, llvm::ConstantInt::get(Int128Ty, 64));
  llvm::Value *Exchange = CGF.Builder.CreateOr(ExchangeHigh, ExchangeLow);

  // Load the comparand for the instruction.
  llvm::Value *Comparand = CGF.Builder.CreateLoad(ComparandAddr);

  auto *CXI = CGF.Builder.CreateAtomicCmpXchg(DestAddr, Comparand, Exchange,
                                              SuccessOrdering, FailureOrdering);

  // The atomic instruction is marked volatile for consistency with MSVC. This
  // blocks the few atomics optimizations that LLVM has. If we want to optimize
  // _Interlocked* operations in the future, we will have to remove the volatile
  // marker.
  CXI->setVolatile(true);

  // Store the result as an outparameter.
  CGF.Builder.CreateStore(CGF.Builder.CreateExtractValue(CXI, 0),
                          ComparandAddr);

  // Get the success boolean and zero extend it to i8.
  Value *Success = CGF.Builder.CreateExtractValue(CXI, 1);
  return CGF.Builder.CreateZExt(Success, CGF.Int8Ty);
}

static Value *EmitAtomicIncrementValue(CodeGenFunction &CGF, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());

  auto *IntTy = CGF.ConvertType(E->getType());
  Address DestAddr = CheckAtomicAlignment(CGF, E);
  auto *Result = CGF.Builder.CreateAtomicRMW(
      AtomicRMWInst::Add, DestAddr, ConstantInt::get(IntTy, 1), Ordering);
  return CGF.Builder.CreateAdd(Result, ConstantInt::get(IntTy, 1));
}

static Value *EmitAtomicDecrementValue(
    CodeGenFunction &CGF, const CallExpr *E,
    AtomicOrdering Ordering = AtomicOrdering::SequentiallyConsistent) {
  assert(E->getArg(0)->getType()->isPointerType());

  auto *IntTy = CGF.ConvertType(E->getType());
  Address DestAddr = CheckAtomicAlignment(CGF, E);
  auto *Result = CGF.Builder.CreateAtomicRMW(
      AtomicRMWInst::Sub, DestAddr, ConstantInt::get(IntTy, 1), Ordering);
  return CGF.Builder.CreateSub(Result, ConstantInt::get(IntTy, 1));
}

// Build a plain volatile load.
static Value *EmitISOVolatileLoad(CodeGenFunction &CGF, const CallExpr *E) {
  Value *Ptr = CGF.EmitScalarExpr(E->getArg(0));
  QualType ElTy = E->getArg(0)->getType()->getPointeeType();
  CharUnits LoadSize = CGF.getContext().getTypeSizeInChars(ElTy);
  llvm::Type *ITy =
      llvm::IntegerType::get(CGF.getLLVMContext(), LoadSize.getQuantity() * 8);
  llvm::LoadInst *Load = CGF.Builder.CreateAlignedLoad(ITy, Ptr, LoadSize);
  Load->setVolatile(true);
  return Load;
}

// Build a plain volatile store.
static Value *EmitISOVolatileStore(CodeGenFunction &CGF, const CallExpr *E) {
  Value *Ptr = CGF.EmitScalarExpr(E->getArg(0));
  Value *Value = CGF.EmitScalarExpr(E->getArg(1));
  QualType ElTy = E->getArg(0)->getType()->getPointeeType();
  CharUnits StoreSize = CGF.getContext().getTypeSizeInChars(ElTy);
  llvm::StoreInst *Store =
      CGF.Builder.CreateAlignedStore(Value, Ptr, StoreSize);
  Store->setVolatile(true);
  return Store;
}

// Emit a simple mangled intrinsic that has 1 argument and a return type
// matching the argument type. Depending on mode, this may be a constrained
// floating-point intrinsic.
static Value *emitUnaryMaybeConstrainedFPBuiltin(CodeGenFunction &CGF,
                                const CallExpr *E, unsigned IntrinsicID,
                                unsigned ConstrainedIntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));

  CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
  if (CGF.Builder.getIsFPConstrained()) {
    Function *F = CGF.CGM.getIntrinsic(ConstrainedIntrinsicID, Src0->getType());
    return CGF.Builder.CreateConstrainedFPCall(F, { Src0 });
  } else {
    Function *F = CGF.CGM.getIntrinsic(IntrinsicID, Src0->getType());
    return CGF.Builder.CreateCall(F, Src0);
  }
}

// Emit an intrinsic that has 2 operands of the same type as its result.
// Depending on mode, this may be a constrained floating-point intrinsic.
static Value *emitBinaryMaybeConstrainedFPBuiltin(CodeGenFunction &CGF,
                                const CallExpr *E, unsigned IntrinsicID,
                                unsigned ConstrainedIntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *Src1 = CGF.EmitScalarExpr(E->getArg(1));

  CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
  if (CGF.Builder.getIsFPConstrained()) {
    Function *F = CGF.CGM.getIntrinsic(ConstrainedIntrinsicID, Src0->getType());
    return CGF.Builder.CreateConstrainedFPCall(F, { Src0, Src1 });
  } else {
    Function *F = CGF.CGM.getIntrinsic(IntrinsicID, Src0->getType());
    return CGF.Builder.CreateCall(F, { Src0, Src1 });
  }
}

// Has second type mangled argument.
static Value *emitBinaryExpMaybeConstrainedFPBuiltin(
    CodeGenFunction &CGF, const CallExpr *E, llvm::Intrinsic::ID IntrinsicID,
    llvm::Intrinsic::ID ConstrainedIntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *Src1 = CGF.EmitScalarExpr(E->getArg(1));

  CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
  if (CGF.Builder.getIsFPConstrained()) {
    Function *F = CGF.CGM.getIntrinsic(ConstrainedIntrinsicID,
                                       {Src0->getType(), Src1->getType()});
    return CGF.Builder.CreateConstrainedFPCall(F, {Src0, Src1});
  }

  Function *F =
      CGF.CGM.getIntrinsic(IntrinsicID, {Src0->getType(), Src1->getType()});
  return CGF.Builder.CreateCall(F, {Src0, Src1});
}

// Emit an intrinsic that has 3 operands of the same type as its result.
// Depending on mode, this may be a constrained floating-point intrinsic.
static Value *emitTernaryMaybeConstrainedFPBuiltin(CodeGenFunction &CGF,
                                 const CallExpr *E, unsigned IntrinsicID,
                                 unsigned ConstrainedIntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *Src1 = CGF.EmitScalarExpr(E->getArg(1));
  llvm::Value *Src2 = CGF.EmitScalarExpr(E->getArg(2));

  CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
  if (CGF.Builder.getIsFPConstrained()) {
    Function *F = CGF.CGM.getIntrinsic(ConstrainedIntrinsicID, Src0->getType());
    return CGF.Builder.CreateConstrainedFPCall(F, { Src0, Src1, Src2 });
  } else {
    Function *F = CGF.CGM.getIntrinsic(IntrinsicID, Src0->getType());
    return CGF.Builder.CreateCall(F, { Src0, Src1, Src2 });
  }
}

// Emit an intrinsic where all operands are of the same type as the result.
// Depending on mode, this may be a constrained floating-point intrinsic.
static Value *emitCallMaybeConstrainedFPBuiltin(CodeGenFunction &CGF,
                                                unsigned IntrinsicID,
                                                unsigned ConstrainedIntrinsicID,
                                                llvm::Type *Ty,
                                                ArrayRef<Value *> Args) {
  Function *F;
  if (CGF.Builder.getIsFPConstrained())
    F = CGF.CGM.getIntrinsic(ConstrainedIntrinsicID, Ty);
  else
    F = CGF.CGM.getIntrinsic(IntrinsicID, Ty);

  if (CGF.Builder.getIsFPConstrained())
    return CGF.Builder.CreateConstrainedFPCall(F, Args);
  else
    return CGF.Builder.CreateCall(F, Args);
}

// Emit a simple intrinsic that has N scalar arguments and a return type
// matching the argument type. It is assumed that only the first argument is
// overloaded.
template <unsigned N>
Value *emitBuiltinWithOneOverloadedType(CodeGenFunction &CGF, const CallExpr *E,
                                        unsigned IntrinsicID,
                                        llvm::StringRef Name = "") {
  static_assert(N, "expect non-empty argument");
  SmallVector<Value *, N> Args;
  for (unsigned I = 0; I < N; ++I)
    Args.push_back(CGF.EmitScalarExpr(E->getArg(I)));
  Function *F = CGF.CGM.getIntrinsic(IntrinsicID, Args[0]->getType());
  return CGF.Builder.CreateCall(F, Args, Name);
}

// Emit an intrinsic that has 1 float or double operand, and 1 integer.
static Value *emitFPIntBuiltin(CodeGenFunction &CGF,
                               const CallExpr *E,
                               unsigned IntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *Src1 = CGF.EmitScalarExpr(E->getArg(1));

  Function *F = CGF.CGM.getIntrinsic(IntrinsicID, Src0->getType());
  return CGF.Builder.CreateCall(F, {Src0, Src1});
}

// Emit an intrinsic that has overloaded integer result and fp operand.
static Value *
emitMaybeConstrainedFPToIntRoundBuiltin(CodeGenFunction &CGF, const CallExpr *E,
                                        unsigned IntrinsicID,
                                        unsigned ConstrainedIntrinsicID) {
  llvm::Type *ResultType = CGF.ConvertType(E->getType());
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));

  if (CGF.Builder.getIsFPConstrained()) {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
    Function *F = CGF.CGM.getIntrinsic(ConstrainedIntrinsicID,
                                       {ResultType, Src0->getType()});
    return CGF.Builder.CreateConstrainedFPCall(F, {Src0});
  } else {
    Function *F =
        CGF.CGM.getIntrinsic(IntrinsicID, {ResultType, Src0->getType()});
    return CGF.Builder.CreateCall(F, Src0);
  }
}

static Value *emitFrexpBuiltin(CodeGenFunction &CGF, const CallExpr *E,
                               llvm::Intrinsic::ID IntrinsicID) {
  llvm::Value *Src0 = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Value *Src1 = CGF.EmitScalarExpr(E->getArg(1));

  QualType IntPtrTy = E->getArg(1)->getType()->getPointeeType();
  llvm::Type *IntTy = CGF.ConvertType(IntPtrTy);
  llvm::Function *F =
      CGF.CGM.getIntrinsic(IntrinsicID, {Src0->getType(), IntTy});
  llvm::Value *Call = CGF.Builder.CreateCall(F, Src0);

  llvm::Value *Exp = CGF.Builder.CreateExtractValue(Call, 1);
  LValue LV = CGF.MakeNaturalAlignAddrLValue(Src1, IntPtrTy);
  CGF.EmitStoreOfScalar(Exp, LV);

  return CGF.Builder.CreateExtractValue(Call, 0);
}

/// EmitFAbs - Emit a call to @llvm.fabs().
static Value *EmitFAbs(CodeGenFunction &CGF, Value *V) {
  Function *F = CGF.CGM.getIntrinsic(Intrinsic::fabs, V->getType());
  llvm::CallInst *Call = CGF.Builder.CreateCall(F, V);
  Call->setDoesNotAccessMemory();
  return Call;
}

/// Emit the computation of the sign bit for a floating point value. Returns
/// the i1 sign bit value.
static Value *EmitSignBit(CodeGenFunction &CGF, Value *V) {
  LLVMContext &C = CGF.CGM.getLLVMContext();

  llvm::Type *Ty = V->getType();
  int Width = Ty->getPrimitiveSizeInBits();
  llvm::Type *IntTy = llvm::IntegerType::get(C, Width);
  V = CGF.Builder.CreateBitCast(V, IntTy);
  if (Ty->isPPC_FP128Ty()) {
    // We want the sign bit of the higher-order double. The bitcast we just
    // did works as if the double-double was stored to memory and then
    // read as an i128. The "store" will put the higher-order double in the
    // lower address in both little- and big-Endian modes, but the "load"
    // will treat those bits as a different part of the i128: the low bits in
    // little-Endian, the high bits in big-Endian. Therefore, on big-Endian
    // we need to shift the high bits down to the low before truncating.
    Width >>= 1;
    if (CGF.getTarget().isBigEndian()) {
      Value *ShiftCst = llvm::ConstantInt::get(IntTy, Width);
      V = CGF.Builder.CreateLShr(V, ShiftCst);
    }
    // We are truncating value in order to extract the higher-order
    // double, which we will be using to extract the sign from.
    IntTy = llvm::IntegerType::get(C, Width);
    V = CGF.Builder.CreateTrunc(V, IntTy);
  }
  Value *Zero = llvm::Constant::getNullValue(IntTy);
  return CGF.Builder.CreateICmpSLT(V, Zero);
}

static RValue emitLibraryCall(CodeGenFunction &CGF, const FunctionDecl *FD,
                              const CallExpr *E, llvm::Constant *calleeValue) {
  CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
  CGCallee callee = CGCallee::forDirect(calleeValue, GlobalDecl(FD));
  RValue Call =
      CGF.EmitCall(E->getCallee()->getType(), callee, E, ReturnValueSlot());

  // Check the supported intrinsic.
  if (unsigned BuiltinID = FD->getBuiltinID()) {
    auto IsErrnoIntrinsic = [&]() -> unsigned {
      switch (BuiltinID) {
      case Builtin::BIexpf:
      case Builtin::BI__builtin_expf:
      case Builtin::BI__builtin_expf128:
        return true;
      }
      // TODO: support more FP math libcalls
      return false;
    }();

    // Restrict to target with errno, for example, MacOS doesn't set errno.
    if (IsErrnoIntrinsic && CGF.CGM.getLangOpts().MathErrno &&
        !CGF.Builder.getIsFPConstrained()) {
      ASTContext &Context = CGF.getContext();
      // Emit "int" TBAA metadata on FP math libcalls.
      clang::QualType IntTy = Context.IntTy;
      TBAAAccessInfo TBAAInfo = CGF.CGM.getTBAAAccessInfo(IntTy);
      Instruction *Inst = cast<llvm::Instruction>(Call.getScalarVal());
      CGF.CGM.DecorateInstructionWithTBAA(Inst, TBAAInfo);
    }
  }
  return Call;
}

/// Emit a call to llvm.{sadd,uadd,ssub,usub,smul,umul}.with.overflow.*
/// depending on IntrinsicID.
///
/// \arg CGF The current codegen function.
/// \arg IntrinsicID The ID for the Intrinsic we wish to generate.
/// \arg X The first argument to the llvm.*.with.overflow.*.
/// \arg Y The second argument to the llvm.*.with.overflow.*.
/// \arg Carry The carry returned by the llvm.*.with.overflow.*.
/// \returns The result (i.e. sum/product) returned by the intrinsic.
static llvm::Value *EmitOverflowIntrinsic(CodeGenFunction &CGF,
                                          const llvm::Intrinsic::ID IntrinsicID,
                                          llvm::Value *X, llvm::Value *Y,
                                          llvm::Value *&Carry) {
  // Make sure we have integers of the same width.
  assert(X->getType() == Y->getType() &&
         "Arguments must be the same type. (Did you forget to make sure both "
         "arguments have the same integer width?)");

  Function *Callee = CGF.CGM.getIntrinsic(IntrinsicID, X->getType());
  llvm::Value *Tmp = CGF.Builder.CreateCall(Callee, {X, Y});
  Carry = CGF.Builder.CreateExtractValue(Tmp, 1);
  return CGF.Builder.CreateExtractValue(Tmp, 0);
}

static Value *emitRangedBuiltin(CodeGenFunction &CGF, unsigned IntrinsicID,
                                int low, int high) {
  Function *F = CGF.CGM.getIntrinsic(IntrinsicID, {});
  llvm::CallInst *Call = CGF.Builder.CreateCall(F);
  llvm::ConstantRange CR(APInt(32, low), APInt(32, high));
  Call->addRangeRetAttr(CR);
  Call->addRetAttr(llvm::Attribute::AttrKind::NoUndef);
  return Call;
}

namespace {
  struct WidthAndSignedness {
    unsigned Width;
    bool Signed;
  };
}

static WidthAndSignedness
getIntegerWidthAndSignedness(const clang::ASTContext &context,
                             const clang::QualType Type) {
  assert(Type->isIntegerType() && "Given type is not an integer.");
  unsigned Width = Type->isBooleanType()  ? 1
                   : Type->isBitIntType() ? context.getIntWidth(Type)
                                          : context.getTypeInfo(Type).Width;
  bool Signed = Type->isSignedIntegerType();
  return {Width, Signed};
}

// Given one or more integer types, this function produces an integer type that
// encompasses them: any value in one of the given types could be expressed in
// the encompassing type.
static struct WidthAndSignedness
EncompassingIntegerType(ArrayRef<struct WidthAndSignedness> Types) {
  assert(Types.size() > 0 && "Empty list of types.");

  // If any of the given types is signed, we must return a signed type.
  bool Signed = false;
  for (const auto &Type : Types) {
    Signed |= Type.Signed;
  }

  // The encompassing type must have a width greater than or equal to the width
  // of the specified types.  Additionally, if the encompassing type is signed,
  // its width must be strictly greater than the width of any unsigned types
  // given.
  unsigned Width = 0;
  for (const auto &Type : Types) {
    unsigned MinWidth = Type.Width + (Signed && !Type.Signed);
    if (Width < MinWidth) {
      Width = MinWidth;
    }
  }

  return {Width, Signed};
}

Value *CodeGenFunction::EmitVAStartEnd(Value *ArgValue, bool IsStart) {
  Intrinsic::ID inst = IsStart ? Intrinsic::vastart : Intrinsic::vaend;
  return Builder.CreateCall(CGM.getIntrinsic(inst, {ArgValue->getType()}),
                            ArgValue);
}

/// Checks if using the result of __builtin_object_size(p, @p From) in place of
/// __builtin_object_size(p, @p To) is correct
static bool areBOSTypesCompatible(int From, int To) {
  // Note: Our __builtin_object_size implementation currently treats Type=0 and
  // Type=2 identically. Encoding this implementation detail here may make
  // improving __builtin_object_size difficult in the future, so it's omitted.
  return From == To || (From == 0 && To == 1) || (From == 3 && To == 2);
}

static llvm::Value *
getDefaultBuiltinObjectSizeResult(unsigned Type, llvm::IntegerType *ResType) {
  return ConstantInt::get(ResType, (Type & 2) ? 0 : -1, /*isSigned=*/true);
}

llvm::Value *
CodeGenFunction::evaluateOrEmitBuiltinObjectSize(const Expr *E, unsigned Type,
                                                 llvm::IntegerType *ResType,
                                                 llvm::Value *EmittedE,
                                                 bool IsDynamic) {
  uint64_t ObjectSize;
  if (!E->tryEvaluateObjectSize(ObjectSize, getContext(), Type))
    return emitBuiltinObjectSize(E, Type, ResType, EmittedE, IsDynamic);
  return ConstantInt::get(ResType, ObjectSize, /*isSigned=*/true);
}

const FieldDecl *CodeGenFunction::FindFlexibleArrayMemberFieldAndOffset(
    ASTContext &Ctx, const RecordDecl *RD, const FieldDecl *FAMDecl,
    uint64_t &Offset) {
  const LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel =
      getLangOpts().getStrictFlexArraysLevel();
  uint32_t FieldNo = 0;

  if (RD->isImplicit())
    return nullptr;

  for (const FieldDecl *FD : RD->fields()) {
    if ((!FAMDecl || FD == FAMDecl) &&
        Decl::isFlexibleArrayMemberLike(
            Ctx, FD, FD->getType(), StrictFlexArraysLevel,
            /*IgnoreTemplateOrMacroSubstitution=*/true)) {
      const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(RD);
      Offset += Layout.getFieldOffset(FieldNo);
      return FD;
    }

    QualType Ty = FD->getType();
    if (Ty->isRecordType()) {
      if (const FieldDecl *Field = FindFlexibleArrayMemberFieldAndOffset(
              Ctx, Ty->getAsRecordDecl(), FAMDecl, Offset)) {
        const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(RD);
        Offset += Layout.getFieldOffset(FieldNo);
        return Field;
      }
    }

    if (!RD->isUnion())
      ++FieldNo;
  }

  return nullptr;
}

static unsigned CountCountedByAttrs(const RecordDecl *RD) {
  unsigned Num = 0;

  for (const FieldDecl *FD : RD->fields()) {
    if (FD->getType()->isCountAttributedType())
      return ++Num;

    QualType Ty = FD->getType();
    if (Ty->isRecordType())
      Num += CountCountedByAttrs(Ty->getAsRecordDecl());
  }

  return Num;
}

llvm::Value *
CodeGenFunction::emitFlexibleArrayMemberSize(const Expr *E, unsigned Type,
                                             llvm::IntegerType *ResType) {
  // The code generated here calculates the size of a struct with a flexible
  // array member that uses the counted_by attribute. There are two instances
  // we handle:
  //
  //       struct s {
  //         unsigned long flags;
  //         int count;
  //         int array[] __attribute__((counted_by(count)));
  //       }
  //
  //   1) bdos of the flexible array itself:
  //
  //     __builtin_dynamic_object_size(p->array, 1) ==
  //         p->count * sizeof(*p->array)
  //
  //   2) bdos of a pointer into the flexible array:
  //
  //     __builtin_dynamic_object_size(&p->array[42], 1) ==
  //         (p->count - 42) * sizeof(*p->array)
  //
  //   2) bdos of the whole struct, including the flexible array:
  //
  //     __builtin_dynamic_object_size(p, 1) ==
  //        max(sizeof(struct s),
  //            offsetof(struct s, array) + p->count * sizeof(*p->array))
  //
  ASTContext &Ctx = getContext();
  const Expr *Base = E->IgnoreParenImpCasts();
  const Expr *Idx = nullptr;

  if (const auto *UO = dyn_cast<UnaryOperator>(Base);
      UO && UO->getOpcode() == UO_AddrOf) {
    Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
    if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(SubExpr)) {
      Base = ASE->getBase()->IgnoreParenImpCasts();
      Idx = ASE->getIdx()->IgnoreParenImpCasts();

      if (const auto *IL = dyn_cast<IntegerLiteral>(Idx)) {
        int64_t Val = IL->getValue().getSExtValue();
        if (Val < 0)
          return getDefaultBuiltinObjectSizeResult(Type, ResType);

        if (Val == 0)
          // The index is 0, so we don't need to take it into account.
          Idx = nullptr;
      }
    } else {
      // Potential pointer to another element in the struct.
      Base = SubExpr;
    }
  }

  // Get the flexible array member Decl.
  const RecordDecl *OuterRD = nullptr;
  const FieldDecl *FAMDecl = nullptr;
  if (const auto *ME = dyn_cast<MemberExpr>(Base)) {
    // Check if \p Base is referencing the FAM itself.
    const ValueDecl *VD = ME->getMemberDecl();
    OuterRD = VD->getDeclContext()->getOuterLexicalRecordContext();
    FAMDecl = dyn_cast<FieldDecl>(VD);
    if (!FAMDecl)
      return nullptr;
  } else if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
    // Check if we're pointing to the whole struct.
    QualType Ty = DRE->getDecl()->getType();
    if (Ty->isPointerType())
      Ty = Ty->getPointeeType();
    OuterRD = Ty->getAsRecordDecl();

    // If we have a situation like this:
    //
    //     struct union_of_fams {
    //         int flags;
    //         union {
    //             signed char normal_field;
    //             struct {
    //                 int count1;
    //                 int arr1[] __counted_by(count1);
    //             };
    //             struct {
    //                 signed char count2;
    //                 int arr2[] __counted_by(count2);
    //             };
    //         };
    //    };
    //
    // We don't know which 'count' to use in this scenario:
    //
    //     size_t get_size(struct union_of_fams *p) {
    //         return __builtin_dynamic_object_size(p, 1);
    //     }
    //
    // Instead of calculating a wrong number, we give up.
    if (OuterRD && CountCountedByAttrs(OuterRD) > 1)
      return nullptr;
  }

  if (!OuterRD)
    return nullptr;

  // We call FindFlexibleArrayMemberAndOffset even if FAMDecl is non-null to
  // get its offset.
  uint64_t Offset = 0;
  FAMDecl =
      FindFlexibleArrayMemberFieldAndOffset(Ctx, OuterRD, FAMDecl, Offset);
  Offset = Ctx.toCharUnitsFromBits(Offset).getQuantity();

  if (!FAMDecl || !FAMDecl->getType()->isCountAttributedType())
    // No flexible array member found or it doesn't have the "counted_by"
    // attribute.
    return nullptr;

  const FieldDecl *CountedByFD = FindCountedByField(FAMDecl);
  if (!CountedByFD)
    // Can't find the field referenced by the "counted_by" attribute.
    return nullptr;

  if (isa<DeclRefExpr>(Base))
    // The whole struct is specificed in the __bdos. The calculation of the
    // whole size of the structure can be done in two ways:
    //
    //     1) sizeof(struct S) + count * sizeof(typeof(fam))
    //     2) offsetof(struct S, fam) + count * sizeof(typeof(fam))
    //
    // The first will add additional padding after the end of the array,
    // allocation while the second method is more precise, but not quite
    // expected from programmers. See
    // https://lore.kernel.org/lkml/ZvV6X5FPBBW7CO1f@archlinux/ for a
    // discussion of the topic.
    //
    // GCC isn't (currently) able to calculate __bdos on a pointer to the whole
    // structure. Therefore, because of the above issue, we'll choose to match
    // what GCC does for consistency's sake.
    return nullptr;

  // Build a load of the counted_by field.
  bool IsSigned = CountedByFD->getType()->isSignedIntegerType();
  Value *CountedByInst = EmitCountedByFieldExpr(Base, FAMDecl, CountedByFD);
  if (!CountedByInst)
    return getDefaultBuiltinObjectSizeResult(Type, ResType);

  CountedByInst = Builder.CreateIntCast(CountedByInst, ResType, IsSigned);

  // Build a load of the index and subtract it from the count.
  Value *IdxInst = nullptr;
  if (Idx) {
    if (Idx->HasSideEffects(getContext()))
      // We can't have side-effects.
      return getDefaultBuiltinObjectSizeResult(Type, ResType);

    bool IdxSigned = Idx->getType()->isSignedIntegerType();
    IdxInst = EmitAnyExprToTemp(Idx).getScalarVal();
    IdxInst = Builder.CreateIntCast(IdxInst, ResType, IdxSigned);

    // We go ahead with the calculation here. If the index turns out to be
    // negative, we'll catch it at the end.
    CountedByInst =
        Builder.CreateSub(CountedByInst, IdxInst, "", !IsSigned, IsSigned);
  }

  // Calculate how large the flexible array member is in bytes.
  const ArrayType *ArrayTy = Ctx.getAsArrayType(FAMDecl->getType());
  CharUnits Size = Ctx.getTypeSizeInChars(ArrayTy->getElementType());
  llvm::Constant *ElemSize =
      llvm::ConstantInt::get(ResType, Size.getQuantity(), IsSigned);
  Value *Res =
      Builder.CreateMul(CountedByInst, ElemSize, "", !IsSigned, IsSigned);
  Res = Builder.CreateIntCast(Res, ResType, IsSigned);

  // A negative \p IdxInst or \p CountedByInst means that the index lands
  // outside of the flexible array member. If that's the case, we want to
  // return 0.
  Value *Cmp = Builder.CreateIsNotNeg(CountedByInst);
  if (IdxInst)
    Cmp = Builder.CreateAnd(Builder.CreateIsNotNeg(IdxInst), Cmp);

  return Builder.CreateSelect(Cmp, Res, ConstantInt::get(ResType, 0, IsSigned));
}

/// Returns a Value corresponding to the size of the given expression.
/// This Value may be either of the following:
///   - A llvm::Argument (if E is a param with the pass_object_size attribute on
///     it)
///   - A call to the @llvm.objectsize intrinsic
///
/// EmittedE is the result of emitting `E` as a scalar expr. If it's non-null
/// and we wouldn't otherwise try to reference a pass_object_size parameter,
/// we'll call @llvm.objectsize on EmittedE, rather than emitting E.
llvm::Value *
CodeGenFunction::emitBuiltinObjectSize(const Expr *E, unsigned Type,
                                       llvm::IntegerType *ResType,
                                       llvm::Value *EmittedE, bool IsDynamic) {
  // We need to reference an argument if the pointer is a parameter with the
  // pass_object_size attribute.
  if (auto *D = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts())) {
    auto *Param = dyn_cast<ParmVarDecl>(D->getDecl());
    auto *PS = D->getDecl()->getAttr<PassObjectSizeAttr>();
    if (Param != nullptr && PS != nullptr &&
        areBOSTypesCompatible(PS->getType(), Type)) {
      auto Iter = SizeArguments.find(Param);
      assert(Iter != SizeArguments.end());

      const ImplicitParamDecl *D = Iter->second;
      auto DIter = LocalDeclMap.find(D);
      assert(DIter != LocalDeclMap.end());

      return EmitLoadOfScalar(DIter->second, /*Volatile=*/false,
                              getContext().getSizeType(), E->getBeginLoc());
    }
  }

  if (IsDynamic) {
    // Emit special code for a flexible array member with the "counted_by"
    // attribute.
    if (Value *V = emitFlexibleArrayMemberSize(E, Type, ResType))
      return V;
  }

  // LLVM can't handle Type=3 appropriately, and __builtin_object_size shouldn't
  // evaluate E for side-effects. In either case, we shouldn't lower to
  // @llvm.objectsize.
  if (Type == 3 || (!EmittedE && E->HasSideEffects(getContext())))
    return getDefaultBuiltinObjectSizeResult(Type, ResType);

  Value *Ptr = EmittedE ? EmittedE : EmitScalarExpr(E);
  assert(Ptr->getType()->isPointerTy() &&
         "Non-pointer passed to __builtin_object_size?");

  Function *F =
      CGM.getIntrinsic(Intrinsic::objectsize, {ResType, Ptr->getType()});

  // LLVM only supports 0 and 2, make sure that we pass along that as a boolean.
  Value *Min = Builder.getInt1((Type & 2) != 0);
  // For GCC compatibility, __builtin_object_size treat NULL as unknown size.
  Value *NullIsUnknown = Builder.getTrue();
  Value *Dynamic = Builder.getInt1(IsDynamic);
  return Builder.CreateCall(F, {Ptr, Min, NullIsUnknown, Dynamic});
}

namespace {
/// A struct to generically describe a bit test intrinsic.
struct BitTest {
  enum ActionKind : uint8_t { TestOnly, Complement, Reset, Set };
  enum InterlockingKind : uint8_t {
    Unlocked,
    Sequential,
    Acquire,
    Release,
    NoFence
  };

  ActionKind Action;
  InterlockingKind Interlocking;
  bool Is64Bit;

  static BitTest decodeBitTestBuiltin(unsigned BuiltinID);
};

} // namespace

BitTest BitTest::decodeBitTestBuiltin(unsigned BuiltinID) {
  switch (BuiltinID) {
    // Main portable variants.
  case Builtin::BI_bittest:
    return {TestOnly, Unlocked, false};
  case Builtin::BI_bittestandcomplement:
    return {Complement, Unlocked, false};
  case Builtin::BI_bittestandreset:
    return {Reset, Unlocked, false};
  case Builtin::BI_bittestandset:
    return {Set, Unlocked, false};
  case Builtin::BI_interlockedbittestandreset:
    return {Reset, Sequential, false};
  case Builtin::BI_interlockedbittestandset:
    return {Set, Sequential, false};

    // X86-specific 64-bit variants.
  case Builtin::BI_bittest64:
    return {TestOnly, Unlocked, true};
  case Builtin::BI_bittestandcomplement64:
    return {Complement, Unlocked, true};
  case Builtin::BI_bittestandreset64:
    return {Reset, Unlocked, true};
  case Builtin::BI_bittestandset64:
    return {Set, Unlocked, true};
  case Builtin::BI_interlockedbittestandreset64:
    return {Reset, Sequential, true};
  case Builtin::BI_interlockedbittestandset64:
    return {Set, Sequential, true};

    // ARM/AArch64-specific ordering variants.
  case Builtin::BI_interlockedbittestandset_acq:
    return {Set, Acquire, false};
  case Builtin::BI_interlockedbittestandset_rel:
    return {Set, Release, false};
  case Builtin::BI_interlockedbittestandset_nf:
    return {Set, NoFence, false};
  case Builtin::BI_interlockedbittestandreset_acq:
    return {Reset, Acquire, false};
  case Builtin::BI_interlockedbittestandreset_rel:
    return {Reset, Release, false};
  case Builtin::BI_interlockedbittestandreset_nf:
    return {Reset, NoFence, false};
  }
  llvm_unreachable("expected only bittest intrinsics");
}

static char bitActionToX86BTCode(BitTest::ActionKind A) {
  switch (A) {
  case BitTest::TestOnly:   return '\0';
  case BitTest::Complement: return 'c';
  case BitTest::Reset:      return 'r';
  case BitTest::Set:        return 's';
  }
  llvm_unreachable("invalid action");
}

static llvm::Value *EmitX86BitTestIntrinsic(CodeGenFunction &CGF,
                                            BitTest BT,
                                            const CallExpr *E, Value *BitBase,
                                            Value *BitPos) {
  char Action = bitActionToX86BTCode(BT.Action);
  char SizeSuffix = BT.Is64Bit ? 'q' : 'l';

  // Build the assembly.
  SmallString<64> Asm;
  raw_svector_ostream AsmOS(Asm);
  if (BT.Interlocking != BitTest::Unlocked)
    AsmOS << "lock ";
  AsmOS << "bt";
  if (Action)
    AsmOS << Action;
  AsmOS << SizeSuffix << " $2, ($1)";

  // Build the constraints. FIXME: We should support immediates when possible.
  std::string Constraints = "={@ccc},r,r,~{cc},~{memory}";
  std::string_view MachineClobbers = CGF.getTarget().getClobbers();
  if (!MachineClobbers.empty()) {
    Constraints += ',';
    Constraints += MachineClobbers;
  }
  llvm::IntegerType *IntType = llvm::IntegerType::get(
      CGF.getLLVMContext(),
      CGF.getContext().getTypeSize(E->getArg(1)->getType()));
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGF.Int8Ty, {CGF.UnqualPtrTy, IntType}, false);

  llvm::InlineAsm *IA =
      llvm::InlineAsm::get(FTy, Asm, Constraints, /*hasSideEffects=*/true);
  return CGF.Builder.CreateCall(IA, {BitBase, BitPos});
}

static llvm::AtomicOrdering
getBitTestAtomicOrdering(BitTest::InterlockingKind I) {
  switch (I) {
  case BitTest::Unlocked:   return llvm::AtomicOrdering::NotAtomic;
  case BitTest::Sequential: return llvm::AtomicOrdering::SequentiallyConsistent;
  case BitTest::Acquire:    return llvm::AtomicOrdering::Acquire;
  case BitTest::Release:    return llvm::AtomicOrdering::Release;
  case BitTest::NoFence:    return llvm::AtomicOrdering::Monotonic;
  }
  llvm_unreachable("invalid interlocking");
}

/// Emit a _bittest* intrinsic. These intrinsics take a pointer to an array of
/// bits and a bit position and read and optionally modify the bit at that
/// position. The position index can be arbitrarily large, i.e. it can be larger
/// than 31 or 63, so we need an indexed load in the general case.
static llvm::Value *EmitBitTestIntrinsic(CodeGenFunction &CGF,
                                         unsigned BuiltinID,
                                         const CallExpr *E) {
  Value *BitBase = CGF.EmitScalarExpr(E->getArg(0));
  Value *BitPos = CGF.EmitScalarExpr(E->getArg(1));

  BitTest BT = BitTest::decodeBitTestBuiltin(BuiltinID);

  // X86 has special BT, BTC, BTR, and BTS instructions that handle the array
  // indexing operation internally. Use them if possible.
  if (CGF.getTarget().getTriple().isX86())
    return EmitX86BitTestIntrinsic(CGF, BT, E, BitBase, BitPos);

  // Otherwise, use generic code to load one byte and test the bit. Use all but
  // the bottom three bits as the array index, and the bottom three bits to form
  // a mask.
  // Bit = BitBaseI8[BitPos >> 3] & (1 << (BitPos & 0x7)) != 0;
  Value *ByteIndex = CGF.Builder.CreateAShr(
      BitPos, llvm::ConstantInt::get(BitPos->getType(), 3), "bittest.byteidx");
  Value *BitBaseI8 = CGF.Builder.CreatePointerCast(BitBase, CGF.Int8PtrTy);
  Address ByteAddr(CGF.Builder.CreateInBoundsGEP(CGF.Int8Ty, BitBaseI8,
                                                 ByteIndex, "bittest.byteaddr"),
                   CGF.Int8Ty, CharUnits::One());
  Value *PosLow =
      CGF.Builder.CreateAnd(CGF.Builder.CreateTrunc(BitPos, CGF.Int8Ty),
                            llvm::ConstantInt::get(CGF.Int8Ty, 0x7));

  // The updating instructions will need a mask.
  Value *Mask = nullptr;
  if (BT.Action != BitTest::TestOnly) {
    Mask = CGF.Builder.CreateShl(llvm::ConstantInt::get(CGF.Int8Ty, 1), PosLow,
                                 "bittest.mask");
  }

  // Check the action and ordering of the interlocked intrinsics.
  llvm::AtomicOrdering Ordering = getBitTestAtomicOrdering(BT.Interlocking);

  Value *OldByte = nullptr;
  if (Ordering != llvm::AtomicOrdering::NotAtomic) {
    // Emit a combined atomicrmw load/store operation for the interlocked
    // intrinsics.
    llvm::AtomicRMWInst::BinOp RMWOp = llvm::AtomicRMWInst::Or;
    if (BT.Action == BitTest::Reset) {
      Mask = CGF.Builder.CreateNot(Mask);
      RMWOp = llvm::AtomicRMWInst::And;
    }
    OldByte = CGF.Builder.CreateAtomicRMW(RMWOp, ByteAddr, Mask, Ordering);
  } else {
    // Emit a plain load for the non-interlocked intrinsics.
    OldByte = CGF.Builder.CreateLoad(ByteAddr, "bittest.byte");
    Value *NewByte = nullptr;
    switch (BT.Action) {
    case BitTest::TestOnly:
      // Don't store anything.
      break;
    case BitTest::Complement:
      NewByte = CGF.Builder.CreateXor(OldByte, Mask);
      break;
    case BitTest::Reset:
      NewByte = CGF.Builder.CreateAnd(OldByte, CGF.Builder.CreateNot(Mask));
      break;
    case BitTest::Set:
      NewByte = CGF.Builder.CreateOr(OldByte, Mask);
      break;
    }
    if (NewByte)
      CGF.Builder.CreateStore(NewByte, ByteAddr);
  }

  // However we loaded the old byte, either by plain load or atomicrmw, shift
  // the bit into the low position and mask it to 0 or 1.
  Value *ShiftedByte = CGF.Builder.CreateLShr(OldByte, PosLow, "bittest.shr");
  return CGF.Builder.CreateAnd(
      ShiftedByte, llvm::ConstantInt::get(CGF.Int8Ty, 1), "bittest.res");
}

static llvm::Value *emitPPCLoadReserveIntrinsic(CodeGenFunction &CGF,
                                                unsigned BuiltinID,
                                                const CallExpr *E) {
  Value *Addr = CGF.EmitScalarExpr(E->getArg(0));

  SmallString<64> Asm;
  raw_svector_ostream AsmOS(Asm);
  llvm::IntegerType *RetType = CGF.Int32Ty;

  switch (BuiltinID) {
  case clang::PPC::BI__builtin_ppc_ldarx:
    AsmOS << "ldarx ";
    RetType = CGF.Int64Ty;
    break;
  case clang::PPC::BI__builtin_ppc_lwarx:
    AsmOS << "lwarx ";
    RetType = CGF.Int32Ty;
    break;
  case clang::PPC::BI__builtin_ppc_lharx:
    AsmOS << "lharx ";
    RetType = CGF.Int16Ty;
    break;
  case clang::PPC::BI__builtin_ppc_lbarx:
    AsmOS << "lbarx ";
    RetType = CGF.Int8Ty;
    break;
  default:
    llvm_unreachable("Expected only PowerPC load reserve intrinsics");
  }

  AsmOS << "$0, ${1:y}";

  std::string Constraints = "=r,*Z,~{memory}";
  std::string_view MachineClobbers = CGF.getTarget().getClobbers();
  if (!MachineClobbers.empty()) {
    Constraints += ',';
    Constraints += MachineClobbers;
  }

  llvm::Type *PtrType = CGF.UnqualPtrTy;
  llvm::FunctionType *FTy = llvm::FunctionType::get(RetType, {PtrType}, false);

  llvm::InlineAsm *IA =
      llvm::InlineAsm::get(FTy, Asm, Constraints, /*hasSideEffects=*/true);
  llvm::CallInst *CI = CGF.Builder.CreateCall(IA, {Addr});
  CI->addParamAttr(
      0, Attribute::get(CGF.getLLVMContext(), Attribute::ElementType, RetType));
  return CI;
}

namespace {
enum class MSVCSetJmpKind {
  _setjmpex,
  _setjmp3,
  _setjmp
};
}

/// MSVC handles setjmp a bit differently on different platforms. On every
/// architecture except 32-bit x86, the frame address is passed. On x86, extra
/// parameters can be passed as variadic arguments, but we always pass none.
static RValue EmitMSVCRTSetJmp(CodeGenFunction &CGF, MSVCSetJmpKind SJKind,
                               const CallExpr *E) {
  llvm::Value *Arg1 = nullptr;
  llvm::Type *Arg1Ty = nullptr;
  StringRef Name;
  bool IsVarArg = false;
  if (SJKind == MSVCSetJmpKind::_setjmp3) {
    Name = "_setjmp3";
    Arg1Ty = CGF.Int32Ty;
    Arg1 = llvm::ConstantInt::get(CGF.IntTy, 0);
    IsVarArg = true;
  } else {
    Name = SJKind == MSVCSetJmpKind::_setjmp ? "_setjmp" : "_setjmpex";
    Arg1Ty = CGF.Int8PtrTy;
    if (CGF.getTarget().getTriple().getArch() == llvm::Triple::aarch64) {
      Arg1 = CGF.Builder.CreateCall(
          CGF.CGM.getIntrinsic(Intrinsic::sponentry, CGF.AllocaInt8PtrTy));
    } else
      Arg1 = CGF.Builder.CreateCall(
          CGF.CGM.getIntrinsic(Intrinsic::frameaddress, CGF.AllocaInt8PtrTy),
          llvm::ConstantInt::get(CGF.Int32Ty, 0));
  }

  // Mark the call site and declaration with ReturnsTwice.
  llvm::Type *ArgTypes[2] = {CGF.Int8PtrTy, Arg1Ty};
  llvm::AttributeList ReturnsTwiceAttr = llvm::AttributeList::get(
      CGF.getLLVMContext(), llvm::AttributeList::FunctionIndex,
      llvm::Attribute::ReturnsTwice);
  llvm::FunctionCallee SetJmpFn = CGF.CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGF.IntTy, ArgTypes, IsVarArg), Name,
      ReturnsTwiceAttr, /*Local=*/true);

  llvm::Value *Buf = CGF.Builder.CreateBitOrPointerCast(
      CGF.EmitScalarExpr(E->getArg(0)), CGF.Int8PtrTy);
  llvm::Value *Args[] = {Buf, Arg1};
  llvm::CallBase *CB = CGF.EmitRuntimeCallOrInvoke(SetJmpFn, Args);
  CB->setAttributes(ReturnsTwiceAttr);
  return RValue::get(CB);
}

// Many of MSVC builtins are on x64, ARM and AArch64; to avoid repeating code,
// we handle them here.
enum class CodeGenFunction::MSVCIntrin {
  _BitScanForward,
  _BitScanReverse,
  _InterlockedAnd,
  _InterlockedDecrement,
  _InterlockedExchange,
  _InterlockedExchangeAdd,
  _InterlockedExchangeSub,
  _InterlockedIncrement,
  _InterlockedOr,
  _InterlockedXor,
  _InterlockedExchangeAdd_acq,
  _InterlockedExchangeAdd_rel,
  _InterlockedExchangeAdd_nf,
  _InterlockedExchange_acq,
  _InterlockedExchange_rel,
  _InterlockedExchange_nf,
  _InterlockedCompareExchange_acq,
  _InterlockedCompareExchange_rel,
  _InterlockedCompareExchange_nf,
  _InterlockedCompareExchange128,
  _InterlockedCompareExchange128_acq,
  _InterlockedCompareExchange128_rel,
  _InterlockedCompareExchange128_nf,
  _InterlockedOr_acq,
  _InterlockedOr_rel,
  _InterlockedOr_nf,
  _InterlockedXor_acq,
  _InterlockedXor_rel,
  _InterlockedXor_nf,
  _InterlockedAnd_acq,
  _InterlockedAnd_rel,
  _InterlockedAnd_nf,
  _InterlockedIncrement_acq,
  _InterlockedIncrement_rel,
  _InterlockedIncrement_nf,
  _InterlockedDecrement_acq,
  _InterlockedDecrement_rel,
  _InterlockedDecrement_nf,
  __fastfail,
};

static std::optional<CodeGenFunction::MSVCIntrin>
translateArmToMsvcIntrin(unsigned BuiltinID) {
  using MSVCIntrin = CodeGenFunction::MSVCIntrin;
  switch (BuiltinID) {
  default:
    return std::nullopt;
  case clang::ARM::BI_BitScanForward:
  case clang::ARM::BI_BitScanForward64:
    return MSVCIntrin::_BitScanForward;
  case clang::ARM::BI_BitScanReverse:
  case clang::ARM::BI_BitScanReverse64:
    return MSVCIntrin::_BitScanReverse;
  case clang::ARM::BI_InterlockedAnd64:
    return MSVCIntrin::_InterlockedAnd;
  case clang::ARM::BI_InterlockedExchange64:
    return MSVCIntrin::_InterlockedExchange;
  case clang::ARM::BI_InterlockedExchangeAdd64:
    return MSVCIntrin::_InterlockedExchangeAdd;
  case clang::ARM::BI_InterlockedExchangeSub64:
    return MSVCIntrin::_InterlockedExchangeSub;
  case clang::ARM::BI_InterlockedOr64:
    return MSVCIntrin::_InterlockedOr;
  case clang::ARM::BI_InterlockedXor64:
    return MSVCIntrin::_InterlockedXor;
  case clang::ARM::BI_InterlockedDecrement64:
    return MSVCIntrin::_InterlockedDecrement;
  case clang::ARM::BI_InterlockedIncrement64:
    return MSVCIntrin::_InterlockedIncrement;
  case clang::ARM::BI_InterlockedExchangeAdd8_acq:
  case clang::ARM::BI_InterlockedExchangeAdd16_acq:
  case clang::ARM::BI_InterlockedExchangeAdd_acq:
  case clang::ARM::BI_InterlockedExchangeAdd64_acq:
    return MSVCIntrin::_InterlockedExchangeAdd_acq;
  case clang::ARM::BI_InterlockedExchangeAdd8_rel:
  case clang::ARM::BI_InterlockedExchangeAdd16_rel:
  case clang::ARM::BI_InterlockedExchangeAdd_rel:
  case clang::ARM::BI_InterlockedExchangeAdd64_rel:
    return MSVCIntrin::_InterlockedExchangeAdd_rel;
  case clang::ARM::BI_InterlockedExchangeAdd8_nf:
  case clang::ARM::BI_InterlockedExchangeAdd16_nf:
  case clang::ARM::BI_InterlockedExchangeAdd_nf:
  case clang::ARM::BI_InterlockedExchangeAdd64_nf:
    return MSVCIntrin::_InterlockedExchangeAdd_nf;
  case clang::ARM::BI_InterlockedExchange8_acq:
  case clang::ARM::BI_InterlockedExchange16_acq:
  case clang::ARM::BI_InterlockedExchange_acq:
  case clang::ARM::BI_InterlockedExchange64_acq:
    return MSVCIntrin::_InterlockedExchange_acq;
  case clang::ARM::BI_InterlockedExchange8_rel:
  case clang::ARM::BI_InterlockedExchange16_rel:
  case clang::ARM::BI_InterlockedExchange_rel:
  case clang::ARM::BI_InterlockedExchange64_rel:
    return MSVCIntrin::_InterlockedExchange_rel;
  case clang::ARM::BI_InterlockedExchange8_nf:
  case clang::ARM::BI_InterlockedExchange16_nf:
  case clang::ARM::BI_InterlockedExchange_nf:
  case clang::ARM::BI_InterlockedExchange64_nf:
    return MSVCIntrin::_InterlockedExchange_nf;
  case clang::ARM::BI_InterlockedCompareExchange8_acq:
  case clang::ARM::BI_InterlockedCompareExchange16_acq:
  case clang::ARM::BI_InterlockedCompareExchange_acq:
  case clang::ARM::BI_InterlockedCompareExchange64_acq:
    return MSVCIntrin::_InterlockedCompareExchange_acq;
  case clang::ARM::BI_InterlockedCompareExchange8_rel:
  case clang::ARM::BI_InterlockedCompareExchange16_rel:
  case clang::ARM::BI_InterlockedCompareExchange_rel:
  case clang::ARM::BI_InterlockedCompareExchange64_rel:
    return MSVCIntrin::_InterlockedCompareExchange_rel;
  case clang::ARM::BI_InterlockedCompareExchange8_nf:
  case clang::ARM::BI_InterlockedCompareExchange16_nf:
  case clang::ARM::BI_InterlockedCompareExchange_nf:
  case clang::ARM::BI_InterlockedCompareExchange64_nf:
    return MSVCIntrin::_InterlockedCompareExchange_nf;
  case clang::ARM::BI_InterlockedOr8_acq:
  case clang::ARM::BI_InterlockedOr16_acq:
  case clang::ARM::BI_InterlockedOr_acq:
  case clang::ARM::BI_InterlockedOr64_acq:
    return MSVCIntrin::_InterlockedOr_acq;
  case clang::ARM::BI_InterlockedOr8_rel:
  case clang::ARM::BI_InterlockedOr16_rel:
  case clang::ARM::BI_InterlockedOr_rel:
  case clang::ARM::BI_InterlockedOr64_rel:
    return MSVCIntrin::_InterlockedOr_rel;
  case clang::ARM::BI_InterlockedOr8_nf:
  case clang::ARM::BI_InterlockedOr16_nf:
  case clang::ARM::BI_InterlockedOr_nf:
  case clang::ARM::BI_InterlockedOr64_nf:
    return MSVCIntrin::_InterlockedOr_nf;
  case clang::ARM::BI_InterlockedXor8_acq:
  case clang::ARM::BI_InterlockedXor16_acq:
  case clang::ARM::BI_InterlockedXor_acq:
  case clang::ARM::BI_InterlockedXor64_acq:
    return MSVCIntrin::_InterlockedXor_acq;
  case clang::ARM::BI_InterlockedXor8_rel:
  case clang::ARM::BI_InterlockedXor16_rel:
  case clang::ARM::BI_InterlockedXor_rel:
  case clang::ARM::BI_InterlockedXor64_rel:
    return MSVCIntrin::_InterlockedXor_rel;
  case clang::ARM::BI_InterlockedXor8_nf:
  case clang::ARM::BI_InterlockedXor16_nf:
  case clang::ARM::BI_InterlockedXor_nf:
  case clang::ARM::BI_InterlockedXor64_nf:
    return MSVCIntrin::_InterlockedXor_nf;
  case clang::ARM::BI_InterlockedAnd8_acq:
  case clang::ARM::BI_InterlockedAnd16_acq:
  case clang::ARM::BI_InterlockedAnd_acq:
  case clang::ARM::BI_InterlockedAnd64_acq:
    return MSVCIntrin::_InterlockedAnd_acq;
  case clang::ARM::BI_InterlockedAnd8_rel:
  case clang::ARM::BI_InterlockedAnd16_rel:
  case clang::ARM::BI_InterlockedAnd_rel:
  case clang::ARM::BI_InterlockedAnd64_rel:
    return MSVCIntrin::_InterlockedAnd_rel;
  case clang::ARM::BI_InterlockedAnd8_nf:
  case clang::ARM::BI_InterlockedAnd16_nf:
  case clang::ARM::BI_InterlockedAnd_nf:
  case clang::ARM::BI_InterlockedAnd64_nf:
    return MSVCIntrin::_InterlockedAnd_nf;
  case clang::ARM::BI_InterlockedIncrement16_acq:
  case clang::ARM::BI_InterlockedIncrement_acq:
  case clang::ARM::BI_InterlockedIncrement64_acq:
    return MSVCIntrin::_InterlockedIncrement_acq;
  case clang::ARM::BI_InterlockedIncrement16_rel:
  case clang::ARM::BI_InterlockedIncrement_rel:
  case clang::ARM::BI_InterlockedIncrement64_rel:
    return MSVCIntrin::_InterlockedIncrement_rel;
  case clang::ARM::BI_InterlockedIncrement16_nf:
  case clang::ARM::BI_InterlockedIncrement_nf:
  case clang::ARM::BI_InterlockedIncrement64_nf:
    return MSVCIntrin::_InterlockedIncrement_nf;
  case clang::ARM::BI_InterlockedDecrement16_acq:
  case clang::ARM::BI_InterlockedDecrement_acq:
  case clang::ARM::BI_InterlockedDecrement64_acq:
    return MSVCIntrin::_InterlockedDecrement_acq;
  case clang::ARM::BI_InterlockedDecrement16_rel:
  case clang::ARM::BI_InterlockedDecrement_rel:
  case clang::ARM::BI_InterlockedDecrement64_rel:
    return MSVCIntrin::_InterlockedDecrement_rel;
  case clang::ARM::BI_InterlockedDecrement16_nf:
  case clang::ARM::BI_InterlockedDecrement_nf:
  case clang::ARM::BI_InterlockedDecrement64_nf:
    return MSVCIntrin::_InterlockedDecrement_nf;
  }
  llvm_unreachable("must return from switch");
}

static std::optional<CodeGenFunction::MSVCIntrin>
translateAarch64ToMsvcIntrin(unsigned BuiltinID) {
  using MSVCIntrin = CodeGenFunction::MSVCIntrin;
  switch (BuiltinID) {
  default:
    return std::nullopt;
  case clang::AArch64::BI_BitScanForward:
  case clang::AArch64::BI_BitScanForward64:
    return MSVCIntrin::_BitScanForward;
  case clang::AArch64::BI_BitScanReverse:
  case clang::AArch64::BI_BitScanReverse64:
    return MSVCIntrin::_BitScanReverse;
  case clang::AArch64::BI_InterlockedAnd64:
    return MSVCIntrin::_InterlockedAnd;
  case clang::AArch64::BI_InterlockedExchange64:
    return MSVCIntrin::_InterlockedExchange;
  case clang::AArch64::BI_InterlockedExchangeAdd64:
    return MSVCIntrin::_InterlockedExchangeAdd;
  case clang::AArch64::BI_InterlockedExchangeSub64:
    return MSVCIntrin::_InterlockedExchangeSub;
  case clang::AArch64::BI_InterlockedOr64:
    return MSVCIntrin::_InterlockedOr;
  case clang::AArch64::BI_InterlockedXor64:
    return MSVCIntrin::_InterlockedXor;
  case clang::AArch64::BI_InterlockedDecrement64:
    return MSVCIntrin::_InterlockedDecrement;
  case clang::AArch64::BI_InterlockedIncrement64:
    return MSVCIntrin::_InterlockedIncrement;
  case clang::AArch64::BI_InterlockedExchangeAdd8_acq:
  case clang::AArch64::BI_InterlockedExchangeAdd16_acq:
  case clang::AArch64::BI_InterlockedExchangeAdd_acq:
  case clang::AArch64::BI_InterlockedExchangeAdd64_acq:
    return MSVCIntrin::_InterlockedExchangeAdd_acq;
  case clang::AArch64::BI_InterlockedExchangeAdd8_rel:
  case clang::AArch64::BI_InterlockedExchangeAdd16_rel:
  case clang::AArch64::BI_InterlockedExchangeAdd_rel:
  case clang::AArch64::BI_InterlockedExchangeAdd64_rel:
    return MSVCIntrin::_InterlockedExchangeAdd_rel;
  case clang::AArch64::BI_InterlockedExchangeAdd8_nf:
  case clang::AArch64::BI_InterlockedExchangeAdd16_nf:
  case clang::AArch64::BI_InterlockedExchangeAdd_nf:
  case clang::AArch64::BI_InterlockedExchangeAdd64_nf:
    return MSVCIntrin::_InterlockedExchangeAdd_nf;
  case clang::AArch64::BI_InterlockedExchange8_acq:
  case clang::AArch64::BI_InterlockedExchange16_acq:
  case clang::AArch64::BI_InterlockedExchange_acq:
  case clang::AArch64::BI_InterlockedExchange64_acq:
    return MSVCIntrin::_InterlockedExchange_acq;
  case clang::AArch64::BI_InterlockedExchange8_rel:
  case clang::AArch64::BI_InterlockedExchange16_rel:
  case clang::AArch64::BI_InterlockedExchange_rel:
  case clang::AArch64::BI_InterlockedExchange64_rel:
    return MSVCIntrin::_InterlockedExchange_rel;
  case clang::AArch64::BI_InterlockedExchange8_nf:
  case clang::AArch64::BI_InterlockedExchange16_nf:
  case clang::AArch64::BI_InterlockedExchange_nf:
  case clang::AArch64::BI_InterlockedExchange64_nf:
    return MSVCIntrin::_InterlockedExchange_nf;
  case clang::AArch64::BI_InterlockedCompareExchange8_acq:
  case clang::AArch64::BI_InterlockedCompareExchange16_acq:
  case clang::AArch64::BI_InterlockedCompareExchange_acq:
  case clang::AArch64::BI_InterlockedCompareExchange64_acq:
    return MSVCIntrin::_InterlockedCompareExchange_acq;
  case clang::AArch64::BI_InterlockedCompareExchange8_rel:
  case clang::AArch64::BI_InterlockedCompareExchange16_rel:
  case clang::AArch64::BI_InterlockedCompareExchange_rel:
  case clang::AArch64::BI_InterlockedCompareExchange64_rel:
    return MSVCIntrin::_InterlockedCompareExchange_rel;
  case clang::AArch64::BI_InterlockedCompareExchange8_nf:
  case clang::AArch64::BI_InterlockedCompareExchange16_nf:
  case clang::AArch64::BI_InterlockedCompareExchange_nf:
  case clang::AArch64::BI_InterlockedCompareExchange64_nf:
    return MSVCIntrin::_InterlockedCompareExchange_nf;
  case clang::AArch64::BI_InterlockedCompareExchange128:
    return MSVCIntrin::_InterlockedCompareExchange128;
  case clang::AArch64::BI_InterlockedCompareExchange128_acq:
    return MSVCIntrin::_InterlockedCompareExchange128_acq;
  case clang::AArch64::BI_InterlockedCompareExchange128_nf:
    return MSVCIntrin::_InterlockedCompareExchange128_nf;
  case clang::AArch64::BI_InterlockedCompareExchange128_rel:
    return MSVCIntrin::_InterlockedCompareExchange128_rel;
  case clang::AArch64::BI_InterlockedOr8_acq:
  case clang::AArch64::BI_InterlockedOr16_acq:
  case clang::AArch64::BI_InterlockedOr_acq:
  case clang::AArch64::BI_InterlockedOr64_acq:
    return MSVCIntrin::_InterlockedOr_acq;
  case clang::AArch64::BI_InterlockedOr8_rel:
  case clang::AArch64::BI_InterlockedOr16_rel:
  case clang::AArch64::BI_InterlockedOr_rel:
  case clang::AArch64::BI_InterlockedOr64_rel:
    return MSVCIntrin::_InterlockedOr_rel;
  case clang::AArch64::BI_InterlockedOr8_nf:
  case clang::AArch64::BI_InterlockedOr16_nf:
  case clang::AArch64::BI_InterlockedOr_nf:
  case clang::AArch64::BI_InterlockedOr64_nf:
    return MSVCIntrin::_InterlockedOr_nf;
  case clang::AArch64::BI_InterlockedXor8_acq:
  case clang::AArch64::BI_InterlockedXor16_acq:
  case clang::AArch64::BI_InterlockedXor_acq:
  case clang::AArch64::BI_InterlockedXor64_acq:
    return MSVCIntrin::_InterlockedXor_acq;
  case clang::AArch64::BI_InterlockedXor8_rel:
  case clang::AArch64::BI_InterlockedXor16_rel:
  case clang::AArch64::BI_InterlockedXor_rel:
  case clang::AArch64::BI_InterlockedXor64_rel:
    return MSVCIntrin::_InterlockedXor_rel;
  case clang::AArch64::BI_InterlockedXor8_nf:
  case clang::AArch64::BI_InterlockedXor16_nf:
  case clang::AArch64::BI_InterlockedXor_nf:
  case clang::AArch64::BI_InterlockedXor64_nf:
    return MSVCIntrin::_InterlockedXor_nf;
  case clang::AArch64::BI_InterlockedAnd8_acq:
  case clang::AArch64::BI_InterlockedAnd16_acq:
  case clang::AArch64::BI_InterlockedAnd_acq:
  case clang::AArch64::BI_InterlockedAnd64_acq:
    return MSVCIntrin::_InterlockedAnd_acq;
  case clang::AArch64::BI_InterlockedAnd8_rel:
  case clang::AArch64::BI_InterlockedAnd16_rel:
  case clang::AArch64::BI_InterlockedAnd_rel:
  case clang::AArch64::BI_InterlockedAnd64_rel:
    return MSVCIntrin::_InterlockedAnd_rel;
  case clang::AArch64::BI_InterlockedAnd8_nf:
  case clang::AArch64::BI_InterlockedAnd16_nf:
  case clang::AArch64::BI_InterlockedAnd_nf:
  case clang::AArch64::BI_InterlockedAnd64_nf:
    return MSVCIntrin::_InterlockedAnd_nf;
  case clang::AArch64::BI_InterlockedIncrement16_acq:
  case clang::AArch64::BI_InterlockedIncrement_acq:
  case clang::AArch64::BI_InterlockedIncrement64_acq:
    return MSVCIntrin::_InterlockedIncrement_acq;
  case clang::AArch64::BI_InterlockedIncrement16_rel:
  case clang::AArch64::BI_InterlockedIncrement_rel:
  case clang::AArch64::BI_InterlockedIncrement64_rel:
    return MSVCIntrin::_InterlockedIncrement_rel;
  case clang::AArch64::BI_InterlockedIncrement16_nf:
  case clang::AArch64::BI_InterlockedIncrement_nf:
  case clang::AArch64::BI_InterlockedIncrement64_nf:
    return MSVCIntrin::_InterlockedIncrement_nf;
  case clang::AArch64::BI_InterlockedDecrement16_acq:
  case clang::AArch64::BI_InterlockedDecrement_acq:
  case clang::AArch64::BI_InterlockedDecrement64_acq:
    return MSVCIntrin::_InterlockedDecrement_acq;
  case clang::AArch64::BI_InterlockedDecrement16_rel:
  case clang::AArch64::BI_InterlockedDecrement_rel:
  case clang::AArch64::BI_InterlockedDecrement64_rel:
    return MSVCIntrin::_InterlockedDecrement_rel;
  case clang::AArch64::BI_InterlockedDecrement16_nf:
  case clang::AArch64::BI_InterlockedDecrement_nf:
  case clang::AArch64::BI_InterlockedDecrement64_nf:
    return MSVCIntrin::_InterlockedDecrement_nf;
  }
  llvm_unreachable("must return from switch");
}

static std::optional<CodeGenFunction::MSVCIntrin>
translateX86ToMsvcIntrin(unsigned BuiltinID) {
  using MSVCIntrin = CodeGenFunction::MSVCIntrin;
  switch (BuiltinID) {
  default:
    return std::nullopt;
  case clang::X86::BI_BitScanForward:
  case clang::X86::BI_BitScanForward64:
    return MSVCIntrin::_BitScanForward;
  case clang::X86::BI_BitScanReverse:
  case clang::X86::BI_BitScanReverse64:
    return MSVCIntrin::_BitScanReverse;
  case clang::X86::BI_InterlockedAnd64:
    return MSVCIntrin::_InterlockedAnd;
  case clang::X86::BI_InterlockedCompareExchange128:
    return MSVCIntrin::_InterlockedCompareExchange128;
  case clang::X86::BI_InterlockedExchange64:
    return MSVCIntrin::_InterlockedExchange;
  case clang::X86::BI_InterlockedExchangeAdd64:
    return MSVCIntrin::_InterlockedExchangeAdd;
  case clang::X86::BI_InterlockedExchangeSub64:
    return MSVCIntrin::_InterlockedExchangeSub;
  case clang::X86::BI_InterlockedOr64:
    return MSVCIntrin::_InterlockedOr;
  case clang::X86::BI_InterlockedXor64:
    return MSVCIntrin::_InterlockedXor;
  case clang::X86::BI_InterlockedDecrement64:
    return MSVCIntrin::_InterlockedDecrement;
  case clang::X86::BI_InterlockedIncrement64:
    return MSVCIntrin::_InterlockedIncrement;
  }
  llvm_unreachable("must return from switch");
}

// Emit an MSVC intrinsic. Assumes that arguments have *not* been evaluated.
Value *CodeGenFunction::EmitMSVCBuiltinExpr(MSVCIntrin BuiltinID,
                                            const CallExpr *E) {
  switch (BuiltinID) {
  case MSVCIntrin::_BitScanForward:
  case MSVCIntrin::_BitScanReverse: {
    Address IndexAddress(EmitPointerWithAlignment(E->getArg(0)));
    Value *ArgValue = EmitScalarExpr(E->getArg(1));

    llvm::Type *ArgType = ArgValue->getType();
    llvm::Type *IndexType = IndexAddress.getElementType();
    llvm::Type *ResultType = ConvertType(E->getType());

    Value *ArgZero = llvm::Constant::getNullValue(ArgType);
    Value *ResZero = llvm::Constant::getNullValue(ResultType);
    Value *ResOne = llvm::ConstantInt::get(ResultType, 1);

    BasicBlock *Begin = Builder.GetInsertBlock();
    BasicBlock *End = createBasicBlock("bitscan_end", this->CurFn);
    Builder.SetInsertPoint(End);
    PHINode *Result = Builder.CreatePHI(ResultType, 2, "bitscan_result");

    Builder.SetInsertPoint(Begin);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, ArgZero);
    BasicBlock *NotZero = createBasicBlock("bitscan_not_zero", this->CurFn);
    Builder.CreateCondBr(IsZero, End, NotZero);
    Result->addIncoming(ResZero, Begin);

    Builder.SetInsertPoint(NotZero);

    if (BuiltinID == MSVCIntrin::_BitScanForward) {
      Function *F = CGM.getIntrinsic(Intrinsic::cttz, ArgType);
      Value *ZeroCount = Builder.CreateCall(F, {ArgValue, Builder.getTrue()});
      ZeroCount = Builder.CreateIntCast(ZeroCount, IndexType, false);
      Builder.CreateStore(ZeroCount, IndexAddress, false);
    } else {
      unsigned ArgWidth = cast<llvm::IntegerType>(ArgType)->getBitWidth();
      Value *ArgTypeLastIndex = llvm::ConstantInt::get(IndexType, ArgWidth - 1);

      Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);
      Value *ZeroCount = Builder.CreateCall(F, {ArgValue, Builder.getTrue()});
      ZeroCount = Builder.CreateIntCast(ZeroCount, IndexType, false);
      Value *Index = Builder.CreateNSWSub(ArgTypeLastIndex, ZeroCount);
      Builder.CreateStore(Index, IndexAddress, false);
    }
    Builder.CreateBr(End);
    Result->addIncoming(ResOne, NotZero);

    Builder.SetInsertPoint(End);
    return Result;
  }
  case MSVCIntrin::_InterlockedAnd:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E);
  case MSVCIntrin::_InterlockedExchange:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E);
  case MSVCIntrin::_InterlockedExchangeAdd:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E);
  case MSVCIntrin::_InterlockedExchangeSub:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Sub, E);
  case MSVCIntrin::_InterlockedOr:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E);
  case MSVCIntrin::_InterlockedXor:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E);
  case MSVCIntrin::_InterlockedExchangeAdd_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedExchangeAdd_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedExchangeAdd_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedExchange_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedExchange_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedExchange_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedCompareExchange_acq:
    return EmitAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedCompareExchange_rel:
    return EmitAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedCompareExchange_nf:
    return EmitAtomicCmpXchgForMSIntrin(*this, E, AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedCompareExchange128:
    return EmitAtomicCmpXchg128ForMSIntrin(
        *this, E, AtomicOrdering::SequentiallyConsistent);
  case MSVCIntrin::_InterlockedCompareExchange128_acq:
    return EmitAtomicCmpXchg128ForMSIntrin(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedCompareExchange128_rel:
    return EmitAtomicCmpXchg128ForMSIntrin(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedCompareExchange128_nf:
    return EmitAtomicCmpXchg128ForMSIntrin(*this, E, AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedOr_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedOr_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedOr_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedXor_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedXor_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedXor_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xor, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedAnd_acq:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedAnd_rel:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedAnd_nf:
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedIncrement_acq:
    return EmitAtomicIncrementValue(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedIncrement_rel:
    return EmitAtomicIncrementValue(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedIncrement_nf:
    return EmitAtomicIncrementValue(*this, E, AtomicOrdering::Monotonic);
  case MSVCIntrin::_InterlockedDecrement_acq:
    return EmitAtomicDecrementValue(*this, E, AtomicOrdering::Acquire);
  case MSVCIntrin::_InterlockedDecrement_rel:
    return EmitAtomicDecrementValue(*this, E, AtomicOrdering::Release);
  case MSVCIntrin::_InterlockedDecrement_nf:
    return EmitAtomicDecrementValue(*this, E, AtomicOrdering::Monotonic);

  case MSVCIntrin::_InterlockedDecrement:
    return EmitAtomicDecrementValue(*this, E);
  case MSVCIntrin::_InterlockedIncrement:
    return EmitAtomicIncrementValue(*this, E);

  case MSVCIntrin::__fastfail: {
    // Request immediate process termination from the kernel. The instruction
    // sequences to do this are documented on MSDN:
    // https://msdn.microsoft.com/en-us/library/dn774154.aspx
    llvm::Triple::ArchType ISA = getTarget().getTriple().getArch();
    StringRef Asm, Constraints;
    switch (ISA) {
    default:
      ErrorUnsupported(E, "__fastfail call for this architecture");
      break;
    case llvm::Triple::x86:
    case llvm::Triple::x86_64:
      Asm = "int $$0x29";
      Constraints = "{cx}";
      break;
    case llvm::Triple::thumb:
      Asm = "udf #251";
      Constraints = "{r0}";
      break;
    case llvm::Triple::aarch64:
      Asm = "brk #0xF003";
      Constraints = "{w0}";
    }
    llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, {Int32Ty}, false);
    llvm::InlineAsm *IA =
        llvm::InlineAsm::get(FTy, Asm, Constraints, /*hasSideEffects=*/true);
    llvm::AttributeList NoReturnAttr = llvm::AttributeList::get(
        getLLVMContext(), llvm::AttributeList::FunctionIndex,
        llvm::Attribute::NoReturn);
    llvm::CallInst *CI = Builder.CreateCall(IA, EmitScalarExpr(E->getArg(0)));
    CI->setAttributes(NoReturnAttr);
    return CI;
  }
  }
  llvm_unreachable("Incorrect MSVC intrinsic!");
}

namespace {
// ARC cleanup for __builtin_os_log_format
struct CallObjCArcUse final : EHScopeStack::Cleanup {
  CallObjCArcUse(llvm::Value *object) : object(object) {}
  llvm::Value *object;

  void Emit(CodeGenFunction &CGF, Flags flags) override {
    CGF.EmitARCIntrinsicUse(object);
  }
};
}

Value *CodeGenFunction::EmitCheckedArgForBuiltin(const Expr *E,
                                                 BuiltinCheckKind Kind) {
  assert((Kind == BCK_CLZPassedZero || Kind == BCK_CTZPassedZero)
          && "Unsupported builtin check kind");

  Value *ArgValue = EmitScalarExpr(E);
  if (!SanOpts.has(SanitizerKind::Builtin))
    return ArgValue;

  SanitizerScope SanScope(this);
  Value *Cond = Builder.CreateICmpNE(
      ArgValue, llvm::Constant::getNullValue(ArgValue->getType()));
  EmitCheck(std::make_pair(Cond, SanitizerKind::Builtin),
            SanitizerHandler::InvalidBuiltin,
            {EmitCheckSourceLocation(E->getExprLoc()),
             llvm::ConstantInt::get(Builder.getInt8Ty(), Kind)},
            std::nullopt);
  return ArgValue;
}

static Value *EmitAbs(CodeGenFunction &CGF, Value *ArgValue, bool HasNSW) {
  return CGF.Builder.CreateBinaryIntrinsic(
      Intrinsic::abs, ArgValue,
      ConstantInt::get(CGF.Builder.getInt1Ty(), HasNSW));
}

static Value *EmitOverflowCheckedAbs(CodeGenFunction &CGF, const CallExpr *E,
                                     bool SanitizeOverflow) {
  Value *ArgValue = CGF.EmitScalarExpr(E->getArg(0));

  // Try to eliminate overflow check.
  if (const auto *VCI = dyn_cast<llvm::ConstantInt>(ArgValue)) {
    if (!VCI->isMinSignedValue())
      return EmitAbs(CGF, ArgValue, true);
  }

  CodeGenFunction::SanitizerScope SanScope(&CGF);

  Constant *Zero = Constant::getNullValue(ArgValue->getType());
  Value *ResultAndOverflow = CGF.Builder.CreateBinaryIntrinsic(
      Intrinsic::ssub_with_overflow, Zero, ArgValue);
  Value *Result = CGF.Builder.CreateExtractValue(ResultAndOverflow, 0);
  Value *NotOverflow = CGF.Builder.CreateNot(
      CGF.Builder.CreateExtractValue(ResultAndOverflow, 1));

  // TODO: support -ftrapv-handler.
  if (SanitizeOverflow) {
    CGF.EmitCheck({{NotOverflow, SanitizerKind::SignedIntegerOverflow}},
                  SanitizerHandler::NegateOverflow,
                  {CGF.EmitCheckSourceLocation(E->getArg(0)->getExprLoc()),
                   CGF.EmitCheckTypeDescriptor(E->getType())},
                  {ArgValue});
  } else
    CGF.EmitTrapCheck(NotOverflow, SanitizerHandler::SubOverflow);

  Value *CmpResult = CGF.Builder.CreateICmpSLT(ArgValue, Zero, "abscond");
  return CGF.Builder.CreateSelect(CmpResult, Result, ArgValue, "abs");
}

/// Get the argument type for arguments to os_log_helper.
static CanQualType getOSLogArgType(ASTContext &C, int Size) {
  QualType UnsignedTy = C.getIntTypeForBitwidth(Size * 8, /*Signed=*/false);
  return C.getCanonicalType(UnsignedTy);
}

llvm::Function *CodeGenFunction::generateBuiltinOSLogHelperFunction(
    const analyze_os_log::OSLogBufferLayout &Layout,
    CharUnits BufferAlignment) {
  ASTContext &Ctx = getContext();

  llvm::SmallString<64> Name;
  {
    raw_svector_ostream OS(Name);
    OS << "__os_log_helper";
    OS << "_" << BufferAlignment.getQuantity();
    OS << "_" << int(Layout.getSummaryByte());
    OS << "_" << int(Layout.getNumArgsByte());
    for (const auto &Item : Layout.Items)
      OS << "_" << int(Item.getSizeByte()) << "_"
         << int(Item.getDescriptorByte());
  }

  if (llvm::Function *F = CGM.getModule().getFunction(Name))
    return F;

  llvm::SmallVector<QualType, 4> ArgTys;
  FunctionArgList Args;
  Args.push_back(ImplicitParamDecl::Create(
      Ctx, nullptr, SourceLocation(), &Ctx.Idents.get("buffer"), Ctx.VoidPtrTy,
      ImplicitParamKind::Other));
  ArgTys.emplace_back(Ctx.VoidPtrTy);

  for (unsigned int I = 0, E = Layout.Items.size(); I < E; ++I) {
    char Size = Layout.Items[I].getSizeByte();
    if (!Size)
      continue;

    QualType ArgTy = getOSLogArgType(Ctx, Size);
    Args.push_back(ImplicitParamDecl::Create(
        Ctx, nullptr, SourceLocation(),
        &Ctx.Idents.get(std::string("arg") + llvm::to_string(I)), ArgTy,
        ImplicitParamKind::Other));
    ArgTys.emplace_back(ArgTy);
  }

  QualType ReturnTy = Ctx.VoidTy;

  // The helper function has linkonce_odr linkage to enable the linker to merge
  // identical functions. To ensure the merging always happens, 'noinline' is
  // attached to the function when compiling with -Oz.
  const CGFunctionInfo &FI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(ReturnTy, Args);
  llvm::FunctionType *FuncTy = CGM.getTypes().GetFunctionType(FI);
  llvm::Function *Fn = llvm::Function::Create(
      FuncTy, llvm::GlobalValue::LinkOnceODRLinkage, Name, &CGM.getModule());
  Fn->setVisibility(llvm::GlobalValue::HiddenVisibility);
  CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI, Fn, /*IsThunk=*/false);
  CGM.SetLLVMFunctionAttributesForDefinition(nullptr, Fn);
  Fn->setDoesNotThrow();

  // Attach 'noinline' at -Oz.
  if (CGM.getCodeGenOpts().OptimizeSize == 2)
    Fn->addFnAttr(llvm::Attribute::NoInline);

  auto NL = ApplyDebugLocation::CreateEmpty(*this);
  StartFunction(GlobalDecl(), ReturnTy, Fn, FI, Args);

  // Create a scope with an artificial location for the body of this function.
  auto AL = ApplyDebugLocation::CreateArtificial(*this);

  CharUnits Offset;
  Address BufAddr = makeNaturalAddressForPointer(
      Builder.CreateLoad(GetAddrOfLocalVar(Args[0]), "buf"), Ctx.VoidTy,
      BufferAlignment);
  Builder.CreateStore(Builder.getInt8(Layout.getSummaryByte()),
                      Builder.CreateConstByteGEP(BufAddr, Offset++, "summary"));
  Builder.CreateStore(Builder.getInt8(Layout.getNumArgsByte()),
                      Builder.CreateConstByteGEP(BufAddr, Offset++, "numArgs"));

  unsigned I = 1;
  for (const auto &Item : Layout.Items) {
    Builder.CreateStore(
        Builder.getInt8(Item.getDescriptorByte()),
        Builder.CreateConstByteGEP(BufAddr, Offset++, "argDescriptor"));
    Builder.CreateStore(
        Builder.getInt8(Item.getSizeByte()),
        Builder.CreateConstByteGEP(BufAddr, Offset++, "argSize"));

    CharUnits Size = Item.size();
    if (!Size.getQuantity())
      continue;

    Address Arg = GetAddrOfLocalVar(Args[I]);
    Address Addr = Builder.CreateConstByteGEP(BufAddr, Offset, "argData");
    Addr = Addr.withElementType(Arg.getElementType());
    Builder.CreateStore(Builder.CreateLoad(Arg), Addr);
    Offset += Size;
    ++I;
  }

  FinishFunction();

  return Fn;
}

RValue CodeGenFunction::emitBuiltinOSLogFormat(const CallExpr &E) {
  assert(E.getNumArgs() >= 2 &&
         "__builtin_os_log_format takes at least 2 arguments");
  ASTContext &Ctx = getContext();
  analyze_os_log::OSLogBufferLayout Layout;
  analyze_os_log::computeOSLogBufferLayout(Ctx, &E, Layout);
  Address BufAddr = EmitPointerWithAlignment(E.getArg(0));
  llvm::SmallVector<llvm::Value *, 4> RetainableOperands;

  // Ignore argument 1, the format string. It is not currently used.
  CallArgList Args;
  Args.add(RValue::get(BufAddr.emitRawPointer(*this)), Ctx.VoidPtrTy);

  for (const auto &Item : Layout.Items) {
    int Size = Item.getSizeByte();
    if (!Size)
      continue;

    llvm::Value *ArgVal;

    if (Item.getKind() == analyze_os_log::OSLogBufferItem::MaskKind) {
      uint64_t Val = 0;
      for (unsigned I = 0, E = Item.getMaskType().size(); I < E; ++I)
        Val |= ((uint64_t)Item.getMaskType()[I]) << I * 8;
      ArgVal = llvm::Constant::getIntegerValue(Int64Ty, llvm::APInt(64, Val));
    } else if (const Expr *TheExpr = Item.getExpr()) {
      ArgVal = EmitScalarExpr(TheExpr, /*Ignore*/ false);

      // If a temporary object that requires destruction after the full
      // expression is passed, push a lifetime-extended cleanup to extend its
      // lifetime to the end of the enclosing block scope.
      auto LifetimeExtendObject = [&](const Expr *E) {
        E = E->IgnoreParenCasts();
        // Extend lifetimes of objects returned by function calls and message
        // sends.

        // FIXME: We should do this in other cases in which temporaries are
        //        created including arguments of non-ARC types (e.g., C++
        //        temporaries).
        if (isa<CallExpr>(E) || isa<ObjCMessageExpr>(E))
          return true;
        return false;
      };

      if (TheExpr->getType()->isObjCRetainableType() &&
          getLangOpts().ObjCAutoRefCount && LifetimeExtendObject(TheExpr)) {
        assert(getEvaluationKind(TheExpr->getType()) == TEK_Scalar &&
               "Only scalar can be a ObjC retainable type");
        if (!isa<Constant>(ArgVal)) {
          CleanupKind Cleanup = getARCCleanupKind();
          QualType Ty = TheExpr->getType();
          RawAddress Alloca = RawAddress::invalid();
          RawAddress Addr = CreateMemTemp(Ty, "os.log.arg", &Alloca);
          ArgVal = EmitARCRetain(Ty, ArgVal);
          Builder.CreateStore(ArgVal, Addr);
          pushLifetimeExtendedDestroy(Cleanup, Alloca, Ty,
                                      CodeGenFunction::destroyARCStrongPrecise,
                                      Cleanup & EHCleanup);

          // Push a clang.arc.use call to ensure ARC optimizer knows that the
          // argument has to be alive.
          if (CGM.getCodeGenOpts().OptimizationLevel != 0)
            pushCleanupAfterFullExpr<CallObjCArcUse>(Cleanup, ArgVal);
        }
      }
    } else {
      ArgVal = Builder.getInt32(Item.getConstValue().getQuantity());
    }

    unsigned ArgValSize =
        CGM.getDataLayout().getTypeSizeInBits(ArgVal->getType());
    llvm::IntegerType *IntTy = llvm::Type::getIntNTy(getLLVMContext(),
                                                     ArgValSize);
    ArgVal = Builder.CreateBitOrPointerCast(ArgVal, IntTy);
    CanQualType ArgTy = getOSLogArgType(Ctx, Size);
    // If ArgVal has type x86_fp80, zero-extend ArgVal.
    ArgVal = Builder.CreateZExtOrBitCast(ArgVal, ConvertType(ArgTy));
    Args.add(RValue::get(ArgVal), ArgTy);
  }

  const CGFunctionInfo &FI =
      CGM.getTypes().arrangeBuiltinFunctionCall(Ctx.VoidTy, Args);
  llvm::Function *F = CodeGenFunction(CGM).generateBuiltinOSLogHelperFunction(
      Layout, BufAddr.getAlignment());
  EmitCall(FI, CGCallee::forDirect(F), ReturnValueSlot(), Args);
  return RValue::get(BufAddr, *this);
}

static bool isSpecialUnsignedMultiplySignedResult(
    unsigned BuiltinID, WidthAndSignedness Op1Info, WidthAndSignedness Op2Info,
    WidthAndSignedness ResultInfo) {
  return BuiltinID == Builtin::BI__builtin_mul_overflow &&
         Op1Info.Width == Op2Info.Width && Op2Info.Width == ResultInfo.Width &&
         !Op1Info.Signed && !Op2Info.Signed && ResultInfo.Signed;
}

static RValue EmitCheckedUnsignedMultiplySignedResult(
    CodeGenFunction &CGF, const clang::Expr *Op1, WidthAndSignedness Op1Info,
    const clang::Expr *Op2, WidthAndSignedness Op2Info,
    const clang::Expr *ResultArg, QualType ResultQTy,
    WidthAndSignedness ResultInfo) {
  assert(isSpecialUnsignedMultiplySignedResult(
             Builtin::BI__builtin_mul_overflow, Op1Info, Op2Info, ResultInfo) &&
         "Cannot specialize this multiply");

  llvm::Value *V1 = CGF.EmitScalarExpr(Op1);
  llvm::Value *V2 = CGF.EmitScalarExpr(Op2);

  llvm::Value *HasOverflow;
  llvm::Value *Result = EmitOverflowIntrinsic(
      CGF, llvm::Intrinsic::umul_with_overflow, V1, V2, HasOverflow);

  // The intrinsic call will detect overflow when the value is > UINT_MAX,
  // however, since the original builtin had a signed result, we need to report
  // an overflow when the result is greater than INT_MAX.
  auto IntMax = llvm::APInt::getSignedMaxValue(ResultInfo.Width);
  llvm::Value *IntMaxValue = llvm::ConstantInt::get(Result->getType(), IntMax);

  llvm::Value *IntMaxOverflow = CGF.Builder.CreateICmpUGT(Result, IntMaxValue);
  HasOverflow = CGF.Builder.CreateOr(HasOverflow, IntMaxOverflow);

  bool isVolatile =
      ResultArg->getType()->getPointeeType().isVolatileQualified();
  Address ResultPtr = CGF.EmitPointerWithAlignment(ResultArg);
  CGF.Builder.CreateStore(CGF.EmitToMemory(Result, ResultQTy), ResultPtr,
                          isVolatile);
  return RValue::get(HasOverflow);
}

/// Determine if a binop is a checked mixed-sign multiply we can specialize.
static bool isSpecialMixedSignMultiply(unsigned BuiltinID,
                                       WidthAndSignedness Op1Info,
                                       WidthAndSignedness Op2Info,
                                       WidthAndSignedness ResultInfo) {
  return BuiltinID == Builtin::BI__builtin_mul_overflow &&
         std::max(Op1Info.Width, Op2Info.Width) >= ResultInfo.Width &&
         Op1Info.Signed != Op2Info.Signed;
}

/// Emit a checked mixed-sign multiply. This is a cheaper specialization of
/// the generic checked-binop irgen.
static RValue
EmitCheckedMixedSignMultiply(CodeGenFunction &CGF, const clang::Expr *Op1,
                             WidthAndSignedness Op1Info, const clang::Expr *Op2,
                             WidthAndSignedness Op2Info,
                             const clang::Expr *ResultArg, QualType ResultQTy,
                             WidthAndSignedness ResultInfo) {
  assert(isSpecialMixedSignMultiply(Builtin::BI__builtin_mul_overflow, Op1Info,
                                    Op2Info, ResultInfo) &&
         "Not a mixed-sign multipliction we can specialize");

  // Emit the signed and unsigned operands.
  const clang::Expr *SignedOp = Op1Info.Signed ? Op1 : Op2;
  const clang::Expr *UnsignedOp = Op1Info.Signed ? Op2 : Op1;
  llvm::Value *Signed = CGF.EmitScalarExpr(SignedOp);
  llvm::Value *Unsigned = CGF.EmitScalarExpr(UnsignedOp);
  unsigned SignedOpWidth = Op1Info.Signed ? Op1Info.Width : Op2Info.Width;
  unsigned UnsignedOpWidth = Op1Info.Signed ? Op2Info.Width : Op1Info.Width;

  // One of the operands may be smaller than the other. If so, [s|z]ext it.
  if (SignedOpWidth < UnsignedOpWidth)
    Signed = CGF.Builder.CreateSExt(Signed, Unsigned->getType(), "op.sext");
  if (UnsignedOpWidth < SignedOpWidth)
    Unsigned = CGF.Builder.CreateZExt(Unsigned, Signed->getType(), "op.zext");

  llvm::Type *OpTy = Signed->getType();
  llvm::Value *Zero = llvm::Constant::getNullValue(OpTy);
  Address ResultPtr = CGF.EmitPointerWithAlignment(ResultArg);
  llvm::Type *ResTy = ResultPtr.getElementType();
  unsigned OpWidth = std::max(Op1Info.Width, Op2Info.Width);

  // Take the absolute value of the signed operand.
  llvm::Value *IsNegative = CGF.Builder.CreateICmpSLT(Signed, Zero);
  llvm::Value *AbsOfNegative = CGF.Builder.CreateSub(Zero, Signed);
  llvm::Value *AbsSigned =
      CGF.Builder.CreateSelect(IsNegative, AbsOfNegative, Signed);

  // Perform a checked unsigned multiplication.
  llvm::Value *UnsignedOverflow;
  llvm::Value *UnsignedResult =
      EmitOverflowIntrinsic(CGF, llvm::Intrinsic::umul_with_overflow, AbsSigned,
                            Unsigned, UnsignedOverflow);

  llvm::Value *Overflow, *Result;
  if (ResultInfo.Signed) {
    // Signed overflow occurs if the result is greater than INT_MAX or lesser
    // than INT_MIN, i.e when |Result| > (INT_MAX + IsNegative).
    auto IntMax =
        llvm::APInt::getSignedMaxValue(ResultInfo.Width).zext(OpWidth);
    llvm::Value *MaxResult =
        CGF.Builder.CreateAdd(llvm::ConstantInt::get(OpTy, IntMax),
                              CGF.Builder.CreateZExt(IsNegative, OpTy));
    llvm::Value *SignedOverflow =
        CGF.Builder.CreateICmpUGT(UnsignedResult, MaxResult);
    Overflow = CGF.Builder.CreateOr(UnsignedOverflow, SignedOverflow);

    // Prepare the signed result (possibly by negating it).
    llvm::Value *NegativeResult = CGF.Builder.CreateNeg(UnsignedResult);
    llvm::Value *SignedResult =
        CGF.Builder.CreateSelect(IsNegative, NegativeResult, UnsignedResult);
    Result = CGF.Builder.CreateTrunc(SignedResult, ResTy);
  } else {
    // Unsigned overflow occurs if the result is < 0 or greater than UINT_MAX.
    llvm::Value *Underflow = CGF.Builder.CreateAnd(
        IsNegative, CGF.Builder.CreateIsNotNull(UnsignedResult));
    Overflow = CGF.Builder.CreateOr(UnsignedOverflow, Underflow);
    if (ResultInfo.Width < OpWidth) {
      auto IntMax =
          llvm::APInt::getMaxValue(ResultInfo.Width).zext(OpWidth);
      llvm::Value *TruncOverflow = CGF.Builder.CreateICmpUGT(
          UnsignedResult, llvm::ConstantInt::get(OpTy, IntMax));
      Overflow = CGF.Builder.CreateOr(Overflow, TruncOverflow);
    }

    // Negate the product if it would be negative in infinite precision.
    Result = CGF.Builder.CreateSelect(
        IsNegative, CGF.Builder.CreateNeg(UnsignedResult), UnsignedResult);

    Result = CGF.Builder.CreateTrunc(Result, ResTy);
  }
  assert(Overflow && Result && "Missing overflow or result");

  bool isVolatile =
      ResultArg->getType()->getPointeeType().isVolatileQualified();
  CGF.Builder.CreateStore(CGF.EmitToMemory(Result, ResultQTy), ResultPtr,
                          isVolatile);
  return RValue::get(Overflow);
}

static bool
TypeRequiresBuiltinLaunderImp(const ASTContext &Ctx, QualType Ty,
                              llvm::SmallPtrSetImpl<const Decl *> &Seen) {
  if (const auto *Arr = Ctx.getAsArrayType(Ty))
    Ty = Ctx.getBaseElementType(Arr);

  const auto *Record = Ty->getAsCXXRecordDecl();
  if (!Record)
    return false;

  // We've already checked this type, or are in the process of checking it.
  if (!Seen.insert(Record).second)
    return false;

  assert(Record->hasDefinition() &&
         "Incomplete types should already be diagnosed");

  if (Record->isDynamicClass())
    return true;

  for (FieldDecl *F : Record->fields()) {
    if (TypeRequiresBuiltinLaunderImp(Ctx, F->getType(), Seen))
      return true;
  }
  return false;
}

/// Determine if the specified type requires laundering by checking if it is a
/// dynamic class type or contains a subobject which is a dynamic class type.
static bool TypeRequiresBuiltinLaunder(CodeGenModule &CGM, QualType Ty) {
  if (!CGM.getCodeGenOpts().StrictVTablePointers)
    return false;
  llvm::SmallPtrSet<const Decl *, 16> Seen;
  return TypeRequiresBuiltinLaunderImp(CGM.getContext(), Ty, Seen);
}

RValue CodeGenFunction::emitRotate(const CallExpr *E, bool IsRotateRight) {
  llvm::Value *Src = EmitScalarExpr(E->getArg(0));
  llvm::Value *ShiftAmt = EmitScalarExpr(E->getArg(1));

  // The builtin's shift arg may have a different type than the source arg and
  // result, but the LLVM intrinsic uses the same type for all values.
  llvm::Type *Ty = Src->getType();
  ShiftAmt = Builder.CreateIntCast(ShiftAmt, Ty, false);

  // Rotate is a special case of LLVM funnel shift - 1st 2 args are the same.
  unsigned IID = IsRotateRight ? Intrinsic::fshr : Intrinsic::fshl;
  Function *F = CGM.getIntrinsic(IID, Ty);
  return RValue::get(Builder.CreateCall(F, { Src, Src, ShiftAmt }));
}

// Map math builtins for long-double to f128 version.
static unsigned mutateLongDoubleBuiltin(unsigned BuiltinID) {
  switch (BuiltinID) {
#define MUTATE_LDBL(func) \
  case Builtin::BI__builtin_##func##l: \
    return Builtin::BI__builtin_##func##f128;
  MUTATE_LDBL(sqrt)
  MUTATE_LDBL(cbrt)
  MUTATE_LDBL(fabs)
  MUTATE_LDBL(log)
  MUTATE_LDBL(log2)
  MUTATE_LDBL(log10)
  MUTATE_LDBL(log1p)
  MUTATE_LDBL(logb)
  MUTATE_LDBL(exp)
  MUTATE_LDBL(exp2)
  MUTATE_LDBL(expm1)
  MUTATE_LDBL(fdim)
  MUTATE_LDBL(hypot)
  MUTATE_LDBL(ilogb)
  MUTATE_LDBL(pow)
  MUTATE_LDBL(fmin)
  MUTATE_LDBL(fmax)
  MUTATE_LDBL(ceil)
  MUTATE_LDBL(trunc)
  MUTATE_LDBL(rint)
  MUTATE_LDBL(nearbyint)
  MUTATE_LDBL(round)
  MUTATE_LDBL(floor)
  MUTATE_LDBL(lround)
  MUTATE_LDBL(llround)
  MUTATE_LDBL(lrint)
  MUTATE_LDBL(llrint)
  MUTATE_LDBL(fmod)
  MUTATE_LDBL(modf)
  MUTATE_LDBL(nan)
  MUTATE_LDBL(nans)
  MUTATE_LDBL(inf)
  MUTATE_LDBL(fma)
  MUTATE_LDBL(sin)
  MUTATE_LDBL(cos)
  MUTATE_LDBL(tan)
  MUTATE_LDBL(sinh)
  MUTATE_LDBL(cosh)
  MUTATE_LDBL(tanh)
  MUTATE_LDBL(asin)
  MUTATE_LDBL(acos)
  MUTATE_LDBL(atan)
  MUTATE_LDBL(asinh)
  MUTATE_LDBL(acosh)
  MUTATE_LDBL(atanh)
  MUTATE_LDBL(atan2)
  MUTATE_LDBL(erf)
  MUTATE_LDBL(erfc)
  MUTATE_LDBL(ldexp)
  MUTATE_LDBL(frexp)
  MUTATE_LDBL(huge_val)
  MUTATE_LDBL(copysign)
  MUTATE_LDBL(nextafter)
  MUTATE_LDBL(nexttoward)
  MUTATE_LDBL(remainder)
  MUTATE_LDBL(remquo)
  MUTATE_LDBL(scalbln)
  MUTATE_LDBL(scalbn)
  MUTATE_LDBL(tgamma)
  MUTATE_LDBL(lgamma)
#undef MUTATE_LDBL
  default:
    return BuiltinID;
  }
}

static Value *tryUseTestFPKind(CodeGenFunction &CGF, unsigned BuiltinID,
                               Value *V) {
  if (CGF.Builder.getIsFPConstrained() &&
      CGF.Builder.getDefaultConstrainedExcept() != fp::ebIgnore) {
    if (Value *Result =
            CGF.getTargetHooks().testFPKind(V, BuiltinID, CGF.Builder, CGF.CGM))
      return Result;
  }
  return nullptr;
}

static RValue EmitHipStdParUnsupportedBuiltin(CodeGenFunction *CGF,
                                              const FunctionDecl *FD) {
  auto Name = FD->getNameAsString() + "__hipstdpar_unsupported";
  auto FnTy = CGF->CGM.getTypes().GetFunctionType(FD);
  auto UBF = CGF->CGM.getModule().getOrInsertFunction(Name, FnTy);

  SmallVector<Value *, 16> Args;
  for (auto &&FormalTy : FnTy->params())
    Args.push_back(llvm::PoisonValue::get(FormalTy));

  return RValue::get(CGF->Builder.CreateCall(UBF, Args));
}

RValue CodeGenFunction::EmitBuiltinExpr(const GlobalDecl GD, unsigned BuiltinID,
                                        const CallExpr *E,
                                        ReturnValueSlot ReturnValue) {
  const FunctionDecl *FD = GD.getDecl()->getAsFunction();
  // See if we can constant fold this builtin.  If so, don't emit it at all.
  // TODO: Extend this handling to all builtin calls that we can constant-fold.
  Expr::EvalResult Result;
  if (E->isPRValue() && E->EvaluateAsRValue(Result, CGM.getContext()) &&
      !Result.hasSideEffects()) {
    if (Result.Val.isInt())
      return RValue::get(llvm::ConstantInt::get(getLLVMContext(),
                                                Result.Val.getInt()));
    if (Result.Val.isFloat())
      return RValue::get(llvm::ConstantFP::get(getLLVMContext(),
                                               Result.Val.getFloat()));
  }

  // If current long-double semantics is IEEE 128-bit, replace math builtins
  // of long-double with f128 equivalent.
  // TODO: This mutation should also be applied to other targets other than PPC,
  // after backend supports IEEE 128-bit style libcalls.
  if (getTarget().getTriple().isPPC64() &&
      &getTarget().getLongDoubleFormat() == &llvm::APFloat::IEEEquad())
    BuiltinID = mutateLongDoubleBuiltin(BuiltinID);

  // If the builtin has been declared explicitly with an assembler label,
  // disable the specialized emitting below. Ideally we should communicate the
  // rename in IR, or at least avoid generating the intrinsic calls that are
  // likely to get lowered to the renamed library functions.
  const unsigned BuiltinIDIfNoAsmLabel =
      FD->hasAttr<AsmLabelAttr>() ? 0 : BuiltinID;

  std::optional<bool> ErrnoOverriden;
  // ErrnoOverriden is true if math-errno is overriden via the
  // '#pragma float_control(precise, on)'. This pragma disables fast-math,
  // which implies math-errno.
  if (E->hasStoredFPFeatures()) {
    FPOptionsOverride OP = E->getFPFeatures();
    if (OP.hasMathErrnoOverride())
      ErrnoOverriden = OP.getMathErrnoOverride();
  }
  // True if 'attribute__((optnone))' is used. This attribute overrides
  // fast-math which implies math-errno.
  bool OptNone = CurFuncDecl && CurFuncDecl->hasAttr<OptimizeNoneAttr>();

  // True if we are compiling at -O2 and errno has been disabled
  // using the '#pragma float_control(precise, off)', and
  // attribute opt-none hasn't been seen.
  bool ErrnoOverridenToFalseWithOpt =
       ErrnoOverriden.has_value() && !ErrnoOverriden.value() && !OptNone &&
       CGM.getCodeGenOpts().OptimizationLevel != 0;

  // There are LLVM math intrinsics/instructions corresponding to math library
  // functions except the LLVM op will never set errno while the math library
  // might. Also, math builtins have the same semantics as their math library
  // twins. Thus, we can transform math library and builtin calls to their
  // LLVM counterparts if the call is marked 'const' (known to never set errno).
  // In case FP exceptions are enabled, the experimental versions of the
  // intrinsics model those.
  bool ConstAlways =
      getContext().BuiltinInfo.isConst(BuiltinID);

  // There's a special case with the fma builtins where they are always const
  // if the target environment is GNU or the target is OS is Windows and we're
  // targeting the MSVCRT.dll environment.
  // FIXME: This list can be become outdated. Need to find a way to get it some
  // other way.
  switch (BuiltinID) {
  case Builtin::BI__builtin_fma:
  case Builtin::BI__builtin_fmaf:
  case Builtin::BI__builtin_fmal:
  case Builtin::BI__builtin_fmaf16:
  case Builtin::BIfma:
  case Builtin::BIfmaf:
  case Builtin::BIfmal: {
    auto &Trip = CGM.getTriple();
    if (Trip.isGNUEnvironment() || Trip.isOSMSVCRT())
      ConstAlways = true;
    break;
  }
  default:
    break;
  }

  bool ConstWithoutErrnoAndExceptions =
      getContext().BuiltinInfo.isConstWithoutErrnoAndExceptions(BuiltinID);
  bool ConstWithoutExceptions =
      getContext().BuiltinInfo.isConstWithoutExceptions(BuiltinID);

  // ConstAttr is enabled in fast-math mode. In fast-math mode, math-errno is
  // disabled.
  // Math intrinsics are generated only when math-errno is disabled. Any pragmas
  // or attributes that affect math-errno should prevent or allow math
  // intrincs to be generated. Intrinsics are generated:
  //   1- In fast math mode, unless math-errno is overriden
  //      via '#pragma float_control(precise, on)', or via an
  //      'attribute__((optnone))'.
  //   2- If math-errno was enabled on command line but overriden
  //      to false via '#pragma float_control(precise, off))' and
  //      'attribute__((optnone))' hasn't been used.
  //   3- If we are compiling with optimization and errno has been disabled
  //      via '#pragma float_control(precise, off)', and
  //      'attribute__((optnone))' hasn't been used.

  bool ConstWithoutErrnoOrExceptions =
      ConstWithoutErrnoAndExceptions || ConstWithoutExceptions;
  bool GenerateIntrinsics =
      (ConstAlways && !OptNone) ||
      (!getLangOpts().MathErrno &&
       !(ErrnoOverriden.has_value() && ErrnoOverriden.value()) && !OptNone);
  if (!GenerateIntrinsics) {
    GenerateIntrinsics =
        ConstWithoutErrnoOrExceptions && !ConstWithoutErrnoAndExceptions;
    if (!GenerateIntrinsics)
      GenerateIntrinsics =
          ConstWithoutErrnoOrExceptions &&
          (!getLangOpts().MathErrno &&
           !(ErrnoOverriden.has_value() && ErrnoOverriden.value()) && !OptNone);
    if (!GenerateIntrinsics)
      GenerateIntrinsics =
          ConstWithoutErrnoOrExceptions && ErrnoOverridenToFalseWithOpt;
  }
  if (GenerateIntrinsics) {
    switch (BuiltinIDIfNoAsmLabel) {
    case Builtin::BIacos:
    case Builtin::BIacosf:
    case Builtin::BIacosl:
    case Builtin::BI__builtin_acos:
    case Builtin::BI__builtin_acosf:
    case Builtin::BI__builtin_acosf16:
    case Builtin::BI__builtin_acosl:
    case Builtin::BI__builtin_acosf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::acos, Intrinsic::experimental_constrained_acos));

    case Builtin::BIasin:
    case Builtin::BIasinf:
    case Builtin::BIasinl:
    case Builtin::BI__builtin_asin:
    case Builtin::BI__builtin_asinf:
    case Builtin::BI__builtin_asinf16:
    case Builtin::BI__builtin_asinl:
    case Builtin::BI__builtin_asinf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::asin, Intrinsic::experimental_constrained_asin));

    case Builtin::BIatan:
    case Builtin::BIatanf:
    case Builtin::BIatanl:
    case Builtin::BI__builtin_atan:
    case Builtin::BI__builtin_atanf:
    case Builtin::BI__builtin_atanf16:
    case Builtin::BI__builtin_atanl:
    case Builtin::BI__builtin_atanf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::atan, Intrinsic::experimental_constrained_atan));

    case Builtin::BIceil:
    case Builtin::BIceilf:
    case Builtin::BIceill:
    case Builtin::BI__builtin_ceil:
    case Builtin::BI__builtin_ceilf:
    case Builtin::BI__builtin_ceilf16:
    case Builtin::BI__builtin_ceill:
    case Builtin::BI__builtin_ceilf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::ceil,
                                   Intrinsic::experimental_constrained_ceil));

    case Builtin::BIcopysign:
    case Builtin::BIcopysignf:
    case Builtin::BIcopysignl:
    case Builtin::BI__builtin_copysign:
    case Builtin::BI__builtin_copysignf:
    case Builtin::BI__builtin_copysignf16:
    case Builtin::BI__builtin_copysignl:
    case Builtin::BI__builtin_copysignf128:
      return RValue::get(
          emitBuiltinWithOneOverloadedType<2>(*this, E, Intrinsic::copysign));

    case Builtin::BIcos:
    case Builtin::BIcosf:
    case Builtin::BIcosl:
    case Builtin::BI__builtin_cos:
    case Builtin::BI__builtin_cosf:
    case Builtin::BI__builtin_cosf16:
    case Builtin::BI__builtin_cosl:
    case Builtin::BI__builtin_cosf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::cos,
                                   Intrinsic::experimental_constrained_cos));

    case Builtin::BIcosh:
    case Builtin::BIcoshf:
    case Builtin::BIcoshl:
    case Builtin::BI__builtin_cosh:
    case Builtin::BI__builtin_coshf:
    case Builtin::BI__builtin_coshf16:
    case Builtin::BI__builtin_coshl:
    case Builtin::BI__builtin_coshf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::cosh, Intrinsic::experimental_constrained_cosh));

    case Builtin::BIexp:
    case Builtin::BIexpf:
    case Builtin::BIexpl:
    case Builtin::BI__builtin_exp:
    case Builtin::BI__builtin_expf:
    case Builtin::BI__builtin_expf16:
    case Builtin::BI__builtin_expl:
    case Builtin::BI__builtin_expf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::exp,
                                   Intrinsic::experimental_constrained_exp));

    case Builtin::BIexp2:
    case Builtin::BIexp2f:
    case Builtin::BIexp2l:
    case Builtin::BI__builtin_exp2:
    case Builtin::BI__builtin_exp2f:
    case Builtin::BI__builtin_exp2f16:
    case Builtin::BI__builtin_exp2l:
    case Builtin::BI__builtin_exp2f128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::exp2,
                                   Intrinsic::experimental_constrained_exp2));
    case Builtin::BI__builtin_exp10:
    case Builtin::BI__builtin_exp10f:
    case Builtin::BI__builtin_exp10f16:
    case Builtin::BI__builtin_exp10l:
    case Builtin::BI__builtin_exp10f128: {
      // TODO: strictfp support
      if (Builder.getIsFPConstrained())
        break;
      return RValue::get(
          emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::exp10));
    }
    case Builtin::BIfabs:
    case Builtin::BIfabsf:
    case Builtin::BIfabsl:
    case Builtin::BI__builtin_fabs:
    case Builtin::BI__builtin_fabsf:
    case Builtin::BI__builtin_fabsf16:
    case Builtin::BI__builtin_fabsl:
    case Builtin::BI__builtin_fabsf128:
      return RValue::get(
          emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::fabs));

    case Builtin::BIfloor:
    case Builtin::BIfloorf:
    case Builtin::BIfloorl:
    case Builtin::BI__builtin_floor:
    case Builtin::BI__builtin_floorf:
    case Builtin::BI__builtin_floorf16:
    case Builtin::BI__builtin_floorl:
    case Builtin::BI__builtin_floorf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::floor,
                                   Intrinsic::experimental_constrained_floor));

    case Builtin::BIfma:
    case Builtin::BIfmaf:
    case Builtin::BIfmal:
    case Builtin::BI__builtin_fma:
    case Builtin::BI__builtin_fmaf:
    case Builtin::BI__builtin_fmaf16:
    case Builtin::BI__builtin_fmal:
    case Builtin::BI__builtin_fmaf128:
      return RValue::get(emitTernaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::fma,
                                   Intrinsic::experimental_constrained_fma));

    case Builtin::BIfmax:
    case Builtin::BIfmaxf:
    case Builtin::BIfmaxl:
    case Builtin::BI__builtin_fmax:
    case Builtin::BI__builtin_fmaxf:
    case Builtin::BI__builtin_fmaxf16:
    case Builtin::BI__builtin_fmaxl:
    case Builtin::BI__builtin_fmaxf128:
      return RValue::get(emitBinaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::maxnum,
                                   Intrinsic::experimental_constrained_maxnum));

    case Builtin::BIfmin:
    case Builtin::BIfminf:
    case Builtin::BIfminl:
    case Builtin::BI__builtin_fmin:
    case Builtin::BI__builtin_fminf:
    case Builtin::BI__builtin_fminf16:
    case Builtin::BI__builtin_fminl:
    case Builtin::BI__builtin_fminf128:
      return RValue::get(emitBinaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::minnum,
                                   Intrinsic::experimental_constrained_minnum));

    // fmod() is a special-case. It maps to the frem instruction rather than an
    // LLVM intrinsic.
    case Builtin::BIfmod:
    case Builtin::BIfmodf:
    case Builtin::BIfmodl:
    case Builtin::BI__builtin_fmod:
    case Builtin::BI__builtin_fmodf:
    case Builtin::BI__builtin_fmodf16:
    case Builtin::BI__builtin_fmodl:
    case Builtin::BI__builtin_fmodf128: {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
      Value *Arg1 = EmitScalarExpr(E->getArg(0));
      Value *Arg2 = EmitScalarExpr(E->getArg(1));
      return RValue::get(Builder.CreateFRem(Arg1, Arg2, "fmod"));
    }

    case Builtin::BIlog:
    case Builtin::BIlogf:
    case Builtin::BIlogl:
    case Builtin::BI__builtin_log:
    case Builtin::BI__builtin_logf:
    case Builtin::BI__builtin_logf16:
    case Builtin::BI__builtin_logl:
    case Builtin::BI__builtin_logf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::log,
                                   Intrinsic::experimental_constrained_log));

    case Builtin::BIlog10:
    case Builtin::BIlog10f:
    case Builtin::BIlog10l:
    case Builtin::BI__builtin_log10:
    case Builtin::BI__builtin_log10f:
    case Builtin::BI__builtin_log10f16:
    case Builtin::BI__builtin_log10l:
    case Builtin::BI__builtin_log10f128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::log10,
                                   Intrinsic::experimental_constrained_log10));

    case Builtin::BIlog2:
    case Builtin::BIlog2f:
    case Builtin::BIlog2l:
    case Builtin::BI__builtin_log2:
    case Builtin::BI__builtin_log2f:
    case Builtin::BI__builtin_log2f16:
    case Builtin::BI__builtin_log2l:
    case Builtin::BI__builtin_log2f128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::log2,
                                   Intrinsic::experimental_constrained_log2));

    case Builtin::BInearbyint:
    case Builtin::BInearbyintf:
    case Builtin::BInearbyintl:
    case Builtin::BI__builtin_nearbyint:
    case Builtin::BI__builtin_nearbyintf:
    case Builtin::BI__builtin_nearbyintl:
    case Builtin::BI__builtin_nearbyintf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                Intrinsic::nearbyint,
                                Intrinsic::experimental_constrained_nearbyint));

    case Builtin::BIpow:
    case Builtin::BIpowf:
    case Builtin::BIpowl:
    case Builtin::BI__builtin_pow:
    case Builtin::BI__builtin_powf:
    case Builtin::BI__builtin_powf16:
    case Builtin::BI__builtin_powl:
    case Builtin::BI__builtin_powf128:
      return RValue::get(emitBinaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::pow,
                                   Intrinsic::experimental_constrained_pow));

    case Builtin::BIrint:
    case Builtin::BIrintf:
    case Builtin::BIrintl:
    case Builtin::BI__builtin_rint:
    case Builtin::BI__builtin_rintf:
    case Builtin::BI__builtin_rintf16:
    case Builtin::BI__builtin_rintl:
    case Builtin::BI__builtin_rintf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::rint,
                                   Intrinsic::experimental_constrained_rint));

    case Builtin::BIround:
    case Builtin::BIroundf:
    case Builtin::BIroundl:
    case Builtin::BI__builtin_round:
    case Builtin::BI__builtin_roundf:
    case Builtin::BI__builtin_roundf16:
    case Builtin::BI__builtin_roundl:
    case Builtin::BI__builtin_roundf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::round,
                                   Intrinsic::experimental_constrained_round));

    case Builtin::BIroundeven:
    case Builtin::BIroundevenf:
    case Builtin::BIroundevenl:
    case Builtin::BI__builtin_roundeven:
    case Builtin::BI__builtin_roundevenf:
    case Builtin::BI__builtin_roundevenf16:
    case Builtin::BI__builtin_roundevenl:
    case Builtin::BI__builtin_roundevenf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::roundeven,
                                   Intrinsic::experimental_constrained_roundeven));

    case Builtin::BIsin:
    case Builtin::BIsinf:
    case Builtin::BIsinl:
    case Builtin::BI__builtin_sin:
    case Builtin::BI__builtin_sinf:
    case Builtin::BI__builtin_sinf16:
    case Builtin::BI__builtin_sinl:
    case Builtin::BI__builtin_sinf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::sin,
                                   Intrinsic::experimental_constrained_sin));

    case Builtin::BIsinh:
    case Builtin::BIsinhf:
    case Builtin::BIsinhl:
    case Builtin::BI__builtin_sinh:
    case Builtin::BI__builtin_sinhf:
    case Builtin::BI__builtin_sinhf16:
    case Builtin::BI__builtin_sinhl:
    case Builtin::BI__builtin_sinhf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::sinh, Intrinsic::experimental_constrained_sinh));

    case Builtin::BIsqrt:
    case Builtin::BIsqrtf:
    case Builtin::BIsqrtl:
    case Builtin::BI__builtin_sqrt:
    case Builtin::BI__builtin_sqrtf:
    case Builtin::BI__builtin_sqrtf16:
    case Builtin::BI__builtin_sqrtl:
    case Builtin::BI__builtin_sqrtf128:
    case Builtin::BI__builtin_elementwise_sqrt: {
      llvm::Value *Call = emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::sqrt, Intrinsic::experimental_constrained_sqrt);
      SetSqrtFPAccuracy(Call);
      return RValue::get(Call);
    }

    case Builtin::BItan:
    case Builtin::BItanf:
    case Builtin::BItanl:
    case Builtin::BI__builtin_tan:
    case Builtin::BI__builtin_tanf:
    case Builtin::BI__builtin_tanf16:
    case Builtin::BI__builtin_tanl:
    case Builtin::BI__builtin_tanf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::tan, Intrinsic::experimental_constrained_tan));

    case Builtin::BItanh:
    case Builtin::BItanhf:
    case Builtin::BItanhl:
    case Builtin::BI__builtin_tanh:
    case Builtin::BI__builtin_tanhf:
    case Builtin::BI__builtin_tanhf16:
    case Builtin::BI__builtin_tanhl:
    case Builtin::BI__builtin_tanhf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::tanh, Intrinsic::experimental_constrained_tanh));

    case Builtin::BItrunc:
    case Builtin::BItruncf:
    case Builtin::BItruncl:
    case Builtin::BI__builtin_trunc:
    case Builtin::BI__builtin_truncf:
    case Builtin::BI__builtin_truncf16:
    case Builtin::BI__builtin_truncl:
    case Builtin::BI__builtin_truncf128:
      return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(*this, E,
                                   Intrinsic::trunc,
                                   Intrinsic::experimental_constrained_trunc));

    case Builtin::BIlround:
    case Builtin::BIlroundf:
    case Builtin::BIlroundl:
    case Builtin::BI__builtin_lround:
    case Builtin::BI__builtin_lroundf:
    case Builtin::BI__builtin_lroundl:
    case Builtin::BI__builtin_lroundf128:
      return RValue::get(emitMaybeConstrainedFPToIntRoundBuiltin(
          *this, E, Intrinsic::lround,
          Intrinsic::experimental_constrained_lround));

    case Builtin::BIllround:
    case Builtin::BIllroundf:
    case Builtin::BIllroundl:
    case Builtin::BI__builtin_llround:
    case Builtin::BI__builtin_llroundf:
    case Builtin::BI__builtin_llroundl:
    case Builtin::BI__builtin_llroundf128:
      return RValue::get(emitMaybeConstrainedFPToIntRoundBuiltin(
          *this, E, Intrinsic::llround,
          Intrinsic::experimental_constrained_llround));

    case Builtin::BIlrint:
    case Builtin::BIlrintf:
    case Builtin::BIlrintl:
    case Builtin::BI__builtin_lrint:
    case Builtin::BI__builtin_lrintf:
    case Builtin::BI__builtin_lrintl:
    case Builtin::BI__builtin_lrintf128:
      return RValue::get(emitMaybeConstrainedFPToIntRoundBuiltin(
          *this, E, Intrinsic::lrint,
          Intrinsic::experimental_constrained_lrint));

    case Builtin::BIllrint:
    case Builtin::BIllrintf:
    case Builtin::BIllrintl:
    case Builtin::BI__builtin_llrint:
    case Builtin::BI__builtin_llrintf:
    case Builtin::BI__builtin_llrintl:
    case Builtin::BI__builtin_llrintf128:
      return RValue::get(emitMaybeConstrainedFPToIntRoundBuiltin(
          *this, E, Intrinsic::llrint,
          Intrinsic::experimental_constrained_llrint));
    case Builtin::BI__builtin_ldexp:
    case Builtin::BI__builtin_ldexpf:
    case Builtin::BI__builtin_ldexpl:
    case Builtin::BI__builtin_ldexpf16:
    case Builtin::BI__builtin_ldexpf128: {
      return RValue::get(emitBinaryExpMaybeConstrainedFPBuiltin(
          *this, E, Intrinsic::ldexp,
          Intrinsic::experimental_constrained_ldexp));
    }
    default:
      break;
    }
  }

  // Check NonnullAttribute/NullabilityArg and Alignment.
  auto EmitArgCheck = [&](TypeCheckKind Kind, Address A, const Expr *Arg,
                          unsigned ParmNum) {
    Value *Val = A.emitRawPointer(*this);
    EmitNonNullArgCheck(RValue::get(Val), Arg->getType(), Arg->getExprLoc(), FD,
                        ParmNum);

    if (SanOpts.has(SanitizerKind::Alignment)) {
      SanitizerSet SkippedChecks;
      SkippedChecks.set(SanitizerKind::All);
      SkippedChecks.clear(SanitizerKind::Alignment);
      SourceLocation Loc = Arg->getExprLoc();
      // Strip an implicit cast.
      if (auto *CE = dyn_cast<ImplicitCastExpr>(Arg))
        if (CE->getCastKind() == CK_BitCast)
          Arg = CE->getSubExpr();
      EmitTypeCheck(Kind, Loc, Val, Arg->getType(), A.getAlignment(),
                    SkippedChecks);
    }
  };

  switch (BuiltinIDIfNoAsmLabel) {
  default: break;
  case Builtin::BI__builtin___CFStringMakeConstantString:
  case Builtin::BI__builtin___NSStringMakeConstantString:
    return RValue::get(ConstantEmitter(*this).emitAbstract(E, E->getType()));
  case Builtin::BI__builtin_stdarg_start:
  case Builtin::BI__builtin_va_start:
  case Builtin::BI__va_start:
  case Builtin::BI__builtin_va_end:
    EmitVAStartEnd(BuiltinID == Builtin::BI__va_start
                       ? EmitScalarExpr(E->getArg(0))
                       : EmitVAListRef(E->getArg(0)).emitRawPointer(*this),
                   BuiltinID != Builtin::BI__builtin_va_end);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_va_copy: {
    Value *DstPtr = EmitVAListRef(E->getArg(0)).emitRawPointer(*this);
    Value *SrcPtr = EmitVAListRef(E->getArg(1)).emitRawPointer(*this);
    Builder.CreateCall(CGM.getIntrinsic(Intrinsic::vacopy, {DstPtr->getType()}),
                       {DstPtr, SrcPtr});
    return RValue::get(nullptr);
  }
  case Builtin::BIabs:
  case Builtin::BIlabs:
  case Builtin::BIllabs:
  case Builtin::BI__builtin_abs:
  case Builtin::BI__builtin_labs:
  case Builtin::BI__builtin_llabs: {
    bool SanitizeOverflow = SanOpts.has(SanitizerKind::SignedIntegerOverflow);

    Value *Result;
    switch (getLangOpts().getSignedOverflowBehavior()) {
    case LangOptions::SOB_Defined:
      Result = EmitAbs(*this, EmitScalarExpr(E->getArg(0)), false);
      break;
    case LangOptions::SOB_Undefined:
      if (!SanitizeOverflow) {
        Result = EmitAbs(*this, EmitScalarExpr(E->getArg(0)), true);
        break;
      }
      [[fallthrough]];
    case LangOptions::SOB_Trapping:
      // TODO: Somehow handle the corner case when the address of abs is taken.
      Result = EmitOverflowCheckedAbs(*this, E, SanitizeOverflow);
      break;
    }
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_complex: {
    Value *Real = EmitScalarExpr(E->getArg(0));
    Value *Imag = EmitScalarExpr(E->getArg(1));
    return RValue::getComplex({Real, Imag});
  }
  case Builtin::BI__builtin_conj:
  case Builtin::BI__builtin_conjf:
  case Builtin::BI__builtin_conjl:
  case Builtin::BIconj:
  case Builtin::BIconjf:
  case Builtin::BIconjl: {
    ComplexPairTy ComplexVal = EmitComplexExpr(E->getArg(0));
    Value *Real = ComplexVal.first;
    Value *Imag = ComplexVal.second;
    Imag = Builder.CreateFNeg(Imag, "neg");
    return RValue::getComplex(std::make_pair(Real, Imag));
  }
  case Builtin::BI__builtin_creal:
  case Builtin::BI__builtin_crealf:
  case Builtin::BI__builtin_creall:
  case Builtin::BIcreal:
  case Builtin::BIcrealf:
  case Builtin::BIcreall: {
    ComplexPairTy ComplexVal = EmitComplexExpr(E->getArg(0));
    return RValue::get(ComplexVal.first);
  }

  case Builtin::BI__builtin_preserve_access_index: {
    // Only enabled preserved access index region when debuginfo
    // is available as debuginfo is needed to preserve user-level
    // access pattern.
    if (!getDebugInfo()) {
      CGM.Error(E->getExprLoc(), "using builtin_preserve_access_index() without -g");
      return RValue::get(EmitScalarExpr(E->getArg(0)));
    }

    // Nested builtin_preserve_access_index() not supported
    if (IsInPreservedAIRegion) {
      CGM.Error(E->getExprLoc(), "nested builtin_preserve_access_index() not supported");
      return RValue::get(EmitScalarExpr(E->getArg(0)));
    }

    IsInPreservedAIRegion = true;
    Value *Res = EmitScalarExpr(E->getArg(0));
    IsInPreservedAIRegion = false;
    return RValue::get(Res);
  }

  case Builtin::BI__builtin_cimag:
  case Builtin::BI__builtin_cimagf:
  case Builtin::BI__builtin_cimagl:
  case Builtin::BIcimag:
  case Builtin::BIcimagf:
  case Builtin::BIcimagl: {
    ComplexPairTy ComplexVal = EmitComplexExpr(E->getArg(0));
    return RValue::get(ComplexVal.second);
  }

  case Builtin::BI__builtin_clrsb:
  case Builtin::BI__builtin_clrsbl:
  case Builtin::BI__builtin_clrsbll: {
    // clrsb(x) -> clz(x < 0 ? ~x : x) - 1 or
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Zero = llvm::Constant::getNullValue(ArgType);
    Value *IsNeg = Builder.CreateICmpSLT(ArgValue, Zero, "isneg");
    Value *Inverse = Builder.CreateNot(ArgValue, "not");
    Value *Tmp = Builder.CreateSelect(IsNeg, Inverse, ArgValue);
    Value *Ctlz = Builder.CreateCall(F, {Tmp, Builder.getFalse()});
    Value *Result = Builder.CreateSub(Ctlz, llvm::ConstantInt::get(ArgType, 1));
    Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                   "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_ctzs:
  case Builtin::BI__builtin_ctz:
  case Builtin::BI__builtin_ctzl:
  case Builtin::BI__builtin_ctzll:
  case Builtin::BI__builtin_ctzg: {
    bool HasFallback = BuiltinIDIfNoAsmLabel == Builtin::BI__builtin_ctzg &&
                       E->getNumArgs() > 1;

    Value *ArgValue =
        HasFallback ? EmitScalarExpr(E->getArg(0))
                    : EmitCheckedArgForBuiltin(E->getArg(0), BCK_CTZPassedZero);

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::cttz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *ZeroUndef =
        Builder.getInt1(HasFallback || getTarget().isCLZForZeroUndef());
    Value *Result = Builder.CreateCall(F, {ArgValue, ZeroUndef});
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ false, "cast");
    if (!HasFallback)
      return RValue::get(Result);

    Value *Zero = Constant::getNullValue(ArgType);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, Zero, "iszero");
    Value *FallbackValue = EmitScalarExpr(E->getArg(1));
    Value *ResultOrFallback =
        Builder.CreateSelect(IsZero, FallbackValue, Result, "ctzg");
    return RValue::get(ResultOrFallback);
  }
  case Builtin::BI__builtin_clzs:
  case Builtin::BI__builtin_clz:
  case Builtin::BI__builtin_clzl:
  case Builtin::BI__builtin_clzll:
  case Builtin::BI__builtin_clzg: {
    bool HasFallback = BuiltinIDIfNoAsmLabel == Builtin::BI__builtin_clzg &&
                       E->getNumArgs() > 1;

    Value *ArgValue =
        HasFallback ? EmitScalarExpr(E->getArg(0))
                    : EmitCheckedArgForBuiltin(E->getArg(0), BCK_CLZPassedZero);

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *ZeroUndef =
        Builder.getInt1(HasFallback || getTarget().isCLZForZeroUndef());
    Value *Result = Builder.CreateCall(F, {ArgValue, ZeroUndef});
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ false, "cast");
    if (!HasFallback)
      return RValue::get(Result);

    Value *Zero = Constant::getNullValue(ArgType);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, Zero, "iszero");
    Value *FallbackValue = EmitScalarExpr(E->getArg(1));
    Value *ResultOrFallback =
        Builder.CreateSelect(IsZero, FallbackValue, Result, "clzg");
    return RValue::get(ResultOrFallback);
  }
  case Builtin::BI__builtin_ffs:
  case Builtin::BI__builtin_ffsl:
  case Builtin::BI__builtin_ffsll: {
    // ffs(x) -> x ? cttz(x) + 1 : 0
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::cttz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Tmp =
        Builder.CreateAdd(Builder.CreateCall(F, {ArgValue, Builder.getTrue()}),
                          llvm::ConstantInt::get(ArgType, 1));
    Value *Zero = llvm::Constant::getNullValue(ArgType);
    Value *IsZero = Builder.CreateICmpEQ(ArgValue, Zero, "iszero");
    Value *Result = Builder.CreateSelect(IsZero, Zero, Tmp, "ffs");
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_parity:
  case Builtin::BI__builtin_parityl:
  case Builtin::BI__builtin_parityll: {
    // parity(x) -> ctpop(x) & 1
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Tmp = Builder.CreateCall(F, ArgValue);
    Value *Result = Builder.CreateAnd(Tmp, llvm::ConstantInt::get(ArgType, 1));
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__lzcnt16:
  case Builtin::BI__lzcnt:
  case Builtin::BI__lzcnt64: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateCall(F, {ArgValue, Builder.getFalse()});
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__popcnt16:
  case Builtin::BI__popcnt:
  case Builtin::BI__popcnt64:
  case Builtin::BI__builtin_popcount:
  case Builtin::BI__builtin_popcountl:
  case Builtin::BI__builtin_popcountll:
  case Builtin::BI__builtin_popcountg: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));

    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ArgType);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateCall(F, ArgValue);
    if (Result->getType() != ResultType)
      Result =
          Builder.CreateIntCast(Result, ResultType, /*isSigned*/ false, "cast");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_unpredictable: {
    // Always return the argument of __builtin_unpredictable. LLVM does not
    // handle this builtin. Metadata for this builtin should be added directly
    // to instructions such as branches or switches that use it.
    return RValue::get(EmitScalarExpr(E->getArg(0)));
  }
  case Builtin::BI__builtin_expect: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    llvm::Type *ArgType = ArgValue->getType();

    Value *ExpectedValue = EmitScalarExpr(E->getArg(1));
    // Don't generate llvm.expect on -O0 as the backend won't use it for
    // anything.
    // Note, we still IRGen ExpectedValue because it could have side-effects.
    if (CGM.getCodeGenOpts().OptimizationLevel == 0)
      return RValue::get(ArgValue);

    Function *FnExpect = CGM.getIntrinsic(Intrinsic::expect, ArgType);
    Value *Result =
        Builder.CreateCall(FnExpect, {ArgValue, ExpectedValue}, "expval");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_expect_with_probability: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    llvm::Type *ArgType = ArgValue->getType();

    Value *ExpectedValue = EmitScalarExpr(E->getArg(1));
    llvm::APFloat Probability(0.0);
    const Expr *ProbArg = E->getArg(2);
    bool EvalSucceed = ProbArg->EvaluateAsFloat(Probability, CGM.getContext());
    assert(EvalSucceed && "probability should be able to evaluate as float");
    (void)EvalSucceed;
    bool LoseInfo = false;
    Probability.convert(llvm::APFloat::IEEEdouble(),
                        llvm::RoundingMode::Dynamic, &LoseInfo);
    llvm::Type *Ty = ConvertType(ProbArg->getType());
    Constant *Confidence = ConstantFP::get(Ty, Probability);
    // Don't generate llvm.expect.with.probability on -O0 as the backend
    // won't use it for anything.
    // Note, we still IRGen ExpectedValue because it could have side-effects.
    if (CGM.getCodeGenOpts().OptimizationLevel == 0)
      return RValue::get(ArgValue);

    Function *FnExpect =
        CGM.getIntrinsic(Intrinsic::expect_with_probability, ArgType);
    Value *Result = Builder.CreateCall(
        FnExpect, {ArgValue, ExpectedValue, Confidence}, "expval");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_assume_aligned: {
    const Expr *Ptr = E->getArg(0);
    Value *PtrValue = EmitScalarExpr(Ptr);
    Value *OffsetValue =
      (E->getNumArgs() > 2) ? EmitScalarExpr(E->getArg(2)) : nullptr;

    Value *AlignmentValue = EmitScalarExpr(E->getArg(1));
    ConstantInt *AlignmentCI = cast<ConstantInt>(AlignmentValue);
    if (AlignmentCI->getValue().ugt(llvm::Value::MaximumAlignment))
      AlignmentCI = ConstantInt::get(AlignmentCI->getIntegerType(),
                                     llvm::Value::MaximumAlignment);

    emitAlignmentAssumption(PtrValue, Ptr,
                            /*The expr loc is sufficient.*/ SourceLocation(),
                            AlignmentCI, OffsetValue);
    return RValue::get(PtrValue);
  }
  case Builtin::BI__assume:
  case Builtin::BI__builtin_assume: {
    if (E->getArg(0)->HasSideEffects(getContext()))
      return RValue::get(nullptr);

    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    Function *FnAssume = CGM.getIntrinsic(Intrinsic::assume);
    Builder.CreateCall(FnAssume, ArgValue);
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_assume_separate_storage: {
    const Expr *Arg0 = E->getArg(0);
    const Expr *Arg1 = E->getArg(1);

    Value *Value0 = EmitScalarExpr(Arg0);
    Value *Value1 = EmitScalarExpr(Arg1);

    Value *Values[] = {Value0, Value1};
    OperandBundleDefT<Value *> OBD("separate_storage", Values);
    Builder.CreateAssumption(ConstantInt::getTrue(getLLVMContext()), {OBD});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_allow_runtime_check: {
    StringRef Kind =
        cast<StringLiteral>(E->getArg(0)->IgnoreParenCasts())->getString();
    LLVMContext &Ctx = CGM.getLLVMContext();
    llvm::Value *Allow = Builder.CreateCall(
        CGM.getIntrinsic(llvm::Intrinsic::allow_runtime_check),
        llvm::MetadataAsValue::get(Ctx, llvm::MDString::get(Ctx, Kind)));
    return RValue::get(Allow);
  }
  case Builtin::BI__arithmetic_fence: {
    // Create the builtin call if FastMath is selected, and the target
    // supports the builtin, otherwise just return the argument.
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    llvm::FastMathFlags FMF = Builder.getFastMathFlags();
    bool isArithmeticFenceEnabled =
        FMF.allowReassoc() &&
        getContext().getTargetInfo().checkArithmeticFenceSupported();
    QualType ArgType = E->getArg(0)->getType();
    if (ArgType->isComplexType()) {
      if (isArithmeticFenceEnabled) {
        QualType ElementType = ArgType->castAs<ComplexType>()->getElementType();
        ComplexPairTy ComplexVal = EmitComplexExpr(E->getArg(0));
        Value *Real = Builder.CreateArithmeticFence(ComplexVal.first,
                                                    ConvertType(ElementType));
        Value *Imag = Builder.CreateArithmeticFence(ComplexVal.second,
                                                    ConvertType(ElementType));
        return RValue::getComplex(std::make_pair(Real, Imag));
      }
      ComplexPairTy ComplexVal = EmitComplexExpr(E->getArg(0));
      Value *Real = ComplexVal.first;
      Value *Imag = ComplexVal.second;
      return RValue::getComplex(std::make_pair(Real, Imag));
    }
    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    if (isArithmeticFenceEnabled)
      return RValue::get(
          Builder.CreateArithmeticFence(ArgValue, ConvertType(ArgType)));
    return RValue::get(ArgValue);
  }
  case Builtin::BI__builtin_bswap16:
  case Builtin::BI__builtin_bswap32:
  case Builtin::BI__builtin_bswap64:
  case Builtin::BI_byteswap_ushort:
  case Builtin::BI_byteswap_ulong:
  case Builtin::BI_byteswap_uint64: {
    return RValue::get(
        emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::bswap));
  }
  case Builtin::BI__builtin_bitreverse8:
  case Builtin::BI__builtin_bitreverse16:
  case Builtin::BI__builtin_bitreverse32:
  case Builtin::BI__builtin_bitreverse64: {
    return RValue::get(
        emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::bitreverse));
  }
  case Builtin::BI__builtin_rotateleft8:
  case Builtin::BI__builtin_rotateleft16:
  case Builtin::BI__builtin_rotateleft32:
  case Builtin::BI__builtin_rotateleft64:
  case Builtin::BI_rotl8: // Microsoft variants of rotate left
  case Builtin::BI_rotl16:
  case Builtin::BI_rotl:
  case Builtin::BI_lrotl:
  case Builtin::BI_rotl64:
    return emitRotate(E, false);

  case Builtin::BI__builtin_rotateright8:
  case Builtin::BI__builtin_rotateright16:
  case Builtin::BI__builtin_rotateright32:
  case Builtin::BI__builtin_rotateright64:
  case Builtin::BI_rotr8: // Microsoft variants of rotate right
  case Builtin::BI_rotr16:
  case Builtin::BI_rotr:
  case Builtin::BI_lrotr:
  case Builtin::BI_rotr64:
    return emitRotate(E, true);

  case Builtin::BI__builtin_constant_p: {
    llvm::Type *ResultType = ConvertType(E->getType());

    const Expr *Arg = E->getArg(0);
    QualType ArgType = Arg->getType();
    // FIXME: The allowance for Obj-C pointers and block pointers is historical
    // and likely a mistake.
    if (!ArgType->isIntegralOrEnumerationType() && !ArgType->isFloatingType() &&
        !ArgType->isObjCObjectPointerType() && !ArgType->isBlockPointerType())
      // Per the GCC documentation, only numeric constants are recognized after
      // inlining.
      return RValue::get(ConstantInt::get(ResultType, 0));

    if (Arg->HasSideEffects(getContext()))
      // The argument is unevaluated, so be conservative if it might have
      // side-effects.
      return RValue::get(ConstantInt::get(ResultType, 0));

    Value *ArgValue = EmitScalarExpr(Arg);
    if (ArgType->isObjCObjectPointerType()) {
      // Convert Objective-C objects to id because we cannot distinguish between
      // LLVM types for Obj-C classes as they are opaque.
      ArgType = CGM.getContext().getObjCIdType();
      ArgValue = Builder.CreateBitCast(ArgValue, ConvertType(ArgType));
    }
    Function *F =
        CGM.getIntrinsic(Intrinsic::is_constant, ConvertType(ArgType));
    Value *Result = Builder.CreateCall(F, ArgValue);
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/false);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_dynamic_object_size:
  case Builtin::BI__builtin_object_size: {
    unsigned Type =
        E->getArg(1)->EvaluateKnownConstInt(getContext()).getZExtValue();
    auto *ResType = cast<llvm::IntegerType>(ConvertType(E->getType()));

    // We pass this builtin onto the optimizer so that it can figure out the
    // object size in more complex cases.
    bool IsDynamic = BuiltinID == Builtin::BI__builtin_dynamic_object_size;
    return RValue::get(emitBuiltinObjectSize(E->getArg(0), Type, ResType,
                                             /*EmittedE=*/nullptr, IsDynamic));
  }
  case Builtin::BI__builtin_prefetch: {
    Value *Locality, *RW, *Address = EmitScalarExpr(E->getArg(0));
    // FIXME: Technically these constants should of type 'int', yes?
    RW = (E->getNumArgs() > 1) ? EmitScalarExpr(E->getArg(1)) :
      llvm::ConstantInt::get(Int32Ty, 0);
    Locality = (E->getNumArgs() > 2) ? EmitScalarExpr(E->getArg(2)) :
      llvm::ConstantInt::get(Int32Ty, 3);
    Value *Data = llvm::ConstantInt::get(Int32Ty, 1);
    Function *F = CGM.getIntrinsic(Intrinsic::prefetch, Address->getType());
    Builder.CreateCall(F, {Address, RW, Locality, Data});
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_readcyclecounter: {
    Function *F = CGM.getIntrinsic(Intrinsic::readcyclecounter);
    return RValue::get(Builder.CreateCall(F));
  }
  case Builtin::BI__builtin_readsteadycounter: {
    Function *F = CGM.getIntrinsic(Intrinsic::readsteadycounter);
    return RValue::get(Builder.CreateCall(F));
  }
  case Builtin::BI__builtin___clear_cache: {
    Value *Begin = EmitScalarExpr(E->getArg(0));
    Value *End = EmitScalarExpr(E->getArg(1));
    Function *F = CGM.getIntrinsic(Intrinsic::clear_cache);
    return RValue::get(Builder.CreateCall(F, {Begin, End}));
  }
  case Builtin::BI__builtin_trap:
    EmitTrapCall(Intrinsic::trap);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_verbose_trap: {
    llvm::DILocation *TrapLocation = Builder.getCurrentDebugLocation();
    if (getDebugInfo()) {
      TrapLocation = getDebugInfo()->CreateTrapFailureMessageFor(
          TrapLocation, *E->getArg(0)->tryEvaluateString(getContext()),
          *E->getArg(1)->tryEvaluateString(getContext()));
    }
    ApplyDebugLocation ApplyTrapDI(*this, TrapLocation);
    // Currently no attempt is made to prevent traps from being merged.
    EmitTrapCall(Intrinsic::trap);
    return RValue::get(nullptr);
  }
  case Builtin::BI__debugbreak:
    EmitTrapCall(Intrinsic::debugtrap);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_unreachable: {
    EmitUnreachable(E->getExprLoc());

    // We do need to preserve an insertion point.
    EmitBlock(createBasicBlock("unreachable.cont"));

    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_powi:
  case Builtin::BI__builtin_powif:
  case Builtin::BI__builtin_powil: {
    llvm::Value *Src0 = EmitScalarExpr(E->getArg(0));
    llvm::Value *Src1 = EmitScalarExpr(E->getArg(1));

    if (Builder.getIsFPConstrained()) {
      // FIXME: llvm.powi has 2 mangling types,
      // llvm.experimental.constrained.powi has one.
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
      Function *F = CGM.getIntrinsic(Intrinsic::experimental_constrained_powi,
                                     Src0->getType());
      return RValue::get(Builder.CreateConstrainedFPCall(F, { Src0, Src1 }));
    }

    Function *F = CGM.getIntrinsic(Intrinsic::powi,
                                   { Src0->getType(), Src1->getType() });
    return RValue::get(Builder.CreateCall(F, { Src0, Src1 }));
  }
  case Builtin::BI__builtin_frexpl: {
    // Linux PPC will not be adding additional PPCDoubleDouble support.
    // WIP to switch default to IEEE long double. Will emit libcall for
    // frexpl instead of legalizing this type in the BE.
    if (&getTarget().getLongDoubleFormat() == &llvm::APFloat::PPCDoubleDouble())
      break;
    [[fallthrough]];
  }
  case Builtin::BI__builtin_frexp:
  case Builtin::BI__builtin_frexpf:
  case Builtin::BI__builtin_frexpf128:
  case Builtin::BI__builtin_frexpf16:
    return RValue::get(emitFrexpBuiltin(*this, E, Intrinsic::frexp));
  case Builtin::BI__builtin_isgreater:
  case Builtin::BI__builtin_isgreaterequal:
  case Builtin::BI__builtin_isless:
  case Builtin::BI__builtin_islessequal:
  case Builtin::BI__builtin_islessgreater:
  case Builtin::BI__builtin_isunordered: {
    // Ordered comparisons: we know the arguments to these are matching scalar
    // floating point values.
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));

    switch (BuiltinID) {
    default: llvm_unreachable("Unknown ordered comparison");
    case Builtin::BI__builtin_isgreater:
      LHS = Builder.CreateFCmpOGT(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isgreaterequal:
      LHS = Builder.CreateFCmpOGE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isless:
      LHS = Builder.CreateFCmpOLT(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_islessequal:
      LHS = Builder.CreateFCmpOLE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_islessgreater:
      LHS = Builder.CreateFCmpONE(LHS, RHS, "cmp");
      break;
    case Builtin::BI__builtin_isunordered:
      LHS = Builder.CreateFCmpUNO(LHS, RHS, "cmp");
      break;
    }
    // ZExt bool to int type.
    return RValue::get(Builder.CreateZExt(LHS, ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_isnan: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *V = EmitScalarExpr(E->getArg(0));
    if (Value *Result = tryUseTestFPKind(*this, BuiltinID, V))
      return RValue::get(Result);
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcNan),
                           ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_issignaling: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *V = EmitScalarExpr(E->getArg(0));
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcSNan),
                           ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_isinf: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *V = EmitScalarExpr(E->getArg(0));
    if (Value *Result = tryUseTestFPKind(*this, BuiltinID, V))
      return RValue::get(Result);
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcInf),
                           ConvertType(E->getType())));
  }

  case Builtin::BIfinite:
  case Builtin::BI__finite:
  case Builtin::BIfinitef:
  case Builtin::BI__finitef:
  case Builtin::BIfinitel:
  case Builtin::BI__finitel:
  case Builtin::BI__builtin_isfinite: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *V = EmitScalarExpr(E->getArg(0));
    if (Value *Result = tryUseTestFPKind(*this, BuiltinID, V))
      return RValue::get(Result);
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcFinite),
                           ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_isnormal: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *V = EmitScalarExpr(E->getArg(0));
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcNormal),
                           ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_issubnormal: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *V = EmitScalarExpr(E->getArg(0));
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcSubnormal),
                           ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_iszero: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *V = EmitScalarExpr(E->getArg(0));
    return RValue::get(
        Builder.CreateZExt(Builder.createIsFPClass(V, FPClassTest::fcZero),
                           ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_isfpclass: {
    Expr::EvalResult Result;
    if (!E->getArg(1)->EvaluateAsInt(Result, CGM.getContext()))
      break;
    uint64_t Test = Result.Val.getInt().getLimitedValue();
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *V = EmitScalarExpr(E->getArg(0));
    return RValue::get(Builder.CreateZExt(Builder.createIsFPClass(V, Test),
                                          ConvertType(E->getType())));
  }

  case Builtin::BI__builtin_nondeterministic_value: {
    llvm::Type *Ty = ConvertType(E->getArg(0)->getType());

    Value *Result = PoisonValue::get(Ty);
    Result = Builder.CreateFreeze(Result);

    return RValue::get(Result);
  }

  case Builtin::BI__builtin_elementwise_abs: {
    Value *Result;
    QualType QT = E->getArg(0)->getType();

    if (auto *VecTy = QT->getAs<VectorType>())
      QT = VecTy->getElementType();
    if (QT->isIntegerType())
      Result = Builder.CreateBinaryIntrinsic(
          llvm::Intrinsic::abs, EmitScalarExpr(E->getArg(0)),
          Builder.getFalse(), nullptr, "elt.abs");
    else
      Result = emitBuiltinWithOneOverloadedType<1>(
          *this, E, llvm::Intrinsic::fabs, "elt.abs");

    return RValue::get(Result);
  }
  case Builtin::BI__builtin_elementwise_acos:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::acos, "elt.acos"));
  case Builtin::BI__builtin_elementwise_asin:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::asin, "elt.asin"));
  case Builtin::BI__builtin_elementwise_atan:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::atan, "elt.atan"));
  case Builtin::BI__builtin_elementwise_ceil:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::ceil, "elt.ceil"));
  case Builtin::BI__builtin_elementwise_exp:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::exp, "elt.exp"));
  case Builtin::BI__builtin_elementwise_exp2:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::exp2, "elt.exp2"));
  case Builtin::BI__builtin_elementwise_log:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::log, "elt.log"));
  case Builtin::BI__builtin_elementwise_log2:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::log2, "elt.log2"));
  case Builtin::BI__builtin_elementwise_log10:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::log10, "elt.log10"));
  case Builtin::BI__builtin_elementwise_pow: {
    return RValue::get(
        emitBuiltinWithOneOverloadedType<2>(*this, E, llvm::Intrinsic::pow));
  }
  case Builtin::BI__builtin_elementwise_bitreverse:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::bitreverse, "elt.bitreverse"));
  case Builtin::BI__builtin_elementwise_cos:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::cos, "elt.cos"));
  case Builtin::BI__builtin_elementwise_cosh:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::cosh, "elt.cosh"));
  case Builtin::BI__builtin_elementwise_floor:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::floor, "elt.floor"));
  case Builtin::BI__builtin_elementwise_roundeven:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::roundeven, "elt.roundeven"));
  case Builtin::BI__builtin_elementwise_round:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::round, "elt.round"));
  case Builtin::BI__builtin_elementwise_rint:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::rint, "elt.rint"));
  case Builtin::BI__builtin_elementwise_nearbyint:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::nearbyint, "elt.nearbyint"));
  case Builtin::BI__builtin_elementwise_sin:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::sin, "elt.sin"));
  case Builtin::BI__builtin_elementwise_sinh:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::sinh, "elt.sinh"));
  case Builtin::BI__builtin_elementwise_tan:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::tan, "elt.tan"));
  case Builtin::BI__builtin_elementwise_tanh:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::tanh, "elt.tanh"));
  case Builtin::BI__builtin_elementwise_trunc:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::trunc, "elt.trunc"));
  case Builtin::BI__builtin_elementwise_canonicalize:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::canonicalize, "elt.canonicalize"));
  case Builtin::BI__builtin_elementwise_copysign:
    return RValue::get(emitBuiltinWithOneOverloadedType<2>(
        *this, E, llvm::Intrinsic::copysign));
  case Builtin::BI__builtin_elementwise_fma:
    return RValue::get(
        emitBuiltinWithOneOverloadedType<3>(*this, E, llvm::Intrinsic::fma));
  case Builtin::BI__builtin_elementwise_add_sat:
  case Builtin::BI__builtin_elementwise_sub_sat: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Result;
    assert(Op0->getType()->isIntOrIntVectorTy() && "integer type expected");
    QualType Ty = E->getArg(0)->getType();
    if (auto *VecTy = Ty->getAs<VectorType>())
      Ty = VecTy->getElementType();
    bool IsSigned = Ty->isSignedIntegerType();
    unsigned Opc;
    if (BuiltinIDIfNoAsmLabel == Builtin::BI__builtin_elementwise_add_sat)
      Opc = IsSigned ? llvm::Intrinsic::sadd_sat : llvm::Intrinsic::uadd_sat;
    else
      Opc = IsSigned ? llvm::Intrinsic::ssub_sat : llvm::Intrinsic::usub_sat;
    Result = Builder.CreateBinaryIntrinsic(Opc, Op0, Op1, nullptr, "elt.sat");
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_elementwise_max: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Result;
    if (Op0->getType()->isIntOrIntVectorTy()) {
      QualType Ty = E->getArg(0)->getType();
      if (auto *VecTy = Ty->getAs<VectorType>())
        Ty = VecTy->getElementType();
      Result = Builder.CreateBinaryIntrinsic(Ty->isSignedIntegerType()
                                                 ? llvm::Intrinsic::smax
                                                 : llvm::Intrinsic::umax,
                                             Op0, Op1, nullptr, "elt.max");
    } else
      Result = Builder.CreateMaxNum(Op0, Op1, "elt.max");
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_elementwise_min: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Result;
    if (Op0->getType()->isIntOrIntVectorTy()) {
      QualType Ty = E->getArg(0)->getType();
      if (auto *VecTy = Ty->getAs<VectorType>())
        Ty = VecTy->getElementType();
      Result = Builder.CreateBinaryIntrinsic(Ty->isSignedIntegerType()
                                                 ? llvm::Intrinsic::smin
                                                 : llvm::Intrinsic::umin,
                                             Op0, Op1, nullptr, "elt.min");
    } else
      Result = Builder.CreateMinNum(Op0, Op1, "elt.min");
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_reduce_max: {
    auto GetIntrinsicID = [this](QualType QT) {
      if (auto *VecTy = QT->getAs<VectorType>())
        QT = VecTy->getElementType();
      else if (QT->isSizelessVectorType())
        QT = QT->getSizelessVectorEltType(CGM.getContext());

      if (QT->isSignedIntegerType())
        return llvm::Intrinsic::vector_reduce_smax;
      if (QT->isUnsignedIntegerType())
        return llvm::Intrinsic::vector_reduce_umax;
      assert(QT->isFloatingType() && "must have a float here");
      return llvm::Intrinsic::vector_reduce_fmax;
    };
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, GetIntrinsicID(E->getArg(0)->getType()), "rdx.min"));
  }

  case Builtin::BI__builtin_reduce_min: {
    auto GetIntrinsicID = [this](QualType QT) {
      if (auto *VecTy = QT->getAs<VectorType>())
        QT = VecTy->getElementType();
      else if (QT->isSizelessVectorType())
        QT = QT->getSizelessVectorEltType(CGM.getContext());

      if (QT->isSignedIntegerType())
        return llvm::Intrinsic::vector_reduce_smin;
      if (QT->isUnsignedIntegerType())
        return llvm::Intrinsic::vector_reduce_umin;
      assert(QT->isFloatingType() && "must have a float here");
      return llvm::Intrinsic::vector_reduce_fmin;
    };

    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, GetIntrinsicID(E->getArg(0)->getType()), "rdx.min"));
  }

  case Builtin::BI__builtin_reduce_add:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::vector_reduce_add, "rdx.add"));
  case Builtin::BI__builtin_reduce_mul:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::vector_reduce_mul, "rdx.mul"));
  case Builtin::BI__builtin_reduce_xor:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::vector_reduce_xor, "rdx.xor"));
  case Builtin::BI__builtin_reduce_or:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::vector_reduce_or, "rdx.or"));
  case Builtin::BI__builtin_reduce_and:
    return RValue::get(emitBuiltinWithOneOverloadedType<1>(
        *this, E, llvm::Intrinsic::vector_reduce_and, "rdx.and"));

  case Builtin::BI__builtin_matrix_transpose: {
    auto *MatrixTy = E->getArg(0)->getType()->castAs<ConstantMatrixType>();
    Value *MatValue = EmitScalarExpr(E->getArg(0));
    MatrixBuilder MB(Builder);
    Value *Result = MB.CreateMatrixTranspose(MatValue, MatrixTy->getNumRows(),
                                             MatrixTy->getNumColumns());
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_matrix_column_major_load: {
    MatrixBuilder MB(Builder);
    // Emit everything that isn't dependent on the first parameter type
    Value *Stride = EmitScalarExpr(E->getArg(3));
    const auto *ResultTy = E->getType()->getAs<ConstantMatrixType>();
    auto *PtrTy = E->getArg(0)->getType()->getAs<PointerType>();
    assert(PtrTy && "arg0 must be of pointer type");
    bool IsVolatile = PtrTy->getPointeeType().isVolatileQualified();

    Address Src = EmitPointerWithAlignment(E->getArg(0));
    EmitNonNullArgCheck(RValue::get(Src.emitRawPointer(*this)),
                        E->getArg(0)->getType(), E->getArg(0)->getExprLoc(), FD,
                        0);
    Value *Result = MB.CreateColumnMajorLoad(
        Src.getElementType(), Src.emitRawPointer(*this),
        Align(Src.getAlignment().getQuantity()), Stride, IsVolatile,
        ResultTy->getNumRows(), ResultTy->getNumColumns(), "matrix");
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_matrix_column_major_store: {
    MatrixBuilder MB(Builder);
    Value *Matrix = EmitScalarExpr(E->getArg(0));
    Address Dst = EmitPointerWithAlignment(E->getArg(1));
    Value *Stride = EmitScalarExpr(E->getArg(2));

    const auto *MatrixTy = E->getArg(0)->getType()->getAs<ConstantMatrixType>();
    auto *PtrTy = E->getArg(1)->getType()->getAs<PointerType>();
    assert(PtrTy && "arg1 must be of pointer type");
    bool IsVolatile = PtrTy->getPointeeType().isVolatileQualified();

    EmitNonNullArgCheck(RValue::get(Dst.emitRawPointer(*this)),
                        E->getArg(1)->getType(), E->getArg(1)->getExprLoc(), FD,
                        0);
    Value *Result = MB.CreateColumnMajorStore(
        Matrix, Dst.emitRawPointer(*this),
        Align(Dst.getAlignment().getQuantity()), Stride, IsVolatile,
        MatrixTy->getNumRows(), MatrixTy->getNumColumns());
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_isinf_sign: {
    // isinf_sign(x) -> fabs(x) == infinity ? (signbit(x) ? -1 : 1) : 0
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    // FIXME: for strictfp/IEEE-754 we need to not trap on SNaN here.
    Value *Arg = EmitScalarExpr(E->getArg(0));
    Value *AbsArg = EmitFAbs(*this, Arg);
    Value *IsInf = Builder.CreateFCmpOEQ(
        AbsArg, ConstantFP::getInfinity(Arg->getType()), "isinf");
    Value *IsNeg = EmitSignBit(*this, Arg);

    llvm::Type *IntTy = ConvertType(E->getType());
    Value *Zero = Constant::getNullValue(IntTy);
    Value *One = ConstantInt::get(IntTy, 1);
    Value *NegativeOne = ConstantInt::get(IntTy, -1);
    Value *SignResult = Builder.CreateSelect(IsNeg, NegativeOne, One);
    Value *Result = Builder.CreateSelect(IsInf, SignResult, Zero);
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_flt_rounds: {
    Function *F = CGM.getIntrinsic(Intrinsic::get_rounding);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateCall(F);
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return RValue::get(Result);
  }

  case Builtin::BI__builtin_set_flt_rounds: {
    Function *F = CGM.getIntrinsic(Intrinsic::set_rounding);

    Value *V = EmitScalarExpr(E->getArg(0));
    Builder.CreateCall(F, V);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_fpclassify: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    // FIXME: for strictfp/IEEE-754 we need to not trap on SNaN here.
    Value *V = EmitScalarExpr(E->getArg(5));
    llvm::Type *Ty = ConvertType(E->getArg(5)->getType());

    // Create Result
    BasicBlock *Begin = Builder.GetInsertBlock();
    BasicBlock *End = createBasicBlock("fpclassify_end", this->CurFn);
    Builder.SetInsertPoint(End);
    PHINode *Result =
      Builder.CreatePHI(ConvertType(E->getArg(0)->getType()), 4,
                        "fpclassify_result");

    // if (V==0) return FP_ZERO
    Builder.SetInsertPoint(Begin);
    Value *IsZero = Builder.CreateFCmpOEQ(V, Constant::getNullValue(Ty),
                                          "iszero");
    Value *ZeroLiteral = EmitScalarExpr(E->getArg(4));
    BasicBlock *NotZero = createBasicBlock("fpclassify_not_zero", this->CurFn);
    Builder.CreateCondBr(IsZero, End, NotZero);
    Result->addIncoming(ZeroLiteral, Begin);

    // if (V != V) return FP_NAN
    Builder.SetInsertPoint(NotZero);
    Value *IsNan = Builder.CreateFCmpUNO(V, V, "cmp");
    Value *NanLiteral = EmitScalarExpr(E->getArg(0));
    BasicBlock *NotNan = createBasicBlock("fpclassify_not_nan", this->CurFn);
    Builder.CreateCondBr(IsNan, End, NotNan);
    Result->addIncoming(NanLiteral, NotZero);

    // if (fabs(V) == infinity) return FP_INFINITY
    Builder.SetInsertPoint(NotNan);
    Value *VAbs = EmitFAbs(*this, V);
    Value *IsInf =
      Builder.CreateFCmpOEQ(VAbs, ConstantFP::getInfinity(V->getType()),
                            "isinf");
    Value *InfLiteral = EmitScalarExpr(E->getArg(1));
    BasicBlock *NotInf = createBasicBlock("fpclassify_not_inf", this->CurFn);
    Builder.CreateCondBr(IsInf, End, NotInf);
    Result->addIncoming(InfLiteral, NotNan);

    // if (fabs(V) >= MIN_NORMAL) return FP_NORMAL else FP_SUBNORMAL
    Builder.SetInsertPoint(NotInf);
    APFloat Smallest = APFloat::getSmallestNormalized(
        getContext().getFloatTypeSemantics(E->getArg(5)->getType()));
    Value *IsNormal =
      Builder.CreateFCmpUGE(VAbs, ConstantFP::get(V->getContext(), Smallest),
                            "isnormal");
    Value *NormalResult =
      Builder.CreateSelect(IsNormal, EmitScalarExpr(E->getArg(2)),
                           EmitScalarExpr(E->getArg(3)));
    Builder.CreateBr(End);
    Result->addIncoming(NormalResult, NotInf);

    // return Result
    Builder.SetInsertPoint(End);
    return RValue::get(Result);
  }

  // An alloca will always return a pointer to the alloca (stack) address
  // space. This address space need not be the same as the AST / Language
  // default (e.g. in C / C++ auto vars are in the generic address space). At
  // the AST level this is handled within CreateTempAlloca et al., but for the
  // builtin / dynamic alloca we have to handle it here. We use an explicit cast
  // instead of passing an AS to CreateAlloca so as to not inhibit optimisation.
  case Builtin::BIalloca:
  case Builtin::BI_alloca:
  case Builtin::BI__builtin_alloca_uninitialized:
  case Builtin::BI__builtin_alloca: {
    Value *Size = EmitScalarExpr(E->getArg(0));
    const TargetInfo &TI = getContext().getTargetInfo();
    // The alignment of the alloca should correspond to __BIGGEST_ALIGNMENT__.
    const Align SuitableAlignmentInBytes =
        CGM.getContext()
            .toCharUnitsFromBits(TI.getSuitableAlign())
            .getAsAlign();
    AllocaInst *AI = Builder.CreateAlloca(Builder.getInt8Ty(), Size);
    AI->setAlignment(SuitableAlignmentInBytes);
    if (BuiltinID != Builtin::BI__builtin_alloca_uninitialized)
      initializeAlloca(*this, AI, Size, SuitableAlignmentInBytes);
    LangAS AAS = getASTAllocaAddressSpace();
    LangAS EAS = E->getType()->getPointeeType().getAddressSpace();
    if (AAS != EAS) {
      llvm::Type *Ty = CGM.getTypes().ConvertType(E->getType());
      return RValue::get(getTargetHooks().performAddrSpaceCast(*this, AI, AAS,
                                                               EAS, Ty));
    }
    return RValue::get(AI);
  }

  case Builtin::BI__builtin_alloca_with_align_uninitialized:
  case Builtin::BI__builtin_alloca_with_align: {
    Value *Size = EmitScalarExpr(E->getArg(0));
    Value *AlignmentInBitsValue = EmitScalarExpr(E->getArg(1));
    auto *AlignmentInBitsCI = cast<ConstantInt>(AlignmentInBitsValue);
    unsigned AlignmentInBits = AlignmentInBitsCI->getZExtValue();
    const Align AlignmentInBytes =
        CGM.getContext().toCharUnitsFromBits(AlignmentInBits).getAsAlign();
    AllocaInst *AI = Builder.CreateAlloca(Builder.getInt8Ty(), Size);
    AI->setAlignment(AlignmentInBytes);
    if (BuiltinID != Builtin::BI__builtin_alloca_with_align_uninitialized)
      initializeAlloca(*this, AI, Size, AlignmentInBytes);
    LangAS AAS = getASTAllocaAddressSpace();
    LangAS EAS = E->getType()->getPointeeType().getAddressSpace();
    if (AAS != EAS) {
      llvm::Type *Ty = CGM.getTypes().ConvertType(E->getType());
      return RValue::get(getTargetHooks().performAddrSpaceCast(*this, AI, AAS,
                                                               EAS, Ty));
    }
    return RValue::get(AI);
  }

  case Builtin::BIbzero:
  case Builtin::BI__builtin_bzero: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Value *SizeVal = EmitScalarExpr(E->getArg(1));
    EmitNonNullArgCheck(Dest, E->getArg(0)->getType(),
                        E->getArg(0)->getExprLoc(), FD, 0);
    Builder.CreateMemSet(Dest, Builder.getInt8(0), SizeVal, false);
    return RValue::get(nullptr);
  }

  case Builtin::BIbcopy:
  case Builtin::BI__builtin_bcopy: {
    Address Src = EmitPointerWithAlignment(E->getArg(0));
    Address Dest = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    EmitNonNullArgCheck(RValue::get(Src.emitRawPointer(*this)),
                        E->getArg(0)->getType(), E->getArg(0)->getExprLoc(), FD,
                        0);
    EmitNonNullArgCheck(RValue::get(Dest.emitRawPointer(*this)),
                        E->getArg(1)->getType(), E->getArg(1)->getExprLoc(), FD,
                        0);
    Builder.CreateMemMove(Dest, Src, SizeVal, false);
    return RValue::get(nullptr);
  }

  case Builtin::BImemcpy:
  case Builtin::BI__builtin_memcpy:
  case Builtin::BImempcpy:
  case Builtin::BI__builtin_mempcpy: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    EmitArgCheck(TCK_Store, Dest, E->getArg(0), 0);
    EmitArgCheck(TCK_Load, Src, E->getArg(1), 1);
    Builder.CreateMemCpy(Dest, Src, SizeVal, false);
    if (BuiltinID == Builtin::BImempcpy ||
        BuiltinID == Builtin::BI__builtin_mempcpy)
      return RValue::get(Builder.CreateInBoundsGEP(
          Dest.getElementType(), Dest.emitRawPointer(*this), SizeVal));
    else
      return RValue::get(Dest, *this);
  }

  case Builtin::BI__builtin_memcpy_inline: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    uint64_t Size =
        E->getArg(2)->EvaluateKnownConstInt(getContext()).getZExtValue();
    EmitArgCheck(TCK_Store, Dest, E->getArg(0), 0);
    EmitArgCheck(TCK_Load, Src, E->getArg(1), 1);
    Builder.CreateMemCpyInline(Dest, Src, Size);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_char_memchr:
    BuiltinID = Builtin::BI__builtin_memchr;
    break;

  case Builtin::BI__builtin___memcpy_chk: {
    // fold __builtin_memcpy_chk(x, y, cst1, cst2) to memcpy iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, CGM.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, CGM.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemCpy(Dest, Src, SizeVal, false);
    return RValue::get(Dest, *this);
  }

  case Builtin::BI__builtin_objc_memmove_collectable: {
    Address DestAddr = EmitPointerWithAlignment(E->getArg(0));
    Address SrcAddr = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    CGM.getObjCRuntime().EmitGCMemmoveCollectable(*this,
                                                  DestAddr, SrcAddr, SizeVal);
    return RValue::get(DestAddr, *this);
  }

  case Builtin::BI__builtin___memmove_chk: {
    // fold __builtin_memmove_chk(x, y, cst1, cst2) to memmove iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, CGM.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, CGM.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemMove(Dest, Src, SizeVal, false);
    return RValue::get(Dest, *this);
  }

  case Builtin::BImemmove:
  case Builtin::BI__builtin_memmove: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    EmitArgCheck(TCK_Store, Dest, E->getArg(0), 0);
    EmitArgCheck(TCK_Load, Src, E->getArg(1), 1);
    Builder.CreateMemMove(Dest, Src, SizeVal, false);
    return RValue::get(Dest, *this);
  }
  case Builtin::BImemset:
  case Builtin::BI__builtin_memset: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Value *ByteVal = Builder.CreateTrunc(EmitScalarExpr(E->getArg(1)),
                                         Builder.getInt8Ty());
    Value *SizeVal = EmitScalarExpr(E->getArg(2));
    EmitNonNullArgCheck(Dest, E->getArg(0)->getType(),
                        E->getArg(0)->getExprLoc(), FD, 0);
    Builder.CreateMemSet(Dest, ByteVal, SizeVal, false);
    return RValue::get(Dest, *this);
  }
  case Builtin::BI__builtin_memset_inline: {
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Value *ByteVal =
        Builder.CreateTrunc(EmitScalarExpr(E->getArg(1)), Builder.getInt8Ty());
    uint64_t Size =
        E->getArg(2)->EvaluateKnownConstInt(getContext()).getZExtValue();
    EmitNonNullArgCheck(RValue::get(Dest.emitRawPointer(*this)),
                        E->getArg(0)->getType(), E->getArg(0)->getExprLoc(), FD,
                        0);
    Builder.CreateMemSetInline(Dest, ByteVal, Size);
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin___memset_chk: {
    // fold __builtin_memset_chk(x, y, cst1, cst2) to memset iff cst1<=cst2.
    Expr::EvalResult SizeResult, DstSizeResult;
    if (!E->getArg(2)->EvaluateAsInt(SizeResult, CGM.getContext()) ||
        !E->getArg(3)->EvaluateAsInt(DstSizeResult, CGM.getContext()))
      break;
    llvm::APSInt Size = SizeResult.Val.getInt();
    llvm::APSInt DstSize = DstSizeResult.Val.getInt();
    if (Size.ugt(DstSize))
      break;
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    Value *ByteVal = Builder.CreateTrunc(EmitScalarExpr(E->getArg(1)),
                                         Builder.getInt8Ty());
    Value *SizeVal = llvm::ConstantInt::get(Builder.getContext(), Size);
    Builder.CreateMemSet(Dest, ByteVal, SizeVal, false);
    return RValue::get(Dest, *this);
  }
  case Builtin::BI__builtin_wmemchr: {
    // The MSVC runtime library does not provide a definition of wmemchr, so we
    // need an inline implementation.
    if (!getTarget().getTriple().isOSMSVCRT())
      break;

    llvm::Type *WCharTy = ConvertType(getContext().WCharTy);
    Value *Str = EmitScalarExpr(E->getArg(0));
    Value *Chr = EmitScalarExpr(E->getArg(1));
    Value *Size = EmitScalarExpr(E->getArg(2));

    BasicBlock *Entry = Builder.GetInsertBlock();
    BasicBlock *CmpEq = createBasicBlock("wmemchr.eq");
    BasicBlock *Next = createBasicBlock("wmemchr.next");
    BasicBlock *Exit = createBasicBlock("wmemchr.exit");
    Value *SizeEq0 = Builder.CreateICmpEQ(Size, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(SizeEq0, Exit, CmpEq);

    EmitBlock(CmpEq);
    PHINode *StrPhi = Builder.CreatePHI(Str->getType(), 2);
    StrPhi->addIncoming(Str, Entry);
    PHINode *SizePhi = Builder.CreatePHI(SizeTy, 2);
    SizePhi->addIncoming(Size, Entry);
    CharUnits WCharAlign =
        getContext().getTypeAlignInChars(getContext().WCharTy);
    Value *StrCh = Builder.CreateAlignedLoad(WCharTy, StrPhi, WCharAlign);
    Value *FoundChr = Builder.CreateConstInBoundsGEP1_32(WCharTy, StrPhi, 0);
    Value *StrEqChr = Builder.CreateICmpEQ(StrCh, Chr);
    Builder.CreateCondBr(StrEqChr, Exit, Next);

    EmitBlock(Next);
    Value *NextStr = Builder.CreateConstInBoundsGEP1_32(WCharTy, StrPhi, 1);
    Value *NextSize = Builder.CreateSub(SizePhi, ConstantInt::get(SizeTy, 1));
    Value *NextSizeEq0 =
        Builder.CreateICmpEQ(NextSize, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(NextSizeEq0, Exit, CmpEq);
    StrPhi->addIncoming(NextStr, Next);
    SizePhi->addIncoming(NextSize, Next);

    EmitBlock(Exit);
    PHINode *Ret = Builder.CreatePHI(Str->getType(), 3);
    Ret->addIncoming(llvm::Constant::getNullValue(Str->getType()), Entry);
    Ret->addIncoming(llvm::Constant::getNullValue(Str->getType()), Next);
    Ret->addIncoming(FoundChr, CmpEq);
    return RValue::get(Ret);
  }
  case Builtin::BI__builtin_wmemcmp: {
    // The MSVC runtime library does not provide a definition of wmemcmp, so we
    // need an inline implementation.
    if (!getTarget().getTriple().isOSMSVCRT())
      break;

    llvm::Type *WCharTy = ConvertType(getContext().WCharTy);

    Value *Dst = EmitScalarExpr(E->getArg(0));
    Value *Src = EmitScalarExpr(E->getArg(1));
    Value *Size = EmitScalarExpr(E->getArg(2));

    BasicBlock *Entry = Builder.GetInsertBlock();
    BasicBlock *CmpGT = createBasicBlock("wmemcmp.gt");
    BasicBlock *CmpLT = createBasicBlock("wmemcmp.lt");
    BasicBlock *Next = createBasicBlock("wmemcmp.next");
    BasicBlock *Exit = createBasicBlock("wmemcmp.exit");
    Value *SizeEq0 = Builder.CreateICmpEQ(Size, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(SizeEq0, Exit, CmpGT);

    EmitBlock(CmpGT);
    PHINode *DstPhi = Builder.CreatePHI(Dst->getType(), 2);
    DstPhi->addIncoming(Dst, Entry);
    PHINode *SrcPhi = Builder.CreatePHI(Src->getType(), 2);
    SrcPhi->addIncoming(Src, Entry);
    PHINode *SizePhi = Builder.CreatePHI(SizeTy, 2);
    SizePhi->addIncoming(Size, Entry);
    CharUnits WCharAlign =
        getContext().getTypeAlignInChars(getContext().WCharTy);
    Value *DstCh = Builder.CreateAlignedLoad(WCharTy, DstPhi, WCharAlign);
    Value *SrcCh = Builder.CreateAlignedLoad(WCharTy, SrcPhi, WCharAlign);
    Value *DstGtSrc = Builder.CreateICmpUGT(DstCh, SrcCh);
    Builder.CreateCondBr(DstGtSrc, Exit, CmpLT);

    EmitBlock(CmpLT);
    Value *DstLtSrc = Builder.CreateICmpULT(DstCh, SrcCh);
    Builder.CreateCondBr(DstLtSrc, Exit, Next);

    EmitBlock(Next);
    Value *NextDst = Builder.CreateConstInBoundsGEP1_32(WCharTy, DstPhi, 1);
    Value *NextSrc = Builder.CreateConstInBoundsGEP1_32(WCharTy, SrcPhi, 1);
    Value *NextSize = Builder.CreateSub(SizePhi, ConstantInt::get(SizeTy, 1));
    Value *NextSizeEq0 =
        Builder.CreateICmpEQ(NextSize, ConstantInt::get(SizeTy, 0));
    Builder.CreateCondBr(NextSizeEq0, Exit, CmpGT);
    DstPhi->addIncoming(NextDst, Next);
    SrcPhi->addIncoming(NextSrc, Next);
    SizePhi->addIncoming(NextSize, Next);

    EmitBlock(Exit);
    PHINode *Ret = Builder.CreatePHI(IntTy, 4);
    Ret->addIncoming(ConstantInt::get(IntTy, 0), Entry);
    Ret->addIncoming(ConstantInt::get(IntTy, 1), CmpGT);
    Ret->addIncoming(ConstantInt::get(IntTy, -1), CmpLT);
    Ret->addIncoming(ConstantInt::get(IntTy, 0), Next);
    return RValue::get(Ret);
  }
  case Builtin::BI__builtin_dwarf_cfa: {
    // The offset in bytes from the first argument to the CFA.
    //
    // Why on earth is this in the frontend?  Is there any reason at
    // all that the backend can't reasonably determine this while
    // lowering llvm.eh.dwarf.cfa()?
    //
    // TODO: If there's a satisfactory reason, add a target hook for
    // this instead of hard-coding 0, which is correct for most targets.
    int32_t Offset = 0;

    Function *F = CGM.getIntrinsic(Intrinsic::eh_dwarf_cfa);
    return RValue::get(Builder.CreateCall(F,
                                      llvm::ConstantInt::get(Int32Ty, Offset)));
  }
  case Builtin::BI__builtin_return_address: {
    Value *Depth = ConstantEmitter(*this).emitAbstract(E->getArg(0),
                                                   getContext().UnsignedIntTy);
    Function *F = CGM.getIntrinsic(Intrinsic::returnaddress);
    return RValue::get(Builder.CreateCall(F, Depth));
  }
  case Builtin::BI_ReturnAddress: {
    Function *F = CGM.getIntrinsic(Intrinsic::returnaddress);
    return RValue::get(Builder.CreateCall(F, Builder.getInt32(0)));
  }
  case Builtin::BI__builtin_frame_address: {
    Value *Depth = ConstantEmitter(*this).emitAbstract(E->getArg(0),
                                                   getContext().UnsignedIntTy);
    Function *F = CGM.getIntrinsic(Intrinsic::frameaddress, AllocaInt8PtrTy);
    return RValue::get(Builder.CreateCall(F, Depth));
  }
  case Builtin::BI__builtin_extract_return_addr: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Result = getTargetHooks().decodeReturnAddress(*this, Address);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_frob_return_addr: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Result = getTargetHooks().encodeReturnAddress(*this, Address);
    return RValue::get(Result);
  }
  case Builtin::BI__builtin_dwarf_sp_column: {
    llvm::IntegerType *Ty
      = cast<llvm::IntegerType>(ConvertType(E->getType()));
    int Column = getTargetHooks().getDwarfEHStackPointer(CGM);
    if (Column == -1) {
      CGM.ErrorUnsupported(E, "__builtin_dwarf_sp_column");
      return RValue::get(llvm::UndefValue::get(Ty));
    }
    return RValue::get(llvm::ConstantInt::get(Ty, Column, true));
  }
  case Builtin::BI__builtin_init_dwarf_reg_size_table: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    if (getTargetHooks().initDwarfEHRegSizeTable(*this, Address))
      CGM.ErrorUnsupported(E, "__builtin_init_dwarf_reg_size_table");
    return RValue::get(llvm::UndefValue::get(ConvertType(E->getType())));
  }
  case Builtin::BI__builtin_eh_return: {
    Value *Int = EmitScalarExpr(E->getArg(0));
    Value *Ptr = EmitScalarExpr(E->getArg(1));

    llvm::IntegerType *IntTy = cast<llvm::IntegerType>(Int->getType());
    assert((IntTy->getBitWidth() == 32 || IntTy->getBitWidth() == 64) &&
           "LLVM's __builtin_eh_return only supports 32- and 64-bit variants");
    Function *F =
        CGM.getIntrinsic(IntTy->getBitWidth() == 32 ? Intrinsic::eh_return_i32
                                                    : Intrinsic::eh_return_i64);
    Builder.CreateCall(F, {Int, Ptr});
    Builder.CreateUnreachable();

    // We do need to preserve an insertion point.
    EmitBlock(createBasicBlock("builtin_eh_return.cont"));

    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_unwind_init: {
    Function *F = CGM.getIntrinsic(Intrinsic::eh_unwind_init);
    Builder.CreateCall(F);
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_extend_pointer: {
    // Extends a pointer to the size of an _Unwind_Word, which is
    // uint64_t on all platforms.  Generally this gets poked into a
    // register and eventually used as an address, so if the
    // addressing registers are wider than pointers and the platform
    // doesn't implicitly ignore high-order bits when doing
    // addressing, we need to make sure we zext / sext based on
    // the platform's expectations.
    //
    // See: http://gcc.gnu.org/ml/gcc-bugs/2002-02/msg00237.html

    // Cast the pointer to intptr_t.
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    Value *Result = Builder.CreatePtrToInt(Ptr, IntPtrTy, "extend.cast");

    // If that's 64 bits, we're done.
    if (IntPtrTy->getBitWidth() == 64)
      return RValue::get(Result);

    // Otherwise, ask the codegen data what to do.
    if (getTargetHooks().extendPointerWithSExt())
      return RValue::get(Builder.CreateSExt(Result, Int64Ty, "extend.sext"));
    else
      return RValue::get(Builder.CreateZExt(Result, Int64Ty, "extend.zext"));
  }
  case Builtin::BI__builtin_setjmp: {
    // Buffer is a void**.
    Address Buf = EmitPointerWithAlignment(E->getArg(0));

    // Store the frame pointer to the setjmp buffer.
    Value *FrameAddr = Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::frameaddress, AllocaInt8PtrTy),
        ConstantInt::get(Int32Ty, 0));
    Builder.CreateStore(FrameAddr, Buf);

    // Store the stack pointer to the setjmp buffer.
    Value *StackAddr = Builder.CreateStackSave();
    assert(Buf.emitRawPointer(*this)->getType() == StackAddr->getType());

    Address StackSaveSlot = Builder.CreateConstInBoundsGEP(Buf, 2);
    Builder.CreateStore(StackAddr, StackSaveSlot);

    // Call LLVM's EH setjmp, which is lightweight.
    Function *F = CGM.getIntrinsic(Intrinsic::eh_sjlj_setjmp);
    return RValue::get(Builder.CreateCall(F, Buf.emitRawPointer(*this)));
  }
  case Builtin::BI__builtin_longjmp: {
    Value *Buf = EmitScalarExpr(E->getArg(0));

    // Call LLVM's EH longjmp, which is lightweight.
    Builder.CreateCall(CGM.getIntrinsic(Intrinsic::eh_sjlj_longjmp), Buf);

    // longjmp doesn't return; mark this as unreachable.
    Builder.CreateUnreachable();

    // We do need to preserve an insertion point.
    EmitBlock(createBasicBlock("longjmp.cont"));

    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_launder: {
    const Expr *Arg = E->getArg(0);
    QualType ArgTy = Arg->getType()->getPointeeType();
    Value *Ptr = EmitScalarExpr(Arg);
    if (TypeRequiresBuiltinLaunder(CGM, ArgTy))
      Ptr = Builder.CreateLaunderInvariantGroup(Ptr);

    return RValue::get(Ptr);
  }
  case Builtin::BI__sync_fetch_and_add:
  case Builtin::BI__sync_fetch_and_sub:
  case Builtin::BI__sync_fetch_and_or:
  case Builtin::BI__sync_fetch_and_and:
  case Builtin::BI__sync_fetch_and_xor:
  case Builtin::BI__sync_fetch_and_nand:
  case Builtin::BI__sync_add_and_fetch:
  case Builtin::BI__sync_sub_and_fetch:
  case Builtin::BI__sync_and_and_fetch:
  case Builtin::BI__sync_or_and_fetch:
  case Builtin::BI__sync_xor_and_fetch:
  case Builtin::BI__sync_nand_and_fetch:
  case Builtin::BI__sync_val_compare_and_swap:
  case Builtin::BI__sync_bool_compare_and_swap:
  case Builtin::BI__sync_lock_test_and_set:
  case Builtin::BI__sync_lock_release:
  case Builtin::BI__sync_swap:
    llvm_unreachable("Shouldn't make it through sema");
  case Builtin::BI__sync_fetch_and_add_1:
  case Builtin::BI__sync_fetch_and_add_2:
  case Builtin::BI__sync_fetch_and_add_4:
  case Builtin::BI__sync_fetch_and_add_8:
  case Builtin::BI__sync_fetch_and_add_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Add, E);
  case Builtin::BI__sync_fetch_and_sub_1:
  case Builtin::BI__sync_fetch_and_sub_2:
  case Builtin::BI__sync_fetch_and_sub_4:
  case Builtin::BI__sync_fetch_and_sub_8:
  case Builtin::BI__sync_fetch_and_sub_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Sub, E);
  case Builtin::BI__sync_fetch_and_or_1:
  case Builtin::BI__sync_fetch_and_or_2:
  case Builtin::BI__sync_fetch_and_or_4:
  case Builtin::BI__sync_fetch_and_or_8:
  case Builtin::BI__sync_fetch_and_or_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Or, E);
  case Builtin::BI__sync_fetch_and_and_1:
  case Builtin::BI__sync_fetch_and_and_2:
  case Builtin::BI__sync_fetch_and_and_4:
  case Builtin::BI__sync_fetch_and_and_8:
  case Builtin::BI__sync_fetch_and_and_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::And, E);
  case Builtin::BI__sync_fetch_and_xor_1:
  case Builtin::BI__sync_fetch_and_xor_2:
  case Builtin::BI__sync_fetch_and_xor_4:
  case Builtin::BI__sync_fetch_and_xor_8:
  case Builtin::BI__sync_fetch_and_xor_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Xor, E);
  case Builtin::BI__sync_fetch_and_nand_1:
  case Builtin::BI__sync_fetch_and_nand_2:
  case Builtin::BI__sync_fetch_and_nand_4:
  case Builtin::BI__sync_fetch_and_nand_8:
  case Builtin::BI__sync_fetch_and_nand_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Nand, E);

  // Clang extensions: not overloaded yet.
  case Builtin::BI__sync_fetch_and_min:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Min, E);
  case Builtin::BI__sync_fetch_and_max:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Max, E);
  case Builtin::BI__sync_fetch_and_umin:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::UMin, E);
  case Builtin::BI__sync_fetch_and_umax:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::UMax, E);

  case Builtin::BI__sync_add_and_fetch_1:
  case Builtin::BI__sync_add_and_fetch_2:
  case Builtin::BI__sync_add_and_fetch_4:
  case Builtin::BI__sync_add_and_fetch_8:
  case Builtin::BI__sync_add_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Add, E,
                                llvm::Instruction::Add);
  case Builtin::BI__sync_sub_and_fetch_1:
  case Builtin::BI__sync_sub_and_fetch_2:
  case Builtin::BI__sync_sub_and_fetch_4:
  case Builtin::BI__sync_sub_and_fetch_8:
  case Builtin::BI__sync_sub_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Sub, E,
                                llvm::Instruction::Sub);
  case Builtin::BI__sync_and_and_fetch_1:
  case Builtin::BI__sync_and_and_fetch_2:
  case Builtin::BI__sync_and_and_fetch_4:
  case Builtin::BI__sync_and_and_fetch_8:
  case Builtin::BI__sync_and_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::And, E,
                                llvm::Instruction::And);
  case Builtin::BI__sync_or_and_fetch_1:
  case Builtin::BI__sync_or_and_fetch_2:
  case Builtin::BI__sync_or_and_fetch_4:
  case Builtin::BI__sync_or_and_fetch_8:
  case Builtin::BI__sync_or_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Or, E,
                                llvm::Instruction::Or);
  case Builtin::BI__sync_xor_and_fetch_1:
  case Builtin::BI__sync_xor_and_fetch_2:
  case Builtin::BI__sync_xor_and_fetch_4:
  case Builtin::BI__sync_xor_and_fetch_8:
  case Builtin::BI__sync_xor_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Xor, E,
                                llvm::Instruction::Xor);
  case Builtin::BI__sync_nand_and_fetch_1:
  case Builtin::BI__sync_nand_and_fetch_2:
  case Builtin::BI__sync_nand_and_fetch_4:
  case Builtin::BI__sync_nand_and_fetch_8:
  case Builtin::BI__sync_nand_and_fetch_16:
    return EmitBinaryAtomicPost(*this, llvm::AtomicRMWInst::Nand, E,
                                llvm::Instruction::And, true);

  case Builtin::BI__sync_val_compare_and_swap_1:
  case Builtin::BI__sync_val_compare_and_swap_2:
  case Builtin::BI__sync_val_compare_and_swap_4:
  case Builtin::BI__sync_val_compare_and_swap_8:
  case Builtin::BI__sync_val_compare_and_swap_16:
    return RValue::get(MakeAtomicCmpXchgValue(*this, E, false));

  case Builtin::BI__sync_bool_compare_and_swap_1:
  case Builtin::BI__sync_bool_compare_and_swap_2:
  case Builtin::BI__sync_bool_compare_and_swap_4:
  case Builtin::BI__sync_bool_compare_and_swap_8:
  case Builtin::BI__sync_bool_compare_and_swap_16:
    return RValue::get(MakeAtomicCmpXchgValue(*this, E, true));

  case Builtin::BI__sync_swap_1:
  case Builtin::BI__sync_swap_2:
  case Builtin::BI__sync_swap_4:
  case Builtin::BI__sync_swap_8:
  case Builtin::BI__sync_swap_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Xchg, E);

  case Builtin::BI__sync_lock_test_and_set_1:
  case Builtin::BI__sync_lock_test_and_set_2:
  case Builtin::BI__sync_lock_test_and_set_4:
  case Builtin::BI__sync_lock_test_and_set_8:
  case Builtin::BI__sync_lock_test_and_set_16:
    return EmitBinaryAtomic(*this, llvm::AtomicRMWInst::Xchg, E);

  case Builtin::BI__sync_lock_release_1:
  case Builtin::BI__sync_lock_release_2:
  case Builtin::BI__sync_lock_release_4:
  case Builtin::BI__sync_lock_release_8:
  case Builtin::BI__sync_lock_release_16: {
    Address Ptr = CheckAtomicAlignment(*this, E);
    QualType ElTy = E->getArg(0)->getType()->getPointeeType();

    llvm::Type *ITy = llvm::IntegerType::get(getLLVMContext(),
                                             getContext().getTypeSize(ElTy));
    llvm::StoreInst *Store =
        Builder.CreateStore(llvm::Constant::getNullValue(ITy), Ptr);
    Store->setAtomic(llvm::AtomicOrdering::Release);
    return RValue::get(nullptr);
  }

  case Builtin::BI__sync_synchronize: {
    // We assume this is supposed to correspond to a C++0x-style
    // sequentially-consistent fence (i.e. this is only usable for
    // synchronization, not device I/O or anything like that). This intrinsic
    // is really badly designed in the sense that in theory, there isn't
    // any way to safely use it... but in practice, it mostly works
    // to use it with non-atomic loads and stores to get acquire/release
    // semantics.
    Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_nontemporal_load:
    return RValue::get(EmitNontemporalLoad(*this, E));
  case Builtin::BI__builtin_nontemporal_store:
    return RValue::get(EmitNontemporalStore(*this, E));
  case Builtin::BI__c11_atomic_is_lock_free:
  case Builtin::BI__atomic_is_lock_free: {
    // Call "bool __atomic_is_lock_free(size_t size, void *ptr)". For the
    // __c11 builtin, ptr is 0 (indicating a properly-aligned object), since
    // _Atomic(T) is always properly-aligned.
    const char *LibCallName = "__atomic_is_lock_free";
    CallArgList Args;
    Args.add(RValue::get(EmitScalarExpr(E->getArg(0))),
             getContext().getSizeType());
    if (BuiltinID == Builtin::BI__atomic_is_lock_free)
      Args.add(RValue::get(EmitScalarExpr(E->getArg(1))),
               getContext().VoidPtrTy);
    else
      Args.add(RValue::get(llvm::Constant::getNullValue(VoidPtrTy)),
               getContext().VoidPtrTy);
    const CGFunctionInfo &FuncInfo =
        CGM.getTypes().arrangeBuiltinFunctionCall(E->getType(), Args);
    llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(FuncInfo);
    llvm::FunctionCallee Func = CGM.CreateRuntimeFunction(FTy, LibCallName);
    return EmitCall(FuncInfo, CGCallee::forDirect(Func),
                    ReturnValueSlot(), Args);
  }

  case Builtin::BI__atomic_test_and_set: {
    // Look at the argument type to determine whether this is a volatile
    // operation. The parameter type is always volatile.
    QualType PtrTy = E->getArg(0)->IgnoreImpCasts()->getType();
    bool Volatile =
        PtrTy->castAs<PointerType>()->getPointeeType().isVolatileQualified();

    Address Ptr =
        EmitPointerWithAlignment(E->getArg(0)).withElementType(Int8Ty);

    Value *NewVal = Builder.getInt8(1);
    Value *Order = EmitScalarExpr(E->getArg(1));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      AtomicRMWInst *Result = nullptr;
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Monotonic);
        break;
      case 1: // memory_order_consume
      case 2: // memory_order_acquire
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Acquire);
        break;
      case 3: // memory_order_release
        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::Release);
        break;
      case 4: // memory_order_acq_rel

        Result = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
                                         llvm::AtomicOrdering::AcquireRelease);
        break;
      case 5: // memory_order_seq_cst
        Result = Builder.CreateAtomicRMW(
            llvm::AtomicRMWInst::Xchg, Ptr, NewVal,
            llvm::AtomicOrdering::SequentiallyConsistent);
        break;
      }
      Result->setVolatile(Volatile);
      return RValue::get(Builder.CreateIsNotNull(Result, "tobool"));
    }

    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    llvm::BasicBlock *BBs[5] = {
      createBasicBlock("monotonic", CurFn),
      createBasicBlock("acquire", CurFn),
      createBasicBlock("release", CurFn),
      createBasicBlock("acqrel", CurFn),
      createBasicBlock("seqcst", CurFn)
    };
    llvm::AtomicOrdering Orders[5] = {
        llvm::AtomicOrdering::Monotonic, llvm::AtomicOrdering::Acquire,
        llvm::AtomicOrdering::Release, llvm::AtomicOrdering::AcquireRelease,
        llvm::AtomicOrdering::SequentiallyConsistent};

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, BBs[0]);

    Builder.SetInsertPoint(ContBB);
    PHINode *Result = Builder.CreatePHI(Int8Ty, 5, "was_set");

    for (unsigned i = 0; i < 5; ++i) {
      Builder.SetInsertPoint(BBs[i]);
      AtomicRMWInst *RMW = Builder.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg,
                                                   Ptr, NewVal, Orders[i]);
      RMW->setVolatile(Volatile);
      Result->addIncoming(RMW, BBs[i]);
      Builder.CreateBr(ContBB);
    }

    SI->addCase(Builder.getInt32(0), BBs[0]);
    SI->addCase(Builder.getInt32(1), BBs[1]);
    SI->addCase(Builder.getInt32(2), BBs[1]);
    SI->addCase(Builder.getInt32(3), BBs[2]);
    SI->addCase(Builder.getInt32(4), BBs[3]);
    SI->addCase(Builder.getInt32(5), BBs[4]);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(Builder.CreateIsNotNull(Result, "tobool"));
  }

  case Builtin::BI__atomic_clear: {
    QualType PtrTy = E->getArg(0)->IgnoreImpCasts()->getType();
    bool Volatile =
        PtrTy->castAs<PointerType>()->getPointeeType().isVolatileQualified();

    Address Ptr = EmitPointerWithAlignment(E->getArg(0));
    Ptr = Ptr.withElementType(Int8Ty);
    Value *NewVal = Builder.getInt8(0);
    Value *Order = EmitScalarExpr(E->getArg(1));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      StoreInst *Store = Builder.CreateStore(NewVal, Ptr, Volatile);
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        Store->setOrdering(llvm::AtomicOrdering::Monotonic);
        break;
      case 3:  // memory_order_release
        Store->setOrdering(llvm::AtomicOrdering::Release);
        break;
      case 5:  // memory_order_seq_cst
        Store->setOrdering(llvm::AtomicOrdering::SequentiallyConsistent);
        break;
      }
      return RValue::get(nullptr);
    }

    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    llvm::BasicBlock *BBs[3] = {
      createBasicBlock("monotonic", CurFn),
      createBasicBlock("release", CurFn),
      createBasicBlock("seqcst", CurFn)
    };
    llvm::AtomicOrdering Orders[3] = {
        llvm::AtomicOrdering::Monotonic, llvm::AtomicOrdering::Release,
        llvm::AtomicOrdering::SequentiallyConsistent};

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, BBs[0]);

    for (unsigned i = 0; i < 3; ++i) {
      Builder.SetInsertPoint(BBs[i]);
      StoreInst *Store = Builder.CreateStore(NewVal, Ptr, Volatile);
      Store->setOrdering(Orders[i]);
      Builder.CreateBr(ContBB);
    }

    SI->addCase(Builder.getInt32(0), BBs[0]);
    SI->addCase(Builder.getInt32(3), BBs[1]);
    SI->addCase(Builder.getInt32(5), BBs[2]);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(nullptr);
  }

  case Builtin::BI__atomic_thread_fence:
  case Builtin::BI__atomic_signal_fence:
  case Builtin::BI__c11_atomic_thread_fence:
  case Builtin::BI__c11_atomic_signal_fence: {
    llvm::SyncScope::ID SSID;
    if (BuiltinID == Builtin::BI__atomic_signal_fence ||
        BuiltinID == Builtin::BI__c11_atomic_signal_fence)
      SSID = llvm::SyncScope::SingleThread;
    else
      SSID = llvm::SyncScope::System;
    Value *Order = EmitScalarExpr(E->getArg(0));
    if (isa<llvm::ConstantInt>(Order)) {
      int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
      switch (ord) {
      case 0:  // memory_order_relaxed
      default: // invalid order
        break;
      case 1:  // memory_order_consume
      case 2:  // memory_order_acquire
        Builder.CreateFence(llvm::AtomicOrdering::Acquire, SSID);
        break;
      case 3:  // memory_order_release
        Builder.CreateFence(llvm::AtomicOrdering::Release, SSID);
        break;
      case 4:  // memory_order_acq_rel
        Builder.CreateFence(llvm::AtomicOrdering::AcquireRelease, SSID);
        break;
      case 5:  // memory_order_seq_cst
        Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent, SSID);
        break;
      }
      return RValue::get(nullptr);
    }

    llvm::BasicBlock *AcquireBB, *ReleaseBB, *AcqRelBB, *SeqCstBB;
    AcquireBB = createBasicBlock("acquire", CurFn);
    ReleaseBB = createBasicBlock("release", CurFn);
    AcqRelBB = createBasicBlock("acqrel", CurFn);
    SeqCstBB = createBasicBlock("seqcst", CurFn);
    llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

    Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
    llvm::SwitchInst *SI = Builder.CreateSwitch(Order, ContBB);

    Builder.SetInsertPoint(AcquireBB);
    Builder.CreateFence(llvm::AtomicOrdering::Acquire, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(1), AcquireBB);
    SI->addCase(Builder.getInt32(2), AcquireBB);

    Builder.SetInsertPoint(ReleaseBB);
    Builder.CreateFence(llvm::AtomicOrdering::Release, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(3), ReleaseBB);

    Builder.SetInsertPoint(AcqRelBB);
    Builder.CreateFence(llvm::AtomicOrdering::AcquireRelease, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(4), AcqRelBB);

    Builder.SetInsertPoint(SeqCstBB);
    Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent, SSID);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32(5), SeqCstBB);

    Builder.SetInsertPoint(ContBB);
    return RValue::get(nullptr);
  }

  case Builtin::BI__builtin_signbit:
  case Builtin::BI__builtin_signbitf:
  case Builtin::BI__builtin_signbitl: {
    return RValue::get(
        Builder.CreateZExt(EmitSignBit(*this, EmitScalarExpr(E->getArg(0))),
                           ConvertType(E->getType())));
  }
  case Builtin::BI__warn_memset_zero_len:
    return RValue::getIgnored();
  case Builtin::BI__annotation: {
    // Re-encode each wide string to UTF8 and make an MDString.
    SmallVector<Metadata *, 1> Strings;
    for (const Expr *Arg : E->arguments()) {
      const auto *Str = cast<StringLiteral>(Arg->IgnoreParenCasts());
      assert(Str->getCharByteWidth() == 2);
      StringRef WideBytes = Str->getBytes();
      std::string StrUtf8;
      if (!convertUTF16ToUTF8String(
              ArrayRef(WideBytes.data(), WideBytes.size()), StrUtf8)) {
        CGM.ErrorUnsupported(E, "non-UTF16 __annotation argument");
        continue;
      }
      Strings.push_back(llvm::MDString::get(getLLVMContext(), StrUtf8));
    }

    // Build and MDTuple of MDStrings and emit the intrinsic call.
    llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::codeview_annotation, {});
    MDTuple *StrTuple = MDTuple::get(getLLVMContext(), Strings);
    Builder.CreateCall(F, MetadataAsValue::get(getLLVMContext(), StrTuple));
    return RValue::getIgnored();
  }
  case Builtin::BI__builtin_annotation: {
    llvm::Value *AnnVal = EmitScalarExpr(E->getArg(0));
    llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::annotation,
                         {AnnVal->getType(), CGM.ConstGlobalsPtrTy});

    // Get the annotation string, go through casts. Sema requires this to be a
    // non-wide string literal, potentially casted, so the cast<> is safe.
    const Expr *AnnotationStrExpr = E->getArg(1)->IgnoreParenCasts();
    StringRef Str = cast<StringLiteral>(AnnotationStrExpr)->getString();
    return RValue::get(
        EmitAnnotationCall(F, AnnVal, Str, E->getExprLoc(), nullptr));
  }
  case Builtin::BI__builtin_addcb:
  case Builtin::BI__builtin_addcs:
  case Builtin::BI__builtin_addc:
  case Builtin::BI__builtin_addcl:
  case Builtin::BI__builtin_addcll:
  case Builtin::BI__builtin_subcb:
  case Builtin::BI__builtin_subcs:
  case Builtin::BI__builtin_subc:
  case Builtin::BI__builtin_subcl:
  case Builtin::BI__builtin_subcll: {

    // We translate all of these builtins from expressions of the form:
    //   int x = ..., y = ..., carryin = ..., carryout, result;
    //   result = __builtin_addc(x, y, carryin, &carryout);
    //
    // to LLVM IR of the form:
    //
    //   %tmp1 = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %x, i32 %y)
    //   %tmpsum1 = extractvalue {i32, i1} %tmp1, 0
    //   %carry1 = extractvalue {i32, i1} %tmp1, 1
    //   %tmp2 = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %tmpsum1,
    //                                                       i32 %carryin)
    //   %result = extractvalue {i32, i1} %tmp2, 0
    //   %carry2 = extractvalue {i32, i1} %tmp2, 1
    //   %tmp3 = or i1 %carry1, %carry2
    //   %tmp4 = zext i1 %tmp3 to i32
    //   store i32 %tmp4, i32* %carryout

    // Scalarize our inputs.
    llvm::Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Value *Y = EmitScalarExpr(E->getArg(1));
    llvm::Value *Carryin = EmitScalarExpr(E->getArg(2));
    Address CarryOutPtr = EmitPointerWithAlignment(E->getArg(3));

    // Decide if we are lowering to a uadd.with.overflow or usub.with.overflow.
    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default: llvm_unreachable("Unknown multiprecision builtin id.");
    case Builtin::BI__builtin_addcb:
    case Builtin::BI__builtin_addcs:
    case Builtin::BI__builtin_addc:
    case Builtin::BI__builtin_addcl:
    case Builtin::BI__builtin_addcll:
      IntrinsicId = llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_subcb:
    case Builtin::BI__builtin_subcs:
    case Builtin::BI__builtin_subc:
    case Builtin::BI__builtin_subcl:
    case Builtin::BI__builtin_subcll:
      IntrinsicId = llvm::Intrinsic::usub_with_overflow;
      break;
    }

    // Construct our resulting LLVM IR expression.
    llvm::Value *Carry1;
    llvm::Value *Sum1 = EmitOverflowIntrinsic(*this, IntrinsicId,
                                              X, Y, Carry1);
    llvm::Value *Carry2;
    llvm::Value *Sum2 = EmitOverflowIntrinsic(*this, IntrinsicId,
                                              Sum1, Carryin, Carry2);
    llvm::Value *CarryOut = Builder.CreateZExt(Builder.CreateOr(Carry1, Carry2),
                                               X->getType());
    Builder.CreateStore(CarryOut, CarryOutPtr);
    return RValue::get(Sum2);
  }

  case Builtin::BI__builtin_add_overflow:
  case Builtin::BI__builtin_sub_overflow:
  case Builtin::BI__builtin_mul_overflow: {
    const clang::Expr *LeftArg = E->getArg(0);
    const clang::Expr *RightArg = E->getArg(1);
    const clang::Expr *ResultArg = E->getArg(2);

    clang::QualType ResultQTy =
        ResultArg->getType()->castAs<PointerType>()->getPointeeType();

    WidthAndSignedness LeftInfo =
        getIntegerWidthAndSignedness(CGM.getContext(), LeftArg->getType());
    WidthAndSignedness RightInfo =
        getIntegerWidthAndSignedness(CGM.getContext(), RightArg->getType());
    WidthAndSignedness ResultInfo =
        getIntegerWidthAndSignedness(CGM.getContext(), ResultQTy);

    // Handle mixed-sign multiplication as a special case, because adding
    // runtime or backend support for our generic irgen would be too expensive.
    if (isSpecialMixedSignMultiply(BuiltinID, LeftInfo, RightInfo, ResultInfo))
      return EmitCheckedMixedSignMultiply(*this, LeftArg, LeftInfo, RightArg,
                                          RightInfo, ResultArg, ResultQTy,
                                          ResultInfo);

    if (isSpecialUnsignedMultiplySignedResult(BuiltinID, LeftInfo, RightInfo,
                                              ResultInfo))
      return EmitCheckedUnsignedMultiplySignedResult(
          *this, LeftArg, LeftInfo, RightArg, RightInfo, ResultArg, ResultQTy,
          ResultInfo);

    WidthAndSignedness EncompassingInfo =
        EncompassingIntegerType({LeftInfo, RightInfo, ResultInfo});

    llvm::Type *EncompassingLLVMTy =
        llvm::IntegerType::get(CGM.getLLVMContext(), EncompassingInfo.Width);

    llvm::Type *ResultLLVMTy = CGM.getTypes().ConvertType(ResultQTy);

    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default:
      llvm_unreachable("Unknown overflow builtin id.");
    case Builtin::BI__builtin_add_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::sadd_with_overflow
                        : llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_sub_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::ssub_with_overflow
                        : llvm::Intrinsic::usub_with_overflow;
      break;
    case Builtin::BI__builtin_mul_overflow:
      IntrinsicId = EncompassingInfo.Signed
                        ? llvm::Intrinsic::smul_with_overflow
                        : llvm::Intrinsic::umul_with_overflow;
      break;
    }

    llvm::Value *Left = EmitScalarExpr(LeftArg);
    llvm::Value *Right = EmitScalarExpr(RightArg);
    Address ResultPtr = EmitPointerWithAlignment(ResultArg);

    // Extend each operand to the encompassing type.
    Left = Builder.CreateIntCast(Left, EncompassingLLVMTy, LeftInfo.Signed);
    Right = Builder.CreateIntCast(Right, EncompassingLLVMTy, RightInfo.Signed);

    // Perform the operation on the extended values.
    llvm::Value *Overflow, *Result;
    Result = EmitOverflowIntrinsic(*this, IntrinsicId, Left, Right, Overflow);

    if (EncompassingInfo.Width > ResultInfo.Width) {
      // The encompassing type is wider than the result type, so we need to
      // truncate it.
      llvm::Value *ResultTrunc = Builder.CreateTrunc(Result, ResultLLVMTy);

      // To see if the truncation caused an overflow, we will extend
      // the result and then compare it to the original result.
      llvm::Value *ResultTruncExt = Builder.CreateIntCast(
          ResultTrunc, EncompassingLLVMTy, ResultInfo.Signed);
      llvm::Value *TruncationOverflow =
          Builder.CreateICmpNE(Result, ResultTruncExt);

      Overflow = Builder.CreateOr(Overflow, TruncationOverflow);
      Result = ResultTrunc;
    }

    // Finally, store the result using the pointer.
    bool isVolatile =
      ResultArg->getType()->getPointeeType().isVolatileQualified();
    Builder.CreateStore(EmitToMemory(Result, ResultQTy), ResultPtr, isVolatile);

    return RValue::get(Overflow);
  }

  case Builtin::BI__builtin_uadd_overflow:
  case Builtin::BI__builtin_uaddl_overflow:
  case Builtin::BI__builtin_uaddll_overflow:
  case Builtin::BI__builtin_usub_overflow:
  case Builtin::BI__builtin_usubl_overflow:
  case Builtin::BI__builtin_usubll_overflow:
  case Builtin::BI__builtin_umul_overflow:
  case Builtin::BI__builtin_umull_overflow:
  case Builtin::BI__builtin_umulll_overflow:
  case Builtin::BI__builtin_sadd_overflow:
  case Builtin::BI__builtin_saddl_overflow:
  case Builtin::BI__builtin_saddll_overflow:
  case Builtin::BI__builtin_ssub_overflow:
  case Builtin::BI__builtin_ssubl_overflow:
  case Builtin::BI__builtin_ssubll_overflow:
  case Builtin::BI__builtin_smul_overflow:
  case Builtin::BI__builtin_smull_overflow:
  case Builtin::BI__builtin_smulll_overflow: {

    // We translate all of these builtins directly to the relevant llvm IR node.

    // Scalarize our inputs.
    llvm::Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Value *Y = EmitScalarExpr(E->getArg(1));
    Address SumOutPtr = EmitPointerWithAlignment(E->getArg(2));

    // Decide which of the overflow intrinsics we are lowering to:
    llvm::Intrinsic::ID IntrinsicId;
    switch (BuiltinID) {
    default: llvm_unreachable("Unknown overflow builtin id.");
    case Builtin::BI__builtin_uadd_overflow:
    case Builtin::BI__builtin_uaddl_overflow:
    case Builtin::BI__builtin_uaddll_overflow:
      IntrinsicId = llvm::Intrinsic::uadd_with_overflow;
      break;
    case Builtin::BI__builtin_usub_overflow:
    case Builtin::BI__builtin_usubl_overflow:
    case Builtin::BI__builtin_usubll_overflow:
      IntrinsicId = llvm::Intrinsic::usub_with_overflow;
      break;
    case Builtin::BI__builtin_umul_overflow:
    case Builtin::BI__builtin_umull_overflow:
    case Builtin::BI__builtin_umulll_overflow:
      IntrinsicId = llvm::Intrinsic::umul_with_overflow;
      break;
    case Builtin::BI__builtin_sadd_overflow:
    case Builtin::BI__builtin_saddl_overflow:
    case Builtin::BI__builtin_saddll_overflow:
      IntrinsicId = llvm::Intrinsic::sadd_with_overflow;
      break;
    case Builtin::BI__builtin_ssub_overflow:
    case Builtin::BI__builtin_ssubl_overflow:
    case Builtin::BI__builtin_ssubll_overflow:
      IntrinsicId = llvm::Intrinsic::ssub_with_overflow;
      break;
    case Builtin::BI__builtin_smul_overflow:
    case Builtin::BI__builtin_smull_overflow:
    case Builtin::BI__builtin_smulll_overflow:
      IntrinsicId = llvm::Intrinsic::smul_with_overflow;
      break;
    }


    llvm::Value *Carry;
    llvm::Value *Sum = EmitOverflowIntrinsic(*this, IntrinsicId, X, Y, Carry);
    Builder.CreateStore(Sum, SumOutPtr);

    return RValue::get(Carry);
  }
  case Builtin::BIaddressof:
  case Builtin::BI__addressof:
  case Builtin::BI__builtin_addressof:
    return RValue::get(EmitLValue(E->getArg(0)).getPointer(*this));
  case Builtin::BI__builtin_function_start:
    return RValue::get(CGM.GetFunctionStart(
        E->getArg(0)->getAsBuiltinConstantDeclRef(CGM.getContext())));
  case Builtin::BI__builtin_operator_new:
    return EmitBuiltinNewDeleteCall(
        E->getCallee()->getType()->castAs<FunctionProtoType>(), E, false);
  case Builtin::BI__builtin_operator_delete:
    EmitBuiltinNewDeleteCall(
        E->getCallee()->getType()->castAs<FunctionProtoType>(), E, true);
    return RValue::get(nullptr);

  case Builtin::BI__builtin_is_aligned:
    return EmitBuiltinIsAligned(E);
  case Builtin::BI__builtin_align_up:
    return EmitBuiltinAlignTo(E, true);
  case Builtin::BI__builtin_align_down:
    return EmitBuiltinAlignTo(E, false);

  case Builtin::BI__noop:
    // __noop always evaluates to an integer literal zero.
    return RValue::get(ConstantInt::get(IntTy, 0));
  case Builtin::BI__builtin_call_with_static_chain: {
    const CallExpr *Call = cast<CallExpr>(E->getArg(0));
    const Expr *Chain = E->getArg(1);
    return EmitCall(Call->getCallee()->getType(),
                    EmitCallee(Call->getCallee()), Call, ReturnValue,
                    EmitScalarExpr(Chain));
  }
  case Builtin::BI_InterlockedExchange8:
  case Builtin::BI_InterlockedExchange16:
  case Builtin::BI_InterlockedExchange:
  case Builtin::BI_InterlockedExchangePointer:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchange, E));
  case Builtin::BI_InterlockedCompareExchangePointer:
  case Builtin::BI_InterlockedCompareExchangePointer_nf: {
    llvm::Type *RTy;
    llvm::IntegerType *IntType = IntegerType::get(
        getLLVMContext(), getContext().getTypeSize(E->getType()));

    Address DestAddr = CheckAtomicAlignment(*this, E);

    llvm::Value *Exchange = EmitScalarExpr(E->getArg(1));
    RTy = Exchange->getType();
    Exchange = Builder.CreatePtrToInt(Exchange, IntType);

    llvm::Value *Comparand =
      Builder.CreatePtrToInt(EmitScalarExpr(E->getArg(2)), IntType);

    auto Ordering =
      BuiltinID == Builtin::BI_InterlockedCompareExchangePointer_nf ?
      AtomicOrdering::Monotonic : AtomicOrdering::SequentiallyConsistent;

    auto Result = Builder.CreateAtomicCmpXchg(DestAddr, Comparand, Exchange,
                                              Ordering, Ordering);
    Result->setVolatile(true);

    return RValue::get(Builder.CreateIntToPtr(Builder.CreateExtractValue(Result,
                                                                         0),
                                              RTy));
  }
  case Builtin::BI_InterlockedCompareExchange8:
  case Builtin::BI_InterlockedCompareExchange16:
  case Builtin::BI_InterlockedCompareExchange:
  case Builtin::BI_InterlockedCompareExchange64:
    return RValue::get(EmitAtomicCmpXchgForMSIntrin(*this, E));
  case Builtin::BI_InterlockedIncrement16:
  case Builtin::BI_InterlockedIncrement:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedIncrement, E));
  case Builtin::BI_InterlockedDecrement16:
  case Builtin::BI_InterlockedDecrement:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedDecrement, E));
  case Builtin::BI_InterlockedAnd8:
  case Builtin::BI_InterlockedAnd16:
  case Builtin::BI_InterlockedAnd:
    return RValue::get(EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedAnd, E));
  case Builtin::BI_InterlockedExchangeAdd8:
  case Builtin::BI_InterlockedExchangeAdd16:
  case Builtin::BI_InterlockedExchangeAdd:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeAdd, E));
  case Builtin::BI_InterlockedExchangeSub8:
  case Builtin::BI_InterlockedExchangeSub16:
  case Builtin::BI_InterlockedExchangeSub:
    return RValue::get(
        EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedExchangeSub, E));
  case Builtin::BI_InterlockedOr8:
  case Builtin::BI_InterlockedOr16:
  case Builtin::BI_InterlockedOr:
    return RValue::get(EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedOr, E));
  case Builtin::BI_InterlockedXor8:
  case Builtin::BI_InterlockedXor16:
  case Builtin::BI_InterlockedXor:
    return RValue::get(EmitMSVCBuiltinExpr(MSVCIntrin::_InterlockedXor, E));

  case Builtin::BI_bittest64:
  case Builtin::BI_bittest:
  case Builtin::BI_bittestandcomplement64:
  case Builtin::BI_bittestandcomplement:
  case Builtin::BI_bittestandreset64:
  case Builtin::BI_bittestandreset:
  case Builtin::BI_bittestandset64:
  case Builtin::BI_bittestandset:
  case Builtin::BI_interlockedbittestandreset:
  case Builtin::BI_interlockedbittestandreset64:
  case Builtin::BI_interlockedbittestandset64:
  case Builtin::BI_interlockedbittestandset:
  case Builtin::BI_interlockedbittestandset_acq:
  case Builtin::BI_interlockedbittestandset_rel:
  case Builtin::BI_interlockedbittestandset_nf:
  case Builtin::BI_interlockedbittestandreset_acq:
  case Builtin::BI_interlockedbittestandreset_rel:
  case Builtin::BI_interlockedbittestandreset_nf:
    return RValue::get(EmitBitTestIntrinsic(*this, BuiltinID, E));

    // These builtins exist to emit regular volatile loads and stores not
    // affected by the -fms-volatile setting.
  case Builtin::BI__iso_volatile_load8:
  case Builtin::BI__iso_volatile_load16:
  case Builtin::BI__iso_volatile_load32:
  case Builtin::BI__iso_volatile_load64:
    return RValue::get(EmitISOVolatileLoad(*this, E));
  case Builtin::BI__iso_volatile_store8:
  case Builtin::BI__iso_volatile_store16:
  case Builtin::BI__iso_volatile_store32:
  case Builtin::BI__iso_volatile_store64:
    return RValue::get(EmitISOVolatileStore(*this, E));

  case Builtin::BI__builtin_ptrauth_sign_constant:
    return RValue::get(ConstantEmitter(*this).emitAbstract(E, E->getType()));

  case Builtin::BI__builtin_ptrauth_auth:
  case Builtin::BI__builtin_ptrauth_auth_and_resign:
  case Builtin::BI__builtin_ptrauth_blend_discriminator:
  case Builtin::BI__builtin_ptrauth_sign_generic_data:
  case Builtin::BI__builtin_ptrauth_sign_unauthenticated:
  case Builtin::BI__builtin_ptrauth_strip: {
    // Emit the arguments.
    SmallVector<llvm::Value *, 5> Args;
    for (auto argExpr : E->arguments())
      Args.push_back(EmitScalarExpr(argExpr));

    // Cast the value to intptr_t, saving its original type.
    llvm::Type *OrigValueType = Args[0]->getType();
    if (OrigValueType->isPointerTy())
      Args[0] = Builder.CreatePtrToInt(Args[0], IntPtrTy);

    switch (BuiltinID) {
    case Builtin::BI__builtin_ptrauth_auth_and_resign:
      if (Args[4]->getType()->isPointerTy())
        Args[4] = Builder.CreatePtrToInt(Args[4], IntPtrTy);
      [[fallthrough]];

    case Builtin::BI__builtin_ptrauth_auth:
    case Builtin::BI__builtin_ptrauth_sign_unauthenticated:
      if (Args[2]->getType()->isPointerTy())
        Args[2] = Builder.CreatePtrToInt(Args[2], IntPtrTy);
      break;

    case Builtin::BI__builtin_ptrauth_sign_generic_data:
      if (Args[1]->getType()->isPointerTy())
        Args[1] = Builder.CreatePtrToInt(Args[1], IntPtrTy);
      break;

    case Builtin::BI__builtin_ptrauth_blend_discriminator:
    case Builtin::BI__builtin_ptrauth_strip:
      break;
    }

    // Call the intrinsic.
    auto IntrinsicID = [&]() -> unsigned {
      switch (BuiltinID) {
      case Builtin::BI__builtin_ptrauth_auth:
        return llvm::Intrinsic::ptrauth_auth;
      case Builtin::BI__builtin_ptrauth_auth_and_resign:
        return llvm::Intrinsic::ptrauth_resign;
      case Builtin::BI__builtin_ptrauth_blend_discriminator:
        return llvm::Intrinsic::ptrauth_blend;
      case Builtin::BI__builtin_ptrauth_sign_generic_data:
        return llvm::Intrinsic::ptrauth_sign_generic;
      case Builtin::BI__builtin_ptrauth_sign_unauthenticated:
        return llvm::Intrinsic::ptrauth_sign;
      case Builtin::BI__builtin_ptrauth_strip:
        return llvm::Intrinsic::ptrauth_strip;
      }
      llvm_unreachable("bad ptrauth intrinsic");
    }();
    auto Intrinsic = CGM.getIntrinsic(IntrinsicID);
    llvm::Value *Result = EmitRuntimeCall(Intrinsic, Args);

    if (BuiltinID != Builtin::BI__builtin_ptrauth_sign_generic_data &&
        BuiltinID != Builtin::BI__builtin_ptrauth_blend_discriminator &&
        OrigValueType->isPointerTy()) {
      Result = Builder.CreateIntToPtr(Result, OrigValueType);
    }
    return RValue::get(Result);
  }

  case Builtin::BI__exception_code:
  case Builtin::BI_exception_code:
    return RValue::get(EmitSEHExceptionCode());
  case Builtin::BI__exception_info:
  case Builtin::BI_exception_info:
    return RValue::get(EmitSEHExceptionInfo());
  case Builtin::BI__abnormal_termination:
  case Builtin::BI_abnormal_termination:
    return RValue::get(EmitSEHAbnormalTermination());
  case Builtin::BI_setjmpex:
    if (getTarget().getTriple().isOSMSVCRT() && E->getNumArgs() == 1 &&
        E->getArg(0)->getType()->isPointerType())
      return EmitMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmpex, E);
    break;
  case Builtin::BI_setjmp:
    if (getTarget().getTriple().isOSMSVCRT() && E->getNumArgs() == 1 &&
        E->getArg(0)->getType()->isPointerType()) {
      if (getTarget().getTriple().getArch() == llvm::Triple::x86)
        return EmitMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmp3, E);
      else if (getTarget().getTriple().getArch() == llvm::Triple::aarch64)
        return EmitMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmpex, E);
      return EmitMSVCRTSetJmp(*this, MSVCSetJmpKind::_setjmp, E);
    }
    break;

  // C++ std:: builtins.
  case Builtin::BImove:
  case Builtin::BImove_if_noexcept:
  case Builtin::BIforward:
  case Builtin::BIforward_like:
  case Builtin::BIas_const:
    return RValue::get(EmitLValue(E->getArg(0)).getPointer(*this));
  case Builtin::BI__GetExceptionInfo: {
    if (llvm::GlobalVariable *GV =
            CGM.getCXXABI().getThrowInfo(FD->getParamDecl(0)->getType()))
      return RValue::get(GV);
    break;
  }

  case Builtin::BI__fastfail:
    return RValue::get(EmitMSVCBuiltinExpr(MSVCIntrin::__fastfail, E));

  case Builtin::BI__builtin_coro_id:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_id);
  case Builtin::BI__builtin_coro_promise:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_promise);
  case Builtin::BI__builtin_coro_resume:
    EmitCoroutineIntrinsic(E, Intrinsic::coro_resume);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_coro_frame:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_frame);
  case Builtin::BI__builtin_coro_noop:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_noop);
  case Builtin::BI__builtin_coro_free:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_free);
  case Builtin::BI__builtin_coro_destroy:
    EmitCoroutineIntrinsic(E, Intrinsic::coro_destroy);
    return RValue::get(nullptr);
  case Builtin::BI__builtin_coro_done:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_done);
  case Builtin::BI__builtin_coro_alloc:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_alloc);
  case Builtin::BI__builtin_coro_begin:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_begin);
  case Builtin::BI__builtin_coro_end:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_end);
  case Builtin::BI__builtin_coro_suspend:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_suspend);
  case Builtin::BI__builtin_coro_size:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_size);
  case Builtin::BI__builtin_coro_align:
    return EmitCoroutineIntrinsic(E, Intrinsic::coro_align);

  // OpenCL v2.0 s6.13.16.2, Built-in pipe read and write functions
  case Builtin::BIread_pipe:
  case Builtin::BIwrite_pipe: {
    Value *Arg0 = EmitScalarExpr(E->getArg(0)),
          *Arg1 = EmitScalarExpr(E->getArg(1));
    CGOpenCLRuntime OpenCLRT(CGM);
    Value *PacketSize = OpenCLRT.getPipeElemSize(E->getArg(0));
    Value *PacketAlign = OpenCLRT.getPipeElemAlign(E->getArg(0));

    // Type of the generic packet parameter.
    unsigned GenericAS =
        getContext().getTargetAddressSpace(LangAS::opencl_generic);
    llvm::Type *I8PTy = llvm::PointerType::get(getLLVMContext(), GenericAS);

    // Testing which overloaded version we should generate the call for.
    if (2U == E->getNumArgs()) {
      const char *Name = (BuiltinID == Builtin::BIread_pipe) ? "__read_pipe_2"
                                                             : "__write_pipe_2";
      // Creating a generic function type to be able to call with any builtin or
      // user defined type.
      llvm::Type *ArgTys[] = {Arg0->getType(), I8PTy, Int32Ty, Int32Ty};
      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
      Value *BCast = Builder.CreatePointerCast(Arg1, I8PTy);
      return RValue::get(
          EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name),
                          {Arg0, BCast, PacketSize, PacketAlign}));
    } else {
      assert(4 == E->getNumArgs() &&
             "Illegal number of parameters to pipe function");
      const char *Name = (BuiltinID == Builtin::BIread_pipe) ? "__read_pipe_4"
                                                             : "__write_pipe_4";

      llvm::Type *ArgTys[] = {Arg0->getType(), Arg1->getType(), Int32Ty, I8PTy,
                              Int32Ty, Int32Ty};
      Value *Arg2 = EmitScalarExpr(E->getArg(2)),
            *Arg3 = EmitScalarExpr(E->getArg(3));
      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
      Value *BCast = Builder.CreatePointerCast(Arg3, I8PTy);
      // We know the third argument is an integer type, but we may need to cast
      // it to i32.
      if (Arg2->getType() != Int32Ty)
        Arg2 = Builder.CreateZExtOrTrunc(Arg2, Int32Ty);
      return RValue::get(
          EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name),
                          {Arg0, Arg1, Arg2, BCast, PacketSize, PacketAlign}));
    }
  }
  // OpenCL v2.0 s6.13.16 ,s9.17.3.5 - Built-in pipe reserve read and write
  // functions
  case Builtin::BIreserve_read_pipe:
  case Builtin::BIreserve_write_pipe:
  case Builtin::BIwork_group_reserve_read_pipe:
  case Builtin::BIwork_group_reserve_write_pipe:
  case Builtin::BIsub_group_reserve_read_pipe:
  case Builtin::BIsub_group_reserve_write_pipe: {
    // Composing the mangled name for the function.
    const char *Name;
    if (BuiltinID == Builtin::BIreserve_read_pipe)
      Name = "__reserve_read_pipe";
    else if (BuiltinID == Builtin::BIreserve_write_pipe)
      Name = "__reserve_write_pipe";
    else if (BuiltinID == Builtin::BIwork_group_reserve_read_pipe)
      Name = "__work_group_reserve_read_pipe";
    else if (BuiltinID == Builtin::BIwork_group_reserve_write_pipe)
      Name = "__work_group_reserve_write_pipe";
    else if (BuiltinID == Builtin::BIsub_group_reserve_read_pipe)
      Name = "__sub_group_reserve_read_pipe";
    else
      Name = "__sub_group_reserve_write_pipe";

    Value *Arg0 = EmitScalarExpr(E->getArg(0)),
          *Arg1 = EmitScalarExpr(E->getArg(1));
    llvm::Type *ReservedIDTy = ConvertType(getContext().OCLReserveIDTy);
    CGOpenCLRuntime OpenCLRT(CGM);
    Value *PacketSize = OpenCLRT.getPipeElemSize(E->getArg(0));
    Value *PacketAlign = OpenCLRT.getPipeElemAlign(E->getArg(0));

    // Building the generic function prototype.
    llvm::Type *ArgTys[] = {Arg0->getType(), Int32Ty, Int32Ty, Int32Ty};
    llvm::FunctionType *FTy = llvm::FunctionType::get(
        ReservedIDTy, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
    // We know the second argument is an integer type, but we may need to cast
    // it to i32.
    if (Arg1->getType() != Int32Ty)
      Arg1 = Builder.CreateZExtOrTrunc(Arg1, Int32Ty);
    return RValue::get(EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name),
                                       {Arg0, Arg1, PacketSize, PacketAlign}));
  }
  // OpenCL v2.0 s6.13.16, s9.17.3.5 - Built-in pipe commit read and write
  // functions
  case Builtin::BIcommit_read_pipe:
  case Builtin::BIcommit_write_pipe:
  case Builtin::BIwork_group_commit_read_pipe:
  case Builtin::BIwork_group_commit_write_pipe:
  case Builtin::BIsub_group_commit_read_pipe:
  case Builtin::BIsub_group_commit_write_pipe: {
    const char *Name;
    if (BuiltinID == Builtin::BIcommit_read_pipe)
      Name = "__commit_read_pipe";
    else if (BuiltinID == Builtin::BIcommit_write_pipe)
      Name = "__commit_write_pipe";
    else if (BuiltinID == Builtin::BIwork_group_commit_read_pipe)
      Name = "__work_group_commit_read_pipe";
    else if (BuiltinID == Builtin::BIwork_group_commit_write_pipe)
      Name = "__work_group_commit_write_pipe";
    else if (BuiltinID == Builtin::BIsub_group_commit_read_pipe)
      Name = "__sub_group_commit_read_pipe";
    else
      Name = "__sub_group_commit_write_pipe";

    Value *Arg0 = EmitScalarExpr(E->getArg(0)),
          *Arg1 = EmitScalarExpr(E->getArg(1));
    CGOpenCLRuntime OpenCLRT(CGM);
    Value *PacketSize = OpenCLRT.getPipeElemSize(E->getArg(0));
    Value *PacketAlign = OpenCLRT.getPipeElemAlign(E->getArg(0));

    // Building the generic function prototype.
    llvm::Type *ArgTys[] = {Arg0->getType(), Arg1->getType(), Int32Ty, Int32Ty};
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(llvm::Type::getVoidTy(getLLVMContext()),
                                llvm::ArrayRef<llvm::Type *>(ArgTys), false);

    return RValue::get(EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name),
                                       {Arg0, Arg1, PacketSize, PacketAlign}));
  }
  // OpenCL v2.0 s6.13.16.4 Built-in pipe query functions
  case Builtin::BIget_pipe_num_packets:
  case Builtin::BIget_pipe_max_packets: {
    const char *BaseName;
    const auto *PipeTy = E->getArg(0)->getType()->castAs<PipeType>();
    if (BuiltinID == Builtin::BIget_pipe_num_packets)
      BaseName = "__get_pipe_num_packets";
    else
      BaseName = "__get_pipe_max_packets";
    std::string Name = std::string(BaseName) +
                       std::string(PipeTy->isReadOnly() ? "_ro" : "_wo");

    // Building the generic function prototype.
    Value *Arg0 = EmitScalarExpr(E->getArg(0));
    CGOpenCLRuntime OpenCLRT(CGM);
    Value *PacketSize = OpenCLRT.getPipeElemSize(E->getArg(0));
    Value *PacketAlign = OpenCLRT.getPipeElemAlign(E->getArg(0));
    llvm::Type *ArgTys[] = {Arg0->getType(), Int32Ty, Int32Ty};
    llvm::FunctionType *FTy = llvm::FunctionType::get(
        Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);

    return RValue::get(EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name),
                                       {Arg0, PacketSize, PacketAlign}));
  }

  // OpenCL v2.0 s6.13.9 - Address space qualifier functions.
  case Builtin::BIto_global:
  case Builtin::BIto_local:
  case Builtin::BIto_private: {
    auto Arg0 = EmitScalarExpr(E->getArg(0));
    auto NewArgT = llvm::PointerType::get(
        getLLVMContext(),
        CGM.getContext().getTargetAddressSpace(LangAS::opencl_generic));
    auto NewRetT = llvm::PointerType::get(
        getLLVMContext(),
        CGM.getContext().getTargetAddressSpace(
            E->getType()->getPointeeType().getAddressSpace()));
    auto FTy = llvm::FunctionType::get(NewRetT, {NewArgT}, false);
    llvm::Value *NewArg;
    if (Arg0->getType()->getPointerAddressSpace() !=
        NewArgT->getPointerAddressSpace())
      NewArg = Builder.CreateAddrSpaceCast(Arg0, NewArgT);
    else
      NewArg = Builder.CreateBitOrPointerCast(Arg0, NewArgT);
    auto NewName = std::string("__") + E->getDirectCallee()->getName().str();
    auto NewCall =
        EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, NewName), {NewArg});
    return RValue::get(Builder.CreateBitOrPointerCast(NewCall,
      ConvertType(E->getType())));
  }

  // OpenCL v2.0, s6.13.17 - Enqueue kernel function.
  // Table 6.13.17.1 specifies four overload forms of enqueue_kernel.
  // The code below expands the builtin call to a call to one of the following
  // functions that an OpenCL runtime library will have to provide:
  //   __enqueue_kernel_basic
  //   __enqueue_kernel_varargs
  //   __enqueue_kernel_basic_events
  //   __enqueue_kernel_events_varargs
  case Builtin::BIenqueue_kernel: {
    StringRef Name; // Generated function call name
    unsigned NumArgs = E->getNumArgs();

    llvm::Type *QueueTy = ConvertType(getContext().OCLQueueTy);
    llvm::Type *GenericVoidPtrTy = Builder.getPtrTy(
        getContext().getTargetAddressSpace(LangAS::opencl_generic));

    llvm::Value *Queue = EmitScalarExpr(E->getArg(0));
    llvm::Value *Flags = EmitScalarExpr(E->getArg(1));
    LValue NDRangeL = EmitAggExprToLValue(E->getArg(2));
    llvm::Value *Range = NDRangeL.getAddress().emitRawPointer(*this);
    llvm::Type *RangeTy = NDRangeL.getAddress().getType();

    if (NumArgs == 4) {
      // The most basic form of the call with parameters:
      // queue_t, kernel_enqueue_flags_t, ndrange_t, block(void)
      Name = "__enqueue_kernel_basic";
      llvm::Type *ArgTys[] = {QueueTy, Int32Ty, RangeTy, GenericVoidPtrTy,
                              GenericVoidPtrTy};
      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);

      auto Info =
          CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(3));
      llvm::Value *Kernel =
          Builder.CreatePointerCast(Info.KernelHandle, GenericVoidPtrTy);
      llvm::Value *Block =
          Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);

      AttrBuilder B(Builder.getContext());
      B.addByValAttr(NDRangeL.getAddress().getElementType());
      llvm::AttributeList ByValAttrSet =
          llvm::AttributeList::get(CGM.getModule().getContext(), 3U, B);

      auto RTCall =
          EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name, ByValAttrSet),
                          {Queue, Flags, Range, Kernel, Block});
      RTCall->setAttributes(ByValAttrSet);
      return RValue::get(RTCall);
    }
    assert(NumArgs >= 5 && "Invalid enqueue_kernel signature");

    // Create a temporary array to hold the sizes of local pointer arguments
    // for the block. \p First is the position of the first size argument.
    auto CreateArrayForSizeVar = [=](unsigned First)
        -> std::tuple<llvm::Value *, llvm::Value *, llvm::Value *> {
      llvm::APInt ArraySize(32, NumArgs - First);
      QualType SizeArrayTy = getContext().getConstantArrayType(
          getContext().getSizeType(), ArraySize, nullptr,
          ArraySizeModifier::Normal,
          /*IndexTypeQuals=*/0);
      auto Tmp = CreateMemTemp(SizeArrayTy, "block_sizes");
      llvm::Value *TmpPtr = Tmp.getPointer();
      llvm::Value *TmpSize = EmitLifetimeStart(
          CGM.getDataLayout().getTypeAllocSize(Tmp.getElementType()), TmpPtr);
      llvm::Value *ElemPtr;
      // Each of the following arguments specifies the size of the corresponding
      // argument passed to the enqueued block.
      auto *Zero = llvm::ConstantInt::get(IntTy, 0);
      for (unsigned I = First; I < NumArgs; ++I) {
        auto *Index = llvm::ConstantInt::get(IntTy, I - First);
        auto *GEP = Builder.CreateGEP(Tmp.getElementType(), TmpPtr,
                                      {Zero, Index});
        if (I == First)
          ElemPtr = GEP;
        auto *V =
            Builder.CreateZExtOrTrunc(EmitScalarExpr(E->getArg(I)), SizeTy);
        Builder.CreateAlignedStore(
            V, GEP, CGM.getDataLayout().getPrefTypeAlign(SizeTy));
      }
      return std::tie(ElemPtr, TmpSize, TmpPtr);
    };

    // Could have events and/or varargs.
    if (E->getArg(3)->getType()->isBlockPointerType()) {
      // No events passed, but has variadic arguments.
      Name = "__enqueue_kernel_varargs";
      auto Info =
          CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(3));
      llvm::Value *Kernel =
          Builder.CreatePointerCast(Info.KernelHandle, GenericVoidPtrTy);
      auto *Block = Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);
      llvm::Value *ElemPtr, *TmpSize, *TmpPtr;
      std::tie(ElemPtr, TmpSize, TmpPtr) = CreateArrayForSizeVar(4);

      // Create a vector of the arguments, as well as a constant value to
      // express to the runtime the number of variadic arguments.
      llvm::Value *const Args[] = {Queue,  Flags,
                                   Range,  Kernel,
                                   Block,  ConstantInt::get(IntTy, NumArgs - 4),
                                   ElemPtr};
      llvm::Type *const ArgTys[] = {
          QueueTy,          IntTy, RangeTy,           GenericVoidPtrTy,
          GenericVoidPtrTy, IntTy, ElemPtr->getType()};

      llvm::FunctionType *FTy = llvm::FunctionType::get(Int32Ty, ArgTys, false);
      auto Call = RValue::get(
          EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name), Args));
      if (TmpSize)
        EmitLifetimeEnd(TmpSize, TmpPtr);
      return Call;
    }
    // Any calls now have event arguments passed.
    if (NumArgs >= 7) {
      llvm::PointerType *PtrTy = llvm::PointerType::get(
          CGM.getLLVMContext(),
          CGM.getContext().getTargetAddressSpace(LangAS::opencl_generic));

      llvm::Value *NumEvents =
          Builder.CreateZExtOrTrunc(EmitScalarExpr(E->getArg(3)), Int32Ty);

      // Since SemaOpenCLBuiltinEnqueueKernel allows fifth and sixth arguments
      // to be a null pointer constant (including `0` literal), we can take it
      // into account and emit null pointer directly.
      llvm::Value *EventWaitList = nullptr;
      if (E->getArg(4)->isNullPointerConstant(
              getContext(), Expr::NPC_ValueDependentIsNotNull)) {
        EventWaitList = llvm::ConstantPointerNull::get(PtrTy);
      } else {
        EventWaitList =
            E->getArg(4)->getType()->isArrayType()
                ? EmitArrayToPointerDecay(E->getArg(4)).emitRawPointer(*this)
                : EmitScalarExpr(E->getArg(4));
        // Convert to generic address space.
        EventWaitList = Builder.CreatePointerCast(EventWaitList, PtrTy);
      }
      llvm::Value *EventRet = nullptr;
      if (E->getArg(5)->isNullPointerConstant(
              getContext(), Expr::NPC_ValueDependentIsNotNull)) {
        EventRet = llvm::ConstantPointerNull::get(PtrTy);
      } else {
        EventRet =
            Builder.CreatePointerCast(EmitScalarExpr(E->getArg(5)), PtrTy);
      }

      auto Info =
          CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(6));
      llvm::Value *Kernel =
          Builder.CreatePointerCast(Info.KernelHandle, GenericVoidPtrTy);
      llvm::Value *Block =
          Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);

      std::vector<llvm::Type *> ArgTys = {
          QueueTy, Int32Ty, RangeTy,          Int32Ty,
          PtrTy,   PtrTy,   GenericVoidPtrTy, GenericVoidPtrTy};

      std::vector<llvm::Value *> Args = {Queue,     Flags,         Range,
                                         NumEvents, EventWaitList, EventRet,
                                         Kernel,    Block};

      if (NumArgs == 7) {
        // Has events but no variadics.
        Name = "__enqueue_kernel_basic_events";
        llvm::FunctionType *FTy = llvm::FunctionType::get(
            Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
        return RValue::get(
            EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name),
                            llvm::ArrayRef<llvm::Value *>(Args)));
      }
      // Has event info and variadics
      // Pass the number of variadics to the runtime function too.
      Args.push_back(ConstantInt::get(Int32Ty, NumArgs - 7));
      ArgTys.push_back(Int32Ty);
      Name = "__enqueue_kernel_events_varargs";

      llvm::Value *ElemPtr, *TmpSize, *TmpPtr;
      std::tie(ElemPtr, TmpSize, TmpPtr) = CreateArrayForSizeVar(7);
      Args.push_back(ElemPtr);
      ArgTys.push_back(ElemPtr->getType());

      llvm::FunctionType *FTy = llvm::FunctionType::get(
          Int32Ty, llvm::ArrayRef<llvm::Type *>(ArgTys), false);
      auto Call =
          RValue::get(EmitRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name),
                                      llvm::ArrayRef<llvm::Value *>(Args)));
      if (TmpSize)
        EmitLifetimeEnd(TmpSize, TmpPtr);
      return Call;
    }
    llvm_unreachable("Unexpected enqueue_kernel signature");
  }
  // OpenCL v2.0 s6.13.17.6 - Kernel query functions need bitcast of block
  // parameter.
  case Builtin::BIget_kernel_work_group_size: {
    llvm::Type *GenericVoidPtrTy = Builder.getPtrTy(
        getContext().getTargetAddressSpace(LangAS::opencl_generic));
    auto Info =
        CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(0));
    Value *Kernel =
        Builder.CreatePointerCast(Info.KernelHandle, GenericVoidPtrTy);
    Value *Arg = Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);
    return RValue::get(EmitRuntimeCall(
        CGM.CreateRuntimeFunction(
            llvm::FunctionType::get(IntTy, {GenericVoidPtrTy, GenericVoidPtrTy},
                                    false),
            "__get_kernel_work_group_size_impl"),
        {Kernel, Arg}));
  }
  case Builtin::BIget_kernel_preferred_work_group_size_multiple: {
    llvm::Type *GenericVoidPtrTy = Builder.getPtrTy(
        getContext().getTargetAddressSpace(LangAS::opencl_generic));
    auto Info =
        CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(0));
    Value *Kernel =
        Builder.CreatePointerCast(Info.KernelHandle, GenericVoidPtrTy);
    Value *Arg = Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);
    return RValue::get(EmitRuntimeCall(
        CGM.CreateRuntimeFunction(
            llvm::FunctionType::get(IntTy, {GenericVoidPtrTy, GenericVoidPtrTy},
                                    false),
            "__get_kernel_preferred_work_group_size_multiple_impl"),
        {Kernel, Arg}));
  }
  case Builtin::BIget_kernel_max_sub_group_size_for_ndrange:
  case Builtin::BIget_kernel_sub_group_count_for_ndrange: {
    llvm::Type *GenericVoidPtrTy = Builder.getPtrTy(
        getContext().getTargetAddressSpace(LangAS::opencl_generic));
    LValue NDRangeL = EmitAggExprToLValue(E->getArg(0));
    llvm::Value *NDRange = NDRangeL.getAddress().emitRawPointer(*this);
    auto Info =
        CGM.getOpenCLRuntime().emitOpenCLEnqueuedBlock(*this, E->getArg(1));
    Value *Kernel =
        Builder.CreatePointerCast(Info.KernelHandle, GenericVoidPtrTy);
    Value *Block = Builder.CreatePointerCast(Info.BlockArg, GenericVoidPtrTy);
    const char *Name =
        BuiltinID == Builtin::BIget_kernel_max_sub_group_size_for_ndrange
            ? "__get_kernel_max_sub_group_size_for_ndrange_impl"
            : "__get_kernel_sub_group_count_for_ndrange_impl";
    return RValue::get(EmitRuntimeCall(
        CGM.CreateRuntimeFunction(
            llvm::FunctionType::get(
                IntTy, {NDRange->getType(), GenericVoidPtrTy, GenericVoidPtrTy},
                false),
            Name),
        {NDRange, Kernel, Block}));
  }
  case Builtin::BI__builtin_store_half:
  case Builtin::BI__builtin_store_halff: {
    Value *Val = EmitScalarExpr(E->getArg(0));
    Address Address = EmitPointerWithAlignment(E->getArg(1));
    Value *HalfVal = Builder.CreateFPTrunc(Val, Builder.getHalfTy());
    Builder.CreateStore(HalfVal, Address);
    return RValue::get(nullptr);
  }
  case Builtin::BI__builtin_load_half: {
    Address Address = EmitPointerWithAlignment(E->getArg(0));
    Value *HalfVal = Builder.CreateLoad(Address);
    return RValue::get(Builder.CreateFPExt(HalfVal, Builder.getDoubleTy()));
  }
  case Builtin::BI__builtin_load_halff: {
    Address Address = EmitPointerWithAlignment(E->getArg(0));
    Value *HalfVal = Builder.CreateLoad(Address);
    return RValue::get(Builder.CreateFPExt(HalfVal, Builder.getFloatTy()));
  }
  case Builtin::BI__builtin_printf:
  case Builtin::BIprintf:
    if (getTarget().getTriple().isNVPTX() ||
        getTarget().getTriple().isAMDGCN() ||
        (getTarget().getTriple().isSPIRV() &&
         getTarget().getTriple().getVendor() == Triple::VendorType::AMD)) {
      if (getLangOpts().OpenMPIsTargetDevice)
        return EmitOpenMPDevicePrintfCallExpr(E);
      if (getTarget().getTriple().isNVPTX())
        return EmitNVPTXDevicePrintfCallExpr(E);
      if ((getTarget().getTriple().isAMDGCN() ||
           getTarget().getTriple().isSPIRV()) &&
          getLangOpts().HIP)
        return EmitAMDGPUDevicePrintfCallExpr(E);
    }

    break;
  case Builtin::BI__builtin_canonicalize:
  case Builtin::BI__builtin_canonicalizef:
  case Builtin::BI__builtin_canonicalizef16:
  case Builtin::BI__builtin_canonicalizel:
    return RValue::get(
        emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::canonicalize));

  case Builtin::BI__builtin_thread_pointer: {
    if (!getContext().getTargetInfo().isTLSSupported())
      CGM.ErrorUnsupported(E, "__builtin_thread_pointer");
    // Fall through - it's already mapped to the intrinsic by ClangBuiltin.
    break;
  }
  case Builtin::BI__builtin_os_log_format:
    return emitBuiltinOSLogFormat(*E);

  case Builtin::BI__xray_customevent: {
    if (!ShouldXRayInstrumentFunction())
      return RValue::getIgnored();

    if (!CGM.getCodeGenOpts().XRayInstrumentationBundle.has(
            XRayInstrKind::Custom))
      return RValue::getIgnored();

    if (const auto *XRayAttr = CurFuncDecl->getAttr<XRayInstrumentAttr>())
      if (XRayAttr->neverXRayInstrument() && !AlwaysEmitXRayCustomEvents())
        return RValue::getIgnored();

    Function *F = CGM.getIntrinsic(Intrinsic::xray_customevent);
    auto FTy = F->getFunctionType();
    auto Arg0 = E->getArg(0);
    auto Arg0Val = EmitScalarExpr(Arg0);
    auto Arg0Ty = Arg0->getType();
    auto PTy0 = FTy->getParamType(0);
    if (PTy0 != Arg0Val->getType()) {
      if (Arg0Ty->isArrayType())
        Arg0Val = EmitArrayToPointerDecay(Arg0).emitRawPointer(*this);
      else
        Arg0Val = Builder.CreatePointerCast(Arg0Val, PTy0);
    }
    auto Arg1 = EmitScalarExpr(E->getArg(1));
    auto PTy1 = FTy->getParamType(1);
    if (PTy1 != Arg1->getType())
      Arg1 = Builder.CreateTruncOrBitCast(Arg1, PTy1);
    return RValue::get(Builder.CreateCall(F, {Arg0Val, Arg1}));
  }

  case Builtin::BI__xray_typedevent: {
    // TODO: There should be a way to always emit events even if the current
    // function is not instrumented. Losing events in a stream can cripple
    // a trace.
    if (!ShouldXRayInstrumentFunction())
      return RValue::getIgnored();

    if (!CGM.getCodeGenOpts().XRayInstrumentationBundle.has(
            XRayInstrKind::Typed))
      return RValue::getIgnored();

    if (const auto *XRayAttr = CurFuncDecl->getAttr<XRayInstrumentAttr>())
      if (XRayAttr->neverXRayInstrument() && !AlwaysEmitXRayTypedEvents())
        return RValue::getIgnored();

    Function *F = CGM.getIntrinsic(Intrinsic::xray_typedevent);
    auto FTy = F->getFunctionType();
    auto Arg0 = EmitScalarExpr(E->getArg(0));
    auto PTy0 = FTy->getParamType(0);
    if (PTy0 != Arg0->getType())
      Arg0 = Builder.CreateTruncOrBitCast(Arg0, PTy0);
    auto Arg1 = E->getArg(1);
    auto Arg1Val = EmitScalarExpr(Arg1);
    auto Arg1Ty = Arg1->getType();
    auto PTy1 = FTy->getParamType(1);
    if (PTy1 != Arg1Val->getType()) {
      if (Arg1Ty->isArrayType())
        Arg1Val = EmitArrayToPointerDecay(Arg1).emitRawPointer(*this);
      else
        Arg1Val = Builder.CreatePointerCast(Arg1Val, PTy1);
    }
    auto Arg2 = EmitScalarExpr(E->getArg(2));
    auto PTy2 = FTy->getParamType(2);
    if (PTy2 != Arg2->getType())
      Arg2 = Builder.CreateTruncOrBitCast(Arg2, PTy2);
    return RValue::get(Builder.CreateCall(F, {Arg0, Arg1Val, Arg2}));
  }

  case Builtin::BI__builtin_ms_va_start:
  case Builtin::BI__builtin_ms_va_end:
    return RValue::get(
        EmitVAStartEnd(EmitMSVAListRef(E->getArg(0)).emitRawPointer(*this),
                       BuiltinID == Builtin::BI__builtin_ms_va_start));

  case Builtin::BI__builtin_ms_va_copy: {
    // Lower this manually. We can't reliably determine whether or not any
    // given va_copy() is for a Win64 va_list from the calling convention
    // alone, because it's legal to do this from a System V ABI function.
    // With opaque pointer types, we won't have enough information in LLVM
    // IR to determine this from the argument types, either. Best to do it
    // now, while we have enough information.
    Address DestAddr = EmitMSVAListRef(E->getArg(0));
    Address SrcAddr = EmitMSVAListRef(E->getArg(1));

    DestAddr = DestAddr.withElementType(Int8PtrTy);
    SrcAddr = SrcAddr.withElementType(Int8PtrTy);

    Value *ArgPtr = Builder.CreateLoad(SrcAddr, "ap.val");
    return RValue::get(Builder.CreateStore(ArgPtr, DestAddr));
  }

  case Builtin::BI__builtin_get_device_side_mangled_name: {
    auto Name = CGM.getCUDARuntime().getDeviceSideName(
        cast<DeclRefExpr>(E->getArg(0)->IgnoreImpCasts())->getDecl());
    auto Str = CGM.GetAddrOfConstantCString(Name, "");
    return RValue::get(Str.getPointer());
  }
  }

  // If this is an alias for a lib function (e.g. __builtin_sin), emit
  // the call using the normal call path, but using the unmangled
  // version of the function name.
  if (getContext().BuiltinInfo.isLibFunction(BuiltinID))
    return emitLibraryCall(*this, FD, E,
                           CGM.getBuiltinLibFunction(FD, BuiltinID));

  // If this is a predefined lib function (e.g. malloc), emit the call
  // using exactly the normal call path.
  if (getContext().BuiltinInfo.isPredefinedLibFunction(BuiltinID))
    return emitLibraryCall(*this, FD, E, CGM.getRawFunctionPointer(FD));

  // Check that a call to a target specific builtin has the correct target
  // features.
  // This is down here to avoid non-target specific builtins, however, if
  // generic builtins start to require generic target features then we
  // can move this up to the beginning of the function.
  checkTargetFeatures(E, FD);

  if (unsigned VectorWidth = getContext().BuiltinInfo.getRequiredVectorWidth(BuiltinID))
    LargestVectorWidth = std::max(LargestVectorWidth, VectorWidth);

  // See if we have a target specific intrinsic.
  StringRef Name = getContext().BuiltinInfo.getName(BuiltinID);
  Intrinsic::ID IntrinsicID = Intrinsic::not_intrinsic;
  StringRef Prefix =
      llvm::Triple::getArchTypePrefix(getTarget().getTriple().getArch());
  if (!Prefix.empty()) {
    IntrinsicID = Intrinsic::getIntrinsicForClangBuiltin(Prefix.data(), Name);
    if (IntrinsicID == Intrinsic::not_intrinsic && Prefix == "spv" &&
        getTarget().getTriple().getOS() == llvm::Triple::OSType::AMDHSA)
      IntrinsicID = Intrinsic::getIntrinsicForClangBuiltin("amdgcn", Name);
    // NOTE we don't need to perform a compatibility flag check here since the
    // intrinsics are declared in Builtins*.def via LANGBUILTIN which filter the
    // MS builtins via ALL_MS_LANGUAGES and are filtered earlier.
    if (IntrinsicID == Intrinsic::not_intrinsic)
      IntrinsicID = Intrinsic::getIntrinsicForMSBuiltin(Prefix.data(), Name);
  }

  if (IntrinsicID != Intrinsic::not_intrinsic) {
    SmallVector<Value*, 16> Args;

    // Find out if any arguments are required to be integer constant
    // expressions.
    unsigned ICEArguments = 0;
    ASTContext::GetBuiltinTypeError Error;
    getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
    assert(Error == ASTContext::GE_None && "Should not codegen an error");

    Function *F = CGM.getIntrinsic(IntrinsicID);
    llvm::FunctionType *FTy = F->getFunctionType();

    for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
      Value *ArgValue = EmitScalarOrConstFoldImmArg(ICEArguments, i, E);
      // If the intrinsic arg type is different from the builtin arg type
      // we need to do a bit cast.
      llvm::Type *PTy = FTy->getParamType(i);
      if (PTy != ArgValue->getType()) {
        // XXX - vector of pointers?
        if (auto *PtrTy = dyn_cast<llvm::PointerType>(PTy)) {
          if (PtrTy->getAddressSpace() !=
              ArgValue->getType()->getPointerAddressSpace()) {
            ArgValue = Builder.CreateAddrSpaceCast(
                ArgValue, llvm::PointerType::get(getLLVMContext(),
                                                 PtrTy->getAddressSpace()));
          }
        }

        // Cast vector type (e.g., v256i32) to x86_amx, this only happen
        // in amx intrinsics.
        if (PTy->isX86_AMXTy())
          ArgValue = Builder.CreateIntrinsic(Intrinsic::x86_cast_vector_to_tile,
                                             {ArgValue->getType()}, {ArgValue});
        else
          ArgValue = Builder.CreateBitCast(ArgValue, PTy);
      }

      Args.push_back(ArgValue);
    }

    Value *V = Builder.CreateCall(F, Args);
    QualType BuiltinRetType = E->getType();

    llvm::Type *RetTy = VoidTy;
    if (!BuiltinRetType->isVoidType())
      RetTy = ConvertType(BuiltinRetType);

    if (RetTy != V->getType()) {
      // XXX - vector of pointers?
      if (auto *PtrTy = dyn_cast<llvm::PointerType>(RetTy)) {
        if (PtrTy->getAddressSpace() != V->getType()->getPointerAddressSpace()) {
          V = Builder.CreateAddrSpaceCast(
              V, llvm::PointerType::get(getLLVMContext(),
                                        PtrTy->getAddressSpace()));
        }
      }

      // Cast x86_amx to vector type (e.g., v256i32), this only happen
      // in amx intrinsics.
      if (V->getType()->isX86_AMXTy())
        V = Builder.CreateIntrinsic(Intrinsic::x86_cast_tile_to_vector, {RetTy},
                                    {V});
      else
        V = Builder.CreateBitCast(V, RetTy);
    }

    if (RetTy->isVoidTy())
      return RValue::get(nullptr);

    return RValue::get(V);
  }

  // Some target-specific builtins can have aggregate return values, e.g.
  // __builtin_arm_mve_vld2q_u32. So if the result is an aggregate, force
  // ReturnValue to be non-null, so that the target-specific emission code can
  // always just emit into it.
  TypeEvaluationKind EvalKind = getEvaluationKind(E->getType());
  if (EvalKind == TEK_Aggregate && ReturnValue.isNull()) {
    Address DestPtr = CreateMemTemp(E->getType(), "agg.tmp");
    ReturnValue = ReturnValueSlot(DestPtr, false);
  }

  // Now see if we can emit a target-specific builtin.
  if (Value *V = EmitTargetBuiltinExpr(BuiltinID, E, ReturnValue)) {
    switch (EvalKind) {
    case TEK_Scalar:
      if (V->getType()->isVoidTy())
        return RValue::get(nullptr);
      return RValue::get(V);
    case TEK_Aggregate:
      return RValue::getAggregate(ReturnValue.getAddress(),
                                  ReturnValue.isVolatile());
    case TEK_Complex:
      llvm_unreachable("No current target builtin returns complex");
    }
    llvm_unreachable("Bad evaluation kind in EmitBuiltinExpr");
  }

  // EmitHLSLBuiltinExpr will check getLangOpts().HLSL
  if (Value *V = EmitHLSLBuiltinExpr(BuiltinID, E))
    return RValue::get(V);

  if (getLangOpts().HIPStdPar && getLangOpts().CUDAIsDevice)
    return EmitHipStdParUnsupportedBuiltin(this, FD);

  ErrorUnsupported(E, "builtin function");

  // Unknown builtin, for now just dump it out and return undef.
  return GetUndefRValue(E->getType());
}

static Value *EmitTargetArchBuiltinExpr(CodeGenFunction *CGF,
                                        unsigned BuiltinID, const CallExpr *E,
                                        ReturnValueSlot ReturnValue,
                                        llvm::Triple::ArchType Arch) {
  // When compiling in HipStdPar mode we have to be conservative in rejecting
  // target specific features in the FE, and defer the possible error to the
  // AcceleratorCodeSelection pass, wherein iff an unsupported target builtin is
  // referenced by an accelerator executable function, we emit an error.
  // Returning nullptr here leads to the builtin being handled in
  // EmitStdParUnsupportedBuiltin.
  if (CGF->getLangOpts().HIPStdPar && CGF->getLangOpts().CUDAIsDevice &&
      Arch != CGF->getTarget().getTriple().getArch())
    return nullptr;

  switch (Arch) {
  case llvm::Triple::arm:
  case llvm::Triple::armeb:
  case llvm::Triple::thumb:
  case llvm::Triple::thumbeb:
    return CGF->EmitARMBuiltinExpr(BuiltinID, E, ReturnValue, Arch);
  case llvm::Triple::aarch64:
  case llvm::Triple::aarch64_32:
  case llvm::Triple::aarch64_be:
    return CGF->EmitAArch64BuiltinExpr(BuiltinID, E, Arch);
  case llvm::Triple::bpfeb:
  case llvm::Triple::bpfel:
    return CGF->EmitBPFBuiltinExpr(BuiltinID, E);
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return CGF->EmitX86BuiltinExpr(BuiltinID, E);
  case llvm::Triple::ppc:
  case llvm::Triple::ppcle:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
    return CGF->EmitPPCBuiltinExpr(BuiltinID, E);
  case llvm::Triple::r600:
  case llvm::Triple::amdgcn:
    return CGF->EmitAMDGPUBuiltinExpr(BuiltinID, E);
  case llvm::Triple::systemz:
    return CGF->EmitSystemZBuiltinExpr(BuiltinID, E);
  case llvm::Triple::nvptx:
  case llvm::Triple::nvptx64:
    return CGF->EmitNVPTXBuiltinExpr(BuiltinID, E);
  case llvm::Triple::wasm32:
  case llvm::Triple::wasm64:
    return CGF->EmitWebAssemblyBuiltinExpr(BuiltinID, E);
  case llvm::Triple::hexagon:
    return CGF->EmitHexagonBuiltinExpr(BuiltinID, E);
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    return CGF->EmitRISCVBuiltinExpr(BuiltinID, E, ReturnValue);
  case llvm::Triple::spirv64:
    if (CGF->getTarget().getTriple().getOS() != llvm::Triple::OSType::AMDHSA)
      return nullptr;
    return CGF->EmitAMDGPUBuiltinExpr(BuiltinID, E);
  default:
    return nullptr;
  }
}

Value *CodeGenFunction::EmitTargetBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E,
                                              ReturnValueSlot ReturnValue) {
  if (getContext().BuiltinInfo.isAuxBuiltinID(BuiltinID)) {
    assert(getContext().getAuxTargetInfo() && "Missing aux target info");
    return EmitTargetArchBuiltinExpr(
        this, getContext().BuiltinInfo.getAuxBuiltinID(BuiltinID), E,
        ReturnValue, getContext().getAuxTargetInfo()->getTriple().getArch());
  }

  return EmitTargetArchBuiltinExpr(this, BuiltinID, E, ReturnValue,
                                   getTarget().getTriple().getArch());
}

static llvm::FixedVectorType *GetNeonType(CodeGenFunction *CGF,
                                          NeonTypeFlags TypeFlags,
                                          bool HasLegalHalfType = true,
                                          bool V1Ty = false,
                                          bool AllowBFloatArgsAndRet = true) {
  int IsQuad = TypeFlags.isQuad();
  switch (TypeFlags.getEltType()) {
  case NeonTypeFlags::Int8:
  case NeonTypeFlags::Poly8:
    return llvm::FixedVectorType::get(CGF->Int8Ty, V1Ty ? 1 : (8 << IsQuad));
  case NeonTypeFlags::Int16:
  case NeonTypeFlags::Poly16:
    return llvm::FixedVectorType::get(CGF->Int16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::BFloat16:
    if (AllowBFloatArgsAndRet)
      return llvm::FixedVectorType::get(CGF->BFloatTy, V1Ty ? 1 : (4 << IsQuad));
    else
      return llvm::FixedVectorType::get(CGF->Int16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::Float16:
    if (HasLegalHalfType)
      return llvm::FixedVectorType::get(CGF->HalfTy, V1Ty ? 1 : (4 << IsQuad));
    else
      return llvm::FixedVectorType::get(CGF->Int16Ty, V1Ty ? 1 : (4 << IsQuad));
  case NeonTypeFlags::Int32:
    return llvm::FixedVectorType::get(CGF->Int32Ty, V1Ty ? 1 : (2 << IsQuad));
  case NeonTypeFlags::Int64:
  case NeonTypeFlags::Poly64:
    return llvm::FixedVectorType::get(CGF->Int64Ty, V1Ty ? 1 : (1 << IsQuad));
  case NeonTypeFlags::Poly128:
    // FIXME: i128 and f128 doesn't get fully support in Clang and llvm.
    // There is a lot of i128 and f128 API missing.
    // so we use v16i8 to represent poly128 and get pattern matched.
    return llvm::FixedVectorType::get(CGF->Int8Ty, 16);
  case NeonTypeFlags::Float32:
    return llvm::FixedVectorType::get(CGF->FloatTy, V1Ty ? 1 : (2 << IsQuad));
  case NeonTypeFlags::Float64:
    return llvm::FixedVectorType::get(CGF->DoubleTy, V1Ty ? 1 : (1 << IsQuad));
  }
  llvm_unreachable("Unknown vector element type!");
}

static llvm::VectorType *GetFloatNeonType(CodeGenFunction *CGF,
                                          NeonTypeFlags IntTypeFlags) {
  int IsQuad = IntTypeFlags.isQuad();
  switch (IntTypeFlags.getEltType()) {
  case NeonTypeFlags::Int16:
    return llvm::FixedVectorType::get(CGF->HalfTy, (4 << IsQuad));
  case NeonTypeFlags::Int32:
    return llvm::FixedVectorType::get(CGF->FloatTy, (2 << IsQuad));
  case NeonTypeFlags::Int64:
    return llvm::FixedVectorType::get(CGF->DoubleTy, (1 << IsQuad));
  default:
    llvm_unreachable("Type can't be converted to floating-point!");
  }
}

Value *CodeGenFunction::EmitNeonSplat(Value *V, Constant *C,
                                      const ElementCount &Count) {
  Value *SV = llvm::ConstantVector::getSplat(Count, C);
  return Builder.CreateShuffleVector(V, V, SV, "lane");
}

Value *CodeGenFunction::EmitNeonSplat(Value *V, Constant *C) {
  ElementCount EC = cast<llvm::VectorType>(V->getType())->getElementCount();
  return EmitNeonSplat(V, C, EC);
}

Value *CodeGenFunction::EmitNeonCall(Function *F, SmallVectorImpl<Value*> &Ops,
                                     const char *name,
                                     unsigned shift, bool rightshift) {
  unsigned j = 0;
  for (Function::const_arg_iterator ai = F->arg_begin(), ae = F->arg_end();
       ai != ae; ++ai, ++j) {
    if (F->isConstrainedFPIntrinsic())
      if (ai->getType()->isMetadataTy())
        continue;
    if (shift > 0 && shift == j)
      Ops[j] = EmitNeonShiftVector(Ops[j], ai->getType(), rightshift);
    else
      Ops[j] = Builder.CreateBitCast(Ops[j], ai->getType(), name);
  }

  if (F->isConstrainedFPIntrinsic())
    return Builder.CreateConstrainedFPCall(F, Ops, name);
  else
    return Builder.CreateCall(F, Ops, name);
}

Value *CodeGenFunction::EmitNeonShiftVector(Value *V, llvm::Type *Ty,
                                            bool neg) {
  int SV = cast<ConstantInt>(V)->getSExtValue();
  return ConstantInt::get(Ty, neg ? -SV : SV);
}

// Right-shift a vector by a constant.
Value *CodeGenFunction::EmitNeonRShiftImm(Value *Vec, Value *Shift,
                                          llvm::Type *Ty, bool usgn,
                                          const char *name) {
  llvm::VectorType *VTy = cast<llvm::VectorType>(Ty);

  int ShiftAmt = cast<ConstantInt>(Shift)->getSExtValue();
  int EltSize = VTy->getScalarSizeInBits();

  Vec = Builder.CreateBitCast(Vec, Ty);

  // lshr/ashr are undefined when the shift amount is equal to the vector
  // element size.
  if (ShiftAmt == EltSize) {
    if (usgn) {
      // Right-shifting an unsigned value by its size yields 0.
      return llvm::ConstantAggregateZero::get(VTy);
    } else {
      // Right-shifting a signed value by its size is equivalent
      // to a shift of size-1.
      --ShiftAmt;
      Shift = ConstantInt::get(VTy->getElementType(), ShiftAmt);
    }
  }

  Shift = EmitNeonShiftVector(Shift, Ty, false);
  if (usgn)
    return Builder.CreateLShr(Vec, Shift, name);
  else
    return Builder.CreateAShr(Vec, Shift, name);
}

enum {
  AddRetType = (1 << 0),
  Add1ArgType = (1 << 1),
  Add2ArgTypes = (1 << 2),

  VectorizeRetType = (1 << 3),
  VectorizeArgTypes = (1 << 4),

  InventFloatType = (1 << 5),
  UnsignedAlts = (1 << 6),

  Use64BitVectors = (1 << 7),
  Use128BitVectors = (1 << 8),

  Vectorize1ArgType = Add1ArgType | VectorizeArgTypes,
  VectorRet = AddRetType | VectorizeRetType,
  VectorRetGetArgs01 =
      AddRetType | Add2ArgTypes | VectorizeRetType | VectorizeArgTypes,
  FpCmpzModifiers =
      AddRetType | VectorizeRetType | Add1ArgType | InventFloatType
};

namespace {
struct ARMVectorIntrinsicInfo {
  const char *NameHint;
  unsigned BuiltinID;
  unsigned LLVMIntrinsic;
  unsigned AltLLVMIntrinsic;
  uint64_t TypeModifier;

  bool operator<(unsigned RHSBuiltinID) const {
    return BuiltinID < RHSBuiltinID;
  }
  bool operator<(const ARMVectorIntrinsicInfo &TE) const {
    return BuiltinID < TE.BuiltinID;
  }
};
} // end anonymous namespace

#define NEONMAP0(NameBase) \
  { #NameBase, NEON::BI__builtin_neon_ ## NameBase, 0, 0, 0 }

#define NEONMAP1(NameBase, LLVMIntrinsic, TypeModifier) \
  { #NameBase, NEON:: BI__builtin_neon_ ## NameBase, \
      Intrinsic::LLVMIntrinsic, 0, TypeModifier }

#define NEONMAP2(NameBase, LLVMIntrinsic, AltLLVMIntrinsic, TypeModifier) \
  { #NameBase, NEON:: BI__builtin_neon_ ## NameBase, \
      Intrinsic::LLVMIntrinsic, Intrinsic::AltLLVMIntrinsic, \
      TypeModifier }

static const ARMVectorIntrinsicInfo ARMSIMDIntrinsicMap [] = {
  NEONMAP1(__a32_vcvt_bf16_f32, arm_neon_vcvtfp2bf, 0),
  NEONMAP0(splat_lane_v),
  NEONMAP0(splat_laneq_v),
  NEONMAP0(splatq_lane_v),
  NEONMAP0(splatq_laneq_v),
  NEONMAP2(vabd_v, arm_neon_vabdu, arm_neon_vabds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vabdq_v, arm_neon_vabdu, arm_neon_vabds, Add1ArgType | UnsignedAlts),
  NEONMAP1(vabs_v, arm_neon_vabs, 0),
  NEONMAP1(vabsq_v, arm_neon_vabs, 0),
  NEONMAP0(vadd_v),
  NEONMAP0(vaddhn_v),
  NEONMAP0(vaddq_v),
  NEONMAP1(vaesdq_u8, arm_neon_aesd, 0),
  NEONMAP1(vaeseq_u8, arm_neon_aese, 0),
  NEONMAP1(vaesimcq_u8, arm_neon_aesimc, 0),
  NEONMAP1(vaesmcq_u8, arm_neon_aesmc, 0),
  NEONMAP1(vbfdot_f32, arm_neon_bfdot, 0),
  NEONMAP1(vbfdotq_f32, arm_neon_bfdot, 0),
  NEONMAP1(vbfmlalbq_f32, arm_neon_bfmlalb, 0),
  NEONMAP1(vbfmlaltq_f32, arm_neon_bfmlalt, 0),
  NEONMAP1(vbfmmlaq_f32, arm_neon_bfmmla, 0),
  NEONMAP1(vbsl_v, arm_neon_vbsl, AddRetType),
  NEONMAP1(vbslq_v, arm_neon_vbsl, AddRetType),
  NEONMAP1(vcadd_rot270_f16, arm_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcadd_rot270_f32, arm_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcadd_rot90_f16, arm_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcadd_rot90_f32, arm_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcaddq_rot270_f16, arm_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcaddq_rot270_f32, arm_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcaddq_rot270_f64, arm_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcaddq_rot90_f16, arm_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcaddq_rot90_f32, arm_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcaddq_rot90_f64, arm_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcage_v, arm_neon_vacge, 0),
  NEONMAP1(vcageq_v, arm_neon_vacge, 0),
  NEONMAP1(vcagt_v, arm_neon_vacgt, 0),
  NEONMAP1(vcagtq_v, arm_neon_vacgt, 0),
  NEONMAP1(vcale_v, arm_neon_vacge, 0),
  NEONMAP1(vcaleq_v, arm_neon_vacge, 0),
  NEONMAP1(vcalt_v, arm_neon_vacgt, 0),
  NEONMAP1(vcaltq_v, arm_neon_vacgt, 0),
  NEONMAP0(vceqz_v),
  NEONMAP0(vceqzq_v),
  NEONMAP0(vcgez_v),
  NEONMAP0(vcgezq_v),
  NEONMAP0(vcgtz_v),
  NEONMAP0(vcgtzq_v),
  NEONMAP0(vclez_v),
  NEONMAP0(vclezq_v),
  NEONMAP1(vcls_v, arm_neon_vcls, Add1ArgType),
  NEONMAP1(vclsq_v, arm_neon_vcls, Add1ArgType),
  NEONMAP0(vcltz_v),
  NEONMAP0(vcltzq_v),
  NEONMAP1(vclz_v, ctlz, Add1ArgType),
  NEONMAP1(vclzq_v, ctlz, Add1ArgType),
  NEONMAP1(vcnt_v, ctpop, Add1ArgType),
  NEONMAP1(vcntq_v, ctpop, Add1ArgType),
  NEONMAP1(vcvt_f16_f32, arm_neon_vcvtfp2hf, 0),
  NEONMAP0(vcvt_f16_s16),
  NEONMAP0(vcvt_f16_u16),
  NEONMAP1(vcvt_f32_f16, arm_neon_vcvthf2fp, 0),
  NEONMAP0(vcvt_f32_v),
  NEONMAP1(vcvt_n_f16_s16, arm_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvt_n_f16_u16, arm_neon_vcvtfxu2fp, 0),
  NEONMAP2(vcvt_n_f32_v, arm_neon_vcvtfxu2fp, arm_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvt_n_s16_f16, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_s32_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_s64_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_u16_f16, arm_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvt_n_u32_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvt_n_u64_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP0(vcvt_s16_f16),
  NEONMAP0(vcvt_s32_v),
  NEONMAP0(vcvt_s64_v),
  NEONMAP0(vcvt_u16_f16),
  NEONMAP0(vcvt_u32_v),
  NEONMAP0(vcvt_u64_v),
  NEONMAP1(vcvta_s16_f16, arm_neon_vcvtas, 0),
  NEONMAP1(vcvta_s32_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvta_s64_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvta_u16_f16, arm_neon_vcvtau, 0),
  NEONMAP1(vcvta_u32_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvta_u64_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvtaq_s16_f16, arm_neon_vcvtas, 0),
  NEONMAP1(vcvtaq_s32_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvtaq_s64_v, arm_neon_vcvtas, 0),
  NEONMAP1(vcvtaq_u16_f16, arm_neon_vcvtau, 0),
  NEONMAP1(vcvtaq_u32_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvtaq_u64_v, arm_neon_vcvtau, 0),
  NEONMAP1(vcvth_bf16_f32, arm_neon_vcvtbfp2bf, 0),
  NEONMAP1(vcvtm_s16_f16, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtm_s32_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtm_s64_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtm_u16_f16, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtm_u32_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtm_u64_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtmq_s16_f16, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtmq_s32_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtmq_s64_v, arm_neon_vcvtms, 0),
  NEONMAP1(vcvtmq_u16_f16, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtmq_u32_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtmq_u64_v, arm_neon_vcvtmu, 0),
  NEONMAP1(vcvtn_s16_f16, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtn_s32_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtn_s64_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtn_u16_f16, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtn_u32_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtn_u64_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtnq_s16_f16, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtnq_s32_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtnq_s64_v, arm_neon_vcvtns, 0),
  NEONMAP1(vcvtnq_u16_f16, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtnq_u32_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtnq_u64_v, arm_neon_vcvtnu, 0),
  NEONMAP1(vcvtp_s16_f16, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtp_s32_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtp_s64_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtp_u16_f16, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtp_u32_v, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtp_u64_v, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtpq_s16_f16, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtpq_s32_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtpq_s64_v, arm_neon_vcvtps, 0),
  NEONMAP1(vcvtpq_u16_f16, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtpq_u32_v, arm_neon_vcvtpu, 0),
  NEONMAP1(vcvtpq_u64_v, arm_neon_vcvtpu, 0),
  NEONMAP0(vcvtq_f16_s16),
  NEONMAP0(vcvtq_f16_u16),
  NEONMAP0(vcvtq_f32_v),
  NEONMAP1(vcvtq_n_f16_s16, arm_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvtq_n_f16_u16, arm_neon_vcvtfxu2fp, 0),
  NEONMAP2(vcvtq_n_f32_v, arm_neon_vcvtfxu2fp, arm_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvtq_n_s16_f16, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_s32_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_s64_v, arm_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_u16_f16, arm_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtq_n_u32_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtq_n_u64_v, arm_neon_vcvtfp2fxu, 0),
  NEONMAP0(vcvtq_s16_f16),
  NEONMAP0(vcvtq_s32_v),
  NEONMAP0(vcvtq_s64_v),
  NEONMAP0(vcvtq_u16_f16),
  NEONMAP0(vcvtq_u32_v),
  NEONMAP0(vcvtq_u64_v),
  NEONMAP1(vdot_s32, arm_neon_sdot, 0),
  NEONMAP1(vdot_u32, arm_neon_udot, 0),
  NEONMAP1(vdotq_s32, arm_neon_sdot, 0),
  NEONMAP1(vdotq_u32, arm_neon_udot, 0),
  NEONMAP0(vext_v),
  NEONMAP0(vextq_v),
  NEONMAP0(vfma_v),
  NEONMAP0(vfmaq_v),
  NEONMAP2(vhadd_v, arm_neon_vhaddu, arm_neon_vhadds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhaddq_v, arm_neon_vhaddu, arm_neon_vhadds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhsub_v, arm_neon_vhsubu, arm_neon_vhsubs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhsubq_v, arm_neon_vhsubu, arm_neon_vhsubs, Add1ArgType | UnsignedAlts),
  NEONMAP0(vld1_dup_v),
  NEONMAP1(vld1_v, arm_neon_vld1, 0),
  NEONMAP1(vld1_x2_v, arm_neon_vld1x2, 0),
  NEONMAP1(vld1_x3_v, arm_neon_vld1x3, 0),
  NEONMAP1(vld1_x4_v, arm_neon_vld1x4, 0),
  NEONMAP0(vld1q_dup_v),
  NEONMAP1(vld1q_v, arm_neon_vld1, 0),
  NEONMAP1(vld1q_x2_v, arm_neon_vld1x2, 0),
  NEONMAP1(vld1q_x3_v, arm_neon_vld1x3, 0),
  NEONMAP1(vld1q_x4_v, arm_neon_vld1x4, 0),
  NEONMAP1(vld2_dup_v, arm_neon_vld2dup, 0),
  NEONMAP1(vld2_lane_v, arm_neon_vld2lane, 0),
  NEONMAP1(vld2_v, arm_neon_vld2, 0),
  NEONMAP1(vld2q_dup_v, arm_neon_vld2dup, 0),
  NEONMAP1(vld2q_lane_v, arm_neon_vld2lane, 0),
  NEONMAP1(vld2q_v, arm_neon_vld2, 0),
  NEONMAP1(vld3_dup_v, arm_neon_vld3dup, 0),
  NEONMAP1(vld3_lane_v, arm_neon_vld3lane, 0),
  NEONMAP1(vld3_v, arm_neon_vld3, 0),
  NEONMAP1(vld3q_dup_v, arm_neon_vld3dup, 0),
  NEONMAP1(vld3q_lane_v, arm_neon_vld3lane, 0),
  NEONMAP1(vld3q_v, arm_neon_vld3, 0),
  NEONMAP1(vld4_dup_v, arm_neon_vld4dup, 0),
  NEONMAP1(vld4_lane_v, arm_neon_vld4lane, 0),
  NEONMAP1(vld4_v, arm_neon_vld4, 0),
  NEONMAP1(vld4q_dup_v, arm_neon_vld4dup, 0),
  NEONMAP1(vld4q_lane_v, arm_neon_vld4lane, 0),
  NEONMAP1(vld4q_v, arm_neon_vld4, 0),
  NEONMAP2(vmax_v, arm_neon_vmaxu, arm_neon_vmaxs, Add1ArgType | UnsignedAlts),
  NEONMAP1(vmaxnm_v, arm_neon_vmaxnm, Add1ArgType),
  NEONMAP1(vmaxnmq_v, arm_neon_vmaxnm, Add1ArgType),
  NEONMAP2(vmaxq_v, arm_neon_vmaxu, arm_neon_vmaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vmin_v, arm_neon_vminu, arm_neon_vmins, Add1ArgType | UnsignedAlts),
  NEONMAP1(vminnm_v, arm_neon_vminnm, Add1ArgType),
  NEONMAP1(vminnmq_v, arm_neon_vminnm, Add1ArgType),
  NEONMAP2(vminq_v, arm_neon_vminu, arm_neon_vmins, Add1ArgType | UnsignedAlts),
  NEONMAP1(vmmlaq_s32, arm_neon_smmla, 0),
  NEONMAP1(vmmlaq_u32, arm_neon_ummla, 0),
  NEONMAP0(vmovl_v),
  NEONMAP0(vmovn_v),
  NEONMAP1(vmul_v, arm_neon_vmulp, Add1ArgType),
  NEONMAP0(vmull_v),
  NEONMAP1(vmulq_v, arm_neon_vmulp, Add1ArgType),
  NEONMAP2(vpadal_v, arm_neon_vpadalu, arm_neon_vpadals, UnsignedAlts),
  NEONMAP2(vpadalq_v, arm_neon_vpadalu, arm_neon_vpadals, UnsignedAlts),
  NEONMAP1(vpadd_v, arm_neon_vpadd, Add1ArgType),
  NEONMAP2(vpaddl_v, arm_neon_vpaddlu, arm_neon_vpaddls, UnsignedAlts),
  NEONMAP2(vpaddlq_v, arm_neon_vpaddlu, arm_neon_vpaddls, UnsignedAlts),
  NEONMAP1(vpaddq_v, arm_neon_vpadd, Add1ArgType),
  NEONMAP2(vpmax_v, arm_neon_vpmaxu, arm_neon_vpmaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vpmin_v, arm_neon_vpminu, arm_neon_vpmins, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqabs_v, arm_neon_vqabs, Add1ArgType),
  NEONMAP1(vqabsq_v, arm_neon_vqabs, Add1ArgType),
  NEONMAP2(vqadd_v, uadd_sat, sadd_sat, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqaddq_v, uadd_sat, sadd_sat, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqdmlal_v, arm_neon_vqdmull, sadd_sat, 0),
  NEONMAP2(vqdmlsl_v, arm_neon_vqdmull, ssub_sat, 0),
  NEONMAP1(vqdmulh_v, arm_neon_vqdmulh, Add1ArgType),
  NEONMAP1(vqdmulhq_v, arm_neon_vqdmulh, Add1ArgType),
  NEONMAP1(vqdmull_v, arm_neon_vqdmull, Add1ArgType),
  NEONMAP2(vqmovn_v, arm_neon_vqmovnu, arm_neon_vqmovns, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqmovun_v, arm_neon_vqmovnsu, Add1ArgType),
  NEONMAP1(vqneg_v, arm_neon_vqneg, Add1ArgType),
  NEONMAP1(vqnegq_v, arm_neon_vqneg, Add1ArgType),
  NEONMAP1(vqrdmlah_s16, arm_neon_vqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlah_s32, arm_neon_vqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlahq_s16, arm_neon_vqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlahq_s32, arm_neon_vqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlsh_s16, arm_neon_vqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmlsh_s32, arm_neon_vqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmlshq_s16, arm_neon_vqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmlshq_s32, arm_neon_vqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmulh_v, arm_neon_vqrdmulh, Add1ArgType),
  NEONMAP1(vqrdmulhq_v, arm_neon_vqrdmulh, Add1ArgType),
  NEONMAP2(vqrshl_v, arm_neon_vqrshiftu, arm_neon_vqrshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqrshlq_v, arm_neon_vqrshiftu, arm_neon_vqrshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqshl_n_v, arm_neon_vqshiftu, arm_neon_vqshifts, UnsignedAlts),
  NEONMAP2(vqshl_v, arm_neon_vqshiftu, arm_neon_vqshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqshlq_n_v, arm_neon_vqshiftu, arm_neon_vqshifts, UnsignedAlts),
  NEONMAP2(vqshlq_v, arm_neon_vqshiftu, arm_neon_vqshifts, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqshlu_n_v, arm_neon_vqshiftsu, 0),
  NEONMAP1(vqshluq_n_v, arm_neon_vqshiftsu, 0),
  NEONMAP2(vqsub_v, usub_sat, ssub_sat, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqsubq_v, usub_sat, ssub_sat, Add1ArgType | UnsignedAlts),
  NEONMAP1(vraddhn_v, arm_neon_vraddhn, Add1ArgType),
  NEONMAP2(vrecpe_v, arm_neon_vrecpe, arm_neon_vrecpe, 0),
  NEONMAP2(vrecpeq_v, arm_neon_vrecpe, arm_neon_vrecpe, 0),
  NEONMAP1(vrecps_v, arm_neon_vrecps, Add1ArgType),
  NEONMAP1(vrecpsq_v, arm_neon_vrecps, Add1ArgType),
  NEONMAP2(vrhadd_v, arm_neon_vrhaddu, arm_neon_vrhadds, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrhaddq_v, arm_neon_vrhaddu, arm_neon_vrhadds, Add1ArgType | UnsignedAlts),
  NEONMAP1(vrnd_v, arm_neon_vrintz, Add1ArgType),
  NEONMAP1(vrnda_v, arm_neon_vrinta, Add1ArgType),
  NEONMAP1(vrndaq_v, arm_neon_vrinta, Add1ArgType),
  NEONMAP0(vrndi_v),
  NEONMAP0(vrndiq_v),
  NEONMAP1(vrndm_v, arm_neon_vrintm, Add1ArgType),
  NEONMAP1(vrndmq_v, arm_neon_vrintm, Add1ArgType),
  NEONMAP1(vrndn_v, arm_neon_vrintn, Add1ArgType),
  NEONMAP1(vrndnq_v, arm_neon_vrintn, Add1ArgType),
  NEONMAP1(vrndp_v, arm_neon_vrintp, Add1ArgType),
  NEONMAP1(vrndpq_v, arm_neon_vrintp, Add1ArgType),
  NEONMAP1(vrndq_v, arm_neon_vrintz, Add1ArgType),
  NEONMAP1(vrndx_v, arm_neon_vrintx, Add1ArgType),
  NEONMAP1(vrndxq_v, arm_neon_vrintx, Add1ArgType),
  NEONMAP2(vrshl_v, arm_neon_vrshiftu, arm_neon_vrshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrshlq_v, arm_neon_vrshiftu, arm_neon_vrshifts, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrshr_n_v, arm_neon_vrshiftu, arm_neon_vrshifts, UnsignedAlts),
  NEONMAP2(vrshrq_n_v, arm_neon_vrshiftu, arm_neon_vrshifts, UnsignedAlts),
  NEONMAP2(vrsqrte_v, arm_neon_vrsqrte, arm_neon_vrsqrte, 0),
  NEONMAP2(vrsqrteq_v, arm_neon_vrsqrte, arm_neon_vrsqrte, 0),
  NEONMAP1(vrsqrts_v, arm_neon_vrsqrts, Add1ArgType),
  NEONMAP1(vrsqrtsq_v, arm_neon_vrsqrts, Add1ArgType),
  NEONMAP1(vrsubhn_v, arm_neon_vrsubhn, Add1ArgType),
  NEONMAP1(vsha1su0q_u32, arm_neon_sha1su0, 0),
  NEONMAP1(vsha1su1q_u32, arm_neon_sha1su1, 0),
  NEONMAP1(vsha256h2q_u32, arm_neon_sha256h2, 0),
  NEONMAP1(vsha256hq_u32, arm_neon_sha256h, 0),
  NEONMAP1(vsha256su0q_u32, arm_neon_sha256su0, 0),
  NEONMAP1(vsha256su1q_u32, arm_neon_sha256su1, 0),
  NEONMAP0(vshl_n_v),
  NEONMAP2(vshl_v, arm_neon_vshiftu, arm_neon_vshifts, Add1ArgType | UnsignedAlts),
  NEONMAP0(vshll_n_v),
  NEONMAP0(vshlq_n_v),
  NEONMAP2(vshlq_v, arm_neon_vshiftu, arm_neon_vshifts, Add1ArgType | UnsignedAlts),
  NEONMAP0(vshr_n_v),
  NEONMAP0(vshrn_n_v),
  NEONMAP0(vshrq_n_v),
  NEONMAP1(vst1_v, arm_neon_vst1, 0),
  NEONMAP1(vst1_x2_v, arm_neon_vst1x2, 0),
  NEONMAP1(vst1_x3_v, arm_neon_vst1x3, 0),
  NEONMAP1(vst1_x4_v, arm_neon_vst1x4, 0),
  NEONMAP1(vst1q_v, arm_neon_vst1, 0),
  NEONMAP1(vst1q_x2_v, arm_neon_vst1x2, 0),
  NEONMAP1(vst1q_x3_v, arm_neon_vst1x3, 0),
  NEONMAP1(vst1q_x4_v, arm_neon_vst1x4, 0),
  NEONMAP1(vst2_lane_v, arm_neon_vst2lane, 0),
  NEONMAP1(vst2_v, arm_neon_vst2, 0),
  NEONMAP1(vst2q_lane_v, arm_neon_vst2lane, 0),
  NEONMAP1(vst2q_v, arm_neon_vst2, 0),
  NEONMAP1(vst3_lane_v, arm_neon_vst3lane, 0),
  NEONMAP1(vst3_v, arm_neon_vst3, 0),
  NEONMAP1(vst3q_lane_v, arm_neon_vst3lane, 0),
  NEONMAP1(vst3q_v, arm_neon_vst3, 0),
  NEONMAP1(vst4_lane_v, arm_neon_vst4lane, 0),
  NEONMAP1(vst4_v, arm_neon_vst4, 0),
  NEONMAP1(vst4q_lane_v, arm_neon_vst4lane, 0),
  NEONMAP1(vst4q_v, arm_neon_vst4, 0),
  NEONMAP0(vsubhn_v),
  NEONMAP0(vtrn_v),
  NEONMAP0(vtrnq_v),
  NEONMAP0(vtst_v),
  NEONMAP0(vtstq_v),
  NEONMAP1(vusdot_s32, arm_neon_usdot, 0),
  NEONMAP1(vusdotq_s32, arm_neon_usdot, 0),
  NEONMAP1(vusmmlaq_s32, arm_neon_usmmla, 0),
  NEONMAP0(vuzp_v),
  NEONMAP0(vuzpq_v),
  NEONMAP0(vzip_v),
  NEONMAP0(vzipq_v)
};

static const ARMVectorIntrinsicInfo AArch64SIMDIntrinsicMap[] = {
  NEONMAP1(__a64_vcvtq_low_bf16_f32, aarch64_neon_bfcvtn, 0),
  NEONMAP0(splat_lane_v),
  NEONMAP0(splat_laneq_v),
  NEONMAP0(splatq_lane_v),
  NEONMAP0(splatq_laneq_v),
  NEONMAP1(vabs_v, aarch64_neon_abs, 0),
  NEONMAP1(vabsq_v, aarch64_neon_abs, 0),
  NEONMAP0(vadd_v),
  NEONMAP0(vaddhn_v),
  NEONMAP0(vaddq_p128),
  NEONMAP0(vaddq_v),
  NEONMAP1(vaesdq_u8, aarch64_crypto_aesd, 0),
  NEONMAP1(vaeseq_u8, aarch64_crypto_aese, 0),
  NEONMAP1(vaesimcq_u8, aarch64_crypto_aesimc, 0),
  NEONMAP1(vaesmcq_u8, aarch64_crypto_aesmc, 0),
  NEONMAP2(vbcaxq_s16, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vbcaxq_s32, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vbcaxq_s64, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vbcaxq_s8, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vbcaxq_u16, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vbcaxq_u32, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vbcaxq_u64, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs, Add1ArgType | UnsignedAlts),
  NEONMAP2(vbcaxq_u8, aarch64_crypto_bcaxu, aarch64_crypto_bcaxs, Add1ArgType | UnsignedAlts),
  NEONMAP1(vbfdot_f32, aarch64_neon_bfdot, 0),
  NEONMAP1(vbfdotq_f32, aarch64_neon_bfdot, 0),
  NEONMAP1(vbfmlalbq_f32, aarch64_neon_bfmlalb, 0),
  NEONMAP1(vbfmlaltq_f32, aarch64_neon_bfmlalt, 0),
  NEONMAP1(vbfmmlaq_f32, aarch64_neon_bfmmla, 0),
  NEONMAP1(vcadd_rot270_f16, aarch64_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcadd_rot270_f32, aarch64_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcadd_rot90_f16, aarch64_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcadd_rot90_f32, aarch64_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcaddq_rot270_f16, aarch64_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcaddq_rot270_f32, aarch64_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcaddq_rot270_f64, aarch64_neon_vcadd_rot270, Add1ArgType),
  NEONMAP1(vcaddq_rot90_f16, aarch64_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcaddq_rot90_f32, aarch64_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcaddq_rot90_f64, aarch64_neon_vcadd_rot90, Add1ArgType),
  NEONMAP1(vcage_v, aarch64_neon_facge, 0),
  NEONMAP1(vcageq_v, aarch64_neon_facge, 0),
  NEONMAP1(vcagt_v, aarch64_neon_facgt, 0),
  NEONMAP1(vcagtq_v, aarch64_neon_facgt, 0),
  NEONMAP1(vcale_v, aarch64_neon_facge, 0),
  NEONMAP1(vcaleq_v, aarch64_neon_facge, 0),
  NEONMAP1(vcalt_v, aarch64_neon_facgt, 0),
  NEONMAP1(vcaltq_v, aarch64_neon_facgt, 0),
  NEONMAP0(vceqz_v),
  NEONMAP0(vceqzq_v),
  NEONMAP0(vcgez_v),
  NEONMAP0(vcgezq_v),
  NEONMAP0(vcgtz_v),
  NEONMAP0(vcgtzq_v),
  NEONMAP0(vclez_v),
  NEONMAP0(vclezq_v),
  NEONMAP1(vcls_v, aarch64_neon_cls, Add1ArgType),
  NEONMAP1(vclsq_v, aarch64_neon_cls, Add1ArgType),
  NEONMAP0(vcltz_v),
  NEONMAP0(vcltzq_v),
  NEONMAP1(vclz_v, ctlz, Add1ArgType),
  NEONMAP1(vclzq_v, ctlz, Add1ArgType),
  NEONMAP1(vcmla_f16, aarch64_neon_vcmla_rot0, Add1ArgType),
  NEONMAP1(vcmla_f32, aarch64_neon_vcmla_rot0, Add1ArgType),
  NEONMAP1(vcmla_rot180_f16, aarch64_neon_vcmla_rot180, Add1ArgType),
  NEONMAP1(vcmla_rot180_f32, aarch64_neon_vcmla_rot180, Add1ArgType),
  NEONMAP1(vcmla_rot270_f16, aarch64_neon_vcmla_rot270, Add1ArgType),
  NEONMAP1(vcmla_rot270_f32, aarch64_neon_vcmla_rot270, Add1ArgType),
  NEONMAP1(vcmla_rot90_f16, aarch64_neon_vcmla_rot90, Add1ArgType),
  NEONMAP1(vcmla_rot90_f32, aarch64_neon_vcmla_rot90, Add1ArgType),
  NEONMAP1(vcmlaq_f16, aarch64_neon_vcmla_rot0, Add1ArgType),
  NEONMAP1(vcmlaq_f32, aarch64_neon_vcmla_rot0, Add1ArgType),
  NEONMAP1(vcmlaq_f64, aarch64_neon_vcmla_rot0, Add1ArgType),
  NEONMAP1(vcmlaq_rot180_f16, aarch64_neon_vcmla_rot180, Add1ArgType),
  NEONMAP1(vcmlaq_rot180_f32, aarch64_neon_vcmla_rot180, Add1ArgType),
  NEONMAP1(vcmlaq_rot180_f64, aarch64_neon_vcmla_rot180, Add1ArgType),
  NEONMAP1(vcmlaq_rot270_f16, aarch64_neon_vcmla_rot270, Add1ArgType),
  NEONMAP1(vcmlaq_rot270_f32, aarch64_neon_vcmla_rot270, Add1ArgType),
  NEONMAP1(vcmlaq_rot270_f64, aarch64_neon_vcmla_rot270, Add1ArgType),
  NEONMAP1(vcmlaq_rot90_f16, aarch64_neon_vcmla_rot90, Add1ArgType),
  NEONMAP1(vcmlaq_rot90_f32, aarch64_neon_vcmla_rot90, Add1ArgType),
  NEONMAP1(vcmlaq_rot90_f64, aarch64_neon_vcmla_rot90, Add1ArgType),
  NEONMAP1(vcnt_v, ctpop, Add1ArgType),
  NEONMAP1(vcntq_v, ctpop, Add1ArgType),
  NEONMAP1(vcvt_f16_f32, aarch64_neon_vcvtfp2hf, 0),
  NEONMAP0(vcvt_f16_s16),
  NEONMAP0(vcvt_f16_u16),
  NEONMAP1(vcvt_f32_f16, aarch64_neon_vcvthf2fp, 0),
  NEONMAP0(vcvt_f32_v),
  NEONMAP1(vcvt_n_f16_s16, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvt_n_f16_u16, aarch64_neon_vcvtfxu2fp, 0),
  NEONMAP2(vcvt_n_f32_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP2(vcvt_n_f64_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvt_n_s16_f16, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_s32_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_s64_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvt_n_u16_f16, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvt_n_u32_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvt_n_u64_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP0(vcvtq_f16_s16),
  NEONMAP0(vcvtq_f16_u16),
  NEONMAP0(vcvtq_f32_v),
  NEONMAP1(vcvtq_high_bf16_f32, aarch64_neon_bfcvtn2, 0),
  NEONMAP1(vcvtq_n_f16_s16, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvtq_n_f16_u16, aarch64_neon_vcvtfxu2fp, 0),
  NEONMAP2(vcvtq_n_f32_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP2(vcvtq_n_f64_v, aarch64_neon_vcvtfxu2fp, aarch64_neon_vcvtfxs2fp, 0),
  NEONMAP1(vcvtq_n_s16_f16, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_s32_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_s64_v, aarch64_neon_vcvtfp2fxs, 0),
  NEONMAP1(vcvtq_n_u16_f16, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtq_n_u32_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtq_n_u64_v, aarch64_neon_vcvtfp2fxu, 0),
  NEONMAP1(vcvtx_f32_v, aarch64_neon_fcvtxn, AddRetType | Add1ArgType),
  NEONMAP1(vdot_s32, aarch64_neon_sdot, 0),
  NEONMAP1(vdot_u32, aarch64_neon_udot, 0),
  NEONMAP1(vdotq_s32, aarch64_neon_sdot, 0),
  NEONMAP1(vdotq_u32, aarch64_neon_udot, 0),
  NEONMAP2(veor3q_s16, aarch64_crypto_eor3u, aarch64_crypto_eor3s, Add1ArgType | UnsignedAlts),
  NEONMAP2(veor3q_s32, aarch64_crypto_eor3u, aarch64_crypto_eor3s, Add1ArgType | UnsignedAlts),
  NEONMAP2(veor3q_s64, aarch64_crypto_eor3u, aarch64_crypto_eor3s, Add1ArgType | UnsignedAlts),
  NEONMAP2(veor3q_s8, aarch64_crypto_eor3u, aarch64_crypto_eor3s, Add1ArgType | UnsignedAlts),
  NEONMAP2(veor3q_u16, aarch64_crypto_eor3u, aarch64_crypto_eor3s, Add1ArgType | UnsignedAlts),
  NEONMAP2(veor3q_u32, aarch64_crypto_eor3u, aarch64_crypto_eor3s, Add1ArgType | UnsignedAlts),
  NEONMAP2(veor3q_u64, aarch64_crypto_eor3u, aarch64_crypto_eor3s, Add1ArgType | UnsignedAlts),
  NEONMAP2(veor3q_u8, aarch64_crypto_eor3u, aarch64_crypto_eor3s, Add1ArgType | UnsignedAlts),
  NEONMAP0(vext_v),
  NEONMAP0(vextq_v),
  NEONMAP0(vfma_v),
  NEONMAP0(vfmaq_v),
  NEONMAP1(vfmlal_high_f16, aarch64_neon_fmlal2, 0),
  NEONMAP1(vfmlal_low_f16, aarch64_neon_fmlal, 0),
  NEONMAP1(vfmlalq_high_f16, aarch64_neon_fmlal2, 0),
  NEONMAP1(vfmlalq_low_f16, aarch64_neon_fmlal, 0),
  NEONMAP1(vfmlsl_high_f16, aarch64_neon_fmlsl2, 0),
  NEONMAP1(vfmlsl_low_f16, aarch64_neon_fmlsl, 0),
  NEONMAP1(vfmlslq_high_f16, aarch64_neon_fmlsl2, 0),
  NEONMAP1(vfmlslq_low_f16, aarch64_neon_fmlsl, 0),
  NEONMAP2(vhadd_v, aarch64_neon_uhadd, aarch64_neon_shadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhaddq_v, aarch64_neon_uhadd, aarch64_neon_shadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhsub_v, aarch64_neon_uhsub, aarch64_neon_shsub, Add1ArgType | UnsignedAlts),
  NEONMAP2(vhsubq_v, aarch64_neon_uhsub, aarch64_neon_shsub, Add1ArgType | UnsignedAlts),
  NEONMAP1(vld1_x2_v, aarch64_neon_ld1x2, 0),
  NEONMAP1(vld1_x3_v, aarch64_neon_ld1x3, 0),
  NEONMAP1(vld1_x4_v, aarch64_neon_ld1x4, 0),
  NEONMAP1(vld1q_x2_v, aarch64_neon_ld1x2, 0),
  NEONMAP1(vld1q_x3_v, aarch64_neon_ld1x3, 0),
  NEONMAP1(vld1q_x4_v, aarch64_neon_ld1x4, 0),
  NEONMAP1(vmmlaq_s32, aarch64_neon_smmla, 0),
  NEONMAP1(vmmlaq_u32, aarch64_neon_ummla, 0),
  NEONMAP0(vmovl_v),
  NEONMAP0(vmovn_v),
  NEONMAP1(vmul_v, aarch64_neon_pmul, Add1ArgType),
  NEONMAP1(vmulq_v, aarch64_neon_pmul, Add1ArgType),
  NEONMAP1(vpadd_v, aarch64_neon_addp, Add1ArgType),
  NEONMAP2(vpaddl_v, aarch64_neon_uaddlp, aarch64_neon_saddlp, UnsignedAlts),
  NEONMAP2(vpaddlq_v, aarch64_neon_uaddlp, aarch64_neon_saddlp, UnsignedAlts),
  NEONMAP1(vpaddq_v, aarch64_neon_addp, Add1ArgType),
  NEONMAP1(vqabs_v, aarch64_neon_sqabs, Add1ArgType),
  NEONMAP1(vqabsq_v, aarch64_neon_sqabs, Add1ArgType),
  NEONMAP2(vqadd_v, aarch64_neon_uqadd, aarch64_neon_sqadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqaddq_v, aarch64_neon_uqadd, aarch64_neon_sqadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqdmlal_v, aarch64_neon_sqdmull, aarch64_neon_sqadd, 0),
  NEONMAP2(vqdmlsl_v, aarch64_neon_sqdmull, aarch64_neon_sqsub, 0),
  NEONMAP1(vqdmulh_lane_v, aarch64_neon_sqdmulh_lane, 0),
  NEONMAP1(vqdmulh_laneq_v, aarch64_neon_sqdmulh_laneq, 0),
  NEONMAP1(vqdmulh_v, aarch64_neon_sqdmulh, Add1ArgType),
  NEONMAP1(vqdmulhq_lane_v, aarch64_neon_sqdmulh_lane, 0),
  NEONMAP1(vqdmulhq_laneq_v, aarch64_neon_sqdmulh_laneq, 0),
  NEONMAP1(vqdmulhq_v, aarch64_neon_sqdmulh, Add1ArgType),
  NEONMAP1(vqdmull_v, aarch64_neon_sqdmull, Add1ArgType),
  NEONMAP2(vqmovn_v, aarch64_neon_uqxtn, aarch64_neon_sqxtn, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqmovun_v, aarch64_neon_sqxtun, Add1ArgType),
  NEONMAP1(vqneg_v, aarch64_neon_sqneg, Add1ArgType),
  NEONMAP1(vqnegq_v, aarch64_neon_sqneg, Add1ArgType),
  NEONMAP1(vqrdmlah_s16, aarch64_neon_sqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlah_s32, aarch64_neon_sqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlahq_s16, aarch64_neon_sqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlahq_s32, aarch64_neon_sqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlsh_s16, aarch64_neon_sqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmlsh_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmlshq_s16, aarch64_neon_sqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmlshq_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmulh_lane_v, aarch64_neon_sqrdmulh_lane, 0),
  NEONMAP1(vqrdmulh_laneq_v, aarch64_neon_sqrdmulh_laneq, 0),
  NEONMAP1(vqrdmulh_v, aarch64_neon_sqrdmulh, Add1ArgType),
  NEONMAP1(vqrdmulhq_lane_v, aarch64_neon_sqrdmulh_lane, 0),
  NEONMAP1(vqrdmulhq_laneq_v, aarch64_neon_sqrdmulh_laneq, 0),
  NEONMAP1(vqrdmulhq_v, aarch64_neon_sqrdmulh, Add1ArgType),
  NEONMAP2(vqrshl_v, aarch64_neon_uqrshl, aarch64_neon_sqrshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqrshlq_v, aarch64_neon_uqrshl, aarch64_neon_sqrshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqshl_n_v, aarch64_neon_uqshl, aarch64_neon_sqshl, UnsignedAlts),
  NEONMAP2(vqshl_v, aarch64_neon_uqshl, aarch64_neon_sqshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqshlq_n_v, aarch64_neon_uqshl, aarch64_neon_sqshl,UnsignedAlts),
  NEONMAP2(vqshlq_v, aarch64_neon_uqshl, aarch64_neon_sqshl, Add1ArgType | UnsignedAlts),
  NEONMAP1(vqshlu_n_v, aarch64_neon_sqshlu, 0),
  NEONMAP1(vqshluq_n_v, aarch64_neon_sqshlu, 0),
  NEONMAP2(vqsub_v, aarch64_neon_uqsub, aarch64_neon_sqsub, Add1ArgType | UnsignedAlts),
  NEONMAP2(vqsubq_v, aarch64_neon_uqsub, aarch64_neon_sqsub, Add1ArgType | UnsignedAlts),
  NEONMAP1(vraddhn_v, aarch64_neon_raddhn, Add1ArgType),
  NEONMAP1(vrax1q_u64, aarch64_crypto_rax1, 0),
  NEONMAP2(vrecpe_v, aarch64_neon_frecpe, aarch64_neon_urecpe, 0),
  NEONMAP2(vrecpeq_v, aarch64_neon_frecpe, aarch64_neon_urecpe, 0),
  NEONMAP1(vrecps_v, aarch64_neon_frecps, Add1ArgType),
  NEONMAP1(vrecpsq_v, aarch64_neon_frecps, Add1ArgType),
  NEONMAP2(vrhadd_v, aarch64_neon_urhadd, aarch64_neon_srhadd, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrhaddq_v, aarch64_neon_urhadd, aarch64_neon_srhadd, Add1ArgType | UnsignedAlts),
  NEONMAP1(vrnd32x_f32, aarch64_neon_frint32x, Add1ArgType),
  NEONMAP1(vrnd32x_f64, aarch64_neon_frint32x, Add1ArgType),
  NEONMAP1(vrnd32xq_f32, aarch64_neon_frint32x, Add1ArgType),
  NEONMAP1(vrnd32xq_f64, aarch64_neon_frint32x, Add1ArgType),
  NEONMAP1(vrnd32z_f32, aarch64_neon_frint32z, Add1ArgType),
  NEONMAP1(vrnd32z_f64, aarch64_neon_frint32z, Add1ArgType),
  NEONMAP1(vrnd32zq_f32, aarch64_neon_frint32z, Add1ArgType),
  NEONMAP1(vrnd32zq_f64, aarch64_neon_frint32z, Add1ArgType),
  NEONMAP1(vrnd64x_f32, aarch64_neon_frint64x, Add1ArgType),
  NEONMAP1(vrnd64x_f64, aarch64_neon_frint64x, Add1ArgType),
  NEONMAP1(vrnd64xq_f32, aarch64_neon_frint64x, Add1ArgType),
  NEONMAP1(vrnd64xq_f64, aarch64_neon_frint64x, Add1ArgType),
  NEONMAP1(vrnd64z_f32, aarch64_neon_frint64z, Add1ArgType),
  NEONMAP1(vrnd64z_f64, aarch64_neon_frint64z, Add1ArgType),
  NEONMAP1(vrnd64zq_f32, aarch64_neon_frint64z, Add1ArgType),
  NEONMAP1(vrnd64zq_f64, aarch64_neon_frint64z, Add1ArgType),
  NEONMAP0(vrndi_v),
  NEONMAP0(vrndiq_v),
  NEONMAP2(vrshl_v, aarch64_neon_urshl, aarch64_neon_srshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrshlq_v, aarch64_neon_urshl, aarch64_neon_srshl, Add1ArgType | UnsignedAlts),
  NEONMAP2(vrshr_n_v, aarch64_neon_urshl, aarch64_neon_srshl, UnsignedAlts),
  NEONMAP2(vrshrq_n_v, aarch64_neon_urshl, aarch64_neon_srshl, UnsignedAlts),
  NEONMAP2(vrsqrte_v, aarch64_neon_frsqrte, aarch64_neon_ursqrte, 0),
  NEONMAP2(vrsqrteq_v, aarch64_neon_frsqrte, aarch64_neon_ursqrte, 0),
  NEONMAP1(vrsqrts_v, aarch64_neon_frsqrts, Add1ArgType),
  NEONMAP1(vrsqrtsq_v, aarch64_neon_frsqrts, Add1ArgType),
  NEONMAP1(vrsubhn_v, aarch64_neon_rsubhn, Add1ArgType),
  NEONMAP1(vsha1su0q_u32, aarch64_crypto_sha1su0, 0),
  NEONMAP1(vsha1su1q_u32, aarch64_crypto_sha1su1, 0),
  NEONMAP1(vsha256h2q_u32, aarch64_crypto_sha256h2, 0),
  NEONMAP1(vsha256hq_u32, aarch64_crypto_sha256h, 0),
  NEONMAP1(vsha256su0q_u32, aarch64_crypto_sha256su0, 0),
  NEONMAP1(vsha256su1q_u32, aarch64_crypto_sha256su1, 0),
  NEONMAP1(vsha512h2q_u64, aarch64_crypto_sha512h2, 0),
  NEONMAP1(vsha512hq_u64, aarch64_crypto_sha512h, 0),
  NEONMAP1(vsha512su0q_u64, aarch64_crypto_sha512su0, 0),
  NEONMAP1(vsha512su1q_u64, aarch64_crypto_sha512su1, 0),
  NEONMAP0(vshl_n_v),
  NEONMAP2(vshl_v, aarch64_neon_ushl, aarch64_neon_sshl, Add1ArgType | UnsignedAlts),
  NEONMAP0(vshll_n_v),
  NEONMAP0(vshlq_n_v),
  NEONMAP2(vshlq_v, aarch64_neon_ushl, aarch64_neon_sshl, Add1ArgType | UnsignedAlts),
  NEONMAP0(vshr_n_v),
  NEONMAP0(vshrn_n_v),
  NEONMAP0(vshrq_n_v),
  NEONMAP1(vsm3partw1q_u32, aarch64_crypto_sm3partw1, 0),
  NEONMAP1(vsm3partw2q_u32, aarch64_crypto_sm3partw2, 0),
  NEONMAP1(vsm3ss1q_u32, aarch64_crypto_sm3ss1, 0),
  NEONMAP1(vsm3tt1aq_u32, aarch64_crypto_sm3tt1a, 0),
  NEONMAP1(vsm3tt1bq_u32, aarch64_crypto_sm3tt1b, 0),
  NEONMAP1(vsm3tt2aq_u32, aarch64_crypto_sm3tt2a, 0),
  NEONMAP1(vsm3tt2bq_u32, aarch64_crypto_sm3tt2b, 0),
  NEONMAP1(vsm4ekeyq_u32, aarch64_crypto_sm4ekey, 0),
  NEONMAP1(vsm4eq_u32, aarch64_crypto_sm4e, 0),
  NEONMAP1(vst1_x2_v, aarch64_neon_st1x2, 0),
  NEONMAP1(vst1_x3_v, aarch64_neon_st1x3, 0),
  NEONMAP1(vst1_x4_v, aarch64_neon_st1x4, 0),
  NEONMAP1(vst1q_x2_v, aarch64_neon_st1x2, 0),
  NEONMAP1(vst1q_x3_v, aarch64_neon_st1x3, 0),
  NEONMAP1(vst1q_x4_v, aarch64_neon_st1x4, 0),
  NEONMAP0(vsubhn_v),
  NEONMAP0(vtst_v),
  NEONMAP0(vtstq_v),
  NEONMAP1(vusdot_s32, aarch64_neon_usdot, 0),
  NEONMAP1(vusdotq_s32, aarch64_neon_usdot, 0),
  NEONMAP1(vusmmlaq_s32, aarch64_neon_usmmla, 0),
  NEONMAP1(vxarq_u64, aarch64_crypto_xar, 0),
};

static const ARMVectorIntrinsicInfo AArch64SISDIntrinsicMap[] = {
  NEONMAP1(vabdd_f64, aarch64_sisd_fabd, Add1ArgType),
  NEONMAP1(vabds_f32, aarch64_sisd_fabd, Add1ArgType),
  NEONMAP1(vabsd_s64, aarch64_neon_abs, Add1ArgType),
  NEONMAP1(vaddlv_s32, aarch64_neon_saddlv, AddRetType | Add1ArgType),
  NEONMAP1(vaddlv_u32, aarch64_neon_uaddlv, AddRetType | Add1ArgType),
  NEONMAP1(vaddlvq_s32, aarch64_neon_saddlv, AddRetType | Add1ArgType),
  NEONMAP1(vaddlvq_u32, aarch64_neon_uaddlv, AddRetType | Add1ArgType),
  NEONMAP1(vaddv_f32, aarch64_neon_faddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddv_s32, aarch64_neon_saddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddv_u32, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_f32, aarch64_neon_faddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_f64, aarch64_neon_faddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_s32, aarch64_neon_saddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_s64, aarch64_neon_saddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_u32, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vaddvq_u64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vcaged_f64, aarch64_neon_facge, AddRetType | Add1ArgType),
  NEONMAP1(vcages_f32, aarch64_neon_facge, AddRetType | Add1ArgType),
  NEONMAP1(vcagtd_f64, aarch64_neon_facgt, AddRetType | Add1ArgType),
  NEONMAP1(vcagts_f32, aarch64_neon_facgt, AddRetType | Add1ArgType),
  NEONMAP1(vcaled_f64, aarch64_neon_facge, AddRetType | Add1ArgType),
  NEONMAP1(vcales_f32, aarch64_neon_facge, AddRetType | Add1ArgType),
  NEONMAP1(vcaltd_f64, aarch64_neon_facgt, AddRetType | Add1ArgType),
  NEONMAP1(vcalts_f32, aarch64_neon_facgt, AddRetType | Add1ArgType),
  NEONMAP1(vcvtad_s64_f64, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
  NEONMAP1(vcvtad_u64_f64, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
  NEONMAP1(vcvtas_s32_f32, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
  NEONMAP1(vcvtas_u32_f32, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_n_f64_s64, aarch64_neon_vcvtfxs2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_n_f64_u64, aarch64_neon_vcvtfxu2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_n_s64_f64, aarch64_neon_vcvtfp2fxs, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_n_u64_f64, aarch64_neon_vcvtfp2fxu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_s64_f64, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
  NEONMAP1(vcvtd_u64_f64, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_bf16_f32, aarch64_neon_bfcvt, 0),
  NEONMAP1(vcvtmd_s64_f64, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmd_u64_f64, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtms_s32_f32, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
  NEONMAP1(vcvtms_u32_f32, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnd_s64_f64, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnd_u64_f64, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtns_s32_f32, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
  NEONMAP1(vcvtns_u32_f32, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtpd_s64_f64, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
  NEONMAP1(vcvtpd_u64_f64, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtps_s32_f32, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
  NEONMAP1(vcvtps_u32_f32, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_n_f32_s32, aarch64_neon_vcvtfxs2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_n_f32_u32, aarch64_neon_vcvtfxu2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_n_s32_f32, aarch64_neon_vcvtfp2fxs, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_n_u32_f32, aarch64_neon_vcvtfp2fxu, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_s32_f32, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
  NEONMAP1(vcvts_u32_f32, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtxd_f32_f64, aarch64_sisd_fcvtxn, 0),
  NEONMAP1(vmaxnmv_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxnmvq_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxnmvq_f64, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxv_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxv_s32, aarch64_neon_smaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxv_u32, aarch64_neon_umaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxvq_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxvq_f64, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxvq_s32, aarch64_neon_smaxv, AddRetType | Add1ArgType),
  NEONMAP1(vmaxvq_u32, aarch64_neon_umaxv, AddRetType | Add1ArgType),
  NEONMAP1(vminnmv_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vminnmvq_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vminnmvq_f64, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vminv_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vminv_s32, aarch64_neon_sminv, AddRetType | Add1ArgType),
  NEONMAP1(vminv_u32, aarch64_neon_uminv, AddRetType | Add1ArgType),
  NEONMAP1(vminvq_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vminvq_f64, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vminvq_s32, aarch64_neon_sminv, AddRetType | Add1ArgType),
  NEONMAP1(vminvq_u32, aarch64_neon_uminv, AddRetType | Add1ArgType),
  NEONMAP1(vmull_p64, aarch64_neon_pmull64, 0),
  NEONMAP1(vmulxd_f64, aarch64_neon_fmulx, Add1ArgType),
  NEONMAP1(vmulxs_f32, aarch64_neon_fmulx, Add1ArgType),
  NEONMAP1(vpaddd_s64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vpaddd_u64, aarch64_neon_uaddv, AddRetType | Add1ArgType),
  NEONMAP1(vpmaxnmqd_f64, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vpmaxnms_f32, aarch64_neon_fmaxnmv, AddRetType | Add1ArgType),
  NEONMAP1(vpmaxqd_f64, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vpmaxs_f32, aarch64_neon_fmaxv, AddRetType | Add1ArgType),
  NEONMAP1(vpminnmqd_f64, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vpminnms_f32, aarch64_neon_fminnmv, AddRetType | Add1ArgType),
  NEONMAP1(vpminqd_f64, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vpmins_f32, aarch64_neon_fminv, AddRetType | Add1ArgType),
  NEONMAP1(vqabsb_s8, aarch64_neon_sqabs, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqabsd_s64, aarch64_neon_sqabs, Add1ArgType),
  NEONMAP1(vqabsh_s16, aarch64_neon_sqabs, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqabss_s32, aarch64_neon_sqabs, Add1ArgType),
  NEONMAP1(vqaddb_s8, aarch64_neon_sqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqaddb_u8, aarch64_neon_uqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqaddd_s64, aarch64_neon_sqadd, Add1ArgType),
  NEONMAP1(vqaddd_u64, aarch64_neon_uqadd, Add1ArgType),
  NEONMAP1(vqaddh_s16, aarch64_neon_sqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqaddh_u16, aarch64_neon_uqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqadds_s32, aarch64_neon_sqadd, Add1ArgType),
  NEONMAP1(vqadds_u32, aarch64_neon_uqadd, Add1ArgType),
  NEONMAP1(vqdmulhh_s16, aarch64_neon_sqdmulh, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqdmulhs_s32, aarch64_neon_sqdmulh, Add1ArgType),
  NEONMAP1(vqdmullh_s16, aarch64_neon_sqdmull, VectorRet | Use128BitVectors),
  NEONMAP1(vqdmulls_s32, aarch64_neon_sqdmulls_scalar, 0),
  NEONMAP1(vqmovnd_s64, aarch64_neon_scalar_sqxtn, AddRetType | Add1ArgType),
  NEONMAP1(vqmovnd_u64, aarch64_neon_scalar_uqxtn, AddRetType | Add1ArgType),
  NEONMAP1(vqmovnh_s16, aarch64_neon_sqxtn, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovnh_u16, aarch64_neon_uqxtn, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovns_s32, aarch64_neon_sqxtn, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovns_u32, aarch64_neon_uqxtn, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovund_s64, aarch64_neon_scalar_sqxtun, AddRetType | Add1ArgType),
  NEONMAP1(vqmovunh_s16, aarch64_neon_sqxtun, VectorRet | Use64BitVectors),
  NEONMAP1(vqmovuns_s32, aarch64_neon_sqxtun, VectorRet | Use64BitVectors),
  NEONMAP1(vqnegb_s8, aarch64_neon_sqneg, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqnegd_s64, aarch64_neon_sqneg, Add1ArgType),
  NEONMAP1(vqnegh_s16, aarch64_neon_sqneg, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqnegs_s32, aarch64_neon_sqneg, Add1ArgType),
  NEONMAP1(vqrdmlahh_s16, aarch64_neon_sqrdmlah, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrdmlahs_s32, aarch64_neon_sqrdmlah, Add1ArgType),
  NEONMAP1(vqrdmlshh_s16, aarch64_neon_sqrdmlsh, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrdmlshs_s32, aarch64_neon_sqrdmlsh, Add1ArgType),
  NEONMAP1(vqrdmulhh_s16, aarch64_neon_sqrdmulh, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrdmulhs_s32, aarch64_neon_sqrdmulh, Add1ArgType),
  NEONMAP1(vqrshlb_s8, aarch64_neon_sqrshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrshlb_u8, aarch64_neon_uqrshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrshld_s64, aarch64_neon_sqrshl, Add1ArgType),
  NEONMAP1(vqrshld_u64, aarch64_neon_uqrshl, Add1ArgType),
  NEONMAP1(vqrshlh_s16, aarch64_neon_sqrshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrshlh_u16, aarch64_neon_uqrshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqrshls_s32, aarch64_neon_sqrshl, Add1ArgType),
  NEONMAP1(vqrshls_u32, aarch64_neon_uqrshl, Add1ArgType),
  NEONMAP1(vqrshrnd_n_s64, aarch64_neon_sqrshrn, AddRetType),
  NEONMAP1(vqrshrnd_n_u64, aarch64_neon_uqrshrn, AddRetType),
  NEONMAP1(vqrshrnh_n_s16, aarch64_neon_sqrshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshrnh_n_u16, aarch64_neon_uqrshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshrns_n_s32, aarch64_neon_sqrshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshrns_n_u32, aarch64_neon_uqrshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshrund_n_s64, aarch64_neon_sqrshrun, AddRetType),
  NEONMAP1(vqrshrunh_n_s16, aarch64_neon_sqrshrun, VectorRet | Use64BitVectors),
  NEONMAP1(vqrshruns_n_s32, aarch64_neon_sqrshrun, VectorRet | Use64BitVectors),
  NEONMAP1(vqshlb_n_s8, aarch64_neon_sqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlb_n_u8, aarch64_neon_uqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlb_s8, aarch64_neon_sqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlb_u8, aarch64_neon_uqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshld_s64, aarch64_neon_sqshl, Add1ArgType),
  NEONMAP1(vqshld_u64, aarch64_neon_uqshl, Add1ArgType),
  NEONMAP1(vqshlh_n_s16, aarch64_neon_sqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlh_n_u16, aarch64_neon_uqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlh_s16, aarch64_neon_sqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlh_u16, aarch64_neon_uqshl, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshls_n_s32, aarch64_neon_sqshl, Add1ArgType),
  NEONMAP1(vqshls_n_u32, aarch64_neon_uqshl, Add1ArgType),
  NEONMAP1(vqshls_s32, aarch64_neon_sqshl, Add1ArgType),
  NEONMAP1(vqshls_u32, aarch64_neon_uqshl, Add1ArgType),
  NEONMAP1(vqshlub_n_s8, aarch64_neon_sqshlu, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshluh_n_s16, aarch64_neon_sqshlu, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqshlus_n_s32, aarch64_neon_sqshlu, Add1ArgType),
  NEONMAP1(vqshrnd_n_s64, aarch64_neon_sqshrn, AddRetType),
  NEONMAP1(vqshrnd_n_u64, aarch64_neon_uqshrn, AddRetType),
  NEONMAP1(vqshrnh_n_s16, aarch64_neon_sqshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqshrnh_n_u16, aarch64_neon_uqshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqshrns_n_s32, aarch64_neon_sqshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqshrns_n_u32, aarch64_neon_uqshrn, VectorRet | Use64BitVectors),
  NEONMAP1(vqshrund_n_s64, aarch64_neon_sqshrun, AddRetType),
  NEONMAP1(vqshrunh_n_s16, aarch64_neon_sqshrun, VectorRet | Use64BitVectors),
  NEONMAP1(vqshruns_n_s32, aarch64_neon_sqshrun, VectorRet | Use64BitVectors),
  NEONMAP1(vqsubb_s8, aarch64_neon_sqsub, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqsubb_u8, aarch64_neon_uqsub, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqsubd_s64, aarch64_neon_sqsub, Add1ArgType),
  NEONMAP1(vqsubd_u64, aarch64_neon_uqsub, Add1ArgType),
  NEONMAP1(vqsubh_s16, aarch64_neon_sqsub, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqsubh_u16, aarch64_neon_uqsub, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vqsubs_s32, aarch64_neon_sqsub, Add1ArgType),
  NEONMAP1(vqsubs_u32, aarch64_neon_uqsub, Add1ArgType),
  NEONMAP1(vrecped_f64, aarch64_neon_frecpe, Add1ArgType),
  NEONMAP1(vrecpes_f32, aarch64_neon_frecpe, Add1ArgType),
  NEONMAP1(vrecpxd_f64, aarch64_neon_frecpx, Add1ArgType),
  NEONMAP1(vrecpxs_f32, aarch64_neon_frecpx, Add1ArgType),
  NEONMAP1(vrshld_s64, aarch64_neon_srshl, Add1ArgType),
  NEONMAP1(vrshld_u64, aarch64_neon_urshl, Add1ArgType),
  NEONMAP1(vrsqrted_f64, aarch64_neon_frsqrte, Add1ArgType),
  NEONMAP1(vrsqrtes_f32, aarch64_neon_frsqrte, Add1ArgType),
  NEONMAP1(vrsqrtsd_f64, aarch64_neon_frsqrts, Add1ArgType),
  NEONMAP1(vrsqrtss_f32, aarch64_neon_frsqrts, Add1ArgType),
  NEONMAP1(vsha1cq_u32, aarch64_crypto_sha1c, 0),
  NEONMAP1(vsha1h_u32, aarch64_crypto_sha1h, 0),
  NEONMAP1(vsha1mq_u32, aarch64_crypto_sha1m, 0),
  NEONMAP1(vsha1pq_u32, aarch64_crypto_sha1p, 0),
  NEONMAP1(vshld_s64, aarch64_neon_sshl, Add1ArgType),
  NEONMAP1(vshld_u64, aarch64_neon_ushl, Add1ArgType),
  NEONMAP1(vslid_n_s64, aarch64_neon_vsli, Vectorize1ArgType),
  NEONMAP1(vslid_n_u64, aarch64_neon_vsli, Vectorize1ArgType),
  NEONMAP1(vsqaddb_u8, aarch64_neon_usqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vsqaddd_u64, aarch64_neon_usqadd, Add1ArgType),
  NEONMAP1(vsqaddh_u16, aarch64_neon_usqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vsqadds_u32, aarch64_neon_usqadd, Add1ArgType),
  NEONMAP1(vsrid_n_s64, aarch64_neon_vsri, Vectorize1ArgType),
  NEONMAP1(vsrid_n_u64, aarch64_neon_vsri, Vectorize1ArgType),
  NEONMAP1(vuqaddb_s8, aarch64_neon_suqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vuqaddd_s64, aarch64_neon_suqadd, Add1ArgType),
  NEONMAP1(vuqaddh_s16, aarch64_neon_suqadd, Vectorize1ArgType | Use64BitVectors),
  NEONMAP1(vuqadds_s32, aarch64_neon_suqadd, Add1ArgType),
  // FP16 scalar intrinisics go here.
  NEONMAP1(vabdh_f16, aarch64_sisd_fabd, Add1ArgType),
  NEONMAP1(vcvtah_s32_f16, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
  NEONMAP1(vcvtah_s64_f16, aarch64_neon_fcvtas, AddRetType | Add1ArgType),
  NEONMAP1(vcvtah_u32_f16, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
  NEONMAP1(vcvtah_u64_f16, aarch64_neon_fcvtau, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_f16_s32, aarch64_neon_vcvtfxs2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_f16_s64, aarch64_neon_vcvtfxs2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_f16_u32, aarch64_neon_vcvtfxu2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_f16_u64, aarch64_neon_vcvtfxu2fp, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_s32_f16, aarch64_neon_vcvtfp2fxs, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_s64_f16, aarch64_neon_vcvtfp2fxs, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_u32_f16, aarch64_neon_vcvtfp2fxu, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_n_u64_f16, aarch64_neon_vcvtfp2fxu, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_s32_f16, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_s64_f16, aarch64_neon_fcvtzs, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_u32_f16, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
  NEONMAP1(vcvth_u64_f16, aarch64_neon_fcvtzu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmh_s32_f16, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmh_s64_f16, aarch64_neon_fcvtms, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmh_u32_f16, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtmh_u64_f16, aarch64_neon_fcvtmu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnh_s32_f16, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnh_s64_f16, aarch64_neon_fcvtns, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnh_u32_f16, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtnh_u64_f16, aarch64_neon_fcvtnu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtph_s32_f16, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
  NEONMAP1(vcvtph_s64_f16, aarch64_neon_fcvtps, AddRetType | Add1ArgType),
  NEONMAP1(vcvtph_u32_f16, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
  NEONMAP1(vcvtph_u64_f16, aarch64_neon_fcvtpu, AddRetType | Add1ArgType),
  NEONMAP1(vmulxh_f16, aarch64_neon_fmulx, Add1ArgType),
  NEONMAP1(vrecpeh_f16, aarch64_neon_frecpe, Add1ArgType),
  NEONMAP1(vrecpxh_f16, aarch64_neon_frecpx, Add1ArgType),
  NEONMAP1(vrsqrteh_f16, aarch64_neon_frsqrte, Add1ArgType),
  NEONMAP1(vrsqrtsh_f16, aarch64_neon_frsqrts, Add1ArgType),
};

// Some intrinsics are equivalent for codegen.
static const std::pair<unsigned, unsigned> NEONEquivalentIntrinsicMap[] = {
  { NEON::BI__builtin_neon_splat_lane_bf16, NEON::BI__builtin_neon_splat_lane_v, },
  { NEON::BI__builtin_neon_splat_laneq_bf16, NEON::BI__builtin_neon_splat_laneq_v, },
  { NEON::BI__builtin_neon_splatq_lane_bf16, NEON::BI__builtin_neon_splatq_lane_v, },
  { NEON::BI__builtin_neon_splatq_laneq_bf16, NEON::BI__builtin_neon_splatq_laneq_v, },
  { NEON::BI__builtin_neon_vabd_f16, NEON::BI__builtin_neon_vabd_v, },
  { NEON::BI__builtin_neon_vabdq_f16, NEON::BI__builtin_neon_vabdq_v, },
  { NEON::BI__builtin_neon_vabs_f16, NEON::BI__builtin_neon_vabs_v, },
  { NEON::BI__builtin_neon_vabsq_f16, NEON::BI__builtin_neon_vabsq_v, },
  { NEON::BI__builtin_neon_vcage_f16, NEON::BI__builtin_neon_vcage_v, },
  { NEON::BI__builtin_neon_vcageq_f16, NEON::BI__builtin_neon_vcageq_v, },
  { NEON::BI__builtin_neon_vcagt_f16, NEON::BI__builtin_neon_vcagt_v, },
  { NEON::BI__builtin_neon_vcagtq_f16, NEON::BI__builtin_neon_vcagtq_v, },
  { NEON::BI__builtin_neon_vcale_f16, NEON::BI__builtin_neon_vcale_v, },
  { NEON::BI__builtin_neon_vcaleq_f16, NEON::BI__builtin_neon_vcaleq_v, },
  { NEON::BI__builtin_neon_vcalt_f16, NEON::BI__builtin_neon_vcalt_v, },
  { NEON::BI__builtin_neon_vcaltq_f16, NEON::BI__builtin_neon_vcaltq_v, },
  { NEON::BI__builtin_neon_vceqz_f16, NEON::BI__builtin_neon_vceqz_v, },
  { NEON::BI__builtin_neon_vceqzq_f16, NEON::BI__builtin_neon_vceqzq_v, },
  { NEON::BI__builtin_neon_vcgez_f16, NEON::BI__builtin_neon_vcgez_v, },
  { NEON::BI__builtin_neon_vcgezq_f16, NEON::BI__builtin_neon_vcgezq_v, },
  { NEON::BI__builtin_neon_vcgtz_f16, NEON::BI__builtin_neon_vcgtz_v, },
  { NEON::BI__builtin_neon_vcgtzq_f16, NEON::BI__builtin_neon_vcgtzq_v, },
  { NEON::BI__builtin_neon_vclez_f16, NEON::BI__builtin_neon_vclez_v, },
  { NEON::BI__builtin_neon_vclezq_f16, NEON::BI__builtin_neon_vclezq_v, },
  { NEON::BI__builtin_neon_vcltz_f16, NEON::BI__builtin_neon_vcltz_v, },
  { NEON::BI__builtin_neon_vcltzq_f16, NEON::BI__builtin_neon_vcltzq_v, },
  { NEON::BI__builtin_neon_vfma_f16, NEON::BI__builtin_neon_vfma_v, },
  { NEON::BI__builtin_neon_vfma_lane_f16, NEON::BI__builtin_neon_vfma_lane_v, },
  { NEON::BI__builtin_neon_vfma_laneq_f16, NEON::BI__builtin_neon_vfma_laneq_v, },
  { NEON::BI__builtin_neon_vfmaq_f16, NEON::BI__builtin_neon_vfmaq_v, },
  { NEON::BI__builtin_neon_vfmaq_lane_f16, NEON::BI__builtin_neon_vfmaq_lane_v, },
  { NEON::BI__builtin_neon_vfmaq_laneq_f16, NEON::BI__builtin_neon_vfmaq_laneq_v, },
  { NEON::BI__builtin_neon_vld1_bf16_x2, NEON::BI__builtin_neon_vld1_x2_v },
  { NEON::BI__builtin_neon_vld1_bf16_x3, NEON::BI__builtin_neon_vld1_x3_v },
  { NEON::BI__builtin_neon_vld1_bf16_x4, NEON::BI__builtin_neon_vld1_x4_v },
  { NEON::BI__builtin_neon_vld1_bf16, NEON::BI__builtin_neon_vld1_v },
  { NEON::BI__builtin_neon_vld1_dup_bf16, NEON::BI__builtin_neon_vld1_dup_v },
  { NEON::BI__builtin_neon_vld1_lane_bf16, NEON::BI__builtin_neon_vld1_lane_v },
  { NEON::BI__builtin_neon_vld1q_bf16_x2, NEON::BI__builtin_neon_vld1q_x2_v },
  { NEON::BI__builtin_neon_vld1q_bf16_x3, NEON::BI__builtin_neon_vld1q_x3_v },
  { NEON::BI__builtin_neon_vld1q_bf16_x4, NEON::BI__builtin_neon_vld1q_x4_v },
  { NEON::BI__builtin_neon_vld1q_bf16, NEON::BI__builtin_neon_vld1q_v },
  { NEON::BI__builtin_neon_vld1q_dup_bf16, NEON::BI__builtin_neon_vld1q_dup_v },
  { NEON::BI__builtin_neon_vld1q_lane_bf16, NEON::BI__builtin_neon_vld1q_lane_v },
  { NEON::BI__builtin_neon_vld2_bf16, NEON::BI__builtin_neon_vld2_v },
  { NEON::BI__builtin_neon_vld2_dup_bf16, NEON::BI__builtin_neon_vld2_dup_v },
  { NEON::BI__builtin_neon_vld2_lane_bf16, NEON::BI__builtin_neon_vld2_lane_v },
  { NEON::BI__builtin_neon_vld2q_bf16, NEON::BI__builtin_neon_vld2q_v },
  { NEON::BI__builtin_neon_vld2q_dup_bf16, NEON::BI__builtin_neon_vld2q_dup_v },
  { NEON::BI__builtin_neon_vld2q_lane_bf16, NEON::BI__builtin_neon_vld2q_lane_v },
  { NEON::BI__builtin_neon_vld3_bf16, NEON::BI__builtin_neon_vld3_v },
  { NEON::BI__builtin_neon_vld3_dup_bf16, NEON::BI__builtin_neon_vld3_dup_v },
  { NEON::BI__builtin_neon_vld3_lane_bf16, NEON::BI__builtin_neon_vld3_lane_v },
  { NEON::BI__builtin_neon_vld3q_bf16, NEON::BI__builtin_neon_vld3q_v },
  { NEON::BI__builtin_neon_vld3q_dup_bf16, NEON::BI__builtin_neon_vld3q_dup_v },
  { NEON::BI__builtin_neon_vld3q_lane_bf16, NEON::BI__builtin_neon_vld3q_lane_v },
  { NEON::BI__builtin_neon_vld4_bf16, NEON::BI__builtin_neon_vld4_v },
  { NEON::BI__builtin_neon_vld4_dup_bf16, NEON::BI__builtin_neon_vld4_dup_v },
  { NEON::BI__builtin_neon_vld4_lane_bf16, NEON::BI__builtin_neon_vld4_lane_v },
  { NEON::BI__builtin_neon_vld4q_bf16, NEON::BI__builtin_neon_vld4q_v },
  { NEON::BI__builtin_neon_vld4q_dup_bf16, NEON::BI__builtin_neon_vld4q_dup_v },
  { NEON::BI__builtin_neon_vld4q_lane_bf16, NEON::BI__builtin_neon_vld4q_lane_v },
  { NEON::BI__builtin_neon_vmax_f16, NEON::BI__builtin_neon_vmax_v, },
  { NEON::BI__builtin_neon_vmaxnm_f16, NEON::BI__builtin_neon_vmaxnm_v, },
  { NEON::BI__builtin_neon_vmaxnmq_f16, NEON::BI__builtin_neon_vmaxnmq_v, },
  { NEON::BI__builtin_neon_vmaxq_f16, NEON::BI__builtin_neon_vmaxq_v, },
  { NEON::BI__builtin_neon_vmin_f16, NEON::BI__builtin_neon_vmin_v, },
  { NEON::BI__builtin_neon_vminnm_f16, NEON::BI__builtin_neon_vminnm_v, },
  { NEON::BI__builtin_neon_vminnmq_f16, NEON::BI__builtin_neon_vminnmq_v, },
  { NEON::BI__builtin_neon_vminq_f16, NEON::BI__builtin_neon_vminq_v, },
  { NEON::BI__builtin_neon_vmulx_f16, NEON::BI__builtin_neon_vmulx_v, },
  { NEON::BI__builtin_neon_vmulxq_f16, NEON::BI__builtin_neon_vmulxq_v, },
  { NEON::BI__builtin_neon_vpadd_f16, NEON::BI__builtin_neon_vpadd_v, },
  { NEON::BI__builtin_neon_vpaddq_f16, NEON::BI__builtin_neon_vpaddq_v, },
  { NEON::BI__builtin_neon_vpmax_f16, NEON::BI__builtin_neon_vpmax_v, },
  { NEON::BI__builtin_neon_vpmaxnm_f16, NEON::BI__builtin_neon_vpmaxnm_v, },
  { NEON::BI__builtin_neon_vpmaxnmq_f16, NEON::BI__builtin_neon_vpmaxnmq_v, },
  { NEON::BI__builtin_neon_vpmaxq_f16, NEON::BI__builtin_neon_vpmaxq_v, },
  { NEON::BI__builtin_neon_vpmin_f16, NEON::BI__builtin_neon_vpmin_v, },
  { NEON::BI__builtin_neon_vpminnm_f16, NEON::BI__builtin_neon_vpminnm_v, },
  { NEON::BI__builtin_neon_vpminnmq_f16, NEON::BI__builtin_neon_vpminnmq_v, },
  { NEON::BI__builtin_neon_vpminq_f16, NEON::BI__builtin_neon_vpminq_v, },
  { NEON::BI__builtin_neon_vrecpe_f16, NEON::BI__builtin_neon_vrecpe_v, },
  { NEON::BI__builtin_neon_vrecpeq_f16, NEON::BI__builtin_neon_vrecpeq_v, },
  { NEON::BI__builtin_neon_vrecps_f16, NEON::BI__builtin_neon_vrecps_v, },
  { NEON::BI__builtin_neon_vrecpsq_f16, NEON::BI__builtin_neon_vrecpsq_v, },
  { NEON::BI__builtin_neon_vrnd_f16, NEON::BI__builtin_neon_vrnd_v, },
  { NEON::BI__builtin_neon_vrnda_f16, NEON::BI__builtin_neon_vrnda_v, },
  { NEON::BI__builtin_neon_vrndaq_f16, NEON::BI__builtin_neon_vrndaq_v, },
  { NEON::BI__builtin_neon_vrndi_f16, NEON::BI__builtin_neon_vrndi_v, },
  { NEON::BI__builtin_neon_vrndiq_f16, NEON::BI__builtin_neon_vrndiq_v, },
  { NEON::BI__builtin_neon_vrndm_f16, NEON::BI__builtin_neon_vrndm_v, },
  { NEON::BI__builtin_neon_vrndmq_f16, NEON::BI__builtin_neon_vrndmq_v, },
  { NEON::BI__builtin_neon_vrndn_f16, NEON::BI__builtin_neon_vrndn_v, },
  { NEON::BI__builtin_neon_vrndnq_f16, NEON::BI__builtin_neon_vrndnq_v, },
  { NEON::BI__builtin_neon_vrndp_f16, NEON::BI__builtin_neon_vrndp_v, },
  { NEON::BI__builtin_neon_vrndpq_f16, NEON::BI__builtin_neon_vrndpq_v, },
  { NEON::BI__builtin_neon_vrndq_f16, NEON::BI__builtin_neon_vrndq_v, },
  { NEON::BI__builtin_neon_vrndx_f16, NEON::BI__builtin_neon_vrndx_v, },
  { NEON::BI__builtin_neon_vrndxq_f16, NEON::BI__builtin_neon_vrndxq_v, },
  { NEON::BI__builtin_neon_vrsqrte_f16, NEON::BI__builtin_neon_vrsqrte_v, },
  { NEON::BI__builtin_neon_vrsqrteq_f16, NEON::BI__builtin_neon_vrsqrteq_v, },
  { NEON::BI__builtin_neon_vrsqrts_f16, NEON::BI__builtin_neon_vrsqrts_v, },
  { NEON::BI__builtin_neon_vrsqrtsq_f16, NEON::BI__builtin_neon_vrsqrtsq_v, },
  { NEON::BI__builtin_neon_vsqrt_f16, NEON::BI__builtin_neon_vsqrt_v, },
  { NEON::BI__builtin_neon_vsqrtq_f16, NEON::BI__builtin_neon_vsqrtq_v, },
  { NEON::BI__builtin_neon_vst1_bf16_x2, NEON::BI__builtin_neon_vst1_x2_v },
  { NEON::BI__builtin_neon_vst1_bf16_x3, NEON::BI__builtin_neon_vst1_x3_v },
  { NEON::BI__builtin_neon_vst1_bf16_x4, NEON::BI__builtin_neon_vst1_x4_v },
  { NEON::BI__builtin_neon_vst1_bf16, NEON::BI__builtin_neon_vst1_v },
  { NEON::BI__builtin_neon_vst1_lane_bf16, NEON::BI__builtin_neon_vst1_lane_v },
  { NEON::BI__builtin_neon_vst1q_bf16_x2, NEON::BI__builtin_neon_vst1q_x2_v },
  { NEON::BI__builtin_neon_vst1q_bf16_x3, NEON::BI__builtin_neon_vst1q_x3_v },
  { NEON::BI__builtin_neon_vst1q_bf16_x4, NEON::BI__builtin_neon_vst1q_x4_v },
  { NEON::BI__builtin_neon_vst1q_bf16, NEON::BI__builtin_neon_vst1q_v },
  { NEON::BI__builtin_neon_vst1q_lane_bf16, NEON::BI__builtin_neon_vst1q_lane_v },
  { NEON::BI__builtin_neon_vst2_bf16, NEON::BI__builtin_neon_vst2_v },
  { NEON::BI__builtin_neon_vst2_lane_bf16, NEON::BI__builtin_neon_vst2_lane_v },
  { NEON::BI__builtin_neon_vst2q_bf16, NEON::BI__builtin_neon_vst2q_v },
  { NEON::BI__builtin_neon_vst2q_lane_bf16, NEON::BI__builtin_neon_vst2q_lane_v },
  { NEON::BI__builtin_neon_vst3_bf16, NEON::BI__builtin_neon_vst3_v },
  { NEON::BI__builtin_neon_vst3_lane_bf16, NEON::BI__builtin_neon_vst3_lane_v },
  { NEON::BI__builtin_neon_vst3q_bf16, NEON::BI__builtin_neon_vst3q_v },
  { NEON::BI__builtin_neon_vst3q_lane_bf16, NEON::BI__builtin_neon_vst3q_lane_v },
  { NEON::BI__builtin_neon_vst4_bf16, NEON::BI__builtin_neon_vst4_v },
  { NEON::BI__builtin_neon_vst4_lane_bf16, NEON::BI__builtin_neon_vst4_lane_v },
  { NEON::BI__builtin_neon_vst4q_bf16, NEON::BI__builtin_neon_vst4q_v },
  { NEON::BI__builtin_neon_vst4q_lane_bf16, NEON::BI__builtin_neon_vst4q_lane_v },
  // The mangling rules cause us to have one ID for each type for vldap1(q)_lane
  // and vstl1(q)_lane, but codegen is equivalent for all of them. Choose an
  // arbitrary one to be handled as tha canonical variation.
  { NEON::BI__builtin_neon_vldap1_lane_u64, NEON::BI__builtin_neon_vldap1_lane_s64 },
  { NEON::BI__builtin_neon_vldap1_lane_f64, NEON::BI__builtin_neon_vldap1_lane_s64 },
  { NEON::BI__builtin_neon_vldap1_lane_p64, NEON::BI__builtin_neon_vldap1_lane_s64 },
  { NEON::BI__builtin_neon_vldap1q_lane_u64, NEON::BI__builtin_neon_vldap1q_lane_s64 },
  { NEON::BI__builtin_neon_vldap1q_lane_f64, NEON::BI__builtin_neon_vldap1q_lane_s64 },
  { NEON::BI__builtin_neon_vldap1q_lane_p64, NEON::BI__builtin_neon_vldap1q_lane_s64 },
  { NEON::BI__builtin_neon_vstl1_lane_u64, NEON::BI__builtin_neon_vstl1_lane_s64 },
  { NEON::BI__builtin_neon_vstl1_lane_f64, NEON::BI__builtin_neon_vstl1_lane_s64 },
  { NEON::BI__builtin_neon_vstl1_lane_p64, NEON::BI__builtin_neon_vstl1_lane_s64 },
  { NEON::BI__builtin_neon_vstl1q_lane_u64, NEON::BI__builtin_neon_vstl1q_lane_s64 },
  { NEON::BI__builtin_neon_vstl1q_lane_f64, NEON::BI__builtin_neon_vstl1q_lane_s64 },
  { NEON::BI__builtin_neon_vstl1q_lane_p64, NEON::BI__builtin_neon_vstl1q_lane_s64 },
};

#undef NEONMAP0
#undef NEONMAP1
#undef NEONMAP2

#define SVEMAP1(NameBase, LLVMIntrinsic, TypeModifier)                         \
  {                                                                            \
    #NameBase, SVE::BI__builtin_sve_##NameBase, Intrinsic::LLVMIntrinsic, 0,   \
        TypeModifier                                                           \
  }

#define SVEMAP2(NameBase, TypeModifier)                                        \
  { #NameBase, SVE::BI__builtin_sve_##NameBase, 0, 0, TypeModifier }
static const ARMVectorIntrinsicInfo AArch64SVEIntrinsicMap[] = {
#define GET_SVE_LLVM_INTRINSIC_MAP
#include "clang/Basic/arm_sve_builtin_cg.inc"
#include "clang/Basic/BuiltinsAArch64NeonSVEBridge_cg.def"
#undef GET_SVE_LLVM_INTRINSIC_MAP
};

#undef SVEMAP1
#undef SVEMAP2

#define SMEMAP1(NameBase, LLVMIntrinsic, TypeModifier)                         \
  {                                                                            \
    #NameBase, SME::BI__builtin_sme_##NameBase, Intrinsic::LLVMIntrinsic, 0,   \
        TypeModifier                                                           \
  }

#define SMEMAP2(NameBase, TypeModifier)                                        \
  { #NameBase, SME::BI__builtin_sme_##NameBase, 0, 0, TypeModifier }
static const ARMVectorIntrinsicInfo AArch64SMEIntrinsicMap[] = {
#define GET_SME_LLVM_INTRINSIC_MAP
#include "clang/Basic/arm_sme_builtin_cg.inc"
#undef GET_SME_LLVM_INTRINSIC_MAP
};

#undef SMEMAP1
#undef SMEMAP2

static bool NEONSIMDIntrinsicsProvenSorted = false;

static bool AArch64SIMDIntrinsicsProvenSorted = false;
static bool AArch64SISDIntrinsicsProvenSorted = false;
static bool AArch64SVEIntrinsicsProvenSorted = false;
static bool AArch64SMEIntrinsicsProvenSorted = false;

static const ARMVectorIntrinsicInfo *
findARMVectorIntrinsicInMap(ArrayRef<ARMVectorIntrinsicInfo> IntrinsicMap,
                            unsigned BuiltinID, bool &MapProvenSorted) {

#ifndef NDEBUG
  if (!MapProvenSorted) {
    assert(llvm::is_sorted(IntrinsicMap));
    MapProvenSorted = true;
  }
#endif

  const ARMVectorIntrinsicInfo *Builtin =
      llvm::lower_bound(IntrinsicMap, BuiltinID);

  if (Builtin != IntrinsicMap.end() && Builtin->BuiltinID == BuiltinID)
    return Builtin;

  return nullptr;
}

Function *CodeGenFunction::LookupNeonLLVMIntrinsic(unsigned IntrinsicID,
                                                   unsigned Modifier,
                                                   llvm::Type *ArgType,
                                                   const CallExpr *E) {
  int VectorSize = 0;
  if (Modifier & Use64BitVectors)
    VectorSize = 64;
  else if (Modifier & Use128BitVectors)
    VectorSize = 128;

  // Return type.
  SmallVector<llvm::Type *, 3> Tys;
  if (Modifier & AddRetType) {
    llvm::Type *Ty = ConvertType(E->getCallReturnType(getContext()));
    if (Modifier & VectorizeRetType)
      Ty = llvm::FixedVectorType::get(
          Ty, VectorSize ? VectorSize / Ty->getPrimitiveSizeInBits() : 1);

    Tys.push_back(Ty);
  }

  // Arguments.
  if (Modifier & VectorizeArgTypes) {
    int Elts = VectorSize ? VectorSize / ArgType->getPrimitiveSizeInBits() : 1;
    ArgType = llvm::FixedVectorType::get(ArgType, Elts);
  }

  if (Modifier & (Add1ArgType | Add2ArgTypes))
    Tys.push_back(ArgType);

  if (Modifier & Add2ArgTypes)
    Tys.push_back(ArgType);

  if (Modifier & InventFloatType)
    Tys.push_back(FloatTy);

  return CGM.getIntrinsic(IntrinsicID, Tys);
}

static Value *EmitCommonNeonSISDBuiltinExpr(
    CodeGenFunction &CGF, const ARMVectorIntrinsicInfo &SISDInfo,
    SmallVectorImpl<Value *> &Ops, const CallExpr *E) {
  unsigned BuiltinID = SISDInfo.BuiltinID;
  unsigned int Int = SISDInfo.LLVMIntrinsic;
  unsigned Modifier = SISDInfo.TypeModifier;
  const char *s = SISDInfo.NameHint;

  switch (BuiltinID) {
  case NEON::BI__builtin_neon_vcled_s64:
  case NEON::BI__builtin_neon_vcled_u64:
  case NEON::BI__builtin_neon_vcles_f32:
  case NEON::BI__builtin_neon_vcled_f64:
  case NEON::BI__builtin_neon_vcltd_s64:
  case NEON::BI__builtin_neon_vcltd_u64:
  case NEON::BI__builtin_neon_vclts_f32:
  case NEON::BI__builtin_neon_vcltd_f64:
  case NEON::BI__builtin_neon_vcales_f32:
  case NEON::BI__builtin_neon_vcaled_f64:
  case NEON::BI__builtin_neon_vcalts_f32:
  case NEON::BI__builtin_neon_vcaltd_f64:
    // Only one direction of comparisons actually exist, cmle is actually a cmge
    // with swapped operands. The table gives us the right intrinsic but we
    // still need to do the swap.
    std::swap(Ops[0], Ops[1]);
    break;
  }

  assert(Int && "Generic code assumes a valid intrinsic");

  // Determine the type(s) of this overloaded AArch64 intrinsic.
  const Expr *Arg = E->getArg(0);
  llvm::Type *ArgTy = CGF.ConvertType(Arg->getType());
  Function *F = CGF.LookupNeonLLVMIntrinsic(Int, Modifier, ArgTy, E);

  int j = 0;
  ConstantInt *C0 = ConstantInt::get(CGF.SizeTy, 0);
  for (Function::const_arg_iterator ai = F->arg_begin(), ae = F->arg_end();
       ai != ae; ++ai, ++j) {
    llvm::Type *ArgTy = ai->getType();
    if (Ops[j]->getType()->getPrimitiveSizeInBits() ==
             ArgTy->getPrimitiveSizeInBits())
      continue;

    assert(ArgTy->isVectorTy() && !Ops[j]->getType()->isVectorTy());
    // The constant argument to an _n_ intrinsic always has Int32Ty, so truncate
    // it before inserting.
    Ops[j] = CGF.Builder.CreateTruncOrBitCast(
        Ops[j], cast<llvm::VectorType>(ArgTy)->getElementType());
    Ops[j] =
        CGF.Builder.CreateInsertElement(PoisonValue::get(ArgTy), Ops[j], C0);
  }

  Value *Result = CGF.EmitNeonCall(F, Ops, s);
  llvm::Type *ResultType = CGF.ConvertType(E->getType());
  if (ResultType->getPrimitiveSizeInBits().getFixedValue() <
      Result->getType()->getPrimitiveSizeInBits().getFixedValue())
    return CGF.Builder.CreateExtractElement(Result, C0);

  return CGF.Builder.CreateBitCast(Result, ResultType, s);
}

Value *CodeGenFunction::EmitCommonNeonBuiltinExpr(
    unsigned BuiltinID, unsigned LLVMIntrinsic, unsigned AltLLVMIntrinsic,
    const char *NameHint, unsigned Modifier, const CallExpr *E,
    SmallVectorImpl<llvm::Value *> &Ops, Address PtrOp0, Address PtrOp1,
    llvm::Triple::ArchType Arch) {
  // Get the last argument, which specifies the vector type.
  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  std::optional<llvm::APSInt> NeonTypeConst =
      Arg->getIntegerConstantExpr(getContext());
  if (!NeonTypeConst)
    return nullptr;

  // Determine the type of this overloaded NEON intrinsic.
  NeonTypeFlags Type(NeonTypeConst->getZExtValue());
  bool Usgn = Type.isUnsigned();
  bool Quad = Type.isQuad();
  const bool HasLegalHalfType = getTarget().hasLegalHalfType();
  const bool AllowBFloatArgsAndRet =
      getTargetHooks().getABIInfo().allowBFloatArgsAndRet();

  llvm::FixedVectorType *VTy =
      GetNeonType(this, Type, HasLegalHalfType, false, AllowBFloatArgsAndRet);
  llvm::Type *Ty = VTy;
  if (!Ty)
    return nullptr;

  auto getAlignmentValue32 = [&](Address addr) -> Value* {
    return Builder.getInt32(addr.getAlignment().getQuantity());
  };

  unsigned Int = LLVMIntrinsic;
  if ((Modifier & UnsignedAlts) && !Usgn)
    Int = AltLLVMIntrinsic;

  switch (BuiltinID) {
  default: break;
  case NEON::BI__builtin_neon_splat_lane_v:
  case NEON::BI__builtin_neon_splat_laneq_v:
  case NEON::BI__builtin_neon_splatq_lane_v:
  case NEON::BI__builtin_neon_splatq_laneq_v: {
    auto NumElements = VTy->getElementCount();
    if (BuiltinID == NEON::BI__builtin_neon_splatq_lane_v)
      NumElements = NumElements * 2;
    if (BuiltinID == NEON::BI__builtin_neon_splat_laneq_v)
      NumElements = NumElements.divideCoefficientBy(2);

    Ops[0] = Builder.CreateBitCast(Ops[0], VTy);
    return EmitNeonSplat(Ops[0], cast<ConstantInt>(Ops[1]), NumElements);
  }
  case NEON::BI__builtin_neon_vpadd_v:
  case NEON::BI__builtin_neon_vpaddq_v:
    // We don't allow fp/int overloading of intrinsics.
    if (VTy->getElementType()->isFloatingPointTy() &&
        Int == Intrinsic::aarch64_neon_addp)
      Int = Intrinsic::aarch64_neon_faddp;
    break;
  case NEON::BI__builtin_neon_vabs_v:
  case NEON::BI__builtin_neon_vabsq_v:
    if (VTy->getElementType()->isFloatingPointTy())
      return EmitNeonCall(CGM.getIntrinsic(Intrinsic::fabs, Ty), Ops, "vabs");
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Ty), Ops, "vabs");
  case NEON::BI__builtin_neon_vadd_v:
  case NEON::BI__builtin_neon_vaddq_v: {
    llvm::Type *VTy = llvm::FixedVectorType::get(Int8Ty, Quad ? 16 : 8);
    Ops[0] = Builder.CreateBitCast(Ops[0], VTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], VTy);
    Ops[0] =  Builder.CreateXor(Ops[0], Ops[1]);
    return Builder.CreateBitCast(Ops[0], Ty);
  }
  case NEON::BI__builtin_neon_vaddhn_v: {
    llvm::FixedVectorType *SrcTy =
        llvm::FixedVectorType::getExtendedElementVectorType(VTy);

    // %sum = add <4 x i32> %lhs, %rhs
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], SrcTy);
    Ops[0] = Builder.CreateAdd(Ops[0], Ops[1], "vaddhn");

    // %high = lshr <4 x i32> %sum, <i32 16, i32 16, i32 16, i32 16>
    Constant *ShiftAmt =
        ConstantInt::get(SrcTy, SrcTy->getScalarSizeInBits() / 2);
    Ops[0] = Builder.CreateLShr(Ops[0], ShiftAmt, "vaddhn");

    // %res = trunc <4 x i32> %high to <4 x i16>
    return Builder.CreateTrunc(Ops[0], VTy, "vaddhn");
  }
  case NEON::BI__builtin_neon_vcale_v:
  case NEON::BI__builtin_neon_vcaleq_v:
  case NEON::BI__builtin_neon_vcalt_v:
  case NEON::BI__builtin_neon_vcaltq_v:
    std::swap(Ops[0], Ops[1]);
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcage_v:
  case NEON::BI__builtin_neon_vcageq_v:
  case NEON::BI__builtin_neon_vcagt_v:
  case NEON::BI__builtin_neon_vcagtq_v: {
    llvm::Type *Ty;
    switch (VTy->getScalarSizeInBits()) {
    default: llvm_unreachable("unexpected type");
    case 32:
      Ty = FloatTy;
      break;
    case 64:
      Ty = DoubleTy;
      break;
    case 16:
      Ty = HalfTy;
      break;
    }
    auto *VecFlt = llvm::FixedVectorType::get(Ty, VTy->getNumElements());
    llvm::Type *Tys[] = { VTy, VecFlt };
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    return EmitNeonCall(F, Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vceqz_v:
  case NEON::BI__builtin_neon_vceqzq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OEQ,
                                         ICmpInst::ICMP_EQ, "vceqz");
  case NEON::BI__builtin_neon_vcgez_v:
  case NEON::BI__builtin_neon_vcgezq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OGE,
                                         ICmpInst::ICMP_SGE, "vcgez");
  case NEON::BI__builtin_neon_vclez_v:
  case NEON::BI__builtin_neon_vclezq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OLE,
                                         ICmpInst::ICMP_SLE, "vclez");
  case NEON::BI__builtin_neon_vcgtz_v:
  case NEON::BI__builtin_neon_vcgtzq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OGT,
                                         ICmpInst::ICMP_SGT, "vcgtz");
  case NEON::BI__builtin_neon_vcltz_v:
  case NEON::BI__builtin_neon_vcltzq_v:
    return EmitAArch64CompareBuiltinExpr(Ops[0], Ty, ICmpInst::FCMP_OLT,
                                         ICmpInst::ICMP_SLT, "vcltz");
  case NEON::BI__builtin_neon_vclz_v:
  case NEON::BI__builtin_neon_vclzq_v:
    // We generate target-independent intrinsic, which needs a second argument
    // for whether or not clz of zero is undefined; on ARM it isn't.
    Ops.push_back(Builder.getInt1(getTarget().isCLZForZeroUndef()));
    break;
  case NEON::BI__builtin_neon_vcvt_f32_v:
  case NEON::BI__builtin_neon_vcvtq_f32_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float32, false, Quad),
                     HasLegalHalfType);
    return Usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_f16_s16:
  case NEON::BI__builtin_neon_vcvt_f16_u16:
  case NEON::BI__builtin_neon_vcvtq_f16_s16:
  case NEON::BI__builtin_neon_vcvtq_f16_u16:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float16, false, Quad),
                     HasLegalHalfType);
    return Usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_n_f16_s16:
  case NEON::BI__builtin_neon_vcvt_n_f16_u16:
  case NEON::BI__builtin_neon_vcvtq_n_f16_s16:
  case NEON::BI__builtin_neon_vcvtq_n_f16_u16: {
    llvm::Type *Tys[2] = { GetFloatNeonType(this, Type), Ty };
    Function *F = CGM.getIntrinsic(Int, Tys);
    return EmitNeonCall(F, Ops, "vcvt_n");
  }
  case NEON::BI__builtin_neon_vcvt_n_f32_v:
  case NEON::BI__builtin_neon_vcvt_n_f64_v:
  case NEON::BI__builtin_neon_vcvtq_n_f32_v:
  case NEON::BI__builtin_neon_vcvtq_n_f64_v: {
    llvm::Type *Tys[2] = { GetFloatNeonType(this, Type), Ty };
    Int = Usgn ? LLVMIntrinsic : AltLLVMIntrinsic;
    Function *F = CGM.getIntrinsic(Int, Tys);
    return EmitNeonCall(F, Ops, "vcvt_n");
  }
  case NEON::BI__builtin_neon_vcvt_n_s16_f16:
  case NEON::BI__builtin_neon_vcvt_n_s32_v:
  case NEON::BI__builtin_neon_vcvt_n_u16_f16:
  case NEON::BI__builtin_neon_vcvt_n_u32_v:
  case NEON::BI__builtin_neon_vcvt_n_s64_v:
  case NEON::BI__builtin_neon_vcvt_n_u64_v:
  case NEON::BI__builtin_neon_vcvtq_n_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_n_s32_v:
  case NEON::BI__builtin_neon_vcvtq_n_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_n_u32_v:
  case NEON::BI__builtin_neon_vcvtq_n_s64_v:
  case NEON::BI__builtin_neon_vcvtq_n_u64_v: {
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    return EmitNeonCall(F, Ops, "vcvt_n");
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_f16:
  case NEON::BI__builtin_neon_vcvt_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_u16_f16: {
    Ops[0] = Builder.CreateBitCast(Ops[0], GetFloatNeonType(this, Type));
    return Usgn ? Builder.CreateFPToUI(Ops[0], Ty, "vcvt")
                : Builder.CreateFPToSI(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvta_s16_f16:
  case NEON::BI__builtin_neon_vcvta_s32_v:
  case NEON::BI__builtin_neon_vcvta_s64_v:
  case NEON::BI__builtin_neon_vcvta_u16_f16:
  case NEON::BI__builtin_neon_vcvta_u32_v:
  case NEON::BI__builtin_neon_vcvta_u64_v:
  case NEON::BI__builtin_neon_vcvtaq_s16_f16:
  case NEON::BI__builtin_neon_vcvtaq_s32_v:
  case NEON::BI__builtin_neon_vcvtaq_s64_v:
  case NEON::BI__builtin_neon_vcvtaq_u16_f16:
  case NEON::BI__builtin_neon_vcvtaq_u32_v:
  case NEON::BI__builtin_neon_vcvtaq_u64_v:
  case NEON::BI__builtin_neon_vcvtn_s16_f16:
  case NEON::BI__builtin_neon_vcvtn_s32_v:
  case NEON::BI__builtin_neon_vcvtn_s64_v:
  case NEON::BI__builtin_neon_vcvtn_u16_f16:
  case NEON::BI__builtin_neon_vcvtn_u32_v:
  case NEON::BI__builtin_neon_vcvtn_u64_v:
  case NEON::BI__builtin_neon_vcvtnq_s16_f16:
  case NEON::BI__builtin_neon_vcvtnq_s32_v:
  case NEON::BI__builtin_neon_vcvtnq_s64_v:
  case NEON::BI__builtin_neon_vcvtnq_u16_f16:
  case NEON::BI__builtin_neon_vcvtnq_u32_v:
  case NEON::BI__builtin_neon_vcvtnq_u64_v:
  case NEON::BI__builtin_neon_vcvtp_s16_f16:
  case NEON::BI__builtin_neon_vcvtp_s32_v:
  case NEON::BI__builtin_neon_vcvtp_s64_v:
  case NEON::BI__builtin_neon_vcvtp_u16_f16:
  case NEON::BI__builtin_neon_vcvtp_u32_v:
  case NEON::BI__builtin_neon_vcvtp_u64_v:
  case NEON::BI__builtin_neon_vcvtpq_s16_f16:
  case NEON::BI__builtin_neon_vcvtpq_s32_v:
  case NEON::BI__builtin_neon_vcvtpq_s64_v:
  case NEON::BI__builtin_neon_vcvtpq_u16_f16:
  case NEON::BI__builtin_neon_vcvtpq_u32_v:
  case NEON::BI__builtin_neon_vcvtpq_u64_v:
  case NEON::BI__builtin_neon_vcvtm_s16_f16:
  case NEON::BI__builtin_neon_vcvtm_s32_v:
  case NEON::BI__builtin_neon_vcvtm_s64_v:
  case NEON::BI__builtin_neon_vcvtm_u16_f16:
  case NEON::BI__builtin_neon_vcvtm_u32_v:
  case NEON::BI__builtin_neon_vcvtm_u64_v:
  case NEON::BI__builtin_neon_vcvtmq_s16_f16:
  case NEON::BI__builtin_neon_vcvtmq_s32_v:
  case NEON::BI__builtin_neon_vcvtmq_s64_v:
  case NEON::BI__builtin_neon_vcvtmq_u16_f16:
  case NEON::BI__builtin_neon_vcvtmq_u32_v:
  case NEON::BI__builtin_neon_vcvtmq_u64_v: {
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vcvtx_f32_v: {
    llvm::Type *Tys[2] = { VTy->getTruncatedElementVectorType(VTy), Ty};
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, NameHint);

  }
  case NEON::BI__builtin_neon_vext_v:
  case NEON::BI__builtin_neon_vextq_v: {
    int CV = cast<ConstantInt>(Ops[2])->getSExtValue();
    SmallVector<int, 16> Indices;
    for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
      Indices.push_back(i+CV);

    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    return Builder.CreateShuffleVector(Ops[0], Ops[1], Indices, "vext");
  }
  case NEON::BI__builtin_neon_vfma_v:
  case NEON::BI__builtin_neon_vfmaq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);

    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, Ty,
        {Ops[1], Ops[2], Ops[0]});
  }
  case NEON::BI__builtin_neon_vld1_v:
  case NEON::BI__builtin_neon_vld1q_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Ops.push_back(getAlignmentValue32(PtrOp0));
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, "vld1");
  }
  case NEON::BI__builtin_neon_vld1_x2_v:
  case NEON::BI__builtin_neon_vld1q_x2_v:
  case NEON::BI__builtin_neon_vld1_x3_v:
  case NEON::BI__builtin_neon_vld1q_x3_v:
  case NEON::BI__builtin_neon_vld1_x4_v:
  case NEON::BI__builtin_neon_vld1q_x4_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld1xN");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_v:
  case NEON::BI__builtin_neon_vld2q_v:
  case NEON::BI__builtin_neon_vld3_v:
  case NEON::BI__builtin_neon_vld3q_v:
  case NEON::BI__builtin_neon_vld4_v:
  case NEON::BI__builtin_neon_vld4q_v:
  case NEON::BI__builtin_neon_vld2_dup_v:
  case NEON::BI__builtin_neon_vld2q_dup_v:
  case NEON::BI__builtin_neon_vld3_dup_v:
  case NEON::BI__builtin_neon_vld3q_dup_v:
  case NEON::BI__builtin_neon_vld4_dup_v:
  case NEON::BI__builtin_neon_vld4q_dup_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    Value *Align = getAlignmentValue32(PtrOp1);
    Ops[1] = Builder.CreateCall(F, {Ops[1], Align}, NameHint);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld1_dup_v:
  case NEON::BI__builtin_neon_vld1q_dup_v: {
    Value *V = PoisonValue::get(Ty);
    PtrOp0 = PtrOp0.withElementType(VTy->getElementType());
    LoadInst *Ld = Builder.CreateLoad(PtrOp0);
    llvm::Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[0] = Builder.CreateInsertElement(V, Ld, CI);
    return EmitNeonSplat(Ops[0], CI);
  }
  case NEON::BI__builtin_neon_vld2_lane_v:
  case NEON::BI__builtin_neon_vld2q_lane_v:
  case NEON::BI__builtin_neon_vld3_lane_v:
  case NEON::BI__builtin_neon_vld3q_lane_v:
  case NEON::BI__builtin_neon_vld4_lane_v:
  case NEON::BI__builtin_neon_vld4q_lane_v: {
    llvm::Type *Tys[] = {Ty, Int8PtrTy};
    Function *F = CGM.getIntrinsic(LLVMIntrinsic, Tys);
    for (unsigned I = 2; I < Ops.size() - 1; ++I)
      Ops[I] = Builder.CreateBitCast(Ops[I], Ty);
    Ops.push_back(getAlignmentValue32(PtrOp1));
    Ops[1] = Builder.CreateCall(F, ArrayRef(Ops).slice(1), NameHint);
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vmovl_v: {
    llvm::FixedVectorType *DTy =
        llvm::FixedVectorType::getTruncatedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], DTy);
    if (Usgn)
      return Builder.CreateZExt(Ops[0], Ty, "vmovl");
    return Builder.CreateSExt(Ops[0], Ty, "vmovl");
  }
  case NEON::BI__builtin_neon_vmovn_v: {
    llvm::FixedVectorType *QTy =
        llvm::FixedVectorType::getExtendedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], QTy);
    return Builder.CreateTrunc(Ops[0], Ty, "vmovn");
  }
  case NEON::BI__builtin_neon_vmull_v:
    // FIXME: the integer vmull operations could be emitted in terms of pure
    // LLVM IR (2 exts followed by a mul). Unfortunately LLVM has a habit of
    // hoisting the exts outside loops. Until global ISel comes along that can
    // see through such movement this leads to bad CodeGen. So we need an
    // intrinsic for now.
    Int = Usgn ? Intrinsic::arm_neon_vmullu : Intrinsic::arm_neon_vmulls;
    Int = Type.isPoly() ? (unsigned)Intrinsic::arm_neon_vmullp : Int;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmull");
  case NEON::BI__builtin_neon_vpadal_v:
  case NEON::BI__builtin_neon_vpadalq_v: {
    // The source operand type has twice as many elements of half the size.
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    llvm::Type *EltTy =
      llvm::IntegerType::get(getLLVMContext(), EltBits / 2);
    auto *NarrowTy =
        llvm::FixedVectorType::get(EltTy, VTy->getNumElements() * 2);
    llvm::Type *Tys[2] = { Ty, NarrowTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vpaddl_v:
  case NEON::BI__builtin_neon_vpaddlq_v: {
    // The source operand type has twice as many elements of half the size.
    unsigned EltBits = VTy->getElementType()->getPrimitiveSizeInBits();
    llvm::Type *EltTy = llvm::IntegerType::get(getLLVMContext(), EltBits / 2);
    auto *NarrowTy =
        llvm::FixedVectorType::get(EltTy, VTy->getNumElements() * 2);
    llvm::Type *Tys[2] = { Ty, NarrowTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vpaddl");
  }
  case NEON::BI__builtin_neon_vqdmlal_v:
  case NEON::BI__builtin_neon_vqdmlsl_v: {
    SmallVector<Value *, 2> MulOps(Ops.begin() + 1, Ops.end());
    Ops[1] =
        EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Ty), MulOps, "vqdmlal");
    Ops.resize(2);
    return EmitNeonCall(CGM.getIntrinsic(AltLLVMIntrinsic, Ty), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vqdmulhq_lane_v:
  case NEON::BI__builtin_neon_vqdmulh_lane_v:
  case NEON::BI__builtin_neon_vqrdmulhq_lane_v:
  case NEON::BI__builtin_neon_vqrdmulh_lane_v: {
    auto *RTy = cast<llvm::FixedVectorType>(Ty);
    if (BuiltinID == NEON::BI__builtin_neon_vqdmulhq_lane_v ||
        BuiltinID == NEON::BI__builtin_neon_vqrdmulhq_lane_v)
      RTy = llvm::FixedVectorType::get(RTy->getElementType(),
                                       RTy->getNumElements() * 2);
    llvm::Type *Tys[2] = {
        RTy, GetNeonType(this, NeonTypeFlags(Type.getEltType(), false,
                                             /*isQuad*/ false))};
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vqdmulhq_laneq_v:
  case NEON::BI__builtin_neon_vqdmulh_laneq_v:
  case NEON::BI__builtin_neon_vqrdmulhq_laneq_v:
  case NEON::BI__builtin_neon_vqrdmulh_laneq_v: {
    llvm::Type *Tys[2] = {
        Ty, GetNeonType(this, NeonTypeFlags(Type.getEltType(), false,
                                            /*isQuad*/ true))};
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, NameHint);
  }
  case NEON::BI__builtin_neon_vqshl_n_v:
  case NEON::BI__builtin_neon_vqshlq_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshl_n",
                        1, false);
  case NEON::BI__builtin_neon_vqshlu_n_v:
  case NEON::BI__builtin_neon_vqshluq_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshlu_n",
                        1, false);
  case NEON::BI__builtin_neon_vrecpe_v:
  case NEON::BI__builtin_neon_vrecpeq_v:
  case NEON::BI__builtin_neon_vrsqrte_v:
  case NEON::BI__builtin_neon_vrsqrteq_v:
    Int = Ty->isFPOrFPVectorTy() ? LLVMIntrinsic : AltLLVMIntrinsic;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, NameHint);
  case NEON::BI__builtin_neon_vrndi_v:
  case NEON::BI__builtin_neon_vrndiq_v:
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_nearbyint
              : Intrinsic::nearbyint;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, NameHint);
  case NEON::BI__builtin_neon_vrshr_n_v:
  case NEON::BI__builtin_neon_vrshrq_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrshr_n",
                        1, true);
  case NEON::BI__builtin_neon_vsha512hq_u64:
  case NEON::BI__builtin_neon_vsha512h2q_u64:
  case NEON::BI__builtin_neon_vsha512su0q_u64:
  case NEON::BI__builtin_neon_vsha512su1q_u64: {
    Function *F = CGM.getIntrinsic(Int);
    return EmitNeonCall(F, Ops, "");
  }
  case NEON::BI__builtin_neon_vshl_n_v:
  case NEON::BI__builtin_neon_vshlq_n_v:
    Ops[1] = EmitNeonShiftVector(Ops[1], Ty, false);
    return Builder.CreateShl(Builder.CreateBitCast(Ops[0],Ty), Ops[1],
                             "vshl_n");
  case NEON::BI__builtin_neon_vshll_n_v: {
    llvm::FixedVectorType *SrcTy =
        llvm::FixedVectorType::getTruncatedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    if (Usgn)
      Ops[0] = Builder.CreateZExt(Ops[0], VTy);
    else
      Ops[0] = Builder.CreateSExt(Ops[0], VTy);
    Ops[1] = EmitNeonShiftVector(Ops[1], VTy, false);
    return Builder.CreateShl(Ops[0], Ops[1], "vshll_n");
  }
  case NEON::BI__builtin_neon_vshrn_n_v: {
    llvm::FixedVectorType *SrcTy =
        llvm::FixedVectorType::getExtendedElementVectorType(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = EmitNeonShiftVector(Ops[1], SrcTy, false);
    if (Usgn)
      Ops[0] = Builder.CreateLShr(Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateAShr(Ops[0], Ops[1]);
    return Builder.CreateTrunc(Ops[0], Ty, "vshrn_n");
  }
  case NEON::BI__builtin_neon_vshr_n_v:
  case NEON::BI__builtin_neon_vshrq_n_v:
    return EmitNeonRShiftImm(Ops[0], Ops[1], Ty, Usgn, "vshr_n");
  case NEON::BI__builtin_neon_vst1_v:
  case NEON::BI__builtin_neon_vst1q_v:
  case NEON::BI__builtin_neon_vst2_v:
  case NEON::BI__builtin_neon_vst2q_v:
  case NEON::BI__builtin_neon_vst3_v:
  case NEON::BI__builtin_neon_vst3q_v:
  case NEON::BI__builtin_neon_vst4_v:
  case NEON::BI__builtin_neon_vst4q_v:
  case NEON::BI__builtin_neon_vst2_lane_v:
  case NEON::BI__builtin_neon_vst2q_lane_v:
  case NEON::BI__builtin_neon_vst3_lane_v:
  case NEON::BI__builtin_neon_vst3q_lane_v:
  case NEON::BI__builtin_neon_vst4_lane_v:
  case NEON::BI__builtin_neon_vst4q_lane_v: {
    llvm::Type *Tys[] = {Int8PtrTy, Ty};
    Ops.push_back(getAlignmentValue32(PtrOp0));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "");
  }
  case NEON::BI__builtin_neon_vsm3partw1q_u32:
  case NEON::BI__builtin_neon_vsm3partw2q_u32:
  case NEON::BI__builtin_neon_vsm3ss1q_u32:
  case NEON::BI__builtin_neon_vsm4ekeyq_u32:
  case NEON::BI__builtin_neon_vsm4eq_u32: {
    Function *F = CGM.getIntrinsic(Int);
    return EmitNeonCall(F, Ops, "");
  }
  case NEON::BI__builtin_neon_vsm3tt1aq_u32:
  case NEON::BI__builtin_neon_vsm3tt1bq_u32:
  case NEON::BI__builtin_neon_vsm3tt2aq_u32:
  case NEON::BI__builtin_neon_vsm3tt2bq_u32: {
    Function *F = CGM.getIntrinsic(Int);
    Ops[3] = Builder.CreateZExt(Ops[3], Int64Ty);
    return EmitNeonCall(F, Ops, "");
  }
  case NEON::BI__builtin_neon_vst1_x2_v:
  case NEON::BI__builtin_neon_vst1q_x2_v:
  case NEON::BI__builtin_neon_vst1_x3_v:
  case NEON::BI__builtin_neon_vst1q_x3_v:
  case NEON::BI__builtin_neon_vst1_x4_v:
  case NEON::BI__builtin_neon_vst1q_x4_v: {
    // TODO: Currently in AArch32 mode the pointer operand comes first, whereas
    // in AArch64 it comes last. We may want to stick to one or another.
    if (Arch == llvm::Triple::aarch64 || Arch == llvm::Triple::aarch64_be ||
        Arch == llvm::Triple::aarch64_32) {
      llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
      std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
      return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, "");
    }
    llvm::Type *Tys[2] = {UnqualPtrTy, VTy};
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, "");
  }
  case NEON::BI__builtin_neon_vsubhn_v: {
    llvm::FixedVectorType *SrcTy =
        llvm::FixedVectorType::getExtendedElementVectorType(VTy);

    // %sum = add <4 x i32> %lhs, %rhs
    Ops[0] = Builder.CreateBitCast(Ops[0], SrcTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], SrcTy);
    Ops[0] = Builder.CreateSub(Ops[0], Ops[1], "vsubhn");

    // %high = lshr <4 x i32> %sum, <i32 16, i32 16, i32 16, i32 16>
    Constant *ShiftAmt =
        ConstantInt::get(SrcTy, SrcTy->getScalarSizeInBits() / 2);
    Ops[0] = Builder.CreateLShr(Ops[0], ShiftAmt, "vsubhn");

    // %res = trunc <4 x i32> %high to <4 x i16>
    return Builder.CreateTrunc(Ops[0], VTy, "vsubhn");
  }
  case NEON::BI__builtin_neon_vtrn_v:
  case NEON::BI__builtin_neon_vtrnq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back(i+vi);
        Indices.push_back(i+e+vi);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vtrn");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vtst_v:
  case NEON::BI__builtin_neon_vtstq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[0] = Builder.CreateAnd(Ops[0], Ops[1]);
    Ops[0] = Builder.CreateICmp(ICmpInst::ICMP_NE, Ops[0],
                                ConstantAggregateZero::get(Ty));
    return Builder.CreateSExt(Ops[0], Ty, "vtst");
  }
  case NEON::BI__builtin_neon_vuzp_v:
  case NEON::BI__builtin_neon_vuzpq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
        Indices.push_back(2*i+vi);

      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vuzp");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vxarq_u64: {
    Function *F = CGM.getIntrinsic(Int);
    Ops[2] = Builder.CreateZExt(Ops[2], Int64Ty);
    return EmitNeonCall(F, Ops, "");
  }
  case NEON::BI__builtin_neon_vzip_v:
  case NEON::BI__builtin_neon_vzipq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back((i + vi*e) >> 1);
        Indices.push_back(((i + vi*e) >> 1)+e);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vzip");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vdot_s32:
  case NEON::BI__builtin_neon_vdot_u32:
  case NEON::BI__builtin_neon_vdotq_s32:
  case NEON::BI__builtin_neon_vdotq_u32: {
    auto *InputTy =
        llvm::FixedVectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vdot");
  }
  case NEON::BI__builtin_neon_vfmlal_low_f16:
  case NEON::BI__builtin_neon_vfmlalq_low_f16: {
    auto *InputTy =
        llvm::FixedVectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vfmlal_low");
  }
  case NEON::BI__builtin_neon_vfmlsl_low_f16:
  case NEON::BI__builtin_neon_vfmlslq_low_f16: {
    auto *InputTy =
        llvm::FixedVectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vfmlsl_low");
  }
  case NEON::BI__builtin_neon_vfmlal_high_f16:
  case NEON::BI__builtin_neon_vfmlalq_high_f16: {
    auto *InputTy =
        llvm::FixedVectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vfmlal_high");
  }
  case NEON::BI__builtin_neon_vfmlsl_high_f16:
  case NEON::BI__builtin_neon_vfmlslq_high_f16: {
    auto *InputTy =
        llvm::FixedVectorType::get(HalfTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vfmlsl_high");
  }
  case NEON::BI__builtin_neon_vmmlaq_s32:
  case NEON::BI__builtin_neon_vmmlaq_u32: {
    auto *InputTy =
        llvm::FixedVectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(LLVMIntrinsic, Tys), Ops, "vmmla");
  }
  case NEON::BI__builtin_neon_vusmmlaq_s32: {
    auto *InputTy =
        llvm::FixedVectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vusmmla");
  }
  case NEON::BI__builtin_neon_vusdot_s32:
  case NEON::BI__builtin_neon_vusdotq_s32: {
    auto *InputTy =
        llvm::FixedVectorType::get(Int8Ty, Ty->getPrimitiveSizeInBits() / 8);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vusdot");
  }
  case NEON::BI__builtin_neon_vbfdot_f32:
  case NEON::BI__builtin_neon_vbfdotq_f32: {
    llvm::Type *InputTy =
        llvm::FixedVectorType::get(BFloatTy, Ty->getPrimitiveSizeInBits() / 16);
    llvm::Type *Tys[2] = { Ty, InputTy };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vbfdot");
  }
  case NEON::BI__builtin_neon___a32_vcvt_bf16_f32: {
    llvm::Type *Tys[1] = { Ty };
    Function *F = CGM.getIntrinsic(Int, Tys);
    return EmitNeonCall(F, Ops, "vcvtfp2bf");
  }

  }

  assert(Int && "Expected valid intrinsic number");

  // Determine the type(s) of this overloaded AArch64 intrinsic.
  Function *F = LookupNeonLLVMIntrinsic(Int, Modifier, Ty, E);

  Value *Result = EmitNeonCall(F, Ops, NameHint);
  llvm::Type *ResultType = ConvertType(E->getType());
  // AArch64 intrinsic one-element vector type cast to
  // scalar type expected by the builtin
  return Builder.CreateBitCast(Result, ResultType, NameHint);
}

Value *CodeGenFunction::EmitAArch64CompareBuiltinExpr(
    Value *Op, llvm::Type *Ty, const CmpInst::Predicate Fp,
    const CmpInst::Predicate Ip, const Twine &Name) {
  llvm::Type *OTy = Op->getType();

  // FIXME: this is utterly horrific. We should not be looking at previous
  // codegen context to find out what needs doing. Unfortunately TableGen
  // currently gives us exactly the same calls for vceqz_f32 and vceqz_s32
  // (etc).
  if (BitCastInst *BI = dyn_cast<BitCastInst>(Op))
    OTy = BI->getOperand(0)->getType();

  Op = Builder.CreateBitCast(Op, OTy);
  if (OTy->getScalarType()->isFloatingPointTy()) {
    if (Fp == CmpInst::FCMP_OEQ)
      Op = Builder.CreateFCmp(Fp, Op, Constant::getNullValue(OTy));
    else
      Op = Builder.CreateFCmpS(Fp, Op, Constant::getNullValue(OTy));
  } else {
    Op = Builder.CreateICmp(Ip, Op, Constant::getNullValue(OTy));
  }
  return Builder.CreateSExt(Op, Ty, Name);
}

static Value *packTBLDVectorList(CodeGenFunction &CGF, ArrayRef<Value *> Ops,
                                 Value *ExtOp, Value *IndexOp,
                                 llvm::Type *ResTy, unsigned IntID,
                                 const char *Name) {
  SmallVector<Value *, 2> TblOps;
  if (ExtOp)
    TblOps.push_back(ExtOp);

  // Build a vector containing sequential number like (0, 1, 2, ..., 15)
  SmallVector<int, 16> Indices;
  auto *TblTy = cast<llvm::FixedVectorType>(Ops[0]->getType());
  for (unsigned i = 0, e = TblTy->getNumElements(); i != e; ++i) {
    Indices.push_back(2*i);
    Indices.push_back(2*i+1);
  }

  int PairPos = 0, End = Ops.size() - 1;
  while (PairPos < End) {
    TblOps.push_back(CGF.Builder.CreateShuffleVector(Ops[PairPos],
                                                     Ops[PairPos+1], Indices,
                                                     Name));
    PairPos += 2;
  }

  // If there's an odd number of 64-bit lookup table, fill the high 64-bit
  // of the 128-bit lookup table with zero.
  if (PairPos == End) {
    Value *ZeroTbl = ConstantAggregateZero::get(TblTy);
    TblOps.push_back(CGF.Builder.CreateShuffleVector(Ops[PairPos],
                                                     ZeroTbl, Indices, Name));
  }

  Function *TblF;
  TblOps.push_back(IndexOp);
  TblF = CGF.CGM.getIntrinsic(IntID, ResTy);

  return CGF.EmitNeonCall(TblF, TblOps, Name);
}

Value *CodeGenFunction::GetValueForARMHint(unsigned BuiltinID) {
  unsigned Value;
  switch (BuiltinID) {
  default:
    return nullptr;
  case clang::ARM::BI__builtin_arm_nop:
    Value = 0;
    break;
  case clang::ARM::BI__builtin_arm_yield:
  case clang::ARM::BI__yield:
    Value = 1;
    break;
  case clang::ARM::BI__builtin_arm_wfe:
  case clang::ARM::BI__wfe:
    Value = 2;
    break;
  case clang::ARM::BI__builtin_arm_wfi:
  case clang::ARM::BI__wfi:
    Value = 3;
    break;
  case clang::ARM::BI__builtin_arm_sev:
  case clang::ARM::BI__sev:
    Value = 4;
    break;
  case clang::ARM::BI__builtin_arm_sevl:
  case clang::ARM::BI__sevl:
    Value = 5;
    break;
  }

  return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_hint),
                            llvm::ConstantInt::get(Int32Ty, Value));
}

enum SpecialRegisterAccessKind {
  NormalRead,
  VolatileRead,
  Write,
};

// Generates the IR for __builtin_read_exec_*.
// Lowers the builtin to amdgcn_ballot intrinsic.
static Value *EmitAMDGCNBallotForExec(CodeGenFunction &CGF, const CallExpr *E,
                                      llvm::Type *RegisterType,
                                      llvm::Type *ValueType, bool isExecHi) {
  CodeGen::CGBuilderTy &Builder = CGF.Builder;
  CodeGen::CodeGenModule &CGM = CGF.CGM;

  Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_ballot, {RegisterType});
  llvm::Value *Call = Builder.CreateCall(F, {Builder.getInt1(true)});

  if (isExecHi) {
    Value *Rt2 = Builder.CreateLShr(Call, 32);
    Rt2 = Builder.CreateTrunc(Rt2, CGF.Int32Ty);
    return Rt2;
  }

  return Call;
}

// Generates the IR for the read/write special register builtin,
// ValueType is the type of the value that is to be written or read,
// RegisterType is the type of the register being written to or read from.
static Value *EmitSpecialRegisterBuiltin(CodeGenFunction &CGF,
                                         const CallExpr *E,
                                         llvm::Type *RegisterType,
                                         llvm::Type *ValueType,
                                         SpecialRegisterAccessKind AccessKind,
                                         StringRef SysReg = "") {
  // write and register intrinsics only support 32, 64 and 128 bit operations.
  assert((RegisterType->isIntegerTy(32) || RegisterType->isIntegerTy(64) ||
          RegisterType->isIntegerTy(128)) &&
         "Unsupported size for register.");

  CodeGen::CGBuilderTy &Builder = CGF.Builder;
  CodeGen::CodeGenModule &CGM = CGF.CGM;
  LLVMContext &Context = CGM.getLLVMContext();

  if (SysReg.empty()) {
    const Expr *SysRegStrExpr = E->getArg(0)->IgnoreParenCasts();
    SysReg = cast<clang::StringLiteral>(SysRegStrExpr)->getString();
  }

  llvm::Metadata *Ops[] = { llvm::MDString::get(Context, SysReg) };
  llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
  llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

  llvm::Type *Types[] = { RegisterType };

  bool MixedTypes = RegisterType->isIntegerTy(64) && ValueType->isIntegerTy(32);
  assert(!(RegisterType->isIntegerTy(32) && ValueType->isIntegerTy(64))
            && "Can't fit 64-bit value in 32-bit register");

  if (AccessKind != Write) {
    assert(AccessKind == NormalRead || AccessKind == VolatileRead);
    llvm::Function *F = CGM.getIntrinsic(
        AccessKind == VolatileRead ? llvm::Intrinsic::read_volatile_register
                                   : llvm::Intrinsic::read_register,
        Types);
    llvm::Value *Call = Builder.CreateCall(F, Metadata);

    if (MixedTypes)
      // Read into 64 bit register and then truncate result to 32 bit.
      return Builder.CreateTrunc(Call, ValueType);

    if (ValueType->isPointerTy())
      // Have i32/i64 result (Call) but want to return a VoidPtrTy (i8*).
      return Builder.CreateIntToPtr(Call, ValueType);

    return Call;
  }

  llvm::Function *F = CGM.getIntrinsic(llvm::Intrinsic::write_register, Types);
  llvm::Value *ArgValue = CGF.EmitScalarExpr(E->getArg(1));
  if (MixedTypes) {
    // Extend 32 bit write value to 64 bit to pass to write.
    ArgValue = Builder.CreateZExt(ArgValue, RegisterType);
    return Builder.CreateCall(F, { Metadata, ArgValue });
  }

  if (ValueType->isPointerTy()) {
    // Have VoidPtrTy ArgValue but want to return an i32/i64.
    ArgValue = Builder.CreatePtrToInt(ArgValue, RegisterType);
    return Builder.CreateCall(F, { Metadata, ArgValue });
  }

  return Builder.CreateCall(F, { Metadata, ArgValue });
}

/// Return true if BuiltinID is an overloaded Neon intrinsic with an extra
/// argument that specifies the vector type.
static bool HasExtraNeonArgument(unsigned BuiltinID) {
  switch (BuiltinID) {
  default: break;
  case NEON::BI__builtin_neon_vget_lane_i8:
  case NEON::BI__builtin_neon_vget_lane_i16:
  case NEON::BI__builtin_neon_vget_lane_bf16:
  case NEON::BI__builtin_neon_vget_lane_i32:
  case NEON::BI__builtin_neon_vget_lane_i64:
  case NEON::BI__builtin_neon_vget_lane_f32:
  case NEON::BI__builtin_neon_vgetq_lane_i8:
  case NEON::BI__builtin_neon_vgetq_lane_i16:
  case NEON::BI__builtin_neon_vgetq_lane_bf16:
  case NEON::BI__builtin_neon_vgetq_lane_i32:
  case NEON::BI__builtin_neon_vgetq_lane_i64:
  case NEON::BI__builtin_neon_vgetq_lane_f32:
  case NEON::BI__builtin_neon_vduph_lane_bf16:
  case NEON::BI__builtin_neon_vduph_laneq_bf16:
  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_bf16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_bf16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
  case NEON::BI__builtin_neon_vsha1h_u32:
  case NEON::BI__builtin_neon_vsha1cq_u32:
  case NEON::BI__builtin_neon_vsha1pq_u32:
  case NEON::BI__builtin_neon_vsha1mq_u32:
  case NEON::BI__builtin_neon_vcvth_bf16_f32:
  case clang::ARM::BI_MoveToCoprocessor:
  case clang::ARM::BI_MoveToCoprocessor2:
    return false;
  }
  return true;
}

Value *CodeGenFunction::EmitARMBuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E,
                                           ReturnValueSlot ReturnValue,
                                           llvm::Triple::ArchType Arch) {
  if (auto Hint = GetValueForARMHint(BuiltinID))
    return Hint;

  if (BuiltinID == clang::ARM::BI__emit) {
    bool IsThumb = getTarget().getTriple().getArch() == llvm::Triple::thumb;
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(VoidTy, /*Variadic=*/false);

    Expr::EvalResult Result;
    if (!E->getArg(0)->EvaluateAsInt(Result, CGM.getContext()))
      llvm_unreachable("Sema will ensure that the parameter is constant");

    llvm::APSInt Value = Result.Val.getInt();
    uint64_t ZExtValue = Value.zextOrTrunc(IsThumb ? 16 : 32).getZExtValue();

    llvm::InlineAsm *Emit =
        IsThumb ? InlineAsm::get(FTy, ".inst.n 0x" + utohexstr(ZExtValue), "",
                                 /*hasSideEffects=*/true)
                : InlineAsm::get(FTy, ".inst 0x" + utohexstr(ZExtValue), "",
                                 /*hasSideEffects=*/true);

    return Builder.CreateCall(Emit);
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_dbg) {
    Value *Option = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_dbg), Option);
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_prefetch) {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *RW      = EmitScalarExpr(E->getArg(1));
    Value *IsData  = EmitScalarExpr(E->getArg(2));

    // Locality is not supported on ARM target
    Value *Locality = llvm::ConstantInt::get(Int32Ty, 3);

    Function *F = CGM.getIntrinsic(Intrinsic::prefetch, Address->getType());
    return Builder.CreateCall(F, {Address, RW, Locality, IsData});
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_rbit) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::bitreverse, Arg->getType()), Arg, "rbit");
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_clz ||
      BuiltinID == clang::ARM::BI__builtin_arm_clz64) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, Arg->getType());
    Value *Res = Builder.CreateCall(F, {Arg, Builder.getInt1(false)});
    if (BuiltinID == clang::ARM::BI__builtin_arm_clz64)
      Res = Builder.CreateTrunc(Res, Builder.getInt32Ty());
    return Res;
  }


  if (BuiltinID == clang::ARM::BI__builtin_arm_cls) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_cls), Arg, "cls");
  }
  if (BuiltinID == clang::ARM::BI__builtin_arm_cls64) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_cls64), Arg,
                              "cls");
  }

  if (BuiltinID == clang::ARM::BI__clear_cache) {
    assert(E->getNumArgs() == 2 && "__clear_cache takes 2 arguments");
    const FunctionDecl *FD = E->getDirectCallee();
    Value *Ops[2];
    for (unsigned i = 0; i < 2; i++)
      Ops[i] = EmitScalarExpr(E->getArg(i));
    llvm::Type *Ty = CGM.getTypes().ConvertType(FD->getType());
    llvm::FunctionType *FTy = cast<llvm::FunctionType>(Ty);
    StringRef Name = FD->getName();
    return EmitNounwindRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name), Ops);
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_mcrr ||
      BuiltinID == clang::ARM::BI__builtin_arm_mcrr2) {
    Function *F;

    switch (BuiltinID) {
    default: llvm_unreachable("unexpected builtin");
    case clang::ARM::BI__builtin_arm_mcrr:
      F = CGM.getIntrinsic(Intrinsic::arm_mcrr);
      break;
    case clang::ARM::BI__builtin_arm_mcrr2:
      F = CGM.getIntrinsic(Intrinsic::arm_mcrr2);
      break;
    }

    // MCRR{2} instruction has 5 operands but
    // the intrinsic has 4 because Rt and Rt2
    // are represented as a single unsigned 64
    // bit integer in the intrinsic definition
    // but internally it's represented as 2 32
    // bit integers.

    Value *Coproc = EmitScalarExpr(E->getArg(0));
    Value *Opc1 = EmitScalarExpr(E->getArg(1));
    Value *RtAndRt2 = EmitScalarExpr(E->getArg(2));
    Value *CRm = EmitScalarExpr(E->getArg(3));

    Value *C1 = llvm::ConstantInt::get(Int64Ty, 32);
    Value *Rt = Builder.CreateTruncOrBitCast(RtAndRt2, Int32Ty);
    Value *Rt2 = Builder.CreateLShr(RtAndRt2, C1);
    Rt2 = Builder.CreateTruncOrBitCast(Rt2, Int32Ty);

    return Builder.CreateCall(F, {Coproc, Opc1, Rt, Rt2, CRm});
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_mrrc ||
      BuiltinID == clang::ARM::BI__builtin_arm_mrrc2) {
    Function *F;

    switch (BuiltinID) {
    default: llvm_unreachable("unexpected builtin");
    case clang::ARM::BI__builtin_arm_mrrc:
      F = CGM.getIntrinsic(Intrinsic::arm_mrrc);
      break;
    case clang::ARM::BI__builtin_arm_mrrc2:
      F = CGM.getIntrinsic(Intrinsic::arm_mrrc2);
      break;
    }

    Value *Coproc = EmitScalarExpr(E->getArg(0));
    Value *Opc1 = EmitScalarExpr(E->getArg(1));
    Value *CRm  = EmitScalarExpr(E->getArg(2));
    Value *RtAndRt2 = Builder.CreateCall(F, {Coproc, Opc1, CRm});

    // Returns an unsigned 64 bit integer, represented
    // as two 32 bit integers.

    Value *Rt = Builder.CreateExtractValue(RtAndRt2, 1);
    Value *Rt1 = Builder.CreateExtractValue(RtAndRt2, 0);
    Rt = Builder.CreateZExt(Rt, Int64Ty);
    Rt1 = Builder.CreateZExt(Rt1, Int64Ty);

    Value *ShiftCast = llvm::ConstantInt::get(Int64Ty, 32);
    RtAndRt2 = Builder.CreateShl(Rt, ShiftCast, "shl", true);
    RtAndRt2 = Builder.CreateOr(RtAndRt2, Rt1);

    return Builder.CreateBitCast(RtAndRt2, ConvertType(E->getType()));
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_ldrexd ||
      ((BuiltinID == clang::ARM::BI__builtin_arm_ldrex ||
        BuiltinID == clang::ARM::BI__builtin_arm_ldaex) &&
       getContext().getTypeSize(E->getType()) == 64) ||
      BuiltinID == clang::ARM::BI__ldrexd) {
    Function *F;

    switch (BuiltinID) {
    default: llvm_unreachable("unexpected builtin");
    case clang::ARM::BI__builtin_arm_ldaex:
      F = CGM.getIntrinsic(Intrinsic::arm_ldaexd);
      break;
    case clang::ARM::BI__builtin_arm_ldrexd:
    case clang::ARM::BI__builtin_arm_ldrex:
    case clang::ARM::BI__ldrexd:
      F = CGM.getIntrinsic(Intrinsic::arm_ldrexd);
      break;
    }

    Value *LdPtr = EmitScalarExpr(E->getArg(0));
    Value *Val = Builder.CreateCall(F, LdPtr, "ldrexd");

    Value *Val0 = Builder.CreateExtractValue(Val, 1);
    Value *Val1 = Builder.CreateExtractValue(Val, 0);
    Val0 = Builder.CreateZExt(Val0, Int64Ty);
    Val1 = Builder.CreateZExt(Val1, Int64Ty);

    Value *ShiftCst = llvm::ConstantInt::get(Int64Ty, 32);
    Val = Builder.CreateShl(Val0, ShiftCst, "shl", true /* nuw */);
    Val = Builder.CreateOr(Val, Val1);
    return Builder.CreateBitCast(Val, ConvertType(E->getType()));
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_ldrex ||
      BuiltinID == clang::ARM::BI__builtin_arm_ldaex) {
    Value *LoadAddr = EmitScalarExpr(E->getArg(0));

    QualType Ty = E->getType();
    llvm::Type *RealResTy = ConvertType(Ty);
    llvm::Type *IntTy =
        llvm::IntegerType::get(getLLVMContext(), getContext().getTypeSize(Ty));

    Function *F = CGM.getIntrinsic(
        BuiltinID == clang::ARM::BI__builtin_arm_ldaex ? Intrinsic::arm_ldaex
                                                       : Intrinsic::arm_ldrex,
        UnqualPtrTy);
    CallInst *Val = Builder.CreateCall(F, LoadAddr, "ldrex");
    Val->addParamAttr(
        0, Attribute::get(getLLVMContext(), Attribute::ElementType, IntTy));

    if (RealResTy->isPointerTy())
      return Builder.CreateIntToPtr(Val, RealResTy);
    else {
      llvm::Type *IntResTy = llvm::IntegerType::get(
          getLLVMContext(), CGM.getDataLayout().getTypeSizeInBits(RealResTy));
      return Builder.CreateBitCast(Builder.CreateTruncOrBitCast(Val, IntResTy),
                                   RealResTy);
    }
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_strexd ||
      ((BuiltinID == clang::ARM::BI__builtin_arm_stlex ||
        BuiltinID == clang::ARM::BI__builtin_arm_strex) &&
       getContext().getTypeSize(E->getArg(0)->getType()) == 64)) {
    Function *F = CGM.getIntrinsic(
        BuiltinID == clang::ARM::BI__builtin_arm_stlex ? Intrinsic::arm_stlexd
                                                       : Intrinsic::arm_strexd);
    llvm::Type *STy = llvm::StructType::get(Int32Ty, Int32Ty);

    Address Tmp = CreateMemTemp(E->getArg(0)->getType());
    Value *Val = EmitScalarExpr(E->getArg(0));
    Builder.CreateStore(Val, Tmp);

    Address LdPtr = Tmp.withElementType(STy);
    Val = Builder.CreateLoad(LdPtr);

    Value *Arg0 = Builder.CreateExtractValue(Val, 0);
    Value *Arg1 = Builder.CreateExtractValue(Val, 1);
    Value *StPtr = EmitScalarExpr(E->getArg(1));
    return Builder.CreateCall(F, {Arg0, Arg1, StPtr}, "strexd");
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_strex ||
      BuiltinID == clang::ARM::BI__builtin_arm_stlex) {
    Value *StoreVal = EmitScalarExpr(E->getArg(0));
    Value *StoreAddr = EmitScalarExpr(E->getArg(1));

    QualType Ty = E->getArg(0)->getType();
    llvm::Type *StoreTy =
        llvm::IntegerType::get(getLLVMContext(), getContext().getTypeSize(Ty));

    if (StoreVal->getType()->isPointerTy())
      StoreVal = Builder.CreatePtrToInt(StoreVal, Int32Ty);
    else {
      llvm::Type *IntTy = llvm::IntegerType::get(
          getLLVMContext(),
          CGM.getDataLayout().getTypeSizeInBits(StoreVal->getType()));
      StoreVal = Builder.CreateBitCast(StoreVal, IntTy);
      StoreVal = Builder.CreateZExtOrBitCast(StoreVal, Int32Ty);
    }

    Function *F = CGM.getIntrinsic(
        BuiltinID == clang::ARM::BI__builtin_arm_stlex ? Intrinsic::arm_stlex
                                                       : Intrinsic::arm_strex,
        StoreAddr->getType());

    CallInst *CI = Builder.CreateCall(F, {StoreVal, StoreAddr}, "strex");
    CI->addParamAttr(
        1, Attribute::get(getLLVMContext(), Attribute::ElementType, StoreTy));
    return CI;
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_clrex) {
    Function *F = CGM.getIntrinsic(Intrinsic::arm_clrex);
    return Builder.CreateCall(F);
  }

  // CRC32
  Intrinsic::ID CRCIntrinsicID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  case clang::ARM::BI__builtin_arm_crc32b:
    CRCIntrinsicID = Intrinsic::arm_crc32b; break;
  case clang::ARM::BI__builtin_arm_crc32cb:
    CRCIntrinsicID = Intrinsic::arm_crc32cb; break;
  case clang::ARM::BI__builtin_arm_crc32h:
    CRCIntrinsicID = Intrinsic::arm_crc32h; break;
  case clang::ARM::BI__builtin_arm_crc32ch:
    CRCIntrinsicID = Intrinsic::arm_crc32ch; break;
  case clang::ARM::BI__builtin_arm_crc32w:
  case clang::ARM::BI__builtin_arm_crc32d:
    CRCIntrinsicID = Intrinsic::arm_crc32w; break;
  case clang::ARM::BI__builtin_arm_crc32cw:
  case clang::ARM::BI__builtin_arm_crc32cd:
    CRCIntrinsicID = Intrinsic::arm_crc32cw; break;
  }

  if (CRCIntrinsicID != Intrinsic::not_intrinsic) {
    Value *Arg0 = EmitScalarExpr(E->getArg(0));
    Value *Arg1 = EmitScalarExpr(E->getArg(1));

    // crc32{c,}d intrinsics are implemented as two calls to crc32{c,}w
    // intrinsics, hence we need different codegen for these cases.
    if (BuiltinID == clang::ARM::BI__builtin_arm_crc32d ||
        BuiltinID == clang::ARM::BI__builtin_arm_crc32cd) {
      Value *C1 = llvm::ConstantInt::get(Int64Ty, 32);
      Value *Arg1a = Builder.CreateTruncOrBitCast(Arg1, Int32Ty);
      Value *Arg1b = Builder.CreateLShr(Arg1, C1);
      Arg1b = Builder.CreateTruncOrBitCast(Arg1b, Int32Ty);

      Function *F = CGM.getIntrinsic(CRCIntrinsicID);
      Value *Res = Builder.CreateCall(F, {Arg0, Arg1a});
      return Builder.CreateCall(F, {Res, Arg1b});
    } else {
      Arg1 = Builder.CreateZExtOrBitCast(Arg1, Int32Ty);

      Function *F = CGM.getIntrinsic(CRCIntrinsicID);
      return Builder.CreateCall(F, {Arg0, Arg1});
    }
  }

  if (BuiltinID == clang::ARM::BI__builtin_arm_rsr ||
      BuiltinID == clang::ARM::BI__builtin_arm_rsr64 ||
      BuiltinID == clang::ARM::BI__builtin_arm_rsrp ||
      BuiltinID == clang::ARM::BI__builtin_arm_wsr ||
      BuiltinID == clang::ARM::BI__builtin_arm_wsr64 ||
      BuiltinID == clang::ARM::BI__builtin_arm_wsrp) {

    SpecialRegisterAccessKind AccessKind = Write;
    if (BuiltinID == clang::ARM::BI__builtin_arm_rsr ||
        BuiltinID == clang::ARM::BI__builtin_arm_rsr64 ||
        BuiltinID == clang::ARM::BI__builtin_arm_rsrp)
      AccessKind = VolatileRead;

    bool IsPointerBuiltin = BuiltinID == clang::ARM::BI__builtin_arm_rsrp ||
                            BuiltinID == clang::ARM::BI__builtin_arm_wsrp;

    bool Is64Bit = BuiltinID == clang::ARM::BI__builtin_arm_rsr64 ||
                   BuiltinID == clang::ARM::BI__builtin_arm_wsr64;

    llvm::Type *ValueType;
    llvm::Type *RegisterType;
    if (IsPointerBuiltin) {
      ValueType = VoidPtrTy;
      RegisterType = Int32Ty;
    } else if (Is64Bit) {
      ValueType = RegisterType = Int64Ty;
    } else {
      ValueType = RegisterType = Int32Ty;
    }

    return EmitSpecialRegisterBuiltin(*this, E, RegisterType, ValueType,
                                      AccessKind);
  }

  if (BuiltinID == ARM::BI__builtin_sponentry) {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::sponentry, AllocaInt8PtrTy);
    return Builder.CreateCall(F);
  }

  // Handle MSVC intrinsics before argument evaluation to prevent double
  // evaluation.
  if (std::optional<MSVCIntrin> MsvcIntId = translateArmToMsvcIntrin(BuiltinID))
    return EmitMSVCBuiltinExpr(*MsvcIntId, E);

  // Deal with MVE builtins
  if (Value *Result = EmitARMMVEBuiltinExpr(BuiltinID, E, ReturnValue, Arch))
    return Result;
  // Handle CDE builtins
  if (Value *Result = EmitARMCDEBuiltinExpr(BuiltinID, E, ReturnValue, Arch))
    return Result;

  // Some intrinsics are equivalent - if they are use the base intrinsic ID.
  auto It = llvm::find_if(NEONEquivalentIntrinsicMap, [BuiltinID](auto &P) {
    return P.first == BuiltinID;
  });
  if (It != end(NEONEquivalentIntrinsicMap))
    BuiltinID = It->second;

  // Find out if any arguments are required to be integer constant
  // expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  auto getAlignmentValue32 = [&](Address addr) -> Value* {
    return Builder.getInt32(addr.getAlignment().getQuantity());
  };

  Address PtrOp0 = Address::invalid();
  Address PtrOp1 = Address::invalid();
  SmallVector<Value*, 4> Ops;
  bool HasExtraArg = HasExtraNeonArgument(BuiltinID);
  unsigned NumArgs = E->getNumArgs() - (HasExtraArg ? 1 : 0);
  for (unsigned i = 0, e = NumArgs; i != e; i++) {
    if (i == 0) {
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vld1_v:
      case NEON::BI__builtin_neon_vld1q_v:
      case NEON::BI__builtin_neon_vld1q_lane_v:
      case NEON::BI__builtin_neon_vld1_lane_v:
      case NEON::BI__builtin_neon_vld1_dup_v:
      case NEON::BI__builtin_neon_vld1q_dup_v:
      case NEON::BI__builtin_neon_vst1_v:
      case NEON::BI__builtin_neon_vst1q_v:
      case NEON::BI__builtin_neon_vst1q_lane_v:
      case NEON::BI__builtin_neon_vst1_lane_v:
      case NEON::BI__builtin_neon_vst2_v:
      case NEON::BI__builtin_neon_vst2q_v:
      case NEON::BI__builtin_neon_vst2_lane_v:
      case NEON::BI__builtin_neon_vst2q_lane_v:
      case NEON::BI__builtin_neon_vst3_v:
      case NEON::BI__builtin_neon_vst3q_v:
      case NEON::BI__builtin_neon_vst3_lane_v:
      case NEON::BI__builtin_neon_vst3q_lane_v:
      case NEON::BI__builtin_neon_vst4_v:
      case NEON::BI__builtin_neon_vst4q_v:
      case NEON::BI__builtin_neon_vst4_lane_v:
      case NEON::BI__builtin_neon_vst4q_lane_v:
        // Get the alignment for the argument in addition to the value;
        // we'll use it later.
        PtrOp0 = EmitPointerWithAlignment(E->getArg(0));
        Ops.push_back(PtrOp0.emitRawPointer(*this));
        continue;
      }
    }
    if (i == 1) {
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vld2_v:
      case NEON::BI__builtin_neon_vld2q_v:
      case NEON::BI__builtin_neon_vld3_v:
      case NEON::BI__builtin_neon_vld3q_v:
      case NEON::BI__builtin_neon_vld4_v:
      case NEON::BI__builtin_neon_vld4q_v:
      case NEON::BI__builtin_neon_vld2_lane_v:
      case NEON::BI__builtin_neon_vld2q_lane_v:
      case NEON::BI__builtin_neon_vld3_lane_v:
      case NEON::BI__builtin_neon_vld3q_lane_v:
      case NEON::BI__builtin_neon_vld4_lane_v:
      case NEON::BI__builtin_neon_vld4q_lane_v:
      case NEON::BI__builtin_neon_vld2_dup_v:
      case NEON::BI__builtin_neon_vld2q_dup_v:
      case NEON::BI__builtin_neon_vld3_dup_v:
      case NEON::BI__builtin_neon_vld3q_dup_v:
      case NEON::BI__builtin_neon_vld4_dup_v:
      case NEON::BI__builtin_neon_vld4q_dup_v:
        // Get the alignment for the argument in addition to the value;
        // we'll use it later.
        PtrOp1 = EmitPointerWithAlignment(E->getArg(1));
        Ops.push_back(PtrOp1.emitRawPointer(*this));
        continue;
      }
    }

    Ops.push_back(EmitScalarOrConstFoldImmArg(ICEArguments, i, E));
  }

  switch (BuiltinID) {
  default: break;

  case NEON::BI__builtin_neon_vget_lane_i8:
  case NEON::BI__builtin_neon_vget_lane_i16:
  case NEON::BI__builtin_neon_vget_lane_i32:
  case NEON::BI__builtin_neon_vget_lane_i64:
  case NEON::BI__builtin_neon_vget_lane_bf16:
  case NEON::BI__builtin_neon_vget_lane_f32:
  case NEON::BI__builtin_neon_vgetq_lane_i8:
  case NEON::BI__builtin_neon_vgetq_lane_i16:
  case NEON::BI__builtin_neon_vgetq_lane_i32:
  case NEON::BI__builtin_neon_vgetq_lane_i64:
  case NEON::BI__builtin_neon_vgetq_lane_bf16:
  case NEON::BI__builtin_neon_vgetq_lane_f32:
  case NEON::BI__builtin_neon_vduph_lane_bf16:
  case NEON::BI__builtin_neon_vduph_laneq_bf16:
    return Builder.CreateExtractElement(Ops[0], Ops[1], "vget_lane");

  case NEON::BI__builtin_neon_vrndns_f32: {
    Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *Tys[] = {Arg->getType()};
    Function *F = CGM.getIntrinsic(Intrinsic::arm_neon_vrintn, Tys);
    return Builder.CreateCall(F, {Arg}, "vrndn"); }

  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_bf16:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_bf16:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");

  case NEON::BI__builtin_neon_vsha1h_u32:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_sha1h), Ops,
                        "vsha1h");
  case NEON::BI__builtin_neon_vsha1cq_u32:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_sha1c), Ops,
                        "vsha1h");
  case NEON::BI__builtin_neon_vsha1pq_u32:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_sha1p), Ops,
                        "vsha1h");
  case NEON::BI__builtin_neon_vsha1mq_u32:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_sha1m), Ops,
                        "vsha1h");

  case NEON::BI__builtin_neon_vcvth_bf16_f32: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vcvtbfp2bf), Ops,
                        "vcvtbfp2bf");
  }

  // The ARM _MoveToCoprocessor builtins put the input register value as
  // the first argument, but the LLVM intrinsic expects it as the third one.
  case clang::ARM::BI_MoveToCoprocessor:
  case clang::ARM::BI_MoveToCoprocessor2: {
    Function *F = CGM.getIntrinsic(BuiltinID == clang::ARM::BI_MoveToCoprocessor
                                       ? Intrinsic::arm_mcr
                                       : Intrinsic::arm_mcr2);
    return Builder.CreateCall(F, {Ops[1], Ops[2], Ops[0],
                                  Ops[3], Ops[4], Ops[5]});
  }
  }

  // Get the last argument, which specifies the vector type.
  assert(HasExtraArg);
  const Expr *Arg = E->getArg(E->getNumArgs()-1);
  std::optional<llvm::APSInt> Result =
      Arg->getIntegerConstantExpr(getContext());
  if (!Result)
    return nullptr;

  if (BuiltinID == clang::ARM::BI__builtin_arm_vcvtr_f ||
      BuiltinID == clang::ARM::BI__builtin_arm_vcvtr_d) {
    // Determine the overloaded type of this builtin.
    llvm::Type *Ty;
    if (BuiltinID == clang::ARM::BI__builtin_arm_vcvtr_f)
      Ty = FloatTy;
    else
      Ty = DoubleTy;

    // Determine whether this is an unsigned conversion or not.
    bool usgn = Result->getZExtValue() == 1;
    unsigned Int = usgn ? Intrinsic::arm_vcvtru : Intrinsic::arm_vcvtr;

    // Call the appropriate intrinsic.
    Function *F = CGM.getIntrinsic(Int, Ty);
    return Builder.CreateCall(F, Ops, "vcvtr");
  }

  // Determine the type of this overloaded NEON intrinsic.
  NeonTypeFlags Type = Result->getZExtValue();
  bool usgn = Type.isUnsigned();
  bool rightShift = false;

  llvm::FixedVectorType *VTy =
      GetNeonType(this, Type, getTarget().hasLegalHalfType(), false,
                  getTarget().hasBFloat16Type());
  llvm::Type *Ty = VTy;
  if (!Ty)
    return nullptr;

  // Many NEON builtins have identical semantics and uses in ARM and
  // AArch64. Emit these in a single function.
  auto IntrinsicMap = ArrayRef(ARMSIMDIntrinsicMap);
  const ARMVectorIntrinsicInfo *Builtin = findARMVectorIntrinsicInMap(
      IntrinsicMap, BuiltinID, NEONSIMDIntrinsicsProvenSorted);
  if (Builtin)
    return EmitCommonNeonBuiltinExpr(
        Builtin->BuiltinID, Builtin->LLVMIntrinsic, Builtin->AltLLVMIntrinsic,
        Builtin->NameHint, Builtin->TypeModifier, E, Ops, PtrOp0, PtrOp1, Arch);

  unsigned Int;
  switch (BuiltinID) {
  default: return nullptr;
  case NEON::BI__builtin_neon_vld1q_lane_v:
    // Handle 64-bit integer elements as a special case.  Use shuffles of
    // one-element vectors to avoid poor code for i64 in the backend.
    if (VTy->getElementType()->isIntegerTy(64)) {
      // Extract the other lane.
      Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
      int Lane = cast<ConstantInt>(Ops[2])->getZExtValue();
      Value *SV = llvm::ConstantVector::get(ConstantInt::get(Int32Ty, 1-Lane));
      Ops[1] = Builder.CreateShuffleVector(Ops[1], Ops[1], SV);
      // Load the value as a one-element vector.
      Ty = llvm::FixedVectorType::get(VTy->getElementType(), 1);
      llvm::Type *Tys[] = {Ty, Int8PtrTy};
      Function *F = CGM.getIntrinsic(Intrinsic::arm_neon_vld1, Tys);
      Value *Align = getAlignmentValue32(PtrOp0);
      Value *Ld = Builder.CreateCall(F, {Ops[0], Align});
      // Combine them.
      int Indices[] = {1 - Lane, Lane};
      return Builder.CreateShuffleVector(Ops[1], Ld, Indices, "vld1q_lane");
    }
    [[fallthrough]];
  case NEON::BI__builtin_neon_vld1_lane_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    PtrOp0 = PtrOp0.withElementType(VTy->getElementType());
    Value *Ld = Builder.CreateLoad(PtrOp0);
    return Builder.CreateInsertElement(Ops[1], Ld, Ops[2], "vld1_lane");
  }
  case NEON::BI__builtin_neon_vqrshrn_n_v:
    Int =
      usgn ? Intrinsic::arm_neon_vqrshiftnu : Intrinsic::arm_neon_vqrshiftns;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqrshrn_n",
                        1, true);
  case NEON::BI__builtin_neon_vqrshrun_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vqrshiftnsu, Ty),
                        Ops, "vqrshrun_n", 1, true);
  case NEON::BI__builtin_neon_vqshrn_n_v:
    Int = usgn ? Intrinsic::arm_neon_vqshiftnu : Intrinsic::arm_neon_vqshiftns;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshrn_n",
                        1, true);
  case NEON::BI__builtin_neon_vqshrun_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vqshiftnsu, Ty),
                        Ops, "vqshrun_n", 1, true);
  case NEON::BI__builtin_neon_vrecpe_v:
  case NEON::BI__builtin_neon_vrecpeq_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vrecpe, Ty),
                        Ops, "vrecpe");
  case NEON::BI__builtin_neon_vrshrn_n_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vrshiftn, Ty),
                        Ops, "vrshrn_n", 1, true);
  case NEON::BI__builtin_neon_vrsra_n_v:
  case NEON::BI__builtin_neon_vrsraq_n_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = EmitNeonShiftVector(Ops[2], Ty, true);
    Int = usgn ? Intrinsic::arm_neon_vrshiftu : Intrinsic::arm_neon_vrshifts;
    Ops[1] = Builder.CreateCall(CGM.getIntrinsic(Int, Ty), {Ops[1], Ops[2]});
    return Builder.CreateAdd(Ops[0], Ops[1], "vrsra_n");
  case NEON::BI__builtin_neon_vsri_n_v:
  case NEON::BI__builtin_neon_vsriq_n_v:
    rightShift = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vsli_n_v:
  case NEON::BI__builtin_neon_vsliq_n_v:
    Ops[2] = EmitNeonShiftVector(Ops[2], Ty, rightShift);
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vshiftins, Ty),
                        Ops, "vsli_n");
  case NEON::BI__builtin_neon_vsra_n_v:
  case NEON::BI__builtin_neon_vsraq_n_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = EmitNeonRShiftImm(Ops[1], Ops[2], Ty, usgn, "vsra_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  case NEON::BI__builtin_neon_vst1q_lane_v:
    // Handle 64-bit integer elements as a special case.  Use a shuffle to get
    // a one-element vector and avoid poor code for i64 in the backend.
    if (VTy->getElementType()->isIntegerTy(64)) {
      Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
      Value *SV = llvm::ConstantVector::get(cast<llvm::Constant>(Ops[2]));
      Ops[1] = Builder.CreateShuffleVector(Ops[1], Ops[1], SV);
      Ops[2] = getAlignmentValue32(PtrOp0);
      llvm::Type *Tys[] = {Int8PtrTy, Ops[1]->getType()};
      return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::arm_neon_vst1,
                                                 Tys), Ops);
    }
    [[fallthrough]];
  case NEON::BI__builtin_neon_vst1_lane_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2]);
    return Builder.CreateStore(Ops[1],
                               PtrOp0.withElementType(Ops[1]->getType()));
  }
  case NEON::BI__builtin_neon_vtbl1_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbl1),
                        Ops, "vtbl1");
  case NEON::BI__builtin_neon_vtbl2_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbl2),
                        Ops, "vtbl2");
  case NEON::BI__builtin_neon_vtbl3_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbl3),
                        Ops, "vtbl3");
  case NEON::BI__builtin_neon_vtbl4_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbl4),
                        Ops, "vtbl4");
  case NEON::BI__builtin_neon_vtbx1_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbx1),
                        Ops, "vtbx1");
  case NEON::BI__builtin_neon_vtbx2_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbx2),
                        Ops, "vtbx2");
  case NEON::BI__builtin_neon_vtbx3_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbx3),
                        Ops, "vtbx3");
  case NEON::BI__builtin_neon_vtbx4_v:
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::arm_neon_vtbx4),
                        Ops, "vtbx4");
  }
}

template<typename Integer>
static Integer GetIntegerConstantValue(const Expr *E, ASTContext &Context) {
  return E->getIntegerConstantExpr(Context)->getExtValue();
}

static llvm::Value *SignOrZeroExtend(CGBuilderTy &Builder, llvm::Value *V,
                                     llvm::Type *T, bool Unsigned) {
  // Helper function called by Tablegen-constructed ARM MVE builtin codegen,
  // which finds it convenient to specify signed/unsigned as a boolean flag.
  return Unsigned ? Builder.CreateZExt(V, T) : Builder.CreateSExt(V, T);
}

static llvm::Value *MVEImmediateShr(CGBuilderTy &Builder, llvm::Value *V,
                                    uint32_t Shift, bool Unsigned) {
  // MVE helper function for integer shift right. This must handle signed vs
  // unsigned, and also deal specially with the case where the shift count is
  // equal to the lane size. In LLVM IR, an LShr with that parameter would be
  // undefined behavior, but in MVE it's legal, so we must convert it to code
  // that is not undefined in IR.
  unsigned LaneBits = cast<llvm::VectorType>(V->getType())
                          ->getElementType()
                          ->getPrimitiveSizeInBits();
  if (Shift == LaneBits) {
    // An unsigned shift of the full lane size always generates zero, so we can
    // simply emit a zero vector. A signed shift of the full lane size does the
    // same thing as shifting by one bit fewer.
    if (Unsigned)
      return llvm::Constant::getNullValue(V->getType());
    else
      --Shift;
  }
  return Unsigned ? Builder.CreateLShr(V, Shift) : Builder.CreateAShr(V, Shift);
}

static llvm::Value *ARMMVEVectorSplat(CGBuilderTy &Builder, llvm::Value *V) {
  // MVE-specific helper function for a vector splat, which infers the element
  // count of the output vector by knowing that MVE vectors are all 128 bits
  // wide.
  unsigned Elements = 128 / V->getType()->getPrimitiveSizeInBits();
  return Builder.CreateVectorSplat(Elements, V);
}

static llvm::Value *ARMMVEVectorReinterpret(CGBuilderTy &Builder,
                                            CodeGenFunction *CGF,
                                            llvm::Value *V,
                                            llvm::Type *DestType) {
  // Convert one MVE vector type into another by reinterpreting its in-register
  // format.
  //
  // Little-endian, this is identical to a bitcast (which reinterprets the
  // memory format). But big-endian, they're not necessarily the same, because
  // the register and memory formats map to each other differently depending on
  // the lane size.
  //
  // We generate a bitcast whenever we can (if we're little-endian, or if the
  // lane sizes are the same anyway). Otherwise we fall back to an IR intrinsic
  // that performs the different kind of reinterpretation.
  if (CGF->getTarget().isBigEndian() &&
      V->getType()->getScalarSizeInBits() != DestType->getScalarSizeInBits()) {
    return Builder.CreateCall(
        CGF->CGM.getIntrinsic(Intrinsic::arm_mve_vreinterpretq,
                              {DestType, V->getType()}),
        V);
  } else {
    return Builder.CreateBitCast(V, DestType);
  }
}

static llvm::Value *VectorUnzip(CGBuilderTy &Builder, llvm::Value *V, bool Odd) {
  // Make a shufflevector that extracts every other element of a vector (evens
  // or odds, as desired).
  SmallVector<int, 16> Indices;
  unsigned InputElements =
      cast<llvm::FixedVectorType>(V->getType())->getNumElements();
  for (unsigned i = 0; i < InputElements; i += 2)
    Indices.push_back(i + Odd);
  return Builder.CreateShuffleVector(V, Indices);
}

static llvm::Value *VectorZip(CGBuilderTy &Builder, llvm::Value *V0,
                              llvm::Value *V1) {
  // Make a shufflevector that interleaves two vectors element by element.
  assert(V0->getType() == V1->getType() && "Can't zip different vector types");
  SmallVector<int, 16> Indices;
  unsigned InputElements =
      cast<llvm::FixedVectorType>(V0->getType())->getNumElements();
  for (unsigned i = 0; i < InputElements; i++) {
    Indices.push_back(i);
    Indices.push_back(i + InputElements);
  }
  return Builder.CreateShuffleVector(V0, V1, Indices);
}

template<unsigned HighBit, unsigned OtherBits>
static llvm::Value *ARMMVEConstantSplat(CGBuilderTy &Builder, llvm::Type *VT) {
  // MVE-specific helper function to make a vector splat of a constant such as
  // UINT_MAX or INT_MIN, in which all bits below the highest one are equal.
  llvm::Type *T = cast<llvm::VectorType>(VT)->getElementType();
  unsigned LaneBits = T->getPrimitiveSizeInBits();
  uint32_t Value = HighBit << (LaneBits - 1);
  if (OtherBits)
    Value |= (1UL << (LaneBits - 1)) - 1;
  llvm::Value *Lane = llvm::ConstantInt::get(T, Value);
  return ARMMVEVectorSplat(Builder, Lane);
}

static llvm::Value *ARMMVEVectorElementReverse(CGBuilderTy &Builder,
                                               llvm::Value *V,
                                               unsigned ReverseWidth) {
  // MVE-specific helper function which reverses the elements of a
  // vector within every (ReverseWidth)-bit collection of lanes.
  SmallVector<int, 16> Indices;
  unsigned LaneSize = V->getType()->getScalarSizeInBits();
  unsigned Elements = 128 / LaneSize;
  unsigned Mask = ReverseWidth / LaneSize - 1;
  for (unsigned i = 0; i < Elements; i++)
    Indices.push_back(i ^ Mask);
  return Builder.CreateShuffleVector(V, Indices);
}

Value *CodeGenFunction::EmitARMMVEBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E,
                                              ReturnValueSlot ReturnValue,
                                              llvm::Triple::ArchType Arch) {
  enum class CustomCodeGen { VLD24, VST24 } CustomCodeGenType;
  Intrinsic::ID IRIntr;
  unsigned NumVectors;

  // Code autogenerated by Tablegen will handle all the simple builtins.
  switch (BuiltinID) {
    #include "clang/Basic/arm_mve_builtin_cg.inc"

    // If we didn't match an MVE builtin id at all, go back to the
    // main EmitARMBuiltinExpr.
  default:
    return nullptr;
  }

  // Anything that breaks from that switch is an MVE builtin that
  // needs handwritten code to generate.

  switch (CustomCodeGenType) {

  case CustomCodeGen::VLD24: {
    llvm::SmallVector<Value *, 4> Ops;
    llvm::SmallVector<llvm::Type *, 4> Tys;

    auto MvecCType = E->getType();
    auto MvecLType = ConvertType(MvecCType);
    assert(MvecLType->isStructTy() &&
           "Return type for vld[24]q should be a struct");
    assert(MvecLType->getStructNumElements() == 1 &&
           "Return-type struct for vld[24]q should have one element");
    auto MvecLTypeInner = MvecLType->getStructElementType(0);
    assert(MvecLTypeInner->isArrayTy() &&
           "Return-type struct for vld[24]q should contain an array");
    assert(MvecLTypeInner->getArrayNumElements() == NumVectors &&
           "Array member of return-type struct vld[24]q has wrong length");
    auto VecLType = MvecLTypeInner->getArrayElementType();

    Tys.push_back(VecLType);

    auto Addr = E->getArg(0);
    Ops.push_back(EmitScalarExpr(Addr));
    Tys.push_back(ConvertType(Addr->getType()));

    Function *F = CGM.getIntrinsic(IRIntr, ArrayRef(Tys));
    Value *LoadResult = Builder.CreateCall(F, Ops);
    Value *MvecOut = PoisonValue::get(MvecLType);
    for (unsigned i = 0; i < NumVectors; ++i) {
      Value *Vec = Builder.CreateExtractValue(LoadResult, i);
      MvecOut = Builder.CreateInsertValue(MvecOut, Vec, {0, i});
    }

    if (ReturnValue.isNull())
      return MvecOut;
    else
      return Builder.CreateStore(MvecOut, ReturnValue.getAddress());
  }

  case CustomCodeGen::VST24: {
    llvm::SmallVector<Value *, 4> Ops;
    llvm::SmallVector<llvm::Type *, 4> Tys;

    auto Addr = E->getArg(0);
    Ops.push_back(EmitScalarExpr(Addr));
    Tys.push_back(ConvertType(Addr->getType()));

    auto MvecCType = E->getArg(1)->getType();
    auto MvecLType = ConvertType(MvecCType);
    assert(MvecLType->isStructTy() && "Data type for vst2q should be a struct");
    assert(MvecLType->getStructNumElements() == 1 &&
           "Data-type struct for vst2q should have one element");
    auto MvecLTypeInner = MvecLType->getStructElementType(0);
    assert(MvecLTypeInner->isArrayTy() &&
           "Data-type struct for vst2q should contain an array");
    assert(MvecLTypeInner->getArrayNumElements() == NumVectors &&
           "Array member of return-type struct vld[24]q has wrong length");
    auto VecLType = MvecLTypeInner->getArrayElementType();

    Tys.push_back(VecLType);

    AggValueSlot MvecSlot = CreateAggTemp(MvecCType);
    EmitAggExpr(E->getArg(1), MvecSlot);
    auto Mvec = Builder.CreateLoad(MvecSlot.getAddress());
    for (unsigned i = 0; i < NumVectors; i++)
      Ops.push_back(Builder.CreateExtractValue(Mvec, {0, i}));

    Function *F = CGM.getIntrinsic(IRIntr, ArrayRef(Tys));
    Value *ToReturn = nullptr;
    for (unsigned i = 0; i < NumVectors; i++) {
      Ops.push_back(llvm::ConstantInt::get(Int32Ty, i));
      ToReturn = Builder.CreateCall(F, Ops);
      Ops.pop_back();
    }
    return ToReturn;
  }
  }
  llvm_unreachable("unknown custom codegen type.");
}

Value *CodeGenFunction::EmitARMCDEBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E,
                                              ReturnValueSlot ReturnValue,
                                              llvm::Triple::ArchType Arch) {
  switch (BuiltinID) {
  default:
    return nullptr;
#include "clang/Basic/arm_cde_builtin_cg.inc"
  }
}

static Value *EmitAArch64TblBuiltinExpr(CodeGenFunction &CGF, unsigned BuiltinID,
                                      const CallExpr *E,
                                      SmallVectorImpl<Value *> &Ops,
                                      llvm::Triple::ArchType Arch) {
  unsigned int Int = 0;
  const char *s = nullptr;

  switch (BuiltinID) {
  default:
    return nullptr;
  case NEON::BI__builtin_neon_vtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1q_v:
  case NEON::BI__builtin_neon_vtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2q_v:
  case NEON::BI__builtin_neon_vtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3q_v:
  case NEON::BI__builtin_neon_vtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4q_v:
    break;
  case NEON::BI__builtin_neon_vtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1q_v:
  case NEON::BI__builtin_neon_vtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2q_v:
  case NEON::BI__builtin_neon_vtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3q_v:
  case NEON::BI__builtin_neon_vtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4q_v:
    break;
  }

  assert(E->getNumArgs() >= 3);

  // Get the last argument, which specifies the vector type.
  const Expr *Arg = E->getArg(E->getNumArgs() - 1);
  std::optional<llvm::APSInt> Result =
      Arg->getIntegerConstantExpr(CGF.getContext());
  if (!Result)
    return nullptr;

  // Determine the type of this overloaded NEON intrinsic.
  NeonTypeFlags Type = Result->getZExtValue();
  llvm::FixedVectorType *Ty = GetNeonType(&CGF, Type);
  if (!Ty)
    return nullptr;

  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  // AArch64 scalar builtins are not overloaded, they do not have an extra
  // argument that specifies the vector type, need to handle each case.
  switch (BuiltinID) {
  case NEON::BI__builtin_neon_vtbl1_v: {
    return packTBLDVectorList(CGF, ArrayRef(Ops).slice(0, 1), nullptr, Ops[1],
                              Ty, Intrinsic::aarch64_neon_tbl1, "vtbl1");
  }
  case NEON::BI__builtin_neon_vtbl2_v: {
    return packTBLDVectorList(CGF, ArrayRef(Ops).slice(0, 2), nullptr, Ops[2],
                              Ty, Intrinsic::aarch64_neon_tbl1, "vtbl1");
  }
  case NEON::BI__builtin_neon_vtbl3_v: {
    return packTBLDVectorList(CGF, ArrayRef(Ops).slice(0, 3), nullptr, Ops[3],
                              Ty, Intrinsic::aarch64_neon_tbl2, "vtbl2");
  }
  case NEON::BI__builtin_neon_vtbl4_v: {
    return packTBLDVectorList(CGF, ArrayRef(Ops).slice(0, 4), nullptr, Ops[4],
                              Ty, Intrinsic::aarch64_neon_tbl2, "vtbl2");
  }
  case NEON::BI__builtin_neon_vtbx1_v: {
    Value *TblRes =
        packTBLDVectorList(CGF, ArrayRef(Ops).slice(1, 1), nullptr, Ops[2], Ty,
                           Intrinsic::aarch64_neon_tbl1, "vtbl1");

    llvm::Constant *EightV = ConstantInt::get(Ty, 8);
    Value *CmpRes = Builder.CreateICmp(ICmpInst::ICMP_UGE, Ops[2], EightV);
    CmpRes = Builder.CreateSExt(CmpRes, Ty);

    Value *EltsFromInput = Builder.CreateAnd(CmpRes, Ops[0]);
    Value *EltsFromTbl = Builder.CreateAnd(Builder.CreateNot(CmpRes), TblRes);
    return Builder.CreateOr(EltsFromInput, EltsFromTbl, "vtbx");
  }
  case NEON::BI__builtin_neon_vtbx2_v: {
    return packTBLDVectorList(CGF, ArrayRef(Ops).slice(1, 2), Ops[0], Ops[3],
                              Ty, Intrinsic::aarch64_neon_tbx1, "vtbx1");
  }
  case NEON::BI__builtin_neon_vtbx3_v: {
    Value *TblRes =
        packTBLDVectorList(CGF, ArrayRef(Ops).slice(1, 3), nullptr, Ops[4], Ty,
                           Intrinsic::aarch64_neon_tbl2, "vtbl2");

    llvm::Constant *TwentyFourV = ConstantInt::get(Ty, 24);
    Value *CmpRes = Builder.CreateICmp(ICmpInst::ICMP_UGE, Ops[4],
                                           TwentyFourV);
    CmpRes = Builder.CreateSExt(CmpRes, Ty);

    Value *EltsFromInput = Builder.CreateAnd(CmpRes, Ops[0]);
    Value *EltsFromTbl = Builder.CreateAnd(Builder.CreateNot(CmpRes), TblRes);
    return Builder.CreateOr(EltsFromInput, EltsFromTbl, "vtbx");
  }
  case NEON::BI__builtin_neon_vtbx4_v: {
    return packTBLDVectorList(CGF, ArrayRef(Ops).slice(1, 4), Ops[0], Ops[5],
                              Ty, Intrinsic::aarch64_neon_tbx2, "vtbx2");
  }
  case NEON::BI__builtin_neon_vqtbl1_v:
  case NEON::BI__builtin_neon_vqtbl1q_v:
    Int = Intrinsic::aarch64_neon_tbl1; s = "vtbl1"; break;
  case NEON::BI__builtin_neon_vqtbl2_v:
  case NEON::BI__builtin_neon_vqtbl2q_v: {
    Int = Intrinsic::aarch64_neon_tbl2; s = "vtbl2"; break;
  case NEON::BI__builtin_neon_vqtbl3_v:
  case NEON::BI__builtin_neon_vqtbl3q_v:
    Int = Intrinsic::aarch64_neon_tbl3; s = "vtbl3"; break;
  case NEON::BI__builtin_neon_vqtbl4_v:
  case NEON::BI__builtin_neon_vqtbl4q_v:
    Int = Intrinsic::aarch64_neon_tbl4; s = "vtbl4"; break;
  case NEON::BI__builtin_neon_vqtbx1_v:
  case NEON::BI__builtin_neon_vqtbx1q_v:
    Int = Intrinsic::aarch64_neon_tbx1; s = "vtbx1"; break;
  case NEON::BI__builtin_neon_vqtbx2_v:
  case NEON::BI__builtin_neon_vqtbx2q_v:
    Int = Intrinsic::aarch64_neon_tbx2; s = "vtbx2"; break;
  case NEON::BI__builtin_neon_vqtbx3_v:
  case NEON::BI__builtin_neon_vqtbx3q_v:
    Int = Intrinsic::aarch64_neon_tbx3; s = "vtbx3"; break;
  case NEON::BI__builtin_neon_vqtbx4_v:
  case NEON::BI__builtin_neon_vqtbx4q_v:
    Int = Intrinsic::aarch64_neon_tbx4; s = "vtbx4"; break;
  }
  }

  if (!Int)
    return nullptr;

  Function *F = CGF.CGM.getIntrinsic(Int, Ty);
  return CGF.EmitNeonCall(F, Ops, s);
}

Value *CodeGenFunction::vectorWrapScalar16(Value *Op) {
  auto *VTy = llvm::FixedVectorType::get(Int16Ty, 4);
  Op = Builder.CreateBitCast(Op, Int16Ty);
  Value *V = PoisonValue::get(VTy);
  llvm::Constant *CI = ConstantInt::get(SizeTy, 0);
  Op = Builder.CreateInsertElement(V, Op, CI);
  return Op;
}

/// SVEBuiltinMemEltTy - Returns the memory element type for this memory
/// access builtin.  Only required if it can't be inferred from the base pointer
/// operand.
llvm::Type *CodeGenFunction::SVEBuiltinMemEltTy(const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getMemEltType()) {
  case SVETypeFlags::MemEltTyDefault:
    return getEltType(TypeFlags);
  case SVETypeFlags::MemEltTyInt8:
    return Builder.getInt8Ty();
  case SVETypeFlags::MemEltTyInt16:
    return Builder.getInt16Ty();
  case SVETypeFlags::MemEltTyInt32:
    return Builder.getInt32Ty();
  case SVETypeFlags::MemEltTyInt64:
    return Builder.getInt64Ty();
  }
  llvm_unreachable("Unknown MemEltType");
}

llvm::Type *CodeGenFunction::getEltType(const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getEltType()) {
  default:
    llvm_unreachable("Invalid SVETypeFlag!");

  case SVETypeFlags::EltTyInt8:
    return Builder.getInt8Ty();
  case SVETypeFlags::EltTyInt16:
    return Builder.getInt16Ty();
  case SVETypeFlags::EltTyInt32:
    return Builder.getInt32Ty();
  case SVETypeFlags::EltTyInt64:
    return Builder.getInt64Ty();
  case SVETypeFlags::EltTyInt128:
    return Builder.getInt128Ty();

  case SVETypeFlags::EltTyFloat16:
    return Builder.getHalfTy();
  case SVETypeFlags::EltTyFloat32:
    return Builder.getFloatTy();
  case SVETypeFlags::EltTyFloat64:
    return Builder.getDoubleTy();

  case SVETypeFlags::EltTyBFloat16:
    return Builder.getBFloatTy();

  case SVETypeFlags::EltTyBool8:
  case SVETypeFlags::EltTyBool16:
  case SVETypeFlags::EltTyBool32:
  case SVETypeFlags::EltTyBool64:
    return Builder.getInt1Ty();
  }
}

// Return the llvm predicate vector type corresponding to the specified element
// TypeFlags.
llvm::ScalableVectorType *
CodeGenFunction::getSVEPredType(const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getEltType()) {
  default: llvm_unreachable("Unhandled SVETypeFlag!");

  case SVETypeFlags::EltTyInt8:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
  case SVETypeFlags::EltTyInt16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyInt32:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 4);
  case SVETypeFlags::EltTyInt64:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 2);

  case SVETypeFlags::EltTyBFloat16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyFloat16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyFloat32:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 4);
  case SVETypeFlags::EltTyFloat64:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 2);

  case SVETypeFlags::EltTyBool8:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
  case SVETypeFlags::EltTyBool16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyBool32:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 4);
  case SVETypeFlags::EltTyBool64:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 2);
  }
}

// Return the llvm vector type corresponding to the specified element TypeFlags.
llvm::ScalableVectorType *
CodeGenFunction::getSVEType(const SVETypeFlags &TypeFlags) {
  switch (TypeFlags.getEltType()) {
  default:
    llvm_unreachable("Invalid SVETypeFlag!");

  case SVETypeFlags::EltTyInt8:
    return llvm::ScalableVectorType::get(Builder.getInt8Ty(), 16);
  case SVETypeFlags::EltTyInt16:
    return llvm::ScalableVectorType::get(Builder.getInt16Ty(), 8);
  case SVETypeFlags::EltTyInt32:
    return llvm::ScalableVectorType::get(Builder.getInt32Ty(), 4);
  case SVETypeFlags::EltTyInt64:
    return llvm::ScalableVectorType::get(Builder.getInt64Ty(), 2);

  case SVETypeFlags::EltTyFloat16:
    return llvm::ScalableVectorType::get(Builder.getHalfTy(), 8);
  case SVETypeFlags::EltTyBFloat16:
    return llvm::ScalableVectorType::get(Builder.getBFloatTy(), 8);
  case SVETypeFlags::EltTyFloat32:
    return llvm::ScalableVectorType::get(Builder.getFloatTy(), 4);
  case SVETypeFlags::EltTyFloat64:
    return llvm::ScalableVectorType::get(Builder.getDoubleTy(), 2);

  case SVETypeFlags::EltTyBool8:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 16);
  case SVETypeFlags::EltTyBool16:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 8);
  case SVETypeFlags::EltTyBool32:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 4);
  case SVETypeFlags::EltTyBool64:
    return llvm::ScalableVectorType::get(Builder.getInt1Ty(), 2);
  }
}

llvm::Value *
CodeGenFunction::EmitSVEAllTruePred(const SVETypeFlags &TypeFlags) {
  Function *Ptrue =
      CGM.getIntrinsic(Intrinsic::aarch64_sve_ptrue, getSVEPredType(TypeFlags));
  return Builder.CreateCall(Ptrue, {Builder.getInt32(/*SV_ALL*/ 31)});
}

constexpr unsigned SVEBitsPerBlock = 128;

static llvm::ScalableVectorType *getSVEVectorForElementType(llvm::Type *EltTy) {
  unsigned NumElts = SVEBitsPerBlock / EltTy->getScalarSizeInBits();
  return llvm::ScalableVectorType::get(EltTy, NumElts);
}

// Reinterpret the input predicate so that it can be used to correctly isolate
// the elements of the specified datatype.
Value *CodeGenFunction::EmitSVEPredicateCast(Value *Pred,
                                             llvm::ScalableVectorType *VTy) {

  if (isa<TargetExtType>(Pred->getType()) &&
      cast<TargetExtType>(Pred->getType())->getName() == "aarch64.svcount")
    return Pred;

  auto *RTy = llvm::VectorType::get(IntegerType::get(getLLVMContext(), 1), VTy);
  if (Pred->getType() == RTy)
    return Pred;

  unsigned IntID;
  llvm::Type *IntrinsicTy;
  switch (VTy->getMinNumElements()) {
  default:
    llvm_unreachable("unsupported element count!");
  case 1:
  case 2:
  case 4:
  case 8:
    IntID = Intrinsic::aarch64_sve_convert_from_svbool;
    IntrinsicTy = RTy;
    break;
  case 16:
    IntID = Intrinsic::aarch64_sve_convert_to_svbool;
    IntrinsicTy = Pred->getType();
    break;
  }

  Function *F = CGM.getIntrinsic(IntID, IntrinsicTy);
  Value *C = Builder.CreateCall(F, Pred);
  assert(C->getType() == RTy && "Unexpected return type!");
  return C;
}

Value *CodeGenFunction::EmitSVEGatherLoad(const SVETypeFlags &TypeFlags,
                                          SmallVectorImpl<Value *> &Ops,
                                          unsigned IntID) {
  auto *ResultTy = getSVEType(TypeFlags);
  auto *OverloadedTy =
      llvm::ScalableVectorType::get(SVEBuiltinMemEltTy(TypeFlags), ResultTy);

  Function *F = nullptr;
  if (Ops[1]->getType()->isVectorTy())
    // This is the "vector base, scalar offset" case. In order to uniquely
    // map this built-in to an LLVM IR intrinsic, we need both the return type
    // and the type of the vector base.
    F = CGM.getIntrinsic(IntID, {OverloadedTy, Ops[1]->getType()});
  else
    // This is the "scalar base, vector offset case". The type of the offset
    // is encoded in the name of the intrinsic. We only need to specify the
    // return type in order to uniquely map this built-in to an LLVM IR
    // intrinsic.
    F = CGM.getIntrinsic(IntID, OverloadedTy);

  // At the ACLE level there's only one predicate type, svbool_t, which is
  // mapped to <n x 16 x i1>. However, this might be incompatible with the
  // actual type being loaded. For example, when loading doubles (i64) the
  // predicate should be <n x 2 x i1> instead. At the IR level the type of
  // the predicate and the data being loaded must match. Cast to the type
  // expected by the intrinsic. The intrinsic itself should be defined in
  // a way than enforces relations between parameter types.
  Ops[0] = EmitSVEPredicateCast(
      Ops[0], cast<llvm::ScalableVectorType>(F->getArg(0)->getType()));

  // Pass 0 when the offset is missing. This can only be applied when using
  // the "vector base" addressing mode for which ACLE allows no offset. The
  // corresponding LLVM IR always requires an offset.
  if (Ops.size() == 2) {
    assert(Ops[1]->getType()->isVectorTy() && "Scalar base requires an offset");
    Ops.push_back(ConstantInt::get(Int64Ty, 0));
  }

  // For "vector base, scalar index" scale the index so that it becomes a
  // scalar offset.
  if (!TypeFlags.isByteIndexed() && Ops[1]->getType()->isVectorTy()) {
    unsigned BytesPerElt =
        OverloadedTy->getElementType()->getScalarSizeInBits() / 8;
    Ops[2] = Builder.CreateShl(Ops[2], Log2_32(BytesPerElt));
  }

  Value *Call = Builder.CreateCall(F, Ops);

  // The following sext/zext is only needed when ResultTy != OverloadedTy. In
  // other cases it's folded into a nop.
  return TypeFlags.isZExtReturn() ? Builder.CreateZExt(Call, ResultTy)
                                  : Builder.CreateSExt(Call, ResultTy);
}

Value *CodeGenFunction::EmitSVEScatterStore(const SVETypeFlags &TypeFlags,
                                            SmallVectorImpl<Value *> &Ops,
                                            unsigned IntID) {
  auto *SrcDataTy = getSVEType(TypeFlags);
  auto *OverloadedTy =
      llvm::ScalableVectorType::get(SVEBuiltinMemEltTy(TypeFlags), SrcDataTy);

  // In ACLE the source data is passed in the last argument, whereas in LLVM IR
  // it's the first argument. Move it accordingly.
  Ops.insert(Ops.begin(), Ops.pop_back_val());

  Function *F = nullptr;
  if (Ops[2]->getType()->isVectorTy())
    // This is the "vector base, scalar offset" case. In order to uniquely
    // map this built-in to an LLVM IR intrinsic, we need both the return type
    // and the type of the vector base.
    F = CGM.getIntrinsic(IntID, {OverloadedTy, Ops[2]->getType()});
  else
    // This is the "scalar base, vector offset case". The type of the offset
    // is encoded in the name of the intrinsic. We only need to specify the
    // return type in order to uniquely map this built-in to an LLVM IR
    // intrinsic.
    F = CGM.getIntrinsic(IntID, OverloadedTy);

  // Pass 0 when the offset is missing. This can only be applied when using
  // the "vector base" addressing mode for which ACLE allows no offset. The
  // corresponding LLVM IR always requires an offset.
  if (Ops.size() == 3) {
    assert(Ops[1]->getType()->isVectorTy() && "Scalar base requires an offset");
    Ops.push_back(ConstantInt::get(Int64Ty, 0));
  }

  // Truncation is needed when SrcDataTy != OverloadedTy. In other cases it's
  // folded into a nop.
  Ops[0] = Builder.CreateTrunc(Ops[0], OverloadedTy);

  // At the ACLE level there's only one predicate type, svbool_t, which is
  // mapped to <n x 16 x i1>. However, this might be incompatible with the
  // actual type being stored. For example, when storing doubles (i64) the
  // predicated should be <n x 2 x i1> instead. At the IR level the type of
  // the predicate and the data being stored must match. Cast to the type
  // expected by the intrinsic. The intrinsic itself should be defined in
  // a way that enforces relations between parameter types.
  Ops[1] = EmitSVEPredicateCast(
      Ops[1], cast<llvm::ScalableVectorType>(F->getArg(1)->getType()));

  // For "vector base, scalar index" scale the index so that it becomes a
  // scalar offset.
  if (!TypeFlags.isByteIndexed() && Ops[2]->getType()->isVectorTy()) {
    unsigned BytesPerElt =
        OverloadedTy->getElementType()->getScalarSizeInBits() / 8;
    Ops[3] = Builder.CreateShl(Ops[3], Log2_32(BytesPerElt));
  }

  return Builder.CreateCall(F, Ops);
}

Value *CodeGenFunction::EmitSVEGatherPrefetch(const SVETypeFlags &TypeFlags,
                                              SmallVectorImpl<Value *> &Ops,
                                              unsigned IntID) {
  // The gather prefetches are overloaded on the vector input - this can either
  // be the vector of base addresses or vector of offsets.
  auto *OverloadedTy = dyn_cast<llvm::ScalableVectorType>(Ops[1]->getType());
  if (!OverloadedTy)
    OverloadedTy = cast<llvm::ScalableVectorType>(Ops[2]->getType());

  // Cast the predicate from svbool_t to the right number of elements.
  Ops[0] = EmitSVEPredicateCast(Ops[0], OverloadedTy);

  // vector + imm addressing modes
  if (Ops[1]->getType()->isVectorTy()) {
    if (Ops.size() == 3) {
      // Pass 0 for 'vector+imm' when the index is omitted.
      Ops.push_back(ConstantInt::get(Int64Ty, 0));

      // The sv_prfop is the last operand in the builtin and IR intrinsic.
      std::swap(Ops[2], Ops[3]);
    } else {
      // Index needs to be passed as scaled offset.
      llvm::Type *MemEltTy = SVEBuiltinMemEltTy(TypeFlags);
      unsigned BytesPerElt = MemEltTy->getPrimitiveSizeInBits() / 8;
      if (BytesPerElt > 1)
        Ops[2] = Builder.CreateShl(Ops[2], Log2_32(BytesPerElt));
    }
  }

  Function *F = CGM.getIntrinsic(IntID, OverloadedTy);
  return Builder.CreateCall(F, Ops);
}

Value *CodeGenFunction::EmitSVEStructLoad(const SVETypeFlags &TypeFlags,
                                          SmallVectorImpl<Value*> &Ops,
                                          unsigned IntID) {
  llvm::ScalableVectorType *VTy = getSVEType(TypeFlags);

  unsigned N;
  switch (IntID) {
  case Intrinsic::aarch64_sve_ld2_sret:
  case Intrinsic::aarch64_sve_ld1_pn_x2:
  case Intrinsic::aarch64_sve_ldnt1_pn_x2:
  case Intrinsic::aarch64_sve_ld2q_sret:
    N = 2;
    break;
  case Intrinsic::aarch64_sve_ld3_sret:
  case Intrinsic::aarch64_sve_ld3q_sret:
    N = 3;
    break;
  case Intrinsic::aarch64_sve_ld4_sret:
  case Intrinsic::aarch64_sve_ld1_pn_x4:
  case Intrinsic::aarch64_sve_ldnt1_pn_x4:
  case Intrinsic::aarch64_sve_ld4q_sret:
    N = 4;
    break;
  default:
    llvm_unreachable("unknown intrinsic!");
  }
  auto RetTy = llvm::VectorType::get(VTy->getElementType(),
                                     VTy->getElementCount() * N);

  Value *Predicate = EmitSVEPredicateCast(Ops[0], VTy);
  Value *BasePtr = Ops[1];

  // Does the load have an offset?
  if (Ops.size() > 2)
    BasePtr = Builder.CreateGEP(VTy, BasePtr, Ops[2]);

  Function *F = CGM.getIntrinsic(IntID, {VTy});
  Value *Call = Builder.CreateCall(F, {Predicate, BasePtr});
  unsigned MinElts = VTy->getMinNumElements();
  Value *Ret = llvm::PoisonValue::get(RetTy);
  for (unsigned I = 0; I < N; I++) {
    Value *Idx = ConstantInt::get(CGM.Int64Ty, I * MinElts);
    Value *SRet = Builder.CreateExtractValue(Call, I);
    Ret = Builder.CreateInsertVector(RetTy, Ret, SRet, Idx);
  }
  return Ret;
}

Value *CodeGenFunction::EmitSVEStructStore(const SVETypeFlags &TypeFlags,
                                           SmallVectorImpl<Value*> &Ops,
                                           unsigned IntID) {
  llvm::ScalableVectorType *VTy = getSVEType(TypeFlags);

  unsigned N;
  switch (IntID) {
  case Intrinsic::aarch64_sve_st2:
  case Intrinsic::aarch64_sve_st1_pn_x2:
  case Intrinsic::aarch64_sve_stnt1_pn_x2:
  case Intrinsic::aarch64_sve_st2q:
    N = 2;
    break;
  case Intrinsic::aarch64_sve_st3:
  case Intrinsic::aarch64_sve_st3q:
    N = 3;
    break;
  case Intrinsic::aarch64_sve_st4:
  case Intrinsic::aarch64_sve_st1_pn_x4:
  case Intrinsic::aarch64_sve_stnt1_pn_x4:
  case Intrinsic::aarch64_sve_st4q:
    N = 4;
    break;
  default:
    llvm_unreachable("unknown intrinsic!");
  }

  Value *Predicate = EmitSVEPredicateCast(Ops[0], VTy);
  Value *BasePtr = Ops[1];

  // Does the store have an offset?
  if (Ops.size() > (2 + N))
    BasePtr = Builder.CreateGEP(VTy, BasePtr, Ops[2]);

  // The llvm.aarch64.sve.st2/3/4 intrinsics take legal part vectors, so we
  // need to break up the tuple vector.
  SmallVector<llvm::Value*, 5> Operands;
  for (unsigned I = Ops.size() - N; I < Ops.size(); ++I)
    Operands.push_back(Ops[I]);
  Operands.append({Predicate, BasePtr});
  Function *F = CGM.getIntrinsic(IntID, { VTy });

  return Builder.CreateCall(F, Operands);
}

// SVE2's svpmullb and svpmullt builtins are similar to the svpmullb_pair and
// svpmullt_pair intrinsics, with the exception that their results are bitcast
// to a wider type.
Value *CodeGenFunction::EmitSVEPMull(const SVETypeFlags &TypeFlags,
                                     SmallVectorImpl<Value *> &Ops,
                                     unsigned BuiltinID) {
  // Splat scalar operand to vector (intrinsics with _n infix)
  if (TypeFlags.hasSplatOperand()) {
    unsigned OpNo = TypeFlags.getSplatOperand();
    Ops[OpNo] = EmitSVEDupX(Ops[OpNo]);
  }

  // The pair-wise function has a narrower overloaded type.
  Function *F = CGM.getIntrinsic(BuiltinID, Ops[0]->getType());
  Value *Call = Builder.CreateCall(F, {Ops[0], Ops[1]});

  // Now bitcast to the wider result type.
  llvm::ScalableVectorType *Ty = getSVEType(TypeFlags);
  return EmitSVEReinterpret(Call, Ty);
}

Value *CodeGenFunction::EmitSVEMovl(const SVETypeFlags &TypeFlags,
                                    ArrayRef<Value *> Ops, unsigned BuiltinID) {
  llvm::Type *OverloadedTy = getSVEType(TypeFlags);
  Function *F = CGM.getIntrinsic(BuiltinID, OverloadedTy);
  return Builder.CreateCall(F, {Ops[0], Builder.getInt32(0)});
}

Value *CodeGenFunction::EmitSVEPrefetchLoad(const SVETypeFlags &TypeFlags,
                                            SmallVectorImpl<Value *> &Ops,
                                            unsigned BuiltinID) {
  auto *MemEltTy = SVEBuiltinMemEltTy(TypeFlags);
  auto *VectorTy = getSVEVectorForElementType(MemEltTy);
  auto *MemoryTy = llvm::ScalableVectorType::get(MemEltTy, VectorTy);

  Value *Predicate = EmitSVEPredicateCast(Ops[0], MemoryTy);
  Value *BasePtr = Ops[1];

  // Implement the index operand if not omitted.
  if (Ops.size() > 3)
    BasePtr = Builder.CreateGEP(MemoryTy, BasePtr, Ops[2]);

  Value *PrfOp = Ops.back();

  Function *F = CGM.getIntrinsic(BuiltinID, Predicate->getType());
  return Builder.CreateCall(F, {Predicate, BasePtr, PrfOp});
}

Value *CodeGenFunction::EmitSVEMaskedLoad(const CallExpr *E,
                                          llvm::Type *ReturnTy,
                                          SmallVectorImpl<Value *> &Ops,
                                          unsigned IntrinsicID,
                                          bool IsZExtReturn) {
  QualType LangPTy = E->getArg(1)->getType();
  llvm::Type *MemEltTy = CGM.getTypes().ConvertType(
      LangPTy->castAs<PointerType>()->getPointeeType());

  // The vector type that is returned may be different from the
  // eventual type loaded from memory.
  auto VectorTy = cast<llvm::ScalableVectorType>(ReturnTy);
  llvm::ScalableVectorType *MemoryTy = nullptr;
  llvm::ScalableVectorType *PredTy = nullptr;
  bool IsQuadLoad = false;
  switch (IntrinsicID) {
  case Intrinsic::aarch64_sve_ld1uwq:
  case Intrinsic::aarch64_sve_ld1udq:
    MemoryTy = llvm::ScalableVectorType::get(MemEltTy, 1);
    PredTy = llvm::ScalableVectorType::get(
        llvm::Type::getInt1Ty(getLLVMContext()), 1);
    IsQuadLoad = true;
    break;
  default:
    MemoryTy = llvm::ScalableVectorType::get(MemEltTy, VectorTy);
    PredTy = MemoryTy;
    break;
  }

  Value *Predicate = EmitSVEPredicateCast(Ops[0], PredTy);
  Value *BasePtr = Ops[1];

  // Does the load have an offset?
  if (Ops.size() > 2)
    BasePtr = Builder.CreateGEP(MemoryTy, BasePtr, Ops[2]);

  Function *F = CGM.getIntrinsic(IntrinsicID, IsQuadLoad ? VectorTy : MemoryTy);
  auto *Load =
      cast<llvm::Instruction>(Builder.CreateCall(F, {Predicate, BasePtr}));
  auto TBAAInfo = CGM.getTBAAAccessInfo(LangPTy->getPointeeType());
  CGM.DecorateInstructionWithTBAA(Load, TBAAInfo);

  if (IsQuadLoad)
    return Load;

  return IsZExtReturn ? Builder.CreateZExt(Load, VectorTy)
                      : Builder.CreateSExt(Load, VectorTy);
}

Value *CodeGenFunction::EmitSVEMaskedStore(const CallExpr *E,
                                           SmallVectorImpl<Value *> &Ops,
                                           unsigned IntrinsicID) {
  QualType LangPTy = E->getArg(1)->getType();
  llvm::Type *MemEltTy = CGM.getTypes().ConvertType(
      LangPTy->castAs<PointerType>()->getPointeeType());

  // The vector type that is stored may be different from the
  // eventual type stored to memory.
  auto VectorTy = cast<llvm::ScalableVectorType>(Ops.back()->getType());
  auto MemoryTy = llvm::ScalableVectorType::get(MemEltTy, VectorTy);

  auto PredTy = MemoryTy;
  auto AddrMemoryTy = MemoryTy;
  bool IsQuadStore = false;

  switch (IntrinsicID) {
  case Intrinsic::aarch64_sve_st1wq:
  case Intrinsic::aarch64_sve_st1dq:
    AddrMemoryTy = llvm::ScalableVectorType::get(MemEltTy, 1);
    PredTy =
        llvm::ScalableVectorType::get(IntegerType::get(getLLVMContext(), 1), 1);
    IsQuadStore = true;
    break;
  default:
    break;
  }
  Value *Predicate = EmitSVEPredicateCast(Ops[0], PredTy);
  Value *BasePtr = Ops[1];

  // Does the store have an offset?
  if (Ops.size() == 4)
    BasePtr = Builder.CreateGEP(AddrMemoryTy, BasePtr, Ops[2]);

  // Last value is always the data
  Value *Val =
      IsQuadStore ? Ops.back() : Builder.CreateTrunc(Ops.back(), MemoryTy);

  Function *F =
      CGM.getIntrinsic(IntrinsicID, IsQuadStore ? VectorTy : MemoryTy);
  auto *Store =
      cast<llvm::Instruction>(Builder.CreateCall(F, {Val, Predicate, BasePtr}));
  auto TBAAInfo = CGM.getTBAAAccessInfo(LangPTy->getPointeeType());
  CGM.DecorateInstructionWithTBAA(Store, TBAAInfo);
  return Store;
}

Value *CodeGenFunction::EmitSMELd1St1(const SVETypeFlags &TypeFlags,
                                      SmallVectorImpl<Value *> &Ops,
                                      unsigned IntID) {
  Ops[2] = EmitSVEPredicateCast(
      Ops[2], getSVEVectorForElementType(SVEBuiltinMemEltTy(TypeFlags)));

  SmallVector<Value *> NewOps;
  NewOps.push_back(Ops[2]);

  llvm::Value *BasePtr = Ops[3];

  // If the intrinsic contains the vnum parameter, multiply it with the vector
  // size in bytes.
  if (Ops.size() == 5) {
    Function *StreamingVectorLength =
        CGM.getIntrinsic(Intrinsic::aarch64_sme_cntsb);
    llvm::Value *StreamingVectorLengthCall =
        Builder.CreateCall(StreamingVectorLength);
    llvm::Value *Mulvl =
        Builder.CreateMul(StreamingVectorLengthCall, Ops[4], "mulvl");
    // The type of the ptr parameter is void *, so use Int8Ty here.
    BasePtr = Builder.CreateGEP(Int8Ty, Ops[3], Mulvl);
  }
  NewOps.push_back(BasePtr);
  NewOps.push_back(Ops[0]);
  NewOps.push_back(Ops[1]);
  Function *F = CGM.getIntrinsic(IntID);
  return Builder.CreateCall(F, NewOps);
}

Value *CodeGenFunction::EmitSMEReadWrite(const SVETypeFlags &TypeFlags,
                                         SmallVectorImpl<Value *> &Ops,
                                         unsigned IntID) {
  auto *VecTy = getSVEType(TypeFlags);
  Function *F = CGM.getIntrinsic(IntID, VecTy);
  if (TypeFlags.isReadZA())
    Ops[1] = EmitSVEPredicateCast(Ops[1], VecTy);
  else if (TypeFlags.isWriteZA())
    Ops[2] = EmitSVEPredicateCast(Ops[2], VecTy);
  return Builder.CreateCall(F, Ops);
}

Value *CodeGenFunction::EmitSMEZero(const SVETypeFlags &TypeFlags,
                                    SmallVectorImpl<Value *> &Ops,
                                    unsigned IntID) {
  // svzero_za() intrinsic zeros the entire za tile and has no paramters.
  if (Ops.size() == 0)
    Ops.push_back(llvm::ConstantInt::get(Int32Ty, 255));
  Function *F = CGM.getIntrinsic(IntID, {});
  return Builder.CreateCall(F, Ops);
}

Value *CodeGenFunction::EmitSMELdrStr(const SVETypeFlags &TypeFlags,
                                      SmallVectorImpl<Value *> &Ops,
                                      unsigned IntID) {
  if (Ops.size() == 2)
    Ops.push_back(Builder.getInt32(0));
  else
    Ops[2] = Builder.CreateIntCast(Ops[2], Int32Ty, true);
  Function *F = CGM.getIntrinsic(IntID, {});
  return Builder.CreateCall(F, Ops);
}

// Limit the usage of scalable llvm IR generated by the ACLE by using the
// sve dup.x intrinsic instead of IRBuilder::CreateVectorSplat.
Value *CodeGenFunction::EmitSVEDupX(Value *Scalar, llvm::Type *Ty) {
  return Builder.CreateVectorSplat(
      cast<llvm::VectorType>(Ty)->getElementCount(), Scalar);
}

Value *CodeGenFunction::EmitSVEDupX(Value* Scalar) {
  return EmitSVEDupX(Scalar, getSVEVectorForElementType(Scalar->getType()));
}

Value *CodeGenFunction::EmitSVEReinterpret(Value *Val, llvm::Type *Ty) {
  // FIXME: For big endian this needs an additional REV, or needs a separate
  // intrinsic that is code-generated as a no-op, because the LLVM bitcast
  // instruction is defined as 'bitwise' equivalent from memory point of
  // view (when storing/reloading), whereas the svreinterpret builtin
  // implements bitwise equivalent cast from register point of view.
  // LLVM CodeGen for a bitcast must add an explicit REV for big-endian.
  return Builder.CreateBitCast(Val, Ty);
}

static void InsertExplicitZeroOperand(CGBuilderTy &Builder, llvm::Type *Ty,
                                      SmallVectorImpl<Value *> &Ops) {
  auto *SplatZero = Constant::getNullValue(Ty);
  Ops.insert(Ops.begin(), SplatZero);
}

static void InsertExplicitUndefOperand(CGBuilderTy &Builder, llvm::Type *Ty,
                                       SmallVectorImpl<Value *> &Ops) {
  auto *SplatUndef = UndefValue::get(Ty);
  Ops.insert(Ops.begin(), SplatUndef);
}

SmallVector<llvm::Type *, 2>
CodeGenFunction::getSVEOverloadTypes(const SVETypeFlags &TypeFlags,
                                     llvm::Type *ResultType,
                                     ArrayRef<Value *> Ops) {
  if (TypeFlags.isOverloadNone())
    return {};

  llvm::Type *DefaultType = getSVEType(TypeFlags);

  if (TypeFlags.isOverloadWhileOrMultiVecCvt())
    return {DefaultType, Ops[1]->getType()};

  if (TypeFlags.isOverloadWhileRW())
    return {getSVEPredType(TypeFlags), Ops[0]->getType()};

  if (TypeFlags.isOverloadCvt())
    return {Ops[0]->getType(), Ops.back()->getType()};

  if (TypeFlags.isReductionQV() && !ResultType->isScalableTy() &&
      ResultType->isVectorTy())
    return {ResultType, Ops[1]->getType()};

  assert(TypeFlags.isOverloadDefault() && "Unexpected value for overloads");
  return {DefaultType};
}

Value *CodeGenFunction::EmitSVETupleSetOrGet(const SVETypeFlags &TypeFlags,
                                             llvm::Type *Ty,
                                             ArrayRef<Value *> Ops) {
  assert((TypeFlags.isTupleSet() || TypeFlags.isTupleGet()) &&
         "Expects TypleFlags.isTupleSet() or TypeFlags.isTupleGet()");

  unsigned I = cast<ConstantInt>(Ops[1])->getSExtValue();
  auto *SingleVecTy = dyn_cast<llvm::ScalableVectorType>(
      TypeFlags.isTupleSet() ? Ops[2]->getType() : Ty);

  if (!SingleVecTy)
    return nullptr;

  Value *Idx = ConstantInt::get(CGM.Int64Ty,
                                I * SingleVecTy->getMinNumElements());

  if (TypeFlags.isTupleSet())
    return Builder.CreateInsertVector(Ty, Ops[0], Ops[2], Idx);
  return Builder.CreateExtractVector(Ty, Ops[0], Idx);
}

Value *CodeGenFunction::EmitSVETupleCreate(const SVETypeFlags &TypeFlags,
                                             llvm::Type *Ty,
                                             ArrayRef<Value *> Ops) {
  assert(TypeFlags.isTupleCreate() && "Expects TypleFlag isTupleCreate");

  auto *SrcTy = dyn_cast<llvm::ScalableVectorType>(Ops[0]->getType());

  if (!SrcTy)
    return nullptr;

  unsigned MinElts = SrcTy->getMinNumElements();
  Value *Call = llvm::PoisonValue::get(Ty);
  for (unsigned I = 0; I < Ops.size(); I++) {
    Value *Idx = ConstantInt::get(CGM.Int64Ty, I * MinElts);
    Call = Builder.CreateInsertVector(Ty, Call, Ops[I], Idx);
  }

  return Call;
}

Value *CodeGenFunction::FormSVEBuiltinResult(Value *Call) {
  // Multi-vector results should be broken up into a single (wide) result
  // vector.
  auto *StructTy = dyn_cast<StructType>(Call->getType());
  if (!StructTy)
    return Call;

  auto *VTy = dyn_cast<ScalableVectorType>(StructTy->getTypeAtIndex(0U));
  if (!VTy)
    return Call;
  unsigned N = StructTy->getNumElements();

  // We may need to emit a cast to a svbool_t
  bool IsPredTy = VTy->getElementType()->isIntegerTy(1);
  unsigned MinElts = IsPredTy ? 16 : VTy->getMinNumElements();

  ScalableVectorType *WideVTy =
      ScalableVectorType::get(VTy->getElementType(), MinElts * N);
  Value *Ret = llvm::PoisonValue::get(WideVTy);
  for (unsigned I = 0; I < N; ++I) {
    Value *SRet = Builder.CreateExtractValue(Call, I);
    assert(SRet->getType() == VTy && "Unexpected type for result value");
    Value *Idx = ConstantInt::get(CGM.Int64Ty, I * MinElts);

    if (IsPredTy)
      SRet = EmitSVEPredicateCast(
          SRet, ScalableVectorType::get(Builder.getInt1Ty(), 16));

    Ret = Builder.CreateInsertVector(WideVTy, Ret, SRet, Idx);
  }
  Call = Ret;

  return Call;
}

void CodeGenFunction::GetAArch64SVEProcessedOperands(
    unsigned BuiltinID, const CallExpr *E, SmallVectorImpl<Value *> &Ops,
    SVETypeFlags TypeFlags) {
  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  // Tuple set/get only requires one insert/extract vector, which is
  // created by EmitSVETupleSetOrGet.
  bool IsTupleGetOrSet = TypeFlags.isTupleSet() || TypeFlags.isTupleGet();

  for (unsigned i = 0, e = E->getNumArgs(); i != e; i++) {
    bool IsICE = ICEArguments & (1 << i);
    Value *Arg = EmitScalarExpr(E->getArg(i));

    if (IsICE) {
      // If this is required to be a constant, constant fold it so that we know
      // that the generated intrinsic gets a ConstantInt.
      std::optional<llvm::APSInt> Result =
          E->getArg(i)->getIntegerConstantExpr(getContext());
      assert(Result && "Expected argument to be a constant");

      // Immediates for SVE llvm intrinsics are always 32bit.  We can safely
      // truncate because the immediate has been range checked and no valid
      // immediate requires more than a handful of bits.
      *Result = Result->extOrTrunc(32);
      Ops.push_back(llvm::ConstantInt::get(getLLVMContext(), *Result));
      continue;
    }

    if (IsTupleGetOrSet || !isa<ScalableVectorType>(Arg->getType())) {
      Ops.push_back(Arg);
      continue;
    }

    auto *VTy = cast<ScalableVectorType>(Arg->getType());
    unsigned MinElts = VTy->getMinNumElements();
    bool IsPred = VTy->getElementType()->isIntegerTy(1);
    unsigned N = (MinElts * VTy->getScalarSizeInBits()) / (IsPred ? 16 : 128);

    if (N == 1) {
      Ops.push_back(Arg);
      continue;
    }

    for (unsigned I = 0; I < N; ++I) {
      Value *Idx = ConstantInt::get(CGM.Int64Ty, (I * MinElts) / N);
      auto *NewVTy =
          ScalableVectorType::get(VTy->getElementType(), MinElts / N);
      Ops.push_back(Builder.CreateExtractVector(NewVTy, Arg, Idx));
    }
  }
}

Value *CodeGenFunction::EmitAArch64SVEBuiltinExpr(unsigned BuiltinID,
                                                  const CallExpr *E) {
  llvm::Type *Ty = ConvertType(E->getType());
  if (BuiltinID >= SVE::BI__builtin_sve_reinterpret_s8_s8 &&
      BuiltinID <= SVE::BI__builtin_sve_reinterpret_f64_f64_x4) {
    Value *Val = EmitScalarExpr(E->getArg(0));
    return EmitSVEReinterpret(Val, Ty);
  }

  auto *Builtin = findARMVectorIntrinsicInMap(AArch64SVEIntrinsicMap, BuiltinID,
                                              AArch64SVEIntrinsicsProvenSorted);

  llvm::SmallVector<Value *, 4> Ops;
  SVETypeFlags TypeFlags(Builtin->TypeModifier);
  GetAArch64SVEProcessedOperands(BuiltinID, E, Ops, TypeFlags);

  if (TypeFlags.isLoad())
    return EmitSVEMaskedLoad(E, Ty, Ops, Builtin->LLVMIntrinsic,
                             TypeFlags.isZExtReturn());
  else if (TypeFlags.isStore())
    return EmitSVEMaskedStore(E, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isGatherLoad())
    return EmitSVEGatherLoad(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isScatterStore())
    return EmitSVEScatterStore(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isPrefetch())
    return EmitSVEPrefetchLoad(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isGatherPrefetch())
    return EmitSVEGatherPrefetch(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isStructLoad())
    return EmitSVEStructLoad(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isStructStore())
    return EmitSVEStructStore(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isTupleSet() || TypeFlags.isTupleGet())
    return EmitSVETupleSetOrGet(TypeFlags, Ty, Ops);
  else if (TypeFlags.isTupleCreate())
    return EmitSVETupleCreate(TypeFlags, Ty, Ops);
  else if (TypeFlags.isUndef())
    return UndefValue::get(Ty);
  else if (Builtin->LLVMIntrinsic != 0) {
    if (TypeFlags.getMergeType() == SVETypeFlags::MergeZeroExp)
      InsertExplicitZeroOperand(Builder, Ty, Ops);

    if (TypeFlags.getMergeType() == SVETypeFlags::MergeAnyExp)
      InsertExplicitUndefOperand(Builder, Ty, Ops);

    // Some ACLE builtins leave out the argument to specify the predicate
    // pattern, which is expected to be expanded to an SV_ALL pattern.
    if (TypeFlags.isAppendSVALL())
      Ops.push_back(Builder.getInt32(/*SV_ALL*/ 31));
    if (TypeFlags.isInsertOp1SVALL())
      Ops.insert(&Ops[1], Builder.getInt32(/*SV_ALL*/ 31));

    // Predicates must match the main datatype.
    for (unsigned i = 0, e = Ops.size(); i != e; ++i)
      if (auto PredTy = dyn_cast<llvm::VectorType>(Ops[i]->getType()))
        if (PredTy->getElementType()->isIntegerTy(1))
          Ops[i] = EmitSVEPredicateCast(Ops[i], getSVEType(TypeFlags));

    // Splat scalar operand to vector (intrinsics with _n infix)
    if (TypeFlags.hasSplatOperand()) {
      unsigned OpNo = TypeFlags.getSplatOperand();
      Ops[OpNo] = EmitSVEDupX(Ops[OpNo]);
    }

    if (TypeFlags.isReverseCompare())
      std::swap(Ops[1], Ops[2]);
    else if (TypeFlags.isReverseUSDOT())
      std::swap(Ops[1], Ops[2]);
    else if (TypeFlags.isReverseMergeAnyBinOp() &&
             TypeFlags.getMergeType() == SVETypeFlags::MergeAny)
      std::swap(Ops[1], Ops[2]);
    else if (TypeFlags.isReverseMergeAnyAccOp() &&
             TypeFlags.getMergeType() == SVETypeFlags::MergeAny)
      std::swap(Ops[1], Ops[3]);

    // Predicated intrinsics with _z suffix need a select w/ zeroinitializer.
    if (TypeFlags.getMergeType() == SVETypeFlags::MergeZero) {
      llvm::Type *OpndTy = Ops[1]->getType();
      auto *SplatZero = Constant::getNullValue(OpndTy);
      Ops[1] = Builder.CreateSelect(Ops[0], Ops[1], SplatZero);
    }

    Function *F = CGM.getIntrinsic(Builtin->LLVMIntrinsic,
                                   getSVEOverloadTypes(TypeFlags, Ty, Ops));
    Value *Call = Builder.CreateCall(F, Ops);

    // Predicate results must be converted to svbool_t.
    if (auto PredTy = dyn_cast<llvm::VectorType>(Call->getType()))
      if (PredTy->getScalarType()->isIntegerTy(1))
        Call = EmitSVEPredicateCast(Call, cast<llvm::ScalableVectorType>(Ty));

    return FormSVEBuiltinResult(Call);
  }

  switch (BuiltinID) {
  default:
    return nullptr;

  case SVE::BI__builtin_sve_svreinterpret_b: {
    auto SVCountTy =
        llvm::TargetExtType::get(getLLVMContext(), "aarch64.svcount");
    Function *CastFromSVCountF =
        CGM.getIntrinsic(Intrinsic::aarch64_sve_convert_to_svbool, SVCountTy);
    return Builder.CreateCall(CastFromSVCountF, Ops[0]);
  }
  case SVE::BI__builtin_sve_svreinterpret_c: {
    auto SVCountTy =
        llvm::TargetExtType::get(getLLVMContext(), "aarch64.svcount");
    Function *CastToSVCountF =
        CGM.getIntrinsic(Intrinsic::aarch64_sve_convert_from_svbool, SVCountTy);
    return Builder.CreateCall(CastToSVCountF, Ops[0]);
  }

  case SVE::BI__builtin_sve_svpsel_lane_b8:
  case SVE::BI__builtin_sve_svpsel_lane_b16:
  case SVE::BI__builtin_sve_svpsel_lane_b32:
  case SVE::BI__builtin_sve_svpsel_lane_b64:
  case SVE::BI__builtin_sve_svpsel_lane_c8:
  case SVE::BI__builtin_sve_svpsel_lane_c16:
  case SVE::BI__builtin_sve_svpsel_lane_c32:
  case SVE::BI__builtin_sve_svpsel_lane_c64: {
    bool IsSVCount = isa<TargetExtType>(Ops[0]->getType());
    assert(((!IsSVCount || cast<TargetExtType>(Ops[0]->getType())->getName() ==
                               "aarch64.svcount")) &&
           "Unexpected TargetExtType");
    auto SVCountTy =
        llvm::TargetExtType::get(getLLVMContext(), "aarch64.svcount");
    Function *CastFromSVCountF =
        CGM.getIntrinsic(Intrinsic::aarch64_sve_convert_to_svbool, SVCountTy);
    Function *CastToSVCountF =
        CGM.getIntrinsic(Intrinsic::aarch64_sve_convert_from_svbool, SVCountTy);

    auto OverloadedTy = getSVEType(SVETypeFlags(Builtin->TypeModifier));
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_sve_psel, OverloadedTy);
    llvm::Value *Ops0 =
        IsSVCount ? Builder.CreateCall(CastFromSVCountF, Ops[0]) : Ops[0];
    llvm::Value *Ops1 = EmitSVEPredicateCast(Ops[1], OverloadedTy);
    llvm::Value *PSel = Builder.CreateCall(F, {Ops0, Ops1, Ops[2]});
    return IsSVCount ? Builder.CreateCall(CastToSVCountF, PSel) : PSel;
  }
  case SVE::BI__builtin_sve_svmov_b_z: {
    // svmov_b_z(pg, op) <=> svand_b_z(pg, op, op)
    SVETypeFlags TypeFlags(Builtin->TypeModifier);
    llvm::Type* OverloadedTy = getSVEType(TypeFlags);
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_sve_and_z, OverloadedTy);
    return Builder.CreateCall(F, {Ops[0], Ops[1], Ops[1]});
  }

  case SVE::BI__builtin_sve_svnot_b_z: {
    // svnot_b_z(pg, op) <=> sveor_b_z(pg, op, pg)
    SVETypeFlags TypeFlags(Builtin->TypeModifier);
    llvm::Type* OverloadedTy = getSVEType(TypeFlags);
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_sve_eor_z, OverloadedTy);
    return Builder.CreateCall(F, {Ops[0], Ops[1], Ops[0]});
  }

  case SVE::BI__builtin_sve_svmovlb_u16:
  case SVE::BI__builtin_sve_svmovlb_u32:
  case SVE::BI__builtin_sve_svmovlb_u64:
    return EmitSVEMovl(TypeFlags, Ops, Intrinsic::aarch64_sve_ushllb);

  case SVE::BI__builtin_sve_svmovlb_s16:
  case SVE::BI__builtin_sve_svmovlb_s32:
  case SVE::BI__builtin_sve_svmovlb_s64:
    return EmitSVEMovl(TypeFlags, Ops, Intrinsic::aarch64_sve_sshllb);

  case SVE::BI__builtin_sve_svmovlt_u16:
  case SVE::BI__builtin_sve_svmovlt_u32:
  case SVE::BI__builtin_sve_svmovlt_u64:
    return EmitSVEMovl(TypeFlags, Ops, Intrinsic::aarch64_sve_ushllt);

  case SVE::BI__builtin_sve_svmovlt_s16:
  case SVE::BI__builtin_sve_svmovlt_s32:
  case SVE::BI__builtin_sve_svmovlt_s64:
    return EmitSVEMovl(TypeFlags, Ops, Intrinsic::aarch64_sve_sshllt);

  case SVE::BI__builtin_sve_svpmullt_u16:
  case SVE::BI__builtin_sve_svpmullt_u64:
  case SVE::BI__builtin_sve_svpmullt_n_u16:
  case SVE::BI__builtin_sve_svpmullt_n_u64:
    return EmitSVEPMull(TypeFlags, Ops, Intrinsic::aarch64_sve_pmullt_pair);

  case SVE::BI__builtin_sve_svpmullb_u16:
  case SVE::BI__builtin_sve_svpmullb_u64:
  case SVE::BI__builtin_sve_svpmullb_n_u16:
  case SVE::BI__builtin_sve_svpmullb_n_u64:
    return EmitSVEPMull(TypeFlags, Ops, Intrinsic::aarch64_sve_pmullb_pair);

  case SVE::BI__builtin_sve_svdup_n_b8:
  case SVE::BI__builtin_sve_svdup_n_b16:
  case SVE::BI__builtin_sve_svdup_n_b32:
  case SVE::BI__builtin_sve_svdup_n_b64: {
    Value *CmpNE =
        Builder.CreateICmpNE(Ops[0], Constant::getNullValue(Ops[0]->getType()));
    llvm::ScalableVectorType *OverloadedTy = getSVEType(TypeFlags);
    Value *Dup = EmitSVEDupX(CmpNE, OverloadedTy);
    return EmitSVEPredicateCast(Dup, cast<llvm::ScalableVectorType>(Ty));
  }

  case SVE::BI__builtin_sve_svdupq_n_b8:
  case SVE::BI__builtin_sve_svdupq_n_b16:
  case SVE::BI__builtin_sve_svdupq_n_b32:
  case SVE::BI__builtin_sve_svdupq_n_b64:
  case SVE::BI__builtin_sve_svdupq_n_u8:
  case SVE::BI__builtin_sve_svdupq_n_s8:
  case SVE::BI__builtin_sve_svdupq_n_u64:
  case SVE::BI__builtin_sve_svdupq_n_f64:
  case SVE::BI__builtin_sve_svdupq_n_s64:
  case SVE::BI__builtin_sve_svdupq_n_u16:
  case SVE::BI__builtin_sve_svdupq_n_f16:
  case SVE::BI__builtin_sve_svdupq_n_bf16:
  case SVE::BI__builtin_sve_svdupq_n_s16:
  case SVE::BI__builtin_sve_svdupq_n_u32:
  case SVE::BI__builtin_sve_svdupq_n_f32:
  case SVE::BI__builtin_sve_svdupq_n_s32: {
    // These builtins are implemented by storing each element to an array and using
    // ld1rq to materialize a vector.
    unsigned NumOpnds = Ops.size();

    bool IsBoolTy =
        cast<llvm::VectorType>(Ty)->getElementType()->isIntegerTy(1);

    // For svdupq_n_b* the element type of is an integer of type 128/numelts,
    // so that the compare can use the width that is natural for the expected
    // number of predicate lanes.
    llvm::Type *EltTy = Ops[0]->getType();
    if (IsBoolTy)
      EltTy = IntegerType::get(getLLVMContext(), SVEBitsPerBlock / NumOpnds);

    SmallVector<llvm::Value *, 16> VecOps;
    for (unsigned I = 0; I < NumOpnds; ++I)
        VecOps.push_back(Builder.CreateZExt(Ops[I], EltTy));
    Value *Vec = BuildVector(VecOps);

    llvm::Type *OverloadedTy = getSVEVectorForElementType(EltTy);
    Value *InsertSubVec = Builder.CreateInsertVector(
        OverloadedTy, PoisonValue::get(OverloadedTy), Vec, Builder.getInt64(0));

    Function *F =
        CGM.getIntrinsic(Intrinsic::aarch64_sve_dupq_lane, OverloadedTy);
    Value *DupQLane =
        Builder.CreateCall(F, {InsertSubVec, Builder.getInt64(0)});

    if (!IsBoolTy)
      return DupQLane;

    SVETypeFlags TypeFlags(Builtin->TypeModifier);
    Value *Pred = EmitSVEAllTruePred(TypeFlags);

    // For svdupq_n_b* we need to add an additional 'cmpne' with '0'.
    F = CGM.getIntrinsic(NumOpnds == 2 ? Intrinsic::aarch64_sve_cmpne
                                       : Intrinsic::aarch64_sve_cmpne_wide,
                         OverloadedTy);
    Value *Call = Builder.CreateCall(
        F, {Pred, DupQLane, EmitSVEDupX(Builder.getInt64(0))});
    return EmitSVEPredicateCast(Call, cast<llvm::ScalableVectorType>(Ty));
  }

  case SVE::BI__builtin_sve_svpfalse_b:
    return ConstantInt::getFalse(Ty);

  case SVE::BI__builtin_sve_svpfalse_c: {
    auto SVBoolTy = ScalableVectorType::get(Builder.getInt1Ty(), 16);
    Function *CastToSVCountF =
        CGM.getIntrinsic(Intrinsic::aarch64_sve_convert_from_svbool, Ty);
    return Builder.CreateCall(CastToSVCountF, ConstantInt::getFalse(SVBoolTy));
  }

  case SVE::BI__builtin_sve_svlen_bf16:
  case SVE::BI__builtin_sve_svlen_f16:
  case SVE::BI__builtin_sve_svlen_f32:
  case SVE::BI__builtin_sve_svlen_f64:
  case SVE::BI__builtin_sve_svlen_s8:
  case SVE::BI__builtin_sve_svlen_s16:
  case SVE::BI__builtin_sve_svlen_s32:
  case SVE::BI__builtin_sve_svlen_s64:
  case SVE::BI__builtin_sve_svlen_u8:
  case SVE::BI__builtin_sve_svlen_u16:
  case SVE::BI__builtin_sve_svlen_u32:
  case SVE::BI__builtin_sve_svlen_u64: {
    SVETypeFlags TF(Builtin->TypeModifier);
    auto VTy = cast<llvm::VectorType>(getSVEType(TF));
    auto *NumEls =
        llvm::ConstantInt::get(Ty, VTy->getElementCount().getKnownMinValue());

    Function *F = CGM.getIntrinsic(Intrinsic::vscale, Ty);
    return Builder.CreateMul(NumEls, Builder.CreateCall(F));
  }

  case SVE::BI__builtin_sve_svtbl2_u8:
  case SVE::BI__builtin_sve_svtbl2_s8:
  case SVE::BI__builtin_sve_svtbl2_u16:
  case SVE::BI__builtin_sve_svtbl2_s16:
  case SVE::BI__builtin_sve_svtbl2_u32:
  case SVE::BI__builtin_sve_svtbl2_s32:
  case SVE::BI__builtin_sve_svtbl2_u64:
  case SVE::BI__builtin_sve_svtbl2_s64:
  case SVE::BI__builtin_sve_svtbl2_f16:
  case SVE::BI__builtin_sve_svtbl2_bf16:
  case SVE::BI__builtin_sve_svtbl2_f32:
  case SVE::BI__builtin_sve_svtbl2_f64: {
    SVETypeFlags TF(Builtin->TypeModifier);
    auto VTy = cast<llvm::ScalableVectorType>(getSVEType(TF));
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_sve_tbl2, VTy);
    return Builder.CreateCall(F, Ops);
  }

  case SVE::BI__builtin_sve_svset_neonq_s8:
  case SVE::BI__builtin_sve_svset_neonq_s16:
  case SVE::BI__builtin_sve_svset_neonq_s32:
  case SVE::BI__builtin_sve_svset_neonq_s64:
  case SVE::BI__builtin_sve_svset_neonq_u8:
  case SVE::BI__builtin_sve_svset_neonq_u16:
  case SVE::BI__builtin_sve_svset_neonq_u32:
  case SVE::BI__builtin_sve_svset_neonq_u64:
  case SVE::BI__builtin_sve_svset_neonq_f16:
  case SVE::BI__builtin_sve_svset_neonq_f32:
  case SVE::BI__builtin_sve_svset_neonq_f64:
  case SVE::BI__builtin_sve_svset_neonq_bf16: {
    return Builder.CreateInsertVector(Ty, Ops[0], Ops[1], Builder.getInt64(0));
  }

  case SVE::BI__builtin_sve_svget_neonq_s8:
  case SVE::BI__builtin_sve_svget_neonq_s16:
  case SVE::BI__builtin_sve_svget_neonq_s32:
  case SVE::BI__builtin_sve_svget_neonq_s64:
  case SVE::BI__builtin_sve_svget_neonq_u8:
  case SVE::BI__builtin_sve_svget_neonq_u16:
  case SVE::BI__builtin_sve_svget_neonq_u32:
  case SVE::BI__builtin_sve_svget_neonq_u64:
  case SVE::BI__builtin_sve_svget_neonq_f16:
  case SVE::BI__builtin_sve_svget_neonq_f32:
  case SVE::BI__builtin_sve_svget_neonq_f64:
  case SVE::BI__builtin_sve_svget_neonq_bf16: {
    return Builder.CreateExtractVector(Ty, Ops[0], Builder.getInt64(0));
  }

  case SVE::BI__builtin_sve_svdup_neonq_s8:
  case SVE::BI__builtin_sve_svdup_neonq_s16:
  case SVE::BI__builtin_sve_svdup_neonq_s32:
  case SVE::BI__builtin_sve_svdup_neonq_s64:
  case SVE::BI__builtin_sve_svdup_neonq_u8:
  case SVE::BI__builtin_sve_svdup_neonq_u16:
  case SVE::BI__builtin_sve_svdup_neonq_u32:
  case SVE::BI__builtin_sve_svdup_neonq_u64:
  case SVE::BI__builtin_sve_svdup_neonq_f16:
  case SVE::BI__builtin_sve_svdup_neonq_f32:
  case SVE::BI__builtin_sve_svdup_neonq_f64:
  case SVE::BI__builtin_sve_svdup_neonq_bf16: {
    Value *Insert = Builder.CreateInsertVector(Ty, PoisonValue::get(Ty), Ops[0],
                                               Builder.getInt64(0));
    return Builder.CreateIntrinsic(Intrinsic::aarch64_sve_dupq_lane, {Ty},
                                   {Insert, Builder.getInt64(0)});
  }
  }

  /// Should not happen
  return nullptr;
}

static void swapCommutativeSMEOperands(unsigned BuiltinID,
                                       SmallVectorImpl<Value *> &Ops) {
  unsigned MultiVec;
  switch (BuiltinID) {
  default:
    return;
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x1:
    MultiVec = 1;
    break;
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x2:
  case SME::BI__builtin_sme_svsudot_za32_s8_vg1x2:
    MultiVec = 2;
    break;
  case SME::BI__builtin_sme_svsudot_za32_s8_vg1x4:
  case SME::BI__builtin_sme_svsumla_za32_s8_vg4x4:
    MultiVec = 4;
    break;
  }

  if (MultiVec > 0)
    for (unsigned I = 0; I < MultiVec; ++I)
      std::swap(Ops[I + 1], Ops[I + 1 + MultiVec]);
}

Value *CodeGenFunction::EmitAArch64SMEBuiltinExpr(unsigned BuiltinID,
                                                  const CallExpr *E) {
  auto *Builtin = findARMVectorIntrinsicInMap(AArch64SMEIntrinsicMap, BuiltinID,
                                              AArch64SMEIntrinsicsProvenSorted);

  llvm::SmallVector<Value *, 4> Ops;
  SVETypeFlags TypeFlags(Builtin->TypeModifier);
  GetAArch64SVEProcessedOperands(BuiltinID, E, Ops, TypeFlags);

  if (TypeFlags.isLoad() || TypeFlags.isStore())
    return EmitSMELd1St1(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (TypeFlags.isReadZA() || TypeFlags.isWriteZA())
    return EmitSMEReadWrite(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (BuiltinID == SME::BI__builtin_sme_svzero_mask_za ||
           BuiltinID == SME::BI__builtin_sme_svzero_za)
    return EmitSMEZero(TypeFlags, Ops, Builtin->LLVMIntrinsic);
  else if (BuiltinID == SME::BI__builtin_sme_svldr_vnum_za ||
           BuiltinID == SME::BI__builtin_sme_svstr_vnum_za ||
           BuiltinID == SME::BI__builtin_sme_svldr_za ||
           BuiltinID == SME::BI__builtin_sme_svstr_za)
    return EmitSMELdrStr(TypeFlags, Ops, Builtin->LLVMIntrinsic);

  // Handle builtins which require their multi-vector operands to be swapped
  swapCommutativeSMEOperands(BuiltinID, Ops);

  // Should not happen!
  if (Builtin->LLVMIntrinsic == 0)
    return nullptr;

  // Predicates must match the main datatype.
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    if (auto PredTy = dyn_cast<llvm::VectorType>(Ops[i]->getType()))
      if (PredTy->getElementType()->isIntegerTy(1))
        Ops[i] = EmitSVEPredicateCast(Ops[i], getSVEType(TypeFlags));

  Function *F =
      TypeFlags.isOverloadNone()
          ? CGM.getIntrinsic(Builtin->LLVMIntrinsic)
          : CGM.getIntrinsic(Builtin->LLVMIntrinsic, {getSVEType(TypeFlags)});
  Value *Call = Builder.CreateCall(F, Ops);

  return FormSVEBuiltinResult(Call);
}

Value *CodeGenFunction::EmitAArch64BuiltinExpr(unsigned BuiltinID,
                                               const CallExpr *E,
                                               llvm::Triple::ArchType Arch) {
  if (BuiltinID >= clang::AArch64::FirstSVEBuiltin &&
      BuiltinID <= clang::AArch64::LastSVEBuiltin)
    return EmitAArch64SVEBuiltinExpr(BuiltinID, E);

  if (BuiltinID >= clang::AArch64::FirstSMEBuiltin &&
      BuiltinID <= clang::AArch64::LastSMEBuiltin)
    return EmitAArch64SMEBuiltinExpr(BuiltinID, E);

  if (BuiltinID == Builtin::BI__builtin_cpu_supports)
    return EmitAArch64CpuSupports(E);

  unsigned HintID = static_cast<unsigned>(-1);
  switch (BuiltinID) {
  default: break;
  case clang::AArch64::BI__builtin_arm_nop:
    HintID = 0;
    break;
  case clang::AArch64::BI__builtin_arm_yield:
  case clang::AArch64::BI__yield:
    HintID = 1;
    break;
  case clang::AArch64::BI__builtin_arm_wfe:
  case clang::AArch64::BI__wfe:
    HintID = 2;
    break;
  case clang::AArch64::BI__builtin_arm_wfi:
  case clang::AArch64::BI__wfi:
    HintID = 3;
    break;
  case clang::AArch64::BI__builtin_arm_sev:
  case clang::AArch64::BI__sev:
    HintID = 4;
    break;
  case clang::AArch64::BI__builtin_arm_sevl:
  case clang::AArch64::BI__sevl:
    HintID = 5;
    break;
  }

  if (HintID != static_cast<unsigned>(-1)) {
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_hint);
    return Builder.CreateCall(F, llvm::ConstantInt::get(Int32Ty, HintID));
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_trap) {
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_break);
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(F, Builder.CreateZExt(Arg, CGM.Int32Ty));
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_get_sme_state) {
    // Create call to __arm_sme_state and store the results to the two pointers.
    CallInst *CI = EmitRuntimeCall(CGM.CreateRuntimeFunction(
        llvm::FunctionType::get(StructType::get(CGM.Int64Ty, CGM.Int64Ty), {},
                                false),
        "__arm_sme_state"));
    auto Attrs = AttributeList().addFnAttribute(getLLVMContext(),
                                                "aarch64_pstate_sm_compatible");
    CI->setAttributes(Attrs);
    CI->setCallingConv(
        llvm::CallingConv::
            AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2);
    Builder.CreateStore(Builder.CreateExtractValue(CI, 0),
                        EmitPointerWithAlignment(E->getArg(0)));
    return Builder.CreateStore(Builder.CreateExtractValue(CI, 1),
                               EmitPointerWithAlignment(E->getArg(1)));
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rbit) {
    assert((getContext().getTypeSize(E->getType()) == 32) &&
           "rbit of unusual size!");
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::bitreverse, Arg->getType()), Arg, "rbit");
  }
  if (BuiltinID == clang::AArch64::BI__builtin_arm_rbit64) {
    assert((getContext().getTypeSize(E->getType()) == 64) &&
           "rbit of unusual size!");
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::bitreverse, Arg->getType()), Arg, "rbit");
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_clz ||
      BuiltinID == clang::AArch64::BI__builtin_arm_clz64) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, Arg->getType());
    Value *Res = Builder.CreateCall(F, {Arg, Builder.getInt1(false)});
    if (BuiltinID == clang::AArch64::BI__builtin_arm_clz64)
      Res = Builder.CreateTrunc(Res, Builder.getInt32Ty());
    return Res;
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_cls) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::aarch64_cls), Arg,
                              "cls");
  }
  if (BuiltinID == clang::AArch64::BI__builtin_arm_cls64) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::aarch64_cls64), Arg,
                              "cls");
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rint32zf ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rint32z) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *Ty = Arg->getType();
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::aarch64_frint32z, Ty),
                              Arg, "frint32z");
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rint64zf ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rint64z) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *Ty = Arg->getType();
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::aarch64_frint64z, Ty),
                              Arg, "frint64z");
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rint32xf ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rint32x) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *Ty = Arg->getType();
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::aarch64_frint32x, Ty),
                              Arg, "frint32x");
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rint64xf ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rint64x) {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *Ty = Arg->getType();
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::aarch64_frint64x, Ty),
                              Arg, "frint64x");
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_jcvt) {
    assert((getContext().getTypeSize(E->getType()) == 32) &&
           "__jcvt of unusual size!");
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::aarch64_fjcvtzs), Arg);
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_ld64b ||
      BuiltinID == clang::AArch64::BI__builtin_arm_st64b ||
      BuiltinID == clang::AArch64::BI__builtin_arm_st64bv ||
      BuiltinID == clang::AArch64::BI__builtin_arm_st64bv0) {
    llvm::Value *MemAddr = EmitScalarExpr(E->getArg(0));
    llvm::Value *ValPtr = EmitScalarExpr(E->getArg(1));

    if (BuiltinID == clang::AArch64::BI__builtin_arm_ld64b) {
      // Load from the address via an LLVM intrinsic, receiving a
      // tuple of 8 i64 words, and store each one to ValPtr.
      Function *F = CGM.getIntrinsic(Intrinsic::aarch64_ld64b);
      llvm::Value *Val = Builder.CreateCall(F, MemAddr);
      llvm::Value *ToRet;
      for (size_t i = 0; i < 8; i++) {
        llvm::Value *ValOffsetPtr =
            Builder.CreateGEP(Int64Ty, ValPtr, Builder.getInt32(i));
        Address Addr =
            Address(ValOffsetPtr, Int64Ty, CharUnits::fromQuantity(8));
        ToRet = Builder.CreateStore(Builder.CreateExtractValue(Val, i), Addr);
      }
      return ToRet;
    } else {
      // Load 8 i64 words from ValPtr, and store them to the address
      // via an LLVM intrinsic.
      SmallVector<llvm::Value *, 9> Args;
      Args.push_back(MemAddr);
      for (size_t i = 0; i < 8; i++) {
        llvm::Value *ValOffsetPtr =
            Builder.CreateGEP(Int64Ty, ValPtr, Builder.getInt32(i));
        Address Addr =
            Address(ValOffsetPtr, Int64Ty, CharUnits::fromQuantity(8));
        Args.push_back(Builder.CreateLoad(Addr));
      }

      auto Intr = (BuiltinID == clang::AArch64::BI__builtin_arm_st64b
                       ? Intrinsic::aarch64_st64b
                   : BuiltinID == clang::AArch64::BI__builtin_arm_st64bv
                       ? Intrinsic::aarch64_st64bv
                       : Intrinsic::aarch64_st64bv0);
      Function *F = CGM.getIntrinsic(Intr);
      return Builder.CreateCall(F, Args);
    }
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rndr ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rndrrs) {

    auto Intr = (BuiltinID == clang::AArch64::BI__builtin_arm_rndr
                     ? Intrinsic::aarch64_rndr
                     : Intrinsic::aarch64_rndrrs);
    Function *F = CGM.getIntrinsic(Intr);
    llvm::Value *Val = Builder.CreateCall(F);
    Value *RandomValue = Builder.CreateExtractValue(Val, 0);
    Value *Status = Builder.CreateExtractValue(Val, 1);

    Address MemAddress = EmitPointerWithAlignment(E->getArg(0));
    Builder.CreateStore(RandomValue, MemAddress);
    Status = Builder.CreateZExt(Status, Int32Ty);
    return Status;
  }

  if (BuiltinID == clang::AArch64::BI__clear_cache) {
    assert(E->getNumArgs() == 2 && "__clear_cache takes 2 arguments");
    const FunctionDecl *FD = E->getDirectCallee();
    Value *Ops[2];
    for (unsigned i = 0; i < 2; i++)
      Ops[i] = EmitScalarExpr(E->getArg(i));
    llvm::Type *Ty = CGM.getTypes().ConvertType(FD->getType());
    llvm::FunctionType *FTy = cast<llvm::FunctionType>(Ty);
    StringRef Name = FD->getName();
    return EmitNounwindRuntimeCall(CGM.CreateRuntimeFunction(FTy, Name), Ops);
  }

  if ((BuiltinID == clang::AArch64::BI__builtin_arm_ldrex ||
       BuiltinID == clang::AArch64::BI__builtin_arm_ldaex) &&
      getContext().getTypeSize(E->getType()) == 128) {
    Function *F =
        CGM.getIntrinsic(BuiltinID == clang::AArch64::BI__builtin_arm_ldaex
                             ? Intrinsic::aarch64_ldaxp
                             : Intrinsic::aarch64_ldxp);

    Value *LdPtr = EmitScalarExpr(E->getArg(0));
    Value *Val = Builder.CreateCall(F, LdPtr, "ldxp");

    Value *Val0 = Builder.CreateExtractValue(Val, 1);
    Value *Val1 = Builder.CreateExtractValue(Val, 0);
    llvm::Type *Int128Ty = llvm::IntegerType::get(getLLVMContext(), 128);
    Val0 = Builder.CreateZExt(Val0, Int128Ty);
    Val1 = Builder.CreateZExt(Val1, Int128Ty);

    Value *ShiftCst = llvm::ConstantInt::get(Int128Ty, 64);
    Val = Builder.CreateShl(Val0, ShiftCst, "shl", true /* nuw */);
    Val = Builder.CreateOr(Val, Val1);
    return Builder.CreateBitCast(Val, ConvertType(E->getType()));
  } else if (BuiltinID == clang::AArch64::BI__builtin_arm_ldrex ||
             BuiltinID == clang::AArch64::BI__builtin_arm_ldaex) {
    Value *LoadAddr = EmitScalarExpr(E->getArg(0));

    QualType Ty = E->getType();
    llvm::Type *RealResTy = ConvertType(Ty);
    llvm::Type *IntTy =
        llvm::IntegerType::get(getLLVMContext(), getContext().getTypeSize(Ty));

    Function *F =
        CGM.getIntrinsic(BuiltinID == clang::AArch64::BI__builtin_arm_ldaex
                             ? Intrinsic::aarch64_ldaxr
                             : Intrinsic::aarch64_ldxr,
                         UnqualPtrTy);
    CallInst *Val = Builder.CreateCall(F, LoadAddr, "ldxr");
    Val->addParamAttr(
        0, Attribute::get(getLLVMContext(), Attribute::ElementType, IntTy));

    if (RealResTy->isPointerTy())
      return Builder.CreateIntToPtr(Val, RealResTy);

    llvm::Type *IntResTy = llvm::IntegerType::get(
        getLLVMContext(), CGM.getDataLayout().getTypeSizeInBits(RealResTy));
    return Builder.CreateBitCast(Builder.CreateTruncOrBitCast(Val, IntResTy),
                                 RealResTy);
  }

  if ((BuiltinID == clang::AArch64::BI__builtin_arm_strex ||
       BuiltinID == clang::AArch64::BI__builtin_arm_stlex) &&
      getContext().getTypeSize(E->getArg(0)->getType()) == 128) {
    Function *F =
        CGM.getIntrinsic(BuiltinID == clang::AArch64::BI__builtin_arm_stlex
                             ? Intrinsic::aarch64_stlxp
                             : Intrinsic::aarch64_stxp);
    llvm::Type *STy = llvm::StructType::get(Int64Ty, Int64Ty);

    Address Tmp = CreateMemTemp(E->getArg(0)->getType());
    EmitAnyExprToMem(E->getArg(0), Tmp, Qualifiers(), /*init*/ true);

    Tmp = Tmp.withElementType(STy);
    llvm::Value *Val = Builder.CreateLoad(Tmp);

    Value *Arg0 = Builder.CreateExtractValue(Val, 0);
    Value *Arg1 = Builder.CreateExtractValue(Val, 1);
    Value *StPtr = EmitScalarExpr(E->getArg(1));
    return Builder.CreateCall(F, {Arg0, Arg1, StPtr}, "stxp");
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_strex ||
      BuiltinID == clang::AArch64::BI__builtin_arm_stlex) {
    Value *StoreVal = EmitScalarExpr(E->getArg(0));
    Value *StoreAddr = EmitScalarExpr(E->getArg(1));

    QualType Ty = E->getArg(0)->getType();
    llvm::Type *StoreTy =
        llvm::IntegerType::get(getLLVMContext(), getContext().getTypeSize(Ty));

    if (StoreVal->getType()->isPointerTy())
      StoreVal = Builder.CreatePtrToInt(StoreVal, Int64Ty);
    else {
      llvm::Type *IntTy = llvm::IntegerType::get(
          getLLVMContext(),
          CGM.getDataLayout().getTypeSizeInBits(StoreVal->getType()));
      StoreVal = Builder.CreateBitCast(StoreVal, IntTy);
      StoreVal = Builder.CreateZExtOrBitCast(StoreVal, Int64Ty);
    }

    Function *F =
        CGM.getIntrinsic(BuiltinID == clang::AArch64::BI__builtin_arm_stlex
                             ? Intrinsic::aarch64_stlxr
                             : Intrinsic::aarch64_stxr,
                         StoreAddr->getType());
    CallInst *CI = Builder.CreateCall(F, {StoreVal, StoreAddr}, "stxr");
    CI->addParamAttr(
        1, Attribute::get(getLLVMContext(), Attribute::ElementType, StoreTy));
    return CI;
  }

  if (BuiltinID == clang::AArch64::BI__getReg) {
    Expr::EvalResult Result;
    if (!E->getArg(0)->EvaluateAsInt(Result, CGM.getContext()))
      llvm_unreachable("Sema will ensure that the parameter is constant");

    llvm::APSInt Value = Result.Val.getInt();
    LLVMContext &Context = CGM.getLLVMContext();
    std::string Reg = Value == 31 ? "sp" : "x" + toString(Value, 10);

    llvm::Metadata *Ops[] = {llvm::MDString::get(Context, Reg)};
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

    llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::read_register, {Int64Ty});
    return Builder.CreateCall(F, Metadata);
  }

  if (BuiltinID == clang::AArch64::BI__break) {
    Expr::EvalResult Result;
    if (!E->getArg(0)->EvaluateAsInt(Result, CGM.getContext()))
      llvm_unreachable("Sema will ensure that the parameter is constant");

    llvm::Function *F = CGM.getIntrinsic(llvm::Intrinsic::aarch64_break);
    return Builder.CreateCall(F, {EmitScalarExpr(E->getArg(0))});
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_clrex) {
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_clrex);
    return Builder.CreateCall(F);
  }

  if (BuiltinID == clang::AArch64::BI_ReadWriteBarrier)
    return Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent,
                               llvm::SyncScope::SingleThread);

  // CRC32
  Intrinsic::ID CRCIntrinsicID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  case clang::AArch64::BI__builtin_arm_crc32b:
    CRCIntrinsicID = Intrinsic::aarch64_crc32b; break;
  case clang::AArch64::BI__builtin_arm_crc32cb:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cb; break;
  case clang::AArch64::BI__builtin_arm_crc32h:
    CRCIntrinsicID = Intrinsic::aarch64_crc32h; break;
  case clang::AArch64::BI__builtin_arm_crc32ch:
    CRCIntrinsicID = Intrinsic::aarch64_crc32ch; break;
  case clang::AArch64::BI__builtin_arm_crc32w:
    CRCIntrinsicID = Intrinsic::aarch64_crc32w; break;
  case clang::AArch64::BI__builtin_arm_crc32cw:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cw; break;
  case clang::AArch64::BI__builtin_arm_crc32d:
    CRCIntrinsicID = Intrinsic::aarch64_crc32x; break;
  case clang::AArch64::BI__builtin_arm_crc32cd:
    CRCIntrinsicID = Intrinsic::aarch64_crc32cx; break;
  }

  if (CRCIntrinsicID != Intrinsic::not_intrinsic) {
    Value *Arg0 = EmitScalarExpr(E->getArg(0));
    Value *Arg1 = EmitScalarExpr(E->getArg(1));
    Function *F = CGM.getIntrinsic(CRCIntrinsicID);

    llvm::Type *DataTy = F->getFunctionType()->getParamType(1);
    Arg1 = Builder.CreateZExtOrBitCast(Arg1, DataTy);

    return Builder.CreateCall(F, {Arg0, Arg1});
  }

  // Memory Operations (MOPS)
  if (BuiltinID == AArch64::BI__builtin_arm_mops_memset_tag) {
    Value *Dst = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    Value *Size = EmitScalarExpr(E->getArg(2));
    Dst = Builder.CreatePointerCast(Dst, Int8PtrTy);
    Val = Builder.CreateTrunc(Val, Int8Ty);
    Size = Builder.CreateIntCast(Size, Int64Ty, false);
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::aarch64_mops_memset_tag), {Dst, Val, Size});
  }

  // Memory Tagging Extensions (MTE) Intrinsics
  Intrinsic::ID MTEIntrinsicID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  case clang::AArch64::BI__builtin_arm_irg:
    MTEIntrinsicID = Intrinsic::aarch64_irg; break;
  case clang::AArch64::BI__builtin_arm_addg:
    MTEIntrinsicID = Intrinsic::aarch64_addg; break;
  case clang::AArch64::BI__builtin_arm_gmi:
    MTEIntrinsicID = Intrinsic::aarch64_gmi; break;
  case clang::AArch64::BI__builtin_arm_ldg:
    MTEIntrinsicID = Intrinsic::aarch64_ldg; break;
  case clang::AArch64::BI__builtin_arm_stg:
    MTEIntrinsicID = Intrinsic::aarch64_stg; break;
  case clang::AArch64::BI__builtin_arm_subp:
    MTEIntrinsicID = Intrinsic::aarch64_subp; break;
  }

  if (MTEIntrinsicID != Intrinsic::not_intrinsic) {
    llvm::Type *T = ConvertType(E->getType());

    if (MTEIntrinsicID == Intrinsic::aarch64_irg) {
      Value *Pointer = EmitScalarExpr(E->getArg(0));
      Value *Mask = EmitScalarExpr(E->getArg(1));

      Pointer = Builder.CreatePointerCast(Pointer, Int8PtrTy);
      Mask = Builder.CreateZExt(Mask, Int64Ty);
      Value *RV = Builder.CreateCall(
                       CGM.getIntrinsic(MTEIntrinsicID), {Pointer, Mask});
       return Builder.CreatePointerCast(RV, T);
    }
    if (MTEIntrinsicID == Intrinsic::aarch64_addg) {
      Value *Pointer = EmitScalarExpr(E->getArg(0));
      Value *TagOffset = EmitScalarExpr(E->getArg(1));

      Pointer = Builder.CreatePointerCast(Pointer, Int8PtrTy);
      TagOffset = Builder.CreateZExt(TagOffset, Int64Ty);
      Value *RV = Builder.CreateCall(
                       CGM.getIntrinsic(MTEIntrinsicID), {Pointer, TagOffset});
      return Builder.CreatePointerCast(RV, T);
    }
    if (MTEIntrinsicID == Intrinsic::aarch64_gmi) {
      Value *Pointer = EmitScalarExpr(E->getArg(0));
      Value *ExcludedMask = EmitScalarExpr(E->getArg(1));

      ExcludedMask = Builder.CreateZExt(ExcludedMask, Int64Ty);
      Pointer = Builder.CreatePointerCast(Pointer, Int8PtrTy);
      return Builder.CreateCall(
                       CGM.getIntrinsic(MTEIntrinsicID), {Pointer, ExcludedMask});
    }
    // Although it is possible to supply a different return
    // address (first arg) to this intrinsic, for now we set
    // return address same as input address.
    if (MTEIntrinsicID == Intrinsic::aarch64_ldg) {
      Value *TagAddress = EmitScalarExpr(E->getArg(0));
      TagAddress = Builder.CreatePointerCast(TagAddress, Int8PtrTy);
      Value *RV = Builder.CreateCall(
                    CGM.getIntrinsic(MTEIntrinsicID), {TagAddress, TagAddress});
      return Builder.CreatePointerCast(RV, T);
    }
    // Although it is possible to supply a different tag (to set)
    // to this intrinsic (as first arg), for now we supply
    // the tag that is in input address arg (common use case).
    if (MTEIntrinsicID == Intrinsic::aarch64_stg) {
        Value *TagAddress = EmitScalarExpr(E->getArg(0));
        TagAddress = Builder.CreatePointerCast(TagAddress, Int8PtrTy);
        return Builder.CreateCall(
                 CGM.getIntrinsic(MTEIntrinsicID), {TagAddress, TagAddress});
    }
    if (MTEIntrinsicID == Intrinsic::aarch64_subp) {
      Value *PointerA = EmitScalarExpr(E->getArg(0));
      Value *PointerB = EmitScalarExpr(E->getArg(1));
      PointerA = Builder.CreatePointerCast(PointerA, Int8PtrTy);
      PointerB = Builder.CreatePointerCast(PointerB, Int8PtrTy);
      return Builder.CreateCall(
                       CGM.getIntrinsic(MTEIntrinsicID), {PointerA, PointerB});
    }
  }

  if (BuiltinID == clang::AArch64::BI__builtin_arm_rsr ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rsr64 ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rsr128 ||
      BuiltinID == clang::AArch64::BI__builtin_arm_rsrp ||
      BuiltinID == clang::AArch64::BI__builtin_arm_wsr ||
      BuiltinID == clang::AArch64::BI__builtin_arm_wsr64 ||
      BuiltinID == clang::AArch64::BI__builtin_arm_wsr128 ||
      BuiltinID == clang::AArch64::BI__builtin_arm_wsrp) {

    SpecialRegisterAccessKind AccessKind = Write;
    if (BuiltinID == clang::AArch64::BI__builtin_arm_rsr ||
        BuiltinID == clang::AArch64::BI__builtin_arm_rsr64 ||
        BuiltinID == clang::AArch64::BI__builtin_arm_rsr128 ||
        BuiltinID == clang::AArch64::BI__builtin_arm_rsrp)
      AccessKind = VolatileRead;

    bool IsPointerBuiltin = BuiltinID == clang::AArch64::BI__builtin_arm_rsrp ||
                            BuiltinID == clang::AArch64::BI__builtin_arm_wsrp;

    bool Is32Bit = BuiltinID == clang::AArch64::BI__builtin_arm_rsr ||
                   BuiltinID == clang::AArch64::BI__builtin_arm_wsr;

    bool Is128Bit = BuiltinID == clang::AArch64::BI__builtin_arm_rsr128 ||
                    BuiltinID == clang::AArch64::BI__builtin_arm_wsr128;

    llvm::Type *ValueType;
    llvm::Type *RegisterType = Int64Ty;
    if (Is32Bit) {
      ValueType = Int32Ty;
    } else if (Is128Bit) {
      llvm::Type *Int128Ty =
          llvm::IntegerType::getInt128Ty(CGM.getLLVMContext());
      ValueType = Int128Ty;
      RegisterType = Int128Ty;
    } else if (IsPointerBuiltin) {
      ValueType = VoidPtrTy;
    } else {
      ValueType = Int64Ty;
    };

    return EmitSpecialRegisterBuiltin(*this, E, RegisterType, ValueType,
                                      AccessKind);
  }

  if (BuiltinID == clang::AArch64::BI_ReadStatusReg ||
      BuiltinID == clang::AArch64::BI_WriteStatusReg) {
    LLVMContext &Context = CGM.getLLVMContext();

    unsigned SysReg =
      E->getArg(0)->EvaluateKnownConstInt(getContext()).getZExtValue();

    std::string SysRegStr;
    llvm::raw_string_ostream(SysRegStr) <<
                       ((1 << 1) | ((SysReg >> 14) & 1))  << ":" <<
                       ((SysReg >> 11) & 7)               << ":" <<
                       ((SysReg >> 7)  & 15)              << ":" <<
                       ((SysReg >> 3)  & 15)              << ":" <<
                       ( SysReg        & 7);

    llvm::Metadata *Ops[] = { llvm::MDString::get(Context, SysRegStr) };
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);

    llvm::Type *RegisterType = Int64Ty;
    llvm::Type *Types[] = { RegisterType };

    if (BuiltinID == clang::AArch64::BI_ReadStatusReg) {
      llvm::Function *F = CGM.getIntrinsic(llvm::Intrinsic::read_register, Types);

      return Builder.CreateCall(F, Metadata);
    }

    llvm::Function *F = CGM.getIntrinsic(llvm::Intrinsic::write_register, Types);
    llvm::Value *ArgValue = EmitScalarExpr(E->getArg(1));

    return Builder.CreateCall(F, { Metadata, ArgValue });
  }

  if (BuiltinID == clang::AArch64::BI_AddressOfReturnAddress) {
    llvm::Function *F =
        CGM.getIntrinsic(Intrinsic::addressofreturnaddress, AllocaInt8PtrTy);
    return Builder.CreateCall(F);
  }

  if (BuiltinID == clang::AArch64::BI__builtin_sponentry) {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::sponentry, AllocaInt8PtrTy);
    return Builder.CreateCall(F);
  }

  if (BuiltinID == clang::AArch64::BI__mulh ||
      BuiltinID == clang::AArch64::BI__umulh) {
    llvm::Type *ResType = ConvertType(E->getType());
    llvm::Type *Int128Ty = llvm::IntegerType::get(getLLVMContext(), 128);

    bool IsSigned = BuiltinID == clang::AArch64::BI__mulh;
    Value *LHS =
        Builder.CreateIntCast(EmitScalarExpr(E->getArg(0)), Int128Ty, IsSigned);
    Value *RHS =
        Builder.CreateIntCast(EmitScalarExpr(E->getArg(1)), Int128Ty, IsSigned);

    Value *MulResult, *HigherBits;
    if (IsSigned) {
      MulResult = Builder.CreateNSWMul(LHS, RHS);
      HigherBits = Builder.CreateAShr(MulResult, 64);
    } else {
      MulResult = Builder.CreateNUWMul(LHS, RHS);
      HigherBits = Builder.CreateLShr(MulResult, 64);
    }
    HigherBits = Builder.CreateIntCast(HigherBits, ResType, IsSigned);

    return HigherBits;
  }

  if (BuiltinID == AArch64::BI__writex18byte ||
      BuiltinID == AArch64::BI__writex18word ||
      BuiltinID == AArch64::BI__writex18dword ||
      BuiltinID == AArch64::BI__writex18qword) {
    // Read x18 as i8*
    LLVMContext &Context = CGM.getLLVMContext();
    llvm::Metadata *Ops[] = {llvm::MDString::get(Context, "x18")};
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);
    llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::read_register, {Int64Ty});
    llvm::Value *X18 = Builder.CreateCall(F, Metadata);
    X18 = Builder.CreateIntToPtr(X18, Int8PtrTy);

    // Store val at x18 + offset
    Value *Offset = Builder.CreateZExt(EmitScalarExpr(E->getArg(0)), Int64Ty);
    Value *Ptr = Builder.CreateGEP(Int8Ty, X18, Offset);
    Value *Val = EmitScalarExpr(E->getArg(1));
    StoreInst *Store = Builder.CreateAlignedStore(Val, Ptr, CharUnits::One());
    return Store;
  }

  if (BuiltinID == AArch64::BI__readx18byte ||
      BuiltinID == AArch64::BI__readx18word ||
      BuiltinID == AArch64::BI__readx18dword ||
      BuiltinID == AArch64::BI__readx18qword) {
    llvm::Type *IntTy = ConvertType(E->getType());

    // Read x18 as i8*
    LLVMContext &Context = CGM.getLLVMContext();
    llvm::Metadata *Ops[] = {llvm::MDString::get(Context, "x18")};
    llvm::MDNode *RegName = llvm::MDNode::get(Context, Ops);
    llvm::Value *Metadata = llvm::MetadataAsValue::get(Context, RegName);
    llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::read_register, {Int64Ty});
    llvm::Value *X18 = Builder.CreateCall(F, Metadata);
    X18 = Builder.CreateIntToPtr(X18, Int8PtrTy);

    // Load x18 + offset
    Value *Offset = Builder.CreateZExt(EmitScalarExpr(E->getArg(0)), Int64Ty);
    Value *Ptr = Builder.CreateGEP(Int8Ty, X18, Offset);
    LoadInst *Load = Builder.CreateAlignedLoad(IntTy, Ptr, CharUnits::One());
    return Load;
  }

  if (BuiltinID == AArch64::BI_CopyDoubleFromInt64 ||
      BuiltinID == AArch64::BI_CopyFloatFromInt32 ||
      BuiltinID == AArch64::BI_CopyInt32FromFloat ||
      BuiltinID == AArch64::BI_CopyInt64FromDouble) {
    Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *RetTy = ConvertType(E->getType());
    return Builder.CreateBitCast(Arg, RetTy);
  }

  if (BuiltinID == AArch64::BI_CountLeadingOnes ||
      BuiltinID == AArch64::BI_CountLeadingOnes64 ||
      BuiltinID == AArch64::BI_CountLeadingZeros ||
      BuiltinID == AArch64::BI_CountLeadingZeros64) {
    Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *ArgType = Arg->getType();

    if (BuiltinID == AArch64::BI_CountLeadingOnes ||
        BuiltinID == AArch64::BI_CountLeadingOnes64)
      Arg = Builder.CreateXor(Arg, Constant::getAllOnesValue(ArgType));

    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ArgType);
    Value *Result = Builder.CreateCall(F, {Arg, Builder.getInt1(false)});

    if (BuiltinID == AArch64::BI_CountLeadingOnes64 ||
        BuiltinID == AArch64::BI_CountLeadingZeros64)
      Result = Builder.CreateTrunc(Result, Builder.getInt32Ty());
    return Result;
  }

  if (BuiltinID == AArch64::BI_CountLeadingSigns ||
      BuiltinID == AArch64::BI_CountLeadingSigns64) {
    Value *Arg = EmitScalarExpr(E->getArg(0));

    Function *F = (BuiltinID == AArch64::BI_CountLeadingSigns)
                      ? CGM.getIntrinsic(Intrinsic::aarch64_cls)
                      : CGM.getIntrinsic(Intrinsic::aarch64_cls64);

    Value *Result = Builder.CreateCall(F, Arg, "cls");
    if (BuiltinID == AArch64::BI_CountLeadingSigns64)
      Result = Builder.CreateTrunc(Result, Builder.getInt32Ty());
    return Result;
  }

  if (BuiltinID == AArch64::BI_CountOneBits ||
      BuiltinID == AArch64::BI_CountOneBits64) {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ArgType);

    Value *Result = Builder.CreateCall(F, ArgValue);
    if (BuiltinID == AArch64::BI_CountOneBits64)
      Result = Builder.CreateTrunc(Result, Builder.getInt32Ty());
    return Result;
  }

  if (BuiltinID == AArch64::BI__prefetch) {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *RW = llvm::ConstantInt::get(Int32Ty, 0);
    Value *Locality = ConstantInt::get(Int32Ty, 3);
    Value *Data = llvm::ConstantInt::get(Int32Ty, 1);
    Function *F = CGM.getIntrinsic(Intrinsic::prefetch, Address->getType());
    return Builder.CreateCall(F, {Address, RW, Locality, Data});
  }

  if (BuiltinID == AArch64::BI__hlt) {
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_hlt);
    Builder.CreateCall(F, {EmitScalarExpr(E->getArg(0))});

    // Return 0 for convenience, even though MSVC returns some other undefined
    // value.
    return ConstantInt::get(Builder.getInt32Ty(), 0);
  }

  // Handle MSVC intrinsics before argument evaluation to prevent double
  // evaluation.
  if (std::optional<MSVCIntrin> MsvcIntId =
          translateAarch64ToMsvcIntrin(BuiltinID))
    return EmitMSVCBuiltinExpr(*MsvcIntId, E);

  // Some intrinsics are equivalent - if they are use the base intrinsic ID.
  auto It = llvm::find_if(NEONEquivalentIntrinsicMap, [BuiltinID](auto &P) {
    return P.first == BuiltinID;
  });
  if (It != end(NEONEquivalentIntrinsicMap))
    BuiltinID = It->second;

  // Find out if any arguments are required to be integer constant
  // expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  llvm::SmallVector<Value*, 4> Ops;
  Address PtrOp0 = Address::invalid();
  for (unsigned i = 0, e = E->getNumArgs() - 1; i != e; i++) {
    if (i == 0) {
      switch (BuiltinID) {
      case NEON::BI__builtin_neon_vld1_v:
      case NEON::BI__builtin_neon_vld1q_v:
      case NEON::BI__builtin_neon_vld1_dup_v:
      case NEON::BI__builtin_neon_vld1q_dup_v:
      case NEON::BI__builtin_neon_vld1_lane_v:
      case NEON::BI__builtin_neon_vld1q_lane_v:
      case NEON::BI__builtin_neon_vst1_v:
      case NEON::BI__builtin_neon_vst1q_v:
      case NEON::BI__builtin_neon_vst1_lane_v:
      case NEON::BI__builtin_neon_vst1q_lane_v:
      case NEON::BI__builtin_neon_vldap1_lane_s64:
      case NEON::BI__builtin_neon_vldap1q_lane_s64:
      case NEON::BI__builtin_neon_vstl1_lane_s64:
      case NEON::BI__builtin_neon_vstl1q_lane_s64:
        // Get the alignment for the argument in addition to the value;
        // we'll use it later.
        PtrOp0 = EmitPointerWithAlignment(E->getArg(0));
        Ops.push_back(PtrOp0.emitRawPointer(*this));
        continue;
      }
    }
    Ops.push_back(EmitScalarOrConstFoldImmArg(ICEArguments, i, E));
  }

  auto SISDMap = ArrayRef(AArch64SISDIntrinsicMap);
  const ARMVectorIntrinsicInfo *Builtin = findARMVectorIntrinsicInMap(
      SISDMap, BuiltinID, AArch64SISDIntrinsicsProvenSorted);

  if (Builtin) {
    Ops.push_back(EmitScalarExpr(E->getArg(E->getNumArgs() - 1)));
    Value *Result = EmitCommonNeonSISDBuiltinExpr(*this, *Builtin, Ops, E);
    assert(Result && "SISD intrinsic should have been handled");
    return Result;
  }

  const Expr *Arg = E->getArg(E->getNumArgs()-1);
  NeonTypeFlags Type(0);
  if (std::optional<llvm::APSInt> Result =
          Arg->getIntegerConstantExpr(getContext()))
    // Determine the type of this overloaded NEON intrinsic.
    Type = NeonTypeFlags(Result->getZExtValue());

  bool usgn = Type.isUnsigned();
  bool quad = Type.isQuad();

  // Handle non-overloaded intrinsics first.
  switch (BuiltinID) {
  default: break;
  case NEON::BI__builtin_neon_vabsh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::fabs, HalfTy), Ops, "vabs");
  case NEON::BI__builtin_neon_vaddq_p128: {
    llvm::Type *Ty = GetNeonType(this, NeonTypeFlags::Poly128);
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[0] =  Builder.CreateXor(Ops[0], Ops[1]);
    llvm::Type *Int128Ty = llvm::Type::getIntNTy(getLLVMContext(), 128);
    return Builder.CreateBitCast(Ops[0], Int128Ty);
  }
  case NEON::BI__builtin_neon_vldrq_p128: {
    llvm::Type *Int128Ty = llvm::Type::getIntNTy(getLLVMContext(), 128);
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    return Builder.CreateAlignedLoad(Int128Ty, Ptr,
                                     CharUnits::fromQuantity(16));
  }
  case NEON::BI__builtin_neon_vstrq_p128: {
    Value *Ptr = Ops[0];
    return Builder.CreateDefaultAlignedStore(EmitScalarExpr(E->getArg(1)), Ptr);
  }
  case NEON::BI__builtin_neon_vcvts_f32_u32:
  case NEON::BI__builtin_neon_vcvtd_f64_u64:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcvts_f32_s32:
  case NEON::BI__builtin_neon_vcvtd_f64_s64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    bool Is64 = Ops[0]->getType()->getPrimitiveSizeInBits() == 64;
    llvm::Type *InTy = Is64 ? Int64Ty : Int32Ty;
    llvm::Type *FTy = Is64 ? DoubleTy : FloatTy;
    Ops[0] = Builder.CreateBitCast(Ops[0], InTy);
    if (usgn)
      return Builder.CreateUIToFP(Ops[0], FTy);
    return Builder.CreateSIToFP(Ops[0], FTy);
  }
  case NEON::BI__builtin_neon_vcvth_f16_u16:
  case NEON::BI__builtin_neon_vcvth_f16_u32:
  case NEON::BI__builtin_neon_vcvth_f16_u64:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vcvth_f16_s16:
  case NEON::BI__builtin_neon_vcvth_f16_s32:
  case NEON::BI__builtin_neon_vcvth_f16_s64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    llvm::Type *FTy = HalfTy;
    llvm::Type *InTy;
    if (Ops[0]->getType()->getPrimitiveSizeInBits() == 64)
      InTy = Int64Ty;
    else if (Ops[0]->getType()->getPrimitiveSizeInBits() == 32)
      InTy = Int32Ty;
    else
      InTy = Int16Ty;
    Ops[0] = Builder.CreateBitCast(Ops[0], InTy);
    if (usgn)
      return Builder.CreateUIToFP(Ops[0], FTy);
    return Builder.CreateSIToFP(Ops[0], FTy);
  }
  case NEON::BI__builtin_neon_vcvtah_u16_f16:
  case NEON::BI__builtin_neon_vcvtmh_u16_f16:
  case NEON::BI__builtin_neon_vcvtnh_u16_f16:
  case NEON::BI__builtin_neon_vcvtph_u16_f16:
  case NEON::BI__builtin_neon_vcvth_u16_f16:
  case NEON::BI__builtin_neon_vcvtah_s16_f16:
  case NEON::BI__builtin_neon_vcvtmh_s16_f16:
  case NEON::BI__builtin_neon_vcvtnh_s16_f16:
  case NEON::BI__builtin_neon_vcvtph_s16_f16:
  case NEON::BI__builtin_neon_vcvth_s16_f16: {
    unsigned Int;
    llvm::Type* InTy = Int32Ty;
    llvm::Type* FTy  = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvtah_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtau; break;
    case NEON::BI__builtin_neon_vcvtmh_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtmu; break;
    case NEON::BI__builtin_neon_vcvtnh_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtnu; break;
    case NEON::BI__builtin_neon_vcvtph_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtpu; break;
    case NEON::BI__builtin_neon_vcvth_u16_f16:
      Int = Intrinsic::aarch64_neon_fcvtzu; break;
    case NEON::BI__builtin_neon_vcvtah_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtas; break;
    case NEON::BI__builtin_neon_vcvtmh_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtms; break;
    case NEON::BI__builtin_neon_vcvtnh_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtns; break;
    case NEON::BI__builtin_neon_vcvtph_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtps; break;
    case NEON::BI__builtin_neon_vcvth_s16_f16:
      Int = Intrinsic::aarch64_neon_fcvtzs; break;
    }
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "fcvt");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcaleh_f16:
  case NEON::BI__builtin_neon_vcalth_f16:
  case NEON::BI__builtin_neon_vcageh_f16:
  case NEON::BI__builtin_neon_vcagth_f16: {
    unsigned Int;
    llvm::Type* InTy = Int32Ty;
    llvm::Type* FTy  = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcageh_f16:
      Int = Intrinsic::aarch64_neon_facge; break;
    case NEON::BI__builtin_neon_vcagth_f16:
      Int = Intrinsic::aarch64_neon_facgt; break;
    case NEON::BI__builtin_neon_vcaleh_f16:
      Int = Intrinsic::aarch64_neon_facge; std::swap(Ops[0], Ops[1]); break;
    case NEON::BI__builtin_neon_vcalth_f16:
      Int = Intrinsic::aarch64_neon_facgt; std::swap(Ops[0], Ops[1]); break;
    }
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "facg");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcvth_n_s16_f16:
  case NEON::BI__builtin_neon_vcvth_n_u16_f16: {
    unsigned Int;
    llvm::Type* InTy = Int32Ty;
    llvm::Type* FTy  = HalfTy;
    llvm::Type *Tys[2] = {InTy, FTy};
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvth_n_s16_f16:
      Int = Intrinsic::aarch64_neon_vcvtfp2fxs; break;
    case NEON::BI__builtin_neon_vcvth_n_u16_f16:
      Int = Intrinsic::aarch64_neon_vcvtfp2fxu; break;
    }
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "fcvth_n");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vcvth_n_f16_s16:
  case NEON::BI__builtin_neon_vcvth_n_f16_u16: {
    unsigned Int;
    llvm::Type* FTy  = HalfTy;
    llvm::Type* InTy = Int32Ty;
    llvm::Type *Tys[2] = {FTy, InTy};
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vcvth_n_f16_s16:
      Int = Intrinsic::aarch64_neon_vcvtfxs2fp;
      Ops[0] = Builder.CreateSExt(Ops[0], InTy, "sext");
      break;
    case NEON::BI__builtin_neon_vcvth_n_f16_u16:
      Int = Intrinsic::aarch64_neon_vcvtfxu2fp;
      Ops[0] = Builder.CreateZExt(Ops[0], InTy);
      break;
    }
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "fcvth_n");
  }
  case NEON::BI__builtin_neon_vpaddd_s64: {
    auto *Ty = llvm::FixedVectorType::get(Int64Ty, 2);
    Value *Vec = EmitScalarExpr(E->getArg(0));
    // The vector is v2f64, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2i64");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f64 into a scalar f64.
    return Builder.CreateAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vpaddd_f64: {
    auto *Ty = llvm::FixedVectorType::get(DoubleTy, 2);
    Value *Vec = EmitScalarExpr(E->getArg(0));
    // The vector is v2f64, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2f64");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f64 into a scalar f64.
    return Builder.CreateFAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vpadds_f32: {
    auto *Ty = llvm::FixedVectorType::get(FloatTy, 2);
    Value *Vec = EmitScalarExpr(E->getArg(0));
    // The vector is v2f32, so make sure it's bitcast to that.
    Vec = Builder.CreateBitCast(Vec, Ty, "v2f32");
    llvm::Value *Idx0 = llvm::ConstantInt::get(SizeTy, 0);
    llvm::Value *Idx1 = llvm::ConstantInt::get(SizeTy, 1);
    Value *Op0 = Builder.CreateExtractElement(Vec, Idx0, "lane0");
    Value *Op1 = Builder.CreateExtractElement(Vec, Idx1, "lane1");
    // Pairwise addition of a v2f32 into a scalar f32.
    return Builder.CreateFAdd(Op0, Op1, "vpaddd");
  }
  case NEON::BI__builtin_neon_vceqzd_s64:
  case NEON::BI__builtin_neon_vceqzd_f64:
  case NEON::BI__builtin_neon_vceqzs_f32:
  case NEON::BI__builtin_neon_vceqzh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OEQ, ICmpInst::ICMP_EQ, "vceqz");
  case NEON::BI__builtin_neon_vcgezd_s64:
  case NEON::BI__builtin_neon_vcgezd_f64:
  case NEON::BI__builtin_neon_vcgezs_f32:
  case NEON::BI__builtin_neon_vcgezh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OGE, ICmpInst::ICMP_SGE, "vcgez");
  case NEON::BI__builtin_neon_vclezd_s64:
  case NEON::BI__builtin_neon_vclezd_f64:
  case NEON::BI__builtin_neon_vclezs_f32:
  case NEON::BI__builtin_neon_vclezh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OLE, ICmpInst::ICMP_SLE, "vclez");
  case NEON::BI__builtin_neon_vcgtzd_s64:
  case NEON::BI__builtin_neon_vcgtzd_f64:
  case NEON::BI__builtin_neon_vcgtzs_f32:
  case NEON::BI__builtin_neon_vcgtzh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OGT, ICmpInst::ICMP_SGT, "vcgtz");
  case NEON::BI__builtin_neon_vcltzd_s64:
  case NEON::BI__builtin_neon_vcltzd_f64:
  case NEON::BI__builtin_neon_vcltzs_f32:
  case NEON::BI__builtin_neon_vcltzh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitAArch64CompareBuiltinExpr(
        Ops[0], ConvertType(E->getCallReturnType(getContext())),
        ICmpInst::FCMP_OLT, ICmpInst::ICMP_SLT, "vcltz");

  case NEON::BI__builtin_neon_vceqzd_u64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[0] =
        Builder.CreateICmpEQ(Ops[0], llvm::Constant::getNullValue(Int64Ty));
    return Builder.CreateSExt(Ops[0], Int64Ty, "vceqzd");
  }
  case NEON::BI__builtin_neon_vceqd_f64:
  case NEON::BI__builtin_neon_vcled_f64:
  case NEON::BI__builtin_neon_vcltd_f64:
  case NEON::BI__builtin_neon_vcged_f64:
  case NEON::BI__builtin_neon_vcgtd_f64: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqd_f64: P = llvm::FCmpInst::FCMP_OEQ; break;
    case NEON::BI__builtin_neon_vcled_f64: P = llvm::FCmpInst::FCMP_OLE; break;
    case NEON::BI__builtin_neon_vcltd_f64: P = llvm::FCmpInst::FCMP_OLT; break;
    case NEON::BI__builtin_neon_vcged_f64: P = llvm::FCmpInst::FCMP_OGE; break;
    case NEON::BI__builtin_neon_vcgtd_f64: P = llvm::FCmpInst::FCMP_OGT; break;
    }
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], DoubleTy);
    if (P == llvm::FCmpInst::FCMP_OEQ)
      Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateFCmpS(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int64Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqs_f32:
  case NEON::BI__builtin_neon_vcles_f32:
  case NEON::BI__builtin_neon_vclts_f32:
  case NEON::BI__builtin_neon_vcges_f32:
  case NEON::BI__builtin_neon_vcgts_f32: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqs_f32: P = llvm::FCmpInst::FCMP_OEQ; break;
    case NEON::BI__builtin_neon_vcles_f32: P = llvm::FCmpInst::FCMP_OLE; break;
    case NEON::BI__builtin_neon_vclts_f32: P = llvm::FCmpInst::FCMP_OLT; break;
    case NEON::BI__builtin_neon_vcges_f32: P = llvm::FCmpInst::FCMP_OGE; break;
    case NEON::BI__builtin_neon_vcgts_f32: P = llvm::FCmpInst::FCMP_OGT; break;
    }
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], FloatTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], FloatTy);
    if (P == llvm::FCmpInst::FCMP_OEQ)
      Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateFCmpS(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int32Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqh_f16:
  case NEON::BI__builtin_neon_vcleh_f16:
  case NEON::BI__builtin_neon_vclth_f16:
  case NEON::BI__builtin_neon_vcgeh_f16:
  case NEON::BI__builtin_neon_vcgth_f16: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqh_f16: P = llvm::FCmpInst::FCMP_OEQ; break;
    case NEON::BI__builtin_neon_vcleh_f16: P = llvm::FCmpInst::FCMP_OLE; break;
    case NEON::BI__builtin_neon_vclth_f16: P = llvm::FCmpInst::FCMP_OLT; break;
    case NEON::BI__builtin_neon_vcgeh_f16: P = llvm::FCmpInst::FCMP_OGE; break;
    case NEON::BI__builtin_neon_vcgth_f16: P = llvm::FCmpInst::FCMP_OGT; break;
    }
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], HalfTy);
    Ops[1] = Builder.CreateBitCast(Ops[1], HalfTy);
    if (P == llvm::FCmpInst::FCMP_OEQ)
      Ops[0] = Builder.CreateFCmp(P, Ops[0], Ops[1]);
    else
      Ops[0] = Builder.CreateFCmpS(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int16Ty, "vcmpd");
  }
  case NEON::BI__builtin_neon_vceqd_s64:
  case NEON::BI__builtin_neon_vceqd_u64:
  case NEON::BI__builtin_neon_vcgtd_s64:
  case NEON::BI__builtin_neon_vcgtd_u64:
  case NEON::BI__builtin_neon_vcltd_s64:
  case NEON::BI__builtin_neon_vcltd_u64:
  case NEON::BI__builtin_neon_vcged_u64:
  case NEON::BI__builtin_neon_vcged_s64:
  case NEON::BI__builtin_neon_vcled_u64:
  case NEON::BI__builtin_neon_vcled_s64: {
    llvm::CmpInst::Predicate P;
    switch (BuiltinID) {
    default: llvm_unreachable("missing builtin ID in switch!");
    case NEON::BI__builtin_neon_vceqd_s64:
    case NEON::BI__builtin_neon_vceqd_u64:P = llvm::ICmpInst::ICMP_EQ;break;
    case NEON::BI__builtin_neon_vcgtd_s64:P = llvm::ICmpInst::ICMP_SGT;break;
    case NEON::BI__builtin_neon_vcgtd_u64:P = llvm::ICmpInst::ICMP_UGT;break;
    case NEON::BI__builtin_neon_vcltd_s64:P = llvm::ICmpInst::ICMP_SLT;break;
    case NEON::BI__builtin_neon_vcltd_u64:P = llvm::ICmpInst::ICMP_ULT;break;
    case NEON::BI__builtin_neon_vcged_u64:P = llvm::ICmpInst::ICMP_UGE;break;
    case NEON::BI__builtin_neon_vcged_s64:P = llvm::ICmpInst::ICMP_SGE;break;
    case NEON::BI__builtin_neon_vcled_u64:P = llvm::ICmpInst::ICMP_ULE;break;
    case NEON::BI__builtin_neon_vcled_s64:P = llvm::ICmpInst::ICMP_SLE;break;
    }
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops[0] = Builder.CreateICmp(P, Ops[0], Ops[1]);
    return Builder.CreateSExt(Ops[0], Int64Ty, "vceqd");
  }
  case NEON::BI__builtin_neon_vtstd_s64:
  case NEON::BI__builtin_neon_vtstd_u64: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[0] = Builder.CreateBitCast(Ops[0], Int64Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops[0] = Builder.CreateAnd(Ops[0], Ops[1]);
    Ops[0] = Builder.CreateICmp(ICmpInst::ICMP_NE, Ops[0],
                                llvm::Constant::getNullValue(Int64Ty));
    return Builder.CreateSExt(Ops[0], Int64Ty, "vtstd");
  }
  case NEON::BI__builtin_neon_vset_lane_i8:
  case NEON::BI__builtin_neon_vset_lane_i16:
  case NEON::BI__builtin_neon_vset_lane_i32:
  case NEON::BI__builtin_neon_vset_lane_i64:
  case NEON::BI__builtin_neon_vset_lane_bf16:
  case NEON::BI__builtin_neon_vset_lane_f32:
  case NEON::BI__builtin_neon_vsetq_lane_i8:
  case NEON::BI__builtin_neon_vsetq_lane_i16:
  case NEON::BI__builtin_neon_vsetq_lane_i32:
  case NEON::BI__builtin_neon_vsetq_lane_i64:
  case NEON::BI__builtin_neon_vsetq_lane_bf16:
  case NEON::BI__builtin_neon_vsetq_lane_f32:
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");
  case NEON::BI__builtin_neon_vset_lane_f64:
    // The vector type needs a cast for the v1f64 variant.
    Ops[1] =
        Builder.CreateBitCast(Ops[1], llvm::FixedVectorType::get(DoubleTy, 1));
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");
  case NEON::BI__builtin_neon_vsetq_lane_f64:
    // The vector type needs a cast for the v2f64 variant.
    Ops[1] =
        Builder.CreateBitCast(Ops[1], llvm::FixedVectorType::get(DoubleTy, 2));
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vset_lane");

  case NEON::BI__builtin_neon_vget_lane_i8:
  case NEON::BI__builtin_neon_vdupb_lane_i8:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int8Ty, 8));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i8:
  case NEON::BI__builtin_neon_vdupb_laneq_i8:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int8Ty, 16));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i16:
  case NEON::BI__builtin_neon_vduph_lane_i16:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int16Ty, 4));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i16:
  case NEON::BI__builtin_neon_vduph_laneq_i16:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int16Ty, 8));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i32:
  case NEON::BI__builtin_neon_vdups_lane_i32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int32Ty, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vdups_lane_f32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(FloatTy, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vdups_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i32:
  case NEON::BI__builtin_neon_vdups_laneq_i32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int32Ty, 4));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_i64:
  case NEON::BI__builtin_neon_vdupd_lane_i64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int64Ty, 1));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vdupd_lane_f64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(DoubleTy, 1));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vdupd_lane");
  case NEON::BI__builtin_neon_vgetq_lane_i64:
  case NEON::BI__builtin_neon_vdupd_laneq_i64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(Int64Ty, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vget_lane_f32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(FloatTy, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vget_lane_f64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(DoubleTy, 1));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  case NEON::BI__builtin_neon_vgetq_lane_f32:
  case NEON::BI__builtin_neon_vdups_laneq_f32:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(FloatTy, 4));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vgetq_lane_f64:
  case NEON::BI__builtin_neon_vdupd_laneq_f64:
    Ops[0] =
        Builder.CreateBitCast(Ops[0], llvm::FixedVectorType::get(DoubleTy, 2));
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  case NEON::BI__builtin_neon_vaddh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateFAdd(Ops[0], Ops[1], "vaddh");
  case NEON::BI__builtin_neon_vsubh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateFSub(Ops[0], Ops[1], "vsubh");
  case NEON::BI__builtin_neon_vmulh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateFMul(Ops[0], Ops[1], "vmulh");
  case NEON::BI__builtin_neon_vdivh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateFDiv(Ops[0], Ops[1], "vdivh");
  case NEON::BI__builtin_neon_vfmah_f16:
    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, HalfTy,
        {EmitScalarExpr(E->getArg(1)), EmitScalarExpr(E->getArg(2)), Ops[0]});
  case NEON::BI__builtin_neon_vfmsh_f16: {
    Value* Neg = Builder.CreateFNeg(EmitScalarExpr(E->getArg(1)), "vsubh");

    // NEON intrinsic puts accumulator first, unlike the LLVM fma.
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, HalfTy,
        {Neg, EmitScalarExpr(E->getArg(2)), Ops[0]});
  }
  case NEON::BI__builtin_neon_vaddd_s64:
  case NEON::BI__builtin_neon_vaddd_u64:
    return Builder.CreateAdd(Ops[0], EmitScalarExpr(E->getArg(1)), "vaddd");
  case NEON::BI__builtin_neon_vsubd_s64:
  case NEON::BI__builtin_neon_vsubd_u64:
    return Builder.CreateSub(Ops[0], EmitScalarExpr(E->getArg(1)), "vsubd");
  case NEON::BI__builtin_neon_vqdmlalh_s16:
  case NEON::BI__builtin_neon_vqdmlslh_s16: {
    SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(vectorWrapScalar16(Ops[1]));
    ProductOps.push_back(vectorWrapScalar16(EmitScalarExpr(E->getArg(2))));
    auto *VTy = llvm::FixedVectorType::get(Int32Ty, 4);
    Ops[1] = EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqdmull, VTy),
                          ProductOps, "vqdmlXl");
    Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[1] = Builder.CreateExtractElement(Ops[1], CI, "lane0");

    unsigned AccumInt = BuiltinID == NEON::BI__builtin_neon_vqdmlalh_s16
                                        ? Intrinsic::aarch64_neon_sqadd
                                        : Intrinsic::aarch64_neon_sqsub;
    return EmitNeonCall(CGM.getIntrinsic(AccumInt, Int32Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqshlud_n_s64: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[1] = Builder.CreateZExt(Ops[1], Int64Ty);
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqshlu, Int64Ty),
                        Ops, "vqshlu_n");
  }
  case NEON::BI__builtin_neon_vqshld_n_u64:
  case NEON::BI__builtin_neon_vqshld_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vqshld_n_u64
                                   ? Intrinsic::aarch64_neon_uqshl
                                   : Intrinsic::aarch64_neon_sqshl;
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops[1] = Builder.CreateZExt(Ops[1], Int64Ty);
    return EmitNeonCall(CGM.getIntrinsic(Int, Int64Ty), Ops, "vqshl_n");
  }
  case NEON::BI__builtin_neon_vrshrd_n_u64:
  case NEON::BI__builtin_neon_vrshrd_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vrshrd_n_u64
                                   ? Intrinsic::aarch64_neon_urshl
                                   : Intrinsic::aarch64_neon_srshl;
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    int SV = cast<ConstantInt>(Ops[1])->getSExtValue();
    Ops[1] = ConstantInt::get(Int64Ty, -SV);
    return EmitNeonCall(CGM.getIntrinsic(Int, Int64Ty), Ops, "vrshr_n");
  }
  case NEON::BI__builtin_neon_vrsrad_n_u64:
  case NEON::BI__builtin_neon_vrsrad_n_s64: {
    unsigned Int = BuiltinID == NEON::BI__builtin_neon_vrsrad_n_u64
                                   ? Intrinsic::aarch64_neon_urshl
                                   : Intrinsic::aarch64_neon_srshl;
    Ops[1] = Builder.CreateBitCast(Ops[1], Int64Ty);
    Ops.push_back(Builder.CreateNeg(EmitScalarExpr(E->getArg(2))));
    Ops[1] = Builder.CreateCall(CGM.getIntrinsic(Int, Int64Ty),
                                {Ops[1], Builder.CreateSExt(Ops[2], Int64Ty)});
    return Builder.CreateAdd(Ops[0], Builder.CreateBitCast(Ops[1], Int64Ty));
  }
  case NEON::BI__builtin_neon_vshld_n_s64:
  case NEON::BI__builtin_neon_vshld_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateShl(
        Ops[0], ConstantInt::get(Int64Ty, Amt->getZExtValue()), "shld_n");
  }
  case NEON::BI__builtin_neon_vshrd_n_s64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    return Builder.CreateAShr(
        Ops[0], ConstantInt::get(Int64Ty, std::min(static_cast<uint64_t>(63),
                                                   Amt->getZExtValue())),
        "shrd_n");
  }
  case NEON::BI__builtin_neon_vshrd_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    uint64_t ShiftAmt = Amt->getZExtValue();
    // Right-shifting an unsigned value by its size yields 0.
    if (ShiftAmt == 64)
      return ConstantInt::get(Int64Ty, 0);
    return Builder.CreateLShr(Ops[0], ConstantInt::get(Int64Ty, ShiftAmt),
                              "shrd_n");
  }
  case NEON::BI__builtin_neon_vsrad_n_s64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(2)));
    Ops[1] = Builder.CreateAShr(
        Ops[1], ConstantInt::get(Int64Ty, std::min(static_cast<uint64_t>(63),
                                                   Amt->getZExtValue())),
        "shrd_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vsrad_n_u64: {
    llvm::ConstantInt *Amt = cast<ConstantInt>(EmitScalarExpr(E->getArg(2)));
    uint64_t ShiftAmt = Amt->getZExtValue();
    // Right-shifting an unsigned value by its size yields 0.
    // As Op + 0 = Op, return Ops[0] directly.
    if (ShiftAmt == 64)
      return Ops[0];
    Ops[1] = Builder.CreateLShr(Ops[1], ConstantInt::get(Int64Ty, ShiftAmt),
                                "shrd_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  }
  case NEON::BI__builtin_neon_vqdmlalh_lane_s16:
  case NEON::BI__builtin_neon_vqdmlalh_laneq_s16:
  case NEON::BI__builtin_neon_vqdmlslh_lane_s16:
  case NEON::BI__builtin_neon_vqdmlslh_laneq_s16: {
    Ops[2] = Builder.CreateExtractElement(Ops[2], EmitScalarExpr(E->getArg(3)),
                                          "lane");
    SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(vectorWrapScalar16(Ops[1]));
    ProductOps.push_back(vectorWrapScalar16(Ops[2]));
    auto *VTy = llvm::FixedVectorType::get(Int32Ty, 4);
    Ops[1] = EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqdmull, VTy),
                          ProductOps, "vqdmlXl");
    Constant *CI = ConstantInt::get(SizeTy, 0);
    Ops[1] = Builder.CreateExtractElement(Ops[1], CI, "lane0");
    Ops.pop_back();

    unsigned AccInt = (BuiltinID == NEON::BI__builtin_neon_vqdmlalh_lane_s16 ||
                       BuiltinID == NEON::BI__builtin_neon_vqdmlalh_laneq_s16)
                          ? Intrinsic::aarch64_neon_sqadd
                          : Intrinsic::aarch64_neon_sqsub;
    return EmitNeonCall(CGM.getIntrinsic(AccInt, Int32Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqdmlals_s32:
  case NEON::BI__builtin_neon_vqdmlsls_s32: {
    SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(Ops[1]);
    ProductOps.push_back(EmitScalarExpr(E->getArg(2)));
    Ops[1] =
        EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqdmulls_scalar),
                     ProductOps, "vqdmlXl");

    unsigned AccumInt = BuiltinID == NEON::BI__builtin_neon_vqdmlals_s32
                                        ? Intrinsic::aarch64_neon_sqadd
                                        : Intrinsic::aarch64_neon_sqsub;
    return EmitNeonCall(CGM.getIntrinsic(AccumInt, Int64Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vqdmlals_lane_s32:
  case NEON::BI__builtin_neon_vqdmlals_laneq_s32:
  case NEON::BI__builtin_neon_vqdmlsls_lane_s32:
  case NEON::BI__builtin_neon_vqdmlsls_laneq_s32: {
    Ops[2] = Builder.CreateExtractElement(Ops[2], EmitScalarExpr(E->getArg(3)),
                                          "lane");
    SmallVector<Value *, 2> ProductOps;
    ProductOps.push_back(Ops[1]);
    ProductOps.push_back(Ops[2]);
    Ops[1] =
        EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_sqdmulls_scalar),
                     ProductOps, "vqdmlXl");
    Ops.pop_back();

    unsigned AccInt = (BuiltinID == NEON::BI__builtin_neon_vqdmlals_lane_s32 ||
                       BuiltinID == NEON::BI__builtin_neon_vqdmlals_laneq_s32)
                          ? Intrinsic::aarch64_neon_sqadd
                          : Intrinsic::aarch64_neon_sqsub;
    return EmitNeonCall(CGM.getIntrinsic(AccInt, Int64Ty), Ops, "vqdmlXl");
  }
  case NEON::BI__builtin_neon_vget_lane_bf16:
  case NEON::BI__builtin_neon_vduph_lane_bf16:
  case NEON::BI__builtin_neon_vduph_lane_f16: {
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vget_lane");
  }
  case NEON::BI__builtin_neon_vgetq_lane_bf16:
  case NEON::BI__builtin_neon_vduph_laneq_bf16:
  case NEON::BI__builtin_neon_vduph_laneq_f16: {
    return Builder.CreateExtractElement(Ops[0], EmitScalarExpr(E->getArg(1)),
                                        "vgetq_lane");
  }

  case clang::AArch64::BI_InterlockedAdd:
  case clang::AArch64::BI_InterlockedAdd64: {
    Address DestAddr = CheckAtomicAlignment(*this, E);
    Value *Val = EmitScalarExpr(E->getArg(1));
    AtomicRMWInst *RMWI =
        Builder.CreateAtomicRMW(AtomicRMWInst::Add, DestAddr, Val,
                                llvm::AtomicOrdering::SequentiallyConsistent);
    return Builder.CreateAdd(RMWI, Val);
  }
  }

  llvm::FixedVectorType *VTy = GetNeonType(this, Type);
  llvm::Type *Ty = VTy;
  if (!Ty)
    return nullptr;

  // Not all intrinsics handled by the common case work for AArch64 yet, so only
  // defer to common code if it's been added to our special map.
  Builtin = findARMVectorIntrinsicInMap(AArch64SIMDIntrinsicMap, BuiltinID,
                                        AArch64SIMDIntrinsicsProvenSorted);

  if (Builtin)
    return EmitCommonNeonBuiltinExpr(
        Builtin->BuiltinID, Builtin->LLVMIntrinsic, Builtin->AltLLVMIntrinsic,
        Builtin->NameHint, Builtin->TypeModifier, E, Ops,
        /*never use addresses*/ Address::invalid(), Address::invalid(), Arch);

  if (Value *V = EmitAArch64TblBuiltinExpr(*this, BuiltinID, E, Ops, Arch))
    return V;

  unsigned Int;
  switch (BuiltinID) {
  default: return nullptr;
  case NEON::BI__builtin_neon_vbsl_v:
  case NEON::BI__builtin_neon_vbslq_v: {
    llvm::Type *BitTy = llvm::VectorType::getInteger(VTy);
    Ops[0] = Builder.CreateBitCast(Ops[0], BitTy, "vbsl");
    Ops[1] = Builder.CreateBitCast(Ops[1], BitTy, "vbsl");
    Ops[2] = Builder.CreateBitCast(Ops[2], BitTy, "vbsl");

    Ops[1] = Builder.CreateAnd(Ops[0], Ops[1], "vbsl");
    Ops[2] = Builder.CreateAnd(Builder.CreateNot(Ops[0]), Ops[2], "vbsl");
    Ops[0] = Builder.CreateOr(Ops[1], Ops[2], "vbsl");
    return Builder.CreateBitCast(Ops[0], Ty);
  }
  case NEON::BI__builtin_neon_vfma_lane_v:
  case NEON::BI__builtin_neon_vfmaq_lane_v: { // Only used for FP types
    // The ARM builtins (and instructions) have the addend as the first
    // operand, but the 'fma' intrinsics have it last. Swap it around here.
    Value *Addend = Ops[0];
    Value *Multiplicand = Ops[1];
    Value *LaneSource = Ops[2];
    Ops[0] = Multiplicand;
    Ops[1] = LaneSource;
    Ops[2] = Addend;

    // Now adjust things to handle the lane access.
    auto *SourceTy = BuiltinID == NEON::BI__builtin_neon_vfmaq_lane_v
                         ? llvm::FixedVectorType::get(VTy->getElementType(),
                                                      VTy->getNumElements() / 2)
                         : VTy;
    llvm::Constant *cst = cast<Constant>(Ops[3]);
    Value *SV = llvm::ConstantVector::getSplat(VTy->getElementCount(), cst);
    Ops[1] = Builder.CreateBitCast(Ops[1], SourceTy);
    Ops[1] = Builder.CreateShuffleVector(Ops[1], Ops[1], SV, "lane");

    Ops.pop_back();
    Int = Builder.getIsFPConstrained() ? Intrinsic::experimental_constrained_fma
                                       : Intrinsic::fma;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "fmla");
  }
  case NEON::BI__builtin_neon_vfma_laneq_v: {
    auto *VTy = cast<llvm::FixedVectorType>(Ty);
    // v1f64 fma should be mapped to Neon scalar f64 fma
    if (VTy && VTy->getElementType() == DoubleTy) {
      Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
      Ops[1] = Builder.CreateBitCast(Ops[1], DoubleTy);
      llvm::FixedVectorType *VTy =
          GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float64, false, true));
      Ops[2] = Builder.CreateBitCast(Ops[2], VTy);
      Ops[2] = Builder.CreateExtractElement(Ops[2], Ops[3], "extract");
      Value *Result;
      Result = emitCallMaybeConstrainedFPBuiltin(
          *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma,
          DoubleTy, {Ops[1], Ops[2], Ops[0]});
      return Builder.CreateBitCast(Result, Ty);
    }
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);

    auto *STy = llvm::FixedVectorType::get(VTy->getElementType(),
                                           VTy->getNumElements() * 2);
    Ops[2] = Builder.CreateBitCast(Ops[2], STy);
    Value *SV = llvm::ConstantVector::getSplat(VTy->getElementCount(),
                                               cast<ConstantInt>(Ops[3]));
    Ops[2] = Builder.CreateShuffleVector(Ops[2], Ops[2], SV, "lane");

    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, Ty,
        {Ops[2], Ops[1], Ops[0]});
  }
  case NEON::BI__builtin_neon_vfmaq_laneq_v: {
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);

    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[2] = EmitNeonSplat(Ops[2], cast<ConstantInt>(Ops[3]));
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, Ty,
        {Ops[2], Ops[1], Ops[0]});
  }
  case NEON::BI__builtin_neon_vfmah_lane_f16:
  case NEON::BI__builtin_neon_vfmas_lane_f32:
  case NEON::BI__builtin_neon_vfmah_laneq_f16:
  case NEON::BI__builtin_neon_vfmas_laneq_f32:
  case NEON::BI__builtin_neon_vfmad_lane_f64:
  case NEON::BI__builtin_neon_vfmad_laneq_f64: {
    Ops.push_back(EmitScalarExpr(E->getArg(3)));
    llvm::Type *Ty = ConvertType(E->getCallReturnType(getContext()));
    Ops[2] = Builder.CreateExtractElement(Ops[2], Ops[3], "extract");
    return emitCallMaybeConstrainedFPBuiltin(
        *this, Intrinsic::fma, Intrinsic::experimental_constrained_fma, Ty,
        {Ops[1], Ops[2], Ops[0]});
  }
  case NEON::BI__builtin_neon_vmull_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_umull : Intrinsic::aarch64_neon_smull;
    if (Type.isPoly()) Int = Intrinsic::aarch64_neon_pmull;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmull");
  case NEON::BI__builtin_neon_vmax_v:
  case NEON::BI__builtin_neon_vmaxq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_umax : Intrinsic::aarch64_neon_smax;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fmax;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmax");
  case NEON::BI__builtin_neon_vmaxh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmax;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vmax");
  }
  case NEON::BI__builtin_neon_vmin_v:
  case NEON::BI__builtin_neon_vminq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_umin : Intrinsic::aarch64_neon_smin;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fmin;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmin");
  case NEON::BI__builtin_neon_vminh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmin;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vmin");
  }
  case NEON::BI__builtin_neon_vabd_v:
  case NEON::BI__builtin_neon_vabdq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_uabd : Intrinsic::aarch64_neon_sabd;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fabd;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vabd");
  case NEON::BI__builtin_neon_vpadal_v:
  case NEON::BI__builtin_neon_vpadalq_v: {
    unsigned ArgElts = VTy->getNumElements();
    llvm::IntegerType *EltTy = cast<IntegerType>(VTy->getElementType());
    unsigned BitWidth = EltTy->getBitWidth();
    auto *ArgTy = llvm::FixedVectorType::get(
        llvm::IntegerType::get(getLLVMContext(), BitWidth / 2), 2 * ArgElts);
    llvm::Type* Tys[2] = { VTy, ArgTy };
    Int = usgn ? Intrinsic::aarch64_neon_uaddlp : Intrinsic::aarch64_neon_saddlp;
    SmallVector<llvm::Value*, 1> TmpOps;
    TmpOps.push_back(Ops[1]);
    Function *F = CGM.getIntrinsic(Int, Tys);
    llvm::Value *tmp = EmitNeonCall(F, TmpOps, "vpadal");
    llvm::Value *addend = Builder.CreateBitCast(Ops[0], tmp->getType());
    return Builder.CreateAdd(tmp, addend);
  }
  case NEON::BI__builtin_neon_vpmin_v:
  case NEON::BI__builtin_neon_vpminq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_uminp : Intrinsic::aarch64_neon_sminp;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fminp;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vpmin");
  case NEON::BI__builtin_neon_vpmax_v:
  case NEON::BI__builtin_neon_vpmaxq_v:
    // FIXME: improve sharing scheme to cope with 3 alternative LLVM intrinsics.
    Int = usgn ? Intrinsic::aarch64_neon_umaxp : Intrinsic::aarch64_neon_smaxp;
    if (Ty->isFPOrFPVectorTy()) Int = Intrinsic::aarch64_neon_fmaxp;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vpmax");
  case NEON::BI__builtin_neon_vminnm_v:
  case NEON::BI__builtin_neon_vminnmq_v:
    Int = Intrinsic::aarch64_neon_fminnm;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vminnm");
  case NEON::BI__builtin_neon_vminnmh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fminnm;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vminnm");
  case NEON::BI__builtin_neon_vmaxnm_v:
  case NEON::BI__builtin_neon_vmaxnmq_v:
    Int = Intrinsic::aarch64_neon_fmaxnm;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmaxnm");
  case NEON::BI__builtin_neon_vmaxnmh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Int = Intrinsic::aarch64_neon_fmaxnm;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vmaxnm");
  case NEON::BI__builtin_neon_vrecpss_f32: {
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_frecps, FloatTy),
                        Ops, "vrecps");
  }
  case NEON::BI__builtin_neon_vrecpsd_f64:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_frecps, DoubleTy),
                        Ops, "vrecps");
  case NEON::BI__builtin_neon_vrecpsh_f16:
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_frecps, HalfTy),
                        Ops, "vrecps");
  case NEON::BI__builtin_neon_vqshrun_n_v:
    Int = Intrinsic::aarch64_neon_sqshrun;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshrun_n");
  case NEON::BI__builtin_neon_vqrshrun_n_v:
    Int = Intrinsic::aarch64_neon_sqrshrun;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqrshrun_n");
  case NEON::BI__builtin_neon_vqshrn_n_v:
    Int = usgn ? Intrinsic::aarch64_neon_uqshrn : Intrinsic::aarch64_neon_sqshrn;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqshrn_n");
  case NEON::BI__builtin_neon_vrshrn_n_v:
    Int = Intrinsic::aarch64_neon_rshrn;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrshrn_n");
  case NEON::BI__builtin_neon_vqrshrn_n_v:
    Int = usgn ? Intrinsic::aarch64_neon_uqrshrn : Intrinsic::aarch64_neon_sqrshrn;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vqrshrn_n");
  case NEON::BI__builtin_neon_vrndah_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_round
              : Intrinsic::round;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrnda");
  }
  case NEON::BI__builtin_neon_vrnda_v:
  case NEON::BI__builtin_neon_vrndaq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_round
              : Intrinsic::round;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrnda");
  }
  case NEON::BI__builtin_neon_vrndih_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_nearbyint
              : Intrinsic::nearbyint;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndi");
  }
  case NEON::BI__builtin_neon_vrndmh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_floor
              : Intrinsic::floor;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndm");
  }
  case NEON::BI__builtin_neon_vrndm_v:
  case NEON::BI__builtin_neon_vrndmq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_floor
              : Intrinsic::floor;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndm");
  }
  case NEON::BI__builtin_neon_vrndnh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_roundeven
              : Intrinsic::roundeven;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndn_v:
  case NEON::BI__builtin_neon_vrndnq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_roundeven
              : Intrinsic::roundeven;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndns_f32: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_roundeven
              : Intrinsic::roundeven;
    return EmitNeonCall(CGM.getIntrinsic(Int, FloatTy), Ops, "vrndn");
  }
  case NEON::BI__builtin_neon_vrndph_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_ceil
              : Intrinsic::ceil;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndp");
  }
  case NEON::BI__builtin_neon_vrndp_v:
  case NEON::BI__builtin_neon_vrndpq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_ceil
              : Intrinsic::ceil;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndp");
  }
  case NEON::BI__builtin_neon_vrndxh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_rint
              : Intrinsic::rint;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndx");
  }
  case NEON::BI__builtin_neon_vrndx_v:
  case NEON::BI__builtin_neon_vrndxq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_rint
              : Intrinsic::rint;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndx");
  }
  case NEON::BI__builtin_neon_vrndh_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_trunc
              : Intrinsic::trunc;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vrndz");
  }
  case NEON::BI__builtin_neon_vrnd32x_f32:
  case NEON::BI__builtin_neon_vrnd32xq_f32:
  case NEON::BI__builtin_neon_vrnd32x_f64:
  case NEON::BI__builtin_neon_vrnd32xq_f64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frint32x;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrnd32x");
  }
  case NEON::BI__builtin_neon_vrnd32z_f32:
  case NEON::BI__builtin_neon_vrnd32zq_f32:
  case NEON::BI__builtin_neon_vrnd32z_f64:
  case NEON::BI__builtin_neon_vrnd32zq_f64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frint32z;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrnd32z");
  }
  case NEON::BI__builtin_neon_vrnd64x_f32:
  case NEON::BI__builtin_neon_vrnd64xq_f32:
  case NEON::BI__builtin_neon_vrnd64x_f64:
  case NEON::BI__builtin_neon_vrnd64xq_f64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frint64x;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrnd64x");
  }
  case NEON::BI__builtin_neon_vrnd64z_f32:
  case NEON::BI__builtin_neon_vrnd64zq_f32:
  case NEON::BI__builtin_neon_vrnd64z_f64:
  case NEON::BI__builtin_neon_vrnd64zq_f64: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Intrinsic::aarch64_neon_frint64z;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrnd64z");
  }
  case NEON::BI__builtin_neon_vrnd_v:
  case NEON::BI__builtin_neon_vrndq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_trunc
              : Intrinsic::trunc;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrndz");
  }
  case NEON::BI__builtin_neon_vcvt_f64_v:
  case NEON::BI__builtin_neon_vcvtq_f64_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ty = GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float64, false, quad));
    return usgn ? Builder.CreateUIToFP(Ops[0], Ty, "vcvt")
                : Builder.CreateSIToFP(Ops[0], Ty, "vcvt");
  case NEON::BI__builtin_neon_vcvt_f64_f32: {
    assert(Type.getEltType() == NeonTypeFlags::Float64 && quad &&
           "unexpected vcvt_f64_f32 builtin");
    NeonTypeFlags SrcFlag = NeonTypeFlags(NeonTypeFlags::Float32, false, false);
    Ops[0] = Builder.CreateBitCast(Ops[0], GetNeonType(this, SrcFlag));

    return Builder.CreateFPExt(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvt_f32_f64: {
    assert(Type.getEltType() == NeonTypeFlags::Float32 &&
           "unexpected vcvt_f32_f64 builtin");
    NeonTypeFlags SrcFlag = NeonTypeFlags(NeonTypeFlags::Float64, false, true);
    Ops[0] = Builder.CreateBitCast(Ops[0], GetNeonType(this, SrcFlag));

    return Builder.CreateFPTrunc(Ops[0], Ty, "vcvt");
  }
  case NEON::BI__builtin_neon_vcvt_s32_v:
  case NEON::BI__builtin_neon_vcvt_u32_v:
  case NEON::BI__builtin_neon_vcvt_s64_v:
  case NEON::BI__builtin_neon_vcvt_u64_v:
  case NEON::BI__builtin_neon_vcvt_s16_f16:
  case NEON::BI__builtin_neon_vcvt_u16_f16:
  case NEON::BI__builtin_neon_vcvtq_s32_v:
  case NEON::BI__builtin_neon_vcvtq_u32_v:
  case NEON::BI__builtin_neon_vcvtq_s64_v:
  case NEON::BI__builtin_neon_vcvtq_u64_v:
  case NEON::BI__builtin_neon_vcvtq_s16_f16:
  case NEON::BI__builtin_neon_vcvtq_u16_f16: {
    Int =
        usgn ? Intrinsic::aarch64_neon_fcvtzu : Intrinsic::aarch64_neon_fcvtzs;
    llvm::Type *Tys[2] = {Ty, GetFloatNeonType(this, Type)};
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvtz");
  }
  case NEON::BI__builtin_neon_vcvta_s16_f16:
  case NEON::BI__builtin_neon_vcvta_u16_f16:
  case NEON::BI__builtin_neon_vcvta_s32_v:
  case NEON::BI__builtin_neon_vcvtaq_s16_f16:
  case NEON::BI__builtin_neon_vcvtaq_s32_v:
  case NEON::BI__builtin_neon_vcvta_u32_v:
  case NEON::BI__builtin_neon_vcvtaq_u16_f16:
  case NEON::BI__builtin_neon_vcvtaq_u32_v:
  case NEON::BI__builtin_neon_vcvta_s64_v:
  case NEON::BI__builtin_neon_vcvtaq_s64_v:
  case NEON::BI__builtin_neon_vcvta_u64_v:
  case NEON::BI__builtin_neon_vcvtaq_u64_v: {
    Int = usgn ? Intrinsic::aarch64_neon_fcvtau : Intrinsic::aarch64_neon_fcvtas;
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvta");
  }
  case NEON::BI__builtin_neon_vcvtm_s16_f16:
  case NEON::BI__builtin_neon_vcvtm_s32_v:
  case NEON::BI__builtin_neon_vcvtmq_s16_f16:
  case NEON::BI__builtin_neon_vcvtmq_s32_v:
  case NEON::BI__builtin_neon_vcvtm_u16_f16:
  case NEON::BI__builtin_neon_vcvtm_u32_v:
  case NEON::BI__builtin_neon_vcvtmq_u16_f16:
  case NEON::BI__builtin_neon_vcvtmq_u32_v:
  case NEON::BI__builtin_neon_vcvtm_s64_v:
  case NEON::BI__builtin_neon_vcvtmq_s64_v:
  case NEON::BI__builtin_neon_vcvtm_u64_v:
  case NEON::BI__builtin_neon_vcvtmq_u64_v: {
    Int = usgn ? Intrinsic::aarch64_neon_fcvtmu : Intrinsic::aarch64_neon_fcvtms;
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvtm");
  }
  case NEON::BI__builtin_neon_vcvtn_s16_f16:
  case NEON::BI__builtin_neon_vcvtn_s32_v:
  case NEON::BI__builtin_neon_vcvtnq_s16_f16:
  case NEON::BI__builtin_neon_vcvtnq_s32_v:
  case NEON::BI__builtin_neon_vcvtn_u16_f16:
  case NEON::BI__builtin_neon_vcvtn_u32_v:
  case NEON::BI__builtin_neon_vcvtnq_u16_f16:
  case NEON::BI__builtin_neon_vcvtnq_u32_v:
  case NEON::BI__builtin_neon_vcvtn_s64_v:
  case NEON::BI__builtin_neon_vcvtnq_s64_v:
  case NEON::BI__builtin_neon_vcvtn_u64_v:
  case NEON::BI__builtin_neon_vcvtnq_u64_v: {
    Int = usgn ? Intrinsic::aarch64_neon_fcvtnu : Intrinsic::aarch64_neon_fcvtns;
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvtn");
  }
  case NEON::BI__builtin_neon_vcvtp_s16_f16:
  case NEON::BI__builtin_neon_vcvtp_s32_v:
  case NEON::BI__builtin_neon_vcvtpq_s16_f16:
  case NEON::BI__builtin_neon_vcvtpq_s32_v:
  case NEON::BI__builtin_neon_vcvtp_u16_f16:
  case NEON::BI__builtin_neon_vcvtp_u32_v:
  case NEON::BI__builtin_neon_vcvtpq_u16_f16:
  case NEON::BI__builtin_neon_vcvtpq_u32_v:
  case NEON::BI__builtin_neon_vcvtp_s64_v:
  case NEON::BI__builtin_neon_vcvtpq_s64_v:
  case NEON::BI__builtin_neon_vcvtp_u64_v:
  case NEON::BI__builtin_neon_vcvtpq_u64_v: {
    Int = usgn ? Intrinsic::aarch64_neon_fcvtpu : Intrinsic::aarch64_neon_fcvtps;
    llvm::Type *Tys[2] = { Ty, GetFloatNeonType(this, Type) };
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vcvtp");
  }
  case NEON::BI__builtin_neon_vmulx_v:
  case NEON::BI__builtin_neon_vmulxq_v: {
    Int = Intrinsic::aarch64_neon_fmulx;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vmulx");
  }
  case NEON::BI__builtin_neon_vmulxh_lane_f16:
  case NEON::BI__builtin_neon_vmulxh_laneq_f16: {
    // vmulx_lane should be mapped to Neon scalar mulx after
    // extracting the scalar element
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2], "extract");
    Ops.pop_back();
    Int = Intrinsic::aarch64_neon_fmulx;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vmulx");
  }
  case NEON::BI__builtin_neon_vmul_lane_v:
  case NEON::BI__builtin_neon_vmul_laneq_v: {
    // v1f64 vmul_lane should be mapped to Neon scalar mul lane
    bool Quad = false;
    if (BuiltinID == NEON::BI__builtin_neon_vmul_laneq_v)
      Quad = true;
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    llvm::FixedVectorType *VTy =
        GetNeonType(this, NeonTypeFlags(NeonTypeFlags::Float64, false, Quad));
    Ops[1] = Builder.CreateBitCast(Ops[1], VTy);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2], "extract");
    Value *Result = Builder.CreateFMul(Ops[0], Ops[1]);
    return Builder.CreateBitCast(Result, Ty);
  }
  case NEON::BI__builtin_neon_vnegd_s64:
    return Builder.CreateNeg(EmitScalarExpr(E->getArg(0)), "vnegd");
  case NEON::BI__builtin_neon_vnegh_f16:
    return Builder.CreateFNeg(EmitScalarExpr(E->getArg(0)), "vnegh");
  case NEON::BI__builtin_neon_vpmaxnm_v:
  case NEON::BI__builtin_neon_vpmaxnmq_v: {
    Int = Intrinsic::aarch64_neon_fmaxnmp;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vpmaxnm");
  }
  case NEON::BI__builtin_neon_vpminnm_v:
  case NEON::BI__builtin_neon_vpminnmq_v: {
    Int = Intrinsic::aarch64_neon_fminnmp;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vpminnm");
  }
  case NEON::BI__builtin_neon_vsqrth_f16: {
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_sqrt
              : Intrinsic::sqrt;
    return EmitNeonCall(CGM.getIntrinsic(Int, HalfTy), Ops, "vsqrt");
  }
  case NEON::BI__builtin_neon_vsqrt_v:
  case NEON::BI__builtin_neon_vsqrtq_v: {
    Int = Builder.getIsFPConstrained()
              ? Intrinsic::experimental_constrained_sqrt
              : Intrinsic::sqrt;
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vsqrt");
  }
  case NEON::BI__builtin_neon_vrbit_v:
  case NEON::BI__builtin_neon_vrbitq_v: {
    Int = Intrinsic::bitreverse;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vrbit");
  }
  case NEON::BI__builtin_neon_vaddv_u8:
    // FIXME: These are handled by the AArch64 scalar code.
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddv_s8: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vaddv_u16:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddv_s16: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddvq_u8:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddvq_s8: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vaddvq_u16:
    usgn = true;
    [[fallthrough]];
  case NEON::BI__builtin_neon_vaddvq_s16: {
    Int = usgn ? Intrinsic::aarch64_neon_uaddv : Intrinsic::aarch64_neon_saddv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_u8: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_u16: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_u8: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_u16: {
    Int = Intrinsic::aarch64_neon_umaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_s8: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_s16: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_s8: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vmaxvq_s16: {
    Int = Intrinsic::aarch64_neon_smaxv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vmaxv_f16: {
    Int = Intrinsic::aarch64_neon_fmaxv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxvq_f16: {
    Int = Intrinsic::aarch64_neon_fmaxv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminv_u8: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminv_u16: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminvq_u8: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminvq_u16: {
    Int = Intrinsic::aarch64_neon_uminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminv_s8: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminv_s16: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminvq_s8: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int8Ty);
  }
  case NEON::BI__builtin_neon_vminvq_s16: {
    Int = Intrinsic::aarch64_neon_sminv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vminv_f16: {
    Int = Intrinsic::aarch64_neon_fminv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminvq_f16: {
    Int = Intrinsic::aarch64_neon_fminv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxnmv_f16: {
    Int = Intrinsic::aarch64_neon_fmaxnmv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmaxnmvq_f16: {
    Int = Intrinsic::aarch64_neon_fmaxnmv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vmaxnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminnmv_f16: {
    Int = Intrinsic::aarch64_neon_fminnmv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vminnmvq_f16: {
    Int = Intrinsic::aarch64_neon_fminnmv;
    Ty = HalfTy;
    VTy = llvm::FixedVectorType::get(HalfTy, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vminnmv");
    return Builder.CreateTrunc(Ops[0], HalfTy);
  }
  case NEON::BI__builtin_neon_vmul_n_f64: {
    Ops[0] = Builder.CreateBitCast(Ops[0], DoubleTy);
    Value *RHS = Builder.CreateBitCast(EmitScalarExpr(E->getArg(1)), DoubleTy);
    return Builder.CreateFMul(Ops[0], RHS);
  }
  case NEON::BI__builtin_neon_vaddlv_u8: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlv_u16: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlvq_u8: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlvq_u16: {
    Int = Intrinsic::aarch64_neon_uaddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlv_s8: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlv_s16: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 4);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vaddlvq_s8: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int8Ty, 16);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops[0] = EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
    return Builder.CreateTrunc(Ops[0], Int16Ty);
  }
  case NEON::BI__builtin_neon_vaddlvq_s16: {
    Int = Intrinsic::aarch64_neon_saddlv;
    Ty = Int32Ty;
    VTy = llvm::FixedVectorType::get(Int16Ty, 8);
    llvm::Type *Tys[2] = { Ty, VTy };
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    return EmitNeonCall(CGM.getIntrinsic(Int, Tys), Ops, "vaddlv");
  }
  case NEON::BI__builtin_neon_vsri_n_v:
  case NEON::BI__builtin_neon_vsriq_n_v: {
    Int = Intrinsic::aarch64_neon_vsri;
    llvm::Function *Intrin = CGM.getIntrinsic(Int, Ty);
    return EmitNeonCall(Intrin, Ops, "vsri_n");
  }
  case NEON::BI__builtin_neon_vsli_n_v:
  case NEON::BI__builtin_neon_vsliq_n_v: {
    Int = Intrinsic::aarch64_neon_vsli;
    llvm::Function *Intrin = CGM.getIntrinsic(Int, Ty);
    return EmitNeonCall(Intrin, Ops, "vsli_n");
  }
  case NEON::BI__builtin_neon_vsra_n_v:
  case NEON::BI__builtin_neon_vsraq_n_v:
    Ops[0] = Builder.CreateBitCast(Ops[0], Ty);
    Ops[1] = EmitNeonRShiftImm(Ops[1], Ops[2], Ty, usgn, "vsra_n");
    return Builder.CreateAdd(Ops[0], Ops[1]);
  case NEON::BI__builtin_neon_vrsra_n_v:
  case NEON::BI__builtin_neon_vrsraq_n_v: {
    Int = usgn ? Intrinsic::aarch64_neon_urshl : Intrinsic::aarch64_neon_srshl;
    SmallVector<llvm::Value*,2> TmpOps;
    TmpOps.push_back(Ops[1]);
    TmpOps.push_back(Ops[2]);
    Function* F = CGM.getIntrinsic(Int, Ty);
    llvm::Value *tmp = EmitNeonCall(F, TmpOps, "vrshr_n", 1, true);
    Ops[0] = Builder.CreateBitCast(Ops[0], VTy);
    return Builder.CreateAdd(Ops[0], tmp);
  }
  case NEON::BI__builtin_neon_vld1_v:
  case NEON::BI__builtin_neon_vld1q_v: {
    return Builder.CreateAlignedLoad(VTy, Ops[0], PtrOp0.getAlignment());
  }
  case NEON::BI__builtin_neon_vst1_v:
  case NEON::BI__builtin_neon_vst1q_v:
    Ops[1] = Builder.CreateBitCast(Ops[1], VTy);
    return Builder.CreateAlignedStore(Ops[1], Ops[0], PtrOp0.getAlignment());
  case NEON::BI__builtin_neon_vld1_lane_v:
  case NEON::BI__builtin_neon_vld1q_lane_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[0] = Builder.CreateAlignedLoad(VTy->getElementType(), Ops[0],
                                       PtrOp0.getAlignment());
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vld1_lane");
  }
  case NEON::BI__builtin_neon_vldap1_lane_s64:
  case NEON::BI__builtin_neon_vldap1q_lane_s64: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    llvm::LoadInst *LI = Builder.CreateAlignedLoad(
        VTy->getElementType(), Ops[0], PtrOp0.getAlignment());
    LI->setAtomic(llvm::AtomicOrdering::Acquire);
    Ops[0] = LI;
    return Builder.CreateInsertElement(Ops[1], Ops[0], Ops[2], "vldap1_lane");
  }
  case NEON::BI__builtin_neon_vld1_dup_v:
  case NEON::BI__builtin_neon_vld1q_dup_v: {
    Value *V = PoisonValue::get(Ty);
    Ops[0] = Builder.CreateAlignedLoad(VTy->getElementType(), Ops[0],
                                       PtrOp0.getAlignment());
    llvm::Constant *CI = ConstantInt::get(Int32Ty, 0);
    Ops[0] = Builder.CreateInsertElement(V, Ops[0], CI);
    return EmitNeonSplat(Ops[0], CI);
  }
  case NEON::BI__builtin_neon_vst1_lane_v:
  case NEON::BI__builtin_neon_vst1q_lane_v:
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2]);
    return Builder.CreateAlignedStore(Ops[1], Ops[0], PtrOp0.getAlignment());
  case NEON::BI__builtin_neon_vstl1_lane_s64:
  case NEON::BI__builtin_neon_vstl1q_lane_s64: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[1] = Builder.CreateExtractElement(Ops[1], Ops[2]);
    llvm::StoreInst *SI =
        Builder.CreateAlignedStore(Ops[1], Ops[0], PtrOp0.getAlignment());
    SI->setAtomic(llvm::AtomicOrdering::Release);
    return SI;
  }
  case NEON::BI__builtin_neon_vld2_v:
  case NEON::BI__builtin_neon_vld2q_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld2, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld2");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_v:
  case NEON::BI__builtin_neon_vld3q_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld3, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld3");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_v:
  case NEON::BI__builtin_neon_vld4q_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld4, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld4");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_dup_v:
  case NEON::BI__builtin_neon_vld2q_dup_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld2r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld2");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_dup_v:
  case NEON::BI__builtin_neon_vld3q_dup_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld3r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld3");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_dup_v:
  case NEON::BI__builtin_neon_vld4q_dup_v: {
    llvm::Type *Tys[2] = {VTy, UnqualPtrTy};
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld4r, Tys);
    Ops[1] = Builder.CreateCall(F, Ops[1], "vld4");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld2_lane_v:
  case NEON::BI__builtin_neon_vld2q_lane_v: {
    llvm::Type *Tys[2] = { VTy, Ops[1]->getType() };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld2lane, Tys);
    std::rotate(Ops.begin() + 1, Ops.begin() + 2, Ops.end());
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateZExt(Ops[3], Int64Ty);
    Ops[1] = Builder.CreateCall(F, ArrayRef(Ops).slice(1), "vld2_lane");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld3_lane_v:
  case NEON::BI__builtin_neon_vld3q_lane_v: {
    llvm::Type *Tys[2] = { VTy, Ops[1]->getType() };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld3lane, Tys);
    std::rotate(Ops.begin() + 1, Ops.begin() + 2, Ops.end());
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateBitCast(Ops[3], Ty);
    Ops[4] = Builder.CreateZExt(Ops[4], Int64Ty);
    Ops[1] = Builder.CreateCall(F, ArrayRef(Ops).slice(1), "vld3_lane");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vld4_lane_v:
  case NEON::BI__builtin_neon_vld4q_lane_v: {
    llvm::Type *Tys[2] = { VTy, Ops[1]->getType() };
    Function *F = CGM.getIntrinsic(Intrinsic::aarch64_neon_ld4lane, Tys);
    std::rotate(Ops.begin() + 1, Ops.begin() + 2, Ops.end());
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Ops[3] = Builder.CreateBitCast(Ops[3], Ty);
    Ops[4] = Builder.CreateBitCast(Ops[4], Ty);
    Ops[5] = Builder.CreateZExt(Ops[5], Int64Ty);
    Ops[1] = Builder.CreateCall(F, ArrayRef(Ops).slice(1), "vld4_lane");
    return Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
  }
  case NEON::BI__builtin_neon_vst2_v:
  case NEON::BI__builtin_neon_vst2q_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    llvm::Type *Tys[2] = { VTy, Ops[2]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st2, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst2_lane_v:
  case NEON::BI__builtin_neon_vst2q_lane_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    Ops[2] = Builder.CreateZExt(Ops[2], Int64Ty);
    llvm::Type *Tys[2] = { VTy, Ops[3]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st2lane, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst3_v:
  case NEON::BI__builtin_neon_vst3q_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    llvm::Type *Tys[2] = { VTy, Ops[3]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st3, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst3_lane_v:
  case NEON::BI__builtin_neon_vst3q_lane_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    Ops[3] = Builder.CreateZExt(Ops[3], Int64Ty);
    llvm::Type *Tys[2] = { VTy, Ops[4]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st3lane, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst4_v:
  case NEON::BI__builtin_neon_vst4q_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    llvm::Type *Tys[2] = { VTy, Ops[4]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st4, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vst4_lane_v:
  case NEON::BI__builtin_neon_vst4q_lane_v: {
    std::rotate(Ops.begin(), Ops.begin() + 1, Ops.end());
    Ops[4] = Builder.CreateZExt(Ops[4], Int64Ty);
    llvm::Type *Tys[2] = { VTy, Ops[5]->getType() };
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_st4lane, Tys),
                        Ops, "");
  }
  case NEON::BI__builtin_neon_vtrn_v:
  case NEON::BI__builtin_neon_vtrnq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back(i+vi);
        Indices.push_back(i+e+vi);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vtrn");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vuzp_v:
  case NEON::BI__builtin_neon_vuzpq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; ++i)
        Indices.push_back(2*i+vi);

      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vuzp");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vzip_v:
  case NEON::BI__builtin_neon_vzipq_v: {
    Ops[1] = Builder.CreateBitCast(Ops[1], Ty);
    Ops[2] = Builder.CreateBitCast(Ops[2], Ty);
    Value *SV = nullptr;

    for (unsigned vi = 0; vi != 2; ++vi) {
      SmallVector<int, 16> Indices;
      for (unsigned i = 0, e = VTy->getNumElements(); i != e; i += 2) {
        Indices.push_back((i + vi*e) >> 1);
        Indices.push_back(((i + vi*e) >> 1)+e);
      }
      Value *Addr = Builder.CreateConstInBoundsGEP1_32(Ty, Ops[0], vi);
      SV = Builder.CreateShuffleVector(Ops[1], Ops[2], Indices, "vzip");
      SV = Builder.CreateDefaultAlignedStore(SV, Addr);
    }
    return SV;
  }
  case NEON::BI__builtin_neon_vqtbl1q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbl1, Ty),
                        Ops, "vtbl1");
  }
  case NEON::BI__builtin_neon_vqtbl2q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbl2, Ty),
                        Ops, "vtbl2");
  }
  case NEON::BI__builtin_neon_vqtbl3q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbl3, Ty),
                        Ops, "vtbl3");
  }
  case NEON::BI__builtin_neon_vqtbl4q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbl4, Ty),
                        Ops, "vtbl4");
  }
  case NEON::BI__builtin_neon_vqtbx1q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbx1, Ty),
                        Ops, "vtbx1");
  }
  case NEON::BI__builtin_neon_vqtbx2q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbx2, Ty),
                        Ops, "vtbx2");
  }
  case NEON::BI__builtin_neon_vqtbx3q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbx3, Ty),
                        Ops, "vtbx3");
  }
  case NEON::BI__builtin_neon_vqtbx4q_v: {
    return EmitNeonCall(CGM.getIntrinsic(Intrinsic::aarch64_neon_tbx4, Ty),
                        Ops, "vtbx4");
  }
  case NEON::BI__builtin_neon_vsqadd_v:
  case NEON::BI__builtin_neon_vsqaddq_v: {
    Int = Intrinsic::aarch64_neon_usqadd;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vsqadd");
  }
  case NEON::BI__builtin_neon_vuqadd_v:
  case NEON::BI__builtin_neon_vuqaddq_v: {
    Int = Intrinsic::aarch64_neon_suqadd;
    return EmitNeonCall(CGM.getIntrinsic(Int, Ty), Ops, "vuqadd");
  }
  }
}

Value *CodeGenFunction::EmitBPFBuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E) {
  assert((BuiltinID == BPF::BI__builtin_preserve_field_info ||
          BuiltinID == BPF::BI__builtin_btf_type_id ||
          BuiltinID == BPF::BI__builtin_preserve_type_info ||
          BuiltinID == BPF::BI__builtin_preserve_enum_value) &&
         "unexpected BPF builtin");

  // A sequence number, injected into IR builtin functions, to
  // prevent CSE given the only difference of the function
  // may just be the debuginfo metadata.
  static uint32_t BuiltinSeqNum;

  switch (BuiltinID) {
  default:
    llvm_unreachable("Unexpected BPF builtin");
  case BPF::BI__builtin_preserve_field_info: {
    const Expr *Arg = E->getArg(0);
    bool IsBitField = Arg->IgnoreParens()->getObjectKind() == OK_BitField;

    if (!getDebugInfo()) {
      CGM.Error(E->getExprLoc(),
                "using __builtin_preserve_field_info() without -g");
      return IsBitField ? EmitLValue(Arg).getRawBitFieldPointer(*this)
                        : EmitLValue(Arg).emitRawPointer(*this);
    }

    // Enable underlying preserve_*_access_index() generation.
    bool OldIsInPreservedAIRegion = IsInPreservedAIRegion;
    IsInPreservedAIRegion = true;
    Value *FieldAddr = IsBitField ? EmitLValue(Arg).getRawBitFieldPointer(*this)
                                  : EmitLValue(Arg).emitRawPointer(*this);
    IsInPreservedAIRegion = OldIsInPreservedAIRegion;

    ConstantInt *C = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    Value *InfoKind = ConstantInt::get(Int64Ty, C->getSExtValue());

    // Built the IR for the preserve_field_info intrinsic.
    llvm::Function *FnGetFieldInfo = llvm::Intrinsic::getDeclaration(
        &CGM.getModule(), llvm::Intrinsic::bpf_preserve_field_info,
        {FieldAddr->getType()});
    return Builder.CreateCall(FnGetFieldInfo, {FieldAddr, InfoKind});
  }
  case BPF::BI__builtin_btf_type_id:
  case BPF::BI__builtin_preserve_type_info: {
    if (!getDebugInfo()) {
      CGM.Error(E->getExprLoc(), "using builtin function without -g");
      return nullptr;
    }

    const Expr *Arg0 = E->getArg(0);
    llvm::DIType *DbgInfo = getDebugInfo()->getOrCreateStandaloneType(
        Arg0->getType(), Arg0->getExprLoc());

    ConstantInt *Flag = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    Value *FlagValue = ConstantInt::get(Int64Ty, Flag->getSExtValue());
    Value *SeqNumVal = ConstantInt::get(Int32Ty, BuiltinSeqNum++);

    llvm::Function *FnDecl;
    if (BuiltinID == BPF::BI__builtin_btf_type_id)
      FnDecl = llvm::Intrinsic::getDeclaration(
          &CGM.getModule(), llvm::Intrinsic::bpf_btf_type_id, {});
    else
      FnDecl = llvm::Intrinsic::getDeclaration(
          &CGM.getModule(), llvm::Intrinsic::bpf_preserve_type_info, {});
    CallInst *Fn = Builder.CreateCall(FnDecl, {SeqNumVal, FlagValue});
    Fn->setMetadata(LLVMContext::MD_preserve_access_index, DbgInfo);
    return Fn;
  }
  case BPF::BI__builtin_preserve_enum_value: {
    if (!getDebugInfo()) {
      CGM.Error(E->getExprLoc(), "using builtin function without -g");
      return nullptr;
    }

    const Expr *Arg0 = E->getArg(0);
    llvm::DIType *DbgInfo = getDebugInfo()->getOrCreateStandaloneType(
        Arg0->getType(), Arg0->getExprLoc());

    // Find enumerator
    const auto *UO = cast<UnaryOperator>(Arg0->IgnoreParens());
    const auto *CE = cast<CStyleCastExpr>(UO->getSubExpr());
    const auto *DR = cast<DeclRefExpr>(CE->getSubExpr());
    const auto *Enumerator = cast<EnumConstantDecl>(DR->getDecl());

    auto InitVal = Enumerator->getInitVal();
    std::string InitValStr;
    if (InitVal.isNegative() || InitVal > uint64_t(INT64_MAX))
      InitValStr = std::to_string(InitVal.getSExtValue());
    else
      InitValStr = std::to_string(InitVal.getZExtValue());
    std::string EnumStr = Enumerator->getNameAsString() + ":" + InitValStr;
    Value *EnumStrVal = Builder.CreateGlobalStringPtr(EnumStr);

    ConstantInt *Flag = cast<ConstantInt>(EmitScalarExpr(E->getArg(1)));
    Value *FlagValue = ConstantInt::get(Int64Ty, Flag->getSExtValue());
    Value *SeqNumVal = ConstantInt::get(Int32Ty, BuiltinSeqNum++);

    llvm::Function *IntrinsicFn = llvm::Intrinsic::getDeclaration(
        &CGM.getModule(), llvm::Intrinsic::bpf_preserve_enum_value, {});
    CallInst *Fn =
        Builder.CreateCall(IntrinsicFn, {SeqNumVal, EnumStrVal, FlagValue});
    Fn->setMetadata(LLVMContext::MD_preserve_access_index, DbgInfo);
    return Fn;
  }
  }
}

llvm::Value *CodeGenFunction::
BuildVector(ArrayRef<llvm::Value*> Ops) {
  assert((Ops.size() & (Ops.size() - 1)) == 0 &&
         "Not a power-of-two sized vector!");
  bool AllConstants = true;
  for (unsigned i = 0, e = Ops.size(); i != e && AllConstants; ++i)
    AllConstants &= isa<Constant>(Ops[i]);

  // If this is a constant vector, create a ConstantVector.
  if (AllConstants) {
    SmallVector<llvm::Constant*, 16> CstOps;
    for (unsigned i = 0, e = Ops.size(); i != e; ++i)
      CstOps.push_back(cast<Constant>(Ops[i]));
    return llvm::ConstantVector::get(CstOps);
  }

  // Otherwise, insertelement the values to build the vector.
  Value *Result = llvm::PoisonValue::get(
      llvm::FixedVectorType::get(Ops[0]->getType(), Ops.size()));

  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    Result = Builder.CreateInsertElement(Result, Ops[i], Builder.getInt64(i));

  return Result;
}

// Convert the mask from an integer type to a vector of i1.
static Value *getMaskVecValue(CodeGenFunction &CGF, Value *Mask,
                              unsigned NumElts) {

  auto *MaskTy = llvm::FixedVectorType::get(
      CGF.Builder.getInt1Ty(),
      cast<IntegerType>(Mask->getType())->getBitWidth());
  Value *MaskVec = CGF.Builder.CreateBitCast(Mask, MaskTy);

  // If we have less than 8 elements, then the starting mask was an i8 and
  // we need to extract down to the right number of elements.
  if (NumElts < 8) {
    int Indices[4];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i;
    MaskVec = CGF.Builder.CreateShuffleVector(
        MaskVec, MaskVec, ArrayRef(Indices, NumElts), "extract");
  }
  return MaskVec;
}

static Value *EmitX86MaskedStore(CodeGenFunction &CGF, ArrayRef<Value *> Ops,
                                 Align Alignment) {
  Value *Ptr = Ops[0];

  Value *MaskVec = getMaskVecValue(
      CGF, Ops[2],
      cast<llvm::FixedVectorType>(Ops[1]->getType())->getNumElements());

  return CGF.Builder.CreateMaskedStore(Ops[1], Ptr, Alignment, MaskVec);
}

static Value *EmitX86MaskedLoad(CodeGenFunction &CGF, ArrayRef<Value *> Ops,
                                Align Alignment) {
  llvm::Type *Ty = Ops[1]->getType();
  Value *Ptr = Ops[0];

  Value *MaskVec = getMaskVecValue(
      CGF, Ops[2], cast<llvm::FixedVectorType>(Ty)->getNumElements());

  return CGF.Builder.CreateMaskedLoad(Ty, Ptr, Alignment, MaskVec, Ops[1]);
}

static Value *EmitX86ExpandLoad(CodeGenFunction &CGF,
                                ArrayRef<Value *> Ops) {
  auto *ResultTy = cast<llvm::VectorType>(Ops[1]->getType());
  Value *Ptr = Ops[0];

  Value *MaskVec = getMaskVecValue(
      CGF, Ops[2], cast<FixedVectorType>(ResultTy)->getNumElements());

  llvm::Function *F = CGF.CGM.getIntrinsic(Intrinsic::masked_expandload,
                                           ResultTy);
  return CGF.Builder.CreateCall(F, { Ptr, MaskVec, Ops[1] });
}

static Value *EmitX86CompressExpand(CodeGenFunction &CGF,
                                    ArrayRef<Value *> Ops,
                                    bool IsCompress) {
  auto *ResultTy = cast<llvm::FixedVectorType>(Ops[1]->getType());

  Value *MaskVec = getMaskVecValue(CGF, Ops[2], ResultTy->getNumElements());

  Intrinsic::ID IID = IsCompress ? Intrinsic::x86_avx512_mask_compress
                                 : Intrinsic::x86_avx512_mask_expand;
  llvm::Function *F = CGF.CGM.getIntrinsic(IID, ResultTy);
  return CGF.Builder.CreateCall(F, { Ops[0], Ops[1], MaskVec });
}

static Value *EmitX86CompressStore(CodeGenFunction &CGF,
                                   ArrayRef<Value *> Ops) {
  auto *ResultTy = cast<llvm::FixedVectorType>(Ops[1]->getType());
  Value *Ptr = Ops[0];

  Value *MaskVec = getMaskVecValue(CGF, Ops[2], ResultTy->getNumElements());

  llvm::Function *F = CGF.CGM.getIntrinsic(Intrinsic::masked_compressstore,
                                           ResultTy);
  return CGF.Builder.CreateCall(F, { Ops[1], Ptr, MaskVec });
}

static Value *EmitX86MaskLogic(CodeGenFunction &CGF, Instruction::BinaryOps Opc,
                              ArrayRef<Value *> Ops,
                              bool InvertLHS = false) {
  unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
  Value *LHS = getMaskVecValue(CGF, Ops[0], NumElts);
  Value *RHS = getMaskVecValue(CGF, Ops[1], NumElts);

  if (InvertLHS)
    LHS = CGF.Builder.CreateNot(LHS);

  return CGF.Builder.CreateBitCast(CGF.Builder.CreateBinOp(Opc, LHS, RHS),
                                   Ops[0]->getType());
}

static Value *EmitX86FunnelShift(CodeGenFunction &CGF, Value *Op0, Value *Op1,
                                 Value *Amt, bool IsRight) {
  llvm::Type *Ty = Op0->getType();

  // Amount may be scalar immediate, in which case create a splat vector.
  // Funnel shifts amounts are treated as modulo and types are all power-of-2 so
  // we only care about the lowest log2 bits anyway.
  if (Amt->getType() != Ty) {
    unsigned NumElts = cast<llvm::FixedVectorType>(Ty)->getNumElements();
    Amt = CGF.Builder.CreateIntCast(Amt, Ty->getScalarType(), false);
    Amt = CGF.Builder.CreateVectorSplat(NumElts, Amt);
  }

  unsigned IID = IsRight ? Intrinsic::fshr : Intrinsic::fshl;
  Function *F = CGF.CGM.getIntrinsic(IID, Ty);
  return CGF.Builder.CreateCall(F, {Op0, Op1, Amt});
}

static Value *EmitX86vpcom(CodeGenFunction &CGF, ArrayRef<Value *> Ops,
                           bool IsSigned) {
  Value *Op0 = Ops[0];
  Value *Op1 = Ops[1];
  llvm::Type *Ty = Op0->getType();
  uint64_t Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0x7;

  CmpInst::Predicate Pred;
  switch (Imm) {
  case 0x0:
    Pred = IsSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
    break;
  case 0x1:
    Pred = IsSigned ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE;
    break;
  case 0x2:
    Pred = IsSigned ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
    break;
  case 0x3:
    Pred = IsSigned ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE;
    break;
  case 0x4:
    Pred = ICmpInst::ICMP_EQ;
    break;
  case 0x5:
    Pred = ICmpInst::ICMP_NE;
    break;
  case 0x6:
    return llvm::Constant::getNullValue(Ty); // FALSE
  case 0x7:
    return llvm::Constant::getAllOnesValue(Ty); // TRUE
  default:
    llvm_unreachable("Unexpected XOP vpcom/vpcomu predicate");
  }

  Value *Cmp = CGF.Builder.CreateICmp(Pred, Op0, Op1);
  Value *Res = CGF.Builder.CreateSExt(Cmp, Ty);
  return Res;
}

static Value *EmitX86Select(CodeGenFunction &CGF,
                            Value *Mask, Value *Op0, Value *Op1) {

  // If the mask is all ones just return first argument.
  if (const auto *C = dyn_cast<Constant>(Mask))
    if (C->isAllOnesValue())
      return Op0;

  Mask = getMaskVecValue(
      CGF, Mask, cast<llvm::FixedVectorType>(Op0->getType())->getNumElements());

  return CGF.Builder.CreateSelect(Mask, Op0, Op1);
}

static Value *EmitX86ScalarSelect(CodeGenFunction &CGF,
                                  Value *Mask, Value *Op0, Value *Op1) {
  // If the mask is all ones just return first argument.
  if (const auto *C = dyn_cast<Constant>(Mask))
    if (C->isAllOnesValue())
      return Op0;

  auto *MaskTy = llvm::FixedVectorType::get(
      CGF.Builder.getInt1Ty(), Mask->getType()->getIntegerBitWidth());
  Mask = CGF.Builder.CreateBitCast(Mask, MaskTy);
  Mask = CGF.Builder.CreateExtractElement(Mask, (uint64_t)0);
  return CGF.Builder.CreateSelect(Mask, Op0, Op1);
}

static Value *EmitX86MaskedCompareResult(CodeGenFunction &CGF, Value *Cmp,
                                         unsigned NumElts, Value *MaskIn) {
  if (MaskIn) {
    const auto *C = dyn_cast<Constant>(MaskIn);
    if (!C || !C->isAllOnesValue())
      Cmp = CGF.Builder.CreateAnd(Cmp, getMaskVecValue(CGF, MaskIn, NumElts));
  }

  if (NumElts < 8) {
    int Indices[8];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i;
    for (unsigned i = NumElts; i != 8; ++i)
      Indices[i] = i % NumElts + NumElts;
    Cmp = CGF.Builder.CreateShuffleVector(
        Cmp, llvm::Constant::getNullValue(Cmp->getType()), Indices);
  }

  return CGF.Builder.CreateBitCast(Cmp,
                                   IntegerType::get(CGF.getLLVMContext(),
                                                    std::max(NumElts, 8U)));
}

static Value *EmitX86MaskedCompare(CodeGenFunction &CGF, unsigned CC,
                                   bool Signed, ArrayRef<Value *> Ops) {
  assert((Ops.size() == 2 || Ops.size() == 4) &&
         "Unexpected number of arguments");
  unsigned NumElts =
      cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
  Value *Cmp;

  if (CC == 3) {
    Cmp = Constant::getNullValue(
        llvm::FixedVectorType::get(CGF.Builder.getInt1Ty(), NumElts));
  } else if (CC == 7) {
    Cmp = Constant::getAllOnesValue(
        llvm::FixedVectorType::get(CGF.Builder.getInt1Ty(), NumElts));
  } else {
    ICmpInst::Predicate Pred;
    switch (CC) {
    default: llvm_unreachable("Unknown condition code");
    case 0: Pred = ICmpInst::ICMP_EQ;  break;
    case 1: Pred = Signed ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT; break;
    case 2: Pred = Signed ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE; break;
    case 4: Pred = ICmpInst::ICMP_NE;  break;
    case 5: Pred = Signed ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE; break;
    case 6: Pred = Signed ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT; break;
    }
    Cmp = CGF.Builder.CreateICmp(Pred, Ops[0], Ops[1]);
  }

  Value *MaskIn = nullptr;
  if (Ops.size() == 4)
    MaskIn = Ops[3];

  return EmitX86MaskedCompareResult(CGF, Cmp, NumElts, MaskIn);
}

static Value *EmitX86ConvertToMask(CodeGenFunction &CGF, Value *In) {
  Value *Zero = Constant::getNullValue(In->getType());
  return EmitX86MaskedCompare(CGF, 1, true, { In, Zero });
}

static Value *EmitX86ConvertIntToFp(CodeGenFunction &CGF, const CallExpr *E,
                                    ArrayRef<Value *> Ops, bool IsSigned) {
  unsigned Rnd = cast<llvm::ConstantInt>(Ops[3])->getZExtValue();
  llvm::Type *Ty = Ops[1]->getType();

  Value *Res;
  if (Rnd != 4) {
    Intrinsic::ID IID = IsSigned ? Intrinsic::x86_avx512_sitofp_round
                                 : Intrinsic::x86_avx512_uitofp_round;
    Function *F = CGF.CGM.getIntrinsic(IID, { Ty, Ops[0]->getType() });
    Res = CGF.Builder.CreateCall(F, { Ops[0], Ops[3] });
  } else {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
    Res = IsSigned ? CGF.Builder.CreateSIToFP(Ops[0], Ty)
                   : CGF.Builder.CreateUIToFP(Ops[0], Ty);
  }

  return EmitX86Select(CGF, Ops[2], Res, Ops[1]);
}

// Lowers X86 FMA intrinsics to IR.
static Value *EmitX86FMAExpr(CodeGenFunction &CGF, const CallExpr *E,
                             ArrayRef<Value *> Ops, unsigned BuiltinID,
                             bool IsAddSub) {

  bool Subtract = false;
  Intrinsic::ID IID = Intrinsic::not_intrinsic;
  switch (BuiltinID) {
  default: break;
  case clang::X86::BI__builtin_ia32_vfmsubph512_mask3:
    Subtract = true;
    [[fallthrough]];
  case clang::X86::BI__builtin_ia32_vfmaddph512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddph512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddph512_mask3:
    IID = llvm::Intrinsic::x86_avx512fp16_vfmadd_ph_512;
    break;
  case clang::X86::BI__builtin_ia32_vfmsubaddph512_mask3:
    Subtract = true;
    [[fallthrough]];
  case clang::X86::BI__builtin_ia32_vfmaddsubph512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubph512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubph512_mask3:
    IID = llvm::Intrinsic::x86_avx512fp16_vfmaddsub_ph_512;
    break;
  case clang::X86::BI__builtin_ia32_vfmsubps512_mask3:
    Subtract = true;
    [[fallthrough]];
  case clang::X86::BI__builtin_ia32_vfmaddps512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddps512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddps512_mask3:
    IID = llvm::Intrinsic::x86_avx512_vfmadd_ps_512; break;
  case clang::X86::BI__builtin_ia32_vfmsubpd512_mask3:
    Subtract = true;
    [[fallthrough]];
  case clang::X86::BI__builtin_ia32_vfmaddpd512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_mask3:
    IID = llvm::Intrinsic::x86_avx512_vfmadd_pd_512; break;
  case clang::X86::BI__builtin_ia32_vfmsubaddps512_mask3:
    Subtract = true;
    [[fallthrough]];
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_mask3:
    IID = llvm::Intrinsic::x86_avx512_vfmaddsub_ps_512;
    break;
  case clang::X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
    Subtract = true;
    [[fallthrough]];
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
    IID = llvm::Intrinsic::x86_avx512_vfmaddsub_pd_512;
    break;
  }

  Value *A = Ops[0];
  Value *B = Ops[1];
  Value *C = Ops[2];

  if (Subtract)
    C = CGF.Builder.CreateFNeg(C);

  Value *Res;

  // Only handle in case of _MM_FROUND_CUR_DIRECTION/4 (no rounding).
  if (IID != Intrinsic::not_intrinsic &&
      (cast<llvm::ConstantInt>(Ops.back())->getZExtValue() != (uint64_t)4 ||
       IsAddSub)) {
    Function *Intr = CGF.CGM.getIntrinsic(IID);
    Res = CGF.Builder.CreateCall(Intr, {A, B, C, Ops.back() });
  } else {
    llvm::Type *Ty = A->getType();
    Function *FMA;
    if (CGF.Builder.getIsFPConstrained()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
      FMA = CGF.CGM.getIntrinsic(Intrinsic::experimental_constrained_fma, Ty);
      Res = CGF.Builder.CreateConstrainedFPCall(FMA, {A, B, C});
    } else {
      FMA = CGF.CGM.getIntrinsic(Intrinsic::fma, Ty);
      Res = CGF.Builder.CreateCall(FMA, {A, B, C});
    }
  }

  // Handle any required masking.
  Value *MaskFalseVal = nullptr;
  switch (BuiltinID) {
  case clang::X86::BI__builtin_ia32_vfmaddph512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddps512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubph512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_mask:
    MaskFalseVal = Ops[0];
    break;
  case clang::X86::BI__builtin_ia32_vfmaddph512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddps512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubph512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
    MaskFalseVal = Constant::getNullValue(Ops[0]->getType());
    break;
  case clang::X86::BI__builtin_ia32_vfmsubph512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddph512_mask3:
  case clang::X86::BI__builtin_ia32_vfmsubps512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddps512_mask3:
  case clang::X86::BI__builtin_ia32_vfmsubpd512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case clang::X86::BI__builtin_ia32_vfmsubaddph512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddsubph512_mask3:
  case clang::X86::BI__builtin_ia32_vfmsubaddps512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case clang::X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
  case clang::X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
    MaskFalseVal = Ops[2];
    break;
  }

  if (MaskFalseVal)
    return EmitX86Select(CGF, Ops[3], Res, MaskFalseVal);

  return Res;
}

static Value *EmitScalarFMAExpr(CodeGenFunction &CGF, const CallExpr *E,
                                MutableArrayRef<Value *> Ops, Value *Upper,
                                bool ZeroMask = false, unsigned PTIdx = 0,
                                bool NegAcc = false) {
  unsigned Rnd = 4;
  if (Ops.size() > 4)
    Rnd = cast<llvm::ConstantInt>(Ops[4])->getZExtValue();

  if (NegAcc)
    Ops[2] = CGF.Builder.CreateFNeg(Ops[2]);

  Ops[0] = CGF.Builder.CreateExtractElement(Ops[0], (uint64_t)0);
  Ops[1] = CGF.Builder.CreateExtractElement(Ops[1], (uint64_t)0);
  Ops[2] = CGF.Builder.CreateExtractElement(Ops[2], (uint64_t)0);
  Value *Res;
  if (Rnd != 4) {
    Intrinsic::ID IID;

    switch (Ops[0]->getType()->getPrimitiveSizeInBits()) {
    case 16:
      IID = Intrinsic::x86_avx512fp16_vfmadd_f16;
      break;
    case 32:
      IID = Intrinsic::x86_avx512_vfmadd_f32;
      break;
    case 64:
      IID = Intrinsic::x86_avx512_vfmadd_f64;
      break;
    default:
      llvm_unreachable("Unexpected size");
    }
    Res = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(IID),
                                 {Ops[0], Ops[1], Ops[2], Ops[4]});
  } else if (CGF.Builder.getIsFPConstrained()) {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(CGF, E);
    Function *FMA = CGF.CGM.getIntrinsic(
        Intrinsic::experimental_constrained_fma, Ops[0]->getType());
    Res = CGF.Builder.CreateConstrainedFPCall(FMA, Ops.slice(0, 3));
  } else {
    Function *FMA = CGF.CGM.getIntrinsic(Intrinsic::fma, Ops[0]->getType());
    Res = CGF.Builder.CreateCall(FMA, Ops.slice(0, 3));
  }
  // If we have more than 3 arguments, we need to do masking.
  if (Ops.size() > 3) {
    Value *PassThru = ZeroMask ? Constant::getNullValue(Res->getType())
                               : Ops[PTIdx];

    // If we negated the accumulator and the its the PassThru value we need to
    // bypass the negate. Conveniently Upper should be the same thing in this
    // case.
    if (NegAcc && PTIdx == 2)
      PassThru = CGF.Builder.CreateExtractElement(Upper, (uint64_t)0);

    Res = EmitX86ScalarSelect(CGF, Ops[3], Res, PassThru);
  }
  return CGF.Builder.CreateInsertElement(Upper, Res, (uint64_t)0);
}

static Value *EmitX86Muldq(CodeGenFunction &CGF, bool IsSigned,
                           ArrayRef<Value *> Ops) {
  llvm::Type *Ty = Ops[0]->getType();
  // Arguments have a vXi32 type so cast to vXi64.
  Ty = llvm::FixedVectorType::get(CGF.Int64Ty,
                                  Ty->getPrimitiveSizeInBits() / 64);
  Value *LHS = CGF.Builder.CreateBitCast(Ops[0], Ty);
  Value *RHS = CGF.Builder.CreateBitCast(Ops[1], Ty);

  if (IsSigned) {
    // Shift left then arithmetic shift right.
    Constant *ShiftAmt = ConstantInt::get(Ty, 32);
    LHS = CGF.Builder.CreateShl(LHS, ShiftAmt);
    LHS = CGF.Builder.CreateAShr(LHS, ShiftAmt);
    RHS = CGF.Builder.CreateShl(RHS, ShiftAmt);
    RHS = CGF.Builder.CreateAShr(RHS, ShiftAmt);
  } else {
    // Clear the upper bits.
    Constant *Mask = ConstantInt::get(Ty, 0xffffffff);
    LHS = CGF.Builder.CreateAnd(LHS, Mask);
    RHS = CGF.Builder.CreateAnd(RHS, Mask);
  }

  return CGF.Builder.CreateMul(LHS, RHS);
}

// Emit a masked pternlog intrinsic. This only exists because the header has to
// use a macro and we aren't able to pass the input argument to a pternlog
// builtin and a select builtin without evaluating it twice.
static Value *EmitX86Ternlog(CodeGenFunction &CGF, bool ZeroMask,
                             ArrayRef<Value *> Ops) {
  llvm::Type *Ty = Ops[0]->getType();

  unsigned VecWidth = Ty->getPrimitiveSizeInBits();
  unsigned EltWidth = Ty->getScalarSizeInBits();
  Intrinsic::ID IID;
  if (VecWidth == 128 && EltWidth == 32)
    IID = Intrinsic::x86_avx512_pternlog_d_128;
  else if (VecWidth == 256 && EltWidth == 32)
    IID = Intrinsic::x86_avx512_pternlog_d_256;
  else if (VecWidth == 512 && EltWidth == 32)
    IID = Intrinsic::x86_avx512_pternlog_d_512;
  else if (VecWidth == 128 && EltWidth == 64)
    IID = Intrinsic::x86_avx512_pternlog_q_128;
  else if (VecWidth == 256 && EltWidth == 64)
    IID = Intrinsic::x86_avx512_pternlog_q_256;
  else if (VecWidth == 512 && EltWidth == 64)
    IID = Intrinsic::x86_avx512_pternlog_q_512;
  else
    llvm_unreachable("Unexpected intrinsic");

  Value *Ternlog = CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(IID),
                                          Ops.drop_back());
  Value *PassThru = ZeroMask ? ConstantAggregateZero::get(Ty) : Ops[0];
  return EmitX86Select(CGF, Ops[4], Ternlog, PassThru);
}

static Value *EmitX86SExtMask(CodeGenFunction &CGF, Value *Op,
                              llvm::Type *DstTy) {
  unsigned NumberOfElements =
      cast<llvm::FixedVectorType>(DstTy)->getNumElements();
  Value *Mask = getMaskVecValue(CGF, Op, NumberOfElements);
  return CGF.Builder.CreateSExt(Mask, DstTy, "vpmovm2");
}

Value *CodeGenFunction::EmitX86CpuIs(const CallExpr *E) {
  const Expr *CPUExpr = E->getArg(0)->IgnoreParenCasts();
  StringRef CPUStr = cast<clang::StringLiteral>(CPUExpr)->getString();
  return EmitX86CpuIs(CPUStr);
}

// Convert F16 halfs to floats.
static Value *EmitX86CvtF16ToFloatExpr(CodeGenFunction &CGF,
                                       ArrayRef<Value *> Ops,
                                       llvm::Type *DstTy) {
  assert((Ops.size() == 1 || Ops.size() == 3 || Ops.size() == 4) &&
         "Unknown cvtph2ps intrinsic");

  // If the SAE intrinsic doesn't use default rounding then we can't upgrade.
  if (Ops.size() == 4 && cast<llvm::ConstantInt>(Ops[3])->getZExtValue() != 4) {
    Function *F =
        CGF.CGM.getIntrinsic(Intrinsic::x86_avx512_mask_vcvtph2ps_512);
    return CGF.Builder.CreateCall(F, {Ops[0], Ops[1], Ops[2], Ops[3]});
  }

  unsigned NumDstElts = cast<llvm::FixedVectorType>(DstTy)->getNumElements();
  Value *Src = Ops[0];

  // Extract the subvector.
  if (NumDstElts !=
      cast<llvm::FixedVectorType>(Src->getType())->getNumElements()) {
    assert(NumDstElts == 4 && "Unexpected vector size");
    Src = CGF.Builder.CreateShuffleVector(Src, ArrayRef<int>{0, 1, 2, 3});
  }

  // Bitcast from vXi16 to vXf16.
  auto *HalfTy = llvm::FixedVectorType::get(
      llvm::Type::getHalfTy(CGF.getLLVMContext()), NumDstElts);
  Src = CGF.Builder.CreateBitCast(Src, HalfTy);

  // Perform the fp-extension.
  Value *Res = CGF.Builder.CreateFPExt(Src, DstTy, "cvtph2ps");

  if (Ops.size() >= 3)
    Res = EmitX86Select(CGF, Ops[2], Res, Ops[1]);
  return Res;
}

Value *CodeGenFunction::EmitX86CpuIs(StringRef CPUStr) {

  llvm::Type *Int32Ty = Builder.getInt32Ty();

  // Matching the struct layout from the compiler-rt/libgcc structure that is
  // filled in:
  // unsigned int __cpu_vendor;
  // unsigned int __cpu_type;
  // unsigned int __cpu_subtype;
  // unsigned int __cpu_features[1];
  llvm::Type *STy = llvm::StructType::get(Int32Ty, Int32Ty, Int32Ty,
                                          llvm::ArrayType::get(Int32Ty, 1));

  // Grab the global __cpu_model.
  llvm::Constant *CpuModel = CGM.CreateRuntimeVariable(STy, "__cpu_model");
  cast<llvm::GlobalValue>(CpuModel)->setDSOLocal(true);

  // Calculate the index needed to access the correct field based on the
  // range. Also adjust the expected value.
  unsigned Index;
  unsigned Value;
  std::tie(Index, Value) = StringSwitch<std::pair<unsigned, unsigned>>(CPUStr)
#define X86_VENDOR(ENUM, STRING)                                               \
  .Case(STRING, {0u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_TYPE_ALIAS(ENUM, ALIAS)                                        \
  .Case(ALIAS, {1u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_TYPE(ENUM, STR)                                                \
  .Case(STR, {1u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_SUBTYPE_ALIAS(ENUM, ALIAS)                                     \
  .Case(ALIAS, {2u, static_cast<unsigned>(llvm::X86::ENUM)})
#define X86_CPU_SUBTYPE(ENUM, STR)                                             \
  .Case(STR, {2u, static_cast<unsigned>(llvm::X86::ENUM)})
#include "llvm/TargetParser/X86TargetParser.def"
                               .Default({0, 0});
  assert(Value != 0 && "Invalid CPUStr passed to CpuIs");

  // Grab the appropriate field from __cpu_model.
  llvm::Value *Idxs[] = {ConstantInt::get(Int32Ty, 0),
                         ConstantInt::get(Int32Ty, Index)};
  llvm::Value *CpuValue = Builder.CreateInBoundsGEP(STy, CpuModel, Idxs);
  CpuValue = Builder.CreateAlignedLoad(Int32Ty, CpuValue,
                                       CharUnits::fromQuantity(4));

  // Check the value of the field against the requested value.
  return Builder.CreateICmpEQ(CpuValue,
                                  llvm::ConstantInt::get(Int32Ty, Value));
}

Value *CodeGenFunction::EmitX86CpuSupports(const CallExpr *E) {
  const Expr *FeatureExpr = E->getArg(0)->IgnoreParenCasts();
  StringRef FeatureStr = cast<StringLiteral>(FeatureExpr)->getString();
  if (!getContext().getTargetInfo().validateCpuSupports(FeatureStr))
    return Builder.getFalse();
  return EmitX86CpuSupports(FeatureStr);
}

Value *CodeGenFunction::EmitX86CpuSupports(ArrayRef<StringRef> FeatureStrs) {
  return EmitX86CpuSupports(llvm::X86::getCpuSupportsMask(FeatureStrs));
}

llvm::Value *
CodeGenFunction::EmitX86CpuSupports(std::array<uint32_t, 4> FeatureMask) {
  Value *Result = Builder.getTrue();
  if (FeatureMask[0] != 0) {
    // Matching the struct layout from the compiler-rt/libgcc structure that is
    // filled in:
    // unsigned int __cpu_vendor;
    // unsigned int __cpu_type;
    // unsigned int __cpu_subtype;
    // unsigned int __cpu_features[1];
    llvm::Type *STy = llvm::StructType::get(Int32Ty, Int32Ty, Int32Ty,
                                            llvm::ArrayType::get(Int32Ty, 1));

    // Grab the global __cpu_model.
    llvm::Constant *CpuModel = CGM.CreateRuntimeVariable(STy, "__cpu_model");
    cast<llvm::GlobalValue>(CpuModel)->setDSOLocal(true);

    // Grab the first (0th) element from the field __cpu_features off of the
    // global in the struct STy.
    Value *Idxs[] = {Builder.getInt32(0), Builder.getInt32(3),
                     Builder.getInt32(0)};
    Value *CpuFeatures = Builder.CreateInBoundsGEP(STy, CpuModel, Idxs);
    Value *Features = Builder.CreateAlignedLoad(Int32Ty, CpuFeatures,
                                                CharUnits::fromQuantity(4));

    // Check the value of the bit corresponding to the feature requested.
    Value *Mask = Builder.getInt32(FeatureMask[0]);
    Value *Bitset = Builder.CreateAnd(Features, Mask);
    Value *Cmp = Builder.CreateICmpEQ(Bitset, Mask);
    Result = Builder.CreateAnd(Result, Cmp);
  }

  llvm::Type *ATy = llvm::ArrayType::get(Int32Ty, 3);
  llvm::Constant *CpuFeatures2 =
      CGM.CreateRuntimeVariable(ATy, "__cpu_features2");
  cast<llvm::GlobalValue>(CpuFeatures2)->setDSOLocal(true);
  for (int i = 1; i != 4; ++i) {
    const uint32_t M = FeatureMask[i];
    if (!M)
      continue;
    Value *Idxs[] = {Builder.getInt32(0), Builder.getInt32(i - 1)};
    Value *Features = Builder.CreateAlignedLoad(
        Int32Ty, Builder.CreateInBoundsGEP(ATy, CpuFeatures2, Idxs),
        CharUnits::fromQuantity(4));
    // Check the value of the bit corresponding to the feature requested.
    Value *Mask = Builder.getInt32(M);
    Value *Bitset = Builder.CreateAnd(Features, Mask);
    Value *Cmp = Builder.CreateICmpEQ(Bitset, Mask);
    Result = Builder.CreateAnd(Result, Cmp);
  }

  return Result;
}

Value *CodeGenFunction::EmitAArch64CpuInit() {
  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
  llvm::FunctionCallee Func =
      CGM.CreateRuntimeFunction(FTy, "__init_cpu_features_resolver");
  cast<llvm::GlobalValue>(Func.getCallee())->setDSOLocal(true);
  cast<llvm::GlobalValue>(Func.getCallee())
      ->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
  return Builder.CreateCall(Func);
}

Value *CodeGenFunction::EmitX86CpuInit() {
  llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy,
                                                    /*Variadic*/ false);
  llvm::FunctionCallee Func =
      CGM.CreateRuntimeFunction(FTy, "__cpu_indicator_init");
  cast<llvm::GlobalValue>(Func.getCallee())->setDSOLocal(true);
  cast<llvm::GlobalValue>(Func.getCallee())
      ->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);
  return Builder.CreateCall(Func);
}

Value *CodeGenFunction::EmitAArch64CpuSupports(const CallExpr *E) {
  const Expr *ArgExpr = E->getArg(0)->IgnoreParenCasts();
  StringRef ArgStr = cast<StringLiteral>(ArgExpr)->getString();
  llvm::SmallVector<StringRef, 8> Features;
  ArgStr.split(Features, "+");
  for (auto &Feature : Features) {
    Feature = Feature.trim();
    if (!llvm::AArch64::parseFMVExtension(Feature))
      return Builder.getFalse();
    if (Feature != "default")
      Features.push_back(Feature);
  }
  return EmitAArch64CpuSupports(Features);
}

llvm::Value *
CodeGenFunction::EmitAArch64CpuSupports(ArrayRef<StringRef> FeaturesStrs) {
  uint64_t FeaturesMask = llvm::AArch64::getCpuSupportsMask(FeaturesStrs);
  Value *Result = Builder.getTrue();
  if (FeaturesMask != 0) {
    // Get features from structure in runtime library
    // struct {
    //   unsigned long long features;
    // } __aarch64_cpu_features;
    llvm::Type *STy = llvm::StructType::get(Int64Ty);
    llvm::Constant *AArch64CPUFeatures =
        CGM.CreateRuntimeVariable(STy, "__aarch64_cpu_features");
    cast<llvm::GlobalValue>(AArch64CPUFeatures)->setDSOLocal(true);
    llvm::Value *CpuFeatures = Builder.CreateGEP(
        STy, AArch64CPUFeatures,
        {ConstantInt::get(Int32Ty, 0), ConstantInt::get(Int32Ty, 0)});
    Value *Features = Builder.CreateAlignedLoad(Int64Ty, CpuFeatures,
                                                CharUnits::fromQuantity(8));
    Value *Mask = Builder.getInt64(FeaturesMask);
    Value *Bitset = Builder.CreateAnd(Features, Mask);
    Value *Cmp = Builder.CreateICmpEQ(Bitset, Mask);
    Result = Builder.CreateAnd(Result, Cmp);
  }
  return Result;
}

Value *CodeGenFunction::EmitX86BuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E) {
  if (BuiltinID == Builtin::BI__builtin_cpu_is)
    return EmitX86CpuIs(E);
  if (BuiltinID == Builtin::BI__builtin_cpu_supports)
    return EmitX86CpuSupports(E);
  if (BuiltinID == Builtin::BI__builtin_cpu_init)
    return EmitX86CpuInit();

  // Handle MSVC intrinsics before argument evaluation to prevent double
  // evaluation.
  if (std::optional<MSVCIntrin> MsvcIntId = translateX86ToMsvcIntrin(BuiltinID))
    return EmitMSVCBuiltinExpr(*MsvcIntId, E);

  SmallVector<Value*, 4> Ops;
  bool IsMaskFCmp = false;
  bool IsConjFMA = false;

  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");

  for (unsigned i = 0, e = E->getNumArgs(); i != e; i++) {
    Ops.push_back(EmitScalarOrConstFoldImmArg(ICEArguments, i, E));
  }

  // These exist so that the builtin that takes an immediate can be bounds
  // checked by clang to avoid passing bad immediates to the backend. Since
  // AVX has a larger immediate than SSE we would need separate builtins to
  // do the different bounds checking. Rather than create a clang specific
  // SSE only builtin, this implements eight separate builtins to match gcc
  // implementation.
  auto getCmpIntrinsicCall = [this, &Ops](Intrinsic::ID ID, unsigned Imm) {
    Ops.push_back(llvm::ConstantInt::get(Int8Ty, Imm));
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, Ops);
  };

  // For the vector forms of FP comparisons, translate the builtins directly to
  // IR.
  // TODO: The builtins could be removed if the SSE header files used vector
  // extension comparisons directly (vector ordered/unordered may need
  // additional support via __builtin_isnan()).
  auto getVectorFCmpIR = [this, &Ops, E](CmpInst::Predicate Pred,
                                         bool IsSignaling) {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    Value *Cmp;
    if (IsSignaling)
      Cmp = Builder.CreateFCmpS(Pred, Ops[0], Ops[1]);
    else
      Cmp = Builder.CreateFCmp(Pred, Ops[0], Ops[1]);
    llvm::VectorType *FPVecTy = cast<llvm::VectorType>(Ops[0]->getType());
    llvm::VectorType *IntVecTy = llvm::VectorType::getInteger(FPVecTy);
    Value *Sext = Builder.CreateSExt(Cmp, IntVecTy);
    return Builder.CreateBitCast(Sext, FPVecTy);
  };

  switch (BuiltinID) {
  default: return nullptr;
  case X86::BI_mm_prefetch: {
    Value *Address = Ops[0];
    ConstantInt *C = cast<ConstantInt>(Ops[1]);
    Value *RW = ConstantInt::get(Int32Ty, (C->getZExtValue() >> 2) & 0x1);
    Value *Locality = ConstantInt::get(Int32Ty, C->getZExtValue() & 0x3);
    Value *Data = ConstantInt::get(Int32Ty, 1);
    Function *F = CGM.getIntrinsic(Intrinsic::prefetch, Address->getType());
    return Builder.CreateCall(F, {Address, RW, Locality, Data});
  }
  case X86::BI_mm_clflush: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse2_clflush),
                              Ops[0]);
  }
  case X86::BI_mm_lfence: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse2_lfence));
  }
  case X86::BI_mm_mfence: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse2_mfence));
  }
  case X86::BI_mm_sfence: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse_sfence));
  }
  case X86::BI_mm_pause: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse2_pause));
  }
  case X86::BI__rdtsc: {
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_rdtsc));
  }
  case X86::BI__builtin_ia32_rdtscp: {
    Value *Call = Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_rdtscp));
    Builder.CreateDefaultAlignedStore(Builder.CreateExtractValue(Call, 1),
                                      Ops[0]);
    return Builder.CreateExtractValue(Call, 0);
  }
  case X86::BI__builtin_ia32_lzcnt_u16:
  case X86::BI__builtin_ia32_lzcnt_u32:
  case X86::BI__builtin_ia32_lzcnt_u64: {
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, Ops[0]->getType());
    return Builder.CreateCall(F, {Ops[0], Builder.getInt1(false)});
  }
  case X86::BI__builtin_ia32_tzcnt_u16:
  case X86::BI__builtin_ia32_tzcnt_u32:
  case X86::BI__builtin_ia32_tzcnt_u64: {
    Function *F = CGM.getIntrinsic(Intrinsic::cttz, Ops[0]->getType());
    return Builder.CreateCall(F, {Ops[0], Builder.getInt1(false)});
  }
  case X86::BI__builtin_ia32_undef128:
  case X86::BI__builtin_ia32_undef256:
  case X86::BI__builtin_ia32_undef512:
    // The x86 definition of "undef" is not the same as the LLVM definition
    // (PR32176). We leave optimizing away an unnecessary zero constant to the
    // IR optimizer and backend.
    // TODO: If we had a "freeze" IR instruction to generate a fixed undef
    // value, we should use that here instead of a zero.
    return llvm::Constant::getNullValue(ConvertType(E->getType()));
  case X86::BI__builtin_ia32_vec_init_v8qi:
  case X86::BI__builtin_ia32_vec_init_v4hi:
  case X86::BI__builtin_ia32_vec_init_v2si:
    return Builder.CreateBitCast(BuildVector(Ops),
                                 llvm::Type::getX86_MMXTy(getLLVMContext()));
  case X86::BI__builtin_ia32_vec_ext_v2si:
  case X86::BI__builtin_ia32_vec_ext_v16qi:
  case X86::BI__builtin_ia32_vec_ext_v8hi:
  case X86::BI__builtin_ia32_vec_ext_v4si:
  case X86::BI__builtin_ia32_vec_ext_v4sf:
  case X86::BI__builtin_ia32_vec_ext_v2di:
  case X86::BI__builtin_ia32_vec_ext_v32qi:
  case X86::BI__builtin_ia32_vec_ext_v16hi:
  case X86::BI__builtin_ia32_vec_ext_v8si:
  case X86::BI__builtin_ia32_vec_ext_v4di: {
    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    uint64_t Index = cast<ConstantInt>(Ops[1])->getZExtValue();
    Index &= NumElts - 1;
    // These builtins exist so we can ensure the index is an ICE and in range.
    // Otherwise we could just do this in the header file.
    return Builder.CreateExtractElement(Ops[0], Index);
  }
  case X86::BI__builtin_ia32_vec_set_v16qi:
  case X86::BI__builtin_ia32_vec_set_v8hi:
  case X86::BI__builtin_ia32_vec_set_v4si:
  case X86::BI__builtin_ia32_vec_set_v2di:
  case X86::BI__builtin_ia32_vec_set_v32qi:
  case X86::BI__builtin_ia32_vec_set_v16hi:
  case X86::BI__builtin_ia32_vec_set_v8si:
  case X86::BI__builtin_ia32_vec_set_v4di: {
    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    unsigned Index = cast<ConstantInt>(Ops[2])->getZExtValue();
    Index &= NumElts - 1;
    // These builtins exist so we can ensure the index is an ICE and in range.
    // Otherwise we could just do this in the header file.
    return Builder.CreateInsertElement(Ops[0], Ops[1], Index);
  }
  case X86::BI_mm_setcsr:
  case X86::BI__builtin_ia32_ldmxcsr: {
    RawAddress Tmp = CreateMemTemp(E->getArg(0)->getType());
    Builder.CreateStore(Ops[0], Tmp);
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse_ldmxcsr),
                              Tmp.getPointer());
  }
  case X86::BI_mm_getcsr:
  case X86::BI__builtin_ia32_stmxcsr: {
    RawAddress Tmp = CreateMemTemp(E->getType());
    Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_sse_stmxcsr),
                       Tmp.getPointer());
    return Builder.CreateLoad(Tmp, "stmxcsr");
  }
  case X86::BI__builtin_ia32_xsave:
  case X86::BI__builtin_ia32_xsave64:
  case X86::BI__builtin_ia32_xrstor:
  case X86::BI__builtin_ia32_xrstor64:
  case X86::BI__builtin_ia32_xsaveopt:
  case X86::BI__builtin_ia32_xsaveopt64:
  case X86::BI__builtin_ia32_xrstors:
  case X86::BI__builtin_ia32_xrstors64:
  case X86::BI__builtin_ia32_xsavec:
  case X86::BI__builtin_ia32_xsavec64:
  case X86::BI__builtin_ia32_xsaves:
  case X86::BI__builtin_ia32_xsaves64:
  case X86::BI__builtin_ia32_xsetbv:
  case X86::BI_xsetbv: {
    Intrinsic::ID ID;
#define INTRINSIC_X86_XSAVE_ID(NAME) \
    case X86::BI__builtin_ia32_##NAME: \
      ID = Intrinsic::x86_##NAME; \
      break
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    INTRINSIC_X86_XSAVE_ID(xsave);
    INTRINSIC_X86_XSAVE_ID(xsave64);
    INTRINSIC_X86_XSAVE_ID(xrstor);
    INTRINSIC_X86_XSAVE_ID(xrstor64);
    INTRINSIC_X86_XSAVE_ID(xsaveopt);
    INTRINSIC_X86_XSAVE_ID(xsaveopt64);
    INTRINSIC_X86_XSAVE_ID(xrstors);
    INTRINSIC_X86_XSAVE_ID(xrstors64);
    INTRINSIC_X86_XSAVE_ID(xsavec);
    INTRINSIC_X86_XSAVE_ID(xsavec64);
    INTRINSIC_X86_XSAVE_ID(xsaves);
    INTRINSIC_X86_XSAVE_ID(xsaves64);
    INTRINSIC_X86_XSAVE_ID(xsetbv);
    case X86::BI_xsetbv:
      ID = Intrinsic::x86_xsetbv;
      break;
    }
#undef INTRINSIC_X86_XSAVE_ID
    Value *Mhi = Builder.CreateTrunc(
      Builder.CreateLShr(Ops[1], ConstantInt::get(Int64Ty, 32)), Int32Ty);
    Value *Mlo = Builder.CreateTrunc(Ops[1], Int32Ty);
    Ops[1] = Mhi;
    Ops.push_back(Mlo);
    return Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
  }
  case X86::BI__builtin_ia32_xgetbv:
  case X86::BI_xgetbv:
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::x86_xgetbv), Ops);
  case X86::BI__builtin_ia32_storedqudi128_mask:
  case X86::BI__builtin_ia32_storedqusi128_mask:
  case X86::BI__builtin_ia32_storedquhi128_mask:
  case X86::BI__builtin_ia32_storedquqi128_mask:
  case X86::BI__builtin_ia32_storeupd128_mask:
  case X86::BI__builtin_ia32_storeups128_mask:
  case X86::BI__builtin_ia32_storedqudi256_mask:
  case X86::BI__builtin_ia32_storedqusi256_mask:
  case X86::BI__builtin_ia32_storedquhi256_mask:
  case X86::BI__builtin_ia32_storedquqi256_mask:
  case X86::BI__builtin_ia32_storeupd256_mask:
  case X86::BI__builtin_ia32_storeups256_mask:
  case X86::BI__builtin_ia32_storedqudi512_mask:
  case X86::BI__builtin_ia32_storedqusi512_mask:
  case X86::BI__builtin_ia32_storedquhi512_mask:
  case X86::BI__builtin_ia32_storedquqi512_mask:
  case X86::BI__builtin_ia32_storeupd512_mask:
  case X86::BI__builtin_ia32_storeups512_mask:
    return EmitX86MaskedStore(*this, Ops, Align(1));

  case X86::BI__builtin_ia32_storesh128_mask:
  case X86::BI__builtin_ia32_storess128_mask:
  case X86::BI__builtin_ia32_storesd128_mask:
    return EmitX86MaskedStore(*this, Ops, Align(1));

  case X86::BI__builtin_ia32_vpopcntb_128:
  case X86::BI__builtin_ia32_vpopcntd_128:
  case X86::BI__builtin_ia32_vpopcntq_128:
  case X86::BI__builtin_ia32_vpopcntw_128:
  case X86::BI__builtin_ia32_vpopcntb_256:
  case X86::BI__builtin_ia32_vpopcntd_256:
  case X86::BI__builtin_ia32_vpopcntq_256:
  case X86::BI__builtin_ia32_vpopcntw_256:
  case X86::BI__builtin_ia32_vpopcntb_512:
  case X86::BI__builtin_ia32_vpopcntd_512:
  case X86::BI__builtin_ia32_vpopcntq_512:
  case X86::BI__builtin_ia32_vpopcntw_512: {
    llvm::Type *ResultType = ConvertType(E->getType());
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ResultType);
    return Builder.CreateCall(F, Ops);
  }
  case X86::BI__builtin_ia32_cvtmask2b128:
  case X86::BI__builtin_ia32_cvtmask2b256:
  case X86::BI__builtin_ia32_cvtmask2b512:
  case X86::BI__builtin_ia32_cvtmask2w128:
  case X86::BI__builtin_ia32_cvtmask2w256:
  case X86::BI__builtin_ia32_cvtmask2w512:
  case X86::BI__builtin_ia32_cvtmask2d128:
  case X86::BI__builtin_ia32_cvtmask2d256:
  case X86::BI__builtin_ia32_cvtmask2d512:
  case X86::BI__builtin_ia32_cvtmask2q128:
  case X86::BI__builtin_ia32_cvtmask2q256:
  case X86::BI__builtin_ia32_cvtmask2q512:
    return EmitX86SExtMask(*this, Ops[0], ConvertType(E->getType()));

  case X86::BI__builtin_ia32_cvtb2mask128:
  case X86::BI__builtin_ia32_cvtb2mask256:
  case X86::BI__builtin_ia32_cvtb2mask512:
  case X86::BI__builtin_ia32_cvtw2mask128:
  case X86::BI__builtin_ia32_cvtw2mask256:
  case X86::BI__builtin_ia32_cvtw2mask512:
  case X86::BI__builtin_ia32_cvtd2mask128:
  case X86::BI__builtin_ia32_cvtd2mask256:
  case X86::BI__builtin_ia32_cvtd2mask512:
  case X86::BI__builtin_ia32_cvtq2mask128:
  case X86::BI__builtin_ia32_cvtq2mask256:
  case X86::BI__builtin_ia32_cvtq2mask512:
    return EmitX86ConvertToMask(*this, Ops[0]);

  case X86::BI__builtin_ia32_cvtdq2ps512_mask:
  case X86::BI__builtin_ia32_cvtqq2ps512_mask:
  case X86::BI__builtin_ia32_cvtqq2pd512_mask:
  case X86::BI__builtin_ia32_vcvtw2ph512_mask:
  case X86::BI__builtin_ia32_vcvtdq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtqq2ph512_mask:
    return EmitX86ConvertIntToFp(*this, E, Ops, /*IsSigned*/ true);
  case X86::BI__builtin_ia32_cvtudq2ps512_mask:
  case X86::BI__builtin_ia32_cvtuqq2ps512_mask:
  case X86::BI__builtin_ia32_cvtuqq2pd512_mask:
  case X86::BI__builtin_ia32_vcvtuw2ph512_mask:
  case X86::BI__builtin_ia32_vcvtudq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtuqq2ph512_mask:
    return EmitX86ConvertIntToFp(*this, E, Ops, /*IsSigned*/ false);

  case X86::BI__builtin_ia32_vfmaddss3:
  case X86::BI__builtin_ia32_vfmaddsd3:
  case X86::BI__builtin_ia32_vfmaddsh3_mask:
  case X86::BI__builtin_ia32_vfmaddss3_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_mask:
    return EmitScalarFMAExpr(*this, E, Ops, Ops[0]);
  case X86::BI__builtin_ia32_vfmaddss:
  case X86::BI__builtin_ia32_vfmaddsd:
    return EmitScalarFMAExpr(*this, E, Ops,
                             Constant::getNullValue(Ops[0]->getType()));
  case X86::BI__builtin_ia32_vfmaddsh3_maskz:
  case X86::BI__builtin_ia32_vfmaddss3_maskz:
  case X86::BI__builtin_ia32_vfmaddsd3_maskz:
    return EmitScalarFMAExpr(*this, E, Ops, Ops[0], /*ZeroMask*/ true);
  case X86::BI__builtin_ia32_vfmaddsh3_mask3:
  case X86::BI__builtin_ia32_vfmaddss3_mask3:
  case X86::BI__builtin_ia32_vfmaddsd3_mask3:
    return EmitScalarFMAExpr(*this, E, Ops, Ops[2], /*ZeroMask*/ false, 2);
  case X86::BI__builtin_ia32_vfmsubsh3_mask3:
  case X86::BI__builtin_ia32_vfmsubss3_mask3:
  case X86::BI__builtin_ia32_vfmsubsd3_mask3:
    return EmitScalarFMAExpr(*this, E, Ops, Ops[2], /*ZeroMask*/ false, 2,
                             /*NegAcc*/ true);
  case X86::BI__builtin_ia32_vfmaddph:
  case X86::BI__builtin_ia32_vfmaddps:
  case X86::BI__builtin_ia32_vfmaddpd:
  case X86::BI__builtin_ia32_vfmaddph256:
  case X86::BI__builtin_ia32_vfmaddps256:
  case X86::BI__builtin_ia32_vfmaddpd256:
  case X86::BI__builtin_ia32_vfmaddph512_mask:
  case X86::BI__builtin_ia32_vfmaddph512_maskz:
  case X86::BI__builtin_ia32_vfmaddph512_mask3:
  case X86::BI__builtin_ia32_vfmaddps512_mask:
  case X86::BI__builtin_ia32_vfmaddps512_maskz:
  case X86::BI__builtin_ia32_vfmaddps512_mask3:
  case X86::BI__builtin_ia32_vfmsubps512_mask3:
  case X86::BI__builtin_ia32_vfmaddpd512_mask:
  case X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubph512_mask3:
    return EmitX86FMAExpr(*this, E, Ops, BuiltinID, /*IsAddSub*/ false);
  case X86::BI__builtin_ia32_vfmaddsubph512_mask:
  case X86::BI__builtin_ia32_vfmaddsubph512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddph512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddps512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
    return EmitX86FMAExpr(*this, E, Ops, BuiltinID, /*IsAddSub*/ true);

  case X86::BI__builtin_ia32_movdqa32store128_mask:
  case X86::BI__builtin_ia32_movdqa64store128_mask:
  case X86::BI__builtin_ia32_storeaps128_mask:
  case X86::BI__builtin_ia32_storeapd128_mask:
  case X86::BI__builtin_ia32_movdqa32store256_mask:
  case X86::BI__builtin_ia32_movdqa64store256_mask:
  case X86::BI__builtin_ia32_storeaps256_mask:
  case X86::BI__builtin_ia32_storeapd256_mask:
  case X86::BI__builtin_ia32_movdqa32store512_mask:
  case X86::BI__builtin_ia32_movdqa64store512_mask:
  case X86::BI__builtin_ia32_storeaps512_mask:
  case X86::BI__builtin_ia32_storeapd512_mask:
    return EmitX86MaskedStore(
        *this, Ops,
        getContext().getTypeAlignInChars(E->getArg(1)->getType()).getAsAlign());

  case X86::BI__builtin_ia32_loadups128_mask:
  case X86::BI__builtin_ia32_loadups256_mask:
  case X86::BI__builtin_ia32_loadups512_mask:
  case X86::BI__builtin_ia32_loadupd128_mask:
  case X86::BI__builtin_ia32_loadupd256_mask:
  case X86::BI__builtin_ia32_loadupd512_mask:
  case X86::BI__builtin_ia32_loaddquqi128_mask:
  case X86::BI__builtin_ia32_loaddquqi256_mask:
  case X86::BI__builtin_ia32_loaddquqi512_mask:
  case X86::BI__builtin_ia32_loaddquhi128_mask:
  case X86::BI__builtin_ia32_loaddquhi256_mask:
  case X86::BI__builtin_ia32_loaddquhi512_mask:
  case X86::BI__builtin_ia32_loaddqusi128_mask:
  case X86::BI__builtin_ia32_loaddqusi256_mask:
  case X86::BI__builtin_ia32_loaddqusi512_mask:
  case X86::BI__builtin_ia32_loaddqudi128_mask:
  case X86::BI__builtin_ia32_loaddqudi256_mask:
  case X86::BI__builtin_ia32_loaddqudi512_mask:
    return EmitX86MaskedLoad(*this, Ops, Align(1));

  case X86::BI__builtin_ia32_loadsh128_mask:
  case X86::BI__builtin_ia32_loadss128_mask:
  case X86::BI__builtin_ia32_loadsd128_mask:
    return EmitX86MaskedLoad(*this, Ops, Align(1));

  case X86::BI__builtin_ia32_loadaps128_mask:
  case X86::BI__builtin_ia32_loadaps256_mask:
  case X86::BI__builtin_ia32_loadaps512_mask:
  case X86::BI__builtin_ia32_loadapd128_mask:
  case X86::BI__builtin_ia32_loadapd256_mask:
  case X86::BI__builtin_ia32_loadapd512_mask:
  case X86::BI__builtin_ia32_movdqa32load128_mask:
  case X86::BI__builtin_ia32_movdqa32load256_mask:
  case X86::BI__builtin_ia32_movdqa32load512_mask:
  case X86::BI__builtin_ia32_movdqa64load128_mask:
  case X86::BI__builtin_ia32_movdqa64load256_mask:
  case X86::BI__builtin_ia32_movdqa64load512_mask:
    return EmitX86MaskedLoad(
        *this, Ops,
        getContext().getTypeAlignInChars(E->getArg(1)->getType()).getAsAlign());

  case X86::BI__builtin_ia32_expandloaddf128_mask:
  case X86::BI__builtin_ia32_expandloaddf256_mask:
  case X86::BI__builtin_ia32_expandloaddf512_mask:
  case X86::BI__builtin_ia32_expandloadsf128_mask:
  case X86::BI__builtin_ia32_expandloadsf256_mask:
  case X86::BI__builtin_ia32_expandloadsf512_mask:
  case X86::BI__builtin_ia32_expandloaddi128_mask:
  case X86::BI__builtin_ia32_expandloaddi256_mask:
  case X86::BI__builtin_ia32_expandloaddi512_mask:
  case X86::BI__builtin_ia32_expandloadsi128_mask:
  case X86::BI__builtin_ia32_expandloadsi256_mask:
  case X86::BI__builtin_ia32_expandloadsi512_mask:
  case X86::BI__builtin_ia32_expandloadhi128_mask:
  case X86::BI__builtin_ia32_expandloadhi256_mask:
  case X86::BI__builtin_ia32_expandloadhi512_mask:
  case X86::BI__builtin_ia32_expandloadqi128_mask:
  case X86::BI__builtin_ia32_expandloadqi256_mask:
  case X86::BI__builtin_ia32_expandloadqi512_mask:
    return EmitX86ExpandLoad(*this, Ops);

  case X86::BI__builtin_ia32_compressstoredf128_mask:
  case X86::BI__builtin_ia32_compressstoredf256_mask:
  case X86::BI__builtin_ia32_compressstoredf512_mask:
  case X86::BI__builtin_ia32_compressstoresf128_mask:
  case X86::BI__builtin_ia32_compressstoresf256_mask:
  case X86::BI__builtin_ia32_compressstoresf512_mask:
  case X86::BI__builtin_ia32_compressstoredi128_mask:
  case X86::BI__builtin_ia32_compressstoredi256_mask:
  case X86::BI__builtin_ia32_compressstoredi512_mask:
  case X86::BI__builtin_ia32_compressstoresi128_mask:
  case X86::BI__builtin_ia32_compressstoresi256_mask:
  case X86::BI__builtin_ia32_compressstoresi512_mask:
  case X86::BI__builtin_ia32_compressstorehi128_mask:
  case X86::BI__builtin_ia32_compressstorehi256_mask:
  case X86::BI__builtin_ia32_compressstorehi512_mask:
  case X86::BI__builtin_ia32_compressstoreqi128_mask:
  case X86::BI__builtin_ia32_compressstoreqi256_mask:
  case X86::BI__builtin_ia32_compressstoreqi512_mask:
    return EmitX86CompressStore(*this, Ops);

  case X86::BI__builtin_ia32_expanddf128_mask:
  case X86::BI__builtin_ia32_expanddf256_mask:
  case X86::BI__builtin_ia32_expanddf512_mask:
  case X86::BI__builtin_ia32_expandsf128_mask:
  case X86::BI__builtin_ia32_expandsf256_mask:
  case X86::BI__builtin_ia32_expandsf512_mask:
  case X86::BI__builtin_ia32_expanddi128_mask:
  case X86::BI__builtin_ia32_expanddi256_mask:
  case X86::BI__builtin_ia32_expanddi512_mask:
  case X86::BI__builtin_ia32_expandsi128_mask:
  case X86::BI__builtin_ia32_expandsi256_mask:
  case X86::BI__builtin_ia32_expandsi512_mask:
  case X86::BI__builtin_ia32_expandhi128_mask:
  case X86::BI__builtin_ia32_expandhi256_mask:
  case X86::BI__builtin_ia32_expandhi512_mask:
  case X86::BI__builtin_ia32_expandqi128_mask:
  case X86::BI__builtin_ia32_expandqi256_mask:
  case X86::BI__builtin_ia32_expandqi512_mask:
    return EmitX86CompressExpand(*this, Ops, /*IsCompress*/false);

  case X86::BI__builtin_ia32_compressdf128_mask:
  case X86::BI__builtin_ia32_compressdf256_mask:
  case X86::BI__builtin_ia32_compressdf512_mask:
  case X86::BI__builtin_ia32_compresssf128_mask:
  case X86::BI__builtin_ia32_compresssf256_mask:
  case X86::BI__builtin_ia32_compresssf512_mask:
  case X86::BI__builtin_ia32_compressdi128_mask:
  case X86::BI__builtin_ia32_compressdi256_mask:
  case X86::BI__builtin_ia32_compressdi512_mask:
  case X86::BI__builtin_ia32_compresssi128_mask:
  case X86::BI__builtin_ia32_compresssi256_mask:
  case X86::BI__builtin_ia32_compresssi512_mask:
  case X86::BI__builtin_ia32_compresshi128_mask:
  case X86::BI__builtin_ia32_compresshi256_mask:
  case X86::BI__builtin_ia32_compresshi512_mask:
  case X86::BI__builtin_ia32_compressqi128_mask:
  case X86::BI__builtin_ia32_compressqi256_mask:
  case X86::BI__builtin_ia32_compressqi512_mask:
    return EmitX86CompressExpand(*this, Ops, /*IsCompress*/true);

  case X86::BI__builtin_ia32_gather3div2df:
  case X86::BI__builtin_ia32_gather3div2di:
  case X86::BI__builtin_ia32_gather3div4df:
  case X86::BI__builtin_ia32_gather3div4di:
  case X86::BI__builtin_ia32_gather3div4sf:
  case X86::BI__builtin_ia32_gather3div4si:
  case X86::BI__builtin_ia32_gather3div8sf:
  case X86::BI__builtin_ia32_gather3div8si:
  case X86::BI__builtin_ia32_gather3siv2df:
  case X86::BI__builtin_ia32_gather3siv2di:
  case X86::BI__builtin_ia32_gather3siv4df:
  case X86::BI__builtin_ia32_gather3siv4di:
  case X86::BI__builtin_ia32_gather3siv4sf:
  case X86::BI__builtin_ia32_gather3siv4si:
  case X86::BI__builtin_ia32_gather3siv8sf:
  case X86::BI__builtin_ia32_gather3siv8si:
  case X86::BI__builtin_ia32_gathersiv8df:
  case X86::BI__builtin_ia32_gathersiv16sf:
  case X86::BI__builtin_ia32_gatherdiv8df:
  case X86::BI__builtin_ia32_gatherdiv16sf:
  case X86::BI__builtin_ia32_gathersiv8di:
  case X86::BI__builtin_ia32_gathersiv16si:
  case X86::BI__builtin_ia32_gatherdiv8di:
  case X86::BI__builtin_ia32_gatherdiv16si: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_gather3div2df:
      IID = Intrinsic::x86_avx512_mask_gather3div2_df;
      break;
    case X86::BI__builtin_ia32_gather3div2di:
      IID = Intrinsic::x86_avx512_mask_gather3div2_di;
      break;
    case X86::BI__builtin_ia32_gather3div4df:
      IID = Intrinsic::x86_avx512_mask_gather3div4_df;
      break;
    case X86::BI__builtin_ia32_gather3div4di:
      IID = Intrinsic::x86_avx512_mask_gather3div4_di;
      break;
    case X86::BI__builtin_ia32_gather3div4sf:
      IID = Intrinsic::x86_avx512_mask_gather3div4_sf;
      break;
    case X86::BI__builtin_ia32_gather3div4si:
      IID = Intrinsic::x86_avx512_mask_gather3div4_si;
      break;
    case X86::BI__builtin_ia32_gather3div8sf:
      IID = Intrinsic::x86_avx512_mask_gather3div8_sf;
      break;
    case X86::BI__builtin_ia32_gather3div8si:
      IID = Intrinsic::x86_avx512_mask_gather3div8_si;
      break;
    case X86::BI__builtin_ia32_gather3siv2df:
      IID = Intrinsic::x86_avx512_mask_gather3siv2_df;
      break;
    case X86::BI__builtin_ia32_gather3siv2di:
      IID = Intrinsic::x86_avx512_mask_gather3siv2_di;
      break;
    case X86::BI__builtin_ia32_gather3siv4df:
      IID = Intrinsic::x86_avx512_mask_gather3siv4_df;
      break;
    case X86::BI__builtin_ia32_gather3siv4di:
      IID = Intrinsic::x86_avx512_mask_gather3siv4_di;
      break;
    case X86::BI__builtin_ia32_gather3siv4sf:
      IID = Intrinsic::x86_avx512_mask_gather3siv4_sf;
      break;
    case X86::BI__builtin_ia32_gather3siv4si:
      IID = Intrinsic::x86_avx512_mask_gather3siv4_si;
      break;
    case X86::BI__builtin_ia32_gather3siv8sf:
      IID = Intrinsic::x86_avx512_mask_gather3siv8_sf;
      break;
    case X86::BI__builtin_ia32_gather3siv8si:
      IID = Intrinsic::x86_avx512_mask_gather3siv8_si;
      break;
    case X86::BI__builtin_ia32_gathersiv8df:
      IID = Intrinsic::x86_avx512_mask_gather_dpd_512;
      break;
    case X86::BI__builtin_ia32_gathersiv16sf:
      IID = Intrinsic::x86_avx512_mask_gather_dps_512;
      break;
    case X86::BI__builtin_ia32_gatherdiv8df:
      IID = Intrinsic::x86_avx512_mask_gather_qpd_512;
      break;
    case X86::BI__builtin_ia32_gatherdiv16sf:
      IID = Intrinsic::x86_avx512_mask_gather_qps_512;
      break;
    case X86::BI__builtin_ia32_gathersiv8di:
      IID = Intrinsic::x86_avx512_mask_gather_dpq_512;
      break;
    case X86::BI__builtin_ia32_gathersiv16si:
      IID = Intrinsic::x86_avx512_mask_gather_dpi_512;
      break;
    case X86::BI__builtin_ia32_gatherdiv8di:
      IID = Intrinsic::x86_avx512_mask_gather_qpq_512;
      break;
    case X86::BI__builtin_ia32_gatherdiv16si:
      IID = Intrinsic::x86_avx512_mask_gather_qpi_512;
      break;
    }

    unsigned MinElts = std::min(
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements(),
        cast<llvm::FixedVectorType>(Ops[2]->getType())->getNumElements());
    Ops[3] = getMaskVecValue(*this, Ops[3], MinElts);
    Function *Intr = CGM.getIntrinsic(IID);
    return Builder.CreateCall(Intr, Ops);
  }

  case X86::BI__builtin_ia32_scattersiv8df:
  case X86::BI__builtin_ia32_scattersiv16sf:
  case X86::BI__builtin_ia32_scatterdiv8df:
  case X86::BI__builtin_ia32_scatterdiv16sf:
  case X86::BI__builtin_ia32_scattersiv8di:
  case X86::BI__builtin_ia32_scattersiv16si:
  case X86::BI__builtin_ia32_scatterdiv8di:
  case X86::BI__builtin_ia32_scatterdiv16si:
  case X86::BI__builtin_ia32_scatterdiv2df:
  case X86::BI__builtin_ia32_scatterdiv2di:
  case X86::BI__builtin_ia32_scatterdiv4df:
  case X86::BI__builtin_ia32_scatterdiv4di:
  case X86::BI__builtin_ia32_scatterdiv4sf:
  case X86::BI__builtin_ia32_scatterdiv4si:
  case X86::BI__builtin_ia32_scatterdiv8sf:
  case X86::BI__builtin_ia32_scatterdiv8si:
  case X86::BI__builtin_ia32_scattersiv2df:
  case X86::BI__builtin_ia32_scattersiv2di:
  case X86::BI__builtin_ia32_scattersiv4df:
  case X86::BI__builtin_ia32_scattersiv4di:
  case X86::BI__builtin_ia32_scattersiv4sf:
  case X86::BI__builtin_ia32_scattersiv4si:
  case X86::BI__builtin_ia32_scattersiv8sf:
  case X86::BI__builtin_ia32_scattersiv8si: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_scattersiv8df:
      IID = Intrinsic::x86_avx512_mask_scatter_dpd_512;
      break;
    case X86::BI__builtin_ia32_scattersiv16sf:
      IID = Intrinsic::x86_avx512_mask_scatter_dps_512;
      break;
    case X86::BI__builtin_ia32_scatterdiv8df:
      IID = Intrinsic::x86_avx512_mask_scatter_qpd_512;
      break;
    case X86::BI__builtin_ia32_scatterdiv16sf:
      IID = Intrinsic::x86_avx512_mask_scatter_qps_512;
      break;
    case X86::BI__builtin_ia32_scattersiv8di:
      IID = Intrinsic::x86_avx512_mask_scatter_dpq_512;
      break;
    case X86::BI__builtin_ia32_scattersiv16si:
      IID = Intrinsic::x86_avx512_mask_scatter_dpi_512;
      break;
    case X86::BI__builtin_ia32_scatterdiv8di:
      IID = Intrinsic::x86_avx512_mask_scatter_qpq_512;
      break;
    case X86::BI__builtin_ia32_scatterdiv16si:
      IID = Intrinsic::x86_avx512_mask_scatter_qpi_512;
      break;
    case X86::BI__builtin_ia32_scatterdiv2df:
      IID = Intrinsic::x86_avx512_mask_scatterdiv2_df;
      break;
    case X86::BI__builtin_ia32_scatterdiv2di:
      IID = Intrinsic::x86_avx512_mask_scatterdiv2_di;
      break;
    case X86::BI__builtin_ia32_scatterdiv4df:
      IID = Intrinsic::x86_avx512_mask_scatterdiv4_df;
      break;
    case X86::BI__builtin_ia32_scatterdiv4di:
      IID = Intrinsic::x86_avx512_mask_scatterdiv4_di;
      break;
    case X86::BI__builtin_ia32_scatterdiv4sf:
      IID = Intrinsic::x86_avx512_mask_scatterdiv4_sf;
      break;
    case X86::BI__builtin_ia32_scatterdiv4si:
      IID = Intrinsic::x86_avx512_mask_scatterdiv4_si;
      break;
    case X86::BI__builtin_ia32_scatterdiv8sf:
      IID = Intrinsic::x86_avx512_mask_scatterdiv8_sf;
      break;
    case X86::BI__builtin_ia32_scatterdiv8si:
      IID = Intrinsic::x86_avx512_mask_scatterdiv8_si;
      break;
    case X86::BI__builtin_ia32_scattersiv2df:
      IID = Intrinsic::x86_avx512_mask_scattersiv2_df;
      break;
    case X86::BI__builtin_ia32_scattersiv2di:
      IID = Intrinsic::x86_avx512_mask_scattersiv2_di;
      break;
    case X86::BI__builtin_ia32_scattersiv4df:
      IID = Intrinsic::x86_avx512_mask_scattersiv4_df;
      break;
    case X86::BI__builtin_ia32_scattersiv4di:
      IID = Intrinsic::x86_avx512_mask_scattersiv4_di;
      break;
    case X86::BI__builtin_ia32_scattersiv4sf:
      IID = Intrinsic::x86_avx512_mask_scattersiv4_sf;
      break;
    case X86::BI__builtin_ia32_scattersiv4si:
      IID = Intrinsic::x86_avx512_mask_scattersiv4_si;
      break;
    case X86::BI__builtin_ia32_scattersiv8sf:
      IID = Intrinsic::x86_avx512_mask_scattersiv8_sf;
      break;
    case X86::BI__builtin_ia32_scattersiv8si:
      IID = Intrinsic::x86_avx512_mask_scattersiv8_si;
      break;
    }

    unsigned MinElts = std::min(
        cast<llvm::FixedVectorType>(Ops[2]->getType())->getNumElements(),
        cast<llvm::FixedVectorType>(Ops[3]->getType())->getNumElements());
    Ops[1] = getMaskVecValue(*this, Ops[1], MinElts);
    Function *Intr = CGM.getIntrinsic(IID);
    return Builder.CreateCall(Intr, Ops);
  }

  case X86::BI__builtin_ia32_vextractf128_pd256:
  case X86::BI__builtin_ia32_vextractf128_ps256:
  case X86::BI__builtin_ia32_vextractf128_si256:
  case X86::BI__builtin_ia32_extract128i256:
  case X86::BI__builtin_ia32_extractf64x4_mask:
  case X86::BI__builtin_ia32_extractf32x4_mask:
  case X86::BI__builtin_ia32_extracti64x4_mask:
  case X86::BI__builtin_ia32_extracti32x4_mask:
  case X86::BI__builtin_ia32_extractf32x8_mask:
  case X86::BI__builtin_ia32_extracti32x8_mask:
  case X86::BI__builtin_ia32_extractf32x4_256_mask:
  case X86::BI__builtin_ia32_extracti32x4_256_mask:
  case X86::BI__builtin_ia32_extractf64x2_256_mask:
  case X86::BI__builtin_ia32_extracti64x2_256_mask:
  case X86::BI__builtin_ia32_extractf64x2_512_mask:
  case X86::BI__builtin_ia32_extracti64x2_512_mask: {
    auto *DstTy = cast<llvm::FixedVectorType>(ConvertType(E->getType()));
    unsigned NumElts = DstTy->getNumElements();
    unsigned SrcNumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    unsigned SubVectors = SrcNumElts / NumElts;
    unsigned Index = cast<ConstantInt>(Ops[1])->getZExtValue();
    assert(llvm::isPowerOf2_32(SubVectors) && "Expected power of 2 subvectors");
    Index &= SubVectors - 1; // Remove any extra bits.
    Index *= NumElts;

    int Indices[16];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i + Index;

    Value *Res = Builder.CreateShuffleVector(Ops[0], ArrayRef(Indices, NumElts),
                                             "extract");

    if (Ops.size() == 4)
      Res = EmitX86Select(*this, Ops[3], Res, Ops[2]);

    return Res;
  }
  case X86::BI__builtin_ia32_vinsertf128_pd256:
  case X86::BI__builtin_ia32_vinsertf128_ps256:
  case X86::BI__builtin_ia32_vinsertf128_si256:
  case X86::BI__builtin_ia32_insert128i256:
  case X86::BI__builtin_ia32_insertf64x4:
  case X86::BI__builtin_ia32_insertf32x4:
  case X86::BI__builtin_ia32_inserti64x4:
  case X86::BI__builtin_ia32_inserti32x4:
  case X86::BI__builtin_ia32_insertf32x8:
  case X86::BI__builtin_ia32_inserti32x8:
  case X86::BI__builtin_ia32_insertf32x4_256:
  case X86::BI__builtin_ia32_inserti32x4_256:
  case X86::BI__builtin_ia32_insertf64x2_256:
  case X86::BI__builtin_ia32_inserti64x2_256:
  case X86::BI__builtin_ia32_insertf64x2_512:
  case X86::BI__builtin_ia32_inserti64x2_512: {
    unsigned DstNumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    unsigned SrcNumElts =
        cast<llvm::FixedVectorType>(Ops[1]->getType())->getNumElements();
    unsigned SubVectors = DstNumElts / SrcNumElts;
    unsigned Index = cast<ConstantInt>(Ops[2])->getZExtValue();
    assert(llvm::isPowerOf2_32(SubVectors) && "Expected power of 2 subvectors");
    Index &= SubVectors - 1; // Remove any extra bits.
    Index *= SrcNumElts;

    int Indices[16];
    for (unsigned i = 0; i != DstNumElts; ++i)
      Indices[i] = (i >= SrcNumElts) ? SrcNumElts + (i % SrcNumElts) : i;

    Value *Op1 = Builder.CreateShuffleVector(
        Ops[1], ArrayRef(Indices, DstNumElts), "widen");

    for (unsigned i = 0; i != DstNumElts; ++i) {
      if (i >= Index && i < (Index + SrcNumElts))
        Indices[i] = (i - Index) + DstNumElts;
      else
        Indices[i] = i;
    }

    return Builder.CreateShuffleVector(Ops[0], Op1,
                                       ArrayRef(Indices, DstNumElts), "insert");
  }
  case X86::BI__builtin_ia32_pmovqd512_mask:
  case X86::BI__builtin_ia32_pmovwb512_mask: {
    Value *Res = Builder.CreateTrunc(Ops[0], Ops[1]->getType());
    return EmitX86Select(*this, Ops[2], Res, Ops[1]);
  }
  case X86::BI__builtin_ia32_pmovdb512_mask:
  case X86::BI__builtin_ia32_pmovdw512_mask:
  case X86::BI__builtin_ia32_pmovqw512_mask: {
    if (const auto *C = dyn_cast<Constant>(Ops[2]))
      if (C->isAllOnesValue())
        return Builder.CreateTrunc(Ops[0], Ops[1]->getType());

    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_pmovdb512_mask:
      IID = Intrinsic::x86_avx512_mask_pmov_db_512;
      break;
    case X86::BI__builtin_ia32_pmovdw512_mask:
      IID = Intrinsic::x86_avx512_mask_pmov_dw_512;
      break;
    case X86::BI__builtin_ia32_pmovqw512_mask:
      IID = Intrinsic::x86_avx512_mask_pmov_qw_512;
      break;
    }

    Function *Intr = CGM.getIntrinsic(IID);
    return Builder.CreateCall(Intr, Ops);
  }
  case X86::BI__builtin_ia32_pblendw128:
  case X86::BI__builtin_ia32_blendpd:
  case X86::BI__builtin_ia32_blendps:
  case X86::BI__builtin_ia32_blendpd256:
  case X86::BI__builtin_ia32_blendps256:
  case X86::BI__builtin_ia32_pblendw256:
  case X86::BI__builtin_ia32_pblendd128:
  case X86::BI__builtin_ia32_pblendd256: {
    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    unsigned Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();

    int Indices[16];
    // If there are more than 8 elements, the immediate is used twice so make
    // sure we handle that.
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = ((Imm >> (i % 8)) & 0x1) ? NumElts + i : i;

    return Builder.CreateShuffleVector(Ops[0], Ops[1],
                                       ArrayRef(Indices, NumElts), "blend");
  }
  case X86::BI__builtin_ia32_pshuflw:
  case X86::BI__builtin_ia32_pshuflw256:
  case X86::BI__builtin_ia32_pshuflw512: {
    uint32_t Imm = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
    auto *Ty = cast<llvm::FixedVectorType>(Ops[0]->getType());
    unsigned NumElts = Ty->getNumElements();

    // Splat the 8-bits of immediate 4 times to help the loop wrap around.
    Imm = (Imm & 0xff) * 0x01010101;

    int Indices[32];
    for (unsigned l = 0; l != NumElts; l += 8) {
      for (unsigned i = 0; i != 4; ++i) {
        Indices[l + i] = l + (Imm & 3);
        Imm >>= 2;
      }
      for (unsigned i = 4; i != 8; ++i)
        Indices[l + i] = l + i;
    }

    return Builder.CreateShuffleVector(Ops[0], ArrayRef(Indices, NumElts),
                                       "pshuflw");
  }
  case X86::BI__builtin_ia32_pshufhw:
  case X86::BI__builtin_ia32_pshufhw256:
  case X86::BI__builtin_ia32_pshufhw512: {
    uint32_t Imm = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
    auto *Ty = cast<llvm::FixedVectorType>(Ops[0]->getType());
    unsigned NumElts = Ty->getNumElements();

    // Splat the 8-bits of immediate 4 times to help the loop wrap around.
    Imm = (Imm & 0xff) * 0x01010101;

    int Indices[32];
    for (unsigned l = 0; l != NumElts; l += 8) {
      for (unsigned i = 0; i != 4; ++i)
        Indices[l + i] = l + i;
      for (unsigned i = 4; i != 8; ++i) {
        Indices[l + i] = l + 4 + (Imm & 3);
        Imm >>= 2;
      }
    }

    return Builder.CreateShuffleVector(Ops[0], ArrayRef(Indices, NumElts),
                                       "pshufhw");
  }
  case X86::BI__builtin_ia32_pshufd:
  case X86::BI__builtin_ia32_pshufd256:
  case X86::BI__builtin_ia32_pshufd512:
  case X86::BI__builtin_ia32_vpermilpd:
  case X86::BI__builtin_ia32_vpermilps:
  case X86::BI__builtin_ia32_vpermilpd256:
  case X86::BI__builtin_ia32_vpermilps256:
  case X86::BI__builtin_ia32_vpermilpd512:
  case X86::BI__builtin_ia32_vpermilps512: {
    uint32_t Imm = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
    auto *Ty = cast<llvm::FixedVectorType>(Ops[0]->getType());
    unsigned NumElts = Ty->getNumElements();
    unsigned NumLanes = Ty->getPrimitiveSizeInBits() / 128;
    unsigned NumLaneElts = NumElts / NumLanes;

    // Splat the 8-bits of immediate 4 times to help the loop wrap around.
    Imm = (Imm & 0xff) * 0x01010101;

    int Indices[16];
    for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
      for (unsigned i = 0; i != NumLaneElts; ++i) {
        Indices[i + l] = (Imm % NumLaneElts) + l;
        Imm /= NumLaneElts;
      }
    }

    return Builder.CreateShuffleVector(Ops[0], ArrayRef(Indices, NumElts),
                                       "permil");
  }
  case X86::BI__builtin_ia32_shufpd:
  case X86::BI__builtin_ia32_shufpd256:
  case X86::BI__builtin_ia32_shufpd512:
  case X86::BI__builtin_ia32_shufps:
  case X86::BI__builtin_ia32_shufps256:
  case X86::BI__builtin_ia32_shufps512: {
    uint32_t Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();
    auto *Ty = cast<llvm::FixedVectorType>(Ops[0]->getType());
    unsigned NumElts = Ty->getNumElements();
    unsigned NumLanes = Ty->getPrimitiveSizeInBits() / 128;
    unsigned NumLaneElts = NumElts / NumLanes;

    // Splat the 8-bits of immediate 4 times to help the loop wrap around.
    Imm = (Imm & 0xff) * 0x01010101;

    int Indices[16];
    for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
      for (unsigned i = 0; i != NumLaneElts; ++i) {
        unsigned Index = Imm % NumLaneElts;
        Imm /= NumLaneElts;
        if (i >= (NumLaneElts / 2))
          Index += NumElts;
        Indices[l + i] = l + Index;
      }
    }

    return Builder.CreateShuffleVector(Ops[0], Ops[1],
                                       ArrayRef(Indices, NumElts), "shufp");
  }
  case X86::BI__builtin_ia32_permdi256:
  case X86::BI__builtin_ia32_permdf256:
  case X86::BI__builtin_ia32_permdi512:
  case X86::BI__builtin_ia32_permdf512: {
    unsigned Imm = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
    auto *Ty = cast<llvm::FixedVectorType>(Ops[0]->getType());
    unsigned NumElts = Ty->getNumElements();

    // These intrinsics operate on 256-bit lanes of four 64-bit elements.
    int Indices[8];
    for (unsigned l = 0; l != NumElts; l += 4)
      for (unsigned i = 0; i != 4; ++i)
        Indices[l + i] = l + ((Imm >> (2 * i)) & 0x3);

    return Builder.CreateShuffleVector(Ops[0], ArrayRef(Indices, NumElts),
                                       "perm");
  }
  case X86::BI__builtin_ia32_palignr128:
  case X86::BI__builtin_ia32_palignr256:
  case X86::BI__builtin_ia32_palignr512: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0xff;

    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    assert(NumElts % 16 == 0);

    // If palignr is shifting the pair of vectors more than the size of two
    // lanes, emit zero.
    if (ShiftVal >= 32)
      return llvm::Constant::getNullValue(ConvertType(E->getType()));

    // If palignr is shifting the pair of input vectors more than one lane,
    // but less than two lanes, convert to shifting in zeroes.
    if (ShiftVal > 16) {
      ShiftVal -= 16;
      Ops[1] = Ops[0];
      Ops[0] = llvm::Constant::getNullValue(Ops[0]->getType());
    }

    int Indices[64];
    // 256-bit palignr operates on 128-bit lanes so we need to handle that
    for (unsigned l = 0; l != NumElts; l += 16) {
      for (unsigned i = 0; i != 16; ++i) {
        unsigned Idx = ShiftVal + i;
        if (Idx >= 16)
          Idx += NumElts - 16; // End of lane, switch operand.
        Indices[l + i] = Idx + l;
      }
    }

    return Builder.CreateShuffleVector(Ops[1], Ops[0],
                                       ArrayRef(Indices, NumElts), "palignr");
  }
  case X86::BI__builtin_ia32_alignd128:
  case X86::BI__builtin_ia32_alignd256:
  case X86::BI__builtin_ia32_alignd512:
  case X86::BI__builtin_ia32_alignq128:
  case X86::BI__builtin_ia32_alignq256:
  case X86::BI__builtin_ia32_alignq512: {
    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0xff;

    // Mask the shift amount to width of a vector.
    ShiftVal &= NumElts - 1;

    int Indices[16];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i + ShiftVal;

    return Builder.CreateShuffleVector(Ops[1], Ops[0],
                                       ArrayRef(Indices, NumElts), "valign");
  }
  case X86::BI__builtin_ia32_shuf_f32x4_256:
  case X86::BI__builtin_ia32_shuf_f64x2_256:
  case X86::BI__builtin_ia32_shuf_i32x4_256:
  case X86::BI__builtin_ia32_shuf_i64x2_256:
  case X86::BI__builtin_ia32_shuf_f32x4:
  case X86::BI__builtin_ia32_shuf_f64x2:
  case X86::BI__builtin_ia32_shuf_i32x4:
  case X86::BI__builtin_ia32_shuf_i64x2: {
    unsigned Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();
    auto *Ty = cast<llvm::FixedVectorType>(Ops[0]->getType());
    unsigned NumElts = Ty->getNumElements();
    unsigned NumLanes = Ty->getPrimitiveSizeInBits() == 512 ? 4 : 2;
    unsigned NumLaneElts = NumElts / NumLanes;

    int Indices[16];
    for (unsigned l = 0; l != NumElts; l += NumLaneElts) {
      unsigned Index = (Imm % NumLanes) * NumLaneElts;
      Imm /= NumLanes; // Discard the bits we just used.
      if (l >= (NumElts / 2))
        Index += NumElts; // Switch to other source.
      for (unsigned i = 0; i != NumLaneElts; ++i) {
        Indices[l + i] = Index + i;
      }
    }

    return Builder.CreateShuffleVector(Ops[0], Ops[1],
                                       ArrayRef(Indices, NumElts), "shuf");
  }

  case X86::BI__builtin_ia32_vperm2f128_pd256:
  case X86::BI__builtin_ia32_vperm2f128_ps256:
  case X86::BI__builtin_ia32_vperm2f128_si256:
  case X86::BI__builtin_ia32_permti256: {
    unsigned Imm = cast<llvm::ConstantInt>(Ops[2])->getZExtValue();
    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();

    // This takes a very simple approach since there are two lanes and a
    // shuffle can have 2 inputs. So we reserve the first input for the first
    // lane and the second input for the second lane. This may result in
    // duplicate sources, but this can be dealt with in the backend.

    Value *OutOps[2];
    int Indices[8];
    for (unsigned l = 0; l != 2; ++l) {
      // Determine the source for this lane.
      if (Imm & (1 << ((l * 4) + 3)))
        OutOps[l] = llvm::ConstantAggregateZero::get(Ops[0]->getType());
      else if (Imm & (1 << ((l * 4) + 1)))
        OutOps[l] = Ops[1];
      else
        OutOps[l] = Ops[0];

      for (unsigned i = 0; i != NumElts/2; ++i) {
        // Start with ith element of the source for this lane.
        unsigned Idx = (l * NumElts) + i;
        // If bit 0 of the immediate half is set, switch to the high half of
        // the source.
        if (Imm & (1 << (l * 4)))
          Idx += NumElts/2;
        Indices[(l * (NumElts/2)) + i] = Idx;
      }
    }

    return Builder.CreateShuffleVector(OutOps[0], OutOps[1],
                                       ArrayRef(Indices, NumElts), "vperm");
  }

  case X86::BI__builtin_ia32_pslldqi128_byteshift:
  case X86::BI__builtin_ia32_pslldqi256_byteshift:
  case X86::BI__builtin_ia32_pslldqi512_byteshift: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[1])->getZExtValue() & 0xff;
    auto *ResultType = cast<llvm::FixedVectorType>(Ops[0]->getType());
    // Builtin type is vXi64 so multiply by 8 to get bytes.
    unsigned NumElts = ResultType->getNumElements() * 8;

    // If pslldq is shifting the vector more than 15 bytes, emit zero.
    if (ShiftVal >= 16)
      return llvm::Constant::getNullValue(ResultType);

    int Indices[64];
    // 256/512-bit pslldq operates on 128-bit lanes so we need to handle that
    for (unsigned l = 0; l != NumElts; l += 16) {
      for (unsigned i = 0; i != 16; ++i) {
        unsigned Idx = NumElts + i - ShiftVal;
        if (Idx < NumElts) Idx -= NumElts - 16; // end of lane, switch operand.
        Indices[l + i] = Idx + l;
      }
    }

    auto *VecTy = llvm::FixedVectorType::get(Int8Ty, NumElts);
    Value *Cast = Builder.CreateBitCast(Ops[0], VecTy, "cast");
    Value *Zero = llvm::Constant::getNullValue(VecTy);
    Value *SV = Builder.CreateShuffleVector(
        Zero, Cast, ArrayRef(Indices, NumElts), "pslldq");
    return Builder.CreateBitCast(SV, Ops[0]->getType(), "cast");
  }
  case X86::BI__builtin_ia32_psrldqi128_byteshift:
  case X86::BI__builtin_ia32_psrldqi256_byteshift:
  case X86::BI__builtin_ia32_psrldqi512_byteshift: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[1])->getZExtValue() & 0xff;
    auto *ResultType = cast<llvm::FixedVectorType>(Ops[0]->getType());
    // Builtin type is vXi64 so multiply by 8 to get bytes.
    unsigned NumElts = ResultType->getNumElements() * 8;

    // If psrldq is shifting the vector more than 15 bytes, emit zero.
    if (ShiftVal >= 16)
      return llvm::Constant::getNullValue(ResultType);

    int Indices[64];
    // 256/512-bit psrldq operates on 128-bit lanes so we need to handle that
    for (unsigned l = 0; l != NumElts; l += 16) {
      for (unsigned i = 0; i != 16; ++i) {
        unsigned Idx = i + ShiftVal;
        if (Idx >= 16) Idx += NumElts - 16; // end of lane, switch operand.
        Indices[l + i] = Idx + l;
      }
    }

    auto *VecTy = llvm::FixedVectorType::get(Int8Ty, NumElts);
    Value *Cast = Builder.CreateBitCast(Ops[0], VecTy, "cast");
    Value *Zero = llvm::Constant::getNullValue(VecTy);
    Value *SV = Builder.CreateShuffleVector(
        Cast, Zero, ArrayRef(Indices, NumElts), "psrldq");
    return Builder.CreateBitCast(SV, ResultType, "cast");
  }
  case X86::BI__builtin_ia32_kshiftliqi:
  case X86::BI__builtin_ia32_kshiftlihi:
  case X86::BI__builtin_ia32_kshiftlisi:
  case X86::BI__builtin_ia32_kshiftlidi: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[1])->getZExtValue() & 0xff;
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();

    if (ShiftVal >= NumElts)
      return llvm::Constant::getNullValue(Ops[0]->getType());

    Value *In = getMaskVecValue(*this, Ops[0], NumElts);

    int Indices[64];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = NumElts + i - ShiftVal;

    Value *Zero = llvm::Constant::getNullValue(In->getType());
    Value *SV = Builder.CreateShuffleVector(
        Zero, In, ArrayRef(Indices, NumElts), "kshiftl");
    return Builder.CreateBitCast(SV, Ops[0]->getType());
  }
  case X86::BI__builtin_ia32_kshiftriqi:
  case X86::BI__builtin_ia32_kshiftrihi:
  case X86::BI__builtin_ia32_kshiftrisi:
  case X86::BI__builtin_ia32_kshiftridi: {
    unsigned ShiftVal = cast<llvm::ConstantInt>(Ops[1])->getZExtValue() & 0xff;
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();

    if (ShiftVal >= NumElts)
      return llvm::Constant::getNullValue(Ops[0]->getType());

    Value *In = getMaskVecValue(*this, Ops[0], NumElts);

    int Indices[64];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i + ShiftVal;

    Value *Zero = llvm::Constant::getNullValue(In->getType());
    Value *SV = Builder.CreateShuffleVector(
        In, Zero, ArrayRef(Indices, NumElts), "kshiftr");
    return Builder.CreateBitCast(SV, Ops[0]->getType());
  }
  case X86::BI__builtin_ia32_movnti:
  case X86::BI__builtin_ia32_movnti64:
  case X86::BI__builtin_ia32_movntsd:
  case X86::BI__builtin_ia32_movntss: {
    llvm::MDNode *Node = llvm::MDNode::get(
        getLLVMContext(), llvm::ConstantAsMetadata::get(Builder.getInt32(1)));

    Value *Ptr = Ops[0];
    Value *Src = Ops[1];

    // Extract the 0'th element of the source vector.
    if (BuiltinID == X86::BI__builtin_ia32_movntsd ||
        BuiltinID == X86::BI__builtin_ia32_movntss)
      Src = Builder.CreateExtractElement(Src, (uint64_t)0, "extract");

    // Unaligned nontemporal store of the scalar value.
    StoreInst *SI = Builder.CreateDefaultAlignedStore(Src, Ptr);
    SI->setMetadata(llvm::LLVMContext::MD_nontemporal, Node);
    SI->setAlignment(llvm::Align(1));
    return SI;
  }
  // Rotate is a special case of funnel shift - 1st 2 args are the same.
  case X86::BI__builtin_ia32_vprotb:
  case X86::BI__builtin_ia32_vprotw:
  case X86::BI__builtin_ia32_vprotd:
  case X86::BI__builtin_ia32_vprotq:
  case X86::BI__builtin_ia32_vprotbi:
  case X86::BI__builtin_ia32_vprotwi:
  case X86::BI__builtin_ia32_vprotdi:
  case X86::BI__builtin_ia32_vprotqi:
  case X86::BI__builtin_ia32_prold128:
  case X86::BI__builtin_ia32_prold256:
  case X86::BI__builtin_ia32_prold512:
  case X86::BI__builtin_ia32_prolq128:
  case X86::BI__builtin_ia32_prolq256:
  case X86::BI__builtin_ia32_prolq512:
  case X86::BI__builtin_ia32_prolvd128:
  case X86::BI__builtin_ia32_prolvd256:
  case X86::BI__builtin_ia32_prolvd512:
  case X86::BI__builtin_ia32_prolvq128:
  case X86::BI__builtin_ia32_prolvq256:
  case X86::BI__builtin_ia32_prolvq512:
    return EmitX86FunnelShift(*this, Ops[0], Ops[0], Ops[1], false);
  case X86::BI__builtin_ia32_prord128:
  case X86::BI__builtin_ia32_prord256:
  case X86::BI__builtin_ia32_prord512:
  case X86::BI__builtin_ia32_prorq128:
  case X86::BI__builtin_ia32_prorq256:
  case X86::BI__builtin_ia32_prorq512:
  case X86::BI__builtin_ia32_prorvd128:
  case X86::BI__builtin_ia32_prorvd256:
  case X86::BI__builtin_ia32_prorvd512:
  case X86::BI__builtin_ia32_prorvq128:
  case X86::BI__builtin_ia32_prorvq256:
  case X86::BI__builtin_ia32_prorvq512:
    return EmitX86FunnelShift(*this, Ops[0], Ops[0], Ops[1], true);
  case X86::BI__builtin_ia32_selectb_128:
  case X86::BI__builtin_ia32_selectb_256:
  case X86::BI__builtin_ia32_selectb_512:
  case X86::BI__builtin_ia32_selectw_128:
  case X86::BI__builtin_ia32_selectw_256:
  case X86::BI__builtin_ia32_selectw_512:
  case X86::BI__builtin_ia32_selectd_128:
  case X86::BI__builtin_ia32_selectd_256:
  case X86::BI__builtin_ia32_selectd_512:
  case X86::BI__builtin_ia32_selectq_128:
  case X86::BI__builtin_ia32_selectq_256:
  case X86::BI__builtin_ia32_selectq_512:
  case X86::BI__builtin_ia32_selectph_128:
  case X86::BI__builtin_ia32_selectph_256:
  case X86::BI__builtin_ia32_selectph_512:
  case X86::BI__builtin_ia32_selectpbf_128:
  case X86::BI__builtin_ia32_selectpbf_256:
  case X86::BI__builtin_ia32_selectpbf_512:
  case X86::BI__builtin_ia32_selectps_128:
  case X86::BI__builtin_ia32_selectps_256:
  case X86::BI__builtin_ia32_selectps_512:
  case X86::BI__builtin_ia32_selectpd_128:
  case X86::BI__builtin_ia32_selectpd_256:
  case X86::BI__builtin_ia32_selectpd_512:
    return EmitX86Select(*this, Ops[0], Ops[1], Ops[2]);
  case X86::BI__builtin_ia32_selectsh_128:
  case X86::BI__builtin_ia32_selectsbf_128:
  case X86::BI__builtin_ia32_selectss_128:
  case X86::BI__builtin_ia32_selectsd_128: {
    Value *A = Builder.CreateExtractElement(Ops[1], (uint64_t)0);
    Value *B = Builder.CreateExtractElement(Ops[2], (uint64_t)0);
    A = EmitX86ScalarSelect(*this, Ops[0], A, B);
    return Builder.CreateInsertElement(Ops[1], A, (uint64_t)0);
  }
  case X86::BI__builtin_ia32_cmpb128_mask:
  case X86::BI__builtin_ia32_cmpb256_mask:
  case X86::BI__builtin_ia32_cmpb512_mask:
  case X86::BI__builtin_ia32_cmpw128_mask:
  case X86::BI__builtin_ia32_cmpw256_mask:
  case X86::BI__builtin_ia32_cmpw512_mask:
  case X86::BI__builtin_ia32_cmpd128_mask:
  case X86::BI__builtin_ia32_cmpd256_mask:
  case X86::BI__builtin_ia32_cmpd512_mask:
  case X86::BI__builtin_ia32_cmpq128_mask:
  case X86::BI__builtin_ia32_cmpq256_mask:
  case X86::BI__builtin_ia32_cmpq512_mask: {
    unsigned CC = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0x7;
    return EmitX86MaskedCompare(*this, CC, true, Ops);
  }
  case X86::BI__builtin_ia32_ucmpb128_mask:
  case X86::BI__builtin_ia32_ucmpb256_mask:
  case X86::BI__builtin_ia32_ucmpb512_mask:
  case X86::BI__builtin_ia32_ucmpw128_mask:
  case X86::BI__builtin_ia32_ucmpw256_mask:
  case X86::BI__builtin_ia32_ucmpw512_mask:
  case X86::BI__builtin_ia32_ucmpd128_mask:
  case X86::BI__builtin_ia32_ucmpd256_mask:
  case X86::BI__builtin_ia32_ucmpd512_mask:
  case X86::BI__builtin_ia32_ucmpq128_mask:
  case X86::BI__builtin_ia32_ucmpq256_mask:
  case X86::BI__builtin_ia32_ucmpq512_mask: {
    unsigned CC = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0x7;
    return EmitX86MaskedCompare(*this, CC, false, Ops);
  }
  case X86::BI__builtin_ia32_vpcomb:
  case X86::BI__builtin_ia32_vpcomw:
  case X86::BI__builtin_ia32_vpcomd:
  case X86::BI__builtin_ia32_vpcomq:
    return EmitX86vpcom(*this, Ops, true);
  case X86::BI__builtin_ia32_vpcomub:
  case X86::BI__builtin_ia32_vpcomuw:
  case X86::BI__builtin_ia32_vpcomud:
  case X86::BI__builtin_ia32_vpcomuq:
    return EmitX86vpcom(*this, Ops, false);

  case X86::BI__builtin_ia32_kortestcqi:
  case X86::BI__builtin_ia32_kortestchi:
  case X86::BI__builtin_ia32_kortestcsi:
  case X86::BI__builtin_ia32_kortestcdi: {
    Value *Or = EmitX86MaskLogic(*this, Instruction::Or, Ops);
    Value *C = llvm::Constant::getAllOnesValue(Ops[0]->getType());
    Value *Cmp = Builder.CreateICmpEQ(Or, C);
    return Builder.CreateZExt(Cmp, ConvertType(E->getType()));
  }
  case X86::BI__builtin_ia32_kortestzqi:
  case X86::BI__builtin_ia32_kortestzhi:
  case X86::BI__builtin_ia32_kortestzsi:
  case X86::BI__builtin_ia32_kortestzdi: {
    Value *Or = EmitX86MaskLogic(*this, Instruction::Or, Ops);
    Value *C = llvm::Constant::getNullValue(Ops[0]->getType());
    Value *Cmp = Builder.CreateICmpEQ(Or, C);
    return Builder.CreateZExt(Cmp, ConvertType(E->getType()));
  }

  case X86::BI__builtin_ia32_ktestcqi:
  case X86::BI__builtin_ia32_ktestzqi:
  case X86::BI__builtin_ia32_ktestchi:
  case X86::BI__builtin_ia32_ktestzhi:
  case X86::BI__builtin_ia32_ktestcsi:
  case X86::BI__builtin_ia32_ktestzsi:
  case X86::BI__builtin_ia32_ktestcdi:
  case X86::BI__builtin_ia32_ktestzdi: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_ktestcqi:
      IID = Intrinsic::x86_avx512_ktestc_b;
      break;
    case X86::BI__builtin_ia32_ktestzqi:
      IID = Intrinsic::x86_avx512_ktestz_b;
      break;
    case X86::BI__builtin_ia32_ktestchi:
      IID = Intrinsic::x86_avx512_ktestc_w;
      break;
    case X86::BI__builtin_ia32_ktestzhi:
      IID = Intrinsic::x86_avx512_ktestz_w;
      break;
    case X86::BI__builtin_ia32_ktestcsi:
      IID = Intrinsic::x86_avx512_ktestc_d;
      break;
    case X86::BI__builtin_ia32_ktestzsi:
      IID = Intrinsic::x86_avx512_ktestz_d;
      break;
    case X86::BI__builtin_ia32_ktestcdi:
      IID = Intrinsic::x86_avx512_ktestc_q;
      break;
    case X86::BI__builtin_ia32_ktestzdi:
      IID = Intrinsic::x86_avx512_ktestz_q;
      break;
    }

    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *LHS = getMaskVecValue(*this, Ops[0], NumElts);
    Value *RHS = getMaskVecValue(*this, Ops[1], NumElts);
    Function *Intr = CGM.getIntrinsic(IID);
    return Builder.CreateCall(Intr, {LHS, RHS});
  }

  case X86::BI__builtin_ia32_kaddqi:
  case X86::BI__builtin_ia32_kaddhi:
  case X86::BI__builtin_ia32_kaddsi:
  case X86::BI__builtin_ia32_kadddi: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_kaddqi:
      IID = Intrinsic::x86_avx512_kadd_b;
      break;
    case X86::BI__builtin_ia32_kaddhi:
      IID = Intrinsic::x86_avx512_kadd_w;
      break;
    case X86::BI__builtin_ia32_kaddsi:
      IID = Intrinsic::x86_avx512_kadd_d;
      break;
    case X86::BI__builtin_ia32_kadddi:
      IID = Intrinsic::x86_avx512_kadd_q;
      break;
    }

    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *LHS = getMaskVecValue(*this, Ops[0], NumElts);
    Value *RHS = getMaskVecValue(*this, Ops[1], NumElts);
    Function *Intr = CGM.getIntrinsic(IID);
    Value *Res = Builder.CreateCall(Intr, {LHS, RHS});
    return Builder.CreateBitCast(Res, Ops[0]->getType());
  }
  case X86::BI__builtin_ia32_kandqi:
  case X86::BI__builtin_ia32_kandhi:
  case X86::BI__builtin_ia32_kandsi:
  case X86::BI__builtin_ia32_kanddi:
    return EmitX86MaskLogic(*this, Instruction::And, Ops);
  case X86::BI__builtin_ia32_kandnqi:
  case X86::BI__builtin_ia32_kandnhi:
  case X86::BI__builtin_ia32_kandnsi:
  case X86::BI__builtin_ia32_kandndi:
    return EmitX86MaskLogic(*this, Instruction::And, Ops, true);
  case X86::BI__builtin_ia32_korqi:
  case X86::BI__builtin_ia32_korhi:
  case X86::BI__builtin_ia32_korsi:
  case X86::BI__builtin_ia32_kordi:
    return EmitX86MaskLogic(*this, Instruction::Or, Ops);
  case X86::BI__builtin_ia32_kxnorqi:
  case X86::BI__builtin_ia32_kxnorhi:
  case X86::BI__builtin_ia32_kxnorsi:
  case X86::BI__builtin_ia32_kxnordi:
    return EmitX86MaskLogic(*this, Instruction::Xor, Ops, true);
  case X86::BI__builtin_ia32_kxorqi:
  case X86::BI__builtin_ia32_kxorhi:
  case X86::BI__builtin_ia32_kxorsi:
  case X86::BI__builtin_ia32_kxordi:
    return EmitX86MaskLogic(*this, Instruction::Xor,  Ops);
  case X86::BI__builtin_ia32_knotqi:
  case X86::BI__builtin_ia32_knothi:
  case X86::BI__builtin_ia32_knotsi:
  case X86::BI__builtin_ia32_knotdi: {
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *Res = getMaskVecValue(*this, Ops[0], NumElts);
    return Builder.CreateBitCast(Builder.CreateNot(Res),
                                 Ops[0]->getType());
  }
  case X86::BI__builtin_ia32_kmovb:
  case X86::BI__builtin_ia32_kmovw:
  case X86::BI__builtin_ia32_kmovd:
  case X86::BI__builtin_ia32_kmovq: {
    // Bitcast to vXi1 type and then back to integer. This gets the mask
    // register type into the IR, but might be optimized out depending on
    // what's around it.
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *Res = getMaskVecValue(*this, Ops[0], NumElts);
    return Builder.CreateBitCast(Res, Ops[0]->getType());
  }

  case X86::BI__builtin_ia32_kunpckdi:
  case X86::BI__builtin_ia32_kunpcksi:
  case X86::BI__builtin_ia32_kunpckhi: {
    unsigned NumElts = Ops[0]->getType()->getIntegerBitWidth();
    Value *LHS = getMaskVecValue(*this, Ops[0], NumElts);
    Value *RHS = getMaskVecValue(*this, Ops[1], NumElts);
    int Indices[64];
    for (unsigned i = 0; i != NumElts; ++i)
      Indices[i] = i;

    // First extract half of each vector. This gives better codegen than
    // doing it in a single shuffle.
    LHS = Builder.CreateShuffleVector(LHS, LHS, ArrayRef(Indices, NumElts / 2));
    RHS = Builder.CreateShuffleVector(RHS, RHS, ArrayRef(Indices, NumElts / 2));
    // Concat the vectors.
    // NOTE: Operands are swapped to match the intrinsic definition.
    Value *Res =
        Builder.CreateShuffleVector(RHS, LHS, ArrayRef(Indices, NumElts));
    return Builder.CreateBitCast(Res, Ops[0]->getType());
  }

  case X86::BI__builtin_ia32_vplzcntd_128:
  case X86::BI__builtin_ia32_vplzcntd_256:
  case X86::BI__builtin_ia32_vplzcntd_512:
  case X86::BI__builtin_ia32_vplzcntq_128:
  case X86::BI__builtin_ia32_vplzcntq_256:
  case X86::BI__builtin_ia32_vplzcntq_512: {
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, Ops[0]->getType());
    return Builder.CreateCall(F, {Ops[0],Builder.getInt1(false)});
  }
  case X86::BI__builtin_ia32_sqrtss:
  case X86::BI__builtin_ia32_sqrtsd: {
    Value *A = Builder.CreateExtractElement(Ops[0], (uint64_t)0);
    Function *F;
    if (Builder.getIsFPConstrained()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
      F = CGM.getIntrinsic(Intrinsic::experimental_constrained_sqrt,
                           A->getType());
      A = Builder.CreateConstrainedFPCall(F, {A});
    } else {
      F = CGM.getIntrinsic(Intrinsic::sqrt, A->getType());
      A = Builder.CreateCall(F, {A});
    }
    return Builder.CreateInsertElement(Ops[0], A, (uint64_t)0);
  }
  case X86::BI__builtin_ia32_sqrtsh_round_mask:
  case X86::BI__builtin_ia32_sqrtsd_round_mask:
  case X86::BI__builtin_ia32_sqrtss_round_mask: {
    unsigned CC = cast<llvm::ConstantInt>(Ops[4])->getZExtValue();
    // Support only if the rounding mode is 4 (AKA CUR_DIRECTION),
    // otherwise keep the intrinsic.
    if (CC != 4) {
      Intrinsic::ID IID;

      switch (BuiltinID) {
      default:
        llvm_unreachable("Unsupported intrinsic!");
      case X86::BI__builtin_ia32_sqrtsh_round_mask:
        IID = Intrinsic::x86_avx512fp16_mask_sqrt_sh;
        break;
      case X86::BI__builtin_ia32_sqrtsd_round_mask:
        IID = Intrinsic::x86_avx512_mask_sqrt_sd;
        break;
      case X86::BI__builtin_ia32_sqrtss_round_mask:
        IID = Intrinsic::x86_avx512_mask_sqrt_ss;
        break;
      }
      return Builder.CreateCall(CGM.getIntrinsic(IID), Ops);
    }
    Value *A = Builder.CreateExtractElement(Ops[1], (uint64_t)0);
    Function *F;
    if (Builder.getIsFPConstrained()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
      F = CGM.getIntrinsic(Intrinsic::experimental_constrained_sqrt,
                           A->getType());
      A = Builder.CreateConstrainedFPCall(F, A);
    } else {
      F = CGM.getIntrinsic(Intrinsic::sqrt, A->getType());
      A = Builder.CreateCall(F, A);
    }
    Value *Src = Builder.CreateExtractElement(Ops[2], (uint64_t)0);
    A = EmitX86ScalarSelect(*this, Ops[3], A, Src);
    return Builder.CreateInsertElement(Ops[0], A, (uint64_t)0);
  }
  case X86::BI__builtin_ia32_sqrtpd256:
  case X86::BI__builtin_ia32_sqrtpd:
  case X86::BI__builtin_ia32_sqrtps256:
  case X86::BI__builtin_ia32_sqrtps:
  case X86::BI__builtin_ia32_sqrtph256:
  case X86::BI__builtin_ia32_sqrtph:
  case X86::BI__builtin_ia32_sqrtph512:
  case X86::BI__builtin_ia32_sqrtps512:
  case X86::BI__builtin_ia32_sqrtpd512: {
    if (Ops.size() == 2) {
      unsigned CC = cast<llvm::ConstantInt>(Ops[1])->getZExtValue();
      // Support only if the rounding mode is 4 (AKA CUR_DIRECTION),
      // otherwise keep the intrinsic.
      if (CC != 4) {
        Intrinsic::ID IID;

        switch (BuiltinID) {
        default:
          llvm_unreachable("Unsupported intrinsic!");
        case X86::BI__builtin_ia32_sqrtph512:
          IID = Intrinsic::x86_avx512fp16_sqrt_ph_512;
          break;
        case X86::BI__builtin_ia32_sqrtps512:
          IID = Intrinsic::x86_avx512_sqrt_ps_512;
          break;
        case X86::BI__builtin_ia32_sqrtpd512:
          IID = Intrinsic::x86_avx512_sqrt_pd_512;
          break;
        }
        return Builder.CreateCall(CGM.getIntrinsic(IID), Ops);
      }
    }
    if (Builder.getIsFPConstrained()) {
      CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
      Function *F = CGM.getIntrinsic(Intrinsic::experimental_constrained_sqrt,
                                     Ops[0]->getType());
      return Builder.CreateConstrainedFPCall(F, Ops[0]);
    } else {
      Function *F = CGM.getIntrinsic(Intrinsic::sqrt, Ops[0]->getType());
      return Builder.CreateCall(F, Ops[0]);
    }
  }

  case X86::BI__builtin_ia32_pmuludq128:
  case X86::BI__builtin_ia32_pmuludq256:
  case X86::BI__builtin_ia32_pmuludq512:
    return EmitX86Muldq(*this, /*IsSigned*/false, Ops);

  case X86::BI__builtin_ia32_pmuldq128:
  case X86::BI__builtin_ia32_pmuldq256:
  case X86::BI__builtin_ia32_pmuldq512:
    return EmitX86Muldq(*this, /*IsSigned*/true, Ops);

  case X86::BI__builtin_ia32_pternlogd512_mask:
  case X86::BI__builtin_ia32_pternlogq512_mask:
  case X86::BI__builtin_ia32_pternlogd128_mask:
  case X86::BI__builtin_ia32_pternlogd256_mask:
  case X86::BI__builtin_ia32_pternlogq128_mask:
  case X86::BI__builtin_ia32_pternlogq256_mask:
    return EmitX86Ternlog(*this, /*ZeroMask*/false, Ops);

  case X86::BI__builtin_ia32_pternlogd512_maskz:
  case X86::BI__builtin_ia32_pternlogq512_maskz:
  case X86::BI__builtin_ia32_pternlogd128_maskz:
  case X86::BI__builtin_ia32_pternlogd256_maskz:
  case X86::BI__builtin_ia32_pternlogq128_maskz:
  case X86::BI__builtin_ia32_pternlogq256_maskz:
    return EmitX86Ternlog(*this, /*ZeroMask*/true, Ops);

  case X86::BI__builtin_ia32_vpshldd128:
  case X86::BI__builtin_ia32_vpshldd256:
  case X86::BI__builtin_ia32_vpshldd512:
  case X86::BI__builtin_ia32_vpshldq128:
  case X86::BI__builtin_ia32_vpshldq256:
  case X86::BI__builtin_ia32_vpshldq512:
  case X86::BI__builtin_ia32_vpshldw128:
  case X86::BI__builtin_ia32_vpshldw256:
  case X86::BI__builtin_ia32_vpshldw512:
    return EmitX86FunnelShift(*this, Ops[0], Ops[1], Ops[2], false);

  case X86::BI__builtin_ia32_vpshrdd128:
  case X86::BI__builtin_ia32_vpshrdd256:
  case X86::BI__builtin_ia32_vpshrdd512:
  case X86::BI__builtin_ia32_vpshrdq128:
  case X86::BI__builtin_ia32_vpshrdq256:
  case X86::BI__builtin_ia32_vpshrdq512:
  case X86::BI__builtin_ia32_vpshrdw128:
  case X86::BI__builtin_ia32_vpshrdw256:
  case X86::BI__builtin_ia32_vpshrdw512:
    // Ops 0 and 1 are swapped.
    return EmitX86FunnelShift(*this, Ops[1], Ops[0], Ops[2], true);

  case X86::BI__builtin_ia32_vpshldvd128:
  case X86::BI__builtin_ia32_vpshldvd256:
  case X86::BI__builtin_ia32_vpshldvd512:
  case X86::BI__builtin_ia32_vpshldvq128:
  case X86::BI__builtin_ia32_vpshldvq256:
  case X86::BI__builtin_ia32_vpshldvq512:
  case X86::BI__builtin_ia32_vpshldvw128:
  case X86::BI__builtin_ia32_vpshldvw256:
  case X86::BI__builtin_ia32_vpshldvw512:
    return EmitX86FunnelShift(*this, Ops[0], Ops[1], Ops[2], false);

  case X86::BI__builtin_ia32_vpshrdvd128:
  case X86::BI__builtin_ia32_vpshrdvd256:
  case X86::BI__builtin_ia32_vpshrdvd512:
  case X86::BI__builtin_ia32_vpshrdvq128:
  case X86::BI__builtin_ia32_vpshrdvq256:
  case X86::BI__builtin_ia32_vpshrdvq512:
  case X86::BI__builtin_ia32_vpshrdvw128:
  case X86::BI__builtin_ia32_vpshrdvw256:
  case X86::BI__builtin_ia32_vpshrdvw512:
    // Ops 0 and 1 are swapped.
    return EmitX86FunnelShift(*this, Ops[1], Ops[0], Ops[2], true);

  // Reductions
  case X86::BI__builtin_ia32_reduce_fadd_pd512:
  case X86::BI__builtin_ia32_reduce_fadd_ps512:
  case X86::BI__builtin_ia32_reduce_fadd_ph512:
  case X86::BI__builtin_ia32_reduce_fadd_ph256:
  case X86::BI__builtin_ia32_reduce_fadd_ph128: {
    Function *F =
        CGM.getIntrinsic(Intrinsic::vector_reduce_fadd, Ops[1]->getType());
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.getFastMathFlags().setAllowReassoc();
    return Builder.CreateCall(F, {Ops[0], Ops[1]});
  }
  case X86::BI__builtin_ia32_reduce_fmul_pd512:
  case X86::BI__builtin_ia32_reduce_fmul_ps512:
  case X86::BI__builtin_ia32_reduce_fmul_ph512:
  case X86::BI__builtin_ia32_reduce_fmul_ph256:
  case X86::BI__builtin_ia32_reduce_fmul_ph128: {
    Function *F =
        CGM.getIntrinsic(Intrinsic::vector_reduce_fmul, Ops[1]->getType());
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.getFastMathFlags().setAllowReassoc();
    return Builder.CreateCall(F, {Ops[0], Ops[1]});
  }
  case X86::BI__builtin_ia32_reduce_fmax_pd512:
  case X86::BI__builtin_ia32_reduce_fmax_ps512:
  case X86::BI__builtin_ia32_reduce_fmax_ph512:
  case X86::BI__builtin_ia32_reduce_fmax_ph256:
  case X86::BI__builtin_ia32_reduce_fmax_ph128: {
    Function *F =
        CGM.getIntrinsic(Intrinsic::vector_reduce_fmax, Ops[0]->getType());
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.getFastMathFlags().setNoNaNs();
    return Builder.CreateCall(F, {Ops[0]});
  }
  case X86::BI__builtin_ia32_reduce_fmin_pd512:
  case X86::BI__builtin_ia32_reduce_fmin_ps512:
  case X86::BI__builtin_ia32_reduce_fmin_ph512:
  case X86::BI__builtin_ia32_reduce_fmin_ph256:
  case X86::BI__builtin_ia32_reduce_fmin_ph128: {
    Function *F =
        CGM.getIntrinsic(Intrinsic::vector_reduce_fmin, Ops[0]->getType());
    IRBuilder<>::FastMathFlagGuard FMFGuard(Builder);
    Builder.getFastMathFlags().setNoNaNs();
    return Builder.CreateCall(F, {Ops[0]});
  }

  case X86::BI__builtin_ia32_rdrand16_step:
  case X86::BI__builtin_ia32_rdrand32_step:
  case X86::BI__builtin_ia32_rdrand64_step:
  case X86::BI__builtin_ia32_rdseed16_step:
  case X86::BI__builtin_ia32_rdseed32_step:
  case X86::BI__builtin_ia32_rdseed64_step: {
    Intrinsic::ID ID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_rdrand16_step:
      ID = Intrinsic::x86_rdrand_16;
      break;
    case X86::BI__builtin_ia32_rdrand32_step:
      ID = Intrinsic::x86_rdrand_32;
      break;
    case X86::BI__builtin_ia32_rdrand64_step:
      ID = Intrinsic::x86_rdrand_64;
      break;
    case X86::BI__builtin_ia32_rdseed16_step:
      ID = Intrinsic::x86_rdseed_16;
      break;
    case X86::BI__builtin_ia32_rdseed32_step:
      ID = Intrinsic::x86_rdseed_32;
      break;
    case X86::BI__builtin_ia32_rdseed64_step:
      ID = Intrinsic::x86_rdseed_64;
      break;
    }

    Value *Call = Builder.CreateCall(CGM.getIntrinsic(ID));
    Builder.CreateDefaultAlignedStore(Builder.CreateExtractValue(Call, 0),
                                      Ops[0]);
    return Builder.CreateExtractValue(Call, 1);
  }
  case X86::BI__builtin_ia32_addcarryx_u32:
  case X86::BI__builtin_ia32_addcarryx_u64:
  case X86::BI__builtin_ia32_subborrow_u32:
  case X86::BI__builtin_ia32_subborrow_u64: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_addcarryx_u32:
      IID = Intrinsic::x86_addcarry_32;
      break;
    case X86::BI__builtin_ia32_addcarryx_u64:
      IID = Intrinsic::x86_addcarry_64;
      break;
    case X86::BI__builtin_ia32_subborrow_u32:
      IID = Intrinsic::x86_subborrow_32;
      break;
    case X86::BI__builtin_ia32_subborrow_u64:
      IID = Intrinsic::x86_subborrow_64;
      break;
    }

    Value *Call = Builder.CreateCall(CGM.getIntrinsic(IID),
                                     { Ops[0], Ops[1], Ops[2] });
    Builder.CreateDefaultAlignedStore(Builder.CreateExtractValue(Call, 1),
                                      Ops[3]);
    return Builder.CreateExtractValue(Call, 0);
  }

  case X86::BI__builtin_ia32_fpclassps128_mask:
  case X86::BI__builtin_ia32_fpclassps256_mask:
  case X86::BI__builtin_ia32_fpclassps512_mask:
  case X86::BI__builtin_ia32_fpclassph128_mask:
  case X86::BI__builtin_ia32_fpclassph256_mask:
  case X86::BI__builtin_ia32_fpclassph512_mask:
  case X86::BI__builtin_ia32_fpclasspd128_mask:
  case X86::BI__builtin_ia32_fpclasspd256_mask:
  case X86::BI__builtin_ia32_fpclasspd512_mask: {
    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    Value *MaskIn = Ops[2];
    Ops.erase(&Ops[2]);

    Intrinsic::ID ID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_fpclassph128_mask:
      ID = Intrinsic::x86_avx512fp16_fpclass_ph_128;
      break;
    case X86::BI__builtin_ia32_fpclassph256_mask:
      ID = Intrinsic::x86_avx512fp16_fpclass_ph_256;
      break;
    case X86::BI__builtin_ia32_fpclassph512_mask:
      ID = Intrinsic::x86_avx512fp16_fpclass_ph_512;
      break;
    case X86::BI__builtin_ia32_fpclassps128_mask:
      ID = Intrinsic::x86_avx512_fpclass_ps_128;
      break;
    case X86::BI__builtin_ia32_fpclassps256_mask:
      ID = Intrinsic::x86_avx512_fpclass_ps_256;
      break;
    case X86::BI__builtin_ia32_fpclassps512_mask:
      ID = Intrinsic::x86_avx512_fpclass_ps_512;
      break;
    case X86::BI__builtin_ia32_fpclasspd128_mask:
      ID = Intrinsic::x86_avx512_fpclass_pd_128;
      break;
    case X86::BI__builtin_ia32_fpclasspd256_mask:
      ID = Intrinsic::x86_avx512_fpclass_pd_256;
      break;
    case X86::BI__builtin_ia32_fpclasspd512_mask:
      ID = Intrinsic::x86_avx512_fpclass_pd_512;
      break;
    }

    Value *Fpclass = Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
    return EmitX86MaskedCompareResult(*this, Fpclass, NumElts, MaskIn);
  }

  case X86::BI__builtin_ia32_vp2intersect_q_512:
  case X86::BI__builtin_ia32_vp2intersect_q_256:
  case X86::BI__builtin_ia32_vp2intersect_q_128:
  case X86::BI__builtin_ia32_vp2intersect_d_512:
  case X86::BI__builtin_ia32_vp2intersect_d_256:
  case X86::BI__builtin_ia32_vp2intersect_d_128: {
    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    Intrinsic::ID ID;

    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_vp2intersect_q_512:
      ID = Intrinsic::x86_avx512_vp2intersect_q_512;
      break;
    case X86::BI__builtin_ia32_vp2intersect_q_256:
      ID = Intrinsic::x86_avx512_vp2intersect_q_256;
      break;
    case X86::BI__builtin_ia32_vp2intersect_q_128:
      ID = Intrinsic::x86_avx512_vp2intersect_q_128;
      break;
    case X86::BI__builtin_ia32_vp2intersect_d_512:
      ID = Intrinsic::x86_avx512_vp2intersect_d_512;
      break;
    case X86::BI__builtin_ia32_vp2intersect_d_256:
      ID = Intrinsic::x86_avx512_vp2intersect_d_256;
      break;
    case X86::BI__builtin_ia32_vp2intersect_d_128:
      ID = Intrinsic::x86_avx512_vp2intersect_d_128;
      break;
    }

    Value *Call = Builder.CreateCall(CGM.getIntrinsic(ID), {Ops[0], Ops[1]});
    Value *Result = Builder.CreateExtractValue(Call, 0);
    Result = EmitX86MaskedCompareResult(*this, Result, NumElts, nullptr);
    Builder.CreateDefaultAlignedStore(Result, Ops[2]);

    Result = Builder.CreateExtractValue(Call, 1);
    Result = EmitX86MaskedCompareResult(*this, Result, NumElts, nullptr);
    return Builder.CreateDefaultAlignedStore(Result, Ops[3]);
  }

  case X86::BI__builtin_ia32_vpmultishiftqb128:
  case X86::BI__builtin_ia32_vpmultishiftqb256:
  case X86::BI__builtin_ia32_vpmultishiftqb512: {
    Intrinsic::ID ID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_vpmultishiftqb128:
      ID = Intrinsic::x86_avx512_pmultishift_qb_128;
      break;
    case X86::BI__builtin_ia32_vpmultishiftqb256:
      ID = Intrinsic::x86_avx512_pmultishift_qb_256;
      break;
    case X86::BI__builtin_ia32_vpmultishiftqb512:
      ID = Intrinsic::x86_avx512_pmultishift_qb_512;
      break;
    }

    return Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
  }

  case X86::BI__builtin_ia32_vpshufbitqmb128_mask:
  case X86::BI__builtin_ia32_vpshufbitqmb256_mask:
  case X86::BI__builtin_ia32_vpshufbitqmb512_mask: {
    unsigned NumElts =
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
    Value *MaskIn = Ops[2];
    Ops.erase(&Ops[2]);

    Intrinsic::ID ID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_vpshufbitqmb128_mask:
      ID = Intrinsic::x86_avx512_vpshufbitqmb_128;
      break;
    case X86::BI__builtin_ia32_vpshufbitqmb256_mask:
      ID = Intrinsic::x86_avx512_vpshufbitqmb_256;
      break;
    case X86::BI__builtin_ia32_vpshufbitqmb512_mask:
      ID = Intrinsic::x86_avx512_vpshufbitqmb_512;
      break;
    }

    Value *Shufbit = Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
    return EmitX86MaskedCompareResult(*this, Shufbit, NumElts, MaskIn);
  }

  // packed comparison intrinsics
  case X86::BI__builtin_ia32_cmpeqps:
  case X86::BI__builtin_ia32_cmpeqpd:
    return getVectorFCmpIR(CmpInst::FCMP_OEQ, /*IsSignaling*/false);
  case X86::BI__builtin_ia32_cmpltps:
  case X86::BI__builtin_ia32_cmpltpd:
    return getVectorFCmpIR(CmpInst::FCMP_OLT, /*IsSignaling*/true);
  case X86::BI__builtin_ia32_cmpleps:
  case X86::BI__builtin_ia32_cmplepd:
    return getVectorFCmpIR(CmpInst::FCMP_OLE, /*IsSignaling*/true);
  case X86::BI__builtin_ia32_cmpunordps:
  case X86::BI__builtin_ia32_cmpunordpd:
    return getVectorFCmpIR(CmpInst::FCMP_UNO, /*IsSignaling*/false);
  case X86::BI__builtin_ia32_cmpneqps:
  case X86::BI__builtin_ia32_cmpneqpd:
    return getVectorFCmpIR(CmpInst::FCMP_UNE, /*IsSignaling*/false);
  case X86::BI__builtin_ia32_cmpnltps:
  case X86::BI__builtin_ia32_cmpnltpd:
    return getVectorFCmpIR(CmpInst::FCMP_UGE, /*IsSignaling*/true);
  case X86::BI__builtin_ia32_cmpnleps:
  case X86::BI__builtin_ia32_cmpnlepd:
    return getVectorFCmpIR(CmpInst::FCMP_UGT, /*IsSignaling*/true);
  case X86::BI__builtin_ia32_cmpordps:
  case X86::BI__builtin_ia32_cmpordpd:
    return getVectorFCmpIR(CmpInst::FCMP_ORD, /*IsSignaling*/false);
  case X86::BI__builtin_ia32_cmpph128_mask:
  case X86::BI__builtin_ia32_cmpph256_mask:
  case X86::BI__builtin_ia32_cmpph512_mask:
  case X86::BI__builtin_ia32_cmpps128_mask:
  case X86::BI__builtin_ia32_cmpps256_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmppd128_mask:
  case X86::BI__builtin_ia32_cmppd256_mask:
  case X86::BI__builtin_ia32_cmppd512_mask:
    IsMaskFCmp = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_cmpps:
  case X86::BI__builtin_ia32_cmpps256:
  case X86::BI__builtin_ia32_cmppd:
  case X86::BI__builtin_ia32_cmppd256: {
    // Lowering vector comparisons to fcmp instructions, while
    // ignoring signalling behaviour requested
    // ignoring rounding mode requested
    // This is only possible if fp-model is not strict and FENV_ACCESS is off.

    // The third argument is the comparison condition, and integer in the
    // range [0, 31]
    unsigned CC = cast<llvm::ConstantInt>(Ops[2])->getZExtValue() & 0x1f;

    // Lowering to IR fcmp instruction.
    // Ignoring requested signaling behaviour,
    // e.g. both _CMP_GT_OS & _CMP_GT_OQ are translated to FCMP_OGT.
    FCmpInst::Predicate Pred;
    bool IsSignaling;
    // Predicates for 16-31 repeat the 0-15 predicates. Only the signalling
    // behavior is inverted. We'll handle that after the switch.
    switch (CC & 0xf) {
    case 0x00: Pred = FCmpInst::FCMP_OEQ;   IsSignaling = false; break;
    case 0x01: Pred = FCmpInst::FCMP_OLT;   IsSignaling = true;  break;
    case 0x02: Pred = FCmpInst::FCMP_OLE;   IsSignaling = true;  break;
    case 0x03: Pred = FCmpInst::FCMP_UNO;   IsSignaling = false; break;
    case 0x04: Pred = FCmpInst::FCMP_UNE;   IsSignaling = false; break;
    case 0x05: Pred = FCmpInst::FCMP_UGE;   IsSignaling = true;  break;
    case 0x06: Pred = FCmpInst::FCMP_UGT;   IsSignaling = true;  break;
    case 0x07: Pred = FCmpInst::FCMP_ORD;   IsSignaling = false; break;
    case 0x08: Pred = FCmpInst::FCMP_UEQ;   IsSignaling = false; break;
    case 0x09: Pred = FCmpInst::FCMP_ULT;   IsSignaling = true;  break;
    case 0x0a: Pred = FCmpInst::FCMP_ULE;   IsSignaling = true;  break;
    case 0x0b: Pred = FCmpInst::FCMP_FALSE; IsSignaling = false; break;
    case 0x0c: Pred = FCmpInst::FCMP_ONE;   IsSignaling = false; break;
    case 0x0d: Pred = FCmpInst::FCMP_OGE;   IsSignaling = true;  break;
    case 0x0e: Pred = FCmpInst::FCMP_OGT;   IsSignaling = true;  break;
    case 0x0f: Pred = FCmpInst::FCMP_TRUE;  IsSignaling = false; break;
    default: llvm_unreachable("Unhandled CC");
    }

    // Invert the signalling behavior for 16-31.
    if (CC & 0x10)
      IsSignaling = !IsSignaling;

    // If the predicate is true or false and we're using constrained intrinsics,
    // we don't have a compare intrinsic we can use. Just use the legacy X86
    // specific intrinsic.
    // If the intrinsic is mask enabled and we're using constrained intrinsics,
    // use the legacy X86 specific intrinsic.
    if (Builder.getIsFPConstrained() &&
        (Pred == FCmpInst::FCMP_TRUE || Pred == FCmpInst::FCMP_FALSE ||
         IsMaskFCmp)) {

      Intrinsic::ID IID;
      switch (BuiltinID) {
      default: llvm_unreachable("Unexpected builtin");
      case X86::BI__builtin_ia32_cmpps:
        IID = Intrinsic::x86_sse_cmp_ps;
        break;
      case X86::BI__builtin_ia32_cmpps256:
        IID = Intrinsic::x86_avx_cmp_ps_256;
        break;
      case X86::BI__builtin_ia32_cmppd:
        IID = Intrinsic::x86_sse2_cmp_pd;
        break;
      case X86::BI__builtin_ia32_cmppd256:
        IID = Intrinsic::x86_avx_cmp_pd_256;
        break;
      case X86::BI__builtin_ia32_cmpph128_mask:
        IID = Intrinsic::x86_avx512fp16_mask_cmp_ph_128;
        break;
      case X86::BI__builtin_ia32_cmpph256_mask:
        IID = Intrinsic::x86_avx512fp16_mask_cmp_ph_256;
        break;
      case X86::BI__builtin_ia32_cmpph512_mask:
        IID = Intrinsic::x86_avx512fp16_mask_cmp_ph_512;
        break;
      case X86::BI__builtin_ia32_cmpps512_mask:
        IID = Intrinsic::x86_avx512_mask_cmp_ps_512;
        break;
      case X86::BI__builtin_ia32_cmppd512_mask:
        IID = Intrinsic::x86_avx512_mask_cmp_pd_512;
        break;
      case X86::BI__builtin_ia32_cmpps128_mask:
        IID = Intrinsic::x86_avx512_mask_cmp_ps_128;
        break;
      case X86::BI__builtin_ia32_cmpps256_mask:
        IID = Intrinsic::x86_avx512_mask_cmp_ps_256;
        break;
      case X86::BI__builtin_ia32_cmppd128_mask:
        IID = Intrinsic::x86_avx512_mask_cmp_pd_128;
        break;
      case X86::BI__builtin_ia32_cmppd256_mask:
        IID = Intrinsic::x86_avx512_mask_cmp_pd_256;
        break;
      }

      Function *Intr = CGM.getIntrinsic(IID);
      if (IsMaskFCmp) {
        unsigned NumElts =
            cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
        Ops[3] = getMaskVecValue(*this, Ops[3], NumElts);
        Value *Cmp = Builder.CreateCall(Intr, Ops);
        return EmitX86MaskedCompareResult(*this, Cmp, NumElts, nullptr);
      }

      return Builder.CreateCall(Intr, Ops);
    }

    // Builtins without the _mask suffix return a vector of integers
    // of the same width as the input vectors
    if (IsMaskFCmp) {
      // We ignore SAE if strict FP is disabled. We only keep precise
      // exception behavior under strict FP.
      // NOTE: If strict FP does ever go through here a CGFPOptionsRAII
      // object will be required.
      unsigned NumElts =
          cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements();
      Value *Cmp;
      if (IsSignaling)
        Cmp = Builder.CreateFCmpS(Pred, Ops[0], Ops[1]);
      else
        Cmp = Builder.CreateFCmp(Pred, Ops[0], Ops[1]);
      return EmitX86MaskedCompareResult(*this, Cmp, NumElts, Ops[3]);
    }

    return getVectorFCmpIR(Pred, IsSignaling);
  }

  // SSE scalar comparison intrinsics
  case X86::BI__builtin_ia32_cmpeqss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 0);
  case X86::BI__builtin_ia32_cmpltss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 1);
  case X86::BI__builtin_ia32_cmpless:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 2);
  case X86::BI__builtin_ia32_cmpunordss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 3);
  case X86::BI__builtin_ia32_cmpneqss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 4);
  case X86::BI__builtin_ia32_cmpnltss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 5);
  case X86::BI__builtin_ia32_cmpnless:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 6);
  case X86::BI__builtin_ia32_cmpordss:
    return getCmpIntrinsicCall(Intrinsic::x86_sse_cmp_ss, 7);
  case X86::BI__builtin_ia32_cmpeqsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 0);
  case X86::BI__builtin_ia32_cmpltsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 1);
  case X86::BI__builtin_ia32_cmplesd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 2);
  case X86::BI__builtin_ia32_cmpunordsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 3);
  case X86::BI__builtin_ia32_cmpneqsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 4);
  case X86::BI__builtin_ia32_cmpnltsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 5);
  case X86::BI__builtin_ia32_cmpnlesd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 6);
  case X86::BI__builtin_ia32_cmpordsd:
    return getCmpIntrinsicCall(Intrinsic::x86_sse2_cmp_sd, 7);

  // f16c half2float intrinsics
  case X86::BI__builtin_ia32_vcvtph2ps:
  case X86::BI__builtin_ia32_vcvtph2ps256:
  case X86::BI__builtin_ia32_vcvtph2ps_mask:
  case X86::BI__builtin_ia32_vcvtph2ps256_mask:
  case X86::BI__builtin_ia32_vcvtph2ps512_mask: {
    CodeGenFunction::CGFPOptionsRAII FPOptsRAII(*this, E);
    return EmitX86CvtF16ToFloatExpr(*this, Ops, ConvertType(E->getType()));
  }

  // AVX512 bf16 intrinsics
  case X86::BI__builtin_ia32_cvtneps2bf16_128_mask: {
    Ops[2] = getMaskVecValue(
        *this, Ops[2],
        cast<llvm::FixedVectorType>(Ops[0]->getType())->getNumElements());
    Intrinsic::ID IID = Intrinsic::x86_avx512bf16_mask_cvtneps2bf16_128;
    return Builder.CreateCall(CGM.getIntrinsic(IID), Ops);
  }
  case X86::BI__builtin_ia32_cvtsbf162ss_32:
    return Builder.CreateFPExt(Ops[0], Builder.getFloatTy());

  case X86::BI__builtin_ia32_cvtneps2bf16_256_mask:
  case X86::BI__builtin_ia32_cvtneps2bf16_512_mask: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported intrinsic!");
    case X86::BI__builtin_ia32_cvtneps2bf16_256_mask:
      IID = Intrinsic::x86_avx512bf16_cvtneps2bf16_256;
      break;
    case X86::BI__builtin_ia32_cvtneps2bf16_512_mask:
      IID = Intrinsic::x86_avx512bf16_cvtneps2bf16_512;
      break;
    }
    Value *Res = Builder.CreateCall(CGM.getIntrinsic(IID), Ops[0]);
    return EmitX86Select(*this, Ops[2], Res, Ops[1]);
  }

  case X86::BI__cpuid:
  case X86::BI__cpuidex: {
    Value *FuncId = EmitScalarExpr(E->getArg(1));
    Value *SubFuncId = BuiltinID == X86::BI__cpuidex
                           ? EmitScalarExpr(E->getArg(2))
                           : llvm::ConstantInt::get(Int32Ty, 0);

    llvm::StructType *CpuidRetTy =
        llvm::StructType::get(Int32Ty, Int32Ty, Int32Ty, Int32Ty);
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(CpuidRetTy, {Int32Ty, Int32Ty}, false);

    StringRef Asm, Constraints;
    if (getTarget().getTriple().getArch() == llvm::Triple::x86) {
      Asm = "cpuid";
      Constraints = "={ax},={bx},={cx},={dx},{ax},{cx}";
    } else {
      // x86-64 uses %rbx as the base register, so preserve it.
      Asm = "xchgq %rbx, ${1:q}\n"
            "cpuid\n"
            "xchgq %rbx, ${1:q}";
      Constraints = "={ax},=r,={cx},={dx},0,2";
    }

    llvm::InlineAsm *IA = llvm::InlineAsm::get(FTy, Asm, Constraints,
                                               /*hasSideEffects=*/false);
    Value *IACall = Builder.CreateCall(IA, {FuncId, SubFuncId});
    Value *BasePtr = EmitScalarExpr(E->getArg(0));
    Value *Store = nullptr;
    for (unsigned i = 0; i < 4; i++) {
      Value *Extracted = Builder.CreateExtractValue(IACall, i);
      Value *StorePtr = Builder.CreateConstInBoundsGEP1_32(Int32Ty, BasePtr, i);
      Store = Builder.CreateAlignedStore(Extracted, StorePtr, getIntAlign());
    }

    // Return the last store instruction to signal that we have emitted the
    // the intrinsic.
    return Store;
  }

  case X86::BI__emul:
  case X86::BI__emulu: {
    llvm::Type *Int64Ty = llvm::IntegerType::get(getLLVMContext(), 64);
    bool isSigned = (BuiltinID == X86::BI__emul);
    Value *LHS = Builder.CreateIntCast(Ops[0], Int64Ty, isSigned);
    Value *RHS = Builder.CreateIntCast(Ops[1], Int64Ty, isSigned);
    return Builder.CreateMul(LHS, RHS, "", !isSigned, isSigned);
  }
  case X86::BI__mulh:
  case X86::BI__umulh:
  case X86::BI_mul128:
  case X86::BI_umul128: {
    llvm::Type *ResType = ConvertType(E->getType());
    llvm::Type *Int128Ty = llvm::IntegerType::get(getLLVMContext(), 128);

    bool IsSigned = (BuiltinID == X86::BI__mulh || BuiltinID == X86::BI_mul128);
    Value *LHS = Builder.CreateIntCast(Ops[0], Int128Ty, IsSigned);
    Value *RHS = Builder.CreateIntCast(Ops[1], Int128Ty, IsSigned);

    Value *MulResult, *HigherBits;
    if (IsSigned) {
      MulResult = Builder.CreateNSWMul(LHS, RHS);
      HigherBits = Builder.CreateAShr(MulResult, 64);
    } else {
      MulResult = Builder.CreateNUWMul(LHS, RHS);
      HigherBits = Builder.CreateLShr(MulResult, 64);
    }
    HigherBits = Builder.CreateIntCast(HigherBits, ResType, IsSigned);

    if (BuiltinID == X86::BI__mulh || BuiltinID == X86::BI__umulh)
      return HigherBits;

    Address HighBitsAddress = EmitPointerWithAlignment(E->getArg(2));
    Builder.CreateStore(HigherBits, HighBitsAddress);
    return Builder.CreateIntCast(MulResult, ResType, IsSigned);
  }

  case X86::BI__faststorefence: {
    return Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent,
                               llvm::SyncScope::System);
  }
  case X86::BI__shiftleft128:
  case X86::BI__shiftright128: {
    llvm::Function *F = CGM.getIntrinsic(
        BuiltinID == X86::BI__shiftleft128 ? Intrinsic::fshl : Intrinsic::fshr,
        Int64Ty);
    // Flip low/high ops and zero-extend amount to matching type.
    // shiftleft128(Low, High, Amt) -> fshl(High, Low, Amt)
    // shiftright128(Low, High, Amt) -> fshr(High, Low, Amt)
    std::swap(Ops[0], Ops[1]);
    Ops[2] = Builder.CreateZExt(Ops[2], Int64Ty);
    return Builder.CreateCall(F, Ops);
  }
  case X86::BI_ReadWriteBarrier:
  case X86::BI_ReadBarrier:
  case X86::BI_WriteBarrier: {
    return Builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent,
                               llvm::SyncScope::SingleThread);
  }

  case X86::BI_AddressOfReturnAddress: {
    Function *F =
        CGM.getIntrinsic(Intrinsic::addressofreturnaddress, AllocaInt8PtrTy);
    return Builder.CreateCall(F);
  }
  case X86::BI__stosb: {
    // We treat __stosb as a volatile memset - it may not generate "rep stosb"
    // instruction, but it will create a memset that won't be optimized away.
    return Builder.CreateMemSet(Ops[0], Ops[1], Ops[2], Align(1), true);
  }
  case X86::BI__ud2:
    // llvm.trap makes a ud2a instruction on x86.
    return EmitTrapCall(Intrinsic::trap);
  case X86::BI__int2c: {
    // This syscall signals a driver assertion failure in x86 NT kernels.
    llvm::FunctionType *FTy = llvm::FunctionType::get(VoidTy, false);
    llvm::InlineAsm *IA =
        llvm::InlineAsm::get(FTy, "int $$0x2c", "", /*hasSideEffects=*/true);
    llvm::AttributeList NoReturnAttr = llvm::AttributeList::get(
        getLLVMContext(), llvm::AttributeList::FunctionIndex,
        llvm::Attribute::NoReturn);
    llvm::CallInst *CI = Builder.CreateCall(IA);
    CI->setAttributes(NoReturnAttr);
    return CI;
  }
  case X86::BI__readfsbyte:
  case X86::BI__readfsword:
  case X86::BI__readfsdword:
  case X86::BI__readfsqword: {
    llvm::Type *IntTy = ConvertType(E->getType());
    Value *Ptr = Builder.CreateIntToPtr(
        Ops[0], llvm::PointerType::get(getLLVMContext(), 257));
    LoadInst *Load = Builder.CreateAlignedLoad(
        IntTy, Ptr, getContext().getTypeAlignInChars(E->getType()));
    Load->setVolatile(true);
    return Load;
  }
  case X86::BI__readgsbyte:
  case X86::BI__readgsword:
  case X86::BI__readgsdword:
  case X86::BI__readgsqword: {
    llvm::Type *IntTy = ConvertType(E->getType());
    Value *Ptr = Builder.CreateIntToPtr(
        Ops[0], llvm::PointerType::get(getLLVMContext(), 256));
    LoadInst *Load = Builder.CreateAlignedLoad(
        IntTy, Ptr, getContext().getTypeAlignInChars(E->getType()));
    Load->setVolatile(true);
    return Load;
  }
  case X86::BI__builtin_ia32_encodekey128_u32: {
    Intrinsic::ID IID = Intrinsic::x86_encodekey128;

    Value *Call = Builder.CreateCall(CGM.getIntrinsic(IID), {Ops[0], Ops[1]});

    for (int i = 0; i < 3; ++i) {
      Value *Extract = Builder.CreateExtractValue(Call, i + 1);
      Value *Ptr = Builder.CreateConstGEP1_32(Int8Ty, Ops[2], i * 16);
      Builder.CreateAlignedStore(Extract, Ptr, Align(1));
    }

    return Builder.CreateExtractValue(Call, 0);
  }
  case X86::BI__builtin_ia32_encodekey256_u32: {
    Intrinsic::ID IID = Intrinsic::x86_encodekey256;

    Value *Call =
        Builder.CreateCall(CGM.getIntrinsic(IID), {Ops[0], Ops[1], Ops[2]});

    for (int i = 0; i < 4; ++i) {
      Value *Extract = Builder.CreateExtractValue(Call, i + 1);
      Value *Ptr = Builder.CreateConstGEP1_32(Int8Ty, Ops[3], i * 16);
      Builder.CreateAlignedStore(Extract, Ptr, Align(1));
    }

    return Builder.CreateExtractValue(Call, 0);
  }
  case X86::BI__builtin_ia32_aesenc128kl_u8:
  case X86::BI__builtin_ia32_aesdec128kl_u8:
  case X86::BI__builtin_ia32_aesenc256kl_u8:
  case X86::BI__builtin_ia32_aesdec256kl_u8: {
    Intrinsic::ID IID;
    StringRef BlockName;
    switch (BuiltinID) {
    default:
      llvm_unreachable("Unexpected builtin");
    case X86::BI__builtin_ia32_aesenc128kl_u8:
      IID = Intrinsic::x86_aesenc128kl;
      BlockName = "aesenc128kl";
      break;
    case X86::BI__builtin_ia32_aesdec128kl_u8:
      IID = Intrinsic::x86_aesdec128kl;
      BlockName = "aesdec128kl";
      break;
    case X86::BI__builtin_ia32_aesenc256kl_u8:
      IID = Intrinsic::x86_aesenc256kl;
      BlockName = "aesenc256kl";
      break;
    case X86::BI__builtin_ia32_aesdec256kl_u8:
      IID = Intrinsic::x86_aesdec256kl;
      BlockName = "aesdec256kl";
      break;
    }

    Value *Call = Builder.CreateCall(CGM.getIntrinsic(IID), {Ops[1], Ops[2]});

    BasicBlock *NoError =
        createBasicBlock(BlockName + "_no_error", this->CurFn);
    BasicBlock *Error = createBasicBlock(BlockName + "_error", this->CurFn);
    BasicBlock *End = createBasicBlock(BlockName + "_end", this->CurFn);

    Value *Ret = Builder.CreateExtractValue(Call, 0);
    Value *Succ = Builder.CreateTrunc(Ret, Builder.getInt1Ty());
    Value *Out = Builder.CreateExtractValue(Call, 1);
    Builder.CreateCondBr(Succ, NoError, Error);

    Builder.SetInsertPoint(NoError);
    Builder.CreateDefaultAlignedStore(Out, Ops[0]);
    Builder.CreateBr(End);

    Builder.SetInsertPoint(Error);
    Constant *Zero = llvm::Constant::getNullValue(Out->getType());
    Builder.CreateDefaultAlignedStore(Zero, Ops[0]);
    Builder.CreateBr(End);

    Builder.SetInsertPoint(End);
    return Builder.CreateExtractValue(Call, 0);
  }
  case X86::BI__builtin_ia32_aesencwide128kl_u8:
  case X86::BI__builtin_ia32_aesdecwide128kl_u8:
  case X86::BI__builtin_ia32_aesencwide256kl_u8:
  case X86::BI__builtin_ia32_aesdecwide256kl_u8: {
    Intrinsic::ID IID;
    StringRef BlockName;
    switch (BuiltinID) {
    case X86::BI__builtin_ia32_aesencwide128kl_u8:
      IID = Intrinsic::x86_aesencwide128kl;
      BlockName = "aesencwide128kl";
      break;
    case X86::BI__builtin_ia32_aesdecwide128kl_u8:
      IID = Intrinsic::x86_aesdecwide128kl;
      BlockName = "aesdecwide128kl";
      break;
    case X86::BI__builtin_ia32_aesencwide256kl_u8:
      IID = Intrinsic::x86_aesencwide256kl;
      BlockName = "aesencwide256kl";
      break;
    case X86::BI__builtin_ia32_aesdecwide256kl_u8:
      IID = Intrinsic::x86_aesdecwide256kl;
      BlockName = "aesdecwide256kl";
      break;
    }

    llvm::Type *Ty = FixedVectorType::get(Builder.getInt64Ty(), 2);
    Value *InOps[9];
    InOps[0] = Ops[2];
    for (int i = 0; i != 8; ++i) {
      Value *Ptr = Builder.CreateConstGEP1_32(Ty, Ops[1], i);
      InOps[i + 1] = Builder.CreateAlignedLoad(Ty, Ptr, Align(16));
    }

    Value *Call = Builder.CreateCall(CGM.getIntrinsic(IID), InOps);

    BasicBlock *NoError =
        createBasicBlock(BlockName + "_no_error", this->CurFn);
    BasicBlock *Error = createBasicBlock(BlockName + "_error", this->CurFn);
    BasicBlock *End = createBasicBlock(BlockName + "_end", this->CurFn);

    Value *Ret = Builder.CreateExtractValue(Call, 0);
    Value *Succ = Builder.CreateTrunc(Ret, Builder.getInt1Ty());
    Builder.CreateCondBr(Succ, NoError, Error);

    Builder.SetInsertPoint(NoError);
    for (int i = 0; i != 8; ++i) {
      Value *Extract = Builder.CreateExtractValue(Call, i + 1);
      Value *Ptr = Builder.CreateConstGEP1_32(Extract->getType(), Ops[0], i);
      Builder.CreateAlignedStore(Extract, Ptr, Align(16));
    }
    Builder.CreateBr(End);

    Builder.SetInsertPoint(Error);
    for (int i = 0; i != 8; ++i) {
      Value *Out = Builder.CreateExtractValue(Call, i + 1);
      Constant *Zero = llvm::Constant::getNullValue(Out->getType());
      Value *Ptr = Builder.CreateConstGEP1_32(Out->getType(), Ops[0], i);
      Builder.CreateAlignedStore(Zero, Ptr, Align(16));
    }
    Builder.CreateBr(End);

    Builder.SetInsertPoint(End);
    return Builder.CreateExtractValue(Call, 0);
  }
  case X86::BI__builtin_ia32_vfcmaddcph512_mask:
    IsConjFMA = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddcph512_mask: {
    Intrinsic::ID IID = IsConjFMA
                            ? Intrinsic::x86_avx512fp16_mask_vfcmadd_cph_512
                            : Intrinsic::x86_avx512fp16_mask_vfmadd_cph_512;
    Value *Call = Builder.CreateCall(CGM.getIntrinsic(IID), Ops);
    return EmitX86Select(*this, Ops[3], Call, Ops[0]);
  }
  case X86::BI__builtin_ia32_vfcmaddcsh_round_mask:
    IsConjFMA = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddcsh_round_mask: {
    Intrinsic::ID IID = IsConjFMA ? Intrinsic::x86_avx512fp16_mask_vfcmadd_csh
                                  : Intrinsic::x86_avx512fp16_mask_vfmadd_csh;
    Value *Call = Builder.CreateCall(CGM.getIntrinsic(IID), Ops);
    Value *And = Builder.CreateAnd(Ops[3], llvm::ConstantInt::get(Int8Ty, 1));
    return EmitX86Select(*this, And, Call, Ops[0]);
  }
  case X86::BI__builtin_ia32_vfcmaddcsh_round_mask3:
    IsConjFMA = true;
    [[fallthrough]];
  case X86::BI__builtin_ia32_vfmaddcsh_round_mask3: {
    Intrinsic::ID IID = IsConjFMA ? Intrinsic::x86_avx512fp16_mask_vfcmadd_csh
                                  : Intrinsic::x86_avx512fp16_mask_vfmadd_csh;
    Value *Call = Builder.CreateCall(CGM.getIntrinsic(IID), Ops);
    static constexpr int Mask[] = {0, 5, 6, 7};
    return Builder.CreateShuffleVector(Call, Ops[2], Mask);
  }
  case X86::BI__builtin_ia32_prefetchi:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::prefetch, Ops[0]->getType()),
        {Ops[0], llvm::ConstantInt::get(Int32Ty, 0), Ops[1],
         llvm::ConstantInt::get(Int32Ty, 0)});
  }
}

Value *CodeGenFunction::EmitPPCBuiltinExpr(unsigned BuiltinID,
                                           const CallExpr *E) {
  // Do not emit the builtin arguments in the arguments of a function call,
  // because the evaluation order of function arguments is not specified in C++.
  // This is important when testing to ensure the arguments are emitted in the
  // same order every time. Eg:
  // Instead of:
  //   return Builder.CreateFDiv(EmitScalarExpr(E->getArg(0)),
  //                             EmitScalarExpr(E->getArg(1)), "swdiv");
  // Use:
  //   Value *Op0 = EmitScalarExpr(E->getArg(0));
  //   Value *Op1 = EmitScalarExpr(E->getArg(1));
  //   return Builder.CreateFDiv(Op0, Op1, "swdiv")

  Intrinsic::ID ID = Intrinsic::not_intrinsic;

#include "llvm/TargetParser/PPCTargetParser.def"
  auto GenAIXPPCBuiltinCpuExpr = [&](unsigned SupportMethod, unsigned FieldIdx,
                                     unsigned Mask, CmpInst::Predicate CompOp,
                                     unsigned OpValue) -> Value * {
    if (SupportMethod == BUILTIN_PPC_FALSE)
      return llvm::ConstantInt::getFalse(ConvertType(E->getType()));

    if (SupportMethod == BUILTIN_PPC_TRUE)
      return llvm::ConstantInt::getTrue(ConvertType(E->getType()));

    assert(SupportMethod <= SYS_CALL && "Invalid value for SupportMethod.");

    llvm::Value *FieldValue = nullptr;
    if (SupportMethod == USE_SYS_CONF) {
      llvm::Type *STy = llvm::StructType::get(PPC_SYSTEMCONFIG_TYPE);
      llvm::Constant *SysConf =
          CGM.CreateRuntimeVariable(STy, "_system_configuration");

      // Grab the appropriate field from _system_configuration.
      llvm::Value *Idxs[] = {ConstantInt::get(Int32Ty, 0),
                             ConstantInt::get(Int32Ty, FieldIdx)};

      FieldValue = Builder.CreateInBoundsGEP(STy, SysConf, Idxs);
      FieldValue = Builder.CreateAlignedLoad(Int32Ty, FieldValue,
                                             CharUnits::fromQuantity(4));
    } else if (SupportMethod == SYS_CALL) {
      llvm::FunctionType *FTy =
          llvm::FunctionType::get(Int64Ty, Int32Ty, false);
      llvm::FunctionCallee Func =
          CGM.CreateRuntimeFunction(FTy, "getsystemcfg");

      FieldValue =
          Builder.CreateCall(Func, {ConstantInt::get(Int32Ty, FieldIdx)});
    }
    assert(FieldValue &&
           "SupportMethod value is not defined in PPCTargetParser.def.");

    if (Mask)
      FieldValue = Builder.CreateAnd(FieldValue, Mask);

    llvm::Type *ValueType = FieldValue->getType();
    bool IsValueType64Bit = ValueType->isIntegerTy(64);
    assert(
        (IsValueType64Bit || ValueType->isIntegerTy(32)) &&
        "Only 32/64-bit integers are supported in GenAIXPPCBuiltinCpuExpr().");

    return Builder.CreateICmp(
        CompOp, FieldValue,
        ConstantInt::get(IsValueType64Bit ? Int64Ty : Int32Ty, OpValue));
  };

  switch (BuiltinID) {
  default: return nullptr;

  case Builtin::BI__builtin_cpu_is: {
    const Expr *CPUExpr = E->getArg(0)->IgnoreParenCasts();
    StringRef CPUStr = cast<clang::StringLiteral>(CPUExpr)->getString();
    llvm::Triple Triple = getTarget().getTriple();

    unsigned LinuxSupportMethod, LinuxIDValue, AIXSupportMethod, AIXIDValue;
    typedef std::tuple<unsigned, unsigned, unsigned, unsigned> CPUInfo;

    std::tie(LinuxSupportMethod, LinuxIDValue, AIXSupportMethod, AIXIDValue) =
        static_cast<CPUInfo>(StringSwitch<CPUInfo>(CPUStr)
#define PPC_CPU(NAME, Linux_SUPPORT_METHOD, LinuxID, AIX_SUPPORT_METHOD,       \
                AIXID)                                                         \
  .Case(NAME, {Linux_SUPPORT_METHOD, LinuxID, AIX_SUPPORT_METHOD, AIXID})
#include "llvm/TargetParser/PPCTargetParser.def"
                                 .Default({BUILTIN_PPC_UNSUPPORTED, 0,
                                           BUILTIN_PPC_UNSUPPORTED, 0}));

    if (Triple.isOSAIX()) {
      assert((AIXSupportMethod != BUILTIN_PPC_UNSUPPORTED) &&
             "Invalid CPU name. Missed by SemaChecking?");
      return GenAIXPPCBuiltinCpuExpr(AIXSupportMethod, AIX_SYSCON_IMPL_IDX, 0,
                                     ICmpInst::ICMP_EQ, AIXIDValue);
    }

    assert(Triple.isOSLinux() &&
           "__builtin_cpu_is() is only supported for AIX and Linux.");

    assert((LinuxSupportMethod != BUILTIN_PPC_UNSUPPORTED) &&
           "Invalid CPU name. Missed by SemaChecking?");

    if (LinuxSupportMethod == BUILTIN_PPC_FALSE)
      return llvm::ConstantInt::getFalse(ConvertType(E->getType()));

    Value *Op0 = llvm::ConstantInt::get(Int32Ty, PPC_FAWORD_CPUID);
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ppc_fixed_addr_ld);
    Value *TheCall = Builder.CreateCall(F, {Op0}, "cpu_is");
    return Builder.CreateICmpEQ(TheCall,
                                llvm::ConstantInt::get(Int32Ty, LinuxIDValue));
  }
  case Builtin::BI__builtin_cpu_supports: {
    llvm::Triple Triple = getTarget().getTriple();
    const Expr *CPUExpr = E->getArg(0)->IgnoreParenCasts();
    StringRef CPUStr = cast<clang::StringLiteral>(CPUExpr)->getString();
    if (Triple.isOSAIX()) {
      unsigned SupportMethod, FieldIdx, Mask, Value;
      CmpInst::Predicate CompOp;
      typedef std::tuple<unsigned, unsigned, unsigned, CmpInst::Predicate,
                         unsigned>
          CPUSupportType;
      std::tie(SupportMethod, FieldIdx, Mask, CompOp, Value) =
          static_cast<CPUSupportType>(StringSwitch<CPUSupportType>(CPUStr)
#define PPC_AIX_FEATURE(NAME, DESC, SUPPORT_METHOD, INDEX, MASK, COMP_OP,      \
                        VALUE)                                                 \
  .Case(NAME, {SUPPORT_METHOD, INDEX, MASK, COMP_OP, VALUE})
#include "llvm/TargetParser/PPCTargetParser.def"
                                          .Default({BUILTIN_PPC_FALSE, 0, 0,
                                                    CmpInst::Predicate(), 0}));
      return GenAIXPPCBuiltinCpuExpr(SupportMethod, FieldIdx, Mask, CompOp,
                                     Value);
    }

    assert(Triple.isOSLinux() &&
           "__builtin_cpu_supports() is only supported for AIX and Linux.");
    unsigned FeatureWord;
    unsigned BitMask;
    std::tie(FeatureWord, BitMask) =
        StringSwitch<std::pair<unsigned, unsigned>>(CPUStr)
#define PPC_LNX_FEATURE(Name, Description, EnumName, Bitmask, FA_WORD)         \
  .Case(Name, {FA_WORD, Bitmask})
#include "llvm/TargetParser/PPCTargetParser.def"
            .Default({0, 0});
    if (!BitMask)
      return Builder.getFalse();
    Value *Op0 = llvm::ConstantInt::get(Int32Ty, FeatureWord);
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ppc_fixed_addr_ld);
    Value *TheCall = Builder.CreateCall(F, {Op0}, "cpu_supports");
    Value *Mask =
        Builder.CreateAnd(TheCall, llvm::ConstantInt::get(Int32Ty, BitMask));
    return Builder.CreateICmpNE(Mask, llvm::Constant::getNullValue(Int32Ty));
#undef PPC_FAWORD_HWCAP
#undef PPC_FAWORD_HWCAP2
#undef PPC_FAWORD_CPUID
  }

  // __builtin_ppc_get_timebase is GCC 4.8+'s PowerPC-specific name for what we
  // call __builtin_readcyclecounter.
  case PPC::BI__builtin_ppc_get_timebase:
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::readcyclecounter));

  // vec_ld, vec_xl_be, vec_lvsl, vec_lvsr
  case PPC::BI__builtin_altivec_lvx:
  case PPC::BI__builtin_altivec_lvxl:
  case PPC::BI__builtin_altivec_lvebx:
  case PPC::BI__builtin_altivec_lvehx:
  case PPC::BI__builtin_altivec_lvewx:
  case PPC::BI__builtin_altivec_lvsl:
  case PPC::BI__builtin_altivec_lvsr:
  case PPC::BI__builtin_vsx_lxvd2x:
  case PPC::BI__builtin_vsx_lxvw4x:
  case PPC::BI__builtin_vsx_lxvd2x_be:
  case PPC::BI__builtin_vsx_lxvw4x_be:
  case PPC::BI__builtin_vsx_lxvl:
  case PPC::BI__builtin_vsx_lxvll:
  {
    SmallVector<Value *, 2> Ops;
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    if (!(BuiltinID == PPC::BI__builtin_vsx_lxvl ||
          BuiltinID == PPC::BI__builtin_vsx_lxvll)) {
      Ops[0] = Builder.CreateGEP(Int8Ty, Ops[1], Ops[0]);
      Ops.pop_back();
    }

    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported ld/lvsl/lvsr intrinsic!");
    case PPC::BI__builtin_altivec_lvx:
      ID = Intrinsic::ppc_altivec_lvx;
      break;
    case PPC::BI__builtin_altivec_lvxl:
      ID = Intrinsic::ppc_altivec_lvxl;
      break;
    case PPC::BI__builtin_altivec_lvebx:
      ID = Intrinsic::ppc_altivec_lvebx;
      break;
    case PPC::BI__builtin_altivec_lvehx:
      ID = Intrinsic::ppc_altivec_lvehx;
      break;
    case PPC::BI__builtin_altivec_lvewx:
      ID = Intrinsic::ppc_altivec_lvewx;
      break;
    case PPC::BI__builtin_altivec_lvsl:
      ID = Intrinsic::ppc_altivec_lvsl;
      break;
    case PPC::BI__builtin_altivec_lvsr:
      ID = Intrinsic::ppc_altivec_lvsr;
      break;
    case PPC::BI__builtin_vsx_lxvd2x:
      ID = Intrinsic::ppc_vsx_lxvd2x;
      break;
    case PPC::BI__builtin_vsx_lxvw4x:
      ID = Intrinsic::ppc_vsx_lxvw4x;
      break;
    case PPC::BI__builtin_vsx_lxvd2x_be:
      ID = Intrinsic::ppc_vsx_lxvd2x_be;
      break;
    case PPC::BI__builtin_vsx_lxvw4x_be:
      ID = Intrinsic::ppc_vsx_lxvw4x_be;
      break;
    case PPC::BI__builtin_vsx_lxvl:
      ID = Intrinsic::ppc_vsx_lxvl;
      break;
    case PPC::BI__builtin_vsx_lxvll:
      ID = Intrinsic::ppc_vsx_lxvll;
      break;
    }
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, Ops, "");
  }

  // vec_st, vec_xst_be
  case PPC::BI__builtin_altivec_stvx:
  case PPC::BI__builtin_altivec_stvxl:
  case PPC::BI__builtin_altivec_stvebx:
  case PPC::BI__builtin_altivec_stvehx:
  case PPC::BI__builtin_altivec_stvewx:
  case PPC::BI__builtin_vsx_stxvd2x:
  case PPC::BI__builtin_vsx_stxvw4x:
  case PPC::BI__builtin_vsx_stxvd2x_be:
  case PPC::BI__builtin_vsx_stxvw4x_be:
  case PPC::BI__builtin_vsx_stxvl:
  case PPC::BI__builtin_vsx_stxvll:
  {
    SmallVector<Value *, 3> Ops;
    Ops.push_back(EmitScalarExpr(E->getArg(0)));
    Ops.push_back(EmitScalarExpr(E->getArg(1)));
    Ops.push_back(EmitScalarExpr(E->getArg(2)));
    if (!(BuiltinID == PPC::BI__builtin_vsx_stxvl ||
          BuiltinID == PPC::BI__builtin_vsx_stxvll)) {
      Ops[1] = Builder.CreateGEP(Int8Ty, Ops[2], Ops[1]);
      Ops.pop_back();
    }

    switch (BuiltinID) {
    default: llvm_unreachable("Unsupported st intrinsic!");
    case PPC::BI__builtin_altivec_stvx:
      ID = Intrinsic::ppc_altivec_stvx;
      break;
    case PPC::BI__builtin_altivec_stvxl:
      ID = Intrinsic::ppc_altivec_stvxl;
      break;
    case PPC::BI__builtin_altivec_stvebx:
      ID = Intrinsic::ppc_altivec_stvebx;
      break;
    case PPC::BI__builtin_altivec_stvehx:
      ID = Intrinsic::ppc_altivec_stvehx;
      break;
    case PPC::BI__builtin_altivec_stvewx:
      ID = Intrinsic::ppc_altivec_stvewx;
      break;
    case PPC::BI__builtin_vsx_stxvd2x:
      ID = Intrinsic::ppc_vsx_stxvd2x;
      break;
    case PPC::BI__builtin_vsx_stxvw4x:
      ID = Intrinsic::ppc_vsx_stxvw4x;
      break;
    case PPC::BI__builtin_vsx_stxvd2x_be:
      ID = Intrinsic::ppc_vsx_stxvd2x_be;
      break;
    case PPC::BI__builtin_vsx_stxvw4x_be:
      ID = Intrinsic::ppc_vsx_stxvw4x_be;
      break;
    case PPC::BI__builtin_vsx_stxvl:
      ID = Intrinsic::ppc_vsx_stxvl;
      break;
    case PPC::BI__builtin_vsx_stxvll:
      ID = Intrinsic::ppc_vsx_stxvll;
      break;
    }
    llvm::Function *F = CGM.getIntrinsic(ID);
    return Builder.CreateCall(F, Ops, "");
  }
  case PPC::BI__builtin_vsx_ldrmb: {
    // Essentially boils down to performing an unaligned VMX load sequence so
    // as to avoid crossing a page boundary and then shuffling the elements
    // into the right side of the vector register.
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    int64_t NumBytes = cast<ConstantInt>(Op1)->getZExtValue();
    llvm::Type *ResTy = ConvertType(E->getType());
    bool IsLE = getTarget().isLittleEndian();

    // If the user wants the entire vector, just load the entire vector.
    if (NumBytes == 16) {
      Value *LD =
          Builder.CreateLoad(Address(Op0, ResTy, CharUnits::fromQuantity(1)));
      if (!IsLE)
        return LD;

      // Reverse the bytes on LE.
      SmallVector<int, 16> RevMask;
      for (int Idx = 0; Idx < 16; Idx++)
        RevMask.push_back(15 - Idx);
      return Builder.CreateShuffleVector(LD, LD, RevMask);
    }

    llvm::Function *Lvx = CGM.getIntrinsic(Intrinsic::ppc_altivec_lvx);
    llvm::Function *Lvs = CGM.getIntrinsic(IsLE ? Intrinsic::ppc_altivec_lvsr
                                                : Intrinsic::ppc_altivec_lvsl);
    llvm::Function *Vperm = CGM.getIntrinsic(Intrinsic::ppc_altivec_vperm);
    Value *HiMem = Builder.CreateGEP(
        Int8Ty, Op0, ConstantInt::get(Op1->getType(), NumBytes - 1));
    Value *LoLd = Builder.CreateCall(Lvx, Op0, "ld.lo");
    Value *HiLd = Builder.CreateCall(Lvx, HiMem, "ld.hi");
    Value *Mask1 = Builder.CreateCall(Lvs, Op0, "mask1");

    Op0 = IsLE ? HiLd : LoLd;
    Op1 = IsLE ? LoLd : HiLd;
    Value *AllElts = Builder.CreateCall(Vperm, {Op0, Op1, Mask1}, "shuffle1");
    Constant *Zero = llvm::Constant::getNullValue(IsLE ? ResTy : AllElts->getType());

    if (IsLE) {
      SmallVector<int, 16> Consts;
      for (int Idx = 0; Idx < 16; Idx++) {
        int Val = (NumBytes - Idx - 1 >= 0) ? (NumBytes - Idx - 1)
                                            : 16 - (NumBytes - Idx);
        Consts.push_back(Val);
      }
      return Builder.CreateShuffleVector(Builder.CreateBitCast(AllElts, ResTy),
                                         Zero, Consts);
    }
    SmallVector<Constant *, 16> Consts;
    for (int Idx = 0; Idx < 16; Idx++)
      Consts.push_back(Builder.getInt8(NumBytes + Idx));
    Value *Mask2 = ConstantVector::get(Consts);
    return Builder.CreateBitCast(
        Builder.CreateCall(Vperm, {Zero, AllElts, Mask2}, "shuffle2"), ResTy);
  }
  case PPC::BI__builtin_vsx_strmb: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    int64_t NumBytes = cast<ConstantInt>(Op1)->getZExtValue();
    bool IsLE = getTarget().isLittleEndian();
    auto StoreSubVec = [&](unsigned Width, unsigned Offset, unsigned EltNo) {
      // Storing the whole vector, simply store it on BE and reverse bytes and
      // store on LE.
      if (Width == 16) {
        Value *StVec = Op2;
        if (IsLE) {
          SmallVector<int, 16> RevMask;
          for (int Idx = 0; Idx < 16; Idx++)
            RevMask.push_back(15 - Idx);
          StVec = Builder.CreateShuffleVector(Op2, Op2, RevMask);
        }
        return Builder.CreateStore(
            StVec, Address(Op0, Op2->getType(), CharUnits::fromQuantity(1)));
      }
      auto *ConvTy = Int64Ty;
      unsigned NumElts = 0;
      switch (Width) {
      default:
        llvm_unreachable("width for stores must be a power of 2");
      case 8:
        ConvTy = Int64Ty;
        NumElts = 2;
        break;
      case 4:
        ConvTy = Int32Ty;
        NumElts = 4;
        break;
      case 2:
        ConvTy = Int16Ty;
        NumElts = 8;
        break;
      case 1:
        ConvTy = Int8Ty;
        NumElts = 16;
        break;
      }
      Value *Vec = Builder.CreateBitCast(
          Op2, llvm::FixedVectorType::get(ConvTy, NumElts));
      Value *Ptr =
          Builder.CreateGEP(Int8Ty, Op0, ConstantInt::get(Int64Ty, Offset));
      Value *Elt = Builder.CreateExtractElement(Vec, EltNo);
      if (IsLE && Width > 1) {
        Function *F = CGM.getIntrinsic(Intrinsic::bswap, ConvTy);
        Elt = Builder.CreateCall(F, Elt);
      }
      return Builder.CreateStore(
          Elt, Address(Ptr, ConvTy, CharUnits::fromQuantity(1)));
    };
    unsigned Stored = 0;
    unsigned RemainingBytes = NumBytes;
    Value *Result;
    if (NumBytes == 16)
      return StoreSubVec(16, 0, 0);
    if (NumBytes >= 8) {
      Result = StoreSubVec(8, NumBytes - 8, IsLE ? 0 : 1);
      RemainingBytes -= 8;
      Stored += 8;
    }
    if (RemainingBytes >= 4) {
      Result = StoreSubVec(4, NumBytes - Stored - 4,
                           IsLE ? (Stored >> 2) : 3 - (Stored >> 2));
      RemainingBytes -= 4;
      Stored += 4;
    }
    if (RemainingBytes >= 2) {
      Result = StoreSubVec(2, NumBytes - Stored - 2,
                           IsLE ? (Stored >> 1) : 7 - (Stored >> 1));
      RemainingBytes -= 2;
      Stored += 2;
    }
    if (RemainingBytes)
      Result =
          StoreSubVec(1, NumBytes - Stored - 1, IsLE ? Stored : 15 - Stored);
    return Result;
  }
  // Square root
  case PPC::BI__builtin_vsx_xvsqrtsp:
  case PPC::BI__builtin_vsx_xvsqrtdp: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    if (Builder.getIsFPConstrained()) {
      llvm::Function *F = CGM.getIntrinsic(
          Intrinsic::experimental_constrained_sqrt, ResultType);
      return Builder.CreateConstrainedFPCall(F, X);
    } else {
      llvm::Function *F = CGM.getIntrinsic(Intrinsic::sqrt, ResultType);
      return Builder.CreateCall(F, X);
    }
  }
  // Count leading zeros
  case PPC::BI__builtin_altivec_vclzb:
  case PPC::BI__builtin_altivec_vclzh:
  case PPC::BI__builtin_altivec_vclzw:
  case PPC::BI__builtin_altivec_vclzd: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Undef = ConstantInt::get(Builder.getInt1Ty(), false);
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ResultType);
    return Builder.CreateCall(F, {X, Undef});
  }
  case PPC::BI__builtin_altivec_vctzb:
  case PPC::BI__builtin_altivec_vctzh:
  case PPC::BI__builtin_altivec_vctzw:
  case PPC::BI__builtin_altivec_vctzd: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Undef = ConstantInt::get(Builder.getInt1Ty(), false);
    Function *F = CGM.getIntrinsic(Intrinsic::cttz, ResultType);
    return Builder.CreateCall(F, {X, Undef});
  }
  case PPC::BI__builtin_altivec_vinsd:
  case PPC::BI__builtin_altivec_vinsw:
  case PPC::BI__builtin_altivec_vinsd_elt:
  case PPC::BI__builtin_altivec_vinsw_elt: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));

    bool IsUnaligned = (BuiltinID == PPC::BI__builtin_altivec_vinsw ||
                        BuiltinID == PPC::BI__builtin_altivec_vinsd);

    bool Is32bit = (BuiltinID == PPC::BI__builtin_altivec_vinsw ||
                    BuiltinID == PPC::BI__builtin_altivec_vinsw_elt);

    // The third argument must be a compile time constant.
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Op2);
    assert(ArgCI &&
           "Third Arg to vinsw/vinsd intrinsic must be a constant integer!");

    // Valid value for the third argument is dependent on the input type and
    // builtin called.
    int ValidMaxValue = 0;
    if (IsUnaligned)
      ValidMaxValue = (Is32bit) ? 12 : 8;
    else
      ValidMaxValue = (Is32bit) ? 3 : 1;

    // Get value of third argument.
    int64_t ConstArg = ArgCI->getSExtValue();

    // Compose range checking error message.
    std::string RangeErrMsg = IsUnaligned ? "byte" : "element";
    RangeErrMsg += " number " + llvm::to_string(ConstArg);
    RangeErrMsg += " is outside of the valid range [0, ";
    RangeErrMsg += llvm::to_string(ValidMaxValue) + "]";

    // Issue error if third argument is not within the valid range.
    if (ConstArg < 0 || ConstArg > ValidMaxValue)
      CGM.Error(E->getExprLoc(), RangeErrMsg);

    // Input to vec_replace_elt is an element index, convert to byte index.
    if (!IsUnaligned) {
      ConstArg *= Is32bit ? 4 : 8;
      // Fix the constant according to endianess.
      if (getTarget().isLittleEndian())
        ConstArg = (Is32bit ? 12 : 8) - ConstArg;
    }

    ID = Is32bit ? Intrinsic::ppc_altivec_vinsw : Intrinsic::ppc_altivec_vinsd;
    Op2 = ConstantInt::getSigned(Int32Ty, ConstArg);
    // Casting input to vector int as per intrinsic definition.
    Op0 =
        Is32bit
            ? Builder.CreateBitCast(Op0, llvm::FixedVectorType::get(Int32Ty, 4))
            : Builder.CreateBitCast(Op0,
                                    llvm::FixedVectorType::get(Int64Ty, 2));
    return Builder.CreateBitCast(
        Builder.CreateCall(CGM.getIntrinsic(ID), {Op0, Op1, Op2}), ResultType);
  }
  case PPC::BI__builtin_altivec_vpopcntb:
  case PPC::BI__builtin_altivec_vpopcnth:
  case PPC::BI__builtin_altivec_vpopcntw:
  case PPC::BI__builtin_altivec_vpopcntd: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ResultType);
    return Builder.CreateCall(F, X);
  }
  case PPC::BI__builtin_altivec_vadduqm:
  case PPC::BI__builtin_altivec_vsubuqm: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    llvm::Type *Int128Ty = llvm::IntegerType::get(getLLVMContext(), 128);
    Op0 = Builder.CreateBitCast(Op0, llvm::FixedVectorType::get(Int128Ty, 1));
    Op1 = Builder.CreateBitCast(Op1, llvm::FixedVectorType::get(Int128Ty, 1));
    if (BuiltinID == PPC::BI__builtin_altivec_vadduqm)
      return Builder.CreateAdd(Op0, Op1, "vadduqm");
    else
      return Builder.CreateSub(Op0, Op1, "vsubuqm");
  }
  case PPC::BI__builtin_altivec_vaddcuq_c:
  case PPC::BI__builtin_altivec_vsubcuq_c: {
    SmallVector<Value *, 2> Ops;
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    llvm::Type *V1I128Ty = llvm::FixedVectorType::get(
        llvm::IntegerType::get(getLLVMContext(), 128), 1);
    Ops.push_back(Builder.CreateBitCast(Op0, V1I128Ty));
    Ops.push_back(Builder.CreateBitCast(Op1, V1I128Ty));
    ID = (BuiltinID == PPC::BI__builtin_altivec_vaddcuq_c)
             ? Intrinsic::ppc_altivec_vaddcuq
             : Intrinsic::ppc_altivec_vsubcuq;
    return Builder.CreateCall(CGM.getIntrinsic(ID), Ops, "");
  }
  case PPC::BI__builtin_altivec_vaddeuqm_c:
  case PPC::BI__builtin_altivec_vaddecuq_c:
  case PPC::BI__builtin_altivec_vsubeuqm_c:
  case PPC::BI__builtin_altivec_vsubecuq_c: {
    SmallVector<Value *, 3> Ops;
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    llvm::Type *V1I128Ty = llvm::FixedVectorType::get(
        llvm::IntegerType::get(getLLVMContext(), 128), 1);
    Ops.push_back(Builder.CreateBitCast(Op0, V1I128Ty));
    Ops.push_back(Builder.CreateBitCast(Op1, V1I128Ty));
    Ops.push_back(Builder.CreateBitCast(Op2, V1I128Ty));
    switch (BuiltinID) {
    default:
      llvm_unreachable("Unsupported intrinsic!");
    case PPC::BI__builtin_altivec_vaddeuqm_c:
      ID = Intrinsic::ppc_altivec_vaddeuqm;
      break;
    case PPC::BI__builtin_altivec_vaddecuq_c:
      ID = Intrinsic::ppc_altivec_vaddecuq;
      break;
    case PPC::BI__builtin_altivec_vsubeuqm_c:
      ID = Intrinsic::ppc_altivec_vsubeuqm;
      break;
    case PPC::BI__builtin_altivec_vsubecuq_c:
      ID = Intrinsic::ppc_altivec_vsubecuq;
      break;
    }
    return Builder.CreateCall(CGM.getIntrinsic(ID), Ops, "");
  }
  case PPC::BI__builtin_ppc_rldimi:
  case PPC::BI__builtin_ppc_rlwimi: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    Value *Op3 = EmitScalarExpr(E->getArg(3));
    // rldimi is 64-bit instruction, expand the intrinsic before isel to
    // leverage peephole and avoid legalization efforts.
    if (BuiltinID == PPC::BI__builtin_ppc_rldimi &&
        !getTarget().getTriple().isPPC64()) {
      Function *F = CGM.getIntrinsic(Intrinsic::fshl, Op0->getType());
      Op2 = Builder.CreateZExt(Op2, Int64Ty);
      Value *Shift = Builder.CreateCall(F, {Op0, Op0, Op2});
      return Builder.CreateOr(Builder.CreateAnd(Shift, Op3),
                              Builder.CreateAnd(Op1, Builder.CreateNot(Op3)));
    }
    return Builder.CreateCall(
        CGM.getIntrinsic(BuiltinID == PPC::BI__builtin_ppc_rldimi
                             ? Intrinsic::ppc_rldimi
                             : Intrinsic::ppc_rlwimi),
        {Op0, Op1, Op2, Op3});
  }
  case PPC::BI__builtin_ppc_rlwnm: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_rlwnm),
                              {Op0, Op1, Op2});
  }
  case PPC::BI__builtin_ppc_poppar4:
  case PPC::BI__builtin_ppc_poppar8: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    llvm::Type *ArgType = Op0->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ArgType);
    Value *Tmp = Builder.CreateCall(F, Op0);

    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Result = Builder.CreateAnd(Tmp, llvm::ConstantInt::get(ArgType, 1));
    if (Result->getType() != ResultType)
      Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                     "cast");
    return Result;
  }
  case PPC::BI__builtin_ppc_cmpb: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    if (getTarget().getTriple().isPPC64()) {
      Function *F =
          CGM.getIntrinsic(Intrinsic::ppc_cmpb, {Int64Ty, Int64Ty, Int64Ty});
      return Builder.CreateCall(F, {Op0, Op1}, "cmpb");
    }
    // For 32 bit, emit the code as below:
    // %conv = trunc i64 %a to i32
    // %conv1 = trunc i64 %b to i32
    // %shr = lshr i64 %a, 32
    // %conv2 = trunc i64 %shr to i32
    // %shr3 = lshr i64 %b, 32
    // %conv4 = trunc i64 %shr3 to i32
    // %0 = tail call i32 @llvm.ppc.cmpb32(i32 %conv, i32 %conv1)
    // %conv5 = zext i32 %0 to i64
    // %1 = tail call i32 @llvm.ppc.cmpb32(i32 %conv2, i32 %conv4)
    // %conv614 = zext i32 %1 to i64
    // %shl = shl nuw i64 %conv614, 32
    // %or = or i64 %shl, %conv5
    // ret i64 %or
    Function *F =
        CGM.getIntrinsic(Intrinsic::ppc_cmpb, {Int32Ty, Int32Ty, Int32Ty});
    Value *ArgOneLo = Builder.CreateTrunc(Op0, Int32Ty);
    Value *ArgTwoLo = Builder.CreateTrunc(Op1, Int32Ty);
    Constant *ShiftAmt = ConstantInt::get(Int64Ty, 32);
    Value *ArgOneHi =
        Builder.CreateTrunc(Builder.CreateLShr(Op0, ShiftAmt), Int32Ty);
    Value *ArgTwoHi =
        Builder.CreateTrunc(Builder.CreateLShr(Op1, ShiftAmt), Int32Ty);
    Value *ResLo = Builder.CreateZExt(
        Builder.CreateCall(F, {ArgOneLo, ArgTwoLo}, "cmpb"), Int64Ty);
    Value *ResHiShift = Builder.CreateZExt(
        Builder.CreateCall(F, {ArgOneHi, ArgTwoHi}, "cmpb"), Int64Ty);
    Value *ResHi = Builder.CreateShl(ResHiShift, ShiftAmt);
    return Builder.CreateOr(ResLo, ResHi);
  }
  // Copy sign
  case PPC::BI__builtin_vsx_xvcpsgnsp:
  case PPC::BI__builtin_vsx_xvcpsgndp: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    ID = Intrinsic::copysign;
    llvm::Function *F = CGM.getIntrinsic(ID, ResultType);
    return Builder.CreateCall(F, {X, Y});
  }
  // Rounding/truncation
  case PPC::BI__builtin_vsx_xvrspip:
  case PPC::BI__builtin_vsx_xvrdpip:
  case PPC::BI__builtin_vsx_xvrdpim:
  case PPC::BI__builtin_vsx_xvrspim:
  case PPC::BI__builtin_vsx_xvrdpi:
  case PPC::BI__builtin_vsx_xvrspi:
  case PPC::BI__builtin_vsx_xvrdpic:
  case PPC::BI__builtin_vsx_xvrspic:
  case PPC::BI__builtin_vsx_xvrdpiz:
  case PPC::BI__builtin_vsx_xvrspiz: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    if (BuiltinID == PPC::BI__builtin_vsx_xvrdpim ||
        BuiltinID == PPC::BI__builtin_vsx_xvrspim)
      ID = Builder.getIsFPConstrained()
               ? Intrinsic::experimental_constrained_floor
               : Intrinsic::floor;
    else if (BuiltinID == PPC::BI__builtin_vsx_xvrdpi ||
             BuiltinID == PPC::BI__builtin_vsx_xvrspi)
      ID = Builder.getIsFPConstrained()
               ? Intrinsic::experimental_constrained_round
               : Intrinsic::round;
    else if (BuiltinID == PPC::BI__builtin_vsx_xvrdpic ||
             BuiltinID == PPC::BI__builtin_vsx_xvrspic)
      ID = Builder.getIsFPConstrained()
               ? Intrinsic::experimental_constrained_rint
               : Intrinsic::rint;
    else if (BuiltinID == PPC::BI__builtin_vsx_xvrdpip ||
             BuiltinID == PPC::BI__builtin_vsx_xvrspip)
      ID = Builder.getIsFPConstrained()
               ? Intrinsic::experimental_constrained_ceil
               : Intrinsic::ceil;
    else if (BuiltinID == PPC::BI__builtin_vsx_xvrdpiz ||
             BuiltinID == PPC::BI__builtin_vsx_xvrspiz)
      ID = Builder.getIsFPConstrained()
               ? Intrinsic::experimental_constrained_trunc
               : Intrinsic::trunc;
    llvm::Function *F = CGM.getIntrinsic(ID, ResultType);
    return Builder.getIsFPConstrained() ? Builder.CreateConstrainedFPCall(F, X)
                                        : Builder.CreateCall(F, X);
  }

  // Absolute value
  case PPC::BI__builtin_vsx_xvabsdp:
  case PPC::BI__builtin_vsx_xvabssp: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::fabs, ResultType);
    return Builder.CreateCall(F, X);
  }

  // Fastmath by default
  case PPC::BI__builtin_ppc_recipdivf:
  case PPC::BI__builtin_ppc_recipdivd:
  case PPC::BI__builtin_ppc_rsqrtf:
  case PPC::BI__builtin_ppc_rsqrtd: {
    FastMathFlags FMF = Builder.getFastMathFlags();
    Builder.getFastMathFlags().setFast();
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));

    if (BuiltinID == PPC::BI__builtin_ppc_recipdivf ||
        BuiltinID == PPC::BI__builtin_ppc_recipdivd) {
      Value *Y = EmitScalarExpr(E->getArg(1));
      Value *FDiv = Builder.CreateFDiv(X, Y, "recipdiv");
      Builder.getFastMathFlags() &= (FMF);
      return FDiv;
    }
    auto *One = ConstantFP::get(ResultType, 1.0);
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::sqrt, ResultType);
    Value *FDiv = Builder.CreateFDiv(One, Builder.CreateCall(F, X), "rsqrt");
    Builder.getFastMathFlags() &= (FMF);
    return FDiv;
  }
  case PPC::BI__builtin_ppc_alignx: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    ConstantInt *AlignmentCI = cast<ConstantInt>(Op0);
    if (AlignmentCI->getValue().ugt(llvm::Value::MaximumAlignment))
      AlignmentCI = ConstantInt::get(AlignmentCI->getIntegerType(),
                                     llvm::Value::MaximumAlignment);

    emitAlignmentAssumption(Op1, E->getArg(1),
                            /*The expr loc is sufficient.*/ SourceLocation(),
                            AlignmentCI, nullptr);
    return Op1;
  }
  case PPC::BI__builtin_ppc_rdlam: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    llvm::Type *Ty = Op0->getType();
    Value *ShiftAmt = Builder.CreateIntCast(Op1, Ty, false);
    Function *F = CGM.getIntrinsic(Intrinsic::fshl, Ty);
    Value *Rotate = Builder.CreateCall(F, {Op0, Op0, ShiftAmt});
    return Builder.CreateAnd(Rotate, Op2);
  }
  case PPC::BI__builtin_ppc_load2r: {
    Function *F = CGM.getIntrinsic(Intrinsic::ppc_load2r);
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *LoadIntrinsic = Builder.CreateCall(F, {Op0});
    return Builder.CreateTrunc(LoadIntrinsic, Int16Ty);
  }
  // FMA variations
  case PPC::BI__builtin_ppc_fnmsub:
  case PPC::BI__builtin_ppc_fnmsubs:
  case PPC::BI__builtin_vsx_xvmaddadp:
  case PPC::BI__builtin_vsx_xvmaddasp:
  case PPC::BI__builtin_vsx_xvnmaddadp:
  case PPC::BI__builtin_vsx_xvnmaddasp:
  case PPC::BI__builtin_vsx_xvmsubadp:
  case PPC::BI__builtin_vsx_xvmsubasp:
  case PPC::BI__builtin_vsx_xvnmsubadp:
  case PPC::BI__builtin_vsx_xvnmsubasp: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    llvm::Function *F;
    if (Builder.getIsFPConstrained())
      F = CGM.getIntrinsic(Intrinsic::experimental_constrained_fma, ResultType);
    else
      F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
    switch (BuiltinID) {
      case PPC::BI__builtin_vsx_xvmaddadp:
      case PPC::BI__builtin_vsx_xvmaddasp:
        if (Builder.getIsFPConstrained())
          return Builder.CreateConstrainedFPCall(F, {X, Y, Z});
        else
          return Builder.CreateCall(F, {X, Y, Z});
      case PPC::BI__builtin_vsx_xvnmaddadp:
      case PPC::BI__builtin_vsx_xvnmaddasp:
        if (Builder.getIsFPConstrained())
          return Builder.CreateFNeg(
              Builder.CreateConstrainedFPCall(F, {X, Y, Z}), "neg");
        else
          return Builder.CreateFNeg(Builder.CreateCall(F, {X, Y, Z}), "neg");
      case PPC::BI__builtin_vsx_xvmsubadp:
      case PPC::BI__builtin_vsx_xvmsubasp:
        if (Builder.getIsFPConstrained())
          return Builder.CreateConstrainedFPCall(
              F, {X, Y, Builder.CreateFNeg(Z, "neg")});
        else
          return Builder.CreateCall(F, {X, Y, Builder.CreateFNeg(Z, "neg")});
      case PPC::BI__builtin_ppc_fnmsub:
      case PPC::BI__builtin_ppc_fnmsubs:
      case PPC::BI__builtin_vsx_xvnmsubadp:
      case PPC::BI__builtin_vsx_xvnmsubasp:
        if (Builder.getIsFPConstrained())
          return Builder.CreateFNeg(
              Builder.CreateConstrainedFPCall(
                  F, {X, Y, Builder.CreateFNeg(Z, "neg")}),
              "neg");
        else
          return Builder.CreateCall(
              CGM.getIntrinsic(Intrinsic::ppc_fnmsub, ResultType), {X, Y, Z});
      }
    llvm_unreachable("Unknown FMA operation");
    return nullptr; // Suppress no-return warning
  }

  case PPC::BI__builtin_vsx_insertword: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ppc_vsx_xxinsertw);

    // Third argument is a compile time constant int. It must be clamped to
    // to the range [0, 12].
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Op2);
    assert(ArgCI &&
           "Third arg to xxinsertw intrinsic must be constant integer");
    const int64_t MaxIndex = 12;
    int64_t Index = std::clamp(ArgCI->getSExtValue(), (int64_t)0, MaxIndex);

    // The builtin semantics don't exactly match the xxinsertw instructions
    // semantics (which ppc_vsx_xxinsertw follows). The builtin extracts the
    // word from the first argument, and inserts it in the second argument. The
    // instruction extracts the word from its second input register and inserts
    // it into its first input register, so swap the first and second arguments.
    std::swap(Op0, Op1);

    // Need to cast the second argument from a vector of unsigned int to a
    // vector of long long.
    Op1 = Builder.CreateBitCast(Op1, llvm::FixedVectorType::get(Int64Ty, 2));

    if (getTarget().isLittleEndian()) {
      // Reverse the double words in the vector we will extract from.
      Op0 = Builder.CreateBitCast(Op0, llvm::FixedVectorType::get(Int64Ty, 2));
      Op0 = Builder.CreateShuffleVector(Op0, Op0, ArrayRef<int>{1, 0});

      // Reverse the index.
      Index = MaxIndex - Index;
    }

    // Intrinsic expects the first arg to be a vector of int.
    Op0 = Builder.CreateBitCast(Op0, llvm::FixedVectorType::get(Int32Ty, 4));
    Op2 = ConstantInt::getSigned(Int32Ty, Index);
    return Builder.CreateCall(F, {Op0, Op1, Op2});
  }

  case PPC::BI__builtin_vsx_extractuword: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ppc_vsx_xxextractuw);

    // Intrinsic expects the first argument to be a vector of doublewords.
    Op0 = Builder.CreateBitCast(Op0, llvm::FixedVectorType::get(Int64Ty, 2));

    // The second argument is a compile time constant int that needs to
    // be clamped to the range [0, 12].
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Op1);
    assert(ArgCI &&
           "Second Arg to xxextractuw intrinsic must be a constant integer!");
    const int64_t MaxIndex = 12;
    int64_t Index = std::clamp(ArgCI->getSExtValue(), (int64_t)0, MaxIndex);

    if (getTarget().isLittleEndian()) {
      // Reverse the index.
      Index = MaxIndex - Index;
      Op1 = ConstantInt::getSigned(Int32Ty, Index);

      // Emit the call, then reverse the double words of the results vector.
      Value *Call = Builder.CreateCall(F, {Op0, Op1});

      Value *ShuffleCall =
          Builder.CreateShuffleVector(Call, Call, ArrayRef<int>{1, 0});
      return ShuffleCall;
    } else {
      Op1 = ConstantInt::getSigned(Int32Ty, Index);
      return Builder.CreateCall(F, {Op0, Op1});
    }
  }

  case PPC::BI__builtin_vsx_xxpermdi: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Op2);
    assert(ArgCI && "Third arg must be constant integer!");

    unsigned Index = ArgCI->getZExtValue();
    Op0 = Builder.CreateBitCast(Op0, llvm::FixedVectorType::get(Int64Ty, 2));
    Op1 = Builder.CreateBitCast(Op1, llvm::FixedVectorType::get(Int64Ty, 2));

    // Account for endianness by treating this as just a shuffle. So we use the
    // same indices for both LE and BE in order to produce expected results in
    // both cases.
    int ElemIdx0 = (Index & 2) >> 1;
    int ElemIdx1 = 2 + (Index & 1);

    int ShuffleElts[2] = {ElemIdx0, ElemIdx1};
    Value *ShuffleCall = Builder.CreateShuffleVector(Op0, Op1, ShuffleElts);
    QualType BIRetType = E->getType();
    auto RetTy = ConvertType(BIRetType);
    return Builder.CreateBitCast(ShuffleCall, RetTy);
  }

  case PPC::BI__builtin_vsx_xxsldwi: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    ConstantInt *ArgCI = dyn_cast<ConstantInt>(Op2);
    assert(ArgCI && "Third argument must be a compile time constant");
    unsigned Index = ArgCI->getZExtValue() & 0x3;
    Op0 = Builder.CreateBitCast(Op0, llvm::FixedVectorType::get(Int32Ty, 4));
    Op1 = Builder.CreateBitCast(Op1, llvm::FixedVectorType::get(Int32Ty, 4));

    // Create a shuffle mask
    int ElemIdx0;
    int ElemIdx1;
    int ElemIdx2;
    int ElemIdx3;
    if (getTarget().isLittleEndian()) {
      // Little endian element N comes from element 8+N-Index of the
      // concatenated wide vector (of course, using modulo arithmetic on
      // the total number of elements).
      ElemIdx0 = (8 - Index) % 8;
      ElemIdx1 = (9 - Index) % 8;
      ElemIdx2 = (10 - Index) % 8;
      ElemIdx3 = (11 - Index) % 8;
    } else {
      // Big endian ElemIdx<N> = Index + N
      ElemIdx0 = Index;
      ElemIdx1 = Index + 1;
      ElemIdx2 = Index + 2;
      ElemIdx3 = Index + 3;
    }

    int ShuffleElts[4] = {ElemIdx0, ElemIdx1, ElemIdx2, ElemIdx3};
    Value *ShuffleCall = Builder.CreateShuffleVector(Op0, Op1, ShuffleElts);
    QualType BIRetType = E->getType();
    auto RetTy = ConvertType(BIRetType);
    return Builder.CreateBitCast(ShuffleCall, RetTy);
  }

  case PPC::BI__builtin_pack_vector_int128: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    bool isLittleEndian = getTarget().isLittleEndian();
    Value *PoisonValue =
        llvm::PoisonValue::get(llvm::FixedVectorType::get(Op0->getType(), 2));
    Value *Res = Builder.CreateInsertElement(
        PoisonValue, Op0, (uint64_t)(isLittleEndian ? 1 : 0));
    Res = Builder.CreateInsertElement(Res, Op1,
                                      (uint64_t)(isLittleEndian ? 0 : 1));
    return Builder.CreateBitCast(Res, ConvertType(E->getType()));
  }

  case PPC::BI__builtin_unpack_vector_int128: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    ConstantInt *Index = cast<ConstantInt>(Op1);
    Value *Unpacked = Builder.CreateBitCast(
        Op0, llvm::FixedVectorType::get(ConvertType(E->getType()), 2));

    if (getTarget().isLittleEndian())
      Index =
          ConstantInt::get(Index->getIntegerType(), 1 - Index->getZExtValue());

    return Builder.CreateExtractElement(Unpacked, Index);
  }

  case PPC::BI__builtin_ppc_sthcx: {
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ppc_sthcx);
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = Builder.CreateSExt(EmitScalarExpr(E->getArg(1)), Int32Ty);
    return Builder.CreateCall(F, {Op0, Op1});
  }

  // The PPC MMA builtins take a pointer to a __vector_quad as an argument.
  // Some of the MMA instructions accumulate their result into an existing
  // accumulator whereas the others generate a new accumulator. So we need to
  // use custom code generation to expand a builtin call with a pointer to a
  // load (if the corresponding instruction accumulates its result) followed by
  // the call to the intrinsic and a store of the result.
#define CUSTOM_BUILTIN(Name, Intr, Types, Accumulate, Feature) \
  case PPC::BI__builtin_##Name:
#include "clang/Basic/BuiltinsPPC.def"
  {
    SmallVector<Value *, 4> Ops;
    for (unsigned i = 0, e = E->getNumArgs(); i != e; i++)
      if (E->getArg(i)->getType()->isArrayType())
        Ops.push_back(
            EmitArrayToPointerDecay(E->getArg(i)).emitRawPointer(*this));
      else
        Ops.push_back(EmitScalarExpr(E->getArg(i)));
    // The first argument of these two builtins is a pointer used to store their
    // result. However, the llvm intrinsics return their result in multiple
    // return values. So, here we emit code extracting these values from the
    // intrinsic results and storing them using that pointer.
    if (BuiltinID == PPC::BI__builtin_mma_disassemble_acc ||
        BuiltinID == PPC::BI__builtin_vsx_disassemble_pair ||
        BuiltinID == PPC::BI__builtin_mma_disassemble_pair) {
      unsigned NumVecs = 2;
      auto Intrinsic = Intrinsic::ppc_vsx_disassemble_pair;
      if (BuiltinID == PPC::BI__builtin_mma_disassemble_acc) {
        NumVecs = 4;
        Intrinsic = Intrinsic::ppc_mma_disassemble_acc;
      }
      llvm::Function *F = CGM.getIntrinsic(Intrinsic);
      Address Addr = EmitPointerWithAlignment(E->getArg(1));
      Value *Vec = Builder.CreateLoad(Addr);
      Value *Call = Builder.CreateCall(F, {Vec});
      llvm::Type *VTy = llvm::FixedVectorType::get(Int8Ty, 16);
      Value *Ptr = Ops[0];
      for (unsigned i=0; i<NumVecs; i++) {
        Value *Vec = Builder.CreateExtractValue(Call, i);
        llvm::ConstantInt* Index = llvm::ConstantInt::get(IntTy, i);
        Value *GEP = Builder.CreateInBoundsGEP(VTy, Ptr, Index);
        Builder.CreateAlignedStore(Vec, GEP, MaybeAlign(16));
      }
      return Call;
    }
    if (BuiltinID == PPC::BI__builtin_vsx_build_pair ||
        BuiltinID == PPC::BI__builtin_mma_build_acc) {
      // Reverse the order of the operands for LE, so the
      // same builtin call can be used on both LE and BE
      // without the need for the programmer to swap operands.
      // The operands are reversed starting from the second argument,
      // the first operand is the pointer to the pair/accumulator
      // that is being built.
      if (getTarget().isLittleEndian())
        std::reverse(Ops.begin() + 1, Ops.end());
    }
    bool Accumulate;
    switch (BuiltinID) {
  #define CUSTOM_BUILTIN(Name, Intr, Types, Acc, Feature) \
    case PPC::BI__builtin_##Name: \
      ID = Intrinsic::ppc_##Intr; \
      Accumulate = Acc; \
      break;
  #include "clang/Basic/BuiltinsPPC.def"
    }
    if (BuiltinID == PPC::BI__builtin_vsx_lxvp ||
        BuiltinID == PPC::BI__builtin_vsx_stxvp ||
        BuiltinID == PPC::BI__builtin_mma_lxvp ||
        BuiltinID == PPC::BI__builtin_mma_stxvp) {
      if (BuiltinID == PPC::BI__builtin_vsx_lxvp ||
          BuiltinID == PPC::BI__builtin_mma_lxvp) {
        Ops[0] = Builder.CreateGEP(Int8Ty, Ops[1], Ops[0]);
      } else {
        Ops[1] = Builder.CreateGEP(Int8Ty, Ops[2], Ops[1]);
      }
      Ops.pop_back();
      llvm::Function *F = CGM.getIntrinsic(ID);
      return Builder.CreateCall(F, Ops, "");
    }
    SmallVector<Value*, 4> CallOps;
    if (Accumulate) {
      Address Addr = EmitPointerWithAlignment(E->getArg(0));
      Value *Acc = Builder.CreateLoad(Addr);
      CallOps.push_back(Acc);
    }
    for (unsigned i=1; i<Ops.size(); i++)
      CallOps.push_back(Ops[i]);
    llvm::Function *F = CGM.getIntrinsic(ID);
    Value *Call = Builder.CreateCall(F, CallOps);
    return Builder.CreateAlignedStore(Call, Ops[0], MaybeAlign(64));
  }

  case PPC::BI__builtin_ppc_compare_and_swap:
  case PPC::BI__builtin_ppc_compare_and_swaplp: {
    Address Addr = EmitPointerWithAlignment(E->getArg(0));
    Address OldValAddr = EmitPointerWithAlignment(E->getArg(1));
    Value *OldVal = Builder.CreateLoad(OldValAddr);
    QualType AtomicTy = E->getArg(0)->getType()->getPointeeType();
    LValue LV = MakeAddrLValue(Addr, AtomicTy);
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    auto Pair = EmitAtomicCompareExchange(
        LV, RValue::get(OldVal), RValue::get(Op2), E->getExprLoc(),
        llvm::AtomicOrdering::Monotonic, llvm::AtomicOrdering::Monotonic, true);
    // Unlike c11's atomic_compare_exchange, according to
    // https://www.ibm.com/docs/en/xl-c-and-cpp-aix/16.1?topic=functions-compare-swap-compare-swaplp
    // > In either case, the contents of the memory location specified by addr
    // > are copied into the memory location specified by old_val_addr.
    // But it hasn't specified storing to OldValAddr is atomic or not and
    // which order to use. Now following XL's codegen, treat it as a normal
    // store.
    Value *LoadedVal = Pair.first.getScalarVal();
    Builder.CreateStore(LoadedVal, OldValAddr);
    return Builder.CreateZExt(Pair.second, Builder.getInt32Ty());
  }
  case PPC::BI__builtin_ppc_fetch_and_add:
  case PPC::BI__builtin_ppc_fetch_and_addlp: {
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Add, E,
                                 llvm::AtomicOrdering::Monotonic);
  }
  case PPC::BI__builtin_ppc_fetch_and_and:
  case PPC::BI__builtin_ppc_fetch_and_andlp: {
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::And, E,
                                 llvm::AtomicOrdering::Monotonic);
  }

  case PPC::BI__builtin_ppc_fetch_and_or:
  case PPC::BI__builtin_ppc_fetch_and_orlp: {
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Or, E,
                                 llvm::AtomicOrdering::Monotonic);
  }
  case PPC::BI__builtin_ppc_fetch_and_swap:
  case PPC::BI__builtin_ppc_fetch_and_swaplp: {
    return MakeBinaryAtomicValue(*this, AtomicRMWInst::Xchg, E,
                                 llvm::AtomicOrdering::Monotonic);
  }
  case PPC::BI__builtin_ppc_ldarx:
  case PPC::BI__builtin_ppc_lwarx:
  case PPC::BI__builtin_ppc_lharx:
  case PPC::BI__builtin_ppc_lbarx:
    return emitPPCLoadReserveIntrinsic(*this, BuiltinID, E);
  case PPC::BI__builtin_ppc_mfspr: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    llvm::Type *RetType = CGM.getDataLayout().getTypeSizeInBits(VoidPtrTy) == 32
                              ? Int32Ty
                              : Int64Ty;
    Function *F = CGM.getIntrinsic(Intrinsic::ppc_mfspr, RetType);
    return Builder.CreateCall(F, {Op0});
  }
  case PPC::BI__builtin_ppc_mtspr: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    llvm::Type *RetType = CGM.getDataLayout().getTypeSizeInBits(VoidPtrTy) == 32
                              ? Int32Ty
                              : Int64Ty;
    Function *F = CGM.getIntrinsic(Intrinsic::ppc_mtspr, RetType);
    return Builder.CreateCall(F, {Op0, Op1});
  }
  case PPC::BI__builtin_ppc_popcntb: {
    Value *ArgValue = EmitScalarExpr(E->getArg(0));
    llvm::Type *ArgType = ArgValue->getType();
    Function *F = CGM.getIntrinsic(Intrinsic::ppc_popcntb, {ArgType, ArgType});
    return Builder.CreateCall(F, {ArgValue}, "popcntb");
  }
  case PPC::BI__builtin_ppc_mtfsf: {
    // The builtin takes a uint32 that needs to be cast to an
    // f64 to be passed to the intrinsic.
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Cast = Builder.CreateUIToFP(Op1, DoubleTy);
    llvm::Function *F = CGM.getIntrinsic(Intrinsic::ppc_mtfsf);
    return Builder.CreateCall(F, {Op0, Cast}, "");
  }

  case PPC::BI__builtin_ppc_swdiv_nochk:
  case PPC::BI__builtin_ppc_swdivs_nochk: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    FastMathFlags FMF = Builder.getFastMathFlags();
    Builder.getFastMathFlags().setFast();
    Value *FDiv = Builder.CreateFDiv(Op0, Op1, "swdiv_nochk");
    Builder.getFastMathFlags() &= (FMF);
    return FDiv;
  }
  case PPC::BI__builtin_ppc_fric:
    return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
                           *this, E, Intrinsic::rint,
                           Intrinsic::experimental_constrained_rint))
        .getScalarVal();
  case PPC::BI__builtin_ppc_frim:
  case PPC::BI__builtin_ppc_frims:
    return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
                           *this, E, Intrinsic::floor,
                           Intrinsic::experimental_constrained_floor))
        .getScalarVal();
  case PPC::BI__builtin_ppc_frin:
  case PPC::BI__builtin_ppc_frins:
    return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
                           *this, E, Intrinsic::round,
                           Intrinsic::experimental_constrained_round))
        .getScalarVal();
  case PPC::BI__builtin_ppc_frip:
  case PPC::BI__builtin_ppc_frips:
    return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
                           *this, E, Intrinsic::ceil,
                           Intrinsic::experimental_constrained_ceil))
        .getScalarVal();
  case PPC::BI__builtin_ppc_friz:
  case PPC::BI__builtin_ppc_frizs:
    return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
                           *this, E, Intrinsic::trunc,
                           Intrinsic::experimental_constrained_trunc))
        .getScalarVal();
  case PPC::BI__builtin_ppc_fsqrt:
  case PPC::BI__builtin_ppc_fsqrts:
    return RValue::get(emitUnaryMaybeConstrainedFPBuiltin(
                           *this, E, Intrinsic::sqrt,
                           Intrinsic::experimental_constrained_sqrt))
        .getScalarVal();
  case PPC::BI__builtin_ppc_test_data_class: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::ppc_test_data_class, Op0->getType()),
        {Op0, Op1}, "test_data_class");
  }
  case PPC::BI__builtin_ppc_maxfe: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    Value *Op3 = EmitScalarExpr(E->getArg(3));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_maxfe),
                              {Op0, Op1, Op2, Op3});
  }
  case PPC::BI__builtin_ppc_maxfl: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    Value *Op3 = EmitScalarExpr(E->getArg(3));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_maxfl),
                              {Op0, Op1, Op2, Op3});
  }
  case PPC::BI__builtin_ppc_maxfs: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    Value *Op3 = EmitScalarExpr(E->getArg(3));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_maxfs),
                              {Op0, Op1, Op2, Op3});
  }
  case PPC::BI__builtin_ppc_minfe: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    Value *Op3 = EmitScalarExpr(E->getArg(3));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_minfe),
                              {Op0, Op1, Op2, Op3});
  }
  case PPC::BI__builtin_ppc_minfl: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    Value *Op3 = EmitScalarExpr(E->getArg(3));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_minfl),
                              {Op0, Op1, Op2, Op3});
  }
  case PPC::BI__builtin_ppc_minfs: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    Value *Op2 = EmitScalarExpr(E->getArg(2));
    Value *Op3 = EmitScalarExpr(E->getArg(3));
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_minfs),
                              {Op0, Op1, Op2, Op3});
  }
  case PPC::BI__builtin_ppc_swdiv:
  case PPC::BI__builtin_ppc_swdivs: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    return Builder.CreateFDiv(Op0, Op1, "swdiv");
  }
  case PPC::BI__builtin_ppc_set_fpscr_rn:
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_setrnd),
                              {EmitScalarExpr(E->getArg(0))});
  case PPC::BI__builtin_ppc_mffs:
    return Builder.CreateCall(CGM.getIntrinsic(Intrinsic::ppc_readflm));
  }
}

namespace {
// If \p E is not null pointer, insert address space cast to match return
// type of \p E if necessary.
Value *EmitAMDGPUDispatchPtr(CodeGenFunction &CGF,
                             const CallExpr *E = nullptr) {
  auto *F = CGF.CGM.getIntrinsic(Intrinsic::amdgcn_dispatch_ptr);
  auto *Call = CGF.Builder.CreateCall(F);
  Call->addRetAttr(
      Attribute::getWithDereferenceableBytes(Call->getContext(), 64));
  Call->addRetAttr(Attribute::getWithAlignment(Call->getContext(), Align(4)));
  if (!E)
    return Call;
  QualType BuiltinRetType = E->getType();
  auto *RetTy = cast<llvm::PointerType>(CGF.ConvertType(BuiltinRetType));
  if (RetTy == Call->getType())
    return Call;
  return CGF.Builder.CreateAddrSpaceCast(Call, RetTy);
}

Value *EmitAMDGPUImplicitArgPtr(CodeGenFunction &CGF) {
  auto *F = CGF.CGM.getIntrinsic(Intrinsic::amdgcn_implicitarg_ptr);
  auto *Call = CGF.Builder.CreateCall(F);
  Call->addRetAttr(
      Attribute::getWithDereferenceableBytes(Call->getContext(), 256));
  Call->addRetAttr(Attribute::getWithAlignment(Call->getContext(), Align(8)));
  return Call;
}

// \p Index is 0, 1, and 2 for x, y, and z dimension, respectively.
/// Emit code based on Code Object ABI version.
/// COV_4    : Emit code to use dispatch ptr
/// COV_5+   : Emit code to use implicitarg ptr
/// COV_NONE : Emit code to load a global variable "__oclc_ABI_version"
///            and use its value for COV_4 or COV_5+ approach. It is used for
///            compiling device libraries in an ABI-agnostic way.
///
/// Note: "__oclc_ABI_version" is supposed to be emitted and intialized by
///       clang during compilation of user code.
Value *EmitAMDGPUWorkGroupSize(CodeGenFunction &CGF, unsigned Index) {
  llvm::LoadInst *LD;

  auto Cov = CGF.getTarget().getTargetOpts().CodeObjectVersion;

  if (Cov == CodeObjectVersionKind::COV_None) {
    StringRef Name = "__oclc_ABI_version";
    auto *ABIVersionC = CGF.CGM.getModule().getNamedGlobal(Name);
    if (!ABIVersionC)
      ABIVersionC = new llvm::GlobalVariable(
          CGF.CGM.getModule(), CGF.Int32Ty, false,
          llvm::GlobalValue::ExternalLinkage, nullptr, Name, nullptr,
          llvm::GlobalVariable::NotThreadLocal,
          CGF.CGM.getContext().getTargetAddressSpace(LangAS::opencl_constant));

    // This load will be eliminated by the IPSCCP because it is constant
    // weak_odr without externally_initialized. Either changing it to weak or
    // adding externally_initialized will keep the load.
    Value *ABIVersion = CGF.Builder.CreateAlignedLoad(CGF.Int32Ty, ABIVersionC,
                                                      CGF.CGM.getIntAlign());

    Value *IsCOV5 = CGF.Builder.CreateICmpSGE(
        ABIVersion,
        llvm::ConstantInt::get(CGF.Int32Ty, CodeObjectVersionKind::COV_5));

    // Indexing the implicit kernarg segment.
    Value *ImplicitGEP = CGF.Builder.CreateConstGEP1_32(
        CGF.Int8Ty, EmitAMDGPUImplicitArgPtr(CGF), 12 + Index * 2);

    // Indexing the HSA kernel_dispatch_packet struct.
    Value *DispatchGEP = CGF.Builder.CreateConstGEP1_32(
        CGF.Int8Ty, EmitAMDGPUDispatchPtr(CGF), 4 + Index * 2);

    auto Result = CGF.Builder.CreateSelect(IsCOV5, ImplicitGEP, DispatchGEP);
    LD = CGF.Builder.CreateLoad(
        Address(Result, CGF.Int16Ty, CharUnits::fromQuantity(2)));
  } else {
    Value *GEP = nullptr;
    if (Cov >= CodeObjectVersionKind::COV_5) {
      // Indexing the implicit kernarg segment.
      GEP = CGF.Builder.CreateConstGEP1_32(
          CGF.Int8Ty, EmitAMDGPUImplicitArgPtr(CGF), 12 + Index * 2);
    } else {
      // Indexing the HSA kernel_dispatch_packet struct.
      GEP = CGF.Builder.CreateConstGEP1_32(
          CGF.Int8Ty, EmitAMDGPUDispatchPtr(CGF), 4 + Index * 2);
    }
    LD = CGF.Builder.CreateLoad(
        Address(GEP, CGF.Int16Ty, CharUnits::fromQuantity(2)));
  }

  llvm::MDBuilder MDHelper(CGF.getLLVMContext());
  llvm::MDNode *RNode = MDHelper.createRange(APInt(16, 1),
      APInt(16, CGF.getTarget().getMaxOpenCLWorkGroupSize() + 1));
  LD->setMetadata(llvm::LLVMContext::MD_range, RNode);
  LD->setMetadata(llvm::LLVMContext::MD_noundef,
                  llvm::MDNode::get(CGF.getLLVMContext(), std::nullopt));
  LD->setMetadata(llvm::LLVMContext::MD_invariant_load,
                  llvm::MDNode::get(CGF.getLLVMContext(), std::nullopt));
  return LD;
}

// \p Index is 0, 1, and 2 for x, y, and z dimension, respectively.
Value *EmitAMDGPUGridSize(CodeGenFunction &CGF, unsigned Index) {
  const unsigned XOffset = 12;
  auto *DP = EmitAMDGPUDispatchPtr(CGF);
  // Indexing the HSA kernel_dispatch_packet struct.
  auto *Offset = llvm::ConstantInt::get(CGF.Int32Ty, XOffset + Index * 4);
  auto *GEP = CGF.Builder.CreateGEP(CGF.Int8Ty, DP, Offset);
  auto *LD = CGF.Builder.CreateLoad(
      Address(GEP, CGF.Int32Ty, CharUnits::fromQuantity(4)));
  LD->setMetadata(llvm::LLVMContext::MD_invariant_load,
                  llvm::MDNode::get(CGF.getLLVMContext(), std::nullopt));
  return LD;
}
} // namespace

// For processing memory ordering and memory scope arguments of various
// amdgcn builtins.
// \p Order takes a C++11 comptabile memory-ordering specifier and converts
// it into LLVM's memory ordering specifier using atomic C ABI, and writes
// to \p AO. \p Scope takes a const char * and converts it into AMDGCN
// specific SyncScopeID and writes it to \p SSID.
void CodeGenFunction::ProcessOrderScopeAMDGCN(Value *Order, Value *Scope,
                                              llvm::AtomicOrdering &AO,
                                              llvm::SyncScope::ID &SSID) {
  int ord = cast<llvm::ConstantInt>(Order)->getZExtValue();

  // Map C11/C++11 memory ordering to LLVM memory ordering
  assert(llvm::isValidAtomicOrderingCABI(ord));
  switch (static_cast<llvm::AtomicOrderingCABI>(ord)) {
  case llvm::AtomicOrderingCABI::acquire:
  case llvm::AtomicOrderingCABI::consume:
    AO = llvm::AtomicOrdering::Acquire;
    break;
  case llvm::AtomicOrderingCABI::release:
    AO = llvm::AtomicOrdering::Release;
    break;
  case llvm::AtomicOrderingCABI::acq_rel:
    AO = llvm::AtomicOrdering::AcquireRelease;
    break;
  case llvm::AtomicOrderingCABI::seq_cst:
    AO = llvm::AtomicOrdering::SequentiallyConsistent;
    break;
  case llvm::AtomicOrderingCABI::relaxed:
    AO = llvm::AtomicOrdering::Monotonic;
    break;
  }

  // Some of the atomic builtins take the scope as a string name.
  StringRef scp;
  if (llvm::getConstantStringInfo(Scope, scp)) {
    SSID = getLLVMContext().getOrInsertSyncScopeID(scp);
    return;
  }

  // Older builtins had an enum argument for the memory scope.
  int scope = cast<llvm::ConstantInt>(Scope)->getZExtValue();
  switch (scope) {
  case 0: // __MEMORY_SCOPE_SYSTEM
    SSID = llvm::SyncScope::System;
    break;
  case 1: // __MEMORY_SCOPE_DEVICE
    SSID = getLLVMContext().getOrInsertSyncScopeID("agent");
    break;
  case 2: // __MEMORY_SCOPE_WRKGRP
    SSID = getLLVMContext().getOrInsertSyncScopeID("workgroup");
    break;
  case 3: // __MEMORY_SCOPE_WVFRNT
    SSID = getLLVMContext().getOrInsertSyncScopeID("wavefront");
    break;
  case 4: // __MEMORY_SCOPE_SINGLE
    SSID = llvm::SyncScope::SingleThread;
    break;
  default:
    SSID = llvm::SyncScope::System;
    break;
  }
}

llvm::Value *CodeGenFunction::EmitScalarOrConstFoldImmArg(unsigned ICEArguments,
                                                          unsigned Idx,
                                                          const CallExpr *E) {
  llvm::Value *Arg = nullptr;
  if ((ICEArguments & (1 << Idx)) == 0) {
    Arg = EmitScalarExpr(E->getArg(Idx));
  } else {
    // If this is required to be a constant, constant fold it so that we
    // know that the generated intrinsic gets a ConstantInt.
    std::optional<llvm::APSInt> Result =
        E->getArg(Idx)->getIntegerConstantExpr(getContext());
    assert(Result && "Expected argument to be a constant");
    Arg = llvm::ConstantInt::get(getLLVMContext(), *Result);
  }
  return Arg;
}

Intrinsic::ID getDotProductIntrinsic(QualType QT, int elementCount) {
  if (QT->hasFloatingRepresentation()) {
    switch (elementCount) {
    case 2:
      return Intrinsic::dx_dot2;
    case 3:
      return Intrinsic::dx_dot3;
    case 4:
      return Intrinsic::dx_dot4;
    }
  }
  if (QT->hasSignedIntegerRepresentation())
    return Intrinsic::dx_sdot;

  assert(QT->hasUnsignedIntegerRepresentation());
  return Intrinsic::dx_udot;
}

Value *CodeGenFunction::EmitHLSLBuiltinExpr(unsigned BuiltinID,
                                            const CallExpr *E) {
  if (!getLangOpts().HLSL)
    return nullptr;

  switch (BuiltinID) {
  case Builtin::BI__builtin_hlsl_elementwise_all: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    return Builder.CreateIntrinsic(
        /*ReturnType=*/llvm::Type::getInt1Ty(getLLVMContext()),
        CGM.getHLSLRuntime().getAllIntrinsic(), ArrayRef<Value *>{Op0}, nullptr,
        "hlsl.all");
  }
  case Builtin::BI__builtin_hlsl_elementwise_any: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    return Builder.CreateIntrinsic(
        /*ReturnType=*/llvm::Type::getInt1Ty(getLLVMContext()),
        CGM.getHLSLRuntime().getAnyIntrinsic(), ArrayRef<Value *>{Op0}, nullptr,
        "hlsl.any");
  }
  case Builtin::BI__builtin_hlsl_elementwise_clamp: {
    Value *OpX = EmitScalarExpr(E->getArg(0));
    Value *OpMin = EmitScalarExpr(E->getArg(1));
    Value *OpMax = EmitScalarExpr(E->getArg(2));

    QualType Ty = E->getArg(0)->getType();
    bool IsUnsigned = false;
    if (auto *VecTy = Ty->getAs<VectorType>())
      Ty = VecTy->getElementType();
    IsUnsigned = Ty->isUnsignedIntegerType();
    return Builder.CreateIntrinsic(
        /*ReturnType=*/OpX->getType(),
        IsUnsigned ? Intrinsic::dx_uclamp : Intrinsic::dx_clamp,
        ArrayRef<Value *>{OpX, OpMin, OpMax}, nullptr, "dx.clamp");
  }
  case Builtin::BI__builtin_hlsl_dot: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    Value *Op1 = EmitScalarExpr(E->getArg(1));
    llvm::Type *T0 = Op0->getType();
    llvm::Type *T1 = Op1->getType();
    if (!T0->isVectorTy() && !T1->isVectorTy()) {
      if (T0->isFloatingPointTy())
        return Builder.CreateFMul(Op0, Op1, "dx.dot");

      if (T0->isIntegerTy())
        return Builder.CreateMul(Op0, Op1, "dx.dot");

      // Bools should have been promoted
      llvm_unreachable(
          "Scalar dot product is only supported on ints and floats.");
    }
    // A VectorSplat should have happened
    assert(T0->isVectorTy() && T1->isVectorTy() &&
           "Dot product of vector and scalar is not supported.");

    // A vector sext or sitofp should have happened
    assert(T0->getScalarType() == T1->getScalarType() &&
           "Dot product of vectors need the same element types.");

    auto *VecTy0 = E->getArg(0)->getType()->getAs<VectorType>();
    [[maybe_unused]] auto *VecTy1 =
        E->getArg(1)->getType()->getAs<VectorType>();
    // A HLSLVectorTruncation should have happend
    assert(VecTy0->getNumElements() == VecTy1->getNumElements() &&
           "Dot product requires vectors to be of the same size.");

    return Builder.CreateIntrinsic(
        /*ReturnType=*/T0->getScalarType(),
        getDotProductIntrinsic(E->getArg(0)->getType(),
                               VecTy0->getNumElements()),
        ArrayRef<Value *>{Op0, Op1}, nullptr, "dx.dot");
  } break;
  case Builtin::BI__builtin_hlsl_lerp: {
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *S = EmitScalarExpr(E->getArg(2));
    if (!E->getArg(0)->getType()->hasFloatingRepresentation())
      llvm_unreachable("lerp operand must have a float representation");
    return Builder.CreateIntrinsic(
        /*ReturnType=*/X->getType(), CGM.getHLSLRuntime().getLerpIntrinsic(),
        ArrayRef<Value *>{X, Y, S}, nullptr, "hlsl.lerp");
  }
  case Builtin::BI__builtin_hlsl_elementwise_frac: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    if (!E->getArg(0)->getType()->hasFloatingRepresentation())
      llvm_unreachable("frac operand must have a float representation");
    return Builder.CreateIntrinsic(
        /*ReturnType=*/Op0->getType(), Intrinsic::dx_frac,
        ArrayRef<Value *>{Op0}, nullptr, "dx.frac");
  }
  case Builtin::BI__builtin_hlsl_elementwise_isinf: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    llvm::Type *Xty = Op0->getType();
    llvm::Type *retType = llvm::Type::getInt1Ty(this->getLLVMContext());
    if (Xty->isVectorTy()) {
      auto *XVecTy = E->getArg(0)->getType()->getAs<VectorType>();
      retType = llvm::VectorType::get(
          retType, ElementCount::getFixed(XVecTy->getNumElements()));
    }
    if (!E->getArg(0)->getType()->hasFloatingRepresentation())
      llvm_unreachable("isinf operand must have a float representation");
    return Builder.CreateIntrinsic(retType, Intrinsic::dx_isinf,
                                   ArrayRef<Value *>{Op0}, nullptr, "dx.isinf");
  }
  case Builtin::BI__builtin_hlsl_mad: {
    Value *M = EmitScalarExpr(E->getArg(0));
    Value *A = EmitScalarExpr(E->getArg(1));
    Value *B = EmitScalarExpr(E->getArg(2));
    if (E->getArg(0)->getType()->hasFloatingRepresentation())
      return Builder.CreateIntrinsic(
          /*ReturnType*/ M->getType(), Intrinsic::fmuladd,
          ArrayRef<Value *>{M, A, B}, nullptr, "hlsl.fmad");

    if (E->getArg(0)->getType()->hasSignedIntegerRepresentation()) {
      if (CGM.getTarget().getTriple().getArch() == llvm::Triple::dxil)
        return Builder.CreateIntrinsic(
            /*ReturnType*/ M->getType(), Intrinsic::dx_imad,
            ArrayRef<Value *>{M, A, B}, nullptr, "dx.imad");

      Value *Mul = Builder.CreateNSWMul(M, A);
      return Builder.CreateNSWAdd(Mul, B);
    }
    assert(E->getArg(0)->getType()->hasUnsignedIntegerRepresentation());
    if (CGM.getTarget().getTriple().getArch() == llvm::Triple::dxil)
      return Builder.CreateIntrinsic(
          /*ReturnType=*/M->getType(), Intrinsic::dx_umad,
          ArrayRef<Value *>{M, A, B}, nullptr, "dx.umad");

    Value *Mul = Builder.CreateNUWMul(M, A);
    return Builder.CreateNUWAdd(Mul, B);
  }
  case Builtin::BI__builtin_hlsl_elementwise_rcp: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    if (!E->getArg(0)->getType()->hasFloatingRepresentation())
      llvm_unreachable("rcp operand must have a float representation");
    llvm::Type *Ty = Op0->getType();
    llvm::Type *EltTy = Ty->getScalarType();
    Constant *One = Ty->isVectorTy()
                        ? ConstantVector::getSplat(
                              ElementCount::getFixed(
                                  cast<FixedVectorType>(Ty)->getNumElements()),
                              ConstantFP::get(EltTy, 1.0))
                        : ConstantFP::get(EltTy, 1.0);
    return Builder.CreateFDiv(One, Op0, "hlsl.rcp");
  }
  case Builtin::BI__builtin_hlsl_elementwise_rsqrt: {
    Value *Op0 = EmitScalarExpr(E->getArg(0));
    if (!E->getArg(0)->getType()->hasFloatingRepresentation())
      llvm_unreachable("rsqrt operand must have a float representation");
    return Builder.CreateIntrinsic(
        /*ReturnType=*/Op0->getType(), CGM.getHLSLRuntime().getRsqrtIntrinsic(),
        ArrayRef<Value *>{Op0}, nullptr, "hlsl.rsqrt");
  }
  case Builtin::BI__builtin_hlsl_wave_get_lane_index: {
    return EmitRuntimeCall(CGM.CreateRuntimeFunction(
        llvm::FunctionType::get(IntTy, {}, false), "__hlsl_wave_get_lane_index",
        {}, false, true));
  }
  }
  return nullptr;
}

void CodeGenFunction::AddAMDGPUFenceAddressSpaceMMRA(llvm::Instruction *Inst,
                                                     const CallExpr *E) {
  constexpr const char *Tag = "amdgpu-as";

  LLVMContext &Ctx = Inst->getContext();
  SmallVector<MMRAMetadata::TagT, 3> MMRAs;
  for (unsigned K = 2; K < E->getNumArgs(); ++K) {
    llvm::Value *V = EmitScalarExpr(E->getArg(K));
    StringRef AS;
    if (llvm::getConstantStringInfo(V, AS)) {
      MMRAs.push_back({Tag, AS});
      // TODO: Delete the resulting unused constant?
      continue;
    }
    CGM.Error(E->getExprLoc(),
              "expected an address space name as a string literal");
  }

  llvm::sort(MMRAs);
  MMRAs.erase(llvm::unique(MMRAs), MMRAs.end());
  Inst->setMetadata(LLVMContext::MD_mmra, MMRAMetadata::getMD(Ctx, MMRAs));
}

Value *CodeGenFunction::EmitAMDGPUBuiltinExpr(unsigned BuiltinID,
                                              const CallExpr *E) {
  llvm::AtomicOrdering AO = llvm::AtomicOrdering::SequentiallyConsistent;
  llvm::SyncScope::ID SSID;
  switch (BuiltinID) {
  case AMDGPU::BI__builtin_amdgcn_div_scale:
  case AMDGPU::BI__builtin_amdgcn_div_scalef: {
    // Translate from the intrinsics's struct return to the builtin's out
    // argument.

    Address FlagOutPtr = EmitPointerWithAlignment(E->getArg(3));

    llvm::Value *X = EmitScalarExpr(E->getArg(0));
    llvm::Value *Y = EmitScalarExpr(E->getArg(1));
    llvm::Value *Z = EmitScalarExpr(E->getArg(2));

    llvm::Function *Callee = CGM.getIntrinsic(Intrinsic::amdgcn_div_scale,
                                           X->getType());

    llvm::Value *Tmp = Builder.CreateCall(Callee, {X, Y, Z});

    llvm::Value *Result = Builder.CreateExtractValue(Tmp, 0);
    llvm::Value *Flag = Builder.CreateExtractValue(Tmp, 1);

    llvm::Type *RealFlagType = FlagOutPtr.getElementType();

    llvm::Value *FlagExt = Builder.CreateZExt(Flag, RealFlagType);
    Builder.CreateStore(FlagExt, FlagOutPtr);
    return Result;
  }
  case AMDGPU::BI__builtin_amdgcn_div_fmas:
  case AMDGPU::BI__builtin_amdgcn_div_fmasf: {
    llvm::Value *Src0 = EmitScalarExpr(E->getArg(0));
    llvm::Value *Src1 = EmitScalarExpr(E->getArg(1));
    llvm::Value *Src2 = EmitScalarExpr(E->getArg(2));
    llvm::Value *Src3 = EmitScalarExpr(E->getArg(3));

    llvm::Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_div_fmas,
                                      Src0->getType());
    llvm::Value *Src3ToBool = Builder.CreateIsNotNull(Src3);
    return Builder.CreateCall(F, {Src0, Src1, Src2, Src3ToBool});
  }

  case AMDGPU::BI__builtin_amdgcn_ds_swizzle:
    return emitBuiltinWithOneOverloadedType<2>(*this, E,
                                               Intrinsic::amdgcn_ds_swizzle);
  case AMDGPU::BI__builtin_amdgcn_mov_dpp8:
    return emitBuiltinWithOneOverloadedType<2>(*this, E,
                                               Intrinsic::amdgcn_mov_dpp8);
  case AMDGPU::BI__builtin_amdgcn_mov_dpp:
  case AMDGPU::BI__builtin_amdgcn_update_dpp: {
    llvm::SmallVector<llvm::Value *, 6> Args;
    // Find out if any arguments are required to be integer constant
    // expressions.
    unsigned ICEArguments = 0;
    ASTContext::GetBuiltinTypeError Error;
    getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
    assert(Error == ASTContext::GE_None && "Should not codegen an error");
    for (unsigned I = 0; I != E->getNumArgs(); ++I) {
      Args.push_back(EmitScalarOrConstFoldImmArg(ICEArguments, I, E));
    }
    assert(Args.size() == 5 || Args.size() == 6);
    if (Args.size() == 5)
      Args.insert(Args.begin(), llvm::PoisonValue::get(Args[0]->getType()));
    Function *F =
        CGM.getIntrinsic(Intrinsic::amdgcn_update_dpp, Args[0]->getType());
    return Builder.CreateCall(F, Args);
  }
  case AMDGPU::BI__builtin_amdgcn_permlane16:
  case AMDGPU::BI__builtin_amdgcn_permlanex16:
    return emitBuiltinWithOneOverloadedType<6>(
        *this, E,
        BuiltinID == AMDGPU::BI__builtin_amdgcn_permlane16
            ? Intrinsic::amdgcn_permlane16
            : Intrinsic::amdgcn_permlanex16);
  case AMDGPU::BI__builtin_amdgcn_permlane64:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::amdgcn_permlane64);
  case AMDGPU::BI__builtin_amdgcn_readlane:
    return emitBuiltinWithOneOverloadedType<2>(*this, E,
                                               Intrinsic::amdgcn_readlane);
  case AMDGPU::BI__builtin_amdgcn_readfirstlane:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::amdgcn_readfirstlane);
  case AMDGPU::BI__builtin_amdgcn_div_fixup:
  case AMDGPU::BI__builtin_amdgcn_div_fixupf:
  case AMDGPU::BI__builtin_amdgcn_div_fixuph:
    return emitBuiltinWithOneOverloadedType<3>(*this, E,
                                               Intrinsic::amdgcn_div_fixup);
  case AMDGPU::BI__builtin_amdgcn_trig_preop:
  case AMDGPU::BI__builtin_amdgcn_trig_preopf:
    return emitFPIntBuiltin(*this, E, Intrinsic::amdgcn_trig_preop);
  case AMDGPU::BI__builtin_amdgcn_rcp:
  case AMDGPU::BI__builtin_amdgcn_rcpf:
  case AMDGPU::BI__builtin_amdgcn_rcph:
    return emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::amdgcn_rcp);
  case AMDGPU::BI__builtin_amdgcn_sqrt:
  case AMDGPU::BI__builtin_amdgcn_sqrtf:
  case AMDGPU::BI__builtin_amdgcn_sqrth:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::amdgcn_sqrt);
  case AMDGPU::BI__builtin_amdgcn_rsq:
  case AMDGPU::BI__builtin_amdgcn_rsqf:
  case AMDGPU::BI__builtin_amdgcn_rsqh:
    return emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::amdgcn_rsq);
  case AMDGPU::BI__builtin_amdgcn_rsq_clamp:
  case AMDGPU::BI__builtin_amdgcn_rsq_clampf:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::amdgcn_rsq_clamp);
  case AMDGPU::BI__builtin_amdgcn_sinf:
  case AMDGPU::BI__builtin_amdgcn_sinh:
    return emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::amdgcn_sin);
  case AMDGPU::BI__builtin_amdgcn_cosf:
  case AMDGPU::BI__builtin_amdgcn_cosh:
    return emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::amdgcn_cos);
  case AMDGPU::BI__builtin_amdgcn_dispatch_ptr:
    return EmitAMDGPUDispatchPtr(*this, E);
  case AMDGPU::BI__builtin_amdgcn_logf:
    return emitBuiltinWithOneOverloadedType<1>(*this, E, Intrinsic::amdgcn_log);
  case AMDGPU::BI__builtin_amdgcn_exp2f:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::amdgcn_exp2);
  case AMDGPU::BI__builtin_amdgcn_log_clampf:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::amdgcn_log_clamp);
  case AMDGPU::BI__builtin_amdgcn_ldexp:
  case AMDGPU::BI__builtin_amdgcn_ldexpf: {
    llvm::Value *Src0 = EmitScalarExpr(E->getArg(0));
    llvm::Value *Src1 = EmitScalarExpr(E->getArg(1));
    llvm::Function *F =
        CGM.getIntrinsic(Intrinsic::ldexp, {Src0->getType(), Src1->getType()});
    return Builder.CreateCall(F, {Src0, Src1});
  }
  case AMDGPU::BI__builtin_amdgcn_ldexph: {
    // The raw instruction has a different behavior for out of bounds exponent
    // values (implicit truncation instead of saturate to short_min/short_max).
    llvm::Value *Src0 = EmitScalarExpr(E->getArg(0));
    llvm::Value *Src1 = EmitScalarExpr(E->getArg(1));
    llvm::Function *F =
        CGM.getIntrinsic(Intrinsic::ldexp, {Src0->getType(), Int16Ty});
    return Builder.CreateCall(F, {Src0, Builder.CreateTrunc(Src1, Int16Ty)});
  }
  case AMDGPU::BI__builtin_amdgcn_frexp_mant:
  case AMDGPU::BI__builtin_amdgcn_frexp_mantf:
  case AMDGPU::BI__builtin_amdgcn_frexp_manth:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::amdgcn_frexp_mant);
  case AMDGPU::BI__builtin_amdgcn_frexp_exp:
  case AMDGPU::BI__builtin_amdgcn_frexp_expf: {
    Value *Src0 = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_frexp_exp,
                                { Builder.getInt32Ty(), Src0->getType() });
    return Builder.CreateCall(F, Src0);
  }
  case AMDGPU::BI__builtin_amdgcn_frexp_exph: {
    Value *Src0 = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_frexp_exp,
                                { Builder.getInt16Ty(), Src0->getType() });
    return Builder.CreateCall(F, Src0);
  }
  case AMDGPU::BI__builtin_amdgcn_fract:
  case AMDGPU::BI__builtin_amdgcn_fractf:
  case AMDGPU::BI__builtin_amdgcn_fracth:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::amdgcn_fract);
  case AMDGPU::BI__builtin_amdgcn_lerp:
    return emitBuiltinWithOneOverloadedType<3>(*this, E,
                                               Intrinsic::amdgcn_lerp);
  case AMDGPU::BI__builtin_amdgcn_ubfe:
    return emitBuiltinWithOneOverloadedType<3>(*this, E,
                                               Intrinsic::amdgcn_ubfe);
  case AMDGPU::BI__builtin_amdgcn_sbfe:
    return emitBuiltinWithOneOverloadedType<3>(*this, E,
                                               Intrinsic::amdgcn_sbfe);
  case AMDGPU::BI__builtin_amdgcn_ballot_w32:
  case AMDGPU::BI__builtin_amdgcn_ballot_w64: {
    llvm::Type *ResultType = ConvertType(E->getType());
    llvm::Value *Src = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_ballot, { ResultType });
    return Builder.CreateCall(F, { Src });
  }
  case AMDGPU::BI__builtin_amdgcn_uicmp:
  case AMDGPU::BI__builtin_amdgcn_uicmpl:
  case AMDGPU::BI__builtin_amdgcn_sicmp:
  case AMDGPU::BI__builtin_amdgcn_sicmpl: {
    llvm::Value *Src0 = EmitScalarExpr(E->getArg(0));
    llvm::Value *Src1 = EmitScalarExpr(E->getArg(1));
    llvm::Value *Src2 = EmitScalarExpr(E->getArg(2));

    // FIXME-GFX10: How should 32 bit mask be handled?
    Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_icmp,
      { Builder.getInt64Ty(), Src0->getType() });
    return Builder.CreateCall(F, { Src0, Src1, Src2 });
  }
  case AMDGPU::BI__builtin_amdgcn_fcmp:
  case AMDGPU::BI__builtin_amdgcn_fcmpf: {
    llvm::Value *Src0 = EmitScalarExpr(E->getArg(0));
    llvm::Value *Src1 = EmitScalarExpr(E->getArg(1));
    llvm::Value *Src2 = EmitScalarExpr(E->getArg(2));

    // FIXME-GFX10: How should 32 bit mask be handled?
    Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_fcmp,
      { Builder.getInt64Ty(), Src0->getType() });
    return Builder.CreateCall(F, { Src0, Src1, Src2 });
  }
  case AMDGPU::BI__builtin_amdgcn_class:
  case AMDGPU::BI__builtin_amdgcn_classf:
  case AMDGPU::BI__builtin_amdgcn_classh:
    return emitFPIntBuiltin(*this, E, Intrinsic::amdgcn_class);
  case AMDGPU::BI__builtin_amdgcn_fmed3f:
  case AMDGPU::BI__builtin_amdgcn_fmed3h:
    return emitBuiltinWithOneOverloadedType<3>(*this, E,
                                               Intrinsic::amdgcn_fmed3);
  case AMDGPU::BI__builtin_amdgcn_ds_append:
  case AMDGPU::BI__builtin_amdgcn_ds_consume: {
    Intrinsic::ID Intrin = BuiltinID == AMDGPU::BI__builtin_amdgcn_ds_append ?
      Intrinsic::amdgcn_ds_append : Intrinsic::amdgcn_ds_consume;
    Value *Src0 = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrin, { Src0->getType() });
    return Builder.CreateCall(F, { Src0, Builder.getFalse() });
  }
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_f64:
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_f32:
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_v2f16:
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fmin_f64:
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fmax_f64:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_f64:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fmin_f64:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fmax_f64:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_f32:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_v2f16: {
    Intrinsic::ID IID;
    llvm::Type *ArgTy = llvm::Type::getDoubleTy(getLLVMContext());
    switch (BuiltinID) {
    case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_f32:
      ArgTy = llvm::Type::getFloatTy(getLLVMContext());
      IID = Intrinsic::amdgcn_global_atomic_fadd;
      break;
    case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_v2f16:
      ArgTy = llvm::FixedVectorType::get(
          llvm::Type::getHalfTy(getLLVMContext()), 2);
      IID = Intrinsic::amdgcn_global_atomic_fadd;
      break;
    case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_f64:
      IID = Intrinsic::amdgcn_global_atomic_fadd;
      break;
    case AMDGPU::BI__builtin_amdgcn_global_atomic_fmin_f64:
      IID = Intrinsic::amdgcn_global_atomic_fmin;
      break;
    case AMDGPU::BI__builtin_amdgcn_global_atomic_fmax_f64:
      IID = Intrinsic::amdgcn_global_atomic_fmax;
      break;
    case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_f64:
      IID = Intrinsic::amdgcn_flat_atomic_fadd;
      break;
    case AMDGPU::BI__builtin_amdgcn_flat_atomic_fmin_f64:
      IID = Intrinsic::amdgcn_flat_atomic_fmin;
      break;
    case AMDGPU::BI__builtin_amdgcn_flat_atomic_fmax_f64:
      IID = Intrinsic::amdgcn_flat_atomic_fmax;
      break;
    case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_f32:
      ArgTy = llvm::Type::getFloatTy(getLLVMContext());
      IID = Intrinsic::amdgcn_flat_atomic_fadd;
      break;
    case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_v2f16:
      ArgTy = llvm::FixedVectorType::get(
          llvm::Type::getHalfTy(getLLVMContext()), 2);
      IID = Intrinsic::amdgcn_flat_atomic_fadd;
      break;
    }
    llvm::Value *Addr = EmitScalarExpr(E->getArg(0));
    llvm::Value *Val = EmitScalarExpr(E->getArg(1));
    llvm::Function *F =
        CGM.getIntrinsic(IID, {ArgTy, Addr->getType(), Val->getType()});
    return Builder.CreateCall(F, {Addr, Val});
  }
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_v2bf16:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_v2bf16: {
    Intrinsic::ID IID;
    switch (BuiltinID) {
    case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_v2bf16:
      IID = Intrinsic::amdgcn_global_atomic_fadd_v2bf16;
      break;
    case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_v2bf16:
      IID = Intrinsic::amdgcn_flat_atomic_fadd_v2bf16;
      break;
    }
    llvm::Value *Addr = EmitScalarExpr(E->getArg(0));
    llvm::Value *Val = EmitScalarExpr(E->getArg(1));
    llvm::Function *F = CGM.getIntrinsic(IID, {Addr->getType()});
    return Builder.CreateCall(F, {Addr, Val});
  }
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b64_i32:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b64_v2i32:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4i16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4f16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4bf16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8i16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8f16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8bf16: {

    Intrinsic::ID IID;
    switch (BuiltinID) {
    case AMDGPU::BI__builtin_amdgcn_global_load_tr_b64_i32:
    case AMDGPU::BI__builtin_amdgcn_global_load_tr_b64_v2i32:
      IID = Intrinsic::amdgcn_global_load_tr_b64;
      break;
    case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4i16:
    case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4f16:
    case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4bf16:
    case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8i16:
    case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8f16:
    case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8bf16:
      IID = Intrinsic::amdgcn_global_load_tr_b128;
      break;
    }
    llvm::Type *LoadTy = ConvertType(E->getType());
    llvm::Value *Addr = EmitScalarExpr(E->getArg(0));
    llvm::Function *F = CGM.getIntrinsic(IID, {LoadTy});
    return Builder.CreateCall(F, {Addr});
  }
  case AMDGPU::BI__builtin_amdgcn_get_fpenv: {
    Function *F = CGM.getIntrinsic(Intrinsic::get_fpenv,
                                   {llvm::Type::getInt64Ty(getLLVMContext())});
    return Builder.CreateCall(F);
  }
  case AMDGPU::BI__builtin_amdgcn_set_fpenv: {
    Function *F = CGM.getIntrinsic(Intrinsic::set_fpenv,
                                   {llvm::Type::getInt64Ty(getLLVMContext())});
    llvm::Value *Env = EmitScalarExpr(E->getArg(0));
    return Builder.CreateCall(F, {Env});
  }
  case AMDGPU::BI__builtin_amdgcn_read_exec:
    return EmitAMDGCNBallotForExec(*this, E, Int64Ty, Int64Ty, false);
  case AMDGPU::BI__builtin_amdgcn_read_exec_lo:
    return EmitAMDGCNBallotForExec(*this, E, Int32Ty, Int32Ty, false);
  case AMDGPU::BI__builtin_amdgcn_read_exec_hi:
    return EmitAMDGCNBallotForExec(*this, E, Int64Ty, Int64Ty, true);
  case AMDGPU::BI__builtin_amdgcn_image_bvh_intersect_ray:
  case AMDGPU::BI__builtin_amdgcn_image_bvh_intersect_ray_h:
  case AMDGPU::BI__builtin_amdgcn_image_bvh_intersect_ray_l:
  case AMDGPU::BI__builtin_amdgcn_image_bvh_intersect_ray_lh: {
    llvm::Value *NodePtr = EmitScalarExpr(E->getArg(0));
    llvm::Value *RayExtent = EmitScalarExpr(E->getArg(1));
    llvm::Value *RayOrigin = EmitScalarExpr(E->getArg(2));
    llvm::Value *RayDir = EmitScalarExpr(E->getArg(3));
    llvm::Value *RayInverseDir = EmitScalarExpr(E->getArg(4));
    llvm::Value *TextureDescr = EmitScalarExpr(E->getArg(5));

    // The builtins take these arguments as vec4 where the last element is
    // ignored. The intrinsic takes them as vec3.
    RayOrigin = Builder.CreateShuffleVector(RayOrigin, RayOrigin,
                                            ArrayRef<int>{0, 1, 2});
    RayDir =
        Builder.CreateShuffleVector(RayDir, RayDir, ArrayRef<int>{0, 1, 2});
    RayInverseDir = Builder.CreateShuffleVector(RayInverseDir, RayInverseDir,
                                                ArrayRef<int>{0, 1, 2});

    Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_image_bvh_intersect_ray,
                                   {NodePtr->getType(), RayDir->getType()});
    return Builder.CreateCall(F, {NodePtr, RayExtent, RayOrigin, RayDir,
                                  RayInverseDir, TextureDescr});
  }

  case AMDGPU::BI__builtin_amdgcn_ds_bvh_stack_rtn: {
    SmallVector<Value *, 4> Args;
    for (int i = 0, e = E->getNumArgs(); i != e; ++i)
      Args.push_back(EmitScalarExpr(E->getArg(i)));

    Function *F = CGM.getIntrinsic(Intrinsic::amdgcn_ds_bvh_stack_rtn);
    Value *Call = Builder.CreateCall(F, Args);
    Value *Rtn = Builder.CreateExtractValue(Call, 0);
    Value *A = Builder.CreateExtractValue(Call, 1);
    llvm::Type *RetTy = ConvertType(E->getType());
    Value *I0 = Builder.CreateInsertElement(PoisonValue::get(RetTy), Rtn,
                                            (uint64_t)0);
    return Builder.CreateInsertElement(I0, A, 1);
  }

  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_bf8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_bf8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_fp8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_fp8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_bf8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_bf8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x32_iu4_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x32_iu4_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_f16_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_f16_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf16_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf16_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x32_f16_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x32_f16_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_bf16_16x16x32_bf16_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_bf16_16x16x32_bf16_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu8_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu4_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu4_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x64_iu4_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x64_iu4_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_bf8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_bf8_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_fp8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_fp8_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_bf8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_bf8_w64: {

    // These operations perform a matrix multiplication and accumulation of
    // the form:
    //             D = A * B + C
    // We need to specify one type for matrices AB and one for matrices CD.
    // Sparse matrix operations can have different types for A and B as well as
    // an additional type for sparsity index.
    // Destination type should be put before types used for source operands.
    SmallVector<unsigned, 2> ArgsForMatchingMatrixTypes;
    // On GFX12, the intrinsics with 16-bit accumulator use a packed layout.
    // There is no need for the variable opsel argument, so always set it to
    // "false".
    bool AppendFalseForOpselArg = false;
    unsigned BuiltinWMMAOp;

    switch (BuiltinID) {
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w32:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w64:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w64_gfx12:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_f32_16x16x16_f16;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w32:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w64:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w64_gfx12:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_f32_16x16x16_bf16;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w64_gfx12:
      AppendFalseForOpselArg = true;
      [[fallthrough]];
    case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w32:
    case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w64:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_f16_16x16x16_f16;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w64_gfx12:
      AppendFalseForOpselArg = true;
      [[fallthrough]];
    case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w32:
    case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w64:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_bf16_16x16x16_bf16;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w32:
    case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w64:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_f16_16x16x16_f16_tied;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w32:
    case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w64:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_bf16_16x16x16_bf16_tied;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w32:
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w64:
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w64_gfx12:
      ArgsForMatchingMatrixTypes = {4, 1}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_i32_16x16x16_iu8;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w32:
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w64:
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w64_gfx12:
      ArgsForMatchingMatrixTypes = {4, 1}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_i32_16x16x16_iu4;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w64_gfx12:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_f32_16x16x16_fp8_fp8;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_bf8_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_bf8_w64_gfx12:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_f32_16x16x16_fp8_bf8;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_fp8_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_fp8_w64_gfx12:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_f32_16x16x16_bf8_fp8;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_bf8_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_bf8_w64_gfx12:
      ArgsForMatchingMatrixTypes = {2, 0}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_f32_16x16x16_bf8_bf8;
      break;
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x32_iu4_w32_gfx12:
    case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x32_iu4_w64_gfx12:
      ArgsForMatchingMatrixTypes = {4, 1}; // CD, AB
      BuiltinWMMAOp = Intrinsic::amdgcn_wmma_i32_16x16x32_iu4;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_f16_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_f16_w64:
      ArgsForMatchingMatrixTypes = {2, 0, 1, 3}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_f32_16x16x32_f16;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf16_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf16_w64:
      ArgsForMatchingMatrixTypes = {2, 0, 1, 3}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_f32_16x16x32_bf16;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x32_f16_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x32_f16_w64:
      ArgsForMatchingMatrixTypes = {2, 0, 1, 3}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_f16_16x16x32_f16;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_bf16_16x16x32_bf16_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_bf16_16x16x32_bf16_w64:
      ArgsForMatchingMatrixTypes = {2, 0, 1, 3}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_bf16_16x16x32_bf16;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu8_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu8_w64:
      ArgsForMatchingMatrixTypes = {4, 1, 3, 5}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_i32_16x16x32_iu8;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu4_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu4_w64:
      ArgsForMatchingMatrixTypes = {4, 1, 3, 5}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_i32_16x16x32_iu4;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x64_iu4_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x64_iu4_w64:
      ArgsForMatchingMatrixTypes = {4, 1, 3, 5}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_i32_16x16x64_iu4;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w64:
      ArgsForMatchingMatrixTypes = {2, 0, 1, 3}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_f32_16x16x32_fp8_fp8;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_bf8_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_bf8_w64:
      ArgsForMatchingMatrixTypes = {2, 0, 1, 3}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_f32_16x16x32_fp8_bf8;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_fp8_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_fp8_w64:
      ArgsForMatchingMatrixTypes = {2, 0, 1, 3}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_f32_16x16x32_bf8_fp8;
      break;
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_bf8_w32:
    case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_bf8_w64:
      ArgsForMatchingMatrixTypes = {2, 0, 1, 3}; // CD, A, B, Index
      BuiltinWMMAOp = Intrinsic::amdgcn_swmmac_f32_16x16x32_bf8_bf8;
      break;
    }

    SmallVector<Value *, 6> Args;
    for (int i = 0, e = E->getNumArgs(); i != e; ++i)
      Args.push_back(EmitScalarExpr(E->getArg(i)));
    if (AppendFalseForOpselArg)
      Args.push_back(Builder.getFalse());

    SmallVector<llvm::Type *, 6> ArgTypes;
    for (auto ArgIdx : ArgsForMatchingMatrixTypes)
      ArgTypes.push_back(Args[ArgIdx]->getType());

    Function *F = CGM.getIntrinsic(BuiltinWMMAOp, ArgTypes);
    return Builder.CreateCall(F, Args);
  }

  // amdgcn workitem
  case AMDGPU::BI__builtin_amdgcn_workitem_id_x:
    return emitRangedBuiltin(*this, Intrinsic::amdgcn_workitem_id_x, 0, 1024);
  case AMDGPU::BI__builtin_amdgcn_workitem_id_y:
    return emitRangedBuiltin(*this, Intrinsic::amdgcn_workitem_id_y, 0, 1024);
  case AMDGPU::BI__builtin_amdgcn_workitem_id_z:
    return emitRangedBuiltin(*this, Intrinsic::amdgcn_workitem_id_z, 0, 1024);

  // amdgcn workgroup size
  case AMDGPU::BI__builtin_amdgcn_workgroup_size_x:
    return EmitAMDGPUWorkGroupSize(*this, 0);
  case AMDGPU::BI__builtin_amdgcn_workgroup_size_y:
    return EmitAMDGPUWorkGroupSize(*this, 1);
  case AMDGPU::BI__builtin_amdgcn_workgroup_size_z:
    return EmitAMDGPUWorkGroupSize(*this, 2);

  // amdgcn grid size
  case AMDGPU::BI__builtin_amdgcn_grid_size_x:
    return EmitAMDGPUGridSize(*this, 0);
  case AMDGPU::BI__builtin_amdgcn_grid_size_y:
    return EmitAMDGPUGridSize(*this, 1);
  case AMDGPU::BI__builtin_amdgcn_grid_size_z:
    return EmitAMDGPUGridSize(*this, 2);

  // r600 intrinsics
  case AMDGPU::BI__builtin_r600_recipsqrt_ieee:
  case AMDGPU::BI__builtin_r600_recipsqrt_ieeef:
    return emitBuiltinWithOneOverloadedType<1>(*this, E,
                                               Intrinsic::r600_recipsqrt_ieee);
  case AMDGPU::BI__builtin_r600_read_tidig_x:
    return emitRangedBuiltin(*this, Intrinsic::r600_read_tidig_x, 0, 1024);
  case AMDGPU::BI__builtin_r600_read_tidig_y:
    return emitRangedBuiltin(*this, Intrinsic::r600_read_tidig_y, 0, 1024);
  case AMDGPU::BI__builtin_r600_read_tidig_z:
    return emitRangedBuiltin(*this, Intrinsic::r600_read_tidig_z, 0, 1024);
  case AMDGPU::BI__builtin_amdgcn_alignbit: {
    llvm::Value *Src0 = EmitScalarExpr(E->getArg(0));
    llvm::Value *Src1 = EmitScalarExpr(E->getArg(1));
    llvm::Value *Src2 = EmitScalarExpr(E->getArg(2));
    Function *F = CGM.getIntrinsic(Intrinsic::fshr, Src0->getType());
    return Builder.CreateCall(F, { Src0, Src1, Src2 });
  }
  case AMDGPU::BI__builtin_amdgcn_fence: {
    ProcessOrderScopeAMDGCN(EmitScalarExpr(E->getArg(0)),
                            EmitScalarExpr(E->getArg(1)), AO, SSID);
    FenceInst *Fence = Builder.CreateFence(AO, SSID);
    if (E->getNumArgs() > 2)
      AddAMDGPUFenceAddressSpaceMMRA(Fence, E);
    return Fence;
  }
  case AMDGPU::BI__builtin_amdgcn_atomic_inc32:
  case AMDGPU::BI__builtin_amdgcn_atomic_inc64:
  case AMDGPU::BI__builtin_amdgcn_atomic_dec32:
  case AMDGPU::BI__builtin_amdgcn_atomic_dec64:
  case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_f64:
  case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_f32:
  case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_v2f16:
  case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_v2bf16:
  case AMDGPU::BI__builtin_amdgcn_ds_faddf:
  case AMDGPU::BI__builtin_amdgcn_ds_fminf:
  case AMDGPU::BI__builtin_amdgcn_ds_fmaxf: {
    llvm::AtomicRMWInst::BinOp BinOp;
    switch (BuiltinID) {
    case AMDGPU::BI__builtin_amdgcn_atomic_inc32:
    case AMDGPU::BI__builtin_amdgcn_atomic_inc64:
      BinOp = llvm::AtomicRMWInst::UIncWrap;
      break;
    case AMDGPU::BI__builtin_amdgcn_atomic_dec32:
    case AMDGPU::BI__builtin_amdgcn_atomic_dec64:
      BinOp = llvm::AtomicRMWInst::UDecWrap;
      break;
    case AMDGPU::BI__builtin_amdgcn_ds_faddf:
    case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_f64:
    case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_f32:
    case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_v2f16:
    case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_v2bf16:
      BinOp = llvm::AtomicRMWInst::FAdd;
      break;
    case AMDGPU::BI__builtin_amdgcn_ds_fminf:
      BinOp = llvm::AtomicRMWInst::FMin;
      break;
    case AMDGPU::BI__builtin_amdgcn_ds_fmaxf:
      BinOp = llvm::AtomicRMWInst::FMax;
      break;
    }

    Address Ptr = CheckAtomicAlignment(*this, E);
    Value *Val = EmitScalarExpr(E->getArg(1));
    llvm::Type *OrigTy = Val->getType();
    QualType PtrTy = E->getArg(0)->IgnoreImpCasts()->getType();

    bool Volatile;

    if (BuiltinID == AMDGPU::BI__builtin_amdgcn_ds_faddf ||
        BuiltinID == AMDGPU::BI__builtin_amdgcn_ds_fminf ||
        BuiltinID == AMDGPU::BI__builtin_amdgcn_ds_fmaxf) {
      // __builtin_amdgcn_ds_faddf/fminf/fmaxf has an explicit volatile argument
      Volatile =
          cast<ConstantInt>(EmitScalarExpr(E->getArg(4)))->getZExtValue();
    } else {
      // Infer volatile from the passed type.
      Volatile =
          PtrTy->castAs<PointerType>()->getPointeeType().isVolatileQualified();
    }

    if (E->getNumArgs() >= 4) {
      // Some of the builtins have explicit ordering and scope arguments.
      ProcessOrderScopeAMDGCN(EmitScalarExpr(E->getArg(2)),
                              EmitScalarExpr(E->getArg(3)), AO, SSID);
    } else {
      // The ds_atomic_fadd_* builtins do not have syncscope/order arguments.
      SSID = llvm::SyncScope::System;
      AO = AtomicOrdering::SequentiallyConsistent;

      // The v2bf16 builtin uses i16 instead of a natural bfloat type.
      if (BuiltinID == AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_v2bf16) {
        llvm::Type *V2BF16Ty = FixedVectorType::get(
            llvm::Type::getBFloatTy(Builder.getContext()), 2);
        Val = Builder.CreateBitCast(Val, V2BF16Ty);
      }
    }

    llvm::AtomicRMWInst *RMW =
        Builder.CreateAtomicRMW(BinOp, Ptr, Val, AO, SSID);
    if (Volatile)
      RMW->setVolatile(true);
    return Builder.CreateBitCast(RMW, OrigTy);
  }
  case AMDGPU::BI__builtin_amdgcn_s_sendmsg_rtn:
  case AMDGPU::BI__builtin_amdgcn_s_sendmsg_rtnl: {
    llvm::Value *Arg = EmitScalarExpr(E->getArg(0));
    llvm::Type *ResultType = ConvertType(E->getType());
    // s_sendmsg_rtn is mangled using return type only.
    Function *F =
        CGM.getIntrinsic(Intrinsic::amdgcn_s_sendmsg_rtn, {ResultType});
    return Builder.CreateCall(F, {Arg});
  }
  case AMDGPU::BI__builtin_amdgcn_make_buffer_rsrc:
    return emitBuiltinWithOneOverloadedType<4>(
        *this, E, Intrinsic::amdgcn_make_buffer_rsrc);
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b8:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b16:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b32:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b64:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b96:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b128:
    return emitBuiltinWithOneOverloadedType<5>(
        *this, E, Intrinsic::amdgcn_raw_ptr_buffer_store);
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b8:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b16:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b32:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b64:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b96:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b128: {
    llvm::Type *RetTy = nullptr;
    switch (BuiltinID) {
    case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b8:
      RetTy = Int8Ty;
      break;
    case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b16:
      RetTy = Int16Ty;
      break;
    case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b32:
      RetTy = Int32Ty;
      break;
    case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b64:
      RetTy = llvm::FixedVectorType::get(Int32Ty, /*NumElements=*/2);
      break;
    case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b96:
      RetTy = llvm::FixedVectorType::get(Int32Ty, /*NumElements=*/3);
      break;
    case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b128:
      RetTy = llvm::FixedVectorType::get(Int32Ty, /*NumElements=*/4);
      break;
    }
    Function *F =
        CGM.getIntrinsic(Intrinsic::amdgcn_raw_ptr_buffer_load, RetTy);
    return Builder.CreateCall(
        F, {EmitScalarExpr(E->getArg(0)), EmitScalarExpr(E->getArg(1)),
            EmitScalarExpr(E->getArg(2)), EmitScalarExpr(E->getArg(3))});
  }
  default:
    return nullptr;
  }
}

/// Handle a SystemZ function in which the final argument is a pointer
/// to an int that receives the post-instruction CC value.  At the LLVM level
/// this is represented as a function that returns a {result, cc} pair.
static Value *EmitSystemZIntrinsicWithCC(CodeGenFunction &CGF,
                                         unsigned IntrinsicID,
                                         const CallExpr *E) {
  unsigned NumArgs = E->getNumArgs() - 1;
  SmallVector<Value *, 8> Args(NumArgs);
  for (unsigned I = 0; I < NumArgs; ++I)
    Args[I] = CGF.EmitScalarExpr(E->getArg(I));
  Address CCPtr = CGF.EmitPointerWithAlignment(E->getArg(NumArgs));
  Function *F = CGF.CGM.getIntrinsic(IntrinsicID);
  Value *Call = CGF.Builder.CreateCall(F, Args);
  Value *CC = CGF.Builder.CreateExtractValue(Call, 1);
  CGF.Builder.CreateStore(CC, CCPtr);
  return CGF.Builder.CreateExtractValue(Call, 0);
}

Value *CodeGenFunction::EmitSystemZBuiltinExpr(unsigned BuiltinID,
                                               const CallExpr *E) {
  switch (BuiltinID) {
  case SystemZ::BI__builtin_tbegin: {
    Value *TDB = EmitScalarExpr(E->getArg(0));
    Value *Control = llvm::ConstantInt::get(Int32Ty, 0xff0c);
    Function *F = CGM.getIntrinsic(Intrinsic::s390_tbegin);
    return Builder.CreateCall(F, {TDB, Control});
  }
  case SystemZ::BI__builtin_tbegin_nofloat: {
    Value *TDB = EmitScalarExpr(E->getArg(0));
    Value *Control = llvm::ConstantInt::get(Int32Ty, 0xff0c);
    Function *F = CGM.getIntrinsic(Intrinsic::s390_tbegin_nofloat);
    return Builder.CreateCall(F, {TDB, Control});
  }
  case SystemZ::BI__builtin_tbeginc: {
    Value *TDB = llvm::ConstantPointerNull::get(Int8PtrTy);
    Value *Control = llvm::ConstantInt::get(Int32Ty, 0xff08);
    Function *F = CGM.getIntrinsic(Intrinsic::s390_tbeginc);
    return Builder.CreateCall(F, {TDB, Control});
  }
  case SystemZ::BI__builtin_tabort: {
    Value *Data = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::s390_tabort);
    return Builder.CreateCall(F, Builder.CreateSExt(Data, Int64Ty, "tabort"));
  }
  case SystemZ::BI__builtin_non_tx_store: {
    Value *Address = EmitScalarExpr(E->getArg(0));
    Value *Data = EmitScalarExpr(E->getArg(1));
    Function *F = CGM.getIntrinsic(Intrinsic::s390_ntstg);
    return Builder.CreateCall(F, {Data, Address});
  }

  // Vector builtins.  Note that most vector builtins are mapped automatically
  // to target-specific LLVM intrinsics.  The ones handled specially here can
  // be represented via standard LLVM IR, which is preferable to enable common
  // LLVM optimizations.

  case SystemZ::BI__builtin_s390_vpopctb:
  case SystemZ::BI__builtin_s390_vpopcth:
  case SystemZ::BI__builtin_s390_vpopctf:
  case SystemZ::BI__builtin_s390_vpopctg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::ctpop, ResultType);
    return Builder.CreateCall(F, X);
  }

  case SystemZ::BI__builtin_s390_vclzb:
  case SystemZ::BI__builtin_s390_vclzh:
  case SystemZ::BI__builtin_s390_vclzf:
  case SystemZ::BI__builtin_s390_vclzg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Undef = ConstantInt::get(Builder.getInt1Ty(), false);
    Function *F = CGM.getIntrinsic(Intrinsic::ctlz, ResultType);
    return Builder.CreateCall(F, {X, Undef});
  }

  case SystemZ::BI__builtin_s390_vctzb:
  case SystemZ::BI__builtin_s390_vctzh:
  case SystemZ::BI__builtin_s390_vctzf:
  case SystemZ::BI__builtin_s390_vctzg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Undef = ConstantInt::get(Builder.getInt1Ty(), false);
    Function *F = CGM.getIntrinsic(Intrinsic::cttz, ResultType);
    return Builder.CreateCall(F, {X, Undef});
  }

  case SystemZ::BI__builtin_s390_verllb:
  case SystemZ::BI__builtin_s390_verllh:
  case SystemZ::BI__builtin_s390_verllf:
  case SystemZ::BI__builtin_s390_verllg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    llvm::Value *Src = EmitScalarExpr(E->getArg(0));
    llvm::Value *Amt = EmitScalarExpr(E->getArg(1));
    // Splat scalar rotate amount to vector type.
    unsigned NumElts = cast<llvm::FixedVectorType>(ResultType)->getNumElements();
    Amt = Builder.CreateIntCast(Amt, ResultType->getScalarType(), false);
    Amt = Builder.CreateVectorSplat(NumElts, Amt);
    Function *F = CGM.getIntrinsic(Intrinsic::fshl, ResultType);
    return Builder.CreateCall(F, { Src, Src, Amt });
  }

  case SystemZ::BI__builtin_s390_verllvb:
  case SystemZ::BI__builtin_s390_verllvh:
  case SystemZ::BI__builtin_s390_verllvf:
  case SystemZ::BI__builtin_s390_verllvg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    llvm::Value *Src = EmitScalarExpr(E->getArg(0));
    llvm::Value *Amt = EmitScalarExpr(E->getArg(1));
    Function *F = CGM.getIntrinsic(Intrinsic::fshl, ResultType);
    return Builder.CreateCall(F, { Src, Src, Amt });
  }

  case SystemZ::BI__builtin_s390_vfsqsb:
  case SystemZ::BI__builtin_s390_vfsqdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    if (Builder.getIsFPConstrained()) {
      Function *F = CGM.getIntrinsic(Intrinsic::experimental_constrained_sqrt, ResultType);
      return Builder.CreateConstrainedFPCall(F, { X });
    } else {
      Function *F = CGM.getIntrinsic(Intrinsic::sqrt, ResultType);
      return Builder.CreateCall(F, X);
    }
  }
  case SystemZ::BI__builtin_s390_vfmasb:
  case SystemZ::BI__builtin_s390_vfmadb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    if (Builder.getIsFPConstrained()) {
      Function *F = CGM.getIntrinsic(Intrinsic::experimental_constrained_fma, ResultType);
      return Builder.CreateConstrainedFPCall(F, {X, Y, Z});
    } else {
      Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
      return Builder.CreateCall(F, {X, Y, Z});
    }
  }
  case SystemZ::BI__builtin_s390_vfmssb:
  case SystemZ::BI__builtin_s390_vfmsdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    if (Builder.getIsFPConstrained()) {
      Function *F = CGM.getIntrinsic(Intrinsic::experimental_constrained_fma, ResultType);
      return Builder.CreateConstrainedFPCall(F, {X, Y, Builder.CreateFNeg(Z, "neg")});
    } else {
      Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
      return Builder.CreateCall(F, {X, Y, Builder.CreateFNeg(Z, "neg")});
    }
  }
  case SystemZ::BI__builtin_s390_vfnmasb:
  case SystemZ::BI__builtin_s390_vfnmadb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    if (Builder.getIsFPConstrained()) {
      Function *F = CGM.getIntrinsic(Intrinsic::experimental_constrained_fma, ResultType);
      return Builder.CreateFNeg(Builder.CreateConstrainedFPCall(F, {X, Y,  Z}), "neg");
    } else {
      Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
      return Builder.CreateFNeg(Builder.CreateCall(F, {X, Y, Z}), "neg");
    }
  }
  case SystemZ::BI__builtin_s390_vfnmssb:
  case SystemZ::BI__builtin_s390_vfnmsdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    Value *Z = EmitScalarExpr(E->getArg(2));
    if (Builder.getIsFPConstrained()) {
      Function *F = CGM.getIntrinsic(Intrinsic::experimental_constrained_fma, ResultType);
      Value *NegZ = Builder.CreateFNeg(Z, "sub");
      return Builder.CreateFNeg(Builder.CreateConstrainedFPCall(F, {X, Y, NegZ}));
    } else {
      Function *F = CGM.getIntrinsic(Intrinsic::fma, ResultType);
      Value *NegZ = Builder.CreateFNeg(Z, "neg");
      return Builder.CreateFNeg(Builder.CreateCall(F, {X, Y, NegZ}));
    }
  }
  case SystemZ::BI__builtin_s390_vflpsb:
  case SystemZ::BI__builtin_s390_vflpdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::fabs, ResultType);
    return Builder.CreateCall(F, X);
  }
  case SystemZ::BI__builtin_s390_vflnsb:
  case SystemZ::BI__builtin_s390_vflndb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::fabs, ResultType);
    return Builder.CreateFNeg(Builder.CreateCall(F, X), "neg");
  }
  case SystemZ::BI__builtin_s390_vfisb:
  case SystemZ::BI__builtin_s390_vfidb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    // Constant-fold the M4 and M5 mask arguments.
    llvm::APSInt M4 = *E->getArg(1)->getIntegerConstantExpr(getContext());
    llvm::APSInt M5 = *E->getArg(2)->getIntegerConstantExpr(getContext());
    // Check whether this instance can be represented via a LLVM standard
    // intrinsic.  We only support some combinations of M4 and M5.
    Intrinsic::ID ID = Intrinsic::not_intrinsic;
    Intrinsic::ID CI;
    switch (M4.getZExtValue()) {
    default: break;
    case 0:  // IEEE-inexact exception allowed
      switch (M5.getZExtValue()) {
      default: break;
      case 0: ID = Intrinsic::rint;
              CI = Intrinsic::experimental_constrained_rint; break;
      }
      break;
    case 4:  // IEEE-inexact exception suppressed
      switch (M5.getZExtValue()) {
      default: break;
      case 0: ID = Intrinsic::nearbyint;
              CI = Intrinsic::experimental_constrained_nearbyint; break;
      case 1: ID = Intrinsic::round;
              CI = Intrinsic::experimental_constrained_round; break;
      case 5: ID = Intrinsic::trunc;
              CI = Intrinsic::experimental_constrained_trunc; break;
      case 6: ID = Intrinsic::ceil;
              CI = Intrinsic::experimental_constrained_ceil; break;
      case 7: ID = Intrinsic::floor;
              CI = Intrinsic::experimental_constrained_floor; break;
      }
      break;
    }
    if (ID != Intrinsic::not_intrinsic) {
      if (Builder.getIsFPConstrained()) {
        Function *F = CGM.getIntrinsic(CI, ResultType);
        return Builder.CreateConstrainedFPCall(F, X);
      } else {
        Function *F = CGM.getIntrinsic(ID, ResultType);
        return Builder.CreateCall(F, X);
      }
    }
    switch (BuiltinID) { // FIXME: constrained version?
      case SystemZ::BI__builtin_s390_vfisb: ID = Intrinsic::s390_vfisb; break;
      case SystemZ::BI__builtin_s390_vfidb: ID = Intrinsic::s390_vfidb; break;
      default: llvm_unreachable("Unknown BuiltinID");
    }
    Function *F = CGM.getIntrinsic(ID);
    Value *M4Value = llvm::ConstantInt::get(getLLVMContext(), M4);
    Value *M5Value = llvm::ConstantInt::get(getLLVMContext(), M5);
    return Builder.CreateCall(F, {X, M4Value, M5Value});
  }
  case SystemZ::BI__builtin_s390_vfmaxsb:
  case SystemZ::BI__builtin_s390_vfmaxdb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    // Constant-fold the M4 mask argument.
    llvm::APSInt M4 = *E->getArg(2)->getIntegerConstantExpr(getContext());
    // Check whether this instance can be represented via a LLVM standard
    // intrinsic.  We only support some values of M4.
    Intrinsic::ID ID = Intrinsic::not_intrinsic;
    Intrinsic::ID CI;
    switch (M4.getZExtValue()) {
    default: break;
    case 4: ID = Intrinsic::maxnum;
            CI = Intrinsic::experimental_constrained_maxnum; break;
    }
    if (ID != Intrinsic::not_intrinsic) {
      if (Builder.getIsFPConstrained()) {
        Function *F = CGM.getIntrinsic(CI, ResultType);
        return Builder.CreateConstrainedFPCall(F, {X, Y});
      } else {
        Function *F = CGM.getIntrinsic(ID, ResultType);
        return Builder.CreateCall(F, {X, Y});
      }
    }
    switch (BuiltinID) {
      case SystemZ::BI__builtin_s390_vfmaxsb: ID = Intrinsic::s390_vfmaxsb; break;
      case SystemZ::BI__builtin_s390_vfmaxdb: ID = Intrinsic::s390_vfmaxdb; break;
      default: llvm_unreachable("Unknown BuiltinID");
    }
    Function *F = CGM.getIntrinsic(ID);
    Value *M4Value = llvm::ConstantInt::get(getLLVMContext(), M4);
    return Builder.CreateCall(F, {X, Y, M4Value});
  }
  case SystemZ::BI__builtin_s390_vfminsb:
  case SystemZ::BI__builtin_s390_vfmindb: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Value *Y = EmitScalarExpr(E->getArg(1));
    // Constant-fold the M4 mask argument.
    llvm::APSInt M4 = *E->getArg(2)->getIntegerConstantExpr(getContext());
    // Check whether this instance can be represented via a LLVM standard
    // intrinsic.  We only support some values of M4.
    Intrinsic::ID ID = Intrinsic::not_intrinsic;
    Intrinsic::ID CI;
    switch (M4.getZExtValue()) {
    default: break;
    case 4: ID = Intrinsic::minnum;
            CI = Intrinsic::experimental_constrained_minnum; break;
    }
    if (ID != Intrinsic::not_intrinsic) {
      if (Builder.getIsFPConstrained()) {
        Function *F = CGM.getIntrinsic(CI, ResultType);
        return Builder.CreateConstrainedFPCall(F, {X, Y});
      } else {
        Function *F = CGM.getIntrinsic(ID, ResultType);
        return Builder.CreateCall(F, {X, Y});
      }
    }
    switch (BuiltinID) {
      case SystemZ::BI__builtin_s390_vfminsb: ID = Intrinsic::s390_vfminsb; break;
      case SystemZ::BI__builtin_s390_vfmindb: ID = Intrinsic::s390_vfmindb; break;
      default: llvm_unreachable("Unknown BuiltinID");
    }
    Function *F = CGM.getIntrinsic(ID);
    Value *M4Value = llvm::ConstantInt::get(getLLVMContext(), M4);
    return Builder.CreateCall(F, {X, Y, M4Value});
  }

  case SystemZ::BI__builtin_s390_vlbrh:
  case SystemZ::BI__builtin_s390_vlbrf:
  case SystemZ::BI__builtin_s390_vlbrg: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *X = EmitScalarExpr(E->getArg(0));
    Function *F = CGM.getIntrinsic(Intrinsic::bswap, ResultType);
    return Builder.CreateCall(F, X);
  }

  // Vector intrinsics that output the post-instruction CC value.

#define INTRINSIC_WITH_CC(NAME) \
    case SystemZ::BI__builtin_##NAME: \
      return EmitSystemZIntrinsicWithCC(*this, Intrinsic::NAME, E)

  INTRINSIC_WITH_CC(s390_vpkshs);
  INTRINSIC_WITH_CC(s390_vpksfs);
  INTRINSIC_WITH_CC(s390_vpksgs);

  INTRINSIC_WITH_CC(s390_vpklshs);
  INTRINSIC_WITH_CC(s390_vpklsfs);
  INTRINSIC_WITH_CC(s390_vpklsgs);

  INTRINSIC_WITH_CC(s390_vceqbs);
  INTRINSIC_WITH_CC(s390_vceqhs);
  INTRINSIC_WITH_CC(s390_vceqfs);
  INTRINSIC_WITH_CC(s390_vceqgs);

  INTRINSIC_WITH_CC(s390_vchbs);
  INTRINSIC_WITH_CC(s390_vchhs);
  INTRINSIC_WITH_CC(s390_vchfs);
  INTRINSIC_WITH_CC(s390_vchgs);

  INTRINSIC_WITH_CC(s390_vchlbs);
  INTRINSIC_WITH_CC(s390_vchlhs);
  INTRINSIC_WITH_CC(s390_vchlfs);
  INTRINSIC_WITH_CC(s390_vchlgs);

  INTRINSIC_WITH_CC(s390_vfaebs);
  INTRINSIC_WITH_CC(s390_vfaehs);
  INTRINSIC_WITH_CC(s390_vfaefs);

  INTRINSIC_WITH_CC(s390_vfaezbs);
  INTRINSIC_WITH_CC(s390_vfaezhs);
  INTRINSIC_WITH_CC(s390_vfaezfs);

  INTRINSIC_WITH_CC(s390_vfeebs);
  INTRINSIC_WITH_CC(s390_vfeehs);
  INTRINSIC_WITH_CC(s390_vfeefs);

  INTRINSIC_WITH_CC(s390_vfeezbs);
  INTRINSIC_WITH_CC(s390_vfeezhs);
  INTRINSIC_WITH_CC(s390_vfeezfs);

  INTRINSIC_WITH_CC(s390_vfenebs);
  INTRINSIC_WITH_CC(s390_vfenehs);
  INTRINSIC_WITH_CC(s390_vfenefs);

  INTRINSIC_WITH_CC(s390_vfenezbs);
  INTRINSIC_WITH_CC(s390_vfenezhs);
  INTRINSIC_WITH_CC(s390_vfenezfs);

  INTRINSIC_WITH_CC(s390_vistrbs);
  INTRINSIC_WITH_CC(s390_vistrhs);
  INTRINSIC_WITH_CC(s390_vistrfs);

  INTRINSIC_WITH_CC(s390_vstrcbs);
  INTRINSIC_WITH_CC(s390_vstrchs);
  INTRINSIC_WITH_CC(s390_vstrcfs);

  INTRINSIC_WITH_CC(s390_vstrczbs);
  INTRINSIC_WITH_CC(s390_vstrczhs);
  INTRINSIC_WITH_CC(s390_vstrczfs);

  INTRINSIC_WITH_CC(s390_vfcesbs);
  INTRINSIC_WITH_CC(s390_vfcedbs);
  INTRINSIC_WITH_CC(s390_vfchsbs);
  INTRINSIC_WITH_CC(s390_vfchdbs);
  INTRINSIC_WITH_CC(s390_vfchesbs);
  INTRINSIC_WITH_CC(s390_vfchedbs);

  INTRINSIC_WITH_CC(s390_vftcisb);
  INTRINSIC_WITH_CC(s390_vftcidb);

  INTRINSIC_WITH_CC(s390_vstrsb);
  INTRINSIC_WITH_CC(s390_vstrsh);
  INTRINSIC_WITH_CC(s390_vstrsf);

  INTRINSIC_WITH_CC(s390_vstrszb);
  INTRINSIC_WITH_CC(s390_vstrszh);
  INTRINSIC_WITH_CC(s390_vstrszf);

#undef INTRINSIC_WITH_CC

  default:
    return nullptr;
  }
}

namespace {
// Helper classes for mapping MMA builtins to particular LLVM intrinsic variant.
struct NVPTXMmaLdstInfo {
  unsigned NumResults;  // Number of elements to load/store
  // Intrinsic IDs for row/col variants. 0 if particular layout is unsupported.
  unsigned IID_col;
  unsigned IID_row;
};

#define MMA_INTR(geom_op_type, layout) \
  Intrinsic::nvvm_wmma_##geom_op_type##_##layout##_stride
#define MMA_LDST(n, geom_op_type)                                              \
  { n, MMA_INTR(geom_op_type, col), MMA_INTR(geom_op_type, row) }

static NVPTXMmaLdstInfo getNVPTXMmaLdstInfo(unsigned BuiltinID) {
  switch (BuiltinID) {
  // FP MMA loads
  case NVPTX::BI__hmma_m16n16k16_ld_a:
    return MMA_LDST(8, m16n16k16_load_a_f16);
  case NVPTX::BI__hmma_m16n16k16_ld_b:
    return MMA_LDST(8, m16n16k16_load_b_f16);
  case NVPTX::BI__hmma_m16n16k16_ld_c_f16:
    return MMA_LDST(4, m16n16k16_load_c_f16);
  case NVPTX::BI__hmma_m16n16k16_ld_c_f32:
    return MMA_LDST(8, m16n16k16_load_c_f32);
  case NVPTX::BI__hmma_m32n8k16_ld_a:
    return MMA_LDST(8, m32n8k16_load_a_f16);
  case NVPTX::BI__hmma_m32n8k16_ld_b:
    return MMA_LDST(8, m32n8k16_load_b_f16);
  case NVPTX::BI__hmma_m32n8k16_ld_c_f16:
    return MMA_LDST(4, m32n8k16_load_c_f16);
  case NVPTX::BI__hmma_m32n8k16_ld_c_f32:
    return MMA_LDST(8, m32n8k16_load_c_f32);
  case NVPTX::BI__hmma_m8n32k16_ld_a:
    return MMA_LDST(8, m8n32k16_load_a_f16);
  case NVPTX::BI__hmma_m8n32k16_ld_b:
    return MMA_LDST(8, m8n32k16_load_b_f16);
  case NVPTX::BI__hmma_m8n32k16_ld_c_f16:
    return MMA_LDST(4, m8n32k16_load_c_f16);
  case NVPTX::BI__hmma_m8n32k16_ld_c_f32:
    return MMA_LDST(8, m8n32k16_load_c_f32);

  // Integer MMA loads
  case NVPTX::BI__imma_m16n16k16_ld_a_s8:
    return MMA_LDST(2, m16n16k16_load_a_s8);
  case NVPTX::BI__imma_m16n16k16_ld_a_u8:
    return MMA_LDST(2, m16n16k16_load_a_u8);
  case NVPTX::BI__imma_m16n16k16_ld_b_s8:
    return MMA_LDST(2, m16n16k16_load_b_s8);
  case NVPTX::BI__imma_m16n16k16_ld_b_u8:
    return MMA_LDST(2, m16n16k16_load_b_u8);
  case NVPTX::BI__imma_m16n16k16_ld_c:
    return MMA_LDST(8, m16n16k16_load_c_s32);
  case NVPTX::BI__imma_m32n8k16_ld_a_s8:
    return MMA_LDST(4, m32n8k16_load_a_s8);
  case NVPTX::BI__imma_m32n8k16_ld_a_u8:
    return MMA_LDST(4, m32n8k16_load_a_u8);
  case NVPTX::BI__imma_m32n8k16_ld_b_s8:
    return MMA_LDST(1, m32n8k16_load_b_s8);
  case NVPTX::BI__imma_m32n8k16_ld_b_u8:
    return MMA_LDST(1, m32n8k16_load_b_u8);
  case NVPTX::BI__imma_m32n8k16_ld_c:
    return MMA_LDST(8, m32n8k16_load_c_s32);
  case NVPTX::BI__imma_m8n32k16_ld_a_s8:
    return MMA_LDST(1, m8n32k16_load_a_s8);
  case NVPTX::BI__imma_m8n32k16_ld_a_u8:
    return MMA_LDST(1, m8n32k16_load_a_u8);
  case NVPTX::BI__imma_m8n32k16_ld_b_s8:
    return MMA_LDST(4, m8n32k16_load_b_s8);
  case NVPTX::BI__imma_m8n32k16_ld_b_u8:
    return MMA_LDST(4, m8n32k16_load_b_u8);
  case NVPTX::BI__imma_m8n32k16_ld_c:
    return MMA_LDST(8, m8n32k16_load_c_s32);

  // Sub-integer MMA loads.
  // Only row/col layout is supported by A/B fragments.
  case NVPTX::BI__imma_m8n8k32_ld_a_s4:
    return {1, 0, MMA_INTR(m8n8k32_load_a_s4, row)};
  case NVPTX::BI__imma_m8n8k32_ld_a_u4:
    return {1, 0, MMA_INTR(m8n8k32_load_a_u4, row)};
  case NVPTX::BI__imma_m8n8k32_ld_b_s4:
    return {1, MMA_INTR(m8n8k32_load_b_s4, col), 0};
  case NVPTX::BI__imma_m8n8k32_ld_b_u4:
    return {1, MMA_INTR(m8n8k32_load_b_u4, col), 0};
  case NVPTX::BI__imma_m8n8k32_ld_c:
    return MMA_LDST(2, m8n8k32_load_c_s32);
  case NVPTX::BI__bmma_m8n8k128_ld_a_b1:
    return {1, 0, MMA_INTR(m8n8k128_load_a_b1, row)};
  case NVPTX::BI__bmma_m8n8k128_ld_b_b1:
    return {1, MMA_INTR(m8n8k128_load_b_b1, col), 0};
  case NVPTX::BI__bmma_m8n8k128_ld_c:
    return MMA_LDST(2, m8n8k128_load_c_s32);

  // Double MMA loads
  case NVPTX::BI__dmma_m8n8k4_ld_a:
    return MMA_LDST(1, m8n8k4_load_a_f64);
  case NVPTX::BI__dmma_m8n8k4_ld_b:
    return MMA_LDST(1, m8n8k4_load_b_f64);
  case NVPTX::BI__dmma_m8n8k4_ld_c:
    return MMA_LDST(2, m8n8k4_load_c_f64);

  // Alternate float MMA loads
  case NVPTX::BI__mma_bf16_m16n16k16_ld_a:
    return MMA_LDST(4, m16n16k16_load_a_bf16);
  case NVPTX::BI__mma_bf16_m16n16k16_ld_b:
    return MMA_LDST(4, m16n16k16_load_b_bf16);
  case NVPTX::BI__mma_bf16_m8n32k16_ld_a:
    return MMA_LDST(2, m8n32k16_load_a_bf16);
  case NVPTX::BI__mma_bf16_m8n32k16_ld_b:
    return MMA_LDST(8, m8n32k16_load_b_bf16);
  case NVPTX::BI__mma_bf16_m32n8k16_ld_a:
    return MMA_LDST(8, m32n8k16_load_a_bf16);
  case NVPTX::BI__mma_bf16_m32n8k16_ld_b:
    return MMA_LDST(2, m32n8k16_load_b_bf16);
  case NVPTX::BI__mma_tf32_m16n16k8_ld_a:
    return MMA_LDST(4, m16n16k8_load_a_tf32);
  case NVPTX::BI__mma_tf32_m16n16k8_ld_b:
    return MMA_LDST(4, m16n16k8_load_b_tf32);
  case NVPTX::BI__mma_tf32_m16n16k8_ld_c:
    return MMA_LDST(8, m16n16k8_load_c_f32);

  // NOTE: We need to follow inconsitent naming scheme used by NVCC.  Unlike
  // PTX and LLVM IR where stores always use fragment D, NVCC builtins always
  // use fragment C for both loads and stores.
  // FP MMA stores.
  case NVPTX::BI__hmma_m16n16k16_st_c_f16:
    return MMA_LDST(4, m16n16k16_store_d_f16);
  case NVPTX::BI__hmma_m16n16k16_st_c_f32:
    return MMA_LDST(8, m16n16k16_store_d_f32);
  case NVPTX::BI__hmma_m32n8k16_st_c_f16:
    return MMA_LDST(4, m32n8k16_store_d_f16);
  case NVPTX::BI__hmma_m32n8k16_st_c_f32:
    return MMA_LDST(8, m32n8k16_store_d_f32);
  case NVPTX::BI__hmma_m8n32k16_st_c_f16:
    return MMA_LDST(4, m8n32k16_store_d_f16);
  case NVPTX::BI__hmma_m8n32k16_st_c_f32:
    return MMA_LDST(8, m8n32k16_store_d_f32);

  // Integer and sub-integer MMA stores.
  // Another naming quirk. Unlike other MMA builtins that use PTX types in the
  // name, integer loads/stores use LLVM's i32.
  case NVPTX::BI__imma_m16n16k16_st_c_i32:
    return MMA_LDST(8, m16n16k16_store_d_s32);
  case NVPTX::BI__imma_m32n8k16_st_c_i32:
    return MMA_LDST(8, m32n8k16_store_d_s32);
  case NVPTX::BI__imma_m8n32k16_st_c_i32:
    return MMA_LDST(8, m8n32k16_store_d_s32);
  case NVPTX::BI__imma_m8n8k32_st_c_i32:
    return MMA_LDST(2, m8n8k32_store_d_s32);
  case NVPTX::BI__bmma_m8n8k128_st_c_i32:
    return MMA_LDST(2, m8n8k128_store_d_s32);

  // Double MMA store
  case NVPTX::BI__dmma_m8n8k4_st_c_f64:
    return MMA_LDST(2, m8n8k4_store_d_f64);

  // Alternate float MMA store
  case NVPTX::BI__mma_m16n16k8_st_c_f32:
    return MMA_LDST(8, m16n16k8_store_d_f32);

  default:
    llvm_unreachable("Unknown MMA builtin");
  }
}
#undef MMA_LDST
#undef MMA_INTR


struct NVPTXMmaInfo {
  unsigned NumEltsA;
  unsigned NumEltsB;
  unsigned NumEltsC;
  unsigned NumEltsD;

  // Variants are ordered by layout-A/layout-B/satf, where 'row' has priority
  // over 'col' for layout. The index of non-satf variants is expected to match
  // the undocumented layout constants used by CUDA's mma.hpp.
  std::array<unsigned, 8> Variants;

  unsigned getMMAIntrinsic(int Layout, bool Satf) {
    unsigned Index = Layout + 4 * Satf;
    if (Index >= Variants.size())
      return 0;
    return Variants[Index];
  }
};

  // Returns an intrinsic that matches Layout and Satf for valid combinations of
  // Layout and Satf, 0 otherwise.
static NVPTXMmaInfo getNVPTXMmaInfo(unsigned BuiltinID) {
  // clang-format off
#define MMA_VARIANTS(geom, type)                                    \
      Intrinsic::nvvm_wmma_##geom##_mma_row_row_##type,             \
      Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type,             \
      Intrinsic::nvvm_wmma_##geom##_mma_col_row_##type,             \
      Intrinsic::nvvm_wmma_##geom##_mma_col_col_##type
#define MMA_SATF_VARIANTS(geom, type)                               \
      MMA_VARIANTS(geom, type),                                     \
      Intrinsic::nvvm_wmma_##geom##_mma_row_row_##type##_satfinite, \
      Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type##_satfinite, \
      Intrinsic::nvvm_wmma_##geom##_mma_col_row_##type##_satfinite, \
      Intrinsic::nvvm_wmma_##geom##_mma_col_col_##type##_satfinite
// Sub-integer MMA only supports row.col layout.
#define MMA_VARIANTS_I4(geom, type) \
      0, \
      Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type,             \
      0, \
      0, \
      0, \
      Intrinsic::nvvm_wmma_##geom##_mma_row_col_##type##_satfinite, \
      0, \
      0
// b1 MMA does not support .satfinite.
#define MMA_VARIANTS_B1_XOR(geom, type) \
      0, \
      Intrinsic::nvvm_wmma_##geom##_mma_xor_popc_row_col_##type,             \
      0, \
      0, \
      0, \
      0, \
      0, \
      0
#define MMA_VARIANTS_B1_AND(geom, type) \
      0, \
      Intrinsic::nvvm_wmma_##geom##_mma_and_popc_row_col_##type,             \
      0, \
      0, \
      0, \
      0, \
      0, \
      0
  // clang-format on
  switch (BuiltinID) {
  // FP MMA
  // Note that 'type' argument of MMA_SATF_VARIANTS uses D_C notation, while
  // NumEltsN of return value are ordered as A,B,C,D.
  case NVPTX::BI__hmma_m16n16k16_mma_f16f16:
    return {8, 8, 4, 4, {{MMA_SATF_VARIANTS(m16n16k16, f16_f16)}}};
  case NVPTX::BI__hmma_m16n16k16_mma_f32f16:
    return {8, 8, 4, 8, {{MMA_SATF_VARIANTS(m16n16k16, f32_f16)}}};
  case NVPTX::BI__hmma_m16n16k16_mma_f16f32:
    return {8, 8, 8, 4, {{MMA_SATF_VARIANTS(m16n16k16, f16_f32)}}};
  case NVPTX::BI__hmma_m16n16k16_mma_f32f32:
    return {8, 8, 8, 8, {{MMA_SATF_VARIANTS(m16n16k16, f32_f32)}}};
  case NVPTX::BI__hmma_m32n8k16_mma_f16f16:
    return {8, 8, 4, 4, {{MMA_SATF_VARIANTS(m32n8k16, f16_f16)}}};
  case NVPTX::BI__hmma_m32n8k16_mma_f32f16:
    return {8, 8, 4, 8, {{MMA_SATF_VARIANTS(m32n8k16, f32_f16)}}};
  case NVPTX::BI__hmma_m32n8k16_mma_f16f32:
    return {8, 8, 8, 4, {{MMA_SATF_VARIANTS(m32n8k16, f16_f32)}}};
  case NVPTX::BI__hmma_m32n8k16_mma_f32f32:
    return {8, 8, 8, 8, {{MMA_SATF_VARIANTS(m32n8k16, f32_f32)}}};
  case NVPTX::BI__hmma_m8n32k16_mma_f16f16:
    return {8, 8, 4, 4, {{MMA_SATF_VARIANTS(m8n32k16, f16_f16)}}};
  case NVPTX::BI__hmma_m8n32k16_mma_f32f16:
    return {8, 8, 4, 8, {{MMA_SATF_VARIANTS(m8n32k16, f32_f16)}}};
  case NVPTX::BI__hmma_m8n32k16_mma_f16f32:
    return {8, 8, 8, 4, {{MMA_SATF_VARIANTS(m8n32k16, f16_f32)}}};
  case NVPTX::BI__hmma_m8n32k16_mma_f32f32:
    return {8, 8, 8, 8, {{MMA_SATF_VARIANTS(m8n32k16, f32_f32)}}};

  // Integer MMA
  case NVPTX::BI__imma_m16n16k16_mma_s8:
    return {2, 2, 8, 8, {{MMA_SATF_VARIANTS(m16n16k16, s8)}}};
  case NVPTX::BI__imma_m16n16k16_mma_u8:
    return {2, 2, 8, 8, {{MMA_SATF_VARIANTS(m16n16k16, u8)}}};
  case NVPTX::BI__imma_m32n8k16_mma_s8:
    return {4, 1, 8, 8, {{MMA_SATF_VARIANTS(m32n8k16, s8)}}};
  case NVPTX::BI__imma_m32n8k16_mma_u8:
    return {4, 1, 8, 8, {{MMA_SATF_VARIANTS(m32n8k16, u8)}}};
  case NVPTX::BI__imma_m8n32k16_mma_s8:
    return {1, 4, 8, 8, {{MMA_SATF_VARIANTS(m8n32k16, s8)}}};
  case NVPTX::BI__imma_m8n32k16_mma_u8:
    return {1, 4, 8, 8, {{MMA_SATF_VARIANTS(m8n32k16, u8)}}};

  // Sub-integer MMA
  case NVPTX::BI__imma_m8n8k32_mma_s4:
    return {1, 1, 2, 2, {{MMA_VARIANTS_I4(m8n8k32, s4)}}};
  case NVPTX::BI__imma_m8n8k32_mma_u4:
    return {1, 1, 2, 2, {{MMA_VARIANTS_I4(m8n8k32, u4)}}};
  case NVPTX::BI__bmma_m8n8k128_mma_xor_popc_b1:
    return {1, 1, 2, 2, {{MMA_VARIANTS_B1_XOR(m8n8k128, b1)}}};
  case NVPTX::BI__bmma_m8n8k128_mma_and_popc_b1:
    return {1, 1, 2, 2, {{MMA_VARIANTS_B1_AND(m8n8k128, b1)}}};

  // Double MMA
  case NVPTX::BI__dmma_m8n8k4_mma_f64:
    return {1, 1, 2, 2, {{MMA_VARIANTS(m8n8k4, f64)}}};

  // Alternate FP MMA
  case NVPTX::BI__mma_bf16_m16n16k16_mma_f32:
    return {4, 4, 8, 8, {{MMA_VARIANTS(m16n16k16, bf16)}}};
  case NVPTX::BI__mma_bf16_m8n32k16_mma_f32:
    return {2, 8, 8, 8, {{MMA_VARIANTS(m8n32k16, bf16)}}};
  case NVPTX::BI__mma_bf16_m32n8k16_mma_f32:
    return {8, 2, 8, 8, {{MMA_VARIANTS(m32n8k16, bf16)}}};
  case NVPTX::BI__mma_tf32_m16n16k8_mma_f32:
    return {4, 4, 8, 8, {{MMA_VARIANTS(m16n16k8, tf32)}}};
  default:
    llvm_unreachable("Unexpected builtin ID.");
  }
#undef MMA_VARIANTS
#undef MMA_SATF_VARIANTS
#undef MMA_VARIANTS_I4
#undef MMA_VARIANTS_B1_AND
#undef MMA_VARIANTS_B1_XOR
}

static Value *MakeLdgLdu(unsigned IntrinsicID, CodeGenFunction &CGF,
                         const CallExpr *E) {
  Value *Ptr = CGF.EmitScalarExpr(E->getArg(0));
  QualType ArgType = E->getArg(0)->getType();
  clang::CharUnits Align = CGF.CGM.getNaturalPointeeTypeAlignment(ArgType);
  llvm::Type *ElemTy = CGF.ConvertTypeForMem(ArgType->getPointeeType());
  return CGF.Builder.CreateCall(
      CGF.CGM.getIntrinsic(IntrinsicID, {ElemTy, Ptr->getType()}),
      {Ptr, ConstantInt::get(CGF.Builder.getInt32Ty(), Align.getQuantity())});
}

static Value *MakeScopedAtomic(unsigned IntrinsicID, CodeGenFunction &CGF,
                               const CallExpr *E) {
  Value *Ptr = CGF.EmitScalarExpr(E->getArg(0));
  llvm::Type *ElemTy =
      CGF.ConvertTypeForMem(E->getArg(0)->getType()->getPointeeType());
  return CGF.Builder.CreateCall(
      CGF.CGM.getIntrinsic(IntrinsicID, {ElemTy, Ptr->getType()}),
      {Ptr, CGF.EmitScalarExpr(E->getArg(1))});
}

static Value *MakeCpAsync(unsigned IntrinsicID, unsigned IntrinsicIDS,
                          CodeGenFunction &CGF, const CallExpr *E,
                          int SrcSize) {
  return E->getNumArgs() == 3
             ? CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(IntrinsicIDS),
                                      {CGF.EmitScalarExpr(E->getArg(0)),
                                       CGF.EmitScalarExpr(E->getArg(1)),
                                       CGF.EmitScalarExpr(E->getArg(2))})
             : CGF.Builder.CreateCall(CGF.CGM.getIntrinsic(IntrinsicID),
                                      {CGF.EmitScalarExpr(E->getArg(0)),
                                       CGF.EmitScalarExpr(E->getArg(1))});
}

static Value *MakeHalfType(unsigned IntrinsicID, unsigned BuiltinID,
                           const CallExpr *E, CodeGenFunction &CGF) {
  auto &C = CGF.CGM.getContext();
  if (!(C.getLangOpts().NativeHalfType ||
        !C.getTargetInfo().useFP16ConversionIntrinsics())) {
    CGF.CGM.Error(E->getExprLoc(), C.BuiltinInfo.getName(BuiltinID).str() +
                                       " requires native half type support.");
    return nullptr;
  }

  if (IntrinsicID == Intrinsic::nvvm_ldg_global_f ||
      IntrinsicID == Intrinsic::nvvm_ldu_global_f)
    return MakeLdgLdu(IntrinsicID, CGF, E);

  SmallVector<Value *, 16> Args;
  auto *F = CGF.CGM.getIntrinsic(IntrinsicID);
  auto *FTy = F->getFunctionType();
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  C.GetBuiltinType(BuiltinID, Error, &ICEArguments);
  assert(Error == ASTContext::GE_None && "Should not codegen an error");
  for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
    assert((ICEArguments & (1 << i)) == 0);
    auto *ArgValue = CGF.EmitScalarExpr(E->getArg(i));
    auto *PTy = FTy->getParamType(i);
    if (PTy != ArgValue->getType())
      ArgValue = CGF.Builder.CreateBitCast(ArgValue, PTy);
    Args.push_back(ArgValue);
  }

  return CGF.Builder.CreateCall(F, Args);
}
} // namespace

Value *CodeGenFunction::EmitNVPTXBuiltinExpr(unsigned BuiltinID,
                                             const CallExpr *E) {
  switch (BuiltinID) {
  case NVPTX::BI__nvvm_atom_add_gen_i:
  case NVPTX::BI__nvvm_atom_add_gen_l:
  case NVPTX::BI__nvvm_atom_add_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Add, E);

  case NVPTX::BI__nvvm_atom_sub_gen_i:
  case NVPTX::BI__nvvm_atom_sub_gen_l:
  case NVPTX::BI__nvvm_atom_sub_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Sub, E);

  case NVPTX::BI__nvvm_atom_and_gen_i:
  case NVPTX::BI__nvvm_atom_and_gen_l:
  case NVPTX::BI__nvvm_atom_and_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::And, E);

  case NVPTX::BI__nvvm_atom_or_gen_i:
  case NVPTX::BI__nvvm_atom_or_gen_l:
  case NVPTX::BI__nvvm_atom_or_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Or, E);

  case NVPTX::BI__nvvm_atom_xor_gen_i:
  case NVPTX::BI__nvvm_atom_xor_gen_l:
  case NVPTX::BI__nvvm_atom_xor_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Xor, E);

  case NVPTX::BI__nvvm_atom_xchg_gen_i:
  case NVPTX::BI__nvvm_atom_xchg_gen_l:
  case NVPTX::BI__nvvm_atom_xchg_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Xchg, E);

  case NVPTX::BI__nvvm_atom_max_gen_i:
  case NVPTX::BI__nvvm_atom_max_gen_l:
  case NVPTX::BI__nvvm_atom_max_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Max, E);

  case NVPTX::BI__nvvm_atom_max_gen_ui:
  case NVPTX::BI__nvvm_atom_max_gen_ul:
  case NVPTX::BI__nvvm_atom_max_gen_ull:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::UMax, E);

  case NVPTX::BI__nvvm_atom_min_gen_i:
  case NVPTX::BI__nvvm_atom_min_gen_l:
  case NVPTX::BI__nvvm_atom_min_gen_ll:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::Min, E);

  case NVPTX::BI__nvvm_atom_min_gen_ui:
  case NVPTX::BI__nvvm_atom_min_gen_ul:
  case NVPTX::BI__nvvm_atom_min_gen_ull:
    return MakeBinaryAtomicValue(*this, llvm::AtomicRMWInst::UMin, E);

  case NVPTX::BI__nvvm_atom_cas_gen_i:
  case NVPTX::BI__nvvm_atom_cas_gen_l:
  case NVPTX::BI__nvvm_atom_cas_gen_ll:
    // __nvvm_atom_cas_gen_* should return the old value rather than the
    // success flag.
    return MakeAtomicCmpXchgValue(*this, E, /*ReturnBool=*/false);

  case NVPTX::BI__nvvm_atom_add_gen_f:
  case NVPTX::BI__nvvm_atom_add_gen_d: {
    Address DestAddr = EmitPointerWithAlignment(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));

    return Builder.CreateAtomicRMW(llvm::AtomicRMWInst::FAdd, DestAddr, Val,
                                   AtomicOrdering::SequentiallyConsistent);
  }

  case NVPTX::BI__nvvm_atom_inc_gen_ui: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    Function *FnALI32 =
        CGM.getIntrinsic(Intrinsic::nvvm_atomic_load_inc_32, Ptr->getType());
    return Builder.CreateCall(FnALI32, {Ptr, Val});
  }

  case NVPTX::BI__nvvm_atom_dec_gen_ui: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    Function *FnALD32 =
        CGM.getIntrinsic(Intrinsic::nvvm_atomic_load_dec_32, Ptr->getType());
    return Builder.CreateCall(FnALD32, {Ptr, Val});
  }

  case NVPTX::BI__nvvm_ldg_c:
  case NVPTX::BI__nvvm_ldg_sc:
  case NVPTX::BI__nvvm_ldg_c2:
  case NVPTX::BI__nvvm_ldg_sc2:
  case NVPTX::BI__nvvm_ldg_c4:
  case NVPTX::BI__nvvm_ldg_sc4:
  case NVPTX::BI__nvvm_ldg_s:
  case NVPTX::BI__nvvm_ldg_s2:
  case NVPTX::BI__nvvm_ldg_s4:
  case NVPTX::BI__nvvm_ldg_i:
  case NVPTX::BI__nvvm_ldg_i2:
  case NVPTX::BI__nvvm_ldg_i4:
  case NVPTX::BI__nvvm_ldg_l:
  case NVPTX::BI__nvvm_ldg_l2:
  case NVPTX::BI__nvvm_ldg_ll:
  case NVPTX::BI__nvvm_ldg_ll2:
  case NVPTX::BI__nvvm_ldg_uc:
  case NVPTX::BI__nvvm_ldg_uc2:
  case NVPTX::BI__nvvm_ldg_uc4:
  case NVPTX::BI__nvvm_ldg_us:
  case NVPTX::BI__nvvm_ldg_us2:
  case NVPTX::BI__nvvm_ldg_us4:
  case NVPTX::BI__nvvm_ldg_ui:
  case NVPTX::BI__nvvm_ldg_ui2:
  case NVPTX::BI__nvvm_ldg_ui4:
  case NVPTX::BI__nvvm_ldg_ul:
  case NVPTX::BI__nvvm_ldg_ul2:
  case NVPTX::BI__nvvm_ldg_ull:
  case NVPTX::BI__nvvm_ldg_ull2:
    // PTX Interoperability section 2.2: "For a vector with an even number of
    // elements, its alignment is set to number of elements times the alignment
    // of its member: n*alignof(t)."
    return MakeLdgLdu(Intrinsic::nvvm_ldg_global_i, *this, E);
  case NVPTX::BI__nvvm_ldg_f:
  case NVPTX::BI__nvvm_ldg_f2:
  case NVPTX::BI__nvvm_ldg_f4:
  case NVPTX::BI__nvvm_ldg_d:
  case NVPTX::BI__nvvm_ldg_d2:
    return MakeLdgLdu(Intrinsic::nvvm_ldg_global_f, *this, E);

  case NVPTX::BI__nvvm_ldu_c:
  case NVPTX::BI__nvvm_ldu_sc:
  case NVPTX::BI__nvvm_ldu_c2:
  case NVPTX::BI__nvvm_ldu_sc2:
  case NVPTX::BI__nvvm_ldu_c4:
  case NVPTX::BI__nvvm_ldu_sc4:
  case NVPTX::BI__nvvm_ldu_s:
  case NVPTX::BI__nvvm_ldu_s2:
  case NVPTX::BI__nvvm_ldu_s4:
  case NVPTX::BI__nvvm_ldu_i:
  case NVPTX::BI__nvvm_ldu_i2:
  case NVPTX::BI__nvvm_ldu_i4:
  case NVPTX::BI__nvvm_ldu_l:
  case NVPTX::BI__nvvm_ldu_l2:
  case NVPTX::BI__nvvm_ldu_ll:
  case NVPTX::BI__nvvm_ldu_ll2:
  case NVPTX::BI__nvvm_ldu_uc:
  case NVPTX::BI__nvvm_ldu_uc2:
  case NVPTX::BI__nvvm_ldu_uc4:
  case NVPTX::BI__nvvm_ldu_us:
  case NVPTX::BI__nvvm_ldu_us2:
  case NVPTX::BI__nvvm_ldu_us4:
  case NVPTX::BI__nvvm_ldu_ui:
  case NVPTX::BI__nvvm_ldu_ui2:
  case NVPTX::BI__nvvm_ldu_ui4:
  case NVPTX::BI__nvvm_ldu_ul:
  case NVPTX::BI__nvvm_ldu_ul2:
  case NVPTX::BI__nvvm_ldu_ull:
  case NVPTX::BI__nvvm_ldu_ull2:
    return MakeLdgLdu(Intrinsic::nvvm_ldu_global_i, *this, E);
  case NVPTX::BI__nvvm_ldu_f:
  case NVPTX::BI__nvvm_ldu_f2:
  case NVPTX::BI__nvvm_ldu_f4:
  case NVPTX::BI__nvvm_ldu_d:
  case NVPTX::BI__nvvm_ldu_d2:
    return MakeLdgLdu(Intrinsic::nvvm_ldu_global_f, *this, E);

  case NVPTX::BI__nvvm_atom_cta_add_gen_i:
  case NVPTX::BI__nvvm_atom_cta_add_gen_l:
  case NVPTX::BI__nvvm_atom_cta_add_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_add_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_add_gen_i:
  case NVPTX::BI__nvvm_atom_sys_add_gen_l:
  case NVPTX::BI__nvvm_atom_sys_add_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_add_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_add_gen_f:
  case NVPTX::BI__nvvm_atom_cta_add_gen_d:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_add_gen_f_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_add_gen_f:
  case NVPTX::BI__nvvm_atom_sys_add_gen_d:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_add_gen_f_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_xchg_gen_i:
  case NVPTX::BI__nvvm_atom_cta_xchg_gen_l:
  case NVPTX::BI__nvvm_atom_cta_xchg_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_exch_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_xchg_gen_i:
  case NVPTX::BI__nvvm_atom_sys_xchg_gen_l:
  case NVPTX::BI__nvvm_atom_sys_xchg_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_exch_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_max_gen_i:
  case NVPTX::BI__nvvm_atom_cta_max_gen_ui:
  case NVPTX::BI__nvvm_atom_cta_max_gen_l:
  case NVPTX::BI__nvvm_atom_cta_max_gen_ul:
  case NVPTX::BI__nvvm_atom_cta_max_gen_ll:
  case NVPTX::BI__nvvm_atom_cta_max_gen_ull:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_max_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_max_gen_i:
  case NVPTX::BI__nvvm_atom_sys_max_gen_ui:
  case NVPTX::BI__nvvm_atom_sys_max_gen_l:
  case NVPTX::BI__nvvm_atom_sys_max_gen_ul:
  case NVPTX::BI__nvvm_atom_sys_max_gen_ll:
  case NVPTX::BI__nvvm_atom_sys_max_gen_ull:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_max_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_min_gen_i:
  case NVPTX::BI__nvvm_atom_cta_min_gen_ui:
  case NVPTX::BI__nvvm_atom_cta_min_gen_l:
  case NVPTX::BI__nvvm_atom_cta_min_gen_ul:
  case NVPTX::BI__nvvm_atom_cta_min_gen_ll:
  case NVPTX::BI__nvvm_atom_cta_min_gen_ull:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_min_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_min_gen_i:
  case NVPTX::BI__nvvm_atom_sys_min_gen_ui:
  case NVPTX::BI__nvvm_atom_sys_min_gen_l:
  case NVPTX::BI__nvvm_atom_sys_min_gen_ul:
  case NVPTX::BI__nvvm_atom_sys_min_gen_ll:
  case NVPTX::BI__nvvm_atom_sys_min_gen_ull:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_min_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_inc_gen_ui:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_inc_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_cta_dec_gen_ui:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_dec_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_inc_gen_ui:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_inc_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_sys_dec_gen_ui:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_dec_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_and_gen_i:
  case NVPTX::BI__nvvm_atom_cta_and_gen_l:
  case NVPTX::BI__nvvm_atom_cta_and_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_and_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_and_gen_i:
  case NVPTX::BI__nvvm_atom_sys_and_gen_l:
  case NVPTX::BI__nvvm_atom_sys_and_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_and_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_or_gen_i:
  case NVPTX::BI__nvvm_atom_cta_or_gen_l:
  case NVPTX::BI__nvvm_atom_cta_or_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_or_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_or_gen_i:
  case NVPTX::BI__nvvm_atom_sys_or_gen_l:
  case NVPTX::BI__nvvm_atom_sys_or_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_or_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_xor_gen_i:
  case NVPTX::BI__nvvm_atom_cta_xor_gen_l:
  case NVPTX::BI__nvvm_atom_cta_xor_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_xor_gen_i_cta, *this, E);
  case NVPTX::BI__nvvm_atom_sys_xor_gen_i:
  case NVPTX::BI__nvvm_atom_sys_xor_gen_l:
  case NVPTX::BI__nvvm_atom_sys_xor_gen_ll:
    return MakeScopedAtomic(Intrinsic::nvvm_atomic_xor_gen_i_sys, *this, E);
  case NVPTX::BI__nvvm_atom_cta_cas_gen_i:
  case NVPTX::BI__nvvm_atom_cta_cas_gen_l:
  case NVPTX::BI__nvvm_atom_cta_cas_gen_ll: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    llvm::Type *ElemTy =
        ConvertTypeForMem(E->getArg(0)->getType()->getPointeeType());
    return Builder.CreateCall(
        CGM.getIntrinsic(
            Intrinsic::nvvm_atomic_cas_gen_i_cta, {ElemTy, Ptr->getType()}),
        {Ptr, EmitScalarExpr(E->getArg(1)), EmitScalarExpr(E->getArg(2))});
  }
  case NVPTX::BI__nvvm_atom_sys_cas_gen_i:
  case NVPTX::BI__nvvm_atom_sys_cas_gen_l:
  case NVPTX::BI__nvvm_atom_sys_cas_gen_ll: {
    Value *Ptr = EmitScalarExpr(E->getArg(0));
    llvm::Type *ElemTy =
        ConvertTypeForMem(E->getArg(0)->getType()->getPointeeType());
    return Builder.CreateCall(
        CGM.getIntrinsic(
            Intrinsic::nvvm_atomic_cas_gen_i_sys, {ElemTy, Ptr->getType()}),
        {Ptr, EmitScalarExpr(E->getArg(1)), EmitScalarExpr(E->getArg(2))});
  }
  case NVPTX::BI__nvvm_match_all_sync_i32p:
  case NVPTX::BI__nvvm_match_all_sync_i64p: {
    Value *Mask = EmitScalarExpr(E->getArg(0));
    Value *Val = EmitScalarExpr(E->getArg(1));
    Address PredOutPtr = EmitPointerWithAlignment(E->getArg(2));
    Value *ResultPair = Builder.CreateCall(
        CGM.getIntrinsic(BuiltinID == NVPTX::BI__nvvm_match_all_sync_i32p
                             ? Intrinsic::nvvm_match_all_sync_i32p
                             : Intrinsic::nvvm_match_all_sync_i64p),
        {Mask, Val});
    Value *Pred = Builder.CreateZExt(Builder.CreateExtractValue(ResultPair, 1),
                                     PredOutPtr.getElementType());
    Builder.CreateStore(Pred, PredOutPtr);
    return Builder.CreateExtractValue(ResultPair, 0);
  }

  // FP MMA loads
  case NVPTX::BI__hmma_m16n16k16_ld_a:
  case NVPTX::BI__hmma_m16n16k16_ld_b:
  case NVPTX::BI__hmma_m16n16k16_ld_c_f16:
  case NVPTX::BI__hmma_m16n16k16_ld_c_f32:
  case NVPTX::BI__hmma_m32n8k16_ld_a:
  case NVPTX::BI__hmma_m32n8k16_ld_b:
  case NVPTX::BI__hmma_m32n8k16_ld_c_f16:
  case NVPTX::BI__hmma_m32n8k16_ld_c_f32:
  case NVPTX::BI__hmma_m8n32k16_ld_a:
  case NVPTX::BI__hmma_m8n32k16_ld_b:
  case NVPTX::BI__hmma_m8n32k16_ld_c_f16:
  case NVPTX::BI__hmma_m8n32k16_ld_c_f32:
  // Integer MMA loads.
  case NVPTX::BI__imma_m16n16k16_ld_a_s8:
  case NVPTX::BI__imma_m16n16k16_ld_a_u8:
  case NVPTX::BI__imma_m16n16k16_ld_b_s8:
  case NVPTX::BI__imma_m16n16k16_ld_b_u8:
  case NVPTX::BI__imma_m16n16k16_ld_c:
  case NVPTX::BI__imma_m32n8k16_ld_a_s8:
  case NVPTX::BI__imma_m32n8k16_ld_a_u8:
  case NVPTX::BI__imma_m32n8k16_ld_b_s8:
  case NVPTX::BI__imma_m32n8k16_ld_b_u8:
  case NVPTX::BI__imma_m32n8k16_ld_c:
  case NVPTX::BI__imma_m8n32k16_ld_a_s8:
  case NVPTX::BI__imma_m8n32k16_ld_a_u8:
  case NVPTX::BI__imma_m8n32k16_ld_b_s8:
  case NVPTX::BI__imma_m8n32k16_ld_b_u8:
  case NVPTX::BI__imma_m8n32k16_ld_c:
  // Sub-integer MMA loads.
  case NVPTX::BI__imma_m8n8k32_ld_a_s4:
  case NVPTX::BI__imma_m8n8k32_ld_a_u4:
  case NVPTX::BI__imma_m8n8k32_ld_b_s4:
  case NVPTX::BI__imma_m8n8k32_ld_b_u4:
  case NVPTX::BI__imma_m8n8k32_ld_c:
  case NVPTX::BI__bmma_m8n8k128_ld_a_b1:
  case NVPTX::BI__bmma_m8n8k128_ld_b_b1:
  case NVPTX::BI__bmma_m8n8k128_ld_c:
  // Double MMA loads.
  case NVPTX::BI__dmma_m8n8k4_ld_a:
  case NVPTX::BI__dmma_m8n8k4_ld_b:
  case NVPTX::BI__dmma_m8n8k4_ld_c:
  // Alternate float MMA loads.
  case NVPTX::BI__mma_bf16_m16n16k16_ld_a:
  case NVPTX::BI__mma_bf16_m16n16k16_ld_b:
  case NVPTX::BI__mma_bf16_m8n32k16_ld_a:
  case NVPTX::BI__mma_bf16_m8n32k16_ld_b:
  case NVPTX::BI__mma_bf16_m32n8k16_ld_a:
  case NVPTX::BI__mma_bf16_m32n8k16_ld_b:
  case NVPTX::BI__mma_tf32_m16n16k8_ld_a:
  case NVPTX::BI__mma_tf32_m16n16k8_ld_b:
  case NVPTX::BI__mma_tf32_m16n16k8_ld_c: {
    Address Dst = EmitPointerWithAlignment(E->getArg(0));
    Value *Src = EmitScalarExpr(E->getArg(1));
    Value *Ldm = EmitScalarExpr(E->getArg(2));
    std::optional<llvm::APSInt> isColMajorArg =
        E->getArg(3)->getIntegerConstantExpr(getContext());
    if (!isColMajorArg)
      return nullptr;
    bool isColMajor = isColMajorArg->getSExtValue();
    NVPTXMmaLdstInfo II = getNVPTXMmaLdstInfo(BuiltinID);
    unsigned IID = isColMajor ? II.IID_col : II.IID_row;
    if (IID == 0)
      return nullptr;

    Value *Result =
        Builder.CreateCall(CGM.getIntrinsic(IID, Src->getType()), {Src, Ldm});

    // Save returned values.
    assert(II.NumResults);
    if (II.NumResults == 1) {
      Builder.CreateAlignedStore(Result, Dst.emitRawPointer(*this),
                                 CharUnits::fromQuantity(4));
    } else {
      for (unsigned i = 0; i < II.NumResults; ++i) {
        Builder.CreateAlignedStore(
            Builder.CreateBitCast(Builder.CreateExtractValue(Result, i),
                                  Dst.getElementType()),
            Builder.CreateGEP(Dst.getElementType(), Dst.emitRawPointer(*this),
                              llvm::ConstantInt::get(IntTy, i)),
            CharUnits::fromQuantity(4));
      }
    }
    return Result;
  }

  case NVPTX::BI__hmma_m16n16k16_st_c_f16:
  case NVPTX::BI__hmma_m16n16k16_st_c_f32:
  case NVPTX::BI__hmma_m32n8k16_st_c_f16:
  case NVPTX::BI__hmma_m32n8k16_st_c_f32:
  case NVPTX::BI__hmma_m8n32k16_st_c_f16:
  case NVPTX::BI__hmma_m8n32k16_st_c_f32:
  case NVPTX::BI__imma_m16n16k16_st_c_i32:
  case NVPTX::BI__imma_m32n8k16_st_c_i32:
  case NVPTX::BI__imma_m8n32k16_st_c_i32:
  case NVPTX::BI__imma_m8n8k32_st_c_i32:
  case NVPTX::BI__bmma_m8n8k128_st_c_i32:
  case NVPTX::BI__dmma_m8n8k4_st_c_f64:
  case NVPTX::BI__mma_m16n16k8_st_c_f32: {
    Value *Dst = EmitScalarExpr(E->getArg(0));
    Address Src = EmitPointerWithAlignment(E->getArg(1));
    Value *Ldm = EmitScalarExpr(E->getArg(2));
    std::optional<llvm::APSInt> isColMajorArg =
        E->getArg(3)->getIntegerConstantExpr(getContext());
    if (!isColMajorArg)
      return nullptr;
    bool isColMajor = isColMajorArg->getSExtValue();
    NVPTXMmaLdstInfo II = getNVPTXMmaLdstInfo(BuiltinID);
    unsigned IID = isColMajor ? II.IID_col : II.IID_row;
    if (IID == 0)
      return nullptr;
    Function *Intrinsic =
        CGM.getIntrinsic(IID, Dst->getType());
    llvm::Type *ParamType = Intrinsic->getFunctionType()->getParamType(1);
    SmallVector<Value *, 10> Values = {Dst};
    for (unsigned i = 0; i < II.NumResults; ++i) {
      Value *V = Builder.CreateAlignedLoad(
          Src.getElementType(),
          Builder.CreateGEP(Src.getElementType(), Src.emitRawPointer(*this),
                            llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
      Values.push_back(Builder.CreateBitCast(V, ParamType));
    }
    Values.push_back(Ldm);
    Value *Result = Builder.CreateCall(Intrinsic, Values);
    return Result;
  }

  // BI__hmma_m16n16k16_mma_<Dtype><CType>(d, a, b, c, layout, satf) -->
  // Intrinsic::nvvm_wmma_m16n16k16_mma_sync<layout A,B><DType><CType><Satf>
  case NVPTX::BI__hmma_m16n16k16_mma_f16f16:
  case NVPTX::BI__hmma_m16n16k16_mma_f32f16:
  case NVPTX::BI__hmma_m16n16k16_mma_f32f32:
  case NVPTX::BI__hmma_m16n16k16_mma_f16f32:
  case NVPTX::BI__hmma_m32n8k16_mma_f16f16:
  case NVPTX::BI__hmma_m32n8k16_mma_f32f16:
  case NVPTX::BI__hmma_m32n8k16_mma_f32f32:
  case NVPTX::BI__hmma_m32n8k16_mma_f16f32:
  case NVPTX::BI__hmma_m8n32k16_mma_f16f16:
  case NVPTX::BI__hmma_m8n32k16_mma_f32f16:
  case NVPTX::BI__hmma_m8n32k16_mma_f32f32:
  case NVPTX::BI__hmma_m8n32k16_mma_f16f32:
  case NVPTX::BI__imma_m16n16k16_mma_s8:
  case NVPTX::BI__imma_m16n16k16_mma_u8:
  case NVPTX::BI__imma_m32n8k16_mma_s8:
  case NVPTX::BI__imma_m32n8k16_mma_u8:
  case NVPTX::BI__imma_m8n32k16_mma_s8:
  case NVPTX::BI__imma_m8n32k16_mma_u8:
  case NVPTX::BI__imma_m8n8k32_mma_s4:
  case NVPTX::BI__imma_m8n8k32_mma_u4:
  case NVPTX::BI__bmma_m8n8k128_mma_xor_popc_b1:
  case NVPTX::BI__bmma_m8n8k128_mma_and_popc_b1:
  case NVPTX::BI__dmma_m8n8k4_mma_f64:
  case NVPTX::BI__mma_bf16_m16n16k16_mma_f32:
  case NVPTX::BI__mma_bf16_m8n32k16_mma_f32:
  case NVPTX::BI__mma_bf16_m32n8k16_mma_f32:
  case NVPTX::BI__mma_tf32_m16n16k8_mma_f32: {
    Address Dst = EmitPointerWithAlignment(E->getArg(0));
    Address SrcA = EmitPointerWithAlignment(E->getArg(1));
    Address SrcB = EmitPointerWithAlignment(E->getArg(2));
    Address SrcC = EmitPointerWithAlignment(E->getArg(3));
    std::optional<llvm::APSInt> LayoutArg =
        E->getArg(4)->getIntegerConstantExpr(getContext());
    if (!LayoutArg)
      return nullptr;
    int Layout = LayoutArg->getSExtValue();
    if (Layout < 0 || Layout > 3)
      return nullptr;
    llvm::APSInt SatfArg;
    if (BuiltinID == NVPTX::BI__bmma_m8n8k128_mma_xor_popc_b1 ||
        BuiltinID == NVPTX::BI__bmma_m8n8k128_mma_and_popc_b1)
      SatfArg = 0;  // .b1 does not have satf argument.
    else if (std::optional<llvm::APSInt> OptSatfArg =
                 E->getArg(5)->getIntegerConstantExpr(getContext()))
      SatfArg = *OptSatfArg;
    else
      return nullptr;
    bool Satf = SatfArg.getSExtValue();
    NVPTXMmaInfo MI = getNVPTXMmaInfo(BuiltinID);
    unsigned IID = MI.getMMAIntrinsic(Layout, Satf);
    if (IID == 0)  // Unsupported combination of Layout/Satf.
      return nullptr;

    SmallVector<Value *, 24> Values;
    Function *Intrinsic = CGM.getIntrinsic(IID);
    llvm::Type *AType = Intrinsic->getFunctionType()->getParamType(0);
    // Load A
    for (unsigned i = 0; i < MI.NumEltsA; ++i) {
      Value *V = Builder.CreateAlignedLoad(
          SrcA.getElementType(),
          Builder.CreateGEP(SrcA.getElementType(), SrcA.emitRawPointer(*this),
                            llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
      Values.push_back(Builder.CreateBitCast(V, AType));
    }
    // Load B
    llvm::Type *BType = Intrinsic->getFunctionType()->getParamType(MI.NumEltsA);
    for (unsigned i = 0; i < MI.NumEltsB; ++i) {
      Value *V = Builder.CreateAlignedLoad(
          SrcB.getElementType(),
          Builder.CreateGEP(SrcB.getElementType(), SrcB.emitRawPointer(*this),
                            llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
      Values.push_back(Builder.CreateBitCast(V, BType));
    }
    // Load C
    llvm::Type *CType =
        Intrinsic->getFunctionType()->getParamType(MI.NumEltsA + MI.NumEltsB);
    for (unsigned i = 0; i < MI.NumEltsC; ++i) {
      Value *V = Builder.CreateAlignedLoad(
          SrcC.getElementType(),
          Builder.CreateGEP(SrcC.getElementType(), SrcC.emitRawPointer(*this),
                            llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
      Values.push_back(Builder.CreateBitCast(V, CType));
    }
    Value *Result = Builder.CreateCall(Intrinsic, Values);
    llvm::Type *DType = Dst.getElementType();
    for (unsigned i = 0; i < MI.NumEltsD; ++i)
      Builder.CreateAlignedStore(
          Builder.CreateBitCast(Builder.CreateExtractValue(Result, i), DType),
          Builder.CreateGEP(Dst.getElementType(), Dst.emitRawPointer(*this),
                            llvm::ConstantInt::get(IntTy, i)),
          CharUnits::fromQuantity(4));
    return Result;
  }
  // The following builtins require half type support
  case NVPTX::BI__nvvm_ex2_approx_f16:
    return MakeHalfType(Intrinsic::nvvm_ex2_approx_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_ex2_approx_f16x2:
    return MakeHalfType(Intrinsic::nvvm_ex2_approx_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_ff2f16x2_rn:
    return MakeHalfType(Intrinsic::nvvm_ff2f16x2_rn, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_ff2f16x2_rn_relu:
    return MakeHalfType(Intrinsic::nvvm_ff2f16x2_rn_relu, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_ff2f16x2_rz:
    return MakeHalfType(Intrinsic::nvvm_ff2f16x2_rz, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_ff2f16x2_rz_relu:
    return MakeHalfType(Intrinsic::nvvm_ff2f16x2_rz_relu, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fma_rn_f16:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fma_rn_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fma_rn_ftz_f16:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_ftz_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fma_rn_ftz_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_ftz_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fma_rn_ftz_relu_f16:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_ftz_relu_f16, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fma_rn_ftz_relu_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_ftz_relu_f16x2, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fma_rn_ftz_sat_f16:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_ftz_sat_f16, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fma_rn_ftz_sat_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_ftz_sat_f16x2, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fma_rn_relu_f16:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_relu_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fma_rn_relu_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_relu_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fma_rn_sat_f16:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_sat_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fma_rn_sat_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fma_rn_sat_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_f16:
    return MakeHalfType(Intrinsic::nvvm_fmax_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmax_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_ftz_f16:
    return MakeHalfType(Intrinsic::nvvm_fmax_ftz_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_ftz_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmax_ftz_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_ftz_nan_f16:
    return MakeHalfType(Intrinsic::nvvm_fmax_ftz_nan_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_ftz_nan_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmax_ftz_nan_f16x2, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmax_ftz_nan_xorsign_abs_f16:
    return MakeHalfType(Intrinsic::nvvm_fmax_ftz_nan_xorsign_abs_f16, BuiltinID,
                        E, *this);
  case NVPTX::BI__nvvm_fmax_ftz_nan_xorsign_abs_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmax_ftz_nan_xorsign_abs_f16x2,
                        BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_ftz_xorsign_abs_f16:
    return MakeHalfType(Intrinsic::nvvm_fmax_ftz_xorsign_abs_f16, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmax_ftz_xorsign_abs_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmax_ftz_xorsign_abs_f16x2, BuiltinID,
                        E, *this);
  case NVPTX::BI__nvvm_fmax_nan_f16:
    return MakeHalfType(Intrinsic::nvvm_fmax_nan_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_nan_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmax_nan_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmax_nan_xorsign_abs_f16:
    return MakeHalfType(Intrinsic::nvvm_fmax_nan_xorsign_abs_f16, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmax_nan_xorsign_abs_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmax_nan_xorsign_abs_f16x2, BuiltinID,
                        E, *this);
  case NVPTX::BI__nvvm_fmax_xorsign_abs_f16:
    return MakeHalfType(Intrinsic::nvvm_fmax_xorsign_abs_f16, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmax_xorsign_abs_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmax_xorsign_abs_f16x2, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmin_f16:
    return MakeHalfType(Intrinsic::nvvm_fmin_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmin_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmin_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmin_ftz_f16:
    return MakeHalfType(Intrinsic::nvvm_fmin_ftz_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmin_ftz_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmin_ftz_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmin_ftz_nan_f16:
    return MakeHalfType(Intrinsic::nvvm_fmin_ftz_nan_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmin_ftz_nan_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmin_ftz_nan_f16x2, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmin_ftz_nan_xorsign_abs_f16:
    return MakeHalfType(Intrinsic::nvvm_fmin_ftz_nan_xorsign_abs_f16, BuiltinID,
                        E, *this);
  case NVPTX::BI__nvvm_fmin_ftz_nan_xorsign_abs_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmin_ftz_nan_xorsign_abs_f16x2,
                        BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmin_ftz_xorsign_abs_f16:
    return MakeHalfType(Intrinsic::nvvm_fmin_ftz_xorsign_abs_f16, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmin_ftz_xorsign_abs_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmin_ftz_xorsign_abs_f16x2, BuiltinID,
                        E, *this);
  case NVPTX::BI__nvvm_fmin_nan_f16:
    return MakeHalfType(Intrinsic::nvvm_fmin_nan_f16, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmin_nan_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmin_nan_f16x2, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_fmin_nan_xorsign_abs_f16:
    return MakeHalfType(Intrinsic::nvvm_fmin_nan_xorsign_abs_f16, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmin_nan_xorsign_abs_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmin_nan_xorsign_abs_f16x2, BuiltinID,
                        E, *this);
  case NVPTX::BI__nvvm_fmin_xorsign_abs_f16:
    return MakeHalfType(Intrinsic::nvvm_fmin_xorsign_abs_f16, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_fmin_xorsign_abs_f16x2:
    return MakeHalfType(Intrinsic::nvvm_fmin_xorsign_abs_f16x2, BuiltinID, E,
                        *this);
  case NVPTX::BI__nvvm_ldg_h:
    return MakeHalfType(Intrinsic::nvvm_ldg_global_f, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_ldg_h2:
    return MakeHalfType(Intrinsic::nvvm_ldg_global_f, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_ldu_h:
    return MakeHalfType(Intrinsic::nvvm_ldu_global_f, BuiltinID, E, *this);
  case NVPTX::BI__nvvm_ldu_h2: {
    return MakeHalfType(Intrinsic::nvvm_ldu_global_f, BuiltinID, E, *this);
  }
  case NVPTX::BI__nvvm_cp_async_ca_shared_global_4:
    return MakeCpAsync(Intrinsic::nvvm_cp_async_ca_shared_global_4,
                       Intrinsic::nvvm_cp_async_ca_shared_global_4_s, *this, E,
                       4);
  case NVPTX::BI__nvvm_cp_async_ca_shared_global_8:
    return MakeCpAsync(Intrinsic::nvvm_cp_async_ca_shared_global_8,
                       Intrinsic::nvvm_cp_async_ca_shared_global_8_s, *this, E,
                       8);
  case NVPTX::BI__nvvm_cp_async_ca_shared_global_16:
    return MakeCpAsync(Intrinsic::nvvm_cp_async_ca_shared_global_16,
                       Intrinsic::nvvm_cp_async_ca_shared_global_16_s, *this, E,
                       16);
  case NVPTX::BI__nvvm_cp_async_cg_shared_global_16:
    return MakeCpAsync(Intrinsic::nvvm_cp_async_cg_shared_global_16,
                       Intrinsic::nvvm_cp_async_cg_shared_global_16_s, *this, E,
                       16);
  case NVPTX::BI__nvvm_read_ptx_sreg_clusterid_x:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_clusterid_x));
  case NVPTX::BI__nvvm_read_ptx_sreg_clusterid_y:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_clusterid_y));
  case NVPTX::BI__nvvm_read_ptx_sreg_clusterid_z:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_clusterid_z));
  case NVPTX::BI__nvvm_read_ptx_sreg_clusterid_w:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_clusterid_w));
  case NVPTX::BI__nvvm_read_ptx_sreg_nclusterid_x:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_nclusterid_x));
  case NVPTX::BI__nvvm_read_ptx_sreg_nclusterid_y:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_nclusterid_y));
  case NVPTX::BI__nvvm_read_ptx_sreg_nclusterid_z:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_nclusterid_z));
  case NVPTX::BI__nvvm_read_ptx_sreg_nclusterid_w:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_nclusterid_w));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_ctaid_x:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_ctaid_x));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_ctaid_y:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_ctaid_y));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_ctaid_z:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_ctaid_z));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_ctaid_w:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_ctaid_w));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_nctaid_x:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_nctaid_x));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_nctaid_y:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_nctaid_y));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_nctaid_z:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_nctaid_z));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_nctaid_w:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_nctaid_w));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_ctarank:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_ctarank));
  case NVPTX::BI__nvvm_read_ptx_sreg_cluster_nctarank:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_read_ptx_sreg_cluster_nctarank));
  case NVPTX::BI__nvvm_is_explicit_cluster:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_is_explicit_cluster));
  case NVPTX::BI__nvvm_isspacep_shared_cluster:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_isspacep_shared_cluster),
        EmitScalarExpr(E->getArg(0)));
  case NVPTX::BI__nvvm_mapa:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_mapa),
        {EmitScalarExpr(E->getArg(0)), EmitScalarExpr(E->getArg(1))});
  case NVPTX::BI__nvvm_mapa_shared_cluster:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_mapa_shared_cluster),
        {EmitScalarExpr(E->getArg(0)), EmitScalarExpr(E->getArg(1))});
  case NVPTX::BI__nvvm_getctarank:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_getctarank),
        EmitScalarExpr(E->getArg(0)));
  case NVPTX::BI__nvvm_getctarank_shared_cluster:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_getctarank_shared_cluster),
        EmitScalarExpr(E->getArg(0)));
  case NVPTX::BI__nvvm_barrier_cluster_arrive:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_barrier_cluster_arrive));
  case NVPTX::BI__nvvm_barrier_cluster_arrive_relaxed:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_barrier_cluster_arrive_relaxed));
  case NVPTX::BI__nvvm_barrier_cluster_wait:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_barrier_cluster_wait));
  case NVPTX::BI__nvvm_fence_sc_cluster:
    return Builder.CreateCall(
        CGM.getIntrinsic(Intrinsic::nvvm_fence_sc_cluster));
  default:
    return nullptr;
  }
}

namespace {
struct BuiltinAlignArgs {
  llvm::Value *Src = nullptr;
  llvm::Type *SrcType = nullptr;
  llvm::Value *Alignment = nullptr;
  llvm::Value *Mask = nullptr;
  llvm::IntegerType *IntType = nullptr;

  BuiltinAlignArgs(const CallExpr *E, CodeGenFunction &CGF) {
    QualType AstType = E->getArg(0)->getType();
    if (AstType->isArrayType())
      Src = CGF.EmitArrayToPointerDecay(E->getArg(0)).emitRawPointer(CGF);
    else
      Src = CGF.EmitScalarExpr(E->getArg(0));
    SrcType = Src->getType();
    if (SrcType->isPointerTy()) {
      IntType = IntegerType::get(
          CGF.getLLVMContext(),
          CGF.CGM.getDataLayout().getIndexTypeSizeInBits(SrcType));
    } else {
      assert(SrcType->isIntegerTy());
      IntType = cast<llvm::IntegerType>(SrcType);
    }
    Alignment = CGF.EmitScalarExpr(E->getArg(1));
    Alignment = CGF.Builder.CreateZExtOrTrunc(Alignment, IntType, "alignment");
    auto *One = llvm::ConstantInt::get(IntType, 1);
    Mask = CGF.Builder.CreateSub(Alignment, One, "mask");
  }
};
} // namespace

/// Generate (x & (y-1)) == 0.
RValue CodeGenFunction::EmitBuiltinIsAligned(const CallExpr *E) {
  BuiltinAlignArgs Args(E, *this);
  llvm::Value *SrcAddress = Args.Src;
  if (Args.SrcType->isPointerTy())
    SrcAddress =
        Builder.CreateBitOrPointerCast(Args.Src, Args.IntType, "src_addr");
  return RValue::get(Builder.CreateICmpEQ(
      Builder.CreateAnd(SrcAddress, Args.Mask, "set_bits"),
      llvm::Constant::getNullValue(Args.IntType), "is_aligned"));
}

/// Generate (x & ~(y-1)) to align down or ((x+(y-1)) & ~(y-1)) to align up.
/// Note: For pointer types we can avoid ptrtoint/inttoptr pairs by using the
/// llvm.ptrmask intrinsic (with a GEP before in the align_up case).
RValue CodeGenFunction::EmitBuiltinAlignTo(const CallExpr *E, bool AlignUp) {
  BuiltinAlignArgs Args(E, *this);
  llvm::Value *SrcForMask = Args.Src;
  if (AlignUp) {
    // When aligning up we have to first add the mask to ensure we go over the
    // next alignment value and then align down to the next valid multiple.
    // By adding the mask, we ensure that align_up on an already aligned
    // value will not change the value.
    if (Args.Src->getType()->isPointerTy()) {
      if (getLangOpts().isSignedOverflowDefined())
        SrcForMask =
            Builder.CreateGEP(Int8Ty, SrcForMask, Args.Mask, "over_boundary");
      else
        SrcForMask = EmitCheckedInBoundsGEP(Int8Ty, SrcForMask, Args.Mask,
                                            /*SignedIndices=*/true,
                                            /*isSubtraction=*/false,
                                            E->getExprLoc(), "over_boundary");
    } else {
      SrcForMask = Builder.CreateAdd(SrcForMask, Args.Mask, "over_boundary");
    }
  }
  // Invert the mask to only clear the lower bits.
  llvm::Value *InvertedMask = Builder.CreateNot(Args.Mask, "inverted_mask");
  llvm::Value *Result = nullptr;
  if (Args.Src->getType()->isPointerTy()) {
    Result = Builder.CreateIntrinsic(
        Intrinsic::ptrmask, {Args.SrcType, Args.IntType},
        {SrcForMask, InvertedMask}, nullptr, "aligned_result");
  } else {
    Result = Builder.CreateAnd(SrcForMask, InvertedMask, "aligned_result");
  }
  assert(Result->getType() == Args.SrcType);
  return RValue::get(Result);
}

Value *CodeGenFunction::EmitWebAssemblyBuiltinExpr(unsigned BuiltinID,
                                                   const CallExpr *E) {
  switch (BuiltinID) {
  case WebAssembly::BI__builtin_wasm_memory_size: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *I = EmitScalarExpr(E->getArg(0));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_memory_size, ResultType);
    return Builder.CreateCall(Callee, I);
  }
  case WebAssembly::BI__builtin_wasm_memory_grow: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Value *Args[] = {EmitScalarExpr(E->getArg(0)),
                     EmitScalarExpr(E->getArg(1))};
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_memory_grow, ResultType);
    return Builder.CreateCall(Callee, Args);
  }
  case WebAssembly::BI__builtin_wasm_tls_size: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_tls_size, ResultType);
    return Builder.CreateCall(Callee);
  }
  case WebAssembly::BI__builtin_wasm_tls_align: {
    llvm::Type *ResultType = ConvertType(E->getType());
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_tls_align, ResultType);
    return Builder.CreateCall(Callee);
  }
  case WebAssembly::BI__builtin_wasm_tls_base: {
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_tls_base);
    return Builder.CreateCall(Callee);
  }
  case WebAssembly::BI__builtin_wasm_throw: {
    Value *Tag = EmitScalarExpr(E->getArg(0));
    Value *Obj = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_throw);
    return Builder.CreateCall(Callee, {Tag, Obj});
  }
  case WebAssembly::BI__builtin_wasm_rethrow: {
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_rethrow);
    return Builder.CreateCall(Callee);
  }
  case WebAssembly::BI__builtin_wasm_memory_atomic_wait32: {
    Value *Addr = EmitScalarExpr(E->getArg(0));
    Value *Expected = EmitScalarExpr(E->getArg(1));
    Value *Timeout = EmitScalarExpr(E->getArg(2));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_memory_atomic_wait32);
    return Builder.CreateCall(Callee, {Addr, Expected, Timeout});
  }
  case WebAssembly::BI__builtin_wasm_memory_atomic_wait64: {
    Value *Addr = EmitScalarExpr(E->getArg(0));
    Value *Expected = EmitScalarExpr(E->getArg(1));
    Value *Timeout = EmitScalarExpr(E->getArg(2));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_memory_atomic_wait64);
    return Builder.CreateCall(Callee, {Addr, Expected, Timeout});
  }
  case WebAssembly::BI__builtin_wasm_memory_atomic_notify: {
    Value *Addr = EmitScalarExpr(E->getArg(0));
    Value *Count = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_memory_atomic_notify);
    return Builder.CreateCall(Callee, {Addr, Count});
  }
  case WebAssembly::BI__builtin_wasm_trunc_s_i32_f32:
  case WebAssembly::BI__builtin_wasm_trunc_s_i32_f64:
  case WebAssembly::BI__builtin_wasm_trunc_s_i64_f32:
  case WebAssembly::BI__builtin_wasm_trunc_s_i64_f64: {
    Value *Src = EmitScalarExpr(E->getArg(0));
    llvm::Type *ResT = ConvertType(E->getType());
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_trunc_signed, {ResT, Src->getType()});
    return Builder.CreateCall(Callee, {Src});
  }
  case WebAssembly::BI__builtin_wasm_trunc_u_i32_f32:
  case WebAssembly::BI__builtin_wasm_trunc_u_i32_f64:
  case WebAssembly::BI__builtin_wasm_trunc_u_i64_f32:
  case WebAssembly::BI__builtin_wasm_trunc_u_i64_f64: {
    Value *Src = EmitScalarExpr(E->getArg(0));
    llvm::Type *ResT = ConvertType(E->getType());
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_trunc_unsigned,
                                        {ResT, Src->getType()});
    return Builder.CreateCall(Callee, {Src});
  }
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i32_f32:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i32_f64:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i64_f32:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i64_f64:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_s_i32x4_f32x4: {
    Value *Src = EmitScalarExpr(E->getArg(0));
    llvm::Type *ResT = ConvertType(E->getType());
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::fptosi_sat, {ResT, Src->getType()});
    return Builder.CreateCall(Callee, {Src});
  }
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i32_f32:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i32_f64:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i64_f32:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i64_f64:
  case WebAssembly::BI__builtin_wasm_trunc_saturate_u_i32x4_f32x4: {
    Value *Src = EmitScalarExpr(E->getArg(0));
    llvm::Type *ResT = ConvertType(E->getType());
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::fptoui_sat, {ResT, Src->getType()});
    return Builder.CreateCall(Callee, {Src});
  }
  case WebAssembly::BI__builtin_wasm_min_f32:
  case WebAssembly::BI__builtin_wasm_min_f64:
  case WebAssembly::BI__builtin_wasm_min_f16x8:
  case WebAssembly::BI__builtin_wasm_min_f32x4:
  case WebAssembly::BI__builtin_wasm_min_f64x2: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::minimum, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_max_f32:
  case WebAssembly::BI__builtin_wasm_max_f64:
  case WebAssembly::BI__builtin_wasm_max_f16x8:
  case WebAssembly::BI__builtin_wasm_max_f32x4:
  case WebAssembly::BI__builtin_wasm_max_f64x2: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::maximum, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_pmin_f16x8:
  case WebAssembly::BI__builtin_wasm_pmin_f32x4:
  case WebAssembly::BI__builtin_wasm_pmin_f64x2: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_pmin, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_pmax_f16x8:
  case WebAssembly::BI__builtin_wasm_pmax_f32x4:
  case WebAssembly::BI__builtin_wasm_pmax_f64x2: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_pmax, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_ceil_f32x4:
  case WebAssembly::BI__builtin_wasm_floor_f32x4:
  case WebAssembly::BI__builtin_wasm_trunc_f32x4:
  case WebAssembly::BI__builtin_wasm_nearest_f32x4:
  case WebAssembly::BI__builtin_wasm_ceil_f64x2:
  case WebAssembly::BI__builtin_wasm_floor_f64x2:
  case WebAssembly::BI__builtin_wasm_trunc_f64x2:
  case WebAssembly::BI__builtin_wasm_nearest_f64x2: {
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_ceil_f32x4:
    case WebAssembly::BI__builtin_wasm_ceil_f64x2:
      IntNo = Intrinsic::ceil;
      break;
    case WebAssembly::BI__builtin_wasm_floor_f32x4:
    case WebAssembly::BI__builtin_wasm_floor_f64x2:
      IntNo = Intrinsic::floor;
      break;
    case WebAssembly::BI__builtin_wasm_trunc_f32x4:
    case WebAssembly::BI__builtin_wasm_trunc_f64x2:
      IntNo = Intrinsic::trunc;
      break;
    case WebAssembly::BI__builtin_wasm_nearest_f32x4:
    case WebAssembly::BI__builtin_wasm_nearest_f64x2:
      IntNo = Intrinsic::nearbyint;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Value *Value = EmitScalarExpr(E->getArg(0));
    Function *Callee = CGM.getIntrinsic(IntNo, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, Value);
  }
  case WebAssembly::BI__builtin_wasm_ref_null_extern: {
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_ref_null_extern);
    return Builder.CreateCall(Callee);
  }
  case WebAssembly::BI__builtin_wasm_ref_null_func: {
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_ref_null_func);
    return Builder.CreateCall(Callee);
  }
  case WebAssembly::BI__builtin_wasm_swizzle_i8x16: {
    Value *Src = EmitScalarExpr(E->getArg(0));
    Value *Indices = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_swizzle);
    return Builder.CreateCall(Callee, {Src, Indices});
  }
  case WebAssembly::BI__builtin_wasm_add_sat_s_i8x16:
  case WebAssembly::BI__builtin_wasm_add_sat_u_i8x16:
  case WebAssembly::BI__builtin_wasm_add_sat_s_i16x8:
  case WebAssembly::BI__builtin_wasm_add_sat_u_i16x8:
  case WebAssembly::BI__builtin_wasm_sub_sat_s_i8x16:
  case WebAssembly::BI__builtin_wasm_sub_sat_u_i8x16:
  case WebAssembly::BI__builtin_wasm_sub_sat_s_i16x8:
  case WebAssembly::BI__builtin_wasm_sub_sat_u_i16x8: {
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_add_sat_s_i8x16:
    case WebAssembly::BI__builtin_wasm_add_sat_s_i16x8:
      IntNo = Intrinsic::sadd_sat;
      break;
    case WebAssembly::BI__builtin_wasm_add_sat_u_i8x16:
    case WebAssembly::BI__builtin_wasm_add_sat_u_i16x8:
      IntNo = Intrinsic::uadd_sat;
      break;
    case WebAssembly::BI__builtin_wasm_sub_sat_s_i8x16:
    case WebAssembly::BI__builtin_wasm_sub_sat_s_i16x8:
      IntNo = Intrinsic::wasm_sub_sat_signed;
      break;
    case WebAssembly::BI__builtin_wasm_sub_sat_u_i8x16:
    case WebAssembly::BI__builtin_wasm_sub_sat_u_i16x8:
      IntNo = Intrinsic::wasm_sub_sat_unsigned;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(IntNo, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_abs_i8x16:
  case WebAssembly::BI__builtin_wasm_abs_i16x8:
  case WebAssembly::BI__builtin_wasm_abs_i32x4:
  case WebAssembly::BI__builtin_wasm_abs_i64x2: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Value *Neg = Builder.CreateNeg(Vec, "neg");
    Constant *Zero = llvm::Constant::getNullValue(Vec->getType());
    Value *ICmp = Builder.CreateICmpSLT(Vec, Zero, "abscond");
    return Builder.CreateSelect(ICmp, Neg, Vec, "abs");
  }
  case WebAssembly::BI__builtin_wasm_min_s_i8x16:
  case WebAssembly::BI__builtin_wasm_min_u_i8x16:
  case WebAssembly::BI__builtin_wasm_max_s_i8x16:
  case WebAssembly::BI__builtin_wasm_max_u_i8x16:
  case WebAssembly::BI__builtin_wasm_min_s_i16x8:
  case WebAssembly::BI__builtin_wasm_min_u_i16x8:
  case WebAssembly::BI__builtin_wasm_max_s_i16x8:
  case WebAssembly::BI__builtin_wasm_max_u_i16x8:
  case WebAssembly::BI__builtin_wasm_min_s_i32x4:
  case WebAssembly::BI__builtin_wasm_min_u_i32x4:
  case WebAssembly::BI__builtin_wasm_max_s_i32x4:
  case WebAssembly::BI__builtin_wasm_max_u_i32x4: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Value *ICmp;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_min_s_i8x16:
    case WebAssembly::BI__builtin_wasm_min_s_i16x8:
    case WebAssembly::BI__builtin_wasm_min_s_i32x4:
      ICmp = Builder.CreateICmpSLT(LHS, RHS);
      break;
    case WebAssembly::BI__builtin_wasm_min_u_i8x16:
    case WebAssembly::BI__builtin_wasm_min_u_i16x8:
    case WebAssembly::BI__builtin_wasm_min_u_i32x4:
      ICmp = Builder.CreateICmpULT(LHS, RHS);
      break;
    case WebAssembly::BI__builtin_wasm_max_s_i8x16:
    case WebAssembly::BI__builtin_wasm_max_s_i16x8:
    case WebAssembly::BI__builtin_wasm_max_s_i32x4:
      ICmp = Builder.CreateICmpSGT(LHS, RHS);
      break;
    case WebAssembly::BI__builtin_wasm_max_u_i8x16:
    case WebAssembly::BI__builtin_wasm_max_u_i16x8:
    case WebAssembly::BI__builtin_wasm_max_u_i32x4:
      ICmp = Builder.CreateICmpUGT(LHS, RHS);
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    return Builder.CreateSelect(ICmp, LHS, RHS);
  }
  case WebAssembly::BI__builtin_wasm_avgr_u_i8x16:
  case WebAssembly::BI__builtin_wasm_avgr_u_i16x8: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_avgr_unsigned,
                                        ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_q15mulr_sat_s_i16x8: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_q15mulr_sat_signed);
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_extadd_pairwise_i8x16_s_i16x8:
  case WebAssembly::BI__builtin_wasm_extadd_pairwise_i8x16_u_i16x8:
  case WebAssembly::BI__builtin_wasm_extadd_pairwise_i16x8_s_i32x4:
  case WebAssembly::BI__builtin_wasm_extadd_pairwise_i16x8_u_i32x4: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_extadd_pairwise_i8x16_s_i16x8:
    case WebAssembly::BI__builtin_wasm_extadd_pairwise_i16x8_s_i32x4:
      IntNo = Intrinsic::wasm_extadd_pairwise_signed;
      break;
    case WebAssembly::BI__builtin_wasm_extadd_pairwise_i8x16_u_i16x8:
    case WebAssembly::BI__builtin_wasm_extadd_pairwise_i16x8_u_i32x4:
      IntNo = Intrinsic::wasm_extadd_pairwise_unsigned;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }

    Function *Callee = CGM.getIntrinsic(IntNo, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, Vec);
  }
  case WebAssembly::BI__builtin_wasm_bitselect: {
    Value *V1 = EmitScalarExpr(E->getArg(0));
    Value *V2 = EmitScalarExpr(E->getArg(1));
    Value *C = EmitScalarExpr(E->getArg(2));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_bitselect, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {V1, V2, C});
  }
  case WebAssembly::BI__builtin_wasm_dot_s_i32x4_i16x8: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_dot);
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_popcnt_i8x16: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::ctpop, ConvertType(E->getType()));
    return Builder.CreateCall(Callee, {Vec});
  }
  case WebAssembly::BI__builtin_wasm_any_true_v128:
  case WebAssembly::BI__builtin_wasm_all_true_i8x16:
  case WebAssembly::BI__builtin_wasm_all_true_i16x8:
  case WebAssembly::BI__builtin_wasm_all_true_i32x4:
  case WebAssembly::BI__builtin_wasm_all_true_i64x2: {
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_any_true_v128:
      IntNo = Intrinsic::wasm_anytrue;
      break;
    case WebAssembly::BI__builtin_wasm_all_true_i8x16:
    case WebAssembly::BI__builtin_wasm_all_true_i16x8:
    case WebAssembly::BI__builtin_wasm_all_true_i32x4:
    case WebAssembly::BI__builtin_wasm_all_true_i64x2:
      IntNo = Intrinsic::wasm_alltrue;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Function *Callee = CGM.getIntrinsic(IntNo, Vec->getType());
    return Builder.CreateCall(Callee, {Vec});
  }
  case WebAssembly::BI__builtin_wasm_bitmask_i8x16:
  case WebAssembly::BI__builtin_wasm_bitmask_i16x8:
  case WebAssembly::BI__builtin_wasm_bitmask_i32x4:
  case WebAssembly::BI__builtin_wasm_bitmask_i64x2: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_bitmask, Vec->getType());
    return Builder.CreateCall(Callee, {Vec});
  }
  case WebAssembly::BI__builtin_wasm_abs_f32x4:
  case WebAssembly::BI__builtin_wasm_abs_f64x2: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Function *Callee = CGM.getIntrinsic(Intrinsic::fabs, Vec->getType());
    return Builder.CreateCall(Callee, {Vec});
  }
  case WebAssembly::BI__builtin_wasm_sqrt_f32x4:
  case WebAssembly::BI__builtin_wasm_sqrt_f64x2: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    Function *Callee = CGM.getIntrinsic(Intrinsic::sqrt, Vec->getType());
    return Builder.CreateCall(Callee, {Vec});
  }
  case WebAssembly::BI__builtin_wasm_narrow_s_i8x16_i16x8:
  case WebAssembly::BI__builtin_wasm_narrow_u_i8x16_i16x8:
  case WebAssembly::BI__builtin_wasm_narrow_s_i16x8_i32x4:
  case WebAssembly::BI__builtin_wasm_narrow_u_i16x8_i32x4: {
    Value *Low = EmitScalarExpr(E->getArg(0));
    Value *High = EmitScalarExpr(E->getArg(1));
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_narrow_s_i8x16_i16x8:
    case WebAssembly::BI__builtin_wasm_narrow_s_i16x8_i32x4:
      IntNo = Intrinsic::wasm_narrow_signed;
      break;
    case WebAssembly::BI__builtin_wasm_narrow_u_i8x16_i16x8:
    case WebAssembly::BI__builtin_wasm_narrow_u_i16x8_i32x4:
      IntNo = Intrinsic::wasm_narrow_unsigned;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Function *Callee =
        CGM.getIntrinsic(IntNo, {ConvertType(E->getType()), Low->getType()});
    return Builder.CreateCall(Callee, {Low, High});
  }
  case WebAssembly::BI__builtin_wasm_trunc_sat_s_zero_f64x2_i32x4:
  case WebAssembly::BI__builtin_wasm_trunc_sat_u_zero_f64x2_i32x4: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_trunc_sat_s_zero_f64x2_i32x4:
      IntNo = Intrinsic::fptosi_sat;
      break;
    case WebAssembly::BI__builtin_wasm_trunc_sat_u_zero_f64x2_i32x4:
      IntNo = Intrinsic::fptoui_sat;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    llvm::Type *SrcT = Vec->getType();
    llvm::Type *TruncT = SrcT->getWithNewType(Builder.getInt32Ty());
    Function *Callee = CGM.getIntrinsic(IntNo, {TruncT, SrcT});
    Value *Trunc = Builder.CreateCall(Callee, Vec);
    Value *Splat = Constant::getNullValue(TruncT);
    return Builder.CreateShuffleVector(Trunc, Splat, ArrayRef<int>{0, 1, 2, 3});
  }
  case WebAssembly::BI__builtin_wasm_shuffle_i8x16: {
    Value *Ops[18];
    size_t OpIdx = 0;
    Ops[OpIdx++] = EmitScalarExpr(E->getArg(0));
    Ops[OpIdx++] = EmitScalarExpr(E->getArg(1));
    while (OpIdx < 18) {
      std::optional<llvm::APSInt> LaneConst =
          E->getArg(OpIdx)->getIntegerConstantExpr(getContext());
      assert(LaneConst && "Constant arg isn't actually constant?");
      Ops[OpIdx++] = llvm::ConstantInt::get(getLLVMContext(), *LaneConst);
    }
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_shuffle);
    return Builder.CreateCall(Callee, Ops);
  }
  case WebAssembly::BI__builtin_wasm_relaxed_madd_f16x8:
  case WebAssembly::BI__builtin_wasm_relaxed_nmadd_f16x8:
  case WebAssembly::BI__builtin_wasm_relaxed_madd_f32x4:
  case WebAssembly::BI__builtin_wasm_relaxed_nmadd_f32x4:
  case WebAssembly::BI__builtin_wasm_relaxed_madd_f64x2:
  case WebAssembly::BI__builtin_wasm_relaxed_nmadd_f64x2: {
    Value *A = EmitScalarExpr(E->getArg(0));
    Value *B = EmitScalarExpr(E->getArg(1));
    Value *C = EmitScalarExpr(E->getArg(2));
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_relaxed_madd_f16x8:
    case WebAssembly::BI__builtin_wasm_relaxed_madd_f32x4:
    case WebAssembly::BI__builtin_wasm_relaxed_madd_f64x2:
      IntNo = Intrinsic::wasm_relaxed_madd;
      break;
    case WebAssembly::BI__builtin_wasm_relaxed_nmadd_f16x8:
    case WebAssembly::BI__builtin_wasm_relaxed_nmadd_f32x4:
    case WebAssembly::BI__builtin_wasm_relaxed_nmadd_f64x2:
      IntNo = Intrinsic::wasm_relaxed_nmadd;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Function *Callee = CGM.getIntrinsic(IntNo, A->getType());
    return Builder.CreateCall(Callee, {A, B, C});
  }
  case WebAssembly::BI__builtin_wasm_relaxed_laneselect_i8x16:
  case WebAssembly::BI__builtin_wasm_relaxed_laneselect_i16x8:
  case WebAssembly::BI__builtin_wasm_relaxed_laneselect_i32x4:
  case WebAssembly::BI__builtin_wasm_relaxed_laneselect_i64x2: {
    Value *A = EmitScalarExpr(E->getArg(0));
    Value *B = EmitScalarExpr(E->getArg(1));
    Value *C = EmitScalarExpr(E->getArg(2));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_relaxed_laneselect, A->getType());
    return Builder.CreateCall(Callee, {A, B, C});
  }
  case WebAssembly::BI__builtin_wasm_relaxed_swizzle_i8x16: {
    Value *Src = EmitScalarExpr(E->getArg(0));
    Value *Indices = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_relaxed_swizzle);
    return Builder.CreateCall(Callee, {Src, Indices});
  }
  case WebAssembly::BI__builtin_wasm_relaxed_min_f32x4:
  case WebAssembly::BI__builtin_wasm_relaxed_max_f32x4:
  case WebAssembly::BI__builtin_wasm_relaxed_min_f64x2:
  case WebAssembly::BI__builtin_wasm_relaxed_max_f64x2: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_relaxed_min_f32x4:
    case WebAssembly::BI__builtin_wasm_relaxed_min_f64x2:
      IntNo = Intrinsic::wasm_relaxed_min;
      break;
    case WebAssembly::BI__builtin_wasm_relaxed_max_f32x4:
    case WebAssembly::BI__builtin_wasm_relaxed_max_f64x2:
      IntNo = Intrinsic::wasm_relaxed_max;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Function *Callee = CGM.getIntrinsic(IntNo, LHS->getType());
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_relaxed_trunc_s_i32x4_f32x4:
  case WebAssembly::BI__builtin_wasm_relaxed_trunc_u_i32x4_f32x4:
  case WebAssembly::BI__builtin_wasm_relaxed_trunc_s_zero_i32x4_f64x2:
  case WebAssembly::BI__builtin_wasm_relaxed_trunc_u_zero_i32x4_f64x2: {
    Value *Vec = EmitScalarExpr(E->getArg(0));
    unsigned IntNo;
    switch (BuiltinID) {
    case WebAssembly::BI__builtin_wasm_relaxed_trunc_s_i32x4_f32x4:
      IntNo = Intrinsic::wasm_relaxed_trunc_signed;
      break;
    case WebAssembly::BI__builtin_wasm_relaxed_trunc_u_i32x4_f32x4:
      IntNo = Intrinsic::wasm_relaxed_trunc_unsigned;
      break;
    case WebAssembly::BI__builtin_wasm_relaxed_trunc_s_zero_i32x4_f64x2:
      IntNo = Intrinsic::wasm_relaxed_trunc_signed_zero;
      break;
    case WebAssembly::BI__builtin_wasm_relaxed_trunc_u_zero_i32x4_f64x2:
      IntNo = Intrinsic::wasm_relaxed_trunc_unsigned_zero;
      break;
    default:
      llvm_unreachable("unexpected builtin ID");
    }
    Function *Callee = CGM.getIntrinsic(IntNo);
    return Builder.CreateCall(Callee, {Vec});
  }
  case WebAssembly::BI__builtin_wasm_relaxed_q15mulr_s_i16x8: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_relaxed_q15mulr_signed);
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_relaxed_dot_i8x16_i7x16_s_i16x8: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_relaxed_dot_i8x16_i7x16_signed);
    return Builder.CreateCall(Callee, {LHS, RHS});
  }
  case WebAssembly::BI__builtin_wasm_relaxed_dot_i8x16_i7x16_add_s_i32x4: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Value *Acc = EmitScalarExpr(E->getArg(2));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_relaxed_dot_i8x16_i7x16_add_signed);
    return Builder.CreateCall(Callee, {LHS, RHS, Acc});
  }
  case WebAssembly::BI__builtin_wasm_relaxed_dot_bf16x8_add_f32_f32x4: {
    Value *LHS = EmitScalarExpr(E->getArg(0));
    Value *RHS = EmitScalarExpr(E->getArg(1));
    Value *Acc = EmitScalarExpr(E->getArg(2));
    Function *Callee =
        CGM.getIntrinsic(Intrinsic::wasm_relaxed_dot_bf16x8_add_f32);
    return Builder.CreateCall(Callee, {LHS, RHS, Acc});
  }
  case WebAssembly::BI__builtin_wasm_loadf16_f32: {
    Value *Addr = EmitScalarExpr(E->getArg(0));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_loadf16_f32);
    return Builder.CreateCall(Callee, {Addr});
  }
  case WebAssembly::BI__builtin_wasm_storef16_f32: {
    Value *Val = EmitScalarExpr(E->getArg(0));
    Value *Addr = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_storef16_f32);
    return Builder.CreateCall(Callee, {Val, Addr});
  }
  case WebAssembly::BI__builtin_wasm_splat_f16x8: {
    Value *Val = EmitScalarExpr(E->getArg(0));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_splat_f16x8);
    return Builder.CreateCall(Callee, {Val});
  }
  case WebAssembly::BI__builtin_wasm_extract_lane_f16x8: {
    Value *Vector = EmitScalarExpr(E->getArg(0));
    Value *Index = EmitScalarExpr(E->getArg(1));
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_extract_lane_f16x8);
    return Builder.CreateCall(Callee, {Vector, Index});
  }
  case WebAssembly::BI__builtin_wasm_table_get: {
    assert(E->getArg(0)->getType()->isArrayType());
    Value *Table = EmitArrayToPointerDecay(E->getArg(0)).emitRawPointer(*this);
    Value *Index = EmitScalarExpr(E->getArg(1));
    Function *Callee;
    if (E->getType().isWebAssemblyExternrefType())
      Callee = CGM.getIntrinsic(Intrinsic::wasm_table_get_externref);
    else if (E->getType().isWebAssemblyFuncrefType())
      Callee = CGM.getIntrinsic(Intrinsic::wasm_table_get_funcref);
    else
      llvm_unreachable(
          "Unexpected reference type for __builtin_wasm_table_get");
    return Builder.CreateCall(Callee, {Table, Index});
  }
  case WebAssembly::BI__builtin_wasm_table_set: {
    assert(E->getArg(0)->getType()->isArrayType());
    Value *Table = EmitArrayToPointerDecay(E->getArg(0)).emitRawPointer(*this);
    Value *Index = EmitScalarExpr(E->getArg(1));
    Value *Val = EmitScalarExpr(E->getArg(2));
    Function *Callee;
    if (E->getArg(2)->getType().isWebAssemblyExternrefType())
      Callee = CGM.getIntrinsic(Intrinsic::wasm_table_set_externref);
    else if (E->getArg(2)->getType().isWebAssemblyFuncrefType())
      Callee = CGM.getIntrinsic(Intrinsic::wasm_table_set_funcref);
    else
      llvm_unreachable(
          "Unexpected reference type for __builtin_wasm_table_set");
    return Builder.CreateCall(Callee, {Table, Index, Val});
  }
  case WebAssembly::BI__builtin_wasm_table_size: {
    assert(E->getArg(0)->getType()->isArrayType());
    Value *Value = EmitArrayToPointerDecay(E->getArg(0)).emitRawPointer(*this);
    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_table_size);
    return Builder.CreateCall(Callee, Value);
  }
  case WebAssembly::BI__builtin_wasm_table_grow: {
    assert(E->getArg(0)->getType()->isArrayType());
    Value *Table = EmitArrayToPointerDecay(E->getArg(0)).emitRawPointer(*this);
    Value *Val = EmitScalarExpr(E->getArg(1));
    Value *NElems = EmitScalarExpr(E->getArg(2));

    Function *Callee;
    if (E->getArg(1)->getType().isWebAssemblyExternrefType())
      Callee = CGM.getIntrinsic(Intrinsic::wasm_table_grow_externref);
    else if (E->getArg(2)->getType().isWebAssemblyFuncrefType())
      Callee = CGM.getIntrinsic(Intrinsic::wasm_table_fill_funcref);
    else
      llvm_unreachable(
          "Unexpected reference type for __builtin_wasm_table_grow");

    return Builder.CreateCall(Callee, {Table, Val, NElems});
  }
  case WebAssembly::BI__builtin_wasm_table_fill: {
    assert(E->getArg(0)->getType()->isArrayType());
    Value *Table = EmitArrayToPointerDecay(E->getArg(0)).emitRawPointer(*this);
    Value *Index = EmitScalarExpr(E->getArg(1));
    Value *Val = EmitScalarExpr(E->getArg(2));
    Value *NElems = EmitScalarExpr(E->getArg(3));

    Function *Callee;
    if (E->getArg(2)->getType().isWebAssemblyExternrefType())
      Callee = CGM.getIntrinsic(Intrinsic::wasm_table_fill_externref);
    else if (E->getArg(2)->getType().isWebAssemblyFuncrefType())
      Callee = CGM.getIntrinsic(Intrinsic::wasm_table_fill_funcref);
    else
      llvm_unreachable(
          "Unexpected reference type for __builtin_wasm_table_fill");

    return Builder.CreateCall(Callee, {Table, Index, Val, NElems});
  }
  case WebAssembly::BI__builtin_wasm_table_copy: {
    assert(E->getArg(0)->getType()->isArrayType());
    Value *TableX = EmitArrayToPointerDecay(E->getArg(0)).emitRawPointer(*this);
    Value *TableY = EmitArrayToPointerDecay(E->getArg(1)).emitRawPointer(*this);
    Value *DstIdx = EmitScalarExpr(E->getArg(2));
    Value *SrcIdx = EmitScalarExpr(E->getArg(3));
    Value *NElems = EmitScalarExpr(E->getArg(4));

    Function *Callee = CGM.getIntrinsic(Intrinsic::wasm_table_copy);

    return Builder.CreateCall(Callee, {TableX, TableY, SrcIdx, DstIdx, NElems});
  }
  default:
    return nullptr;
  }
}

static std::pair<Intrinsic::ID, unsigned>
getIntrinsicForHexagonNonClangBuiltin(unsigned BuiltinID) {
  struct Info {
    unsigned BuiltinID;
    Intrinsic::ID IntrinsicID;
    unsigned VecLen;
  };
  static Info Infos[] = {
#define CUSTOM_BUILTIN_MAPPING(x,s) \
  { Hexagon::BI__builtin_HEXAGON_##x, Intrinsic::hexagon_##x, s },
    CUSTOM_BUILTIN_MAPPING(L2_loadrub_pci, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadrb_pci, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadruh_pci, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadrh_pci, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadri_pci, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadrd_pci, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadrub_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadrb_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadruh_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadrh_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadri_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(L2_loadrd_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storerb_pci, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storerh_pci, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storerf_pci, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storeri_pci, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storerd_pci, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storerb_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storerh_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storerf_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storeri_pcr, 0)
    CUSTOM_BUILTIN_MAPPING(S2_storerd_pcr, 0)
    // Legacy builtins that take a vector in place of a vector predicate.
    CUSTOM_BUILTIN_MAPPING(V6_vmaskedstoreq, 64)
    CUSTOM_BUILTIN_MAPPING(V6_vmaskedstorenq, 64)
    CUSTOM_BUILTIN_MAPPING(V6_vmaskedstorentq, 64)
    CUSTOM_BUILTIN_MAPPING(V6_vmaskedstorentnq, 64)
    CUSTOM_BUILTIN_MAPPING(V6_vmaskedstoreq_128B, 128)
    CUSTOM_BUILTIN_MAPPING(V6_vmaskedstorenq_128B, 128)
    CUSTOM_BUILTIN_MAPPING(V6_vmaskedstorentq_128B, 128)
    CUSTOM_BUILTIN_MAPPING(V6_vmaskedstorentnq_128B, 128)
#include "clang/Basic/BuiltinsHexagonMapCustomDep.def"
#undef CUSTOM_BUILTIN_MAPPING
  };

  auto CmpInfo = [] (Info A, Info B) { return A.BuiltinID < B.BuiltinID; };
  static const bool SortOnce = (llvm::sort(Infos, CmpInfo), true);
  (void)SortOnce;

  const Info *F = llvm::lower_bound(Infos, Info{BuiltinID, 0, 0}, CmpInfo);
  if (F == std::end(Infos) || F->BuiltinID != BuiltinID)
    return {Intrinsic::not_intrinsic, 0};

  return {F->IntrinsicID, F->VecLen};
}

Value *CodeGenFunction::EmitHexagonBuiltinExpr(unsigned BuiltinID,
                                               const CallExpr *E) {
  Intrinsic::ID ID;
  unsigned VecLen;
  std::tie(ID, VecLen) = getIntrinsicForHexagonNonClangBuiltin(BuiltinID);

  auto MakeCircOp = [this, E](unsigned IntID, bool IsLoad) {
    // The base pointer is passed by address, so it needs to be loaded.
    Address A = EmitPointerWithAlignment(E->getArg(0));
    Address BP = Address(A.emitRawPointer(*this), Int8PtrTy, A.getAlignment());
    llvm::Value *Base = Builder.CreateLoad(BP);
    // The treatment of both loads and stores is the same: the arguments for
    // the builtin are the same as the arguments for the intrinsic.
    // Load:
    //   builtin(Base, Inc, Mod, Start) -> intr(Base, Inc, Mod, Start)
    //   builtin(Base, Mod, Start)      -> intr(Base, Mod, Start)
    // Store:
    //   builtin(Base, Inc, Mod, Val, Start) -> intr(Base, Inc, Mod, Val, Start)
    //   builtin(Base, Mod, Val, Start)      -> intr(Base, Mod, Val, Start)
    SmallVector<llvm::Value*,5> Ops = { Base };
    for (unsigned i = 1, e = E->getNumArgs(); i != e; ++i)
      Ops.push_back(EmitScalarExpr(E->getArg(i)));

    llvm::Value *Result = Builder.CreateCall(CGM.getIntrinsic(IntID), Ops);
    // The load intrinsics generate two results (Value, NewBase), stores
    // generate one (NewBase). The new base address needs to be stored.
    llvm::Value *NewBase = IsLoad ? Builder.CreateExtractValue(Result, 1)
                                  : Result;
    llvm::Value *LV = EmitScalarExpr(E->getArg(0));
    Address Dest = EmitPointerWithAlignment(E->getArg(0));
    llvm::Value *RetVal =
        Builder.CreateAlignedStore(NewBase, LV, Dest.getAlignment());
    if (IsLoad)
      RetVal = Builder.CreateExtractValue(Result, 0);
    return RetVal;
  };

  // Handle the conversion of bit-reverse load intrinsics to bit code.
  // The intrinsic call after this function only reads from memory and the
  // write to memory is dealt by the store instruction.
  auto MakeBrevLd = [this, E](unsigned IntID, llvm::Type *DestTy) {
    // The intrinsic generates one result, which is the new value for the base
    // pointer. It needs to be returned. The result of the load instruction is
    // passed to intrinsic by address, so the value needs to be stored.
    llvm::Value *BaseAddress = EmitScalarExpr(E->getArg(0));

    // Expressions like &(*pt++) will be incremented per evaluation.
    // EmitPointerWithAlignment and EmitScalarExpr evaluates the expression
    // per call.
    Address DestAddr = EmitPointerWithAlignment(E->getArg(1));
    DestAddr = DestAddr.withElementType(Int8Ty);
    llvm::Value *DestAddress = DestAddr.emitRawPointer(*this);

    // Operands are Base, Dest, Modifier.
    // The intrinsic format in LLVM IR is defined as
    // { ValueType, i8* } (i8*, i32).
    llvm::Value *Result = Builder.CreateCall(
        CGM.getIntrinsic(IntID), {BaseAddress, EmitScalarExpr(E->getArg(2))});

    // The value needs to be stored as the variable is passed by reference.
    llvm::Value *DestVal = Builder.CreateExtractValue(Result, 0);

    // The store needs to be truncated to fit the destination type.
    // While i32 and i64 are natively supported on Hexagon, i8 and i16 needs
    // to be handled with stores of respective destination type.
    DestVal = Builder.CreateTrunc(DestVal, DestTy);

    Builder.CreateAlignedStore(DestVal, DestAddress, DestAddr.getAlignment());
    // The updated value of the base pointer is returned.
    return Builder.CreateExtractValue(Result, 1);
  };

  auto V2Q = [this, VecLen] (llvm::Value *Vec) {
    Intrinsic::ID ID = VecLen == 128 ? Intrinsic::hexagon_V6_vandvrt_128B
                                     : Intrinsic::hexagon_V6_vandvrt;
    return Builder.CreateCall(CGM.getIntrinsic(ID),
                              {Vec, Builder.getInt32(-1)});
  };
  auto Q2V = [this, VecLen] (llvm::Value *Pred) {
    Intrinsic::ID ID = VecLen == 128 ? Intrinsic::hexagon_V6_vandqrt_128B
                                     : Intrinsic::hexagon_V6_vandqrt;
    return Builder.CreateCall(CGM.getIntrinsic(ID),
                              {Pred, Builder.getInt32(-1)});
  };

  switch (BuiltinID) {
  // These intrinsics return a tuple {Vector, VectorPred} in LLVM IR,
  // and the corresponding C/C++ builtins use loads/stores to update
  // the predicate.
  case Hexagon::BI__builtin_HEXAGON_V6_vaddcarry:
  case Hexagon::BI__builtin_HEXAGON_V6_vaddcarry_128B:
  case Hexagon::BI__builtin_HEXAGON_V6_vsubcarry:
  case Hexagon::BI__builtin_HEXAGON_V6_vsubcarry_128B: {
    // Get the type from the 0-th argument.
    llvm::Type *VecType = ConvertType(E->getArg(0)->getType());
    Address PredAddr =
        EmitPointerWithAlignment(E->getArg(2)).withElementType(VecType);
    llvm::Value *PredIn = V2Q(Builder.CreateLoad(PredAddr));
    llvm::Value *Result = Builder.CreateCall(CGM.getIntrinsic(ID),
        {EmitScalarExpr(E->getArg(0)), EmitScalarExpr(E->getArg(1)), PredIn});

    llvm::Value *PredOut = Builder.CreateExtractValue(Result, 1);
    Builder.CreateAlignedStore(Q2V(PredOut), PredAddr.emitRawPointer(*this),
                               PredAddr.getAlignment());
    return Builder.CreateExtractValue(Result, 0);
  }
  // These are identical to the builtins above, except they don't consume
  // input carry, only generate carry-out. Since they still produce two
  // outputs, generate the store of the predicate, but no load.
  case Hexagon::BI__builtin_HEXAGON_V6_vaddcarryo:
  case Hexagon::BI__builtin_HEXAGON_V6_vaddcarryo_128B:
  case Hexagon::BI__builtin_HEXAGON_V6_vsubcarryo:
  case Hexagon::BI__builtin_HEXAGON_V6_vsubcarryo_128B: {
    // Get the type from the 0-th argument.
    llvm::Type *VecType = ConvertType(E->getArg(0)->getType());
    Address PredAddr =
        EmitPointerWithAlignment(E->getArg(2)).withElementType(VecType);
    llvm::Value *Result = Builder.CreateCall(CGM.getIntrinsic(ID),
        {EmitScalarExpr(E->getArg(0)), EmitScalarExpr(E->getArg(1))});

    llvm::Value *PredOut = Builder.CreateExtractValue(Result, 1);
    Builder.CreateAlignedStore(Q2V(PredOut), PredAddr.emitRawPointer(*this),
                               PredAddr.getAlignment());
    return Builder.CreateExtractValue(Result, 0);
  }

  case Hexagon::BI__builtin_HEXAGON_V6_vmaskedstoreq:
  case Hexagon::BI__builtin_HEXAGON_V6_vmaskedstorenq:
  case Hexagon::BI__builtin_HEXAGON_V6_vmaskedstorentq:
  case Hexagon::BI__builtin_HEXAGON_V6_vmaskedstorentnq:
  case Hexagon::BI__builtin_HEXAGON_V6_vmaskedstoreq_128B:
  case Hexagon::BI__builtin_HEXAGON_V6_vmaskedstorenq_128B:
  case Hexagon::BI__builtin_HEXAGON_V6_vmaskedstorentq_128B:
  case Hexagon::BI__builtin_HEXAGON_V6_vmaskedstorentnq_128B: {
    SmallVector<llvm::Value*,4> Ops;
    const Expr *PredOp = E->getArg(0);
    // There will be an implicit cast to a boolean vector. Strip it.
    if (auto *Cast = dyn_cast<ImplicitCastExpr>(PredOp)) {
      if (Cast->getCastKind() == CK_BitCast)
        PredOp = Cast->getSubExpr();
      Ops.push_back(V2Q(EmitScalarExpr(PredOp)));
    }
    for (int i = 1, e = E->getNumArgs(); i != e; ++i)
      Ops.push_back(EmitScalarExpr(E->getArg(i)));
    return Builder.CreateCall(CGM.getIntrinsic(ID), Ops);
  }

  case Hexagon::BI__builtin_HEXAGON_L2_loadrub_pci:
  case Hexagon::BI__builtin_HEXAGON_L2_loadrb_pci:
  case Hexagon::BI__builtin_HEXAGON_L2_loadruh_pci:
  case Hexagon::BI__builtin_HEXAGON_L2_loadrh_pci:
  case Hexagon::BI__builtin_HEXAGON_L2_loadri_pci:
  case Hexagon::BI__builtin_HEXAGON_L2_loadrd_pci:
  case Hexagon::BI__builtin_HEXAGON_L2_loadrub_pcr:
  case Hexagon::BI__builtin_HEXAGON_L2_loadrb_pcr:
  case Hexagon::BI__builtin_HEXAGON_L2_loadruh_pcr:
  case Hexagon::BI__builtin_HEXAGON_L2_loadrh_pcr:
  case Hexagon::BI__builtin_HEXAGON_L2_loadri_pcr:
  case Hexagon::BI__builtin_HEXAGON_L2_loadrd_pcr:
    return MakeCircOp(ID, /*IsLoad=*/true);
  case Hexagon::BI__builtin_HEXAGON_S2_storerb_pci:
  case Hexagon::BI__builtin_HEXAGON_S2_storerh_pci:
  case Hexagon::BI__builtin_HEXAGON_S2_storerf_pci:
  case Hexagon::BI__builtin_HEXAGON_S2_storeri_pci:
  case Hexagon::BI__builtin_HEXAGON_S2_storerd_pci:
  case Hexagon::BI__builtin_HEXAGON_S2_storerb_pcr:
  case Hexagon::BI__builtin_HEXAGON_S2_storerh_pcr:
  case Hexagon::BI__builtin_HEXAGON_S2_storerf_pcr:
  case Hexagon::BI__builtin_HEXAGON_S2_storeri_pcr:
  case Hexagon::BI__builtin_HEXAGON_S2_storerd_pcr:
    return MakeCircOp(ID, /*IsLoad=*/false);
  case Hexagon::BI__builtin_brev_ldub:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadrub_pbr, Int8Ty);
  case Hexagon::BI__builtin_brev_ldb:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadrb_pbr, Int8Ty);
  case Hexagon::BI__builtin_brev_lduh:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadruh_pbr, Int16Ty);
  case Hexagon::BI__builtin_brev_ldh:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadrh_pbr, Int16Ty);
  case Hexagon::BI__builtin_brev_ldw:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadri_pbr, Int32Ty);
  case Hexagon::BI__builtin_brev_ldd:
    return MakeBrevLd(Intrinsic::hexagon_L2_loadrd_pbr, Int64Ty);
  } // switch

  return nullptr;
}

Value *CodeGenFunction::EmitRISCVBuiltinExpr(unsigned BuiltinID,
                                             const CallExpr *E,
                                             ReturnValueSlot ReturnValue) {
  SmallVector<Value *, 4> Ops;
  llvm::Type *ResultType = ConvertType(E->getType());

  // Find out if any arguments are required to be integer constant expressions.
  unsigned ICEArguments = 0;
  ASTContext::GetBuiltinTypeError Error;
  getContext().GetBuiltinType(BuiltinID, Error, &ICEArguments);
  if (Error == ASTContext::GE_Missing_type) {
    // Vector intrinsics don't have a type string.
    assert(BuiltinID >= clang::RISCV::FirstRVVBuiltin &&
           BuiltinID <= clang::RISCV::LastRVVBuiltin);
    ICEArguments = 0;
    if (BuiltinID == RISCVVector::BI__builtin_rvv_vget_v ||
        BuiltinID == RISCVVector::BI__builtin_rvv_vset_v)
      ICEArguments = 1 << 1;
  } else {
    assert(Error == ASTContext::GE_None && "Unexpected error");
  }

  if (BuiltinID == RISCV::BI__builtin_riscv_ntl_load)
    ICEArguments |= (1 << 1);
  if (BuiltinID == RISCV::BI__builtin_riscv_ntl_store)
    ICEArguments |= (1 << 2);

  for (unsigned i = 0, e = E->getNumArgs(); i != e; i++) {
    // Handle aggregate argument, namely RVV tuple types in segment load/store
    if (hasAggregateEvaluationKind(E->getArg(i)->getType())) {
      LValue L = EmitAggExprToLValue(E->getArg(i));
      llvm::Value *AggValue = Builder.CreateLoad(L.getAddress());
      Ops.push_back(AggValue);
      continue;
    }
    Ops.push_back(EmitScalarOrConstFoldImmArg(ICEArguments, i, E));
  }

  Intrinsic::ID ID = Intrinsic::not_intrinsic;
  unsigned NF = 1;
  // The 0th bit simulates the `vta` of RVV
  // The 1st bit simulates the `vma` of RVV
  constexpr unsigned RVV_VTA = 0x1;
  constexpr unsigned RVV_VMA = 0x2;
  int PolicyAttrs = 0;
  bool IsMasked = false;

  // Required for overloaded intrinsics.
  llvm::SmallVector<llvm::Type *, 2> IntrinsicTypes;
  switch (BuiltinID) {
  default: llvm_unreachable("unexpected builtin ID");
  case RISCV::BI__builtin_riscv_orc_b_32:
  case RISCV::BI__builtin_riscv_orc_b_64:
  case RISCV::BI__builtin_riscv_clz_32:
  case RISCV::BI__builtin_riscv_clz_64:
  case RISCV::BI__builtin_riscv_ctz_32:
  case RISCV::BI__builtin_riscv_ctz_64:
  case RISCV::BI__builtin_riscv_clmul_32:
  case RISCV::BI__builtin_riscv_clmul_64:
  case RISCV::BI__builtin_riscv_clmulh_32:
  case RISCV::BI__builtin_riscv_clmulh_64:
  case RISCV::BI__builtin_riscv_clmulr_32:
  case RISCV::BI__builtin_riscv_clmulr_64:
  case RISCV::BI__builtin_riscv_xperm4_32:
  case RISCV::BI__builtin_riscv_xperm4_64:
  case RISCV::BI__builtin_riscv_xperm8_32:
  case RISCV::BI__builtin_riscv_xperm8_64:
  case RISCV::BI__builtin_riscv_brev8_32:
  case RISCV::BI__builtin_riscv_brev8_64:
  case RISCV::BI__builtin_riscv_zip_32:
  case RISCV::BI__builtin_riscv_unzip_32: {
    switch (BuiltinID) {
    default: llvm_unreachable("unexpected builtin ID");
    // Zbb
    case RISCV::BI__builtin_riscv_orc_b_32:
    case RISCV::BI__builtin_riscv_orc_b_64:
      ID = Intrinsic::riscv_orc_b;
      break;
    case RISCV::BI__builtin_riscv_clz_32:
    case RISCV::BI__builtin_riscv_clz_64: {
      Function *F = CGM.getIntrinsic(Intrinsic::ctlz, Ops[0]->getType());
      Value *Result = Builder.CreateCall(F, {Ops[0], Builder.getInt1(false)});
      if (Result->getType() != ResultType)
        Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                       "cast");
      return Result;
    }
    case RISCV::BI__builtin_riscv_ctz_32:
    case RISCV::BI__builtin_riscv_ctz_64: {
      Function *F = CGM.getIntrinsic(Intrinsic::cttz, Ops[0]->getType());
      Value *Result = Builder.CreateCall(F, {Ops[0], Builder.getInt1(false)});
      if (Result->getType() != ResultType)
        Result = Builder.CreateIntCast(Result, ResultType, /*isSigned*/true,
                                       "cast");
      return Result;
    }

    // Zbc
    case RISCV::BI__builtin_riscv_clmul_32:
    case RISCV::BI__builtin_riscv_clmul_64:
      ID = Intrinsic::riscv_clmul;
      break;
    case RISCV::BI__builtin_riscv_clmulh_32:
    case RISCV::BI__builtin_riscv_clmulh_64:
      ID = Intrinsic::riscv_clmulh;
      break;
    case RISCV::BI__builtin_riscv_clmulr_32:
    case RISCV::BI__builtin_riscv_clmulr_64:
      ID = Intrinsic::riscv_clmulr;
      break;

    // Zbkx
    case RISCV::BI__builtin_riscv_xperm8_32:
    case RISCV::BI__builtin_riscv_xperm8_64:
      ID = Intrinsic::riscv_xperm8;
      break;
    case RISCV::BI__builtin_riscv_xperm4_32:
    case RISCV::BI__builtin_riscv_xperm4_64:
      ID = Intrinsic::riscv_xperm4;
      break;

    // Zbkb
    case RISCV::BI__builtin_riscv_brev8_32:
    case RISCV::BI__builtin_riscv_brev8_64:
      ID = Intrinsic::riscv_brev8;
      break;
    case RISCV::BI__builtin_riscv_zip_32:
      ID = Intrinsic::riscv_zip;
      break;
    case RISCV::BI__builtin_riscv_unzip_32:
      ID = Intrinsic::riscv_unzip;
      break;
    }

    IntrinsicTypes = {ResultType};
    break;
  }

  // Zk builtins

  // Zknh
  case RISCV::BI__builtin_riscv_sha256sig0:
    ID = Intrinsic::riscv_sha256sig0;
    break;
  case RISCV::BI__builtin_riscv_sha256sig1:
    ID = Intrinsic::riscv_sha256sig1;
    break;
  case RISCV::BI__builtin_riscv_sha256sum0:
    ID = Intrinsic::riscv_sha256sum0;
    break;
  case RISCV::BI__builtin_riscv_sha256sum1:
    ID = Intrinsic::riscv_sha256sum1;
    break;

  // Zksed
  case RISCV::BI__builtin_riscv_sm4ks:
    ID = Intrinsic::riscv_sm4ks;
    break;
  case RISCV::BI__builtin_riscv_sm4ed:
    ID = Intrinsic::riscv_sm4ed;
    break;

  // Zksh
  case RISCV::BI__builtin_riscv_sm3p0:
    ID = Intrinsic::riscv_sm3p0;
    break;
  case RISCV::BI__builtin_riscv_sm3p1:
    ID = Intrinsic::riscv_sm3p1;
    break;

  // Zihintntl
  case RISCV::BI__builtin_riscv_ntl_load: {
    llvm::Type *ResTy = ConvertType(E->getType());
    unsigned DomainVal = 5; // Default __RISCV_NTLH_ALL
    if (Ops.size() == 2)
      DomainVal = cast<ConstantInt>(Ops[1])->getZExtValue();

    llvm::MDNode *RISCVDomainNode = llvm::MDNode::get(
        getLLVMContext(),
        llvm::ConstantAsMetadata::get(Builder.getInt32(DomainVal)));
    llvm::MDNode *NontemporalNode = llvm::MDNode::get(
        getLLVMContext(), llvm::ConstantAsMetadata::get(Builder.getInt32(1)));

    int Width;
    if(ResTy->isScalableTy()) {
      const ScalableVectorType *SVTy = cast<ScalableVectorType>(ResTy);
      llvm::Type *ScalarTy = ResTy->getScalarType();
      Width = ScalarTy->getPrimitiveSizeInBits() *
              SVTy->getElementCount().getKnownMinValue();
    } else
      Width = ResTy->getPrimitiveSizeInBits();
    LoadInst *Load = Builder.CreateLoad(
        Address(Ops[0], ResTy, CharUnits::fromQuantity(Width / 8)));

    Load->setMetadata(llvm::LLVMContext::MD_nontemporal, NontemporalNode);
    Load->setMetadata(CGM.getModule().getMDKindID("riscv-nontemporal-domain"),
                      RISCVDomainNode);

    return Load;
  }
  case RISCV::BI__builtin_riscv_ntl_store: {
    unsigned DomainVal = 5; // Default __RISCV_NTLH_ALL
    if (Ops.size() == 3)
      DomainVal = cast<ConstantInt>(Ops[2])->getZExtValue();

    llvm::MDNode *RISCVDomainNode = llvm::MDNode::get(
        getLLVMContext(),
        llvm::ConstantAsMetadata::get(Builder.getInt32(DomainVal)));
    llvm::MDNode *NontemporalNode = llvm::MDNode::get(
        getLLVMContext(), llvm::ConstantAsMetadata::get(Builder.getInt32(1)));

    StoreInst *Store = Builder.CreateDefaultAlignedStore(Ops[1], Ops[0]);
    Store->setMetadata(llvm::LLVMContext::MD_nontemporal, NontemporalNode);
    Store->setMetadata(CGM.getModule().getMDKindID("riscv-nontemporal-domain"),
                       RISCVDomainNode);

    return Store;
  }

  // Vector builtins are handled from here.
#include "clang/Basic/riscv_vector_builtin_cg.inc"
  // SiFive Vector builtins are handled from here.
#include "clang/Basic/riscv_sifive_vector_builtin_cg.inc"
  }

  assert(ID != Intrinsic::not_intrinsic);

  llvm::Function *F = CGM.getIntrinsic(ID, IntrinsicTypes);
  return Builder.CreateCall(F, Ops, "");
}
