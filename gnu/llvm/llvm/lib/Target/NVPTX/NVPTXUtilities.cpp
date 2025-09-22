//===- NVPTXUtilities.cpp - Utility Functions -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains miscellaneous utility functions
//
//===----------------------------------------------------------------------===//

#include "NVPTXUtilities.h"
#include "NVPTX.h"
#include "NVPTXTargetMachine.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Mutex.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

namespace {
typedef std::map<std::string, std::vector<unsigned> > key_val_pair_t;
typedef std::map<const GlobalValue *, key_val_pair_t> global_val_annot_t;

struct AnnotationCache {
  sys::Mutex Lock;
  std::map<const Module *, global_val_annot_t> Cache;
};

AnnotationCache &getAnnotationCache() {
  static AnnotationCache AC;
  return AC;
}
} // anonymous namespace

void clearAnnotationCache(const Module *Mod) {
  auto &AC = getAnnotationCache();
  std::lock_guard<sys::Mutex> Guard(AC.Lock);
  AC.Cache.erase(Mod);
}

static void readIntVecFromMDNode(const MDNode *MetadataNode,
                                 std::vector<unsigned> &Vec) {
  for (unsigned i = 0, e = MetadataNode->getNumOperands(); i != e; ++i) {
    ConstantInt *Val =
        mdconst::extract<ConstantInt>(MetadataNode->getOperand(i));
    Vec.push_back(Val->getZExtValue());
  }
}

static void cacheAnnotationFromMD(const MDNode *MetadataNode,
                                  key_val_pair_t &retval) {
  auto &AC = getAnnotationCache();
  std::lock_guard<sys::Mutex> Guard(AC.Lock);
  assert(MetadataNode && "Invalid mdnode for annotation");
  assert((MetadataNode->getNumOperands() % 2) == 1 &&
         "Invalid number of operands");
  // start index = 1, to skip the global variable key
  // increment = 2, to skip the value for each property-value pairs
  for (unsigned i = 1, e = MetadataNode->getNumOperands(); i != e; i += 2) {
    // property
    const MDString *prop = dyn_cast<MDString>(MetadataNode->getOperand(i));
    assert(prop && "Annotation property not a string");
    std::string Key = prop->getString().str();

    // value
    if (ConstantInt *Val = mdconst::dyn_extract<ConstantInt>(
            MetadataNode->getOperand(i + 1))) {
      retval[Key].push_back(Val->getZExtValue());
    } else if (MDNode *VecMd =
                   dyn_cast<MDNode>(MetadataNode->getOperand(i + 1))) {
      // note: only "grid_constant" annotations support vector MDNodes.
      // assert: there can only exist one unique key value pair of
      // the form (string key, MDNode node). Operands of such a node
      // shall always be unsigned ints.
      if (retval.find(Key) == retval.end()) {
        readIntVecFromMDNode(VecMd, retval[Key]);
        continue;
      }
    } else {
      llvm_unreachable("Value operand not a constant int or an mdnode");
    }
  }
}

static void cacheAnnotationFromMD(const Module *m, const GlobalValue *gv) {
  auto &AC = getAnnotationCache();
  std::lock_guard<sys::Mutex> Guard(AC.Lock);
  NamedMDNode *NMD = m->getNamedMetadata("nvvm.annotations");
  if (!NMD)
    return;
  key_val_pair_t tmp;
  for (unsigned i = 0, e = NMD->getNumOperands(); i != e; ++i) {
    const MDNode *elem = NMD->getOperand(i);

    GlobalValue *entity =
        mdconst::dyn_extract_or_null<GlobalValue>(elem->getOperand(0));
    // entity may be null due to DCE
    if (!entity)
      continue;
    if (entity != gv)
      continue;

    // accumulate annotations for entity in tmp
    cacheAnnotationFromMD(elem, tmp);
  }

  if (tmp.empty()) // no annotations for this gv
    return;

  if (AC.Cache.find(m) != AC.Cache.end())
    AC.Cache[m][gv] = std::move(tmp);
  else {
    global_val_annot_t tmp1;
    tmp1[gv] = std::move(tmp);
    AC.Cache[m] = std::move(tmp1);
  }
}

bool findOneNVVMAnnotation(const GlobalValue *gv, const std::string &prop,
                           unsigned &retval) {
  auto &AC = getAnnotationCache();
  std::lock_guard<sys::Mutex> Guard(AC.Lock);
  const Module *m = gv->getParent();
  if (AC.Cache.find(m) == AC.Cache.end())
    cacheAnnotationFromMD(m, gv);
  else if (AC.Cache[m].find(gv) == AC.Cache[m].end())
    cacheAnnotationFromMD(m, gv);
  if (AC.Cache[m][gv].find(prop) == AC.Cache[m][gv].end())
    return false;
  retval = AC.Cache[m][gv][prop][0];
  return true;
}

