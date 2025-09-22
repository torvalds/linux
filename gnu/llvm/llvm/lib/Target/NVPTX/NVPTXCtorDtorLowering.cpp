//===-- NVPTXCtorDtorLowering.cpp - Handle global ctors and dtors --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This pass creates a unified init and fini kernel with the required metadata
//===----------------------------------------------------------------------===//

#include "NVPTXCtorDtorLowering.h"
#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "NVPTX.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MD5.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

#define DEBUG_TYPE "nvptx-lower-ctor-dtor"

static cl::opt<std::string>
    GlobalStr("nvptx-lower-global-ctor-dtor-id",
              cl::desc("Override unique ID of ctor/dtor globals."),
              cl::init(""), cl::Hidden);

static cl::opt<bool>
    CreateKernels("nvptx-emit-init-fini-kernel",
                  cl::desc("Emit kernels to call ctor/dtor globals."),
                  cl::init(true), cl::Hidden);

namespace {

static std::string getHash(StringRef Str) {
  llvm::MD5 Hasher;
  llvm::MD5::MD5Result Hash;
  Hasher.update(Str);
  Hasher.final(Hash);
  return llvm::utohexstr(Hash.low(), /*LowerCase=*/true);
}

static void addKernelMetadata(Module &M, GlobalValue *GV) {
  llvm::LLVMContext &Ctx = M.getContext();

  // Get "nvvm.annotations" metadata node.
  llvm::NamedMDNode *MD = M.getOrInsertNamedMetadata("nvvm.annotations");

  llvm::Metadata *KernelMDVals[] = {
      llvm::ConstantAsMetadata::get(GV), llvm::MDString::get(Ctx, "kernel"),
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 1))};

  // This kernel is only to be called single-threaded.
  llvm::Metadata *ThreadXMDVals[] = {
      llvm::ConstantAsMetadata::get(GV), llvm::MDString::get(Ctx, "maxntidx"),
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 1))};
  llvm::Metadata *ThreadYMDVals[] = {
      llvm::ConstantAsMetadata::get(GV), llvm::MDString::get(Ctx, "maxntidy"),
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 1))};
  llvm::Metadata *ThreadZMDVals[] = {
      llvm::ConstantAsMetadata::get(GV), llvm::MDString::get(Ctx, "maxntidz"),
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 1))};

  llvm::Metadata *BlockMDVals[] = {
      llvm::ConstantAsMetadata::get(GV),
      llvm::MDString::get(Ctx, "maxclusterrank"),
      llvm::ConstantAsMetadata::get(
          llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 1))};

  // Append metadata to nvvm.annotations.
  MD->addOperand(llvm::MDNode::get(Ctx, KernelMDVals));
  MD->addOperand(llvm::MDNode::get(Ctx, ThreadXMDVals));
  MD->addOperand(llvm::MDNode::get(Ctx, ThreadYMDVals));
  MD->addOperand(llvm::MDNode::get(Ctx, ThreadZMDVals));
  MD->addOperand(llvm::MDNode::get(Ctx, BlockMDVals));
}

static Function *createInitOrFiniKernelFunction(Module &M, bool IsCtor) {
  StringRef InitOrFiniKernelName =
      IsCtor ? "nvptx$device$init" : "nvptx$device$fini";
  if (M.getFunction(InitOrFiniKernelName))
    return nullptr;

  Function *InitOrFiniKernel = Function::createWithDefaultAttr(
      FunctionType::get(Type::getVoidTy(M.getContext()), false),
      GlobalValue::WeakODRLinkage, 0, InitOrFiniKernelName, &M);
  addKernelMetadata(M, InitOrFiniKernel);

  return InitOrFiniKernel;
}

