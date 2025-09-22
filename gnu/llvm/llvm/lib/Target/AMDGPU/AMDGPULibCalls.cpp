//===- AMDGPULibCalls.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file does AMD library function optimizations.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPULibFunc.h"
#include "GCNSubtarget.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include <cmath>

#define DEBUG_TYPE "amdgpu-simplifylib"

using namespace llvm;
using namespace llvm::PatternMatch;

static cl::opt<bool> EnablePreLink("amdgpu-prelink",
  cl::desc("Enable pre-link mode optimizations"),
  cl::init(false),
  cl::Hidden);

static cl::list<std::string> UseNative("amdgpu-use-native",
  cl::desc("Comma separated list of functions to replace with native, or all"),
  cl::CommaSeparated, cl::ValueOptional,
  cl::Hidden);

#define MATH_PI      numbers::pi
#define MATH_E       numbers::e
#define MATH_SQRT2   numbers::sqrt2
#define MATH_SQRT1_2 numbers::inv_sqrt2

namespace llvm {

class AMDGPULibCalls {
private:
  const TargetLibraryInfo *TLInfo = nullptr;
  AssumptionCache *AC = nullptr;
  DominatorTree *DT = nullptr;

  using FuncInfo = llvm::AMDGPULibFunc;

  bool UnsafeFPMath = false;

  // -fuse-native.
  bool AllNative = false;

  bool useNativeFunc(const StringRef F) const;

  // Return a pointer (pointer expr) to the function if function definition with
  // "FuncName" exists. It may create a new function prototype in pre-link mode.
  FunctionCallee getFunction(Module *M, const FuncInfo &fInfo);

  bool parseFunctionName(const StringRef &FMangledName, FuncInfo &FInfo);

  bool TDOFold(CallInst *CI, const FuncInfo &FInfo);

  /* Specialized optimizations */

  // pow/powr/pown
  bool fold_pow(FPMathOperator *FPOp, IRBuilder<> &B, const FuncInfo &FInfo);

  // rootn
  bool fold_rootn(FPMathOperator *FPOp, IRBuilder<> &B, const FuncInfo &FInfo);

  // -fuse-native for sincos
  bool sincosUseNative(CallInst *aCI, const FuncInfo &FInfo);

  // evaluate calls if calls' arguments are constants.
  bool evaluateScalarMathFunc(const FuncInfo &FInfo, double &Res0, double &Res1,
                              Constant *copr0, Constant *copr1);
  bool evaluateCall(CallInst *aCI, const FuncInfo &FInfo);

  /// Insert a value to sincos function \p Fsincos. Returns (value of sin, value
  /// of cos, sincos call).
  std::tuple<Value *, Value *, Value *> insertSinCos(Value *Arg,
                                                     FastMathFlags FMF,
                                                     IRBuilder<> &B,
                                                     FunctionCallee Fsincos);

  // sin/cos
  bool fold_sincos(FPMathOperator *FPOp, IRBuilder<> &B, const FuncInfo &FInfo);

  // __read_pipe/__write_pipe
  bool fold_read_write_pipe(CallInst *CI, IRBuilder<> &B,
                            const FuncInfo &FInfo);

  // Get a scalar native builtin single argument FP function
  FunctionCallee getNativeFunction(Module *M, const FuncInfo &FInfo);

  /// Substitute a call to a known libcall with an intrinsic call. If \p
  /// AllowMinSize is true, allow the replacement in a minsize function.
  bool shouldReplaceLibcallWithIntrinsic(const CallInst *CI,
                                         bool AllowMinSizeF32 = false,
                                         bool AllowF64 = false,
                                         bool AllowStrictFP = false);
  void replaceLibCallWithSimpleIntrinsic(IRBuilder<> &B, CallInst *CI,
                                         Intrinsic::ID IntrID);

  bool tryReplaceLibcallWithSimpleIntrinsic(IRBuilder<> &B, CallInst *CI,
                                            Intrinsic::ID IntrID,
                                            bool AllowMinSizeF32 = false,
                                            bool AllowF64 = false,
                                            bool AllowStrictFP = false);

protected:
  bool isUnsafeMath(const FPMathOperator *FPOp) const;
  bool isUnsafeFiniteOnlyMath(const FPMathOperator *FPOp) const;

  bool canIncreasePrecisionOfConstantFold(const FPMathOperator *FPOp) const;

  static void replaceCall(Instruction *I, Value *With) {
    I->replaceAllUsesWith(With);
    I->eraseFromParent();
  }

  static void replaceCall(FPMathOperator *I, Value *With) {
    replaceCall(cast<Instruction>(I), With);
  }

public:
  AMDGPULibCalls() = default;

  bool fold(CallInst *CI);

  void initFunction(Function &F, FunctionAnalysisManager &FAM);
  void initNativeFuncs();

  // Replace a normal math function call with that native version
  bool useNative(CallInst *CI);
};

} // end namespace llvm

template <typename IRB>
static CallInst *CreateCallEx(IRB &B, FunctionCallee Callee, Value *Arg,
                              const Twine &Name = "") {
  CallInst *R = B.CreateCall(Callee, Arg, Name);
  if (Function *F = dyn_cast<Function>(Callee.getCallee()))
    R->setCallingConv(F->getCallingConv());
  return R;
}

template <typename IRB>
static CallInst *CreateCallEx2(IRB &B, FunctionCallee Callee, Value *Arg1,
                               Value *Arg2, const Twine &Name = "") {
  CallInst *R = B.CreateCall(Callee, {Arg1, Arg2}, Name);
  if (Function *F = dyn_cast<Function>(Callee.getCallee()))
    R->setCallingConv(F->getCallingConv());
  return R;
}

static FunctionType *getPownType(FunctionType *FT) {
  Type *PowNExpTy = Type::getInt32Ty(FT->getContext());
  if (VectorType *VecTy = dyn_cast<VectorType>(FT->getReturnType()))
    PowNExpTy = VectorType::get(PowNExpTy, VecTy->getElementCount());

  return FunctionType::get(FT->getReturnType(),
                           {FT->getParamType(0), PowNExpTy}, false);
}

//  Data structures for table-driven optimizations.
//  FuncTbl works for both f32 and f64 functions with 1 input argument

struct TableEntry {
  double   result;
  double   input;
};

