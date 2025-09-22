//===- InlineSizeEstimatorAnalysis.cpp - IR to native size from ML model --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements feature and label extraction for offline supervised learning
// of a IR to native size model.
//
//===----------------------------------------------------------------------===//
#include "llvm/Analysis/InlineSizeEstimatorAnalysis.h"

#ifdef LLVM_HAVE_TFLITE
#include "llvm/Analysis/Utils/TFUtils.h"
#endif
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

AnalysisKey InlineSizeEstimatorAnalysis::Key;

#ifdef LLVM_HAVE_TFLITE
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include <algorithm>
#include <deque>
#include <optional>

cl::opt<std::string> TFIR2NativeModelPath(
    "ml-inliner-ir2native-model", cl::Hidden,
    cl::desc("Path to saved model evaluating native size from IR."));

#define DEBUG_TYPE "inline-size-estimator"
namespace {
unsigned getMaxInstructionID() {
#define LAST_OTHER_INST(NR) return NR;
#include "llvm/IR/Instruction.def"
}

class IRToNativeSizeLearning {
public:
  enum class NamedFeatureIndex : size_t {
    InitialSize,
    Blocks,
    Calls,
    IsLocal,
    IsLinkOnceODR,
    IsLinkOnce,
    Loops,
    MaxLoopDepth,
    MaxDomTreeLevel,

    NumNamedFeatures
  };
  static const size_t NumNamedFeatures =
      static_cast<size_t>(NamedFeatureIndex::NumNamedFeatures);
  struct FunctionFeatures {
    static const size_t FeatureCount;

    std::array<int32_t, NumNamedFeatures> NamedFeatures = {0};
    std::vector<int32_t> InstructionHistogram;
    std::vector<int32_t> InstructionPairHistogram;

    void fillTensor(int32_t *Ptr) const;
    int32_t &operator[](NamedFeatureIndex Pos) {
      return NamedFeatures[static_cast<size_t>(Pos)];
    }
  };
  IRToNativeSizeLearning() = default;

