//===- BuildLibCalls.cpp - Utility builder for libcalls -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements some functions that will create standard C libcalls.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "build-libcalls"

//- Infer Attributes ---------------------------------------------------------//

STATISTIC(NumReadNone, "Number of functions inferred as readnone");
STATISTIC(NumInaccessibleMemOnly,
          "Number of functions inferred as inaccessiblememonly");
STATISTIC(NumReadOnly, "Number of functions inferred as readonly");
STATISTIC(NumWriteOnly, "Number of functions inferred as writeonly");
STATISTIC(NumArgMemOnly, "Number of functions inferred as argmemonly");
STATISTIC(NumInaccessibleMemOrArgMemOnly,
          "Number of functions inferred as inaccessiblemem_or_argmemonly");
STATISTIC(NumNoUnwind, "Number of functions inferred as nounwind");
STATISTIC(NumNoCapture, "Number of arguments inferred as nocapture");
STATISTIC(NumWriteOnlyArg, "Number of arguments inferred as writeonly");
STATISTIC(NumReadOnlyArg, "Number of arguments inferred as readonly");
STATISTIC(NumNoAlias, "Number of function returns inferred as noalias");
STATISTIC(NumNoUndef, "Number of function returns inferred as noundef returns");
STATISTIC(NumReturnedArg, "Number of arguments inferred as returned");
STATISTIC(NumWillReturn, "Number of functions inferred as willreturn");

static bool setDoesNotAccessMemory(Function &F) {
  if (F.doesNotAccessMemory())
    return false;
  F.setDoesNotAccessMemory();
  ++NumReadNone;
  return true;
}

static bool setOnlyAccessesInaccessibleMemory(Function &F) {
  if (F.onlyAccessesInaccessibleMemory())
    return false;
  F.setOnlyAccessesInaccessibleMemory();
  ++NumInaccessibleMemOnly;
  return true;
}

static bool setOnlyReadsMemory(Function &F) {
  if (F.onlyReadsMemory())
    return false;
  F.setOnlyReadsMemory();
  ++NumReadOnly;
  return true;
}

static bool setOnlyWritesMemory(Function &F) {
  if (F.onlyWritesMemory()) // writeonly or readnone
    return false;
  ++NumWriteOnly;
  F.setOnlyWritesMemory();
  return true;
}

static bool setOnlyAccessesArgMemory(Function &F) {
  if (F.onlyAccessesArgMemory())
    return false;
  F.setOnlyAccessesArgMemory();
  ++NumArgMemOnly;
  return true;
}

static bool setOnlyAccessesInaccessibleMemOrArgMem(Function &F) {
  if (F.onlyAccessesInaccessibleMemOrArgMem())
    return false;
  F.setOnlyAccessesInaccessibleMemOrArgMem();
  ++NumInaccessibleMemOrArgMemOnly;
  return true;
}

static bool setDoesNotThrow(Function &F) {
  if (F.doesNotThrow())
    return false;
  F.setDoesNotThrow();
  ++NumNoUnwind;
  return true;
}

static bool setRetDoesNotAlias(Function &F) {
  if (F.hasRetAttribute(Attribute::NoAlias))
    return false;
  F.addRetAttr(Attribute::NoAlias);
  ++NumNoAlias;
  return true;
}

static bool setDoesNotCapture(Function &F, unsigned ArgNo) {
  if (F.hasParamAttribute(ArgNo, Attribute::NoCapture))
    return false;
  F.addParamAttr(ArgNo, Attribute::NoCapture);
  ++NumNoCapture;
  return true;
}

static bool setDoesNotAlias(Function &F, unsigned ArgNo) {
  if (F.hasParamAttribute(ArgNo, Attribute::NoAlias))
    return false;
  F.addParamAttr(ArgNo, Attribute::NoAlias);
  ++NumNoAlias;
  return true;
}

static bool setOnlyReadsMemory(Function &F, unsigned ArgNo) {
  if (F.hasParamAttribute(ArgNo, Attribute::ReadOnly))
    return false;
  F.addParamAttr(ArgNo, Attribute::ReadOnly);
  ++NumReadOnlyArg;
  return true;
}

static bool setOnlyWritesMemory(Function &F, unsigned ArgNo) {
  if (F.hasParamAttribute(ArgNo, Attribute::WriteOnly))
    return false;
  F.addParamAttr(ArgNo, Attribute::WriteOnly);
  ++NumWriteOnlyArg;
  return true;
}

static bool setRetNoUndef(Function &F) {
  if (!F.getReturnType()->isVoidTy() &&
      !F.hasRetAttribute(Attribute::NoUndef)) {
    F.addRetAttr(Attribute::NoUndef);
    ++NumNoUndef;
    return true;
  }
  return false;
}

static bool setArgsNoUndef(Function &F) {
  bool Changed = false;
  for (unsigned ArgNo = 0; ArgNo < F.arg_size(); ++ArgNo) {
    if (!F.hasParamAttribute(ArgNo, Attribute::NoUndef)) {
      F.addParamAttr(ArgNo, Attribute::NoUndef);
      ++NumNoUndef;
      Changed = true;
    }
  }
  return Changed;
}

static bool setArgNoUndef(Function &F, unsigned ArgNo) {
  if (F.hasParamAttribute(ArgNo, Attribute::NoUndef))
    return false;
  F.addParamAttr(ArgNo, Attribute::NoUndef);
  ++NumNoUndef;
  return true;
}

static bool setRetAndArgsNoUndef(Function &F) {
  bool UndefAdded = false;
  UndefAdded |= setRetNoUndef(F);
  UndefAdded |= setArgsNoUndef(F);
  return UndefAdded;
}

static bool setReturnedArg(Function &F, unsigned ArgNo) {
  if (F.hasParamAttribute(ArgNo, Attribute::Returned))
    return false;
  F.addParamAttr(ArgNo, Attribute::Returned);
  ++NumReturnedArg;
  return true;
}

static bool setNonLazyBind(Function &F) {
  if (F.hasFnAttribute(Attribute::NonLazyBind))
    return false;
  F.addFnAttr(Attribute::NonLazyBind);
  return true;
}

static bool setDoesNotFreeMemory(Function &F) {
  if (F.hasFnAttribute(Attribute::NoFree))
    return false;
  F.addFnAttr(Attribute::NoFree);
  return true;
}

static bool setWillReturn(Function &F) {
  if (F.hasFnAttribute(Attribute::WillReturn))
    return false;
  F.addFnAttr(Attribute::WillReturn);
  ++NumWillReturn;
  return true;
}

static bool setAlignedAllocParam(Function &F, unsigned ArgNo) {
  if (F.hasParamAttribute(ArgNo, Attribute::AllocAlign))
    return false;
  F.addParamAttr(ArgNo, Attribute::AllocAlign);
  return true;
}

static bool setAllocatedPointerParam(Function &F, unsigned ArgNo) {
  if (F.hasParamAttribute(ArgNo, Attribute::AllocatedPointer))
    return false;
  F.addParamAttr(ArgNo, Attribute::AllocatedPointer);
  return true;
}