/* a list of {result, input} */
static const TableEntry tbl_acos[] = {
  {MATH_PI / 2.0, 0.0},
  {MATH_PI / 2.0, -0.0},
  {0.0, 1.0},
  {MATH_PI, -1.0}
};
static const TableEntry tbl_acosh[] = {
  {0.0, 1.0}
};
static const TableEntry tbl_acospi[] = {
  {0.5, 0.0},
  {0.5, -0.0},
  {0.0, 1.0},
  {1.0, -1.0}
};
static const TableEntry tbl_asin[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {MATH_PI / 2.0, 1.0},
  {-MATH_PI / 2.0, -1.0}
};
static const TableEntry tbl_asinh[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_asinpi[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {0.5, 1.0},
  {-0.5, -1.0}
};
static const TableEntry tbl_atan[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {MATH_PI / 4.0, 1.0},
  {-MATH_PI / 4.0, -1.0}
};
static const TableEntry tbl_atanh[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_atanpi[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {0.25, 1.0},
  {-0.25, -1.0}
};
static const TableEntry tbl_cbrt[] = {
  {0.0, 0.0},
  {-0.0, -0.0},
  {1.0, 1.0},
  {-1.0, -1.0},
};
static const TableEntry tbl_cos[] = {
  {1.0, 0.0},
  {1.0, -0.0}
};
static const TableEntry tbl_cosh[] = {
  {1.0, 0.0},
  {1.0, -0.0}
};
static const TableEntry tbl_cospi[] = {
  {1.0, 0.0},
  {1.0, -0.0}
};
static const TableEntry tbl_erfc[] = {
  {1.0, 0.0},
  {1.0, -0.0}
};
static const TableEntry tbl_erf[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_exp[] = {
  {1.0, 0.0},
  {1.0, -0.0},
  {MATH_E, 1.0}
};
static const TableEntry tbl_exp2[] = {
  {1.0, 0.0},
  {1.0, -0.0},
  {2.0, 1.0}
};
static const TableEntry tbl_exp10[] = {
  {1.0, 0.0},
  {1.0, -0.0},
  {10.0, 1.0}
};
static const TableEntry tbl_expm1[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_log[] = {
  {0.0, 1.0},
  {1.0, MATH_E}
};
static const TableEntry tbl_log2[] = {
  {0.0, 1.0},
  {1.0, 2.0}
};
static const TableEntry tbl_log10[] = {
  {0.0, 1.0},
  {1.0, 10.0}
};
static const TableEntry tbl_rsqrt[] = {
  {1.0, 1.0},
  {MATH_SQRT1_2, 2.0}
};
static const TableEntry tbl_sin[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_sinh[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_sinpi[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_sqrt[] = {
  {0.0, 0.0},
  {1.0, 1.0},
  {MATH_SQRT2, 2.0}
};
static const TableEntry tbl_tan[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_tanh[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_tanpi[] = {
  {0.0, 0.0},
  {-0.0, -0.0}
};
static const TableEntry tbl_tgamma[] = {
  {1.0, 1.0},
  {1.0, 2.0},
  {2.0, 3.0},
  {6.0, 4.0}
};

static bool HasNative(AMDGPULibFunc::EFuncId id) {
  switch(id) {
  case AMDGPULibFunc::EI_DIVIDE:
  case AMDGPULibFunc::EI_COS:
  case AMDGPULibFunc::EI_EXP:
  case AMDGPULibFunc::EI_EXP2:
  case AMDGPULibFunc::EI_EXP10:
  case AMDGPULibFunc::EI_LOG:
  case AMDGPULibFunc::EI_LOG2:
  case AMDGPULibFunc::EI_LOG10:
  case AMDGPULibFunc::EI_POWR:
  case AMDGPULibFunc::EI_RECIP:
  case AMDGPULibFunc::EI_RSQRT:
  case AMDGPULibFunc::EI_SIN:
  case AMDGPULibFunc::EI_SINCOS:
  case AMDGPULibFunc::EI_SQRT:
  case AMDGPULibFunc::EI_TAN:
    return true;
  default:;
  }
  return false;
}

using TableRef = ArrayRef<TableEntry>;

static TableRef getOptTable(AMDGPULibFunc::EFuncId id) {
  switch(id) {
  case AMDGPULibFunc::EI_ACOS:    return TableRef(tbl_acos);
  case AMDGPULibFunc::EI_ACOSH:   return TableRef(tbl_acosh);
  case AMDGPULibFunc::EI_ACOSPI:  return TableRef(tbl_acospi);
  case AMDGPULibFunc::EI_ASIN:    return TableRef(tbl_asin);
  case AMDGPULibFunc::EI_ASINH:   return TableRef(tbl_asinh);
  case AMDGPULibFunc::EI_ASINPI:  return TableRef(tbl_asinpi);
  case AMDGPULibFunc::EI_ATAN:    return TableRef(tbl_atan);
  case AMDGPULibFunc::EI_ATANH:   return TableRef(tbl_atanh);
  case AMDGPULibFunc::EI_ATANPI:  return TableRef(tbl_atanpi);
  case AMDGPULibFunc::EI_CBRT:    return TableRef(tbl_cbrt);
  case AMDGPULibFunc::EI_NCOS:
  case AMDGPULibFunc::EI_COS:     return TableRef(tbl_cos);
  case AMDGPULibFunc::EI_COSH:    return TableRef(tbl_cosh);
  case AMDGPULibFunc::EI_COSPI:   return TableRef(tbl_cospi);
  case AMDGPULibFunc::EI_ERFC:    return TableRef(tbl_erfc);
  case AMDGPULibFunc::EI_ERF:     return TableRef(tbl_erf);
  case AMDGPULibFunc::EI_EXP:     return TableRef(tbl_exp);
  case AMDGPULibFunc::EI_NEXP2:
  case AMDGPULibFunc::EI_EXP2:    return TableRef(tbl_exp2);
  case AMDGPULibFunc::EI_EXP10:   return TableRef(tbl_exp10);
  case AMDGPULibFunc::EI_EXPM1:   return TableRef(tbl_expm1);
  case AMDGPULibFunc::EI_LOG:     return TableRef(tbl_log);
  case AMDGPULibFunc::EI_NLOG2:
  case AMDGPULibFunc::EI_LOG2:    return TableRef(tbl_log2);
  case AMDGPULibFunc::EI_LOG10:   return TableRef(tbl_log10);
  case AMDGPULibFunc::EI_NRSQRT:
  case AMDGPULibFunc::EI_RSQRT:   return TableRef(tbl_rsqrt);
  case AMDGPULibFunc::EI_NSIN:
  case AMDGPULibFunc::EI_SIN:     return TableRef(tbl_sin);
  case AMDGPULibFunc::EI_SINH:    return TableRef(tbl_sinh);
  case AMDGPULibFunc::EI_SINPI:   return TableRef(tbl_sinpi);
  case AMDGPULibFunc::EI_NSQRT:
  case AMDGPULibFunc::EI_SQRT:    return TableRef(tbl_sqrt);
  case AMDGPULibFunc::EI_TAN:     return TableRef(tbl_tan);
  case AMDGPULibFunc::EI_TANH:    return TableRef(tbl_tanh);
  case AMDGPULibFunc::EI_TANPI:   return TableRef(tbl_tanpi);
  case AMDGPULibFunc::EI_TGAMMA:  return TableRef(tbl_tgamma);
  default:;
  }
  return TableRef();
}

static inline int getVecSize(const AMDGPULibFunc& FInfo) {
  return FInfo.getLeads()[0].VectorSize;
}

static inline AMDGPULibFunc::EType getArgType(const AMDGPULibFunc& FInfo) {
  return (AMDGPULibFunc::EType)FInfo.getLeads()[0].ArgType;
}

FunctionCallee AMDGPULibCalls::getFunction(Module *M, const FuncInfo &fInfo) {
  // If we are doing PreLinkOpt, the function is external. So it is safe to
  // use getOrInsertFunction() at this stage.

  return EnablePreLink ? AMDGPULibFunc::getOrInsertFunction(M, fInfo)
                       : AMDGPULibFunc::getFunction(M, fInfo);
}

bool AMDGPULibCalls::parseFunctionName(const StringRef &FMangledName,
                                       FuncInfo &FInfo) {
  return AMDGPULibFunc::parse(FMangledName, FInfo);
}

bool AMDGPULibCalls::isUnsafeMath(const FPMathOperator *FPOp) const {
  return UnsafeFPMath || FPOp->isFast();
}

bool AMDGPULibCalls::isUnsafeFiniteOnlyMath(const FPMathOperator *FPOp) const {
  return UnsafeFPMath ||
         (FPOp->hasApproxFunc() && FPOp->hasNoNaNs() && FPOp->hasNoInfs());
}

bool AMDGPULibCalls::canIncreasePrecisionOfConstantFold(
    const FPMathOperator *FPOp) const {
  // TODO: Refine to approxFunc or contract
  return isUnsafeMath(FPOp);
}

void AMDGPULibCalls::initFunction(Function &F, FunctionAnalysisManager &FAM) {
  UnsafeFPMath = F.getFnAttribute("unsafe-fp-math").getValueAsBool();
  AC = &FAM.getResult<AssumptionAnalysis>(F);
  TLInfo = &FAM.getResult<TargetLibraryAnalysis>(F);
  DT = FAM.getCachedResult<DominatorTreeAnalysis>(F);
}

bool AMDGPULibCalls::useNativeFunc(const StringRef F) const {
  return AllNative || llvm::is_contained(UseNative, F);
}

void AMDGPULibCalls::initNativeFuncs() {
  AllNative = useNativeFunc("all") ||
              (UseNative.getNumOccurrences() && UseNative.size() == 1 &&
               UseNative.begin()->empty());
}

bool AMDGPULibCalls::sincosUseNative(CallInst *aCI, const FuncInfo &FInfo) {
  bool native_sin = useNativeFunc("sin");
  bool native_cos = useNativeFunc("cos");

  if (native_sin && native_cos) {
    Module *M = aCI->getModule();
    Value *opr0 = aCI->getArgOperand(0);

    AMDGPULibFunc nf;
    nf.getLeads()[0].ArgType = FInfo.getLeads()[0].ArgType;
    nf.getLeads()[0].VectorSize = FInfo.getLeads()[0].VectorSize;

    nf.setPrefix(AMDGPULibFunc::NATIVE);
    nf.setId(AMDGPULibFunc::EI_SIN);
    FunctionCallee sinExpr = getFunction(M, nf);

    nf.setPrefix(AMDGPULibFunc::NATIVE);
    nf.setId(AMDGPULibFunc::EI_COS);
    FunctionCallee cosExpr = getFunction(M, nf);
    if (sinExpr && cosExpr) {
      Value *sinval =
          CallInst::Create(sinExpr, opr0, "splitsin", aCI->getIterator());
      Value *cosval =
          CallInst::Create(cosExpr, opr0, "splitcos", aCI->getIterator());
      new StoreInst(cosval, aCI->getArgOperand(1), aCI->getIterator());

      DEBUG_WITH_TYPE("usenative", dbgs() << "<useNative> replace " << *aCI
                                          << " with native version of sin/cos");

      replaceCall(aCI, sinval);
      return true;
    }
  }
  return false;
}

bool AMDGPULibCalls::useNative(CallInst *aCI) {
  Function *Callee = aCI->getCalledFunction();
  if (!Callee || aCI->isNoBuiltin())
    return false;

  FuncInfo FInfo;
  if (!parseFunctionName(Callee->getName(), FInfo) || !FInfo.isMangled() ||
      FInfo.getPrefix() != AMDGPULibFunc::NOPFX ||
      getArgType(FInfo) == AMDGPULibFunc::F64 || !HasNative(FInfo.getId()) ||
      !(AllNative || useNativeFunc(FInfo.getName()))) {
    return false;
  }

  if (FInfo.getId() == AMDGPULibFunc::EI_SINCOS)
    return sincosUseNative(aCI, FInfo);

  FInfo.setPrefix(AMDGPULibFunc::NATIVE);
  FunctionCallee F = getFunction(aCI->getModule(), FInfo);
  if (!F)
    return false;

  aCI->setCalledFunction(F);
  DEBUG_WITH_TYPE("usenative", dbgs() << "<useNative> replace " << *aCI
                                      << " with native version");
  return true;
}

// Clang emits call of __read_pipe_2 or __read_pipe_4 for OpenCL read_pipe
// builtin, with appended type size and alignment arguments, where 2 or 4
// indicates the original number of arguments. The library has optimized version
// of __read_pipe_2/__read_pipe_4 when the type size and alignment has the same
// power of 2 value. This function transforms __read_pipe_2 to __read_pipe_2_N
// for such cases where N is the size in bytes of the type (N = 1, 2, 4, 8, ...,
// 128). The same for __read_pipe_4, write_pipe_2, and write_pipe_4.
bool AMDGPULibCalls::fold_read_write_pipe(CallInst *CI, IRBuilder<> &B,
                                          const FuncInfo &FInfo) {
  auto *Callee = CI->getCalledFunction();
  if (!Callee->isDeclaration())
    return false;

  assert(Callee->hasName() && "Invalid read_pipe/write_pipe function");
  auto *M = Callee->getParent();
  std::string Name = std::string(Callee->getName());
  auto NumArg = CI->arg_size();
  if (NumArg != 4 && NumArg != 6)
    return false;
  ConstantInt *PacketSize =
      dyn_cast<ConstantInt>(CI->getArgOperand(NumArg - 2));
  ConstantInt *PacketAlign =
      dyn_cast<ConstantInt>(CI->getArgOperand(NumArg - 1));
  if (!PacketSize || !PacketAlign)
    return false;

  unsigned Size = PacketSize->getZExtValue();
  Align Alignment = PacketAlign->getAlignValue();
  if (Alignment != Size)
    return false;

  unsigned PtrArgLoc = CI->arg_size() - 3;
  Value *PtrArg = CI->getArgOperand(PtrArgLoc);
  Type *PtrTy = PtrArg->getType();

  SmallVector<llvm::Type *, 6> ArgTys;
  for (unsigned I = 0; I != PtrArgLoc; ++I)
    ArgTys.push_back(CI->getArgOperand(I)->getType());
  ArgTys.push_back(PtrTy);

  Name = Name + "_" + std::to_string(Size);
  auto *FTy = FunctionType::get(Callee->getReturnType(),
                                ArrayRef<Type *>(ArgTys), false);
  AMDGPULibFunc NewLibFunc(Name, FTy);
  FunctionCallee F = AMDGPULibFunc::getOrInsertFunction(M, NewLibFunc);
  if (!F)
    return false;

  SmallVector<Value *, 6> Args;
  for (unsigned I = 0; I != PtrArgLoc; ++I)
    Args.push_back(CI->getArgOperand(I));
  Args.push_back(PtrArg);

  auto *NCI = B.CreateCall(F, Args);
  NCI->setAttributes(CI->getAttributes());
  CI->replaceAllUsesWith(NCI);
  CI->dropAllReferences();
  CI->eraseFromParent();

  return true;
}

static bool isKnownIntegral(const Value *V, const DataLayout &DL,
                            FastMathFlags FMF) {
  if (isa<PoisonValue>(V))
    return true;
  if (isa<UndefValue>(V))
    return false;

  if (const ConstantFP *CF = dyn_cast<ConstantFP>(V))
    return CF->getValueAPF().isInteger();

  auto *VFVTy = dyn_cast<FixedVectorType>(V->getType());
  const Constant *CV = dyn_cast<Constant>(V);
  if (VFVTy && CV) {
    unsigned NumElts = VFVTy->getNumElements();
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *Elt = CV->getAggregateElement(i);
      if (!Elt)
        return false;
      if (isa<PoisonValue>(Elt))
        continue;

      const ConstantFP *CFP = dyn_cast<ConstantFP>(Elt);
      if (!CFP || !CFP->getValue().isInteger())
        return false;
    }

    return true;
  }

  const Instruction *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  switch (I->getOpcode()) {
  case Instruction::SIToFP:
  case Instruction::UIToFP:
    // TODO: Could check nofpclass(inf) on incoming argument
    if (FMF.noInfs())
      return true;

    // Need to check int size cannot produce infinity, which computeKnownFPClass
    // knows how to do already.
    return isKnownNeverInfinity(I, /*Depth=*/0, SimplifyQuery(DL));
  case Instruction::Call: {
    const CallInst *CI = cast<CallInst>(I);
    switch (CI->getIntrinsicID()) {
    case Intrinsic::trunc:
    case Intrinsic::floor:
    case Intrinsic::ceil:
    case Intrinsic::rint:
    case Intrinsic::nearbyint:
    case Intrinsic::round:
    case Intrinsic::roundeven:
      return (FMF.noInfs() && FMF.noNaNs()) ||
             isKnownNeverInfOrNaN(I, /*Depth=*/0, SimplifyQuery(DL));
    default:
      break;
    }

    break;
  }
  default:
    break;
  }

  return false;
}

// This function returns false if no change; return true otherwise.
bool AMDGPULibCalls::fold(CallInst *CI) {
  Function *Callee = CI->getCalledFunction();
  // Ignore indirect calls.
  if (!Callee || Callee->isIntrinsic() || CI->isNoBuiltin())
    return false;

  FuncInfo FInfo;
  if (!parseFunctionName(Callee->getName(), FInfo))
    return false;

  // Further check the number of arguments to see if they match.
  // TODO: Check calling convention matches too
  if (!FInfo.isCompatibleSignature(CI->getFunctionType()))
    return false;

  LLVM_DEBUG(dbgs() << "AMDIC: try folding " << *CI << '\n');

  if (TDOFold(CI, FInfo))
    return true;

  IRBuilder<> B(CI);
  if (CI->isStrictFP())
    B.setIsFPConstrained(true);

  if (FPMathOperator *FPOp = dyn_cast<FPMathOperator>(CI)) {
    // Under unsafe-math, evaluate calls if possible.
    // According to Brian Sumner, we can do this for all f32 function calls
    // using host's double function calls.
    if (canIncreasePrecisionOfConstantFold(FPOp) && evaluateCall(CI, FInfo))
      return true;

    // Copy fast flags from the original call.
    FastMathFlags FMF = FPOp->getFastMathFlags();
    B.setFastMathFlags(FMF);

    // Specialized optimizations for each function call.
    //
    // TODO: Handle native functions
    switch (FInfo.getId()) {
    case AMDGPULibFunc::EI_EXP:
      if (FMF.none())
        return false;
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::exp,
                                                  FMF.approxFunc());
    case AMDGPULibFunc::EI_EXP2:
      if (FMF.none())
        return false;
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::exp2,
                                                  FMF.approxFunc());
    case AMDGPULibFunc::EI_LOG:
      if (FMF.none())
        return false;
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::log,
                                                  FMF.approxFunc());
    case AMDGPULibFunc::EI_LOG2:
      if (FMF.none())
        return false;
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::log2,
                                                  FMF.approxFunc());
    case AMDGPULibFunc::EI_LOG10:
      if (FMF.none())
        return false;
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::log10,
                                                  FMF.approxFunc());
    case AMDGPULibFunc::EI_FMIN:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::minnum,
                                                  true, true);
    case AMDGPULibFunc::EI_FMAX:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::maxnum,
                                                  true, true);
    case AMDGPULibFunc::EI_FMA:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::fma, true,
                                                  true);
    case AMDGPULibFunc::EI_MAD:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::fmuladd,
                                                  true, true);
    case AMDGPULibFunc::EI_FABS:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::fabs, true,
                                                  true, true);
    case AMDGPULibFunc::EI_COPYSIGN:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::copysign,
                                                  true, true, true);
    case AMDGPULibFunc::EI_FLOOR:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::floor, true,
                                                  true);
    case AMDGPULibFunc::EI_CEIL:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::ceil, true,
                                                  true);
    case AMDGPULibFunc::EI_TRUNC:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::trunc, true,
                                                  true);
    case AMDGPULibFunc::EI_RINT:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::rint, true,
                                                  true);
    case AMDGPULibFunc::EI_ROUND:
      return tryReplaceLibcallWithSimpleIntrinsic(B, CI, Intrinsic::round, true,
                                                  true);
    case AMDGPULibFunc::EI_LDEXP: {
      if (!shouldReplaceLibcallWithIntrinsic(CI, true, true))
        return false;

      Value *Arg1 = CI->getArgOperand(1);
      if (VectorType *VecTy = dyn_cast<VectorType>(CI->getType());
          VecTy && !isa<VectorType>(Arg1->getType())) {
        Value *SplatArg1 = B.CreateVectorSplat(VecTy->getElementCount(), Arg1);
        CI->setArgOperand(1, SplatArg1);
      }

      CI->setCalledFunction(Intrinsic::getDeclaration(
          CI->getModule(), Intrinsic::ldexp,
          {CI->getType(), CI->getArgOperand(1)->getType()}));
      return true;
    }
    case AMDGPULibFunc::EI_POW: {
      Module *M = Callee->getParent();
      AMDGPULibFunc PowrInfo(AMDGPULibFunc::EI_POWR, FInfo);
      FunctionCallee PowrFunc = getFunction(M, PowrInfo);
      CallInst *Call = cast<CallInst>(FPOp);

      // pow(x, y) -> powr(x, y) for x >= -0.0
      // TODO: Account for flags on current call
      if (PowrFunc &&
          cannotBeOrderedLessThanZero(
              FPOp->getOperand(0), /*Depth=*/0,
              SimplifyQuery(M->getDataLayout(), TLInfo, DT, AC, Call))) {
        Call->setCalledFunction(PowrFunc);
        return fold_pow(FPOp, B, PowrInfo) || true;
      }

      // pow(x, y) -> pown(x, y) for known integral y
      if (isKnownIntegral(FPOp->getOperand(1), M->getDataLayout(),
                          FPOp->getFastMathFlags())) {
        FunctionType *PownType = getPownType(CI->getFunctionType());
        AMDGPULibFunc PownInfo(AMDGPULibFunc::EI_POWN, PownType, true);
        FunctionCallee PownFunc = getFunction(M, PownInfo);
        if (PownFunc) {
          // TODO: If the incoming integral value is an sitofp/uitofp, it won't
          // fold out without a known range. We can probably take the source
          // value directly.
          Value *CastedArg =
              B.CreateFPToSI(FPOp->getOperand(1), PownType->getParamType(1));
          // Have to drop any nofpclass attributes on the original call site.
          Call->removeParamAttrs(
              1, AttributeFuncs::typeIncompatible(CastedArg->getType()));
          Call->setCalledFunction(PownFunc);
          Call->setArgOperand(1, CastedArg);
          return fold_pow(FPOp, B, PownInfo) || true;
        }
      }

      return fold_pow(FPOp, B, FInfo);
    }
    case AMDGPULibFunc::EI_POWR:
    case AMDGPULibFunc::EI_POWN:
      return fold_pow(FPOp, B, FInfo);
    case AMDGPULibFunc::EI_ROOTN:
      return fold_rootn(FPOp, B, FInfo);
    case AMDGPULibFunc::EI_SQRT:
      // TODO: Allow with strictfp + constrained intrinsic
      return tryReplaceLibcallWithSimpleIntrinsic(
          B, CI, Intrinsic::sqrt, true, true, /*AllowStrictFP=*/false);
    case AMDGPULibFunc::EI_COS:
    case AMDGPULibFunc::EI_SIN:
      return fold_sincos(FPOp, B, FInfo);
    default:
      break;
    }
  } else {
    // Specialized optimizations for each function call
    switch (FInfo.getId()) {
    case AMDGPULibFunc::EI_READ_PIPE_2:
    case AMDGPULibFunc::EI_READ_PIPE_4:
    case AMDGPULibFunc::EI_WRITE_PIPE_2:
    case AMDGPULibFunc::EI_WRITE_PIPE_4:
      return fold_read_write_pipe(CI, B, FInfo);
    default:
      break;
    }
  }

  return false;
}

