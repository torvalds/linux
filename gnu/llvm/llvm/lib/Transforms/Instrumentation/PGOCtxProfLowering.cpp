//===- PGOCtxProfLowering.cpp - Contextual PGO Instr. Lowering ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//

#include "llvm/Transforms/Instrumentation/PGOCtxProfLowering.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "ctx-instr-lower"

static cl::list<std::string> ContextRoots(
    "profile-context-root", cl::Hidden,
    cl::desc(
        "A function name, assumed to be global, which will be treated as the "
        "root of an interesting graph, which will be profiled independently "
        "from other similar graphs."));

bool PGOCtxProfLoweringPass::isContextualIRPGOEnabled() {
  return !ContextRoots.empty();
}

// the names of symbols we expect in compiler-rt. Using a namespace for
// readability.
namespace CompilerRtAPINames {
static auto StartCtx = "__llvm_ctx_profile_start_context";
static auto ReleaseCtx = "__llvm_ctx_profile_release_context";
static auto GetCtx = "__llvm_ctx_profile_get_context";
static auto ExpectedCalleeTLS = "__llvm_ctx_profile_expected_callee";
static auto CallsiteTLS = "__llvm_ctx_profile_callsite";
} // namespace CompilerRtAPINames

namespace {
// The lowering logic and state.
class CtxInstrumentationLowerer final {
  Module &M;
  ModuleAnalysisManager &MAM;
  Type *ContextNodeTy = nullptr;
  Type *ContextRootTy = nullptr;

  DenseMap<const Function *, Constant *> ContextRootMap;
  Function *StartCtx = nullptr;
  Function *GetCtx = nullptr;
  Function *ReleaseCtx = nullptr;
  GlobalVariable *ExpectedCalleeTLS = nullptr;
  GlobalVariable *CallsiteInfoTLS = nullptr;

public:
  CtxInstrumentationLowerer(Module &M, ModuleAnalysisManager &MAM);
  // return true if lowering happened (i.e. a change was made)
  bool lowerFunction(Function &F);
};

// llvm.instrprof.increment[.step] captures the total number of counters as one
// of its parameters, and llvm.instrprof.callsite captures the total number of
// callsites. Those values are the same for instances of those intrinsics in
// this function. Find the first instance of each and return them.
std::pair<uint32_t, uint32_t> getNrCountersAndCallsites(const Function &F) {
  uint32_t NrCounters = 0;
  uint32_t NrCallsites = 0;
  for (const auto &BB : F) {
    for (const auto &I : BB) {
      if (const auto *Incr = dyn_cast<InstrProfIncrementInst>(&I)) {
        uint32_t V =
            static_cast<uint32_t>(Incr->getNumCounters()->getZExtValue());
        assert((!NrCounters || V == NrCounters) &&
               "expected all llvm.instrprof.increment[.step] intrinsics to "
               "have the same total nr of counters parameter");
        NrCounters = V;
      } else if (const auto *CSIntr = dyn_cast<InstrProfCallsite>(&I)) {
        uint32_t V =
            static_cast<uint32_t>(CSIntr->getNumCounters()->getZExtValue());
        assert((!NrCallsites || V == NrCallsites) &&
               "expected all llvm.instrprof.callsite intrinsics to have the "
               "same total nr of callsites parameter");
        NrCallsites = V;
      }
#if NDEBUG
      if (NrCounters && NrCallsites)
        return std::make_pair(NrCounters, NrCallsites);
#endif
    }
  }
  return {NrCounters, NrCallsites};
}
} // namespace

