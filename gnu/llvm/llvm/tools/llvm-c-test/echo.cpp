//===-- echo.cpp - tool for testing libLLVM and llvm-c API ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the --echo command in llvm-c-test.
//
// This command uses the C API to read a module and output an exact copy of it
// as output. It is used to check that the resulting module matches the input
// to validate that the C API can read and write modules properly.
//
//===----------------------------------------------------------------------===//

#include "llvm-c-test.h"
#include "llvm-c/DebugInfo.h"
#include "llvm-c/ErrorHandling.h"
#include "llvm-c/Target.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"

#include <stdio.h>
#include <stdlib.h>

using namespace llvm;

// Provide DenseMapInfo for C API opaque types.
template<typename T>
struct CAPIDenseMap {};

// The default DenseMapInfo require to know about pointer alignment.
// Because the C API uses opaque pointer types, their alignment is unknown.
// As a result, we need to roll out our own implementation.
template<typename T>
struct CAPIDenseMap<T*> {
  struct CAPIDenseMapInfo {
    static inline T* getEmptyKey() {
      uintptr_t Val = static_cast<uintptr_t>(-1);
      return reinterpret_cast<T*>(Val);
    }
    static inline T* getTombstoneKey() {
      uintptr_t Val = static_cast<uintptr_t>(-2);
      return reinterpret_cast<T*>(Val);
    }
    static unsigned getHashValue(const T *PtrVal) {
      return hash_value(PtrVal);
    }
    static bool isEqual(const T *LHS, const T *RHS) { return LHS == RHS; }
  };

  typedef DenseMap<T*, T*, CAPIDenseMapInfo> Map;
};

typedef CAPIDenseMap<LLVMValueRef>::Map ValueMap;
typedef CAPIDenseMap<LLVMBasicBlockRef>::Map BasicBlockMap;

struct TypeCloner {
  LLVMModuleRef M;
  LLVMContextRef Ctx;

  TypeCloner(LLVMModuleRef M): M(M), Ctx(LLVMGetModuleContext(M)) {}

  LLVMTypeRef Clone(LLVMValueRef Src) {
    return Clone(LLVMTypeOf(Src));
  }

  LLVMTypeRef Clone(LLVMTypeRef Src) {
    LLVMTypeKind Kind = LLVMGetTypeKind(Src);
    switch (Kind) {
      case LLVMVoidTypeKind:
        return LLVMVoidTypeInContext(Ctx);
      case LLVMHalfTypeKind:
        return LLVMHalfTypeInContext(Ctx);
      case LLVMBFloatTypeKind:
        return LLVMHalfTypeInContext(Ctx);
      case LLVMFloatTypeKind:
        return LLVMFloatTypeInContext(Ctx);
      case LLVMDoubleTypeKind:
        return LLVMDoubleTypeInContext(Ctx);
      case LLVMX86_FP80TypeKind:
        return LLVMX86FP80TypeInContext(Ctx);
      case LLVMFP128TypeKind:
        return LLVMFP128TypeInContext(Ctx);
      case LLVMPPC_FP128TypeKind:
        return LLVMPPCFP128TypeInContext(Ctx);
      case LLVMLabelTypeKind:
        return LLVMLabelTypeInContext(Ctx);
      case LLVMIntegerTypeKind:
        return LLVMIntTypeInContext(Ctx, LLVMGetIntTypeWidth(Src));
      case LLVMFunctionTypeKind: {
        unsigned ParamCount = LLVMCountParamTypes(Src);
        LLVMTypeRef* Params = nullptr;
        if (ParamCount > 0) {
          Params = static_cast<LLVMTypeRef*>(
              safe_malloc(ParamCount * sizeof(LLVMTypeRef)));
          LLVMGetParamTypes(Src, Params);
          for (unsigned i = 0; i < ParamCount; i++)
            Params[i] = Clone(Params[i]);
        }

        LLVMTypeRef FunTy = LLVMFunctionType(Clone(LLVMGetReturnType(Src)),
                                             Params, ParamCount,
                                             LLVMIsFunctionVarArg(Src));
        if (ParamCount > 0)
          free(Params);
        return FunTy;
      }
      case LLVMStructTypeKind: {
        LLVMTypeRef S = nullptr;
        const char *Name = LLVMGetStructName(Src);
        if (Name) {
          S = LLVMGetTypeByName2(Ctx, Name);
          if (S)
            return S;
          S = LLVMStructCreateNamed(Ctx, Name);
          if (LLVMIsOpaqueStruct(Src))
            return S;
        }

        unsigned EltCount = LLVMCountStructElementTypes(Src);
        SmallVector<LLVMTypeRef, 8> Elts;
        for (unsigned i = 0; i < EltCount; i++)
          Elts.push_back(Clone(LLVMStructGetTypeAtIndex(Src, i)));
        if (Name)
          LLVMStructSetBody(S, Elts.data(), EltCount, LLVMIsPackedStruct(Src));
        else
          S = LLVMStructTypeInContext(Ctx, Elts.data(), EltCount,
                                      LLVMIsPackedStruct(Src));
        return S;
      }
      case LLVMArrayTypeKind:
        return LLVMArrayType2(Clone(LLVMGetElementType(Src)),
                              LLVMGetArrayLength2(Src));
      case LLVMPointerTypeKind:
        if (LLVMPointerTypeIsOpaque(Src))
          return LLVMPointerTypeInContext(Ctx, LLVMGetPointerAddressSpace(Src));
        else
          return LLVMPointerType(Clone(LLVMGetElementType(Src)),
                                 LLVMGetPointerAddressSpace(Src));
      case LLVMVectorTypeKind:
        return LLVMVectorType(
          Clone(LLVMGetElementType(Src)),
          LLVMGetVectorSize(Src)
        );
      case LLVMScalableVectorTypeKind:
        return LLVMScalableVectorType(Clone(LLVMGetElementType(Src)),
                                      LLVMGetVectorSize(Src));
      case LLVMMetadataTypeKind:
        return LLVMMetadataTypeInContext(Ctx);
      case LLVMX86_AMXTypeKind:
        return LLVMX86AMXTypeInContext(Ctx);
      case LLVMX86_MMXTypeKind:
        return LLVMX86MMXTypeInContext(Ctx);
      case LLVMTokenTypeKind:
        return LLVMTokenTypeInContext(Ctx);
      case LLVMTargetExtTypeKind: {
        const char *Name = LLVMGetTargetExtTypeName(Src);
        unsigned NumTypeParams = LLVMGetTargetExtTypeNumTypeParams(Src);
        unsigned NumIntParams = LLVMGetTargetExtTypeNumIntParams(Src);

        SmallVector<LLVMTypeRef, 4> TypeParams((size_t)NumTypeParams);
        SmallVector<unsigned, 4> IntParams((size_t)NumIntParams);

        for (unsigned i = 0; i < TypeParams.size(); i++)
          TypeParams[i] = Clone(LLVMGetTargetExtTypeTypeParam(Src, i));

        for (unsigned i = 0; i < IntParams.size(); i++)
          IntParams[i] = LLVMGetTargetExtTypeIntParam(Src, i);

        LLVMTypeRef TargetExtTy = LLVMTargetExtTypeInContext(
            Ctx, Name, TypeParams.data(), TypeParams.size(), IntParams.data(),
            IntParams.size());

        return TargetExtTy;
      }
    }

    fprintf(stderr, "%d is not a supported typekind\n", Kind);
    exit(-1);
  }
};