bool AMDGPULibCalls::TDOFold(CallInst *CI, const FuncInfo &FInfo) {
  // Table-Driven optimization
  const TableRef tr = getOptTable(FInfo.getId());
  if (tr.empty())
    return false;

  int const sz = (int)tr.size();
  Value *opr0 = CI->getArgOperand(0);

  if (getVecSize(FInfo) > 1) {
    if (ConstantDataVector *CV = dyn_cast<ConstantDataVector>(opr0)) {
      SmallVector<double, 0> DVal;
      for (int eltNo = 0; eltNo < getVecSize(FInfo); ++eltNo) {
        ConstantFP *eltval = dyn_cast<ConstantFP>(
                               CV->getElementAsConstant((unsigned)eltNo));
        assert(eltval && "Non-FP arguments in math function!");
        bool found = false;
        for (int i=0; i < sz; ++i) {
          if (eltval->isExactlyValue(tr[i].input)) {
            DVal.push_back(tr[i].result);
            found = true;
            break;
          }
        }
        if (!found) {
          // This vector constants not handled yet.
          return false;
        }
      }
      LLVMContext &context = CI->getParent()->getParent()->getContext();
      Constant *nval;
      if (getArgType(FInfo) == AMDGPULibFunc::F32) {
        SmallVector<float, 0> FVal;
        for (double D : DVal)
          FVal.push_back((float)D);
        ArrayRef<float> tmp(FVal);
        nval = ConstantDataVector::get(context, tmp);
      } else { // F64
        ArrayRef<double> tmp(DVal);
        nval = ConstantDataVector::get(context, tmp);
      }
      LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *nval << "\n");
      replaceCall(CI, nval);
      return true;
    }
  } else {
    // Scalar version
    if (ConstantFP *CF = dyn_cast<ConstantFP>(opr0)) {
      for (int i = 0; i < sz; ++i) {
        if (CF->isExactlyValue(tr[i].input)) {
          Value *nval = ConstantFP::get(CF->getType(), tr[i].result);
          LLVM_DEBUG(errs() << "AMDIC: " << *CI << " ---> " << *nval << "\n");
          replaceCall(CI, nval);
          return true;
        }
      }
    }
  }

  return false;
}