// We create the IR required to call each callback in this section. This is
// equivalent to the following code. Normally, the linker would provide us with
// the definitions of the init and fini array sections. The 'nvlink' linker does
// not do this so initializing these values is done by the runtime.
//
// extern "C" void **__init_array_start = nullptr;
// extern "C" void **__init_array_end = nullptr;
// extern "C" void **__fini_array_start = nullptr;
// extern "C" void **__fini_array_end = nullptr;
//
// using InitCallback = void();
// using FiniCallback = void();
//
// void call_init_array_callbacks() {
//   for (auto start = __init_array_start; start != __init_array_end; ++start)
//     reinterpret_cast<InitCallback *>(*start)();
// }
//
// void call_init_array_callbacks() {
//   size_t fini_array_size = __fini_array_end - __fini_array_start;
//   for (size_t i = fini_array_size; i > 0; --i)
//     reinterpret_cast<FiniCallback *>(__fini_array_start[i - 1])();
// }
static void createInitOrFiniCalls(Function &F, bool IsCtor) {
  Module &M = *F.getParent();
  LLVMContext &C = M.getContext();

  IRBuilder<> IRB(BasicBlock::Create(C, "entry", &F));
  auto *LoopBB = BasicBlock::Create(C, "while.entry", &F);
  auto *ExitBB = BasicBlock::Create(C, "while.end", &F);
  Type *PtrTy = IRB.getPtrTy(llvm::ADDRESS_SPACE_GLOBAL);

  auto *Begin = M.getOrInsertGlobal(
      IsCtor ? "__init_array_start" : "__fini_array_start",
      PointerType::get(C, 0), [&]() {
        auto *GV = new GlobalVariable(
            M, PointerType::get(C, 0),
            /*isConstant=*/false, GlobalValue::WeakAnyLinkage,
            Constant::getNullValue(PointerType::get(C, 0)),
            IsCtor ? "__init_array_start" : "__fini_array_start",
            /*InsertBefore=*/nullptr, GlobalVariable::NotThreadLocal,
            /*AddressSpace=*/llvm::ADDRESS_SPACE_GLOBAL);
        GV->setVisibility(GlobalVariable::ProtectedVisibility);
        return GV;
      });
  auto *End = M.getOrInsertGlobal(
      IsCtor ? "__init_array_end" : "__fini_array_end", PointerType::get(C, 0),
      [&]() {
        auto *GV = new GlobalVariable(
            M, PointerType::get(C, 0),
            /*isConstant=*/false, GlobalValue::WeakAnyLinkage,
            Constant::getNullValue(PointerType::get(C, 0)),
            IsCtor ? "__init_array_end" : "__fini_array_end",
            /*InsertBefore=*/nullptr, GlobalVariable::NotThreadLocal,
            /*AddressSpace=*/llvm::ADDRESS_SPACE_GLOBAL);
        GV->setVisibility(GlobalVariable::ProtectedVisibility);
        return GV;
      });

  // The constructor type is suppoed to allow using the argument vectors, but
  // for now we just call them with no arguments.
  auto *CallBackTy = FunctionType::get(IRB.getVoidTy(), {});

  // The destructor array must be called in reverse order. Get an expression to
  // the end of the array and iterate backwards in that case.
  Value *BeginVal = IRB.CreateLoad(Begin->getType(), Begin, "begin");
  Value *EndVal = IRB.CreateLoad(Begin->getType(), End, "stop");
  if (!IsCtor) {
    auto *BeginInt = IRB.CreatePtrToInt(BeginVal, IntegerType::getInt64Ty(C));
    auto *EndInt = IRB.CreatePtrToInt(EndVal, IntegerType::getInt64Ty(C));
    auto *SubInst = IRB.CreateSub(EndInt, BeginInt);
    auto *Offset = IRB.CreateAShr(
        SubInst, ConstantInt::get(IntegerType::getInt64Ty(C), 3), "offset",
        /*IsExact=*/true);
    auto *ValuePtr = IRB.CreateGEP(PointerType::get(C, 0), BeginVal,
                                   ArrayRef<Value *>({Offset}));
    EndVal = BeginVal;
    BeginVal = IRB.CreateInBoundsGEP(
        PointerType::get(C, 0), ValuePtr,
        ArrayRef<Value *>(ConstantInt::get(IntegerType::getInt64Ty(C), -1)),
        "start");
  }
  IRB.CreateCondBr(
      IRB.CreateCmp(IsCtor ? ICmpInst::ICMP_NE : ICmpInst::ICMP_UGT, BeginVal,
                    EndVal),
      LoopBB, ExitBB);
  IRB.SetInsertPoint(LoopBB);
  auto *CallBackPHI = IRB.CreatePHI(PtrTy, 2, "ptr");
  auto *CallBack = IRB.CreateLoad(IRB.getPtrTy(F.getAddressSpace()),
                                  CallBackPHI, "callback");
  IRB.CreateCall(CallBackTy, CallBack);
  auto *NewCallBack =
      IRB.CreateConstGEP1_64(PtrTy, CallBackPHI, IsCtor ? 1 : -1, "next");
  auto *EndCmp = IRB.CreateCmp(IsCtor ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_ULT,
                               NewCallBack, EndVal, "end");
  CallBackPHI->addIncoming(BeginVal, &F.getEntryBlock());
  CallBackPHI->addIncoming(NewCallBack, LoopBB);
  IRB.CreateCondBr(EndCmp, ExitBB, LoopBB);
  IRB.SetInsertPoint(ExitBB);
  IRB.CreateRetVoid();
}

