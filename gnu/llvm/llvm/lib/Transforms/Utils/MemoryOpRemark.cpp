//===-- MemoryOpRemark.cpp - Auto-init remark analysis---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the analysis for the "auto-init" remark.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/MemoryOpRemark.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include <optional>

using namespace llvm;
using namespace llvm::ore;

MemoryOpRemark::~MemoryOpRemark() = default;

bool MemoryOpRemark::canHandle(const Instruction *I, const TargetLibraryInfo &TLI) {
  if (isa<StoreInst>(I))
    return true;

  if (auto *II = dyn_cast<IntrinsicInst>(I)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::memcpy_inline:
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memset:
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
    case Intrinsic::memset_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }

  if (auto *CI = dyn_cast<CallInst>(I)) {
    auto *CF = CI->getCalledFunction();
    if (!CF)
      return false;

    if (!CF->hasName())
      return false;

    LibFunc LF;
    bool KnownLibCall = TLI.getLibFunc(*CF, LF) && TLI.has(LF);
    if (!KnownLibCall)
      return false;

    switch (LF) {
    case LibFunc_memcpy_chk:
    case LibFunc_mempcpy_chk:
    case LibFunc_memset_chk:
    case LibFunc_memmove_chk:
    case LibFunc_memcpy:
    case LibFunc_mempcpy:
    case LibFunc_memset:
    case LibFunc_memmove:
    case LibFunc_bzero:
    case LibFunc_bcopy:
      return true;
    default:
      return false;
    }
  }

  return false;
}

void MemoryOpRemark::visit(const Instruction *I) {
  // For some of them, we can provide more information:

  // For stores:
  // * size
  // * volatile / atomic
  if (auto *SI = dyn_cast<StoreInst>(I)) {
    visitStore(*SI);
    return;
  }

  // For intrinsics:
  // * user-friendly name
  // * size
  if (auto *II = dyn_cast<IntrinsicInst>(I)) {
    visitIntrinsicCall(*II);
    return;
  }

  // For calls:
  // * known/unknown function (e.g. the compiler knows bzero, but it doesn't
  //                                know my_bzero)
  // * memory operation size
  if (auto *CI = dyn_cast<CallInst>(I)) {
    visitCall(*CI);
    return;
  }

  visitUnknown(*I);
}

std::string MemoryOpRemark::explainSource(StringRef Type) const {
  return (Type + ".").str();
}

StringRef MemoryOpRemark::remarkName(RemarkKind RK) const {
  switch (RK) {
  case RK_Store:
    return "MemoryOpStore";
  case RK_Unknown:
    return "MemoryOpUnknown";
  case RK_IntrinsicCall:
    return "MemoryOpIntrinsicCall";
  case RK_Call:
    return "MemoryOpCall";
  }
  llvm_unreachable("missing RemarkKind case");
}

static void inlineVolatileOrAtomicWithExtraArgs(bool *Inline, bool Volatile,
                                                bool Atomic,
                                                DiagnosticInfoIROptimization &R) {
  if (Inline && *Inline)
    R << " Inlined: " << NV("StoreInlined", true) << ".";
  if (Volatile)
    R << " Volatile: " << NV("StoreVolatile", true) << ".";
  if (Atomic)
    R << " Atomic: " << NV("StoreAtomic", true) << ".";
  // Emit the false cases under ExtraArgs. This won't show them in the remark
  // message but will end up in the serialized remarks.
  if ((Inline && !*Inline) || !Volatile || !Atomic)
    R << setExtraArgs();
  if (Inline && !*Inline)
    R << " Inlined: " << NV("StoreInlined", false) << ".";
  if (!Volatile)
    R << " Volatile: " << NV("StoreVolatile", false) << ".";
  if (!Atomic)
    R << " Atomic: " << NV("StoreAtomic", false) << ".";
}

static std::optional<uint64_t>
getSizeInBytes(std::optional<uint64_t> SizeInBits) {
  if (!SizeInBits || *SizeInBits % 8 != 0)
    return std::nullopt;
  return *SizeInBits / 8;
}

template<typename ...Ts>
std::unique_ptr<DiagnosticInfoIROptimization>
MemoryOpRemark::makeRemark(Ts... Args) {
  switch (diagnosticKind()) {
  case DK_OptimizationRemarkAnalysis:
    return std::make_unique<OptimizationRemarkAnalysis>(Args...);
  case DK_OptimizationRemarkMissed:
    return std::make_unique<OptimizationRemarkMissed>(Args...);
  default:
    llvm_unreachable("unexpected DiagnosticKind");
  }
}