  static FunctionFeatures getFunctionFeatures(Function &F,
                                              FunctionAnalysisManager &FAM);
};

// This is a point in time - we determined including these pairs of
// consecutive instructions (in the IR layout available at inline time) as
// features improves the model performance. We want to move away from manual
// feature selection.
// The array is given in opcode pairs rather than labels because 1) labels
// weren't readily available, and 2) the successions were hand - extracted.
//
// This array must be sorted.
static const std::array<std::pair<size_t, size_t>, 137>
    ImportantInstructionSuccessions{
        {{1, 1},   {1, 4},   {1, 5},   {1, 7},   {1, 8},   {1, 9},   {1, 11},
         {1, 12},  {1, 13},  {1, 14},  {1, 18},  {1, 20},  {1, 22},  {1, 24},
         {1, 25},  {1, 26},  {1, 27},  {1, 28},  {1, 29},  {1, 30},  {1, 31},
         {1, 32},  {1, 33},  {1, 34},  {1, 39},  {1, 40},  {1, 42},  {1, 45},
         {2, 1},   {2, 2},   {2, 13},  {2, 28},  {2, 29},  {2, 32},  {2, 33},
         {2, 34},  {2, 38},  {2, 48},  {2, 49},  {2, 53},  {2, 55},  {2, 56},
         {13, 2},  {13, 13}, {13, 26}, {13, 33}, {13, 34}, {13, 56}, {15, 27},
         {28, 2},  {28, 48}, {28, 53}, {29, 2},  {29, 33}, {29, 56}, {31, 31},
         {31, 33}, {31, 34}, {31, 49}, {32, 1},  {32, 2},  {32, 13}, {32, 15},
         {32, 28}, {32, 29}, {32, 32}, {32, 33}, {32, 34}, {32, 39}, {32, 40},
         {32, 48}, {32, 49}, {32, 53}, {32, 56}, {33, 1},  {33, 2},  {33, 32},
         {33, 33}, {33, 34}, {33, 49}, {33, 53}, {33, 56}, {34, 1},  {34, 2},
         {34, 32}, {34, 33}, {34, 34}, {34, 49}, {34, 53}, {34, 56}, {38, 34},
         {39, 57}, {40, 34}, {47, 15}, {47, 49}, {48, 2},  {48, 34}, {48, 56},
         {49, 1},  {49, 2},  {49, 28}, {49, 32}, {49, 33}, {49, 34}, {49, 39},
         {49, 49}, {49, 56}, {53, 1},  {53, 2},  {53, 28}, {53, 34}, {53, 53},
         {53, 57}, {55, 1},  {55, 28}, {55, 34}, {55, 53}, {55, 55}, {55, 56},
         {56, 1},  {56, 2},  {56, 7},  {56, 13}, {56, 32}, {56, 33}, {56, 34},
         {56, 49}, {56, 53}, {56, 56}, {56, 64}, {57, 34}, {57, 56}, {57, 57},
         {64, 1},  {64, 64}, {65, 1},  {65, 65}}};

// We have: 9 calculated features (the features here); 1 feature for each
// instruction opcode; and 1 feature for each manually-identified sequence.
// For the latter 2, we build a histogram: we count the number of
// occurrences of each instruction opcode or succession of instructions,
// respectively.
// Note that instruction opcodes start from 1. For convenience, we also have an
// always 0 feature for the '0' opcode, hence the extra 1.
const size_t IRToNativeSizeLearning::FunctionFeatures::FeatureCount =
    ImportantInstructionSuccessions.size() + getMaxInstructionID() + 1 +
    IRToNativeSizeLearning::NumNamedFeatures;

size_t getSize(Function &F, TargetTransformInfo &TTI) {
  size_t Ret = 0;
  for (const auto &BB : F)
    for (const auto &I : BB)
      Ret += *(TTI.getInstructionCost(
          &I, TargetTransformInfo::TargetCostKind::TCK_CodeSize).getValue());
  return Ret;
}

size_t getSize(Function &F, FunctionAnalysisManager &FAM) {
  auto &TTI = FAM.getResult<TargetIRAnalysis>(F);
  return getSize(F, TTI);
}

unsigned getMaxDominatorTreeDepth(const Function &F,
                                  const DominatorTree &Tree) {
  unsigned Ret = 0;
  for (const auto &BB : F)
    if (const auto *TN = Tree.getNode(&BB))
      Ret = std::max(Ret, TN->getLevel());
  return Ret;
}
} // namespace

IRToNativeSizeLearning::FunctionFeatures
IRToNativeSizeLearning::getFunctionFeatures(Function &F,
                                            FunctionAnalysisManager &FAM) {
  assert(llvm::is_sorted(ImportantInstructionSuccessions) &&
         "expected function features are sorted");

  auto &DomTree = FAM.getResult<DominatorTreeAnalysis>(F);
  FunctionFeatures FF;
  size_t InstrCount = getMaxInstructionID() + 1;
  FF.InstructionHistogram.resize(InstrCount);

  FF.InstructionPairHistogram.resize(ImportantInstructionSuccessions.size());

  int StartID = 0;
  int LastID = StartID;
  auto getPairIndex = [](size_t a, size_t b) {
    auto I = llvm::find(ImportantInstructionSuccessions, std::make_pair(a, b));
    if (I == ImportantInstructionSuccessions.end())
      return -1;
    return static_cast<int>(
        std::distance(ImportantInstructionSuccessions.begin(), I));
  };

  // We don't want debug calls, because they'd just add noise.
  for (const auto &BB : F) {
    for (const auto &I : BB.instructionsWithoutDebug()) {
      auto ID = I.getOpcode();

      ++FF.InstructionHistogram[ID];
      int PairIndex = getPairIndex(LastID, ID);
      if (PairIndex >= 0)
        ++FF.InstructionPairHistogram[PairIndex];
      LastID = ID;
      if (isa<CallBase>(I))
        ++FF[NamedFeatureIndex::Calls];
    }
  }

  FF[NamedFeatureIndex::InitialSize] = getSize(F, FAM);
  FF[NamedFeatureIndex::IsLocal] = F.hasLocalLinkage();
  FF[NamedFeatureIndex::IsLinkOnceODR] = F.hasLinkOnceODRLinkage();
  FF[NamedFeatureIndex::IsLinkOnce] = F.hasLinkOnceLinkage();
  FF[NamedFeatureIndex::Blocks] = F.size();
  auto &LI = FAM.getResult<LoopAnalysis>(F);
  FF[NamedFeatureIndex::Loops] = std::distance(LI.begin(), LI.end());
  for (auto &L : LI)
    FF[NamedFeatureIndex::MaxLoopDepth] =
        std::max(FF[NamedFeatureIndex::MaxLoopDepth],
                 static_cast<int32_t>(L->getLoopDepth()));
  FF[NamedFeatureIndex::MaxDomTreeLevel] = getMaxDominatorTreeDepth(F, DomTree);
  return FF;
}