// set up tie-in with compiler-rt.
// NOTE!!!
// These have to match compiler-rt/lib/ctx_profile/CtxInstrProfiling.h
CtxInstrumentationLowerer::CtxInstrumentationLowerer(Module &M,
                                                     ModuleAnalysisManager &MAM)
    : M(M), MAM(MAM) {
  auto *PointerTy = PointerType::get(M.getContext(), 0);
  auto *SanitizerMutexType = Type::getInt8Ty(M.getContext());
  auto *I32Ty = Type::getInt32Ty(M.getContext());
  auto *I64Ty = Type::getInt64Ty(M.getContext());

  // The ContextRoot type
  ContextRootTy =
      StructType::get(M.getContext(), {
                                          PointerTy,          /*FirstNode*/
                                          PointerTy,          /*FirstMemBlock*/
                                          PointerTy,          /*CurrentMem*/
                                          SanitizerMutexType, /*Taken*/
                                      });
  // The Context header.
  ContextNodeTy = StructType::get(M.getContext(), {
                                                      I64Ty,     /*Guid*/
                                                      PointerTy, /*Next*/
                                                      I32Ty,     /*NrCounters*/
                                                      I32Ty,     /*NrCallsites*/
                                                  });

  // Define a global for each entrypoint. We'll reuse the entrypoint's name as
  // prefix. We assume the entrypoint names to be unique.
  for (const auto &Fname : ContextRoots) {
    if (const auto *F = M.getFunction(Fname)) {
      if (F->isDeclaration())
        continue;
      auto *G = M.getOrInsertGlobal(Fname + "_ctx_root", ContextRootTy);
      cast<GlobalVariable>(G)->setInitializer(
          Constant::getNullValue(ContextRootTy));
      ContextRootMap.insert(std::make_pair(F, G));
      for (const auto &BB : *F)
        for (const auto &I : BB)
          if (const auto *CB = dyn_cast<CallBase>(&I))
            if (CB->isMustTailCall()) {
              M.getContext().emitError(
                  "The function " + Fname +
                  " was indicated as a context root, but it features musttail "
                  "calls, which is not supported.");
            }
    }
  }

  // Declare the functions we will call.
  StartCtx = cast<Function>(
      M.getOrInsertFunction(
           CompilerRtAPINames::StartCtx,
           FunctionType::get(ContextNodeTy->getPointerTo(),
                             {ContextRootTy->getPointerTo(), /*ContextRoot*/
                              I64Ty, /*Guid*/ I32Ty,
                              /*NrCounters*/ I32Ty /*NrCallsites*/},
                             false))
          .getCallee());
  GetCtx = cast<Function>(
      M.getOrInsertFunction(CompilerRtAPINames::GetCtx,
                            FunctionType::get(ContextNodeTy->getPointerTo(),
                                              {PointerTy, /*Callee*/
                                               I64Ty,     /*Guid*/
                                               I32Ty,     /*NrCounters*/
                                               I32Ty},    /*NrCallsites*/
                                              false))
          .getCallee());
  ReleaseCtx = cast<Function>(
      M.getOrInsertFunction(
           CompilerRtAPINames::ReleaseCtx,
           FunctionType::get(Type::getVoidTy(M.getContext()),
                             {
                                 ContextRootTy->getPointerTo(), /*ContextRoot*/
                             },
                             false))
          .getCallee());

  // Declare the TLSes we will need to use.
  CallsiteInfoTLS =
      new GlobalVariable(M, PointerTy, false, GlobalValue::ExternalLinkage,
                         nullptr, CompilerRtAPINames::CallsiteTLS);
  CallsiteInfoTLS->setThreadLocal(true);
  CallsiteInfoTLS->setVisibility(llvm::GlobalValue::HiddenVisibility);
  ExpectedCalleeTLS =
      new GlobalVariable(M, PointerTy, false, GlobalValue::ExternalLinkage,
                         nullptr, CompilerRtAPINames::ExpectedCalleeTLS);
  ExpectedCalleeTLS->setThreadLocal(true);
  ExpectedCalleeTLS->setVisibility(llvm::GlobalValue::HiddenVisibility);
}