static ValueMap clone_params(LLVMValueRef Src, LLVMValueRef Dst) {
  unsigned Count = LLVMCountParams(Src);
  if (Count != LLVMCountParams(Dst))
    report_fatal_error("Parameter count mismatch");

  ValueMap VMap;
  if (Count == 0)
    return VMap;

  LLVMValueRef SrcFirst = LLVMGetFirstParam(Src);
  LLVMValueRef DstFirst = LLVMGetFirstParam(Dst);
  LLVMValueRef SrcLast = LLVMGetLastParam(Src);
  LLVMValueRef DstLast = LLVMGetLastParam(Dst);

  LLVMValueRef SrcCur = SrcFirst;
  LLVMValueRef DstCur = DstFirst;
  LLVMValueRef SrcNext = nullptr;
  LLVMValueRef DstNext = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(SrcCur, &NameLen);
    LLVMSetValueName2(DstCur, Name, NameLen);

    VMap[SrcCur] = DstCur;

    Count--;
    SrcNext = LLVMGetNextParam(SrcCur);
    DstNext = LLVMGetNextParam(DstCur);
    if (SrcNext == nullptr && DstNext == nullptr) {
      if (SrcCur != SrcLast)
        report_fatal_error("SrcLast param does not match End");
      if (DstCur != DstLast)
        report_fatal_error("DstLast param does not match End");
      break;
    }

    if (SrcNext == nullptr)
      report_fatal_error("SrcNext was unexpectedly null");
    if (DstNext == nullptr)
      report_fatal_error("DstNext was unexpectedly null");

    LLVMValueRef SrcPrev = LLVMGetPreviousParam(SrcNext);
    if (SrcPrev != SrcCur)
      report_fatal_error("SrcNext.Previous param is not Current");

    LLVMValueRef DstPrev = LLVMGetPreviousParam(DstNext);
    if (DstPrev != DstCur)
      report_fatal_error("DstNext.Previous param is not Current");

    SrcCur = SrcNext;
    DstCur = DstNext;
  }

  if (Count != 0)
    report_fatal_error("Parameter count does not match iteration");

  return VMap;
}

static void check_value_kind(LLVMValueRef V, LLVMValueKind K) {
  if (LLVMGetValueKind(V) != K)
    report_fatal_error("LLVMGetValueKind returned incorrect type");
}

static LLVMValueRef clone_constant_impl(LLVMValueRef Cst, LLVMModuleRef M);

static LLVMValueRef clone_constant(LLVMValueRef Cst, LLVMModuleRef M) {
  LLVMValueRef Ret = clone_constant_impl(Cst, M);
  check_value_kind(Ret, LLVMGetValueKind(Cst));
  return Ret;
}

static LLVMValueRef clone_constant_impl(LLVMValueRef Cst, LLVMModuleRef M) {
  if (!LLVMIsAConstant(Cst))
    report_fatal_error("Expected a constant");

  // Maybe it is a symbol
  if (LLVMIsAGlobalValue(Cst)) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cst, &NameLen);

    // Try function
    if (LLVMIsAFunction(Cst)) {
      check_value_kind(Cst, LLVMFunctionValueKind);

      LLVMValueRef Dst = nullptr;
      // Try an intrinsic
      unsigned ID = LLVMGetIntrinsicID(Cst);
      if (ID > 0 && !LLVMIntrinsicIsOverloaded(ID)) {
        Dst = LLVMGetIntrinsicDeclaration(M, ID, nullptr, 0);
      } else {
        // Try a normal function
        Dst = LLVMGetNamedFunction(M, Name);
      }

      if (Dst)
        return Dst;
      report_fatal_error("Could not find function");
    }

    // Try global variable
    if (LLVMIsAGlobalVariable(Cst)) {
      check_value_kind(Cst, LLVMGlobalVariableValueKind);
      LLVMValueRef Dst = LLVMGetNamedGlobal(M, Name);
      if (Dst)
        return Dst;
      report_fatal_error("Could not find variable");
    }

    // Try global alias
    if (LLVMIsAGlobalAlias(Cst)) {
      check_value_kind(Cst, LLVMGlobalAliasValueKind);
      LLVMValueRef Dst = LLVMGetNamedGlobalAlias(M, Name, NameLen);
      if (Dst)
        return Dst;
      report_fatal_error("Could not find alias");
    }

    fprintf(stderr, "Could not find @%s\n", Name);
    exit(-1);
  }

  // Try integer literal
  if (LLVMIsAConstantInt(Cst)) {
    check_value_kind(Cst, LLVMConstantIntValueKind);
    return LLVMConstInt(TypeCloner(M).Clone(Cst),
                        LLVMConstIntGetZExtValue(Cst), false);
  }

  // Try zeroinitializer
  if (LLVMIsAConstantAggregateZero(Cst)) {
    check_value_kind(Cst, LLVMConstantAggregateZeroValueKind);
    return LLVMConstNull(TypeCloner(M).Clone(Cst));
  }

  // Try constant array or constant data array
  if (LLVMIsAConstantArray(Cst) || LLVMIsAConstantDataArray(Cst)) {
    check_value_kind(Cst, LLVMIsAConstantArray(Cst)
                              ? LLVMConstantArrayValueKind
                              : LLVMConstantDataArrayValueKind);
    LLVMTypeRef Ty = TypeCloner(M).Clone(Cst);
    uint64_t EltCount = LLVMGetArrayLength2(Ty);
    SmallVector<LLVMValueRef, 8> Elts;
    for (uint64_t i = 0; i < EltCount; i++)
      Elts.push_back(clone_constant(LLVMGetAggregateElement(Cst, i), M));
    return LLVMConstArray(LLVMGetElementType(Ty), Elts.data(), EltCount);
  }

  // Try constant struct
  if (LLVMIsAConstantStruct(Cst)) {
    check_value_kind(Cst, LLVMConstantStructValueKind);
    LLVMTypeRef Ty = TypeCloner(M).Clone(Cst);
    unsigned EltCount = LLVMCountStructElementTypes(Ty);
    SmallVector<LLVMValueRef, 8> Elts;
    for (unsigned i = 0; i < EltCount; i++)
      Elts.push_back(clone_constant(LLVMGetOperand(Cst, i), M));
    if (LLVMGetStructName(Ty))
      return LLVMConstNamedStruct(Ty, Elts.data(), EltCount);
    return LLVMConstStructInContext(LLVMGetModuleContext(M), Elts.data(),
                                    EltCount, LLVMIsPackedStruct(Ty));
  }

  // Try ConstantPointerNull
  if (LLVMIsAConstantPointerNull(Cst)) {
    check_value_kind(Cst, LLVMConstantPointerNullValueKind);
    LLVMTypeRef Ty = TypeCloner(M).Clone(Cst);
    return LLVMConstNull(Ty);
  }

  // Try undef
  if (LLVMIsUndef(Cst)) {
    check_value_kind(Cst, LLVMUndefValueValueKind);
    return LLVMGetUndef(TypeCloner(M).Clone(Cst));
  }

  // Try poison
  if (LLVMIsPoison(Cst)) {
    check_value_kind(Cst, LLVMPoisonValueValueKind);
    return LLVMGetPoison(TypeCloner(M).Clone(Cst));
  }

  // Try null
  if (LLVMIsNull(Cst)) {
    check_value_kind(Cst, LLVMConstantTokenNoneValueKind);
    LLVMTypeRef Ty = TypeCloner(M).Clone(Cst);
    return LLVMConstNull(Ty);
  }

  // Try float literal
  if (LLVMIsAConstantFP(Cst)) {
    check_value_kind(Cst, LLVMConstantFPValueKind);
    report_fatal_error("ConstantFP is not supported");
  }

  // Try ConstantVector or ConstantDataVector
  if (LLVMIsAConstantVector(Cst) || LLVMIsAConstantDataVector(Cst)) {
    check_value_kind(Cst, LLVMIsAConstantVector(Cst)
                              ? LLVMConstantVectorValueKind
                              : LLVMConstantDataVectorValueKind);
    LLVMTypeRef Ty = TypeCloner(M).Clone(Cst);
    unsigned EltCount = LLVMGetVectorSize(Ty);
    SmallVector<LLVMValueRef, 8> Elts;
    for (unsigned i = 0; i < EltCount; i++)
      Elts.push_back(clone_constant(LLVMGetAggregateElement(Cst, i), M));
    return LLVMConstVector(Elts.data(), EltCount);
  }

  if (LLVMIsAConstantPtrAuth(Cst)) {
    LLVMValueRef Ptr = clone_constant(LLVMGetConstantPtrAuthPointer(Cst), M);
    LLVMValueRef Key = clone_constant(LLVMGetConstantPtrAuthKey(Cst), M);
    LLVMValueRef Disc =
        clone_constant(LLVMGetConstantPtrAuthDiscriminator(Cst), M);
    LLVMValueRef AddrDisc =
        clone_constant(LLVMGetConstantPtrAuthAddrDiscriminator(Cst), M);
    return LLVMConstantPtrAuth(Ptr, Key, Disc, AddrDisc);
  }

  // At this point, if it's not a constant expression, it's a kind of constant
  // which is not supported
  if (!LLVMIsAConstantExpr(Cst))
    report_fatal_error("Unsupported constant kind");

  // At this point, it must be a constant expression
  check_value_kind(Cst, LLVMConstantExprValueKind);

  LLVMOpcode Op = LLVMGetConstOpcode(Cst);
  switch(Op) {
    case LLVMBitCast:
      return LLVMConstBitCast(clone_constant(LLVMGetOperand(Cst, 0), M),
                              TypeCloner(M).Clone(Cst));
    case LLVMGetElementPtr: {
      LLVMTypeRef ElemTy =
          TypeCloner(M).Clone(LLVMGetGEPSourceElementType(Cst));
      LLVMValueRef Ptr = clone_constant(LLVMGetOperand(Cst, 0), M);
      int NumIdx = LLVMGetNumIndices(Cst);
      SmallVector<LLVMValueRef, 8> Idx;
      for (int i = 1; i <= NumIdx; i++)
        Idx.push_back(clone_constant(LLVMGetOperand(Cst, i), M));

      return LLVMConstGEPWithNoWrapFlags(ElemTy, Ptr, Idx.data(), NumIdx,
                                         LLVMGEPGetNoWrapFlags(Cst));
    }
    default:
      fprintf(stderr, "%d is not a supported opcode for constant expressions\n",
              Op);
      exit(-1);
  }
}