static std::optional<unsigned>
findOneNVVMAnnotation(const GlobalValue &GV, const std::string &PropName) {
  unsigned RetVal;
  if (findOneNVVMAnnotation(&GV, PropName, RetVal))
    return RetVal;
  return std::nullopt;
}

bool findAllNVVMAnnotation(const GlobalValue *gv, const std::string &prop,
                           std::vector<unsigned> &retval) {
  auto &AC = getAnnotationCache();
  std::lock_guard<sys::Mutex> Guard(AC.Lock);
  const Module *m = gv->getParent();
  if (AC.Cache.find(m) == AC.Cache.end())
    cacheAnnotationFromMD(m, gv);
  else if (AC.Cache[m].find(gv) == AC.Cache[m].end())
    cacheAnnotationFromMD(m, gv);
  if (AC.Cache[m][gv].find(prop) == AC.Cache[m][gv].end())
    return false;
  retval = AC.Cache[m][gv][prop];
  return true;
}

bool isTexture(const Value &val) {
  if (const GlobalValue *gv = dyn_cast<GlobalValue>(&val)) {
    unsigned Annot;
    if (findOneNVVMAnnotation(gv, "texture", Annot)) {
      assert((Annot == 1) && "Unexpected annotation on a texture symbol");
      return true;
    }
  }
  return false;
}

bool isSurface(const Value &val) {
  if (const GlobalValue *gv = dyn_cast<GlobalValue>(&val)) {
    unsigned Annot;
    if (findOneNVVMAnnotation(gv, "surface", Annot)) {
      assert((Annot == 1) && "Unexpected annotation on a surface symbol");
      return true;
    }
  }
  return false;
}

static bool argHasNVVMAnnotation(const Value &Val,
                                 const std::string &Annotation,
                                 const bool StartArgIndexAtOne = false) {
  if (const Argument *Arg = dyn_cast<Argument>(&Val)) {
    const Function *Func = Arg->getParent();
    std::vector<unsigned> Annot;
    if (findAllNVVMAnnotation(Func, Annotation, Annot)) {
      const unsigned BaseOffset = StartArgIndexAtOne ? 1 : 0;
      if (is_contained(Annot, BaseOffset + Arg->getArgNo())) {
        return true;
      }
    }
  }
  return false;
}

bool isParamGridConstant(const Value &V) {
  if (const Argument *Arg = dyn_cast<Argument>(&V)) {
    // "grid_constant" counts argument indices starting from 1
    if (Arg->hasByValAttr() &&
        argHasNVVMAnnotation(*Arg, "grid_constant",
                             /*StartArgIndexAtOne*/ true)) {
      assert(isKernelFunction(*Arg->getParent()) &&
             "only kernel arguments can be grid_constant");
      return true;
    }
  }
  return false;
}

bool isSampler(const Value &val) {
  const char *AnnotationName = "sampler";

  if (const GlobalValue *gv = dyn_cast<GlobalValue>(&val)) {
    unsigned Annot;
    if (findOneNVVMAnnotation(gv, AnnotationName, Annot)) {
      assert((Annot == 1) && "Unexpected annotation on a sampler symbol");
      return true;
    }
  }
  return argHasNVVMAnnotation(val, AnnotationName);
}

bool isImageReadOnly(const Value &val) {
  return argHasNVVMAnnotation(val, "rdoimage");
}

bool isImageWriteOnly(const Value &val) {
  return argHasNVVMAnnotation(val, "wroimage");
}

bool isImageReadWrite(const Value &val) {
  return argHasNVVMAnnotation(val, "rdwrimage");
}

bool isImage(const Value &val) {
  return isImageReadOnly(val) || isImageWriteOnly(val) || isImageReadWrite(val);
}

bool isManaged(const Value &val) {
  if(const GlobalValue *gv = dyn_cast<GlobalValue>(&val)) {
    unsigned Annot;
    if (findOneNVVMAnnotation(gv, "managed", Annot)) {
      assert((Annot == 1) && "Unexpected annotation on a managed symbol");
      return true;
    }
  }
  return false;
}

std::string getTextureName(const Value &val) {
  assert(val.hasName() && "Found texture variable with no name");
  return std::string(val.getName());
}

std::string getSurfaceName(const Value &val) {
  assert(val.hasName() && "Found surface variable with no name");
  return std::string(val.getName());
}

std::string getSamplerName(const Value &val) {
  assert(val.hasName() && "Found sampler variable with no name");
  return std::string(val.getName());
}

std::optional<unsigned> getMaxNTIDx(const Function &F) {
  return findOneNVVMAnnotation(F, "maxntidx");
}

std::optional<unsigned> getMaxNTIDy(const Function &F) {
  return findOneNVVMAnnotation(F, "maxntidy");
}

std::optional<unsigned> getMaxNTIDz(const Function &F) {
  return findOneNVVMAnnotation(F, "maxntidz");
}