namespace llvm {
static double log2(double V) {
#if _XOPEN_SOURCE >= 600 || defined(_ISOC99_SOURCE) || _POSIX_C_SOURCE >= 200112L
  return ::log2(V);
#else
  return log(V) / numbers::ln2;
#endif
}
} // namespace llvm

bool AMDGPULibCalls::fold_pow(FPMathOperator *FPOp, IRBuilder<> &B,
                              const FuncInfo &FInfo) {
  assert((FInfo.getId() == AMDGPULibFunc::EI_POW ||
          FInfo.getId() == AMDGPULibFunc::EI_POWR ||
          FInfo.getId() == AMDGPULibFunc::EI_POWN) &&
         "fold_pow: encounter a wrong function call");

  Module *M = B.GetInsertBlock()->getModule();
  Type *eltType = FPOp->getType()->getScalarType();
  Value *opr0 = FPOp->getOperand(0);
  Value *opr1 = FPOp->getOperand(1);

  const APFloat *CF = nullptr;
  const APInt *CINT = nullptr;
  if (!match(opr1, m_APFloatAllowPoison(CF)))
    match(opr1, m_APIntAllowPoison(CINT));

  // 0x1111111 means that we don't do anything for this call.
  int ci_opr1 = (CINT ? (int)CINT->getSExtValue() : 0x1111111);

  if ((CF && CF->isZero()) || (CINT && ci_opr1 == 0)) {
    //  pow/powr/pown(x, 0) == 1
    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> 1\n");
    Constant *cnval = ConstantFP::get(eltType, 1.0);
    if (getVecSize(FInfo) > 1) {
      cnval = ConstantDataVector::getSplat(getVecSize(FInfo), cnval);
    }
    replaceCall(FPOp, cnval);
    return true;
  }
  if ((CF && CF->isExactlyValue(1.0)) || (CINT && ci_opr1 == 1)) {
    // pow/powr/pown(x, 1.0) = x
    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> " << *opr0 << "\n");
    replaceCall(FPOp, opr0);
    return true;
  }
  if ((CF && CF->isExactlyValue(2.0)) || (CINT && ci_opr1 == 2)) {
    // pow/powr/pown(x, 2.0) = x*x
    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> " << *opr0 << " * "
                      << *opr0 << "\n");
    Value *nval = B.CreateFMul(opr0, opr0, "__pow2");
    replaceCall(FPOp, nval);
    return true;
  }
  if ((CF && CF->isExactlyValue(-1.0)) || (CINT && ci_opr1 == -1)) {
    // pow/powr/pown(x, -1.0) = 1.0/x
    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> 1 / " << *opr0 << "\n");
    Constant *cnval = ConstantFP::get(eltType, 1.0);
    if (getVecSize(FInfo) > 1) {
      cnval = ConstantDataVector::getSplat(getVecSize(FInfo), cnval);
    }
    Value *nval = B.CreateFDiv(cnval, opr0, "__powrecip");
    replaceCall(FPOp, nval);
    return true;
  }

  if (CF && (CF->isExactlyValue(0.5) || CF->isExactlyValue(-0.5))) {
    // pow[r](x, [-]0.5) = sqrt(x)
    bool issqrt = CF->isExactlyValue(0.5);
    if (FunctionCallee FPExpr =
            getFunction(M, AMDGPULibFunc(issqrt ? AMDGPULibFunc::EI_SQRT
                                                : AMDGPULibFunc::EI_RSQRT,
                                         FInfo))) {
      LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> " << FInfo.getName()
                        << '(' << *opr0 << ")\n");
      Value *nval = CreateCallEx(B,FPExpr, opr0, issqrt ? "__pow2sqrt"
                                                        : "__pow2rsqrt");
      replaceCall(FPOp, nval);
      return true;
    }
  }

  if (!isUnsafeFiniteOnlyMath(FPOp))
    return false;

  // Unsafe Math optimization

  // Remember that ci_opr1 is set if opr1 is integral
  if (CF) {
    double dval = (getArgType(FInfo) == AMDGPULibFunc::F32)
                      ? (double)CF->convertToFloat()
                      : CF->convertToDouble();
    int ival = (int)dval;
    if ((double)ival == dval) {
      ci_opr1 = ival;
    } else
      ci_opr1 = 0x11111111;
  }

  // pow/powr/pown(x, c) = [1/](x*x*..x); where
  //   trunc(c) == c && the number of x == c && |c| <= 12
  unsigned abs_opr1 = (ci_opr1 < 0) ? -ci_opr1 : ci_opr1;
  if (abs_opr1 <= 12) {
    Constant *cnval;
    Value *nval;
    if (abs_opr1 == 0) {
      cnval = ConstantFP::get(eltType, 1.0);
      if (getVecSize(FInfo) > 1) {
        cnval = ConstantDataVector::getSplat(getVecSize(FInfo), cnval);
      }
      nval = cnval;
    } else {
      Value *valx2 = nullptr;
      nval = nullptr;
      while (abs_opr1 > 0) {
        valx2 = valx2 ? B.CreateFMul(valx2, valx2, "__powx2") : opr0;
        if (abs_opr1 & 1) {
          nval = nval ? B.CreateFMul(nval, valx2, "__powprod") : valx2;
        }
        abs_opr1 >>= 1;
      }
    }

    if (ci_opr1 < 0) {
      cnval = ConstantFP::get(eltType, 1.0);
      if (getVecSize(FInfo) > 1) {
        cnval = ConstantDataVector::getSplat(getVecSize(FInfo), cnval);
      }
      nval = B.CreateFDiv(cnval, nval, "__1powprod");
    }
    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> "
                      << ((ci_opr1 < 0) ? "1/prod(" : "prod(") << *opr0
                      << ")\n");
    replaceCall(FPOp, nval);
    return true;
  }

  // If we should use the generic intrinsic instead of emitting a libcall
  const bool ShouldUseIntrinsic = eltType->isFloatTy() || eltType->isHalfTy();

  // powr ---> exp2(y * log2(x))
  // pown/pow ---> powr(fabs(x), y) | (x & ((int)y << 31))
  FunctionCallee ExpExpr;
  if (ShouldUseIntrinsic)
    ExpExpr = Intrinsic::getDeclaration(M, Intrinsic::exp2, {FPOp->getType()});
  else {
    ExpExpr = getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_EXP2, FInfo));
    if (!ExpExpr)
      return false;
  }

  bool needlog = false;
  bool needabs = false;
  bool needcopysign = false;
  Constant *cnval = nullptr;
  if (getVecSize(FInfo) == 1) {
    CF = nullptr;
    match(opr0, m_APFloatAllowPoison(CF));

    if (CF) {
      double V = (getArgType(FInfo) == AMDGPULibFunc::F32)
                     ? (double)CF->convertToFloat()
                     : CF->convertToDouble();

      V = log2(std::abs(V));
      cnval = ConstantFP::get(eltType, V);
      needcopysign = (FInfo.getId() != AMDGPULibFunc::EI_POWR) &&
                     CF->isNegative();
    } else {
      needlog = true;
      needcopysign = needabs = FInfo.getId() != AMDGPULibFunc::EI_POWR;
    }
  } else {
    ConstantDataVector *CDV = dyn_cast<ConstantDataVector>(opr0);

    if (!CDV) {
      needlog = true;
      needcopysign = needabs = FInfo.getId() != AMDGPULibFunc::EI_POWR;
    } else {
      assert ((int)CDV->getNumElements() == getVecSize(FInfo) &&
              "Wrong vector size detected");

      SmallVector<double, 0> DVal;
      for (int i=0; i < getVecSize(FInfo); ++i) {
        double V = CDV->getElementAsAPFloat(i).convertToDouble();
        if (V < 0.0) needcopysign = true;
        V = log2(std::abs(V));
        DVal.push_back(V);
      }
      if (getArgType(FInfo) == AMDGPULibFunc::F32) {
        SmallVector<float, 0> FVal;
        for (double D : DVal)
          FVal.push_back((float)D);
        ArrayRef<float> tmp(FVal);
        cnval = ConstantDataVector::get(M->getContext(), tmp);
      } else {
        ArrayRef<double> tmp(DVal);
        cnval = ConstantDataVector::get(M->getContext(), tmp);
      }
    }
  }

  if (needcopysign && (FInfo.getId() == AMDGPULibFunc::EI_POW)) {
    // We cannot handle corner cases for a general pow() function, give up
    // unless y is a constant integral value. Then proceed as if it were pown.
    if (!isKnownIntegral(opr1, M->getDataLayout(), FPOp->getFastMathFlags()))
      return false;
  }

  Value *nval;
  if (needabs) {
    nval = B.CreateUnaryIntrinsic(Intrinsic::fabs, opr0, nullptr, "__fabs");
  } else {
    nval = cnval ? cnval : opr0;
  }
  if (needlog) {
    FunctionCallee LogExpr;
    if (ShouldUseIntrinsic) {
      LogExpr =
          Intrinsic::getDeclaration(M, Intrinsic::log2, {FPOp->getType()});
    } else {
      LogExpr = getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_LOG2, FInfo));
      if (!LogExpr)
        return false;
    }

    nval = CreateCallEx(B,LogExpr, nval, "__log2");
  }

  if (FInfo.getId() == AMDGPULibFunc::EI_POWN) {
    // convert int(32) to fp(f32 or f64)
    opr1 = B.CreateSIToFP(opr1, nval->getType(), "pownI2F");
  }
  nval = B.CreateFMul(opr1, nval, "__ylogx");
  nval = CreateCallEx(B,ExpExpr, nval, "__exp2");

  if (needcopysign) {
    Type* nTyS = B.getIntNTy(eltType->getPrimitiveSizeInBits());
    Type *nTy = FPOp->getType()->getWithNewType(nTyS);
    unsigned size = nTy->getScalarSizeInBits();
    Value *opr_n = FPOp->getOperand(1);
    if (opr_n->getType()->getScalarType()->isIntegerTy())
      opr_n = B.CreateZExtOrTrunc(opr_n, nTy, "__ytou");
    else
      opr_n = B.CreateFPToSI(opr1, nTy, "__ytou");

    Value *sign = B.CreateShl(opr_n, size-1, "__yeven");
    sign = B.CreateAnd(B.CreateBitCast(opr0, nTy), sign, "__pow_sign");
    nval = B.CreateOr(B.CreateBitCast(nval, nTy), sign);
    nval = B.CreateBitCast(nval, opr0->getType());
  }

  LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> "
                    << "exp2(" << *opr1 << " * log2(" << *opr0 << "))\n");
  replaceCall(FPOp, nval);

  return true;
}