static bool setAllocSize(Function &F, unsigned ElemSizeArg,
                         std::optional<unsigned> NumElemsArg) {
  if (F.hasFnAttribute(Attribute::AllocSize))
    return false;
  F.addFnAttr(Attribute::getWithAllocSizeArgs(F.getContext(), ElemSizeArg,
                                              NumElemsArg));
  return true;
}

static bool setAllocFamily(Function &F, StringRef Family) {
  if (F.hasFnAttribute("alloc-family"))
    return false;
  F.addFnAttr("alloc-family", Family);
  return true;
}

static bool setAllocKind(Function &F, AllocFnKind K) {
  if (F.hasFnAttribute(Attribute::AllocKind))
    return false;
  F.addFnAttr(
      Attribute::get(F.getContext(), Attribute::AllocKind, uint64_t(K)));
  return true;
}

bool llvm::inferNonMandatoryLibFuncAttrs(Module *M, StringRef Name,
                                         const TargetLibraryInfo &TLI) {
  Function *F = M->getFunction(Name);
  if (!F)
    return false;
  return inferNonMandatoryLibFuncAttrs(*F, TLI);
}

bool llvm::inferNonMandatoryLibFuncAttrs(Function &F,
                                         const TargetLibraryInfo &TLI) {
  LibFunc TheLibFunc;
  if (!(TLI.getLibFunc(F, TheLibFunc) && TLI.has(TheLibFunc)))
    return false;

  bool Changed = false;

  if (F.getParent() != nullptr && F.getParent()->getRtLibUseGOT())
    Changed |= setNonLazyBind(F);

  switch (TheLibFunc) {
  case LibFunc_strlen:
  case LibFunc_strnlen:
  case LibFunc_wcslen:
    Changed |= setOnlyReadsMemory(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_strchr:
  case LibFunc_strrchr:
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setOnlyReadsMemory(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    break;
  case LibFunc_strtol:
  case LibFunc_strtod:
  case LibFunc_strtof:
  case LibFunc_strtoul:
  case LibFunc_strtoll:
  case LibFunc_strtold:
  case LibFunc_strtoull:
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_strcat:
  case LibFunc_strncat:
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setReturnedArg(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    Changed |= setDoesNotAlias(F, 0);
    Changed |= setDoesNotAlias(F, 1);
    break;
  case LibFunc_strcpy:
  case LibFunc_strncpy:
    Changed |= setReturnedArg(F, 0);
    [[fallthrough]];
  case LibFunc_stpcpy:
  case LibFunc_stpncpy:
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyWritesMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    Changed |= setDoesNotAlias(F, 0);
    Changed |= setDoesNotAlias(F, 1);
    break;
  case LibFunc_strxfrm:
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_strcmp:      // 0,1
  case LibFunc_strspn:      // 0,1
  case LibFunc_strncmp:     // 0,1
  case LibFunc_strcspn:     // 0,1
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setWillReturn(F);
    Changed |= setOnlyReadsMemory(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_strcoll:
  case LibFunc_strcasecmp:  // 0,1
  case LibFunc_strncasecmp: //
    // Those functions may depend on the locale, which may be accessed through
    // global memory.
    Changed |= setOnlyReadsMemory(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_strstr:
  case LibFunc_strpbrk:
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setOnlyReadsMemory(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_strtok:
  case LibFunc_strtok_r:
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_scanf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_setbuf:
  case LibFunc_setvbuf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_strndup:
    Changed |= setArgNoUndef(F, 1);
    [[fallthrough]];
  case LibFunc_strdup:
    Changed |= setAllocFamily(F, "malloc");
    Changed |= setOnlyAccessesInaccessibleMemOrArgMem(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_stat:
  case LibFunc_statvfs:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_sscanf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_sprintf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotAlias(F, 0);
    Changed |= setOnlyWritesMemory(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_snprintf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotAlias(F, 0);
    Changed |= setOnlyWritesMemory(F, 0);
    Changed |= setDoesNotCapture(F, 2);
    Changed |= setOnlyReadsMemory(F, 2);
    break;
  case LibFunc_setitimer:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setDoesNotCapture(F, 2);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_system:
    // May throw; "system" is a valid pthread cancellation point.
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_aligned_alloc:
    Changed |= setAlignedAllocParam(F, 0);
    Changed |= setAllocSize(F, 1, std::nullopt);
    Changed |= setAllocKind(F, AllocFnKind::Alloc | AllocFnKind::Uninitialized | AllocFnKind::Aligned);
    [[fallthrough]];
  case LibFunc_valloc:
  case LibFunc_malloc:
  case LibFunc_vec_malloc:
    Changed |= setAllocFamily(F, TheLibFunc == LibFunc_vec_malloc ? "vec_malloc"
                                                                  : "malloc");
    Changed |= setAllocKind(F, AllocFnKind::Alloc | AllocFnKind::Uninitialized);
    Changed |= setAllocSize(F, 0, std::nullopt);
    Changed |= setOnlyAccessesInaccessibleMemory(F);
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setWillReturn(F);
    break;
  case LibFunc_memcmp:
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setOnlyReadsMemory(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_memchr:
  case LibFunc_memrchr:
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setOnlyReadsMemory(F);
    Changed |= setWillReturn(F);
    break;
  case LibFunc_modf:
  case LibFunc_modff:
  case LibFunc_modfl:
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setOnlyWritesMemory(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_memcpy:
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotAlias(F, 0);
    Changed |= setReturnedArg(F, 0);
    Changed |= setOnlyWritesMemory(F, 0);
    Changed |= setDoesNotAlias(F, 1);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_memmove:
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setWillReturn(F);
    Changed |= setReturnedArg(F, 0);
    Changed |= setOnlyWritesMemory(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_mempcpy:
  case LibFunc_memccpy:
    Changed |= setWillReturn(F);
    [[fallthrough]];
  case LibFunc_memcpy_chk:
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setDoesNotAlias(F, 0);
    Changed |= setOnlyWritesMemory(F, 0);
    Changed |= setDoesNotAlias(F, 1);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_memalign:
    Changed |= setAllocFamily(F, "malloc");
    Changed |= setAllocKind(F, AllocFnKind::Alloc | AllocFnKind::Aligned |
                                   AllocFnKind::Uninitialized);
    Changed |= setAllocSize(F, 1, std::nullopt);
    Changed |= setAlignedAllocParam(F, 0);
    Changed |= setOnlyAccessesInaccessibleMemory(F);
    Changed |= setRetNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setWillReturn(F);
    break;
  case LibFunc_mkdir:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_mktime:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_realloc:
  case LibFunc_reallocf:
  case LibFunc_vec_realloc:
    Changed |= setAllocFamily(
        F, TheLibFunc == LibFunc_vec_realloc ? "vec_malloc" : "malloc");
    Changed |= setAllocKind(F, AllocFnKind::Realloc);
    Changed |= setAllocatedPointerParam(F, 0);
    Changed |= setAllocSize(F, 1, std::nullopt);
    Changed |= setOnlyAccessesInaccessibleMemOrArgMem(F);
    Changed |= setRetNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setArgNoUndef(F, 1);
    break;
  case LibFunc_read:
    // May throw; "read" is a valid pthread cancellation point.
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_rewind:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_rmdir:
  case LibFunc_remove:
  case LibFunc_realpath:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_rename:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_readlink:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_write:
    // May throw; "write" is a valid pthread cancellation point.
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_bcopy:
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyWritesMemory(F, 1);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_bcmp:
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setOnlyReadsMemory(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_bzero:
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyWritesMemory(F, 0);
    break;
  case LibFunc_calloc:
  case LibFunc_vec_calloc:
    Changed |= setAllocFamily(F, TheLibFunc == LibFunc_vec_calloc ? "vec_malloc"
                                                                  : "malloc");
    Changed |= setAllocKind(F, AllocFnKind::Alloc | AllocFnKind::Zeroed);
    Changed |= setAllocSize(F, 0, 1);
    Changed |= setOnlyAccessesInaccessibleMemory(F);
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setWillReturn(F);
    break;
  case LibFunc_chmod:
  case LibFunc_chown:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_ctermid:
  case LibFunc_clearerr:
  case LibFunc_closedir:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_atoi:
  case LibFunc_atol:
  case LibFunc_atof:
  case LibFunc_atoll:
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyReadsMemory(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_access:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_fopen:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_fdopen:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_feof:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_free:
  case LibFunc_vec_free:
    Changed |= setAllocFamily(F, TheLibFunc == LibFunc_vec_free ? "vec_malloc"
                                                                : "malloc");
    Changed |= setAllocKind(F, AllocFnKind::Free);
    Changed |= setAllocatedPointerParam(F, 0);
    Changed |= setOnlyAccessesInaccessibleMemOrArgMem(F);
    Changed |= setArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_fseek:
  case LibFunc_ftell:
  case LibFunc_fgetc:
  case LibFunc_fgetc_unlocked:
  case LibFunc_fseeko:
  case LibFunc_ftello:
  case LibFunc_fileno:
  case LibFunc_fflush:
  case LibFunc_fclose:
  case LibFunc_fsetpos:
  case LibFunc_flockfile:
  case LibFunc_funlockfile:
  case LibFunc_ftrylockfile:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_ferror:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F);
    break;
  case LibFunc_fputc:
  case LibFunc_fputc_unlocked:
  case LibFunc_fstat:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_frexp:
  case LibFunc_frexpf:
  case LibFunc_frexpl:
    Changed |= setDoesNotThrow(F);
    Changed |= setWillReturn(F);
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setOnlyWritesMemory(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_fstatvfs:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_fgets:
  case LibFunc_fgets_unlocked:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 2);
    break;
  case LibFunc_fread:
  case LibFunc_fread_unlocked:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 3);
    break;
  case LibFunc_fwrite:
  case LibFunc_fwrite_unlocked:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 3);
    // FIXME: readonly #1?
    break;
  case LibFunc_fputs:
  case LibFunc_fputs_unlocked:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_fscanf:
  case LibFunc_fprintf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_fgetpos:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_getc:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_getlogin_r:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_getc_unlocked:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_getenv:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setOnlyReadsMemory(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_gets:
  case LibFunc_getchar:
  case LibFunc_getchar_unlocked:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    break;
  case LibFunc_getitimer:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_getpwnam:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_ungetc:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_uname:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_unlink:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_unsetenv:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_utime:
  case LibFunc_utimes:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_putc:
  case LibFunc_putc_unlocked:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_puts:
  case LibFunc_printf:
  case LibFunc_perror:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_pread:
    // May throw; "pread" is a valid pthread cancellation point.
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_pwrite:
    // May throw; "pwrite" is a valid pthread cancellation point.
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_putchar:
  case LibFunc_putchar_unlocked:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    break;
  case LibFunc_popen:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_pclose:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_vscanf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_vsscanf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_vfscanf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_vprintf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_vfprintf:
  case LibFunc_vsprintf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_vsnprintf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 2);
    Changed |= setOnlyReadsMemory(F, 2);
    break;
  case LibFunc_open:
    // May throw; "open" is a valid pthread cancellation point.
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_opendir:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_tmpfile:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    break;
  case LibFunc_times:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_htonl:
  case LibFunc_htons:
  case LibFunc_ntohl:
  case LibFunc_ntohs:
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotAccessMemory(F);
    break;
  case LibFunc_lstat:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_lchown:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_qsort:
    // May throw; places call through function pointer.
    // Cannot give undef pointer/size
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotCapture(F, 3);
    break;
  case LibFunc_dunder_strndup:
    Changed |= setArgNoUndef(F, 1);
    [[fallthrough]];
  case LibFunc_dunder_strdup:
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setWillReturn(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_dunder_strtok_r:
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_under_IO_getc:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_under_IO_putc:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_dunder_isoc99_scanf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_stat64:
  case LibFunc_lstat64:
  case LibFunc_statvfs64:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_dunder_isoc99_sscanf:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_fopen64:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 0);
    Changed |= setOnlyReadsMemory(F, 1);
    break;
  case LibFunc_fseeko64:
  case LibFunc_ftello64:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    break;
  case LibFunc_tmpfile64:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setRetDoesNotAlias(F);
    break;
  case LibFunc_fstat64:
  case LibFunc_fstatvfs64:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_open64:
    // May throw; "open" is a valid pthread cancellation point.
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setOnlyReadsMemory(F, 0);
    break;
  case LibFunc_gettimeofday:
    // Currently some platforms have the restrict keyword on the arguments to
    // gettimeofday. To be conservative, do not add noalias to gettimeofday's
    // arguments.
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    break;
  case LibFunc_memset_pattern4:
  case LibFunc_memset_pattern8:
  case LibFunc_memset_pattern16:
    Changed |= setDoesNotCapture(F, 0);
    Changed |= setDoesNotCapture(F, 1);
    Changed |= setOnlyReadsMemory(F, 1);
    [[fallthrough]];
  case LibFunc_memset:
    Changed |= setWillReturn(F);
    [[fallthrough]];
  case LibFunc_memset_chk:
    Changed |= setOnlyAccessesArgMemory(F);
    Changed |= setOnlyWritesMemory(F, 0);
    Changed |= setDoesNotThrow(F);
    break;
  // int __nvvm_reflect(const char *)
  case LibFunc_nvvm_reflect:
    Changed |= setRetAndArgsNoUndef(F);
    Changed |= setDoesNotAccessMemory(F);
    Changed |= setDoesNotThrow(F);
    break;
  case LibFunc_ldexp:
  case LibFunc_ldexpf:
  case LibFunc_ldexpl:
    Changed |= setWillReturn(F);
    break;
  case LibFunc_remquo:
  case LibFunc_remquof:
  case LibFunc_remquol:
    Changed |= setDoesNotCapture(F, 2);
    [[fallthrough]];
  case LibFunc_abs:
  case LibFunc_acos:
  case LibFunc_acosf:
  case LibFunc_acosh:
  case LibFunc_acoshf:
  case LibFunc_acoshl:
  case LibFunc_acosl:
  case LibFunc_asin:
  case LibFunc_asinf:
  case LibFunc_asinh:
  case LibFunc_asinhf:
  case LibFunc_asinhl:
  case LibFunc_asinl:
  case LibFunc_atan:
  case LibFunc_atan2:
  case LibFunc_atan2f:
  case LibFunc_atan2l:
  case LibFunc_atanf:
  case LibFunc_atanh:
  case LibFunc_atanhf:
  case LibFunc_atanhl:
  case LibFunc_atanl:
  case LibFunc_cbrt:
  case LibFunc_cbrtf:
  case LibFunc_cbrtl:
  case LibFunc_ceil:
  case LibFunc_ceilf:
  case LibFunc_ceill:
  case LibFunc_copysign:
  case LibFunc_copysignf:
  case LibFunc_copysignl:
  case LibFunc_cos:
  case LibFunc_cosh:
  case LibFunc_coshf:
  case LibFunc_coshl:
  case LibFunc_cosf:
  case LibFunc_cosl:
  case LibFunc_cospi:
  case LibFunc_cospif:
  case LibFunc_erf:
  case LibFunc_erff:
  case LibFunc_erfl:
  case LibFunc_exp:
  case LibFunc_expf:
  case LibFunc_expl:
  case LibFunc_exp2:
  case LibFunc_exp2f:
  case LibFunc_exp2l:
  case LibFunc_expm1:
  case LibFunc_expm1f:
  case LibFunc_expm1l:
  case LibFunc_fabs:
  case LibFunc_fabsf:
  case LibFunc_fabsl:
  case LibFunc_ffs:
  case LibFunc_ffsl:
  case LibFunc_ffsll:
  case LibFunc_floor:
  case LibFunc_floorf:
  case LibFunc_floorl:
  case LibFunc_fls:
  case LibFunc_flsl:
  case LibFunc_flsll:
  case LibFunc_fmax:
  case LibFunc_fmaxf:
  case LibFunc_fmaxl:
  case LibFunc_fmin:
  case LibFunc_fminf:
  case LibFunc_fminl:
  case LibFunc_fmod:
  case LibFunc_fmodf:
  case LibFunc_fmodl:
  case LibFunc_isascii:
  case LibFunc_isdigit:
  case LibFunc_labs:
  case LibFunc_llabs:
  case LibFunc_log:
  case LibFunc_log10:
  case LibFunc_log10f:
  case LibFunc_log10l:
  case LibFunc_log1p:
  case LibFunc_log1pf:
  case LibFunc_log1pl:
  case LibFunc_log2:
  case LibFunc_log2f:
  case LibFunc_log2l:
  case LibFunc_logb:
  case LibFunc_logbf:
  case LibFunc_logbl:
  case LibFunc_logf:
  case LibFunc_logl:
  case LibFunc_nearbyint:
  case LibFunc_nearbyintf:
  case LibFunc_nearbyintl:
  case LibFunc_pow:
  case LibFunc_powf:
  case LibFunc_powl:
  case LibFunc_remainder:
  case LibFunc_remainderf:
  case LibFunc_remainderl:
  case LibFunc_rint:
  case LibFunc_rintf:
  case LibFunc_rintl:
  case LibFunc_round:
  case LibFunc_roundf:
  case LibFunc_roundl:
  case LibFunc_sin:
  case LibFunc_sincospif_stret:
  case LibFunc_sinf:
  case LibFunc_sinh:
  case LibFunc_sinhf:
  case LibFunc_sinhl:
  case LibFunc_sinl:
  case LibFunc_sinpi:
  case LibFunc_sinpif:
  case LibFunc_sqrt:
  case LibFunc_sqrtf:
  case LibFunc_sqrtl:
  case LibFunc_tan:
  case LibFunc_tanf:
  case LibFunc_tanh:
  case LibFunc_tanhf:
  case LibFunc_tanhl:
  case LibFunc_tanl:
  case LibFunc_toascii:
  case LibFunc_trunc:
  case LibFunc_truncf:
  case LibFunc_truncl:
    Changed |= setDoesNotThrow(F);
    Changed |= setDoesNotFreeMemory(F);
    Changed |= setOnlyWritesMemory(F);
    Changed |= setWillReturn(F);
    break;
  default:
    // FIXME: It'd be really nice to cover all the library functions we're
    // aware of here.
    break;
  }
  // We have to do this step after AllocKind has been inferred on functions so
  // we can reliably identify free-like and realloc-like functions.
  if (!isLibFreeFunction(&F, TheLibFunc) && !isReallocLikeFn(&F))
    Changed |= setDoesNotFreeMemory(F);
  return Changed;
}

static void setArgExtAttr(Function &F, unsigned ArgNo,
                          const TargetLibraryInfo &TLI, bool Signed = true) {
  Attribute::AttrKind ExtAttr = TLI.getExtAttrForI32Param(Signed);
  if (ExtAttr != Attribute::None && !F.hasParamAttribute(ArgNo, ExtAttr))
    F.addParamAttr(ArgNo, ExtAttr);
}

static void setRetExtAttr(Function &F,
                          const TargetLibraryInfo &TLI, bool Signed = true) {
  Attribute::AttrKind ExtAttr = TLI.getExtAttrForI32Return(Signed);
  if (ExtAttr != Attribute::None && !F.hasRetAttribute(ExtAttr))
    F.addRetAttr(ExtAttr);
}

// Modeled after X86TargetLowering::markLibCallAttributes.
void llvm::markRegisterParameterAttributes(Function *F) {
  if (!F->arg_size() || F->isVarArg())
    return;

  const CallingConv::ID CC = F->getCallingConv();
  if (CC != CallingConv::C && CC != CallingConv::X86_StdCall)
    return;

  const Module *M = F->getParent();
  unsigned N = M->getNumberRegisterParameters();
  if (!N)
    return;

  const DataLayout &DL = M->getDataLayout();

  for (Argument &A : F->args()) {
    Type *T = A.getType();
    if (!T->isIntOrPtrTy())
      continue;

    const TypeSize &TS = DL.getTypeAllocSize(T);
    if (TS > 8)
      continue;

    assert(TS <= 4 && "Need to account for parameters larger than word size");
    const unsigned NumRegs = TS > 4 ? 2 : 1;
    if (N < NumRegs)
      return;

    N -= NumRegs;
    F->addParamAttr(A.getArgNo(), Attribute::InReg);
  }
}

FunctionCallee llvm::getOrInsertLibFunc(Module *M, const TargetLibraryInfo &TLI,
                                        LibFunc TheLibFunc, FunctionType *T,
                                        AttributeList AttributeList) {
  assert(TLI.has(TheLibFunc) &&
         "Creating call to non-existing library function.");
  StringRef Name = TLI.getName(TheLibFunc);
  FunctionCallee C = M->getOrInsertFunction(Name, T, AttributeList);

  // Make sure any mandatory argument attributes are added.

  // Any outgoing i32 argument should be handled with setArgExtAttr() which
  // will add an extension attribute if the target ABI requires it. Adding
  // argument extensions is typically done by the front end but when an
  // optimizer is building a library call on its own it has to take care of
  // this. Each such generated function must be handled here with sign or
  // zero extensions as needed.  F is retreived with cast<> because we demand
  // of the caller to have called isLibFuncEmittable() first.
  Function *F = cast<Function>(C.getCallee());
  assert(F->getFunctionType() == T && "Function type does not match.");
  switch (TheLibFunc) {
  case LibFunc_fputc:
  case LibFunc_putchar:
    setArgExtAttr(*F, 0, TLI);
    break;
  case LibFunc_ldexp:
  case LibFunc_ldexpf:
  case LibFunc_ldexpl:
  case LibFunc_memchr:
  case LibFunc_memrchr:
  case LibFunc_strchr:
    setArgExtAttr(*F, 1, TLI);
    break;
  case LibFunc_memccpy:
    setArgExtAttr(*F, 2, TLI);
    break;

    // These are functions that are known to not need any argument extension
    // on any target: A size_t argument (which may be an i32 on some targets)
    // should not trigger the assert below.
  case LibFunc_bcmp:
    setRetExtAttr(*F, TLI);
    break;
  case LibFunc_calloc:
  case LibFunc_fwrite:
  case LibFunc_malloc:
  case LibFunc_memcmp:
  case LibFunc_memcpy_chk:
  case LibFunc_mempcpy:
  case LibFunc_memset_pattern16:
  case LibFunc_snprintf:
  case LibFunc_stpncpy:
  case LibFunc_strlcat:
  case LibFunc_strlcpy:
  case LibFunc_strncat:
  case LibFunc_strncmp:
  case LibFunc_strncpy:
  case LibFunc_vsnprintf:
    break;

  default:
#ifndef NDEBUG
    for (unsigned i = 0; i < T->getNumParams(); i++)
      assert(!isa<IntegerType>(T->getParamType(i)) &&
             "Unhandled integer argument.");
#endif
    break;
  }

  markRegisterParameterAttributes(F);

  return C;
}

FunctionCallee llvm::getOrInsertLibFunc(Module *M, const TargetLibraryInfo &TLI,
                                        LibFunc TheLibFunc, FunctionType *T) {
  return getOrInsertLibFunc(M, TLI, TheLibFunc, T, AttributeList());
}

bool llvm::isLibFuncEmittable(const Module *M, const TargetLibraryInfo *TLI,
                              LibFunc TheLibFunc) {
  StringRef FuncName = TLI->getName(TheLibFunc);
  if (!TLI->has(TheLibFunc))
    return false;

  // Check if the Module already has a GlobalValue with the same name, in
  // which case it must be a Function with the expected type.
  if (GlobalValue *GV = M->getNamedValue(FuncName)) {
    if (auto *F = dyn_cast<Function>(GV))
      return TLI->isValidProtoForLibFunc(*F->getFunctionType(), TheLibFunc, *M);
    return false;
  }

  return true;
}

bool llvm::isLibFuncEmittable(const Module *M, const TargetLibraryInfo *TLI,
                              StringRef Name) {
  LibFunc TheLibFunc;
  return TLI->getLibFunc(Name, TheLibFunc) &&
         isLibFuncEmittable(M, TLI, TheLibFunc);
}

bool llvm::hasFloatFn(const Module *M, const TargetLibraryInfo *TLI, Type *Ty,
                      LibFunc DoubleFn, LibFunc FloatFn, LibFunc LongDoubleFn) {
  switch (Ty->getTypeID()) {
  case Type::HalfTyID:
    return false;
  case Type::FloatTyID:
    return isLibFuncEmittable(M, TLI, FloatFn);
  case Type::DoubleTyID:
    return isLibFuncEmittable(M, TLI, DoubleFn);
  default:
    return isLibFuncEmittable(M, TLI, LongDoubleFn);
  }
}

StringRef llvm::getFloatFn(const Module *M, const TargetLibraryInfo *TLI,
                           Type *Ty, LibFunc DoubleFn, LibFunc FloatFn,
                           LibFunc LongDoubleFn, LibFunc &TheLibFunc) {
  assert(hasFloatFn(M, TLI, Ty, DoubleFn, FloatFn, LongDoubleFn) &&
         "Cannot get name for unavailable function!");

  switch (Ty->getTypeID()) {
  case Type::HalfTyID:
    llvm_unreachable("No name for HalfTy!");
  case Type::FloatTyID:
    TheLibFunc = FloatFn;
    return TLI->getName(FloatFn);
  case Type::DoubleTyID:
    TheLibFunc = DoubleFn;
    return TLI->getName(DoubleFn);
  default:
    TheLibFunc = LongDoubleFn;
    return TLI->getName(LongDoubleFn);
  }
}

//- Emit LibCalls ------------------------------------------------------------//

static IntegerType *getIntTy(IRBuilderBase &B, const TargetLibraryInfo *TLI) {
  return B.getIntNTy(TLI->getIntSize());
}

static IntegerType *getSizeTTy(IRBuilderBase &B, const TargetLibraryInfo *TLI) {
  const Module *M = B.GetInsertBlock()->getModule();
  return B.getIntNTy(TLI->getSizeTSize(*M));
}

static Value *emitLibCall(LibFunc TheLibFunc, Type *ReturnType,
                          ArrayRef<Type *> ParamTypes,
                          ArrayRef<Value *> Operands, IRBuilderBase &B,
                          const TargetLibraryInfo *TLI,
                          bool IsVaArgs = false) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, TheLibFunc))
    return nullptr;

  StringRef FuncName = TLI->getName(TheLibFunc);
  FunctionType *FuncType = FunctionType::get(ReturnType, ParamTypes, IsVaArgs);
  FunctionCallee Callee = getOrInsertLibFunc(M, *TLI, TheLibFunc, FuncType);
  inferNonMandatoryLibFuncAttrs(M, FuncName, *TLI);
  CallInst *CI = B.CreateCall(Callee, Operands, FuncName);
  if (const Function *F =
          dyn_cast<Function>(Callee.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());
  return CI;
}

Value *llvm::emitStrLen(Value *Ptr, IRBuilderBase &B, const DataLayout &DL,
                        const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_strlen, SizeTTy, CharPtrTy, Ptr, B, TLI);
}

Value *llvm::emitStrDup(Value *Ptr, IRBuilderBase &B,
                        const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  return emitLibCall(LibFunc_strdup, CharPtrTy, CharPtrTy, Ptr, B, TLI);
}

Value *llvm::emitStrChr(Value *Ptr, char C, IRBuilderBase &B,
                        const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  return emitLibCall(LibFunc_strchr, CharPtrTy, {CharPtrTy, IntTy},
                     {Ptr, ConstantInt::get(IntTy, C)}, B, TLI);
}

Value *llvm::emitStrNCmp(Value *Ptr1, Value *Ptr2, Value *Len, IRBuilderBase &B,
                         const DataLayout &DL, const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(
      LibFunc_strncmp, IntTy,
      {CharPtrTy, CharPtrTy, SizeTTy},
      {Ptr1, Ptr2, Len}, B, TLI);
}

Value *llvm::emitStrCpy(Value *Dst, Value *Src, IRBuilderBase &B,
                        const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = Dst->getType();
  return emitLibCall(LibFunc_strcpy, CharPtrTy, {CharPtrTy, CharPtrTy},
                     {Dst, Src}, B, TLI);
}

Value *llvm::emitStpCpy(Value *Dst, Value *Src, IRBuilderBase &B,
                        const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  return emitLibCall(LibFunc_stpcpy, CharPtrTy, {CharPtrTy, CharPtrTy},
                     {Dst, Src}, B, TLI);
}

Value *llvm::emitStrNCpy(Value *Dst, Value *Src, Value *Len, IRBuilderBase &B,
                         const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_strncpy, CharPtrTy, {CharPtrTy, CharPtrTy, SizeTTy},
                     {Dst, Src, Len}, B, TLI);
}

Value *llvm::emitStpNCpy(Value *Dst, Value *Src, Value *Len, IRBuilderBase &B,
                         const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_stpncpy, CharPtrTy, {CharPtrTy, CharPtrTy, SizeTTy},
                     {Dst, Src, Len}, B, TLI);
}

Value *llvm::emitMemCpyChk(Value *Dst, Value *Src, Value *Len, Value *ObjSize,
                           IRBuilderBase &B, const DataLayout &DL,
                           const TargetLibraryInfo *TLI) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, LibFunc_memcpy_chk))
    return nullptr;

  AttributeList AS;
  AS = AttributeList::get(M->getContext(), AttributeList::FunctionIndex,
                          Attribute::NoUnwind);
  Type *VoidPtrTy = B.getPtrTy();
  Type *SizeTTy = getSizeTTy(B, TLI);
  FunctionCallee MemCpy = getOrInsertLibFunc(M, *TLI, LibFunc_memcpy_chk,
      AttributeList::get(M->getContext(), AS), VoidPtrTy,
      VoidPtrTy, VoidPtrTy, SizeTTy, SizeTTy);
  CallInst *CI = B.CreateCall(MemCpy, {Dst, Src, Len, ObjSize});
  if (const Function *F =
          dyn_cast<Function>(MemCpy.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());
  return CI;
}

Value *llvm::emitMemPCpy(Value *Dst, Value *Src, Value *Len, IRBuilderBase &B,
                         const DataLayout &DL, const TargetLibraryInfo *TLI) {
  Type *VoidPtrTy = B.getPtrTy();
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_mempcpy, VoidPtrTy,
                     {VoidPtrTy, VoidPtrTy, SizeTTy},
                     {Dst, Src, Len}, B, TLI);
}

Value *llvm::emitMemChr(Value *Ptr, Value *Val, Value *Len, IRBuilderBase &B,
                        const DataLayout &DL, const TargetLibraryInfo *TLI) {
  Type *VoidPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_memchr, VoidPtrTy,
                     {VoidPtrTy, IntTy, SizeTTy},
                     {Ptr, Val, Len}, B, TLI);
}

Value *llvm::emitMemRChr(Value *Ptr, Value *Val, Value *Len, IRBuilderBase &B,
                        const DataLayout &DL, const TargetLibraryInfo *TLI) {
  Type *VoidPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_memrchr, VoidPtrTy,
                     {VoidPtrTy, IntTy, SizeTTy},
                     {Ptr, Val, Len}, B, TLI);
}