void MemoryOpRemark::visitStore(const StoreInst &SI) {
  bool Volatile = SI.isVolatile();
  bool Atomic = SI.isAtomic();
  int64_t Size = DL.getTypeStoreSize(SI.getOperand(0)->getType());

  auto R = makeRemark(RemarkPass.data(), remarkName(RK_Store), &SI);
  *R << explainSource("Store") << "\nStore size: " << NV("StoreSize", Size)
     << " bytes.";
  visitPtr(SI.getOperand(1), /*IsRead=*/false, *R);
  inlineVolatileOrAtomicWithExtraArgs(nullptr, Volatile, Atomic, *R);
  ORE.emit(*R);
}

void MemoryOpRemark::visitUnknown(const Instruction &I) {
  auto R = makeRemark(RemarkPass.data(), remarkName(RK_Unknown), &I);
  *R << explainSource("Initialization");
  ORE.emit(*R);
}

void MemoryOpRemark::visitIntrinsicCall(const IntrinsicInst &II) {
  SmallString<32> CallTo;
  bool Atomic = false;
  bool Inline = false;
  switch (II.getIntrinsicID()) {
  case Intrinsic::memcpy_inline:
    CallTo = "memcpy";
    Inline = true;
    break;
  case Intrinsic::memcpy:
    CallTo = "memcpy";
    break;
  case Intrinsic::memmove:
    CallTo = "memmove";
    break;
  case Intrinsic::memset:
    CallTo = "memset";
    break;
  case Intrinsic::memcpy_element_unordered_atomic:
    CallTo = "memcpy";
    Atomic = true;
    break;
  case Intrinsic::memmove_element_unordered_atomic:
    CallTo = "memmove";
    Atomic = true;
    break;
  case Intrinsic::memset_element_unordered_atomic:
    CallTo = "memset";
    Atomic = true;
    break;
  default:
    return visitUnknown(II);
  }

  auto R = makeRemark(RemarkPass.data(), remarkName(RK_IntrinsicCall), &II);
  visitCallee(CallTo.str(), /*KnownLibCall=*/true, *R);
  visitSizeOperand(II.getOperand(2), *R);

  auto *CIVolatile = dyn_cast<ConstantInt>(II.getOperand(3));
  // No such thing as a memory intrinsic that is both atomic and volatile.
  bool Volatile = !Atomic && CIVolatile && CIVolatile->getZExtValue();
  switch (II.getIntrinsicID()) {
  case Intrinsic::memcpy_inline:
  case Intrinsic::memcpy:
  case Intrinsic::memmove:
  case Intrinsic::memcpy_element_unordered_atomic:
    visitPtr(II.getOperand(1), /*IsRead=*/true, *R);
    visitPtr(II.getOperand(0), /*IsRead=*/false, *R);
    break;
  case Intrinsic::memset:
  case Intrinsic::memset_element_unordered_atomic:
    visitPtr(II.getOperand(0), /*IsRead=*/false, *R);
    break;
  }
  inlineVolatileOrAtomicWithExtraArgs(&Inline, Volatile, Atomic, *R);
  ORE.emit(*R);
}

void MemoryOpRemark::visitCall(const CallInst &CI) {
  Function *F = CI.getCalledFunction();
  if (!F)
    return visitUnknown(CI);

  LibFunc LF;
  bool KnownLibCall = TLI.getLibFunc(*F, LF) && TLI.has(LF);
  auto R = makeRemark(RemarkPass.data(), remarkName(RK_Call), &CI);
  visitCallee(F, KnownLibCall, *R);
  visitKnownLibCall(CI, LF, *R);
  ORE.emit(*R);
}

template <typename FTy>
void MemoryOpRemark::visitCallee(FTy F, bool KnownLibCall,
                                 DiagnosticInfoIROptimization &R) {
  R << "Call to ";
  if (!KnownLibCall)
    R << NV("UnknownLibCall", "unknown") << " function ";
  R << NV("Callee", F) << explainSource("");
}

void MemoryOpRemark::visitKnownLibCall(const CallInst &CI, LibFunc LF,
                                       DiagnosticInfoIROptimization &R) {
  switch (LF) {
  default:
    return;
  case LibFunc_memset_chk:
  case LibFunc_memset:
    visitSizeOperand(CI.getOperand(2), R);
    visitPtr(CI.getOperand(0), /*IsRead=*/false, R);
    break;
  case LibFunc_bzero:
    visitSizeOperand(CI.getOperand(1), R);
    visitPtr(CI.getOperand(0), /*IsRead=*/false, R);
    break;
  case LibFunc_memcpy_chk:
  case LibFunc_mempcpy_chk:
  case LibFunc_memmove_chk:
  case LibFunc_memcpy:
  case LibFunc_mempcpy:
  case LibFunc_memmove:
  case LibFunc_bcopy:
    visitSizeOperand(CI.getOperand(2), R);
    visitPtr(CI.getOperand(1), /*IsRead=*/true, R);
    visitPtr(CI.getOperand(0), /*IsRead=*/false, R);
    break;
  }
}