static LLVMValueRef clone_inline_asm(LLVMValueRef Asm, LLVMModuleRef M) {

  if (!LLVMIsAInlineAsm(Asm))
      report_fatal_error("Expected inline assembly");

  size_t AsmStringSize = 0;
  const char *AsmString = LLVMGetInlineAsmAsmString(Asm, &AsmStringSize);

  size_t ConstraintStringSize = 0;
  const char *ConstraintString =
      LLVMGetInlineAsmConstraintString(Asm, &ConstraintStringSize);

  LLVMInlineAsmDialect AsmDialect = LLVMGetInlineAsmDialect(Asm);

  LLVMTypeRef AsmFunctionType = LLVMGetInlineAsmFunctionType(Asm);

  LLVMBool HasSideEffects = LLVMGetInlineAsmHasSideEffects(Asm);
  LLVMBool NeedsAlignStack = LLVMGetInlineAsmNeedsAlignedStack(Asm);
  LLVMBool CanUnwind = LLVMGetInlineAsmCanUnwind(Asm);

  return LLVMGetInlineAsm(AsmFunctionType, AsmString, AsmStringSize,
                          ConstraintString, ConstraintStringSize,
                          HasSideEffects, NeedsAlignStack, AsmDialect,
                          CanUnwind);
}

struct FunCloner {
  LLVMValueRef Fun;
  LLVMModuleRef M;

  ValueMap VMap;
  BasicBlockMap BBMap;

  FunCloner(LLVMValueRef Src, LLVMValueRef Dst): Fun(Dst),
    M(LLVMGetGlobalParent(Fun)), VMap(clone_params(Src, Dst)) {}

  LLVMTypeRef CloneType(LLVMTypeRef Src) {
    return TypeCloner(M).Clone(Src);
  }

  LLVMTypeRef CloneType(LLVMValueRef Src) {
    return TypeCloner(M).Clone(Src);
  }

  // Try to clone everything in the llvm::Value hierarchy.
  LLVMValueRef CloneValue(LLVMValueRef Src) {
    // First, the value may be constant.
    if (LLVMIsAConstant(Src))
      return clone_constant(Src, M);

    // Function argument should always be in the map already.
    auto i = VMap.find(Src);
    if (i != VMap.end())
      return i->second;

    // Inline assembly is a Value, but not an Instruction
    if (LLVMIsAInlineAsm(Src))
      return clone_inline_asm(Src, M);

    if (!LLVMIsAInstruction(Src))
      report_fatal_error("Expected an instruction");

    auto Ctx = LLVMGetModuleContext(M);
    auto Builder = LLVMCreateBuilderInContext(Ctx);
    auto BB = DeclareBB(LLVMGetInstructionParent(Src));
    LLVMPositionBuilderAtEnd(Builder, BB);
    auto Dst = CloneInstruction(Src, Builder);
    LLVMDisposeBuilder(Builder);
    return Dst;
  }

  void CloneAttrs(LLVMValueRef Src, LLVMValueRef Dst) {
    auto Ctx = LLVMGetModuleContext(M);
    int ArgCount = LLVMGetNumArgOperands(Src);
    for (int i = LLVMAttributeReturnIndex; i <= ArgCount; i++) {
      for (unsigned k = 0, e = LLVMGetLastEnumAttributeKind(); k < e; ++k) {
        if (auto SrcA = LLVMGetCallSiteEnumAttribute(Src, i, k)) {
          auto Val = LLVMGetEnumAttributeValue(SrcA);
          auto A = LLVMCreateEnumAttribute(Ctx, k, Val);
          LLVMAddCallSiteAttribute(Dst, i, A);
        }
      }
    }
  }