Value *llvm::emitMemCmp(Value *Ptr1, Value *Ptr2, Value *Len, IRBuilderBase &B,
                        const DataLayout &DL, const TargetLibraryInfo *TLI) {
  Type *VoidPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_memcmp, IntTy,
                     {VoidPtrTy, VoidPtrTy, SizeTTy},
                     {Ptr1, Ptr2, Len}, B, TLI);
}

Value *llvm::emitBCmp(Value *Ptr1, Value *Ptr2, Value *Len, IRBuilderBase &B,
                      const DataLayout &DL, const TargetLibraryInfo *TLI) {
  Type *VoidPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_bcmp, IntTy,
                     {VoidPtrTy, VoidPtrTy, SizeTTy},
                     {Ptr1, Ptr2, Len}, B, TLI);
}

Value *llvm::emitMemCCpy(Value *Ptr1, Value *Ptr2, Value *Val, Value *Len,
                         IRBuilderBase &B, const TargetLibraryInfo *TLI) {
  Type *VoidPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_memccpy, VoidPtrTy,
                     {VoidPtrTy, VoidPtrTy, IntTy, SizeTTy},
                     {Ptr1, Ptr2, Val, Len}, B, TLI);
}

Value *llvm::emitSNPrintf(Value *Dest, Value *Size, Value *Fmt,
                          ArrayRef<Value *> VariadicArgs, IRBuilderBase &B,
                          const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  Type *SizeTTy = getSizeTTy(B, TLI);
  SmallVector<Value *, 8> Args{Dest, Size, Fmt};
  llvm::append_range(Args, VariadicArgs);
  return emitLibCall(LibFunc_snprintf, IntTy,
                     {CharPtrTy, SizeTTy, CharPtrTy},
                     Args, B, TLI, /*IsVaArgs=*/true);
}