bool AMDGPULibCalls::fold_rootn(FPMathOperator *FPOp, IRBuilder<> &B,
                                const FuncInfo &FInfo) {
  Value *opr0 = FPOp->getOperand(0);
  Value *opr1 = FPOp->getOperand(1);

  const APInt *CINT = nullptr;
  if (!match(opr1, m_APIntAllowPoison(CINT)))
    return false;

  Function *Parent = B.GetInsertBlock()->getParent();

  int ci_opr1 = (int)CINT->getSExtValue();
  if (ci_opr1 == 1 && !Parent->hasFnAttribute(Attribute::StrictFP)) {
    // rootn(x, 1) = x
    //
    // TODO: Insert constrained canonicalize for strictfp case.
    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> " << *opr0 << '\n');
    replaceCall(FPOp, opr0);
    return true;
  }

  Module *M = B.GetInsertBlock()->getModule();

  CallInst *CI = cast<CallInst>(FPOp);
  if (ci_opr1 == 2 &&
      shouldReplaceLibcallWithIntrinsic(CI,
                                        /*AllowMinSizeF32=*/true,
                                        /*AllowF64=*/true)) {
    // rootn(x, 2) = sqrt(x)
    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> sqrt(" << *opr0 << ")\n");

    CallInst *NewCall = B.CreateUnaryIntrinsic(Intrinsic::sqrt, opr0, CI);
    NewCall->takeName(CI);

    // OpenCL rootn has a looser ulp of 2 requirement than sqrt, so add some
    // metadata.
    MDBuilder MDHelper(M->getContext());
    MDNode *FPMD = MDHelper.createFPMath(std::max(FPOp->getFPAccuracy(), 2.0f));
    NewCall->setMetadata(LLVMContext::MD_fpmath, FPMD);

    replaceCall(CI, NewCall);
    return true;
  }

  if (ci_opr1 == 3) { // rootn(x, 3) = cbrt(x)
    if (FunctionCallee FPExpr =
            getFunction(M, AMDGPULibFunc(AMDGPULibFunc::EI_CBRT, FInfo))) {
      LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> cbrt(" << *opr0
                        << ")\n");
      Value *nval = CreateCallEx(B,FPExpr, opr0, "__rootn2cbrt");
      replaceCall(FPOp, nval);
      return true;
    }
  } else if (ci_opr1 == -1) { // rootn(x, -1) = 1.0/x
    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> 1.0 / " << *opr0 << "\n");
    Value *nval = B.CreateFDiv(ConstantFP::get(opr0->getType(), 1.0),
                               opr0,
                               "__rootn2div");
    replaceCall(FPOp, nval);
    return true;
  }

  if (ci_opr1 == -2 &&
      shouldReplaceLibcallWithIntrinsic(CI,
                                        /*AllowMinSizeF32=*/true,
                                        /*AllowF64=*/true)) {
    // rootn(x, -2) = rsqrt(x)

    // The original rootn had looser ulp requirements than the resultant sqrt
    // and fdiv.
    MDBuilder MDHelper(M->getContext());
    MDNode *FPMD = MDHelper.createFPMath(std::max(FPOp->getFPAccuracy(), 2.0f));

    // TODO: Could handle strictfp but need to fix strict sqrt emission
    FastMathFlags FMF = FPOp->getFastMathFlags();
    FMF.setAllowContract(true);

    CallInst *Sqrt = B.CreateUnaryIntrinsic(Intrinsic::sqrt, opr0, CI);
    Instruction *RSqrt = cast<Instruction>(
        B.CreateFDiv(ConstantFP::get(opr0->getType(), 1.0), Sqrt));
    Sqrt->setFastMathFlags(FMF);
    RSqrt->setFastMathFlags(FMF);
    RSqrt->setMetadata(LLVMContext::MD_fpmath, FPMD);

    LLVM_DEBUG(errs() << "AMDIC: " << *FPOp << " ---> rsqrt(" << *opr0
                      << ")\n");
    replaceCall(CI, RSqrt);
    return true;
  }

  return false;
}