  LLVMValueRef CloneInstruction(LLVMValueRef Src, LLVMBuilderRef Builder) {
    check_value_kind(Src, LLVMInstructionValueKind);
    if (!LLVMIsAInstruction(Src))
      report_fatal_error("Expected an instruction");

    size_t NameLen;
    const char *Name = LLVMGetValueName2(Src, &NameLen);

    // Check if this is something we already computed.
    {
      auto i = VMap.find(Src);
      if (i != VMap.end()) {
        // If we have a hit, it means we already generated the instruction
        // as a dependency to something else. We need to make sure
        // it is ordered properly.
        auto I = i->second;
        LLVMInstructionRemoveFromParent(I);
        LLVMInsertIntoBuilderWithName(Builder, I, Name);
        return I;
      }
    }

    // We tried everything, it must be an instruction
    // that hasn't been generated already.
    LLVMValueRef Dst = nullptr;

    LLVMOpcode Op = LLVMGetInstructionOpcode(Src);
    switch(Op) {
      case LLVMRet: {
        int OpCount = LLVMGetNumOperands(Src);
        if (OpCount == 0)
          Dst = LLVMBuildRetVoid(Builder);
        else
          Dst = LLVMBuildRet(Builder, CloneValue(LLVMGetOperand(Src, 0)));
        break;
      }
      case LLVMBr: {
        if (!LLVMIsConditional(Src)) {
          LLVMValueRef SrcOp = LLVMGetOperand(Src, 0);
          LLVMBasicBlockRef SrcBB = LLVMValueAsBasicBlock(SrcOp);
          Dst = LLVMBuildBr(Builder, DeclareBB(SrcBB));
          break;
        }

        LLVMValueRef Cond = LLVMGetCondition(Src);
        LLVMValueRef Else = LLVMGetOperand(Src, 1);
        LLVMBasicBlockRef ElseBB = DeclareBB(LLVMValueAsBasicBlock(Else));
        LLVMValueRef Then = LLVMGetOperand(Src, 2);
        LLVMBasicBlockRef ThenBB = DeclareBB(LLVMValueAsBasicBlock(Then));
        Dst = LLVMBuildCondBr(Builder, CloneValue(Cond), ThenBB, ElseBB);
        break;
      }
      case LLVMSwitch:
      case LLVMIndirectBr:
        break;
      case LLVMInvoke: {
        SmallVector<LLVMValueRef, 8> Args;
        SmallVector<LLVMOperandBundleRef, 8> Bundles;
        unsigned ArgCount = LLVMGetNumArgOperands(Src);
        for (unsigned i = 0; i < ArgCount; ++i)
          Args.push_back(CloneValue(LLVMGetOperand(Src, i)));
        unsigned BundleCount = LLVMGetNumOperandBundles(Src);
        for (unsigned i = 0; i < BundleCount; ++i) {
          auto Bundle = LLVMGetOperandBundleAtIndex(Src, i);
          Bundles.push_back(CloneOB(Bundle));
          LLVMDisposeOperandBundle(Bundle);
        }
        LLVMTypeRef FnTy = CloneType(LLVMGetCalledFunctionType(Src));
        LLVMValueRef Fn = CloneValue(LLVMGetCalledValue(Src));
        LLVMBasicBlockRef Then = DeclareBB(LLVMGetNormalDest(Src));
        LLVMBasicBlockRef Unwind = DeclareBB(LLVMGetUnwindDest(Src));
        Dst = LLVMBuildInvokeWithOperandBundles(
            Builder, FnTy, Fn, Args.data(), ArgCount, Then, Unwind,
            Bundles.data(), Bundles.size(), Name);
        CloneAttrs(Src, Dst);
        for (auto Bundle : Bundles)
          LLVMDisposeOperandBundle(Bundle);
        break;
      }
      case LLVMCallBr: {
        LLVMTypeRef FnTy = CloneType(LLVMGetCalledFunctionType(Src));
        LLVMValueRef Fn = CloneValue(LLVMGetCalledValue(Src));

        LLVMBasicBlockRef DefaultDest =
            DeclareBB(LLVMGetCallBrDefaultDest(Src));

        // Clone indirect destinations
        SmallVector<LLVMBasicBlockRef, 8> IndirectDests;
        unsigned IndirectDestCount = LLVMGetCallBrNumIndirectDests(Src);
        for (unsigned i = 0; i < IndirectDestCount; ++i)
          IndirectDests.push_back(DeclareBB(LLVMGetCallBrIndirectDest(Src, i)));

        // Clone input arguments
        SmallVector<LLVMValueRef, 8> Args;
        unsigned ArgCount = LLVMGetNumArgOperands(Src);
        for (unsigned i = 0; i < ArgCount; ++i)
          Args.push_back(CloneValue(LLVMGetOperand(Src, i)));

        // Clone operand bundles
        SmallVector<LLVMOperandBundleRef, 8> Bundles;
        unsigned BundleCount = LLVMGetNumOperandBundles(Src);
        for (unsigned i = 0; i < BundleCount; ++i) {
          auto Bundle = LLVMGetOperandBundleAtIndex(Src, i);
          Bundles.push_back(CloneOB(Bundle));
          LLVMDisposeOperandBundle(Bundle);
        }

        Dst = LLVMBuildCallBr(Builder, FnTy, Fn, DefaultDest,
                              IndirectDests.data(), IndirectDests.size(),
                              Args.data(), Args.size(), Bundles.data(),
                              Bundles.size(), Name);

        CloneAttrs(Src, Dst);

        for (auto Bundle : Bundles)
          LLVMDisposeOperandBundle(Bundle);

        break;
      }
      case LLVMUnreachable:
        Dst = LLVMBuildUnreachable(Builder);
        break;
      case LLVMAdd: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool NUW = LLVMGetNUW(Src);
        LLVMBool NSW = LLVMGetNSW(Src);
        Dst = LLVMBuildAdd(Builder, LHS, RHS, Name);
        LLVMSetNUW(Dst, NUW);
        LLVMSetNSW(Dst, NSW);
        break;
      }
      case LLVMSub: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool NUW = LLVMGetNUW(Src);
        LLVMBool NSW = LLVMGetNSW(Src);
        Dst = LLVMBuildSub(Builder, LHS, RHS, Name);
        LLVMSetNUW(Dst, NUW);
        LLVMSetNSW(Dst, NSW);
        break;
      }
      case LLVMMul: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool NUW = LLVMGetNUW(Src);
        LLVMBool NSW = LLVMGetNSW(Src);
        Dst = LLVMBuildMul(Builder, LHS, RHS, Name);
        LLVMSetNUW(Dst, NUW);
        LLVMSetNSW(Dst, NSW);
        break;
      }
      case LLVMUDiv: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool IsExact = LLVMGetExact(Src);
        Dst = LLVMBuildUDiv(Builder, LHS, RHS, Name);
        LLVMSetExact(Dst, IsExact);
        break;
      }
      case LLVMSDiv: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool IsExact = LLVMGetExact(Src);
        Dst = LLVMBuildSDiv(Builder, LHS, RHS, Name);
        LLVMSetExact(Dst, IsExact);
        break;
      }
      case LLVMURem: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildURem(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMSRem: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildSRem(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMShl: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool NUW = LLVMGetNUW(Src);
        LLVMBool NSW = LLVMGetNSW(Src);
        Dst = LLVMBuildShl(Builder, LHS, RHS, Name);
        LLVMSetNUW(Dst, NUW);
        LLVMSetNSW(Dst, NSW);
        break;
      }
      case LLVMLShr: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool IsExact = LLVMGetExact(Src);
        Dst = LLVMBuildLShr(Builder, LHS, RHS, Name);
        LLVMSetExact(Dst, IsExact);
        break;
      }
      case LLVMAShr: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool IsExact = LLVMGetExact(Src);
        Dst = LLVMBuildAShr(Builder, LHS, RHS, Name);
        LLVMSetExact(Dst, IsExact);
        break;
      }
      case LLVMAnd: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildAnd(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMOr: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        LLVMBool IsDisjoint = LLVMGetIsDisjoint(Src);
        Dst = LLVMBuildOr(Builder, LHS, RHS, Name);
        LLVMSetIsDisjoint(Dst, IsDisjoint);
        break;
      }
      case LLVMXor: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildXor(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMAlloca: {
        LLVMTypeRef Ty = CloneType(LLVMGetAllocatedType(Src));
        Dst = LLVMBuildAlloca(Builder, Ty, Name);
        LLVMSetAlignment(Dst, LLVMGetAlignment(Src));
        break;
      }
      case LLVMLoad: {
        LLVMValueRef Ptr = CloneValue(LLVMGetOperand(Src, 0));
        Dst = LLVMBuildLoad2(Builder, CloneType(Src), Ptr, Name);
        LLVMSetAlignment(Dst, LLVMGetAlignment(Src));
        LLVMSetOrdering(Dst, LLVMGetOrdering(Src));
        LLVMSetVolatile(Dst, LLVMGetVolatile(Src));
        LLVMSetAtomicSingleThread(Dst, LLVMIsAtomicSingleThread(Src));
        break;
      }
      case LLVMStore: {
        LLVMValueRef Val = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef Ptr = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildStore(Builder, Val, Ptr);
        LLVMSetAlignment(Dst, LLVMGetAlignment(Src));
        LLVMSetOrdering(Dst, LLVMGetOrdering(Src));
        LLVMSetVolatile(Dst, LLVMGetVolatile(Src));
        LLVMSetAtomicSingleThread(Dst, LLVMIsAtomicSingleThread(Src));
        break;
      }
      case LLVMGetElementPtr: {
        LLVMTypeRef ElemTy = CloneType(LLVMGetGEPSourceElementType(Src));
        LLVMValueRef Ptr = CloneValue(LLVMGetOperand(Src, 0));
        SmallVector<LLVMValueRef, 8> Idx;
        int NumIdx = LLVMGetNumIndices(Src);
        for (int i = 1; i <= NumIdx; i++)
          Idx.push_back(CloneValue(LLVMGetOperand(Src, i)));

        Dst = LLVMBuildGEPWithNoWrapFlags(Builder, ElemTy, Ptr, Idx.data(),
                                          NumIdx, Name,
                                          LLVMGEPGetNoWrapFlags(Src));
        break;
      }
      case LLVMAtomicRMW: {
        LLVMValueRef Ptr = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef Val = CloneValue(LLVMGetOperand(Src, 1));
        LLVMAtomicRMWBinOp BinOp = LLVMGetAtomicRMWBinOp(Src);
        LLVMAtomicOrdering Ord = LLVMGetOrdering(Src);
        LLVMBool SingleThread = LLVMIsAtomicSingleThread(Src);
        Dst = LLVMBuildAtomicRMW(Builder, BinOp, Ptr, Val, Ord, SingleThread);
        LLVMSetAlignment(Dst, LLVMGetAlignment(Src));
        LLVMSetVolatile(Dst, LLVMGetVolatile(Src));
        LLVMSetValueName2(Dst, Name, NameLen);
        break;
      }
      case LLVMAtomicCmpXchg: {
        LLVMValueRef Ptr = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef Cmp = CloneValue(LLVMGetOperand(Src, 1));
        LLVMValueRef New = CloneValue(LLVMGetOperand(Src, 2));
        LLVMAtomicOrdering Succ = LLVMGetCmpXchgSuccessOrdering(Src);
        LLVMAtomicOrdering Fail = LLVMGetCmpXchgFailureOrdering(Src);
        LLVMBool SingleThread = LLVMIsAtomicSingleThread(Src);

        Dst = LLVMBuildAtomicCmpXchg(Builder, Ptr, Cmp, New, Succ, Fail,
                                     SingleThread);
        LLVMSetAlignment(Dst, LLVMGetAlignment(Src));
        LLVMSetVolatile(Dst, LLVMGetVolatile(Src));
        LLVMSetWeak(Dst, LLVMGetWeak(Src));
        LLVMSetValueName2(Dst, Name, NameLen);
        break;
      }
      case LLVMBitCast: {
        LLVMValueRef V = CloneValue(LLVMGetOperand(Src, 0));
        Dst = LLVMBuildBitCast(Builder, V, CloneType(Src), Name);
        break;
      }
      case LLVMICmp: {
        LLVMIntPredicate Pred = LLVMGetICmpPredicate(Src);
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildICmp(Builder, Pred, LHS, RHS, Name);
        break;
      }
      case LLVMPHI: {
        // We need to aggressively set things here because of loops.
        VMap[Src] = Dst = LLVMBuildPhi(Builder, CloneType(Src), Name);

        SmallVector<LLVMValueRef, 8> Values;
        SmallVector<LLVMBasicBlockRef, 8> Blocks;

        unsigned IncomingCount = LLVMCountIncoming(Src);
        for (unsigned i = 0; i < IncomingCount; ++i) {
          Blocks.push_back(DeclareBB(LLVMGetIncomingBlock(Src, i)));
          Values.push_back(CloneValue(LLVMGetIncomingValue(Src, i)));
        }

        LLVMAddIncoming(Dst, Values.data(), Blocks.data(), IncomingCount);
        // Copy fast math flags here since we return early
        if (LLVMCanValueUseFastMathFlags(Src))
          LLVMSetFastMathFlags(Dst, LLVMGetFastMathFlags(Src));
        return Dst;
      }
      case LLVMSelect: {
        LLVMValueRef If = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef Then = CloneValue(LLVMGetOperand(Src, 1));
        LLVMValueRef Else = CloneValue(LLVMGetOperand(Src, 2));
        Dst = LLVMBuildSelect(Builder, If, Then, Else, Name);
        break;
      }
      case LLVMCall: {
        SmallVector<LLVMValueRef, 8> Args;
        SmallVector<LLVMOperandBundleRef, 8> Bundles;
        unsigned ArgCount = LLVMGetNumArgOperands(Src);
        for (unsigned i = 0; i < ArgCount; ++i)
          Args.push_back(CloneValue(LLVMGetOperand(Src, i)));
        unsigned BundleCount = LLVMGetNumOperandBundles(Src);
        for (unsigned i = 0; i < BundleCount; ++i) {
          auto Bundle = LLVMGetOperandBundleAtIndex(Src, i);
          Bundles.push_back(CloneOB(Bundle));
          LLVMDisposeOperandBundle(Bundle);
        }
        LLVMTypeRef FnTy = CloneType(LLVMGetCalledFunctionType(Src));
        LLVMValueRef Fn = CloneValue(LLVMGetCalledValue(Src));
        Dst = LLVMBuildCallWithOperandBundles(Builder, FnTy, Fn, Args.data(),
                                              ArgCount, Bundles.data(),
                                              Bundles.size(), Name);
        LLVMSetTailCallKind(Dst, LLVMGetTailCallKind(Src));
        CloneAttrs(Src, Dst);
        for (auto Bundle : Bundles)
          LLVMDisposeOperandBundle(Bundle);
        break;
      }
      case LLVMResume: {
        Dst = LLVMBuildResume(Builder, CloneValue(LLVMGetOperand(Src, 0)));
        break;
      }
      case LLVMLandingPad: {
        // The landing pad API is a bit screwed up for historical reasons.
        Dst = LLVMBuildLandingPad(Builder, CloneType(Src), nullptr, 0, Name);
        unsigned NumClauses = LLVMGetNumClauses(Src);
        for (unsigned i = 0; i < NumClauses; ++i)
          LLVMAddClause(Dst, CloneValue(LLVMGetClause(Src, i)));
        LLVMSetCleanup(Dst, LLVMIsCleanup(Src));
        break;
      }
      case LLVMCleanupRet: {
        LLVMValueRef CatchPad = CloneValue(LLVMGetOperand(Src, 0));
        LLVMBasicBlockRef Unwind = nullptr;
        if (LLVMBasicBlockRef UDest = LLVMGetUnwindDest(Src))
          Unwind = DeclareBB(UDest);
        Dst = LLVMBuildCleanupRet(Builder, CatchPad, Unwind);
        break;
      }
      case LLVMCatchRet: {
        LLVMValueRef CatchPad = CloneValue(LLVMGetOperand(Src, 0));
        LLVMBasicBlockRef SuccBB = DeclareBB(LLVMGetSuccessor(Src, 0));
        Dst = LLVMBuildCatchRet(Builder, CatchPad, SuccBB);
        break;
      }
      case LLVMCatchPad: {
        LLVMValueRef ParentPad = CloneValue(LLVMGetParentCatchSwitch(Src));
        SmallVector<LLVMValueRef, 8> Args;
        int ArgCount = LLVMGetNumArgOperands(Src);
        for (int i = 0; i < ArgCount; i++)
          Args.push_back(CloneValue(LLVMGetOperand(Src, i)));
        Dst = LLVMBuildCatchPad(Builder, ParentPad,
                                Args.data(), ArgCount, Name);
        break;
      }
      case LLVMCleanupPad: {
        LLVMValueRef ParentPad = CloneValue(LLVMGetOperand(Src, 0));
        SmallVector<LLVMValueRef, 8> Args;
        int ArgCount = LLVMGetNumArgOperands(Src);
        for (int i = 0; i < ArgCount; i++)
          Args.push_back(CloneValue(LLVMGetArgOperand(Src, i)));
        Dst = LLVMBuildCleanupPad(Builder, ParentPad,
                                  Args.data(), ArgCount, Name);
        break;
      }
      case LLVMCatchSwitch: {
        LLVMValueRef ParentPad = CloneValue(LLVMGetOperand(Src, 0));
        LLVMBasicBlockRef UnwindBB = nullptr;
        if (LLVMBasicBlockRef UDest = LLVMGetUnwindDest(Src)) {
          UnwindBB = DeclareBB(UDest);
        }
        unsigned NumHandlers = LLVMGetNumHandlers(Src);
        Dst = LLVMBuildCatchSwitch(Builder, ParentPad, UnwindBB, NumHandlers, Name);
        if (NumHandlers > 0) {
          LLVMBasicBlockRef *Handlers = static_cast<LLVMBasicBlockRef*>(
                       safe_malloc(NumHandlers * sizeof(LLVMBasicBlockRef)));
          LLVMGetHandlers(Src, Handlers);
          for (unsigned i = 0; i < NumHandlers; i++)
            LLVMAddHandler(Dst, DeclareBB(Handlers[i]));
          free(Handlers);
        }
        break;
      }
      case LLVMExtractValue: {
        LLVMValueRef Agg = CloneValue(LLVMGetOperand(Src, 0));
        if (LLVMGetNumIndices(Src) > 1)
          report_fatal_error("ExtractValue: Expected only one index");
        else if (LLVMGetNumIndices(Src) < 1)
          report_fatal_error("ExtractValue: Expected an index");
        auto I = LLVMGetIndices(Src)[0];
        Dst = LLVMBuildExtractValue(Builder, Agg, I, Name);
        break;
      }
      case LLVMInsertValue: {
        LLVMValueRef Agg = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef V = CloneValue(LLVMGetOperand(Src, 1));
        if (LLVMGetNumIndices(Src) > 1)
          report_fatal_error("InsertValue: Expected only one index");
        else if (LLVMGetNumIndices(Src) < 1)
          report_fatal_error("InsertValue: Expected an index");
        auto I = LLVMGetIndices(Src)[0];
        Dst = LLVMBuildInsertValue(Builder, Agg, V, I, Name);
        break;
      }
      case LLVMExtractElement: {
        LLVMValueRef Agg = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef Index = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildExtractElement(Builder, Agg, Index, Name);
        break;
      }
      case LLVMInsertElement: {
        LLVMValueRef Agg = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef V = CloneValue(LLVMGetOperand(Src, 1));
        LLVMValueRef Index = CloneValue(LLVMGetOperand(Src, 2));
        Dst = LLVMBuildInsertElement(Builder, Agg, V, Index, Name);
        break;
      }
      case LLVMShuffleVector: {
        LLVMValueRef Agg0 = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef Agg1 = CloneValue(LLVMGetOperand(Src, 1));
        SmallVector<LLVMValueRef, 8> MaskElts;
        unsigned NumMaskElts = LLVMGetNumMaskElements(Src);
        for (unsigned i = 0; i < NumMaskElts; i++) {
          int Val = LLVMGetMaskValue(Src, i);
          if (Val == LLVMGetUndefMaskElem()) {
            MaskElts.push_back(LLVMGetUndef(LLVMInt64Type()));
          } else {
            MaskElts.push_back(LLVMConstInt(LLVMInt64Type(), Val, true));
          }
        }
        LLVMValueRef Mask = LLVMConstVector(MaskElts.data(), NumMaskElts);
        Dst = LLVMBuildShuffleVector(Builder, Agg0, Agg1, Mask, Name);
        break;
      }
      case LLVMFreeze: {
        LLVMValueRef Arg = CloneValue(LLVMGetOperand(Src, 0));
        Dst = LLVMBuildFreeze(Builder, Arg, Name);
        break;
      }
      case LLVMFence: {
        LLVMAtomicOrdering Ordering = LLVMGetOrdering(Src);
        LLVMBool IsSingleThreaded = LLVMIsAtomicSingleThread(Src);
        Dst = LLVMBuildFence(Builder, Ordering, IsSingleThreaded, Name);
        break;
      }
      case LLVMZExt: {
        LLVMValueRef Val = CloneValue(LLVMGetOperand(Src, 0));
        LLVMTypeRef DestTy = CloneType(LLVMTypeOf(Src));
        LLVMBool NNeg = LLVMGetNNeg(Src);
        Dst = LLVMBuildZExt(Builder, Val, DestTy, Name);
        LLVMSetNNeg(Dst, NNeg);
        break;
      }
      case LLVMFAdd: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildFAdd(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMFSub: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildFSub(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMFMul: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildFMul(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMFDiv: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildFDiv(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMFRem: {
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildFRem(Builder, LHS, RHS, Name);
        break;
      }
      case LLVMFNeg: {
        LLVMValueRef Val = CloneValue(LLVMGetOperand(Src, 0));
        Dst = LLVMBuildFNeg(Builder, Val, Name);
        break;
      }
      case LLVMFCmp: {
        LLVMRealPredicate Pred = LLVMGetFCmpPredicate(Src);
        LLVMValueRef LHS = CloneValue(LLVMGetOperand(Src, 0));
        LLVMValueRef RHS = CloneValue(LLVMGetOperand(Src, 1));
        Dst = LLVMBuildFCmp(Builder, Pred, LHS, RHS, Name);
        break;
      }
      default:
        break;
    }

    if (Dst == nullptr) {
      fprintf(stderr, "%d is not a supported opcode\n", Op);
      exit(-1);
    }

    // Copy fast-math flags on instructions that support them
    if (LLVMCanValueUseFastMathFlags(Src))
      LLVMSetFastMathFlags(Dst, LLVMGetFastMathFlags(Src));

    auto Ctx = LLVMGetModuleContext(M);
    size_t NumMetadataEntries;
    auto *AllMetadata =
        LLVMInstructionGetAllMetadataOtherThanDebugLoc(Src,
                                                       &NumMetadataEntries);
    for (unsigned i = 0; i < NumMetadataEntries; ++i) {
      unsigned Kind = LLVMValueMetadataEntriesGetKind(AllMetadata, i);
      LLVMMetadataRef MD = LLVMValueMetadataEntriesGetMetadata(AllMetadata, i);
      LLVMSetMetadata(Dst, Kind, LLVMMetadataAsValue(Ctx, MD));
    }
    LLVMDisposeValueMetadataEntries(AllMetadata);
    LLVMAddMetadataToInst(Builder, Dst);

    check_value_kind(Dst, LLVMInstructionValueKind);
    return VMap[Src] = Dst;
  }

  LLVMOperandBundleRef CloneOB(LLVMOperandBundleRef Src) {
    size_t TagLen;
    const char *Tag = LLVMGetOperandBundleTag(Src, &TagLen);

    SmallVector<LLVMValueRef, 8> Args;
    for (unsigned i = 0, n = LLVMGetNumOperandBundleArgs(Src); i != n; ++i)
      Args.push_back(CloneValue(LLVMGetOperandBundleArgAtIndex(Src, i)));

    return LLVMCreateOperandBundle(Tag, TagLen, Args.data(), Args.size());
  }

  LLVMBasicBlockRef DeclareBB(LLVMBasicBlockRef Src) {
    // Check if this is something we already computed.
    {
      auto i = BBMap.find(Src);
      if (i != BBMap.end()) {
        return i->second;
      }
    }

    LLVMValueRef V = LLVMBasicBlockAsValue(Src);
    if (!LLVMValueIsBasicBlock(V) || LLVMValueAsBasicBlock(V) != Src)
      report_fatal_error("Basic block is not a basic block");

    const char *Name = LLVMGetBasicBlockName(Src);
    size_t NameLen;
    const char *VName = LLVMGetValueName2(V, &NameLen);
    if (Name != VName)
      report_fatal_error("Basic block name mismatch");

    LLVMBasicBlockRef BB = LLVMAppendBasicBlock(Fun, Name);
    return BBMap[Src] = BB;
  }

  LLVMBasicBlockRef CloneBB(LLVMBasicBlockRef Src) {
    LLVMBasicBlockRef BB = DeclareBB(Src);

    // Make sure ordering is correct.
    LLVMBasicBlockRef Prev = LLVMGetPreviousBasicBlock(Src);
    if (Prev)
      LLVMMoveBasicBlockAfter(BB, DeclareBB(Prev));

    LLVMValueRef First = LLVMGetFirstInstruction(Src);
    LLVMValueRef Last = LLVMGetLastInstruction(Src);

    if (First == nullptr) {
      if (Last != nullptr)
        report_fatal_error("Has no first instruction, but last one");
      return BB;
    }

    auto Ctx = LLVMGetModuleContext(M);
    LLVMBuilderRef Builder = LLVMCreateBuilderInContext(Ctx);
    LLVMPositionBuilderAtEnd(Builder, BB);

    LLVMValueRef Cur = First;
    LLVMValueRef Next = nullptr;
    while(true) {
      CloneInstruction(Cur, Builder);
      Next = LLVMGetNextInstruction(Cur);
      if (Next == nullptr) {
        if (Cur != Last)
          report_fatal_error("Final instruction does not match Last");
        break;
      }

      LLVMValueRef Prev = LLVMGetPreviousInstruction(Next);
      if (Prev != Cur)
        report_fatal_error("Next.Previous instruction is not Current");

      Cur = Next;
    }

    LLVMDisposeBuilder(Builder);
    return BB;
  }

  void CloneBBs(LLVMValueRef Src) {
    unsigned Count = LLVMCountBasicBlocks(Src);
    if (Count == 0)
      return;

    LLVMBasicBlockRef First = LLVMGetFirstBasicBlock(Src);
    LLVMBasicBlockRef Last = LLVMGetLastBasicBlock(Src);

    LLVMBasicBlockRef Cur = First;
    LLVMBasicBlockRef Next = nullptr;
    while(true) {
      CloneBB(Cur);
      Count--;
      Next = LLVMGetNextBasicBlock(Cur);
      if (Next == nullptr) {
        if (Cur != Last)
          report_fatal_error("Final basic block does not match Last");
        break;
      }

      LLVMBasicBlockRef Prev = LLVMGetPreviousBasicBlock(Next);
      if (Prev != Cur)
        report_fatal_error("Next.Previous basic bloc is not Current");

      Cur = Next;
    }

    if (Count != 0)
      report_fatal_error("Basic block count does not match iterration");
  }
};

static void declare_symbols(LLVMModuleRef Src, LLVMModuleRef M) {
  auto Ctx = LLVMGetModuleContext(M);

  LLVMValueRef Begin = LLVMGetFirstGlobal(Src);
  LLVMValueRef End = LLVMGetLastGlobal(Src);

  LLVMValueRef Cur = Begin;
  LLVMValueRef Next = nullptr;
  if (!Begin) {
    if (End != nullptr)
      report_fatal_error("Range has an end but no beginning");
    goto FunDecl;
  }

  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cur, &NameLen);
    if (LLVMGetNamedGlobal(M, Name))
      report_fatal_error("GlobalVariable already cloned");
    LLVMAddGlobal(M, TypeCloner(M).Clone(LLVMGlobalGetValueType(Cur)), Name);

    Next = LLVMGetNextGlobal(Cur);
    if (Next == nullptr) {
      if (Cur != End)
        report_fatal_error("");
      break;
    }

    LLVMValueRef Prev = LLVMGetPreviousGlobal(Next);
    if (Prev != Cur)
      report_fatal_error("Next.Previous global is not Current");

    Cur = Next;
  }

FunDecl:
  Begin = LLVMGetFirstFunction(Src);
  End = LLVMGetLastFunction(Src);
  if (!Begin) {
    if (End != nullptr)
      report_fatal_error("Range has an end but no beginning");
    goto AliasDecl;
  }

  Cur = Begin;
  Next = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cur, &NameLen);
    if (LLVMGetNamedFunction(M, Name))
      report_fatal_error("Function already cloned");
    LLVMTypeRef Ty = TypeCloner(M).Clone(LLVMGlobalGetValueType(Cur));

    auto F = LLVMAddFunction(M, Name, Ty);

    // Copy attributes
    for (int i = LLVMAttributeFunctionIndex, c = LLVMCountParams(F);
         i <= c; ++i) {
      for (unsigned k = 0, e = LLVMGetLastEnumAttributeKind(); k < e; ++k) {
        if (auto SrcA = LLVMGetEnumAttributeAtIndex(Cur, i, k)) {
          auto Val = LLVMGetEnumAttributeValue(SrcA);
          auto DstA = LLVMCreateEnumAttribute(Ctx, k, Val);
          LLVMAddAttributeAtIndex(F, i, DstA);
        }
      }
    }

    Next = LLVMGetNextFunction(Cur);
    if (Next == nullptr) {
      if (Cur != End)
        report_fatal_error("Last function does not match End");
      break;
    }

    LLVMValueRef Prev = LLVMGetPreviousFunction(Next);
    if (Prev != Cur)
      report_fatal_error("Next.Previous function is not Current");

    Cur = Next;
  }