Value *llvm::emitSPrintf(Value *Dest, Value *Fmt,
                         ArrayRef<Value *> VariadicArgs, IRBuilderBase &B,
                         const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  SmallVector<Value *, 8> Args{Dest, Fmt};
  llvm::append_range(Args, VariadicArgs);
  return emitLibCall(LibFunc_sprintf, IntTy,
                     {CharPtrTy, CharPtrTy}, Args, B, TLI,
                     /*IsVaArgs=*/true);
}

Value *llvm::emitStrCat(Value *Dest, Value *Src, IRBuilderBase &B,
                        const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  return emitLibCall(LibFunc_strcat, CharPtrTy,
                     {CharPtrTy, CharPtrTy},
                     {Dest, Src}, B, TLI);
}

Value *llvm::emitStrLCpy(Value *Dest, Value *Src, Value *Size, IRBuilderBase &B,
                         const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_strlcpy, SizeTTy,
                     {CharPtrTy, CharPtrTy, SizeTTy},
                     {Dest, Src, Size}, B, TLI);
}

Value *llvm::emitStrLCat(Value *Dest, Value *Src, Value *Size, IRBuilderBase &B,
                         const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_strlcat, SizeTTy,
                     {CharPtrTy, CharPtrTy, SizeTTy},
                     {Dest, Src, Size}, B, TLI);
}