void IRToNativeSizeLearning::FunctionFeatures::fillTensor(int32_t *Ptr) const {
  std::copy(NamedFeatures.begin(), NamedFeatures.end(), Ptr);
  Ptr += NamedFeatures.size();
  std::copy(InstructionHistogram.begin(), InstructionHistogram.end(), Ptr);
  Ptr += InstructionHistogram.size();
  std::copy(InstructionPairHistogram.begin(), InstructionPairHistogram.end(),
            Ptr);
}

bool InlineSizeEstimatorAnalysis::isEvaluatorRequested() {
  return !TFIR2NativeModelPath.empty();
}

InlineSizeEstimatorAnalysis::InlineSizeEstimatorAnalysis() {
  if (!isEvaluatorRequested()) {
    return;
  }
  std::vector<TensorSpec> InputSpecs{TensorSpec::createSpec<int32_t>(
      "serving_default_input_1",
      {1, static_cast<int64_t>(
              IRToNativeSizeLearning::FunctionFeatures::FeatureCount)})};
  std::vector<TensorSpec> OutputSpecs{
      TensorSpec::createSpec<float>("StatefulPartitionedCall", {1})};
  Evaluator = std::make_unique<TFModelEvaluator>(
      TFIR2NativeModelPath.getValue().c_str(), InputSpecs, OutputSpecs);
  if (!Evaluator || !Evaluator->isValid()) {
    Evaluator.reset();
    return;
  }
}

InlineSizeEstimatorAnalysis::Result
InlineSizeEstimatorAnalysis::run(const Function &F,
                                 FunctionAnalysisManager &FAM) {
  if (!Evaluator)
    return std::nullopt;
  auto Features = IRToNativeSizeLearning::getFunctionFeatures(
      const_cast<Function &>(F), FAM);
  int32_t *V = Evaluator->getInput<int32_t>(0);
  Features.fillTensor(V);
  auto ER = Evaluator->evaluate();
  if (!ER)
    return std::nullopt;
  float Ret = *ER->getTensorValue<float>(0);
  if (Ret < 0.0)
    Ret = 0.0;
  return static_cast<size_t>(Ret);
}

InlineSizeEstimatorAnalysis::~InlineSizeEstimatorAnalysis() {}
InlineSizeEstimatorAnalysis::InlineSizeEstimatorAnalysis(
    InlineSizeEstimatorAnalysis &&Other)
    : Evaluator(std::move(Other.Evaluator)) {}

#else
namespace llvm {
class TFModelEvaluator {};
} // namespace llvm
InlineSizeEstimatorAnalysis::InlineSizeEstimatorAnalysis() = default;
InlineSizeEstimatorAnalysis ::InlineSizeEstimatorAnalysis(
    InlineSizeEstimatorAnalysis &&) {}
InlineSizeEstimatorAnalysis::~InlineSizeEstimatorAnalysis() = default;
InlineSizeEstimatorAnalysis::Result
InlineSizeEstimatorAnalysis::run(const Function &F,
                                 FunctionAnalysisManager &FAM) {
  return std::nullopt;
}
bool InlineSizeEstimatorAnalysis::isEvaluatorRequested() { return false; }
#endif

PreservedAnalyses
InlineSizeEstimatorAnalysisPrinterPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  OS << "[InlineSizeEstimatorAnalysis] size estimate for " << F.getName()
     << ": " << AM.getResult<InlineSizeEstimatorAnalysis>(F) << "\n";
  return PreservedAnalyses::all();
}