AliasDecl:
  Begin = LLVMGetFirstGlobalAlias(Src);
  End = LLVMGetLastGlobalAlias(Src);
  if (!Begin) {
    if (End != nullptr)
      report_fatal_error("Range has an end but no beginning");
    goto GlobalIFuncDecl;
  }

  Cur = Begin;
  Next = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cur, &NameLen);
    if (LLVMGetNamedGlobalAlias(M, Name, NameLen))
      report_fatal_error("Global alias already cloned");
    LLVMTypeRef PtrType = TypeCloner(M).Clone(Cur);
    LLVMTypeRef ValType = TypeCloner(M).Clone(LLVMGlobalGetValueType(Cur));
    unsigned AddrSpace = LLVMGetPointerAddressSpace(PtrType);
    // FIXME: Allow NULL aliasee.
    LLVMAddAlias2(M, ValType, AddrSpace, LLVMGetUndef(PtrType), Name);

    Next = LLVMGetNextGlobalAlias(Cur);
    if (Next == nullptr) {
      if (Cur != End)
        report_fatal_error("");
      break;
    }

    LLVMValueRef Prev = LLVMGetPreviousGlobalAlias(Next);
    if (Prev != Cur)
      report_fatal_error("Next.Previous global is not Current");

    Cur = Next;
  }