Value *llvm::emitStrNCat(Value *Dest, Value *Src, Value *Size, IRBuilderBase &B,
                         const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(LibFunc_strncat, CharPtrTy,
                     {CharPtrTy, CharPtrTy, SizeTTy},
                     {Dest, Src, Size}, B, TLI);
}

Value *llvm::emitVSNPrintf(Value *Dest, Value *Size, Value *Fmt, Value *VAList,
                           IRBuilderBase &B, const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  Type *SizeTTy = getSizeTTy(B, TLI);
  return emitLibCall(
      LibFunc_vsnprintf, IntTy,
      {CharPtrTy, SizeTTy, CharPtrTy, VAList->getType()},
      {Dest, Size, Fmt, VAList}, B, TLI);
}

Value *llvm::emitVSPrintf(Value *Dest, Value *Fmt, Value *VAList,
                          IRBuilderBase &B, const TargetLibraryInfo *TLI) {
  Type *CharPtrTy = B.getPtrTy();
  Type *IntTy = getIntTy(B, TLI);
  return emitLibCall(LibFunc_vsprintf, IntTy,
                     {CharPtrTy, CharPtrTy, VAList->getType()},
                     {Dest, Fmt, VAList}, B, TLI);
}

/// Append a suffix to the function name according to the type of 'Op'.
static void appendTypeSuffix(Value *Op, StringRef &Name,
                             SmallString<20> &NameBuffer) {
  if (!Op->getType()->isDoubleTy()) {
      NameBuffer += Name;

    if (Op->getType()->isFloatTy())
      NameBuffer += 'f';
    else
      NameBuffer += 'l';

    Name = NameBuffer;
  }
}