void MemoryOpRemark::visitSizeOperand(Value *V, DiagnosticInfoIROptimization &R) {
  if (auto *Len = dyn_cast<ConstantInt>(V)) {
    uint64_t Size = Len->getZExtValue();
    R << " Memory operation size: " << NV("StoreSize", Size) << " bytes.";
  }
}

static std::optional<StringRef> nameOrNone(const Value *V) {
  if (V->hasName())
    return V->getName();
  return std::nullopt;
}

void MemoryOpRemark::visitVariable(const Value *V,
                                   SmallVectorImpl<VariableInfo> &Result) {
  if (auto *GV = dyn_cast<GlobalVariable>(V)) {
    auto *Ty = GV->getValueType();
    uint64_t Size = DL.getTypeSizeInBits(Ty).getFixedValue();
    VariableInfo Var{nameOrNone(GV), Size};
    if (!Var.isEmpty())
      Result.push_back(std::move(Var));
    return;
  }

  // If we find some information in the debug info, take that.
  bool FoundDI = false;
  // Try to get an llvm.dbg.declare, which has a DILocalVariable giving us the
  // real debug info name and size of the variable.
  auto FindDI = [&](const auto *DVI) {
    if (DILocalVariable *DILV = DVI->getVariable()) {
      std::optional<uint64_t> DISize = getSizeInBytes(DILV->getSizeInBits());
      VariableInfo Var{DILV->getName(), DISize};
      if (!Var.isEmpty()) {
        Result.push_back(std::move(Var));
        FoundDI = true;
      }
    }
  };
  for_each(findDbgDeclares(const_cast<Value *>(V)), FindDI);
  for_each(findDVRDeclares(const_cast<Value *>(V)), FindDI);

  if (FoundDI) {
    assert(!Result.empty());
    return;
  }

  const auto *AI = dyn_cast<AllocaInst>(V);
  if (!AI)
    return;

  // If not, get it from the alloca.
  std::optional<TypeSize> TySize = AI->getAllocationSize(DL);
  std::optional<uint64_t> Size =
      TySize ? std::optional(TySize->getFixedValue()) : std::nullopt;
  VariableInfo Var{nameOrNone(AI), Size};
  if (!Var.isEmpty())
    Result.push_back(std::move(Var));
}

void MemoryOpRemark::visitPtr(Value *Ptr, bool IsRead, DiagnosticInfoIROptimization &R) {
  // Find if Ptr is a known variable we can give more information on.
  SmallVector<Value *, 2> Objects;
  getUnderlyingObjectsForCodeGen(Ptr, Objects);
  SmallVector<VariableInfo, 2> VIs;
  for (const Value *V : Objects)
    visitVariable(V, VIs);

  if (VIs.empty()) {
    bool CanBeNull;
    bool CanBeFreed;
    uint64_t Size = Ptr->getPointerDereferenceableBytes(DL, CanBeNull, CanBeFreed);
    if (!Size)
      return;
    VIs.push_back({std::nullopt, Size});
  }

  R << (IsRead ? "\n Read Variables: " : "\n Written Variables: ");
  for (unsigned i = 0; i < VIs.size(); ++i) {
    const VariableInfo &VI = VIs[i];
    assert(!VI.isEmpty() && "No extra content to display.");
    if (i != 0)
      R << ", ";
    if (VI.Name)
      R << NV(IsRead ? "RVarName" : "WVarName", *VI.Name);
    else
      R << NV(IsRead ? "RVarName" : "WVarName", "<unknown>");
    if (VI.Size)
      R << " (" << NV(IsRead ? "RVarSize" : "WVarSize", *VI.Size) << " bytes)";
  }
  R << ".";
}

bool AutoInitRemark::canHandle(const Instruction *I) {
  if (!I->hasMetadata(LLVMContext::MD_annotation))
    return false;
  return any_of(I->getMetadata(LLVMContext::MD_annotation)->operands(),
                [](const MDOperand &Op) {
                  return isa<MDString>(Op.get()) &&
                         cast<MDString>(Op.get())->getString() == "auto-init";
                });
}

std::string AutoInitRemark::explainSource(StringRef Type) const {
  return (Type + " inserted by -ftrivial-auto-var-init.").str();
}

StringRef AutoInitRemark::remarkName(RemarkKind RK) const {
  switch (RK) {
  case RK_Store:
    return "AutoInitStore";
  case RK_Unknown:
    return "AutoInitUnknownInstruction";
  case RK_IntrinsicCall:
    return "AutoInitIntrinsicCall";
  case RK_Call:
    return "AutoInitCall";
  }
  llvm_unreachable("missing RemarkKind case");
}