GlobalIFuncDecl:
  Begin = LLVMGetFirstGlobalIFunc(Src);
  End = LLVMGetLastGlobalIFunc(Src);
  if (!Begin) {
    if (End != nullptr)
      report_fatal_error("Range has an end but no beginning");
    goto NamedMDDecl;
  }

  Cur = Begin;
  Next = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cur, &NameLen);
    if (LLVMGetNamedGlobalIFunc(M, Name, NameLen))
      report_fatal_error("Global ifunc already cloned");
    LLVMTypeRef CurType = TypeCloner(M).Clone(LLVMGlobalGetValueType(Cur));
    // FIXME: Allow NULL resolver.
    LLVMAddGlobalIFunc(M, Name, NameLen,
                       CurType, /*addressSpace*/ 0, LLVMGetUndef(CurType));

    Next = LLVMGetNextGlobalIFunc(Cur);
    if (Next == nullptr) {
      if (Cur != End)
        report_fatal_error("");
      break;
    }

    LLVMValueRef Prev = LLVMGetPreviousGlobalIFunc(Next);
    if (Prev != Cur)
      report_fatal_error("Next.Previous global is not Current");

    Cur = Next;
  }

NamedMDDecl:
  LLVMNamedMDNodeRef BeginMD = LLVMGetFirstNamedMetadata(Src);
  LLVMNamedMDNodeRef EndMD = LLVMGetLastNamedMetadata(Src);
  if (!BeginMD) {
    if (EndMD != nullptr)
      report_fatal_error("Range has an end but no beginning");
    return;
  }

  LLVMNamedMDNodeRef CurMD = BeginMD;
  LLVMNamedMDNodeRef NextMD = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetNamedMetadataName(CurMD, &NameLen);
    if (LLVMGetNamedMetadata(M, Name, NameLen))
      report_fatal_error("Named Metadata Node already cloned");
    LLVMGetOrInsertNamedMetadata(M, Name, NameLen);

    NextMD = LLVMGetNextNamedMetadata(CurMD);
    if (NextMD == nullptr) {
      if (CurMD != EndMD)
        report_fatal_error("");
      break;
    }

    LLVMNamedMDNodeRef PrevMD = LLVMGetPreviousNamedMetadata(NextMD);
    if (PrevMD != CurMD)
      report_fatal_error("Next.Previous global is not Current");

    CurMD = NextMD;
  }
}

