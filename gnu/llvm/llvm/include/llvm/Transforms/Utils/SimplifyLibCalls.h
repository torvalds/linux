//===- SimplifyLibCalls.h - Library call simplifier -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes an interface to build some C language libcalls for
// optimization passes that need to call the various functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SIMPLIFYLIBCALLS_H
#define LLVM_TRANSFORMS_UTILS_SIMPLIFYLIBCALLS_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

namespace llvm {
class AssumptionCache;
class StringRef;
class Value;
class CallInst;
class DataLayout;
class Instruction;
class IRBuilderBase;
class Function;
class OptimizationRemarkEmitter;
class BlockFrequencyInfo;
class ProfileSummaryInfo;

/// This class implements simplifications for calls to fortified library
/// functions (__st*cpy_chk, __memcpy_chk, __memmove_chk, __memset_chk), to,
/// when possible, replace them with their non-checking counterparts.
/// Other optimizations can also be done, but it's possible to disable them and
/// only simplify needless use of the checking versions (when the object size
/// is unknown) by passing true for OnlyLowerUnknownSize.
class FortifiedLibCallSimplifier {
private:
  const TargetLibraryInfo *TLI;
  bool OnlyLowerUnknownSize;

public:
  FortifiedLibCallSimplifier(const TargetLibraryInfo *TLI,
                             bool OnlyLowerUnknownSize = false);

  /// Take the given call instruction and return a more
  /// optimal value to replace the instruction with or 0 if a more
  /// optimal form can't be found.
  /// The call must not be an indirect call.
  Value *optimizeCall(CallInst *CI, IRBuilderBase &B);

private:
  Value *optimizeMemCpyChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemMoveChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemSetChk(CallInst *CI, IRBuilderBase &B);

  /// Str/Stp cpy are similar enough to be handled in the same functions.
  Value *optimizeStrpCpyChk(CallInst *CI, IRBuilderBase &B, LibFunc Func);
  Value *optimizeStrpNCpyChk(CallInst *CI, IRBuilderBase &B, LibFunc Func);
  Value *optimizeStrLenChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemPCpyChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemCCpyChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSNPrintfChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSPrintfChk(CallInst *CI,IRBuilderBase &B);
  Value *optimizeStrCatChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrLCat(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrNCatChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrLCpyChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeVSNPrintfChk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeVSPrintfChk(CallInst *CI, IRBuilderBase &B);

  /// Checks whether the call \p CI to a fortified libcall is foldable
  /// to the non-fortified version.
  ///
  /// \param CI the call to the fortified libcall.
  ///
  /// \param ObjSizeOp the index of the object size parameter of this chk
  /// function. Not optional since this is mandatory.
  ///
  /// \param SizeOp optionally set to the parameter index of an explicit buffer
  /// size argument. For instance, set to '2' for __strncpy_chk.
  ///
  /// \param StrOp optionally set to the parameter index of the source string
  /// parameter to strcpy-like functions, where only the strlen of the source
  /// will be writtin into the destination.
  ///
  /// \param FlagsOp optionally set to the parameter index of a 'flags'
  /// parameter. These are used by an implementation to opt-into stricter
  /// checking.
  bool isFortifiedCallFoldable(CallInst *CI, unsigned ObjSizeOp,
                               std::optional<unsigned> SizeOp = std::nullopt,
                               std::optional<unsigned> StrOp = std::nullopt,
                               std::optional<unsigned> FlagsOp = std::nullopt);
};

/// LibCallSimplifier - This class implements a collection of optimizations
/// that replace well formed calls to library functions with a more optimal
/// form.  For example, replacing 'printf("Hello!")' with 'puts("Hello!")'.
class LibCallSimplifier {
private:
  FortifiedLibCallSimplifier FortifiedSimplifier;
  const DataLayout &DL;
  const TargetLibraryInfo *TLI;
  AssumptionCache *AC;
  OptimizationRemarkEmitter &ORE;
  BlockFrequencyInfo *BFI;
  ProfileSummaryInfo *PSI;
  bool UnsafeFPShrink = false;
  function_ref<void(Instruction *, Value *)> Replacer;
  function_ref<void(Instruction *)> Eraser;

  /// Internal wrapper for RAUW that is the default implementation.
  ///
  /// Other users may provide an alternate function with this signature instead
  /// of this one.
  static void replaceAllUsesWithDefault(Instruction *I, Value *With) {
    I->replaceAllUsesWith(With);
  }

  /// Internal wrapper for eraseFromParent that is the default implementation.
  static void eraseFromParentDefault(Instruction *I) { I->eraseFromParent(); }

  /// Replace an instruction's uses with a value using our replacer.
  void replaceAllUsesWith(Instruction *I, Value *With);

  /// Erase an instruction from its parent with our eraser.
  void eraseFromParent(Instruction *I);

  /// Replace an instruction with a value and erase it from its parent.
  void substituteInParent(Instruction *I, Value *With) {
    replaceAllUsesWith(I, With);
    eraseFromParent(I);
  }

public:
  LibCallSimplifier(
      const DataLayout &DL, const TargetLibraryInfo *TLI, AssumptionCache *AC,
      OptimizationRemarkEmitter &ORE, BlockFrequencyInfo *BFI,
      ProfileSummaryInfo *PSI,
      function_ref<void(Instruction *, Value *)> Replacer =
          &replaceAllUsesWithDefault,
      function_ref<void(Instruction *)> Eraser = &eraseFromParentDefault);