static bool createInitOrFiniGlobals(Module &M, GlobalVariable *GV,
                                    bool IsCtor) {
  ConstantArray *GA = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!GA || GA->getNumOperands() == 0)
    return false;

  // NVPTX has no way to emit variables at specific sections or support for
  // the traditional constructor sections. Instead, we emit mangled global
  // names so the runtime can build the list manually.
  for (Value *V : GA->operands()) {
    auto *CS = cast<ConstantStruct>(V);
    auto *F = cast<Constant>(CS->getOperand(1));
    uint64_t Priority = cast<ConstantInt>(CS->getOperand(0))->getSExtValue();
    std::string PriorityStr = "." + std::to_string(Priority);
    // We append a semi-unique hash and the priority to the global name.
    std::string GlobalID =
        !GlobalStr.empty() ? GlobalStr : getHash(M.getSourceFileName());
    std::string NameStr =
        ((IsCtor ? "__init_array_object_" : "__fini_array_object_") +
         F->getName() + "_" + GlobalID + "_" + std::to_string(Priority))
            .str();
    // PTX does not support exported names with '.' in them.
    llvm::transform(NameStr, NameStr.begin(),
                    [](char c) { return c == '.' ? '_' : c; });

    auto *GV = new GlobalVariable(M, F->getType(), /*IsConstant=*/true,
                                  GlobalValue::ExternalLinkage, F, NameStr,
                                  nullptr, GlobalValue::NotThreadLocal,
                                  /*AddressSpace=*/4);
    // This isn't respected by Nvidia, simply put here for clarity.
    GV->setSection(IsCtor ? ".init_array" + PriorityStr
                          : ".fini_array" + PriorityStr);
    GV->setVisibility(GlobalVariable::ProtectedVisibility);
    appendToUsed(M, {GV});
  }

  return true;
}

static bool createInitOrFiniKernel(Module &M, StringRef GlobalName,
                                   bool IsCtor) {
  GlobalVariable *GV = M.getGlobalVariable(GlobalName);
  if (!GV || !GV->hasInitializer())
    return false;

  if (!createInitOrFiniGlobals(M, GV, IsCtor))
    return false;

  if (!CreateKernels)
    return true;

  Function *InitOrFiniKernel = createInitOrFiniKernelFunction(M, IsCtor);
  if (!InitOrFiniKernel)
    return false;

  createInitOrFiniCalls(*InitOrFiniKernel, IsCtor);

  GV->eraseFromParent();
  return true;
}

static bool lowerCtorsAndDtors(Module &M) {
  bool Modified = false;
  Modified |= createInitOrFiniKernel(M, "llvm.global_ctors", /*IsCtor =*/true);
  Modified |= createInitOrFiniKernel(M, "llvm.global_dtors", /*IsCtor =*/false);
  return Modified;
}

class NVPTXCtorDtorLoweringLegacy final : public ModulePass {
public:
  static char ID;
  NVPTXCtorDtorLoweringLegacy() : ModulePass(ID) {}
  bool runOnModule(Module &M) override { return lowerCtorsAndDtors(M); }
};

} // End anonymous namespace

PreservedAnalyses NVPTXCtorDtorLoweringPass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  return lowerCtorsAndDtors(M) ? PreservedAnalyses::none()
                               : PreservedAnalyses::all();
}

char NVPTXCtorDtorLoweringLegacy::ID = 0;
char &llvm::NVPTXCtorDtorLoweringLegacyPassID = NVPTXCtorDtorLoweringLegacy::ID;
INITIALIZE_PASS(NVPTXCtorDtorLoweringLegacy, DEBUG_TYPE,
                "Lower ctors and dtors for NVPTX", false, false)

ModulePass *llvm::createNVPTXCtorDtorLoweringLegacyPass() {
  return new NVPTXCtorDtorLoweringLegacy();
}