std::optional<unsigned> getMaxNTID(const Function &F) {
  // Note: The semantics here are a bit strange. The PTX ISA states the
  // following (11.4.2. Performance-Tuning Directives: .maxntid):
  //
  //  Note that this directive guarantees that the total number of threads does
  //  not exceed the maximum, but does not guarantee that the limit in any
  //  particular dimension is not exceeded.
  std::optional<unsigned> MaxNTIDx = getMaxNTIDx(F);
  std::optional<unsigned> MaxNTIDy = getMaxNTIDy(F);
  std::optional<unsigned> MaxNTIDz = getMaxNTIDz(F);
  if (MaxNTIDx || MaxNTIDy || MaxNTIDz)
    return MaxNTIDx.value_or(1) * MaxNTIDy.value_or(1) * MaxNTIDz.value_or(1);
  return std::nullopt;
}

bool getMaxClusterRank(const Function &F, unsigned &x) {
  return findOneNVVMAnnotation(&F, "maxclusterrank", x);
}

std::optional<unsigned> getReqNTIDx(const Function &F) {
  return findOneNVVMAnnotation(F, "reqntidx");
}

std::optional<unsigned> getReqNTIDy(const Function &F) {
  return findOneNVVMAnnotation(F, "reqntidy");
}

std::optional<unsigned> getReqNTIDz(const Function &F) {
  return findOneNVVMAnnotation(F, "reqntidz");
}

std::optional<unsigned> getReqNTID(const Function &F) {
  // Note: The semantics here are a bit strange. See getMaxNTID.
  std::optional<unsigned> ReqNTIDx = getReqNTIDx(F);
  std::optional<unsigned> ReqNTIDy = getReqNTIDy(F);
  std::optional<unsigned> ReqNTIDz = getReqNTIDz(F);
  if (ReqNTIDx || ReqNTIDy || ReqNTIDz)
    return ReqNTIDx.value_or(1) * ReqNTIDy.value_or(1) * ReqNTIDz.value_or(1);
  return std::nullopt;
}

bool getMinCTASm(const Function &F, unsigned &x) {
  return findOneNVVMAnnotation(&F, "minctasm", x);
}

bool getMaxNReg(const Function &F, unsigned &x) {
  return findOneNVVMAnnotation(&F, "maxnreg", x);
}

bool isKernelFunction(const Function &F) {
  unsigned x = 0;
  if (!findOneNVVMAnnotation(&F, "kernel", x)) {
    // There is no NVVM metadata, check the calling convention
    return F.getCallingConv() == CallingConv::PTX_Kernel;
  }
  return (x == 1);
}

MaybeAlign getAlign(const Function &F, unsigned Index) {
  // First check the alignstack metadata
  if (MaybeAlign StackAlign =
          F.getAttributes().getAttributes(Index).getStackAlignment())
    return StackAlign;

  // If that is missing, check the legacy nvvm metadata
  std::vector<unsigned> Vs;
  bool retval = findAllNVVMAnnotation(&F, "align", Vs);
  if (!retval)
    return std::nullopt;
  for (unsigned V : Vs)
    if ((V >> 16) == Index)
      return Align(V & 0xFFFF);

  return std::nullopt;
}

MaybeAlign getAlign(const CallInst &I, unsigned Index) {
  // First check the alignstack metadata
  if (MaybeAlign StackAlign =
          I.getAttributes().getAttributes(Index).getStackAlignment())
    return StackAlign;

  // If that is missing, check the legacy nvvm metadata
  if (MDNode *alignNode = I.getMetadata("callalign")) {
    for (int i = 0, n = alignNode->getNumOperands(); i < n; i++) {
      if (const ConstantInt *CI =
              mdconst::dyn_extract<ConstantInt>(alignNode->getOperand(i))) {
        unsigned V = CI->getZExtValue();
        if ((V >> 16) == Index)
          return Align(V & 0xFFFF);
        if ((V >> 16) > Index)
          return std::nullopt;
      }
    }
  }
  return std::nullopt;
}

Function *getMaybeBitcastedCallee(const CallBase *CB) {
  return dyn_cast<Function>(CB->getCalledOperand()->stripPointerCasts());
}

bool shouldEmitPTXNoReturn(const Value *V, const TargetMachine &TM) {
  const auto &ST =
      *static_cast<const NVPTXTargetMachine &>(TM).getSubtargetImpl();
  if (!ST.hasNoReturn())
    return false;

  assert((isa<Function>(V) || isa<CallInst>(V)) &&
         "Expect either a call instruction or a function");

  if (const CallInst *CallI = dyn_cast<CallInst>(V))
    return CallI->doesNotReturn() &&
           CallI->getFunctionType()->getReturnType()->isVoidTy();

  const Function *F = cast<Function>(V);
  return F->doesNotReturn() &&
         F->getFunctionType()->getReturnType()->isVoidTy() &&
         !isKernelFunction(*F);
}

bool Isv2x16VT(EVT VT) {
  return (VT == MVT::v2f16 || VT == MVT::v2bf16 || VT == MVT::v2i16);
}

} // namespace llvm
