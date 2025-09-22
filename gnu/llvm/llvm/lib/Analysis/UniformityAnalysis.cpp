//===- UniformityAnalysis.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/ADT/GenericUniformityImpl.h"
#include "llvm/Analysis/CycleAnalysis.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

template <>
bool llvm::GenericUniformityAnalysisImpl<SSAContext>::hasDivergentDefs(
    const Instruction &I) const {
  return isDivergent((const Value *)&I);
}

template <>
bool llvm::GenericUniformityAnalysisImpl<SSAContext>::markDefsDivergent(
    const Instruction &Instr) {
  return markDivergent(cast<Value>(&Instr));
}

template <> void llvm::GenericUniformityAnalysisImpl<SSAContext>::initialize() {
  for (auto &I : instructions(F)) {
    if (TTI->isSourceOfDivergence(&I))
      markDivergent(I);
    else if (TTI->isAlwaysUniform(&I))
      addUniformOverride(I);
  }
  for (auto &Arg : F.args()) {
    if (TTI->isSourceOfDivergence(&Arg)) {
      markDivergent(&Arg);
    }
  }
}

template <>
void llvm::GenericUniformityAnalysisImpl<SSAContext>::pushUsers(
    const Value *V) {
  for (const auto *User : V->users()) {
    if (const auto *UserInstr = dyn_cast<const Instruction>(User)) {
      markDivergent(*UserInstr);
    }
  }
}

template <>
void llvm::GenericUniformityAnalysisImpl<SSAContext>::pushUsers(
    const Instruction &Instr) {
  assert(!isAlwaysUniform(Instr));
  if (Instr.isTerminator())
    return;
  pushUsers(cast<Value>(&Instr));
}

template <>
bool llvm::GenericUniformityAnalysisImpl<SSAContext>::usesValueFromCycle(
    const Instruction &I, const Cycle &DefCycle) const {
  assert(!isAlwaysUniform(I));
  for (const Use &U : I.operands()) {
    if (auto *I = dyn_cast<Instruction>(&U)) {
      if (DefCycle.contains(I->getParent()))
        return true;
    }
  }
  return false;
}

template <>
void llvm::GenericUniformityAnalysisImpl<
    SSAContext>::propagateTemporalDivergence(const Instruction &I,
                                             const Cycle &DefCycle) {
  if (isDivergent(I))
    return;
  for (auto *User : I.users()) {
    auto *UserInstr = cast<Instruction>(User);
    if (DefCycle.contains(UserInstr->getParent()))
      continue;
    markDivergent(*UserInstr);
  }
}

template <>
bool llvm::GenericUniformityAnalysisImpl<SSAContext>::isDivergentUse(
    const Use &U) const {
  const auto *V = U.get();
  if (isDivergent(V))
    return true;
  if (const auto *DefInstr = dyn_cast<Instruction>(V)) {
    const auto *UseInstr = cast<Instruction>(U.getUser());
    return isTemporalDivergent(*UseInstr->getParent(), *DefInstr);
  }
  return false;
}

// This ensures explicit instantiation of
// GenericUniformityAnalysisImpl::ImplDeleter::operator()
template class llvm::GenericUniformityInfo<SSAContext>;
template struct llvm::GenericUniformityAnalysisImplDeleter<
    llvm::GenericUniformityAnalysisImpl<SSAContext>>;

//===----------------------------------------------------------------------===//
//  UniformityInfoAnalysis and related pass implementations
//===----------------------------------------------------------------------===//

llvm::UniformityInfo UniformityInfoAnalysis::run(Function &F,
                                                 FunctionAnalysisManager &FAM) {
  auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  auto &TTI = FAM.getResult<TargetIRAnalysis>(F);
  auto &CI = FAM.getResult<CycleAnalysis>(F);
  UniformityInfo UI{DT, CI, &TTI};
  // Skip computation if we can assume everything is uniform.
  if (TTI.hasBranchDivergence(&F))
    UI.compute();

  return UI;
}

AnalysisKey UniformityInfoAnalysis::Key;

UniformityInfoPrinterPass::UniformityInfoPrinterPass(raw_ostream &OS)
    : OS(OS) {}

PreservedAnalyses UniformityInfoPrinterPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  OS << "UniformityInfo for function '" << F.getName() << "':\n";
  AM.getResult<UniformityInfoAnalysis>(F).print(OS);

  return PreservedAnalyses::all();
}

//===----------------------------------------------------------------------===//
//  UniformityInfoWrapperPass Implementation
//===----------------------------------------------------------------------===//

char UniformityInfoWrapperPass::ID = 0;

UniformityInfoWrapperPass::UniformityInfoWrapperPass() : FunctionPass(ID) {
  initializeUniformityInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS_BEGIN(UniformityInfoWrapperPass, "uniformity",
                      "Uniformity Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(CycleInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(UniformityInfoWrapperPass, "uniformity",
                    "Uniformity Analysis", true, true)

void UniformityInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<CycleInfoWrapperPass>();
  AU.addRequired<TargetTransformInfoWrapperPass>();
}

bool UniformityInfoWrapperPass::runOnFunction(Function &F) {
  auto &cycleInfo = getAnalysis<CycleInfoWrapperPass>().getResult();
  auto &domTree = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &targetTransformInfo =
      getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);

  m_function = &F;
  m_uniformityInfo = UniformityInfo{domTree, cycleInfo, &targetTransformInfo};

  // Skip computation if we can assume everything is uniform.
  if (targetTransformInfo.hasBranchDivergence(m_function))
    m_uniformityInfo.compute();

  return false;
}

void UniformityInfoWrapperPass::print(raw_ostream &OS, const Module *) const {
  OS << "UniformityInfo for function '" << m_function->getName() << "':\n";
}

void UniformityInfoWrapperPass::releaseMemory() {
  m_uniformityInfo = UniformityInfo{};
  m_function = nullptr;
}