static Value *emitUnaryFloatFnCallHelper(Value *Op, LibFunc TheLibFunc,
                                         StringRef Name, IRBuilderBase &B,
                                         const AttributeList &Attrs,
                                         const TargetLibraryInfo *TLI) {
  assert((Name != "") && "Must specify Name to emitUnaryFloatFnCall");

  Module *M = B.GetInsertBlock()->getModule();
  FunctionCallee Callee = getOrInsertLibFunc(M, *TLI, TheLibFunc, Op->getType(),
                                             Op->getType());
  CallInst *CI = B.CreateCall(Callee, Op, Name);

  // The incoming attribute set may have come from a speculatable intrinsic, but
  // is being replaced with a library call which is not allowed to be
  // speculatable.
  CI->setAttributes(
      Attrs.removeFnAttribute(B.getContext(), Attribute::Speculatable));
  if (const Function *F =
          dyn_cast<Function>(Callee.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());

  return CI;
}

Value *llvm::emitUnaryFloatFnCall(Value *Op, const TargetLibraryInfo *TLI,
                                  StringRef Name, IRBuilderBase &B,
                                  const AttributeList &Attrs) {
  SmallString<20> NameBuffer;
  appendTypeSuffix(Op, Name, NameBuffer);

  LibFunc TheLibFunc;
  TLI->getLibFunc(Name, TheLibFunc);

  return emitUnaryFloatFnCallHelper(Op, TheLibFunc, Name, B, Attrs, TLI);
}

Value *llvm::emitUnaryFloatFnCall(Value *Op, const TargetLibraryInfo *TLI,
                                  LibFunc DoubleFn, LibFunc FloatFn,
                                  LibFunc LongDoubleFn, IRBuilderBase &B,
                                  const AttributeList &Attrs) {
  // Get the name of the function according to TLI.
  Module *M = B.GetInsertBlock()->getModule();
  LibFunc TheLibFunc;
  StringRef Name = getFloatFn(M, TLI, Op->getType(), DoubleFn, FloatFn,
                              LongDoubleFn, TheLibFunc);

  return emitUnaryFloatFnCallHelper(Op, TheLibFunc, Name, B, Attrs, TLI);
}

static Value *emitBinaryFloatFnCallHelper(Value *Op1, Value *Op2,
                                          LibFunc TheLibFunc,
                                          StringRef Name, IRBuilderBase &B,
                                          const AttributeList &Attrs,
                                          const TargetLibraryInfo *TLI) {
  assert((Name != "") && "Must specify Name to emitBinaryFloatFnCall");

  Module *M = B.GetInsertBlock()->getModule();
  FunctionCallee Callee = getOrInsertLibFunc(M, *TLI, TheLibFunc, Op1->getType(),
                                             Op1->getType(), Op2->getType());
  inferNonMandatoryLibFuncAttrs(M, Name, *TLI);
  CallInst *CI = B.CreateCall(Callee, { Op1, Op2 }, Name);

  // The incoming attribute set may have come from a speculatable intrinsic, but
  // is being replaced with a library call which is not allowed to be
  // speculatable.
  CI->setAttributes(
      Attrs.removeFnAttribute(B.getContext(), Attribute::Speculatable));
  if (const Function *F =
          dyn_cast<Function>(Callee.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());

  return CI;
}

Value *llvm::emitBinaryFloatFnCall(Value *Op1, Value *Op2,
                                   const TargetLibraryInfo *TLI,
                                   StringRef Name, IRBuilderBase &B,
                                   const AttributeList &Attrs) {
  assert((Name != "") && "Must specify Name to emitBinaryFloatFnCall");

  SmallString<20> NameBuffer;
  appendTypeSuffix(Op1, Name, NameBuffer);

  LibFunc TheLibFunc;
  TLI->getLibFunc(Name, TheLibFunc);

  return emitBinaryFloatFnCallHelper(Op1, Op2, TheLibFunc, Name, B, Attrs, TLI);
}

Value *llvm::emitBinaryFloatFnCall(Value *Op1, Value *Op2,
                                   const TargetLibraryInfo *TLI,
                                   LibFunc DoubleFn, LibFunc FloatFn,
                                   LibFunc LongDoubleFn, IRBuilderBase &B,
                                   const AttributeList &Attrs) {
  // Get the name of the function according to TLI.
  Module *M = B.GetInsertBlock()->getModule();
  LibFunc TheLibFunc;
  StringRef Name = getFloatFn(M, TLI, Op1->getType(), DoubleFn, FloatFn,
                              LongDoubleFn, TheLibFunc);

  return emitBinaryFloatFnCallHelper(Op1, Op2, TheLibFunc, Name, B, Attrs, TLI);
}

// Emit a call to putchar(int) with Char as the argument.  Char must have
// the same precision as int, which need not be 32 bits.
Value *llvm::emitPutChar(Value *Char, IRBuilderBase &B,
                         const TargetLibraryInfo *TLI) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, LibFunc_putchar))
    return nullptr;

  Type *IntTy = getIntTy(B, TLI);
  StringRef PutCharName = TLI->getName(LibFunc_putchar);
  FunctionCallee PutChar = getOrInsertLibFunc(M, *TLI, LibFunc_putchar,
                                              IntTy, IntTy);
  inferNonMandatoryLibFuncAttrs(M, PutCharName, *TLI);
  CallInst *CI = B.CreateCall(PutChar, Char, PutCharName);

  if (const Function *F =
          dyn_cast<Function>(PutChar.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());
  return CI;
}