static void clone_symbols(LLVMModuleRef Src, LLVMModuleRef M) {
  LLVMValueRef Begin = LLVMGetFirstGlobal(Src);
  LLVMValueRef End = LLVMGetLastGlobal(Src);

  LLVMValueRef Cur = Begin;
  LLVMValueRef Next = nullptr;
  if (!Begin) {
    if (End != nullptr)
      report_fatal_error("Range has an end but no beginning");
    goto FunClone;
  }

  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cur, &NameLen);
    LLVMValueRef G = LLVMGetNamedGlobal(M, Name);
    if (!G)
      report_fatal_error("GlobalVariable must have been declared already");

    if (auto I = LLVMGetInitializer(Cur))
      LLVMSetInitializer(G, clone_constant(I, M));

    size_t NumMetadataEntries;
    auto *AllMetadata = LLVMGlobalCopyAllMetadata(Cur, &NumMetadataEntries);
    for (unsigned i = 0; i < NumMetadataEntries; ++i) {
      unsigned Kind = LLVMValueMetadataEntriesGetKind(AllMetadata, i);
      LLVMMetadataRef MD = LLVMValueMetadataEntriesGetMetadata(AllMetadata, i);
      LLVMGlobalSetMetadata(G, Kind, MD);
    }
    LLVMDisposeValueMetadataEntries(AllMetadata);

    LLVMSetGlobalConstant(G, LLVMIsGlobalConstant(Cur));
    LLVMSetThreadLocal(G, LLVMIsThreadLocal(Cur));
    LLVMSetExternallyInitialized(G, LLVMIsExternallyInitialized(Cur));
    LLVMSetLinkage(G, LLVMGetLinkage(Cur));
    LLVMSetSection(G, LLVMGetSection(Cur));
    LLVMSetVisibility(G, LLVMGetVisibility(Cur));
    LLVMSetUnnamedAddress(G, LLVMGetUnnamedAddress(Cur));
    LLVMSetAlignment(G, LLVMGetAlignment(Cur));

    Next = LLVMGetNextGlobal(Cur);
    if (Next == nullptr) {
      if (Cur != End)
        report_fatal_error("");
      break;
    }

    LLVMValueRef Prev = LLVMGetPreviousGlobal(Next);
    if (Prev != Cur)
      report_fatal_error("Next.Previous global is not Current");

    Cur = Next;
  }