  /// optimizeCall - Take the given call instruction and return a more
  /// optimal value to replace the instruction with or 0 if a more
  /// optimal form can't be found.  Note that the returned value may
  /// be equal to the instruction being optimized.  In this case all
  /// other instructions that use the given instruction were modified
  /// and the given instruction is dead.
  /// The call must not be an indirect call.
  Value *optimizeCall(CallInst *CI, IRBuilderBase &B);

private:
  // String and Memory Library Call Optimizations
  Value *optimizeStrCat(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrNCat(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrChr(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrRChr(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrCmp(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrNCmp(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrNDup(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrCpy(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStpCpy(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrLCpy(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrNCpy(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrLen(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrNLen(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrPBrk(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrTo(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrSpn(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrCSpn(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrStr(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemChr(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemRChr(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemCmp(CallInst *CI, IRBuilderBase &B);
  Value *optimizeBCmp(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemCmpBCmpCommon(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemCCpy(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemPCpy(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemCpy(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemMove(CallInst *CI, IRBuilderBase &B);
  Value *optimizeMemSet(CallInst *CI, IRBuilderBase &B);
  Value *optimizeRealloc(CallInst *CI, IRBuilderBase &B);
  Value *optimizeNew(CallInst *CI, IRBuilderBase &B, LibFunc &Func);
  Value *optimizeWcslen(CallInst *CI, IRBuilderBase &B);
  Value *optimizeBCopy(CallInst *CI, IRBuilderBase &B);

  // Helper to optimize stpncpy and strncpy.
  Value *optimizeStringNCpy(CallInst *CI, bool RetEnd, IRBuilderBase &B);
  // Wrapper for all String/Memory Library Call Optimizations
  Value *optimizeStringMemoryLibCall(CallInst *CI, IRBuilderBase &B);

  // Math Library Optimizations
  Value *optimizeCAbs(CallInst *CI, IRBuilderBase &B);
  Value *optimizePow(CallInst *CI, IRBuilderBase &B);
  Value *replacePowWithExp(CallInst *Pow, IRBuilderBase &B);
  Value *replacePowWithSqrt(CallInst *Pow, IRBuilderBase &B);
  Value *optimizeExp2(CallInst *CI, IRBuilderBase &B);
  Value *optimizeFMinFMax(CallInst *CI, IRBuilderBase &B);
  Value *optimizeLog(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSqrt(CallInst *CI, IRBuilderBase &B);
  Value *mergeSqrtToExp(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSinCosPi(CallInst *CI, bool IsSin, IRBuilderBase &B);
  Value *optimizeTrigInversionPairs(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSymmetric(CallInst *CI, LibFunc Func, IRBuilderBase &B);
  // Wrapper for all floating point library call optimizations
  Value *optimizeFloatingPointLibCall(CallInst *CI, LibFunc Func,
                                      IRBuilderBase &B);

  // Integer Library Call Optimizations
  Value *optimizeFFS(CallInst *CI, IRBuilderBase &B);
  Value *optimizeFls(CallInst *CI, IRBuilderBase &B);
  Value *optimizeAbs(CallInst *CI, IRBuilderBase &B);
  Value *optimizeIsDigit(CallInst *CI, IRBuilderBase &B);
  Value *optimizeIsAscii(CallInst *CI, IRBuilderBase &B);
  Value *optimizeToAscii(CallInst *CI, IRBuilderBase &B);
  Value *optimizeAtoi(CallInst *CI, IRBuilderBase &B);
  Value *optimizeStrToInt(CallInst *CI, IRBuilderBase &B, bool AsSigned);

  // Formatting and IO Library Call Optimizations
  Value *optimizeErrorReporting(CallInst *CI, IRBuilderBase &B,
                                int StreamArg = -1);
  Value *optimizePrintF(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSPrintF(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSnPrintF(CallInst *CI, IRBuilderBase &B);
  Value *optimizeFPrintF(CallInst *CI, IRBuilderBase &B);
  Value *optimizeFWrite(CallInst *CI, IRBuilderBase &B);
  Value *optimizeFPuts(CallInst *CI, IRBuilderBase &B);
  Value *optimizePuts(CallInst *CI, IRBuilderBase &B);

  // Helper methods
  Value* emitSnPrintfMemCpy(CallInst *CI, Value *StrArg, StringRef Str,
                            uint64_t N, IRBuilderBase &B);
  Value *emitStrLenMemCpy(Value *Src, Value *Dst, uint64_t Len,
                          IRBuilderBase &B);
  void classifyArgUse(Value *Val, Function *F, bool IsFloat,
                      SmallVectorImpl<CallInst *> &SinCalls,
                      SmallVectorImpl<CallInst *> &CosCalls,
                      SmallVectorImpl<CallInst *> &SinCosCalls);
  Value *optimizePrintFString(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSPrintFString(CallInst *CI, IRBuilderBase &B);
  Value *optimizeSnPrintFString(CallInst *CI, IRBuilderBase &B);
  Value *optimizeFPrintFString(CallInst *CI, IRBuilderBase &B);

  /// hasFloatVersion - Checks if there is a float version of the specified
  /// function by checking for an existing function with name FuncName + f
  bool hasFloatVersion(const Module *M, StringRef FuncName);

  /// Shared code to optimize strlen+wcslen and strnlen+wcsnlen.
  Value *optimizeStringLength(CallInst *CI, IRBuilderBase &B, unsigned CharSize,
                              Value *Bound = nullptr);
};
} // End llvm namespace

#endif