Value *llvm::emitPutS(Value *Str, IRBuilderBase &B,
                      const TargetLibraryInfo *TLI) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, LibFunc_puts))
    return nullptr;

  Type *IntTy = getIntTy(B, TLI);
  StringRef PutsName = TLI->getName(LibFunc_puts);
  FunctionCallee PutS = getOrInsertLibFunc(M, *TLI, LibFunc_puts, IntTy,
                                           B.getPtrTy());
  inferNonMandatoryLibFuncAttrs(M, PutsName, *TLI);
  CallInst *CI = B.CreateCall(PutS, Str, PutsName);
  if (const Function *F =
          dyn_cast<Function>(PutS.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());
  return CI;
}

Value *llvm::emitFPutC(Value *Char, Value *File, IRBuilderBase &B,
                       const TargetLibraryInfo *TLI) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, LibFunc_fputc))
    return nullptr;

  Type *IntTy = getIntTy(B, TLI);
  StringRef FPutcName = TLI->getName(LibFunc_fputc);
  FunctionCallee F = getOrInsertLibFunc(M, *TLI, LibFunc_fputc, IntTy,
                                        IntTy, File->getType());
  if (File->getType()->isPointerTy())
    inferNonMandatoryLibFuncAttrs(M, FPutcName, *TLI);
  CallInst *CI = B.CreateCall(F, {Char, File}, FPutcName);

  if (const Function *Fn =
          dyn_cast<Function>(F.getCallee()->stripPointerCasts()))
    CI->setCallingConv(Fn->getCallingConv());
  return CI;
}