PreservedAnalyses PGOCtxProfLoweringPass::run(Module &M,
                                              ModuleAnalysisManager &MAM) {
  CtxInstrumentationLowerer Lowerer(M, MAM);
  bool Changed = false;
  for (auto &F : M)
    Changed |= Lowerer.lowerFunction(F);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

bool CtxInstrumentationLowerer::lowerFunction(Function &F) {
  if (F.isDeclaration())
    return false;
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);

  Value *Guid = nullptr;
  auto [NrCounters, NrCallsites] = getNrCountersAndCallsites(F);

  Value *Context = nullptr;
  Value *RealContext = nullptr;

  StructType *ThisContextType = nullptr;
  Value *TheRootContext = nullptr;
  Value *ExpectedCalleeTLSAddr = nullptr;
  Value *CallsiteInfoTLSAddr = nullptr;

  auto &Head = F.getEntryBlock();
  for (auto &I : Head) {
    // Find the increment intrinsic in the entry basic block.
    if (auto *Mark = dyn_cast<InstrProfIncrementInst>(&I)) {
      assert(Mark->getIndex()->isZero());

      IRBuilder<> Builder(Mark);
      // FIXME(mtrofin): use InstrProfSymtab::getCanonicalName
      Guid = Builder.getInt64(F.getGUID());
      // The type of the context of this function is now knowable since we have
      // NrCallsites and NrCounters. We delcare it here because it's more
      // convenient - we have the Builder.
      ThisContextType = StructType::get(
          F.getContext(),
          {ContextNodeTy, ArrayType::get(Builder.getInt64Ty(), NrCounters),
           ArrayType::get(Builder.getPtrTy(), NrCallsites)});
      // Figure out which way we obtain the context object for this function -
      // if it's an entrypoint, then we call StartCtx, otherwise GetCtx. In the
      // former case, we also set TheRootContext since we need to release it
      // at the end (plus it can be used to know if we have an entrypoint or a
      // regular function)
      auto Iter = ContextRootMap.find(&F);
      if (Iter != ContextRootMap.end()) {
        TheRootContext = Iter->second;
        Context = Builder.CreateCall(StartCtx, {TheRootContext, Guid,
                                                Builder.getInt32(NrCounters),
                                                Builder.getInt32(NrCallsites)});
        ORE.emit(
            [&] { return OptimizationRemark(DEBUG_TYPE, "Entrypoint", &F); });
      } else {
        Context =
            Builder.CreateCall(GetCtx, {&F, Guid, Builder.getInt32(NrCounters),
                                        Builder.getInt32(NrCallsites)});
        ORE.emit([&] {
          return OptimizationRemark(DEBUG_TYPE, "RegularFunction", &F);
        });
      }
      // The context could be scratch.
      auto *CtxAsInt = Builder.CreatePtrToInt(Context, Builder.getInt64Ty());
      if (NrCallsites > 0) {
        // Figure out which index of the TLS 2-element buffers to use.
        // Scratch context => we use index == 1. Real contexts => index == 0.
        auto *Index = Builder.CreateAnd(CtxAsInt, Builder.getInt64(1));
        // The GEPs corresponding to that index, in the respective TLS.
        ExpectedCalleeTLSAddr = Builder.CreateGEP(
            Builder.getInt8Ty()->getPointerTo(),
            Builder.CreateThreadLocalAddress(ExpectedCalleeTLS), {Index});
        CallsiteInfoTLSAddr = Builder.CreateGEP(
            Builder.getInt32Ty(),
            Builder.CreateThreadLocalAddress(CallsiteInfoTLS), {Index});
      }
      // Because the context pointer may have LSB set (to indicate scratch),
      // clear it for the value we use as base address for the counter vector.
      // This way, if later we want to have "real" (not clobbered) buffers
      // acting as scratch, the lowering (at least this part of it that deals
      // with counters) stays the same.
      RealContext = Builder.CreateIntToPtr(
          Builder.CreateAnd(CtxAsInt, Builder.getInt64(-2)),
          ThisContextType->getPointerTo());
      I.eraseFromParent();
      break;
    }
  }
  if (!Context) {
    ORE.emit([&] {
      return OptimizationRemarkMissed(DEBUG_TYPE, "Skip", &F)
             << "Function doesn't have instrumentation, skipping";
    });
    return false;
  }

  bool ContextWasReleased = false;
  for (auto &BB : F) {
    for (auto &I : llvm::make_early_inc_range(BB)) {
      if (auto *Instr = dyn_cast<InstrProfCntrInstBase>(&I)) {
        IRBuilder<> Builder(Instr);
        switch (Instr->getIntrinsicID()) {
        case llvm::Intrinsic::instrprof_increment:
        case llvm::Intrinsic::instrprof_increment_step: {
          // Increments (or increment-steps) are just a typical load - increment
          // - store in the RealContext.
          auto *AsStep = cast<InstrProfIncrementInst>(Instr);
          auto *GEP = Builder.CreateGEP(
              ThisContextType, RealContext,
              {Builder.getInt32(0), Builder.getInt32(1), AsStep->getIndex()});
          Builder.CreateStore(
              Builder.CreateAdd(Builder.CreateLoad(Builder.getInt64Ty(), GEP),
                                AsStep->getStep()),
              GEP);
        } break;
        case llvm::Intrinsic::instrprof_callsite:
          // callsite lowering: write the called value in the expected callee
          // TLS we treat the TLS as volatile because of signal handlers and to
          // avoid these being moved away from the callsite they decorate.
          auto *CSIntrinsic = dyn_cast<InstrProfCallsite>(Instr);
          Builder.CreateStore(CSIntrinsic->getCallee(), ExpectedCalleeTLSAddr,
                              true);
          // write the GEP of the slot in the sub-contexts portion of the
          // context in TLS. Now, here, we use the actual Context value - as
          // returned from compiler-rt - which may have the LSB set if the
          // Context was scratch. Since the header of the context object and
          // then the values are all 8-aligned (or, really, insofar as we care,
          // they are even) - if the context is scratch (meaning, an odd value),
          // so will the GEP. This is important because this is then visible to
          // compiler-rt which will produce scratch contexts for callers that
          // have a scratch context.
          Builder.CreateStore(
              Builder.CreateGEP(ThisContextType, Context,
                                {Builder.getInt32(0), Builder.getInt32(2),
                                 CSIntrinsic->getIndex()}),
              CallsiteInfoTLSAddr, true);
          break;
        }
        I.eraseFromParent();
      } else if (TheRootContext && isa<ReturnInst>(I)) {
        // Remember to release the context if we are an entrypoint.
        IRBuilder<> Builder(&I);
        Builder.CreateCall(ReleaseCtx, {TheRootContext});
        ContextWasReleased = true;
      }
    }
  }
  // FIXME: This would happen if the entrypoint tailcalls. A way to fix would be
  // to disallow this, (so this then stays as an error), another is to detect
  // that and then do a wrapper or disallow the tail call. This only affects
  // instrumentation, when we want to detect the call graph.
  if (TheRootContext && !ContextWasReleased)
    F.getContext().emitError(
        "[ctx_prof] An entrypoint was instrumented but it has no `ret` "
        "instructions above which to release the context: " +
        F.getName());
  return true;
}