// Get a scalar native builtin single argument FP function
FunctionCallee AMDGPULibCalls::getNativeFunction(Module *M,
                                                 const FuncInfo &FInfo) {
  if (getArgType(FInfo) == AMDGPULibFunc::F64 || !HasNative(FInfo.getId()))
    return nullptr;
  FuncInfo nf = FInfo;
  nf.setPrefix(AMDGPULibFunc::NATIVE);
  return getFunction(M, nf);
}

// Some library calls are just wrappers around llvm intrinsics, but compiled
// conservatively. Preserve the flags from the original call site by
// substituting them with direct calls with all the flags.
bool AMDGPULibCalls::shouldReplaceLibcallWithIntrinsic(const CallInst *CI,
                                                       bool AllowMinSizeF32,
                                                       bool AllowF64,
                                                       bool AllowStrictFP) {
  Type *FltTy = CI->getType()->getScalarType();
  const bool IsF32 = FltTy->isFloatTy();

  // f64 intrinsics aren't implemented for most operations.
  if (!IsF32 && !FltTy->isHalfTy() && (!AllowF64 || !FltTy->isDoubleTy()))
    return false;

  // We're implicitly inlining by replacing the libcall with the intrinsic, so
  // don't do it for noinline call sites.
  if (CI->isNoInline())
    return false;

  const Function *ParentF = CI->getFunction();
  // TODO: Handle strictfp
  if (!AllowStrictFP && ParentF->hasFnAttribute(Attribute::StrictFP))
    return false;

  if (IsF32 && !AllowMinSizeF32 && ParentF->hasMinSize())
    return false;
  return true;
}

void AMDGPULibCalls::replaceLibCallWithSimpleIntrinsic(IRBuilder<> &B,
                                                       CallInst *CI,
                                                       Intrinsic::ID IntrID) {
  if (CI->arg_size() == 2) {
    Value *Arg0 = CI->getArgOperand(0);
    Value *Arg1 = CI->getArgOperand(1);
    VectorType *Arg0VecTy = dyn_cast<VectorType>(Arg0->getType());
    VectorType *Arg1VecTy = dyn_cast<VectorType>(Arg1->getType());
    if (Arg0VecTy && !Arg1VecTy) {
      Value *SplatRHS = B.CreateVectorSplat(Arg0VecTy->getElementCount(), Arg1);
      CI->setArgOperand(1, SplatRHS);
    } else if (!Arg0VecTy && Arg1VecTy) {
      Value *SplatLHS = B.CreateVectorSplat(Arg1VecTy->getElementCount(), Arg0);
      CI->setArgOperand(0, SplatLHS);
    }
  }

  CI->setCalledFunction(
      Intrinsic::getDeclaration(CI->getModule(), IntrID, {CI->getType()}));
}

bool AMDGPULibCalls::tryReplaceLibcallWithSimpleIntrinsic(
    IRBuilder<> &B, CallInst *CI, Intrinsic::ID IntrID, bool AllowMinSizeF32,
    bool AllowF64, bool AllowStrictFP) {
  if (!shouldReplaceLibcallWithIntrinsic(CI, AllowMinSizeF32, AllowF64,
                                         AllowStrictFP))
    return false;
  replaceLibCallWithSimpleIntrinsic(B, CI, IntrID);
  return true;
}