Value *llvm::emitFPutS(Value *Str, Value *File, IRBuilderBase &B,
                       const TargetLibraryInfo *TLI) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, LibFunc_fputs))
    return nullptr;

  Type *IntTy = getIntTy(B, TLI);
  StringRef FPutsName = TLI->getName(LibFunc_fputs);
  FunctionCallee F = getOrInsertLibFunc(M, *TLI, LibFunc_fputs, IntTy,
                                        B.getPtrTy(), File->getType());
  if (File->getType()->isPointerTy())
    inferNonMandatoryLibFuncAttrs(M, FPutsName, *TLI);
  CallInst *CI = B.CreateCall(F, {Str, File}, FPutsName);

  if (const Function *Fn =
          dyn_cast<Function>(F.getCallee()->stripPointerCasts()))
    CI->setCallingConv(Fn->getCallingConv());
  return CI;
}

Value *llvm::emitFWrite(Value *Ptr, Value *Size, Value *File, IRBuilderBase &B,
                        const DataLayout &DL, const TargetLibraryInfo *TLI) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, LibFunc_fwrite))
    return nullptr;

  Type *SizeTTy = getSizeTTy(B, TLI);
  StringRef FWriteName = TLI->getName(LibFunc_fwrite);
  FunctionCallee F = getOrInsertLibFunc(M, *TLI, LibFunc_fwrite,
                                        SizeTTy, B.getPtrTy(), SizeTTy,
                                        SizeTTy, File->getType());

  if (File->getType()->isPointerTy())
    inferNonMandatoryLibFuncAttrs(M, FWriteName, *TLI);
  CallInst *CI =
      B.CreateCall(F, {Ptr, Size,
                       ConstantInt::get(SizeTTy, 1), File});

  if (const Function *Fn =
          dyn_cast<Function>(F.getCallee()->stripPointerCasts()))
    CI->setCallingConv(Fn->getCallingConv());
  return CI;
}

Value *llvm::emitMalloc(Value *Num, IRBuilderBase &B, const DataLayout &DL,
                        const TargetLibraryInfo *TLI) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, LibFunc_malloc))
    return nullptr;

  StringRef MallocName = TLI->getName(LibFunc_malloc);
  Type *SizeTTy = getSizeTTy(B, TLI);
  FunctionCallee Malloc = getOrInsertLibFunc(M, *TLI, LibFunc_malloc,
                                             B.getPtrTy(), SizeTTy);
  inferNonMandatoryLibFuncAttrs(M, MallocName, *TLI);
  CallInst *CI = B.CreateCall(Malloc, Num, MallocName);

  if (const Function *F =
          dyn_cast<Function>(Malloc.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());

  return CI;
}

Value *llvm::emitCalloc(Value *Num, Value *Size, IRBuilderBase &B,
                        const TargetLibraryInfo &TLI) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, &TLI, LibFunc_calloc))
    return nullptr;

  StringRef CallocName = TLI.getName(LibFunc_calloc);
  Type *SizeTTy = getSizeTTy(B, &TLI);
  FunctionCallee Calloc = getOrInsertLibFunc(M, TLI, LibFunc_calloc,
                                             B.getPtrTy(), SizeTTy, SizeTTy);
  inferNonMandatoryLibFuncAttrs(M, CallocName, TLI);
  CallInst *CI = B.CreateCall(Calloc, {Num, Size}, CallocName);

  if (const auto *F =
          dyn_cast<Function>(Calloc.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());

  return CI;
}

Value *llvm::emitHotColdNew(Value *Num, IRBuilderBase &B,
                            const TargetLibraryInfo *TLI, LibFunc NewFunc,
                            uint8_t HotCold) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, NewFunc))
    return nullptr;

  StringRef Name = TLI->getName(NewFunc);
  FunctionCallee Func = M->getOrInsertFunction(Name, B.getPtrTy(),
                                               Num->getType(), B.getInt8Ty());
  inferNonMandatoryLibFuncAttrs(M, Name, *TLI);
  CallInst *CI = B.CreateCall(Func, {Num, B.getInt8(HotCold)}, Name);

  if (const Function *F =
          dyn_cast<Function>(Func.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());

  return CI;
}

Value *llvm::emitHotColdNewNoThrow(Value *Num, Value *NoThrow, IRBuilderBase &B,
                                   const TargetLibraryInfo *TLI,
                                   LibFunc NewFunc, uint8_t HotCold) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, NewFunc))
    return nullptr;

  StringRef Name = TLI->getName(NewFunc);
  FunctionCallee Func =
      M->getOrInsertFunction(Name, B.getPtrTy(), Num->getType(),
                             NoThrow->getType(), B.getInt8Ty());
  inferNonMandatoryLibFuncAttrs(M, Name, *TLI);
  CallInst *CI = B.CreateCall(Func, {Num, NoThrow, B.getInt8(HotCold)}, Name);

  if (const Function *F =
          dyn_cast<Function>(Func.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());

  return CI;
}

Value *llvm::emitHotColdNewAligned(Value *Num, Value *Align, IRBuilderBase &B,
                                   const TargetLibraryInfo *TLI,
                                   LibFunc NewFunc, uint8_t HotCold) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, NewFunc))
    return nullptr;

  StringRef Name = TLI->getName(NewFunc);
  FunctionCallee Func = M->getOrInsertFunction(
      Name, B.getPtrTy(), Num->getType(), Align->getType(), B.getInt8Ty());
  inferNonMandatoryLibFuncAttrs(M, Name, *TLI);
  CallInst *CI = B.CreateCall(Func, {Num, Align, B.getInt8(HotCold)}, Name);

  if (const Function *F =
          dyn_cast<Function>(Func.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());

  return CI;
}

Value *llvm::emitHotColdNewAlignedNoThrow(Value *Num, Value *Align,
                                          Value *NoThrow, IRBuilderBase &B,
                                          const TargetLibraryInfo *TLI,
                                          LibFunc NewFunc, uint8_t HotCold) {
  Module *M = B.GetInsertBlock()->getModule();
  if (!isLibFuncEmittable(M, TLI, NewFunc))
    return nullptr;

  StringRef Name = TLI->getName(NewFunc);
  FunctionCallee Func = M->getOrInsertFunction(
      Name, B.getPtrTy(), Num->getType(), Align->getType(),
      NoThrow->getType(), B.getInt8Ty());
  inferNonMandatoryLibFuncAttrs(M, Name, *TLI);
  CallInst *CI =
      B.CreateCall(Func, {Num, Align, NoThrow, B.getInt8(HotCold)}, Name);

  if (const Function *F =
          dyn_cast<Function>(Func.getCallee()->stripPointerCasts()))
    CI->setCallingConv(F->getCallingConv());

  return CI;
}