FunClone:
  Begin = LLVMGetFirstFunction(Src);
  End = LLVMGetLastFunction(Src);
  if (!Begin) {
    if (End != nullptr)
      report_fatal_error("Range has an end but no beginning");
    goto AliasClone;
  }

  Cur = Begin;
  Next = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cur, &NameLen);
    LLVMValueRef Fun = LLVMGetNamedFunction(M, Name);
    if (!Fun)
      report_fatal_error("Function must have been declared already");

    if (LLVMHasPersonalityFn(Cur)) {
      size_t FNameLen;
      const char *FName = LLVMGetValueName2(LLVMGetPersonalityFn(Cur),
                                           &FNameLen);
      LLVMValueRef P = LLVMGetNamedFunction(M, FName);
      if (!P)
        report_fatal_error("Could not find personality function");
      LLVMSetPersonalityFn(Fun, P);
    }

    size_t NumMetadataEntries;
    auto *AllMetadata = LLVMGlobalCopyAllMetadata(Cur, &NumMetadataEntries);
    for (unsigned i = 0; i < NumMetadataEntries; ++i) {
      unsigned Kind = LLVMValueMetadataEntriesGetKind(AllMetadata, i);
      LLVMMetadataRef MD = LLVMValueMetadataEntriesGetMetadata(AllMetadata, i);
      LLVMGlobalSetMetadata(Fun, Kind, MD);
    }
    LLVMDisposeValueMetadataEntries(AllMetadata);

    // Copy any prefix data that may be on the function
    if (LLVMHasPrefixData(Cur))
      LLVMSetPrefixData(Fun, clone_constant(LLVMGetPrefixData(Cur), M));

    // Copy any prologue data that may be on the function
    if (LLVMHasPrologueData(Cur))
      LLVMSetPrologueData(Fun, clone_constant(LLVMGetPrologueData(Cur), M));

    FunCloner FC(Cur, Fun);
    FC.CloneBBs(Cur);

    Next = LLVMGetNextFunction(Cur);
    if (Next == nullptr) {
      if (Cur != End)
        report_fatal_error("Last function does not match End");
      break;
    }

    LLVMValueRef Prev = LLVMGetPreviousFunction(Next);
    if (Prev != Cur)
      report_fatal_error("Next.Previous function is not Current");

    Cur = Next;
  }

AliasClone:
  Begin = LLVMGetFirstGlobalAlias(Src);
  End = LLVMGetLastGlobalAlias(Src);
  if (!Begin) {
    if (End != nullptr)
      report_fatal_error("Range has an end but no beginning");
    goto GlobalIFuncClone;
  }

  Cur = Begin;
  Next = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cur, &NameLen);
    LLVMValueRef Alias = LLVMGetNamedGlobalAlias(M, Name, NameLen);
    if (!Alias)
      report_fatal_error("Global alias must have been declared already");

    if (LLVMValueRef Aliasee = LLVMAliasGetAliasee(Cur)) {
      LLVMAliasSetAliasee(Alias, clone_constant(Aliasee, M));
    }

    LLVMSetLinkage(Alias, LLVMGetLinkage(Cur));
    LLVMSetUnnamedAddress(Alias, LLVMGetUnnamedAddress(Cur));

    Next = LLVMGetNextGlobalAlias(Cur);
    if (Next == nullptr) {
      if (Cur != End)
        report_fatal_error("Last global alias does not match End");
      break;
    }

    LLVMValueRef Prev = LLVMGetPreviousGlobalAlias(Next);
    if (Prev != Cur)
      report_fatal_error("Next.Previous global alias is not Current");

    Cur = Next;
  }

GlobalIFuncClone:
  Begin = LLVMGetFirstGlobalIFunc(Src);
  End = LLVMGetLastGlobalIFunc(Src);
  if (!Begin) {
    if (End != nullptr)
      report_fatal_error("Range has an end but no beginning");
    goto NamedMDClone;
  }

  Cur = Begin;
  Next = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetValueName2(Cur, &NameLen);
    LLVMValueRef IFunc = LLVMGetNamedGlobalIFunc(M, Name, NameLen);
    if (!IFunc)
      report_fatal_error("Global ifunc must have been declared already");

    if (LLVMValueRef Resolver = LLVMGetGlobalIFuncResolver(Cur)) {
      LLVMSetGlobalIFuncResolver(IFunc, clone_constant(Resolver, M));
    }

    LLVMSetLinkage(IFunc, LLVMGetLinkage(Cur));
    LLVMSetUnnamedAddress(IFunc, LLVMGetUnnamedAddress(Cur));

    Next = LLVMGetNextGlobalIFunc(Cur);
    if (Next == nullptr) {
      if (Cur != End)
        report_fatal_error("Last global alias does not match End");
      break;
    }

    LLVMValueRef Prev = LLVMGetPreviousGlobalIFunc(Next);
    if (Prev != Cur)
      report_fatal_error("Next.Previous global alias is not Current");

    Cur = Next;
  }

NamedMDClone:
  LLVMNamedMDNodeRef BeginMD = LLVMGetFirstNamedMetadata(Src);
  LLVMNamedMDNodeRef EndMD = LLVMGetLastNamedMetadata(Src);
  if (!BeginMD) {
    if (EndMD != nullptr)
      report_fatal_error("Range has an end but no beginning");
    return;
  }

  LLVMNamedMDNodeRef CurMD = BeginMD;
  LLVMNamedMDNodeRef NextMD = nullptr;
  while (true) {
    size_t NameLen;
    const char *Name = LLVMGetNamedMetadataName(CurMD, &NameLen);
    LLVMNamedMDNodeRef NamedMD = LLVMGetNamedMetadata(M, Name, NameLen);
    if (!NamedMD)
      report_fatal_error("Named MD Node must have been declared already");

    unsigned OperandCount = LLVMGetNamedMetadataNumOperands(Src, Name);
    LLVMValueRef *OperandBuf = static_cast<LLVMValueRef *>(
              safe_malloc(OperandCount * sizeof(LLVMValueRef)));
    LLVMGetNamedMetadataOperands(Src, Name, OperandBuf);
    for (unsigned i = 0, e = OperandCount; i != e; ++i) {
      LLVMAddNamedMetadataOperand(M, Name, OperandBuf[i]);
    }
    free(OperandBuf);

    NextMD = LLVMGetNextNamedMetadata(CurMD);
    if (NextMD == nullptr) {
      if (CurMD != EndMD)
        report_fatal_error("Last Named MD Node does not match End");
      break;
    }

    LLVMNamedMDNodeRef PrevMD = LLVMGetPreviousNamedMetadata(NextMD);
    if (PrevMD != CurMD)
      report_fatal_error("Next.Previous Named MD Node is not Current");

    CurMD = NextMD;
  }
}

int llvm_echo(void) {
  LLVMEnablePrettyStackTrace();

  LLVMModuleRef Src = llvm_load_module(false, true);
  size_t SourceFileLen;
  const char *SourceFileName = LLVMGetSourceFileName(Src, &SourceFileLen);
  size_t ModuleIdentLen;
  const char *ModuleName = LLVMGetModuleIdentifier(Src, &ModuleIdentLen);
  LLVMContextRef Ctx = LLVMContextCreate();
  LLVMModuleRef M = LLVMModuleCreateWithNameInContext(ModuleName, Ctx);

  LLVMSetSourceFileName(M, SourceFileName, SourceFileLen);
  LLVMSetModuleIdentifier(M, ModuleName, ModuleIdentLen);

  LLVMSetTarget(M, LLVMGetTarget(Src));
  LLVMSetModuleDataLayout(M, LLVMGetModuleDataLayout(Src));
  if (strcmp(LLVMGetDataLayoutStr(M), LLVMGetDataLayoutStr(Src)))
    report_fatal_error("Inconsistent DataLayout string representation");

  size_t ModuleInlineAsmLen;
  const char *ModuleAsm = LLVMGetModuleInlineAsm(Src, &ModuleInlineAsmLen);
  LLVMSetModuleInlineAsm2(M, ModuleAsm, ModuleInlineAsmLen);

  declare_symbols(Src, M);
  clone_symbols(Src, M);
  char *Str = LLVMPrintModuleToString(M);
  fputs(Str, stdout);

  LLVMDisposeMessage(Str);
  LLVMDisposeModule(Src);
  LLVMDisposeModule(M);
  LLVMContextDispose(Ctx);

  return 0;
}