std::tuple<Value *, Value *, Value *>
AMDGPULibCalls::insertSinCos(Value *Arg, FastMathFlags FMF, IRBuilder<> &B,
                             FunctionCallee Fsincos) {
  DebugLoc DL = B.getCurrentDebugLocation();
  Function *F = B.GetInsertBlock()->getParent();
  B.SetInsertPointPastAllocas(F);

  AllocaInst *Alloc = B.CreateAlloca(Arg->getType(), nullptr, "__sincos_");

  if (Instruction *ArgInst = dyn_cast<Instruction>(Arg)) {
    // If the argument is an instruction, it must dominate all uses so put our
    // sincos call there. Otherwise, right after the allocas works well enough
    // if it's an argument or constant.

    B.SetInsertPoint(ArgInst->getParent(), ++ArgInst->getIterator());

    // SetInsertPoint unwelcomely always tries to set the debug loc.
    B.SetCurrentDebugLocation(DL);
  }

  Type *CosPtrTy = Fsincos.getFunctionType()->getParamType(1);

  // The allocaInst allocates the memory in private address space. This need
  // to be addrspacecasted to point to the address space of cos pointer type.
  // In OpenCL 2.0 this is generic, while in 1.2 that is private.
  Value *CastAlloc = B.CreateAddrSpaceCast(Alloc, CosPtrTy);

  CallInst *SinCos = CreateCallEx2(B, Fsincos, Arg, CastAlloc);

  // TODO: Is it worth trying to preserve the location for the cos calls for the
  // load?

  LoadInst *LoadCos = B.CreateLoad(Alloc->getAllocatedType(), Alloc);
  return {SinCos, LoadCos, SinCos};
}

// fold sin, cos -> sincos.
bool AMDGPULibCalls::fold_sincos(FPMathOperator *FPOp, IRBuilder<> &B,
                                 const FuncInfo &fInfo) {
  assert(fInfo.getId() == AMDGPULibFunc::EI_SIN ||
         fInfo.getId() == AMDGPULibFunc::EI_COS);

  if ((getArgType(fInfo) != AMDGPULibFunc::F32 &&
       getArgType(fInfo) != AMDGPULibFunc::F64) ||
      fInfo.getPrefix() != AMDGPULibFunc::NOPFX)
    return false;

  bool const isSin = fInfo.getId() == AMDGPULibFunc::EI_SIN;

  Value *CArgVal = FPOp->getOperand(0);
  CallInst *CI = cast<CallInst>(FPOp);

  Function *F = B.GetInsertBlock()->getParent();
  Module *M = F->getParent();

  // Merge the sin and cos. For OpenCL 2.0, there may only be a generic pointer
  // implementation. Prefer the private form if available.
  AMDGPULibFunc SinCosLibFuncPrivate(AMDGPULibFunc::EI_SINCOS, fInfo);
  SinCosLibFuncPrivate.getLeads()[0].PtrKind =
      AMDGPULibFunc::getEPtrKindFromAddrSpace(AMDGPUAS::PRIVATE_ADDRESS);

  AMDGPULibFunc SinCosLibFuncGeneric(AMDGPULibFunc::EI_SINCOS, fInfo);
  SinCosLibFuncGeneric.getLeads()[0].PtrKind =
      AMDGPULibFunc::getEPtrKindFromAddrSpace(AMDGPUAS::FLAT_ADDRESS);

  FunctionCallee FSinCosPrivate = getFunction(M, SinCosLibFuncPrivate);
  FunctionCallee FSinCosGeneric = getFunction(M, SinCosLibFuncGeneric);
  FunctionCallee FSinCos = FSinCosPrivate ? FSinCosPrivate : FSinCosGeneric;
  if (!FSinCos)
    return false;

  SmallVector<CallInst *> SinCalls;
  SmallVector<CallInst *> CosCalls;
  SmallVector<CallInst *> SinCosCalls;
  FuncInfo PartnerInfo(isSin ? AMDGPULibFunc::EI_COS : AMDGPULibFunc::EI_SIN,
                       fInfo);
  const std::string PairName = PartnerInfo.mangle();

  StringRef SinName = isSin ? CI->getCalledFunction()->getName() : PairName;
  StringRef CosName = isSin ? PairName : CI->getCalledFunction()->getName();
  const std::string SinCosPrivateName = SinCosLibFuncPrivate.mangle();
  const std::string SinCosGenericName = SinCosLibFuncGeneric.mangle();

  // Intersect the two sets of flags.
  FastMathFlags FMF = FPOp->getFastMathFlags();
  MDNode *FPMath = CI->getMetadata(LLVMContext::MD_fpmath);

  SmallVector<DILocation *> MergeDbgLocs = {CI->getDebugLoc()};

  for (User* U : CArgVal->users()) {
    CallInst *XI = dyn_cast<CallInst>(U);
    if (!XI || XI->getFunction() != F || XI->isNoBuiltin())
      continue;

    Function *UCallee = XI->getCalledFunction();
    if (!UCallee)
      continue;

    bool Handled = true;

    if (UCallee->getName() == SinName)
      SinCalls.push_back(XI);
    else if (UCallee->getName() == CosName)
      CosCalls.push_back(XI);
    else if (UCallee->getName() == SinCosPrivateName ||
             UCallee->getName() == SinCosGenericName)
      SinCosCalls.push_back(XI);
    else
      Handled = false;

    if (Handled) {
      MergeDbgLocs.push_back(XI->getDebugLoc());
      auto *OtherOp = cast<FPMathOperator>(XI);
      FMF &= OtherOp->getFastMathFlags();
      FPMath = MDNode::getMostGenericFPMath(
          FPMath, XI->getMetadata(LLVMContext::MD_fpmath));
    }
  }

  if (SinCalls.empty() || CosCalls.empty())
    return false;

  B.setFastMathFlags(FMF);
  B.setDefaultFPMathTag(FPMath);
  DILocation *DbgLoc = DILocation::getMergedLocations(MergeDbgLocs);
  B.SetCurrentDebugLocation(DbgLoc);

  auto [Sin, Cos, SinCos] = insertSinCos(CArgVal, FMF, B, FSinCos);

  auto replaceTrigInsts = [](ArrayRef<CallInst *> Calls, Value *Res) {
    for (CallInst *C : Calls)
      C->replaceAllUsesWith(Res);

    // Leave the other dead instructions to avoid clobbering iterators.
  };

  replaceTrigInsts(SinCalls, Sin);
  replaceTrigInsts(CosCalls, Cos);
  replaceTrigInsts(SinCosCalls, SinCos);

  // It's safe to delete the original now.
  CI->eraseFromParent();
  return true;
}

bool AMDGPULibCalls::evaluateScalarMathFunc(const FuncInfo &FInfo, double &Res0,
                                            double &Res1, Constant *copr0,
                                            Constant *copr1) {
  // By default, opr0/opr1/opr3 holds values of float/double type.
  // If they are not float/double, each function has to its
  // operand separately.
  double opr0 = 0.0, opr1 = 0.0;
  ConstantFP *fpopr0 = dyn_cast_or_null<ConstantFP>(copr0);
  ConstantFP *fpopr1 = dyn_cast_or_null<ConstantFP>(copr1);
  if (fpopr0) {
    opr0 = (getArgType(FInfo) == AMDGPULibFunc::F64)
             ? fpopr0->getValueAPF().convertToDouble()
             : (double)fpopr0->getValueAPF().convertToFloat();
  }

  if (fpopr1) {
    opr1 = (getArgType(FInfo) == AMDGPULibFunc::F64)
             ? fpopr1->getValueAPF().convertToDouble()
             : (double)fpopr1->getValueAPF().convertToFloat();
  }

  switch (FInfo.getId()) {
  default : return false;

  case AMDGPULibFunc::EI_ACOS:
    Res0 = acos(opr0);
    return true;

  case AMDGPULibFunc::EI_ACOSH:
    // acosh(x) == log(x + sqrt(x*x - 1))
    Res0 = log(opr0 + sqrt(opr0*opr0 - 1.0));
    return true;

  case AMDGPULibFunc::EI_ACOSPI:
    Res0 = acos(opr0) / MATH_PI;
    return true;

  case AMDGPULibFunc::EI_ASIN:
    Res0 = asin(opr0);
    return true;

  case AMDGPULibFunc::EI_ASINH:
    // asinh(x) == log(x + sqrt(x*x + 1))
    Res0 = log(opr0 + sqrt(opr0*opr0 + 1.0));
    return true;

  case AMDGPULibFunc::EI_ASINPI:
    Res0 = asin(opr0) / MATH_PI;
    return true;

  case AMDGPULibFunc::EI_ATAN:
    Res0 = atan(opr0);
    return true;

  case AMDGPULibFunc::EI_ATANH:
    // atanh(x) == (log(x+1) - log(x-1))/2;
    Res0 = (log(opr0 + 1.0) - log(opr0 - 1.0))/2.0;
    return true;

  case AMDGPULibFunc::EI_ATANPI:
    Res0 = atan(opr0) / MATH_PI;
    return true;

  case AMDGPULibFunc::EI_CBRT:
    Res0 = (opr0 < 0.0) ? -pow(-opr0, 1.0/3.0) : pow(opr0, 1.0/3.0);
    return true;

  case AMDGPULibFunc::EI_COS:
    Res0 = cos(opr0);
    return true;

  case AMDGPULibFunc::EI_COSH:
    Res0 = cosh(opr0);
    return true;

  case AMDGPULibFunc::EI_COSPI:
    Res0 = cos(MATH_PI * opr0);
    return true;

  case AMDGPULibFunc::EI_EXP:
    Res0 = exp(opr0);
    return true;

  case AMDGPULibFunc::EI_EXP2:
    Res0 = pow(2.0, opr0);
    return true;

  case AMDGPULibFunc::EI_EXP10:
    Res0 = pow(10.0, opr0);
    return true;

  case AMDGPULibFunc::EI_LOG:
    Res0 = log(opr0);
    return true;

  case AMDGPULibFunc::EI_LOG2:
    Res0 = log(opr0) / log(2.0);
    return true;

  case AMDGPULibFunc::EI_LOG10:
    Res0 = log(opr0) / log(10.0);
    return true;

  case AMDGPULibFunc::EI_RSQRT:
    Res0 = 1.0 / sqrt(opr0);
    return true;

  case AMDGPULibFunc::EI_SIN:
    Res0 = sin(opr0);
    return true;

  case AMDGPULibFunc::EI_SINH:
    Res0 = sinh(opr0);
    return true;

  case AMDGPULibFunc::EI_SINPI:
    Res0 = sin(MATH_PI * opr0);
    return true;

  case AMDGPULibFunc::EI_TAN:
    Res0 = tan(opr0);
    return true;

  case AMDGPULibFunc::EI_TANH:
    Res0 = tanh(opr0);
    return true;

  case AMDGPULibFunc::EI_TANPI:
    Res0 = tan(MATH_PI * opr0);
    return true;

  // two-arg functions
  case AMDGPULibFunc::EI_POW:
  case AMDGPULibFunc::EI_POWR:
    Res0 = pow(opr0, opr1);
    return true;

  case AMDGPULibFunc::EI_POWN: {
    if (ConstantInt *iopr1 = dyn_cast_or_null<ConstantInt>(copr1)) {
      double val = (double)iopr1->getSExtValue();
      Res0 = pow(opr0, val);
      return true;
    }
    return false;
  }

  case AMDGPULibFunc::EI_ROOTN: {
    if (ConstantInt *iopr1 = dyn_cast_or_null<ConstantInt>(copr1)) {
      double val = (double)iopr1->getSExtValue();
      Res0 = pow(opr0, 1.0 / val);
      return true;
    }
    return false;
  }

  // with ptr arg
  case AMDGPULibFunc::EI_SINCOS:
    Res0 = sin(opr0);
    Res1 = cos(opr0);
    return true;
  }

  return false;
}

bool AMDGPULibCalls::evaluateCall(CallInst *aCI, const FuncInfo &FInfo) {
  int numArgs = (int)aCI->arg_size();
  if (numArgs > 3)
    return false;

  Constant *copr0 = nullptr;
  Constant *copr1 = nullptr;
  if (numArgs > 0) {
    if ((copr0 = dyn_cast<Constant>(aCI->getArgOperand(0))) == nullptr)
      return false;
  }

  if (numArgs > 1) {
    if ((copr1 = dyn_cast<Constant>(aCI->getArgOperand(1))) == nullptr) {
      if (FInfo.getId() != AMDGPULibFunc::EI_SINCOS)
        return false;
    }
  }

  // At this point, all arguments to aCI are constants.

  // max vector size is 16, and sincos will generate two results.
  double DVal0[16], DVal1[16];
  int FuncVecSize = getVecSize(FInfo);
  bool hasTwoResults = (FInfo.getId() == AMDGPULibFunc::EI_SINCOS);
  if (FuncVecSize == 1) {
    if (!evaluateScalarMathFunc(FInfo, DVal0[0], DVal1[0], copr0, copr1)) {
      return false;
    }
  } else {
    ConstantDataVector *CDV0 = dyn_cast_or_null<ConstantDataVector>(copr0);
    ConstantDataVector *CDV1 = dyn_cast_or_null<ConstantDataVector>(copr1);
    for (int i = 0; i < FuncVecSize; ++i) {
      Constant *celt0 = CDV0 ? CDV0->getElementAsConstant(i) : nullptr;
      Constant *celt1 = CDV1 ? CDV1->getElementAsConstant(i) : nullptr;
      if (!evaluateScalarMathFunc(FInfo, DVal0[i], DVal1[i], celt0, celt1)) {
        return false;
      }
    }
  }

  LLVMContext &context = aCI->getContext();
  Constant *nval0, *nval1;
  if (FuncVecSize == 1) {
    nval0 = ConstantFP::get(aCI->getType(), DVal0[0]);
    if (hasTwoResults)
      nval1 = ConstantFP::get(aCI->getType(), DVal1[0]);
  } else {
    if (getArgType(FInfo) == AMDGPULibFunc::F32) {
      SmallVector <float, 0> FVal0, FVal1;
      for (int i = 0; i < FuncVecSize; ++i)
        FVal0.push_back((float)DVal0[i]);
      ArrayRef<float> tmp0(FVal0);
      nval0 = ConstantDataVector::get(context, tmp0);
      if (hasTwoResults) {
        for (int i = 0; i < FuncVecSize; ++i)
          FVal1.push_back((float)DVal1[i]);
        ArrayRef<float> tmp1(FVal1);
        nval1 = ConstantDataVector::get(context, tmp1);
      }
    } else {
      ArrayRef<double> tmp0(DVal0);
      nval0 = ConstantDataVector::get(context, tmp0);
      if (hasTwoResults) {
        ArrayRef<double> tmp1(DVal1);
        nval1 = ConstantDataVector::get(context, tmp1);
      }
    }
  }

  if (hasTwoResults) {
    // sincos
    assert(FInfo.getId() == AMDGPULibFunc::EI_SINCOS &&
           "math function with ptr arg not supported yet");
    new StoreInst(nval1, aCI->getArgOperand(1), aCI->getIterator());
  }

  replaceCall(aCI, nval0);
  return true;
}

PreservedAnalyses AMDGPUSimplifyLibCallsPass::run(Function &F,
                                                  FunctionAnalysisManager &AM) {
  AMDGPULibCalls Simplifier;
  Simplifier.initNativeFuncs();
  Simplifier.initFunction(F, AM);

  bool Changed = false;

  LLVM_DEBUG(dbgs() << "AMDIC: process function ";
             F.printAsOperand(dbgs(), false, F.getParent()); dbgs() << '\n';);

  for (auto &BB : F) {
    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E;) {
      // Ignore non-calls.
      CallInst *CI = dyn_cast<CallInst>(I);
      ++I;

      if (CI) {
        if (Simplifier.fold(CI))
          Changed = true;
      }
    }
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

PreservedAnalyses AMDGPUUseNativeCallsPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  if (UseNative.empty())
    return PreservedAnalyses::all();

  AMDGPULibCalls Simplifier;
  Simplifier.initNativeFuncs();
  Simplifier.initFunction(F, AM);

  bool Changed = false;
  for (auto &BB : F) {
    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E;) {
      // Ignore non-calls.
      CallInst *CI = dyn_cast<CallInst>(I);
      ++I;
      if (CI && Simplifier.useNative(CI))
        Changed = true;
    }
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
